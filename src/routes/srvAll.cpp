#ifndef ESPWiFi_SRV_ALL
#define ESPWiFi_SRV_ALL

#include "ESPWiFi.h"

void ESPWiFi::srvAll() {
  srvRoot();
  srvAuth();
  srvWildcard();
}

#endif // ESPWiFi_SRV_ALL
