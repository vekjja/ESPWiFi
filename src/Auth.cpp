// Auth.cpp - Stubbed for now
#include "ESPWiFi.h"

std::string ESPWiFi::generateToken() {
  // TODO: Implement token generation
  return "stub_token";
}

bool ESPWiFi::authEnabled() { return config["auth"]["enabled"].as<bool>(); }

void ESPWiFi::srvAuth() {
  if (!webServer) {
    log(ERROR, "Cannot start auth API /api/auth: web server not initialized");
    return;
  }

  // Auth endpoint (no auth required for login) - middleware applied
  // automatically
  HTTPRoute("/api/auth", HTTP_POST, [](httpd_req_t *req) -> esp_err_t {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    return espwifi->withMiddleware(
        req,
        [](httpd_req_t *req) -> esp_err_t {
          ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;

          // Read request body
          size_t content_len = req->content_len;
          if (content_len > 512) {
            httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE,
                                "Request body too large");
            return ESP_FAIL;
          }

          char *content = (char *)malloc(content_len + 1);
          if (content == nullptr) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
          }

          int ret = httpd_req_recv(req, content, content_len);
          if (ret <= 0) {
            free(content);
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
              httpd_resp_send_408(req);
            }
            return ESP_FAIL;
          }

          content[content_len] = '\0';

          // TODO: Parse credentials and validate, generate token
          std::string token = espwifi->generateToken();
          std::string json = "{\"token\":\"" + token + "\"}";

          free(content);
          espwifi->sendJsonResponse(req, 200, json);
          return ESP_OK;
        },
        false); // requireAuth = false (this is the login endpoint)
  });
}

bool ESPWiFi::authorized(httpd_req_t *req) {
  if (!authEnabled()) {
    return true;
  }

  // Get Authorization header
  size_t auth_hdr_len = httpd_req_get_hdr_value_len(req, "Authorization");
  if (auth_hdr_len == 0) {
    return false;
  }

  char *auth_hdr = (char *)malloc(auth_hdr_len + 1);
  if (auth_hdr == nullptr) {
    return false;
  }

  httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, auth_hdr_len + 1);

  // Check for Bearer token
  std::string auth_str(auth_hdr);
  free(auth_hdr);

  if (auth_str.find("Bearer ") == 0) {
    std::string token = auth_str.substr(7);
    // TODO: Validate token against stored tokens
    // For now, simple check - you should implement proper token validation
    return !token.empty();
  }

  return false;
}