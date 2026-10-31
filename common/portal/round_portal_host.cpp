#include "portal_host.h"

#include <Preferences.h>
#include <WebServer.h>

#include "ui_Settings.h"

extern uint8_t LCD_Backlight;
extern WebServer config_server;
extern void Set_Backlight(uint8_t Light);
extern bool apply_all_screen_visuals();

// Round portal: the config page leaves the socket up, there is no OTA page,
// and the display-type list is the two types these boards store.
const PortalHost &portal_host() {
    static const PortalHost host = {
        "ESP32-RoundDisplay",
        "",
        false,
        false,
        true,
        false
    };
    return host;
}

void portal_load_device_keys(Preferences &preferences) {
    // Slider position is stored on its own key. The old hardware brightness
    // key is only a fallback, and the curve is not applied through the
    // settings helper because LVGL is not up yet at load time.
    int16_t slider_pos = preferences.getShort("bright_pos", -1);
    if (slider_pos < 0) {
        uint16_t old_bright = preferences.getUShort("brightness", 50);
        if (old_bright < 10) old_bright = 10;
        if (old_bright > 100) old_bright = 100;
        slider_pos = 50 + ((int)old_bright - 10) * 50 / 90;
    }
    if (slider_pos > 100) slider_pos = 100;
    if (slider_pos < 0) slider_pos = 0;
    brightness_slider_pos = (uint8_t)slider_pos;
    if (brightness_slider_pos >= 50)
        LCD_Backlight = (uint8_t)(10 + (brightness_slider_pos - 50) * 90 / 50);
    else
        LCD_Backlight = (uint8_t)(1 + brightness_slider_pos * 9 / 49);
    Set_Backlight(LCD_Backlight);
}

void portal_save_device_keys(Preferences &preferences) {
    preferences.putShort("bright_pos", (int16_t)brightness_slider_pos);
}

void portal_append_brightness_html(String &html) {
    html += "<div class='form-row'><label>Brightness:</label><input id='brightness' name='brightness' type='range' min='0' max='100' value='" + String(brightness_slider_pos) + "' oninput=\"var v=this.value;document.getElementById('brightval').innerText=v;document.getElementById('modetext').innerText=v<50?' (Night Mode)':'';\"></div>";
    html += "<div class='form-row'><label>Current:</label><span id='brightval'>" + String(brightness_slider_pos) + "</span><span id='modetext'>" + String(brightness_slider_pos < 50 ? " (Night Mode)" : "") + "</span></div>";
}

void portal_apply_brightness_post() {
    int brightness = config_server.arg("brightness").toInt();
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;
    apply_brightness_from_slider((uint8_t)brightness);
}

void portal_silence_join_buzzer() {
}

void portal_after_gauge_save(int save_only) {
    (void)save_only;
    apply_all_screen_visuals();
}
