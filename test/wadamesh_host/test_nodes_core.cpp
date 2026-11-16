// Compiles PacketAPI's production node-fact/dequeue methods and guarded native
// removal body against a deterministic NodeDB fixture. Guards must inspect current
// firmware state, not an old UI snapshot; queued facts must refresh before handoff.
#include "WadaNodeActions.h"
#include "WadaNodeState.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <pb_encode.h>
#include <vector>
constexpr uint32_t NODENUM_BROADCAST = UINT32_MAX;
uint32_t nowMs = 0;
namespace Time
{
uint32_t getMillis()
{
    return nowMs;
}
} // namespace Time
struct Throttle {
    static bool hasElapsed(uint32_t start, uint32_t span) { return uint32_t(nowMs - start) >= span; }
};
size_t pb_encode_to_bytes(uint8_t *out, size_t cap, const pb_msgdesc_t *desc, const void *data)
{
    auto stream = pb_ostream_from_buffer(out, cap);
    return pb_encode(&stream, desc, data) ? stream.bytes_written : 0;
}
struct NativeNode {
    meshtastic_NodeInfo info{};
};
struct NativeDB {
    std::vector<NativeNode> nodes;
    unsigned removed = 0;
    uint32_t getNodeNum() const { return 0x11111111; }
    uint32_t committedLoraSlot() const { return 1; }
    NativeNode *getMeshNode(uint32_t number)
    {
        for (auto &node : nodes)
            if (node.info.num == number)
                return &node;
        return nullptr;
    }
    const NativeNode *readNextMeshNode(uint32_t &index) { return index < nodes.size() ? &nodes[index++] : nullptr; }
    void removeNodeByNum(uint32_t number)
    {
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const auto &node) { return node.info.num == number; }),
                    nodes.end());
        ++removed;
    }
} database;
NativeDB *nodeDB = &database;
namespace TypeConversions
{
meshtastic_NodeInfo ConvertToNodeInfoThin(const NativeNode *node)
{
    return node->info;
}
} // namespace TypeConversions
bool nodeInfoLiteIsFavorite(const NativeNode *node)
{
    return node->info.is_favorite;
}
bool nodeInfoLiteViaMqtt(const NativeNode *node)
{
    return node->info.via_mqtt;
}
bool nodeInfoLiteHeardOnSlot(const NativeNode *node, uint32_t)
{
    return node->info.heard_on_current_lora;
}
struct NativeConfig {
    meshtastic_Config_LoRaConfig lora{};
    struct {
        bool is_managed = false;
    } security;
} config;
struct Channels {
    meshtastic_Channel primary{};
    unsigned getPrimaryIndex() const { return 0; }
    const meshtastic_Channel &getByIndex(unsigned) const { return primary; }
} channels;
struct Queue {
    std::deque<meshtastic_FromRadio> events;
} queue;
constexpr bool pdTRUE = true;
unsigned uxQueueMessagesWaiting(Queue *q)
{
    return q->events.size();
}
bool xQueueReceive(Queue *q, meshtastic_FromRadio *out, unsigned)
{
    if (q->events.empty())
        return false;
    *out = q->events.front();
    q->events.pop_front();
    return true;
}
class PacketAPI
{
  public:
    void refreshNodeFacts();
    bool readPrivateReceiveQueue(meshtastic_FromRadio &, bool);
    void publish(const meshtastic_FromRadio &event)
    {
        if (queue.events.size() == 32)
            ++droppedPackets;
        else
            queue.events.push_back(event);
    }
    void publishNotification(const meshtastic_ClientNotification &note)
    {
        meshtastic_FromRadio event{};
        event.which_payload_variant = meshtastic_FromRadio_clientNotification_tag;
        event.clientNotification = note;
        publish(event);
    }

  private:
    struct NodeFact {
        uint32_t node = 0, hash = 0;
        bool seen = false;
    };
    NodeFact nodeFacts[256]{};
    uint32_t lastNodeFacts = 0;
    bool nodeFactsReady = false;
    std::atomic<uint32_t> droppedPackets{0};
    Queue *receiveQueue = &queue;
} api;
PacketAPI *packetAPI = &api;
#include "native_nodes_core.inc"
static meshtastic_FromRadio read()
{
    meshtastic_FromRadio event{};
    assert(api.readPrivateReceiveQueue(event, true));
    return event;
}
static void drain()
{
    meshtastic_FromRadio event{};
    while (api.readPrivateReceiveQueue(event, true)) {
    }
}
int main()
{
    constexpr uint32_t peer = 0x22222222;
    NativeNode own{}, other{};
    own.info.num = database.getNodeNum();
    other.info.num = peer;
    database.nodes = {own, other};
    api.refreshNodeFacts();
    assert(queue.events.empty());
    database.getMeshNode(peer)->info.heard_on_current_lora = true;
    nowMs += 1000;
    api.refreshNodeFacts();
    meshtastic_FromRadio peek{};
    assert(api.readPrivateReceiveQueue(peek, false));
    database.getMeshNode(peer)->info.heard_on_current_lora = false;
    auto event = read();
    assert(event.id == wada::NodeDelta && event.node_info.num == peer && !event.node_info.heard_on_current_lora);
    database.getMeshNode(peer)->info.is_favorite = true;
    nowMs += 1000;
    api.refreshNodeFacts();
    database.removeNodeByNum(peer);
    event = read();
    assert(event.id == wada::NodeGone && event.node_info.num == peer);
    other.info.has_user = true;
    strcpy(other.info.user.long_name, "Accepted native name");
    database.nodes.push_back(other);
    nowMs += 1000;
    api.refreshNodeFacts();
    event = read();
    assert(event.id == wada::NodeDelta && !strcmp(event.node_info.user.long_name, "Accepted native name"));
    drain();

    PacketAPI fresh;
    database.nodes.clear();
    for (unsigned i = 0; i < 100; ++i) {
        NativeNode n{};
        n.info.num = 0x30000000 + i;
        database.nodes.push_back(n);
    }
    nowMs += 1000;
    fresh.refreshNodeFacts();
    for (auto &n : database.nodes)
        n.info.is_favorite = true;
    nowMs += 1000;
    fresh.refreshNodeFacts();
    assert(fresh.readPrivateReceiveQueue(event, true));
    assert(event.which_payload_variant == meshtastic_FromRadio_clientNotification_tag &&
           strstr(event.clientNotification.message, "resync required"));
    while (fresh.readPrivateReceiveQueue(event, true)) {
    }

    auto setup = [&] {
        database.nodes = {own, other};
        database.removed = 0;
        config = {};
        channels.primary = {};
        strcpy(channels.primary.settings.name, "Public");
        queue.events.clear();
        wada::nodeRemovalRequests = {};
    };
    uint32_t serial = 50;
    auto attempt = [&](wada::NodeRemovalResult expected, std::function<void()> change = {}) {
        wada::UnheardRemoval request{};
        request.id = ++serial;
        request.node = peer;
        request.self = database.getNodeNum();
        request.lora = config.lora;
        strcpy(request.primaryName, channels.primary.settings.name);
        assert(wada::queueUnheardRemoval(request));
        if (change)
            change();
        assert(wadaProcessNodeActions());
        const auto reply = read();
        assert(reply.clientNotification.has_reply_id && reply.clientNotification.reply_id == request.id);
        assert(!strcmp(reply.clientNotification.message, wada::nodeRemovalReason(expected)));
        assert(database.removed == (expected == wada::NodeRemovalResult::Removed ? 1u : 0u));
    };
    setup();
    attempt(wada::NodeRemovalResult::Favorite, [&] { database.getMeshNode(peer)->info.is_favorite = true; });
    setup();
    attempt(wada::NodeRemovalResult::Mqtt, [&] { database.getMeshNode(peer)->info.via_mqtt = true; });
    setup();
    attempt(wada::NodeRemovalResult::Heard, [&] { database.getMeshNode(peer)->info.heard_on_current_lora = true; });
    setup();
    attempt(wada::NodeRemovalResult::Managed, [&] { config.security.is_managed = true; });
    setup();
    attempt(wada::NodeRemovalResult::Changed, [&] { config.lora.channel_num = 7; });
    setup();
    attempt(wada::NodeRemovalResult::Changed, [&] { strcpy(channels.primary.settings.name, "Changed"); });
    setup();
    attempt(wada::NodeRemovalResult::Missing, [&] { database.nodes.pop_back(); });
    setup();
    attempt(wada::NodeRemovalResult::Removed);
    assert(!database.getMeshNode(peer));
    setup();
    wada::UnheardRemoval request{};
    request.id = ++serial;
    request.node = database.getNodeNum();
    request.self = database.getNodeNum();
    strcpy(request.primaryName, channels.primary.settings.name);
    assert(wada::queueUnheardRemoval(request) && wadaProcessNodeActions());
    event = read();
    assert(!strcmp(event.clientNotification.message, wada::nodeRemovalReason(wada::NodeRemovalResult::Self)));
    puts("PASS: production native node refresh, overflow and guarded removal bodies");
}
