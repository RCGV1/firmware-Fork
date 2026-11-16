#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>

// Private in-process sidecar. Nothing is added to protobufs or sent over RF.
namespace wada
{
struct ChannelIntent {
    uint32_t id = 0;
    uint8_t slot = 0;
    uint8_t identity[32] = {};
};
inline std::array<ChannelIntent, 32> channelIntents{};
inline std::mutex channelIntentMutex;
inline bool rememberChannelIntent(uint32_t id, uint8_t slot, const uint8_t identity[32])
{
    if (!id)
        return false;
    std::lock_guard<std::mutex> lock(channelIntentMutex);
    for (auto &i : channelIntents)
        if (!i.id) {
            i.id = id;
            i.slot = slot;
            memcpy(i.identity, identity, 32);
            return true;
        }
    return false;
}
inline bool consumeChannelIntent(uint32_t id, uint8_t slot, const uint8_t *actualIdentity)
{
    std::lock_guard<std::mutex> lock(channelIntentMutex);
    for (auto &i : channelIntents)
        if (i.id == id) {
            const bool match = actualIdentity && i.slot == slot && memcmp(i.identity, actualIdentity, 32) == 0;
            i = {};
            return match;
        }
    return true;
}
inline void forgetChannelIntent(uint32_t id)
{
    consumeChannelIntent(id, 0, nullptr);
}
} // namespace wada
