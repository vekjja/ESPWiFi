#ifndef ESPWiFi_SRV_FILES
#define ESPWiFi_SRV_FILES

#include "ESPWiFi.h"

#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

namespace {
std::string normalizePath(std::string p) {
  if (p.empty()) {
    return std::string("/");
  }
  if (p.front() != '/') {
    p.insert(p.begin(), '/');
  }
  while (p.size() > 1 && p.back() == '/') {
    p.pop_back();
  }
  return p;
}

bool isSafePath(const std::string &p) {
  if (p.empty() || p.size() > 255) {
    return false;
  }
  if (p.front() != '/') {
    return false;
  }
  if (p.find('\0') != std::string::npos) {
    return false;
  }
  if (p.find("..") != std::string::npos) {
    return false;
  }
  return true;
}

int jsonEscapedAppend(char *dst, size_t dstSize, const char *src) {
  if (dst == nullptr || dstSize == 0) {
    return -1;
  }
  size_t out = 0;
  auto putc_safe = [&](char c) -> bool {
    if (out + 1 >= dstSize) {
      return false;
    }
    dst[out++] = c;
    return true;
  };
  auto puts_safe = [&](const char *s) -> bool {
    while (s && *s) {
      if (!putc_safe(*s++)) {
        return false;
      }
    }
    return true;
  };

  if (src == nullptr) {
    dst[0] = '\0';
    return 0;
  }

  for (const unsigned char *p = (const unsigned char *)src; *p; ++p) {
    unsigned char c = *p;
    if (c == '\"') {
      if (!puts_safe("\\\"")) {
        return -1;
      }
    } else if (c == '\\') {
      if (!puts_safe("\\\\")) {
        return -1;
      }
    } else if (c == '\b') {
      if (!puts_safe("\\b")) {
        return -1;
      }
    } else if (c == '\f') {
      if (!puts_safe("\\f")) {
        return -1;
      }
    } else if (c == '\n') {
      if (!puts_safe("\\n")) {
        return -1;
      }
    } else if (c == '\r') {
      if (!puts_safe("\\r")) {
        return -1;
      }
    } else if (c == '\t') {
      if (!puts_safe("\\t")) {
        return -1;
      }
    } else if (c < 0x20) {
      char tmp[7];
      snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned)c);
      if (!puts_safe(tmp)) {
        return -1;
      }
    } else {
      if (!putc_safe((char)c)) {
        return -1;
      }
    }
  }
  dst[out] = '\0';
  return (int)out;
}

esp_err_t sendChunk(ESPWiFi *espwifi, httpd_req_t *req, const char *data,
                    size_t len, size_t &ioBytes) {
  if (len == 0) {
    return ESP_OK;
  }
  esp_err_t ret = httpd_resp_send_chunk(req, data, len);
  if (ret == ESP_OK) {
    ioBytes += len;
    espwifi->feedWatchDog();
  }
  return ret;
}

bool pickMountPoint(ESPWiFi *espwifi, const std::string &fsParam,
                    std::string &outMountPoint) {
  if (fsParam == "lfs") {
    if (espwifi->lfs == nullptr) {
      return false;
    }
    outMountPoint = espwifi->lfsMountPoint;
    return true;
  }
  if (fsParam == "sd") {
    if (espwifi->sdCard == nullptr) {
      return false;
    }
    outMountPoint = "/sd";
    return true;
  }
  return false;
}
} // namespace

void ESPWiFi::srvFiles() {
  // API endpoint for file browser JSON data - GET /api/files
  registerRoute(
      "/api/files", HTTP_GET,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        // Local helpers are captured via static function pointers above.
        // (Keep this handler small; it runs in the httpd task.)
        std::string fsParam = espwifi->getQueryParam(req, "fs");
        if (fsParam.empty()) {
          fsParam = "lfs";
        }

        std::string path = espwifi->getQueryParam(req, "path");
        path = normalizePath(path);
        if (!isSafePath(path)) {
          (void)espwifi->sendJsonResponse(req, 400, "{\"error\":\"Bad path\"}",
                                          &clientInfo);
          return ESP_OK;
        }

        std::string mountPoint;
        if (!pickMountPoint(espwifi, fsParam, mountPoint)) {
          (void)espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"File system not available\"}",
              &clientInfo);
          return ESP_OK;
        }

        std::string fullPath = mountPoint + path;
        DIR *dir = opendir(fullPath.c_str());
        if (!dir) {
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"Directory not found\"}", &clientInfo);
          return ESP_OK;
        }

        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "200 OK");

        size_t bytesSent = 0;
        esp_err_t ret = ESP_OK;

        // Stream JSON: {"files":[ ... ]}
        ret = sendChunk(espwifi, req, "{\"files\":[", strlen("{\"files\":["),
                        bytesSent);
        if (ret != ESP_OK) {
          closedir(dir);
          espwifi->logAccess(500, clientInfo, bytesSent);
          return ESP_FAIL;
        }

        struct dirent *entry;
        int fileCount = 0;
        const int maxFiles = 1000;
        unsigned long startTime = millis();
        const unsigned long timeout = 3000;
        bool first = true;

        char nameEsc[192];
        char pathEsc[384];
        char objBuf[768];

        while ((entry = readdir(dir)) != nullptr && fileCount < maxFiles &&
               (millis() - startTime) < timeout) {
          if (strcmp(entry->d_name, ".") == 0 ||
              strcmp(entry->d_name, "..") == 0) {
            continue;
          }

          std::string entryPath = path;
          if (entryPath.back() != '/') {
            entryPath += "/";
          }
          entryPath += entry->d_name;

          std::string fullEntryPath = mountPoint + entryPath;
          struct stat st;
          if (stat(fullEntryPath.c_str(), &st) != 0) {
            continue;
          }

          const bool isDir = S_ISDIR(st.st_mode);
          const int64_t size = isDir ? 0 : (int64_t)st.st_size;
          const int64_t modified = (int64_t)st.st_mtime;

          if (jsonEscapedAppend(nameEsc, sizeof(nameEsc), entry->d_name) < 0 ||
              jsonEscapedAppend(pathEsc, sizeof(pathEsc), entryPath.c_str()) <
                  0) {
            // Too long for our fixed buffers; skip this entry.
            continue;
          }

          int n = snprintf(objBuf, sizeof(objBuf),
                           "%s{\"name\":\"%s\",\"path\":\"%s\",\"isDirectory\":"
                           "%s,\"size\":%lld,"
                           "\"modified\":%lld}",
                           first ? "" : ",", nameEsc, pathEsc,
                           isDir ? "true" : "false", (long long)size,
                           (long long)modified);
          if (n <= 0 || (size_t)n >= sizeof(objBuf)) {
            continue;
          }

          ret = sendChunk(espwifi, req, objBuf, (size_t)n, bytesSent);
          if (ret != ESP_OK) {
            break;
          }

          first = false;
          fileCount++;

          // Yield periodically (in addition to sendChunk yields) so directory
          // scans don't hog the httpd task.
          if ((fileCount % 16) == 0) {
            espwifi->feedWatchDog();
          }
        }

        closedir(dir);

        if (ret == ESP_OK) {
          ret = sendChunk(espwifi, req, "]}", 2, bytesSent);
        }
        // Finalize chunked response.
        (void)httpd_resp_send_chunk(req, nullptr, 0);
        espwifi->feedWatchDog();

        espwifi->logAccess((ret == ESP_OK) ? 200 : 500, clientInfo, bytesSent);
        return (ret == ESP_OK) ? ESP_OK : ESP_FAIL;
      });

  // API endpoint for storage information - GET /api/storage
  registerRoute(
      "/api/storage", HTTP_GET,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        std::string fsParam = espwifi->getQueryParam(req, "fs");
        if (fsParam.empty()) {
          fsParam = "lfs";
        }

        size_t totalBytes, usedBytes, freeBytes;
        espwifi->getStorageInfo(fsParam, totalBytes, usedBytes, freeBytes);
        char buf[192];
        int n = snprintf(buf, sizeof(buf),
                         "{\"total\":%llu,\"used\":%llu,\"free\":%llu,"
                         "\"filesystem\":\"%s\"}",
                         (unsigned long long)totalBytes,
                         (unsigned long long)usedBytes,
                         (unsigned long long)freeBytes, fsParam.c_str());
        std::string jsonResponse = (n > 0 && (size_t)n < sizeof(buf))
                                       ? std::string(buf)
                                       : std::string("{}");
        (void)espwifi->sendJsonResponse(req, 200, jsonResponse, &clientInfo);
        return ESP_OK;
      });

  // API endpoint for creating directories - POST /api/files/mkdir
  registerRoute(
      "/api/files/mkdir", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        // Read small JSON body into a stack buffer to minimize heap.
        const size_t content_len = req->content_len;
        if (content_len == 0 || content_len > 512) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Bad request\"}", &clientInfo);
          return ESP_OK;
        }

        char body[513];
        int r = httpd_req_recv(req, body, content_len);
        if (r <= 0) {
          if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
          }
          return ESP_FAIL;
        }
        body[content_len] = '\0';

        JsonDocument jsonDoc;
        DeserializationError error = deserializeJson(jsonDoc, body);
        if (error) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Invalid JSON\"}", &clientInfo);
          return ESP_OK;
        }

        std::string fsParam = jsonDoc["fs"] | "lfs";
        std::string path = jsonDoc["path"] | "/";
        std::string name = jsonDoc["name"] | "";
        path = normalizePath(path);
        if (!isSafePath(path) || name.empty()) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Bad request\"}", &clientInfo);
          return ESP_OK;
        }

        std::string mountPoint;
        if (!pickMountPoint(espwifi, fsParam, mountPoint)) {
          (void)espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"File system not available\"}",
              &clientInfo);
          return ESP_OK;
        }

        const std::string sanitizedName = espwifi->sanitizeFilename(name);
        if (sanitizedName.empty() || sanitizedName == "." ||
            sanitizedName == "..") {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Bad folder name\"}", &clientInfo);
          return ESP_OK;
        }

        std::string dirPath = path;
        if (dirPath.back() != '/') {
          dirPath += "/";
        }
        dirPath += sanitizedName;

        // Prevent creation of protected paths
        if (espwifi->isProtectedFile(fsParam, dirPath)) {
          return espwifi->sendJsonResponse(
              req, 403, "{\"error\":\"Path is protected\"}", &clientInfo);
        }

        std::string fullDirPath = mountPoint + dirPath;
        if (espwifi->mkDir(fullDirPath)) {
          (void)espwifi->sendJsonResponse(req, 200, "{\"success\":true}",
                                          &clientInfo);
        } else {
          (void)espwifi->sendJsonResponse(
              req, 500, "{\"error\":\"Failed to create directory\"}",
              &clientInfo);
        }
        return ESP_OK;
      });

  // API endpoint for file rename - POST /api/files/rename
  registerRoute(
      "/api/files/rename", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        std::string fsParam = espwifi->getQueryParam(req, "fs");
        std::string oldPath = espwifi->getQueryParam(req, "oldPath");
        std::string newName = espwifi->getQueryParam(req, "newName");

        if (fsParam.empty() || oldPath.empty() || newName.empty()) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing parameters\"}", &clientInfo);
          return ESP_OK;
        }

        oldPath = normalizePath(oldPath);
        if (!isSafePath(oldPath)) {
          (void)espwifi->sendJsonResponse(req, 400, "{\"error\":\"Bad path\"}",
                                          &clientInfo);
          return ESP_OK;
        }

        std::string mountPoint;
        if (!pickMountPoint(espwifi, fsParam, mountPoint)) {
          (void)espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"File system not available\"}",
              &clientInfo);
          return ESP_OK;
        }

        // Prevent renaming of protected paths
        if (espwifi->isProtectedFile(fsParam, oldPath)) {
          return espwifi->sendJsonResponse(
              req, 403, "{\"error\":\"Path is protected\"}", &clientInfo);
        }

        // Sanitize and validate the new name.
        std::string sanitizedNewName = espwifi->sanitizeFilename(newName);
        if (sanitizedNewName.empty() || sanitizedNewName == "." ||
            sanitizedNewName == "..") {
          (void)espwifi->sendJsonResponse(req, 400, "{\"error\":\"Bad name\"}",
                                          &clientInfo);
          return ESP_OK;
        }

        // Get directory path and construct new path
        size_t lastSlash = oldPath.find_last_of('/');
        std::string dirPath = (lastSlash != std::string::npos)
                                  ? oldPath.substr(0, lastSlash)
                                  : "/";
        std::string newPath = dirPath;
        if (newPath.back() != '/') {
          newPath += "/";
        }
        newPath += sanitizedNewName;

        // Prevent renaming INTO a protected path as well.
        if (espwifi->isProtectedFile(fsParam, newPath)) {
          return espwifi->sendJsonResponse(
              req, 403, "{\"error\":\"Target path is protected\"}",
              &clientInfo);
        }

        std::string fullOldPath = mountPoint + oldPath;
        std::string fullNewPath = mountPoint + newPath;

        if (rename(fullOldPath.c_str(), fullNewPath.c_str()) == 0) {
          (void)espwifi->sendJsonResponse(req, 200, "{\"success\":true}",
                                          &clientInfo);
        } else {
          (void)espwifi->sendJsonResponse(
              req, 500, "{\"error\":\"Failed to rename file\"}", &clientInfo);
        }

        return ESP_OK;
      });

  // API endpoint for file deletion - POST /api/files/delete
  registerRoute(
      "/api/files/delete", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        std::string fsParam = espwifi->getQueryParam(req, "fs");
        std::string filePath = espwifi->getQueryParam(req, "path");

        if (fsParam.empty() || filePath.empty()) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing parameters\"}", &clientInfo);
          return ESP_OK;
        }

        filePath = normalizePath(filePath);
        if (!isSafePath(filePath)) {
          (void)espwifi->sendJsonResponse(req, 400, "{\"error\":\"Bad path\"}",
                                          &clientInfo);
          return ESP_OK;
        }

        std::string mountPoint;
        if (!pickMountPoint(espwifi, fsParam, mountPoint)) {
          (void)espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"File system not available\"}",
              &clientInfo);
          return ESP_OK;
        }

        std::string fullPath = mountPoint + filePath;

        // Check if file exists
        if (!espwifi->fileExists(fullPath) && !espwifi->dirExists(fullPath)) {
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"File not found\"}", &clientInfo);
          return ESP_OK;
        }

        // Prevent deletion of protected paths
        if (espwifi->isProtectedFile(fsParam, filePath)) {
          return espwifi->sendJsonResponse(
              req, 403, "{\"error\":\"Path is protected\"}", &clientInfo);
        }

        // Iterative delete to avoid deep recursion; yield periodically.
        bool deleteSuccess = false;
        if (espwifi->dirExists(fullPath)) {
          // Bounded stack of dirs to process.
          std::vector<std::string> stack;
          stack.reserve(16);
          stack.push_back(filePath);
          int ops = 0;
          unsigned long startTime = millis();
          const unsigned long timeout = 10000;

          // First pass: delete files and collect directories.
          while (!stack.empty() && (millis() - startTime) < timeout) {
            std::string cur = stack.back();
            stack.pop_back();

            std::string curFull = mountPoint + cur;
            DIR *dir = opendir(curFull.c_str());
            if (!dir) {
              continue;
            }

            struct dirent *entry;
            while ((entry = readdir(dir)) != nullptr &&
                   (millis() - startTime) < timeout) {
              if (strcmp(entry->d_name, ".") == 0 ||
                  strcmp(entry->d_name, "..") == 0) {
                continue;
              }

              std::string child = cur;
              if (child.back() != '/') {
                child += "/";
              }
              child += entry->d_name;

              std::string childFull = mountPoint + child;
              struct stat st;
              if (stat(childFull.c_str(), &st) != 0) {
                continue;
              }
              if (S_ISDIR(st.st_mode)) {
                stack.push_back(child);
              } else {
                (void)::remove(childFull.c_str());
              }

              if ((++ops % 16) == 0) {
                espwifi->feedWatchDog();
              }
            }
            closedir(dir);
            if ((++ops % 16) == 0) {
              espwifi->feedWatchDog();
            }
          }

          // Second pass: remove directories bottom-up (best-effort).
          // We re-walk using the same bounded logic but delete directories
          // after clearing their children. Simpler: repeatedly attempt rmdir
          // until it succeeds or timeout.
          unsigned long start2 = millis();
          while ((millis() - start2) < timeout) {
            int removed = 0;
            std::vector<std::string> dirs;
            dirs.reserve(16);
            dirs.push_back(filePath);
            while (!dirs.empty() && (millis() - start2) < timeout) {
              std::string cur = dirs.back();
              dirs.pop_back();
              std::string curFull = mountPoint + cur;
              DIR *dir = opendir(curFull.c_str());
              if (!dir) {
                // Might already be gone
                continue;
              }
              struct dirent *entry;
              bool hasSubdir = false;
              while ((entry = readdir(dir)) != nullptr) {
                if (strcmp(entry->d_name, ".") == 0 ||
                    strcmp(entry->d_name, "..") == 0) {
                  continue;
                }
                std::string child = cur;
                if (child.back() != '/') {
                  child += "/";
                }
                child += entry->d_name;
                std::string childFull = mountPoint + child;
                struct stat st;
                if (stat(childFull.c_str(), &st) != 0) {
                  continue;
                }
                if (S_ISDIR(st.st_mode)) {
                  hasSubdir = true;
                  dirs.push_back(child);
                }
              }
              closedir(dir);

              if (!hasSubdir) {
                if (::rmdir(curFull.c_str()) == 0) {
                  removed++;
                }
              }
              if ((++ops % 16) == 0) {
                espwifi->feedWatchDog();
              }
            }
            if (removed == 0) {
              // Nothing else removable; we're done (or stuck).
              break;
            }
            espwifi->feedWatchDog();
          }

          deleteSuccess =
              !espwifi->dirExists(fullPath) && !espwifi->fileExists(fullPath);
        } else {
          deleteSuccess = (::remove(fullPath.c_str()) == 0);
        }

        if (deleteSuccess) {
          (void)espwifi->sendJsonResponse(req, 200, "{\"success\":true}",
                                          &clientInfo);
        } else {
          (void)espwifi->sendJsonResponse(
              req, 500, "{\"error\":\"Failed to delete file\"}", &clientInfo);
        }

        return ESP_OK;
      });

  // API endpoint for file upload - POST /api/files/upload
  // Note: ESP-IDF httpd doesn't have built-in multipart support, so we stream
  // parse multipart/form-data (single file) without buffering the whole body.
  registerRoute(
      "/api/files/upload", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        std::string fsParam = espwifi->getQueryParam(req, "fs");
        std::string path = espwifi->getQueryParam(req, "path");
        path = normalizePath(path);
        if (!isSafePath(path)) {
          (void)espwifi->sendJsonResponse(req, 400, "{\"error\":\"Bad path\"}",
                                          &clientInfo);
          return ESP_OK;
        }

        // Determine filesystem. Default to SD if mounted, else LFS.
        if (fsParam.empty()) {
          fsParam = (espwifi->sdCard != nullptr) ? "sd" : "lfs";
        }

        std::string mountPoint;
        if (!pickMountPoint(espwifi, fsParam, mountPoint)) {
          (void)espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"File system not available\"}",
              &clientInfo);
          return ESP_OK;
        }

        // Content-Type boundary.
        size_t ctypeLen = httpd_req_get_hdr_value_len(req, "Content-Type");
        if (ctypeLen == 0 || ctypeLen > 192) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing Content-Type\"}", &clientInfo);
          return ESP_OK;
        }
        char ctype[193];
        if (httpd_req_get_hdr_value_str(req, "Content-Type", ctype,
                                        sizeof(ctype)) != ESP_OK) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing Content-Type\"}", &clientInfo);
          return ESP_OK;
        }
        std::string contentType(ctype);
        size_t bpos = contentType.find("boundary=");
        if (contentType.find("multipart/form-data") == std::string::npos ||
            bpos == std::string::npos) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Invalid Content-Type\"}", &clientInfo);
          return ESP_OK;
        }
        std::string boundary = "--" + contentType.substr(bpos + 9);
        // Trim any trailing parameters (rare).
        size_t semi = boundary.find(';');
        if (semi != std::string::npos) {
          boundary = boundary.substr(0, semi);
        }

        // Ensure target directory exists (mkdir -p).
        std::string fullDir = mountPoint + path;
        if (!espwifi->mkDir(fullDir)) {
          (void)espwifi->sendJsonResponse(
              req, 500, "{\"error\":\"Failed to create directory\"}",
              &clientInfo);
          return ESP_OK;
        }

        // Streaming multipart parser (single file).
        const size_t RX_CHUNK = 2048;
        char *rx = (char *)malloc(RX_CHUNK);
        if (!rx) {
          httpd_resp_send_500(req);
          return ESP_FAIL;
        }

        std::string carry;
        carry.reserve(boundary.size() + 16);

        FILE *out = nullptr;
        std::string relFilePath;
        std::string fullFilePath;
        bool headersParsed = false;
        bool fileOpen = false;
        size_t totalWritten = 0;

        auto closeAndCleanup = [&]() {
          if (out) {
            fclose(out);
            out = nullptr;
          }
          free(rx);
        };

        size_t remaining = req->content_len;
        unsigned long startTime = millis();
        const unsigned long timeout = 15000; // allow larger uploads

        while (remaining > 0 && (millis() - startTime) < timeout) {
          size_t toRead = (remaining > RX_CHUNK) ? RX_CHUNK : remaining;
          int r = httpd_req_recv(req, rx, toRead);
          if (r <= 0) {
            closeAndCleanup();
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
              httpd_resp_send_408(req);
            }
            return ESP_FAIL;
          }
          remaining -= (size_t)r;

          // Append into a small working buffer.
          std::string chunk(rx, rx + r);
          std::string data = carry + chunk;
          carry.clear();

          // Step 1: parse headers once.
          if (!headersParsed) {
            // Expect boundary then headers. Find the header terminator.
            size_t hdrEnd = data.find("\r\n\r\n");
            if (hdrEnd == std::string::npos) {
              // Need more data; keep bounded tail.
              if (data.size() > 4096) {
                closeAndCleanup();
                (void)espwifi->sendJsonResponse(
                    req, 400, "{\"error\":\"Bad multipart\"}", &clientInfo);
                return ESP_OK;
              }
              carry = data;
              espwifi->feedWatchDog();
              continue;
            }

            std::string headers = data.substr(0, hdrEnd);
            // Extract filename="...".
            size_t fnPos = headers.find("filename=\"");
            if (fnPos == std::string::npos) {
              closeAndCleanup();
              (void)espwifi->sendJsonResponse(
                  req, 400, "{\"error\":\"No filename\"}", &clientInfo);
              return ESP_OK;
            }
            fnPos += 10;
            size_t fnEnd = headers.find("\"", fnPos);
            if (fnEnd == std::string::npos) {
              closeAndCleanup();
              (void)espwifi->sendJsonResponse(
                  req, 400, "{\"error\":\"Bad filename\"}", &clientInfo);
              return ESP_OK;
            }
            std::string filename = headers.substr(fnPos, fnEnd - fnPos);
            std::string sanitized = espwifi->sanitizeFilename(filename);
            if (sanitized.empty()) {
              closeAndCleanup();
              (void)espwifi->sendJsonResponse(
                  req, 400, "{\"error\":\"Bad filename\"}", &clientInfo);
              return ESP_OK;
            }

            relFilePath = path;
            if (relFilePath.back() != '/') {
              relFilePath += "/";
            }
            relFilePath += sanitized;
            fullFilePath = mountPoint + relFilePath;

            // Prevent uploads that would create/overwrite protected paths.
            if (espwifi->isProtectedFile(fsParam, relFilePath)) {
              closeAndCleanup();
              return espwifi->sendJsonResponse(
                  req, 403, "{\"error\":\"Path is protected\"}", &clientInfo);
            }

            out = fopen(fullFilePath.c_str(), "wb");
            if (!out) {
              closeAndCleanup();
              (void)espwifi->sendJsonResponse(
                  req, 500, "{\"error\":\"Failed to create file\"}",
                  &clientInfo);
              return ESP_OK;
            }
            fileOpen = true;
            headersParsed = true;

            // Continue with the remaining payload after headers.
            data = data.substr(hdrEnd + 4);
          }

          if (!fileOpen) {
            // Shouldn't happen, but be safe.
            closeAndCleanup();
            (void)espwifi->sendJsonResponse(req, 500, "{\"error\":\"Upload\"}",
                                            &clientInfo);
            return ESP_OK;
          }

          // Step 2: stream file data until boundary delimiter is found.
          // The delimiter appears as "\r\n--boundary" in the stream.
          std::string marker = "\r\n" + boundary;
          size_t mpos = data.find(marker);
          if (mpos == std::string::npos) {
            // No boundary: write all but keep a tail to catch split marker.
            const size_t keep = marker.size() + 8;
            if (data.size() > keep) {
              size_t writeLen = data.size() - keep;
              size_t written = fwrite(data.data(), 1, writeLen, out);
              totalWritten += written;
              if (written != writeLen) {
                fclose(out);
                out = nullptr;
                (void)::remove(fullFilePath.c_str());
                closeAndCleanup();
                (void)espwifi->sendJsonResponse(
                    req, 500, "{\"error\":\"File write failed\"}", &clientInfo);
                return ESP_OK;
              }
              carry.assign(data.data() + writeLen, keep);
            } else {
              carry = data;
            }
          } else {
            // Write up to marker (excluding the preceding "\r\n").
            if (mpos > 0) {
              size_t writeLen = mpos;
              size_t written = fwrite(data.data(), 1, writeLen, out);
              totalWritten += written;
              if (written != writeLen) {
                fclose(out);
                out = nullptr;
                (void)::remove(fullFilePath.c_str());
                closeAndCleanup();
                (void)espwifi->sendJsonResponse(
                    req, 500, "{\"error\":\"File write failed\"}", &clientInfo);
                return ESP_OK;
              }
            }

            // Done.
            fclose(out);
            out = nullptr;

            closeAndCleanup();
            (void)espwifi->sendJsonResponse(req, 200, "{\"success\":true}",
                                            &clientInfo);
            return ESP_OK;
          }

          if ((totalWritten % 8192) == 0) {
            espwifi->feedWatchDog();
          }
        }

        // Timeout or incomplete.
        if (out) {
          fclose(out);
          out = nullptr;
          (void)::remove(fullFilePath.c_str());
        }
        closeAndCleanup();
        (void)espwifi->sendJsonResponse(
            req, 408, "{\"error\":\"Upload timeout\"}", &clientInfo);
        return ESP_OK;
      });
}
#endif // ESPWiFi_SRV_FILES