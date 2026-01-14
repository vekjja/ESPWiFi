#ifndef ESPWiFi_SRV_ROOT
#define ESPWiFi_SRV_ROOT

#include "ESPWiFi.h"

void ESPWiFi::srvRoot() {
  // Root route - serve index.html from LFS (no auth required)
  registerRoute("/", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    return sendFileResponse(request, "/index.html");
  });
}

#endif // ESPWiFi_SRV_ROOT
