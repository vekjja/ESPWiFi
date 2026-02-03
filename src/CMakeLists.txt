# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

# Base requirements for all builds
set(COMPONENT_REQUIRES esp_http_server esp_https_server esp_netif esp_event nvs_flash app_update esp_littlefs mdns esp_psram)

# Classic Bluetooth A2DP is only supported on classic ESP32 target (not ESP32-S3/C3).
if(CONFIG_IDF_TARGET_ESP32 AND CONFIG_BT_A2DP_ENABLE)
    list(APPEND COMPONENT_REQUIRES bt ESP32-A2DP arduino-audio-tools)
endif()

# Add TFT dependencies when a TFT model is selected (via -DESPWiFi_TFT_MODEL_*)
set(ESPWiFi_HAS_TFT OFF)
foreach(_flags_var IN ITEMS CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_ASM_FLAGS)
    if(DEFINED ${_flags_var})
        if("${${_flags_var}}" MATCHES "([\\s]|^)-DESPWiFi_TFT_MODEL_[A-Za-z0-9_]+")
            set(ESPWiFi_HAS_TFT ON)
        endif()
    endif()
endforeach()

if(ESPWiFi_HAS_TFT)
    list(APPEND COMPONENT_REQUIRES esp_lcd esp_lcd_touch_xpt2046)
endif()

# Add Camera dependencies when a camera model is selected (via -DESPWiFi_CAMERA_MODEL_*)
set(ESPWiFi_HAS_CAMERA OFF)
foreach(_flags_var IN ITEMS CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_ASM_FLAGS)
    if(DEFINED ${_flags_var})
        if("${${_flags_var}}" MATCHES "([\\s]|^)-DESPWiFi_CAMERA_MODEL_[A-Za-z0-9_]+")
            set(ESPWiFi_HAS_CAMERA ON)
        endif()
    endif()
endforeach()

if(ESPWiFi_HAS_CAMERA)
    list(APPEND COMPONENT_REQUIRES esp32-camera)
endif()

idf_component_register(
    SRCS ${app_sources}
    REQUIRES ${COMPONENT_REQUIRES}
)