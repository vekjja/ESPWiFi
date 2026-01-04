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
if(CONFIG_CAMERA_MODEL_AI_THINKER OR 
   CONFIG_CAMERA_MODEL_WROVER_KIT OR 
   CONFIG_CAMERA_MODEL_ESP_EYE OR 
   CONFIG_CAMERA_MODEL_M5STACK_PSRAM OR 
   CONFIG_CAMERA_MODEL_M5STACK_V2_PSRAM OR 
   CONFIG_CAMERA_MODEL_M5STACK_WIDE OR 
   CONFIG_CAMERA_MODEL_M5STACK_ESP32CAM OR 
   CONFIG_CAMERA_MODEL_M5STACK_UNITCAM OR 
   CONFIG_CAMERA_MODEL_TTGO_T_JOURNAL OR 
   CONFIG_CAMERA_MODEL_XIAO_ESP32S3 OR 
   CONFIG_CAMERA_MODEL_ESP32_CAM_BOARD OR 
   CONFIG_CAMERA_MODEL_ESP32S3_CAM_LCD OR 
   CONFIG_CAMERA_MODEL_ESP32S2_CAM_BOARD OR 
   CONFIG_CAMERA_MODEL_ESP32S3_EYE)
    list(APPEND COMPONENT_REQUIRES esp32-camera)
endif()

idf_component_register(
    SRCS ${app_sources}
    REQUIRES ${COMPONENT_REQUIRES}
)