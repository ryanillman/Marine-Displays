// Gauge and gauge-plus-number for the round boards.
// Included by each round firmware rather than built as a library: this file
// includes that board's UI headers, which a library translation unit does not see.
// Both round boards compile this one text. It does not branch on which round it is.

#include "widget_registry.h"
#include "ui.h"
#include "ui_helpers.h"
#include "screen_config_c_api.h"
#include "signalk_config.h"
#include "network_setup.h"
#include "gauge_config.h"
#include "gauge_number_display.h"
#include "unit_convert.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

extern bool test_mode;
extern int16_t last_top_angle[6];
extern int16_t last_bottom_angle[6];

static Sample make_sample(float value) {
    Sample sample;
    sample.value = value;
    sample.age_ms = 0;
    sample.valid = true;
    return sample;
}

static void add_config_path(void *ctx, WidgetPathFn add, const char *path) {
    if (path && path[0] != '\0') add(ctx, path);
}

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

// Needles are SquareLine objects. Switching back to a gauge removes the center number.
static void gauge_create(int screen_idx) {
    gauge_number_display_destroy(screen_idx);
}

static void gauge_destroy(int screen_idx) {
    (void)screen_idx;
}

static void gauge_number_update(int screen_num, const FieldView *fields, int field_count) {
    int s = screen_num - 1;
    if (s < 0 || s >= NUM_SCREENS) return;
    if (screen_configs[s].display_type != DISPLAY_TYPE_GAUGE_NUMBER) return;
    if (field_count < 1) return;
    // An empty path or a NaN reading leaves the center text alone.
    if (isnan(fields[0].sample.value)) return;
    gauge_number_display_update_center(s, fields[0].sample.value, fields[0].unit, fields[0].description);
}

static void gauge_number_collect(int screen_idx, void *ctx, WidgetPathFn add) {
    add_config_path(ctx, add, screen_configs[screen_idx].gauge_num_center_path);
}

static void gauge_number_create(int screen_idx) {
    gauge_number_display_create(screen_idx,
        screen_configs[screen_idx].gauge_num_center_font_size,
        screen_configs[screen_idx].gauge_num_center_font_color);
}

static void gauge_number_destroy(int screen_idx) {
    gauge_number_display_destroy(screen_idx);
}

static const WidgetOps kGaugeOps = {
    gauge_create,
    gauge_destroy,
    gauge_update,
    gauge_collect,
};

static const WidgetOps kGaugeNumberOps = {
    gauge_number_create,
    gauge_number_destroy,
    gauge_number_update,
    gauge_number_collect,
};

const WidgetOps *widget_ops_for(int display_type) {
    if (display_type == DISPLAY_TYPE_GAUGE_NUMBER) return &kGaugeNumberOps;
    return &kGaugeOps;
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
    // Needles run for every round screen, then the center number when configured.
    gauge_collect(screen_idx, &dedup, dedup_add);
    if (screen_configs[screen_idx].display_type == DISPLAY_TYPE_GAUGE_NUMBER) {
        gauge_number_collect(screen_idx, &dedup, dedup_add);
    }
}

static void gauge_number_run(int screen_num) {
    int s = screen_num - 1;
    if (s < 0 || s >= NUM_SCREENS) return;
    if (screen_configs[s].display_type != DISPLAY_TYPE_GAUGE_NUMBER) return;

    String path = String(screen_configs[s].gauge_num_center_path);
    String unit;
    String description;
    float display_value = NAN;
    if (path.length() > 0) {
        float raw = get_sensor_value_by_path(path);
        if (!isnan(raw)) {
            unit = get_sensor_unit_by_path(path);
            description = get_sensor_description_by_path(path);
            display_value = convert_unit(raw, unit, path, unit);
        }
    }

    FieldView field;
    field.sample = make_sample(display_value);
    field.unit = unit.c_str();
    field.description = description.c_str();
    kGaugeNumberOps.update(screen_num, &field, 1);
}

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
    kGaugeOps.update(screen_num, fields, 2);
}

extern "C" void update_needles_for_screen(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    if (test_mode) return;
    gauge_number_run(screen_num);
    gauge_run(screen_num);
}
