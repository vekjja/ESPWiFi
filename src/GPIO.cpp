// GPIO.cpp - GPIO control endpoint (ESP-IDF httpd-safe)
#include "ESPWiFi.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

#include <algorithm>

namespace {

// static bool strEqAny(const std::string &s, const char *a, const char *b,
//                      const char *c = nullptr) {
//   if (s == a || s == b) {
//     return true;
//   }
//   return (c != nullptr) ? (s == c) : false;
// }

// Minimal PWM channel manager (avoid heap).
static bool pwm_timer_configured = false;
static int pwm_pin_for_channel[LEDC_CHANNEL_MAX];
static bool pwm_map_initialized = false;

static void pwm_init_map_once() {
  if (pwm_map_initialized) {
    return;
  }
  for (int i = 0; i < LEDC_CHANNEL_MAX; ++i) {
    pwm_pin_for_channel[i] = -1;
  }
  pwm_map_initialized = true;
}

static ledc_channel_t pwm_find_or_alloc_channel_for_pin(int pin) {
  pwm_init_map_once();
  // Reuse existing assignment if present.
  for (int i = 0; i < LEDC_CHANNEL_MAX; ++i) {
    if (pwm_pin_for_channel[i] == pin) {
      return static_cast<ledc_channel_t>(i);
    }
  }
  // Allocate a free channel.
  for (int i = 0; i < LEDC_CHANNEL_MAX; ++i) {
    if (pwm_pin_for_channel[i] < 0) {
      pwm_pin_for_channel[i] = pin;
      return static_cast<ledc_channel_t>(i);
    }
  }
  return LEDC_CHANNEL_MAX; // sentinel for "none"
}

static void pwm_free_channel_for_pin(int pin, ledc_mode_t speed_mode) {
  pwm_init_map_once();
  for (int i = 0; i < LEDC_CHANNEL_MAX; ++i) {
    if (pwm_pin_for_channel[i] == pin) {
      (void)ledc_stop(speed_mode, static_cast<ledc_channel_t>(i), 0);
      pwm_pin_for_channel[i] = -1;
      return;
    }
  }
}

} // namespace

// GPIO helper method - set digital pin
bool ESPWiFi::setGPIO(int pin, bool state, std::string &errorMsg) {
  // Validate pin
  if (pin < 0 || pin >= (int)GPIO_NUM_MAX || pin > 63) {
    errorMsg = "Invalid pin number";
    return false;
  }

  // Reset pin to ensure clean state
  (void)gpio_reset_pin((gpio_num_t)pin);

  // Configure as output
  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = (1ULL << pin);
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;

  esp_err_t err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    errorMsg = "GPIO config failed";
    log(ERROR, "GPIO config failed for pin %d: %s", pin, esp_err_to_name(err));
    return false;
  }

  // Set level
  err = gpio_set_level((gpio_num_t)pin, state ? 1 : 0);
  if (err != ESP_OK) {
    errorMsg = "GPIO write failed";
    log(ERROR, "GPIO write failed for pin %d: %s", pin, esp_err_to_name(err));
    return false;
  }

  log(INFO, "📍 GPIO %d out %s", pin, state ? "high" : "low");
  return true;
}

// GPIO helper method - get digital pin state
bool ESPWiFi::getGPIO(int pin, int &state, std::string &errorMsg) {
  if (pin < 0 || pin >= (int)GPIO_NUM_MAX || pin > 63) {
    errorMsg = "Invalid pin number";
    return false;
  }

  state = gpio_get_level((gpio_num_t)pin);
  log(DEBUG, "📍 GPIO %d read %s", pin, state ? "high" : "low");
  return true;
}

// GPIO helper method - set PWM
bool ESPWiFi::setPWM(int pin, int duty, int freq, std::string &errorMsg) {
  // Validate pin and duty
  if (pin < 0 || pin >= (int)GPIO_NUM_MAX || pin > 63) {
    errorMsg = "Invalid pin number";
    return false;
  }

  duty = std::clamp(duty, 0, 255);
  freq = std::clamp(freq, 1, 40000);

  // Reset pin first
  (void)gpio_reset_pin((gpio_num_t)pin);

  // Configure as output
  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = (1ULL << pin);
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;

  esp_err_t err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    errorMsg = "GPIO config failed";
    return false;
  }

  // Configure timer if needed
  if (!pwm_timer_configured) {
    ledc_timer_config_t timer_conf = {};
    timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_conf.timer_num = LEDC_TIMER_0;
    timer_conf.duty_resolution = LEDC_TIMER_8_BIT;
    timer_conf.freq_hz = freq;
    timer_conf.clk_cfg = LEDC_AUTO_CLK;

    err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) {
      errorMsg = "PWM timer config failed";
      log(ERROR, "LEDC timer config failed: %s", esp_err_to_name(err));
      return false;
    }
    pwm_timer_configured = true;
  }

  // Find or allocate channel
  ledc_channel_t chan = pwm_find_or_alloc_channel_for_pin(pin);
  if (chan == LEDC_CHANNEL_MAX) {
    errorMsg = "No PWM channels available";
    return false;
  }

  // Configure channel
  ledc_channel_config_t ch_conf = {};
  ch_conf.speed_mode = LEDC_LOW_SPEED_MODE;
  ch_conf.channel = chan;
  ch_conf.timer_sel = LEDC_TIMER_0;
  ch_conf.intr_type = LEDC_INTR_DISABLE;
  ch_conf.gpio_num = pin;
  ch_conf.duty = (uint32_t)duty;
  ch_conf.hpoint = 0;

  err = ledc_channel_config(&ch_conf);
  if (err != ESP_OK) {
    errorMsg = "PWM channel config failed";
    log(ERROR, "LEDC channel config failed (pin %d): %s", pin,
        esp_err_to_name(err));
    pwm_free_channel_for_pin(pin, LEDC_LOW_SPEED_MODE);
    return false;
  }

  // Set duty
  err = ledc_set_duty(LEDC_LOW_SPEED_MODE, chan, (uint32_t)duty);
  if (err == ESP_OK) {
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, chan);
  }
  if (err != ESP_OK) {
    errorMsg = "PWM duty update failed";
    log(ERROR, "LEDC duty update failed (pin %d): %s", pin,
        esp_err_to_name(err));
    return false;
  }

  log(INFO, "📍 GPIO %d pwm duty=%d freq=%d", pin, duty, freq);
  return true;
}
