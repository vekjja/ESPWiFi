#ifndef ESPWiFi_UTILS
#define ESPWiFi_UTILS

#include "ESPWiFi.h"

void ESPWiFi::runAtInterval(unsigned int interval,
                            unsigned long &lastIntervalRun,
                            std::function<void()> functionToRun) {
  unsigned long currentRunTime = millis();
  if (currentRunTime - lastIntervalRun >= interval) {
    lastIntervalRun = currentRunTime;
    functionToRun();
  }
}

String ESPWiFi::getFileExtension(const String &filename) {
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
  else if (filename.endsWith(".wav"))
    return "audio/wav";
  else if (filename.endsWith(".json"))
    return "application/json";
  else if (filename.endsWith(".avi"))
    return "video/x-msvideo";
  else if (filename.endsWith(".mjpg") || filename.endsWith(".mjpeg"))
    return "video/x-motion-jpeg";
  else if (filename.endsWith(".mp4"))
    return "video/mp4";
  else if (filename.endsWith(".webm"))
    return "video/webm";
  else if (filename.endsWith(".mkv"))
    return "video/x-matroska";
  else if (filename.endsWith(".mov"))
    return "video/quicktime";
  else if (filename.endsWith("log"))
    return "text/plain; charset=utf-8";
  else
    return "text/plain";
}

String ESPWiFi::bytesToHumanReadable(size_t bytes) {
  const char *units[] = {"B", "KB", "MB", "GB"};
  int unitIndex = 0;
  double size = bytes;

  while (size >= 1024.0 && unitIndex < 3) {
    size /= 1024.0;
    unitIndex++;
  }

  if (unitIndex == 0) {
    return String((int)size) + " " + units[unitIndex];
  } else {
    return String(size, 1) + " " + units[unitIndex];
  }
}

#endif // ESPWiFi_UTILS