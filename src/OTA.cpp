#include "ESPWiFi.h"

void ESPWiFi::handleOTAUpdate(AsyncWebServerRequest *request, String filename,
                              size_t index, uint8_t *data, size_t len,
                              bool final) {
  static size_t totalSize = 0;
  static size_t currentSize = 0;

  if (index == 0) {
    // Start of update
    currentSize = 0;

    // Get total size from Content-Length header
    if (request->hasHeader("Content-Length")) {
      totalSize = request->getHeader("Content-Length")->value().toInt();
    } else {
      // Fallback to available space
      totalSize = ESP.getFreeSketchSpace();
    }

    if (Update.begin(totalSize)) {
      logf("📦 Starting firmware update: %s (%d bytes)", filename.c_str(),
           totalSize);
    } else {
      logError("Failed to start firmware update");
      request->send(400, "text/plain", "Update failed to start");
      return;
    }
  }

  if (Update.write(data, len) != len) {
    logError("ailed to write firmware data");
    request->send(400, "text/plain", "Update write failed");
    return;
  }

  currentSize += len;

  if (final) {
    if (Update.end()) {
      log("✅ Firmware update completed successfully");
      log("🔄 Restarting device...");
      request->send(200, "text/plain",
                    "Update successful! Device will restart.");
      delay(1000);
      ESP.restart();
    } else {
      logError("❌ Firmware update failed to complete");
      request->send(400, "text/plain", "Update failed to complete");
    }
  } else {
    // Progress update
    int progress = (currentSize * 100) / totalSize;
    if (progress % 10 == 0) { // Log every 10%
      logf("📦 Firmware update progress: %d%%", progress);
    }
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
      logError("❌ Failed to start LittleFS");
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
      logf("✅ Filesystem update completed: %s", filename.c_str());
      request->send(200, "text/plain", "Filesystem update successful!");
    } else {
      // Progress update
      if (totalSize > 0) {
        int progress = (currentSize * 100) / totalSize;
        if (progress % 10 == 0) { // Log every 10%
          logf("📁 Filesystem update progress: %d%%", progress);
        }
      }
    }
  }
}