#ifndef ESPWiFi_FileSystem
#define ESPWiFi_FileSystem

#include "ESPWiFi.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include <ArduinoJson.h>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

void ESPWiFi::initLittleFS() {
  if (littleFsInitialized) {
    return;
  }

  esp_vfs_littlefs_conf_t conf = {
      .base_path = "/lfs",
      .partition_label = "littlefsp",
      .partition = nullptr,
      .format_if_mount_failed = true,
      .read_only = false,
      .dont_mount = false,
      .grow_on_mount = false,
  };

  esp_err_t ret = esp_vfs_littlefs_register(&conf);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      printf("Failed to mount or format filesystem\n");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      printf("Failed to find LittleFS partition\n");
    } else {
      printf("Failed to initialize LittleFS (%s)\n", esp_err_to_name(ret));
    }
    return;
  }

  littleFsInitialized = true;
  log(INFO, "💾 LittleFS Initialized");
}

void ESPWiFi::initSDCard() {
  // SD Card support commented out for now
  sdCardInitialized = false;
  log(WARNING, "💾 SD Card not implemented yet");
}

std::string ESPWiFi::sanitizeFilename(const std::string &filename) {
  std::string sanitized = filename;

  // Replace spaces with underscores
  for (char &c : sanitized) {
    if (c == ' ')
      c = '_';
    else if (!isalnum(c) && c != '.' && c != '-' && c != '_' && c != '/') {
      c = '_';
    }
  }

  // Remove duplicate underscores
  size_t pos;
  while ((pos = sanitized.find("__")) != std::string::npos) {
    sanitized.replace(pos, 2, "_");
  }

  // Trim leading/trailing underscores
  if (!sanitized.empty() && sanitized[0] == '_') {
    sanitized = sanitized.substr(1);
  }
  if (!sanitized.empty() && sanitized[sanitized.length() - 1] == '_') {
    sanitized = sanitized.substr(0, sanitized.length() - 1);
  }

  return sanitized;
}

void ESPWiFi::getStorageInfo(const std::string &fsParam, size_t &totalBytes,
                             size_t &usedBytes, size_t &freeBytes) {
  totalBytes = 0;
  usedBytes = 0;
  freeBytes = 0;

  if (fsParam == "lfs" && littleFsInitialized) {
    size_t total = 0, used = 0;
    esp_err_t ret = esp_littlefs_info("littlefsp", &total, &used);
    if (ret == ESP_OK) {
      totalBytes = total;
      usedBytes = used;
      freeBytes = total - used;
    }
  }
}

void ESPWiFi::logFilesystemInfo(const std::string &fsName, size_t totalBytes,
                                size_t usedBytes) {
  log(INFO, "💾 %s Filesystem", fsName.c_str());
  log(DEBUG, "\tTotal: %s", bytesToHumanReadable(totalBytes).c_str());
  log(DEBUG, "\tUsed: %s", bytesToHumanReadable(usedBytes).c_str());
  log(DEBUG, "\tFree: %s",
      bytesToHumanReadable(totalBytes - usedBytes).c_str());
}

void ESPWiFi::printFilesystemInfo() {
  if (littleFsInitialized) {
    size_t totalBytes, usedBytes, freeBytes;
    getStorageInfo("lfs", totalBytes, usedBytes, freeBytes);
    logFilesystemInfo("LittleFS", totalBytes, usedBytes);
  }
}

bool ESPWiFi::deleteDirectoryRecursive(const std::string &dirPath) {
  if (!littleFsInitialized)
    return false;

  std::string full_path = lfsMountPoint + dirPath;
  DIR *dir = opendir(full_path.c_str());
  if (!dir)
    return false;

  struct dirent *entry;
  int entryCount = 0;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    std::string entry_path = dirPath + "/" + entry->d_name;
    std::string full_entry_path = lfsMountPoint + entry_path;

    struct stat st;
    if (stat(full_entry_path.c_str(), &st) == 0) {
      if (S_ISDIR(st.st_mode)) {
        deleteDirectoryRecursive(entry_path);
      } else {
        ::remove(full_entry_path.c_str());
      }
    }

    // Yield periodically to prevent watchdog timeout
    if (++entryCount % 10 == 0) {
      yield();
    }
  }
  closedir(dir);

  std::string full_dir_path = lfsMountPoint + dirPath;
  return ::rmdir(full_dir_path.c_str()) == 0;
}

bool ESPWiFi::writeFile(const std::string &filePath, const uint8_t *data,
                        size_t len) {
  if (!littleFsInitialized)
    return false;

  std::string full_path = lfsMountPoint + filePath;
  FILE *file = fopen(full_path.c_str(), "wb");
  if (!file)
    return false;

  size_t written = fwrite(data, 1, len, file);
  // Yield after file I/O to prevent watchdog timeout
  if (len > 1024) {
    yield();
  }
  fclose(file);

  return written == len;
}

bool ESPWiFi::writeFileAtomic(const std::string &filePath, const uint8_t *data,
                              size_t len) {
  if (!littleFsInitialized)
    return false;

  // Use atomic write: write to temp file first, then rename
  // This prevents corruption if write fails partway through
  std::string full_path = lfsMountPoint + filePath;
  std::string temp_path = full_path + ".tmp";

  // Remove temp file if it exists (leftover from previous failed write)
  ::remove(temp_path.c_str());

  FILE *f = fopen(temp_path.c_str(), "wb");
  if (!f) {
    return false;
  }

  size_t bytesWritten = fwrite(data, 1, len, f);
  yield(); // Yield after file write

  // Flush the file to ensure data is written to filesystem
  if (fflush(f) != 0) {
    fclose(f);
    ::remove(temp_path.c_str()); // Clean up temp file
    return false;
  }

  fclose(f);

  if (bytesWritten != len) {
    ::remove(temp_path.c_str()); // Clean up temp file
    return false;
  }

  // Atomically replace the file with the temp file
  if (rename(temp_path.c_str(), full_path.c_str()) != 0) {
    ::remove(temp_path.c_str()); // Clean up temp file
    return false;
  }

  return true;
}

char *ESPWiFi::readFile(const std::string &filePath, size_t *outSize) {
  if (!littleFsInitialized) {
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  std::string full_path = lfsMountPoint + filePath;
  FILE *f = fopen(full_path.c_str(), "rb");
  if (!f) {
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  // Get file size
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  long fileSize = ftell(f);
  if (fileSize < 0) {
    fclose(f);
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  if (fileSize == 0) {
    fclose(f);
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  // Allocate buffer (caller must free)
  char *buffer = (char *)malloc(fileSize + 1);
  if (!buffer) {
    fclose(f);
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  size_t bytesRead = fread(buffer, 1, fileSize, f);
  fclose(f);

  if (bytesRead != (size_t)fileSize) {
    free(buffer);
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  buffer[bytesRead] = '\0'; // Null terminate for string operations

  if (outSize)
    *outSize = bytesRead;

  return buffer;
}

bool ESPWiFi::isRestrictedSystemFile(const std::string &fsParam,
                                     const std::string &filePath) {
  // Only restrict files on LittleFS, not SD card
  if (fsParam != "lfs") {
    return false;
  }

  // List of restricted system files and directories
  if (filePath == "/config.json" || filePath == "/index.html" ||
      filePath == "/asset-manifest.json" || filePath == "/dashboard.zip" ||
      filePath.find("/static/") == 0 || filePath.find("/system/") == 0 ||
      filePath.find("/boot/") == 0) {
    return true;
  }

  // Also protect config file and log file
  if (filePath == configFile || filePath == logFilePath) {
    return true;
  }

  return false;
}

// Helper function to check if file exists
static bool fileExists(const std::string &fullPath) {
  struct stat st;
  if (stat(fullPath.c_str(), &st) == 0) {
    return !S_ISDIR(st.st_mode);
  }
  return false;
}

// Helper function to check if directory exists
static bool dirExists(const std::string &fullPath) {
  struct stat st;
  if (stat(fullPath.c_str(), &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  return false;
}

// Helper function to create directory
static bool mkDir(const std::string &fullPath) {
  if (dirExists(fullPath)) {
    return true;
  }

  // Create parent directories if needed
  size_t pos = fullPath.find_last_of('/');
  if (pos != std::string::npos && pos > 0) {
    std::string parent = fullPath.substr(0, pos);
    if (!dirExists(parent)) {
      mkDir(parent);
    }
  }

  if (mkdir(fullPath.c_str(), 0755) == 0) {
    return true;
  }

  // Check if directory was actually created despite the error
  return dirExists(fullPath);
}

std::string ESPWiFi::getFileExtension(const std::string &filename) {
  size_t pos = filename.find_last_of('.');
  if (pos != std::string::npos) {
    return filename.substr(pos + 1);
  }
  return "";
}

// File upload handler - used for chunked uploads if needed
// Currently uploads are handled directly in the /api/files/upload endpoint
void ESPWiFi::handleFileUpload(void *req, const std::string &filename,
                               size_t index, uint8_t *data, size_t len,
                               bool final) {
  // This function signature matches the original AsyncWebServer pattern
  // For ESP-IDF, we handle uploads directly in the endpoint handler
  // This stub is kept for API compatibility
  (void)req;
  (void)filename;
  (void)index;
  (void)data;
  (void)len;
  (void) final;
}

// Helper function to get query parameter (static to avoid lambda capture
// issues)
static std::string getQueryParam(httpd_req_t *req, const char *key) {
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    char *buf = (char *)malloc(buf_len);
    if (buf) {
      if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
        char value[128];
        if (httpd_query_key_value(buf, key, value, sizeof(value)) == ESP_OK) {
          std::string result(value);
          free(buf);
          return result;
        }
      }
      free(buf);
    }
  }
  return "";
}

void ESPWiFi::srvFS() {
  // API endpoint for file browser JSON data - GET /api/files
  registerRoute(
      "/api/files", HTTP_GET,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        std::string fsParam = getQueryParam(req, "fs");
        if (fsParam.empty()) {
          fsParam = "lfs";
        }

        std::string path = getQueryParam(req, "path");
        if (path.empty()) {
          path = "/";
        }
        if (path[0] != '/') {
          path = "/" + path;
        }

        // Only support LittleFS for now (SD card not implemented)
        if (fsParam != "lfs" || !espwifi->littleFsInitialized) {
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"File system not available\"}", &clientInfo);
          return ESP_OK;
        }

        std::string fullPath = espwifi->lfsMountPoint + path;
        DIR *dir = opendir(fullPath.c_str());
        if (!dir) {
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"Directory not found\"}", &clientInfo);
          return ESP_OK;
        }

        JsonDocument jsonDoc;
        JsonArray filesArray = jsonDoc["files"].to<JsonArray>();

        struct dirent *entry;
        int fileCount = 0;
        const int maxFiles = 1000; // Prevent infinite loops
        unsigned long startTime = millis();
        const unsigned long timeout = 3000; // 3 second timeout

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

          std::string fullEntryPath = espwifi->lfsMountPoint + entryPath;

          struct stat st;
          if (stat(fullEntryPath.c_str(), &st) == 0) {
            JsonObject fileObj = filesArray.add<JsonObject>();
            fileObj["name"] = entry->d_name;
            fileObj["path"] = entryPath;
            fileObj["isDirectory"] = S_ISDIR(st.st_mode);
            fileObj["size"] = S_ISDIR(st.st_mode) ? 0 : (int64_t)st.st_size;
            fileObj["modified"] = st.st_mtime;
            fileCount++;
          }

          // Yield control periodically
          if (fileCount % 10 == 0) {
            espwifi->yield();
          }
        }

        closedir(dir);

        std::string jsonResponse;
        serializeJson(jsonDoc, jsonResponse);
        (void)espwifi->sendJsonResponse(req, 200, jsonResponse, &clientInfo);
        return ESP_OK;
      },
      true);

  // API endpoint for storage information - GET /api/storage
  registerRoute(
      "/api/storage", HTTP_GET,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        std::string fsParam = getQueryParam(req, "fs");
        if (fsParam.empty()) {
          fsParam = "lfs";
        }

        size_t totalBytes, usedBytes, freeBytes;
        espwifi->getStorageInfo(fsParam, totalBytes, usedBytes, freeBytes);

        JsonDocument jsonDoc;
        jsonDoc["total"] = totalBytes;
        jsonDoc["used"] = usedBytes;
        jsonDoc["free"] = freeBytes;
        jsonDoc["filesystem"] = fsParam;

        std::string jsonResponse;
        serializeJson(jsonDoc, jsonResponse);
        (void)espwifi->sendJsonResponse(req, 200, jsonResponse, &clientInfo);
        return ESP_OK;
      },
      true);

  // API endpoint for creating directories - POST /api/files/mkdir
  registerRoute(
      "/api/files/mkdir", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        // Read request body
        size_t content_len = req->content_len;
        if (content_len > 512) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Request too large\"}", &clientInfo);
          return ESP_OK;
        }

        char *content = (char *)malloc(content_len + 1);
        if (content == nullptr) {
          httpd_resp_send_500(req);
          return ESP_FAIL;
        }

        int ret = httpd_req_recv(req, content, content_len);
        if (ret <= 0) {
          free(content);
          if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
          }
          return ESP_FAIL;
        }

        content[content_len] = '\0';

        JsonDocument jsonDoc;
        DeserializationError error = deserializeJson(jsonDoc, content);
        free(content);

        if (error) {
          (void)espwifi->sendJsonResponse(req, 400, "{\"error\":\"Invalid JSON\"}",
                                          &clientInfo);
          return ESP_OK;
        }

        std::string fsParam = jsonDoc["fs"] | "lfs";
        std::string path = jsonDoc["path"] | "/";
        std::string name = jsonDoc["name"] | "";

        if (name.empty()) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Folder name required\"}", &clientInfo);
          return ESP_OK;
        }

        // Sanitize folder name
        std::string sanitizedName = espwifi->sanitizeFilename(name);

        // Only support LittleFS for now
        if (fsParam != "lfs" || !espwifi->littleFsInitialized) {
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"File system not available\"}", &clientInfo);
          return ESP_OK;
        }

        // Create directory path
        if (path[0] != '/') {
          path = "/" + path;
        }
        std::string dirPath = path;
        if (dirPath.back() != '/') {
          dirPath += "/";
        }
        dirPath += sanitizedName;

        std::string fullDirPath = espwifi->lfsMountPoint + dirPath;

        if (mkDir(fullDirPath)) {
          (void)espwifi->sendJsonResponse(req, 200, "{\"success\":true}",
                                          &clientInfo);
        } else {
          (void)espwifi->sendJsonResponse(
              req, 500, "{\"error\":\"Failed to create directory\"}", &clientInfo);
        }

        return ESP_OK;
      },
      true);

  // API endpoint for file rename - POST /api/files/rename
  registerRoute(
      "/api/files/rename", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        std::string fsParam = getQueryParam(req, "fs");
        std::string oldPath = getQueryParam(req, "oldPath");
        std::string newName = getQueryParam(req, "newName");

        if (fsParam.empty() || oldPath.empty() || newName.empty()) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing parameters\"}", &clientInfo);
          return ESP_OK;
        }

        // Only support LittleFS for now
        if (fsParam != "lfs" || !espwifi->littleFsInitialized) {
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"File system not available\"}", &clientInfo);
          return ESP_OK;
        }

        // Prevent renaming of system files
        if (espwifi->isRestrictedSystemFile(fsParam, oldPath)) {
          (void)espwifi->sendJsonResponse(
              req, 403, "{\"error\":\"Cannot rename system files\"}", &clientInfo);
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
        newPath += newName;

        std::string fullOldPath = espwifi->lfsMountPoint + oldPath;
        std::string fullNewPath = espwifi->lfsMountPoint + newPath;

        if (rename(fullOldPath.c_str(), fullNewPath.c_str()) == 0) {
          espwifi->log(INFO, "📁 Renamed file: %s -> %s", oldPath.c_str(),
                       newName.c_str());
          (void)espwifi->sendJsonResponse(req, 200, "{\"success\":true}",
                                          &clientInfo);
        } else {
          (void)espwifi->sendJsonResponse(
              req, 500, "{\"error\":\"Failed to rename file\"}", &clientInfo);
        }

        return ESP_OK;
      },
      true);

  // API endpoint for file deletion - POST /api/files/delete
  registerRoute(
      "/api/files/delete", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        std::string fsParam = getQueryParam(req, "fs");
        std::string filePath = getQueryParam(req, "path");

        if (fsParam.empty() || filePath.empty()) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing parameters\"}", &clientInfo);
          return ESP_OK;
        }

        // Only support LittleFS for now
        if (fsParam != "lfs" || !espwifi->littleFsInitialized) {
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"File system not available\"}", &clientInfo);
          return ESP_OK;
        }

        std::string fullPath = espwifi->lfsMountPoint + filePath;

        // Check if file exists
        if (!fileExists(fullPath) && !dirExists(fullPath)) {
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"File not found\"}", &clientInfo);
          return ESP_OK;
        }

        // Prevent deletion of system files
        if (espwifi->isRestrictedSystemFile(fsParam, filePath)) {
          (void)espwifi->sendJsonResponse(
              req, 403, "{\"error\":\"Cannot delete system files\"}", &clientInfo);
          return ESP_OK;
        }

        bool deleteSuccess = false;
        if (dirExists(fullPath)) {
          // Delete directory recursively
          deleteSuccess = espwifi->deleteDirectoryRecursive(filePath);
        } else {
          // Delete file
          deleteSuccess = (remove(fullPath.c_str()) == 0);
        }

        if (deleteSuccess) {
          std::string fsName = (fsParam == "sd") ? "SD Card" : "LittleFS";
          if (dirExists(fullPath)) {
            espwifi->log(INFO, "🗑️  Deleted directory on %s: %s",
                         fsName.c_str(), filePath.c_str());
          } else {
            espwifi->log(INFO, "🗑️  Deleted file on %s: %s", fsName.c_str(),
                         filePath.c_str());
          }
          (void)espwifi->sendJsonResponse(req, 200, "{\"success\":true}",
                                          &clientInfo);
        } else {
          (void)espwifi->sendJsonResponse(
              req, 500, "{\"error\":\"Failed to delete file\"}", &clientInfo);
        }

        return ESP_OK;
      },
      true);

  // API endpoint for file upload - POST /api/files/upload
  // Note: ESP-IDF httpd doesn't have built-in multipart support, so we'll
  // handle it manually
  registerRoute(
      "/api/files/upload", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        // Get parameters from query string
        std::string fsParam = getQueryParam(req, "fs");
        std::string path = getQueryParam(req, "path");

        if (fsParam.empty() || path.empty()) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing parameters\"}", &clientInfo);
          return ESP_OK;
        }

        // Only support LittleFS for now
        if (fsParam != "lfs" || !espwifi->littleFsInitialized) {
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"File system not available\"}", &clientInfo);
          return ESP_OK;
        }

        // Check Content-Type header
        size_t content_type_len =
            httpd_req_get_hdr_value_len(req, "Content-Type");
        if (content_type_len == 0) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing Content-Type\"}", &clientInfo);
          return ESP_OK;
        }

        char *content_type = (char *)malloc(content_type_len + 1);
        if (content_type == nullptr) {
          httpd_resp_send_500(req);
          return ESP_FAIL;
        }

        httpd_req_get_hdr_value_str(req, "Content-Type", content_type,
                                    content_type_len + 1);

        // Check if it's multipart/form-data
        std::string contentType(content_type);
        free(content_type);

        if (contentType.find("multipart/form-data") == std::string::npos) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Invalid Content-Type\"}", &clientInfo);
          return ESP_OK;
        }

        // Extract boundary from Content-Type
        size_t boundary_pos = contentType.find("boundary=");
        if (boundary_pos == std::string::npos) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing boundary\"}", &clientInfo);
          return ESP_OK;
        }

        std::string boundary = "--" + contentType.substr(boundary_pos + 9);

        // Read the entire request body (for small files)
        // For large files, we'd need chunked reading, but this is simpler
        size_t content_len = req->content_len;
        if (content_len > 1024 * 1024) { // 1MB limit
          (void)espwifi->sendJsonResponse(req, 413, "{\"error\":\"File too large\"}",
                                          &clientInfo);
          return ESP_OK;
        }

        char *body = (char *)malloc(content_len);
        if (body == nullptr) {
          httpd_resp_send_500(req);
          return ESP_FAIL;
        }

        int received = httpd_req_recv(req, body, content_len);
        if (received <= 0) {
          free(body);
          if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
          }
          return ESP_FAIL;
        }

        // Parse multipart data (simplified - assumes single file upload)
        // Find filename in headers
        std::string bodyStr(body, received);
        free(body);

        size_t filename_pos = bodyStr.find("filename=\"");
        if (filename_pos == std::string::npos) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"No filename found\"}", &clientInfo);
          return ESP_OK;
        }

        filename_pos += 10; // Skip "filename=\""
        size_t filename_end = bodyStr.find("\"", filename_pos);
        if (filename_end == std::string::npos) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Invalid filename\"}", &clientInfo);
          return ESP_OK;
        }

        std::string filename =
            bodyStr.substr(filename_pos, filename_end - filename_pos);

        // Sanitize filename
        std::string sanitizedFilename = espwifi->sanitizeFilename(filename);

        // Find the actual file data (after headers and blank line)
        size_t data_start = bodyStr.find("\r\n\r\n", filename_end);
        if (data_start == std::string::npos) {
          data_start = bodyStr.find("\n\n", filename_end);
          if (data_start == std::string::npos) {
            (void)espwifi->sendJsonResponse(
                req, 400, "{\"error\":\"No file data found\"}", &clientInfo);
            return ESP_OK;
          }
          data_start += 2;
        } else {
          data_start += 4;
        }

        // Find the end boundary
        size_t data_end = bodyStr.find(boundary, data_start);
        if (data_end == std::string::npos) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Invalid multipart data\"}", &clientInfo);
          return ESP_OK;
        }

        // Trim trailing \r\n before boundary
        while (data_end > data_start && (bodyStr[data_end - 1] == '\n' ||
                                         bodyStr[data_end - 1] == '\r')) {
          data_end--;
        }

        // Construct file path
        if (path[0] != '/') {
          path = "/" + path;
        }
        std::string filePath = path;
        if (filePath.back() != '/') {
          filePath += "/";
        }
        filePath += sanitizedFilename;

        std::string fullFilePath = espwifi->lfsMountPoint + filePath;

        // Write file
        FILE *file = fopen(fullFilePath.c_str(), "wb");
        if (!file) {
          (void)espwifi->sendJsonResponse(
              req, 500, "{\"error\":\"Failed to create file\"}", &clientInfo);
          return ESP_OK;
        }

        size_t data_len = data_end - data_start;
        size_t written =
            fwrite(bodyStr.c_str() + data_start, 1, data_len, file);
        // Yield after file I/O to prevent watchdog timeout
        if (data_len > 1024) {
          espwifi->yield();
        }
        fclose(file);

        if (written != data_len) {
          remove(fullFilePath.c_str());
          (void)espwifi->sendJsonResponse(
              req, 500, "{\"error\":\"File write failed\"}", &clientInfo);
          return ESP_OK;
        }

        espwifi->log(INFO, "📁 Uploaded: %s (%zu bytes)", filePath.c_str(),
                     written);
        (void)espwifi->sendJsonResponse(req, 200, "{\"success\":true}",
                                        &clientInfo);
        return ESP_OK;
      },
      true);
}
#endif // ESPWiFi_FileSystem
