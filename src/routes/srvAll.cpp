#ifndef ESPWiFi_SRV_ALL
#define ESPWiFi_SRV_ALL

#include "ESPWiFi.h"
#include "sdkconfig.h"

void ESPWiFi::srvAll() {
  srvRoot();
  srvAuth();
  srvConfig();
  srvInfo();
  srvLog();
  srvFiles();
  srvGPIO();
#ifdef ESPWiFi_CAMERA_ENABLED
  srvCamera();
#endif
#ifdef CONFIG_BT_A2DP_ENABLE
  srvBluetooth();
#endif
  srvWildcard();
}

#endif // ESPWiFi_SRV_ALL
