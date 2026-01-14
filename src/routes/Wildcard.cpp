#ifndef ESPWiFi_SRV_WILDCARD
#define ESPWiFi_SRV_WILDCARD

#include "ESPWiFi.h"

void ESPWiFi::srvWildcard() {
  // GET handler for static files
  registerRoute("/*", HTTP_GET, [this](PsychicRequest *request) -> esp_err_t {
    // Get path from URI (remove query string)
    String uri = request->uri();
    int q = uri.indexOf('?');
    std::string path = (q >= 0) ? uri.substring(0, q).c_str() : uri.c_str();
    // URL-decode the path to handle special characters like spaces
    path = urlDecode(path);
    return sendFileResponse(request, path);
  });
}

#endif // ESPWiFi_SRV_WILDCARD
