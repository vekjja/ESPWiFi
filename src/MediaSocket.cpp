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
// Media streaming (per-client file -> binary chunks on request)
// ----------------------------------------------------------------------------

struct MediaStreamState {
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

static constexpr size_t kMaxMediaStreams = 8;
static MediaStreamState s_media[kMaxMediaStreams];

static MediaStreamState *mediaStateForFd(int fd) {
  for (size_t i = 0; i < kMaxMediaStreams; i++) {
    if (s_media[i].fd == fd) {
      return &s_media[i];
    }
  }
  return nullptr;
}

static MediaStreamState *allocMediaState(int fd) {
  MediaStreamState *existing = mediaStateForFd(fd);
  if (existing) {
    return existing;
  }
  for (size_t i = 0; i < kMaxMediaStreams; i++) {
    if (s_media[i].fd == 0) {
      s_media[i].fd = fd;
      return &s_media[i];
    }
  }
  return nullptr;
}

static void closeMediaState(MediaStreamState *st) {
  if (!st) {
    return;
  }
  if (st->f) {
    fclose(st->f);
  }
  *st = MediaStreamState{};
}

static bool hasDotDot(const std::string &p) {
  return p.find("..") != std::string::npos;
}

// File upload/download on /ws/media. One transfer per client, separate from
// the media FILE* so a song or movie can keep playing on another connection.
struct FileXferState {
  int fd = 0;
  FILE *f = nullptr;
  bool upload = false;
  uint32_t offset = 0;
  uint32_t size = 0;
  uint32_t chunkSize = 8192;
  char fullPath[256] = {0};
};

static constexpr size_t kMaxFileXfers = 4;
static constexpr uint32_t kFileUpChunk = 8192;
static constexpr uint32_t kFileDownChunk = 16 * 1024;
static FileXferState s_files[kMaxFileXfers];

static FileXferState *fileStateForFd(int fd) {
  for (size_t i = 0; i < kMaxFileXfers; i++) {
    if (s_files[i].fd == fd) {
      return &s_files[i];
    }
  }
  return nullptr;
}

static FileXferState *allocFileState(int fd) {
  FileXferState *existing = fileStateForFd(fd);
  if (existing) {
    return existing;
  }
  for (size_t i = 0; i < kMaxFileXfers; i++) {
    if (s_files[i].fd == 0) {
      s_files[i].fd = fd;
      return &s_files[i];
    }
  }
  return nullptr;
}

static void closeFileXfer(FileXferState *st, bool removePartial) {
  if (!st) {
    return;
  }
  if (st->f) {
    fclose(st->f);
    st->f = nullptr;
  }
  if (removePartial && st->fullPath[0] != '\0') {
    ::remove(st->fullPath);
  }
  *st = FileXferState{};
}

static bool fileNameOk(const std::string &name) {
  if (name.empty() || name.size() > 120) {
    return false;
  }
  if (name == "." || name == "..") {
    return false;
  }
  if (name.find('/') != std::string::npos ||
      name.find('\\') != std::string::npos ||
      name.find('"') != std::string::npos) {
    return false;
  }
  return !hasDotDot(name);
}

static bool virtToAbs(ESPWiFi *self, const char *fsC, const std::string &rel,
                      std::string &full, std::string &err) {
  if (rel.empty() || hasDotDot(rel)) {
    err = "invalid_path";
    return false;
  }
  std::string path = rel;
  if (path[0] != '/') {
    path = "/" + path;
  }
  const bool useSd = fsC != nullptr && strcmp(fsC, "sd") == 0;
  if (useSd) {
    if (self->sdCard == nullptr) {
      err = "sd_not_mounted";
      return false;
    }
    full = self->sdMountPoint + path;
  } else {
    if (self->lfs == nullptr) {
      err = "lfs_not_mounted";
      return false;
    }
    full = self->lfsMountPoint + path;
  }
  if (full.size() >= sizeof(FileXferState::fullPath)) {
    err = "invalid_path";
    return false;
  }
  return true;
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

static int mp3FrameBytes(const uint8_t *p, size_t n) {
  if (!p || n < 4) return 0;
  if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return 0;
  const int version = (p[1] >> 3) & 3;
  const int layer = (p[1] >> 1) & 3;
  const int bitrateIdx = (p[2] >> 4) & 0xF;
  const int srIdx = (p[2] >> 2) & 3;
  const int padding = (p[2] >> 1) & 1;
  if (version == 1 || layer != 1 || srIdx == 3) return 0;
  const int bitrate = mp3Bitrate(version, bitrateIdx);
  const int sampleRate = mp3SampleRate(version, srIdx);
  if (bitrate <= 0 || sampleRate <= 0) return 0;
  const int coeff = (version == 3) ? 144 : 72;
  return (coeff * bitrate) / sampleRate + padding;
}

static uint32_t snapMp3Frame(FILE *f, uint32_t pos, uint32_t limit) {
  if (!f) return pos;
  if (pos >= limit || fseek(f, (long)pos, SEEK_SET) != 0) {
    fseek(f, (long)pos, SEEK_SET);
    return pos;
  }
  uint8_t buf[2048];
  size_t n = fread(buf, 1, sizeof(buf), f);
  if (limit > pos && n > (size_t)(limit - pos)) {
    n = (size_t)(limit - pos);
  }
  for (size_t i = 0; i + 4 < n; i++) {
    if (mp3FrameBytes(buf + i, n - i) <= 0) continue;
    const uint32_t at = pos + (uint32_t)i;
    if (fseek(f, (long)at, SEEK_SET) == 0) return at;
    break;
  }
  fseek(f, (long)pos, SEEK_SET);
  return pos;
}

// Reads just enough of the file to learn duration, bitrate, and where the
// audio frames start. The file position is restored to the beginning.
static void probeMediaInfo(FILE *f, uint32_t fileSize, MediaStreamState *st) {
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
  espwifi->log(INFO, "📱 LAN client connected to /ws/media (fd=%d)", clientFd);
}

static void mediaOnDisconnect(WebSocket *ws, int clientFd, void *userCtx) {
  (void)ws;
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!espwifi) {
    return;
  }

  espwifi->log(INFO, "📱 LAN client disconnected from /ws/media (fd=%d)",
               clientFd);

#if ESPWiFi_HAS_CAMERA
  // Stop streaming for this client. Camera deinit is handled by stream loop.
  espwifi->clearMediaCameraStreamSubscribed(clientFd);
#endif

  // Stop the media stream for this client (close file handle).
  MediaStreamState *st = mediaStateForFd(clientFd);
  if (st && st->f) {
    espwifi->log(INFO,
                 "📱 Media stream closed on disconnect (fd=%d, offset=%u, "
                 "chunks=%u, file=%s)",
                 clientFd, (unsigned)st->offset, (unsigned)st->chunksSent,
                 st->fullPath);
  }
  closeMediaState(st);

  FileXferState *fx = fileStateForFd(clientFd);
  if (fx) {
    const bool partial = fx->upload && fx->offset < fx->size;
    if (partial) {
      espwifi->log(INFO, "📁 Upload aborted on disconnect (fd=%d, file=%s)",
                   clientFd, fx->fullPath);
    }
    closeFileXfer(fx, partial);
  }
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

static void releaseFileSlot(int clientFd) {
  FileXferState *prev = fileStateForFd(clientFd);
  if (!prev) {
    return;
  }
  const bool partial = prev->upload && prev->offset < prev->size;
  closeFileXfer(prev, partial);
}

static void handleFileUploadChunk(WebSocket *ws, int clientFd, ESPWiFi *espwifi,
                                  const uint8_t *data, size_t len) {
  JsonDocument resp;
  resp["cmd"] = "file_up_chunk";
  FileXferState *st = fileStateForFd(clientFd);
  if (!st || !st->upload || !st->f) {
    resp["ok"] = false;
    resp["type"] = "file_up_ack";
    resp["error"] = "no_active_upload";
    sendMediaAck(ws, clientFd, resp);
    return;
  }
  if (st->offset >= st->size || (uint32_t)len > st->size - st->offset) {
    resp["ok"] = false;
    resp["type"] = "file_up_ack";
    resp["error"] = "bad_size";
    sendMediaAck(ws, clientFd, resp);
    closeFileXfer(st, true);
    return;
  }
  if (!espwifi->writeFileChunk(st->f, data, len)) {
    resp["ok"] = false;
    resp["type"] = "file_up_ack";
    resp["error"] = "write_failed";
    sendMediaAck(ws, clientFd, resp);
    closeFileXfer(st, true);
    return;
  }
  st->offset += (uint32_t)len;
  espwifi->feedWatchDog(1);
  if (st->offset < st->size) {
    resp["ok"] = true;
    resp["type"] = "file_up_ack";
    resp["offset"] = st->offset;
    resp["chunkSize"] = st->chunkSize;
    sendMediaAck(ws, clientFd, resp);
    return;
  }

  FILE *out = st->f;
  st->f = nullptr;
  const std::string path = st->fullPath;
  const uint32_t size = st->offset;
  const bool closed = espwifi->closeFileStream(out, path);
  closeFileXfer(st, !closed);
  if (!closed) {
    resp["ok"] = false;
    resp["type"] = "file_up_done";
    resp["error"] = "write_failed";
    sendMediaAck(ws, clientFd, resp);
    return;
  }
  espwifi->log(INFO, "📁 Upload complete (fd=%d, size=%u, file=%s)", clientFd,
               (unsigned)size, path.c_str());
  resp["ok"] = true;
  resp["type"] = "file_up_done";
  resp["size"] = size;
  sendMediaAck(ws, clientFd, resp);
}

static bool handleFileCommand(WebSocket *ws, int clientFd, ESPWiFi *espwifi,
                              JsonDocument &req, JsonDocument &resp,
                              const char *cmd) {
  if (strcmp(cmd, "file_up_abort") == 0 || strcmp(cmd, "file_down_stop") == 0) {
    releaseFileSlot(clientFd);
    resp["type"] = cmd;
    resp["stopped"] = true;
    sendMediaAck(ws, clientFd, resp);
    return true;
  }

  if (strcmp(cmd, "file_up_start") == 0) {
    resp["type"] = "file_up_start";
    const char *fsC = req["fs"] | "lfs";
    const std::string fs = (fsC != nullptr) ? fsC : "lfs";
    std::string dir = req["path"] | "/";
    const std::string name = req["name"] | "";
    if (!fileNameOk(name)) {
      resp["ok"] = false;
      resp["error"] = "invalid_name";
      sendMediaAck(ws, clientFd, resp);
      return true;
    }
    if (dir.empty()) {
      dir = "/";
    }
    const std::string virt =
        (dir == "/") ? ("/" + name)
                     : (dir.back() == '/' ? dir + name : dir + "/" + name);
    if (espwifi->isProtectedFile(fs, virt)) {
      resp["ok"] = false;
      resp["error"] = "protected";
      sendMediaAck(ws, clientFd, resp);
      return true;
    }
    std::string full;
    std::string err;
    if (!virtToAbs(espwifi, fsC, virt, full, err)) {
      resp["ok"] = false;
      resp["error"] = err;
      sendMediaAck(ws, clientFd, resp);
      return true;
    }
    struct stat existing {};
    if (stat(full.c_str(), &existing) == 0 && S_ISDIR(existing.st_mode)) {
      resp["ok"] = false;
      resp["error"] = "is_directory";
      sendMediaAck(ws, clientFd, resp);
      return true;
    }
    const uint32_t size = req["size"] | 0;
    releaseFileSlot(clientFd);
    FILE *out = espwifi->openFileForWrite(full);
    if (!out) {
      espwifi->log(WARNING, "📁 Upload open failed (fd=%d, errno=%d, file=%s)",
                   clientFd, errno, full.c_str());
      resp["ok"] = false;
      resp["error"] = "file_open_failed";
      resp["errno"] = errno;
      sendMediaAck(ws, clientFd, resp);
      return true;
    }
    if (size == 0) {
      if (!espwifi->closeFileStream(out, full)) {
        resp["ok"] = false;
        resp["error"] = "write_failed";
        sendMediaAck(ws, clientFd, resp);
        return true;
      }
      resp["type"] = "file_up_done";
      resp["size"] = 0;
      resp["name"] = name;
      sendMediaAck(ws, clientFd, resp);
      return true;
    }
    FileXferState *st = allocFileState(clientFd);
    if (!st) {
      fclose(out);
      ::remove(full.c_str());
      resp["ok"] = false;
      resp["error"] = "too_many_transfers";
      sendMediaAck(ws, clientFd, resp);
      return true;
    }
    st->f = out;
    st->upload = true;
    st->offset = 0;
    st->size = size;
    st->chunkSize = kFileUpChunk;
    (void)snprintf(st->fullPath, sizeof(st->fullPath), "%s", full.c_str());
    espwifi->log(INFO, "📁 Upload start (fd=%d, fs=%s, size=%u, file=%s)",
                 clientFd, fs.c_str(), (unsigned)size, virt.c_str());
    resp["chunkSize"] = st->chunkSize;
    resp["size"] = size;
    resp["name"] = name;
    sendMediaAck(ws, clientFd, resp);
    return true;
  }

  if (strcmp(cmd, "file_down_start") == 0) {
    resp["type"] = "file_down_start";
    const char *fsC = req["fs"] | "lfs";
    const std::string virt = req["path"] | "";
    std::string full;
    std::string err;
    if (!virtToAbs(espwifi, fsC, virt, full, err)) {
      resp["ok"] = false;
      resp["error"] = err;
      sendMediaAck(ws, clientFd, resp);
      return true;
    }
    struct stat stbuf {};
    if (stat(full.c_str(), &stbuf) != 0) {
      resp["ok"] = false;
      resp["error"] = "file_open_failed";
      resp["errno"] = errno;
      sendMediaAck(ws, clientFd, resp);
      return true;
    }
    if (S_ISDIR(stbuf.st_mode)) {
      resp["ok"] = false;
      resp["error"] = "is_directory";
      sendMediaAck(ws, clientFd, resp);
      return true;
    }
    releaseFileSlot(clientFd);
    FILE *in = fopen(full.c_str(), "rb");
    if (!in) {
      resp["ok"] = false;
      resp["error"] = "file_open_failed";
      resp["errno"] = errno;
      sendMediaAck(ws, clientFd, resp);
      return true;
    }
    FileXferState *st = allocFileState(clientFd);
    if (!st) {
      fclose(in);
      resp["ok"] = false;
      resp["error"] = "too_many_transfers";
      sendMediaAck(ws, clientFd, resp);
      return true;
    }
    st->f = in;
    st->upload = false;
    st->offset = 0;
    st->size = stbuf.st_size > 0 ? (uint32_t)stbuf.st_size : 0;
    st->chunkSize = kFileDownChunk;
    (void)snprintf(st->fullPath, sizeof(st->fullPath), "%s", full.c_str());
    std::string name = virt;
    const size_t slash = name.rfind('/');
    if (slash != std::string::npos) {
      name = name.substr(slash + 1);
    }
    espwifi->log(INFO, "📁 Download start (fd=%d, size=%u, file=%s)", clientFd,
                 (unsigned)st->size, virt.c_str());
    resp["size"] = st->size;
    resp["chunkSize"] = st->chunkSize;
    resp["name"] = name;
    sendMediaAck(ws, clientFd, resp);
    return true;
  }

  if (strcmp(cmd, "file_down_next") == 0) {
    FileXferState *st = fileStateForFd(clientFd);
    if (!st || st->upload || !st->f) {
      resp["ok"] = false;
      resp["type"] = "file_chunk";
      resp["error"] = "no_active_download";
      sendMediaAck(ws, clientFd, resp);
      return true;
    }
    uint32_t want = st->chunkSize;
    if (want == 0 || want > kFileDownChunk) {
      want = kFileDownChunk;
    }
    uint8_t *chunkBuf = (uint8_t *)malloc((size_t)want);
    if (!chunkBuf) {
      resp["ok"] = false;
      resp["type"] = "file_chunk";
      resp["error"] = "out_of_memory";
      sendMediaAck(ws, clientFd, resp);
      return true;
    }
    const size_t n = fread(chunkBuf, 1, (size_t)want, st->f);
    if (n == 0) {
      free(chunkBuf);
      resp["type"] = "file_chunk";
      resp["len"] = 0;
      resp["eof"] = true;
      resp["offset"] = st->offset;
      sendMediaAck(ws, clientFd, resp);
      closeFileXfer(st, false);
      return true;
    }
    st->offset += (uint32_t)n;
    const bool eof =
        feof(st->f) || (st->size > 0 && st->offset >= st->size);
    resp["type"] = "file_chunk";
    resp["len"] = (uint32_t)n;
    resp["eof"] = eof;
    resp["offset"] = st->offset;
    sendMediaAck(ws, clientFd, resp);
    (void)ws->sendBinary(clientFd, chunkBuf, n);
    free(chunkBuf);
    espwifi->feedWatchDog(1);
    if (eof) {
      closeFileXfer(st, false);
    }
    return true;
  }

  return false;
}

static void mediaOnMessage(WebSocket *ws, int clientFd, httpd_ws_type_t type,
                           const uint8_t *data, size_t len, void *userCtx) {
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!ws || !espwifi || clientFd <= 0 || data == nullptr || len == 0) {
    return;
  }

  // Uploads arrive as binary frames. Downloads and media are device->client.
  if (type == HTTPD_WS_TYPE_BINARY) {
    handleFileUploadChunk(ws, clientFd, espwifi, data, len);
    return;
  }
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
  // Media streaming commands
  // --------------------------
  if (strcmp(cmd, "media_start") == 0) {
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
      espwifi->log(WARNING, "📱 media_start missing path (fd=%d)", clientFd);
      resp["ok"] = false;
      resp["error"] = "missing_path";
      sendMediaAck(ws, clientFd, resp);
      return;
    }
    if (hasDotDot(rel)) {
      espwifi->log(WARNING, "📱 media_start invalid path (fd=%d, path=%s)",
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

    MediaStreamState *st = allocMediaState(clientFd);
    if (!st) {
      resp["ok"] = false;
      resp["error"] = "too_many_streams";
      sendMediaAck(ws, clientFd, resp);
      return;
    }

    // Close any previous stream for this fd
    closeMediaState(st);
    st = allocMediaState(clientFd);
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
                   "📱 media_start open failed (fd=%d, errno=%d, file=%s)",
                   clientFd, errno, full.c_str());
      resp["ok"] = false;
      resp["error"] = "file_open_failed";
      resp["errno"] = errno;
      sendMediaAck(ws, clientFd, resp);
      closeMediaState(st);
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
    probeMediaInfo(f, totalSize, st);

    espwifi->log(
        INFO,
        "📱 media_start (fd=%d, fs=%s, chunk=%u, size=%u, duration=%ums, "
        "bitrate=%u, rate=%u, mime=%s, path=%s)",
        clientFd, useSd ? "sd" : "lfs", (unsigned)st->chunkSize,
        (unsigned)st->size, (unsigned)st->durationMs, (unsigned)st->bitrate,
        (unsigned)st->sampleRate, st->mime, rel.c_str());

    resp["type"] = "media_start";
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

  if (strcmp(cmd, "media_stop") == 0) {
    MediaStreamState *st = mediaStateForFd(clientFd);
    if (st && st->f) {
      espwifi->log(INFO, "📱 media_stop (fd=%d, offset=%u, chunks=%u, file=%s)",
                   clientFd, (unsigned)st->offset, (unsigned)st->chunksSent,
                   st->fullPath);
    } else {
      espwifi->log(DEBUG, "📱 media_stop (fd=%d, no active stream)", clientFd);
    }
    closeMediaState(st);
    resp["type"] = "media_stop";
    resp["stopped"] = true;
    sendMediaAck(ws, clientFd, resp);
    return;
  }

  if (strcmp(cmd, "media_seek") == 0) {
    resp["type"] = "media_seek";
    MediaStreamState *st = mediaStateForFd(clientFd);
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
    uint32_t at = clamped;
    const uint32_t limit = st->size > 0 ? st->size : clamped;
    if (strstr(st->mime, "mpeg") != nullptr) {
      at = snapMp3Frame(st->f, clamped, limit);
    } else if (fseek(st->f, (long)clamped, SEEK_SET) != 0) {
      resp["ok"] = false;
      resp["error"] = "seek_failed";
      sendMediaAck(ws, clientFd, resp);
      return;
    }
    st->offset = at;
    espwifi->log(DEBUG, "📱 media_seek (fd=%d, ms=%u, offset=%u)", clientFd,
                 (unsigned)ms, (unsigned)at);
    resp["ms"] = ms;
    resp["offset"] = at;
    sendMediaAck(ws, clientFd, resp);
    return;
  }

  if (strcmp(cmd, "media_read") == 0) {
    MediaStreamState *st = mediaStateForFd(clientFd);
    if (!st || !st->f) {
      resp["ok"] = false;
      resp["error"] = "no_active_stream";
      sendMediaAck(ws, clientFd, resp);
      return;
    }

    uint32_t offset = req["offset"] | 0;
    uint32_t want = req["maxBytes"] | st->chunkSize;
    if (want < 1024) {
      want = 1024;
    }
    if (want > 48 * 1024) {
      want = 48 * 1024;
    }
    if (st->size > 0 && offset >= st->size) {
      resp["type"] = "media_chunk";
      resp["len"] = 0;
      resp["eof"] = true;
      resp["offset"] = offset;
      sendMediaAck(ws, clientFd, resp);
      return;
    }
    if (st->size > 0 && offset + want > st->size) {
      want = st->size - offset;
    }

    // fseek takes a signed long. Movies larger than that cannot be ranged.
    if (offset > 0x7fffffffu) {
      resp["ok"] = false;
      resp["error"] = "seek_failed";
      sendMediaAck(ws, clientFd, resp);
      return;
    }

    clearerr(st->f);
    if (fseek(st->f, (long)offset, SEEK_SET) != 0) {
      resp["ok"] = false;
      resp["error"] = "seek_failed";
      sendMediaAck(ws, clientFd, resp);
      return;
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
      resp["type"] = "media_chunk";
      resp["len"] = 0;
      resp["eof"] = true;
      resp["offset"] = offset;
      sendMediaAck(ws, clientFd, resp);
      return;
    }

    st->offset = offset + (uint32_t)n;
    st->chunksSent += 1;
    resp["type"] = "media_chunk";
    resp["len"] = (uint32_t)n;
    resp["eof"] = false;
    resp["offset"] = st->offset;
    sendMediaAck(ws, clientFd, resp);
    (void)ws->sendBinary(clientFd, chunkBuf, n);
    free(chunkBuf);
    return;
  }

  if (strcmp(cmd, "media_next") == 0) {
    MediaStreamState *st = mediaStateForFd(clientFd);
    if (!st || !st->f) {
      espwifi->log(VERBOSE, "📱 media_next no active stream (fd=%d)", clientFd);
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
      espwifi->log(INFO, "📱 media_eof (fd=%d, offset=%u, chunks=%u, file=%s)",
                   clientFd, (unsigned)st->offset, (unsigned)st->chunksSent,
                   st->fullPath);
      resp["type"] = "media_chunk";
      resp["len"] = 0;
      resp["eof"] = eof;
      resp["offset"] = st->offset;
      sendMediaAck(ws, clientFd, resp);
      // Leave the file open so media_seek can reposition after the last byte.
      return;
    }

    st->offset += (uint32_t)n;
    st->chunksSent += 1;
    // Avoid spamming per-chunk logs. Emit a periodic progress log.
    if ((st->chunksSent % 64) == 1) {
      espwifi->log(DEBUG, "📱 media_progress (fd=%d, offset=%u, chunks=%u)",
                   clientFd, (unsigned)st->offset, (unsigned)st->chunksSent);
    }
    resp["type"] = "media_chunk";
    resp["len"] = (uint32_t)n;
    resp["eof"] = false;
    resp["offset"] = st->offset;
    sendMediaAck(ws, clientFd, resp);
    (void)ws->sendBinary(clientFd, chunkBuf, n);
    free(chunkBuf);
    return;
  }

  if (handleFileCommand(ws, clientFd, espwifi, req, resp, cmd)) {
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
                                   /*maxMessageLen*/ 8192,
                                   /*maxBroadcastLen*/ 200 * 1024,
                                   /*requireAuth*/ false,
                                   /*authCheck*/ wsAuthCheck);
  if (!mediaSocStarted) {
    log(ERROR, "📱 Media WebSocket failed to start");
    return;
  }

  log(INFO, "📱 Media WebSocket started: /ws/media");
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
  log(INFO, "📱 Media WebSocket stopped");
#endif
}
