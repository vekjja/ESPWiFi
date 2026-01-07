# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

# Base requirements for all builds
set(COMPONENT_REQUIRES esp_http_server esp_https_server esp_netif esp_event nvs_flash app_update esp_littlefs mdns esp_psram)

# Add Bluetooth dependencies only if A2DP is enabled
if(CONFIG_BT_A2DP_ENABLE)
    list(APPEND COMPONENT_REQUIRES bt ESP32-A2DP arduino-audio-tools)
endif()

# Add Camera dependencies if enabled
if(ESPWiFi_CAMERA_INSTALLED)
    list(APPEND COMPONENT_REQUIRES esp32-camera)
endif()

idf_component_register(
    SRCS ${app_sources}
    REQUIRES ${COMPONENT_REQUIRES}
)