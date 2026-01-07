#pragma once

// Camera pin mappings selected by ESPWiFi_CAMERA_MODEL_* compile definitions.
// This header is safe to include even when no camera is selected.
// - When a model is selected: ESPWiFi_CAMERA_MODEL_SELECTED = 1 and GPIO macros
// are set
// - When no model is selected: ESPWiFi_CAMERA_MODEL_SELECTED = 0 and GPIO
// macros are set to -1

#if defined(ESPWiFi_CAMERA_MODEL_WROVER_KIT)
#define ESPWiFi_CAMERA_MODEL_SELECTED 1
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 21
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 19
#define Y4_GPIO_NUM 18
#define Y3_GPIO_NUM 5
#define Y2_GPIO_NUM 4
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#elif defined(ESPWiFi_CAMERA_MODEL_ESP_EYE)
#define ESPWiFi_CAMERA_MODEL_SELECTED 1
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 4
#define SIOD_GPIO_NUM 18
#define SIOC_GPIO_NUM 23

#define Y9_GPIO_NUM 36
#define Y8_GPIO_NUM 37
#define Y7_GPIO_NUM 38
#define Y6_GPIO_NUM 39
#define Y5_GPIO_NUM 35
#define Y4_GPIO_NUM 14
#define Y3_GPIO_NUM 13
#define Y2_GPIO_NUM 34
#define VSYNC_GPIO_NUM 5
#define HREF_GPIO_NUM 27
#define PCLK_GPIO_NUM 25

#define LED_GPIO_NUM 22

#elif defined(ESPWiFi_CAMERA_MODEL_M5STACK_PSRAM)
#define ESPWiFi_CAMERA_MODEL_SELECTED 1
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM 15
#define XCLK_GPIO_NUM 27
#define SIOD_GPIO_NUM 25
#define SIOC_GPIO_NUM 23

#define Y9_GPIO_NUM 19
#define Y8_GPIO_NUM 36
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 39
#define Y5_GPIO_NUM 5
#define Y4_GPIO_NUM 34
#define Y3_GPIO_NUM 35
#define Y2_GPIO_NUM 32
#define VSYNC_GPIO_NUM 22
#define HREF_GPIO_NUM 26
#define PCLK_GPIO_NUM 21

#elif defined(ESPWiFi_CAMERA_MODEL_M5STACK_V2_PSRAM)
#define ESPWiFi_CAMERA_MODEL_SELECTED 1
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM 15
#define XCLK_GPIO_NUM 27
#define SIOD_GPIO_NUM 22
#define SIOC_GPIO_NUM 23

#define Y9_GPIO_NUM 19
#define Y8_GPIO_NUM 36
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 39
#define Y5_GPIO_NUM 5
#define Y4_GPIO_NUM 34
#define Y3_GPIO_NUM 35
#define Y2_GPIO_NUM 32
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 26
#define PCLK_GPIO_NUM 21

#elif defined(ESPWiFi_CAMERA_MODEL_M5STACK_WIDE)
#define ESPWiFi_CAMERA_MODEL_SELECTED 1
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM 15
#define XCLK_GPIO_NUM 27
#define SIOD_GPIO_NUM 22
#define SIOC_GPIO_NUM 23

#define Y9_GPIO_NUM 19
#define Y8_GPIO_NUM 36
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 39
#define Y5_GPIO_NUM 5
#define Y4_GPIO_NUM 34
#define Y3_GPIO_NUM 35
#define Y2_GPIO_NUM 32
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 26
#define PCLK_GPIO_NUM 21

#define LED_GPIO_NUM 2

#elif defined(ESPWiFi_CAMERA_MODEL_M5STACK_ESP32CAM)
#define ESPWiFi_CAMERA_MODEL_SELECTED 1
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM 15
#define XCLK_GPIO_NUM 27
#define SIOD_GPIO_NUM 25
#define SIOC_GPIO_NUM 23

#define Y9_GPIO_NUM 19
#define Y8_GPIO_NUM 36
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 39
#define Y5_GPIO_NUM 5
#define Y4_GPIO_NUM 34
#define Y3_GPIO_NUM 35
#define Y2_GPIO_NUM 17
#define VSYNC_GPIO_NUM 22
#define HREF_GPIO_NUM 26
#define PCLK_GPIO_NUM 21

#elif defined(ESPWiFi_CAMERA_MODEL_M5STACK_UNITCAM)
#define ESPWiFi_CAMERA_MODEL_SELECTED 1
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM 15
#define XCLK_GPIO_NUM 27
#define SIOD_GPIO_NUM 25
#define SIOC_GPIO_NUM 23

#define Y9_GPIO_NUM 19
#define Y8_GPIO_NUM 36
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 39
#define Y5_GPIO_NUM 5
#define Y4_GPIO_NUM 34
#define Y3_GPIO_NUM 35
#define Y2_GPIO_NUM 32
#define VSYNC_GPIO_NUM 22
#define HREF_GPIO_NUM 26
#define PCLK_GPIO_NUM 21

#elif defined(ESPWiFi_CAMERA_MODEL_AI_THINKER)
#define ESPWiFi_CAMERA_MODEL_SELECTED 1
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define LED_GPIO_NUM 33

#elif defined(ESPWiFi_CAMERA_MODEL_TTGO_T_JOURNAL)
#define ESPWiFi_CAMERA_MODEL_SELECTED 1
#define PWDN_GPIO_NUM 0
#define RESET_GPIO_NUM 15
#define XCLK_GPIO_NUM 27
#define SIOD_GPIO_NUM 25
#define SIOC_GPIO_NUM 23

#define Y9_GPIO_NUM 19
#define Y8_GPIO_NUM 36
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 39
#define Y5_GPIO_NUM 5
#define Y4_GPIO_NUM 34
#define Y3_GPIO_NUM 35
#define Y2_GPIO_NUM 17
#define VSYNC_GPIO_NUM 22
#define HREF_GPIO_NUM 26
#define PCLK_GPIO_NUM 21

#elif defined(ESPWiFi_CAMERA_MODEL_XIAO_ESP32S3)
#define ESPWiFi_CAMERA_MODEL_SELECTED 1
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 10
#define SIOD_GPIO_NUM 40
#define SIOC_GPIO_NUM 39

#define Y9_GPIO_NUM 48
#define Y8_GPIO_NUM 11
#define Y7_GPIO_NUM 12
#define Y6_GPIO_NUM 14
#define Y5_GPIO_NUM 16
#define Y4_GPIO_NUM 18
#define Y3_GPIO_NUM 17
#define Y2_GPIO_NUM 15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM 47
#define PCLK_GPIO_NUM 13

#define LED_GPIO_NUM 21

#elif defined(ESPWiFi_CAMERA_MODEL_ESP32_CAM_BOARD)
#define ESPWiFi_CAMERA_MODEL_SELECTED 1
// The 18 pin header on the board has Y5 and Y3 swapped
#define USE_BOARD_HEADER 0
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM 33
#define XCLK_GPIO_NUM 4
#define SIOD_GPIO_NUM 18
#define SIOC_GPIO_NUM 23

#define Y9_GPIO_NUM 36
#define Y8_GPIO_NUM 19
#define Y7_GPIO_NUM 21
#define Y6_GPIO_NUM 39
#if USE_BOARD_HEADER
#define Y5_GPIO_NUM 13
#else
#define Y5_GPIO_NUM 35
#endif
#define Y4_GPIO_NUM 14
#if USE_BOARD_HEADER
#define Y3_GPIO_NUM 35
#else
#define Y3_GPIO_NUM 13
#endif
#define Y2_GPIO_NUM 34
#define VSYNC_GPIO_NUM 5
#define HREF_GPIO_NUM 27
#define PCLK_GPIO_NUM 25

#elif defined(ESPWiFi_CAMERA_MODEL_ESP32S3_CAM_LCD)
#define ESPWiFi_CAMERA_MODEL_SELECTED 1
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 40
#define SIOD_GPIO_NUM 17
#define SIOC_GPIO_NUM 18

#define Y9_GPIO_NUM 39
#define Y8_GPIO_NUM 41
#define Y7_GPIO_NUM 42
#define Y6_GPIO_NUM 12
#define Y5_GPIO_NUM 3
#define Y4_GPIO_NUM 14
#define Y3_GPIO_NUM 47
#define Y2_GPIO_NUM 13
#define VSYNC_GPIO_NUM 21
#define HREF_GPIO_NUM 38
#define PCLK_GPIO_NUM 11

#elif defined(ESPWiFi_CAMERA_MODEL_ESP32S2_CAM_BOARD)
#define ESPWiFi_CAMERA_MODEL_SELECTED 1
// The 18 pin header on the board has Y5 and Y3 swapped
#define USE_BOARD_HEADER 0
#define PWDN_GPIO_NUM 1
#define RESET_GPIO_NUM 2
#define XCLK_GPIO_NUM 42
#define SIOD_GPIO_NUM 41
#define SIOC_GPIO_NUM 18

#define Y9_GPIO_NUM 16
#define Y8_GPIO_NUM 39
#define Y7_GPIO_NUM 40
#define Y6_GPIO_NUM 15
#if USE_BOARD_HEADER
#define Y5_GPIO_NUM 12
#else
#define Y5_GPIO_NUM 13
#endif
#define Y4_GPIO_NUM 5
#if USE_BOARD_HEADER
#define Y3_GPIO_NUM 13
#else
#define Y3_GPIO_NUM 12
#endif
#define Y2_GPIO_NUM 14
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM 4
#define PCLK_GPIO_NUM 3

#elif defined(ESPWiFi_CAMERA_MODEL_ESP32S3_EYE)
#define ESPWiFi_CAMERA_MODEL_SELECTED 1
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5

#define Y2_GPIO_NUM 11
#define Y3_GPIO_NUM 9
#define Y4_GPIO_NUM 8
#define Y5_GPIO_NUM 10
#define Y6_GPIO_NUM 12
#define Y7_GPIO_NUM 18
#define Y8_GPIO_NUM 17
#define Y9_GPIO_NUM 16

#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

#else
#define ESPWiFi_CAMERA_MODEL_SELECTED 0
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM -1
#define SIOD_GPIO_NUM -1
#define SIOC_GPIO_NUM -1
#define Y9_GPIO_NUM -1
#define Y8_GPIO_NUM -1
#define Y7_GPIO_NUM -1
#define Y6_GPIO_NUM -1
#define Y5_GPIO_NUM -1
#define Y4_GPIO_NUM -1
#define Y3_GPIO_NUM -1
#define Y2_GPIO_NUM -1
#define VSYNC_GPIO_NUM -1
#define HREF_GPIO_NUM -1
#define PCLK_GPIO_NUM -1
#define LED_GPIO_NUM -1
#endif

// Single helper macro for "camera feature compiled in?"
// This avoids repeating `defined(...) && (...)` checks across the codebase.
#if ESPWiFi_CAMERA_MODEL_SELECTED
#define ESPWiFi_HAS_CAMERA 1
#else
#define ESPWiFi_HAS_CAMERA 0
#endif