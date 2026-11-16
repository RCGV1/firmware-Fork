#pragma once
#include <cstdint>
#include <string>
namespace fs
{
class SPIFFSFS
{
  public:
    std::string partition, mount;
    unsigned formats = 0;
    bool begin(bool, const char *path, uint8_t, const char *label)
    {
        mount = path;
        partition = label ? label : "DEFAULT";
        return true;
    }
    bool format()
    {
        ++formats;
        return true;
    }
};
} // namespace fs
