#include "OpenAI.h"

#include <ArduinoJson.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "ESPWiFi.h"
#include "HTTP.h"

namespace {

constexpr const char* kChatCompletionsPath = "/v1/chat/completions";
constexpr const char* kSpeechPath = "/v1/audio/speech";
constexpr size_t kMinTtsBudgetBytes = 8192;
constexpr size_t kMaxLfsUsagePercent = 99;

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

  if (const char* v = config["openai"]["apiKey"].as<const char*>()) {
    cfg.apiKey = v;
  }
  if (const char* v = config["openai"]["model"].as<const char*>()) {
    cfg.model = v;
  }
  if (const char* v = config["openai"]["baseUrl"].as<const char*>()) {
    cfg.baseUrl = v;
  }
  if (const char* v = config["openai"]["systemMessage"].as<const char*>()) {
    cfg.systemMessage = v;
  }
  if (const char* v = config["openai"]["ttsModel"].as<const char*>()) {
    cfg.ttsModel = v;
  }
  if (const char* v = config["openai"]["ttsVoice"].as<const char*>()) {
    cfg.ttsVoice = v;
  }

  cfg.ttsTimeoutMs = config["openai"]["ttsTimeoutMs"].as<uint32_t>();
  if (cfg.ttsTimeoutMs == 0) {
    cfg.ttsTimeoutMs = 60000;
  }

  cfg.ttsSpeed = config["openai"]["ttsSpeed"].as<float>();
  if (cfg.ttsSpeed <= 0.0f) {
    cfg.ttsSpeed = 1.0f;
  }

  return cfg;
}

float clampTtsSpeed(float speed) {
  if (speed < 0.25f) return 0.25f;
  if (speed > 4.0f) return 4.0f;
  return speed;
}

size_t maxTtsCharsForBudget(size_t byteBudget, float ttsSpeed) {
  constexpr size_t kWavOverheadBytes = 128;
  constexpr size_t kBytesPerCharEstimate = 3200;
  constexpr size_t kCharBudgetPercent = 92;

  if (byteBudget <= kWavOverheadBytes) {
    return 1;
  }

  const float speed = clampTtsSpeed(ttsSpeed);
  const size_t pcmBudget = byteBudget - kWavOverheadBytes;
  const size_t safePcm = pcmBudget * kCharBudgetPercent / 100;
  const size_t charLimit =
      static_cast<size_t>(safePcm * speed / kBytesPerCharEstimate);
  return charLimit > 0 ? charLimit : 1;
}

int maxCompletionTokensForChars(size_t chars) {
  const int fromChars = static_cast<int>(chars / 4);
  return fromChars > 0 ? fromChars : 1;
}

void appendVoiceBudgetHint(OpenAI::Config& cfg, size_t charLimit) {
  const size_t approxSeconds = cfg.ttsByteBudget / 48000;
  if (!cfg.systemMessage.empty()) {
    cfg.systemMessage += ' ';
  }
  cfg.systemMessage +=
      "Voice transmission limit: maximum " +
      std::to_string(charLimit) + " characters (~" +
      std::to_string(approxSeconds) +
      " seconds spoken). Write one complete transmission that fits this limit, "
      "uses complete sentences only, and ends with Over. Never exceed the "
      "character limit.";
}

std::string truncateForTts(const std::string& text, size_t maxChars) {
  if (maxChars == 0 || text.size() <= maxChars) {
    return text;
  }

  size_t cut = maxChars;
  size_t sentenceEnd = 0;
  for (size_t i = 0; i < cut && i < text.size(); ++i) {
    const char c = text[i];
    if (c == '.' || c == '!' || c == '?') {
      if (i + 1 >= cut / 2) {
        sentenceEnd = i + 1;
      }
    }
  }

  if (sentenceEnd > 0) {
    while (sentenceEnd < text.size() &&
           isspace(static_cast<unsigned char>(text[sentenceEnd]))) {
      ++sentenceEnd;
    }
    return text.substr(0, sentenceEnd);
  }

  const size_t lastSpace = text.rfind(' ', maxChars);
  if (lastSpace > maxChars / 2) {
    cut = lastSpace;
  }
  return text.substr(0, cut);
}

bool isValidWavFile(const char* path) {
  FILE* f = fopen(path, "rb");
  if (f == nullptr) {
    return false;
  }

  uint8_t riff[12];
  const bool ok = fread(riff, 1, sizeof(riff), f) == sizeof(riff) &&
                  memcmp(riff, "RIFF", 4) == 0 &&
                  memcmp(riff + 8, "WAVE", 4) == 0;
  fclose(f);
  return ok;
}

size_t maxResponseBytesForChars(size_t charLimit) {
  constexpr size_t kMinResponseBytes = 4096;
  const size_t estimated = charLimit * 4;
  return estimated > kMinResponseBytes ? estimated : kMinResponseBytes;
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

struct OpenAiRuntime {
  OpenAI client{OpenAI::Config{}};
  size_t freeBytes = 0;
  OpenAI::Budget budget{};
};

bool prepareOpenAiRuntime(ESPWiFi* espwifi, OpenAiRuntime& rt,
                          bool requireApiKey, bool injectVoiceBudget) {
  OpenAI::Config cfg =
      openAiConfigFromEspWiFi(espwifi->config, espwifi->connectTimeout);
  if (requireApiKey && cfg.apiKey.empty()) {
    espwifi->log(ERROR, "🤖 OpenAI: apiKey not configured");
    return false;
  }

  size_t totalBytes = 0;
  size_t usedBytes = 0;
  espwifi->getStorageInfo("lfs", totalBytes, usedBytes, rt.freeBytes);
  cfg.ttsByteBudget = rt.freeBytes * kMaxLfsUsagePercent / 100;

  if (rt.freeBytes == 0 || cfg.ttsByteBudget < kMinTtsBudgetBytes) {
    espwifi->log(ERROR,
                 "🤖 OpenAI: LittleFS tight (%u bytes free, byteBudget %u)",
                 static_cast<unsigned>(rt.freeBytes),
                 static_cast<unsigned>(cfg.ttsByteBudget));
    return false;
  }

  if (injectVoiceBudget) {
    appendVoiceBudgetHint(cfg, maxTtsCharsForBudget(cfg.ttsByteBudget, cfg.ttsSpeed));
  }

  rt.client = OpenAI(std::move(cfg));
  rt.budget = rt.client.budget();
  return true;
}

}  // namespace

OpenAI::OpenAI(Config config) : config_(std::move(config)) {}

bool OpenAI::isConfigured() const { return !config_.apiKey.empty(); }

OpenAI::Budget OpenAI::budget() const {
  Budget b;
  b.bytes = config_.ttsByteBudget;
  b.chars = maxTtsChars();
  b.tokens = maxCompletionTokens();
  return b;
}

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

  return postStream(kSpeechPath, buildTtsRequestBody(text),
                    config_.ttsTimeoutMs, onData);
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

  reqDoc["max_tokens"] = maxCompletionTokens();

  std::string reqBody;
  serializeJson(reqDoc, reqBody);
  return reqBody;
}

size_t OpenAI::maxTtsChars() const {
  return maxTtsCharsForBudget(config_.ttsByteBudget, config_.ttsSpeed);
}

int OpenAI::maxCompletionTokens() const {
  return maxCompletionTokensForChars(maxTtsChars());
}

std::string OpenAI::buildTtsRequestBody(const std::string& text) const {
  JsonDocument reqDoc;
  reqDoc["model"] = config_.ttsModel;
  reqDoc["input"] = truncateForTts(text, maxTtsChars());
  reqDoc["voice"] = config_.ttsVoice;
  reqDoc["response_format"] = "wav";

  const float speed = clampTtsSpeed(config_.ttsSpeed);
  if (speed != 1.0f) {
    reqDoc["speed"] = speed;
  }

  std::string reqBody;
  serializeJson(reqDoc, reqBody);
  return reqBody;
}

OpenAIResult OpenAI::postJson(const std::string& path,
                              const std::string& requestBody) const {
  const std::string url = Http::joinUrl(config_.baseUrl, path.c_str());
  const size_t maxResponseBytes = maxResponseBytesForChars(maxTtsChars());
  Http::Response response = Http::makeRequest(openAiRequest(
      config_, url, requestBody, config_.timeoutMs, maxResponseBytes));

  if (response.err != ESP_OK) {
    return OpenAIResult{false, "", esp_err_to_name(response.err),
                        response.status};
  }

  if (response.status != 200) {
    std::string error =
        response.body.empty() ? "HTTP request failed" : response.body;
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
    std::string error =
        response.body.empty() ? "HTTP request failed" : response.body;
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

  OpenAiRuntime rt;
  if (!prepareOpenAiRuntime(this, rt, false, true)) {
    return "";
  }

  log(INFO,
      "🤖 OpenAI completion: auto maxTokens=%d byteBudget=%u lfsFree=%u "
      "(~%u chars)",
      rt.budget.tokens, static_cast<unsigned>(rt.budget.bytes),
      static_cast<unsigned>(rt.freeBytes),
      static_cast<unsigned>(rt.budget.chars));

  OpenAIResult result = rt.client.chatCompletion(prompt);
  if (!result.ok) {
    logOpenAiError(this, "completion", result);
    return "";
  }

  if (result.content.size() > rt.budget.chars) {
    log(WARNING,
        "🤖 OpenAI completion: over budget (%u > %u chars), trimming to fit",
        static_cast<unsigned>(result.content.size()),
        static_cast<unsigned>(rt.budget.chars));
    return truncateForTts(result.content, rt.budget.chars);
  }

  return result.content;
}

#ifdef ESPWiFi_DAC_ENABLED
void ESPWiFi::oai_TTS(const std::string& text, int outputPin, float volume) {
  if (!isWiFiConnected()) {
    log(ERROR, "🤖 OpenAI: WiFi not connected");
    return;
  }

  OpenAiRuntime rt;
  if (!prepareOpenAiRuntime(this, rt, true, false)) {
    return;
  }

  if (text.empty()) {
    log(ERROR, "🤖 OpenAI TTS: text is empty");
    return;
  }

  log(INFO, "🤖 OpenAI TTS: byteBudget=%u lfsFree=%u (~%u chars)",
      static_cast<unsigned>(rt.budget.bytes),
      static_cast<unsigned>(rt.freeBytes),
      static_cast<unsigned>(rt.budget.chars));

  const std::string tempPath = lfsMountPoint + "/tts.wav";
  (void)remove(tempPath.c_str());

  const size_t writeLimit = rt.budget.bytes;

  FILE* out = fopen(tempPath.c_str(), "wb");
  if (out == nullptr) {
    log(ERROR, "🤖 OpenAI TTS: failed to open %s", tempPath.c_str());
    return;
  }

  size_t totalBytes = 0;
  bool downloadCapped = false;
  bool writeFailed = false;
  OpenAIResult result =
      rt.client.streamTextToSpeech(text, [&](const uint8_t* data, size_t len) {
        if (data == nullptr || len == 0) {
          return true;
        }

        if (totalBytes >= writeLimit) {
          downloadCapped = true;
          return false;
        }

        size_t toWrite = len;
        if (totalBytes + len > writeLimit) {
          toWrite = writeLimit - totalBytes;
          downloadCapped = true;
        }

        const size_t written = fwrite(data, 1, toWrite, out);
        totalBytes += written;
        if (written != toWrite) {
          log(ERROR, "🤖 OpenAI TTS: write failed at %u bytes",
              static_cast<unsigned>(totalBytes));
          writeFailed = true;
          return false;
        }

        if (downloadCapped) {
          log(WARNING, "🤖 OpenAI TTS: capped download at %u bytes",
              static_cast<unsigned>(totalBytes));
          return false;
        }
        return true;
      });
  fclose(out);

  auto cleanup = [&]() { (void)remove(tempPath.c_str()); };

  if (writeFailed || totalBytes == 0) {
    if (!writeFailed) {
      logOpenAiError(this, "TTS", result);
    }
    cleanup();
    return;
  }

  if (!result.ok && !downloadCapped) {
    logOpenAiError(this, "TTS", result);
    cleanup();
    return;
  }

  if (totalBytes > writeLimit || !isValidWavFile(tempPath.c_str())) {
    log(ERROR, "🤖 OpenAI TTS: invalid WAV (%u bytes, limit %u)",
        static_cast<unsigned>(totalBytes), static_cast<unsigned>(writeLimit));
    cleanup();
    return;
  }

  log(INFO, "🤖 OpenAI TTS: saved %u bytes to %s (%u%% of byteBudget %u)",
      static_cast<unsigned>(totalBytes), tempPath.c_str(),
      static_cast<unsigned>(writeLimit > 0 ? totalBytes * 100 / writeLimit
                                           : 0),
      static_cast<unsigned>(writeLimit));

  playAudio("/tts.wav", volume, outputPin, true);
}
#endif
