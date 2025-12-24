#ifndef ESPWiFi_OTA_H
#define ESPWiFi_OTA_H

#include "ESPWiFi.h"
#include "esp_partition.h"

bool ESPWiFi::isOTAEnabled() {
  // Check if OTA partition (app1/ota_1) exists
  // If no OTA partition exists, OTA updates are disabled
  const esp_partition_t *ota_partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
  return (ota_partition != NULL);
}

void ESPWiFi::handleOTAStart(AsyncWebServerRequest *request) {
  // Reset OTA state
  this->otaCurrentSize = 0;
  this->otaTotalSize = 0;
  this->otaErrorString = "";
  this->otaInProgress = true;

  // Get mode from parameter (firmware or filesystem)
  String mode = "firmware";
  if (request->hasParam("mode")) {
    mode = request->getParam("mode")->value();
  }

  // Get MD5 hash if provided
  if (request->hasParam("hash")) {
    String hash = request->getParam("hash")->value();
    log(INFO, "📦 OTA MD5 Hash: %s", hash.c_str());
    if (!Update.setMD5(hash.c_str())) {
      log(ERROR, "Invalid MD5 hash provided");
      this->otaInProgress = false;
      request->send(400, "text/plain", "MD5 parameter invalid");
      return;
    }
  }

  // Start update process based on mode
  if (mode == "fs" || mode == "filesystem") {
    log(INFO, "📁 Starting filesystem update");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
      this->otaErrorString =
          "Update.begin failed: " + String(Update.getError());
      log(ERROR, "Failed to start filesystem update");
      log(ERROR, "%s", this->otaErrorString.c_str());
      this->otaInProgress = false;
      request->send(400, "text/plain", this->otaErrorString);
      return;
    }
  } else {
    // Check if OTA is enabled for firmware updates
    if (!isOTAEnabled()) {
      this->otaErrorString = "OTA firmware updates are disabled. Partition "
                             "table does not support OTA.";
      log(ERROR, "%s", this->otaErrorString.c_str());
      this->otaInProgress = false;
      request->send(400, "text/plain", this->otaErrorString);
      return;
    }
    log(INFO, "📦 Starting firmware update");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      this->otaErrorString =
          "Update.begin failed: " + String(Update.getError());
      log(ERROR, "Failed to start firmware update");
      log(ERROR, "%s", this->otaErrorString.c_str());
      this->otaInProgress = false;
      request->send(400, "text/plain", this->otaErrorString);
      return;
    }
  }

  log(INFO, "✅ OTA update initialized successfully");
  request->send(200, "text/plain", "OK");
}

void ESPWiFi::handleOTAUpdate(AsyncWebServerRequest *request, String filename,
                              size_t index, uint8_t *data, size_t len,
                              bool final) {
  // Check if OTA is in progress
  if (!this->otaInProgress) {
    // This can happen during polling before OTA starts, handle gracefully
    request->send(200, "application/json",
                  "{\"in_progress\":false,\"progress\":0}");
    return;
  }

  if (index == 0) {
    // Reset progress on first chunk
    this->otaCurrentSize = 0;

    // Get total size from Content-Length header for progress tracking
    if (request->hasHeader("Content-Length")) {
      this->otaTotalSize =
          request->getHeader("Content-Length")->value().toInt();
    }

    log(INFO, "📦 Starting upload: %s (%d bytes)", filename.c_str(),
            this->otaTotalSize);
  }

  // Write chunked data
  if (len > 0) {
    if (Update.write(data, len) != len) {
      this->otaErrorString =
          "Update.write failed: " + String(Update.getError());
      log(ERROR, "Failed to write firmware data");
      log(ERROR, "%s", this->otaErrorString.c_str());
      Update.abort();
      this->otaInProgress = false;
      request->send(400, "text/plain", "Failed to write chunked data");
      return;
    }
    this->otaCurrentSize += len;

    // Progress update every 10%
    if (request->contentLength() > 0) {
      int progress = (this->otaCurrentSize * 100) / request->contentLength();
      if (progress % 10 == 0) {
        log(INFO, "📦 Upload progress: %d%%", progress);
      }
    }
  }

  if (final) {
    // Finalize the update
    if (Update.end(true)) {
      log(INFO, "✅ OTA update completed successfully");
      this->otaInProgress = false;

      // Send success response
      AsyncWebServerResponse *response =
          request->beginResponse(200, "text/plain", "OK");
      response->addHeader("Connection", "close");
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
      delay(3000);

      // Restart device after a longer delay to allow UI to receive response
      log(INFO, "🔄 Restarting Device...");
      ESP.restart();
    } else {
      this->otaErrorString = "Update.end failed: " + String(Update.getError());
      log(ERROR, "❌ OTA update failed to complete");
      log(ERROR, "%s", this->otaErrorString.c_str());
      Update.abort();
      this->otaInProgress = false;

      AsyncWebServerResponse *response =
          request->beginResponse(400, "text/plain", this->otaErrorString);
      response->addHeader("Connection", "close");
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
    }
  }
}

// Reset OTA state (can be called externally if needed)
void ESPWiFi::resetOTAState() {
  this->otaInProgress = false;
  this->otaCurrentSize = 0;
  this->otaTotalSize = 0;
  this->otaErrorString = "";
  if (Update.isRunning()) {
    Update.abort();
  }
}

void ESPWiFi::handleOTAFileUpload(AsyncWebServerRequest *request,
                                  String filename, size_t index, uint8_t *data,
                                  size_t len, bool final) {
  // Static variables to maintain state across multiple calls
  static File currentFile;
  static size_t currentSize = 0;
  static String currentFilePath = "";

  if (index == 0) {
    // First chunk - initialize upload
    if (!lfs) {
      log(ERROR, "LittleFS not available for OTA filesystem upload");
      sendJsonResponse(request, 500, "{\"error\":\"LittleFS not available\"}");
      return;
    }

    // Get the target path from request parameter
    String targetPath = "/";
    if (request->hasParam("path")) {
      targetPath = request->getParam("path")->value();
      // Ensure path starts with /
      if (!targetPath.startsWith("/")) {
        targetPath = "/" + targetPath;
      }
    }

    // Create the full file path: targetPath + filename
    // This preserves folder structure from the frontend
    currentFilePath =
        targetPath + (targetPath.endsWith("/") ? "" : "/") + filename;

    // Strip the first directory level (e.g., "/data/static/css/main.css" ->
    // "/static/css/main.css")
    if (currentFilePath.startsWith("/") &&
        currentFilePath.indexOf("/", 1) > 0) {
      int firstSlash = currentFilePath.indexOf("/", 1);
      // Remove the first directory (e.g., "/data")
      currentFilePath = currentFilePath.substring(firstSlash);
    }

    // Validate file path length
    if (currentFilePath.length() > 100) {
      log(ERROR, "File path too long for OTA filesystem upload");
      log(INFO, "📁 File path: %s", currentFilePath.c_str());
      sendJsonResponse(request, 500, "{\"error\":\"File path too long\"}");
      return;
    }

    // Pre-create known directory structure for dashboard files
    if (currentFilePath.startsWith("/static/")) {
      if (!lfs->exists("/static")) {
        lfs->mkdir("/static");
      }
      if (currentFilePath.startsWith("/static/css/") &&
          !lfs->exists("/static/css")) {
        lfs->mkdir("/static/css");
      }
      if (currentFilePath.startsWith("/static/js/") &&
          !lfs->exists("/static/js")) {
        lfs->mkdir("/static/js");
      }
    }

    // Create the file
    currentFile = lfs->open(currentFilePath, "w");
    if (!currentFile) {
      log(ERROR, "Failed to create file for OTA filesystem upload");
      log(INFO, "📁 File path: %s", currentFilePath.c_str());
      sendJsonResponse(request, 500, "{\"error\":\"Failed to create file\"}");
      return;
    }

    currentSize = 0;
    log(INFO, "📁 Starting OTA filesystem upload: %s", currentFilePath.c_str());

    // Special handling for large JS files - skip them for now
    if (currentFilePath.endsWith(".js") &&
        currentFilePath.indexOf("main.") >= 0) {
      log(INFO, "📁 Skipping large JS file to prevent crash: %s",
              currentFilePath.c_str());
      request->send(200, "text/plain", "OK");
      return;
    }
  }

  // Write data chunk with optimized handling for large files
  if (currentFile && len > 0 && data != nullptr) {
    // Write data in smaller chunks to prevent memory issues
    size_t bytesWritten = 0;
    size_t remaining = len;
    uint8_t *ptr = data;

    while (remaining > 0) {
      size_t chunkSize = (remaining > 512) ? 512 : remaining;
      size_t written = currentFile.write(ptr, chunkSize);

      if (written != chunkSize) {
        log(ERROR, "Failed to write chunk to OTA filesystem file");
        currentFile.close();
        sendJsonResponse(request, 500, "{\"error\":\"File write failed\"}");
        return;
      }

      bytesWritten += written;
      remaining -= written;
      ptr += written;

      // Small delay between chunks for large files
      if (remaining > 0 && chunkSize >= 512) {
        delay(1);
        yield();
      }
    }

    currentSize += bytesWritten;
  }

  if (final) {
    // Last chunk - close file and send response
    if (currentFile) {
      currentFile.close();
      String sizeStr =
          (currentSize > 0) ? bytesToHumanReadable(currentSize) : "0 B";
      log(INFO, "📁 OTA filesystem upload completed: %s (%s)",
              currentFilePath.c_str(), sizeStr.c_str());
    }

    // Reset static variables for next upload
    currentFilePath = "";
    currentSize = 0;

    request->send(200, "text/plain", "OK");
  }
}

void ESPWiFi::srvOTA() {
  initWebServer();

  // API endpoint for OTA status information
  webServer->on(
      "/api/ota/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        // Handle CORS preflight requests
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        JsonDocument jsonDoc;
        jsonDoc["firmware_size"] = ESP.getSketchSize();
        jsonDoc["free_space"] = ESP.getFreeSketchSpace();
        jsonDoc["sdk_version"] = String(ESP.getSdkVersion());
        jsonDoc["chip_model"] = String(ESP.getChipModel());
        jsonDoc["ota_enabled"] = isOTAEnabled();
        jsonDoc["in_progress"] = this->otaInProgress;
        jsonDoc["current_size"] = this->otaCurrentSize;
        jsonDoc["total_size"] = this->otaTotalSize;
        jsonDoc["progress"] = 0; // Will be calculated below

        // Calculate progress percentage if we have total size and OTA
        // is in progress
        if (this->otaInProgress && this->otaTotalSize > 0) {
          int progress = (this->otaCurrentSize * 100) / this->otaTotalSize;
          jsonDoc["progress"] = progress;
        }

        String jsonResponse;
        serializeJson(jsonDoc, jsonResponse);
        sendJsonResponse(request, 200, jsonResponse);
      });

  // API endpoint for OTA progress
  webServer->on(
      "/api/ota/progress", HTTP_GET, [this](AsyncWebServerRequest *request) {
        // Handle CORS preflight requests
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        JsonDocument jsonDoc;
        jsonDoc["ota_enabled"] = isOTAEnabled();
        jsonDoc["in_progress"] = this->otaInProgress;
        jsonDoc["current_size"] = this->otaCurrentSize;
        jsonDoc["total_size"] = this->otaTotalSize;
        jsonDoc["progress"] = 0; // Will be calculated below

        // Calculate progress percentage if we have total size and OTA
        // is in progress
        if (this->otaInProgress && this->otaTotalSize > 0) {
          int progress = (this->otaCurrentSize * 100) / this->otaTotalSize;
          jsonDoc["progress"] = progress;
        }

        String jsonResponse;
        serializeJson(jsonDoc, jsonResponse);
        sendJsonResponse(request, 200, jsonResponse);
      });

  // API endpoint for starting OTA update
  webServer->on(
      "/api/ota/start", HTTP_POST, [this](AsyncWebServerRequest *request) {
        // Handle CORS preflight requests
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        // Get mode from parameter (firmware or filesystem)
        String mode = "firmware";
        if (request->hasParam("mode")) {
          mode = request->getParam("mode")->value();
        }

        // Check if OTA is enabled for firmware updates
        if (mode != "fs" && mode != "filesystem" && !isOTAEnabled()) {
          sendJsonResponse(request, 400,
                           "{\"error\":\"OTA firmware updates are disabled. "
                           "Partition table does not support OTA.\"}");
          return;
        }

        // Get MD5 hash if provided
        if (request->hasParam("hash")) {
          String hash = request->getParam("hash")->value();
          log(INFO, "📦 OTA MD5 Hash: %s", hash.c_str());
          if (!Update.setMD5(hash.c_str())) {
            log(ERROR, "Invalid MD5 hash provided");
            sendJsonResponse(request, 400, "{\"error\":\"Invalid MD5 hash\"}");
            return;
          }
        }

        // Reset OTA state
        this->otaCurrentSize = 0;
        this->otaTotalSize = 0;
        this->otaErrorString = "";
        this->otaInProgress = true;

        // Start update process based on mode
        if (mode == "fs" || mode == "filesystem") {
          log(INFO, "📁 Starting filesystem update");
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
            this->otaErrorString =
                "Update.begin failed: " + String(Update.getError());
            log(ERROR, "Failed to start filesystem update");
            log(ERROR, "%s", this->otaErrorString.c_str());
            this->otaInProgress = false;
            sendJsonResponse(request, 400,
                             "{\"error\":\"" + this->otaErrorString + "\"}");
            return;
          }
        } else {
          log(INFO, "📦 Starting firmware update");
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            this->otaErrorString =
                "Update.begin failed: " + String(Update.getError());
            log(ERROR, "Failed to start firmware update");
            log(ERROR, "%s", this->otaErrorString.c_str());
            this->otaInProgress = false;
            sendJsonResponse(request, 400,
                             "{\"error\":\"" + this->otaErrorString + "\"}");
            return;
          }
        }

        log(INFO, "✅ OTA update initialized successfully");
        sendJsonResponse(request, 200, "{\"success\":true}");
      });

  // API endpoint for resetting OTA state
  webServer->on(
      "/api/ota/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        // Handle CORS preflight requests
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        resetOTAState();
        sendJsonResponse(request, 200, "{\"success\":true}");
      });

  // API endpoint for firmware upload using OTA pattern
  webServer->on(
      "/api/ota/upload", HTTP_POST,
      [this](AsyncWebServerRequest *request) {
        // Handle CORS preflight requests
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }
        // This will be handled by the upload handler
      },
      [this](AsyncWebServerRequest *request, String filename, size_t index,
             uint8_t *data, size_t len, bool final) {
        handleOTAUpdate(request, filename, index, data, len, final);
      });

  // API endpoint for filesystem upload using filesystem pattern
  webServer->on(
      "/api/ota/filesystem", HTTP_POST,
      [this](AsyncWebServerRequest *request) {
        // Handle CORS preflight requests
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }
        // This will be handled by the upload handler
      },
      [this](AsyncWebServerRequest *request, String filename, size_t index,
             uint8_t *data, size_t len, bool final) {
        handleOTAFileUpload(request, filename, index, data, len, final);
      });

  // Legacy endpoints for backward compatibility
  webServer->on("/ota/start", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!authorized(request)) {
      sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
      return;
    }
    handleOTAStart(request);
  });

  webServer->on("/ota/reset", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!authorized(request)) {
      sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
      return;
    }
    resetOTAState();
    request->send(200, "text/plain", "OTA state reset");
  });

  webServer->on(
      "/ota/progress", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }
        JsonDocument jsonDoc;
        jsonDoc["in_progress"] = this->otaInProgress;
        jsonDoc["current_size"] = this->otaCurrentSize;
        jsonDoc["total_size"] = this->otaTotalSize;
        jsonDoc["progress"] = 0;

        if (this->otaInProgress && this->otaTotalSize > 0) {
          int progress = (this->otaCurrentSize * 100) / this->otaTotalSize;
          jsonDoc["progress"] = progress;
        }

        String jsonResponse;
        serializeJson(jsonDoc, jsonResponse);
        AsyncWebServerResponse *response =
            request->beginResponse(200, "application/json", jsonResponse);
        addCORS(response);
        request->send(response);
      });

  webServer->on(
      "/ota/upload", HTTP_POST,
      [this](AsyncWebServerRequest *request) {
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }
      },
      [this](AsyncWebServerRequest *request, String filename, size_t index,
             uint8_t *data, size_t len, bool final) {
        handleOTAUpdate(request, filename, index, data, len, final);
      });

  webServer->on(
      "/ota/fsupload", HTTP_POST,
      [this](AsyncWebServerRequest *request) {
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }
      },
      [this](AsyncWebServerRequest *request, String filename, size_t index,
             uint8_t *data, size_t len, bool final) {
        handleOTAFileUpload(request, filename, index, data, len, final);
      });

  webServer->on(
      "/ota/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument jsonDoc;
        jsonDoc["firmware_size"] = ESP.getSketchSize();
        jsonDoc["free_space"] = ESP.getFreeSketchSpace();
        jsonDoc["sdk_version"] = String(ESP.getSdkVersion());
        jsonDoc["chip_model"] = String(ESP.getChipModel());
        jsonDoc["ota_start_url"] =
            "http://" + WiFi.localIP().toString() + "/ota/start";
        jsonDoc["ota_upload_url"] =
            "http://" + WiFi.localIP().toString() + "/ota/upload";
        jsonDoc["fs_update_url"] =
            "http://" + WiFi.localIP().toString() + "/ota/fsupload";

        String jsonResponse;
        serializeJson(jsonDoc, jsonResponse);
        AsyncWebServerResponse *response =
            request->beginResponse(200, "application/json", jsonResponse);
        addCORS(response);
        request->send(response);
      });
}
#endif // ESPWiFi_OTA_H