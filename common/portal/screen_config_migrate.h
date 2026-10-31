#pragma once

// Field-by-field load of a saved ScreenConfig blob.
// A 4" blob and a round blob are both accepted. The round layout is not a
// prefix of the 4" layout: its center-number fields sit where the 4" layout
// stores the number, dual, and quad fields.

#include "screen_config.h"

#include <stddef.h>
#include <string.h>

static inline void screen_config_copy_square_v0(ScreenConfig *dst, const ScreenConfigSquareV0 *src) {
    memcpy(dst->cal, src->cal, sizeof(dst->cal));
    memcpy(dst->icon_paths, src->icon_paths, sizeof(dst->icon_paths));
    dst->show_bottom = src->show_bottom;
    memcpy(dst->background_path, src->background_path, sizeof(dst->background_path));
    memcpy(dst->icon_pos, src->icon_pos, sizeof(dst->icon_pos));
    memcpy(dst->min, src->min, sizeof(dst->min));
    memcpy(dst->max, src->max, sizeof(dst->max));
    memcpy(dst->color, src->color, sizeof(dst->color));
    memcpy(dst->transparent, src->transparent, sizeof(dst->transparent));
    memcpy(dst->buzzer, src->buzzer, sizeof(dst->buzzer));
    dst->display_type = src->display_type;
    memcpy(dst->number_bg_color, src->number_bg_color, sizeof(dst->number_bg_color));
    dst->number_font_size = src->number_font_size;
    memcpy(dst->number_font_color, src->number_font_color, sizeof(dst->number_font_color));
    memcpy(dst->number_path, src->number_path, sizeof(dst->number_path));
    memcpy(dst->dual_top_path, src->dual_top_path, sizeof(dst->dual_top_path));
    dst->dual_top_font_size = src->dual_top_font_size;
    memcpy(dst->dual_top_font_color, src->dual_top_font_color, sizeof(dst->dual_top_font_color));
    memcpy(dst->dual_bottom_path, src->dual_bottom_path, sizeof(dst->dual_bottom_path));
    dst->dual_bottom_font_size = src->dual_bottom_font_size;
    memcpy(dst->dual_bottom_font_color, src->dual_bottom_font_color, sizeof(dst->dual_bottom_font_color));
    memcpy(dst->quad_tl_path, src->quad_tl_path, sizeof(dst->quad_tl_path));
    dst->quad_tl_font_size = src->quad_tl_font_size;
    memcpy(dst->quad_tl_font_color, src->quad_tl_font_color, sizeof(dst->quad_tl_font_color));
    memcpy(dst->quad_tr_path, src->quad_tr_path, sizeof(dst->quad_tr_path));
    dst->quad_tr_font_size = src->quad_tr_font_size;
    memcpy(dst->quad_tr_font_color, src->quad_tr_font_color, sizeof(dst->quad_tr_font_color));
    memcpy(dst->quad_bl_path, src->quad_bl_path, sizeof(dst->quad_bl_path));
    dst->quad_bl_font_size = src->quad_bl_font_size;
    memcpy(dst->quad_bl_font_color, src->quad_bl_font_color, sizeof(dst->quad_bl_font_color));
    memcpy(dst->quad_br_path, src->quad_br_path, sizeof(dst->quad_br_path));
    dst->quad_br_font_size = src->quad_br_font_size;
    memcpy(dst->quad_br_font_color, src->quad_br_font_color, sizeof(dst->quad_br_font_color));
    memcpy(dst->gauge_num_center_path, src->gauge_num_center_path, sizeof(dst->gauge_num_center_path));
    dst->gauge_num_center_font_size = src->gauge_num_center_font_size;
    memcpy(dst->gauge_num_center_font_color, src->gauge_num_center_font_color, sizeof(dst->gauge_num_center_font_color));
    dst->graph_chart_type = src->graph_chart_type;
    dst->graph_time_range = src->graph_time_range;
    memcpy(dst->graph_path_2, src->graph_path_2, sizeof(dst->graph_path_2));
    memcpy(dst->graph_color_2, src->graph_color_2, sizeof(dst->graph_color_2));
    memcpy(dst->pos_latlon_color, src->pos_latlon_color, sizeof(dst->pos_latlon_color));
    memcpy(dst->pos_time_color, src->pos_time_color, sizeof(dst->pos_time_color));
    memcpy(dst->pos_divider_color, src->pos_divider_color, sizeof(dst->pos_divider_color));
    dst->schema_version = SCREEN_CONFIG_SCHEMA;
    memset(dst->threshold_unit, 0, sizeof(dst->threshold_unit));
}

static inline void screen_config_copy_round_v0(ScreenConfig *dst, const ScreenConfigRoundV0 *src) {
    memcpy(dst->cal, src->cal, sizeof(dst->cal));
    memcpy(dst->icon_paths, src->icon_paths, sizeof(dst->icon_paths));
    dst->show_bottom = src->show_bottom;
    memcpy(dst->background_path, src->background_path, sizeof(dst->background_path));
    memcpy(dst->icon_pos, src->icon_pos, sizeof(dst->icon_pos));
    memcpy(dst->min, src->min, sizeof(dst->min));
    memcpy(dst->max, src->max, sizeof(dst->max));
    memcpy(dst->color, src->color, sizeof(dst->color));
    memcpy(dst->transparent, src->transparent, sizeof(dst->transparent));
    memcpy(dst->buzzer, src->buzzer, sizeof(dst->buzzer));
    dst->display_type = src->display_type;
    memcpy(dst->gauge_num_center_path, src->gauge_num_center_path, sizeof(dst->gauge_num_center_path));
    dst->gauge_num_center_font_size = src->gauge_num_center_font_size;
    memcpy(dst->gauge_num_center_font_color, src->gauge_num_center_font_color, sizeof(dst->gauge_num_center_font_color));
    dst->schema_version = SCREEN_CONFIG_SCHEMA;
    memset(dst->threshold_unit, 0, sizeof(dst->threshold_unit));
}

// dst keeps fields the blob does not carry (round blobs leave number/dual/quad
// fields alone). Returns false when the size matches none of the known layouts.
static inline bool screen_config_from_blob(const uint8_t *bytes, size_t len, ScreenConfig *dst) {
    if (bytes == NULL || dst == NULL || len == 0) return false;
    if (len == sizeof(ScreenConfig)) {
        memcpy(dst, bytes, sizeof(ScreenConfig));
        return true;
    }
    if (len == sizeof(ScreenConfigSquareV0)) {
        ScreenConfigSquareV0 src;
        memcpy(&src, bytes, sizeof(src));
        screen_config_copy_square_v0(dst, &src);
        return true;
    }
    if (len == sizeof(ScreenConfigRoundV0)) {
        ScreenConfigRoundV0 src;
        memcpy(&src, bytes, sizeof(src));
        screen_config_copy_round_v0(dst, &src);
        return true;
    }
    return false;
}

static_assert(sizeof(ScreenConfig) > sizeof(ScreenConfigSquareV0), "screen schema did not grow");
static_assert(sizeof(ScreenConfigSquareV0) != sizeof(ScreenConfigRoundV0), "round and 4-inch blobs must stay distinct");
static_assert(offsetof(ScreenConfig, schema_version) == sizeof(ScreenConfigSquareV0), "4-inch v0 must remain a prefix of ScreenConfig");
