# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

# Base requirements for all builds
set(COMPONENT_REQUIRES esp_http_server esp_netif esp_event nvs_flash app_update esp_littlefs bt mdns ESP32-A2DP arduino-audio-tools)

idf_component_register(
    SRCS ${app_sources}
    REQUIRES ${COMPONENT_REQUIRES}
)