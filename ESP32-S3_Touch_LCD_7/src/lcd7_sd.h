#pragma once

// The shared portal talks to SD_MMC. This board's socket is SPI, and the
// chip-select line is an expander pin held low, so those calls land here.

#include <SD.h>

struct Lcd7Sd {
    bool begin(const char *mountpoint, bool mode1bit);
    void end();
    sdcard_type_t cardType();
    bool exists(const char *path);
    bool exists(const String &path);
    bool mkdir(const char *path);
    bool mkdir(const String &path);
    bool remove(const char *path);
    bool remove(const String &path);
    bool rename(const char *pathFrom, const char *pathTo);
    bool rename(const String &pathFrom, const String &pathTo);
    File open(const char *path, const char *mode = FILE_READ);
    File open(const String &path, const char *mode = FILE_READ);
};

extern Lcd7Sd lcd7_sd;

bool lcd7_sd_mount();
void lcd7_sd_unmount();
