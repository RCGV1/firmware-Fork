#pragma once
#include "MeshBackend.h"
#include "WadaDelivery.h"
#include "WadaNativeConfig.h"
#include "WadaNodeState.h"
#include "WadaTelemetry.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include "wadacomms/PacketClient.h"
#include <array>

class WadaMeshtasticBackend : public MeshBackend
{
  public:
    using Delivery = WadaDelivery::State;
    const char *name() const override { return "Meshtastic PacketAPI"; }
    void begin(MeshBackendEvents &events, NodePrefs &prefs, uint8_t key[32]) override;
    void loop(uint32_t now) override;
    int sendDirect(const ContactInfo &, uint32_t, const char *, uint32_t &, uint32_t &, uint32_t &) override;
    bool sendChannel(int, const ChannelDetails &, uint32_t, const char *, const char *, uint32_t &) override;
    bool sendAdvert(bool) override;
    bool ownsPrefs() const override { return configured; }
    bool busy() const override;
    void prefsChanged(const NodePrefs &) override;
    void channelChanged(int, const ChannelDetails &) override;
    void advertPositionChanged(int32_t, int32_t) override;
    bool contactChanged(const ContactInfo &) override;
    bool contactRemoved(const uint8_t[32]) override;
    bool setBlePin(uint32_t) override;
    int requestTelemetry(const ContactInfo &) override;
    bool telemetryAvailable(uint32_t node, wada::TelemetryKind) const;
    bool requestTelemetry(uint32_t node, wada::TelemetryKind);
    size_t telemetryLog(uint32_t node, wada::TelemetryKind kind, char *out, size_t cap, bool imperial = false) const
    {
        return telemetryHistory.format(node, kind, out, cap, imperial);
    }
    int requestStatus(const ContactInfo &c) override { return requestTelemetry(c); }
    uint32_t tracePing(const uint8_t[32]) override;
    bool runNodeCli(const char *) override;
    uint32_t packetsRecv() const override { return received; }
    uint32_t packetsSent() const override { return transmitted; }
    bool sendAdmin(const meshtastic_AdminMessage &);
    bool setConfig(const meshtastic_Config &);
    bool power(bool shutdown);
    bool sendNodeInfo(bool oneHop = false, bool requestReplies = false);
    bool requestUserInfo(uint32_t node);
    bool canMessage(uint32_t node) const;
    const char *directMessageReason(uint32_t node) const;
    const meshtastic_NodeInfo *nodeInfo(uint32_t node) const;
    bool reportsHeardOnCurrentLora() const { return metadataKnown && wada::firmwareAtLeast(metadataVersion, 2, 8, 1); }
    bool nodeSnapshotReady() const { return configured && metadataKnown && !snapshotActive; }
    uint32_t nodeRevision() const { return nodesRevision; }
    bool unheardOnCurrentLora(uint32_t node) const;
    bool removeUnheardNode(uint32_t node);
    bool removingUnheardNodes() const { return removalCount != 0; }
    bool nodeKeyMismatch(uint32_t node) const;
    wada::NodeSecurity nodeSecurity(uint32_t node) const;
    const char *nodeFirmwareVersion(uint32_t node) const;
    bool nodeSigned(uint32_t node) const;
    bool nodeEncrypted(uint32_t node) const;
    bool requestNodeMetadata(uint32_t node);
    const char *nodeStatus(uint32_t node) const;
    bool canEditStatus() const;
    static constexpr size_t statusMaxBytes() { return sizeof(meshtastic_ModuleConfig_StatusMessageConfig::node_status) - 1; }
    bool setStatusMessage(const char *text);
    bool channelIdentity(unsigned index, uint8_t out[32]) const;
    bool setOwner(const char *longName, const char *shortName);
    void resync()
    {
        configured = false;
        syncPending = false;
    }
    size_t mirroredNodes() const { return nodeCount; }
    bool channelsReady() const { return configured && haveLoRa; }
    bool isManaged() const { return managed; }
    unsigned primaryIndex() const
    {
        for (unsigned i = 0; i < 8; ++i)
            if (channels[i].role == meshtastic_Channel_Role_PRIMARY)
                return i;
        return 0;
    }
    const meshtastic_Config_DeviceConfig &deviceSettings() const { return deviceConfig; }
    const meshtastic_Config_LoRaConfig &loraConfig() const { return lora; }
    const meshtastic_Config_BluetoothConfig &bluetoothConfig() const { return bluetooth; }
    const meshtastic_Config_NetworkConfig &networkConfig() const { return network; }
    const meshtastic_Config_PositionConfig &positionSettings() const { return positionConfig; }
    const meshtastic_Channel &channelConfig(unsigned idx) const { return channels[idx < 8 ? idx : 0]; }
    bool contactUrl(uint32_t, char *, size_t) const;
    bool channelUrl(unsigned, char *, size_t, bool add = false) const;
    const meshtastic_User &ownUser() const { return owner; }
    bool saveChannel(const meshtastic_Channel &);
    bool validPreset(meshtastic_Config_LoRaConfig_RegionCode, meshtastic_Config_LoRaConfig_ModemPreset) const;
    bool defaultPreset(meshtastic_Config_LoRaConfig_RegionCode, meshtastic_Config_LoRaConfig_ModemPreset &) const;
    bool regionLicensedOnly(meshtastic_Config_LoRaConfig_RegionCode) const;
    meshtastic_Config_LoRaConfig_ModemPreset presetForRegionChange(meshtastic_Config_LoRaConfig_RegionCode previous,
                                                                   meshtastic_Config_LoRaConfig_RegionCode next,
                                                                   meshtastic_Config_LoRaConfig_ModemPreset current) const;
    uint32_t nodeNumber() const { return self; }
    static void nodeKey(uint32_t node, uint8_t key[32]);
    static uint32_t keyNode(const uint8_t key[32]);

  private:
    struct Pending {
        uint32_t id = 0, to = 0, started = 0;
        Delivery state = Delivery::Pending;
        bool expectAck = true;
        uint8_t error = 0;
    };
    struct Seen {
        uint32_t from = 0, id = 0;
    };
    PacketClient client;
    MeshBackendEvents *events = nullptr;
    NodePrefs prefs{};
    meshtastic_NodeInfo nodes[256]{};
    size_t nodeCount = 0;
    meshtastic_Channel channels[8]{};
    meshtastic_Config_LoRaConfig lora{};
    meshtastic_Config_BluetoothConfig bluetooth{};
    meshtastic_Config_NetworkConfig network{};
    meshtastic_Config_PositionConfig positionConfig{};
    meshtastic_Config_DeviceConfig deviceConfig{};
    meshtastic_User owner{};
    meshtastic_LoRaRegionPresetMap regionPresets{};
    struct PeerMetadata {
        uint32_t node = 0;
        char version[18]{};
        bool mismatch = false;
    };
    std::array<PeerMetadata, 256> peerMetadata{};
    struct StatusRecord {
        uint32_t node = 0;
        char text[sizeof(meshtastic_StatusMessage::status)]{};
        bool seenInSnapshot = false;
    };
    std::array<StatusRecord, 256> statuses{};
    bool statusConfigKnown = false;
    void onStatus(uint32_t node, const char *text);
    void finishStatusSnapshot();
    struct MetadataRequest {
        uint32_t id = 0, node = 0, started = 0;
        uint8_t key[32]{};
    };
    std::array<MetadataRequest, 4> metadataRequests{};
    wada::NodeState nodeState;
    uint32_t nodeStateSelf = 0, nodeStateGeneration = 0, lastNodeStateSave = 0, nodesRevision = 0;
    unsigned nodeStateSlot = 0;
    bool nodeStateDirty = false, metadataKnown = false;
    char metadataVersion[18]{};
    uint32_t excludedModules = 0;
    std::array<uint32_t, 256> removals{};
    unsigned removalCount = 0, removalNext = 0;
    uint32_t removalActive = 0, removalActiveNode = 0;
    meshtastic_Config_LoRaConfig removalLora{};
    char removalPrimary[32]{};
    struct ExplicitRemoval {
        uint32_t id = 0, node = 0, started = 0;
        bool acknowledged = false;
    };
    std::array<ExplicitRemoval, 8> explicitRemovals{};
    void explicitRemovalFailure(uint32_t id, const char *reason);
    void finishExplicitRemovals();
    bool confirmExplicitRemoval(uint32_t node);
    void cancelExplicitRemovals(const char *reason);
    void traceFailure(uint32_t id, const char *reason);
    std::array<Pending, 16> pending{};
    struct TelemetryRequest {
        uint32_t id = 0, node = 0, started = 0;
        wada::TelemetryKind kind = wada::TelemetryKind::Device;
    };
    std::array<TelemetryRequest, 12> telemetryRequests{};
    wada::TelemetryLog telemetryHistory;
    uint32_t telemetryGeneration = 0, lastTelemetrySave = 0;
    unsigned telemetrySlot = 0;
    bool telemetryDirty = false;
    std::array<Seen, 64> seen{};
    std::array<bool, 256> snapshotNodeSeen{};
    bool snapshotActive = false;
    size_t seenNext = 0;
    uint32_t self = 0, nonce = 0, nextId = 0, lastSync = 0, lastHeartbeat = 0;
    uint32_t received = 0, transmitted = 0;
    uint32_t traceId = 0, traceTo = 0, traceAt = 0, lastTrace = 0;
    bool configured = false, syncPending = false, haveLoRa = false, managed = false;
    uint32_t newId();
    bool submit(meshtastic_ToRadio &);
    void requestConfig(uint32_t);
    void onRadio(const meshtastic_FromRadio &);
    void onPacket(const meshtastic_MeshPacket &);
    void onNode(const meshtastic_NodeInfo &, bool full = false);
    PeerMetadata *metadataFor(uint32_t node, bool create = true);
    void noteKeyMismatch(uint32_t node, const uint8_t trusted[32]);
    void forgetNode(uint32_t node, bool forgetWarning = false, bool notify = true);
    void nodeActions();
    void cancelNodeRemovals(const char *reason);
    void protectNodeRemoval(uint32_t node);
    bool nodeNotification(const meshtastic_ClientNotification &);
    bool nodeAdmin(const meshtastic_MeshPacket &);
    void nodeRequestFailure(uint32_t id, const char *reason);
    void loadNodeState();
    void saveNodeState(uint32_t now, bool force = false);
    void telemetryStatus(const TelemetryRequest &, const char *reason);
    void telemetryFailure(uint32_t id, const char *reason);
    void loadTelemetry();
    void saveTelemetry(uint32_t now);
    const meshtastic_LoRaPresetGroup *presetGroup(meshtastic_Config_LoRaConfig_RegionCode) const;
    void onChannel(const meshtastic_Channel &);
    void updateSelf();
    void delivery(uint32_t, Delivery, uint8_t error = 0);
    static bool reusable(const Pending &);
    Pending *availableSlot();
    meshtastic_NodeInfo *findNode(uint32_t, bool create = true);
    uint32_t sendData(uint32_t to, uint8_t channel, meshtastic_PortNum port, const uint8_t *, size_t, bool ack,
                      bool response = false, uint32_t hopLimit = 0);
};
extern WadaMeshtasticBackend *wadaBackend;
