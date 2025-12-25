#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino.h>

// Forward declarations provided by the existing Arduino sketch
extern void setup();
extern void loop();

extern "C" void app_main(void) {
  initArduino();
  setup();
  for (;;) {
    loop();
    vTaskDelay(1); // yield to the scheduler
  }
}
