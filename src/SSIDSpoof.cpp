// #ifdef ESP8266

// #ifndef ESPWIFI_SSIDSPOOF
// #define ESPWIFI_SSIDSPOOF

// #include "ESPWiFi.h"

// extern "C" {
// #include "user_interface.h"
// typedef void (*freedom_outside_cb_t)(uint8 status);
// int wifi_register_send_pkt_freedom_cb(freedom_outside_cb_t cb);
// void wifi_unregister_send_pkt_freedom_cb(void);
// int wifi_send_pkt_freedom(uint8* buf, int len, bool sys_seq);
// }

// // ===== Settings ===== //
// const uint8_t channels[] = {1, 6, 11};  // used Wi-Fi channels (available:
// 1-14) const bool wpa2 = false;                // WPA2 networks const bool
// appendSpaces =
//     false;  // makes all SSIDs 32 characters long to improve performance

// // run-time variables
// uint8_t channelIndex = 0;
// uint8_t macAddr[6];
// uint8_t wifi_channel = 1;
// uint32_t packetSize = 0;
// uint32_t packetCounter = 0;
// uint32_t attackTime = 0;
// uint32_t packetRateTime = 0;

// // beacon frame definition
// uint8_t beaconPacket[109] = {
//     /*  0 - 3  */ 0x80, 0x00, 0x00,
//     0x00,  // Type/Subtype: managment beacon frame
//     /*  4 - 9  */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Destination:
//     broadcast
//     /* 10 - 15 */ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // Source
//     /* 16 - 21 */ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // Source

//     // Fixed parameters
//     /* 22 - 23 */ 0x00,
//     0x00,  // Fragment & sequence number (will be done by the SDK)
//     /* 24 - 31 */ 0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00,  //
//     Timestamp
//     /* 32 - 33 */ 0xe8,
//     0x03,  // Interval: 0x64, 0x00 => every 100ms - 0xe8, 0x03 => every 1s
//     /* 34 - 35 */ 0x31, 0x00,  // capabilities Tnformation

//     // Tagged parameters

//     // SSID parameters
//     /* 36 - 37 */ 0x00, 0x20,  // Tag: Set SSID length, Tag length: 32
//     /* 38 - 69 */ 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
//     0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
//     0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,  // SSID

//     // Supported Rates
//     /* 70 - 71 */ 0x01, 0x08,  // Tag: Supported Rates, Tag length: 8
//     /* 72 */ 0x82,             // 1(B)
//     /* 73 */ 0x84,             // 2(B)
//     /* 74 */ 0x8b,             // 5.5(B)
//     /* 75 */ 0x96,             // 11(B)
//     /* 76 */ 0x24,             // 18
//     /* 77 */ 0x30,             // 24
//     /* 78 */ 0x48,             // 36
//     /* 79 */ 0x6c,             // 54

//     // Current Channel
//     /* 80 - 81 */ 0x03, 0x01,  // Channel set, length
//     /* 82 */ 0x01,             // Current Channel

//     // RSN information
//     /*  83 -  84 */ 0x30, 0x18,
//     /*  85 -  86 */ 0x01, 0x00,
//     /*  87 -  90 */ 0x00, 0x0f, 0xac, 0x02,
//     /*  91 -  92 */ 0x02, 0x00,
//     /*  93 - 100 */ 0x00, 0x0f, 0xac, 0x04, 0x00, 0x0f, 0xac,
//     0x04, /*Fix: changed 0x02(TKIP) to 0x04(CCMP) is default. WPA2 with TKIP
//     not
//              supported by many devices*/
//     /* 101 - 102 */ 0x01, 0x00,
//     /* 103 - 106 */ 0x00, 0x0f, 0xac, 0x02,
//     /* 107 - 108 */ 0x00, 0x00};

// /*
//   SSIDs:
//   - don't forget the \n at the end of each SSID!
//   - max. 32 characters per SSID
//   - No duplicates! You must change at least one character
// */
// // Store a single SSID for spoofing (max 32 chars)
// const char spoofedSSID[33] PROGMEM = "Test_Spoofed_SSID";

// // Random MAC generator
// void randomMac() {
//   for (int i = 0; i < 6; i++) {
//     macAddr[i] = random(256);
//   }
// }

// // ================= SSID Spoofing State & Helpers =================
// namespace {
// static unsigned long lastChannelTime = 0;

// // Helper: Cycle WiFi channel
// void cycleChannel() {
//   channelIndex = (channelIndex + 1) % (sizeof(channels) /
//   sizeof(channels[0])); wifi_channel = channels[channelIndex];
//   wifi_set_channel(wifi_channel);
// }
// }  // namespace
// // ================= END Helpers =================

// void ESPWiFi::initSSIDSpoof() {
//   randomSeed(os_random());
//   // set packetSize
//   packetSize = sizeof(beaconPacket);
//   if (wpa2) {
//     beaconPacket[34] = 0x31;
//   } else {
//     beaconPacket[34] = 0x21;
//     packetSize -= 26;
//   }
//   randomMac();
//   webServer.on("/ssidspoof/start", HTTP_OPTIONS,
//                [this]() { handleCorsPreflight(); });
//   webServer.on("/ssidspoof/start", HTTP_POST, [this]() {
//     ssidSpoofEnabled = true;
//     Serial.println("🟢 📶🔓 SSID Spoofing Started");
//     webServer.sendHeader("Access-Control-Allow-Origin", "*");
//     webServer.send(200, "application/json", "{\"status\":\"Started\"}");
//   });
//   webServer.on("/ssidspoof/stop", HTTP_OPTIONS,
//                [this]() { handleCorsPreflight(); });
//   webServer.on("/ssidspoof/stop", HTTP_POST, [this]() {
//     ssidSpoofEnabled = false;
//     Serial.println("🛑 📶🔒 SSID Spoofing Stopped");
//     webServer.sendHeader("Access-Control-Allow-Origin", "*");
//     webServer.send(200, "application/json", "{\"status\":\"Stopped\"}");
//   });
//   Serial.println("✅ 📶🔓 SSID Spoofing Enabled");
// }

// // Remove extern and define these here:
// const char ssids[] PROGMEM = {
//     "FBI Surveillance Van #119871\n"
//     "DEA Surveillance #4188AEC2D\n"
//     "Silence of the LANs\n"
//     "House LANnister\n"
//     "Winternet is Coming\n"
//     "Ping's Landing\n"
//     "This LAN is My LAN\n"
//     "LAN of the Rising Sun\n"
//     "LAN-tastic Voyage\n"
//     "LAN-tastic Voyage 2\n"
//     "Get off My LAN!\n"
//     "The Promised LAN\n"
//     "LAN Solo\n"
//     "LAN of Milk and Honey\n"
//     "LAN of the Free\n"
//     // ...add more as needed...
// };
// char emptySSID[32] = {' '};

// void nextChannel() {
//   // Example implementation, update as needed
//   static uint8_t channelIndex = 0;
//   const uint8_t channels[] = {1, 6, 11};
//   channelIndex = (channelIndex + 1) % (sizeof(channels) /
//   sizeof(channels[0])); wifi_channel = channels[channelIndex];
//   // If using ESP SDK: wifi_set_channel(wifi_channel);
// }

// void ESPWiFi::handleSSIDSpoof() {
//   static int i = 0;
//   static int j = 0;
//   static int ssidNum = 1;
//   static char tmp;
//   static int ssidsLen = strlen_P(ssids);
//   static unsigned long lastAttackTime = 0;
//   static unsigned long lastPacketRateTime = 0;
//   static uint32_t localPacketCounter = 0;

//   unsigned long currentTime = millis();

//   // send out SSIDs every 100ms
//   if (currentTime - lastAttackTime > 100) {
//     lastAttackTime = currentTime;
//     i = 0;
//     ssidNum = 1;
//     // Go to next channel
//     nextChannel();
//     while (i < ssidsLen) {
//       // Get the next SSID
//       j = 0;
//       do {
//         tmp = pgm_read_byte(ssids + i + j);
//         j++;
//       } while (tmp != '\n' && j <= 32 && i + j < ssidsLen);
//       uint8_t ssidLen = j - 1;
//       // set MAC address
//       macAddr[5] = ssidNum;
//       ssidNum++;
//       // write MAC address into beacon frame
//       memcpy(&beaconPacket[10], macAddr, 6);
//       memcpy(&beaconPacket[16], macAddr, 6);
//       // reset SSID
//       memcpy(&beaconPacket[38], emptySSID, 32);
//       // write new SSID into beacon frame
//       memcpy_P(&beaconPacket[38], &ssids[i], ssidLen);
//       // set channel for beacon frame
//       beaconPacket[82] = wifi_channel;
//       // send packet
//       if (appendSpaces) {
//         for (int k = 0; k < 3; k++) {
//           localPacketCounter +=
//               wifi_send_pkt_freedom(beaconPacket, packetSize, 0) == 0;
//           delay(1);
//         }
//       } else {
//         uint16_t tmpPacketSize = (packetSize - 32) + ssidLen;
//         uint8_t* tmpPacket = new uint8_t[tmpPacketSize];
//         memcpy(&tmpPacket[0], &beaconPacket[0], 38 + ssidLen);
//         tmpPacket[37] = ssidLen;
//         memcpy(&tmpPacket[38 + ssidLen], &beaconPacket[70], wpa2 ? 39 : 13);
//         for (int k = 0; k < 3; k++) {
//           localPacketCounter +=
//               wifi_send_pkt_freedom(tmpPacket, tmpPacketSize, 0) == 0;
//           delay(1);
//         }
//         delete[] tmpPacket;
//       }
//       i += j;
//     }
//   }
//   // show packet-rate each second
//   if (currentTime - lastPacketRateTime > 1000) {
//     lastPacketRateTime = currentTime;
//     Serial.print("Spoofed Packets/s: ");
//     Serial.println(localPacketCounter);
//     localPacketCounter = 0;
//   }
// }

// #endif  // ESPWIFI_SSIDSPOOF

// #endif  // ESP8266