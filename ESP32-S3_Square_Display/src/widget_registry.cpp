// Screen widgets for the square firmware.
// update receives samples filled once. Every sample is valid, and the value is
// the number this widget already draws, so the glass does not change.

#include "widget_registry.h"
#include "ui.h"
#include "ui_helpers.h"
#include "ui_Settings.h"
#include "screen_config_c_api.h"
#include "signalk_config.h"
#include "network_setup.h"
#include "gauge_config.h"
#include "number_display.h"
#include "dual_number_display.h"
#include "quad_number_display.h"
#include "gauge_number_display.h"
#include "graph_display.h"
#include "position_display.h"
#include "compass_display.h"
#include "ais_display.h"
#include "anchor_display.h"
#include "unit_convert.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG_UIHOT = "ui_hotupdate";

#define ALARM_LOG_INTERVAL_MS 5000

extern bool test_mode;
extern int16_t last_top_angle[6];
extern int16_t last_bottom_angle[6];
extern unsigned long last_buzzer_time;
extern unsigned long last_alarm_log_time;

extern "C" void mark_compass_display_created(int screen_num);
extern "C" void mark_compass_display_destroyed(int screen_num);
extern "C" void mark_position_display_created(int screen_num);
extern "C" void mark_position_display_destroyed(int screen_num);

static bool number_displays_created[5] = {false, false, false, false, false};
static float last_display_values[5] = {NAN, NAN, NAN, NAN, NAN};
static String last_display_units[5] = {"", "", "", "", ""};
static String last_display_descriptions[5] = {"", "", "", "", ""};

static Sample make_sample(float value) {
    Sample sample;
    sample.value = value;
    sample.age_ms = 0;
    sample.valid = true;
    return sample;
}

struct HeldField {
    FieldView view;
    String unit;
    String description;
    void bind(float value) {
        view.sample = make_sample(value);
        view.unit = unit.c_str();
        view.description = description.c_str();
    }
};

// Same path read the dual, quad, gauge-number, and graph updates used.
// Empty path: value 0, unit "No Path". NaN: unit "N/A", value left NaN, no convert.
static void read_path(const String &path, HeldField *out) {
    if (path.length() == 0) {
        out->unit = "No Path";
        out->description = "";
        out->bind(0.0f);
        return;
    }
    float value = get_sensor_value_by_path(path);
    out->unit = get_sensor_unit_by_path(path);
    out->description = get_sensor_description_by_path(path);
    if (isnan(value)) {
        out->unit = "N/A";
        out->description = "";
        out->bind(value);
        return;
    }
    value = convert_unit(value, out->unit, path, out->unit);
    out->bind(value);
}

static void add_path(void *ctx, WidgetPathFn add, const char *path) {
    if (path && path[0] != '\0') add(ctx, path);
}

static lv_obj_t *get_screen_obj(int s) {
    switch (s) {
        case 0: return ui_Screen1;
        case 1: return ui_Screen2;
        case 2: return ui_Screen3;
        case 3: return ui_Screen4;
        case 4: return ui_Screen5;
        default: return NULL;
    }
}

static lv_obj_t *get_top_icon_obj_for_screen(int s) {
    switch (s) {
        case 0: return ui_TopIcon1;
        case 1: return ui_TopIcon2;
        case 2: return ui_TopIcon3;
        case 3: return ui_TopIcon4;
        case 4: return ui_TopIcon5;
        default: return NULL;
    }
}

static lv_obj_t *get_bottom_icon_obj_for_screen(int s) {
    switch (s) {
        case 0: return ui_BottomIcon1;
        case 1: return ui_BottomIcon2;
        case 2: return ui_BottomIcon3;
        case 3: return ui_BottomIcon4;
        case 4: return ui_BottomIcon5;
        default: return NULL;
    }
}

static lv_obj_t *get_upper_needle_obj_for_screen(int s) {
    switch (s) {
        case 0: return ui_Needle;
        case 1: return ui_Needle2;
        case 2: return ui_Needle3;
        case 3: return ui_Needle4;
        case 4: return ui_Needle5;
        default: return NULL;
    }
}

static lv_obj_t *get_lower_needle_obj_for_screen(int s) {
    switch (s) {
        case 0: return ui_Lower_Needle;
        case 1: return ui_Lower_Needle2;
        case 2: return ui_Lower_Needle3;
        case 3: return ui_Lower_Needle4;
        case 4: return ui_Lower_Needle5;
        default: return NULL;
    }
}

static void hide_needles_and_icons(int s) {
    lv_obj_t *upper_needle = get_upper_needle_obj_for_screen(s);
    lv_obj_t *lower_needle = get_lower_needle_obj_for_screen(s);
    if (upper_needle) lv_obj_add_flag(upper_needle, LV_OBJ_FLAG_HIDDEN);
    if (lower_needle) lv_obj_add_flag(lower_needle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *top_icon = get_top_icon_obj_for_screen(s);
    lv_obj_t *bot_icon = get_bottom_icon_obj_for_screen(s);
    if (top_icon) lv_obj_add_flag(top_icon, LV_OBJ_FLAG_HIDDEN);
    if (bot_icon) lv_obj_add_flag(bot_icon, LV_OBJ_FLAG_HIDDEN);
}

// --- number -----------------------------------------------------------------

extern "C" void reset_number_display_tracking(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    int screen_idx = screen_num - 1;
    number_displays_created[screen_idx] = true;
    last_display_values[screen_idx] = NAN;
    last_display_units[screen_idx] = "";
    last_display_descriptions[screen_idx] = "";
}

static void draw_number(int screen_idx, float display_value, const char *unit, const char *description) {
    if (!isnan(display_value)) {
        number_display_update(screen_idx, display_value, unit, description);
    } else {
        number_display_update(screen_idx, 0.0f, unit, description);
    }
}

extern "C" void force_update_number_display(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    int screen_idx = screen_num - 1;

    String number_path = String(screen_configs[screen_idx].number_path);
    if (number_path.length() == 0) {
        number_display_update(screen_idx, 0.0f, "No Path", "");
        return;
    }

    float display_value = get_sensor_value_by_path(number_path);
    String unit_str = get_sensor_unit_by_path(number_path);
    String description = get_sensor_description_by_path(number_path);
    display_value = convert_unit(display_value, unit_str, number_path, unit_str);
    Sample sample = make_sample(display_value);
    draw_number(screen_idx, sample.value, unit_str.c_str(), description.c_str());

    last_display_values[screen_idx] = display_value;
    last_display_units[screen_idx] = unit_str;
    last_display_descriptions[screen_idx] = description;
}

static void fill_number(int screen_idx, const String &number_path, HeldField *out) {
    int sensor_idx = -1;
    for (int i = 0; i < NUM_SCREENS * 2; i++) {
        if (get_signalk_path_by_index(i) == number_path) {
            sensor_idx = i;
            break;
        }
    }
    float display_value = 0.0f;
    if (sensor_idx >= 0) {
        display_value = get_sensor_value(sensor_idx);
        out->unit = get_sensor_unit(sensor_idx);
        out->description = get_sensor_description(sensor_idx);
        display_value = convert_unit(display_value, out->unit, number_path, out->unit);
    } else {
        display_value = get_sensor_value_by_path(number_path);
        out->unit = get_sensor_unit_by_path(number_path);
        out->description = get_sensor_description_by_path(number_path);
        display_value = convert_unit(display_value, out->unit, number_path, out->unit);
    }
    out->bind(display_value);
}

static void number_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5) return;
    int screen_idx = screen_num - 1;
    String number_path = String(screen_configs[screen_idx].number_path);
    if (number_path.length() == 0) {
        if (!number_displays_created[screen_idx]) {
            number_display_create(screen_idx);
            number_displays_created[screen_idx] = true;
        }
        number_display_update(screen_idx, 0.0f, "No Path", "");
        return;
    }
    if (field_count < 1) return;

    float display_value = fields[0].sample.value;
    const char *unit_str = fields[0].unit ? fields[0].unit : "";
    const char *description = fields[0].description ? fields[0].description : "";

    if (!number_displays_created[screen_idx]) {
        number_display_create(screen_idx);
        number_displays_created[screen_idx] = true;
        last_display_values[screen_idx] = NAN;
        last_display_units[screen_idx] = "";
        last_display_descriptions[screen_idx] = "";
    }

    bool value_changed = (isnan(display_value) != isnan(last_display_values[screen_idx])) ||
                         (!isnan(display_value) && fabs(display_value - last_display_values[screen_idx]) > 0.001f);
    bool unit_changed = (last_display_units[screen_idx] != String(unit_str));
    bool desc_changed = (last_display_descriptions[screen_idx] != String(description));

    if (value_changed || unit_changed || desc_changed) {
        draw_number(screen_idx, display_value, unit_str, description);
        last_display_values[screen_idx] = display_value;
        last_display_units[screen_idx] = unit_str;
        last_display_descriptions[screen_idx] = description;
        last_display_values[screen_idx] = display_value;
        last_display_units[screen_idx] = unit_str;
    }

    if (!isnan(display_value) && buzzer_mode != 0) {
        unsigned long ALERT_COOLDOWN_MS = (buzzer_cooldown_sec == 0) ? 0UL : (unsigned long)buzzer_cooldown_sec * 1000UL;
        unsigned long now_buz = millis();
        bool cooldown_ok = (now_buz - last_buzzer_time > ALERT_COOLDOWN_MS);
        bool low_armed  = (screen_configs[screen_idx].buzzer[0][1] != 0);
        bool high_armed = (screen_configs[screen_idx].buzzer[0][2] != 0);
        float low_thresh  = screen_configs[screen_idx].min[0][1];
        float high_thresh = screen_configs[screen_idx].max[0][2];
        bool low_fired  = low_armed  && (display_value < low_thresh);
        bool high_fired = high_armed && (display_value > high_thresh);
        if ((low_fired || high_fired) && (first_run_buzzer || cooldown_ok)) {
            if (now_buz - last_alarm_log_time > ALARM_LOG_INTERVAL_MS) {
                Serial.printf("[ALARM] screen=%d number=%.2f low=%s(%.2f) high=%s(%.2f)\n",
                    screen_idx, display_value,
                    low_fired  ? "TRIP" : "ok", low_thresh,
                    high_fired ? "TRIP" : "ok", high_thresh);
                last_alarm_log_time = now_buz;
            }
            trigger_buzzer_alert();
            last_buzzer_time = now_buz;
            first_run_buzzer = false;
        }
    }
}

static void number_collect(int screen_idx, void *ctx, WidgetPathFn add) {
    add_path(ctx, add, screen_configs[screen_idx].number_path);
}

static void number_create(int s);
static void number_destroy(int s) { number_display_destroy(s); }

static void number_run(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    int screen_idx = screen_num - 1;
    String number_path = String(screen_configs[screen_idx].number_path);
    HeldField held;
    if (number_path.length() > 0) fill_number(screen_idx, number_path, &held);
    else held.bind(0.0f);
    number_update(screen_num, &held.view, 1);
}

// --- dual --------------------------------------------------------------------

static void dual_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5 || field_count < 2) return;
    int screen_idx = screen_num - 1;
    float top_value = fields[0].sample.value;
    float bottom_value = fields[1].sample.value;

    dual_number_display_update_top(screen_idx,
        isnan(top_value) ? 0.0f : top_value,
        fields[0].unit, fields[0].description);
    dual_number_display_update_bottom(screen_idx,
        isnan(bottom_value) ? 0.0f : bottom_value,
        fields[1].unit, fields[1].description);

    if (buzzer_mode != 0) {
        unsigned long ALERT_COOLDOWN_MS = (buzzer_cooldown_sec == 0) ? 0UL : (unsigned long)buzzer_cooldown_sec * 1000UL;
        unsigned long now_buz = millis();
        bool cooldown_ok = (now_buz - last_buzzer_time > ALERT_COOLDOWN_MS);
        float dual_vals[2] = { top_value, bottom_value };
        for (int ch = 0; ch < 2; ch++) {
            if (isnan(dual_vals[ch])) continue;
            bool la = (screen_configs[screen_idx].buzzer[ch][1] != 0);
            bool ha = (screen_configs[screen_idx].buzzer[ch][2] != 0);
            bool lf = la && (dual_vals[ch] < screen_configs[screen_idx].min[ch][1]);
            bool hf = ha && (dual_vals[ch] > screen_configs[screen_idx].max[ch][2]);
            if ((lf || hf) && (first_run_buzzer || cooldown_ok)) {
                if (now_buz - last_alarm_log_time > ALARM_LOG_INTERVAL_MS) {
                    Serial.printf("[ALARM DUAL] screen=%d ch=%d val=%.2f low=%s(%.2f) high=%s(%.2f)\n",
                        screen_idx, ch, dual_vals[ch],
                        lf ? "TRIP" : "ok", screen_configs[screen_idx].min[ch][1],
                        hf ? "TRIP" : "ok", screen_configs[screen_idx].max[ch][2]);
                    last_alarm_log_time = now_buz;
                }
                trigger_buzzer_alert();
                last_buzzer_time = now_buz;
                first_run_buzzer = false;
                break;
            }
        }
    }
}

static void dual_collect(int screen_idx, void *ctx, WidgetPathFn add) {
    add_path(ctx, add, screen_configs[screen_idx].dual_top_path);
    add_path(ctx, add, screen_configs[screen_idx].dual_bottom_path);
}

static void dual_destroy(int s) { dual_number_display_destroy(s); }
static void dual_create(int s);

static void dual_run(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    int screen_idx = screen_num - 1;
    HeldField top;
    HeldField bottom;
    read_path(String(screen_configs[screen_idx].dual_top_path), &top);
    read_path(String(screen_configs[screen_idx].dual_bottom_path), &bottom);
    FieldView fields[2] = { top.view, bottom.view };
    dual_update(screen_num, fields, 2);
}

// --- quad --------------------------------------------------------------------

static void quad_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5 || field_count < 4) return;
    int screen_idx = screen_num - 1;
    float tl_value = fields[0].sample.value;
    float tr_value = fields[1].sample.value;
    float bl_value = fields[2].sample.value;
    float br_value = fields[3].sample.value;

    quad_number_display_update_tl(screen_idx, isnan(tl_value) ? 0.0f : tl_value, fields[0].unit, fields[0].description);
    quad_number_display_update_tr(screen_idx, isnan(tr_value) ? 0.0f : tr_value, fields[1].unit, fields[1].description);
    quad_number_display_update_bl(screen_idx, isnan(bl_value) ? 0.0f : bl_value, fields[2].unit, fields[2].description);
    quad_number_display_update_br(screen_idx, isnan(br_value) ? 0.0f : br_value, fields[3].unit, fields[3].description);

    if (buzzer_mode != 0) {
        unsigned long ALERT_COOLDOWN_MS = (buzzer_cooldown_sec == 0) ? 0UL : (unsigned long)buzzer_cooldown_sec * 1000UL;
        unsigned long now_buz = millis();
        bool cooldown_ok = (now_buz - last_buzzer_time > ALERT_COOLDOWN_MS);
        bool quad_fired = false;
        auto check_quad = [&](float val, int g, int zl, int zh) {
            if (quad_fired || isnan(val)) return;
            bool la = (screen_configs[screen_idx].buzzer[g][zl] != 0);
            bool ha = (screen_configs[screen_idx].buzzer[g][zh] != 0);
            bool lf = la && (val < screen_configs[screen_idx].min[g][zl]);
            bool hf = ha && (val > screen_configs[screen_idx].max[g][zh]);
            if ((lf || hf) && (first_run_buzzer || cooldown_ok)) quad_fired = true;
        };
        check_quad(tl_value, 0, 1, 2);
        check_quad(tr_value, 0, 3, 4);
        check_quad(bl_value, 1, 1, 2);
        check_quad(br_value, 1, 3, 4);
        if (quad_fired) {
            if (now_buz - last_alarm_log_time > ALARM_LOG_INTERVAL_MS) {
                Serial.printf("[ALARM QUAD] screen=%d fired\n", screen_idx);
                last_alarm_log_time = now_buz;
            }
            trigger_buzzer_alert();
            last_buzzer_time = now_buz;
            first_run_buzzer = false;
        }
    }
}

static void quad_collect(int screen_idx, void *ctx, WidgetPathFn add) {
    add_path(ctx, add, screen_configs[screen_idx].quad_tl_path);
    add_path(ctx, add, screen_configs[screen_idx].quad_tr_path);
    add_path(ctx, add, screen_configs[screen_idx].quad_bl_path);
    add_path(ctx, add, screen_configs[screen_idx].quad_br_path);
}

static void quad_destroy(int s) { quad_number_display_destroy(s); }
static void quad_create(int s);

static void quad_run(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    int screen_idx = screen_num - 1;
    HeldField tl, tr, bl, br;
    read_path(String(screen_configs[screen_idx].quad_tl_path), &tl);
    read_path(String(screen_configs[screen_idx].quad_tr_path), &tr);
    read_path(String(screen_configs[screen_idx].quad_bl_path), &bl);
    read_path(String(screen_configs[screen_idx].quad_br_path), &br);
    FieldView fields[4] = { tl.view, tr.view, bl.view, br.view };
    quad_update(screen_num, fields, 4);
}

// --- gauge + number (center). Needles are the gauge widget, run after this. -

static void gauge_number_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5 || field_count < 1) return;
    int screen_idx = screen_num - 1;
    float center_value = fields[0].sample.value;
    gauge_number_display_update_center(screen_idx,
        isnan(center_value) ? 0.0f : center_value,
        fields[0].unit, fields[0].description);

    if (!isnan(center_value) && buzzer_mode != 0) {
        unsigned long ALERT_COOLDOWN_MS = (buzzer_cooldown_sec == 0) ? 0UL : (unsigned long)buzzer_cooldown_sec * 1000UL;
        unsigned long now_buz = millis();
        bool cooldown_ok = (now_buz - last_buzzer_time > ALERT_COOLDOWN_MS);
        bool la = (screen_configs[screen_idx].buzzer[1][1] != 0);
        bool ha = (screen_configs[screen_idx].buzzer[1][2] != 0);
        bool lf = la && (center_value < screen_configs[screen_idx].min[1][1]);
        bool hf = ha && (center_value > screen_configs[screen_idx].max[1][2]);
        if ((lf || hf) && (first_run_buzzer || cooldown_ok)) {
            if (now_buz - last_alarm_log_time > ALARM_LOG_INTERVAL_MS) {
                Serial.printf("[ALARM GNUM] screen=%d center=%.2f low=%s(%.2f) high=%s(%.2f)\n",
                    screen_idx, center_value,
                    lf ? "TRIP" : "ok", screen_configs[screen_idx].min[1][1],
                    hf ? "TRIP" : "ok", screen_configs[screen_idx].max[1][2]);
                last_alarm_log_time = now_buz;
            }
            trigger_buzzer_alert();
            last_buzzer_time = now_buz;
            first_run_buzzer = false;
        }
    }
}

static void gauge_number_collect(int screen_idx, void *ctx, WidgetPathFn add) {
    add_path(ctx, add, screen_configs[screen_idx].gauge_num_center_path);
}

static void gauge_number_destroy(int s) { gauge_number_display_destroy(s); }
static void gauge_number_create(int s);

static void gauge_number_run(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    int screen_idx = screen_num - 1;
    HeldField center;
    read_path(String(screen_configs[screen_idx].gauge_num_center_path), &center);
    gauge_number_update(screen_num, &center.view, 1);
}

// --- graph -------------------------------------------------------------------

static void graph_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5 || field_count < 2) return;
    int screen_idx = screen_num - 1;
    float graph_value = fields[0].sample.value;
    graph_display_update(screen_idx,
        isnan(graph_value) ? 0.0f : graph_value,
        fields[0].unit,
        fields[0].description,
        fields[1].sample.value,
        fields[1].unit,
        fields[1].description);
}

static void graph_collect(int screen_idx, void *ctx, WidgetPathFn add) {
    add_path(ctx, add, screen_configs[screen_idx].number_path);
    add_path(ctx, add, screen_configs[screen_idx].graph_path_2);
}

static void graph_destroy(int s) { graph_display_destroy(s); }
static void graph_create(int s);

void update_graph_display_for_screen(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    int screen_idx = screen_num - 1;
    HeldField series1;
    read_path(String(screen_configs[screen_idx].number_path), &series1);

    HeldField series2;
    String graph_path_2 = String(screen_configs[screen_idx].graph_path_2);
    if (graph_path_2.length() > 0) {
        read_path(graph_path_2, &series2);
    } else {
        series2.unit = "";
        series2.description = "";
        series2.bind(NAN);
    }
    FieldView fields[2] = { series1.view, series2.view };
    graph_update(screen_num, fields, 2);
}

// --- compass -----------------------------------------------------------------

static void compass_read_field(const String &path, HeldField *out) {
    if (path.length() == 0) {
        out->unit = "";
        out->description = "";
        out->bind(NAN);
        return;
    }
    float value = get_sensor_value_by_path(path);
    out->unit = get_sensor_unit_by_path(path);
    out->description = get_sensor_description_by_path(path);
    value = convert_unit(value, out->unit, path, out->unit);
    out->bind(value);
}

static void compass_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5 || field_count < 3) return;
    int screen_idx = screen_num - 1;
    String hdg_path = String(screen_configs[screen_idx].number_path);
    if (hdg_path.length() == 0) hdg_path = "navigation.headingMagnetic";
    if (!isnan(fields[0].sample.value)) {
        bool is_true = (hdg_path.indexOf("True") >= 0 || hdg_path.indexOf("true") >= 0);
        compass_display_update(screen_idx, fields[0].sample.value, is_true ? 1 : 0);
    }
    compass_display_update_bl(screen_idx, fields[1].sample.value, fields[1].unit, fields[1].description);
    compass_display_update_br(screen_idx, fields[2].sample.value, fields[2].unit, fields[2].description);
}

static void compass_collect(int screen_idx, void *ctx, WidgetPathFn add) {
    if (screen_configs[screen_idx].number_path[0] != '\0') {
        add(ctx, screen_configs[screen_idx].number_path);
    } else {
        add(ctx, "navigation.headingMagnetic");
    }
    add_path(ctx, add, screen_configs[screen_idx].quad_bl_path);
    add_path(ctx, add, screen_configs[screen_idx].quad_br_path);
}

static void compass_destroy(int s) {
    compass_display_destroy(s);
    mark_compass_display_destroyed(s + 1);
}
static void compass_create(int s);

static void compass_run(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    int screen_idx = screen_num - 1;
    String hdg_path = String(screen_configs[screen_idx].number_path);
    if (hdg_path.length() == 0) hdg_path = "navigation.headingMagnetic";
    float hdg_rad = get_sensor_value_by_path(hdg_path);
    HeldField heading;
    heading.unit = "";
    heading.description = "";
    heading.bind(isnan(hdg_rad) ? NAN : convert_angle_rad(hdg_rad));

    HeldField bl;
    HeldField br;
    compass_read_field(String(screen_configs[screen_idx].quad_bl_path), &bl);
    compass_read_field(String(screen_configs[screen_idx].quad_br_path), &br);
    FieldView fields[3] = { heading.view, bl.view, br.view };
    compass_update(screen_num, fields, 3);
}

// --- position ----------------------------------------------------------------

static void position_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5 || field_count < 2) return;
    position_display_update(screen_num - 1, fields[0].sample.value, fields[1].sample.value, g_nav_datetime);
}

static void position_collect(int screen_idx, void *ctx, WidgetPathFn add) {
    (void)screen_idx;
    add(ctx, "navigation.position");
    add(ctx, "navigation.datetime");
}

static void position_destroy(int s) {
    position_display_destroy(s);
    mark_position_display_destroyed(s + 1);
}
static void position_create(int s);

static void position_run(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    FieldView fields[2];
    fields[0].sample = make_sample((float)g_nav_latitude);
    fields[0].unit = "";
    fields[0].description = "";
    fields[1].sample = make_sample((float)g_nav_longitude);
    fields[1].unit = "";
    fields[1].description = "";
    position_update(screen_num, fields, 2);
}

// --- AIS ---------------------------------------------------------------------

static void ais_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5 || field_count < 4) return;
    ais_display_update(screen_num - 1,
        fields[0].sample.value, fields[1].sample.value,
        fields[2].sample.value, fields[3].sample.value);
}

static void ais_collect(int screen_idx, void *ctx, WidgetPathFn add) {
    (void)screen_idx;
    add(ctx, "navigation.position");
    add(ctx, "navigation.datetime");
    add(ctx, "navigation.courseOverGroundTrue");
    add(ctx, "navigation.speedOverGround");
}

static void ais_destroy(int s) { ais_display_destroy(s); }
static void ais_create(int s);

static void ais_run(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    // Fetch before the course and speed reads. The target request can refresh them.
    String sk_ip = get_signalk_server_ip();
    uint16_t sk_port = get_signalk_server_port();
    ais_fetch_targets(sk_ip.c_str(), sk_port);

    float own_cog_rad = get_sensor_value_by_path("navigation.courseOverGroundTrue");
    float own_sog_ms = get_sensor_value_by_path("navigation.speedOverGround");
    FieldView fields[4];
    fields[0].sample = make_sample((float)g_nav_latitude);
    fields[1].sample = make_sample((float)g_nav_longitude);
    fields[2].sample = make_sample(isnan(own_cog_rad) ? NAN : convert_angle_rad(own_cog_rad));
    fields[3].sample = make_sample(isnan(own_sog_ms) ? NAN : convert_speed(own_sog_ms));
    for (int i = 0; i < 4; i++) {
        fields[i].unit = "";
        fields[i].description = "";
    }
    // Targets were fetched above, before course and speed were read.
    ais_update(screen_num, fields, 4);
}

// --- anchor ------------------------------------------------------------------

static void anchor_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5 || field_count < 5) return;
    anchor_display_update(screen_num - 1,
        fields[0].sample.value, fields[1].sample.value,
        fields[2].sample.value, fields[3].sample.value, fields[4].sample.value);
}

static void anchor_collect(int screen_idx, void *ctx, WidgetPathFn add) {
    add(ctx, "navigation.position");
    add(ctx, "navigation.courseOverGroundTrue");
    add(ctx, "navigation.speedOverGround");
    add_path(ctx, add, screen_configs[screen_idx].number_path);
}

static void anchor_destroy(int s) { anchor_display_destroy(s); }
static void anchor_create(int s);

static void anchor_run(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    int screen_idx = screen_num - 1;
    float cog_rad = get_sensor_value_by_path("navigation.courseOverGroundTrue");
    float sog_ms = get_sensor_value_by_path("navigation.speedOverGround");
    float cog_deg = isnan(cog_rad) ? NAN : cog_rad * (180.0f / (float)M_PI);
    float depth_m = NAN;
    if (screen_configs[screen_idx].number_path[0] != '\0') {
        depth_m = get_sensor_value_by_path(String(screen_configs[screen_idx].number_path));
    }
    FieldView fields[5];
    fields[0].sample = make_sample((float)g_nav_latitude);
    fields[1].sample = make_sample((float)g_nav_longitude);
    fields[2].sample = make_sample(cog_deg);
    fields[3].sample = make_sample(sog_ms);
    fields[4].sample = make_sample(depth_m);
    for (int i = 0; i < 5; i++) {
        fields[i].unit = "";
        fields[i].description = "";
    }
    anchor_update(screen_num, fields, 5);
}

// --- gauge needles -----------------------------------------------------------

static bool gauge_read(int screen_num, float *top, float *bottom) {
    switch (screen_num) {
        case 1:
            *top = get_sensor_value(SCREEN1_RPM);
            *bottom = get_sensor_value(SCREEN1_COOLANT_TEMP);
            return true;
        case 2:
            *top = get_sensor_value(SCREEN2_RPM);
            *bottom = get_sensor_value(SCREEN2_FUEL);
            return true;
        case 3:
            *top = get_sensor_value(SCREEN3_COOLANT_TEMP);
            *bottom = get_sensor_value(SCREEN3_EXHAUST_TEMP);
            return true;
        case 4:
            *top = get_sensor_value(SCREEN4_FUEL);
            *bottom = get_sensor_value(SCREEN4_COOLANT_TEMP);
            return true;
        case 5:
            *top = get_sensor_value(SCREEN5_OIL_PRESSURE);
            *bottom = get_sensor_value(SCREEN5_COOLANT_TEMP);
            return true;
        default:
            return false;
    }
}

static void gauge_needles(int screen_num, lv_obj_t **top, lv_obj_t **bottom) {
    *top = NULL;
    *bottom = NULL;
    switch (screen_num) {
        case 1: *top = ui_Needle; *bottom = ui_Lower_Needle; break;
        case 2: *top = ui_Needle2; *bottom = ui_Lower_Needle2; break;
        case 3: *top = ui_Needle3; *bottom = ui_Lower_Needle3; break;
        case 4: *top = ui_Needle4; *bottom = ui_Lower_Needle4; break;
        case 5: *top = ui_Needle5; *bottom = ui_Lower_Needle5; break;
        default: break;
    }
}

static void gauge_icons(int screen_num, lv_obj_t **top, lv_obj_t **bottom) {
    *top = NULL;
    *bottom = NULL;
    switch (screen_num) {
        case 1: *top = ui_TopIcon1; *bottom = ui_BottomIcon1; break;
        case 2: *top = ui_TopIcon2; *bottom = ui_BottomIcon2; break;
        case 3: *top = ui_TopIcon3; *bottom = ui_BottomIcon3; break;
        case 4: *top = ui_TopIcon4; *bottom = ui_BottomIcon4; break;
        case 5: *top = ui_TopIcon5; *bottom = ui_BottomIcon5; break;
        default: break;
    }
}

static void gauge_update(int screen_num, const FieldView *fields, int field_count) {
    if (field_count < 2) return;
    static bool initialized[6] = {false, false, false, false, false, false};

    lv_obj_t *top_needle = NULL;
    lv_obj_t *bottom_needle = NULL;
    gauge_needles(screen_num, &top_needle, &bottom_needle);

    float top_value = fields[0].sample.value;
    float bottom_value = fields[1].sample.value;
    int16_t top_angle, bottom_angle;

    if (!initialized[screen_num]) {
        top_angle = 0;
        bottom_angle = 180;
        initialized[screen_num] = true;
    } else {
        if (!isnan(top_value)) {
            top_angle = value_to_angle_for_param(top_value, screen_num - 1, 0);
        } else {
            top_angle = last_top_angle[screen_num];
        }
        if (!isnan(bottom_value)) {
            bottom_angle = value_to_angle_for_param(bottom_value, screen_num - 1, 1);
        } else {
            bottom_angle = last_bottom_angle[screen_num];
        }
    }

    animate_generic_needle(top_needle, last_top_angle[screen_num], top_angle, false);
    animate_generic_needle(bottom_needle, last_bottom_angle[screen_num], bottom_angle, true);

    lv_obj_t *top_icon = NULL;
    lv_obj_t *bottom_icon = NULL;
    gauge_icons(screen_num, &top_icon, &bottom_icon);
    if (top_icon) _ui_apply_icon_style(top_icon, screen_num - 1, 0);
    if (bottom_icon) _ui_apply_icon_style(bottom_icon, screen_num - 1, 1);
}

static void gauge_collect(int screen_idx, void *ctx, WidgetPathFn add) {
    String top = get_signalk_path_by_index(screen_idx * 2);
    String bottom = get_signalk_path_by_index(screen_idx * 2 + 1);
    if (top.length() > 0) add(ctx, top.c_str());
    if (bottom.length() > 0) add(ctx, bottom.c_str());
}

static void gauge_destroy(int s) {
    (void)s;
}

static void gauge_create(int s);

static void gauge_run(int screen_num) {
    float top = 0.0f;
    float bottom = 0.0f;
    if (!gauge_read(screen_num, &top, &bottom)) return;
    FieldView fields[2];
    fields[0].sample = make_sample(top);
    fields[0].unit = "";
    fields[0].description = "";
    fields[1].sample = make_sample(bottom);
    fields[1].unit = "";
    fields[1].description = "";
    gauge_update(screen_num, fields, 2);
}

// --- create: each branch is the screen transition for that type -------------

static void number_create(int s) {
    Serial.printf("[APPLY_ONE] s=%d → NUMBER\n", s);
    dual_destroy(s);
    quad_destroy(s);
    gauge_number_destroy(s);
    graph_destroy(s);
    hide_needles_and_icons(s);
    number_display_create(s);
    reset_number_display_tracking(s + 1);
    force_update_number_display(s + 1);
}

static void dual_create(int s) {
    Serial.printf("[APPLY_ONE] s=%d → DUAL\n", s);
    number_destroy(s);
    quad_destroy(s);
    gauge_number_destroy(s);
    graph_destroy(s);
    hide_needles_and_icons(s);
    dual_number_display_create(s,
        screen_configs[s].dual_top_font_size, screen_configs[s].dual_top_font_color,
        screen_configs[s].dual_bottom_font_size, screen_configs[s].dual_bottom_font_color,
        (strcmp(screen_configs[s].background_path, "Custom Color") == 0) ? screen_configs[s].number_bg_color : "");
    Serial.printf("[APPLY_ONE] s=%d DUAL created, invalidating screen\n", s);
    lv_obj_invalidate(get_screen_obj(s));
}

static void quad_create(int s) {
    Serial.printf("[APPLY_ONE] s=%d → QUAD\n", s);
    number_destroy(s);
    dual_destroy(s);
    gauge_number_destroy(s);
    graph_destroy(s);
    hide_needles_and_icons(s);
    quad_number_display_create(s,
        screen_configs[s].quad_tl_font_size, screen_configs[s].quad_tl_font_color,
        screen_configs[s].quad_tr_font_size, screen_configs[s].quad_tr_font_color,
        screen_configs[s].quad_bl_font_size, screen_configs[s].quad_bl_font_color,
        screen_configs[s].quad_br_font_size, screen_configs[s].quad_br_font_color,
        (strcmp(screen_configs[s].background_path, "Custom Color") == 0) ? screen_configs[s].number_bg_color : "");
    Serial.printf("[APPLY_ONE] s=%d QUAD created, invalidating screen\n", s);
    lv_obj_invalidate(get_screen_obj(s));
}

static void gauge_number_create(int s) {
    ESP_LOGI(TAG_UIHOT, "[GAUGE_NUM] Handling screen=%d as GAUGE_NUMBER", s);
    number_destroy(s);
    dual_destroy(s);
    quad_destroy(s);
    graph_destroy(s);
    lv_obj_t *lower_needle = get_lower_needle_obj_for_screen(s);
    if (lower_needle) lv_obj_add_flag(lower_needle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *bot = get_bottom_icon_obj_for_screen(s);
    if (bot) lv_obj_add_flag(bot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *top_icon = get_top_icon_obj_for_screen(s);
    const char *p = screen_configs[s].icon_paths[0];
    ESP_LOGI(TAG_UIHOT, "[GAUGE_NUM] screen=%d icon_paths[0]=%p, value='%s', len=%d",
        s, p, (p ? p : "NULL"), (p ? strlen(p) : -1));
    if (top_icon) {
        if (p && p[0] != '\0') {
            lv_img_set_src(top_icon, p);
            lv_obj_set_style_img_opa(top_icon, LV_OPA_COVER, 0);
            lv_obj_set_size(top_icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_align(top_icon, LV_ALIGN_CENTER, 0, -70);
            lv_obj_clear_flag(top_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_set_style_img_opa(top_icon, LV_OPA_TRANSP, 0);
            lv_obj_add_flag(top_icon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(top_icon, -5000, -5000);
            lv_obj_set_size(top_icon, 0, 0);
        }
    }
    gauge_number_display_create(s,
        screen_configs[s].gauge_num_center_font_size,
        screen_configs[s].gauge_num_center_font_color);
    Serial.printf("[APPLY_ONE] s=%d GAUGE_NUMBER created, invalidating screen\n", s);
    lv_obj_invalidate(get_screen_obj(s));
    if (top_icon && (!p || p[0] == '\0')) {
        if (!lv_obj_has_flag(top_icon, LV_OBJ_FLAG_HIDDEN))
            lv_obj_add_flag(top_icon, LV_OBJ_FLAG_HIDDEN);
    }
}

static void graph_create(int s) {
    Serial.printf("[APPLY_ONE] s=%d → GRAPH\n", s);
    number_destroy(s);
    dual_destroy(s);
    quad_destroy(s);
    gauge_number_destroy(s);
    graph_destroy(s);
    hide_needles_and_icons(s);
    graph_display_create(s);
    Serial.printf("[APPLY_ONE] s=%d GRAPH created, invalidating screen\n", s);
    lv_obj_invalidate(get_screen_obj(s));
}

static void compass_create(int s) {
    Serial.printf("[APPLY_ONE] s=%d → COMPASS\n", s);
    number_destroy(s);
    dual_destroy(s);
    quad_destroy(s);
    gauge_number_destroy(s);
    graph_destroy(s);
    position_destroy(s);
    compass_display_create(s);
    mark_compass_display_created(s + 1);
}

static void position_create(int s) {
    Serial.printf("[APPLY_ONE] s=%d → POSITION\n", s);
    number_destroy(s);
    dual_destroy(s);
    quad_destroy(s);
    gauge_number_destroy(s);
    graph_destroy(s);
    compass_destroy(s);
    ais_destroy(s);
    anchor_destroy(s);
    position_display_create(s);
    mark_position_display_created(s + 1);
}

static void anchor_create(int s) {
    Serial.printf("[APPLY_ONE] s=%d → ANCHOR\n", s);
    number_destroy(s);
    dual_destroy(s);
    quad_destroy(s);
    gauge_number_destroy(s);
    graph_destroy(s);
    compass_destroy(s);
    position_destroy(s);
    ais_destroy(s);
    anchor_destroy(s);
    hide_needles_and_icons(s);
    anchor_display_create(s);
}

static void ais_create(int s) {
    Serial.printf("[APPLY_ONE] s=%d → AIS\n", s);
    number_destroy(s);
    dual_destroy(s);
    quad_destroy(s);
    gauge_number_destroy(s);
    graph_destroy(s);
    compass_destroy(s);
    position_destroy(s);
    ais_display_create(s);
}

static void gauge_create(int s) {
    Serial.printf("[APPLY_ONE] s=%d → GAUGE\n", s);
    number_destroy(s);
    dual_destroy(s);
    quad_destroy(s);
    gauge_number_destroy(s);
    graph_destroy(s);
    compass_destroy(s);
    position_destroy(s);
    ais_destroy(s);
    anchor_destroy(s);
    lv_obj_t *upper_needle = get_upper_needle_obj_for_screen(s);
    if (upper_needle) lv_obj_clear_flag(upper_needle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *lower_needle = get_lower_needle_obj_for_screen(s);
    if (lower_needle) {
        if (screen_configs[s].show_bottom)
            lv_obj_clear_flag(lower_needle, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(lower_needle, LV_OBJ_FLAG_HIDDEN);
    }
}

static const WidgetOps kNumberOps = { number_create, number_destroy, number_update, number_collect };
static const WidgetOps kDualOps = { dual_create, dual_destroy, dual_update, dual_collect };
static const WidgetOps kQuadOps = { quad_create, quad_destroy, quad_update, quad_collect };
static const WidgetOps kGaugeNumberOps = { gauge_number_create, gauge_number_destroy, gauge_number_update, gauge_number_collect };
static const WidgetOps kGraphOps = { graph_create, graph_destroy, graph_update, graph_collect };
static const WidgetOps kCompassOps = { compass_create, compass_destroy, compass_update, compass_collect };
static const WidgetOps kPositionOps = { position_create, position_destroy, position_update, position_collect };
static const WidgetOps kAisOps = { ais_create, ais_destroy, ais_update, ais_collect };
static const WidgetOps kAnchorOps = { anchor_create, anchor_destroy, anchor_update, anchor_collect };
static const WidgetOps kGaugeOps = { gauge_create, gauge_destroy, gauge_update, gauge_collect };

const WidgetOps *widget_ops_for(int display_type) {
    switch (display_type) {
        case DISPLAY_TYPE_NUMBER: return &kNumberOps;
        case DISPLAY_TYPE_DUAL: return &kDualOps;
        case DISPLAY_TYPE_QUAD: return &kQuadOps;
        case DISPLAY_TYPE_GAUGE_NUMBER: return &kGaugeNumberOps;
        case DISPLAY_TYPE_GRAPH: return &kGraphOps;
        case DISPLAY_TYPE_COMPASS: return &kCompassOps;
        case DISPLAY_TYPE_POSITION: return &kPositionOps;
        case DISPLAY_TYPE_AIS: return &kAisOps;
        case DISPLAY_TYPE_ANCHOR: return &kAnchorOps;
        case DISPLAY_TYPE_GAUGE:
        default: return &kGaugeOps;
    }
}

void widget_apply_type(int screen_idx) {
    const WidgetOps *ops = widget_ops_for(screen_configs[screen_idx].display_type);
    if (ops && ops->create) ops->create(screen_idx);
}

struct PathDedup {
    char buf[WIDGET_MAX_FIELDS][128];
    int n;
    void *ctx;
    WidgetPathFn add;
};

static void dedup_add(void *ctx, const char *path) {
    PathDedup *dedup = static_cast<PathDedup *>(ctx);
    if (!path || path[0] == '\0' || !dedup->add) return;
    for (int i = 0; i < dedup->n; i++) {
        if (strcmp(dedup->buf[i], path) == 0) return;
    }
    if (dedup->n >= WIDGET_MAX_FIELDS) return;
    strncpy(dedup->buf[dedup->n], path, 127);
    dedup->buf[dedup->n][127] = '\0';
    dedup->add(dedup->ctx, dedup->buf[dedup->n]);
    dedup->n++;
}

void widget_collect_paths(int screen_idx, void *ctx, WidgetPathFn add) {
    if (screen_idx < 0 || screen_idx >= NUM_SCREENS || !add) return;
    PathDedup dedup;
    dedup.n = 0;
    dedup.ctx = ctx;
    dedup.add = add;
    int type = screen_configs[screen_idx].display_type;
    const WidgetOps *ops = widget_ops_for(type);
    if (ops && ops->collect_paths) ops->collect_paths(screen_idx, &dedup, dedup_add);
    // Gauge+number still moves the needles after the center value.
    if (type == DISPLAY_TYPE_GAUGE_NUMBER) gauge_collect(screen_idx, &dedup, dedup_add);
}

extern "C" void update_needles_for_screen(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    int type = screen_configs[screen_num - 1].display_type;

    if (type == DISPLAY_TYPE_NUMBER) {
        number_run(screen_num);
        return;
    } else if (type == DISPLAY_TYPE_DUAL) {
        dual_run(screen_num);
        return;
    } else if (type == DISPLAY_TYPE_QUAD) {
        quad_run(screen_num);
        return;
    } else if (type == DISPLAY_TYPE_GAUGE_NUMBER) {
        gauge_number_run(screen_num);
    } else if (type == DISPLAY_TYPE_GRAPH) {
        update_graph_display_for_screen(screen_num);
        return;
    } else if (type == DISPLAY_TYPE_POSITION) {
        position_run(screen_num);
        return;
    } else if (type == DISPLAY_TYPE_COMPASS) {
        compass_run(screen_num);
        return;
    } else if (type == DISPLAY_TYPE_AIS) {
        ais_run(screen_num);
        return;
    } else if (type == DISPLAY_TYPE_ANCHOR) {
        anchor_run(screen_num);
        return;
    }

    if (test_mode) return;
    gauge_run(screen_num);
}
