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
      StreamString str;
      Update.printError(str);
      otaErrorString = str.c_str();
      logError("Failed to start filesystem update");
      logError(otaErrorString);
      otaInProgress = false;
      request->send(400, "text/plain", otaErrorString);
      return;
    }
  } else {
    log("📦 Starting firmware update");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      StreamString str;
      Update.printError(str);
      otaErrorString = str.c_str();
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
      StreamString str;
      Update.printError(str);
      otaErrorString = str.c_str();
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

      // Restart device after a short delay
      log("🔄 Restarting device in 2 seconds...");
      delay(2000);
      ESP.restart();
    } else {
      StreamString str;
      Update.printError(str);
      otaErrorString = str.c_str();
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