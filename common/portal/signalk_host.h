#pragma once

// Per-firmware Signal K behavior that is already different between boards.
// The client is one file. These fields select the behavior each firmware has today.
struct SignalkHost {
    int subscribe_period_ms;
    bool fetch_metadata_on_connect;
    bool capture_ws_meta;
    bool parse_nav_objects;
    bool gauge_slot_units;
};

const SignalkHost &signalk_host();
