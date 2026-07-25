#ifndef HTTP_H
#define HTTP_H

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "esp_err.h"
#include "esp_http_client.h"

namespace Http {

struct Response {
  esp_err_t err = ESP_OK;
  int status = 0;
  std::string body;
};

struct Request {
  std::string url;
  esp_http_client_method_t method = HTTP_METHOD_POST;
  std::string body;
  const char* contentType = nullptr;
  uint32_t timeoutMs = 15000;
  size_t maxResponseBytes = 16384;
  std::vector<std::pair<std::string, std::string>> headers;
};

using StreamDataCallback = std::function<bool(const uint8_t* data, size_t len)>;

struct StreamRequest {
  std::string url;
  esp_http_client_method_t method = HTTP_METHOD_POST;
  std::string body;
  const char* contentType = nullptr;
  uint32_t timeoutMs = 15000;
  size_t readChunkSize = 4096;
  std::vector<std::pair<std::string, std::string>> headers;
  StreamDataCallback onData;
};

Response makeRequest(const Request& request);
Response makeStreamingRequest(const StreamRequest& request);
std::string joinUrl(const std::string& baseUrl, const char* path);

}  // namespace Http

#endif  // HTTP_H
