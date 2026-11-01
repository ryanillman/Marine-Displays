#include "signalk_host.h"

// 7" client: same subscription behavior as the 4". On-change, HTTP metadata,
// navigation objects as objects, and unit strings stored on the gauge slots.
const SignalkHost &signalk_host() {
    static const SignalkHost host = { 0, true, false, true, true };
    return host;
}
