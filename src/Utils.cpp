// Utils.cpp
#include "ESPWiFi.h"
#include "driver/uart.h"
#include "sdkconfig.h"
#include <cctype>
#include <cstring>

namespace {
inline int hexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  c = (char)std::tolower((unsigned char)c);
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  return -1;
}

std::string urlDecode(const char *in) {
  if (in == nullptr) {
    return std::string();
  }
  const size_t n = strlen(in);
  std::string out;
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const char c = in[i];
    if (c == '+') {
      out.push_back(' ');
      continue;
    }
    if (c == '%' && (i + 2) < n) {
      const int hi = hexNibble(in[i + 1]);
      const int lo = hexNibble(in[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back((char)((hi << 4) | lo));
        i += 2;
        continue;
      }
      // Invalid escape; fall through and keep the '%' literal.
    }
    out.push_back(c);
  }
  return out;
}
} // namespace

bool ESPWiFi::fileExists(const std::string &fullPath) {
  struct stat st;
  if (stat(fullPath.c_str(), &st) == 0) {
    return !S_ISDIR(st.st_mode);
  }
  return false;
}

bool ESPWiFi::dirExists(const std::string &fullPath) {
  struct stat st;
  if (stat(fullPath.c_str(), &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  return false;
}

bool ESPWiFi::mkDir(const std::string &fullPath) {
  if (dirExists(fullPath)) {
    return true;
  }

  // Create parent directories if needed (mkdir -p style)
  size_t pos = fullPath.find_last_of('/');
  if (pos != std::string::npos && pos > 0) {
    std::string parent = fullPath.substr(0, pos);
    if (!dirExists(parent)) {
      (void)mkDir(parent);
    }
  }

  if (::mkdir(fullPath.c_str(), 0755) == 0) {
    return true;
  }

  // Check if directory was actually created despite the error
  return dirExists(fullPath);
}

std::string ESPWiFi::getQueryParam(httpd_req_t *req, const char *key) {
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    char *buf = (char *)malloc(buf_len);
    if (buf) {
      if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
        // Keep in sync with path limits in routes (e.g. /api/files allows 255).
        // Note: this is the *decoded* max we care about; encoded values may be
        // longer but should still fit under max_uri_len.
        char value[256];
        if (httpd_query_key_value(buf, key, value, sizeof(value)) == ESP_OK) {
          std::string result = urlDecode(value);
          free(buf);
          return result;
        }
      }
      free(buf);
    }
  }
  return "";
}

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
  const char *status_text = "IDK This Status Code";
  if (statusCode == 200)
    status_text = "OK";
  if (statusCode == 201)
    status_text = "Created";
  else if (statusCode == 204)
    status_text = "No Content";
  else if (statusCode == 400)
    status_text = "Bad Request";
  else if (statusCode == 401)
    status_text = "Unauthorized";
  else if (statusCode == 403)
    status_text = "Forbidden";
  else if (statusCode == 404)
    status_text = "Not Found";
  else if (statusCode == 503)
    status_text = "Service Unavailable";
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
bool ESPWiFi::matchPattern(std::string_view uri, std::string_view pattern) {
  // Fast paths (common in config): exact match patterns, or global wildcard.
  if (pattern == "*") {
    return true;
  }
  if (pattern.find_first_of("*?") == std::string_view::npos) {
    return uri == pattern;
  }

  const size_t uriLen = uri.size();
  const size_t patternLen = pattern.size();

  // Hard bound to keep this watchdog-safe even on adversarial input.
  // This algorithm is linear-ish, but the bound guarantees termination.
  uint64_t maxOps =
      (uint64_t)(uriLen + 1) * (uint64_t)(patternLen + 1) * 2ULL + 16ULL;
  uint64_t ops = 0;

  size_t uriPos = 0;
  size_t patternPos = 0;
  size_t uriBackup = std::string_view::npos;
  size_t patternBackup = std::string_view::npos;

  while (uriPos < uriLen || patternPos < patternLen) {
    if (++ops > maxOps) {
      return false;
    }

    if (patternPos < patternLen && pattern[patternPos] == '*') {
      // Wildcard: remember positions to allow consuming more URI chars later.
      patternBackup = ++patternPos;
      uriBackup = uriPos;
      continue;
    }

    if (patternPos < patternLen && uriPos < uriLen &&
        (pattern[patternPos] == uri[uriPos] || pattern[patternPos] == '?')) {
      patternPos++;
      uriPos++;
      continue;
    }

    if (patternBackup != std::string_view::npos) {
      // Backtrack: extend what '*' consumes by one.
      patternPos = patternBackup;
      if (uriBackup >= uriLen) {
        return false;
      }
      uriPos = ++uriBackup;
      continue;
    }

    return false;
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
