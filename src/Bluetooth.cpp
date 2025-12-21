#ifndef ESPWiFi_BLUETOOTH
#define ESPWiFi_BLUETOOTH

#include "ESPWiFi.h"

// ESP32-C3 only supports BLE, not Bluetooth Classic (SPP)
// BluetoothSerial requires Bluetooth Classic support
// Map ESPWiFi_* flags to CONFIG_* for ESP-IDF compatibility
#if defined(ESPWiFi_BT_ENABLED) && !defined(CONFIG_BT_ENABLED)
#define CONFIG_BT_ENABLED 1
#endif
#if defined(ESPWiFi_BLUEDROID_ENABLED) && !defined(CONFIG_BLUEDROID_ENABLED)
#define CONFIG_BLUEDROID_ENABLED 1
#endif
#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED) &&         \
    !defined(CONFIG_IDF_TARGET_ESP32C3)
#include "BluetoothSerial.h"

BluetoothSerial SerialBT;

bool ESPWiFi::startBluetooth() {
  if (!config["bluetooth"]["enabled"].as<bool>()) {
    return false;
  }

  String deviceName = config["deviceName"].as<String>();

  if (!SerialBT.begin(deviceName)) {
    logError("🔵 Bluetooth initialization failed");
    return false;
  }

  log("🔵 Bluetooth Started:");
  logf("\tDevice Name: %s\n", deviceName.c_str());
  logf("\tMAC Address: %s\n", SerialBT.getBtAddressString().c_str());

  config["bluetooth"]["address"] = SerialBT.getBtAddressString();
  return true;
}

void ESPWiFi::stopBluetooth() {
  if (SerialBT.hasClient()) {
    log("🔵 Disconnecting Bluetooth clients");
    SerialBT.disconnect();
    delay(100);
  }

  SerialBT.end();
  log("🔵 Bluetooth Stopped");
}

bool ESPWiFi::isBluetoothConnected() { return SerialBT.hasClient(); }

void ESPWiFi::bluetoothConfigHandler() {
  bool bluetoothEnabled = config["bluetooth"]["enabled"].as<bool>();
  static String lastDeviceName = "";

  String currentDeviceName = config["deviceName"].as<String>();

  // Check if device name changed (only relevant if we had a previous name)
  bool deviceNameChanged =
      (lastDeviceName.length() > 0 && currentDeviceName != lastDeviceName);

  // Check if Bluetooth is currently running (we track this via lastDeviceName)
  bool bluetoothCurrentlyRunning = (lastDeviceName.length() > 0);

  // If device name changed and Bluetooth should be enabled, restart it
  if (deviceNameChanged && bluetoothEnabled && bluetoothCurrentlyRunning) {
    stopBluetooth();
    delay(200);
    startBluetooth();
    lastDeviceName = currentDeviceName;
    return;
  }

  // Start or stop Bluetooth based on config
  if (bluetoothEnabled) {
    // Only start if not already running, or if it's the first time
    if (!bluetoothCurrentlyRunning) {
      startBluetooth();
      lastDeviceName = currentDeviceName;
    }
  } else {
    if (bluetoothCurrentlyRunning) {
      stopBluetooth();
      lastDeviceName = "";
    }
  }
}

void ESPWiFi::scanBluetoothDevices() {
  // Note: ESP32 BluetoothSerial doesn't have built-in scanning
  // This is a placeholder for future implementation
  // For now, scanning would need to be done via ESP-IDF APIs
  log("🔵 Bluetooth scanning not yet implemented");
}

void ESPWiFi::srvBluetooth() {
  initWebServer();

  // Get Bluetooth status
  webServer->on(
      "/api/bluetooth/status", HTTP_OPTIONS,
      [this](AsyncWebServerRequest *request) { handleCorsPreflight(request); });

  webServer->on(
      "/api/bluetooth/status", HTTP_GET,
      [this](AsyncWebServerRequest *request) {
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        JsonDocument jsonDoc;
        bool btEnabled = config["bluetooth"]["enabled"].as<bool>();
        bool btInstalled = config["bluetooth"]["installed"].as<bool>();
        jsonDoc["enabled"] = btEnabled;
        jsonDoc["installed"] = btInstalled;
        jsonDoc["deviceName"] = config["deviceName"].as<String>();

        bool btConnected = false;
        String btAddress = config["bluetooth"]["address"].as<String>();
#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED) &&         \
    !defined(CONFIG_IDF_TARGET_ESP32C3)
        btConnected = SerialBT.hasClient();
        if (btAddress.length() == 0 && btEnabled) {
          btAddress = SerialBT.getBtAddressString();
          if (btAddress.length() > 0) {
            config["bluetooth"]["address"] = btAddress;
          }
        }
#endif
        jsonDoc["address"] = btAddress;
        jsonDoc["connected"] = btConnected;

        String jsonResponse;
        serializeJson(jsonDoc, jsonResponse);
        sendJsonResponse(request, 200, jsonResponse);
      });

  // Enable/Disable Bluetooth
  webServer->addHandler(new AsyncCallbackJsonWebHandler(
      "/api/bluetooth/enable",
      [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        JsonObject reqJson = json.as<JsonObject>();
        bool enabled = reqJson["enabled"] | false;
        config["bluetooth"]["enabled"] = enabled;

        bluetoothConfigHandler();
        saveConfig();

        JsonDocument responseDoc;
        responseDoc["enabled"] = enabled;
        responseDoc["success"] = true;

        String jsonResponse;
        serializeJson(responseDoc, jsonResponse);
        sendJsonResponse(request, 200, jsonResponse);
      }));

  // Send file via Bluetooth
  webServer->on(
      "/api/bluetooth/send", HTTP_OPTIONS,
      [this](AsyncWebServerRequest *request) { handleCorsPreflight(request); });

  webServer->on(
      "/api/bluetooth/send", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        if (!SerialBT.hasClient()) {
          sendJsonResponse(request, 400,
                           "{\"error\":\"Bluetooth not connected\"}");
          return;
        }

        String fsParam = "";
        String filePath = "";

        if (request->hasParam("fs")) {
          fsParam = request->getParam("fs")->value();
        }
        if (request->hasParam("path")) {
          filePath = request->getParam("path")->value();
        }

        if (fsParam.length() == 0 || filePath.length() == 0) {
          sendJsonResponse(request, 400, "{\"error\":\"Missing parameters\"}");
          return;
        }

        FS *filesystem = nullptr;
        if (fsParam == "sd" && sdCardInitialized && sd) {
          filesystem = sd;
        } else if (fsParam == "lfs" && lfs) {
          filesystem = lfs;
        }

        if (!filesystem || !filesystem->exists(filePath)) {
          sendJsonResponse(request, 404, "{\"error\":\"File not found\"}");
          return;
        }

        File file = filesystem->open(filePath, "r");
        if (!file) {
          sendJsonResponse(request, 500, "{\"error\":\"Failed to open file\"}");
          return;
        }

        // Send file via Bluetooth
        size_t fileSize = file.size();
        size_t bytesSent = 0;
        uint8_t buffer[512];

        // Send file header
        String header =
            "FILE:" +
            String(filePath.substring(filePath.lastIndexOf('/') + 1)) + ":" +
            String(fileSize) + "\n";
        SerialBT.print(header);

        // Send file data
        while (file.available() && SerialBT.hasClient()) {
          size_t bytesRead = file.read(buffer, sizeof(buffer));
          if (bytesRead > 0) {
            size_t written = SerialBT.write(buffer, bytesRead);
            bytesSent += written;

            // Yield periodically to prevent watchdog
            if (bytesSent % 4096 == 0) {
              yield();
            }
          }
        }

        file.close();

        JsonDocument responseDoc;
        responseDoc["success"] = true;
        responseDoc["bytesSent"] = bytesSent;
        responseDoc["fileSize"] = fileSize;

        String jsonResponse;
        serializeJson(responseDoc, jsonResponse);
        sendJsonResponse(request, 200, jsonResponse);
      });

  // Receive file via Bluetooth (reads from SerialBT buffer)
  webServer->on(
      "/api/bluetooth/receive", HTTP_GET,
      [this](AsyncWebServerRequest *request) {
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        if (!SerialBT.hasClient()) {
          sendJsonResponse(request, 400,
                           "{\"error\":\"Bluetooth not connected\"}");
          return;
        }

        String fsParam =
            request->hasParam("fs") ? request->getParam("fs")->value() : "sd";
        String savePath = request->hasParam("path")
                              ? request->getParam("path")->value()
                              : "/";

        if (!savePath.startsWith("/")) {
          savePath = "/" + savePath;
        }

        FS *filesystem = nullptr;
        if (fsParam == "sd" && sdCardInitialized && sd) {
          filesystem = sd;
        } else if (fsParam == "lfs" && lfs) {
          filesystem = lfs;
        }

        if (!filesystem) {
          sendJsonResponse(request, 404,
                           "{\"error\":\"File system not available\"}");
          return;
        }

        // Check if there's data available
        if (SerialBT.available() > 0) {
          // For simplicity, we'll read available data and save it
          // In a real implementation, you'd want a protocol for file transfer
          String filename = "bt_received_" + timestampForFilename() + ".bin";
          String filePath =
              savePath + (savePath.endsWith("/") ? "" : "/") + filename;

          File file = filesystem->open(filePath, "w");
          if (!file) {
            sendJsonResponse(request, 500,
                             "{\"error\":\"Failed to create file\"}");
            return;
          }

          size_t bytesReceived = 0;
          uint8_t buffer[512];

          while (SerialBT.available() > 0 &&
                 bytesReceived < 1024 * 1024) { // Max 1MB
            size_t bytesRead = SerialBT.readBytes(buffer, sizeof(buffer));
            file.write(buffer, bytesRead);
            bytesReceived += bytesRead;

            if (bytesReceived % 4096 == 0) {
              yield();
            }
          }

          file.close();

          JsonDocument responseDoc;
          responseDoc["success"] = true;
          responseDoc["bytesReceived"] = bytesReceived;
          responseDoc["filePath"] = filePath;

          String jsonResponse;
          serializeJson(responseDoc, jsonResponse);
          sendJsonResponse(request, 200, jsonResponse);
        } else {
          sendJsonResponse(request, 400, "{\"error\":\"No data available\"}");
        }
      });

  // Disconnect Bluetooth
  webServer->on(
      "/api/bluetooth/disconnect", HTTP_OPTIONS,
      [this](AsyncWebServerRequest *request) { handleCorsPreflight(request); });

  webServer->on("/api/bluetooth/disconnect", HTTP_POST,
                [this](AsyncWebServerRequest *request) {
                  if (!authorized(request)) {
                    sendJsonResponse(request, 401,
                                     "{\"error\":\"Unauthorized\"}");
                    return;
                  }

                  if (SerialBT.hasClient()) {
                    SerialBT.disconnect();
                  }

                  JsonDocument responseDoc;
                  responseDoc["success"] = true;

                  String jsonResponse;
                  serializeJson(responseDoc, jsonResponse);
                  sendJsonResponse(request, 200, jsonResponse);
                });
}

#else // CONFIG_BT_ENABLED && CONFIG_BLUEDROID_ENABLED

// Bluetooth not available - stub implementations
bool ESPWiFi::startBluetooth() {
  log("🔵 Bluetooth not available on this device");
  config["bluetooth"]["enabled"] = false;
  config["bluetooth"]["installed"] = false;
  return false;
}

void ESPWiFi::stopBluetooth() {}

bool ESPWiFi::isBluetoothConnected() { return false; }

void ESPWiFi::bluetoothConfigHandler() {}

void ESPWiFi::scanBluetoothDevices() {}

void ESPWiFi::srvBluetooth() {
  initWebServer();
  webServer->on("/api/bluetooth/status", HTTP_GET,
                [this](AsyncWebServerRequest *request) {
                  if (!authorized(request)) {
                    sendJsonResponse(request, 401,
                                     "{\"error\":\"Unauthorized\"}");
                    return;
                  }
                  JsonDocument jsonDoc;
                  jsonDoc["enabled"] = false;
                  jsonDoc["installed"] = false;
                  jsonDoc["deviceName"] = config["deviceName"].as<String>();
                  jsonDoc["address"] = "";
                  jsonDoc["connected"] = false;

                  String jsonResponse;
                  serializeJson(jsonDoc, jsonResponse);
                  sendJsonResponse(request, 200, jsonResponse);
                });
}
#endif

#endif // ESPWiFi_BLUETOOTH
