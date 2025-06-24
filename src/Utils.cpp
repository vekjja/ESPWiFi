#ifndef ESPWIFI_UTILS_H
#define ESPWIFI_UTILS_H

#include "ESPWiFi.h"

void ESPWiFi::runAtInterval(unsigned int interval,
                            unsigned long& lastIntervalRun,
                            std::function<void()> functionToRun) {
  unsigned long currentRunTime = millis();
  if (currentRunTime - lastIntervalRun >= interval) {
    lastIntervalRun = currentRunTime;
    functionToRun();
  }
}

String ESPWiFi::getFileExtension(const String& filename) {
  int dotIndex = filename.lastIndexOf(".");
  if (dotIndex == -1) {
    return "";
  }
  return filename.substring(dotIndex + 1);
}

String ESPWiFi::getContentType(String filename) {
  if (filename.endsWith(".html"))
    return "text/html";
  else if (filename.endsWith(".css"))
    return "text/css";
  else if (filename.endsWith(".js"))
    return "application/javascript";
  else if (filename.endsWith(".png"))
    return "image/png";
  else if (filename.endsWith(".jpg"))
    return "image/jpeg";
  else if (filename.endsWith(".gif"))
    return "image/gif";
  else if (filename.endsWith(".ico"))
    return "image/x-icon";
  else if (filename.endsWith(".xml"))
    return "text/xml";
  else if (filename.endsWith(".pdf"))
    return "application/x-pdf";
  else if (filename.endsWith(".zip"))
    return "application/x-zip";
  else if (filename.endsWith(".gz"))
    return "application/x-gzip";
  else if (filename.endsWith(".mp3"))
    return "audio/mpeg";
  // Add more MIME types here as needed
  return "text/plain";
}

#endif  // ESPWIFI_UTILS_H