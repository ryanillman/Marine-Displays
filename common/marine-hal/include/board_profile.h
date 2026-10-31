#pragma once

#include <stdint.h>
#include <stdbool.h>

// Hardware description for one PCB. This library drives ST7701_RGB only.
// RGB_PARALLEL is a generic RGB panel with no vendor init. ST7305_SPI is the reflective LCD.

enum class PanelKind : uint8_t {
  ST7701_RGB = 0,
  RGB_PARALLEL,
  ST7305_SPI,
};

enum class TouchKind : uint8_t {
  NONE = 0,
  GT911,
  CST820,
};

enum class ExpanderKind : uint8_t {
  NONE = 0,
  TCA9554,
  CH32V003,
  CH422G,
};

enum class SdKind : uint8_t {
  NONE = 0,
  SDMMC,
  SPI,
};

enum class BacklightKind : uint8_t {
  PWM_GPIO = 0,
  EXPANDER_LEVEL,
};

// Which vendor command list the round bit-bang transport sends.
// Square uses a different transport and leaves this NONE.
enum class St7701Script : uint8_t {
  NONE = 0,
  ROUND21,
  ROUND28,
};

struct RgbTiming {
  uint32_t pclk_hz;
  uint16_t hpw;
  uint16_t hbp;
  uint16_t hfp;
  uint16_t vpw;
  uint16_t vbp;
  uint16_t vfp;
  uint8_t pclk_active_neg;  // 0 rising, 1 falling
  uint8_t data_width;
  uint8_t pixel_bits;
  uint8_t frame_buf_num;
  uint16_t bounce_buf_size;
};

struct RgbPins {
  int hsync;
  int vsync;
  int de;
  int pclk;
  int disp;
  int data[16];
};

// Pins and script for an ST7701 panel. Null on boards that are not ST7701.
struct St7701Link {
  int clk_gpio;
  int mosi_gpio;
  int cs_gpio;         // -1 when chip-select is the IO expander
  int backlight_gpio;  // -1 when backlight is the IO expander
  St7701Script script;
};

struct BoardProfile {
  const char *id;  // "round21", "round28", "square4"
  uint16_t width;
  uint16_t height;
  bool round_mask;
  bool color;
  PanelKind panel;
  const RgbTiming *timing;  // null for SPI
  const RgbPins *rgb;       // null for SPI
  TouchKind touch;
  ExpanderKind expander;
  SdKind sd;
  BacklightKind backlight;
  bool has_rtc;
  bool has_imu;
  bool has_audio;
  bool has_can;
  const St7701Link *st7701;
};

// A function, not a macro. Display_ST7701.h reads the profile by calling it.
const BoardProfile *board_profile();
