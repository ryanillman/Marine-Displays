#include "portal_host.h"

#include <Preferences.h>
#include <WebServer.h>

#include "ui_Settings.h"
#include "TCA9554PWR.h"

// Defined later in the shared portal. This file is compiled into that
// translation unit, ahead of those definitions.
extern uint16_t screen_off_timeout_min;
extern uint8_t LCD_Backlight;
extern volatile bool g_pending_visual_apply;
extern volatile bool g_screens_need_apply[5];
extern WebServer config_server;
extern void Set_Backlight(uint8_t Light);

// 4" portal: on-change is a client concern. Here the config page drops the
// socket, OTA is offered, and the buzzer cooldown is armed on load and save.
const PortalHost &portal_host() {
    static const PortalHost host = {
        "ESP32-SquareDisplay",
        "openplotter.local",
        true,
        true,
        false,
        true
    };
    return host;
}

void portal_load_device_keys(Preferences &preferences) {
    screen_off_timeout_min = preferences.getUShort("screen_off_tmout", 0);
    uint16_t saved_brightness = preferences.getUShort("brightness", (uint16_t)LCD_Backlight);
    LCD_Backlight = (uint8_t)saved_brightness;
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
    // V4: unified slider (10-200, red mode below 110). V3: 4-step slider.
    if (is_board_v4()) {
        html += "<div class='form-row'><label>Brightness:</label>";
        html += "<input type='range' name='brightness_lv' min='10' max='200' value='" + String(brightness_level) + "'";
        html += " oninput='var v=parseInt(this.value),s=this.nextElementSibling;";
        html += "if(v>=110){var f=(v-110)/90;var p=Math.round(10+f*f*90);s.textContent=p+\"%\";s.style.color=\"#111\";}";
        html += "else{var f=(v-10)/99;var p=Math.round(10+f*f*90);s.textContent=\"R:\"+p+\"%\";s.style.color=\"#f44\";}' style='width:120px;vertical-align:middle;'>";
        {
            float frac;
            int pwm;
            if (brightness_level >= 110) {
                frac = (float)(brightness_level - 110) / 90.0f;
            } else {
                frac = (float)(brightness_level - 10) / 99.0f;
            }
            pwm = 10 + (int)(frac * frac * 90.0f);
            if (brightness_level >= 110) {
                html += "<span style='margin-left:8px;color:#111;'>" + String(pwm) + "%</span>";
            } else {
                html += "<span style='margin-left:8px;color:#f44;'>R:" + String(pwm) + "%</span>";
            }
        }
        html += "</div>";
    } else {
        static const char *blabels[] = {"Normal", "Dim", "Night", "Night+"};
        html += "<div class='form-row'><label>Brightness:</label>";
        html += "<input type='range' name='brightness_lv' min='0' max='3' value='" + String(brightness_level) + "'";
        html += " oninput='this.nextElementSibling.textContent=[\"Normal\",\"Dim\",\"Night\",\"Night+\"][this.value]'";
        html += " style='width:120px;vertical-align:middle;'>";
        html += "<span style='margin-left:8px;color:#fff;'>" + String(blabels[brightness_level < 4 ? brightness_level : 0]) + "</span>";
        html += "</div>";
    }
}

void portal_apply_brightness_post() {
    uint16_t sot = (uint16_t)config_server.arg("screen_off_timeout").toInt();
    if (sot != 1 && sot != 5 && sot != 10 && sot != 30) sot = 0;
    screen_off_timeout_min = sot;

    uint8_t bl = (uint8_t)config_server.arg("brightness_lv").toInt();
    if (is_board_v4()) {
        int bl_raw = config_server.arg("brightness_lv").toInt();
        if (bl_raw > 200) bl_raw = 200;
        if (bl_raw < 10) bl_raw = 10;
        bl = (uint8_t)bl_raw;
    } else {
        if (bl > 3) bl = 0;
    }
    set_brightness_level(bl);
}

void portal_silence_join_buzzer() {
    // setup() may have left the buzzer pin high if the expander direction
    // write failed after a crash-reboot. Hold it off while WiFi joins.
    if (is_board_v4()) {
        uint8_t cur_dir;
        if (I2C_Read_EXIO_safe(exio_dir_reg(), &cur_dir) && cur_dir != CH32V003_DIR_SAFE) {
            I2C_Write_EXIO(exio_dir_reg(), CH32V003_DIR_SAFE);
        }
    } else {
        Set_EXIOS(Read_EXIOS(exio_output_reg()) & (uint8_t)~(1 << (EXIO_PIN6 - 1)));
        Mode_EXIOS(0x00);
    }
}

void portal_after_gauge_save(int save_only) {
    // Defer the rebuild to loop(). Applying it inside handleClient() races
    // the display DMA. Only the saved screen is marked, so unchanged screens
    // do not re-read their background off the SD card.
    g_pending_visual_apply = true;
    if (save_only >= 0) {
        g_screens_need_apply[save_only] = true;
    } else {
        for (int i = 0; i < 5; i++) g_screens_need_apply[i] = true;
    }
}
