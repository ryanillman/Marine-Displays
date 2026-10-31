// screen_config_c_api.h - C-friendly API for ScreenConfig
#ifndef SCREEN_CONFIG_C_API_H
#define SCREEN_CONFIG_C_API_H

#include "calibration_types.h" // For GaugeCalibrationPoint
#include <stdint.h>

// Avoid including C++-heavy headers here (UI C files include this header).
// If `NUM_SCREENS` is not defined by the build, default to 5.
#ifndef NUM_SCREENS
#define NUM_SCREENS 5
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Display type enumeration
typedef enum {
	DISPLAY_TYPE_GAUGE = 0,       // Traditional gauge with needles
	DISPLAY_TYPE_NUMBER = 1,      // Full screen number display
	DISPLAY_TYPE_DUAL = 2,        // Dual number display (top and bottom)
	DISPLAY_TYPE_QUAD = 3,        // Quad number display (4 quadrants)
	DISPLAY_TYPE_GAUGE_NUMBER = 4,// Gauge (top) + Number (center)
        DISPLAY_TYPE_GRAPH = 5,       // LVGL chart/graph display
        DISPLAY_TYPE_COMPASS = 6,     // Rotating compass display (heading)
        DISPLAY_TYPE_POSITION = 7,    // Lat/Lon + UTC time display
        DISPLAY_TYPE_AIS = 8,         // AIS radar display
        DISPLAY_TYPE_ANCHOR = 9       // Anchor alarm with map view
} DisplayType;

// Number display background type (uses bg_image field)
// bg_image = "" or "Default" -> transparent/default background
// bg_image = "/path/to/file.bin" -> bin file background
// bg_image = "Custom Color" -> use number_bg_color field

// Number display font size
typedef enum {
	NUMBER_FONT_SMALL = 0,     // 48pt (1x)
	NUMBER_FONT_MEDIUM = 1,    // 72pt (1.5x scale)
	NUMBER_FONT_LARGE = 2,     // 96pt (2x scale)
	NUMBER_FONT_XLARGE = 3,    // 120pt (2.5x scale)
	NUMBER_FONT_XXLARGE = 4    // 144pt (3x scale)
} NumberFontSize;

// Graph time range options
typedef enum {
	GRAPH_TIME_10S = 0,        // 10 seconds
	GRAPH_TIME_30S = 1,        // 30 seconds
	GRAPH_TIME_1M = 2,         // 1 minute
	GRAPH_TIME_5M = 3,         // 5 minutes
	GRAPH_TIME_10M = 4,        // 10 minutes
	GRAPH_TIME_30M = 5         // 30 minutes
} GraphTimeRange;

// Graph chart type
typedef enum {
	GRAPH_CHART_LINE = 0,      // Line chart
	GRAPH_CHART_BAR = 1,       // Bar/column chart
	GRAPH_CHART_SCATTER = 2    // Scatter plot
} GraphChartType;

// Previous packed layout saved by the 4" firmware, before schema_version.
typedef struct {
	GaugeCalibrationPoint cal[2][5];
	char icon_paths[2][128];
	uint8_t show_bottom;
	char background_path[128];
	uint8_t icon_pos[2];
	float min[2][5];
	float max[2][5];
	char color[2][5][8];
	int transparent[2][5];
	int buzzer[2][5];
	uint8_t display_type;
	char number_bg_color[8];
	uint8_t number_font_size;
	char number_font_color[8];
	char number_path[128];
	char dual_top_path[128];
	uint8_t dual_top_font_size;
	char dual_top_font_color[8];
	char dual_bottom_path[128];
	uint8_t dual_bottom_font_size;
	char dual_bottom_font_color[8];
	char quad_tl_path[128];
	uint8_t quad_tl_font_size;
	char quad_tl_font_color[8];
	char quad_tr_path[128];
	uint8_t quad_tr_font_size;
	char quad_tr_font_color[8];
	char quad_bl_path[128];
	uint8_t quad_bl_font_size;
	char quad_bl_font_color[8];
	char quad_br_path[128];
	uint8_t quad_br_font_size;
	char quad_br_font_color[8];
	char gauge_num_center_path[128];
	uint8_t gauge_num_center_font_size;
	char gauge_num_center_font_color[8];
	uint8_t graph_chart_type;
	uint8_t graph_time_range;
	char graph_path_2[128];
	char graph_color_2[8];
	char pos_latlon_color[8];
	char pos_time_color[8];
	char pos_divider_color[8];
} __attribute__((packed)) ScreenConfigSquareV0;

// Previous packed layout saved by the round firmwares.
// Gauge+number fields sit where the 4" layout inserts number/dual/quad fields,
// so a round blob is not a prefix of the 4" blob.
typedef struct {
	GaugeCalibrationPoint cal[2][5];
	char icon_paths[2][128];
	uint8_t show_bottom;
	char background_path[128];
	uint8_t icon_pos[2];
	float min[2][5];
	float max[2][5];
	char color[2][5][8];
	int transparent[2][5];
	int buzzer[2][5];
	uint8_t display_type;
	char gauge_num_center_path[128];
	uint8_t gauge_num_center_font_size;
	char gauge_num_center_font_color[8];
} __attribute__((packed)) ScreenConfigRoundV0;

#define SCREEN_CONFIG_SCHEMA 1

typedef struct {
	GaugeCalibrationPoint cal[2][5]; // 2 gauges, 5 points each
	char icon_paths[2][128];         // 2 icons (top/bottom)
	uint8_t show_bottom;             // 1 = show bottom gauge/icon, 0 = hide (single gauge mode)
	char background_path[128];      // optional per-screen background image path
	uint8_t icon_pos[2];            // icon position per icon: 0=top,1=right,2=bottom,3=left
	float min[2][5];                 // min for each zone
	float max[2][5];                 // max for each zone
	char color[2][5][8];             // color hex for each zone
	int transparent[2][5];           // transparency for each zone
	int buzzer[2][5];                // buzzer enabled for each zone
	uint8_t display_type;            // DisplayType: 0=Gauge, 1=Number, 2=Dual
	char number_bg_color[8];
	uint8_t number_font_size;
	char number_font_color[8];
	char number_path[128];
	char dual_top_path[128];
	uint8_t dual_top_font_size;
	char dual_top_font_color[8];
	char dual_bottom_path[128];
	uint8_t dual_bottom_font_size;
	char dual_bottom_font_color[8];
	char quad_tl_path[128];
	uint8_t quad_tl_font_size;
	char quad_tl_font_color[8];
	char quad_tr_path[128];
	uint8_t quad_tr_font_size;
	char quad_tr_font_color[8];
	char quad_bl_path[128];
	uint8_t quad_bl_font_size;
	char quad_bl_font_color[8];
	char quad_br_path[128];
	uint8_t quad_br_font_size;
	char quad_br_font_color[8];
	char gauge_num_center_path[128];
	uint8_t gauge_num_center_font_size;
	char gauge_num_center_font_color[8];
	uint8_t graph_chart_type;
	uint8_t graph_time_range;
	char graph_path_2[128];
	char graph_color_2[8];
	char pos_latlon_color[8];
	char pos_time_color[8];
	char pos_divider_color[8];
	// schema_version is SCREEN_CONFIG_SCHEMA for a blob written by this firmware.
	// threshold_unit[gauge][zone] is the display unit shown beside that threshold
	// when it was saved. Empty means a migrated blob did not record a unit.
	uint16_t schema_version;
	char threshold_unit[2][5][16];
} __attribute__((packed)) ScreenConfig;

extern ScreenConfig screen_configs[NUM_SCREENS];

#ifdef __cplusplus
}
#endif

#endif // SCREEN_CONFIG_C_API_H
