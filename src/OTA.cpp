#ifndef ESPWiFi_OTA_H
#define ESPWiFi_OTA_H

#include "ESPWiFi.h"

// OTA state management - these are now class members defined in ESPWiFi.h

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
    logf("📦 OTA MD5 Hash: %s", hash.c_str());
    if (!Update.setMD5(hash.c_str())) {
      logError("Invalid MD5 hash provided");
      this->otaInProgress = false;
      request->send(400, "text/plain", "MD5 parameter invalid");
      return;
    }
  }

  // Start update process based on mode
  if (mode == "fs" || mode == "filesystem") {
    log("📁 Starting filesystem update");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
      this->otaErrorString =
          "Update.begin failed: " + String(Update.getError());
      logError("Failed to start filesystem update");
      logError(this->otaErrorString);
      this->otaInProgress = false;
      request->send(400, "text/plain", this->otaErrorString);
      return;
    }
  } else {
    log("📦 Starting firmware update");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      this->otaErrorString =
          "Update.begin failed: " + String(Update.getError());
      logError("Failed to start firmware update");
      logError(this->otaErrorString);
      this->otaInProgress = false;
      request->send(400, "text/plain", this->otaErrorString);
      return;
    }
  }

  log("✅ OTA update initialized successfully");
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

    logf("📦 Starting upload: %s (%d bytes)\n", filename.c_str(),
         this->otaTotalSize);
  }

  // Write chunked data
  if (len > 0) {
    if (Update.write(data, len) != len) {
      this->otaErrorString =
          "Update.write failed: " + String(Update.getError());
      logError("Failed to write firmware data");
      logError(this->otaErrorString);
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
        logf("📦 Upload progress: %d%%\n", progress);
      }
    }
  }

  if (final) {
    // Finalize the update
    if (Update.end(true)) {
      log("✅ OTA update completed successfully");
      this->otaInProgress = false;

      // Send success response
      AsyncWebServerResponse *response =
          request->beginResponse(200, "text/plain", "OK");
      response->addHeader("Connection", "close");
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
      delay(3000);

      // Restart device after a longer delay to allow UI to receive response
      log("🔄 Restarting Device...");
      ESP.restart();
    } else {
      this->otaErrorString = "Update.end failed: " + String(Update.getError());
      logError("❌ OTA update failed to complete");
      logError(this->otaErrorString);
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
      logError("LittleFS not available for OTA filesystem upload");
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
      logError("File path too long for OTA filesystem upload");
      logf("📁 File path: %s\n", currentFilePath.c_str());
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
      logError("Failed to create file for OTA filesystem upload");
      logf("📁 File path: %s\n", currentFilePath.c_str());
      sendJsonResponse(request, 500, "{\"error\":\"Failed to create file\"}");
      return;
    }

    currentSize = 0;
    logf("📁 Starting OTA filesystem upload: %s\n", currentFilePath.c_str());

    // Special handling for large JS files - skip them for now
    if (currentFilePath.endsWith(".js") &&
        currentFilePath.indexOf("main.") >= 0) {
      logf("📁 Skipping large JS file to prevent crash: %s\n",
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
        logError("Failed to write chunk to OTA filesystem file");
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
      logf("📁 OTA filesystem upload completed: %s (%s)\n",
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

        JsonDocument jsonDoc;
        jsonDoc["firmware_size"] = ESP.getSketchSize();
        jsonDoc["free_space"] = ESP.getFreeSketchSpace();
        jsonDoc["sdk_version"] = String(ESP.getSdkVersion());
        jsonDoc["chip_model"] = String(ESP.getChipModel());
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

        JsonDocument jsonDoc;
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

        // Get mode from parameter (firmware or filesystem)
        String mode = "firmware";
        if (request->hasParam("mode")) {
          mode = request->getParam("mode")->value();
        }

        // Get MD5 hash if provided
        if (request->hasParam("hash")) {
          String hash = request->getParam("hash")->value();
          logf("📦 OTA MD5 Hash: %s", hash.c_str());
          if (!Update.setMD5(hash.c_str())) {
            logError("Invalid MD5 hash provided");
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
          log("📁 Starting filesystem update");
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
            this->otaErrorString =
                "Update.begin failed: " + String(Update.getError());
            logError("Failed to start filesystem update");
            logError(this->otaErrorString);
            this->otaInProgress = false;
            sendJsonResponse(request, 400,
                             "{\"error\":\"" + this->otaErrorString + "\"}");
            return;
          }
        } else {
          log("📦 Starting firmware update");
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            this->otaErrorString =
                "Update.begin failed: " + String(Update.getError());
            logError("Failed to start firmware update");
            logError(this->otaErrorString);
            this->otaInProgress = false;
            sendJsonResponse(request, 400,
                             "{\"error\":\"" + this->otaErrorString + "\"}");
            return;
          }
        }

        log("✅ OTA update initialized successfully");
        sendJsonResponse(request, 200, "{\"success\":true}");
      });

  // API endpoint for resetting OTA state
  webServer->on("/api/ota/reset", HTTP_POST,
                [this](AsyncWebServerRequest *request) {
                  // Handle CORS preflight requests
                  if (request->method() == HTTP_OPTIONS) {
                    handleCorsPreflight(request);
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
        // This will be handled by the upload handler
      },
      [this](AsyncWebServerRequest *request, String filename, size_t index,
             uint8_t *data, size_t len, bool final) {
        handleOTAFileUpload(request, filename, index, data, len, final);
      });

  // Legacy endpoints for backward compatibility
  webServer->on("/ota/start", HTTP_GET, [this](AsyncWebServerRequest *request) {
    handleOTAStart(request);
  });

  webServer->on("/ota/reset", HTTP_GET, [this](AsyncWebServerRequest *request) {
    resetOTAState();
    request->send(200, "text/plain", "OTA state reset");
  });

  webServer->on(
      "/ota/progress", HTTP_GET, [this](AsyncWebServerRequest *request) {
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
      "/ota/upload", HTTP_POST, [this](AsyncWebServerRequest *request) {},
      [this](AsyncWebServerRequest *request, String filename, size_t index,
             uint8_t *data, size_t len, bool final) {
        handleOTAUpdate(request, filename, index, data, len, final);
      });

  webServer->on(
      "/ota/fsupload", HTTP_POST, [this](AsyncWebServerRequest *request) {},
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