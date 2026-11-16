#pragma once
#include <cstddef>
#include <cstdint>
struct ContactInfo {
    struct {
        uint8_t pub_key[32]{};
    } id;
    char name[32]{};
    uint8_t type = 0, flags = 0, out_path_len = 0;
    uint32_t lastmod = 0, last_advert_timestamp = 0;
    int32_t gps_lat = 0, gps_lon = 0;
    bool isFav() const { return flags & 1; }
};
