// GPIO.cpp - GPIO control endpoint (ESP-IDF httpd-safe)
#include "driver/gpio.h"

#include <algorithm>
#include <cmath>

#include "ESPWiFi.h"
#include "driver/ledc.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

namespace {

static bool validatePin(int pin, std::string *errorMsg) {
  if (pin < 0 || pin >= (int)GPIO_NUM_MAX || pin > 63) {
    if (errorMsg) {
      *errorMsg = "Invalid pin number";
    }
    return false;
  }
  return true;
}

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
  return LEDC_CHANNEL_MAX;  // sentinel for "none"
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

static adc_oneshot_unit_handle_t adc1_handle = nullptr;
static adc_cali_handle_t adc1_cali_handle = nullptr;

static bool ensureAdc1Calibration(adc_channel_t channel,
                                  std::string *errorMsg) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  static adc_cali_handle_t cali_for_channel[10] = {};
  if (channel >= 10) {
    if (errorMsg) {
      *errorMsg = "Invalid ADC channel";
    }
    return false;
  }
  if (cali_for_channel[channel] != nullptr) {
    adc1_cali_handle = cali_for_channel[channel];
    return true;
  }

  adc_cali_curve_fitting_config_t config = {
      .unit_id = ADC_UNIT_1,
      .chan = channel,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  if (adc_cali_create_scheme_curve_fitting(
          &config, &cali_for_channel[channel]) != ESP_OK) {
    if (errorMsg) {
      *errorMsg = "ADC calibration init failed";
    }
    return false;
  }
  adc1_cali_handle = cali_for_channel[channel];
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  if (adc1_cali_handle != nullptr) {
    return true;
  }

  adc_cali_line_fitting_config_t config = {
    .unit_id = ADC_UNIT_1,
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_DEFAULT,
#if CONFIG_IDF_TARGET_ESP32
    .default_vref = 1100,
#endif
  };
  if (adc_cali_create_scheme_line_fitting(&config, &adc1_cali_handle) !=
      ESP_OK) {
    if (errorMsg) {
      *errorMsg = "ADC calibration init failed";
    }
    return false;
  }
#else
  if (errorMsg) {
    *errorMsg = "ADC calibration not supported";
  }
  return false;
#endif

  return true;
}

static bool ensureAdc1Channel(int pin, adc_channel_t *channel,
                              std::string *errorMsg) {
  adc_unit_t unit;
  if (adc_oneshot_io_to_channel(pin, &unit, channel) != ESP_OK) {
    if (errorMsg) {
      *errorMsg = "Pin is not an ADC channel";
    }
    return false;
  }
  if (unit != ADC_UNIT_1) {
    if (errorMsg) {
      *errorMsg = "Only ADC1 pins are supported";
    }
    return false;
  }

  if (adc1_handle == nullptr) {
    adc_oneshot_unit_init_cfg_t init = {
      .unit_id = ADC_UNIT_1,
#if SOC_ADC_RTC_CTRL_SUPPORTED
      .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
#else
      .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
#endif
      .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&init, &adc1_handle) != ESP_OK) {
      if (errorMsg) {
        *errorMsg = "ADC init failed";
      }
      return false;
    }
  }

  adc_oneshot_chan_cfg_t cfg = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  if (adc_oneshot_config_channel(adc1_handle, *channel, &cfg) != ESP_OK) {
    if (errorMsg) {
      *errorMsg = "ADC channel config failed";
    }
    return false;
  }

  return true;
}

static bool isInputState(const std::string &state) { return state == "in"; }

static bool isHighState(const std::string &state) { return state == "high"; }

static bool isLowState(const std::string &state) { return state == "low"; }

}  // namespace

// GPIO helper method - configure pin state ("in", "high", "low")
bool ESPWiFi::setGPIO(int pin, const std::string &state,
                      std::string *errorMsg) {
  if (!validatePin(pin, errorMsg)) {
    return false;
  }

  if (isInputState(state)) {
    esp_err_t err = gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
      if (errorMsg) {
        *errorMsg = "GPIO input config failed";
      }
      log(ERROR, "GPIO input config failed for pin %d: %s", pin,
          esp_err_to_name(err));
      return false;
    }
    log(INFO, "📍 GPIO %d in", pin);
    return true;
  }

  if (!isHighState(state) && !isLowState(state)) {
    if (errorMsg) {
      *errorMsg = "Invalid GPIO state (use in, high, or low)";
    }
    return false;
  }

  const bool high = isHighState(state);

  (void)gpio_reset_pin((gpio_num_t)pin);

  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = (1ULL << pin);
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;

  esp_err_t err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    if (errorMsg) {
      *errorMsg = "GPIO config failed";
    }
    log(ERROR, "GPIO config failed for pin %d: %s", pin, esp_err_to_name(err));
    return false;
  }

  err = gpio_set_level((gpio_num_t)pin, high ? 1 : 0);
  if (err != ESP_OK) {
    if (errorMsg) {
      *errorMsg = "GPIO write failed";
    }
    log(ERROR, "GPIO write failed for pin %d: %s", pin, esp_err_to_name(err));
    return false;
  }

  log(INFO, "📍 GPIO %d out %s", pin, high ? "high" : "low");
  return true;
}

int ESPWiFi::readDigital(int pin, std::string *errorMsg) {
  if (!validatePin(pin, errorMsg)) {
    return -1;
  }

  esp_err_t err = gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
  if (err != ESP_OK) {
    if (errorMsg) {
      *errorMsg = "GPIO input config failed";
    }
    return -1;
  }

  const int state = gpio_get_level((gpio_num_t)pin);
  // log(DEBUG, "📍 GPIO %d read %s", pin, state ? "high" : "low");
  return state;
}

float ESPWiFi::readAnalog(int pin, std::string *errorMsg) {
  adc_channel_t channel;
  if (!ensureAdc1Channel(pin, &channel, errorMsg)) {
    return NAN;
  }
  if (!ensureAdc1Calibration(channel, errorMsg)) {
    return NAN;
  }

  int raw = 0;
  if (adc_oneshot_read(adc1_handle, channel, &raw) != ESP_OK) {
    if (errorMsg) {
      *errorMsg = "ADC read failed";
    }
    return NAN;
  }

  int voltage_mv = 0;
  if (adc_cali_raw_to_voltage(adc1_cali_handle, raw, &voltage_mv) != ESP_OK) {
    if (errorMsg) {
      *errorMsg = "ADC calibration failed";
    }
    return NAN;
  }

  const float value = voltage_mv / 1000.0f;
  // log(DEBUG, "📍 GPIO %d analog %.3f V", pin, value);
  return value;
}

// GPIO helper method - set PWM
bool ESPWiFi::setPWM(int pin, int duty, int freq, std::string *errorMsg) {
  // Validate pin and duty
  if (pin < 0 || pin >= (int)GPIO_NUM_MAX || pin > 63) {
    if (errorMsg) {
      *errorMsg = "Invalid pin number";
    }
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
    if (errorMsg) {
      *errorMsg = "GPIO config failed";
    }
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
      if (errorMsg) {
        *errorMsg = "PWM timer config failed";
      }
      log(ERROR, "LEDC timer config failed: %s", esp_err_to_name(err));
      return false;
    }
    pwm_timer_configured = true;
  }

  // Find or allocate channel
  ledc_channel_t chan = pwm_find_or_alloc_channel_for_pin(pin);
  if (chan == LEDC_CHANNEL_MAX) {
    if (errorMsg) {
      *errorMsg = "No PWM channels available";
    }
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
    if (errorMsg) {
      *errorMsg = "PWM channel config failed";
    }
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
    if (errorMsg) {
      *errorMsg = "PWM duty update failed";
    }
    log(ERROR, "LEDC duty update failed (pin %d): %s", pin,
        esp_err_to_name(err));
    return false;
  }

  log(INFO, "📍 GPIO %d pwm duty=%d freq=%d", pin, duty, freq);
  return true;
}
