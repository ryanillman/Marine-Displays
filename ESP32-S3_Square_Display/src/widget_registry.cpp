// Screen widgets for the square firmware.
// A field draws only while its sample is valid. Otherwise that field shows
// the fault flag and a needle stays hidden.

#include "widget_registry.h"
#include "ui.h"
#include "../../common/widget-registry/fault_flag.cpp"
#include "ui_helpers.h"
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

extern bool test_mode;
extern int16_t last_top_angle[6];
extern int16_t last_bottom_angle[6];

extern "C" void mark_compass_display_created(int screen_num);
extern "C" void mark_compass_display_destroyed(int screen_num);
extern "C" void mark_position_display_created(int screen_num);
extern "C" void mark_position_display_destroyed(int screen_num);

static bool number_displays_created[5] = {false, false, false, false, false};
static float last_display_values[5] = {NAN, NAN, NAN, NAN, NAN};
static String last_display_units[5] = {"", "", "", "", ""};
static String last_display_descriptions[5] = {"", "", "", "", ""};

struct HeldField {
    FieldView view;
    String unit;
    String description;
    String path;
    void bind(const Sample &sample) {
        view.sample = sample;
        view.unit = unit.c_str();
        view.description = description.c_str();
        view.source = "Signal K";
        view.path = path.c_str();
    }
};

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

// Empty path and a failed sample stay invalid. A live finite sample is converted
// into the unit on the glass.
static void read_path(const String &path, HeldField *out) {
    out->path = path;
    out->description = "";
    if (path.length() == 0) {
        out->unit = "No Path";
        out->bind(invalid_sample());
        return;
    }
    Sample sample = get_sensor_sample_by_path(path);
    out->unit = get_sensor_unit_by_path(path);
    out->description = get_sensor_description_by_path(path);
    if (sample.valid) {
        sample.value = convert_unit(sample.value, out->unit, path, out->unit);
    }
    out->bind(sample);
}

static void add_path(void *ctx, WidgetPathFn add, const char *path) {
    if (path && path[0] != '\0') add(ctx, path);
}

static lv_obj_t *get_screen_obj(int s);

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

static void number_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5 || field_count < 1) return;
    int screen_idx = screen_num - 1;
    if (!number_displays_created[screen_idx]) {
        number_display_create(screen_idx);
        number_displays_created[screen_idx] = true;
        last_display_values[screen_idx] = NAN;
        last_display_units[screen_idx] = "";
        last_display_descriptions[screen_idx] = "";
    }

    present_fault(screen_idx, "num", 40, 80, 400, 320, fields[0]);
    if (!fields[0].sample.valid) return;

    float display_value = fields[0].sample.value;
    const char *unit_str = fields[0].unit ? fields[0].unit : "";
    const char *description = fields[0].description ? fields[0].description : "";
    bool value_changed = (isnan(display_value) != isnan(last_display_values[screen_idx])) ||
                         (!isnan(display_value) && fabs(display_value - last_display_values[screen_idx]) > 0.001f);
    bool unit_changed = (last_display_units[screen_idx] != String(unit_str));
    bool desc_changed = (last_display_descriptions[screen_idx] != String(description));
    if (value_changed || unit_changed || desc_changed) {
        number_display_update(screen_idx, display_value, unit_str, description);
        last_display_values[screen_idx] = display_value;
        last_display_units[screen_idx] = unit_str;
        last_display_descriptions[screen_idx] = description;
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
    HeldField held;
    read_path(String(screen_configs[screen_idx].number_path), &held);
    number_update(screen_num, &held.view, 1);
}

extern "C" void force_update_number_display(int screen_num) {
    number_run(screen_num);
}

// --- dual --------------------------------------------------------------------

static void dual_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5 || field_count < 2) return;
    int screen_idx = screen_num - 1;
    present_fault(screen_idx, "d0", 0, 0, 480, 240, fields[0]);
    present_fault(screen_idx, "d1", 0, 240, 480, 240, fields[1]);
    if (fields[0].sample.valid) {
        dual_number_display_update_top(screen_idx, fields[0].sample.value, fields[0].unit, fields[0].description);
    }
    if (fields[1].sample.valid) {
        dual_number_display_update_bottom(screen_idx, fields[1].sample.value, fields[1].unit, fields[1].description);
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
    present_fault(screen_idx, "q0", 0, 0, 240, 240, fields[0]);
    present_fault(screen_idx, "q1", 240, 0, 240, 240, fields[1]);
    present_fault(screen_idx, "q2", 0, 240, 240, 240, fields[2]);
    present_fault(screen_idx, "q3", 240, 240, 240, 240, fields[3]);
    if (fields[0].sample.valid) quad_number_display_update_tl(screen_idx, fields[0].sample.value, fields[0].unit, fields[0].description);
    if (fields[1].sample.valid) quad_number_display_update_tr(screen_idx, fields[1].sample.value, fields[1].unit, fields[1].description);
    if (fields[2].sample.valid) quad_number_display_update_bl(screen_idx, fields[2].sample.value, fields[2].unit, fields[2].description);
    if (fields[3].sample.valid) quad_number_display_update_br(screen_idx, fields[3].sample.value, fields[3].unit, fields[3].description);
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
    present_fault(screen_idx, "ctr", 120, 180, 240, 140, fields[0]);
    if (!fields[0].sample.valid) return;
    gauge_number_display_update_center(screen_idx, fields[0].sample.value, fields[0].unit, fields[0].description);
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
    present_fault(screen_idx, "g1", 30, 70, 420, 300, fields[0]);
    if (fields[1].path && fields[1].path[0] != '\0') {
        present_fault(screen_idx, "g2", 250, 360, 210, 100, fields[1]);
    } else {
        char id[24];
        snprintf(id, sizeof(id), "%d-g2", screen_idx);
        fault_flag_hide(id);
    }
    float second = fields[1].sample.valid ? fields[1].sample.value : NAN;
    float first = fields[0].sample.valid ? fields[0].sample.value : NAN;
    graph_display_update(screen_idx, first, fields[0].unit, fields[0].description,
        second, fields[1].unit, fields[1].description);
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
        series2.path = "";
        series2.bind(invalid_sample());
    }
    FieldView fields[2] = { series1.view, series2.view };
    graph_update(screen_num, fields, 2);
}

// --- compass -----------------------------------------------------------------

static void compass_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5 || field_count < 3) return;
    int screen_idx = screen_num - 1;
    String hdg_path = String(screen_configs[screen_idx].number_path);
    if (hdg_path.length() == 0) hdg_path = "navigation.headingMagnetic";
    present_fault(screen_idx, "hdg", 40, 20, 400, 300, fields[0]);
    if (fields[0].sample.valid) {
        bool is_true = (hdg_path.indexOf("True") >= 0 || hdg_path.indexOf("true") >= 0);
        compass_display_update(screen_idx, fields[0].sample.value, is_true ? 1 : 0);
    }
    if (fields[1].path && fields[1].path[0] != '\0') {
        present_fault(screen_idx, "cbl", 0, 320, 230, 160, fields[1]);
        compass_display_update_bl(screen_idx, fields[1].sample.valid ? fields[1].sample.value : NAN,
            fields[1].unit, fields[1].description);
    }
    if (fields[2].path && fields[2].path[0] != '\0') {
        present_fault(screen_idx, "cbr", 250, 320, 230, 160, fields[2]);
        compass_display_update_br(screen_idx, fields[2].sample.valid ? fields[2].sample.value : NAN,
            fields[2].unit, fields[2].description);
    }
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
    Sample heading_sample = get_sensor_sample_by_path(hdg_path);
    HeldField heading;
    heading.path = hdg_path;
    heading.unit = "";
    heading.description = "";
    if (heading_sample.valid) heading_sample.value = convert_angle_rad(heading_sample.value);
    heading.bind(heading_sample);

    HeldField bl;
    HeldField br;
    read_path(String(screen_configs[screen_idx].quad_bl_path), &bl);
    read_path(String(screen_configs[screen_idx].quad_br_path), &br);
    FieldView fields[3] = { heading.view, bl.view, br.view };
    compass_update(screen_num, fields, 3);
}

// --- position ----------------------------------------------------------------

static void position_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5 || field_count < 2) return;
    int screen_idx = screen_num - 1;
    present_fault(screen_idx, "pos", 40, 120, 400, 240, fields[0]);
    if (!fields[0].sample.valid) return;
    position_display_update(screen_idx, fields[0].sample.value, fields[1].sample.value, g_nav_datetime);
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
    bool fix = nav_position_valid();
    Sample lat = invalid_sample();
    lat.value = (float)g_nav_latitude;
    lat.valid = fix;
    Sample lon = invalid_sample();
    lon.value = (float)g_nav_longitude;
    lon.valid = fix;
    FieldView fields[2];
    assign_field(&fields[0], lat, "navigation.position");
    assign_field(&fields[1], lon, "navigation.position");
    position_update(screen_num, fields, 2);
}

// --- AIS ---------------------------------------------------------------------

static void ais_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5 || field_count < 4) return;
    int screen_idx = screen_num - 1;
    present_fault(screen_idx, "ais", 80, 140, 320, 180, fields[0]);
    present_fault(screen_idx, "cog", 0, 400, 230, 70, fields[2]);
    present_fault(screen_idx, "sog", 250, 400, 230, 70, fields[3]);
    float lat = fields[0].sample.valid ? fields[0].sample.value : NAN;
    float lon = fields[1].sample.valid ? fields[1].sample.value : NAN;
    float cog = fields[2].sample.valid ? fields[2].sample.value : NAN;
    float sog = fields[3].sample.valid ? fields[3].sample.value : NAN;
    ais_display_update(screen_idx, lat, lon, cog, sog);
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

    bool fix = nav_position_valid();
    Sample lat = invalid_sample();
    lat.value = (float)g_nav_latitude;
    lat.valid = fix;
    Sample lon = invalid_sample();
    lon.value = (float)g_nav_longitude;
    lon.valid = fix;
    Sample cog = get_sensor_sample_by_path("navigation.courseOverGroundTrue");
    if (cog.valid) cog.value = convert_angle_rad(cog.value);
    Sample sog = get_sensor_sample_by_path("navigation.speedOverGround");
    if (sog.valid) sog.value = convert_speed(sog.value);
    FieldView fields[4];
    assign_field(&fields[0], lat, "navigation.position");
    assign_field(&fields[1], lon, "navigation.position");
    assign_field(&fields[2], cog, "navigation.courseOverGroundTrue");
    assign_field(&fields[3], sog, "navigation.speedOverGround");
    // Targets were fetched above, before course and speed were read.
    ais_update(screen_num, fields, 4);
}

// --- anchor ------------------------------------------------------------------

static void anchor_update(int screen_num, const FieldView *fields, int field_count) {
    if (screen_num < 1 || screen_num > 5 || field_count < 5) return;
    int screen_idx = screen_num - 1;
    present_fault(screen_idx, "anc", 70, 80, 340, 220, fields[0]);
    float lat = fields[0].sample.valid ? fields[0].sample.value : NAN;
    float lon = fields[1].sample.valid ? fields[1].sample.value : NAN;
    float cog = fields[2].sample.valid ? fields[2].sample.value : NAN;
    float sog = fields[3].sample.valid ? fields[3].sample.value : NAN;
    float depth = fields[4].sample.valid ? fields[4].sample.value : NAN;
    anchor_display_update(screen_idx, lat, lon, cog, sog, depth);
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
    bool fix = nav_position_valid();
    Sample lat = invalid_sample();
    lat.value = (float)g_nav_latitude;
    lat.valid = fix;
    Sample lon = invalid_sample();
    lon.value = (float)g_nav_longitude;
    lon.valid = fix;
    Sample cog = get_sensor_sample_by_path("navigation.courseOverGroundTrue");
    if (cog.valid) cog.value = cog.value * (180.0f / (float)M_PI);
    Sample sog = get_sensor_sample_by_path("navigation.speedOverGround");
    Sample depth = invalid_sample();
    if (screen_configs[screen_idx].number_path[0] != '\0') {
        depth = get_sensor_sample_by_path(String(screen_configs[screen_idx].number_path));
    }
    FieldView fields[5];
    assign_field(&fields[0], lat, "navigation.position");
    assign_field(&fields[1], lon, "navigation.position");
    assign_field(&fields[2], cog, "navigation.courseOverGroundTrue");
    assign_field(&fields[3], sog, "navigation.speedOverGround");
    assign_field(&fields[4], depth, screen_configs[screen_idx].number_path);
    anchor_update(screen_num, fields, 5);
}

// --- gauge needles -----------------------------------------------------------

// Showing an already visible object still invalidates it. Only touch the flag
// when it changes.
static void set_needle_hidden(lv_obj_t *needle, bool hidden) {
    if (!needle) return;
    if (lv_obj_has_flag(needle, LV_OBJ_FLAG_HIDDEN) == hidden) return;
    if (hidden) lv_obj_add_flag(needle, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(needle, LV_OBJ_FLAG_HIDDEN);
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

extern "C" float ui_get_runtime_value(int screen_idx, int gauge_idx);

// Last icon style per screen and gauge: 0 unknown, 1 no sample, 2+ zone.
// _ui_apply_icon_style invalidates the icon every call, so it runs only when
// this changes.
static uint8_t s_icon_key[5][2];

static uint8_t icon_style_key(int screen_idx, int gauge) {
    float v = ui_get_runtime_value(screen_idx, gauge);
    if (isnan(v)) return 1;
    int zone = 1;
    for (int z = 1; z <= 4; ++z) {
        float mn = screen_configs[screen_idx].min[gauge][z];
        float mx = screen_configs[screen_idx].max[gauge][z];
        if (mn == mx) continue;
        if (v >= mn && v <= mx) { zone = z; break; }
    }
    return (uint8_t)(1 + zone);
}

static void restyle_icon(lv_obj_t *icon, int screen_idx, int gauge) {
    if (!icon) return;
#if defined(BOARD_ID_LCD7)
    // Zoom 250 sends every icon draw through the software transform. On
    // the 7" that is ~15 ms per icon per frame. Native size is 2% larger.
    lv_img_set_zoom(icon, LV_IMG_ZOOM_NONE);
#endif
    uint8_t key = icon_style_key(screen_idx, gauge);
    if (s_icon_key[screen_idx][gauge] == key) return;
    s_icon_key[screen_idx][gauge] = key;
    _ui_apply_icon_style(icon, screen_idx, gauge);
}

static void gauge_update(int screen_num, const FieldView *fields, int field_count) {
    if (field_count < 2 || screen_num < 1 || screen_num > 5) return;
    int screen_idx = screen_num - 1;
    bool show_bottom = screen_configs[screen_idx].show_bottom != 0;

    lv_obj_t *top_needle = NULL;
    lv_obj_t *bottom_needle = NULL;
    gauge_needles(screen_num, &top_needle, &bottom_needle);

    // A hidden needle is not a reading. The cross is drawn after it is hidden.
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
    restyle_icon(top_icon, screen_idx, 0);
    restyle_icon(bottom_icon, screen_idx, 1);
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
    if (screen_num < 1 || screen_num > 5) return;
    int screen_idx = screen_num - 1;
    Sample top = get_sensor_sample(screen_idx * 2);
    Sample bottom = get_sensor_sample(screen_idx * 2 + 1);
    String top_path = get_signalk_path_by_index(screen_idx * 2);
    String bottom_path = get_signalk_path_by_index(screen_idx * 2 + 1);
    FieldView fields[2];
    assign_field(&fields[0], top, top_path.c_str());
    assign_field(&fields[1], bottom, bottom_path.c_str());
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
    s_icon_key[s][0] = 0;
    s_icon_key[s][1] = 0;
    // Stay hidden until gauge_update has a finite sample. A needle at 0 is a reading.
    lv_obj_t *upper_needle = get_upper_needle_obj_for_screen(s);
    if (upper_needle) lv_obj_add_flag(upper_needle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *lower_needle = get_lower_needle_obj_for_screen(s);
    if (lower_needle) lv_obj_add_flag(lower_needle, LV_OBJ_FLAG_HIDDEN);
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
