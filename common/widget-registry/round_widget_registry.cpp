// Gauge and gauge-plus-number for the round boards.
// Included by each round firmware rather than built as a library: this file
// includes that board's UI headers, which a library translation unit does not see.
// Both round boards compile this one text. It does not branch on which round it is.

#include "widget_registry.h"
#include "fault_flag.cpp"
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

static Sample invalid_sample() {
    Sample sample;
    sample.value = NAN;
    sample.age_ms = 0;
    sample.valid = false;
    return sample;
}

static void assign_field(FieldView *field, Sample sample, const char *path) {
    field_view_clear(field);
    field->sample = sample;
    field->source = "Signal K";
    field->path = path ? path : "";
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

static void present_fault(int screen_idx, const char *key, int x, int y, int w, int h, const FieldView &field) {
    char id[24];
    snprintf(id, sizeof(id), "%d-%s", screen_idx, key);
    if (field.sample.valid) {
        fault_flag_hide(id);
        return;
    }
    const char *source = (field.source && field.source[0]) ? field.source : "Signal K";
    fault_flag_show(get_screen_obj(screen_idx), id, x, y, w, h, source, field.path ? field.path : "");
}

static void set_needle_hidden(lv_obj_t *needle, bool hidden) {
    if (!needle) return;
    if (hidden) lv_obj_add_flag(needle, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(needle, LV_OBJ_FLAG_HIDDEN);
}

static void add_config_path(void *ctx, WidgetPathFn add, const char *path) {
    if (path && path[0] != '\0') add(ctx, path);
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
    if (field_count < 2 || screen_num < 1 || screen_num > 5) return;
    int screen_idx = screen_num - 1;
    bool show_bottom = screen_configs[screen_idx].show_bottom != 0;

    lv_obj_t *top_needle = NULL;
    lv_obj_t *bottom_needle = NULL;
    gauge_needles(screen_num, &top_needle, &bottom_needle);

    if (!fields[0].sample.valid) set_needle_hidden(top_needle, true);
    present_fault(screen_idx, "top", 20, 8, 440, 210, fields[0]);
    if (fields[0].sample.valid) {
        set_needle_hidden(top_needle, false);
        int16_t top_angle = value_to_angle_for_param(fields[0].sample.value, screen_idx, 0);
        animate_generic_needle(top_needle, last_top_angle[screen_num], top_angle, false);
    }

    if (!show_bottom) {
        set_needle_hidden(bottom_needle, true);
        char id[24];
        snprintf(id, sizeof(id), "%d-bot", screen_idx);
        fault_flag_hide(id);
    } else {
        if (!fields[1].sample.valid) set_needle_hidden(bottom_needle, true);
        present_fault(screen_idx, "bot", 20, 262, 440, 210, fields[1]);
        if (fields[1].sample.valid) {
            set_needle_hidden(bottom_needle, false);
            int16_t bottom_angle = value_to_angle_for_param(fields[1].sample.value, screen_idx, 1);
            animate_generic_needle(bottom_needle, last_bottom_angle[screen_num], bottom_angle, true);
        }
    }

    lv_obj_t *top_icon = NULL;
    lv_obj_t *bottom_icon = NULL;
    gauge_icons(screen_num, &top_icon, &bottom_icon);
    if (top_icon) _ui_apply_icon_style(top_icon, screen_idx, 0);
    if (bottom_icon) _ui_apply_icon_style(bottom_icon, screen_idx, 1);
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
    lv_obj_t *top = NULL;
    lv_obj_t *bottom = NULL;
    gauge_needles(screen_idx + 1, &top, &bottom);
    set_needle_hidden(top, true);
    set_needle_hidden(bottom, true);
}

static void gauge_destroy(int screen_idx) {
    (void)screen_idx;
}

static void gauge_number_update(int screen_num, const FieldView *fields, int field_count) {
    int s = screen_num - 1;
    if (s < 0 || s >= NUM_SCREENS) return;
    if (screen_configs[s].display_type != DISPLAY_TYPE_GAUGE_NUMBER) return;
    if (field_count < 1) return;
    present_fault(s, "ctr", 120, 180, 240, 140, fields[0]);
    if (!fields[0].sample.valid) return;
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
    fault_flag_clear_screen(screen_idx);
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

    const char *path = screen_configs[s].gauge_num_center_path;
    Sample sample = invalid_sample();
    String unit;
    String description;
    if (path[0] != '\0') {
        String key = String(path);
        sample = get_sensor_sample_by_path(key);
        unit = get_sensor_unit_by_path(key);
        description = get_sensor_description_by_path(key);
        if (sample.valid) sample.value = convert_unit(sample.value, unit, key, unit);
    }
    FieldView field;
    assign_field(&field, sample, path);
    field.unit = unit.c_str();
    field.description = description.c_str();
    kGaugeNumberOps.update(screen_num, &field, 1);
}

static void gauge_run(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    int screen_idx = screen_num - 1;
    Sample top = get_sensor_sample(screen_idx * 2);
    Sample bottom = get_sensor_sample(screen_idx * 2 + 1);
    String top_path = get_signalk_path_by_index(screen_idx * 2);
    String bottom_path = get_signalk_path_by_index(screen_idx * 2 + 1);
    FieldView fields[2];
    assign_field(&fields[0], top, top_path.c_str());
    assign_field(&fields[1], bottom, bottom_path.c_str());
    kGaugeOps.update(screen_num, fields, 2);
}

extern "C" void update_needles_for_screen(int screen_num) {
    if (screen_num < 1 || screen_num > 5) return;
    if (test_mode) return;
    gauge_number_run(screen_num);
    gauge_run(screen_num);
}
