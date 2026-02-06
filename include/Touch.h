// Touch.h - XPT2046 software SPI touch (CYD)
#pragma once

#include "TFTPins.h"

#if ESPWiFi_HAS_TFT && (TOUCH_CS_GPIO_NUM >= 0)

/** Initialize touch GPIO and diagnostic read. Call before LVGL/display. */
void touchBegin();

/** LVGL input device read callback. Register with lv_indev_set_read_cb(). indev
 * = lv_indev_t*, data = lv_indev_data_t*. */
void touchIndevReadCb(void *indev, void *data);

/** True after touchBegin() succeeded. Use to decide whether to register LVGL
 * indev. */
bool touchIsActive();

#else

inline void touchBegin() {}
inline void touchIndevReadCb(void *, void *) {}
inline bool touchIsActive() { return false; }

#endif
