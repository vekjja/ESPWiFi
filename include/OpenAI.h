#ifndef OPENAI_H
#define OPENAI_H

#include <cstdint>
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
    uint32_t timeoutMs = 15000;
    size_t maxResponseBytes = 16384;
  };

  explicit OpenAI(Config config);

  bool isConfigured() const;
  const Config& config() const { return config_; }

  OpenAIResult chatCompletion(const std::string& prompt) const;
  OpenAIResult chatCompletion(
      const std::vector<OpenAIChatMessage>& messages) const;

 private:
  Config config_;

  std::string buildChatRequestBody(
      const std::vector<OpenAIChatMessage>& messages) const;
  OpenAIResult postJson(const std::string& path,
                        const std::string& requestBody) const;
  OpenAIResult parseChatCompletionResponse(const std::string& responseBody,
                                           int httpStatus) const;
};

#endif  // OPENAI_H
