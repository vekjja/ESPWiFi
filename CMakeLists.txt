cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Add extra component directories
get_filename_component(configName "${CMAKE_BINARY_DIR}" NAME)
list(APPEND EXTRA_COMPONENT_DIRS 
    "${CMAKE_SOURCE_DIR}/.pio/libdeps/${configName}/esp_littlefs"
)

# Arduino component will be automatically added via idf_component.yml
# No need to manually add it here - Component Manager handles it

project(ESPWiFi)
