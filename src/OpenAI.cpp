#include "OpenAI.h"

#include <ArduinoJson.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

namespace {

constexpr const char* kChatCompletionsPath = "/v1/chat/completions";

struct HttpResponse {
  esp_err_t err = ESP_OK;
  int status = 0;
  size_t maxBytes = 0;
  std::string body;
};

esp_err_t httpEventHandler(esp_http_client_event_t* evt) {
  if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data_len <= 0 ||
      evt->user_data == nullptr) {
    return ESP_OK;
  }

  auto* response = static_cast<HttpResponse*>(evt->user_data);
  if (response->body.size() + static_cast<size_t>(evt->data_len) >
      response->maxBytes) {
    return ESP_FAIL;
  }

  response->body.append(static_cast<const char*>(evt->data),
                        static_cast<size_t>(evt->data_len));
  return ESP_OK;
}

HttpResponse httpPostJson(const std::string& url, const std::string& apiKey,
                          const std::string& requestBody, uint32_t timeoutMs,
                          size_t maxResponseBytes) {
  HttpResponse response;
  response.maxBytes = maxResponseBytes;
  response.body.reserve(1024);

  esp_http_client_config_t httpConfig = {};
  httpConfig.url = url.c_str();
  httpConfig.method = HTTP_METHOD_POST;
  httpConfig.timeout_ms = timeoutMs;
  httpConfig.crt_bundle_attach = esp_crt_bundle_attach;
  httpConfig.event_handler = httpEventHandler;
  httpConfig.user_data = &response;

  esp_http_client_handle_t client = esp_http_client_init(&httpConfig);
  if (client == nullptr) {
    response.err = ESP_FAIL;
    response.status = 0;
    return response;
  }

  std::string authHeader = std::string("Bearer ") + apiKey;
  esp_http_client_set_header(client, "Authorization", authHeader.c_str());
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, requestBody.c_str(), requestBody.size());

  response.err = esp_http_client_perform(client);
  response.status = esp_http_client_get_status_code(client);
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

}  // namespace

OpenAI::OpenAI(Config config) : config_(std::move(config)) {}

bool OpenAI::isConfigured() const { return !config_.apiKey.empty(); }

OpenAIResult OpenAI::chatCompletion(const std::string& prompt) const {
  std::vector<OpenAIChatMessage> messages;
  if (!config_.systemMessage.empty()) {
    messages.push_back(OpenAIChatMessage{"system", config_.systemMessage});
  }
  messages.push_back(OpenAIChatMessage{"user", prompt});
  return chatCompletion(messages);
}

OpenAIResult OpenAI::chatCompletion(
    const std::vector<OpenAIChatMessage>& messages) const {
  if (!isConfigured()) {
    return OpenAIResult{false, "", "apiKey not configured", 0};
  }

  if (messages.empty()) {
    return OpenAIResult{false, "", "messages must not be empty", 0};
  }

  return postJson(kChatCompletionsPath, buildChatRequestBody(messages));
}

std::string OpenAI::buildChatRequestBody(
    const std::vector<OpenAIChatMessage>& messages) const {
  JsonDocument reqDoc;
  reqDoc["model"] = config_.model;

  JsonArray msgArray = reqDoc["messages"].to<JsonArray>();
  for (const OpenAIChatMessage& message : messages) {
    JsonObject msg = msgArray.add<JsonObject>();
    msg["role"] = message.role;
    msg["content"] = message.content;
  }

  std::string reqBody;
  serializeJson(reqDoc, reqBody);
  return reqBody;
}

OpenAIResult OpenAI::postJson(const std::string& path,
                              const std::string& requestBody) const {
  const std::string url = joinUrl(config_.baseUrl, path.c_str());
  HttpResponse response =
      httpPostJson(url, config_.apiKey, requestBody, config_.timeoutMs,
                   config_.maxResponseBytes);

  if (response.err != ESP_OK) {
    return OpenAIResult{false, "", esp_err_to_name(response.err),
                        response.status};
  }

  if (response.status != 200) {
    std::string error = response.body.empty()
                            ? "HTTP request failed"
                            : response.body;
    return OpenAIResult{false, "", std::move(error), response.status};
  }

  return parseChatCompletionResponse(response.body, response.status);
}

OpenAIResult OpenAI::parseChatCompletionResponse(
    const std::string& responseBody, int httpStatus) const {
  JsonDocument respDoc;
  DeserializationError parseErr = deserializeJson(respDoc, responseBody);
  if (parseErr) {
    return OpenAIResult{false, "", parseErr.c_str(), httpStatus};
  }

  const char* content =
      respDoc["choices"][0]["message"]["content"].as<const char*>();
  if (content == nullptr) {
    return OpenAIResult{false, "", "no content in response", httpStatus};
  }

  return OpenAIResult{true, content, "", httpStatus};
}
