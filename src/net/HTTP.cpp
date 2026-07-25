#include "HTTP.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

namespace Http {
namespace {

constexpr size_t kStreamReadSize = 512;

struct ResponseContext {
  size_t maxBytes = 0;
  std::string* body = nullptr;
};

esp_err_t httpEventHandler(esp_http_client_event_t* evt) {
  if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data_len <= 0 ||
      evt->user_data == nullptr) {
    return ESP_OK;
  }

  auto* ctx = static_cast<ResponseContext*>(evt->user_data);
  if (ctx->body == nullptr) {
    return ESP_OK;
  }

  if (ctx->body->size() + static_cast<size_t>(evt->data_len) >
      ctx->maxBytes) {
    return ESP_FAIL;
  }

  ctx->body->append(static_cast<const char*>(evt->data),
                    static_cast<size_t>(evt->data_len));
  return ESP_OK;
}

bool configureClient(esp_http_client_handle_t client,
                     const StreamRequest& request) {
  for (const auto& header : request.headers) {
    if (esp_http_client_set_header(client, header.first.c_str(),
                                   header.second.c_str()) != ESP_OK) {
      return false;
    }
  }

  if (request.contentType != nullptr &&
      esp_http_client_set_header(client, "Content-Type",
                                 request.contentType) != ESP_OK) {
    return false;
  }

  return true;
}

}  // namespace

Response makeRequest(const Request& request) {
  Response response;
  response.body.reserve(1024);

  ResponseContext ctx;
  ctx.maxBytes = request.maxResponseBytes;
  ctx.body = &response.body;

  esp_http_client_config_t httpConfig = {};
  httpConfig.url = request.url.c_str();
  httpConfig.method = request.method;
  httpConfig.timeout_ms = request.timeoutMs;
  httpConfig.crt_bundle_attach = esp_crt_bundle_attach;
  httpConfig.event_handler = httpEventHandler;
  httpConfig.user_data = &ctx;

  esp_http_client_handle_t client = esp_http_client_init(&httpConfig);
  if (client == nullptr) {
    response.err = ESP_FAIL;
    return response;
  }

  for (const auto& header : request.headers) {
    esp_http_client_set_header(client, header.first.c_str(),
                               header.second.c_str());
  }

  if (request.contentType != nullptr) {
    esp_http_client_set_header(client, "Content-Type", request.contentType);
  }

  if (!request.body.empty() ||
      request.method == HTTP_METHOD_POST || request.method == HTTP_METHOD_PUT) {
    esp_http_client_set_post_field(client, request.body.c_str(),
                                   request.body.size());
  }

  response.err = esp_http_client_perform(client);
  response.status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  return response;
}

Response makeStreamingRequest(const StreamRequest& request) {
  Response response;

  if (!request.onData) {
    response.err = ESP_ERR_INVALID_ARG;
    return response;
  }

  esp_http_client_config_t httpConfig = {};
  httpConfig.url = request.url.c_str();
  httpConfig.method = request.method;
  httpConfig.timeout_ms = request.timeoutMs;
  httpConfig.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&httpConfig);
  if (client == nullptr) {
    response.err = ESP_FAIL;
    return response;
  }

  if (!configureClient(client, request)) {
    response.err = ESP_FAIL;
    esp_http_client_cleanup(client);
    return response;
  }

  const int bodyLen = static_cast<int>(request.body.size());
  if (esp_http_client_open(client, bodyLen) != ESP_OK) {
    response.err = ESP_FAIL;
    esp_http_client_cleanup(client);
    return response;
  }

  if (bodyLen > 0) {
    const int written =
        esp_http_client_write(client, request.body.c_str(), bodyLen);
    if (written < 0 || written != bodyLen) {
      response.err = ESP_FAIL;
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return response;
    }
  }

  const int headerLen = esp_http_client_fetch_headers(client);
  if (headerLen < 0) {
    response.err = ESP_FAIL;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return response;
  }

  response.status = esp_http_client_get_status_code(client);
  if (response.status != 200) {
    char errorBuf[kStreamReadSize];
    while (true) {
      const int readLen =
          esp_http_client_read(client, errorBuf, sizeof(errorBuf));
      if (readLen <= 0) {
        break;
      }
      response.body.append(errorBuf, static_cast<size_t>(readLen));
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return response;
  }

  char readBuf[kStreamReadSize];
  while (true) {
    const int readLen = esp_http_client_read(client, readBuf, sizeof(readBuf));
    if (readLen < 0) {
      response.err = ESP_FAIL;
      break;
    }
    if (readLen == 0) {
      break;
    }

    if (!request.onData(reinterpret_cast<const uint8_t*>(readBuf),
                        static_cast<size_t>(readLen))) {
      response.err = ESP_FAIL;
      break;
    }
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return response;
}

std::string joinUrl(const std::string& baseUrl, const char* path) {
  if (baseUrl.empty()) {
    return path;
  }

  if (baseUrl.back() == '/') {
    return baseUrl + (path[0] == '/' ? path + 1 : path);
  }

  if (path[0] == '/') {
    return baseUrl + path;
  }

  return baseUrl + "/" + path;
}

}  // namespace Http
