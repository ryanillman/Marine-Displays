#pragma once

#include "lvgl.h"

// Cover one field with a red cross, a yellow NO DATA label, and the source
// that failed. key identifies the field on that screen ("0-top"). The box is
// a child of parent. A later hide leaves the object and clears it from view.
void fault_flag_show(lv_obj_t *parent, const char *key,
                     lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                     const char *source, const char *path);
void fault_flag_hide(const char *key);
void fault_flag_clear_screen(int screen_idx);
