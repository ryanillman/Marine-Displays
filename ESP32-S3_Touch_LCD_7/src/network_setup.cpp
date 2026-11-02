// PlatformIO only scans this directory for library includes. The shared
// source below lives outside src/, so the framework headers it needs are named here.
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include <Update.h>
#include "../../common/portal/lcd7_portal_host.cpp"
#include "lcd7_sd.h"
// Skip the real SD_MMC header. This socket is not the SDMMC peripheral.
#define _SDMMC_H_
#define SD_MMC lcd7_sd
#include "../../common/portal/network_setup.cpp"
#undef SD_MMC
