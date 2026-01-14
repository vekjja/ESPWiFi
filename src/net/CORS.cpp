// CORS.cpp - Config-driven CORS handling (ESP-friendly)
#include "ESPWiFi.h"

#include <cctype>
#include <cstring>

void ESPWiFi::corsConfigHandler() {
  // Defaults (preserve legacy behavior unless configured)
  cors_cache_enabled = true;
  cors_cache_has_origins = false;
  cors_cache_allow_any_origin = true;
  cors_cache_allow_methods = "GET, POST, PUT, DELETE, OPTIONS";
  cors_cache_allow_headers = "Content-Type, Authorization";

  JsonVariant cors = config["auth"]["cors"];
  if (!cors.is<JsonObject>()) {
    return;
  }

  if (!cors["enabled"].isNull()) {
    cors_cache_enabled = cors["enabled"].as<bool>();
  }

  // origins/origins list: precompute whether we allow "*" without scanning
  // every request.
  JsonVariant originsVar = cors["origins"];
  if (originsVar.isNull()) {
    originsVar = cors["allowed_origins"];
  }
  if (originsVar.is<JsonArray>()) {
    cors_cache_has_origins = true;
    cors_cache_allow_any_origin = false;
    for (JsonVariant v : originsVar.as<JsonArray>()) {
      const char *pat = v.as<const char *>();
      if (pat == nullptr || pat[0] == '\0') {
        continue;
      }
      if (strcmp(pat, "*") == 0) {
        cors_cache_allow_any_origin = true;
        break;
      }
    }
  }

  // methods: compute once, always include OPTIONS.
  JsonVariant methodsVar = cors["methods"];
  if (methodsVar.isNull()) {
    methodsVar = cors["allowed_methods"];
  }
  if (methodsVar.is<JsonArray>()) {
    std::string out;
    out.reserve(96);
    bool hasAny = false;
    bool hasOptions = false;

    for (JsonVariant v : methodsVar.as<JsonArray>()) {
      const char *m = v.as<const char *>();
      if (m == nullptr || m[0] == '\0') {
        continue;
      }
      if (hasAny) {
        out += ", ";
      }
      out += m;
      hasAny = true;

      // Check for OPTIONS (case-insensitive, bounded)
      if (strlen(m) == 7) {
        char t[8];
        for (int i = 0; i < 7; i++) {
          t[i] = (char)std::toupper((unsigned char)m[i]);
        }
        t[7] = '\0';
        if (strcmp(t, "OPTIONS") == 0) {
          hasOptions = true;
        }
      }
    }

    if (!hasAny) {
      out = "GET, POST, PUT, DELETE, OPTIONS";
    } else if (!hasOptions) {
      out += ", OPTIONS";
    }
    cors_cache_allow_methods = std::move(out);
  }

  // headers: compute once.
  JsonVariant headersVar = cors["headers"];
  if (headersVar.isNull()) {
    headersVar = cors["allowed_headers"];
  }
  if (headersVar.is<JsonArray>()) {
    std::string out;
    out.reserve(96);
    bool hasAny = false;
    for (JsonVariant v : headersVar.as<JsonArray>()) {
      const char *h = v.as<const char *>();
      if (h == nullptr || h[0] == '\0') {
        continue;
      }
      if (hasAny) {
        out += ", ";
      }
      out += h;
      hasAny = true;
    }
    if (hasAny) {
      cors_cache_allow_headers = std::move(out);
    }
  }
}
