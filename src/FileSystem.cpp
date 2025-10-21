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
    logf("\t%s (%s)\n", file.name(), bytesToHumanReadable(file.size()).c_str());
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

  logf("📂 Reading file: %s\n", filePath.c_str());
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
  logf("📂 Written to file: %s\n", filePath.c_str());
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
  logf("📂 Appended to file: %s\n", filePath.c_str());
}

void ESPWiFi::deleteFile(FS *fs, const String &filePath) {
  if (!fs) {
    logError("File system is null");
    return;
  }

  if (fs->remove(filePath)) {
    logf("📂 Deleted file: %s\n", filePath.c_str());
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
    logf("📁 Directory already exists: %s\n", dirPath.c_str());
    return true;
  }

  if (fs->mkdir(dirPath)) {
    logf("📁 Created directory: %s\n", dirPath.c_str());
    return true;
  } else {
    logError("Failed to create directory: " + dirPath);
    return false;
  }
}

bool ESPWiFi::deleteDirectoryRecursive(FS *fs, const String &dirPath) {
  if (!fs) {
    logError("File system is null");
    return false;
  }

  if (!dirExists(fs, dirPath)) {
    logf("📁 Directory does not exist: %s\n", dirPath.c_str());
    return true; // Directory doesn't exist, consider it "deleted"
  }

  File dir = fs->open(dirPath);
  if (!dir || !dir.isDirectory()) {
    logError("Failed to open directory: " + dirPath);
    return false;
  }

  int fileCount = 0;
  const int MAX_FILES_PER_BATCH =
      50; // Process files in batches to prevent memory issues

  // List all files and directories in the directory
  File file = dir.openNextFile();
  while (file) {
    String filePath = file.path();
    if (file.isDirectory()) {
      // Recursively delete subdirectory
      if (!deleteDirectoryRecursive(fs, filePath)) {
        file.close();
        dir.close();
        return false;
      }
    } else {
      // Delete file
      if (!fs->remove(filePath)) {
        logError("Failed to delete file: " + filePath);
        file.close();
        dir.close();
        return false;
      }
      fileCount++;

      // Log every 50th file to reduce log spam
      if (fileCount % 50 == 0) {
        logf("🗑️ Deleted %d files from: %s\n", fileCount, dirPath.c_str());
      }

      // Yield control every batch to prevent watchdog timeout
      if (fileCount % MAX_FILES_PER_BATCH == 0) {
        delay(10); // Small delay to yield control
        yield();   // Allow other tasks to run
      }
    }
    file.close();
    file = dir.openNextFile();
  }
  dir.close();

  // Now remove the empty directory
  if (fs->rmdir(dirPath)) {
    logf("🗑️ Deleted directory: %s (%d files)\n", dirPath.c_str(), fileCount);
    return true;
  } else {
    logError("Failed to delete directory: " + dirPath);
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

  // API endpoint for file browser JSON data
  webServer->on("/api/files", HTTP_GET, [this](AsyncWebServerRequest *request) {
    // Handle CORS preflight requests
    if (request->method() == HTTP_OPTIONS) {
      handleCorsPreflight(request);
      return;
    }

    String fsParam = "sd";
    String path = "/";

    if (request->hasParam("fs")) {
      fsParam = request->getParam("fs")->value();
    }
    if (request->hasParam("path")) {
      path = request->getParam("path")->value();
      if (!path.startsWith("/"))
        path = "/" + path;
    }

    FS *filesystem = nullptr;
    if (fsParam == "sd" && sdCardInitialized && fs) {
      filesystem = fs;
    } else if (fsParam == "lfs") {
      filesystem = &LittleFS;
    }

    if (!filesystem) {
      AsyncWebServerResponse *response = request->beginResponse(
          404, "application/json", "{\"error\":\"File system not available\"}");
      addCORS(response);
      request->send(response);
      return;
    }

    File root = filesystem->open(path, "r");
    if (!root || !root.isDirectory()) {
      AsyncWebServerResponse *response = request->beginResponse(
          404, "application/json", "{\"error\":\"Directory not found\"}");
      addCORS(response);
      request->send(response);
      return;
    }

    JsonDocument jsonDoc;
    JsonArray filesArray = jsonDoc["files"].to<JsonArray>();

    File file = root.openNextFile();
    int fileCount = 0;
    const int maxFiles = 1000; // Prevent infinite loops

    while (file && fileCount < maxFiles) {
      try {
        JsonObject fileObj = filesArray.add<JsonObject>();
        fileObj["name"] = file.name();
        fileObj["path"] = path + (path.endsWith("/") ? "" : "/") + file.name();
        fileObj["isDirectory"] = file.isDirectory();
        fileObj["size"] = file.size();
        fileObj["modified"] = file.getLastWrite();
        fileCount++;
      } catch (...) {
        // Skip problematic files
        break;
      }
      file = root.openNextFile();
    }

    root.close();

    String jsonResponse;
    serializeJson(jsonDoc, jsonResponse);
    AsyncWebServerResponse *response =
        request->beginResponse(200, "application/json", jsonResponse);
    addCORS(response);
    request->send(response);
  });

  // API endpoint for file rename
  webServer->on(
      "/api/files/rename", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }

        // Get parameters from URL
        String fsParam = "";
        String oldPath = "";
        String newName = "";

        if (request->hasParam("fs")) {
          fsParam = request->getParam("fs")->value();
        }
        if (request->hasParam("oldPath")) {
          oldPath = request->getParam("oldPath")->value();
        }
        if (request->hasParam("newName")) {
          newName = request->getParam("newName")->value();
        }

        // Validate parameters
        if (fsParam.length() == 0 || oldPath.length() == 0 ||
            newName.length() == 0) {
          AsyncWebServerResponse *response = request->beginResponse(
              400, "application/json", "{\"error\":\"Missing parameters\"}");
          addCORS(response);
          request->send(response);
          return;
        }

        FS *filesystem = nullptr;
        if (fsParam == "sd" && sdCardInitialized && fs) {
          filesystem = fs;
        } else if (fsParam == "lfs") {
          filesystem = &LittleFS;
        }

        if (!filesystem) {
          AsyncWebServerResponse *response = request->beginResponse(
              404, "application/json",
              "{\"error\":\"File system not available\"}");
          addCORS(response);
          request->send(response);
          return;
        }

        // Get directory path and construct new path
        String dirPath = oldPath.substring(0, oldPath.lastIndexOf('/'));
        String newPath = dirPath + "/" + newName;

        if (filesystem->rename(oldPath, newPath)) {
          logf("📁 Renamed file: %s -> %s\n", oldPath.c_str(), newName.c_str());
          AsyncWebServerResponse *response = request->beginResponse(
              200, "application/json", "{\"success\":true}");
          addCORS(response);
          request->send(response);
        } else {
          AsyncWebServerResponse *response = request->beginResponse(
              500, "application/json", "{\"error\":\"Failed to rename file\"}");
          addCORS(response);
          request->send(response);
        }
      });

  // API endpoint for file deletion
  webServer->on(
      "/api/files/delete", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }

        // Get parameters from URL
        String fsParam = "";
        String filePath = "";

        if (request->hasParam("fs")) {
          fsParam = request->getParam("fs")->value();
        }
        if (request->hasParam("path")) {
          filePath = request->getParam("path")->value();
        }

        // Validate parameters
        if (fsParam.length() == 0 || filePath.length() == 0) {
          AsyncWebServerResponse *response = request->beginResponse(
              400, "application/json", "{\"error\":\"Missing parameters\"}");
          addCORS(response);
          request->send(response);
          return;
        }

        // Validate file path length
        if (filePath.length() > 255) {
          AsyncWebServerResponse *response = request->beginResponse(
              400, "application/json", "{\"error\":\"Invalid file path\"}");
          addCORS(response);
          request->send(response);
          return;
        }

        FS *filesystem = nullptr;
        if (fsParam == "sd" && sdCardInitialized && fs) {
          filesystem = fs;
        } else if (fsParam == "lfs") {
          filesystem = &LittleFS;
        }

        if (!filesystem) {
          AsyncWebServerResponse *response = request->beginResponse(
              404, "application/json",
              "{\"error\":\"File system not available\"}");
          addCORS(response);
          request->send(response);
          return;
        }

        // Additional safety check - ensure file system is still mounted
        if (fsParam == "sd" && !sdCardInitialized) {
          AsyncWebServerResponse *response = request->beginResponse(
              500, "application/json", "{\"error\":\"SD card not mounted\"}");
          addCORS(response);
          request->send(response);
          return;
        }

        // Check if file exists before attempting deletion
        if (!filesystem->exists(filePath)) {
          AsyncWebServerResponse *response = request->beginResponse(
              404, "application/json", "{\"error\":\"File not found\"}");
          addCORS(response);
          request->send(response);
          return;
        }

        // Prevent deletion of system files
        if (filePath == "/config.json" || filePath == "/log" ||
            filePath.startsWith("/system/") || filePath.startsWith("/boot/")) {
          AsyncWebServerResponse *response = request->beginResponse(
              403, "application/json",
              "{\"error\":\"Cannot delete system files\"}");
          addCORS(response);
          request->send(response);
          return;
        }

        // Check if it's a directory or file
        File file = filesystem->open(filePath);
        bool isDirectory = file.isDirectory();
        file.close();

        // Attempt to delete the file/directory with error handling
        bool deleteSuccess = false;
        try {
          // Add a small delay to ensure file system is ready
          delay(10);

          // Double-check file system is still valid
          if (filesystem && filesystem->exists(filePath)) {
            if (isDirectory) {
              // Delete directory recursively
              deleteSuccess = deleteDirectoryRecursive(filesystem, filePath);
            } else {
              // Delete file
              deleteSuccess = filesystem->remove(filePath);
            }
          }
        } catch (...) {
          deleteSuccess = false;
        }

        if (deleteSuccess) {
          if (isDirectory) {
            logf("🗑️ Deleted directory: %s\n", filePath.c_str());
          } else {
            logf("🗑️ Deleted file: %s\n", filePath.c_str());
          }
          AsyncWebServerResponse *response = request->beginResponse(
              200, "application/json", "{\"success\":true}");
          addCORS(response);
          request->send(response);
        } else {
          AsyncWebServerResponse *response = request->beginResponse(
              500, "application/json", "{\"error\":\"Failed to delete file\"}");
          addCORS(response);
          request->send(response);
        }
      });

  // API endpoint for file upload using OTA pattern
  webServer->on(
      "/api/files/upload", HTTP_POST,
      [this](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }
        // This will be handled by the upload handler
      },
      [this](AsyncWebServerRequest *request, String filename, size_t index,
             uint8_t *data, size_t len, bool final) {
        handleFileUpload(request, filename, index, data, len, final);
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

void ESPWiFi::handleFileUpload(AsyncWebServerRequest *request, String filename,
                               size_t index, uint8_t *data, size_t len,
                               bool final) {
  // Static variables to maintain state across multiple calls (like OTA system)
  static File currentFile;
  static size_t currentSize = 0;
  static String currentFs = "";
  static String currentPath = "";

  if (index == 0) {
    // First chunk - get parameters and initialize (URL parameters like OTA)
    if (request->hasParam("fs")) {
      currentFs = request->getParam("fs")->value();
    }
    if (request->hasParam("path")) {
      currentPath = request->getParam("path")->value();
    }

    // Validate parameters
    if (currentFs.length() == 0 || currentPath.length() == 0) {
      logError("Missing fs or path parameters for file upload");
      request->send(400, "application/json",
                    "{\"error\":\"Missing parameters\"}");
      return;
    }

    if (!currentPath.startsWith("/"))
      currentPath = "/" + currentPath;

    String filePath =
        currentPath + (currentPath.endsWith("/") ? "" : "/") + filename;

    logf("📁 Starting file upload: %s\n", filename.c_str());

    // Determine filesystem
    FS *filesystem = nullptr;
    if (currentFs == "sd" && sdCardInitialized && fs) {
      filesystem = fs;
    } else if (currentFs == "lfs") {
      filesystem = &LittleFS;
    }

    if (filesystem) {
      currentFile = filesystem->open(filePath, "w");
      if (!currentFile) {
        logError("Failed to create file for upload");
        request->send(500, "application/json",
                      "{\"error\":\"Failed to create file\"}");
        return;
      }
    } else {
      logError("File system not available");
      request->send(404, "application/json",
                    "{\"error\":\"File system not available\"}");
      return;
    }

    currentSize = 0;
  }

  // Write data chunk
  if (currentFile && len > 0) {
    size_t bytesWritten = currentFile.write(data, len);
    if (bytesWritten != len) {
      logError("Failed to write all data to file");
      currentFile.close();
      request->send(500, "application/json",
                    "{\"error\":\"File write failed\"}");
      return;
    }
    currentSize += len;

    // Yield control periodically for large files to prevent watchdog timeout
    if (currentSize % 8192 == 0) { // Every 8KB
      yield();
    }
  }

  if (final) {
    // Last chunk - close file and send response
    if (currentFile) {
      currentFile.close();
      logf("📁 File uploaded: %s (%d bytes)\n", filename.c_str(), currentSize);
    }

    // Reset static variables for next upload
    currentFs = "";
    currentPath = "";
    currentSize = 0;

    AsyncWebServerResponse *response =
        request->beginResponse(200, "application/json", "{\"success\":true}");
    addCORS(response);
    request->send(response);
  }
}

#endif // ESPWiFi_FileSystem