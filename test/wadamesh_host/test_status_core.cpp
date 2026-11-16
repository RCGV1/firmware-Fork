// Compiles the production status commit/broadcast and PacketAPI refresh bodies.
// A clear must reach the actual primary channel once, transaction drafts must not
// publish, and queued replay must never resurrect a status superseded by a clear.
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/generated/meshtastic/module_config.pb.h"
#include "mesh/generated/meshtastic/portnums.pb.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>
#include <vector>
#define WADA_MESHTASTIC 1
#define LOG_INFO(...) ((void)0)
constexpr int SEGMENT_CONFIG = 1, SEGMENT_CHANNELS = 2, SEGMENT_MODULECONFIG = 4;
uint32_t millis()
{
    return 500;
}
constexpr int ERRNO_OK = 0, ERRNO_SHOULD_RELEASE = 35, ERRNO_UNKNOWN = 32;
constexpr uint32_t NODENUM_BROADCAST = UINT32_MAX;
size_t pb_encode_to_bytes(uint8_t *out, size_t cap, const pb_msgdesc_t *desc, const void *data)
{
    auto stream = pb_ostream_from_buffer(out, cap);
    assert(pb_encode(&stream, desc, data));
    return stream.bytes_written;
}
struct Config {
    bool has_statusmessage = false;
    meshtastic_ModuleConfig_StatusMessageConfig statusmessage{};
} moduleConfig;
struct NativeDB {
    meshtastic_StatusMessage status{};
    bool peerExists = true, haveStatus = true;
    int savedSegments = 0;
    unsigned radioResets = 0, slotRefreshes = 0;
    void resetRadioConfig() { ++radioResets; }
    void refreshCommittedLoraSlot() { ++slotRefreshes; }
    void saveToDisk(int value) { savedSegments = value; }
    uint32_t getNodeNum() const { return 0x11111111; }
    bool getMeshNode(uint32_t) const { return peerExists; }
    void setNodeStatus(uint32_t node, const meshtastic_StatusMessage &value)
    {
        assert(node == getNodeNum());
        status = value;
    }
    bool copyNodeStatus(uint32_t, meshtastic_StatusMessage &out)
    {
        if (haveStatus)
            out = status;
        return haveStatus;
    }
} database;
NativeDB *nodeDB = &database;
struct Channels {
    unsigned primary = 3;
    unsigned getPrimaryIndex() const { return primary; }
} channels;
struct Observable {
    unsigned notifications = 0;
    void notifyObservers(void *) { ++notifications; }
};
struct NativeService {
    std::vector<meshtastic_MeshPacket> sent;
    int result = ERRNO_OK;
    Observable configChanged;
    void reloadConfig(int);
    int sendToMesh(meshtastic_MeshPacket *p)
    {
        sent.push_back(*p);
        delete p;
        return result;
    }
} nativeService;
NativeService *service = &nativeService;
class SinglePortModule
{
  public:
    SinglePortModule(const char *, meshtastic_PortNum) {}
};
namespace concurrency
{
class OSThread
{
  public:
    OSThread(const char *) {}
};
} // namespace concurrency
class StatusMessageModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    StatusMessageModule();
    char committedStatus[80]{};
    bool announceChange = false, allocationFails = false;
    unsigned interval = 0;
    void setIntervalFromNow(unsigned value) { interval = value; }
    void setInterval(unsigned value) { interval = value; }
    meshtastic_MeshPacket *allocDataPacket()
    {
        if (allocationFails)
            return nullptr;
        auto *p = new meshtastic_MeshPacket{};
        p->which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        p->decoded.portnum = meshtastic_PortNum_NODE_STATUS_APP;
        return p;
    }
    const char *currentStatus() const { return committedStatus; }
    void onConfigChanged();
    int32_t runOnce();
} statusModule;
StatusMessageModule *statusMessageModule = &statusModule;
class PacketAPI
{
  public:
    void refreshStatus(meshtastic_FromRadio &);
    std::vector<meshtastic_FromRadio> published;
    unsigned notifications = 0;
    void publish(const meshtastic_FromRadio &event) { published.push_back(event); }
    void publishNotification(const meshtastic_ClientNotification &) { ++notifications; }
} api;
PacketAPI *packetAPI = &api;
class AdminModule
{
  public:
    bool hasOpenEditTransaction = false, rebooted = false;
    uint32_t editTransactionActivityMs = 0;
    int deferredEditSegments = 0;
    void reboot(int) { rebooted = true; }
    void saveChanges(int, bool = true);
};
constexpr int DEFAULT_REBOOT_SECONDS = 15;
#include "native_status_core.inc"
meshtastic_StatusMessage decoded(const meshtastic_MeshPacket &packet)
{
    meshtastic_StatusMessage value{};
    auto stream = pb_istream_from_buffer(packet.decoded.payload.bytes, packet.decoded.payload.size);
    assert(pb_decode(&stream, meshtastic_StatusMessage_fields, &value));
    return value;
}
int main()
{
    assert(statusModule.runOnce() == 12 * 60 * 60 * 1000 && nativeService.sent.empty());
    moduleConfig.has_statusmessage = true;
    strcpy(moduleConfig.statusmessage.node_status, "Uncommitted draft");
    statusModule.runOnce();
    assert(nativeService.sent.empty());
    statusModule.onConfigChanged();
    assert(statusModule.interval == 1000 && statusModule.announceChange);
    assert(!strcmp(database.status.status, "Uncommitted draft"));
    strcpy(moduleConfig.statusmessage.node_status, "A later transaction draft");
    statusModule.allocationFails = true;
    assert(statusModule.runOnce() == 60000 && statusModule.announceChange);
    statusModule.allocationFails = false;
    nativeService.result = ERRNO_UNKNOWN;
    assert(statusModule.runOnce() == 60000 && statusModule.announceChange);
    nativeService.result = ERRNO_OK;
    assert(statusModule.runOnce() == 12 * 60 * 60 * 1000 && !statusModule.announceChange);
    auto packet = nativeService.sent.back();
    assert(packet.to == UINT32_MAX && packet.channel == 3 && !packet.decoded.want_response);
    assert(packet.priority == meshtastic_MeshPacket_Priority_BACKGROUND);
    assert(!strcmp(decoded(packet).status, "Uncommitted draft"));
    statusModule.interval = 99;
    strcpy(moduleConfig.statusmessage.node_status, "Uncommitted draft");
    statusModule.onConfigChanged();
    assert(statusModule.interval == 99 && !statusModule.announceChange);
    moduleConfig.statusmessage.node_status[0] = 0;
    statusModule.onConfigChanged();
    assert(!database.status.status[0] && statusModule.announceChange);
    nativeService.result = ERRNO_SHOULD_RELEASE;
    statusModule.runOnce();
    assert(!statusModule.announceChange && !decoded(nativeService.sent.back()).status[0]);
    const auto count = nativeService.sent.size();
    statusModule.runOnce();
    assert(nativeService.sent.size() == count); // Empty periodic status stays silent after the clear.
    meshtastic_FromRadio event{};
    event.which_payload_variant = meshtastic_FromRadio_packet_tag;
    event.packet = packet; // A previously prefetched nonempty replay now follows the live clear.
    api.refreshStatus(event);
    assert(!decoded(event.packet).status[0]);
    strcpy(database.status.status, "Newest peer value");
    api.refreshStatus(event);
    assert(!strcmp(decoded(event.packet).status, "Newest peer value"));
    database.peerExists = false;
    api.refreshStatus(event);
    assert(!decoded(event.packet).status[0]);
    database.peerExists = true;
    database.haveStatus = false;
    api.refreshStatus(event);
    assert(!decoded(event.packet).status[0]);
    event = {};
    event.which_payload_variant = meshtastic_FromRadio_moduleConfig_tag;
    event.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_statusmessage_tag;
    strcpy(event.moduleConfig.payload_variant.statusmessage.node_status, "Old queued config");
    strcpy(moduleConfig.statusmessage.node_status, "Not committed");
    api.refreshStatus(event);
    assert(!event.moduleConfig.payload_variant.statusmessage.node_status[0]);
    AdminModule admin;
    admin.hasOpenEditTransaction = true;
    moduleConfig.has_statusmessage = true;
    strcpy(moduleConfig.statusmessage.node_status, "Commit only");
    const auto publishedBefore = api.published.size();
    admin.saveChanges(SEGMENT_MODULECONFIG, false);
    assert(admin.deferredEditSegments == SEGMENT_MODULECONFIG && admin.editTransactionActivityMs == 500);
    assert(!statusModule.currentStatus()[0] && api.published.size() == publishedBefore);
    admin.hasOpenEditTransaction = false;
    admin.saveChanges(SEGMENT_MODULECONFIG, false);
    assert(database.savedSegments == SEGMENT_MODULECONFIG && !strcmp(statusModule.currentStatus(), "Commit only"));
    assert(api.published.size() == publishedBefore + 1 && !database.radioResets && !database.slotRefreshes);
    assert(!strcmp(api.published.back().moduleConfig.payload_variant.statusmessage.node_status, "Commit only"));
    assert(!admin.rebooted);
    strcpy(database.status.status, "Stale boot satellite");
    moduleConfig.has_statusmessage = false;
    StatusMessageModule bootEmpty;
    assert(!database.status.status[0] && !bootEmpty.announceChange && bootEmpty.interval == 12 * 60 * 60 * 1000);
    moduleConfig.has_statusmessage = true;
    strcpy(moduleConfig.statusmessage.node_status, "Boot config");
    StatusMessageModule bootValue;
    assert(!strcmp(database.status.status, "Boot config") && !bootValue.announceChange && bootValue.interval == 120000);
    puts("PASS: production committed status broadcasts, explicit clears and authoritative replay refresh");
}
