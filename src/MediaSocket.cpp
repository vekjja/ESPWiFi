// MediaSocket.cpp - Media WebSocket endpoint (camera + audio streaming on
// request)
#include "ESPWiFi.h"

#ifdef CONFIG_HTTPD_WS_SUPPORT

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

// Auth check helper for WebSocket
static bool wsAuthCheck(httpd_req_t *req, void *userCtx) {
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!espwifi || !espwifi->authEnabled() ||
      espwifi->isExcludedPath(req->uri)) {
    return true;
  }

  bool ok = espwifi->authorized(req);
  if (!ok) {
    // Browser WebSocket APIs can't set Authorization headers. Allow token
    // via query param: ws://host/path?token=...
    const std::string tok = espwifi->getQueryParam(req, "token");
    const char *expectedC = espwifi->config["auth"]["token"].as<const char *>();
    const std::string expected = (expectedC != nullptr) ? expectedC : "";
    ok = (!tok.empty() && !expected.empty() && tok == expected);
  }
  return ok;
}

// ----------------------------------------------------------------------------
// Music streaming (per-client file -> binary chunks on request)
// ----------------------------------------------------------------------------

struct MusicStreamState {
  int fd = 0;
  FILE *f = nullptr;
  uint32_t offset = 0;
  uint32_t size = 0;
  uint32_t chunkSize = 16 * 1024;
  uint32_t chunksSent = 0;
  char fullPath[256] = {0};
  char mime[48] = {0};
};

static constexpr size_t kMaxMusicStreams = 8;
static MusicStreamState s_music[kMaxMusicStreams];

static MusicStreamState *musicStateForFd(int fd) {
  for (size_t i = 0; i < kMaxMusicStreams; i++) {
    if (s_music[i].fd == fd) {
      return &s_music[i];
    }
  }
  return nullptr;
}

static MusicStreamState *allocMusicState(int fd) {
  MusicStreamState *existing = musicStateForFd(fd);
  if (existing) {
    return existing;
  }
  for (size_t i = 0; i < kMaxMusicStreams; i++) {
    if (s_music[i].fd == 0) {
      s_music[i].fd = fd;
      return &s_music[i];
    }
  }
  return nullptr;
}

static void closeMusicState(MusicStreamState *st) {
  if (!st) {
    return;
  }
  if (st->f) {
    fclose(st->f);
  }
  *st = MusicStreamState{};
}

static bool hasDotDot(const std::string &p) {
  return p.find("..") != std::string::npos;
}

static void mediaOnConnect(WebSocket *ws, int clientFd, void *userCtx) {
  (void)ws;
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!espwifi) {
    return;
  }
  espwifi->log(INFO, "🎞️ LAN client connected to /ws/media (fd=%d)", clientFd);
}

static void mediaOnDisconnect(WebSocket *ws, int clientFd, void *userCtx) {
  (void)ws;
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!espwifi) {
    return;
  }

  espwifi->log(INFO, "🎞️ LAN client disconnected from /ws/media (fd=%d)",
               clientFd);

#if ESPWiFi_HAS_CAMERA
  // Stop streaming for this client. Camera deinit is handled by stream loop.
  espwifi->clearMediaCameraStreamSubscribed(clientFd);
#endif

  // Stop music stream for this client (close file handle).
  MusicStreamState *st = musicStateForFd(clientFd);
  if (st && st->f) {
    espwifi->log(INFO,
                 "🎵 Music stream closed on disconnect (fd=%d, offset=%u, "
                 "chunks=%u, file=%s)",
                 clientFd, (unsigned)st->offset, (unsigned)st->chunksSent,
                 st->fullPath);
  }
  closeMusicState(st);
}

static void sendMediaAck(WebSocket *ws, int clientFd,
                         const JsonDocument &resp) {
  if (!ws || clientFd <= 0) {
    return;
  }
  std::string out;
  serializeJson(resp, out);
  (void)ws->sendText(clientFd, out.c_str(), out.size());
}

static void mediaOnMessage(WebSocket *ws, int clientFd, httpd_ws_type_t type,
                           const uint8_t *data, size_t len, void *userCtx) {
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!ws || !espwifi || clientFd <= 0 || data == nullptr || len == 0) {
    return;
  }

  // Media control is JSON text; media payloads are sent device->client as
  // binary.
  if (type != HTTPD_WS_TYPE_TEXT) {
    return;
  }

  JsonDocument req;
  DeserializationError err = deserializeJson(req, (const char *)data, len);

  JsonDocument resp;
  resp["ok"] = true;
  resp["type"] = "media_ack";

  if (err) {
    resp["ok"] = false;
    resp["error"] = "bad_json";
    resp["detail"] = err.c_str();
    sendMediaAck(ws, clientFd, resp);
    return;
  }

  const char *cmd = req["cmd"] | "";
  resp["cmd"] = cmd;

  // --------------------------
  // Music streaming commands
  // --------------------------
  if (strcmp(cmd, "music_start") == 0) {
    const char *fsC = req["fs"] | "sd";   // "sd" or "lfs"
    const char *pathC = req["path"] | ""; // "/music/foo.mp3"
    const char *mimeC = req["mime"] | "audio/mpeg";
    uint32_t chunkSize = req["chunkSize"] | (16 * 1024);
    if (chunkSize < 4096) {
      chunkSize = 4096;
    }
    if (chunkSize > (128 * 1024)) {
      chunkSize = 128 * 1024;
    }

    std::string rel = (pathC != nullptr) ? std::string(pathC) : "";
    if (rel.empty()) {
      espwifi->log(WARNING, "🎵 music_start missing path (fd=%d)", clientFd);
      resp["ok"] = false;
      resp["error"] = "missing_path";
      sendMediaAck(ws, clientFd, resp);
      return;
    }
    if (hasDotDot(rel)) {
      espwifi->log(WARNING, "🎵 music_start invalid path (fd=%d, path=%s)",
                   clientFd, rel.c_str());
      resp["ok"] = false;
      resp["error"] = "invalid_path";
      sendMediaAck(ws, clientFd, resp);
      return;
    }
    if (rel[0] != '/') {
      rel = "/" + rel;
    }

    const bool useSd = (fsC != nullptr && strcmp(fsC, "sd") == 0);
    const std::string base =
        useSd ? espwifi->sdMountPoint : espwifi->lfsMountPoint;
    const std::string full = base + rel;

    MusicStreamState *st = allocMusicState(clientFd);
    if (!st) {
      resp["ok"] = false;
      resp["error"] = "too_many_streams";
      sendMediaAck(ws, clientFd, resp);
      return;
    }

    // Close any previous stream for this fd
    closeMusicState(st);
    st = allocMusicState(clientFd);
    if (!st) {
      resp["ok"] = false;
      resp["error"] = "stream_alloc_failed";
      sendMediaAck(ws, clientFd, resp);
      return;
    }

    // Best-effort file size
    struct stat stbuf;
    uint32_t totalSize = 0;
    if (stat(full.c_str(), &stbuf) == 0 && stbuf.st_size > 0) {
      totalSize = (uint32_t)stbuf.st_size;
    }

    FILE *f = fopen(full.c_str(), "rb");
    if (!f) {
      espwifi->log(WARNING,
                   "🎵 music_start open failed (fd=%d, errno=%d, file=%s)",
                   clientFd, errno, full.c_str());
      resp["ok"] = false;
      resp["error"] = "file_open_failed";
      resp["errno"] = errno;
      sendMediaAck(ws, clientFd, resp);
      closeMusicState(st);
      return;
    }

    st->f = f;
    st->offset = 0;
    st->size = totalSize;
    st->chunkSize = chunkSize;
    st->chunksSent = 0;
    (void)snprintf(st->fullPath, sizeof(st->fullPath), "%s", full.c_str());
    (void)snprintf(st->mime, sizeof(st->mime), "%s",
                   (mimeC != nullptr) ? mimeC : "audio/mpeg");

    espwifi->log(
        INFO,
        "🎵 music_start (fd=%d, fs=%s, chunk=%u, size=%u, mime=%s, path=%s)",
        clientFd, useSd ? "sd" : "lfs", (unsigned)st->chunkSize,
        (unsigned)st->size, st->mime, rel.c_str());

    resp["type"] = "music_start";
    resp["fs"] = useSd ? "sd" : "lfs";
    resp["path"] = rel;
    resp["mime"] = st->mime;
    resp["chunkSize"] = (uint32_t)st->chunkSize;
    if (st->size > 0) {
      resp["size"] = (uint32_t)st->size;
    }
    sendMediaAck(ws, clientFd, resp);
    return;
  }

  if (strcmp(cmd, "music_stop") == 0) {
    MusicStreamState *st = musicStateForFd(clientFd);
    if (st && st->f) {
      espwifi->log(INFO, "🎵 music_stop (fd=%d, offset=%u, chunks=%u, file=%s)",
                   clientFd, (unsigned)st->offset, (unsigned)st->chunksSent,
                   st->fullPath);
    } else {
      espwifi->log(DEBUG, "🎵 music_stop (fd=%d, no active stream)", clientFd);
    }
    closeMusicState(st);
    resp["type"] = "music_stop";
    resp["stopped"] = true;
    sendMediaAck(ws, clientFd, resp);
    return;
  }

  if (strcmp(cmd, "music_next") == 0) {
    MusicStreamState *st = musicStateForFd(clientFd);
    if (!st || !st->f) {
      espwifi->log(VERBOSE, "🎵 music_next no active stream (fd=%d)", clientFd);
      resp["ok"] = false;
      resp["error"] = "no_active_stream";
      sendMediaAck(ws, clientFd, resp);
      return;
    }

    uint32_t want = req["maxBytes"] | st->chunkSize;
    if (want < 1024) {
      want = 1024;
    }
    if (want > 64 * 1024) {
      want = 64 * 1024;
    }

    static uint8_t s_chunkBuf[64 * 1024];
    size_t n = fread(s_chunkBuf, 1, (size_t)want, st->f);
    if (n == 0) {
      const bool eof = feof(st->f) != 0;
      espwifi->log(INFO, "🎵 music_eof (fd=%d, offset=%u, chunks=%u, file=%s)",
                   clientFd, (unsigned)st->offset, (unsigned)st->chunksSent,
                   st->fullPath);
      resp["type"] = "music_chunk";
      resp["len"] = 0;
      resp["eof"] = eof;
      resp["offset"] = st->offset;
      sendMediaAck(ws, clientFd, resp);
      closeMusicState(st);
      return;
    }

    st->offset += (uint32_t)n;
    st->chunksSent += 1;
    // Avoid spamming per-chunk logs. Emit a periodic progress log.
    if ((st->chunksSent % 64) == 1) {
      espwifi->log(DEBUG, "🎵 music_progress (fd=%d, offset=%u, chunks=%u)",
                   clientFd, (unsigned)st->offset, (unsigned)st->chunksSent);
    }
    resp["type"] = "music_chunk";
    resp["len"] = (uint32_t)n;
    resp["eof"] = false;
    resp["offset"] = st->offset;
    sendMediaAck(ws, clientFd, resp);
    (void)ws->sendBinary(clientFd, s_chunkBuf, n);
    return;
  }

#if !ESPWiFi_HAS_CAMERA
  (void)espwifi;
  if (strcmp(cmd, "camera_start") == 0 || strcmp(cmd, "camera_stop") == 0 ||
      strcmp(cmd, "camera_frame") == 0) {
    resp["ok"] = false;
    resp["error"] = "camera_not_supported";
    sendMediaAck(ws, clientFd, resp);
    return;
  }
#else
  if (strcmp(cmd, "camera_start") == 0) {
    // Explicit request: start streaming to this client (pull-based "start").
    if (!espwifi->initCamera()) {
      resp["ok"] = false;
      resp["error"] = "camera_init_failed";
      sendMediaAck(ws, clientFd, resp);
      return;
    }
    espwifi->setMediaCameraStreamSubscribed(clientFd, true);
    resp["streaming"] = true;
    sendMediaAck(ws, clientFd, resp);
    return;
  }

  if (strcmp(cmd, "camera_stop") == 0) {
    espwifi->setMediaCameraStreamSubscribed(clientFd, false);
    resp["streaming"] = false;
    sendMediaAck(ws, clientFd, resp);
    return;
  }

  if (strcmp(cmd, "camera_frame") == 0) {
    // One-shot frame: capture and send binary to requester only.
    if (!espwifi->initCamera()) {
      resp["ok"] = false;
      resp["error"] = "camera_init_failed";
      sendMediaAck(ws, clientFd, resp);
      return;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb || fb->format != PIXFORMAT_JPEG || !fb->buf || fb->len == 0) {
      if (fb) {
        esp_camera_fb_return(fb);
      }
      resp["ok"] = false;
      resp["error"] = "camera_capture_failed";
      sendMediaAck(ws, clientFd, resp);
      return;
    }

    // Send metadata (text) + payload (binary).
    resp["type"] = "camera_frame";
    resp["len"] = (uint32_t)fb->len;
    sendMediaAck(ws, clientFd, resp);
    (void)ws->sendBinary(clientFd, fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return;
  }
#endif

  resp["ok"] = false;
  resp["error"] = "unknown_cmd";
  sendMediaAck(ws, clientFd, resp);
}

#endif

void ESPWiFi::startMediaWebSocket() {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  return;
#else
  if (mediaSocStarted) {
    return;
  }

  // Note: Keep unauthenticated by default to preserve current LAN UX.
  // When auth is enabled, callers can still connect using ?token=...
  mediaSocStarted = mediaSoc.begin("/ws/media", webServer, this,
                                   /*onMessage*/ mediaOnMessage,
                                   /*onConnect*/ mediaOnConnect,
                                   /*onDisconnect*/ mediaOnDisconnect,
                                   /*maxMessageLen*/ 2048,
                                   /*maxBroadcastLen*/ 200 * 1024,
                                   /*requireAuth*/ false,
                                   /*authCheck*/ wsAuthCheck);
  if (!mediaSocStarted) {
    log(ERROR, "🎞️ Media WebSocket failed to start");
    return;
  }

  log(INFO, "🎞️ Media WebSocket started: /ws/media");
#endif
}
