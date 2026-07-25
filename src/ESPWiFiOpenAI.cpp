#include "ESPWiFi.h"
#include "OpenAI.h"

namespace {

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

  return cfg;
}

}  // namespace

std::string ESPWiFi::oai_completion(const std::string& prompt) {
  if (!isWiFiConnected()) {
    log(ERROR, "OpenAI: WiFi not connected");
    return "";
  }

  OpenAI client(openAiConfigFromEspWiFi(config, connectTimeout));
  OpenAIResult result = client.chatCompletion(prompt);
  if (!result.ok) {
    if (result.httpStatus > 0) {
      log(ERROR, "OpenAI: HTTP %d: %s", result.httpStatus, result.error.c_str());
    } else {
      log(ERROR, "OpenAI: %s", result.error.c_str());
    }
    return "";
  }

  return result.content;
}
