#pragma once
// Event list and helpers for the Heltec V3's OLED status screen (see the display: section of
// steam-bridge.yaml). Every event is also written to the log.
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>

#include "driver/gpio.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace steam_ui {

// Heltec V3: GPIO36 switches "Vext", which powers the OLED (LOW = on). This has to happen before
// the I2C bus starts, so it's done with the raw ESP-IDF driver from an early on_boot hook.
inline void vext_on() {
  gpio_reset_pin(GPIO_NUM_36);
  gpio_set_direction(GPIO_NUM_36, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_36, 0);
  esphome::delay(50);
}

static const size_t MAX_EVENTS = 5;

struct Event {
  uint32_t at;
  std::string text;
};

inline std::deque<Event> &events() {
  static std::deque<Event> e;
  return e;
}

// millis() of the last thing worth showing (an event, or remote activity); drives screen sleep.
inline uint32_t &last_activity() {
  static uint32_t t = 0;
  return t;
}

// millis() of the last event; the screen jumps to the Activity page for a few seconds after one.
inline uint32_t &last_event() {
  static uint32_t t = 0;
  return t;
}

inline void touch() { last_activity() = esphome::millis(); }

inline void note(const std::string &text) {
  auto &e = events();
  // The remote sends most commands 2-5 times; show a repeat within 3 s as one event.
  if (!e.empty() && e.front().text == text && esphome::millis() - e.front().at < 3000) {
    e.front().at = esphome::millis();
    touch();
    return;
  }
  e.push_front({esphome::millis(), text});
  if (e.size() > MAX_EVENTS)
    e.pop_back();
  last_event() = esphome::millis();
  touch();
  ESP_LOGI("steam", "%s", text.c_str());
}

// "12s", "4m", "2h"
inline std::string ago(uint32_t at) {
  uint32_t s = (esphome::millis() - at) / 1000;
  char b[8];
  if (s < 60)
    snprintf(b, sizeof(b), "%us", (unsigned) s);
  else if (s < 3600)
    snprintf(b, sizeof(b), "%um", (unsigned) (s / 60));
  else
    snprintf(b, sizeof(b), "%uh", (unsigned) (s / 3600));
  return b;
}

// Human name of a remote command (first payload byte, plus its value byte where it helps).
inline std::string command_name(uint8_t cmd, uint8_t value) {
  switch (cmd) {
    case 0x00: return "poll";
    case 0x03: return "set temp";
    case 0x04: return "set time";
    case 0x05: return value ? "aroma on" : "aroma off";
    case 0x06: return "profile";
    case 0x07: return value ? "steam ON" : "steam OFF";
    case 0x15: return value ? "light on" : "light off";
    case 0x16: return "light mode";
    default: {
      char b[8];
      snprintf(b, sizeof(b), "cmd %02X", cmd);
      return b;
    }
  }
}

}  // namespace steam_ui
