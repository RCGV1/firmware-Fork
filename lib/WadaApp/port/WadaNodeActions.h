#pragma once
#include "mesh/generated/meshtastic/config.pb.h"
#include <array>
#include <cstdint>
#include <mutex>

namespace wada
{
struct UnheardRemoval {
    uint32_t id = 0, node = 0, self = 0;
    meshtastic_Config_LoRaConfig lora{};
    char primaryName[32]{};
};
enum class NodeRemovalResult : uint8_t { Removed, Missing, Favorite, Heard, Mqtt, Changed, Managed, Self, Unavailable };
inline std::mutex nodeActionMutex;
inline std::array<UnheardRemoval, 16> nodeRemovalRequests{};
inline const char *nodeRemovalReason(NodeRemovalResult result)
{
    switch (result) {
    case NodeRemovalResult::Removed:
        return "Node removed";
    case NodeRemovalResult::Missing:
        return "Node no longer present";
    case NodeRemovalResult::Favorite:
        return "Favorite kept";
    case NodeRemovalResult::Heard:
        return "Node heard on current LoRa settings; kept";
    case NodeRemovalResult::Mqtt:
        return "MQTT node kept";
    case NodeRemovalResult::Changed:
        return "Radio settings changed; node kept";
    case NodeRemovalResult::Managed:
        return "Managed node; removal not permitted";
    case NodeRemovalResult::Self:
        return "Connected radio kept";
    case NodeRemovalResult::Unavailable:
        return "Removal could not be confirmed";
    }
    return "Removal could not be confirmed";
}
inline bool queueUnheardRemoval(const UnheardRemoval &request)
{
    if (!request.id || !request.node || request.node == UINT32_MAX || !request.self || request.self == UINT32_MAX)
        return false;
    std::lock_guard<std::mutex> lock(nodeActionMutex);
    for (auto &entry : nodeRemovalRequests)
        if (!entry.id) {
            entry = request;
            return true;
        }
    return false;
}
inline bool takeUnheardRemoval(UnheardRemoval &request)
{
    std::lock_guard<std::mutex> lock(nodeActionMutex);
    for (auto &entry : nodeRemovalRequests)
        if (entry.id) {
            request = entry;
            entry = {};
            return true;
        }
    return false;
}
inline bool cancelUnheardRemoval(uint32_t id)
{
    if (!id)
        return false;
    std::lock_guard<std::mutex> lock(nodeActionMutex);
    for (auto &entry : nodeRemovalRequests)
        if (entry.id == id) {
            entry = {};
            return true;
        }
    return false;
}
} // namespace wada
