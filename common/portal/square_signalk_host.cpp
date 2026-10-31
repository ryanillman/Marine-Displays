#include "signalk_host.h"

// 4" client: on-change subscriptions, HTTP metadata, navigation objects as
// objects, and unit strings stored on the gauge slots.
const SignalkHost &signalk_host() {
    static const SignalkHost host = { 0, true, false, true, true };
    return host;
}
