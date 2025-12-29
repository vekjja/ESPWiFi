#ifndef ESPWiFi_SRV_ALL
#define ESPWiFi_SRV_ALL

#include "ESPWiFi.h"

void ESPWiFi::srvAll() {
  srvRoot();
  srvAuth();
  srvConfig();
  srvInfo();
  srvLog();
  srvFiles();
  srvGPIO();
#ifdef ESPWiFi_CAMERA
  srvCamera();
#endif
  srvWildcard();
}

#endif // ESPWiFi_SRV_ALL
