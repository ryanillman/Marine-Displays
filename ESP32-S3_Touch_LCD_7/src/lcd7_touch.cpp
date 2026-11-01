// GT911 on GPIO8/9, interrupt GPIO4. Reset already ran in ch422g_begin
// with INT held low, which latches address 0x5D. 0x14 is probed as well.

#include "../../ESP32-S3_Square_Display/src/Touch_GT911.h"

#include <Wire.h>
#include <string.h>

extern "C" int ets_printf(const char *fmt, ...);

uint8_t gt911_addr = 0;
uint8_t Touch_interrupts = 0;
static GT911_Touch touch_data = {};

static bool gt911_read(uint8_t addr, uint16_t reg, uint8_t *data, uint32_t len) {
    if (addr == 0 || data == NULL) return false;
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, (int)len) != (int)len) return false;
    for (uint32_t i = 0; i < len; i++) data[i] = Wire.read();
    return true;
}

static bool gt911_write(uint8_t addr, uint16_t reg, const uint8_t *data, uint32_t len) {
    if (addr == 0) return false;
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    for (uint32_t i = 0; i < len; i++) Wire.write(data[i]);
    return Wire.endTransmission(true) == 0;
}

static bool probe_addr(uint8_t addr) {
    uint8_t id[4] = {0, 0, 0, 0};
    if (!gt911_read(addr, ESP_LCD_TOUCH_GT911_PRODUCT_ID_REG, id, 3)) return false;
    if (id[0] < 0x20 || id[0] > 0x7e) return false;
    ets_printf("[LCD7] GT911 at 0x%02x id=%c%c%c\r\n", addr, id[0], id[1], id[2]);
    Serial.printf("[LCD7] GT911 at 0x%02x id=%c%c%c\n", addr, id[0], id[1], id[2]);
    return true;
}

uint8_t Touch_Init(void) {
    gt911_addr = 0;
    if (probe_addr(GT911_ADDR_PRIMARY)) gt911_addr = GT911_ADDR_PRIMARY;
    else if (probe_addr(GT911_ADDR_SECONDARY)) gt911_addr = GT911_ADDR_SECONDARY;
    else ets_printf("[LCD7] GT911 not found at 0x5D or 0x14\r\n");
    return gt911_addr ? 1 : 0;
}

uint8_t GT911_Touch_Reset(void) { return 0; }
void GT911_Read_cfg(void) {}

uint8_t Touch_Read_Data(void) {
    uint8_t buf[41];
    uint8_t clear = 0;
    memset(buf, 0, sizeof(buf));
    if (gt911_addr == 0) return 0;
    if (!gt911_read(gt911_addr, ESP_LCD_TOUCH_GT911_READ_XY_REG, buf, 1)) return 0;
    if ((buf[0] & 0x80) == 0) {
        gt911_write(gt911_addr, ESP_LCD_TOUCH_GT911_READ_XY_REG, &clear, 1);
        touch_data.points = 0;
        return 1;
    }
    uint8_t touch_cnt = buf[0] & 0x0F;
    if (touch_cnt == 0 || touch_cnt > GT911_LCD_TOUCH_MAX_POINTS) {
        gt911_write(gt911_addr, ESP_LCD_TOUCH_GT911_READ_XY_REG, &clear, 1);
        touch_data.points = 0;
        return 1;
    }
    if (!gt911_read(gt911_addr, ESP_LCD_TOUCH_GT911_READ_XY_REG + 1, &buf[1], (uint32_t)touch_cnt * 8)) {
        gt911_write(gt911_addr, ESP_LCD_TOUCH_GT911_READ_XY_REG, &clear, 1);
        touch_data.points = 0;
        return 0;
    }
    gt911_write(gt911_addr, ESP_LCD_TOUCH_GT911_READ_XY_REG, &clear, 1);
    touch_data.points = touch_cnt;
    for (uint8_t i = 0; i < touch_cnt; i++) {
        touch_data.coords[i].x = (uint16_t)(((uint16_t)buf[(i * 8) + 3] << 8) + buf[(i * 8) + 2]);
        touch_data.coords[i].y = (uint16_t)(((uint16_t)buf[(i * 8) + 5] << 8) + buf[(i * 8) + 4]);
        touch_data.coords[i].strength = (uint16_t)(((uint16_t)buf[(i * 8) + 7] << 8) + buf[(i * 8) + 6]);
    }
    return 1;
}

uint8_t Touch_Get_XY(uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num) {
    if (!x || !y || !point_num || max_point_num == 0) return 0;
    uint8_t n = touch_data.points;
    if (n > max_point_num) n = max_point_num;
    for (uint8_t i = 0; i < n; i++) {
        x[i] = touch_data.coords[i].x;
        y[i] = touch_data.coords[i].y;
        if (strength) strength[i] = touch_data.coords[i].strength;
    }
    *point_num = n;
    touch_data.points = 0;
    return n > 0;
}

void Touch_Loop(void) {}
void example_touchpad_read(void) {}
void IRAM_ATTR Touch_GT911_ISR(void) { Touch_interrupts = 1; }
