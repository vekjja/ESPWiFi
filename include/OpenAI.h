#ifndef OPENAI_H
#define OPENAI_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct OpenAIChatMessage {
  std::string role;
  std::string content;
};

struct OpenAIResult {
  bool ok = false;
  std::string content;
  std::string error;
  int httpStatus = 0;
};

class OpenAI {
 public:
  struct Config {
    std::string apiKey;
    std::string model = "gpt-4o-mini";
    std::string baseUrl = "https://api.openai.com";
    std::string systemMessage;
    std::string ttsModel = "tts-1";
    std::string ttsVoice = "alloy";
    uint32_t timeoutMs = 15000;
    uint32_t ttsTimeoutMs = 60000;
    size_t maxResponseBytes = 16384;
    // Chat: limits how long the model reply can be (smaller reply -> smaller TTS).
    int maxTokens = 120;
    // TTS: truncate spoken text (OpenAI max 4096 chars; shorter -> smaller WAV).
    size_t maxTtsChars = 265;
    // TTS: 0.25-4.0; values >1 shorten playback duration.
    float ttsSpeed = 1.0f;
    // Abort TTS download if WAV exceeds this many bytes (fits LittleFS budget).
    size_t maxTtsBytes = 983040;
  };

  explicit OpenAI(Config config);

  bool isConfigured() const;
  const Config& config() const { return config_; }

  OpenAIResult chatCompletion(const std::string& prompt) const;
  OpenAIResult chatCompletion(
      const std::vector<OpenAIChatMessage>& messages) const;
  OpenAIResult streamTextToSpeech(
      const std::string& text,
      const std::function<bool(const uint8_t* data, size_t len)>& onData) const;

  std::string prepareTtsText(const std::string& text) const;
  size_t effectiveMaxTtsChars() const;

 private:
  Config config_;

  std::string buildChatRequestBody(
      const std::vector<OpenAIChatMessage>& messages) const;
  std::string buildTtsRequestBody(const std::string& text) const;
  OpenAIResult postJson(const std::string& path,
                        const std::string& requestBody) const;
  OpenAIResult postStream(const std::string& path,
                          const std::string& requestBody, uint32_t timeoutMs,
                          const std::function<bool(const uint8_t*, size_t)>&
                              onData) const;
  OpenAIResult parseChatCompletionResponse(const std::string& responseBody,
                                           int httpStatus) const;
};

#endif  // OPENAI_H
