#include "OpenAI.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

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
  if (model != nullptr) {
    cfg.model = model;
  }

  const char* baseUrl = config["openai"]["baseUrl"].as<const char*>();
  if (baseUrl != nullptr) {
    cfg.baseUrl = baseUrl;
  }

  const char* systemMessage =
      config["openai"]["systemMessage"].as<const char*>();
  if (systemMessage != nullptr) {
    cfg.systemMessage = systemMessage;
  }

  const char* ttsModel = config["openai"]["ttsModel"].as<const char*>();
  if (ttsModel != nullptr) {
    cfg.ttsModel = ttsModel;
  }

  const char* ttsVoice = config["openai"]["ttsVoice"].as<const char*>();
  if (ttsVoice != nullptr) {
    cfg.ttsVoice = ttsVoice;
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

std::string truncateForTts(const std::string& text, size_t maxChars) {
  if (maxChars == 0 || text.size() <= maxChars) {
    return text;
  }

  size_t cut = maxChars;
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

size_t ttsByteBudgetFromLfs(size_t freeBytes) {
  constexpr size_t kMaxLfsUsagePercent = 99;
  return freeBytes * kMaxLfsUsagePercent / 100;
}

void applyLfsTtsBudget(OpenAI::Config& cfg, size_t freeBytes) {
  cfg.ttsByteBudget = ttsByteBudgetFromLfs(freeBytes);
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

  const int tokenLimit = effectiveMaxCompletionTokens();
  reqDoc["max_tokens"] = tokenLimit;

  std::string reqBody;
  serializeJson(reqDoc, reqBody);
  return reqBody;
}

std::string OpenAI::prepareTtsText(const std::string& text) const {
  return truncateForTts(text, effectiveMaxTtsChars());
}

size_t OpenAI::effectiveMaxTtsChars() const {
  // tts-1 @ 24 kHz mono: ~48 KB/s, ~15 chars/s -> ~2700 bytes/char (measured).
  constexpr size_t kWavOverheadBytes = 128;
  constexpr size_t kBytesPerCharEstimate = 2700;

  if (config_.ttsByteBudget <= kWavOverheadBytes) {
    return 1;
  }

  float speed = config_.ttsSpeed;
  if (speed < 0.25f) {
    speed = 0.25f;
  }

  const size_t pcmBudget = config_.ttsByteBudget - kWavOverheadBytes;
  const size_t charLimit =
      static_cast<size_t>(pcmBudget * speed / kBytesPerCharEstimate);
  return charLimit > 0 ? charLimit : 1;
}

int OpenAI::effectiveMaxCompletionTokens() const {
  const size_t chars = effectiveMaxTtsChars();
  const int fromChars = static_cast<int>(chars / 3);
  return fromChars > 0 ? fromChars : 1;
}

std::string OpenAI::buildTtsRequestBody(const std::string& text) const {
  const std::string input = prepareTtsText(text);

  JsonDocument reqDoc;
  reqDoc["model"] = config_.ttsModel;
  reqDoc["input"] = input;
  reqDoc["voice"] = config_.ttsVoice;
  reqDoc["response_format"] = "wav";

  float speed = config_.ttsSpeed;
  if (speed < 0.25f) speed = 0.25f;
  if (speed > 4.0f) speed = 4.0f;
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
  const size_t maxResponseBytes =
      maxResponseBytesForChars(effectiveMaxTtsChars());
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

  OpenAI::Config cfg = openAiConfigFromEspWiFi(config, connectTimeout);

  size_t totalPartitionBytes = 0;
  size_t usedBytes = 0;
  size_t freeBytes = 0;
  getStorageInfo("lfs", totalPartitionBytes, usedBytes, freeBytes);
  applyLfsTtsBudget(cfg, freeBytes);

  constexpr size_t kMinDownloadBytes = 8192;
  if (freeBytes == 0 || cfg.ttsByteBudget < kMinDownloadBytes) {
    log(ERROR,
        "🤖 OpenAI completion: LittleFS tight (%u bytes free, byteBudget %u)",
        static_cast<unsigned>(freeBytes),
        static_cast<unsigned>(cfg.ttsByteBudget));
    return "";
  }

  OpenAI client(cfg);
  const int tokenLimit = client.effectiveMaxCompletionTokens();
  log(INFO,
      "🤖 OpenAI completion: auto maxTokens=%d byteBudget=%u lfsFree=%u "
      "(~%u chars)",
      tokenLimit, static_cast<unsigned>(cfg.ttsByteBudget),
      static_cast<unsigned>(freeBytes),
      static_cast<unsigned>(client.effectiveMaxTtsChars()));

  OpenAIResult result = client.chatCompletion(prompt);
  if (!result.ok) {
    logOpenAiError(this, "completion", result);
    return "";
  }

  const size_t charLimit = client.effectiveMaxTtsChars();
  if (result.content.size() > charLimit) {
    log(INFO, "🤖 OpenAI completion: truncated to %u chars for TTS budget",
        static_cast<unsigned>(charLimit));
    return truncateForTts(result.content, charLimit);
  }

  return result.content;
}

#ifdef ESPWiFi_DAC_ENABLED
void ESPWiFi::oai_TTS(const std::string& text, int outputPin, float volume) {
  if (!isWiFiConnected()) {
    log(ERROR, "🤖 OpenAI: WiFi not connected");
    return;
  }

  OpenAI::Config cfg = openAiConfigFromEspWiFi(config, connectTimeout);
  if (cfg.apiKey.empty()) {
    log(ERROR, "🤖 OpenAI: apiKey not configured");
    return;
  }

  size_t totalPartitionBytes = 0;
  size_t usedBytes = 0;
  size_t freeBytes = 0;
  getStorageInfo("lfs", totalPartitionBytes, usedBytes, freeBytes);
  applyLfsTtsBudget(cfg, freeBytes);

  OpenAI client(cfg);
  const size_t charLimit = client.effectiveMaxTtsChars();
  const std::string ttsText = client.prepareTtsText(text);
  if (ttsText.empty()) {
    log(ERROR, "🤖 OpenAI TTS: text is empty");
    return;
  }

  if (ttsText.size() != text.size()) {
    log(INFO,
        "🤖 OpenAI TTS: truncated to %u chars (limit %u for byteBudget %u)",
        static_cast<unsigned>(ttsText.size()), static_cast<unsigned>(charLimit),
        static_cast<unsigned>(cfg.ttsByteBudget));
  }

  const std::string tempPath = lfsMountPoint + "/tts.wav";
  (void)remove(tempPath.c_str());

  log(INFO, "🤖 OpenAI TTS: byteBudget=%u lfsFree=%u (99%% LFS cap)",
      static_cast<unsigned>(cfg.ttsByteBudget),
      static_cast<unsigned>(freeBytes));

  constexpr size_t kMinDownloadBytes = 8192;
  if (freeBytes == 0 || cfg.ttsByteBudget < kMinDownloadBytes) {
    log(ERROR,
        "🤖 OpenAI TTS: LittleFS tight (%u bytes free, byteBudget %u). "
        "Delete "
        "/lfs/tts.wav or unused files.",
        static_cast<unsigned>(freeBytes),
        static_cast<unsigned>(cfg.ttsByteBudget));
    return;
  }

  const size_t writeLimit = cfg.ttsByteBudget;

  FILE* out = fopen(tempPath.c_str(), "wb");
  if (out == nullptr) {
    log(ERROR, "🤖 OpenAI TTS: failed to open %s", tempPath.c_str());
    return;
  }

  size_t totalBytes = 0;
  bool downloadCapped = false;
  bool writeFailed = false;
  OpenAIResult result =
      client.streamTextToSpeech(ttsText, [&](const uint8_t* data, size_t len) {
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
          log(ERROR,
              "🤖 OpenAI TTS: write failed at %u bytes (%u bytes free)",
              static_cast<unsigned>(totalBytes),
              static_cast<unsigned>(freeBytes));
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
    log(ERROR,
        "🤖 OpenAI TTS: invalid WAV after download (%u bytes, limit %u)",
        static_cast<unsigned>(totalBytes), static_cast<unsigned>(writeLimit));
    cleanup();
    return;
  }

  log(INFO, "🤖 OpenAI TTS: saved %u bytes to %s (%u%% of byteBudget %u)",
      static_cast<unsigned>(totalBytes), tempPath.c_str(),
      static_cast<unsigned>(
          cfg.ttsByteBudget > 0 ? (totalBytes * 100 / cfg.ttsByteBudget) : 0),
      static_cast<unsigned>(cfg.ttsByteBudget));

  playAudio("/tts.wav", volume, outputPin, true);
}
#endif
