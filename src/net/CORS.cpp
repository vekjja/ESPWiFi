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

void ESPWiFi::addCORS(httpd_req_t *req) {
  if (req == nullptr) {
    return;
  }
  if (!cors_cache_enabled) {
    return;
  }

  // Read Origin header into a fixed buffer (avoid heap).
  const size_t originLen = httpd_req_get_hdr_value_len(req, "Origin");
  if (originLen == 0 || originLen >= 256) {
    // No Origin => not a browser CORS request, so skip emitting CORS headers.
    // (This keeps responses smaller and avoids extra header work.)
    return;
  }

  char originBuf[256];
  originBuf[0] = '\0';
  if (httpd_req_get_hdr_value_str(req, "Origin", originBuf,
                                  sizeof(originBuf)) != ESP_OK ||
      originBuf[0] == '\0') {
    return;
  }

  // Gate by path if configured (auth.cors.paths / allowed_paths).
  JsonVariant cors = config["auth"]["cors"];
  if (cors.is<JsonObject>()) {
    JsonVariant pathsVar = cors["paths"];
    if (pathsVar.isNull()) {
      pathsVar = cors["allowed_paths"];
    }
    if (!pathsVar.isNull() && pathsVar.is<JsonArray>()) {
      std::string_view full(req->uri);
      size_t q = full.find('?');
      std::string_view path =
          (q == std::string_view::npos) ? full : full.substr(0, q);

      bool pathAllowed = false;
      for (JsonVariant v : pathsVar.as<JsonArray>()) {
        const char *pat = v.as<const char *>();
        if (pat == nullptr || pat[0] == '\0') {
          continue;
        }
        if (matchPattern(path, std::string_view(pat))) {
          pathAllowed = true;
          break;
        }
      }
      if (!pathAllowed) {
        return;
      }
    }
  }

  // Origin policy.
  // ESP-IDF httpd's response header setter can behave poorly with values
  // containing ':' (e.g. "http://..."), yielding truncated header values like
  // "http". To stay robust and browser-compatible, we use "*" for allowed
  // origins (works for our token-based auth; no cookies/credentials).
  //
  // We still *gate* whether to emit CORS headers based on the configured
  // allow-list; we just don't echo the Origin string back.
  if (!cors_cache_allow_any_origin) {
    // Match request Origin against configured origin patterns.
    bool originAllowed = false;
    if (!cors.is<JsonObject>() || !cors_cache_has_origins) {
      // No origins configured => legacy allow-any (but still require Origin).
      originAllowed = true;
    } else {
      JsonVariant originsVar = cors["origins"];
      if (originsVar.isNull()) {
        originsVar = cors["allowed_origins"];
      }
      if (originsVar.is<JsonArray>()) {
        std::string_view originView(originBuf);
        for (JsonVariant v : originsVar.as<JsonArray>()) {
          const char *pat = v.as<const char *>();
          if (pat == nullptr || pat[0] == '\0') {
            continue;
          }
          if (matchPattern(originView, std::string_view(pat))) {
            originAllowed = true;
            break;
          }
        }
      }
    }
    if (!originAllowed) {
      return;
    }
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  // Cached methods/headers (computed on config load/update).
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods",
                     cors_cache_allow_methods.c_str());
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers",
                     cors_cache_allow_headers.c_str());
}

void ESPWiFi::handleCorsPreflight(httpd_req_t *req) {
  addCORS(req);
  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_send(req, nullptr, 0);
}
