#ifndef ESPWiFi_SRV_FILES
#define ESPWiFi_SRV_FILES

#include "ESPWiFi.h"

#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

void ESPWiFi::srvFS() {
  // API endpoint for file browser JSON data - GET /api/files
  registerRoute(
      "/api/files", HTTP_GET, true,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        std::string fsParam = espwifi->getQueryParam(req, "fs");
        if (fsParam.empty()) {
          fsParam = "lfs";
        }

        std::string path = espwifi->getQueryParam(req, "path");
        if (path.empty()) {
          path = "/";
        }
        if (path[0] != '/') {
          path = "/" + path;
        }

        // Only support LittleFS for now (SD card not implemented)
        if (fsParam != "lfs" || !espwifi->littleFsInitialized) {
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"File system not available\"}",
              &clientInfo);
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
      });

  // API endpoint for storage information - GET /api/storage
  registerRoute(
      "/api/storage", HTTP_GET, true,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        std::string fsParam = espwifi->getQueryParam(req, "fs");
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
      });

  // API endpoint for creating directories - POST /api/files/mkdir
  registerRoute(
      "/api/files/mkdir", HTTP_POST, true,
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
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Invalid JSON\"}", &clientInfo);
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
              req, 404, "{\"error\":\"File system not available\"}",
              &clientInfo);
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
      "/api/files/rename", HTTP_POST, true,
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

        // Only support LittleFS for now
        if (fsParam != "lfs" || !espwifi->littleFsInitialized) {
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"File system not available\"}",
              &clientInfo);
          return ESP_OK;
        }

        // Prevent renaming of system files
        if (espwifi->isRestrictedSystemFile(fsParam, oldPath)) {
          (void)espwifi->sendJsonResponse(
              req, 403, "{\"error\":\"Cannot rename system files\"}",
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
      });

  // API endpoint for file deletion - POST /api/files/delete
  registerRoute(
      "/api/files/delete", HTTP_POST, true,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        std::string fsParam = espwifi->getQueryParam(req, "fs");
        std::string filePath = espwifi->getQueryParam(req, "path");

        if (fsParam.empty() || filePath.empty()) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing parameters\"}", &clientInfo);
          return ESP_OK;
        }

        // Only support LittleFS for now
        if (fsParam != "lfs" || !espwifi->littleFsInitialized) {
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"File system not available\"}",
              &clientInfo);
          return ESP_OK;
        }

        std::string fullPath = espwifi->lfsMountPoint + filePath;

        // Check if file exists
        if (!espwifi->fileExists(fullPath) && !espwifi->dirExists(fullPath)) {
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"File not found\"}", &clientInfo);
          return ESP_OK;
        }

        // Prevent deletion of system files
        if (espwifi->isRestrictedSystemFile(fsParam, filePath)) {
          (void)espwifi->sendJsonResponse(
              req, 403, "{\"error\":\"Cannot delete system files\"}",
              &clientInfo);
          return ESP_OK;
        }

        bool deleteSuccess = false;
        if (espwifi->dirExists(fullPath)) {
          // Delete directory recursively
          deleteSuccess = espwifi->deleteDirectoryRecursive(filePath);
        } else {
          // Delete file
          deleteSuccess = (remove(fullPath.c_str()) == 0);
        }

        if (deleteSuccess) {
          std::string fsName = (fsParam == "sd") ? "SD Card" : "LittleFS";
          if (espwifi->dirExists(fullPath)) {
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
      });

  // API endpoint for file upload - POST /api/files/upload
  // Note: ESP-IDF httpd doesn't have built-in multipart support, so we'll
  // handle it manually
  registerRoute(
      "/api/files/upload", HTTP_POST, true,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        // Get parameters from query string
        std::string fsParam = espwifi->getQueryParam(req, "fs");
        std::string path = espwifi->getQueryParam(req, "path");

        if (fsParam.empty() || path.empty()) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing parameters\"}", &clientInfo);
          return ESP_OK;
        }

        // Only support LittleFS for now
        if (fsParam != "lfs" || !espwifi->littleFsInitialized) {
          (void)espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"File system not available\"}",
              &clientInfo);
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
          (void)espwifi->sendJsonResponse(
              req, 413, "{\"error\":\"File too large\"}", &clientInfo);
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
      });
}
#endif // ESPWiFi_SRV_FILES