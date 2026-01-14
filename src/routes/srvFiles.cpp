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

// ===== REUSABLE FILE BROWSER FUNCTIONS =====

bool ESPWiFi::listFiles(const std::string &fs, const std::string &path,
                        JsonDocument &outDoc, std::string &errorMsg) {
  std::string normalizedPath = normalizePath(path);
  if (!isSafePath(normalizedPath)) {
    errorMsg = "Invalid path";
    return false;
  }

  std::string mountPoint;
  if (!pickMountPoint(this, fs, mountPoint)) {
    errorMsg = "File system not available";
    return false;
  }

  std::string fullPath = mountPoint + normalizedPath;
  DIR *dir = opendir(fullPath.c_str());
  if (!dir) {
    errorMsg = "Directory not found";
    return false;
  }

  JsonArray filesArray = outDoc["files"].to<JsonArray>();
  struct dirent *entry;
  int fileCount = 0;
  const int maxFiles = 1000;

  while ((entry = readdir(dir)) != nullptr && fileCount < maxFiles) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    std::string entryPath = normalizedPath;
    if (entryPath.back() != '/') {
      entryPath += "/";
    }
    entryPath += entry->d_name;

    std::string fullEntryPath = mountPoint + entryPath;
    struct stat st;
    if (stat(fullEntryPath.c_str(), &st) != 0) {
      continue;
    }

    JsonObject fileObj = filesArray.add<JsonObject>();
    fileObj["name"] = entry->d_name;
    fileObj["path"] = entryPath;
    fileObj["isDirectory"] = S_ISDIR(st.st_mode);
    fileObj["size"] = S_ISDIR(st.st_mode) ? 0 : (int64_t)st.st_size;
    fileObj["modified"] = (int64_t)st.st_mtime;

    fileCount++;
    if ((fileCount % 16) == 0) {
      feedWatchDog();
    }
  }

  closedir(dir);
  // log(DEBUG, "📁 List: Success, found %d files", fileCount);
  return true;
}

bool ESPWiFi::makeDirectory(const std::string &fs, const std::string &path,
                            const std::string &name, std::string &errorMsg) {
  std::string normalizedPath = normalizePath(path);
  if (!isSafePath(normalizedPath) || name.empty()) {
    errorMsg = "Invalid path or name";
    return false;
  }

  std::string mountPoint;
  if (!pickMountPoint(this, fs, mountPoint)) {
    errorMsg = "File system not available";
    return false;
  }

  const std::string sanitizedName = sanitizeFilename(name);
  if (sanitizedName.empty() || sanitizedName == "." || sanitizedName == "..") {
    errorMsg = "Bad folder name";
    return false;
  }

  std::string dirPath = normalizedPath;
  if (dirPath.back() != '/') {
    dirPath += "/";
  }
  dirPath += sanitizedName;

  if (isProtectedFile(fs, dirPath)) {
    errorMsg = "Path is protected";
    return false;
  }

  std::string fullDirPath = mountPoint + dirPath;
  if (mkDir(fullDirPath)) {
    log(INFO, "📂 MkDir: Created: %s", dirPath.c_str());
    return true;
  } else {
    errorMsg = "Failed to create directory";
    log(ERROR, " 📂 MkDir: Failed: %s", fullDirPath.c_str());
    return false;
  }
}

bool ESPWiFi::renameFile(const std::string &fs, const std::string &oldPath,
                         const std::string &newName, std::string &errorMsg) {
  std::string normalizedPath = normalizePath(oldPath);
  if (!isSafePath(normalizedPath) || newName.empty()) {
    errorMsg = "Invalid path or name";
    return false;
  }

  std::string mountPoint;
  if (!pickMountPoint(this, fs, mountPoint)) {
    errorMsg = "File system not available";
    return false;
  }

  if (isProtectedFile(fs, normalizedPath)) {
    errorMsg = "Path is protected";
    return false;
  }

  std::string sanitizedNewName = sanitizeFilename(newName);
  if (sanitizedNewName.empty() || sanitizedNewName == "." ||
      sanitizedNewName == "..") {
    errorMsg = "Bad name";
    return false;
  }

  size_t lastSlash = normalizedPath.find_last_of('/');
  std::string dirPath = (lastSlash != std::string::npos)
                            ? normalizedPath.substr(0, lastSlash)
                            : "/";
  std::string newPath = dirPath;
  if (newPath.back() != '/') {
    newPath += "/";
  }
  newPath += sanitizedNewName;

  if (isProtectedFile(fs, newPath)) {
    errorMsg = "Target path is protected";
    return false;
  }

  std::string fullOldPath = mountPoint + normalizedPath;
  std::string fullNewPath = mountPoint + newPath;

  if (rename(fullOldPath.c_str(), fullNewPath.c_str()) == 0) {
    log(INFO, "✏️ Rename: %s -> %s", normalizedPath.c_str(), newPath.c_str());
    return true;
  } else {
    errorMsg = "Failed to rename file";
    log(ERROR, " ✏️ Rename: Failed: %s -> %s", normalizedPath.c_str(),
        fullNewPath.c_str());
    return false;
  }
}

bool ESPWiFi::deleteFile(const std::string &fs, const std::string &filePath,
                         std::string &errorMsg) {
  std::string normalizedPath = normalizePath(filePath);
  if (!isSafePath(normalizedPath)) {
    errorMsg = "Invalid path";
    return false;
  }

  std::string mountPoint;
  if (!pickMountPoint(this, fs, mountPoint)) {
    errorMsg = "File system not available";
    return false;
  }

  std::string fullPath = mountPoint + normalizedPath;

  if (!fileExists(fullPath) && !dirExists(fullPath)) {
    errorMsg = "File not found";
    return false;
  }

  if (isProtectedFile(fs, normalizedPath)) {
    errorMsg = "Path is protected";
    return false;
  }

  bool deleteSuccess = false;
  if (dirExists(fullPath)) {
    // Simple recursive delete for directories
    std::vector<std::string> stack;
    stack.reserve(16);
    stack.push_back(normalizedPath);
    int ops = 0;

    // Delete files
    while (!stack.empty()) {
      std::string cur = stack.back();
      stack.pop_back();
      std::string curFull = mountPoint + cur;
      DIR *dir = opendir(curFull.c_str());
      if (!dir)
        continue;

      struct dirent *entry;
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
        if (stat(childFull.c_str(), &st) != 0)
          continue;

        if (S_ISDIR(st.st_mode)) {
          stack.push_back(child);
        } else {
          ::remove(childFull.c_str());
        }

        if ((++ops % 16) == 0) {
          feedWatchDog();
        }
      }
      closedir(dir);
    }

    // Remove directories
    stack.clear();
    stack.push_back(normalizedPath);
    while (!stack.empty()) {
      std::string cur = stack.back();
      stack.pop_back();
      std::string curFull = mountPoint + cur;
      DIR *dir = opendir(curFull.c_str());
      if (!dir) {
        ::rmdir(curFull.c_str());
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
        if (stat(childFull.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
          hasSubdir = true;
          stack.push_back(child);
        }
      }
      closedir(dir);

      if (!hasSubdir) {
        ::rmdir(curFull.c_str());
      }
    }

    deleteSuccess = !dirExists(fullPath) && !fileExists(fullPath);
  } else {
    deleteSuccess = (::remove(fullPath.c_str()) == 0);
  }

  if (deleteSuccess) {
    log(INFO, "🗑️ Delete: Removed: %s", normalizedPath.c_str());
    return true;
  } else {
    errorMsg = "Failed to delete file";
    log(ERROR, " 🗑️ Delete: Failed: %s", normalizedPath.c_str());
    return false;
  }
}

void ESPWiFi::srvFiles() {
  // API endpoint for file browser JSON data - GET /api/files
  registerRoute(
      "/api/files", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
        std::string fsParam = getQueryParam(request, "fs");
        if (fsParam.empty()) {
          fsParam = "lfs";
        }

        std::string path = getQueryParam(request, "path");
        path = normalizePath(path);

        log(DEBUG, "📁 List: fs=%s, path=%s", fsParam.c_str(), path.c_str());

        if (!isSafePath(path)) {
          log(WARNING, "📁 List: Invalid path: %s", path.c_str());
          sendJsonResponse(request, 400, "{\"error\":\"Bad path\"}");
          return ESP_OK;
        }

        std::string mountPoint;
        if (!pickMountPoint(this, fsParam, mountPoint)) {
          log(WARNING, "📁 List: Filesystem not available: %s",
              fsParam.c_str());
          sendJsonResponse(request, 503,
                           "{\"error\":\"File system not available\"}");
          return ESP_OK;
        }

        std::string fullPath = mountPoint + path;
        DIR *dir = opendir(fullPath.c_str());
        if (!dir) {
          log(WARNING, "📁 List: Directory not found: %s", fullPath.c_str());
          sendJsonResponse(request, 404, "{\"error\":\"Directory not found\"}");
          return ESP_OK;
        }

        // Build JSON response in memory
        JsonDocument doc;
        JsonArray files = doc["files"].to<JsonArray>();

        struct dirent *entry;
        int fileCount = 0;
        const int maxFiles = 1000;
        unsigned long startTime = millis();
        const unsigned long timeout = 3000;

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

          JsonObject fileObj = files.add<JsonObject>();
          fileObj["name"] = entry->d_name;
          fileObj["path"] = entryPath;
          fileObj["isDirectory"] = S_ISDIR(st.st_mode);
          fileObj["size"] = (int64_t)st.st_size;
          fileObj["modified"] = (int64_t)st.st_mtime;

          fileCount++;
          if ((fileCount % 16) == 0) {
            feedWatchDog();
          }
        }

        closedir(dir);

        std::string jsonResponse;
        serializeJson(doc, jsonResponse);
        log(DEBUG, "📁 List: Success, sent %d files", fileCount);
        return sendJsonResponse(request, 200, jsonResponse);
      });

  // API endpoint for storage information - GET /api/storage
  registerRoute("/api/storage", HTTP_GET,
                [this](PsychicRequest *request) -> esp_err_t {
                  std::string fsParam = getQueryParam(request, "fs");
                  if (fsParam.empty()) {
                    fsParam = "lfs";
                  }

                  size_t totalBytes, usedBytes, freeBytes;
                  getStorageInfo(fsParam, totalBytes, usedBytes, freeBytes);

                  JsonDocument doc;
                  doc["total"] = (uint64_t)totalBytes;
                  doc["used"] = (uint64_t)usedBytes;
                  doc["free"] = (uint64_t)freeBytes;
                  doc["filesystem"] = fsParam;

                  std::string jsonResponse;
                  serializeJson(doc, jsonResponse);
                  return sendJsonResponse(request, 200, jsonResponse);
                });

  // API endpoint for creating directories - POST /api/files/mkdir
  registerRoute(
      "/api/files/mkdir", HTTP_POST,
      [this](PsychicRequest *request) -> esp_err_t {
        String body = request->body();
        JsonDocument jsonDoc;
        DeserializationError error = deserializeJson(jsonDoc, body.c_str());
        if (error) {
          return sendJsonResponse(request, 400, "{\"error\":\"Invalid JSON\"}");
        }

        std::string fsParam = jsonDoc["fs"] | "lfs";
        std::string path = jsonDoc["path"] | "/";
        std::string name = jsonDoc["name"] | "";
        path = normalizePath(path);

        log(DEBUG, "📂 MkDir: fs=%s, path=%s, name=%s", fsParam.c_str(),
            path.c_str(), name.c_str());

        if (!isSafePath(path) || name.empty()) {
          log(WARNING, "📂 MkDir: Invalid request");
          return sendJsonResponse(request, 400, "{\"error\":\"Bad request\"}");
        }

        std::string mountPoint;
        if (!pickMountPoint(this, fsParam, mountPoint)) {
          log(WARNING, "📂 MkDir: Filesystem not available: %s",
              fsParam.c_str());
          return sendJsonResponse(request, 503,
                                  "{\"error\":\"File system not available\"}");
        }

        const std::string sanitizedName = sanitizeFilename(name);
        if (sanitizedName.empty() || sanitizedName == "." ||
            sanitizedName == "..") {
          log(WARNING, "📂 MkDir: Bad folder name: %s", name.c_str());
          return sendJsonResponse(request, 400,
                                  "{\"error\":\"Bad folder name\"}");
        }

        std::string dirPath = path;
        if (dirPath.back() != '/') {
          dirPath += "/";
        }
        dirPath += sanitizedName;

        // Prevent creation of protected paths
        if (isProtectedFile(fsParam, dirPath)) {
          log(WARNING, "📂 MkDir: Protected path: %s", dirPath.c_str());
          return sendJsonResponse(request, 403,
                                  "{\"error\":\"Path is protected\"}");
        }

        std::string fullDirPath = mountPoint + dirPath;
        if (mkDir(fullDirPath)) {
          log(INFO, "📂 MkDir: Created: %s", dirPath.c_str());
          return sendJsonResponse(request, 200, "{\"success\":true}");
        } else {
          log(ERROR, " 📂 MkDir: Failed: %s", fullDirPath.c_str());
          return sendJsonResponse(request, 500,
                                  "{\"error\":\"Failed to create directory\"}");
        }
      });

  // API endpoint for file rename - POST /api/files/rename
  registerRoute(
      "/api/files/rename", HTTP_POST,
      [this](PsychicRequest *request) -> esp_err_t {
        std::string fsParam = getQueryParam(request, "fs");
        std::string oldPath = getQueryParam(request, "oldPath");
        std::string newName = getQueryParam(request, "newName");

        log(INFO, "✏️ Rename: fs=%s, oldPath=%s, newName=%s", fsParam.c_str(),
            oldPath.c_str(), newName.c_str());

        if (fsParam.empty() || oldPath.empty() || newName.empty()) {
          log(WARNING, "✏️ Rename: Missing parameters");
          return sendJsonResponse(request, 400,
                                  "{\"error\":\"Missing parameters\"}");
        }

        oldPath = normalizePath(oldPath);
        if (!isSafePath(oldPath)) {
          log(WARNING, "✏️ Rename: Invalid path: %s", oldPath.c_str());
          return sendJsonResponse(request, 400, "{\"error\":\"Bad path\"}");
        }

        std::string mountPoint;
        if (!pickMountPoint(this, fsParam, mountPoint)) {
          log(WARNING, "✏️ Rename: Filesystem not available: %s",
              fsParam.c_str());
          return sendJsonResponse(request, 503,
                                  "{\"error\":\"File system not available\"}");
        }

        // Prevent renaming of protected paths
        if (isProtectedFile(fsParam, oldPath)) {
          log(WARNING, "✏️ Rename: Protected path: %s", oldPath.c_str());
          return sendJsonResponse(request, 403,
                                  "{\"error\":\"Path is protected\"}");
        }

        // Sanitize and validate the new name
        std::string sanitizedNewName = sanitizeFilename(newName);
        if (sanitizedNewName.empty() || sanitizedNewName == "." ||
            sanitizedNewName == "..") {
          log(WARNING, "✏️ Rename: Bad name: %s", newName.c_str());
          return sendJsonResponse(request, 400, "{\"error\":\"Bad name\"}");
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

        // Prevent renaming INTO a protected path
        if (isProtectedFile(fsParam, newPath)) {
          log(WARNING, "✏️ Rename: Target path is protected: %s",
              newPath.c_str());
          return sendJsonResponse(request, 403,
                                  "{\"error\":\"Target path is protected\"}");
        }

        std::string fullOldPath = mountPoint + oldPath;
        std::string fullNewPath = mountPoint + newPath;

        if (rename(fullOldPath.c_str(), fullNewPath.c_str()) == 0) {
          log(INFO, "✏️ Rename: %s -> %s", oldPath.c_str(), newPath.c_str());
          return sendJsonResponse(request, 200, "{\"success\":true}");
        } else {
          log(ERROR, " ✏️ Rename: Failed: %s -> %s", oldPath.c_str(),
              fullNewPath.c_str());
          return sendJsonResponse(request, 500,
                                  "{\"error\":\"Failed to rename file\"}");
        }
      });

  // API endpoint for file deletion - POST /api/files/delete
  registerRoute(
      "/api/files/delete", HTTP_POST,
      [this](PsychicRequest *request) -> esp_err_t {
        std::string fsParam = getQueryParam(request, "fs");
        std::string filePath = getQueryParam(request, "path");

        log(INFO, "🗑️ Delete: fs=%s, path=%s", fsParam.c_str(),
            filePath.c_str());

        if (fsParam.empty() || filePath.empty()) {
          log(WARNING, "🗑️ Delete: Missing parameters");
          return sendJsonResponse(request, 400,
                                  "{\"error\":\"Missing parameters\"}");
        }

        filePath = normalizePath(filePath);
        if (!isSafePath(filePath)) {
          log(WARNING, "🗑️ Delete: Invalid path: %s", filePath.c_str());
          return sendJsonResponse(request, 400, "{\"error\":\"Bad path\"}");
        }

        std::string mountPoint;
        if (!pickMountPoint(this, fsParam, mountPoint)) {
          log(WARNING, "🗑️ Delete: Filesystem not available: %s",
              fsParam.c_str());
          return sendJsonResponse(request, 503,
                                  "{\"error\":\"File system not available\"}");
        }

        std::string fullPath = mountPoint + filePath;

        // Check if file exists
        if (!fileExists(fullPath) && !dirExists(fullPath)) {
          log(WARNING, "🗑️ Delete: Not found: %s", filePath.c_str());
          return sendJsonResponse(request, 404,
                                  "{\"error\":\"File not found\"}");
        }

        // Prevent deletion of protected paths
        if (isProtectedFile(fsParam, filePath)) {
          log(WARNING, "🗑️ Delete: Protected path: %s", filePath.c_str());
          return sendJsonResponse(request, 403,
                                  "{\"error\":\"Path is protected\"}");
        }

        // Iterative delete to avoid deep recursion; yield periodically.
        bool deleteSuccess = false;
        if (dirExists(fullPath)) {
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
                feedWatchDog();
              }
            }
            closedir(dir);
            if ((++ops % 16) == 0) {
              feedWatchDog();
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
                feedWatchDog();
              }
            }
            if (removed == 0) {
              // Nothing else removable; we're done (or stuck).
              break;
            }
            feedWatchDog();
          }

          deleteSuccess = !dirExists(fullPath) && !fileExists(fullPath);
        } else {
          deleteSuccess = (::remove(fullPath.c_str()) == 0);
        }

        if (deleteSuccess) {
          log(INFO, "🗑️ Delete: Removed: %s", filePath.c_str());
          return sendJsonResponse(request, 200, "{\"success\":true}");
        } else {
          log(ERROR, " 🗑️ Delete: Failed: %s", filePath.c_str());
          return sendJsonResponse(request, 500,
                                  "{\"error\":\"Failed to delete file\"}");
        }
      });

  // API endpoint for file upload - POST /api/files/upload
  // Industry-standard streaming multipart/form-data parser
  // Simplified and standardized for maintainability and reliability
  registerRoute(
      "/api/files/upload", HTTP_POST,
      [this](PsychicRequest *request) -> esp_err_t {
        // Get underlying httpd_req_t for streaming operations
        httpd_req_t *req = request->request();
        if (req == nullptr) {
          return ESP_ERR_INVALID_ARG;
        }

        std::string clientInfo = getClientInfo(request);

        // ===== PHASE 1: VALIDATION =====
        std::string fsParam = getQueryParam(request, "fs");
        std::string path = getQueryParam(request, "path");
        path = normalizePath(path);

        log(DEBUG, "📤 Upload: fs=%s, path=%s, len=%zu", fsParam.c_str(),
            path.c_str(), req->content_len);

        if (!isSafePath(path)) {
          log(WARNING, "📤 Upload: Invalid path: %s", path.c_str());
          return sendJsonResponse(request, 400, "{\"error\":\"Invalid path\"}");
        }

        // Determine filesystem with fallback
        if (fsParam.empty()) {
          fsParam = (sdCard != nullptr) ? "sd" : "lfs";
        }

        std::string mountPoint;
        if (!pickMountPoint(this, fsParam, mountPoint)) {
          return sendJsonResponse(request, 503,
                                  "{\"error\":\"File system not available\"}");
        }

        // Validate Content-Type header
        size_t ctypeLen = httpd_req_get_hdr_value_len(req, "Content-Type");
        if (ctypeLen == 0 || ctypeLen > 192) {
          return sendJsonResponse(
              request, 400, "{\"error\":\"Missing or invalid Content-Type\"}");
        }
        char ctype[193];
        if (httpd_req_get_hdr_value_str(req, "Content-Type", ctype,
                                        sizeof(ctype)) != ESP_OK) {
          return sendJsonResponse(
              request, 400, "{\"error\":\"Invalid Content-Type header\"}");
        }

        // Parse boundary from Content-Type
        std::string contentType(ctype);
        if (contentType.find("multipart/form-data") == std::string::npos) {
          return sendJsonResponse(
              request, 400,
              "{\"error\":\"Content-Type must be multipart/form-data\"}");
        }

        size_t bpos = contentType.find("boundary=");
        if (bpos == std::string::npos) {
          return sendJsonResponse(
              request, 400, "{\"error\":\"Missing boundary in Content-Type\"}");
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
          log(WARNING, "📤 Upload: Empty boundary after parsing");
          return sendJsonResponse(request, 400,
                                  "{\"error\":\"Empty boundary\"}");
        }

        // Add leading -- for boundary marker (RFC 2046)
        boundary = "--" + boundary;
        if (boundary.size() < 4) {
          log(WARNING, "📤 Upload: Invalid boundary size: %zu",
              boundary.size());
          return sendJsonResponse(request, 400,
                                  "{\"error\":\"Invalid boundary\"}");
        }

        log(DEBUG, "📤 Upload: boundary=%s", boundary.c_str());

        // Validate content length
        if (req->content_len == 0) {
          return sendJsonResponse(request, 400,
                                  "{\"error\":\"Empty request body\"}");
        }

        // ===== PHASE 2: STREAMING PARSER SETUP =====
        const size_t RX_CHUNK = 4096;
        const size_t MAX_HEADER_SIZE = 4096;
        const size_t MAX_CARRY_SIZE = 256;

        char *rxBuffer = (char *)malloc(RX_CHUNK);
        if (!rxBuffer) {
          return sendJsonResponse(request, 500,
                                  "{\"error\":\"Memory allocation failed\"}");
        }

        char carryBuffer[MAX_CARRY_SIZE];
        size_t carrySize = 0;

        FILE *outFile = nullptr;
        std::string relFilePath;
        std::string fullFilePath;
        std::string uploadFilename;
        bool headersParsed = false;
        size_t totalWritten = 0;
        size_t bytesReceived = 0;
        size_t totalReceived = 0;
        uint32_t nextProgressPct = 10;
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
              return sendJsonResponse(
                  request, 500, "{\"error\":\"Network error during upload\"}");
            }
          }
          remaining -= (size_t)bytesRead;
          bytesReceived += (size_t)bytesRead;
          totalReceived += (size_t)bytesRead;

          // Progress logging (percent of request body received)
          if (req->content_len > 0) {
            uint32_t pct = (uint32_t)(((uint64_t)totalReceived * 100ULL) /
                                      (uint64_t)req->content_len);
            if (pct > 100)
              pct = 100;
            if (pct >= nextProgressPct || pct == 100) {
              const char *name =
                  uploadFilename.empty() ? "<pending>" : uploadFilename.c_str();
              log(DEBUG,
                  "📤 Upload: Progress: %s: %u%% (%zu/%zu bytes, "
                  "written=%zu)",
                  name, (unsigned)pct, totalReceived, req->content_len,
                  totalWritten);
              while (nextProgressPct <= pct && nextProgressPct < 100) {
                nextProgressPct += 10;
              }
            }
          }

          // Feed watchdog periodically
          if (bytesReceived >= 32768) {
            feedWatchDog(1);
            bytesReceived = 0;
          }

          // Combine carry buffer with new data
          size_t totalSize = carrySize + (size_t)bytesRead;
          char *combined = (char *)malloc(totalSize);
          if (!combined) {
            cleanup();
            return sendJsonResponse(request, 500,
                                    "{\"error\":\"Memory allocation failed\"}");
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
              return sendJsonResponse(request, 400,
                                      "{\"error\":\"Headers too large\"}");
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
              return sendJsonResponse(request, 400,
                                      "{\"error\":\"No filename in upload\"}");
            }

            fnPos += 10;
            size_t fnEnd = headers.find("\"", fnPos);
            if (fnEnd == std::string::npos) {
              free(combined);
              cleanup();
              return sendJsonResponse(
                  request, 400, "{\"error\":\"Malformed filename field\"}");
            }

            std::string filename = headers.substr(fnPos, fnEnd - fnPos);
            std::string sanitized = sanitizeFilename(filename);
            uploadFilename = sanitized;

            log(DEBUG, "📤 Upload: Sanitized '%s' -> '%s'", filename.c_str(),
                sanitized.c_str());

            // Build file path
            relFilePath = path;
            if (relFilePath.back() != '/') {
              relFilePath += "/";
            }
            relFilePath += sanitized;
            fullFilePath = mountPoint + relFilePath;

            log(INFO, "📤 Upload: Starting file: %s (%zu bytes)",
                sanitized.c_str(), req->content_len);
            log(DEBUG, "📤 Upload: Full path: %s", fullFilePath.c_str());

            // Open file using member function
            outFile = openFileForWrite(fullFilePath);
            if (!outFile) {
              // Get more specific error information
              int err = errno;
              log(ERROR, "📤 Upload: Failed to create file: %s (errno=%d: %s)",
                  fullFilePath.c_str(), err, strerror(err));

              // Check if it's a disk full error
              if (err == ENOSPC || err == 28) {
                free(combined);
                cleanup();
                return sendJsonResponse(request, 507,
                                        "{\"error\":\"Disk full\"}");
              }

              free(combined);
              cleanup();
              return sendJsonResponse(request, 500,
                                      "{\"error\":\"Failed to create file\"}");
            }
            headersParsed = true;
            log(DEBUG, "📤 Upload: Headers parsed, file opened");

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
                if (!writeFileChunk(outFile, combined + dataStart, writeSize)) {
                  free(combined);
                  cleanup();
                  return sendJsonResponse(request, 500,
                                          "{\"error\":\"File write failed\"}");
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
              if (!writeFileChunk(outFile, combined, writeSize)) {
                free(combined);
                cleanup();
                return sendJsonResponse(request, 500,
                                        "{\"error\":\"File write failed\"}");
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
              if (!writeFileChunk(outFile, combined, finalPos)) {
                free(combined);
                cleanup();
                return sendJsonResponse(request, 500,
                                        "{\"error\":\"File write failed\"}");
              }
              totalWritten += finalPos;
            }

            // Close file successfully using member function
            bool closeSuccess = closeFileStream(outFile, fullFilePath);
            outFile = nullptr;
            free(combined);
            free(rxBuffer);
            rxBuffer = nullptr;
            cleanupDone = true;

            if (!closeSuccess) {
              log(ERROR, "📤 Upload: File close failed (final boundary): %s",
                  fullFilePath.c_str());
              return sendJsonResponse(request, 500,
                                      "{\"error\":\"File close failed\"}");
            }

            log(INFO, "📤 Upload: Complete (final boundary): %s (%zu bytes)",
                relFilePath.c_str(), totalWritten);
            return sendJsonResponse(request, 200, "{\"success\":true}");
          } else if (dataPos != std::string::npos) {
            // Found data boundary - write up to it and close
            if (dataPos > 0) {
              if (!writeFileChunk(outFile, combined, dataPos)) {
                free(combined);
                cleanup();
                return sendJsonResponse(request, 500,
                                        "{\"error\":\"File write failed\"}");
              }
              totalWritten += dataPos;
            }

            // Close file successfully using member function
            bool closeSuccess = closeFileStream(outFile, fullFilePath);
            outFile = nullptr;
            free(combined);
            free(rxBuffer);
            rxBuffer = nullptr;
            cleanupDone = true;

            if (!closeSuccess) {
              log(ERROR, "📤 Upload: File close failed (data boundary): %s",
                  fullFilePath.c_str());
              return sendJsonResponse(request, 500,
                                      "{\"error\":\"File close failed\"}");
            }

            log(INFO, "📤 Upload: Complete (data boundary): %s (%zu bytes)",
                relFilePath.c_str(), totalWritten);
            return sendJsonResponse(request, 200, "{\"success\":true}");
          } else {
            // No boundary found - write all but keep tail for next iteration
            size_t keepSize = maxMarkerLen;
            size_t writeSize =
                (totalSize > keepSize) ? totalSize - keepSize : 0;

            if (writeSize > 0) {
              if (!writeFileChunk(outFile, combined, writeSize)) {
                free(combined);
                cleanup();
                return sendJsonResponse(request, 500,
                                        "{\"error\":\"File write failed\"}");
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
              feedWatchDog(1);
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
                  writeFileChunk(outFile, combined, finalPos);
                  totalWritten += finalPos;
                }

                bool closeSuccess = closeFileStream(outFile, fullFilePath);
                outFile = nullptr;
                free(combined);
                free(rxBuffer);
                rxBuffer = nullptr;
                cleanupDone = true;

                if (closeSuccess) {
                  return sendJsonResponse(request, 200, "{\"success\":true}");
                }
              }
              free(combined);
            }
          }
        }

        // Cleanup and send error response
        cleanup();
        log(WARNING, "📤 Upload: Failed - remaining=%zu, timeout=%s", remaining,
            (remaining > 0) ? "yes" : "no");
        if (remaining > 0) {
          return sendJsonResponse(request, 408,
                                  "{\"error\":\"Upload timeout\"}");
        } else {
          return sendJsonResponse(request, 400,
                                  "{\"error\":\"Incomplete multipart data\"}");
        }
      });
}
#endif // ESPWiFi_SRV_FILES