#pragma once

/* Anchor header. Including it makes PlatformIO build the shared Inter
 * fonts in this library. The lv_font_t declarations stay in each board
 * tree (ui.h on the square boards, gauge_number_display.cpp on the
 * rounds, LV_FONT_DECLARE on the RLCD) so this move does not change
 * linkage.
 */
