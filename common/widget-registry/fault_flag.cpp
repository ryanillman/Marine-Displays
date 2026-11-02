#include "fault_flag.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// Square ui.h declares these without const. Rounds do not declare them,
// and the font objects are const.
#if defined(BOARD_ID_ROUND21) || defined(BOARD_ID_ROUND28)
LV_FONT_DECLARE(inter_16)
LV_FONT_DECLARE(inter_24)
#endif

enum { kFlagSlots = 32 };
// Stroke of the red cross, measured across the line.
enum { kCrossStroke = 5 };

struct FlagSlot {
    char key[24];
    lv_obj_t *parent;
    lv_coord_t x, y, w, h;
    char source_buf[24];
    char detail_buf[96];
    uint16_t source_len;
    uint16_t detail_len;
    bool shown;
    lv_obj_t *box;
    lv_obj_t *title;
    lv_obj_t *source;
    lv_obj_t *detail;
};

static FlagSlot g_flags[kFlagSlots];

static FlagSlot *find_slot(const char *key, bool create) {
    FlagSlot *free_slot = NULL;
    for (int i = 0; i < kFlagSlots; i++) {
        if (g_flags[i].key[0] != '\0' && strcmp(g_flags[i].key, key) == 0) return &g_flags[i];
        if (!free_slot && g_flags[i].key[0] == '\0') free_slot = &g_flags[i];
    }
    if (!create) return NULL;
    if (!free_slot) return NULL;
    strncpy(free_slot->key, key, sizeof(free_slot->key) - 1);
    free_slot->key[sizeof(free_slot->key) - 1] = '\0';
    return free_slot;
}

// The cross is drawn by the box, one short span per row, clipped to the area
// being redrawn. An lv_line diagonal walks its whole bounding box through
// the software mask; on the 7" one 440x210 diagonal cost ~40 ms a frame.
static void draw_cross(lv_event_t *e) {
    lv_obj_t *box = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    if (!ctx || !ctx->clip_area) return;
    lv_area_t a;
    lv_obj_get_coords(box, &a);
    int32_t w = lv_area_get_width(&a);
    int32_t h = lv_area_get_height(&a);
    int32_t x0 = 8, y0 = 8;
    int32_t x1 = (w > 16) ? w - 8 : w;
    int32_t y1 = (h > 16) ? h - 8 : h;
    int32_t dx = x1 - x0, dy = y1 - y0;
    if (dx <= 0 || dy <= 0) return;
    // Horizontal span that gives kCrossStroke measured across the diagonal.
    int32_t len = (int32_t)sqrtf((float)(dx * dx + dy * dy));
    int32_t span = (kCrossStroke * len + dy - 1) / dy;
    const lv_area_t *clip = ctx->clip_area;
    int32_t ya = LV_MAX(y0, clip->y1 - a.y1);
    int32_t yb = LV_MIN(y1, clip->y2 - a.y1);
    if (ya > yb) return;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_make(220, 40, 40);
    dsc.bg_opa = LV_OPA_COVER;
    for (int32_t y = ya; y <= yb; y++) {
        int32_t off = dx * (y - y0) / dy;
        lv_area_t r;
        r.y1 = r.y2 = (lv_coord_t)(a.y1 + y);
        r.x1 = (lv_coord_t)(a.x1 + x0 + off - span / 2);
        r.x2 = (lv_coord_t)(r.x1 + span - 1);
        lv_draw_rect(ctx, &dsc, &r);
        r.x1 = (lv_coord_t)(a.x1 + x1 - off - span / 2);
        r.x2 = (lv_coord_t)(r.x1 + span - 1);
        lv_draw_rect(ctx, &dsc, &r);
    }
}

// A screen slide is already redrawing both faces. Painting the cross in the
// middle of that holds the UI thread long enough that the next swipe is lost.
static bool screen_animating(void) {
    lv_disp_t *disp = lv_disp_get_default();
    if (!disp) return false;
    return lv_disp_get_scr_prev(disp) != NULL;
}

// True when the stored text is the same string, including one longer than the
// buffer. The stored length is the full length, so a long path does not miss
// the match and get rewritten on every poll.
static bool text_same(const char *stored, uint16_t stored_len, const char *incoming, size_t cap) {
    if (!incoming) incoming = "";
    size_t n = strlen(incoming);
    if (stored_len != n) return false;
    size_t keep = n < cap - 1 ? n : cap - 1;
    return memcmp(stored, incoming, keep) == 0;
}

static void store_text(char *dst, uint16_t *len, const char *incoming, size_t cap) {
    if (!incoming) incoming = "";
    size_t n = strlen(incoming);
    size_t keep = n < cap - 1 ? n : cap - 1;
    memcpy(dst, incoming, keep);
    dst[keep] = '\0';
    *len = (uint16_t)n;
}

static void ensure_box(FlagSlot *slot, lv_obj_t *parent, lv_coord_t w, lv_coord_t h) {
    if (slot->box) return;
    slot->box = lv_obj_create(parent);
    lv_obj_remove_style_all(slot->box);
    lv_obj_set_style_bg_color(slot->box, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(slot->box, LV_OPA_COVER, 0);
    lv_obj_clear_flag(slot->box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(slot->box, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_FLOATING);

    lv_obj_add_event_cb(slot->box, draw_cross, LV_EVENT_DRAW_MAIN_END, NULL);

    slot->title = lv_label_create(slot->box);
    lv_label_set_text(slot->title, "NO DATA");
    lv_obj_set_style_text_color(slot->title, lv_color_make(255, 210, 0), 0);
    lv_obj_set_style_text_font(slot->title, (h >= 120) ? &inter_24 : &inter_16, 0);
    lv_obj_set_style_text_align(slot->title, LV_TEXT_ALIGN_CENTER, 0);

    slot->source = lv_label_create(slot->box);
    lv_obj_set_style_text_color(slot->source, lv_color_make(255, 210, 0), 0);
    lv_obj_set_style_text_font(slot->source, &inter_16, 0);
    lv_obj_set_width(slot->source, w > 16 ? w - 16 : w);
    lv_label_set_long_mode(slot->source, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(slot->source, LV_TEXT_ALIGN_CENTER, 0);

    slot->detail = lv_label_create(slot->box);
    lv_obj_set_style_text_color(slot->detail, lv_color_make(255, 210, 0), 0);
    lv_obj_set_style_text_font(slot->detail, &inter_16, 0);
    lv_obj_set_width(slot->detail, w > 16 ? w - 16 : w);
    lv_label_set_long_mode(slot->detail, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(slot->detail, LV_TEXT_ALIGN_CENTER, 0);
}

void fault_flag_show(lv_obj_t *parent, const char *key,
                     lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                     const char *source, const char *path) {
    if (!parent || !key || key[0] == '\0') return;
    FlagSlot *slot = find_slot(key, true);
    if (!slot) return;

    const char *source_text = (source && source[0]) ? source : "";
    const char *path_text = (path && path[0]) ? path : "";
    // Same box already on screen. Touching it again invalidates the whole
    // cross, and on the 7" that stalls the scan until the picture tears.
    if (slot->box && slot->shown && slot->parent == parent &&
        slot->x == x && slot->y == y && slot->w == w && slot->h == h &&
        text_same(slot->source_buf, slot->source_len, source_text, sizeof(slot->source_buf)) &&
        text_same(slot->detail_buf, slot->detail_len, path_text, sizeof(slot->detail_buf))) {
        return;
    }
    if (screen_animating()) return;

    if (slot->box && slot->parent != parent) {
        lv_obj_del(slot->box);
        slot->box = NULL;
        slot->title = NULL;
        slot->source = NULL;
        slot->detail = NULL;
        slot->shown = false;
    }

    ensure_box(slot, parent, w, h);
    if (!slot->box) return;

    slot->parent = parent;
    slot->x = x;
    slot->y = y;
    slot->w = w;
    slot->h = h;
    store_text(slot->source_buf, &slot->source_len, source_text, sizeof(slot->source_buf));
    store_text(slot->detail_buf, &slot->detail_len, path_text, sizeof(slot->detail_buf));

    lv_obj_set_pos(slot->box, x, y);
    lv_obj_set_size(slot->box, w, h);
    lv_obj_clear_flag(slot->box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(slot->box);


    lv_obj_set_width(slot->title, w > 8 ? w - 8 : w);
    lv_obj_align(slot->title, LV_ALIGN_CENTER, 0, -16);
    lv_label_set_text(slot->source, slot->source_buf);
    lv_obj_set_width(slot->source, w > 16 ? w - 16 : w);
    lv_obj_align(slot->source, LV_ALIGN_CENTER, 0, 12);
    lv_label_set_text(slot->detail, slot->detail_buf);
    lv_obj_set_width(slot->detail, w > 16 ? w - 16 : w);
    lv_obj_align(slot->detail, LV_ALIGN_CENTER, 0, 32);
    slot->shown = true;
}

void fault_flag_hide(const char *key) {
    if (!key) return;
    FlagSlot *slot = find_slot(key, false);
    if (!slot || !slot->box || !slot->shown) return;
    lv_obj_add_flag(slot->box, LV_OBJ_FLAG_HIDDEN);
    slot->shown = false;
}

void fault_flag_clear_screen(int screen_idx) {
    char prefix[8];
    snprintf(prefix, sizeof(prefix), "%d-", screen_idx);
    size_t n = strlen(prefix);
    for (int i = 0; i < kFlagSlots; i++) {
        if (strncmp(g_flags[i].key, prefix, n) != 0) continue;
        if (g_flags[i].box) lv_obj_del(g_flags[i].box);
        memset(&g_flags[i], 0, sizeof(g_flags[i]));
    }
}
