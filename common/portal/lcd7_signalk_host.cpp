#include "signalk_host.h"

// 7" client: same metadata and slot units as the 4". The subscribe period is
// 250 ms (4 Hz). The panel scan already owns the PSRAM bus, and a faster
// stream stalls swipes. A current Signal K server keeps the latest sample
// and sends it at most that often. The 4" stays on-change (period 0).
const SignalkHost &signalk_host() {
    static const SignalkHost host = { 250, true, false, true, true };
    return host;
}
