#include "HTTP.h"

#include <functional>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace Http {
namespace {

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

bool isTransientConnectError(esp_err_t err) {
  return err == ESP_ERR_HTTP_CONNECT || err == ESP_ERR_HTTP_CONNECTING ||
         err == ESP_FAIL;
}

constexpr int kMaxConnectAttempts = 3;

Response makeRequestOnce(const Request& request) {
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

Response makeStreamingRequestOnce(const StreamRequest& request) {
  Response response;

  if (!request.onData) {
    response.err = ESP_ERR_INVALID_ARG;
    return response;
  }

  const size_t readChunkSize =
      request.readChunkSize > 0 ? request.readChunkSize : 4096;

  esp_http_client_config_t httpConfig = {};
  httpConfig.url = request.url.c_str();
  httpConfig.method = request.method;
  httpConfig.timeout_ms = request.timeoutMs;
  httpConfig.buffer_size = static_cast<int>(readChunkSize);
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
    std::vector<char> errorBuf(readChunkSize);
    while (true) {
      const int readLen =
          esp_http_client_read(client, errorBuf.data(), errorBuf.size());
      if (readLen <= 0) {
        break;
      }
      response.body.append(errorBuf.data(), static_cast<size_t>(readLen));
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return response;
  }

  std::vector<char> readBuf(readChunkSize);
  while (true) {
    const int readLen =
        esp_http_client_read(client, readBuf.data(), readBuf.size());
    if (readLen < 0) {
      response.err = ESP_FAIL;
      break;
    }
    if (readLen == 0) {
      break;
    }

    if (!request.onData(reinterpret_cast<const uint8_t*>(readBuf.data()),
                        static_cast<size_t>(readLen))) {
      response.err = ESP_FAIL;
      break;
    }
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return response;
}

Response withConnectRetries(const Response& first,
                            const std::function<Response()>& attemptFn,
                            const char* label) {
  Response response = first;
  for (int attempt = 1; attempt < kMaxConnectAttempts; ++attempt) {
    if (!isTransientConnectError(response.err)) {
      break;
    }
    ESP_LOGW("HTTP", "%s retry %d/%d (%s, heap=%u)", label, attempt + 1,
             kMaxConnectAttempts, esp_err_to_name(response.err),
             static_cast<unsigned>(esp_get_free_heap_size()));
    vTaskDelay(pdMS_TO_TICKS(500 * attempt));
    response = attemptFn();
  }
  return response;
}

}  // namespace

Response makeRequest(const Request& request) {
  return withConnectRetries(makeRequestOnce(request),
                            [&]() { return makeRequestOnce(request); },
                            "connect");
}

Response makeStreamingRequest(const StreamRequest& request) {
  return withConnectRetries(makeStreamingRequestOnce(request),
                            [&]() { return makeStreamingRequestOnce(request); },
                            "stream connect");
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
