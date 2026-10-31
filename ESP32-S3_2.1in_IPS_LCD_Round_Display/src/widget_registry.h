#pragma once

#include "widget_ops.h"
#include <lvgl.h>

const WidgetOps *widget_ops_for(int display_type);
void widget_apply_type(int screen_idx);
void widget_collect_paths(int screen_idx, void *ctx, WidgetPathFn add);

// Defined in main.cpp. The gauge widget animates through it.
void animate_generic_needle(lv_obj_t *needle, int16_t &last_angle, int16_t new_angle, bool is_lower);
