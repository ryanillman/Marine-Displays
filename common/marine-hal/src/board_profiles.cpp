#include "board_profile.h"

// The 2.1", 2.8", and 4" boards share this map. A panel with different pins
// supplies its own RgbPins; it does not need another copy of the driver.
static const RgbPins kRgbPins = {
    38, 39, 40, 41, -1,
    {5, 45, 48, 47, 21, 14, 13, 12, 11, 10, 9, 46, 3, 8, 18, 17},
};

#if defined(BOARD_ID_ROUND21)

static const RgbTiming kTiming = {
    8 * 1000 * 1000, 8, 10, 50, 3, 8, 8, 0, 16, 16, 2, 0,
};
static const St7701Link kSt7701 = {2, 1, -1, 6, St7701Script::ROUND21};
static const BoardProfile kProfile = {
    "round21", 480, 480, true, true, PanelKind::ST7701_RGB, &kTiming, &kRgbPins,
    TouchKind::CST820, ExpanderKind::TCA9554, SdKind::SDMMC, BacklightKind::PWM_GPIO,
    true, false, true, false, &kSt7701,
};

#elif defined(BOARD_ID_ROUND28)

static const RgbTiming kTiming = {
    8 * 1000 * 1000, 8, 10, 50, 2, 18, 8, 0, 16, 16, 2, 0,
};
static const St7701Link kSt7701 = {2, 1, -1, 6, St7701Script::ROUND28};
static const BoardProfile kProfile = {
    "round28", 480, 480, true, true, PanelKind::ST7701_RGB, &kTiming, &kRgbPins,
    TouchKind::GT911, ExpanderKind::TCA9554, SdKind::SDMMC, BacklightKind::PWM_GPIO,
    true, false, true, false, &kSt7701,
};

#elif defined(BOARD_ID_SQUARE)

// Square and Square-NMEA share this PCB profile (id "square4"). NMEA's IMU
// and CAN belong to that firmware, not to a second board. The expander is
// still probed at runtime (TCA9554 0x20 or CH32V003 0x24); ExpanderKind
// records the v3 part and the driver does not branch on it.
static const RgbTiming kTiming = {
    10 * 1000 * 1000, 8, 50, 10, 8, 40, 10, 1, 16, 16, 2, 0,
};
static const St7701Link kSt7701 = {2, 1, 42, -1, St7701Script::NONE};
static const BoardProfile kProfile = {
    "square4", 480, 480, false, true, PanelKind::ST7701_RGB, &kTiming, &kRgbPins,
    TouchKind::GT911, ExpanderKind::TCA9554, SdKind::SDMMC, BacklightKind::EXPANDER_LEVEL,
    true, false, true, false, &kSt7701,
};

#elif defined(BOARD_ID_LCD7)

// ESP32-S3-Touch-LCD-7 (800x480). These pins are not the 4" map above.
// Waveshare demo: 16 MHz, porches 4/8/8 and 4/8/8, data clocked on the falling edge.
// DATA0 is GPIO14 (B3). EXIO5 stays low so USB keeps GPIO19/20 and CAN stays off.
static const RgbPins kLcd7Pins = {
    46, 3, 5, 7, -1,
    {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40},
};
static const RgbTiming kTiming = {
    16 * 1000 * 1000, 4, 8, 8, 4, 8, 8, 1, 16, 16, 2, 800 * 10,
};
static const BoardProfile kProfile = {
    "lcd7", 800, 480, false, true, PanelKind::RGB_PARALLEL, &kTiming, &kLcd7Pins,
    TouchKind::GT911, ExpanderKind::CH422G, SdKind::SPI, BacklightKind::EXPANDER_LEVEL,
    false, false, false, false, nullptr,
};

#else
#error "Define BOARD_ID_ROUND21, BOARD_ID_ROUND28, BOARD_ID_SQUARE, or BOARD_ID_LCD7"
#endif

const BoardProfile *board_profile() {
  return &kProfile;
}

__attribute__((weak)) void setup_parallel_rgb_board() {}
