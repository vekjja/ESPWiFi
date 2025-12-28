// Utils.cpp
#include "ESPWiFi.h"
#include "driver/uart.h"
#include "sdkconfig.h"

std::string ESPWiFi::getContentType(std::string filename) {
  if (filename.find(".html") != std::string::npos)
    return "text/html";
  else if (filename.find(".css") != std::string::npos)
    return "text/css";
  else if (filename.find(".js") != std::string::npos)
    return "application/javascript";
  else if (filename.find(".json") != std::string::npos)
    return "application/json";
  else if (filename.find(".png") != std::string::npos)
    return "image/png";
  else if (filename.find(".jpg") != std::string::npos ||
           filename.find(".jpeg") != std::string::npos)
    return "image/jpeg";
  else if (filename.find(".gif") != std::string::npos)
    return "image/gif";
  else if (filename.find(".svg") != std::string::npos)
    return "image/svg+xml";
  else if (filename.find(".ico") != std::string::npos)
    return "image/x-icon";
  return "text/plain; charset=utf-8";
}

std::string ESPWiFi::getStatusFromCode(int statusCode) {
  const char *status_text = "OK";
  if (statusCode == 201)
    status_text = "Created";
  else if (statusCode == 204)
    status_text = "No Content";
  else if (statusCode == 400)
    status_text = "Bad Request";
  else if (statusCode == 401)
    status_text = "Unauthorized";
  else if (statusCode == 404)
    status_text = "Not Found";
  else if (statusCode == 500)
    status_text = "Internal Server Error";

  char buf[32];
  (void)snprintf(buf, sizeof(buf), "%d %s", statusCode, status_text);
  return std::string(buf);
}

std::string ESPWiFi::bytesToHumanReadable(size_t bytes) {
  const char *units[] = {"B", "KB", "MB", "GB"};
  int unitIndex = 0;
  double size = (double)bytes;

  while (size >= 1024.0 && unitIndex < 3) {
    size /= 1024.0;
    unitIndex++;
  }

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unitIndex]);
  return std::string(buffer);
}

void ESPWiFi::runAtInterval(unsigned int interval,
                            unsigned long &lastIntervalRun,
                            std::function<void()> functionToRun) {
  unsigned long currentTime =
      esp_timer_get_time() / 1000; // Convert to milliseconds
  if (currentTime - lastIntervalRun >= interval) {
    functionToRun();
    lastIntervalRun = currentTime;
  }
}

// Helper function to match URI against a pattern with wildcard support
// Supports '*' to match any sequence of characters (including empty)
// Supports '?' to match any single character
bool ESPWiFi::matchPattern(const std::string &uri, const std::string &pattern) {
  size_t uriPos = 0;
  size_t patternPos = 0;
  size_t uriBackup = std::string::npos;
  size_t patternBackup = std::string::npos;

  while (uriPos < uri.length() || patternPos < pattern.length()) {
    if (patternPos < pattern.length() && pattern[patternPos] == '*') {
      // Wildcard: try matching zero or more characters
      patternBackup = ++patternPos;
      uriBackup = uriPos;
    } else if (patternPos < pattern.length() && uriPos < uri.length() &&
               (pattern[patternPos] == uri[uriPos] ||
                pattern[patternPos] == '?')) {
      // Exact match or single character wildcard
      patternPos++;
      uriPos++;
    } else if (patternBackup != std::string::npos) {
      // Backtrack: use wildcard to match more characters
      patternPos = patternBackup;
      // If we've already tried consuming the entire URI, we can't match.
      // Without this guard, uriBackup can run past the end and loop forever.
      if (uriBackup >= uri.length()) {
        return false;
      }
      uriPos = ++uriBackup;
    } else {
      return false;
    }
  }
  return true;
}

JsonDocument ESPWiFi::readRequestBody(httpd_req_t *req) {
  JsonDocument doc;

  // Get content length
  size_t content_len = req->content_len;
  if (content_len == 0 || content_len > 10240) { // Limit to 10KB
    // Return empty document if no content or too large
    return doc;
  }

  // Allocate buffer for request body
  char *content = (char *)malloc(content_len + 1);
  if (content == nullptr) {
    // Return empty document if memory allocation fails
    return doc;
  }

  // Read the request body
  int ret = httpd_req_recv(req, content, content_len);
  if (ret <= 0) {
    free(content);
    // Return empty document if read failed
    return doc;
  }

  // Null-terminate the string
  content[content_len] = '\0';
  std::string json_body(content, content_len);
  free(content);

  // Parse JSON
  DeserializationError error = deserializeJson(doc, json_body);
  if (error) {
    // Return empty document if JSON parsing fails (doc is already empty)
    JsonDocument emptyDoc;
    return emptyDoc;
  }

  return doc;
}

int ESPWiFi::getSerialBaudRate() {
  // Start with our last-known value, then prefer sdkconfig (authoritative for
  // ESP-IDF console), and finally try querying the UART driver if available.
  int baud = this->baudRate;

#if defined(CONFIG_ESP_CONSOLE_UART_BAUDRATE)
  baud = CONFIG_ESP_CONSOLE_UART_BAUDRATE;
#elif defined(CONFIG_CONSOLE_UART_BAUDRATE)
  baud = CONFIG_CONSOLE_UART_BAUDRATE;
#endif

  int uart_num = UART_NUM_0;
#if defined(CONFIG_ESP_CONSOLE_UART_NUM)
  uart_num = CONFIG_ESP_CONSOLE_UART_NUM;
#elif defined(CONFIG_CONSOLE_UART_NUM)
  uart_num = CONFIG_CONSOLE_UART_NUM;
#endif

  uint32_t driverBaud = 0;
  esp_err_t err = uart_get_baudrate((uart_port_t)uart_num, &driverBaud);
  if (err == ESP_OK && driverBaud > 0) {
    baud = (int)driverBaud;
  }

  return baud;
}
