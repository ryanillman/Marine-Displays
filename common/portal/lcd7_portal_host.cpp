#include "portal_host.h"

#include <Preferences.h>
#include <WebServer.h>

#include "Display_ST7701.h"

extern uint16_t screen_off_timeout_min;
extern uint8_t LCD_Backlight;
extern volatile bool g_pending_visual_apply;
extern volatile bool g_screens_need_apply[5];
extern WebServer config_server;

// 7" portal. The config page drops the socket (the gauges page is large).
// OTA is offered. The backlight pin is on or off. There is no buzzer to arm.
const PortalHost &portal_host() {
    static const PortalHost host = {
        "ESP32-LCD7",
        "openplotter.local",
        true,
        true,
        false,
        false
    };
    return host;
}

void portal_load_device_keys(Preferences &preferences) {
    screen_off_timeout_min = preferences.getUShort("screen_off_tmout", 0);
    uint16_t saved_brightness = preferences.getUShort("brightness", 100);
    LCD_Backlight = (saved_brightness == 0) ? 0 : 100;
    Set_Backlight(LCD_Backlight);
}

void portal_save_device_keys(Preferences &preferences) {
    preferences.putUShort("brightness", (uint16_t)LCD_Backlight);
    preferences.putUShort("screen_off_tmout", screen_off_timeout_min);
}

void portal_append_brightness_html(String &html) {
    html += "<div class='form-row'><label>Screen Sleep:</label><select name='screen_off_timeout'>";
    html += "<option value='0'"  + String(screen_off_timeout_min==0 ?" selected":"") + ">Always on</option>";
    html += "<option value='1'"  + String(screen_off_timeout_min==1 ?" selected":"") + ">1 min</option>";
    html += "<option value='5'"  + String(screen_off_timeout_min==5 ?" selected":"") + ">5 min</option>";
    html += "<option value='10'" + String(screen_off_timeout_min==10?" selected":"") + ">10 min</option>";
    html += "<option value='30'" + String(screen_off_timeout_min==30?" selected":"") + ">30 min</option>";
    html += "</select></div>";
    bool on = LCD_Backlight > 0;
    html += "<div class='form-row'><label>Backlight:</label><select name='brightness_lv'>";
    html += "<option value='100'" + String(on ? " selected" : "") + ">On</option>";
    html += "<option value='0'" + String(on ? "" : " selected") + ">Off</option>";
    html += "</select></div>";
}

void portal_apply_brightness_post() {
    uint16_t sot = (uint16_t)config_server.arg("screen_off_timeout").toInt();
    if (sot != 1 && sot != 5 && sot != 10 && sot != 30) sot = 0;
    screen_off_timeout_min = sot;

    uint8_t bl = (uint8_t)config_server.arg("brightness_lv").toInt();
    bl = (bl == 0) ? 0 : 100;
    LCD_Backlight = bl;
    Set_Backlight(bl);
}

void portal_silence_join_buzzer() {}

void portal_after_gauge_save(int save_only) {
    // Defer the rebuild to loop(). Applying it inside handleClient() races
    // the display DMA. Only the saved screen is marked, so unchanged screens
    // do not re-read their background off the card.
    g_pending_visual_apply = true;
    if (save_only >= 0) {
        g_screens_need_apply[save_only] = true;
    } else {
        for (int i = 0; i < 5; i++) g_screens_need_apply[i] = true;
    }
}
