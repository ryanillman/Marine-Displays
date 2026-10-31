#pragma once

#include <Arduino.h>

class Preferences;

// Per-firmware portal behavior. The page builder is one file.
// Each firmware fills this in before that file is compiled.
struct PortalHost {
    const char *ap_ssid;
    const char *default_signalk_ip;
    bool pause_ws_on_config;
    bool has_ota;
    bool gauge_types_only;
    bool square_buzzer_arm;
};

const PortalHost &portal_host();

void portal_load_device_keys(Preferences &preferences);
void portal_save_device_keys(Preferences &preferences);
void portal_append_brightness_html(String &html);
void portal_apply_brightness_post();
void portal_silence_join_buzzer();
void portal_after_gauge_save(int save_only);
