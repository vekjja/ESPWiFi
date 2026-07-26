#include "OpenAI.h"

#include <ArduinoJson.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "ESPWiFi.h"
#include "HTTP.h"
#include "esp_heap_caps.h"

namespace {

constexpr const char* kChatCompletionsPath = "/v1/chat/completions";
constexpr const char* kSpeechPath = "/v1/audio/speech";
constexpr size_t kMinTtsBudgetBytes = 8192;
constexpr size_t kMaxLfsUsagePercent = 99;
constexpr size_t kMaxHeapUsagePercent = 99;

// Streaming heap: readiness gate + completion char budget (no byte cap while
// streaming).
constexpr size_t kStreamDownloadStackBytes = 12288;
constexpr size_t kStreamPlaybackStackBytes = 16384;
constexpr size_t kStreamReadChunkBytes = 2048;
constexpr size_t kStreamHttpReserveBytes = 32 * 1024;
constexpr size_t kStreamMiscReserveBytes = 16 * 1024;
constexpr size_t kStreamBufferMinBytes = 16 * 1024;

constexpr size_t kNonBufferStreamingReserve =
    kStreamDownloadStackBytes + kStreamPlaybackStackBytes +
    kStreamReadChunkBytes + kStreamHttpReserveBytes + kStreamMiscReserveBytes;

constexpr size_t kTtsPcmBytesPerSecond = 48000;
constexpr size_t kUnlimitedResponseBytes = 32 * 1024;

enum class OaiByteLimit { None, Heap, Lfs };

OaiByteLimit parseByteLimit(const std::string& limit) {
  if (limit == "heap") return OaiByteLimit::Heap;
  if (limit == "lfs") return OaiByteLimit::Lfs;
  return OaiByteLimit::None;
}

// ---- Config / budget --------------------------------------------------------

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
      "Voice transmission limit: maximum " + std::to_string(charLimit) +
      " characters (~" + std::to_string(approxSeconds) +
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

size_t maxResponseBytesForChars(size_t charLimit) {
  constexpr size_t kMinResponseBytes = 4096;
  const size_t estimated = charLimit * 4;
  return estimated > kMinResponseBytes ? estimated : kMinResponseBytes;
}

// ---- Runtime ----------------------------------------------------------------

struct OpenAiRuntime {
  OpenAI client{OpenAI::Config{}};
  OaiByteLimit byteLimit = OaiByteLimit::None;
  size_t freeBytes = 0;
  size_t largestBlock = 0;
  OpenAI::Budget budget{};
};

size_t pickStreamBufferTier(size_t largestBlock) {
  if (largestBlock >= 48 * 1024) return 48 * 1024;
  if (largestBlock >= 32 * 1024) return 32 * 1024;
  if (largestBlock >= 24 * 1024) return 24 * 1024;
  if (largestBlock >= kStreamBufferMinBytes) return kStreamBufferMinBytes;
  return 0;
}

bool heapReadyForStreaming(size_t freeHeap, size_t largestBlock) {
  const size_t bufferTier = pickStreamBufferTier(largestBlock);
  if (bufferTier == 0) {
    return false;
  }

  const size_t footprint = kNonBufferStreamingReserve + bufferTier;
  return freeHeap > footprint + kMinTtsBudgetBytes;
}

size_t computeHeapTtsByteBudget(size_t freeHeap, size_t largestBlock) {
  const size_t bufferTier = pickStreamBufferTier(largestBlock);
  if (bufferTier == 0 || !heapReadyForStreaming(freeHeap, largestBlock)) {
    return 0;
  }

  // Completion voice hint only — streamed WAV bytes are not held entirely in
  // RAM.
  size_t byteBudget = bufferTier * 3;
  return byteBudget * kMaxHeapUsagePercent / 100;
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

OpenAIResult httpFailureResult(const Http::Response& response) {
  std::string error =
      response.body.empty() ? "HTTP request failed" : response.body;
  return OpenAIResult{false, "", std::move(error), response.status};
}

bool ensureWiFiForOpenAi(ESPWiFi* espwifi) {
  if (espwifi->isWiFiConnected()) {
    return true;
  }
  espwifi->log(ERROR, "OpenAI: WiFi not connected");
  return false;
}

bool prepareOpenAiClient(ESPWiFi* espwifi, OpenAiRuntime& rt,
                         bool requireApiKey, OaiByteLimit byteLimit) {
  OpenAI::Config cfg =
      openAiConfigFromEspWiFi(espwifi->config, espwifi->connectTimeout);
  if (requireApiKey && cfg.apiKey.empty()) {
    espwifi->log(ERROR, "🤖 OpenAI: apiKey not configured");
    return false;
  }

  rt.byteLimit = byteLimit;
  cfg.ttsByteBudget = 0;

  if (byteLimit == OaiByteLimit::Heap) {
    rt.freeBytes = esp_get_free_heap_size();
    rt.largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    cfg.ttsByteBudget = computeHeapTtsByteBudget(rt.freeBytes, rt.largestBlock);
    if (cfg.ttsByteBudget < kMinTtsBudgetBytes) {
      espwifi->log(
          ERROR, "🤖 OpenAI: heap tight (free=%u largest=%u, byteBudget %u)",
          static_cast<unsigned>(rt.freeBytes),
          static_cast<unsigned>(rt.largestBlock),
          static_cast<unsigned>(cfg.ttsByteBudget));
      return false;
    }
  } else if (byteLimit == OaiByteLimit::Lfs) {
    size_t totalBytes = 0;
    size_t usedBytes = 0;
    espwifi->getStorageInfo("lfs", totalBytes, usedBytes, rt.freeBytes);
    rt.largestBlock = 0;
    cfg.ttsByteBudget = rt.freeBytes * kMaxLfsUsagePercent / 100;

    if (rt.freeBytes == 0 || cfg.ttsByteBudget < kMinTtsBudgetBytes) {
      espwifi->log(ERROR,
                   "🤖 OpenAI: LittleFS tight (%u bytes free, byteBudget %u)",
                   static_cast<unsigned>(rt.freeBytes),
                   static_cast<unsigned>(cfg.ttsByteBudget));
      return false;
    }
  }

  if (byteLimit != OaiByteLimit::None) {
    appendVoiceBudgetHint(
        cfg, maxTtsCharsForBudget(cfg.ttsByteBudget, cfg.ttsSpeed));
  }

  rt.client = OpenAI(std::move(cfg));
  rt.budget = rt.client.budget();
  return true;
}

bool prepareTtsClient(ESPWiFi* espwifi, OpenAI& client) {
  if (!ensureWiFiForOpenAi(espwifi)) {
    return false;
  }

  OpenAI::Config cfg =
      openAiConfigFromEspWiFi(espwifi->config, espwifi->connectTimeout);
  if (cfg.apiKey.empty()) {
    espwifi->log(ERROR, "🤖 OpenAI: apiKey not configured");
    return false;
  }

  cfg.ttsByteBudget = 0;
  client = OpenAI(std::move(cfg));
  return true;
}

#ifdef ESPWiFi_DAC_ENABLED
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

struct TtsDownloadResult {
  OpenAIResult apiResult{};
  size_t totalBytes = 0;
  bool capped = false;
  bool writeFailed = false;
};

TtsDownloadResult streamTtsToCallback(
    OpenAI& client, const std::string& text, size_t byteLimit,
    const std::function<bool(const uint8_t* data, size_t len)>& writeFn) {
  TtsDownloadResult result;
  result.apiResult =
      client.streamTextToSpeech(text, [&](const uint8_t* data, size_t len) {
        if (data == nullptr || len == 0) {
          return true;
        }

        if (result.totalBytes >= byteLimit) {
          result.capped = true;
          return false;
        }

        size_t toWrite = len;
        if (result.totalBytes + len > byteLimit) {
          toWrite = byteLimit - result.totalBytes;
          result.capped = true;
        }

        if (!writeFn(data, toWrite)) {
          result.writeFailed = true;
          return false;
        }

        result.totalBytes += toWrite;
        if (result.capped) {
          return false;
        }
        return true;
      });

  return result;
}
#endif

}  // namespace

OpenAI::OpenAI(Config config) : config_(std::move(config)) {}

bool OpenAI::isConfigured() const { return !config_.apiKey.empty(); }

OpenAI::Budget OpenAI::budget() const {
  Budget b;
  if (config_.ttsByteBudget == 0) {
    return b;
  }

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

  if (config_.ttsByteBudget != 0) {
    reqDoc["max_tokens"] = maxCompletionTokens();
  }

  std::string reqBody;
  serializeJson(reqDoc, reqBody);
  return reqBody;
}

size_t OpenAI::maxTtsChars() const {
  if (config_.ttsByteBudget == 0) {
    return static_cast<size_t>(-1);
  }
  return maxTtsCharsForBudget(config_.ttsByteBudget, config_.ttsSpeed);
}

int OpenAI::maxCompletionTokens() const {
  if (config_.ttsByteBudget == 0) {
    return 0;
  }
  return maxCompletionTokensForChars(maxTtsChars());
}

std::string OpenAI::buildTtsRequestBody(const std::string& text) const {
  JsonDocument reqDoc;
  reqDoc["model"] = config_.ttsModel;
  reqDoc["input"] =
      config_.ttsByteBudget == 0 ? text : truncateForTts(text, maxTtsChars());
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
  const size_t maxResponseBytes = config_.ttsByteBudget == 0
                                      ? kUnlimitedResponseBytes
                                      : maxResponseBytesForChars(maxTtsChars());
  Http::Response response = Http::makeRequest(openAiRequest(
      config_, url, requestBody, config_.timeoutMs, maxResponseBytes));

  if (response.err != ESP_OK) {
    return OpenAIResult{false, "", esp_err_to_name(response.err),
                        response.status};
  }

  if (response.status != 200) {
    return httpFailureResult(response);
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
    return httpFailureResult(response);
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

std::string ESPWiFi::oai_completion(const std::string& prompt,
                                    const std::string& byteLimit) {
  if (!ensureWiFiForOpenAi(this)) {
    return "";
  }

  const OaiByteLimit mode = parseByteLimit(byteLimit);
  OpenAiRuntime rt;
  if (!prepareOpenAiClient(this, rt, false, mode)) {
    return "";
  }

  if (mode == OaiByteLimit::None) {
    log(INFO, "🤖 OpenAI completion: byteLimit=none");
  } else if (mode == OaiByteLimit::Heap) {
    log(INFO,
        "🤖 OpenAI completion: byteLimit=heap maxTokens=%d byteBudget=%u "
        "heapFree=%u largest=%u (~%u chars)",
        rt.budget.tokens, static_cast<unsigned>(rt.budget.bytes),
        static_cast<unsigned>(rt.freeBytes),
        static_cast<unsigned>(rt.largestBlock),
        static_cast<unsigned>(rt.budget.chars));
  } else {
    log(INFO,
        "🤖 OpenAI completion: byteLimit=lfs maxTokens=%d byteBudget=%u "
        "lfsFree=%u (~%u chars)",
        rt.budget.tokens, static_cast<unsigned>(rt.budget.bytes),
        static_cast<unsigned>(rt.freeBytes),
        static_cast<unsigned>(rt.budget.chars));
  }

  OpenAIResult result = rt.client.chatCompletion(prompt);
  if (!result.ok) {
    logOpenAiError(this, "completion", result);
    return "";
  }

  if (mode != OaiByteLimit::None && result.content.size() > rt.budget.chars) {
    log(WARNING,
        "🤖 OpenAI completion: over budget (%u > %u chars), trimming to fit",
        static_cast<unsigned>(result.content.size()),
        static_cast<unsigned>(rt.budget.chars));
    return truncateForTts(result.content, rt.budget.chars);
  }

  return result.content;
}

#ifdef ESPWiFi_DAC_ENABLED
void ESPWiFi::TTS(const std::string& text, float volume, int outputPin) {
  if (text.empty()) {
    log(ERROR, "🤖 OpenAI TTS: text is empty");
    return;
  }

  OpenAI client(OpenAI::Config{});
  if (!prepareTtsClient(this, client)) {
    return;
  }

  size_t totalBytes = 0;
  size_t usedBytes = 0;
  size_t lfsFree = 0;
  getStorageInfo("lfs", totalBytes, usedBytes, lfsFree);
  const size_t writeLimit = lfsFree * kMaxLfsUsagePercent / 100;
  if (writeLimit < kMinTtsBudgetBytes) {
    log(ERROR, "🤖 OpenAI TTS: LittleFS tight (%u bytes free)",
        static_cast<unsigned>(lfsFree));
    return;
  }

  log(INFO, "🤖 OpenAI TTS: %u chars, lfsFree=%u (file cap %u bytes)",
      static_cast<unsigned>(text.size()), static_cast<unsigned>(lfsFree),
      static_cast<unsigned>(writeLimit));

  const std::string tempPath = lfsMountPoint + "/tts.wav";
  (void)remove(tempPath.c_str());

  FILE* out = fopen(tempPath.c_str(), "wb");
  if (out == nullptr) {
    log(ERROR, "🤖 OpenAI TTS: failed to open %s", tempPath.c_str());
    return;
  }

  log(INFO, "🤖 OpenAI TTS: downloading to %s", tempPath.c_str());

  TtsDownloadResult download = streamTtsToCallback(
      client, text, writeLimit, [&](const uint8_t* data, size_t len) {
        return fwrite(data, 1, len, out) == len;
      });
  fclose(out);

  auto cleanup = [&]() { (void)remove(tempPath.c_str()); };

  if (download.writeFailed) {
    log(ERROR, "🤖 OpenAI TTS: write failed at %u bytes",
        static_cast<unsigned>(download.totalBytes));
    cleanup();
    return;
  }

  if (download.totalBytes == 0) {
    logOpenAiError(this, "TTS", download.apiResult);
    cleanup();
    return;
  }

  if (!download.apiResult.ok && !download.capped) {
    logOpenAiError(this, "TTS", download.apiResult);
    cleanup();
    return;
  }

  if (download.capped) {
    log(WARNING, "🤖 OpenAI TTS: capped download at %u bytes",
        static_cast<unsigned>(download.totalBytes));
  }

  if (download.totalBytes > writeLimit || !isValidWavFile(tempPath.c_str())) {
    log(ERROR, "🤖 OpenAI TTS: invalid WAV (%u bytes, limit %u)",
        static_cast<unsigned>(download.totalBytes),
        static_cast<unsigned>(writeLimit));
    cleanup();
    return;
  }

  log(INFO, "🤖 OpenAI TTS: saved %u bytes to %s (%u%% of byteBudget %u)",
      static_cast<unsigned>(download.totalBytes), tempPath.c_str(),
      static_cast<unsigned>(
          writeLimit > 0 ? download.totalBytes * 100 / writeLimit : 0),
      static_cast<unsigned>(writeLimit));

  playAudio("/tts.wav", volume, outputPin, true);
  while (this->audioPlaying) {
    this->feedWatchDog();
  }
}

void ESPWiFi::streamTTS(const std::string& text, float volume, int outputPin) {
  if (text.empty()) {
    log(ERROR, "🤖 OpenAI Stream TTS: text is empty");
    return;
  }

  const size_t freeHeap = esp_get_free_heap_size();
  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (!heapReadyForStreaming(freeHeap, largestBlock)) {
    log(ERROR,
        "🤖 OpenAI Stream TTS: heap tight (free=%u largest=%u, need %u "
        "buffer)",
        static_cast<unsigned>(freeHeap), static_cast<unsigned>(largestBlock),
        static_cast<unsigned>(kStreamBufferMinBytes));
    return;
  }

  OpenAI client(OpenAI::Config{});
  if (!prepareTtsClient(this, client)) {
    return;
  }

  log(INFO, "🤖 OpenAI Stream TTS: %u chars, heapFree=%u largest=%u",
      static_cast<unsigned>(text.size()), static_cast<unsigned>(freeHeap),
      static_cast<unsigned>(largestBlock));

  playStreamingWav(
      volume, outputPin,
      [this, client = std::move(client),
       text](std::function<bool(const uint8_t*, size_t)> writeFn) mutable {
        OpenAIResult result = client.streamTextToSpeech(text, writeFn);
        if (!result.ok) {
          logOpenAiError(this, "Stream TTS", result);
        }
        return result.ok;
      });
  while (this->audioPlaying) {
    this->feedWatchDog();
  }
}
#endif
