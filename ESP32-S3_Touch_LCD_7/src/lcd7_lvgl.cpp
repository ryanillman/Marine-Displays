// SquareLine stays 480x480. The panel is wider. Flush adds the side offset
// so the gauges sit in the middle and the bars drawn at boot stay black.

#include "../../ESP32-S3_Square_Display/src/LVGL_Driver.h"

#include "board_profile.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <WiFi.h>

extern bool g_screen_is_off;
extern uint32_t g_last_activity_ms;

static const int kUiW = 480;
static const int kUiH = 480;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;

lv_disp_drv_t disp_drv;

static int letterbox_x() {
    int panel_w = board_profile()->width;
    if (panel_w <= kUiW) return 0;
    return (panel_w - kUiW) / 2;
}

void Lvgl_print(const char *buf) { (void)buf; }

void Lvgl_Display_LCD(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    int x0 = letterbox_x();
    LCD_addWindow((uint16_t)(area->x1 + x0), (uint16_t)area->y1,
                  (uint16_t)(area->x2 + x0), (uint16_t)area->y2,
                  (uint8_t *)color_p);
    lv_disp_flush_ready(drv);
}

void Lvgl_Touchpad_Read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    (void)indev_drv;
    uint16_t touchpad_x[1] = {0};
    uint16_t touchpad_y[1] = {0};
    uint16_t strength[1] = {0};
    uint8_t touchpad_cnt = 0;
    Touch_Read_Data();
    uint8_t pressed = Touch_Get_XY(touchpad_x, touchpad_y, strength, &touchpad_cnt, 1);
    if (!(pressed && touchpad_cnt > 0)) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    g_last_activity_ms = millis();
    int x0 = letterbox_x();
    int x = (int)touchpad_x[0] - x0;
    int y = (int)touchpad_y[0];
    static uint32_t last_log = 0;
    if (millis() - last_log > 500) {
        last_log = millis();
        Serial.printf("[LCD7] touch raw %u,%u -> ui %d,%d\n",
                      touchpad_x[0], touchpad_y[0], x, y);
    }

    if (g_screen_is_off) {
        g_screen_is_off = false;
        Set_Backlight(LCD_Backlight);
        WiFi.setSleep(false);
        Serial.println("[SCREEN] Wake on touch — screen on");
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    if (x < 0 || x >= kUiW || y < 0 || y >= kUiH) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    data->point.x = (lv_coord_t)x;
    data->point.y = (lv_coord_t)y;
    data->state = LV_INDEV_STATE_PR;
}

static void lvgl_tick(void *arg) {
    (void)arg;
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void *alloc_buf(size_t bytes) {
    const size_t align = 64;
    void *p = heap_caps_aligned_alloc(align, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!p) p = heap_caps_aligned_alloc(align, bytes, MALLOC_CAP_DMA);
    if (!p) p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL);
    return p;
}

void Lvgl_Init(void) {
    lv_init();
    size_t px = (size_t)(kUiW * kUiH / 5);
    size_t bytes = px * sizeof(lv_color_t);
    buf1 = (lv_color_t *)alloc_buf(bytes);
    buf2 = (lv_color_t *)alloc_buf(bytes);
    Serial.printf("[LCD7] LVGL buffers %u bytes buf1=%p buf2=%p letterbox x=%d\n",
                  (unsigned)bytes, buf1, buf2, letterbox_x());
    if (!buf1 || !buf2) {
        ets_printf("[LCD7] LVGL buffer alloc failed\r\n");
        return;
    }
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, px);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = kUiW;
    disp_drv.ver_res = kUiH;
    disp_drv.flush_cb = Lvgl_Display_LCD;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = panel_handle;
    disp_drv.sw_rotate = 0;
    disp_drv.rotated = LV_DISP_ROT_NONE;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    lv_disp_set_default(disp);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = Lvgl_Touchpad_Read;
    lv_indev_drv_register(&indev_drv);

    const esp_timer_create_args_t args = {
        .callback = &lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t timer = NULL;
    esp_timer_create(&args, &timer);
    esp_timer_start_periodic(timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000);
}

void Lvgl_Loop(void) { lv_timer_handler(); }

uint32_t get_flush_max_us() { return 0; }
uint32_t get_flush_count() { return 0; }
void reset_flush_stats() {}
void Lvgl_ToggleRemap() {}
void Lvgl_EnableRemap(bool en) { (void)en; }
void Lvgl_SetRemapTable(const uint8_t table[16]) { (void)table; }
void Lvgl_PrintRemap() {}
