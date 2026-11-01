// ESP32-S3-Touch-LCD-7 bring-up. The square firmware's setup() calls
// setup_parallel_rgb_board() and then continues into LVGL and the portal.
// Symbols below exist so the 4" hardware calls in that same file still link.
// They are not used: the profile panel is RGB_PARALLEL, so that branch does
// not run. The CH422G outputs stay under this file.

#include "board_profile.h"
#include "Display_ST7701.h"

#include "../../ESP32-S3_Square_Display/src/I2C_Driver.h"
#include "../../ESP32-S3_Square_Display/src/TCA9554PWR.h"
#include "../../ESP32-S3_Square_Display/src/SD_Card.h"
#include "../../ESP32-S3_Square_Display/src/RTC_PCF85063.h"
#include "../../ESP32-S3_Square_Display/src/Touch_GT911.h"

#include <Arduino.h>
#include <Wire.h>
#include <string.h>

extern "C" int ets_printf(const char *fmt, ...);

static const int kSda = 8;
static const int kScl = 9;
static const int kTouchInt = 4;

static const uint8_t kWrSet = 0x24;
static const uint8_t kWrIo = 0x38;
static const int kExioTpRst = 1;
static const int kExioBacklight = 2;
static const int kExioLcdRst = 3;
static const int kExioSdCs = 4;
static const int kExioUsbSel = 5;

static uint8_t s_io = 0;

static bool ch422g_write(uint8_t addr, uint8_t data) {
    Wire.beginTransmission(addr);
    Wire.write(data);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
        ets_printf("[LCD7] CH422G write addr 0x%02x data 0x%02x err %u\r\n", addr, data, err);
        return false;
    }
    return true;
}

static void ch422g_bit(int bit, bool high) {
    if (high) s_io = (uint8_t)(s_io | (1u << bit));
    else s_io = (uint8_t)(s_io & ~(1u << bit));
    ch422g_write(kWrIo, s_io);
}

void lcd7_set_backlight(bool on) {
    ch422g_bit(kExioBacklight, on);
    ets_printf("[LCD7] backlight %s\r\n", on ? "on" : "off");
}

static void i2c_bus_recover() {
    pinMode(kScl, OUTPUT);
    digitalWrite(kScl, HIGH);
    pinMode(kSda, INPUT_PULLUP);
    delayMicroseconds(10);
    if (digitalRead(kSda) == LOW) {
        for (int i = 0; i < 9; i++) {
            digitalWrite(kScl, LOW);
            delayMicroseconds(5);
            digitalWrite(kScl, HIGH);
            delayMicroseconds(5);
            if (digitalRead(kSda) == HIGH) break;
        }
        pinMode(kSda, OUTPUT);
        digitalWrite(kSda, LOW);
        delayMicroseconds(5);
        digitalWrite(kScl, HIGH);
        delayMicroseconds(5);
        digitalWrite(kSda, HIGH);
        delayMicroseconds(5);
    }
}

void I2C_Init(void) {
    i2c_bus_recover();
    Wire.begin(kSda, kScl);
    Wire.setClock(400000);
}

bool I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length) {
    Wire.beginTransmission(Driver_addr);
    Wire.write(Reg_addr);
    if (Wire.endTransmission(true) != 0) return false;
    if (Wire.requestFrom(Driver_addr, Length) != Length) return false;
    for (uint32_t i = 0; i < Length; i++) *Reg_data++ = Wire.read();
    return true;
}

bool I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length) {
    Wire.beginTransmission(Driver_addr);
    Wire.write(Reg_addr);
    for (uint32_t i = 0; i < Length; i++) Wire.write(*Reg_data++);
    return Wire.endTransmission(true) == 0;
}

static void ch422g_begin() {
    // Latch the pin levels before the pins become outputs. USB/CAN select
    // (EXIO5) stays low so native USB keeps GPIO19/20 and CAN stays off.
    // SD chip-select is held high so a card, if present, stays idle.
    s_io = (uint8_t)(1u << kExioSdCs);
    ch422g_write(kWrIo, s_io);
    ch422g_write(kWrSet, 0x01);

    // Panel reset on EXIO3. The RGB bus has no reset GPIO of its own.
    delay(10);
    ch422g_bit(kExioLcdRst, true);
    delay(100);

    // GT911 latches 0x5D when INT is low as reset rises.
    pinMode(kTouchInt, OUTPUT);
    digitalWrite(kTouchInt, LOW);
    delay(10);
    delay(100);
    ch422g_bit(kExioTpRst, true);
    delay(200);
    pinMode(kTouchInt, INPUT);
    ets_printf("[LCD7] CH422G ready, USB select low\r\n");
}

void setup_parallel_rgb_board() {
    Serial.begin(115200);
    ets_printf("[LCD7] parallel RGB board\r\n");
    I2C_Init();
    delay(10);
    ch422g_begin();
    LCD_Backlight = 100;
    lcd7_set_backlight(true);
    LCD_Init();
    Touch_Init();
    if (SD_Init() == ESP_OK) Serial.println("[LCD7] SD mounted");
    Serial.printf("LCD initialized at %u MHz pixel clock\n",
                  (unsigned)(ESP_PANEL_LCD_RGB_TIMING_FREQ_HZ / 1000000));
}

// --- links for the 4" calls that this image does not run ---

uint8_t g_tca9554_address = 0x20;

bool detect_expander_address() { return false; }
bool is_board_v4() { return false; }
uint8_t ch32v003_get_dir_shadow() { return 0; }
uint8_t ch32v003_get_out_shadow() { return 0; }
uint8_t exio_output_reg() { return 0x01; }
uint8_t exio_dir_reg() { return 0x03; }
uint8_t exio_input_reg() { return 0x00; }
uint8_t pin_tp_rst() { return 1; }
uint8_t pin_lcd_rst() { return 3; }
uint8_t pin_sdcs() { return 4; }
bool I2C_Read_EXIO_safe(uint8_t REG, uint8_t *out) {
    (void)REG;
    if (out) *out = 0;
    return false;
}
uint8_t I2C_Read_EXIO(uint8_t REG) {
    (void)REG;
    return 0;
}
uint8_t I2C_Write_EXIO(uint8_t REG, uint8_t Data) {
    (void)REG;
    (void)Data;
    return 0;
}
void Mode_EXIO(uint8_t Pin, uint8_t State) {
    (void)Pin;
    (void)State;
}
void Mode_EXIOS(uint8_t PinState) { (void)PinState; }
uint8_t Read_EXIO(uint8_t Pin) {
    (void)Pin;
    return 0;
}
uint8_t Read_EXIOS(uint8_t REG) {
    (void)REG;
    return 0;
}
void Set_EXIO(uint8_t Pin, uint8_t State) {
    (void)Pin;
    (void)State;
}
void Set_EXIOS(uint8_t PinState) { (void)PinState; }
void Set_Toggle(uint8_t Pin) { (void)Pin; }
void TCA9554PWR_Init(uint8_t PinState) { (void)PinState; }
void backlight_set_pwm(uint8_t duty) { (void)duty; }

void PCF85063_Init(void) {}
void PCF85063_Set_All(datetime_t time) { (void)time; }
void PCF85063_Read_Time(datetime_t *time) {
    if (time) memset(time, 0, sizeof(*time));
}

uint16_t SDCard_Size = 0;
uint16_t Flash_Size = 0;

esp_err_t SD_Init() {
    // The portal reads and writes SD_MMC. Those pins are not set, so begin()
    // fails without touching the RGB bus. Screen blobs stay in NVS.
    Serial.println("[LCD7] SD not mounted (screens stay in NVS)");
    return ESP_FAIL;
}
void SD_D3_Dis() {}
void SD_D3_EN() {}
void SD_RecoveryCheck(void) {}
bool SD_IsMounted() { return false; }
bool File_Search(const char *directory, const char *fileName) {
    (void)directory;
    (void)fileName;
    return false;
}
uint16_t Folder_retrieval(const char *directory, const char *fileExtension, char File_Name[][100], uint16_t maxFiles) {
    (void)directory;
    (void)fileExtension;
    (void)File_Name;
    (void)maxFiles;
    return 0;
}
void Flash_test() {}
