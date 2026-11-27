// MediaSocket.cpp - Media WebSocket endpoint (camera + audio streaming on
// request)
#include "ESPWiFi.h"

#ifdef CONFIG_HTTPD_WS_SUPPORT

#include <cerrno>
#include <cstdio>
#include <cstdlib>
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
  uint32_t durationMs = 0;
  uint32_t bitrate = 0;
  uint32_t sampleRate = 0;
  uint32_t audioStart = 0;
  uint32_t audioBytes = 0;
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

static uint32_t readBe32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 |
         (uint32_t)p[3];
}

static uint32_t readLe32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}

static int mp3SampleRate(int versionBits, int srIdx) {
  static const int v1[] = {44100, 48000, 32000};
  static const int v2[] = {22050, 24000, 16000};
  static const int v25[] = {11025, 12000, 8000};
  if (srIdx < 0 || srIdx > 2) {
    return 0;
  }
  if (versionBits == 3) return v1[srIdx];
  if (versionBits == 2) return v2[srIdx];
  if (versionBits == 0) return v25[srIdx];
  return 0;
}

static int mp3Bitrate(int versionBits, int bitrateIdx) {
  static const int v1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
  static const int v2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
  if (bitrateIdx <= 0 || bitrateIdx >= 15) return 0;
  const int kbps = (versionBits == 3) ? v1[bitrateIdx] : v2[bitrateIdx];
  return kbps * 1000;
}

// Reads just enough of the file to learn duration, bitrate, and where the
// audio frames start. The file position is restored to the beginning.
static void probeMusicInfo(FILE *f, uint32_t fileSize, MusicStreamState *st) {
  if (!f || !st) return;
  st->audioStart = 0;
  st->audioBytes = fileSize;
  st->durationMs = 0;
  st->bitrate = 0;
  st->sampleRate = 0;

  uint8_t buf[2048];
  const size_t n = fread(buf, 1, sizeof(buf), f);
  if (n >= 12 && memcmp(buf, "RIFF", 4) == 0 && memcmp(buf + 8, "WAVE", 4) == 0) {
    size_t off = 12;
    uint32_t byteRate = 0;
    uint32_t dataBytes = 0;
    while (off + 8 <= n) {
      const uint32_t chunkSize = readLe32(buf + off + 4);
      if (memcmp(buf + off, "fmt ", 4) == 0 && off + 20 <= n) {
        byteRate = readLe32(buf + off + 16);
        st->sampleRate = readLe32(buf + off + 12);
      } else if (memcmp(buf + off, "data", 4) == 0) {
        dataBytes = chunkSize;
        st->audioStart = (uint32_t)(off + 8);
        break;
      }
      if (chunkSize > 1024 * 1024) break;
      off += 8 + (size_t)chunkSize + (chunkSize & 1);
    }
    if (dataBytes == 0 && fileSize > st->audioStart) {
      dataBytes = fileSize - st->audioStart;
    }
    st->audioBytes = dataBytes;
    st->bitrate = byteRate * 8;
    if (byteRate > 0 && dataBytes > 0) {
      st->durationMs = (uint32_t)(((uint64_t)dataBytes * 1000ull) / byteRate);
    }
    clearerr(f);
    fseek(f, 0, SEEK_SET);
    return;
  }

  size_t audioOff = 0;
  if (n >= 10 && memcmp(buf, "ID3", 3) == 0) {
    const uint32_t tagSize = ((uint32_t)(buf[6] & 0x7f) << 21) |
                             ((uint32_t)(buf[7] & 0x7f) << 14) |
                             ((uint32_t)(buf[8] & 0x7f) << 7) |
                             (uint32_t)(buf[9] & 0x7f);
    const uint32_t tagBytes = 10 + tagSize + ((buf[5] & 0x10) ? 10 : 0);
    st->audioStart = tagBytes;
    audioOff = tagBytes;
  }

  size_t available = n;
  if (audioOff >= n) {
    if (fseek(f, (long)st->audioStart, SEEK_SET) != 0) {
      clearerr(f);
      fseek(f, 0, SEEK_SET);
      return;
    }
    available = fread(buf, 1, sizeof(buf), f);
    if (available < 4) {
      clearerr(f);
      fseek(f, 0, SEEK_SET);
      return;
    }
    audioOff = 0;
  }

  size_t sync = audioOff;
  int version = 0;
  int bitrate = 0;
  int sampleRate = 0;
  int samplesPerFrame = 1152;
  bool found = false;
  for (size_t i = audioOff; i + 4 < available; i++) {
    if (buf[i] != 0xFF || (buf[i + 1] & 0xE0) != 0xE0) continue;
    version = (buf[i + 1] >> 3) & 3;
    const int layer = (buf[i + 1] >> 1) & 3;
    const int bitrateIdx = (buf[i + 2] >> 4) & 0xF;
    const int srIdx = (buf[i + 2] >> 2) & 3;
    if (version == 1 || layer != 1 || srIdx == 3) continue;
    bitrate = mp3Bitrate(version, bitrateIdx);
    sampleRate = mp3SampleRate(version, srIdx);
    if (bitrate <= 0 || sampleRate <= 0) continue;
    samplesPerFrame = (version == 3) ? 1152 : 576;
    sync = i;
    found = true;
    break;
  }

  if (found) {
    st->bitrate = (uint32_t)bitrate;
    st->sampleRate = (uint32_t)sampleRate;
    if (audioOff != 0) {
      st->audioStart += (uint32_t)(sync - audioOff);
    } else if (st->audioStart == 0) {
      st->audioStart = (uint32_t)sync;
    }
    uint32_t frames = 0;
    const size_t frameAvail = available - sync;
    const size_t scan = frameAvail < 180 ? frameAvail : 180;
    for (size_t i = 0; i + 8 < scan; i++) {
      const bool xing = memcmp(buf + sync + i, "Xing", 4) == 0;
      const bool info = memcmp(buf + sync + i, "Info", 4) == 0;
      if (!xing && !info) continue;
      const uint32_t flags = readBe32(buf + sync + i + 4);
      if ((flags & 1) && i + 12 <= frameAvail) {
        frames = readBe32(buf + sync + i + 8);
      }
      break;
    }
    if (fileSize > st->audioStart) {
      st->audioBytes = fileSize - st->audioStart;
    }
    if (fileSize > 128) {
      uint8_t tag[3] = {0};
      if (fseek(f, (long)fileSize - 128, SEEK_SET) == 0 &&
          fread(tag, 1, 3, f) == 3 && memcmp(tag, "TAG", 3) == 0 &&
          st->audioBytes > 128) {
        st->audioBytes -= 128;
      }
    }
    if (frames > 0 && sampleRate > 0) {
      st->durationMs = (uint32_t)(((uint64_t)frames * (uint64_t)samplesPerFrame *
                                   1000ull) /
                                  (uint64_t)sampleRate);
    } else if (st->bitrate > 0 && st->audioBytes > 0) {
      st->durationMs = (uint32_t)(((uint64_t)st->audioBytes * 8ull * 1000ull) /
                                  st->bitrate);
    }
  }

  clearerr(f);
  fseek(f, 0, SEEK_SET);
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
    probeMusicInfo(f, totalSize, st);

    espwifi->log(
        INFO,
        "🎵 music_start (fd=%d, fs=%s, chunk=%u, size=%u, duration=%ums, "
        "bitrate=%u, rate=%u, mime=%s, path=%s)",
        clientFd, useSd ? "sd" : "lfs", (unsigned)st->chunkSize,
        (unsigned)st->size, (unsigned)st->durationMs, (unsigned)st->bitrate,
        (unsigned)st->sampleRate, st->mime, rel.c_str());

    resp["type"] = "music_start";
    resp["fs"] = useSd ? "sd" : "lfs";
    resp["path"] = rel;
    resp["mime"] = st->mime;
    resp["chunkSize"] = (uint32_t)st->chunkSize;
    if (st->size > 0) {
      resp["size"] = (uint32_t)st->size;
    }
    if (st->durationMs > 0) {
      resp["durationMs"] = st->durationMs;
    }
    if (st->bitrate > 0) {
      resp["bitrate"] = st->bitrate;
    }
    if (st->sampleRate > 0) {
      resp["sampleRate"] = st->sampleRate;
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

  if (strcmp(cmd, "music_seek") == 0) {
    MusicStreamState *st = musicStateForFd(clientFd);
    if (!st || !st->f || st->durationMs == 0 || st->audioBytes == 0) {
      resp["ok"] = false;
      resp["error"] = "no_active_stream";
      sendMediaAck(ws, clientFd, resp);
      return;
    }
    uint32_t ms = req["ms"] | 0;
    if (ms > st->durationMs) ms = st->durationMs;
    const uint32_t pos =
        st->audioStart +
        (uint32_t)(((uint64_t)ms * (uint64_t)st->audioBytes) / st->durationMs);
    const uint32_t clamped = (st->size > 0 && pos > st->size) ? st->size : pos;
    if (fseek(st->f, (long)clamped, SEEK_SET) != 0) {
      resp["ok"] = false;
      resp["error"] = "seek_failed";
      sendMediaAck(ws, clientFd, resp);
      return;
    }
    st->offset = clamped;
    espwifi->log(DEBUG, "🎵 music_seek (fd=%d, ms=%u, offset=%u)", clientFd,
                 (unsigned)ms, (unsigned)clamped);
    resp["type"] = "music_seek";
    resp["ms"] = ms;
    resp["offset"] = clamped;
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

    uint8_t *chunkBuf = (uint8_t *)malloc((size_t)want);
    if (!chunkBuf) {
      resp["ok"] = false;
      resp["error"] = "out_of_memory";
      sendMediaAck(ws, clientFd, resp);
      return;
    }

    size_t n = fread(chunkBuf, 1, (size_t)want, st->f);
    if (n == 0) {
      free(chunkBuf);
      const bool eof = feof(st->f) != 0;
      espwifi->log(INFO, "🎵 music_eof (fd=%d, offset=%u, chunks=%u, file=%s)",
                   clientFd, (unsigned)st->offset, (unsigned)st->chunksSent,
                   st->fullPath);
      resp["type"] = "music_chunk";
      resp["len"] = 0;
      resp["eof"] = eof;
      resp["offset"] = st->offset;
      sendMediaAck(ws, clientFd, resp);
      // Leave the file open so music_seek can reposition after the last byte.
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
    (void)ws->sendBinary(clientFd, chunkBuf, n);
    free(chunkBuf);
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

void ESPWiFi::stopMediaWebSocket() {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  return;
#else
  if (!mediaSocStarted) {
    return;
  }
  mediaSoc.end();
  mediaSocStarted = false;
  log(INFO, "🎞️ Media WebSocket stopped");
#endif
}
