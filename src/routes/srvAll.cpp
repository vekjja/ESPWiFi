#ifndef ESPWiFi_SRV_ALL
#define ESPWiFi_SRV_ALL

#include "ESPWiFi.h"

void ESPWiFi::srvAll() {
  srvRoot();
  srvAuth();
  srvLog();
  srvFiles();
  srvGPIO();
  srvBLE();
  srvCamera();
  srvBluetooth();
  srvWildcard();
}

#endif // ESPWiFi_SRV_ALL
