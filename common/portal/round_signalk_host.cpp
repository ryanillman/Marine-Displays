#include "signalk_host.h"

// Round client: 200 ms subscriptions, units from the socket meta, no HTTP
// metadata fetch, and no special case for navigation objects.
const SignalkHost &signalk_host() {
    static const SignalkHost host = { 200, false, true, false, false };
    return host;
}
