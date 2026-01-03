# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

# Base requirements for all builds
set(COMPONENT_REQUIRES esp_http_server esp_netif esp_event nvs_flash app_update esp_littlefs mdns)

# Add Bluetooth dependencies only if A2DP is enabled
if(CONFIG_BT_A2DP_ENABLE)
    list(APPEND COMPONENT_REQUIRES bt ESP32-A2DP arduino-audio-tools)
else()
    # Exclude Bluetooth source files when A2DP is disabled
    list(FILTER app_sources EXCLUDE REGEX ".*/(Bluetooth.*|srvBluetooth)\\.cpp$")
endif()

idf_component_register(
    SRCS ${app_sources}
    REQUIRES ${COMPONENT_REQUIRES}
)