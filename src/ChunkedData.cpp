#include "ESPWiFi.h"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

void ESPWiFi::fillChunkedDataResponse(JsonDocument &resp,
                                      const std::string &fullPath,
                                      const std::string &virtualPath,
                                      const std::string &source, int64_t offset,
                                      int tailBytes, int maxBytes) {
  if (tailBytes < 0)
    tailBytes = 0;
  if (maxBytes < 256)
    maxBytes = 256;
  // Keep response bounded: JSON escaping can grow payload.
  if (maxBytes > (8 * 1024))
    maxBytes = 8 * 1024;

  struct stat st;
  if (stat(fullPath.c_str(), &st) != 0) {
    resp["ok"] = false;
    resp["error"] = "file_not_found";
    resp["path"] = virtualPath;
    resp["source"] = source;
    return;
  }

  const int64_t size = (int64_t)st.st_size;
  int64_t start = 0;
  if (offset >= 0) {
    start = offset;
  } else {
    start = size - (int64_t)tailBytes;
  }
  if (start < 0)
    start = 0;
  if (start > size)
    start = size;

  int64_t remaining = size - start;
  int toRead = (remaining > 0) ? (int)remaining : 0;
  if (toRead > maxBytes)
    toRead = maxBytes;

  FILE *f = fopen(fullPath.c_str(), "r");
  if (!f) {
    resp["ok"] = false;
    resp["error"] = "open_failed";
    resp["path"] = virtualPath;
    resp["source"] = source;
    return;
  }

  // fseek uses long; keep this conservative.
  if (start > (int64_t)LONG_MAX || fseek(f, (long)start, SEEK_SET) != 0) {
    fclose(f);
    resp["ok"] = false;
    resp["error"] = "seek_failed";
    resp["path"] = virtualPath;
    resp["source"] = source;
    resp["size"] = size;
    resp["offset"] = start;
    return;
  }

  char *buf = (char *)malloc((size_t)toRead + 1);
  if (!buf) {
    fclose(f);
    resp["ok"] = false;
    resp["error"] = "oom";
    return;
  }

  const size_t got = fread(buf, 1, (size_t)toRead, f);
  buf[got] = '\0';
  fclose(f);

  resp["source"] = source;
  resp["path"] = virtualPath;
  resp["size"] = size;
  resp["offset"] = start;
  resp["next"] = start + (int64_t)got;
  resp["eof"] = (start + (int64_t)got) >= size;
  resp["data"] = std::string(buf, got);
  free(buf);
}
