#ifndef ESPWiFi_SRV_LOG
#define ESPWiFi_SRV_LOG

#include "ESPWiFi.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <cstring>
#include <stdarg.h>
#include <sys/time.h>

void ESPWiFi::srvLog() {
  if (!webServer) {
    log(ERROR, "Cannot start log API /logs: web server not initialized");
    return;
  }

  // GET /logs - return log file content as HTML
  httpd_uri_t logs_route = {
      .uri = "/logs",
      .method = HTTP_GET,
      .handler = [](httpd_req_t *req) -> esp_err_t {
        ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
        if (espwifi->verifyRequest(req) != ESP_OK) {
          return ESP_ERR_HTTPD_INVALID_REQ;
        }
        {
          ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
          std::string clientInfo = espwifi->getClientInfo(req);

          // Check if LFS is initialized
          if (!espwifi->littleFsInitialized) {
            espwifi->sendJsonResponse(
                req, 503, "{\"error\":\"Filesystem not available\"}");
            return ESP_OK;
          }

          // Construct full path to log file
          std::string fullPath = espwifi->lfsMountPoint + espwifi->logFilePath;

          // Check if log file exists
          struct stat fileStat;
          if (stat(fullPath.c_str(), &fileStat) != 0) {
            espwifi->sendJsonResponse(req, 404,
                                      "{\"error\":\"Log file not found\"}");
            return ESP_OK;
          }

          // Open log file
          FILE *file = fopen(fullPath.c_str(), "r");
          if (!file) {
            espwifi->sendJsonResponse(
                req, 500, "{\"error\":\"Failed to open log file\"}");
            return ESP_OK;
          }

          // Set content type and CORS headers
          espwifi->addCORS(req);
          httpd_resp_set_type(req, "text/html");

          // Send HTML header with CSS and JavaScript (stream it)
          const char *htmlHeader =
              "<!DOCTYPE html><html><head><meta "
              "charset=\"utf-8\"><style>body{margin:0;padding:10px;background:#"
              "1e1e1e;color:#d4d4d4;font-family:monospace;font-size:12px;}pre{"
              "white-"
              "space:pre;overflow-x:auto;margin:0;}.controls{position:fixed;"
              "top:10px;"
              "right:10px;z-index:1000;background:#2d2d2d;padding:10px;border-"
              "radius:"
              "4px;border:1px solid #444;}.controls "
              "label{display:block;margin:5px "
              "0;color:#d4d4d4;cursor:pointer;}.controls "
              "input[type=\"checkbox\"]{margin-right:8px;cursor:pointer;}</"
              "style><script>var autoScroll=true;var autoRefresh=true;var "
              "refreshInterval;function initControls(){var "
              "scrollCheckbox=document.getElementById('autoScroll');var "
              "refreshCheckbox=document.getElementById('autoRefresh');"
              "autoScroll="
              "localStorage.getItem('autoScroll')!=='false';autoRefresh="
              "localStorage."
              "getItem('autoRefresh')!=='false';if(scrollCheckbox){"
              "scrollCheckbox."
              "checked=autoScroll;scrollCheckbox.addEventListener('change',"
              "function()"
              "{autoScroll=this.checked;localStorage.setItem('autoScroll',"
              "autoScroll)"
              ";if(autoScroll)scrollToBottom();});}if(refreshCheckbox){"
              "refreshCheckbox.checked=autoRefresh;refreshCheckbox."
              "addEventListener('"
              "change',function(){autoRefresh=this.checked;localStorage."
              "setItem('"
              "autoRefresh',autoRefresh);if(autoRefresh){startRefresh();}else{"
              "stopRefresh();}});}if(autoRefresh)startRefresh();}function "
              "scrollToBottom(){if(autoScroll){window.scrollTo(0,document.body."
              "scrollHeight||document.documentElement.scrollHeight);}}function "
              "startRefresh(){if(refreshInterval)clearInterval(refreshInterval)"
              ";"
              "refreshInterval=setInterval(function(){if(autoRefresh)location."
              "reload("
              ");},5000);}function "
              "stopRefresh(){if(refreshInterval){clearInterval(refreshInterval)"
              ";"
              "refreshInterval=null;}}window.addEventListener('load',function()"
              "{"
              "initControls();scrollToBottom();});document.addEventListener('"
              "DOMContentLoaded',function(){setTimeout(scrollToBottom,100);});"
              "setTimeout(scrollToBottom,200);</script></head><body><div "
              "class=\"controls\"><label><input type=\"checkbox\" "
              "id=\"autoScroll\" "
              "checked> Auto Scroll</label><label><input type=\"checkbox\" "
              "id=\"autoRefresh\" checked> Auto Refresh</label></div><pre>";

          esp_err_t ret =
              httpd_resp_send_chunk(req, htmlHeader, strlen(htmlHeader));
          if (ret != ESP_OK) {
            fclose(file);
            return ESP_FAIL;
          }

          // Stream log file content with HTML escaping in small chunks
          // Use heap allocation to avoid stack overflow
          const size_t readChunkSize = 1024;    // Read 1KB at a time
          const size_t escapeBufferSize = 4096; // Escape buffer (4KB max)
          char *readBuffer = (char *)malloc(readChunkSize);
          char *escapeBuffer = (char *)malloc(escapeBufferSize);

          if (!readBuffer || !escapeBuffer) {
            if (readBuffer)
              free(readBuffer);
            if (escapeBuffer)
              free(escapeBuffer);
            fclose(file);
            espwifi->sendJsonResponse(
                req, 500, "{\"error\":\"Memory allocation failed\"}");
            return ESP_OK;
          }

          size_t escapePos = 0;
          size_t totalRead = 0;

          while ((ret == ESP_OK) && !feof(file)) {
            espwifi->yield(); // Yield before file I/O
            size_t bytesRead = fread(readBuffer, 1, readChunkSize, file);
            if (bytesRead == 0) {
              break;
            }
            totalRead += bytesRead;

            // Escape HTML special characters and accumulate in buffer
            for (size_t i = 0; i < bytesRead; i++) {
              if (readBuffer[i] == '<') {
                const char *escaped = "&lt;";
                size_t escapedLen = 4;
                if (escapePos + escapedLen >= escapeBufferSize) {
                  // Send current buffer
                  ret = httpd_resp_send_chunk(req, escapeBuffer, escapePos);
                  if (ret != ESP_OK)
                    break;
                  escapePos = 0;
                  espwifi->yield(); // Yield after network I/O
                }
                memcpy(escapeBuffer + escapePos, escaped, escapedLen);
                escapePos += escapedLen;
              } else if (readBuffer[i] == '>') {
                const char *escaped = "&gt;";
                size_t escapedLen = 4;
                if (escapePos + escapedLen >= escapeBufferSize) {
                  // Send current buffer
                  ret = httpd_resp_send_chunk(req, escapeBuffer, escapePos);
                  if (ret != ESP_OK)
                    break;
                  escapePos = 0;
                  espwifi->yield(); // Yield after network I/O
                }
                memcpy(escapeBuffer + escapePos, escaped, escapedLen);
                escapePos += escapedLen;
              } else if (readBuffer[i] == '&') {
                const char *escaped = "&amp;";
                size_t escapedLen = 5;
                if (escapePos + escapedLen >= escapeBufferSize) {
                  // Send current buffer
                  ret = httpd_resp_send_chunk(req, escapeBuffer, escapePos);
                  if (ret != ESP_OK)
                    break;
                  escapePos = 0;
                  espwifi->yield(); // Yield after network I/O
                }
                memcpy(escapeBuffer + escapePos, escaped, escapedLen);
                escapePos += escapedLen;
              } else {
                if (escapePos + 1 >= escapeBufferSize) {
                  // Send current buffer
                  ret = httpd_resp_send_chunk(req, escapeBuffer, escapePos);
                  if (ret != ESP_OK)
                    break;
                  escapePos = 0;
                  espwifi->yield(); // Yield after network I/O
                }
                escapeBuffer[escapePos++] = readBuffer[i];
              }
            }

            // Send accumulated buffer if it's getting full
            if (escapePos > escapeBufferSize / 2) {
              ret = httpd_resp_send_chunk(req, escapeBuffer, escapePos);
              if (ret != ESP_OK)
                break;
              escapePos = 0;
              espwifi->yield(); // Yield after network I/O
            }
          }

          // Send any remaining escaped content
          if (ret == ESP_OK && escapePos > 0) {
            ret = httpd_resp_send_chunk(req, escapeBuffer, escapePos);
            espwifi->yield(); // Yield after network I/O
          }

          // Free allocated buffers
          free(readBuffer);
          free(escapeBuffer);

          fclose(file);

          // Send closing HTML tags
          if (ret == ESP_OK) {
            const char *htmlFooter = "</pre></body></html>";
            ret = httpd_resp_send_chunk(req, htmlFooter, strlen(htmlFooter));
            espwifi->yield(); // Yield after network I/O
          }

          // Finalize chunked transfer
          if (ret == ESP_OK) {
            ret =
                httpd_resp_send_chunk(req, nullptr, 0); // End chunked transfer
            espwifi->yield(); // Yield after network I/O
          }

          espwifi->logAccess(200, clientInfo, totalRead);
          return (ret == ESP_OK) ? ESP_OK : ESP_FAIL;
        }
      },
      .user_ctx = this};
  httpd_register_uri_handler(webServer, &logs_route);
}

#endif // ESPWiFi_SRV_LOG