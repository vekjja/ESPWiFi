#ifndef ESPWiFi_SRV_ALL
#define ESPWiFi_SRV_ALL

#include "ESPWiFi.h"

void ESPWiFi::srvAll() {
  srvRoot();
  srvAuth();
  srvConfig();
  srvInfo();
  srvLog();
  srvBluetooth();
  srvFiles();
  srvGPIO();
  srvWildcard();
}

#endif // ESPWiFi_SRV_ALL
