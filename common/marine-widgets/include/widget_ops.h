#pragma once

#include <stddef.h>
#include <stdint.h>

// One measured field passed into a widget.
// valid is false until a finite sample has arrived, and whenever that
// source is down or the value was not a finite number. age_ms is the time
// since that finite sample. value is the number the glass draws when valid,
// after unit conversion when the widget converts.
struct Sample {
    float value;
    uint32_t age_ms;
    bool valid;
};

// Pointers live only for the update call.
// source names who produced the sample ("Signal K"). path is the subscription
// the failed field shows, so the dead reading can be traced.
struct FieldView {
    Sample sample;
    const char *unit;
    const char *description;
    const char *source;
    const char *path;
};

inline void field_view_clear(FieldView *field) {
    field->sample.value = 0.0f;
    field->sample.age_ms = 0;
    field->sample.valid = false;
    field->unit = "";
    field->description = "";
    field->source = "";
    field->path = "";
}

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
