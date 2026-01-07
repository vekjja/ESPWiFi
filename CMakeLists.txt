cmake_minimum_required(VERSION 3.16.0)

get_filename_component(configName "${CMAKE_BINARY_DIR}" NAME)
list(APPEND EXTRA_COMPONENT_DIRS 
    "${CMAKE_SOURCE_DIR}/.pio/libdeps/${configName}/esp_littlefs"
)

set(COMPONENTS_DIR "${CMAKE_SOURCE_DIR}/components")

# Camera enablement (keep CMake clean: infer from build flags)
# PlatformIO typically injects -D* macros into compiler flags; we detect any
# selected ESPWiFi camera model without enumerating models here.
set(ESPWiFi_HAS_CAMERA OFF)
foreach(_flags_var IN ITEMS CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_ASM_FLAGS)
    if(DEFINED ${_flags_var})
        if("${${_flags_var}}" MATCHES "([\\s]|^)-DESPWiFi_CAMERA_MODEL_[A-Za-z0-9_]+")
            set(ESPWiFi_HAS_CAMERA ON)
        endif()
    endif()
endforeach()

# ESP-IDF auto-discovers and builds all components under the default `components/` dir.
# Classic Bluetooth A2DP + arduino-audio-tools are ESP32-only; exclude them on non-ESP32 targets.
if(NOT DEFINED IDF_TARGET AND DEFINED ENV{IDF_TARGET})
    set(IDF_TARGET "$ENV{IDF_TARGET}")
endif()
if(DEFINED IDF_TARGET AND NOT IDF_TARGET STREQUAL "esp32")
    list(APPEND EXCLUDE_COMPONENTS ESP32-A2DP arduino-audio-tools)
endif()

if(ESPWiFi_HAS_CAMERA)
    # Add Camera dependencies if enabled
    list(APPEND EXTRA_COMPONENT_DIRS "${COMPONENTS_DIR}/esp32-camera")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(ESPWiFi)