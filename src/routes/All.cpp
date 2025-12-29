#ifndef ESPWiFi_SRV_ALL
#define ESPWiFi_SRV_ALL

#include "ESPWiFi.h"

void ESPWiFi::srvAll() {
  srvFS();
  srvLog();
  srvInfo();
  srvConfig();
  srvAuth();
  srvGPIO();
  srvWildcard();
}

#endif // ESPWiFi_SRV_ALL
