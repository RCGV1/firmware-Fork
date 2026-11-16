#pragma once
#include <cstdint>
struct ChannelDetails {
    struct {
        uint8_t secret[32]{}, hash[8]{};
    } channel;
    char name[32]{};
};
