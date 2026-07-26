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
    std::string model;
    std::string baseUrl;
    std::string systemMessage;
    std::string ttsModel;
    std::string ttsVoice;
    uint32_t timeoutMs = 0;
    uint32_t ttsTimeoutMs = 0;
    float ttsSpeed = 0.0f;
    // Set at runtime from LittleFS free space (99% of free bytes).
    size_t ttsByteBudget = 0;
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
  int effectiveMaxCompletionTokens() const;

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
