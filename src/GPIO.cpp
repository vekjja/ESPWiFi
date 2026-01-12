// GPIO.cpp - GPIO control endpoint (ESP-IDF httpd-safe)
#include "ESPWiFi.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

#include <algorithm>

namespace {

static bool strEqAny(const std::string &s, const char *a, const char *b,
                     const char *c = nullptr) {
  if (s == a || s == b) {
    return true;
  }
  return (c != nullptr) ? (s == c) : false;
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

void ESPWiFi::srvGPIO() {
  (void)registerRoute(
      "/api/gpio", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        if (espwifi == nullptr || req == nullptr) {
          if (req != nullptr) {
            httpd_resp_send_500(req);
          }
          return ESP_OK;
        }

        JsonDocument reqJson = espwifi->readRequestBody(req);
        if (reqJson.size() == 0) {
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"EmptyInput\"}", &clientInfo);
        }

        const int pinNum = reqJson["num"] | -1;
        std::string mode = reqJson["mode"] | "";
        std::string state = reqJson["state"] | "";
        int duty = reqJson["duty"] | 0;
        const bool isDelete = reqJson["delete"] | false;

        toLowerCase(mode);
        toLowerCase(state);

        if (pinNum < 0) {
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing pin number\"}", &clientInfo);
        }
        // Keep bitmask safe (gpio_config uses uint64_t).
        if (pinNum >= (int)GPIO_NUM_MAX || pinNum > 63) {
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Invalid pin number\"}", &clientInfo);
        }

        // If requested, reset/disable the pin (and detach PWM if any).
        if (isDelete) {
          pwm_free_channel_for_pin(pinNum, LEDC_LOW_SPEED_MODE);
          (void)gpio_reset_pin((gpio_num_t)pinNum);
          espwifi->log(INFO, "📍 GPIO %d delete", pinNum);
          return espwifi->sendJsonResponse(req, 200, "{\"status\":\"Success\"}",
                                           &clientInfo);
        }

        const bool wantPwm = strEqAny(mode, "pwm", "ledc");
        const bool wantOut = strEqAny(mode, "out", "output") || wantPwm;
        const bool wantIn = strEqAny(mode, "in", "input");

        if (!wantOut && !wantIn) {
          char buf[160];
          (void)snprintf(buf, sizeof(buf), "{\"error\":\"Invalid mode: %s\"}",
                         mode.c_str());
          return espwifi->sendJsonResponse(req, 400, std::string(buf),
                                           &clientInfo);
        }

        // Validate state early (Arduino-style API: "high"/"low")
        const bool stateHigh = (state == "high");
        const bool stateLow = (state == "low");
        if (!stateHigh && !stateLow) {
          char buf[160];
          (void)snprintf(buf, sizeof(buf), "{\"error\":\"Invalid state: %s\"}",
                         state.c_str());
          return espwifi->sendJsonResponse(req, 400, std::string(buf),
                                           &clientInfo);
        }

        // If switching away from PWM, stop/detach any previous PWM on this pin.
        if (!wantPwm) {
          pwm_free_channel_for_pin(pinNum, LEDC_LOW_SPEED_MODE);
        }

        // Configure GPIO mode.
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << pinNum);
        io_conf.intr_type = GPIO_INTR_DISABLE;
        if (wantOut) {
          io_conf.mode = GPIO_MODE_OUTPUT;
          io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
          io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        } else {
          // INPUT: Arduino's digitalWrite(HIGH) after pinMode(INPUT) enables
          // pull-up. We'll map state=high -> pull-up enabled, state=low -> off.
          io_conf.mode = GPIO_MODE_INPUT;
          io_conf.pull_up_en =
              stateHigh ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
          io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        }

        // Best-effort reset to ensure pad mux is GPIO.
        (void)gpio_reset_pin((gpio_num_t)pinNum);
        esp_err_t gerr = gpio_config(&io_conf);
        if (gerr != ESP_OK) {
          espwifi->log(ERROR, "GPIO config failed for pin %d: %s", pinNum,
                       esp_err_to_name(gerr));
          return espwifi->sendJsonResponse(
              req, 500, "{\"error\":\"GPIO config failed\"}", &clientInfo);
        }

        if (wantPwm) {
          // Configure timer once. Keep it simple: 8-bit duty like Arduino
          // analogWrite(), 5kHz.
          if (!pwm_timer_configured) {
            ledc_timer_config_t timer_conf = {};
            timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
            timer_conf.timer_num = LEDC_TIMER_0;
            timer_conf.duty_resolution = LEDC_TIMER_8_BIT;
            timer_conf.freq_hz = 5000;
            timer_conf.clk_cfg = LEDC_AUTO_CLK;

            esp_err_t terr = ledc_timer_config(&timer_conf);
            if (terr != ESP_OK) {
              espwifi->log(ERROR, "LEDC timer config failed: %s",
                           esp_err_to_name(terr));
              return espwifi->sendJsonResponse(
                  req, 500, "{\"error\":\"PWM init failed\"}", &clientInfo);
            }
            pwm_timer_configured = true;
          }

          // Clamp duty to 8-bit range.
          duty = std::clamp(duty, 0, 255);
          const int appliedDuty = stateHigh ? duty : 0;

          ledc_channel_t chan = pwm_find_or_alloc_channel_for_pin(pinNum);
          if (chan == LEDC_CHANNEL_MAX) {
            return espwifi->sendJsonResponse(
                req, 400, "{\"error\":\"No PWM channels available\"}",
                &clientInfo);
          }

          ledc_channel_config_t ch_conf = {};
          ch_conf.speed_mode = LEDC_LOW_SPEED_MODE;
          ch_conf.channel = chan;
          ch_conf.timer_sel = LEDC_TIMER_0;
          ch_conf.intr_type = LEDC_INTR_DISABLE;
          ch_conf.gpio_num = pinNum;
          ch_conf.duty = (uint32_t)appliedDuty;
          ch_conf.hpoint = 0;

          esp_err_t cerr = ledc_channel_config(&ch_conf);
          if (cerr != ESP_OK) {
            espwifi->log(ERROR, "LEDC channel config failed (pin %d): %s",
                         pinNum, esp_err_to_name(cerr));
            pwm_free_channel_for_pin(pinNum, LEDC_LOW_SPEED_MODE);
            return espwifi->sendJsonResponse(
                req, 500, "{\"error\":\"PWM config failed\"}", &clientInfo);
          }

          esp_err_t derr =
              ledc_set_duty(LEDC_LOW_SPEED_MODE, chan, (uint32_t)appliedDuty);
          if (derr == ESP_OK) {
            derr = ledc_update_duty(LEDC_LOW_SPEED_MODE, chan);
          }
          if (derr != ESP_OK) {
            espwifi->log(ERROR, "LEDC duty update failed (pin %d): %s", pinNum,
                         esp_err_to_name(derr));
            return espwifi->sendJsonResponse(
                req, 500, "{\"error\":\"PWM duty update failed\"}",
                &clientInfo);
          }

          espwifi->log(INFO, "📍 GPIO %d pwm %s %d", pinNum,
                       stateHigh ? "high" : "low", appliedDuty);
        } else if (wantOut) {
          esp_err_t werr =
              gpio_set_level((gpio_num_t)pinNum, stateHigh ? 1 : 0);
          if (werr != ESP_OK) {
            espwifi->log(ERROR, "GPIO write failed for pin %d: %s", pinNum,
                         esp_err_to_name(werr));
            return espwifi->sendJsonResponse(
                req, 500, "{\"error\":\"GPIO write failed\"}", &clientInfo);
          }
          espwifi->log(INFO, "📍 GPIO %d out %s", pinNum,
                       stateHigh ? "high" : "low");
        } else {
          // INPUT: config already applied; state selects pull-up (see above).
          espwifi->log(INFO, "📍 GPIO %d in %s", pinNum,
                       stateHigh ? "high" : "low");
        }

        return espwifi->sendJsonResponse(req, 200, "{\"status\":\"Success\"}",
                                         &clientInfo);
      });
}