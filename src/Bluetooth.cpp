#ifndef ESPWiFi_BLUETOOTH
#define ESPWiFi_BLUETOOTH

#include "ESPWiFi.h"

// Convert to BLE for ESP32-S3 and ESP32-C3 compatibility using NimBLE
// CONFIG_BT_ENABLED comes from sdkconfig (board_build.sdkconfig in
// platformio.ini)
#if defined(CONFIG_BT_ENABLED)
#include "NimBLEDevice.h"

// BLE UUIDs for the file transfer service
#define SERVICE_UUID "0000ff00-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_TX_UUID "0000ff01-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_RX_UUID "0000ff02-0000-1000-8000-00805f9b34fb"

NimBLEServer *pServer = nullptr;
NimBLECharacteristic *pTxCharacteristic = nullptr;
NimBLECharacteristic *pRxCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;
String bleDeviceName = "";

// Callback for device connection events
class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) {
    deviceConnected = true;
    oldDeviceConnected = true;
    Serial.printf("[BLE] 🔵 Device Connected: %s\n",
                  connInfo.getAddress().toString().c_str());

    // Update connection parameters for better reliability
    // Args: connection handle, min interval, max interval, latency, timeout
    // Units: Min/Max Intervals: 1.25ms increments, Latency: intervals to skip,
    // Timeout: 10ms increments
    pServer->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
  }

  void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo,
                    int reason) {
    deviceConnected = false;
    Serial.printf("[BLE] 🔵 Device Disconnected: %s (reason: %d)\n",
                  connInfo.getAddress().toString().c_str(), reason);
    // Restart advertising when disconnected
    delay(500);
    NimBLEDevice::startAdvertising();
    Serial.println("[BLE] 🔵 Advertising Restarted");
  }
};

// Callback for RX characteristic (data received from client)
class MyCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *pCharacteristic,
               NimBLEConnInfo &connInfo) {
    // Data received, handled in receive endpoint
  }
};

bool ESPWiFi::startBluetooth() {
  if (bluetoothStarted) {
    return true; // Already started
  }

  if (!config["bluetooth"]["enabled"].as<bool>()) {
    log("🔵 Bluetooth Disabled");
    return false;
  }

  // ESP32-C3 BLE and WiFi share the same radio, so WiFi must be initialized
  // even if WiFi is disabled. Initialize WiFi in STA mode for BLE
  // compatibility.
  if (!config["wifi"]["enabled"].as<bool>()) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
  }

  bleDeviceName = config["deviceName"].as<String>();

  // Following NimBLE-Arduino New User Guide quick start pattern:
  // https://github.com/h2zero/NimBLE-Arduino/blob/master/docs/New_user_guide.md#creating-a-server

  // Initialize NimBLE device
  if (NimBLEDevice::getInitialized()) {
    NimBLEDevice::deinit(true);
    delay(100);
  }
  NimBLEDevice::init(bleDeviceName.c_str());

  // Create BLE server
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create BLE service
  NimBLEService *pService = pServer->createService(SERVICE_UUID);

  // Create TX characteristic (for sending data to client)
  pTxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_TX_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pTxCharacteristic->setCallbacks(new MyCallbacks());

  // Create RX characteristic (for receiving data from client)
  pRxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_RX_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  // Start the service
  pService->start();

  // Start advertising - following guide order exactly
  // Optimized for macOS/iOS discoverability
  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(
      SERVICE_UUID); // advertise the UUID of our service
  pAdvertising->setName(bleDeviceName.c_str()); // advertise the device name

  // Enable scan response for better macOS/iOS discoverability
  // This allows more data to be sent in response to scan requests
  pAdvertising->setScanResponse(true);

  // Start advertising
  if (!pAdvertising->start()) {
    log("⚠️  Failed to start BLE advertising");
    return false;
  }

  // Give advertising time to start
  delay(200);

  // Logging
  log("🔵 Bluetooth LE Started:");
  logf("\tDevice Name: %s\n", bleDeviceName.c_str());
  String macAddress = String(NimBLEDevice::getAddress().toString().c_str());
  logf("\tMAC Address: %s\n", macAddress.c_str());
  logf("\tService UUID: %s\n", SERVICE_UUID);
  log("🔵 Advertising active - device should be discoverable");
  log("🔵 Waiting for connections...");

  config["bluetooth"]["address"] = macAddress;
  bluetoothStarted = true;
  return true;
}

void ESPWiFi::stopBluetooth() {
  if (!bluetoothStarted) {
    return; // Already stopped
  }

  if (deviceConnected) {
    log("🔵 Disconnecting BLE clients");
    // In NimBLE, deinit will disconnect all clients
    delay(100);
  }

  NimBLEDevice::deinit(true);
  pServer = nullptr;
  pTxCharacteristic = nullptr;
  pRxCharacteristic = nullptr;
  deviceConnected = false;
  oldDeviceConnected = false;
  bluetoothStarted = false;
  log("🔵 Bluetooth LE Stopped");
}

bool ESPWiFi::isBluetoothConnected() { return deviceConnected; }

void ESPWiFi::bluetoothConfigHandler() {
  bool btInstalled = config["bluetooth"]["installed"].as<bool>();
  bool bluetoothEnabled = config["bluetooth"]["enabled"].as<bool>();

  // If Bluetooth is not installed, disable it and return
  if (!btInstalled) {
    if (bluetoothEnabled) {
      config["bluetooth"]["enabled"] = false;
    }
    if (bluetoothStarted) {
      stopBluetooth();
    }
    return;
  }

  static String lastDeviceName = "";
  String currentDeviceName = config["deviceName"].as<String>();

  // Check if device name changed (only relevant if we had a previous name)
  bool deviceNameChanged =
      (lastDeviceName.length() > 0 && currentDeviceName != lastDeviceName);

  // If device name changed and Bluetooth should be enabled, restart it
  if (deviceNameChanged && bluetoothEnabled && bluetoothStarted) {
    stopBluetooth();
    delay(200);
    startBluetooth();
    lastDeviceName = currentDeviceName;
    return;
  }

  // Start or stop Bluetooth based on config
  if (bluetoothEnabled) {
    // Only start if not already running
    if (!bluetoothStarted) {
      startBluetooth();
      lastDeviceName = currentDeviceName;
    }
  } else {
    if (bluetoothStarted) {
      stopBluetooth();
      lastDeviceName = "";
    }
  }
}

void ESPWiFi::scanBluetoothDevices() {
  log("🔵 BLE scanning not yet implemented");
}

void ESPWiFi::srvBluetooth() {
  initWebServer();

  // Get Bluetooth status
  webServer->on(
      "/api/bluetooth/status", HTTP_GET,
      [this](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        JsonDocument jsonDoc;
        bool btInstalled = config["bluetooth"]["installed"].as<bool>();
        bool btEnabled = config["bluetooth"]["enabled"].as<bool>();

        // If Bluetooth is not installed, it cannot be enabled
        if (!btInstalled) {
          btEnabled = false;
        }

        jsonDoc["enabled"] = btEnabled;
        jsonDoc["installed"] = btInstalled;
        jsonDoc["deviceName"] = config["deviceName"].as<String>();

        bool btConnected = false;
        String btAddress = "";

        // Only get connection status and address if Bluetooth is installed
        // and enabled
        if (btInstalled && btEnabled) {
          btConnected = deviceConnected;
          btAddress = config["bluetooth"]["address"].as<String>();

          // Try to get address from BLE if not in config (only if BLE is
          // initialized)
          if (btAddress.length() == 0 && pServer != nullptr) {
            btAddress = String(NimBLEDevice::getAddress().toString().c_str());
            if (btAddress.length() > 0) {
              config["bluetooth"]["address"] = btAddress;
            }
          }
        }

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

  // Send file via BLE
  webServer->on(
      "/api/bluetooth/send", HTTP_OPTIONS,
      [this](AsyncWebServerRequest *request) { handleCorsPreflight(request); });

  webServer->on(
      "/api/bluetooth/send", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        if (!deviceConnected || pTxCharacteristic == nullptr) {
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

        // Send file via BLE
        size_t fileSize = file.size();
        size_t bytesSent = 0;
        uint8_t buffer[512]; // BLE supports up to 512 bytes per packet

        String filename = filePath.substring(filePath.lastIndexOf('/') + 1);
        log("🔵 Sending file via BLE:");
        logf("\tFile: %s\n", filename.c_str());
        logf("\tSize: %d bytes\n", fileSize);

        // Send file header: FILE:filename:size\n
        String header = "FILE:" + filename + ":" + String(fileSize) + "\n";
        pTxCharacteristic->setValue((uint8_t *)header.c_str(), header.length());
        pTxCharacteristic->notify();
        delay(20); // Small delay for BLE transmission

        // Send file data in chunks
        while (file.available() && deviceConnected) {
          size_t bytesRead = file.read(buffer, sizeof(buffer));
          if (bytesRead > 0) {
            pTxCharacteristic->setValue(buffer, bytesRead);
            pTxCharacteristic->notify();
            bytesSent += bytesRead;
            delay(20); // Small delay for BLE transmission

            // Yield periodically to prevent watchdog
            if (bytesSent % 4096 == 0) {
              yield();
            }
          }
        }

        file.close();

        logf("🔵 File sent: %d/%d bytes\n", bytesSent, fileSize);

        JsonDocument responseDoc;
        responseDoc["success"] = true;
        responseDoc["bytesSent"] = bytesSent;
        responseDoc["fileSize"] = fileSize;

        String jsonResponse;
        serializeJson(responseDoc, jsonResponse);
        sendJsonResponse(request, 200, jsonResponse);
      });

  // Receive file via BLE (reads from RX characteristic)
  webServer->on(
      "/api/bluetooth/receive", HTTP_GET,
      [this](AsyncWebServerRequest *request) {
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        if (!deviceConnected || pRxCharacteristic == nullptr) {
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

        log("🔵 Receiving file via BLE:");
        logf("\tSave Path: %s\n", savePath.c_str());
        logf("\tFile System: %s\n", fsParam.c_str());

        // Read available data from RX characteristic
        NimBLEAttValue rxValue = pRxCharacteristic->getValue();
        if (rxValue.length() > 0) {
          logf("🔵 Received %d bytes from BLE\n", rxValue.length());
          String filename = "bt_received_" + timestampForFilename() + ".bin";
          String filePath =
              savePath + (savePath.endsWith("/") ? "" : "/") + filename;

          File file = filesystem->open(filePath, "w");
          if (!file) {
            sendJsonResponse(request, 500,
                             "{\"error\":\"Failed to create file\"}");
            return;
          }

          size_t bytesReceived = file.write(rxValue.data(), rxValue.length());
          file.close();

          logf("🔵 File received: %d bytes saved to %s\n", bytesReceived,
               filePath.c_str());

          // Clear the characteristic value after reading
          pRxCharacteristic->setValue((uint8_t *)"", 0);

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

                  // For BLE, clients disconnect themselves
                  // We can't force disconnect without tracking connection IDs
                  // Just return success - the client will disconnect when ready
                  JsonDocument responseDoc;
                  responseDoc["success"] = true;
                  responseDoc["message"] = "BLE clients disconnect themselves";

                  String jsonResponse;
                  serializeJson(responseDoc, jsonResponse);
                  sendJsonResponse(request, 200, jsonResponse);
                });
}

#else // CONFIG_BT_ENABLED

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
