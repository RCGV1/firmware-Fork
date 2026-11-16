#pragma once
#include <SPIFFS.h>

// Keep every legacy begin()/format() call inside the dedicated UI partition.
// Arduino's default null partition label would otherwise select the firmware FS.
class WadaStorageFS : public fs::SPIFFSFS
{
  public:
    bool begin(bool formatOnFail = false, const char * = nullptr, uint8_t maxOpenFiles = 10, const char * = nullptr)
    {
        return fs::SPIFFSFS::begin(formatOnFail, "/wada", maxOpenFiles, "wada");
    }
    bool format()
    {
        begin(false);
        return fs::SPIFFSFS::format();
    }
};
extern WadaStorageFS wadaStorage;
#define SPIFFS wadaStorage
