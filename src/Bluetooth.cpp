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

// Track multiple connection handles (ESP32 typically supports 5-10 simultaneous
// connections)
#define MAX_BLE_CONNECTIONS 10
uint16_t connectionHandles[MAX_BLE_CONNECTIONS];
uint8_t connectionCount = 0;

// Forward declaration
class ESPWiFi;

// Callback for device connection events
class MyServerCallbacks : public NimBLEServerCallbacks {
private:
  ESPWiFi *espWiFi;

public:
  MyServerCallbacks(ESPWiFi *esp) : espWiFi(esp) {}

  void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) {
    uint16_t handle = connInfo.getConnHandle();

    // Add connection handle to list if not at max
    if (connectionCount < MAX_BLE_CONNECTIONS) {
      connectionHandles[connectionCount++] = handle;
      deviceConnected = true;
      oldDeviceConnected = true;

      // Update config with connection status
      if (espWiFi) {
        espWiFi->config["bluetooth"]["connected"] = true;
        espWiFi->config["bluetooth"]["connectionCount"] = (int)connectionCount;
      }
    }

    String address = String(connInfo.getAddress().toString().c_str());

    // Log directly to Serial first as fallback
    Serial.print("[BLE] Device Connected - Address: ");
    Serial.print(address);
    Serial.print(", Connections: ");
    Serial.println(connectionCount);

    if (espWiFi) {
      espWiFi->log("📱 Bluetooth Device Connected");
      espWiFi->logf("\tAddress: %s\n", address.c_str());
      espWiFi->logf("\tConnection Handle: %d\n", handle);
      espWiFi->logf("\tTotal Connections: %d\n", connectionCount);
    } else {
      Serial.println("[BLE] Warning: ESPWiFi pointer is null!");
    }

    // Update connection parameters for better reliability
    pServer->updateConnParams(handle, 24, 48, 0, 180);

    // Continue advertising to allow more connections (unless at max)
    if (connectionCount < MAX_BLE_CONNECTIONS) {
      NimBLEDevice::startAdvertising();
    }
  }

  void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo,
                    int reason) {
    uint16_t handle = connInfo.getConnHandle();

    // Remove connection handle from list
    for (uint8_t i = 0; i < connectionCount; i++) {
      if (connectionHandles[i] == handle) {
        // Shift remaining handles down
        for (uint8_t j = i; j < connectionCount - 1; j++) {
          connectionHandles[j] = connectionHandles[j + 1];
        }
        connectionCount--;
        break;
      }
    }

    deviceConnected = (connectionCount > 0);

    // Update config with connection status
    if (espWiFi) {
      espWiFi->config["bluetooth"]["connected"] = deviceConnected;
      espWiFi->config["bluetooth"]["connectionCount"] = (int)connectionCount;
    }

    String address = String(connInfo.getAddress().toString().c_str());

    // Log directly to Serial first as fallback
    Serial.print("[BLE] Device Disconnected - Address: ");
    Serial.print(address);
    Serial.print(", Reason: ");
    Serial.print(reason);
    Serial.print(", Remaining Connections: ");
    Serial.println(connectionCount);

    if (espWiFi) {
      espWiFi->log("📱 Bluetooth Device Disconnected");
      espWiFi->logf("\tAddress: %s\n", address.c_str());
      espWiFi->logf("\tReason: %d\n", reason);
      espWiFi->logf("\tRemaining Connections: %d\n", connectionCount);
    } else {
      Serial.println("[BLE] Warning: ESPWiFi pointer is null!");
    }

    // Restart advertising when disconnected to allow new connections
    delay(500);
    NimBLEDevice::startAdvertising();
  }
};

// Callback for characteristics
class MyCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) {
    // Characteristic read
  }

  void onWrite(NimBLECharacteristic *pCharacteristic,
               NimBLEConnInfo &connInfo) {
    // Data received, handled in receive endpoint
  }

  void onSubscribe(NimBLECharacteristic *pCharacteristic,
                   NimBLEConnInfo &connInfo, uint16_t subValue) {
    // Client subscribed to notifications/indications
  }
};

bool ESPWiFi::startBluetooth() {
  if (bluetoothStarted) {
    return true;
  }

  if (!config["bluetooth"]["enabled"].as<bool>()) {
    log("📱 Bluetooth Disabled");
    return false;
  }

  // ESP32 BLE and WiFi share the same radio, so WiFi must be initialized
  // even if WiFi is disabled. Initialize WiFi in STA mode for BLE
  // compatibility.
  if (!config["wifi"]["enabled"].as<bool>()) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
  }

  bleDeviceName = config["deviceName"].as<String>();

  // Following NimBLE-Arduino Server example pattern:
  // https://github.com/h2zero/NimBLE-Arduino/blob/master/examples/NimBLE_Server/NimBLE_Server.ino

  // Initialize NimBLE and set the device name
  NimBLEDevice::init(bleDeviceName.c_str());

  // Create BLE server
  pServer = NimBLEDevice::createServer();
  MyServerCallbacks *serverCallbacks = new MyServerCallbacks(this);
  pServer->setCallbacks(serverCallbacks);

  // Verify callback setup
  log("📱 Bluetooth Callbacks initialized");

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

  // Start the service when finished creating all Characteristics
  pService->start();

  // Create an advertising instance and add the service to the advertised data
  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(bleDeviceName.c_str());
  pAdvertising->addServiceUUID(SERVICE_UUID);
  // If your device is battery powered you may consider setting scan response
  // to false as it will extend battery life at the expense of less data sent.
  pAdvertising->setScanResponse(true);
  pAdvertising->start();

  // Store MAC address and mode in config
  String macAddress = String(NimBLEDevice::getAddress().toString().c_str());
  config["bluetooth"]["address"] = macAddress;
  config["bluetooth"]["mode"] = "BLE";
  config["bluetooth"]["connected"] = false;
  config["bluetooth"]["connectionCount"] = 0;

  log("📱 Bluetooth Started:");
  logf("\tMode: BLE\n");
  logf("\tDevice Name: %s\n", bleDeviceName.c_str());
  logf("\tMAC: %s\n", macAddress.c_str());

  bluetoothStarted = true;
  return true;
}

void ESPWiFi::stopBluetooth() {
  if (!bluetoothStarted) {
    return;
  }

  NimBLEDevice::deinit(true);
  pServer = nullptr;
  pTxCharacteristic = nullptr;
  pRxCharacteristic = nullptr;
  deviceConnected = false;
  oldDeviceConnected = false;
  connectionCount = 0;
  for (uint8_t i = 0; i < MAX_BLE_CONNECTIONS; i++) {
    connectionHandles[i] = 0xFFFF;
  }
  config["bluetooth"]["connected"] = false;
  config["bluetooth"]["connectionCount"] = 0;
  bluetoothStarted = false;
  log("📱 Bluetooth Stopped\n");
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
    config["bluetooth"]["connected"] = false;
    config["bluetooth"]["connectionCount"] = 0;
    if (bluetoothStarted) {
      stopBluetooth();
    }
    return;
  }

  // Update connection status in config based on current state
  if (bluetoothStarted && pServer != nullptr) {
    uint16_t connCount = pServer->getConnectedCount();
    if (connCount != connectionCount) {
      connectionCount =
          (connCount > MAX_BLE_CONNECTIONS) ? MAX_BLE_CONNECTIONS : connCount;
      deviceConnected = (connectionCount > 0);
    }
    config["bluetooth"]["connected"] = deviceConnected;
    config["bluetooth"]["connectionCount"] = (int)connectionCount;
  } else {
    config["bluetooth"]["connected"] = false;
    config["bluetooth"]["connectionCount"] = 0;
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
  // BLE scanning not yet implemented
}

void ESPWiFi::srvBluetooth() {
  initWebServer();

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

        // Read available data from RX characteristic
        NimBLEAttValue rxValue = pRxCharacteristic->getValue();
        if (rxValue.length() > 0) {
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

  webServer->on(
      "/api/bluetooth/disconnect", HTTP_POST,
      [this](AsyncWebServerRequest *request) {
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        JsonDocument responseDoc;

        if (!bluetoothStarted || pServer == nullptr) {
          responseDoc["success"] = false;
          responseDoc["error"] = "Bluetooth not started";
          String jsonResponse;
          serializeJson(responseDoc, jsonResponse);
          sendJsonResponse(request, 400, jsonResponse);
          return;
        }

        if (connectionCount == 0) {
          responseDoc["success"] = false;
          responseDoc["error"] = "No active connections";
          String jsonResponse;
          serializeJson(responseDoc, jsonResponse);
          sendJsonResponse(request, 400, jsonResponse);
          return;
        }

        // Disconnect all BLE clients
        log("📱 Disconnecting Bluetooth clients");
        logf("\tConnection count: %d\n", connectionCount);

        uint8_t disconnected = 0;
        for (uint8_t i = 0; i < connectionCount; i++) {
          pServer->disconnect(connectionHandles[i]);
          disconnected++;
        }

        // Note: The onDisconnect callback will be called
        // automatically for each connection and will update
        // connectionCount and deviceConnected

        responseDoc["success"] = true;
        responseDoc["message"] = "Disconnect initiated for all connections";
        responseDoc["disconnected"] = disconnected;

        String jsonResponse;
        serializeJson(responseDoc, jsonResponse);
        sendJsonResponse(request, 200, jsonResponse);
      });
}

#else // CONFIG_BT_ENABLED

// Bluetooth not available - stub implementations
bool ESPWiFi::startBluetooth() {
  config["bluetooth"]["enabled"] = false;
  config["bluetooth"]["installed"] = false;
  return false;
}

void ESPWiFi::stopBluetooth() {}

bool ESPWiFi::isBluetoothConnected() { return false; }

void ESPWiFi::bluetoothConfigHandler() {}

void ESPWiFi::scanBluetoothDevices() {}

void ESPWiFi::srvBluetooth() {
  // initWebServer();
}
#endif

#endif // ESPWiFi_BLUETOOTH
