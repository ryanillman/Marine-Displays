// Threshold alarms for every armed screen, and the anchor watch.
// An invalid sample does not trip a high or low threshold.
// The anchor watch does sound when it is armed and the position is unknown,
// including while another screen is in front.

#include "unit_convert.h"
#include "ui_Settings.h"
#include <math.h>

extern unsigned long last_buzzer_time;

__attribute__((weak)) bool anchor_watch_alarm_due(int) { return false; }

static bool cooldown_ready(unsigned long now) {
    unsigned long cooldown_ms = (buzzer_cooldown_sec == 0) ? 0UL : (unsigned long)buzzer_cooldown_sec * 1000UL;
    return first_run_buzzer || (now - last_buzzer_time > cooldown_ms);
}

static void alarm_fire(int screen, const char *why) {
    Serial.printf("[ALARM] screen=%d %s\n", screen, why);
    trigger_buzzer_alert();
    last_buzzer_time = millis();
    first_run_buzzer = false;
}

static float to_display(float raw, const String &path) {
    String si = get_sensor_unit_by_path(path);
    String shown;
    return convert_unit(raw, si, path, shown);
}

static bool high_low(int screen, int gauge, int low_zone, int high_zone, float display) {
    if (!isfinite(display)) return false;
    bool low = screen_configs[screen].buzzer[gauge][low_zone] != 0
        && display < screen_configs[screen].min[gauge][low_zone];
    bool high = screen_configs[screen].buzzer[gauge][high_zone] != 0
        && display > screen_configs[screen].max[gauge][high_zone];
    return low || high;
}

static bool gauge_zone_tripped(int screen, int gauge) {
    Sample sample = get_sensor_sample(screen * 2 + gauge);
    if (!sample.valid) return false;
    String path = get_signalk_path_by_index(screen * 2 + gauge);
    bool legacy_raw = true;
    for (int zone = 1; zone <= 4; zone++) {
        if (screen_configs[screen].threshold_unit[gauge][zone][0] != '\0') legacy_raw = false;
    }
    float value = legacy_raw ? sample.value : to_display(sample.value, path);
    int chosen = -1;
    float best = 1e30f;
    for (int zone = 1; zone <= 4; zone++) {
        float mn = screen_configs[screen].min[gauge][zone];
        float mx = screen_configs[screen].max[gauge][zone];
        if (mn == mx) continue;
        if (value >= mn && value <= mx) {
            float range = mx - mn;
            if (range < best) {
                best = range;
                chosen = zone;
            }
        }
    }
    if (chosen < 0) return false;
    return screen_configs[screen].buzzer[gauge][chosen] != 0;
}

static bool path_tripped(int screen, const char *path, int gauge, int low_zone, int high_zone) {
    if (path == NULL || path[0] == '\0') return false;
    Sample sample = get_sensor_sample_by_path(String(path));
    if (!sample.valid) return false;
    return high_low(screen, gauge, low_zone, high_zone, to_display(sample.value, String(path)));
}

static bool screen_tripped(int screen) {
    int type = screen_configs[screen].display_type;
    if (type == DISPLAY_TYPE_GAUGE || type == DISPLAY_TYPE_GAUGE_NUMBER) {
        if (gauge_zone_tripped(screen, 0) || gauge_zone_tripped(screen, 1)) return true;
    }
    if (type == DISPLAY_TYPE_GAUGE_NUMBER) {
        return path_tripped(screen, screen_configs[screen].gauge_num_center_path, 1, 1, 2);
    }
    if (type == DISPLAY_TYPE_NUMBER) {
        return path_tripped(screen, screen_configs[screen].number_path, 0, 1, 2);
    }
    if (type == DISPLAY_TYPE_DUAL) {
        return path_tripped(screen, screen_configs[screen].dual_top_path, 0, 1, 2)
            || path_tripped(screen, screen_configs[screen].dual_bottom_path, 1, 1, 2);
    }
    if (type == DISPLAY_TYPE_QUAD) {
        return path_tripped(screen, screen_configs[screen].quad_tl_path, 0, 1, 2)
            || path_tripped(screen, screen_configs[screen].quad_tr_path, 0, 3, 4)
            || path_tripped(screen, screen_configs[screen].quad_bl_path, 1, 1, 2)
            || path_tripped(screen, screen_configs[screen].quad_br_path, 1, 3, 4);
    }
    return false;
}

void alarm_monitor_poll(int visible_screen_1based) {
    unsigned long now = millis();
    if (!cooldown_ready(now)) return;

    if (buzzer_mode != 0) {
        int from = 0;
        int to = NUM_SCREENS;
        if (buzzer_mode == 2) {
            int visible = visible_screen_1based - 1;
            if (visible < 0) visible = 0;
            if (visible >= NUM_SCREENS) visible = NUM_SCREENS - 1;
            from = visible;
            to = visible + 1;
        }
        for (int screen = from; screen < to; screen++) {
            if (screen_tripped(screen)) {
                alarm_fire(screen, "threshold");
                return;
            }
        }
    }

    for (int screen = 0; screen < NUM_SCREENS; screen++) {
        if (anchor_watch_alarm_due(screen)) {
            alarm_fire(screen, "anchor");
            return;
        }
    }
}
