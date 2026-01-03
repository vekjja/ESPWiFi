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
#ifdef CONFIG_BT_CLASSIC_ENABLED
  srvBluetooth();
#endif
  srvFiles();
  srvGPIO();
  srvWildcard();
}

#endif // ESPWiFi_SRV_ALL
