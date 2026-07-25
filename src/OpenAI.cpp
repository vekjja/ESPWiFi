#include "OpenAI.h"

#include <ArduinoJson.h>

#include "ESPWiFi.h"
#include "HTTP.h"

namespace {

constexpr const char* kChatCompletionsPath = "/v1/chat/completions";
constexpr const char* kSpeechPath = "/v1/audio/speech";

Http::Request openAiRequest(const OpenAI::Config& config,
                            const std::string& url,
                            const std::string& requestBody, uint32_t timeoutMs,
                            size_t maxResponseBytes) {
  Http::Request request;
  request.url = url;
  request.body = requestBody;
  request.contentType = "application/json";
  request.timeoutMs = timeoutMs;
  request.maxResponseBytes = maxResponseBytes;
  request.headers.push_back(
      {"Authorization", std::string("Bearer ") + config.apiKey});
  return request;
}

OpenAI::Config openAiConfigFromEspWiFi(const JsonDocument& config,
                                       uint32_t timeoutMs) {
  OpenAI::Config cfg;
  cfg.timeoutMs = timeoutMs;

  const char* apiKey = config["openai"]["apiKey"].as<const char*>();
  if (apiKey != nullptr) {
    cfg.apiKey = apiKey;
  }

  const char* model = config["openai"]["model"].as<const char*>();
  if (model != nullptr && model[0] != '\0') {
    cfg.model = model;
  }

  const char* baseUrl = config["openai"]["baseUrl"].as<const char*>();
  if (baseUrl != nullptr && baseUrl[0] != '\0') {
    cfg.baseUrl = baseUrl;
  }

  const char* systemMessage =
      config["openai"]["systemMessage"].as<const char*>();
  if (systemMessage != nullptr) {
    cfg.systemMessage = systemMessage;
  }

  const char* ttsModel = config["openai"]["ttsModel"].as<const char*>();
  if (ttsModel != nullptr && ttsModel[0] != '\0') {
    cfg.ttsModel = ttsModel;
  }

  const char* ttsVoice = config["openai"]["ttsVoice"].as<const char*>();
  if (ttsVoice != nullptr && ttsVoice[0] != '\0') {
    cfg.ttsVoice = ttsVoice;
  }

  if (!config["openai"]["ttsTimeoutMs"].isNull()) {
    cfg.ttsTimeoutMs = config["openai"]["ttsTimeoutMs"].as<uint32_t>();
  }

  return cfg;
}

void logOpenAiError(ESPWiFi* espwifi, const char* action,
                    const OpenAIResult& result) {
  if (result.httpStatus > 0) {
    espwifi->log(ERROR, "OpenAI %s: HTTP %d: %s", action, result.httpStatus,
                 result.error.c_str());
  } else {
    espwifi->log(ERROR, "OpenAI %s: %s", action, result.error.c_str());
  }
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

OpenAIResult OpenAI::streamTextToSpeech(
    const std::string& text,
    const std::function<bool(const uint8_t* data, size_t len)>& onData) const {
  if (!isConfigured()) {
    return OpenAIResult{false, "", "apiKey not configured", 0};
  }

  if (text.empty()) {
    return OpenAIResult{false, "", "text must not be empty", 0};
  }

  if (!onData) {
    return OpenAIResult{false, "", "onData callback required", 0};
  }

  return postStream(kSpeechPath, buildTtsRequestBody(text), config_.ttsTimeoutMs,
                    onData);
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

std::string OpenAI::buildTtsRequestBody(const std::string& text) const {
  JsonDocument reqDoc;
  reqDoc["model"] = config_.ttsModel;
  reqDoc["input"] = text;
  reqDoc["voice"] = config_.ttsVoice;
  reqDoc["response_format"] = "wav";

  std::string reqBody;
  serializeJson(reqDoc, reqBody);
  return reqBody;
}

OpenAIResult OpenAI::postJson(const std::string& path,
                              const std::string& requestBody) const {
  const std::string url = Http::joinUrl(config_.baseUrl, path.c_str());
  Http::Response response =
      Http::makeRequest(openAiRequest(config_, url, requestBody, config_.timeoutMs,
                                config_.maxResponseBytes));

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

OpenAIResult OpenAI::postStream(
    const std::string& path, const std::string& requestBody, uint32_t timeoutMs,
    const std::function<bool(const uint8_t*, size_t)>& onData) const {
  const std::string url = Http::joinUrl(config_.baseUrl, path.c_str());

  Http::StreamRequest request;
  request.url = url;
  request.body = requestBody;
  request.contentType = "application/json";
  request.timeoutMs = timeoutMs;
  request.readChunkSize = 4096;
  request.headers.push_back(
      {"Authorization", std::string("Bearer ") + config_.apiKey});
  request.onData = onData;

  Http::Response response = Http::makeStreamingRequest(request);

  if (response.err != ESP_OK) {
    return OpenAIResult{false, "", esp_err_to_name(response.err),
                        response.status};
  }

  if (response.status != 200) {
    std::string error = response.body.empty() ? "HTTP request failed"
                                              : response.body;
    return OpenAIResult{false, "", std::move(error), response.status};
  }

  return OpenAIResult{true, "", "", response.status};
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

std::string ESPWiFi::oai_completion(const std::string& prompt) {
  if (!isWiFiConnected()) {
    log(ERROR, "OpenAI: WiFi not connected");
    return "";
  }

  OpenAI client(openAiConfigFromEspWiFi(config, connectTimeout));
  OpenAIResult result = client.chatCompletion(prompt);
  if (!result.ok) {
    logOpenAiError(this, "completion", result);
    return "";
  }

  return result.content;
}

#ifdef ESPWiFi_DAC_ENABLED
void ESPWiFi::oai_TTS(const std::string& text, int outputPin, float volume) {
  if (!isWiFiConnected()) {
    log(ERROR, "OpenAI: WiFi not connected");
    return;
  }

  OpenAI::Config cfg = openAiConfigFromEspWiFi(config, connectTimeout);
  if (cfg.apiKey.empty()) {
    log(ERROR, "OpenAI: apiKey not configured");
    return;
  }

  playStreamingWav(volume, outputPin, [cfg, text, this](auto writeChunk) {
    OpenAI client(cfg);
    OpenAIResult result = client.streamTextToSpeech(text, writeChunk);
    if (!result.ok) {
      logOpenAiError(this, "TTS", result);
      return false;
    }
    return true;
  });
}
#endif
