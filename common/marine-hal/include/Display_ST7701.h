#pragma once

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_rgb.h"
#include <esp_err.h>

#include "board_profile.h"

// These names used to be numeric literals in each board tree. They now read
// the compiled-in profile. They are not integer constant expressions.
// LVGL_Driver.h defines LVGL_WIDTH from ESP_PANEL_LCD_WIDTH, which is fine:
// nothing uses these as array bounds.

#define LCD_CLK_PIN (board_profile()->st7701->clk_gpio)
#define LCD_MOSI_PIN (board_profile()->st7701->mosi_gpio)
#define LCD_CS_PIN (board_profile()->st7701->cs_gpio)
#define LCD_Backlight_PIN (board_profile()->st7701->backlight_gpio)

// PWM numbers are the same on every color board, so they stay literals.
#define PWM_Channel 1
#define Frequency 20000
#define Resolution 10
#define Dutyfactor 500
#define Backlight_MAX 100

#define ESP_PANEL_LCD_WIDTH (board_profile()->width)
#define ESP_PANEL_LCD_HEIGHT (board_profile()->height)
#define ESP_PANEL_LCD_RGB_TIMING_FREQ_HZ (board_profile()->timing->pclk_hz)
#define ESP_PANEL_LCD_RGB_TIMING_HPW (board_profile()->timing->hpw)
#define ESP_PANEL_LCD_RGB_TIMING_HBP (board_profile()->timing->hbp)
#define ESP_PANEL_LCD_RGB_TIMING_HFP (board_profile()->timing->hfp)
#define ESP_PANEL_LCD_RGB_TIMING_VPW (board_profile()->timing->vpw)
#define ESP_PANEL_LCD_RGB_TIMING_VBP (board_profile()->timing->vbp)
#define ESP_PANEL_LCD_RGB_TIMING_VFP (board_profile()->timing->vfp)
#define ESP_PANEL_LCD_RGB_PCLK_ACTIVE_NEG (board_profile()->timing->pclk_active_neg)
#define ESP_PANEL_LCD_RGB_DATA_WIDTH (board_profile()->timing->data_width)
#define ESP_PANEL_LCD_RGB_PIXEL_BITS (board_profile()->timing->pixel_bits)
#define ESP_PANEL_LCD_RGB_FRAME_BUF_NUM (board_profile()->timing->frame_buf_num)
#define ESP_PANEL_LCD_RGB_BOUNCE_BUF_SIZE (board_profile()->timing->bounce_buf_size)
#define ESP_PANEL_LCD_BOUNCE_BUF_SIZE ESP_PANEL_LCD_RGB_BOUNCE_BUF_SIZE

#define ESP_PANEL_LCD_PIN_NUM_RGB_HSYNC (board_profile()->rgb->hsync)
#define ESP_PANEL_LCD_PIN_NUM_RGB_VSYNC (board_profile()->rgb->vsync)
#define ESP_PANEL_LCD_PIN_NUM_RGB_DE (board_profile()->rgb->de)
#define ESP_PANEL_LCD_PIN_NUM_RGB_PCLK (board_profile()->rgb->pclk)
#define ESP_PANEL_LCD_PIN_NUM_RGB_DISP (board_profile()->rgb->disp)
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA0 (board_profile()->rgb->data[0])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA1 (board_profile()->rgb->data[1])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA2 (board_profile()->rgb->data[2])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA3 (board_profile()->rgb->data[3])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA4 (board_profile()->rgb->data[4])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA5 (board_profile()->rgb->data[5])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA6 (board_profile()->rgb->data[6])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA7 (board_profile()->rgb->data[7])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA8 (board_profile()->rgb->data[8])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA9 (board_profile()->rgb->data[9])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA10 (board_profile()->rgb->data[10])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA11 (board_profile()->rgb->data[11])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA12 (board_profile()->rgb->data[12])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA13 (board_profile()->rgb->data[13])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA14 (board_profile()->rgb->data[14])
#define ESP_PANEL_LCD_PIN_NUM_RGB_DATA15 (board_profile()->rgb->data[15])

#define ESP_PANEL_LCD_BK_LIGHT_ON_LEVEL (1)
#define ESP_PANEL_LCD_BK_LIGHT_OFF_LEVEL !ESP_PANEL_LCD_BK_LIGHT_ON_LEVEL

// Square transport only. Defined for every board so the shared driver sees
// the same switches the 4" tree compiled with. Rounds do not reference them.
#define REMOVE_PANEL_TESTS 1
#define USE_SWSPI_VENDOR_INIT 0

extern uint8_t LCD_Backlight;
extern esp_lcd_panel_handle_t panel_handle;
bool example_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_data);
uint32_t get_vsync_count();
uint32_t get_vsync_max_gap_us();
void reset_vsync_stats();
void ST7701_Init();

void LCD_Init();
void LCD_addWindow(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend, uint8_t *color);

// Declared on every board. Defined by the square transport only. Nothing in the
// round trees calls them, and the 4" build already left the RunDiagnostics
// entry points without a definition.
void Display_RunDiagnostics();
void Display_RunDiagnosticsLoop(int cycles, uint32_t ms_each);
void Display_RunDiagnosticsAsync(int cycles, uint32_t ms_each);
#ifdef ENABLE_RGB_DIAG
void Display_Diag_HandleChar(char c);
#endif
esp_err_t Display_SDA_Probe();
esp_err_t Display_SDA_Hold_Test(uint32_t hold_ms);

void Backlight_Init();
void Set_Backlight(uint8_t Light);
