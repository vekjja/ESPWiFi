cmake_minimum_required(VERSION 3.16.0)

# Add extra component directories before including project.cmake
# For PlatformIO ESP-IDF projects, components from lib_deps are in .pio/libdeps/<env_name>/
# Managed components from idf_component.yml are in managed_components/
get_filename_component(configName "${CMAKE_BINARY_DIR}" NAME)
list(APPEND EXTRA_COMPONENT_DIRS 
    "${CMAKE_SOURCE_DIR}/.pio/libdeps/${configName}/esp_littlefs"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(ESPWiFi)
