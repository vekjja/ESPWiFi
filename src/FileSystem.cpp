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
    log(ERROR, "💾 Failed to mount LittleFS");
    return;
  }

  lfs = &LittleFS;
  littleFsInitialized = true;
  log(INFO, "💾 LittleFS Initialized");
}

void ESPWiFi::initSDCard() {
  if (sdCardInitialized) {
    return;
  }

#if defined(CONFIG_IDF_TARGET_ESP32) // ESP32-CAM

  if (!SD_MMC.begin()) {
    config["sd"]["enabled"] = false;
    log(WARNING, "💾 Failed to mount SD card");
    return;
  }
  sd = &SD_MMC;
  config["sd"]["enabled"] = true;

#else                                  // ESP32-S2, ESP32-S3, ESP32-C3
#if defined(CONFIG_IDF_TARGET_ESP32S3) // ESP32-S3
  int sdCardPin = 21;
#else
  int sdCardPin = 4;
#endif

  if (!SD.begin(sdCardPin)) {
    config["sd"]["enabled"] = false;
    log(WARNING, "💾 Failed to mount SD card");
    return;
  }
  sd = &SD;
  config["sd"]["enabled"] = true;

#endif // ESP32-S2, ESP32-S3, ESP32-C3

  sdCardInitialized = true;
  log(INFO, "💾 SD Card Initialized");
}

String ESPWiFi::sanitizeFilename(const String &filename) {
  String sanitized = filename;
  sanitized.replace(" ", "_");

  for (int i = 0; i < sanitized.length(); i++) {
    char c = sanitized.charAt(i);
    if (!isalnum(c) && c != '.' && c != '-' && c != '_') {
      sanitized.setCharAt(i, '_');
    }
  }

  while (sanitized.indexOf("__") != -1) {
    sanitized.replace("__", "_");
  }

  sanitized.trim();
  if (sanitized.startsWith("_"))
    sanitized = sanitized.substring(1);
  if (sanitized.endsWith("_"))
    sanitized = sanitized.substring(0, sanitized.length() - 1);

  return sanitized;
}

void ESPWiFi::getStorageInfo(const String &fsParam, size_t &totalBytes,
                             size_t &usedBytes, size_t &freeBytes) {
  totalBytes = 0;
  usedBytes = 0;
  freeBytes = 0;

  if (fsParam == "lfs") {
    totalBytes = LittleFS.totalBytes();
    usedBytes = LittleFS.usedBytes();
    freeBytes = totalBytes - usedBytes;
  } else if (fsParam == "sd" && sdCardInitialized) {
#if defined(CONFIG_IDF_TARGET_ESP32) // ESP32-CAM
    totalBytes = SD_MMC.totalBytes();
    usedBytes = SD_MMC.usedBytes();
#else // ESP32-S2, ESP32-S3, ESP32-C3
    totalBytes = SD.totalBytes();
    usedBytes = SD.usedBytes();
#endif
    freeBytes = totalBytes - usedBytes;
  }
}

bool ESPWiFi::isRestrictedSystemFile(const String &fsParam,
                                     const String &filePath) {
  // Only restrict files on LittleFS, not SD card
  if (fsParam != "lfs") {
    return false;
  }

  // List of restricted system files and directories
  return (filePath == "/config.json" || filePath == "/index.html" ||
          filePath == "/asset-manifest.json" || filePath == "/dashboard.zip" ||
          filePath.startsWith("/static/") || filePath.startsWith("/system/") ||
          filePath.startsWith("/boot/"));
}

bool ESPWiFi::fileExists(FS *fs, const String &filePath) {
  if (!fs) {
    log(ERROR, "File system is null");
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
    log(ERROR, "File system is null");
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
    log(ERROR, "File system is null");
    return false;
  }

  if (dirExists(fs, dirPath)) {
    log(INFO, "📁 Directory already exists: %s", dirPath.c_str());
    return true;
  }

  if (fs->mkdir(dirPath)) {
    log(INFO, "📁 Created directory: %s", dirPath.c_str());
    return true;
  } else {
    log(ERROR, "Failed to create directory: %s", dirPath.c_str());

    // Check if directory was actually created despite the error
    if (dirExists(fs, dirPath)) {
      log(INFO, "📁 Directory exists after failed mkdir: %s", dirPath.c_str());
      return true;
    }

    return false;
  }
}

bool ESPWiFi::deleteDirectoryRecursive(FS *fs, const String &dirPath) {
  if (!fs) {
    log(ERROR, "File system is null");
    return false;
  }

  if (!dirExists(fs, dirPath)) {
    log(INFO, "📁 Directory does not exist: %s", dirPath.c_str());
    return true; // Directory doesn't exist, consider it "deleted"
  }

  // Use iterative approach with a queue to avoid deep recursion
  String dirsToProcess[20]; // Queue for directories to process
  int dirQueueHead = 0;
  int dirQueueTail = 0;
  int totalFilesDeleted = 0;
  unsigned long startTime = millis();
  const unsigned long MAX_DELETE_TIME = 10000; // 10 second timeout

  // Add the root directory to the queue
  dirsToProcess[dirQueueTail] = dirPath;
  dirQueueTail = (dirQueueTail + 1) % 20;

  while (dirQueueHead != dirQueueTail &&
         (millis() - startTime) < MAX_DELETE_TIME) {
    String currentDir = dirsToProcess[dirQueueHead];
    dirQueueHead = (dirQueueHead + 1) % 20;

    File dir = fs->open(currentDir);
    if (!dir || !dir.isDirectory()) {
      log(ERROR, "Failed to open directory: %s", currentDir.c_str());
      continue;
    }

    int filesInThisDir = 0;
    File file = dir.openNextFile();

    while (file && (millis() - startTime) < MAX_DELETE_TIME) {
      String filePath = file.path();

      if (file.isDirectory()) {
        // Add subdirectory to queue for later processing
        if (dirQueueTail != dirQueueHead) { // Check queue not full
          dirsToProcess[dirQueueTail] = filePath;
          dirQueueTail = (dirQueueTail + 1) % 20;
        } else {
          log(ERROR, "Directory queue full, skipping: %s", filePath.c_str());
        }
      } else {
        // Delete file
        if (fs->remove(filePath)) {
          filesInThisDir++;
          totalFilesDeleted++;
        } else {
          log(ERROR, "Failed to delete file: %s", filePath.c_str());
        }
      }

      file.close();

      // Yield every 10 files to prevent watchdog timeout
      if (filesInThisDir % 10 == 0) {
        yield();
        delay(1); // Small delay to prevent overwhelming the filesystem
      }

      file = dir.openNextFile();
    }

    dir.close();

    // Log progress every 100 files
    if (totalFilesDeleted % 100 == 0 && totalFilesDeleted > 0) {
      log(INFO, "🗑️ Deleted %d files so far...", totalFilesDeleted);
    }

    // Yield after each directory to prevent blocking
    yield();
    delay(5); // Small delay between directories
  }

  // Now remove all empty directories (in reverse order)
  for (int i = 0; i < 20; i++) {
    int idx = (dirQueueTail - 1 - i + 20) % 20;
    if (dirsToProcess[idx].length() > 0) {
      fs->rmdir(dirsToProcess[idx]);
    }
  }

  if ((millis() - startTime) >= MAX_DELETE_TIME) {
    log(ERROR, "Directory deletion timed out");
    return false;
  }

  log(INFO, "🗑️ Deleted %d files from %s", totalFilesDeleted, dirPath.c_str());
  return true;
}

void ESPWiFi::srvFiles() {
  initWebServer();

  // Generic file requests - updated to handle both LittleFS and SD card
  webServer->onNotFound([this](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_OPTIONS) {
      handleCorsPreflight(request);
      return;
    }

    // Allow static files (HTML, JS, CSS, images) without auth - needed for
    // login page Only protect API endpoints
    String path = request->url();
    bool isApiEndpoint =
        path.startsWith("/api/") && !path.startsWith("/api/auth/");

    bool isRestrictedFile =
        path.endsWith("log") || path.endsWith("config.json");

    if ((isApiEndpoint || isRestrictedFile) && !authorized(request)) {
      sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
      return;
    }

    // Allow static files and non-API routes to pass through

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
    if (fsPrefix == "/sd" && sdCardInitialized && sd &&
        sd->exists(actualPath)) {
      String contentType = getContentType(actualPath);
      AsyncWebServerResponse *response =
          request->beginResponse(*sd, actualPath, contentType);
      addCORS(response);
      request->send(response);
    } else if (fsPrefix == "/lfs" && lfs && lfs->exists(actualPath)) {
      String contentType = getContentType(actualPath);
      AsyncWebServerResponse *response =
          request->beginResponse(*lfs, actualPath, contentType);
      addCORS(response);
      request->send(response);
    } else if (fsPrefix == "" && lfs && lfs->exists(path)) {
      // Default to LittleFS for paths without prefix
      String contentType = getContentType(path);
      AsyncWebServerResponse *response =
          request->beginResponse(*lfs, path, contentType);
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

    if (!authorized(request)) {
      sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
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
    if (fsParam == "sd" && sdCardInitialized && sd) {
      filesystem = sd;
    } else if (fsParam == "lfs" && lfs) {
      filesystem = lfs;
    }

    if (!filesystem) {
      sendJsonResponse(request, 404,
                       "{\"error\":\"File system not available\"}");
      return;
    }

    File root = filesystem->open(path, "r");
    if (!root || !root.isDirectory()) {
      sendJsonResponse(request, 404, "{\"error\":\"Directory not found\"}");
      return;
    }

    JsonDocument jsonDoc;
    JsonArray filesArray = jsonDoc["files"].to<JsonArray>();

    File file = root.openNextFile();
    int fileCount = 0;
    const int maxFiles = 1000; // Prevent infinite loops
    unsigned long startTime = millis();
    const unsigned long timeout = 3000; // 3 second timeout

    while (file && fileCount < maxFiles && (millis() - startTime) < timeout) {
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

      // Yield control to prevent watchdog timeout
      yield();

      file = root.openNextFile();
    }

    root.close();

    String jsonResponse;
    serializeJson(jsonDoc, jsonResponse);
    sendJsonResponse(request, 200, jsonResponse);
  });

  // API endpoint for storage information
  webServer->on(
      "/api/storage", HTTP_GET, [this](AsyncWebServerRequest *request) {
        // Handle CORS preflight requests
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }

        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        String fsParam = "lfs";
        if (request->hasParam("fs")) {
          fsParam = request->getParam("fs")->value();
        }

        size_t totalBytes, usedBytes, freeBytes;
        getStorageInfo(fsParam, totalBytes, usedBytes, freeBytes);

        // Create JSON response
        JsonDocument jsonDoc;
        jsonDoc["total"] = totalBytes;
        jsonDoc["used"] = usedBytes;
        jsonDoc["free"] = freeBytes;
        jsonDoc["filesystem"] = fsParam;

        String jsonResponse;
        serializeJson(jsonDoc, jsonResponse);
        sendJsonResponse(request, 200, jsonResponse);
      });

  // API endpoint for creating directories
  webServer->on(
      "/api/files/mkdir", HTTP_POST,
      [this](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }
        // Defer response until body is received
      },
      NULL,
      [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
             size_t index, size_t total) {
        // Handle CORS preflight requests
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }

        // Parse JSON body
        JsonDocument jsonDoc;
        String body = String((char *)data).substring(0, len);

        DeserializationError error = deserializeJson(jsonDoc, body);

        if (error) {
          sendJsonResponse(request, 400, "{\"error\":\"Invalid JSON\"}");
          return;
        }

        String fsParam = jsonDoc["fs"] | "lfs";
        String path = jsonDoc["path"] | "/";
        String name = jsonDoc["name"] | "";

        if (name.length() == 0) {
          sendJsonResponse(request, 400,
                           "{\"error\":\"Folder name required\"}");
          return;
        }

        // Sanitize folder name
        String sanitizedName = sanitizeFilename(name);

        // Determine filesystem
        FS *filesystem = nullptr;
        if (fsParam == "sd" && sdCardInitialized && sd) {
          filesystem = sd;
        } else if (fsParam == "lfs" && lfs) {
          filesystem = lfs;
        }

        if (!filesystem) {
          sendJsonResponse(request, 404,
                           "{\"error\":\"File system not available\"}");
          return;
        }

        // Create directory path
        if (!path.startsWith("/"))
          path = "/" + path;
        String dirPath = path + (path.endsWith("/") ? "" : "/") + sanitizedName;

        // Create directory using the robust mkDir function
        if (mkDir(filesystem, dirPath)) {
          sendJsonResponse(request, 200, "{\"success\":true}");
        } else {
          sendJsonResponse(request, 500,
                           "{\"error\":\"Failed to create directory\"}");
        }
      });

  // API endpoint for file rename
  webServer->on(
      "/api/files/rename", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
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
          sendJsonResponse(request, 400, "{\"error\":\"Missing parameters\"}");
          return;
        }

        FS *filesystem = nullptr;
        if (fsParam == "sd" && sdCardInitialized && sd) {
          filesystem = sd;
        } else if (fsParam == "lfs" && lfs) {
          filesystem = lfs;
        }

        if (!filesystem) {
          sendJsonResponse(request, 404,
                           "{\"error\":\"File system not available\"}");
          return;
        }

        // Prevent renaming of system files on LittleFS
        if (isRestrictedSystemFile(fsParam, oldPath)) {
          sendJsonResponse(request, 403,
                           "{\"error\":\"Cannot rename system files\"}");
          return;
        }

        // Get directory path and construct new path
        String dirPath = oldPath.substring(0, oldPath.lastIndexOf('/'));
        String newPath = dirPath + "/" + newName;

        if (filesystem->rename(oldPath, newPath)) {
          log(INFO, "📁 Renamed file: %s -> %s", oldPath.c_str(),
              newName.c_str());
          sendJsonResponse(request, 200, "{\"success\":true}");
        } else {
          sendJsonResponse(request, 500,
                           "{\"error\":\"Failed to rename file\"}");
        }
      });

  // API endpoint for file deletion
  webServer->on(
      "/api/files/delete", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
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
          sendJsonResponse(request, 400, "{\"error\":\"Missing parameters\"}");
          return;
        }

        // Validate file path length
        if (filePath.length() > 255) {
          sendJsonResponse(request, 400, "{\"error\":\"Invalid file path\"}");
          return;
        }

        FS *filesystem = nullptr;
        if (fsParam == "sd" && sdCardInitialized && sd) {
          filesystem = sd;
        } else if (fsParam == "lfs" && lfs) {
          filesystem = lfs;
        }

        if (!filesystem) {
          sendJsonResponse(request, 400,
                           "{\"error\":\"File system not available\"}");
          return;
        }

        // Additional safety check - ensure file system is still mounted
        if (fsParam == "sd" && !sdCardInitialized) {
          sendJsonResponse(request, 500, "{\"error\":\"SD card not mounted\"}");
          return;
        }

        // Check if file exists before attempting deletion
        if (!filesystem->exists(filePath)) {
          sendJsonResponse(request, 404, "{\"error\":\"File not found\"}");
          return;
        }

        // Prevent deletion of system files on LittleFS
        if (isRestrictedSystemFile(fsParam, filePath)) {
          sendJsonResponse(request, 403,
                           "{\"error\":\"Cannot delete system files\"}");
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
          // Determine filesystem name for logging
          String fsName = (fsParam == "sd") ? "SD Card" : "LittleFS";
          if (isDirectory) {
            log(INFO, "🗑️  Deleted directory on %s: %s", fsName.c_str(),
                filePath.c_str());
          } else {
            log(INFO, "🗑️  Deleted file on %s: %s", fsName.c_str(),
                filePath.c_str());
          }
          sendJsonResponse(request, 200, "{\"success\":true}");
        } else {
          sendJsonResponse(request, 500,
                           "{\"error\":\"Failed to delete file\"}");
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
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }
        // This will be handled by the upload handler
      },
      [this](AsyncWebServerRequest *request, String filename, size_t index,
             uint8_t *data, size_t len, bool final) {
        handleFileUpload(request, filename, index, data, len, final);
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
  static String sanitizedFilename = "";

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
      log(ERROR, "Missing fs or path parameters for file upload");
      sendJsonResponse(request, 400, "{\"error\":\"Missing parameters\"}");
      return;
    }

    if (!currentPath.startsWith("/"))
      currentPath = "/" + currentPath;

    // Sanitize filename
    sanitizedFilename = sanitizeFilename(filename);

    // Check filename length limit (LittleFS typically has 31 char limit)
    const int maxFilenameLength = 31;
    if (sanitizedFilename.length() > maxFilenameLength) {
      // Try to preserve file extension
      int lastDot = sanitizedFilename.lastIndexOf('.');
      String extension = "";
      String baseName = sanitizedFilename;

      if (lastDot > 0 && lastDot > sanitizedFilename.length() -
                                       6) { // Extension is reasonable length
        extension = sanitizedFilename.substring(lastDot);
        baseName = sanitizedFilename.substring(0, lastDot);
      }

      // Truncate base name to fit extension and add unique suffix
      int maxBaseLength = maxFilenameLength - extension.length() -
                          4; // Reserve 4 chars for unique suffix
      if (maxBaseLength > 0) {
        // Generate 4-character random suffix
        String uniqueSuffix =
            String(random(1000, 9999)); // 4-digit random number
        sanitizedFilename = baseName.substring(0, maxBaseLength) + "_" +
                            uniqueSuffix + extension;
      } else {
        // If no room for extension, just truncate and add suffix
        String uniqueSuffix = String(random(1000, 9999));
        sanitizedFilename =
            sanitizedFilename.substring(0, maxFilenameLength - 5) + "_" +
            uniqueSuffix;
      }

      log(INFO, "📁 Filename truncated: %s", sanitizedFilename.c_str());
    }

    String filePath = currentPath + (currentPath.endsWith("/") ? "" : "/") +
                      sanitizedFilename;

    // Determine filesystem - use SD card first if available, error if not
    FS *filesystem = nullptr;
    if (currentFs == "lfs" && lfs) {
      filesystem = lfs;
    } else if (currentFs == "sd" && sdCardInitialized && sd) {
      filesystem = sd;
    } else if (currentFs == "" && sdCardInitialized && sd) {
      // Default to SD card if available
      filesystem = sd;
      currentFs = "sd";
    } else {
      // No fallback - send error if SD card not available
      log(ERROR, "SD card not available for file upload");
      sendJsonResponse(request, 500, "{\"error\":\"SD card not available\"}");
      return;
    }

    if (filesystem) {
      // Check available space before creating file
      size_t totalBytes, usedBytes, freeBytes;
      getStorageInfo(currentFs, totalBytes, usedBytes, freeBytes);
      currentFile = filesystem->open(filePath, "w");
      if (!currentFile) {
        log(ERROR, "Failed to create file for upload");
        log(INFO, "📁 File path: %s", filePath.c_str());
        log(INFO, "📁 Filesystem: %s", currentFs.c_str());
        sendJsonResponse(request, 500, "{\"error\":\"Failed to create file\"}");
        return;
      }
    } else {
      log(ERROR, "File system not available");
      sendJsonResponse(request, 404,
                       "{\"error\":\"File system not available\"}");
      return;
    }

    currentSize = 0;
  }

  // Write data chunk
  if (currentFile && len > 0) {
    size_t bytesWritten = currentFile.write(data, len);
    if (bytesWritten != len) {
      log(ERROR, "Failed to write all data to file");
      currentFile.close();
      sendJsonResponse(request, 500, "{\"error\":\"File write failed\"}");
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
      String fullPath = currentPath + (currentPath.endsWith("/") ? "" : "/") +
                        sanitizedFilename;
      log(INFO, "📁 Uploaded: %s (%s)", fullPath.c_str(),
          bytesToHumanReadable(currentSize).c_str());
    }

    // Reset static variables for next upload
    currentFs = "";
    currentPath = "";
    currentSize = 0;
    sanitizedFilename = "";

    sendJsonResponse(request, 200, "{\"success\":true}");
  }
}

void ESPWiFi::logFilesystemInfo(const String &fsName, size_t totalBytes,
                                size_t usedBytes) {
  log(DEBUG, "\tUsed: %s", bytesToHumanReadable(usedBytes).c_str());
  log(DEBUG, "\tFree: %s",
      bytesToHumanReadable(totalBytes - usedBytes).c_str());
  log(DEBUG, "\tTotal: %s", bytesToHumanReadable(totalBytes).c_str());
}

void ESPWiFi::printFilesystemInfo() {
  if (lfs) {
    log(INFO, "📁 LittleFS Available:");
    size_t totalBytes, usedBytes, freeBytes;
    getStorageInfo("lfs", totalBytes, usedBytes, freeBytes);
    logFilesystemInfo("LittleFS", totalBytes, usedBytes);
  }

  if (sdCardInitialized && sd) {
    log(INFO, "💾 SD Card Available:");
    size_t totalBytes, usedBytes, freeBytes;
    getStorageInfo("sd", totalBytes, usedBytes, freeBytes);
    logFilesystemInfo("SD Card", totalBytes, usedBytes);
  }
}

bool ESPWiFi::writeFile(FS *filesystem, const String &filePath,
                        const uint8_t *data, size_t len) {
  if (!filesystem) {
    log(ERROR, "File system is null");
    return false;
  }

  // Check available storage before writing
  size_t totalBytes, usedBytes, freeBytes;
  if (filesystem == sd) {
    getStorageInfo("sd", totalBytes, usedBytes, freeBytes);
  } else {
    getStorageInfo("lfs", totalBytes, usedBytes, freeBytes);
  }

  // Check if we have enough free space (add 10% buffer for safety)
  size_t requiredSpace = len + (len / 10);
  if (freeBytes < requiredSpace) {
    log(ERROR, "💔 Insufficient storage space");
    log(ERROR, "Required: %s, Available: %s",
        bytesToHumanReadable(requiredSpace).c_str(),
        bytesToHumanReadable(freeBytes).c_str());
    return false;
  }

  // Open file for writing
  File file = filesystem->open(filePath, "w");
  if (!file) {
    log(ERROR, "Failed to open file for writing: %s", filePath.c_str());
    return false;
  }

  // Write data
  size_t written = file.write(data, len);
  file.close();

  if (written != len) {
    log(ERROR, "Failed to write all data to file");
    return false;
  }

  // Determine filesystem name for logging
  String fsName = (filesystem == sd) ? "SD Card" : "LittleFS";
  log(INFO, "📁 File Written to %s: %s (%s)", fsName.c_str(), filePath.c_str(),
      bytesToHumanReadable(written).c_str());
  return true;
}

#endif // ESPWiFi_FileSystem