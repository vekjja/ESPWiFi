#ifndef ESPWiFi_FileSystem
#define ESPWiFi_FileSystem
#include <LittleFS.h>
#include <SD.h>
#include <SD_MMC.h>

#include "ESPWiFi.h"

void ESPWiFi::initLittleFS() {
  if (littleFsInitialized) {
    return;
  }
  if (!LittleFS.begin()) {
    logError("  Failed to mount LittleFS");
    return;
  }

  fs = &LittleFS;
  littleFsInitialized = true;
  log("💾 LittleFS Initialized:");

  size_t totalBytes, usedBytes;

#ifdef ESP8266
  FSInfo fs_info;
  LittleFS.info(fs_info);
  totalBytes = fs_info.totalBytes;
  usedBytes = fs_info.usedBytes;
#elif defined(ESP32)
  totalBytes = LittleFS.totalBytes();
  usedBytes = LittleFS.usedBytes();
#endif

  logf("\tUsed: %s\n", bytesToHumanReadable(usedBytes).c_str());
  logf("\tFree: %s\n", bytesToHumanReadable(totalBytes - usedBytes).c_str());
  logf("\tTotal: %s\n", bytesToHumanReadable(totalBytes).c_str());
}

void ESPWiFi::initSDCard() {
  if (sdCardInitialized) {
    return;
  }

#if defined(CONFIG_IDF_TARGET_ESP32) // ESP32-CAM
  if (!SD_MMC.begin()) {
    log("⚠️  Failed to mount SD card");
    return;
  }
  fs = &SD_MMC;
  size_t totalBytes = SD_MMC.totalBytes();
  size_t usedBytes = SD_MMC.usedBytes();
#else // ESP32-S2, ESP32-S3, ESP32-C3

#if defined(CONFIG_IDF_TARGET_ESP32S3) // ESP32-S3
  int sdCardPin = 21;
#endif

  if (!SD.begin(sdCardPin)) {
    log("⚠️  Failed to mount SD card");
    return;
  }
  fs = &SD;

  size_t totalBytes = SD.totalBytes();
  size_t usedBytes = SD.usedBytes();
#endif

  sdCardInitialized = true;
  log("💾 SD Card Initialized:");

  logf("\tUsed: %s\n", bytesToHumanReadable(usedBytes).c_str());
  logf("\tFree: %s\n", bytesToHumanReadable(totalBytes - usedBytes).c_str());
  logf("\tTotal: %s\n", bytesToHumanReadable(totalBytes).c_str());
}

void ESPWiFi::listFiles(FS *fs) {
  if (!fs) {
    logError("File system is null");
    return;
  }

  log("📂 Listing Files:");
  File root = fs->open("/", "r");
  if (!root) {
    logError("Failed to open root directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    logf("\t%s (%s)", file.name(), bytesToHumanReadable(file.size()).c_str());
    file = root.openNextFile();
  }

  root.close();
}

void ESPWiFi::readFile(FS *fs, const String &filePath) {
  if (!fs) {
    logError("File system is null");
    return;
  }

  File file = fs->open(filePath, "r");
  if (!file) {
    logError("Failed to open file: " + filePath);
    return;
  }

  logf("📂 Reading file: %s", filePath.c_str());
  while (file.available()) {
    Serial.write(file.read());
  }

  file.close();
}

void ESPWiFi::writeFile(FS *fs, const String &filePath, const String &data) {
  if (!fs) {
    logError("File system is null");
    return;
  }

  File file = fs->open(filePath, "w");
  if (!file) {
    logError("Failed to open file for writing: " + filePath);
    return;
  }

  file.print(data);
  file.close();
  logf("📂 Written to file: %s", filePath.c_str());
}

void ESPWiFi::appendToFile(FS *fs, const String &filePath, const String &data) {
  if (!fs) {
    logError("File system is null");
    return;
  }

  File file = fs->open(filePath, "a");
  if (!file) {
    logError("Failed to open file for appending: " + filePath);
    return;
  }

  file.print(data);
  file.close();
  logf("📂 Appended to file: %s", filePath.c_str());
}

void ESPWiFi::deleteFile(FS *fs, const String &filePath) {
  if (!fs) {
    logError("File system is null");
    return;
  }

  if (fs->remove(filePath)) {
    logf("📂 Deleted file: %s", filePath.c_str());
  } else {
    logError("Failed to delete file: " + filePath);
  }
}

bool ESPWiFi::fileExists(FS *fs, const String &filePath) {
  if (!fs) {
    logError("File system is null");
    return false;
  }

  File file = fs->open(filePath, "r");
  if (file) {
    file.close();
    return true;
  }
  return false;
}

bool ESPWiFi::dirExists(FS *fs, const String &dirPath) {
  if (!fs) {
    logError("File system is null");
    return false;
  }

  File dir = fs->open(dirPath, "r");
  if (dir) {
    bool isDir = dir.isDirectory();
    dir.close();
    return isDir;
  }
  return false;
}

bool ESPWiFi::mkDir(FS *fs, const String &dirPath) {
  if (!fs) {
    logError("File system is null");
    return false;
  }

  if (dirExists(fs, dirPath)) {
    logf("📁 Directory already exists: %s", dirPath.c_str());
    return true;
  }

  if (fs->mkdir(dirPath)) {
    logf("📁 Created directory: %s", dirPath.c_str());
    return true;
  } else {
    logError("Failed to create directory: " + dirPath);
    return false;
  }
}

void ESPWiFi::srvFiles() {
  initWebServer();

  // Generic file requests - updated to handle both LittleFS and SD card
  webServer->onNotFound([this](AsyncWebServerRequest *request) {
    // Handle CORS preflight requests
    if (request->method() == HTTP_OPTIONS) {
      handleCorsPreflight(request);
      return;
    }

    String path = request->url();

    // Check for filesystem prefix (e.g., /sd/path/to/file or
    // /lfs/path/to/file)
    String fsPrefix = "";
    String actualPath = path;

    if (path.startsWith("/sd/")) {
      fsPrefix = "/sd";
      actualPath = path.substring(3); // Remove "/sd"
      if (!actualPath.startsWith("/"))
        actualPath = "/" + actualPath;
    } else if (path.startsWith("/lfs/")) {
      fsPrefix = "/lfs";
      actualPath = path.substring(4); // Remove "/lfs"
      if (!actualPath.startsWith("/"))
        actualPath = "/" + actualPath;
    }

    // Try to serve from the appropriate filesystem
    if (fsPrefix == "/sd" && sdCardInitialized && fs &&
        fs->exists(actualPath)) {
      String contentType = getContentType(actualPath);
      AsyncWebServerResponse *response =
          request->beginResponse(*fs, actualPath, contentType);
      addCORS(response);
      request->send(response);
    } else if (fsPrefix == "/lfs" && LittleFS.exists(actualPath)) {
      String contentType = getContentType(actualPath);
      AsyncWebServerResponse *response =
          request->beginResponse(LittleFS, actualPath, contentType);
      addCORS(response);
      request->send(response);
    } else if (fsPrefix == "" && LittleFS.exists(path)) {
      // Default to LittleFS for paths without prefix
      String contentType = getContentType(path);
      AsyncWebServerResponse *response =
          request->beginResponse(LittleFS, path, contentType);
      addCORS(response);
      request->send(response);
    } else {
      AsyncWebServerResponse *response =
          request->beginResponse(404, "text/plain", "404: Not Found");
      addCORS(response);
      request->send(response);
    }
  });

  webServer->on("/files", HTTP_GET, [this](AsyncWebServerRequest *request) {
    String html = "<!DOCTYPE html><html><head><meta "
                  "charset='UTF-8'><title>ESPWiFi Files</title>";
    html += "<style>body{font-family:sans-serif;background:#181a1b;color:#"
            "e8eaed;margin:0;padding:2em;}";
    html += "h2{color:#7FF9E9;}h3{color:#7FF9E9;margin-top:2em;border-bottom:"
            "2px solid #7FF9E9;padding-bottom:0.5em;}";
    html += "ul{background:#23272a;border-radius:8px;box-shadow:0 2px 8px "
            "#0008;max-width:700px;margin:auto;padding:1em;}";
    html += "li{margin:0.5em "
            "0;}a{color:#7FF9E9;text-decoration:none;font-weight:bold;}a:hover{"
            "text-decoration:underline;}";
    html += ".folder{color:#7FF9E9;font-weight:bold;}.file{color:#e8eaed;}";
    html += ".fs-section{margin-bottom:2em;}.fs-header{background:#2d3748;"
            "padding:1em;border-radius:8px;margin-bottom:1em;}";
    html += "::-webkit-scrollbar{background:#23272a;}::-webkit-scrollbar-thumb{"
            "background:#333;border-radius:8px;}";
    html += "</style></head><body>";

    html += "<h2>📁 ESPWiFi Files</h2>";

    // Get filesystem and path parameters
    String fsParam = "";
    String path = "/";

    if (request->hasParam("fs")) {
      fsParam = request->getParam("fs")->value();
    }
    if (request->hasParam("dir")) {
      path = request->getParam("dir")->value();
      if (!path.startsWith("/"))
        path = "/" + path;
    }

    // Helper function to generate file listing HTML
    auto generateFileListing = [&](FS *filesystem, const String &fsName,
                                   const String &fsPrefix,
                                   const String &currentPath) -> String {
      String result = "";

      if (!filesystem) {
        result += "<div class='fs-section'>";
        result += "<div class='fs-header'><h3>💾 " + fsName +
                  " (Not Available)</h3></div>";
        result += "<p>File system not mounted or available.</p></div>";
        return result;
      }

      File root = filesystem->open(currentPath, "r");
      if (!root || !root.isDirectory()) {
        result += "<div class='fs-section'>";
        result += "<div class='fs-header'><h3>💾 " + fsName + "</h3></div>";
        result += "<p>Directory not found: " + currentPath + "</p></div>";
        return result;
      }

      result += "<div class='fs-section'>";
      result += "<div class='fs-header'><h3>💾 " + fsName + "</h3></div>";
      result += "<ul>";

      // Add parent directory link if not root
      if (currentPath != "/") {
        String parent = currentPath;
        if (parent.endsWith("/"))
          parent = parent.substring(0, parent.length() - 1);
        int lastSlash = parent.lastIndexOf('/');
        if (lastSlash > 0) {
          parent = parent.substring(0, lastSlash);
        } else {
          parent = "/";
        }
        String parentQuery = "?fs=" + fsPrefix + "&dir=" + parent;
        if (parent.startsWith("/"))
          parent = parent.substring(1);
        result += "<li class='folder'>⬆️ <a href='/files" + parentQuery +
                  "'>../</a></li>";
      }

      File file = root.openNextFile();
      while (file) {
        String fname = String(file.name());
        String displayName = fname;
        if (fname.startsWith(currentPath) && currentPath != "/") {
          displayName = fname.substring(currentPath.length());
          if (displayName.startsWith("/"))
            displayName = displayName.substring(1);
        }
        if (displayName == "")
          displayName = fname;

        if (file.isDirectory()) {
          String subdirPath = currentPath;
          if (!subdirPath.endsWith("/"))
            subdirPath += "/";
          subdirPath += displayName;
          String subdirQuery = "?fs=" + fsPrefix + "&dir=" + subdirPath;
          if (subdirPath.startsWith("/"))
            subdirQuery = "?fs=" + fsPrefix + "&dir=" + subdirPath.substring(1);
          result += "<li class='folder'>📁 <a href='/files" + subdirQuery +
                    "'>" + displayName + "/</a></li>";
        } else {
          String filePath = fsPrefix + currentPath;
          if (!filePath.endsWith("/"))
            filePath += "/";
          filePath += displayName;
          if (!filePath.startsWith("/"))
            filePath = "/" + filePath;
          result += "<li class='file'>📄 <a href='" + filePath +
                    "' target='_blank'>" + displayName + "</a></li>";
        }
        file = root.openNextFile();
      }

      result += "</ul></div>";
      return result;
    };

    // Show LittleFS files
    if (fsParam == "" || fsParam == "lfs") {
      html += generateFileListing(&LittleFS, "LittleFS", "lfs", path);
    }

    // Show SD Card files (if available)
    if (fsParam == "" || fsParam == "sd") {
      FS *sdFS = nullptr;
      if (sdCardInitialized && fs) {
        sdFS = fs;
      }
      html += generateFileListing(sdFS, "SD Card", "sd", path);
    }

    html += "</body></html>";

    AsyncWebServerResponse *response =
        request->beginResponse(200, "text/html", html);
    addCORS(response);
    request->send(response);
  });
}

#endif // ESPWiFi_FileSystem