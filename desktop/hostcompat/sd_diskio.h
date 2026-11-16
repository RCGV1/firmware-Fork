#pragma once
#include <cstdint>
class SPIClass;
static inline uint8_t sdcard_init(uint8_t, SPIClass *, uint32_t)
{
    return 0xff;
}
static inline void sdcard_uninit(uint8_t) {}
static inline bool sd_read_raw(uint8_t, uint8_t *, uint32_t)
{
    return false;
}
static inline bool sd_write_raw(uint8_t, const uint8_t *, uint32_t)
{
    return false;
}
