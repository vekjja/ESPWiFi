// Utils.cpp
#include "ESPWiFi.h"

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
