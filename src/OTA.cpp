#ifndef ESPWiFi_OTA_H
#define ESPWiFi_OTA_H

#include "ESPWiFi.h"

// Global variables for OTA state management
bool otaInProgress = false;
size_t otaCurrentSize = 0;
size_t otaTotalSize = 0;
String otaErrorString = "";

void ESPWiFi::handleOTAStart(AsyncWebServerRequest *request) {
  // Reset OTA state
  otaCurrentSize = 0;
  otaTotalSize = 0;
  otaErrorString = "";
  otaInProgress = true;

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
      otaInProgress = false;
      request->send(400, "text/plain", "MD5 parameter invalid");
      return;
    }
  }

  // Start update process based on mode
  if (mode == "fs" || mode == "filesystem") {
    log("📁 Starting filesystem update");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
      otaErrorString = "Update.begin failed: " + String(Update.getError());
      logError("Failed to start filesystem update");
      logError(otaErrorString);
      otaInProgress = false;
      request->send(400, "text/plain", otaErrorString);
      return;
    }
  } else {
    log("📦 Starting firmware update");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      otaErrorString = "Update.begin failed: " + String(Update.getError());
      logError("Failed to start firmware update");
      logError(otaErrorString);
      otaInProgress = false;
      request->send(400, "text/plain", otaErrorString);
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
  if (!otaInProgress) {
    // This can happen during polling before OTA starts, handle gracefully
    request->send(200, "application/json",
                  "{\"in_progress\":false,\"progress\":0}");
    return;
  }

  if (index == 0) {
    // Reset progress on first chunk
    otaCurrentSize = 0;

    // Get total size from Content-Length header for progress tracking
    if (request->hasHeader("Content-Length")) {
      otaTotalSize = request->getHeader("Content-Length")->value().toInt();
    }

    logf("📦 Starting upload: %s (%d bytes)\n", filename.c_str(), otaTotalSize);
  }

  // Write chunked data
  if (len > 0) {
    if (Update.write(data, len) != len) {
      otaErrorString = "Update.write failed: " + String(Update.getError());
      logError("Failed to write firmware data");
      logError(otaErrorString);
      Update.abort();
      otaInProgress = false;
      request->send(400, "text/plain", "Failed to write chunked data");
      return;
    }
    otaCurrentSize += len;

    // Progress update every 10%
    if (request->contentLength() > 0) {
      int progress = (otaCurrentSize * 100) / request->contentLength();
      if (progress % 10 == 0) {
        logf("📦 Upload progress: %d%%\n", progress);
      }
    }
  }

  if (final) {
    // Finalize the update
    if (Update.end(true)) {
      log("✅ OTA update completed successfully");
      otaInProgress = false;

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
      otaErrorString = "Update.end failed: " + String(Update.getError());
      logError("❌ OTA update failed to complete");
      logError(otaErrorString);
      Update.abort();
      otaInProgress = false;

      AsyncWebServerResponse *response =
          request->beginResponse(400, "text/plain", otaErrorString);
      response->addHeader("Connection", "close");
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
    }
  }
}

// Reset OTA state (can be called externally if needed)
void ESPWiFi::resetOTAState() {
  otaInProgress = false;
  otaCurrentSize = 0;
  otaTotalSize = 0;
  otaErrorString = "";
  if (Update.isRunning()) {
    Update.abort();
  }
}

void ESPWiFi::handleFSUpdate(AsyncWebServerRequest *request, String filename,
                             size_t index, uint8_t *data, size_t len,
                             bool final) {
  static File fsFile;
  static size_t totalSize = 0;
  static size_t currentSize = 0;

  if (index == 0) {
    // Start of filesystem update
    if (!LittleFS.begin()) {
      log("❌ Failed to start LittleFS");
      request->send(400, "text/plain", "Filesystem not available");
      return;
    }

    // Get total size from Content-Length header
    if (request->hasHeader("Content-Length")) {
      totalSize = request->getHeader("Content-Length")->value().toInt();
    }

    // Create or truncate the file
    filename = "/" + filename;
    fsFile = LittleFS.open(filename, "w");
    if (!fsFile) {
      logError("Failed to create filesystem file: " + filename);
      request->send(400, "text/plain", "Failed to create file");
      return;
    }

    currentSize = 0;
    logf("📁 Starting filesystem update: %s (%d bytes)\n", filename.c_str(),
         totalSize);
  }

  if (fsFile) {
    if (fsFile.write(data, len) != len) {
      logError("Failed to write filesystem data");
      fsFile.close();
      request->send(400, "text/plain", "File write failed");
      return;
    }

    currentSize += len;

    if (final) {
      fsFile.close();
      logf("✅ Filesystem update completed: %s\n", filename.c_str());
      request->send(200, "text/plain", "Filesystem update successful!");
    } else {
      // Progress update
      if (totalSize > 0) {
        int progress = (currentSize * 100) / totalSize;
        if (progress % 10 == 0) { // Log every 10%
          logf("📁 Filesystem update progress: %d%%\n", progress);
        }
      }
    }
  }
}

void ESPWiFi::handleOTAHtml(AsyncWebServerRequest *request) {
  if (LittleFS.exists("/ota.html")) {
    AsyncWebServerResponse *response =
        request->beginResponse(LittleFS, "/ota.html", "text/html");
    response->addHeader("Content-Type", "text/html; charset=UTF-8");
    addCORS(response);
    request->send(response);
  } else {
    request->send(404, "text/plain", "OTA HTML file not found");
  }
}

void ESPWiFi::srvOTA() {
  initWebServer();

  // OTA start endpoint (initialize update)
  webServer->on("/ota/start", HTTP_GET, [this](AsyncWebServerRequest *request) {
    handleOTAStart(request);
  });

  // OTA reset endpoint (reset stuck updates)
  webServer->on("/ota/reset", HTTP_GET, [this](AsyncWebServerRequest *request) {
    resetOTAState();
    request->send(200, "text/plain", "OTA state reset");
  });

  // OTA progress endpoint
  webServer->on(
      "/ota/progress", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument jsonDoc;
        jsonDoc["in_progress"] = otaInProgress;
        jsonDoc["current_size"] = otaCurrentSize;
        jsonDoc["total_size"] = otaTotalSize;
        jsonDoc["progress"] = 0; // Will be calculated below

        // Calculate progress percentage if we have total size and OTA is in
        // progress
        if (otaInProgress && otaTotalSize > 0) {
          int progress = (otaCurrentSize * 100) / otaTotalSize;
          jsonDoc["progress"] = progress;
        }

        String jsonResponse;
        serializeJson(jsonDoc, jsonResponse);
        AsyncWebServerResponse *response =
            request->beginResponse(200, "application/json", jsonResponse);
        addCORS(response);
        request->send(response);
      });

  // OTA upload endpoint (actual file upload)
  webServer->on(
      "/ota/upload", HTTP_POST,
      [this](AsyncWebServerRequest *request) {
        // This will be handled by the upload handler
      },
      [this](AsyncWebServerRequest *request, String filename, size_t index,
             uint8_t *data, size_t len, bool final) {
        handleOTAUpdate(request, filename, index, data, len, final);
      });

  // Filesystem update endpoint
  webServer->on(
      "/ota/fsupload", HTTP_POST,
      [this](AsyncWebServerRequest *request) {
        // This will be handled by the upload handler
      },
      [this](AsyncWebServerRequest *request, String filename, size_t index,
             uint8_t *data, size_t len, bool final) {
        handleFSUpdate(request, filename, index, data, len, final);
      });

  // OTA status endpoint
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

  // OTA update page endpoint
  webServer->on("/ota", HTTP_GET, [this](AsyncWebServerRequest *request) {
    handleOTAHtml(request);
  });
}
#endif // ESPWiFi_OTA_H