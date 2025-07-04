#ifndef CAMERA_PINS_H
#define CAMERA_PINS_H

#if CONFIG_IDF_TARGET_ESP32S3
#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM

// ESP32-S3 Camera Pin Definitions #########################################
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

#endif // CONFIG_IDF_TARGET_ESP32S3

// ESP32-CAM Camera Pin Definitions #########################################
#if CONFIG_IDF_TARGET_ESP32
#define CAMERA_MODEL_AI_THINKER // ESP32-CAM AI-Thinker

// ESP32-CAM Pin Definitions
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

#endif // CONFIG_IDF_TARGET_ESP32

#endif // CAMERA_PINS_H
