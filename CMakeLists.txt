cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# =============================================================================
# SDK Config Defaults Selection
# =============================================================================
# Automatically select the correct sdkconfig defaults file based on target chip.
# This ensures consistent configuration regardless of build method (PlatformIO, ESP-IDF CLI, etc.)
#
# Order of precedence (ESP-IDF reads these in order):
# 1. sdkconfig.defaults (common settings for all targets)
# 2. sdkconfig.defaults.<target> (target-specific overrides)
#
# Environment variable IDF_TARGET can be set externally, or defaults to esp32.
# =============================================================================

if(NOT DEFINED IDF_TARGET)
    set(IDF_TARGET "esp32")
    message(STATUS "IDF_TARGET not set, defaulting to: ${IDF_TARGET}")
endif()

# Build the list of defaults files to use
set(SDKCONFIG_DEFAULTS_LIST "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")

# Add target-specific defaults if the file exists
if(IDF_TARGET STREQUAL "esp32")
    set(TARGET_DEFAULTS "${CMAKE_SOURCE_DIR}/sdkconfig.esp32.defaults")
elseif(IDF_TARGET STREQUAL "esp32s3")
    set(TARGET_DEFAULTS "${CMAKE_SOURCE_DIR}/sdkconfig.esp32-s3.defaults")
elseif(IDF_TARGET STREQUAL "esp32c3")
    set(TARGET_DEFAULTS "${CMAKE_SOURCE_DIR}/sdkconfig.esp32-c3.defaults")
else()
    message(WARNING "Unknown IDF_TARGET: ${IDF_TARGET}, using only sdkconfig.defaults")
    set(TARGET_DEFAULTS "")
endif()

if(TARGET_DEFAULTS AND EXISTS "${TARGET_DEFAULTS}")
    list(APPEND SDKCONFIG_DEFAULTS_LIST "${TARGET_DEFAULTS}")
    message(STATUS "Using sdkconfig defaults: sdkconfig.defaults + ${TARGET_DEFAULTS}")
else()
    message(STATUS "Using sdkconfig defaults: sdkconfig.defaults only")
endif()

# Set the SDKCONFIG_DEFAULTS variable (ESP-IDF will read this)
set(SDKCONFIG_DEFAULTS "${SDKCONFIG_DEFAULTS_LIST}")

get_filename_component(configName "${CMAKE_BINARY_DIR}" NAME)
list(APPEND EXTRA_COMPONENT_DIRS 
    "${CMAKE_SOURCE_DIR}/.pio/libdeps/${configName}/esp_littlefs"
    "${CMAKE_SOURCE_DIR}/components"
)

project(ESPWiFi)
