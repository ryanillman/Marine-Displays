#pragma once

#include <stddef.h>
#include <stdint.h>

// One measured field passed into a widget.
// valid stays true until a source can say the field has no sample.
// age_ms stays 0 until a source records when the value was read.
// value is the number the glass already uses, after unit conversion when
// the widget converts. NaN still means what that widget does with NaN today
// (drawn as 0, held as the last angle, or skipped).
struct Sample {
    float value;
    uint32_t age_ms;
    bool valid;
};

// Unit and description are the strings looked up with the sample.
// They are not part of Sample. Pointers live only for the update call.
struct FieldView {
    Sample sample;
    const char *unit;
    const char *description;
};

enum { WIDGET_MAX_FIELDS = 8 };

typedef void (*WidgetPathFn)(void *ctx, const char *path);

// screen_idx is 0-based. screen_num on update is 1-based.
// update does not read the sensor value again; the caller filled fields once.
struct WidgetOps {
    void (*create)(int screen_idx);
    void (*destroy)(int screen_idx);
    void (*update)(int screen_num, const FieldView *fields, int field_count);
    void (*collect_paths)(int screen_idx, void *ctx, WidgetPathFn add);
};
