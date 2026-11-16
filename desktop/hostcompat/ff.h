#pragma once
#include <cstdint>
struct MKFS_PARM {
    uint8_t fmt;
};
#define FM_FAT32 2
#define FR_OK 0
static inline int f_mkfs(const char *, const MKFS_PARM *, void *, unsigned)
{
    return 1;
}
static inline int f_chmod(const char *, uint8_t, uint8_t)
{
    return 1;
}
