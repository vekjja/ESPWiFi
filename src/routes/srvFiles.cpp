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

        espwifi->log(DEBUG, "📁 List: fs=%s, path=%s", fsParam.c_str(),
                     path.c_str());

        if (!isSafePath(path)) {
          espwifi->log(WARNING, "📁 List: Invalid path: %s", path.c_str());
          (void)espwifi->sendJsonResponse(req, 400, "{\"error\":\"Bad path\"}",
                                          &clientInfo);
          return ESP_OK;
        }

        std::string mountPoint;
        if (!pickMountPoint(espwifi, fsParam, mountPoint)) {
          espwifi->log(WARNING, "📁 List: Filesystem not available: %s",
                       fsParam.c_str());
          (void)espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"File system not available\"}",
              &clientInfo);
          return ESP_OK;
        }

        std::string fullPath = mountPoint + path;
        DIR *dir = opendir(fullPath.c_str());
        if (!dir) {
          espwifi->log(WARNING, "📁 List: Directory not found: %s",
                       fullPath.c_str());
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

        if (ret == ESP_OK) {
          espwifi->log(DEBUG, "📁 List: Success, sent %d files (%zu bytes)",
                       fileCount, bytesSent);
        } else {
          espwifi->log(ERROR, "💔 📁 List: Failed during streaming");
        }

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
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Bad request\"}", &clientInfo);
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
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Invalid JSON\"}", &clientInfo);
        }

        std::string fsParam = jsonDoc["fs"] | "lfs";
        std::string path = jsonDoc["path"] | "/";
        std::string name = jsonDoc["name"] | "";
        path = normalizePath(path);

        espwifi->log(DEBUG, "📂 MkDir: fs=%s, path=%s, name=%s",
                     fsParam.c_str(), path.c_str(), name.c_str());

        if (!isSafePath(path) || name.empty()) {
          espwifi->log(WARNING, "📂 MkDir: Invalid request");
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Bad request\"}", &clientInfo);
        }

        std::string mountPoint;
        if (!pickMountPoint(espwifi, fsParam, mountPoint)) {
          espwifi->log(WARNING, "📂 MkDir: Filesystem not available: %s",
                       fsParam.c_str());
          return espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"File system not available\"}",
              &clientInfo);
        }

        const std::string sanitizedName = espwifi->sanitizeFilename(name);
        if (sanitizedName.empty() || sanitizedName == "." ||
            sanitizedName == "..") {
          espwifi->log(WARNING, "📂 MkDir: Bad folder name: %s", name.c_str());
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Bad folder name\"}", &clientInfo);
        }

        std::string dirPath = path;
        if (dirPath.back() != '/') {
          dirPath += "/";
        }
        dirPath += sanitizedName;

        // Prevent creation of protected paths
        if (espwifi->isProtectedFile(fsParam, dirPath)) {
          espwifi->log(WARNING, "📂 MkDir: Protected path: %s",
                       dirPath.c_str());
          return espwifi->sendJsonResponse(
              req, 403, "{\"error\":\"Path is protected\"}", &clientInfo);
        }

        std::string fullDirPath = mountPoint + dirPath;
        if (espwifi->mkDir(fullDirPath)) {
          espwifi->log(INFO, "📂 MkDir: Created: %s", dirPath.c_str());
          (void)espwifi->sendJsonResponse(req, 200, "{\"success\":true}",
                                          &clientInfo);
        } else {
          espwifi->log(ERROR, "💔 📂 MkDir: Failed: %s", fullDirPath.c_str());
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

        espwifi->log(DEBUG, "✏️ Rename: fs=%s, oldPath=%s, newName=%s",
                     fsParam.c_str(), oldPath.c_str(), newName.c_str());

        if (fsParam.empty() || oldPath.empty() || newName.empty()) {
          espwifi->log(WARNING, "✏️ Rename: Missing parameters");
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing parameters\"}", &clientInfo);
          return ESP_OK;
        }

        oldPath = normalizePath(oldPath);
        if (!isSafePath(oldPath)) {
          espwifi->log(WARNING, "✏️ Rename: Invalid path: %s", oldPath.c_str());
          (void)espwifi->sendJsonResponse(req, 400, "{\"error\":\"Bad path\"}",
                                          &clientInfo);
          return ESP_OK;
        }

        std::string mountPoint;
        if (!pickMountPoint(espwifi, fsParam, mountPoint)) {
          espwifi->log(WARNING, "✏️ Rename: Filesystem not available: %s",
                       fsParam.c_str());
          (void)espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"File system not available\"}",
              &clientInfo);
          return ESP_OK;
        }

        // Prevent renaming of protected paths
        if (espwifi->isProtectedFile(fsParam, oldPath)) {
          espwifi->log(WARNING, "✏️ Rename: Protected path: %s",
                       oldPath.c_str());
          return espwifi->sendJsonResponse(
              req, 403, "{\"error\":\"Path is protected\"}", &clientInfo);
        }

        // Sanitize and validate the new name.
        std::string sanitizedNewName = espwifi->sanitizeFilename(newName);
        if (sanitizedNewName.empty() || sanitizedNewName == "." ||
            sanitizedNewName == "..") {
          espwifi->log(WARNING, "✏️ Rename: Bad name: %s", newName.c_str());
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
          espwifi->log(WARNING, "✏️ Rename: Target path is protected: %s",
                       newPath.c_str());
          return espwifi->sendJsonResponse(
              req, 403, "{\"error\":\"Target path is protected\"}",
              &clientInfo);
        }

        std::string fullOldPath = mountPoint + oldPath;
        std::string fullNewPath = mountPoint + newPath;

        if (rename(fullOldPath.c_str(), fullNewPath.c_str()) == 0) {
          espwifi->log(INFO, "✏️ Rename: %s -> %s", oldPath.c_str(),
                       newPath.c_str());
          (void)espwifi->sendJsonResponse(req, 200, "{\"success\":true}",
                                          &clientInfo);
        } else {
          espwifi->log(ERROR, "💔 ✏️ Rename: Failed: %s -> %s", oldPath.c_str(),
                       fullNewPath.c_str());
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

        espwifi->log(DEBUG, "🗑️ Delete: fs=%s, path=%s", fsParam.c_str(),
                     filePath.c_str());

        if (fsParam.empty() || filePath.empty()) {
          espwifi->log(WARNING, "🗑️ Delete: Missing parameters");
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing parameters\"}", &clientInfo);
          return ESP_OK;
        }

        filePath = normalizePath(filePath);
        if (!isSafePath(filePath)) {
          espwifi->log(WARNING, "🗑️ Delete: Invalid path: %s",
                       filePath.c_str());
          (void)espwifi->sendJsonResponse(req, 400, "{\"error\":\"Bad path\"}",
                                          &clientInfo);
          return ESP_OK;
        }

        std::string mountPoint;
        if (!pickMountPoint(espwifi, fsParam, mountPoint)) {
          espwifi->log(WARNING, "🗑️ Delete: Filesystem not available: %s",
                       fsParam.c_str());
          (void)espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"File system not available\"}",
              &clientInfo);
          return ESP_OK;
        }

        std::string fullPath = mountPoint + filePath;

        // Check if file exists
        if (!espwifi->fileExists(fullPath) && !espwifi->dirExists(fullPath)) {
          espwifi->log(WARNING, "🗑️ Delete: Not found: %s", filePath.c_str());
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"File not found\"}", &clientInfo);
          return ESP_OK;
        }

        // Prevent deletion of protected paths
        if (espwifi->isProtectedFile(fsParam, filePath)) {
          espwifi->log(WARNING, "🗑️ Delete: Protected path: %s",
                       filePath.c_str());
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
          espwifi->log(INFO, "🗑️ Delete: Removed: %s", filePath.c_str());
          (void)espwifi->sendJsonResponse(req, 200, "{\"success\":true}",
                                          &clientInfo);
        } else {
          espwifi->log(ERROR, "💔 🗑️ Delete: Failed: %s", filePath.c_str());
          (void)espwifi->sendJsonResponse(
              req, 500, "{\"error\":\"Failed to delete file\"}", &clientInfo);
        }

        return ESP_OK;
      });

  // API endpoint for file upload - POST /api/files/upload
  // Industry-standard streaming multipart/form-data parser
  // Simplified and standardized for maintainability and reliability
  registerRoute(
      "/api/files/upload", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        // ===== PHASE 1: VALIDATION =====
        std::string fsParam = espwifi->getQueryParam(req, "fs");
        std::string path = espwifi->getQueryParam(req, "path");
        path = normalizePath(path);

        espwifi->log(DEBUG, "📤 Upload: fs=%s, path=%s, len=%zu",
                     fsParam.c_str(), path.c_str(), req->content_len);

        if (!isSafePath(path)) {
          espwifi->log(WARNING, "📤 Upload: Invalid path: %s", path.c_str());
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Invalid path\"}", &clientInfo);
        }

        // Determine filesystem with fallback
        if (fsParam.empty()) {
          fsParam = (espwifi->sdCard != nullptr) ? "sd" : "lfs";
        }

        std::string mountPoint;
        if (!pickMountPoint(espwifi, fsParam, mountPoint)) {
          return espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"File system not available\"}",
              &clientInfo);
        }

        // Validate Content-Type header
        size_t ctypeLen = httpd_req_get_hdr_value_len(req, "Content-Type");
        if (ctypeLen == 0 || ctypeLen > 192) {
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing or invalid Content-Type\"}",
              &clientInfo);
        }
        char ctype[193];
        if (httpd_req_get_hdr_value_str(req, "Content-Type", ctype,
                                        sizeof(ctype)) != ESP_OK) {
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Invalid Content-Type header\"}",
              &clientInfo);
        }

        // Parse boundary from Content-Type
        std::string contentType(ctype);
        if (contentType.find("multipart/form-data") == std::string::npos) {
          return espwifi->sendJsonResponse(
              req, 400,
              "{\"error\":\"Content-Type must be multipart/form-data\"}",
              &clientInfo);
        }

        size_t bpos = contentType.find("boundary=");
        if (bpos == std::string::npos) {
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing boundary in Content-Type\"}",
              &clientInfo);
        }

        // Extract and normalize boundary
        std::string boundary = contentType.substr(bpos + 9);
        size_t semi = boundary.find(';');
        if (semi != std::string::npos) {
          boundary = boundary.substr(0, semi);
        }
        // Trim whitespace and quotes
        while (!boundary.empty() &&
               (boundary.back() == ' ' || boundary.back() == '\t' ||
                boundary.back() == '"' || boundary.back() == '\'')) {
          boundary.pop_back();
        }
        while (!boundary.empty() &&
               (boundary.front() == ' ' || boundary.front() == '\t' ||
                boundary.front() == '"' || boundary.front() == '\'')) {
          boundary = boundary.substr(1);
        }

        if (boundary.empty()) {
          espwifi->log(WARNING, "📤 Upload: Empty boundary after parsing");
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Empty boundary\"}", &clientInfo);
        }

        // Add leading -- for boundary marker (RFC 2046)
        boundary = "--" + boundary;
        if (boundary.size() < 4) {
          espwifi->log(WARNING, "📤 Upload: Invalid boundary size: %zu",
                       boundary.size());
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Invalid boundary\"}", &clientInfo);
        }

        espwifi->log(DEBUG, "📤 Upload: boundary=%s", boundary.c_str());

        // Validate content length
        if (req->content_len == 0) {
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Empty request body\"}", &clientInfo);
        }

        // ===== PHASE 2: STREAMING PARSER SETUP =====
        const size_t RX_CHUNK = 4096;
        const size_t MAX_HEADER_SIZE = 4096;
        const size_t MAX_CARRY_SIZE = 256;

        char *rxBuffer = (char *)malloc(RX_CHUNK);
        if (!rxBuffer) {
          return espwifi->sendJsonResponse(
              req, 500, "{\"error\":\"Memory allocation failed\"}",
              &clientInfo);
        }

        char carryBuffer[MAX_CARRY_SIZE];
        size_t carrySize = 0;

        FILE *outFile = nullptr;
        std::string relFilePath;
        std::string fullFilePath;
        bool headersParsed = false;
        size_t totalWritten = 0;
        size_t bytesReceived = 0;
        bool cleanupDone = false;

        // Boundary markers (RFC 2046 compliant)
        std::string dataMarker = "\r\n" + boundary;
        std::string finalMarker = "\r\n" + boundary + "--";
        const size_t maxMarkerLen = finalMarker.size();

        // Idempotent cleanup function
        auto cleanup = [&]() {
          if (cleanupDone)
            return;
          cleanupDone = true;

          if (outFile) {
            fclose(outFile);
            outFile = nullptr;
            // Only remove file on error (closeFileStream would do this)
            if (!fullFilePath.empty()) {
              ::remove(fullFilePath.c_str());
            }
          }
          if (rxBuffer) {
            free(rxBuffer);
            rxBuffer = nullptr;
          }
        };

        // ===== PHASE 3: STREAMING PROCESSING =====
        size_t remaining = req->content_len;
        unsigned long startTime = millis();
        // Dynamic timeout: 30s base + 1s per 20KB, max 2 minutes
        unsigned long timeout = 30000 + ((req->content_len / 20480) * 1000);
        if (timeout > 120000)
          timeout = 120000;

        while (remaining > 0 && (millis() - startTime) < timeout) {
          size_t toRead = (remaining > RX_CHUNK) ? RX_CHUNK : remaining;
          int bytesRead = httpd_req_recv(req, rxBuffer, toRead);
          if (bytesRead <= 0) {
            cleanup();
            if (bytesRead == HTTPD_SOCK_ERR_TIMEOUT) {
              httpd_resp_send_408(req);
              return ESP_FAIL;
            } else {
              return espwifi->sendJsonResponse(
                  req, 500, "{\"error\":\"Network error during upload\"}",
                  &clientInfo);
            }
          }
          remaining -= (size_t)bytesRead;
          bytesReceived += (size_t)bytesRead;

          // Feed watchdog periodically
          if (bytesReceived >= 32768) {
            espwifi->feedWatchDog(1);
            bytesReceived = 0;
          }

          // Combine carry buffer with new data
          size_t totalSize = carrySize + (size_t)bytesRead;
          char *combined = (char *)malloc(totalSize);
          if (!combined) {
            cleanup();
            return espwifi->sendJsonResponse(
                req, 500, "{\"error\":\"Memory allocation failed\"}",
                &clientInfo);
          }
          memcpy(combined, carryBuffer, carrySize);
          if (bytesRead > 0) {
            memcpy(combined + carrySize, rxBuffer, bytesRead);
          }

          // Parse headers if not yet parsed
          if (!headersParsed) {
            if (totalSize > MAX_HEADER_SIZE) {
              free(combined);
              cleanup();
              return espwifi->sendJsonResponse(
                  req, 400, "{\"error\":\"Headers too large\"}", &clientInfo);
            }

            // Find header terminator \r\n\r\n
            size_t headerEnd = 0;
            bool foundHeaders = false;
            for (size_t i = 0; i <= totalSize - 4; ++i) {
              if (combined[i] == '\r' && combined[i + 1] == '\n' &&
                  combined[i + 2] == '\r' && combined[i + 3] == '\n') {
                headerEnd = i + 4;
                foundHeaders = true;
                break;
              }
            }

            if (!foundHeaders) {
              // Keep tail in carry buffer
              size_t keepSize =
                  (totalSize < maxMarkerLen) ? totalSize : maxMarkerLen;
              if (keepSize > MAX_CARRY_SIZE)
                keepSize = MAX_CARRY_SIZE;
              memcpy(carryBuffer, combined + (totalSize - keepSize), keepSize);
              carrySize = keepSize;
              free(combined);
              continue;
            }

            // Parse headers
            std::string headers(combined, headerEnd);
            size_t fnPos = headers.find("filename=\"");
            if (fnPos == std::string::npos) {
              free(combined);
              cleanup();
              return espwifi->sendJsonResponse(
                  req, 400, "{\"error\":\"No filename in upload\"}",
                  &clientInfo);
            }

            fnPos += 10;
            size_t fnEnd = headers.find("\"", fnPos);
            if (fnEnd == std::string::npos) {
              free(combined);
              cleanup();
              return espwifi->sendJsonResponse(
                  req, 400, "{\"error\":\"Malformed filename field\"}",
                  &clientInfo);
            }

            std::string filename = headers.substr(fnPos, fnEnd - fnPos);
            std::string sanitized = espwifi->sanitizeFilename(filename);

            espwifi->log(DEBUG, "📤 Upload: Sanitized '%s' -> '%s'",
                         filename.c_str(), sanitized.c_str());

            // Build file path
            relFilePath = path;
            if (relFilePath.back() != '/') {
              relFilePath += "/";
            }
            relFilePath += sanitized;
            fullFilePath = mountPoint + relFilePath;

            espwifi->log(INFO, "📤 Upload: Starting file: %s (%zu bytes)",
                         sanitized.c_str(), req->content_len);
            espwifi->log(DEBUG, "📤 Upload: Full path: %s",
                         fullFilePath.c_str());

            // Open file using member function
            outFile = espwifi->openFileForWrite(fullFilePath);
            if (!outFile) {
              // Get more specific error information
              int err = errno;
              espwifi->log(
                  ERROR, "📤 Upload: Failed to create file: %s (errno=%d: %s)",
                  fullFilePath.c_str(), err, strerror(err));

              // Check if it's a disk full error
              if (err == ENOSPC || err == 28) {
                free(combined);
                cleanup();
                return espwifi->sendJsonResponse(
                    req, 507, "{\"error\":\"Disk full\"}", &clientInfo);
              }

              free(combined);
              cleanup();
              return espwifi->sendJsonResponse(
                  req, 500, "{\"error\":\"Failed to create file\"}",
                  &clientInfo);
            }
            headersParsed = true;
            espwifi->log(DEBUG, "📤 Upload: Headers parsed, file opened");

            // Process remaining data after headers
            size_t dataStart = headerEnd;
            size_t dataSize = totalSize - dataStart;
            if (dataSize > 0) {
              // Move data after headers to carry buffer for processing
              if (dataSize <= MAX_CARRY_SIZE) {
                memcpy(carryBuffer, combined + dataStart, dataSize);
                carrySize = dataSize;
              } else {
                // Write what we can, keep tail
                size_t writeSize = dataSize - maxMarkerLen;
                if (!espwifi->writeFileChunk(outFile, combined + dataStart,
                                             writeSize)) {
                  free(combined);
                  cleanup();
                  return espwifi->sendJsonResponse(
                      req, 500, "{\"error\":\"File write failed\"}",
                      &clientInfo);
                }
                totalWritten += writeSize;
                memcpy(carryBuffer, combined + dataStart + writeSize,
                       maxMarkerLen);
                carrySize = maxMarkerLen;
              }
            } else {
              carrySize = 0;
            }
            free(combined);
            continue;
          }

          // Process file data - find boundaries
          // Guard against buffer overflow
          if (totalSize < finalMarker.size() && totalSize < dataMarker.size()) {
            // Data too small to contain any boundary marker, keep in carry
            if (totalSize <= MAX_CARRY_SIZE) {
              memcpy(carryBuffer, combined, totalSize);
              carrySize = totalSize;
            } else {
              // Write most of it, keep tail
              size_t writeSize = totalSize - maxMarkerLen;
              if (!espwifi->writeFileChunk(outFile, combined, writeSize)) {
                free(combined);
                cleanup();
                return espwifi->sendJsonResponse(
                    req, 500, "{\"error\":\"File write failed\"}", &clientInfo);
              }
              totalWritten += writeSize;
              memcpy(carryBuffer, combined + writeSize, totalSize - writeSize);
              carrySize = totalSize - writeSize;
            }
            free(combined);
            continue;
          }

          size_t finalPos = std::string::npos;
          size_t dataPos = std::string::npos;

          // Search for final marker first (more specific) - with bounds check
          if (totalSize >= finalMarker.size()) {
            for (size_t i = 0; i <= totalSize - finalMarker.size(); ++i) {
              if (memcmp(combined + i, finalMarker.c_str(),
                         finalMarker.size()) == 0) {
                finalPos = i;
                break;
              }
            }
          }

          // Search for data marker if final not found - with bounds check
          if (finalPos == std::string::npos && totalSize >= dataMarker.size()) {
            for (size_t i = 0; i <= totalSize - dataMarker.size(); ++i) {
              if (memcmp(combined + i, dataMarker.c_str(), dataMarker.size()) ==
                  0) {
                dataPos = i;
                break;
              }
            }
          }

          if (finalPos != std::string::npos) {
            // Found final boundary - write up to it and close
            if (finalPos > 0) {
              if (!espwifi->writeFileChunk(outFile, combined, finalPos)) {
                free(combined);
                cleanup();
                return espwifi->sendJsonResponse(
                    req, 500, "{\"error\":\"File write failed\"}", &clientInfo);
              }
              totalWritten += finalPos;
            }

            // Close file successfully using member function
            bool closeSuccess = espwifi->closeFileStream(outFile, fullFilePath);
            outFile = nullptr;
            free(combined);
            free(rxBuffer);
            rxBuffer = nullptr;
            cleanupDone = true;

            if (!closeSuccess) {
              espwifi->log(ERROR,
                           "📤 Upload: File close failed (final boundary): %s",
                           fullFilePath.c_str());
              return espwifi->sendJsonResponse(
                  req, 500, "{\"error\":\"File close failed\"}", &clientInfo);
            }

            espwifi->log(INFO,
                         "📤 Upload: Complete (final boundary): %s (%zu bytes)",
                         relFilePath.c_str(), totalWritten);
            return espwifi->sendJsonResponse(req, 200, "{\"success\":true}",
                                             &clientInfo);
          } else if (dataPos != std::string::npos) {
            // Found data boundary - write up to it and close
            if (dataPos > 0) {
              if (!espwifi->writeFileChunk(outFile, combined, dataPos)) {
                free(combined);
                cleanup();
                return espwifi->sendJsonResponse(
                    req, 500, "{\"error\":\"File write failed\"}", &clientInfo);
              }
              totalWritten += dataPos;
            }

            // Close file successfully using member function
            bool closeSuccess = espwifi->closeFileStream(outFile, fullFilePath);
            outFile = nullptr;
            free(combined);
            free(rxBuffer);
            rxBuffer = nullptr;
            cleanupDone = true;

            if (!closeSuccess) {
              espwifi->log(ERROR,
                           "📤 Upload: File close failed (data boundary): %s",
                           fullFilePath.c_str());
              return espwifi->sendJsonResponse(
                  req, 500, "{\"error\":\"File close failed\"}", &clientInfo);
            }

            espwifi->log(INFO,
                         "📤 Upload: Complete (data boundary): %s (%zu bytes)",
                         relFilePath.c_str(), totalWritten);
            return espwifi->sendJsonResponse(req, 200, "{\"success\":true}",
                                             &clientInfo);
          } else {
            // No boundary found - write all but keep tail for next iteration
            size_t keepSize = maxMarkerLen;
            size_t writeSize =
                (totalSize > keepSize) ? totalSize - keepSize : 0;

            if (writeSize > 0) {
              if (!espwifi->writeFileChunk(outFile, combined, writeSize)) {
                free(combined);
                cleanup();
                return espwifi->sendJsonResponse(
                    req, 500, "{\"error\":\"File write failed\"}", &clientInfo);
              }
              totalWritten += writeSize;
            }

            // Keep tail in carry buffer
            if (totalSize > 0) {
              size_t tailKeepSize =
                  (totalSize > maxMarkerLen) ? maxMarkerLen : totalSize;
              memcpy(carryBuffer, combined + (totalSize - tailKeepSize),
                     tailKeepSize);
              carrySize = tailKeepSize;
            } else {
              carrySize = 0;
            }

            free(combined);

            if ((totalWritten % 65536) == 0) {
              espwifi->feedWatchDog(1);
            }
          }
        }

        // ===== PHASE 4: FINALIZATION =====
        // Process carry buffer if upload completed without finding final
        // boundary
        if (headersParsed && outFile && carrySize > 0 && remaining == 0) {
          // Bounds check before processing carry buffer
          if (carrySize >= finalMarker.size()) {
            char *combined = (char *)malloc(carrySize);
            if (combined) {
              memcpy(combined, carryBuffer, carrySize);
              size_t finalPos = std::string::npos;

              for (size_t i = 0; i <= carrySize - finalMarker.size(); ++i) {
                if (memcmp(combined + i, finalMarker.c_str(),
                           finalMarker.size()) == 0) {
                  finalPos = i;
                  break;
                }
              }

              if (finalPos != std::string::npos) {
                if (finalPos > 0) {
                  espwifi->writeFileChunk(outFile, combined, finalPos);
                  totalWritten += finalPos;
                }

                bool closeSuccess =
                    espwifi->closeFileStream(outFile, fullFilePath);
                outFile = nullptr;
                free(combined);
                free(rxBuffer);
                rxBuffer = nullptr;
                cleanupDone = true;

                if (closeSuccess) {
                  return espwifi->sendJsonResponse(
                      req, 200, "{\"success\":true}", &clientInfo);
                }
              }
              free(combined);
            }
          }
        }

        // Cleanup and send error response
        cleanup();
        espwifi->log(WARNING, "📤 Upload: Failed - remaining=%zu, timeout=%s",
                     remaining, (remaining > 0) ? "yes" : "no");
        if (remaining > 0) {
          return espwifi->sendJsonResponse(
              req, 408, "{\"error\":\"Upload timeout\"}", &clientInfo);
        } else {
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Incomplete multipart data\"}",
              &clientInfo);
        }
      });
}
#endif // ESPWiFi_SRV_FILES