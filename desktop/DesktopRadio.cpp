// Local official-protobuf fixture for the production UI adapter. No transport or RF.
#include "DesktopRadio.h"
#include "WadaChannelIdentity.h"
#include "WadaChannelIntent.h"
#include "WadaMeshtasticBackend.h"
#include "WadaNodeActions.h"
#include "WadaStatus.h"
#include "mesh/generated/meshtastic/portnums.pb.h"
#include "wadacomms/PacketServer.h"
#include <Arduino.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <optional>
#include <pb_decode.h>
#include <pb_encode.h>
#include <sstream>
#include <vector>

namespace
{
constexpr uint32_t self = 0xd35c0001, peer = 0xd35c0002, broadcast = UINT32_MAX;
constexpr size_t maxNodes = 256, maxOutput = 384; // Entire bounded node/config snapshot fits.
PacketServer *server = nullptr;
std::deque<meshtastic_FromRadio> output;
std::vector<meshtastic_NodeInfo> nodes;
std::array<meshtastic_Channel, 8> channels{};
std::map<uint32_t, std::string> heardSlots, statuses;
meshtastic_Config_LoRaConfig lora{};
meshtastic_Config_BluetoothConfig bluetooth{};
meshtastic_Config_NetworkConfig network{};
meshtastic_Config_PositionConfig position{};
meshtastic_Config_DeviceConfig device{};
meshtastic_Config_SecurityConfig security{};
meshtastic_ModuleConfig_StatusMessageConfig ownStatus{};
meshtastic_LoRaRegionPresetMap presetMap{};
std::map<std::string, uint32_t> counts;
uint32_t packetId = 0x60000000, excludedModules = 0, lastRequest = 0, lastPort = 0, lastAdmin = 0;
uint32_t lastTelemetry = 0, lastBroadcastHop = 0;
bool welcomed = false, broadcastWantResponse = false, externalPower = false;
std::string version = "2.8.2.desktop-sim", nextText = "normal", nextPacket = "normal", telemetryMode = "normal";
std::string lastError, lastText, powerState = "running", scenarioName = "normal";
std::optional<meshtastic_MeshPacket> delayedAck;

template <class T> std::vector<uint8_t> encode(const T &v, const pb_msgdesc_t *fields)
{
    std::vector<uint8_t> bytes(2048);
    auto stream = pb_ostream_from_buffer(bytes.data(), bytes.size());
    if (!pb_encode(&stream, fields, &v))
        return {};
    bytes.resize(stream.bytes_written);
    return bytes;
}
template <class T> bool decode(const meshtastic_Data &d, T &v, const pb_msgdesc_t *fields)
{
    if (d.payload.size > sizeof d.payload.bytes)
        return false;
    auto stream = pb_istream_from_buffer(d.payload.bytes, d.payload.size);
    return pb_decode(&stream, fields, &v);
}
void publish(const meshtastic_FromRadio &value)
{
    if (output.size() < maxOutput)
        output.push_back(value);
    else
        ++counts["output_dropped"];
}
meshtastic_NodeInfo *find(uint32_t number)
{
    for (auto &n : nodes)
        if (n.num == number)
            return &n;
    return nullptr;
}
meshtastic_NodeInfo *ensure(uint32_t number)
{
    if (!number || number == broadcast)
        return nullptr;
    if (auto *n = find(number))
        return n;
    if (nodes.size() >= maxNodes)
        return nullptr;
    meshtastic_NodeInfo n{};
    n.num = number;
    n.has_user = true;
    snprintf(n.user.id, sizeof n.user.id, "!%08x", number);
    snprintf(n.user.long_name, sizeof n.user.long_name, "Sim node %04x", number & 0xffff);
    snprintf(n.user.short_name, sizeof n.user.short_name, "%04x", number & 0xffff);
    nodes.push_back(n);
    return &nodes.back();
}
unsigned primary()
{
    for (unsigned i = 0; i < channels.size(); ++i)
        if (channels[i].role == meshtastic_Channel_Role_PRIMARY)
            return i;
    return 0;
}
const char *primaryName()
{
    const auto &ch = channels[primary()];
    return ch.settings.name[0] ? ch.settings.name : wada::effectivePresetName(lora.modem_preset, lora.use_preset);
}
std::string currentSlot()
{
    meshtastic_Config_LoRaConfig active{};
    active.region = lora.region;
    active.use_preset = lora.use_preset;
    if (lora.use_preset)
        active.modem_preset = lora.modem_preset;
    else {
        active.bandwidth = lora.bandwidth;
        active.spread_factor = lora.spread_factor;
        active.coding_rate = lora.coding_rate;
    }
    active.override_frequency = lora.override_frequency;
    active.channel_num = lora.channel_num;
    auto bytes = encode(active, meshtastic_Config_LoRaConfig_fields);
    return std::string(bytes.begin(), bytes.end()) + '\0' + primaryName();
}
void refreshHeard()
{
    const auto slot = currentSlot();
    for (auto &n : nodes) {
        const auto it = heardSlots.find(n.num);
        n.heard_on_current_lora = n.num == self || (!n.via_mqtt && it != heardSlots.end() && it->second == slot);
    }
}
void notification(const char *message, uint32_t id = 0, bool error = false)
{
    meshtastic_FromRadio r{};
    r.which_payload_variant = meshtastic_FromRadio_clientNotification_tag;
    r.clientNotification.level = error ? meshtastic_LogRecord_Level_ERROR : meshtastic_LogRecord_Level_INFO;
    r.clientNotification.has_reply_id = id != 0;
    r.clientNotification.reply_id = id;
    snprintf(r.clientNotification.message, sizeof r.clientNotification.message, "%s", message);
    publish(r);
}
void node(const meshtastic_NodeInfo &n, uint32_t marker = wada::NodeDelta)
{
    meshtastic_FromRadio r{};
    r.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    r.id = marker;
    r.node_info = n;
    publish(r);
}
void packet(const meshtastic_MeshPacket &p)
{
    // Router::perhapsHandleReceived discards all packets from locally ignored nodes.
    if (const auto *n = find(p.from); n && n->is_ignored) {
        ++counts["ignored_packets_dropped"];
        return;
    }
    meshtastic_FromRadio r{};
    r.which_payload_variant = meshtastic_FromRadio_packet_tag;
    r.packet = p;
    publish(r);
}
meshtastic_MeshPacket reply(const meshtastic_MeshPacket &q, meshtastic_PortNum port)
{
    meshtastic_MeshPacket p{};
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.from = q.to == broadcast ? peer : q.to;
    p.to = self;
    p.id = ++packetId;
    p.channel = q.channel;
    p.has_rx_time = true;
    p.rx_time = time(nullptr);
    p.decoded.portnum = port;
    p.decoded.request_id = q.id;
    p.hop_start = p.hop_limit = 3;
    p.decoded.has_bitfield = true;
    p.rx_snr = 0;
    p.has_rx_rssi = true;
    p.rx_rssi = -74;
    if (const auto *n = find(p.from)) {
        p.via_mqtt = n->via_mqtt;
        if (p.via_mqtt)
            p.has_rx_rssi = false;
    }
    return p;
}
template <class T> void response(const meshtastic_MeshPacket &q, meshtastic_PortNum port, const T &v, const pb_msgdesc_t *fields)
{
    auto p = reply(q, port);
    auto bytes = encode(v, fields);
    if (bytes.size() > sizeof p.decoded.payload.bytes)
        return;
    if (!bytes.empty())
        memcpy(p.decoded.payload.bytes, bytes.data(), bytes.size());
    p.decoded.payload.size = bytes.size();
    packet(p);
}
void route(const meshtastic_MeshPacket &q, meshtastic_Routing_Error error = meshtastic_Routing_Error_NONE, bool relay = false)
{
    meshtastic_Routing r{};
    r.which_variant = meshtastic_Routing_error_reason_tag;
    r.error_reason = error;
    auto p = reply(q, meshtastic_PortNum_ROUTING_APP);
    if (q.to == broadcast || relay)
        p.from = self;
    auto bytes = encode(r, meshtastic_Routing_fields);
    if (!bytes.empty())
        memcpy(p.decoded.payload.bytes, bytes.data(), bytes.size());
    p.decoded.payload.size = bytes.size();
    packet(p);
}
void fail(const meshtastic_MeshPacket &q, meshtastic_Routing_Error error, const char *text)
{
    ++counts["rejections"];
    lastError = text;
    route(q, error);
}
void config(const meshtastic_Config &value)
{
    meshtastic_FromRadio r{};
    r.which_payload_variant = meshtastic_FromRadio_config_tag;
    r.config = value;
    publish(r);
}
void publishMetadata()
{
    meshtastic_FromRadio r{};
    r.which_payload_variant = meshtastic_FromRadio_metadata_tag;
    snprintf(r.metadata.firmware_version, sizeof r.metadata.firmware_version, "%s", version.c_str());
    r.metadata.hw_model = meshtastic_HardwareModel_T_DECK;
    r.metadata.hasWifi = r.metadata.hasBluetooth = r.metadata.hasPKC = r.metadata.has_xeddsa = true;
    r.metadata.canShutdown = true;
    r.metadata.excluded_modules = excludedModules;
    publish(r);
}
void publishConfig()
{
    meshtastic_Config c{};
    c.which_payload_variant = meshtastic_Config_lora_tag;
    c.payload_variant.lora = lora;
    config(c);
    c = {};
    c.which_payload_variant = meshtastic_Config_device_tag;
    c.payload_variant.device = device;
    config(c);
    c = {};
    c.which_payload_variant = meshtastic_Config_security_tag;
    c.payload_variant.security = security;
    config(c);
    c = {};
    c.which_payload_variant = meshtastic_Config_bluetooth_tag;
    c.payload_variant.bluetooth = bluetooth;
    config(c);
    c = {};
    c.which_payload_variant = meshtastic_Config_network_tag;
    c.payload_variant.network = network;
    config(c);
    c = {};
    c.which_payload_variant = meshtastic_Config_position_tag;
    c.payload_variant.position = position;
    config(c);
}
void moduleStatus()
{
    meshtastic_FromRadio r{};
    r.which_payload_variant = meshtastic_FromRadio_moduleConfig_tag;
    r.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_statusmessage_tag;
    r.moduleConfig.payload_variant.statusmessage = ownStatus;
    publish(r);
}
void status(uint32_t number, const char *value)
{
    statuses[number] = value;
    meshtastic_MeshPacket request{};
    request.to = number;
    meshtastic_StatusMessage s{};
    snprintf(s.status, sizeof s.status, "%s", value);
    response(request, meshtastic_PortNum_NODE_STATUS_APP, s, meshtastic_StatusMessage_fields);
}
void publishMap()
{
    meshtastic_FromRadio r{};
    r.which_payload_variant = meshtastic_FromRadio_region_presets_tag;
    r.region_presets = presetMap;
    publish(r);
}
void handshake(uint32_t nonce)
{
    ++counts["snapshots"];
    refreshHeard();
    meshtastic_FromRadio r{};
    r.which_payload_variant = meshtastic_FromRadio_my_info_tag;
    r.my_info.my_node_num = self;
    publish(r);
    publishMetadata();
    publishMap();
    publishConfig();
    for (const auto &ch : channels) {
        r = {};
        r.which_payload_variant = meshtastic_FromRadio_channel_tag;
        r.channel = ch;
        publish(r);
    }
    moduleStatus();
    for (const auto &n : nodes)
        node(n, 0);
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    r.config_complete_id = nonce;
    publish(r);
    for (const auto &value : statuses)
        if (value.first != self)
            status(value.first, value.second.c_str());
    if (!welcomed) {
        welcomed = true;
        meshtastic_MeshPacket p{};
        p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        p.from = peer;
        p.to = broadcast;
        p.id = ++packetId;
        p.channel = primary();
        p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        p.has_rx_time = true;
        p.rx_time = time(nullptr);
        const char *text = "SIMULATION: our Meshtastic UI/backend. Messages and ACKs stay on this Mac.";
        memcpy(p.decoded.payload.bytes, text, strlen(text));
        p.decoded.payload.size = strlen(text);
        packet(p);
    }
    fprintf(stderr, "[desktop-sim] real UI/adapter ready: %zu simulated nodes; no RF\n", nodes.size());
}
bool keyPublic(unsigned index)
{
    const auto *key = &channels[index].settings.psk;
    if (!key->size && channels[index].role == meshtastic_Channel_Role_SECONDARY)
        key = &channels[primary()].settings.psk;
    if (key->size <= 1)
        return true;
    static const uint8_t prefix[] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69};
    return key->size == 16 && !memcmp(key->bytes, prefix, sizeof prefix);
}
void repairChannels()
{
    bool hasPrimary = false;
    for (const auto &ch : channels)
        hasPrimary |= ch.has_settings && ch.role == meshtastic_Channel_Role_PRIMARY;
    if (!hasPrimary) {
        auto &ch = channels[0];
        if (ch.role == meshtastic_Channel_Role_SECONDARY)
            ch.role = meshtastic_Channel_Role_PRIMARY;
        else {
            ch = {};
            ch.index = 0;
            ch.role = meshtastic_Channel_Role_PRIMARY;
            ch.has_settings = true;
            ch.settings.psk.size = 1;
            ch.settings.psk.bytes[0] = 1;
        }
    }
    for (unsigned i = 0; i < channels.size(); ++i) {
        auto &ch = channels[i];
        ch.index = i;
        if (!ch.has_settings) {
            ch.role = meshtastic_Channel_Role_DISABLED;
            ch.settings = {};
            ch.has_settings = true;
        }
        if (!strcmp(ch.settings.name, "Default"))
            ch.settings.name[0] = 0;
        const auto &key = !ch.settings.psk.size && ch.role == meshtastic_Channel_Role_SECONDARY ? channels[primary()].settings.psk
                                                                                                : ch.settings.psk;
        if (!key.size || (key.size == 1 && !key.bytes[0]))
            ch.settings.use_aead = false;
        if (ch.settings.has_module_settings) {
            auto &precision = ch.settings.module_settings.position_precision;
            uint32_t allowed = ch.role == meshtastic_Channel_Role_DISABLED ? 0
                               : keyPublic(i)                              ? std::min<uint32_t>(precision, 15)
                                                                           : precision;
            if (allowed != precision) {
                ++counts["privacy_clamps"];
                precision = allowed;
            }
        }
    }
    refreshHeard();
}
bool validLora(const meshtastic_Config_LoRaConfig &v)
{
    if (unsigned(v.region) >= _meshtastic_Config_LoRaConfig_RegionCode_ARRAYSIZE || v.hop_limit > 7 || v.tx_power < -9 ||
        v.tx_power > 30 || !std::isfinite(v.override_frequency) || v.override_frequency < 0 || v.override_frequency > 2500)
        return false;
    // These protobuf placeholders have no native RegionInfo entry.
    if (v.region == meshtastic_Config_LoRaConfig_RegionCode_UA_868 ||
        v.region == meshtastic_Config_LoRaConfig_RegionCode_EU_874 || v.region == meshtastic_Config_LoRaConfig_RegionCode_EU_917)
        return false;
    for (pb_size_t i = 0; i < presetMap.region_groups_count; ++i) {
        const auto &r = presetMap.region_groups[i];
        if (r.region == v.region && r.group_index < presetMap.groups_count && presetMap.groups[r.group_index].licensed_only &&
            !find(self)->user.is_licensed)
            return false;
    }
    // Unknown/T-Deck target cannot establish SX128x-only support (Android bandwidth policy).
    if (!v.use_preset && v.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24 && v.bandwidth != 0 && v.bandwidth != 200 &&
        v.bandwidth != 400 && v.bandwidth != 800)
        return false;
    if (!v.use_preset)
        return v.spread_factor >= 5 && v.spread_factor <= 12 && v.coding_rate >= 5 && v.coding_rate <= 8;
    if (unsigned(v.modem_preset) >= _meshtastic_Config_LoRaConfig_ModemPreset_ARRAYSIZE ||
        v.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW)
        return false;
    for (pb_size_t i = 0; i < presetMap.region_groups_count; ++i) {
        const auto &r = presetMap.region_groups[i];
        if (r.region != v.region || r.group_index >= presetMap.groups_count)
            continue;
        const auto &g = presetMap.groups[r.group_index];
        if (g.licensed_only && !find(self)->user.is_licensed)
            return false;
        if (!g.presets_count || v.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
            return true;
        for (pb_size_t j = 0; j < g.presets_count; ++j)
            if (g.presets[j] == v.modem_preset)
                return true;
        return false;
    }
    return true;
}
void applyEuPresetRegion()
{
    if (!lora.use_preset || (lora.region != meshtastic_Config_LoRaConfig_RegionCode_EU_868 &&
                             lora.region != meshtastic_Config_LoRaConfig_RegionCode_EU_866 &&
                             lora.region != meshtastic_Config_LoRaConfig_RegionCode_EU_N_868))
        return;
    if (lora.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_LITE_FAST ||
        lora.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_LITE_SLOW)
        lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_866;
    else if (lora.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST ||
             lora.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW)
        lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_N_868;
    else
        lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
}
bool writeConfig(const meshtastic_Config &c)
{
    switch (c.which_payload_variant) {
    case meshtastic_Config_lora_tag:
        if (!validLora(c.payload_variant.lora))
            return false;
        lora = c.payload_variant.lora;
        applyEuPresetRegion();
        repairChannels();
        ++counts["lora_writes"];
        break;
    case meshtastic_Config_bluetooth_tag:
        if (c.payload_variant.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN &&
            (c.payload_variant.bluetooth.fixed_pin < 100000 || c.payload_variant.bluetooth.fixed_pin > 999999))
            return false;
        bluetooth = c.payload_variant.bluetooth;
        ++counts["bluetooth_writes"];
        break;
    case meshtastic_Config_network_tag:
        network = c.payload_variant.network;
        ++counts["network_writes"];
        break;
    case meshtastic_Config_position_tag:
        if (position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED &&
            c.payload_variant.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED && !position.fixed_position &&
            !c.payload_variant.position.fixed_position) {
            find(self)->has_position = false;
            find(self)->position = {};
            node(*find(self));
        }
        position = c.payload_variant.position;
        ++counts["position_writes"];
        break;
    case meshtastic_Config_device_tag:
        device = c.payload_variant.device;
        if (device.node_info_broadcast_secs < 3600)
            device.node_info_broadcast_secs = 3600;
        if (device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT ||
            device.role == meshtastic_Config_DeviceConfig_Role_REPEATER)
            device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
        find(self)->user.role = device.role;
        ++counts["device_writes"];
        break;
    case meshtastic_Config_security_tag:
        security = c.payload_variant.security;
        ++counts["security_writes"];
        break;
    default:
        return false;
    }
    ++counts["config_writes"];
    publishConfig();
    return true;
}
void clearNode(uint32_t number)
{
    if (number == self || !number || number == broadcast)
        return;
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [=](const auto &n) { return n.num == number; }), nodes.end());
    heardSlots.erase(number);
    statuses.erase(number);
    meshtastic_NodeInfo gone{};
    gone.num = number;
    node(gone, wada::NodeGone);
}
void addContact(const meshtastic_SharedContact &c)
{
    if (!c.has_user || !c.node_num || c.node_num == broadcast || c.node_num == self)
        return;
    auto *n = ensure(c.node_num);
    if (!n)
        return;
    if (n->is_key_manually_verified && !c.manually_verified &&
        (n->user.public_key.size != c.user.public_key.size ||
         memcmp(n->user.public_key.bytes, c.user.public_key.bytes, n->user.public_key.size)))
        return;
    const auto saved = n->user.public_key;
    n->has_user = true;
    n->user = c.user;
    if (saved.size == 32 && n->user.public_key.size != 32)
        n->user.public_key = saved;
    n->user.long_name[24] = 0;
    snprintf(n->user.id, sizeof n->user.id, "!%08x", n->num);
    if (c.should_ignore) {
        n->is_ignored = true;
        n->is_favorite = false;
        n->has_position = false;
        n->position = {};
        n->has_device_metrics = false;
        n->device_metrics = {};
        statuses.erase(n->num);
    } else {
        if (device.role == meshtastic_Config_DeviceConfig_Role_CLIENT_BASE)
            n->last_heard = time(nullptr);
        else
            n->is_favorite = true;
        if (c.manually_verified)
            n->is_key_manually_verified = true;
    }
    node(*n);
}
void admin(const meshtastic_MeshPacket &p)
{
    ++counts["admin_requests"];
    meshtastic_AdminMessage a{};
    if (!decode(p.decoded, a, meshtastic_AdminMessage_fields)) {
        fail(p, meshtastic_Routing_Error_BAD_REQUEST, "Malformed admin message");
        return;
    }
    lastAdmin = a.which_payload_variant;
    if (a.which_payload_variant == meshtastic_AdminMessage_get_device_metadata_request_tag) {
        ++counts["metadata_requests"];
        auto *n = find(p.to);
        if (!n) {
            fail(p, meshtastic_Routing_Error_NO_RESPONSE, "Unknown simulated node");
            return;
        }
        meshtastic_AdminMessage m{};
        m.which_payload_variant = meshtastic_AdminMessage_get_device_metadata_response_tag;
        snprintf(m.get_device_metadata_response.firmware_version, sizeof m.get_device_metadata_response.firmware_version,
                 "2.8.2.sim-peer");
        m.get_device_metadata_response.hw_model = n->user.hw_model;
        m.get_device_metadata_response.hasPKC = m.get_device_metadata_response.has_xeddsa = true;
        auto value = reply(p, meshtastic_PortNum_ADMIN_APP);
        // Simulated verified-decrypt facts, never a cryptographic validation result.
        value.pki_encrypted = n->user.public_key.size == 32;
        value.public_key.size = n->user.public_key.size;
        memcpy(value.public_key.bytes, n->user.public_key.bytes, n->user.public_key.size);
        auto bytes = encode(m, meshtastic_AdminMessage_fields);
        if (!bytes.empty())
            memcpy(value.decoded.payload.bytes, bytes.data(), bytes.size());
        value.decoded.payload.size = bytes.size();
        packet(value);
        return;
    }
    if (p.to != self) {
        fail(p, meshtastic_Routing_Error_NOT_AUTHORIZED, "Remote administration not modeled");
        return;
    }
    if (security.is_managed) {
        fail(p, meshtastic_Routing_Error_NOT_AUTHORIZED, "Managed simulated node");
        return;
    }
    bool changed = false;
    switch (a.which_payload_variant) {
    case meshtastic_AdminMessage_set_owner_tag: {
        if (!wada::statusText(a.set_owner.long_name, 24, true) || !wada::statusText(a.set_owner.short_name, 4, true)) {
            fail(p, meshtastic_Routing_Error_BAD_REQUEST, "Invalid name byte length");
            return;
        }
        auto &o = find(self)->user;
        if (a.set_owner.long_name[0])
            strcpy(o.long_name, a.set_owner.long_name);
        if (a.set_owner.short_name[0])
            strcpy(o.short_name, a.set_owner.short_name);
        o.has_is_unmessagable |= a.set_owner.has_is_unmessagable;
        if (a.set_owner.has_is_unmessagable)
            o.is_unmessagable = a.set_owner.is_unmessagable;
        o.is_licensed = a.set_owner.is_licensed;
        ++counts["owner_writes"];
        node(*find(self));
        break;
    }
    case meshtastic_AdminMessage_set_module_config_tag:
        if (a.set_module_config.which_payload_variant != meshtastic_ModuleConfig_statusmessage_tag ||
            !wada::statusText(a.set_module_config.payload_variant.statusmessage.node_status, 79, false)) {
            fail(p, meshtastic_Routing_Error_BAD_REQUEST, "Unsupported module or invalid status");
            return;
        }
        ownStatus = a.set_module_config.payload_variant.statusmessage;
        ++counts["status_writes"];
        moduleStatus();
        break;
    case meshtastic_AdminMessage_set_config_tag:
        if (!writeConfig(a.set_config)) {
            fail(p, meshtastic_Routing_Error_BAD_REQUEST, "Unsupported or invalid configuration");
            return;
        }
        changed = true;
        break;
    case meshtastic_AdminMessage_set_channel_tag: {
        const auto &ch = a.set_channel;
        if (ch.index < 0 || ch.index >= 8 || unsigned(ch.role) > meshtastic_Channel_Role_SECONDARY ||
            !wada::statusText(ch.settings.name, 11, true) || ch.settings.psk.size > 32 ||
            (ch.settings.has_module_settings && ch.settings.module_settings.position_precision > 32)) {
            fail(p, meshtastic_Routing_Error_BAD_REQUEST, "Invalid channel");
            return;
        }
        if (ch.role == meshtastic_Channel_Role_PRIMARY)
            for (auto &old : channels)
                if (old.role == meshtastic_Channel_Role_PRIMARY)
                    old.role = meshtastic_Channel_Role_SECONDARY;
        channels[ch.index] = ch;
        repairChannels();
        ++counts["channel_writes"];
        changed = true;
        break;
    }
    case meshtastic_AdminMessage_add_contact_tag:
        addContact(a.add_contact);
        ++counts["contact_writes"];
        break;
    case meshtastic_AdminMessage_set_favorite_node_tag:
    case meshtastic_AdminMessage_remove_favorite_node_tag: {
        bool value = a.which_payload_variant == meshtastic_AdminMessage_set_favorite_node_tag;
        if (auto *n = find(value ? a.set_favorite_node : a.remove_favorite_node)) {
            n->is_favorite = value;
            node(*n);
        }
        ++counts["favorite_writes"];
        break;
    }
    case meshtastic_AdminMessage_set_ignored_node_tag:
    case meshtastic_AdminMessage_remove_ignored_node_tag: {
        bool value = a.which_payload_variant == meshtastic_AdminMessage_set_ignored_node_tag;
        auto *n = value ? ensure(a.set_ignored_node) : find(a.remove_ignored_node);
        if (n) {
            n->is_ignored = value;
            if (value) {
                n->has_position = false;
                n->position = {};
                n->has_device_metrics = false;
                n->device_metrics = {};
                statuses.erase(n->num);
            }
            node(*n);
        }
        ++counts["ignore_writes"];
        break;
    }
    case meshtastic_AdminMessage_toggle_muted_node_tag:
        if (auto *n = find(a.toggle_muted_node)) {
            n->is_muted = !n->is_muted;
            node(*n);
        }
        ++counts["mute_writes"];
        break;
    case meshtastic_AdminMessage_remove_by_nodenum_tag:
        if (a.remove_by_nodenum == self || a.remove_by_nodenum == broadcast || !a.remove_by_nodenum) {
            fail(p, meshtastic_Routing_Error_BAD_REQUEST, "Cannot remove connected simulated radio");
            return;
        }
        clearNode(a.remove_by_nodenum);
        ++counts["removals"];
        break;
    case meshtastic_AdminMessage_set_fixed_position_tag: {
        const auto &v = a.set_fixed_position;
        if (!v.has_latitude_i || !v.has_longitude_i || v.latitude_i < -900000000 || v.latitude_i > 900000000 ||
            int64_t(v.longitude_i) < -1800000000LL || int64_t(v.longitude_i) > 1800000000LL) {
            fail(p, meshtastic_Routing_Error_BAD_REQUEST, "Invalid fixed coordinates");
            return;
        }
        position.fixed_position = true;
        auto *n = find(self);
        n->has_position = true;
        n->position = v;
        n->position.time = time(nullptr);
        node(*n);
        ++counts["fixed_position_writes"];
        publishConfig();
        changed = true;
        break;
    }
    case meshtastic_AdminMessage_remove_fixed_position_tag:
        position.fixed_position = false;
        find(self)->has_position = false;
        find(self)->position = {};
        node(*find(self));
        ++counts["fixed_position_writes"];
        publishConfig();
        changed = true;
        break;
    case meshtastic_AdminMessage_reboot_seconds_tag:
        ++counts["reboot_requests"];
        powerState = "reboot_requested";
        notification("Simulation: reboot request recorded; no host/hardware power action");
        break;
    case meshtastic_AdminMessage_shutdown_seconds_tag:
        ++counts["shutdown_requests"];
        powerState = "shutdown_requested";
        notification("Simulation: shutdown request recorded; this Mac stays on");
        break;
    default:
        ++counts["unsupported"];
        fail(p, meshtastic_Routing_Error_BAD_REQUEST, "Simulation: administration action not modeled");
        return;
    }
    route(p);
    if (changed)
        notification("WADAMESH configuration changed; resync");
}
void telemetry(const meshtastic_MeshPacket &p)
{
    ++counts["telemetry_requests"];
    meshtastic_Telemetry t{};
    if (!decode(p.decoded, t, meshtastic_Telemetry_fields)) {
        fail(p, meshtastic_Routing_Error_BAD_REQUEST, "Invalid telemetry request");
        return;
    }
    lastTelemetry = t.which_variant;
    t.time = time(nullptr);
    const bool missing = telemetryMode == "missing", zero = telemetryMode == "zero";
    switch (t.which_variant) {
    case meshtastic_Telemetry_device_metrics_tag:
        t.variant.device_metrics = {};
        if (!missing) {
            auto &m = t.variant.device_metrics;
            m.has_battery_level = m.has_voltage = m.has_channel_utilization = m.has_air_util_tx = m.has_uptime_seconds = true;
            m.battery_level = externalPower ? 101 : zero ? 0 : 87;
            m.voltage = zero ? 0 : externalPower ? 5.0 : 4.05;
            m.channel_utilization = 0;
            m.air_util_tx = 0;
            m.uptime_seconds = zero ? 0 : 3600;
        }
        if (auto *n = find(p.to); n && !n->is_ignored) {
            n->has_device_metrics = true;
            n->device_metrics = t.variant.device_metrics;
        }
        break;
    case meshtastic_Telemetry_environment_metrics_tag:
        t.variant.environment_metrics = {};
        if (!missing) {
            auto &m = t.variant.environment_metrics;
            m.has_temperature = m.has_relative_humidity = m.has_barometric_pressure = m.has_iaq = true;
            m.temperature = zero ? 0 : 21.5;
            m.relative_humidity = zero ? 0 : 46;
            m.barometric_pressure = zero ? 0 : 1013.2;
            m.iaq = zero ? 0 : 120;
        }
        break;
    case meshtastic_Telemetry_air_quality_metrics_tag:
        t.variant.air_quality_metrics = {};
        if (!missing) {
            auto &m = t.variant.air_quality_metrics;
            m.has_pm25_environmental = m.has_pm25_standard = m.has_co2 = m.has_co2_temperature = true;
            m.pm25_environmental = zero ? 0 : 6;
            m.pm25_standard = 0;
            m.co2 = zero ? 0 : 780;
            m.co2_temperature = 0;
        }
        break;
    case meshtastic_Telemetry_power_metrics_tag:
        t.variant.power_metrics = {};
        if (!missing) {
            auto &m = t.variant.power_metrics;
            m.has_ch1_voltage = m.has_ch1_current = m.has_ch3_voltage = m.has_ch3_current = true;
            m.ch1_voltage = zero ? 0 : 12.4;
            m.ch1_current = 0;
            m.ch3_voltage = zero ? 0 : 5.0;
            m.ch3_current = zero ? 0 : 650.25;
        }
        break;
    case meshtastic_Telemetry_local_stats_tag:
        t.variant.local_stats = {};
        if (!zero && !missing) {
            auto &m = t.variant.local_stats;
            m.uptime_seconds = 3600;
            m.num_packets_tx = 12;
            m.num_packets_rx = 42;
            m.num_online_nodes = 2;
            m.num_total_nodes = nodes.size();
            m.heap_total_bytes = 102400;
            m.heap_free_bytes = 51200;
            m.noise_floor = -110;
        }
        break;
    case meshtastic_Telemetry_host_metrics_tag:
        if (!find(p.to) || find(p.to)->user.hw_model != meshtastic_HardwareModel_PORTDUINO) {
            fail(p, meshtastic_Routing_Error_BAD_REQUEST, "Host metrics require simulated PORTDUINO node");
            return;
        }
        t.variant.host_metrics = {};
        if (!zero && !missing) {
            auto &m = t.variant.host_metrics;
            m.uptime_seconds = 3600;
            m.freemem_bytes = 1073741824;
            m.diskfree1_bytes = 2147483648ULL;
            m.load1 = 50;
            m.load5 = 25;
            m.load15 = 0;
            m.has_user_string = true;
            strcpy(m.user_string, "Simulated host metrics");
        }
        break;
    default:
        ++counts["unsupported"];
        fail(p, meshtastic_Routing_Error_BAD_REQUEST, "Simulation: telemetry variant not modeled");
        return;
    }
    response(p, meshtastic_PortNum_TELEMETRY_APP, t, meshtastic_Telemetry_fields);
}
void trace(const meshtastic_MeshPacket &p)
{
    ++counts["traceroute_requests"];
    meshtastic_RouteDiscovery r{};
    r.route_count = 2;
    r.route[0] = self + 2;
    r.route[1] = broadcast;
    r.snr_towards_count = 3;
    r.snr_towards[0] = 32;
    r.snr_towards[1] = INT8_MIN;
    r.snr_towards[2] = 0;
    r.route_back_count = 1;
    r.route_back[0] = self + 3;
    r.snr_back_count = 2;
    r.snr_back[0] = INT8_MIN;
    r.snr_back[1] = 28;
    response(p, meshtastic_PortNum_TRACEROUTE_APP, r, meshtastic_RouteDiscovery_fields);
}
void userInfo(const meshtastic_MeshPacket &p)
{
    ++counts["nodeinfo_requests"];
    lastBroadcastHop = p.hop_limit;
    broadcastWantResponse = p.decoded.want_response;
    if (p.to == broadcast)
        ++counts["broadcasts"];
    else
        ++counts["userinfo_requests"];
    if (p.decoded.want_response) {
        for (auto &n : nodes) {
            if (n.num == self || n.is_ignored || (p.to != broadcast && n.num != p.to))
                continue;
            n.last_heard = time(nullptr);
            if (!n.via_mqtt)
                heardSlots[n.num] = currentSlot();
            refreshHeard();
            node(n);
            auto q = p;
            q.to = n.num;
            response(q, meshtastic_PortNum_NODEINFO_APP, n.user, meshtastic_User_fields);
        }
    }
    notification("Node info: simulated announcement only");
}
void echoText(const meshtastic_MeshPacket &p)
{
    auto value = reply(p, meshtastic_PortNum_TEXT_MESSAGE_APP);
    value.to = p.to == broadcast ? broadcast : self;
    value.decoded.request_id = 0;
    const char *text = "Simulation echo: received locally. No radio transmission.";
    memcpy(value.decoded.payload.bytes, text, strlen(text));
    value.decoded.payload.size = strlen(text);
    packet(value);
}
void outgoing(const meshtastic_MeshPacket &p)
{
    ++counts["packets"];
    lastRequest = p.id;
    lastPort = p.decoded.portnum;
    if (p.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return;
    if (nextPacket == "reject") {
        nextPacket = "normal";
        ++counts["rejections"];
        lastError = "Simulated radio queue rejection";
        meshtastic_FromRadio r{};
        r.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
        r.queueStatus.res = 32;
        r.queueStatus.mesh_packet_id = p.id;
        publish(r);
        return;
    }
    switch (p.decoded.portnum) {
    case meshtastic_PortNum_ADMIN_APP:
        admin(p);
        return;
    case meshtastic_PortNum_TELEMETRY_APP:
        telemetry(p);
        return;
    case meshtastic_PortNum_TRACEROUTE_APP:
        trace(p);
        return;
    case meshtastic_PortNum_NODEINFO_APP:
        userInfo(p);
        return;
    case meshtastic_PortNum_POSITION_APP:
        if (auto *n = find(p.to))
            response(p, meshtastic_PortNum_POSITION_APP, n->position, meshtastic_Position_fields);
        ++counts["position_requests"];
        return;
    case meshtastic_PortNum_TEXT_MESSAGE_APP:
        break;
    default:
        ++counts["unsupported"];
        fail(p, meshtastic_Routing_Error_BAD_REQUEST, "Simulation: packet type not modeled");
        return;
    }
    ++counts["text_sends"];
    lastText.assign(reinterpret_cast<const char *>(p.decoded.payload.bytes), p.decoded.payload.size);
    uint8_t identity[32]{};
    const bool have = p.channel < 8 && wada::channelIdentity(channels.data(), lora, p.channel, identity);
    if (p.to == broadcast && !wada::consumeChannelIntent(p.id, p.channel, have ? identity : nullptr)) {
        fail(p, meshtastic_Routing_Error_NO_CHANNEL, "Channel changed before send");
        return;
    }
    const auto mode = nextText;
    nextText = "normal";
    if (mode == "no-ack") {
        lastError = "Simulated missing confirmation";
        return;
    }
    if (mode == "nak-channel") {
        fail(p, meshtastic_Routing_Error_NO_CHANNEL, "Simulated channel/key mismatch");
        return;
    }
    if (mode == "nak-large") {
        fail(p, meshtastic_Routing_Error_TOO_LARGE, "Simulated oversized packet");
        return;
    }
    if (mode == "relay" || mode == "late") {
        route(p, meshtastic_Routing_Error_NONE, true);
        if (mode == "late")
            delayedAck = p;
        ++counts["relay_confirmations"];
        return;
    }
    route(p);
    echoText(p);
    ++counts[p.to == broadcast ? "relay_confirmations" : "recipient_confirmations"];
    fprintf(stderr, "[desktop-sim] simulated message/ACK only\n");
}
void makePresetMap()
{
    presetMap = {};
    using P = meshtastic_Config_LoRaConfig_ModemPreset;
    const P standard[] = {
        P::meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,     P::meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW,
        P::meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,   P::meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST,
        P::meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW,    P::meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,
        P::meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE, P::meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO,
        P::meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO,    P::meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO};
    presetMap.groups_count = 6;
    auto &s = presetMap.groups[0];
    s.presets_count = 10;
    std::copy(std::begin(standard), std::end(standard), s.presets);
    s.default_preset = P::meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    for (unsigned g = 1; g <= 3; ++g) {
        auto &group = presetMap.groups[g];
        group.presets_count = 11;
        std::copy(standard, standard + 7, group.presets);
        group.presets[7] = P::meshtastic_Config_LoRaConfig_ModemPreset_LITE_FAST;
        group.presets[8] = P::meshtastic_Config_LoRaConfig_ModemPreset_LITE_SLOW;
        group.presets[9] = P::meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST;
        group.presets[10] = P::meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW;
        group.default_preset = g == 1   ? P::meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST
                               : g == 2 ? P::meshtastic_Config_LoRaConfig_ModemPreset_LITE_FAST
                                        : P::meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW;
    }
    auto &ham = presetMap.groups[4];
    ham.presets_count = 2;
    ham.presets[0] = P::meshtastic_Config_LoRaConfig_ModemPreset_TINY_FAST;
    ham.presets[1] = P::meshtastic_Config_LoRaConfig_ModemPreset_TINY_SLOW;
    ham.default_preset = ham.presets[0];
    ham.licensed_only = true;
    auto &hamNarrow = presetMap.groups[5];
    hamNarrow.presets_count = 2;
    hamNarrow.presets[0] = P::meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST;
    hamNarrow.presets[1] = P::meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW;
    hamNarrow.default_preset = hamNarrow.presets[1];
    hamNarrow.licensed_only = true;
    // Mirrors every native region/profile in RadioInterface.cpp; UNSET stays unconstrained.
    presetMap.region_groups_count = 34;
    presetMap.region_groups[0] = {meshtastic_Config_LoRaConfig_RegionCode_US, 0};
    presetMap.region_groups[1] = {meshtastic_Config_LoRaConfig_RegionCode_EU_433, 0};
    presetMap.region_groups[2] = {meshtastic_Config_LoRaConfig_RegionCode_EU_868, 1};
    presetMap.region_groups[3] = {meshtastic_Config_LoRaConfig_RegionCode_EU_866, 2};
    presetMap.region_groups[4] = {meshtastic_Config_LoRaConfig_RegionCode_EU_N_868, 3};
    presetMap.region_groups[5] = {meshtastic_Config_LoRaConfig_RegionCode_CN, 0};
    presetMap.region_groups[6] = {meshtastic_Config_LoRaConfig_RegionCode_JP, 0};
    presetMap.region_groups[7] = {meshtastic_Config_LoRaConfig_RegionCode_ANZ, 0};
    presetMap.region_groups[8] = {meshtastic_Config_LoRaConfig_RegionCode_ANZ_433, 0};
    presetMap.region_groups[9] = {meshtastic_Config_LoRaConfig_RegionCode_RU, 0};
    presetMap.region_groups[10] = {meshtastic_Config_LoRaConfig_RegionCode_KR, 0};
    presetMap.region_groups[11] = {meshtastic_Config_LoRaConfig_RegionCode_TW, 0};
    presetMap.region_groups[12] = {meshtastic_Config_LoRaConfig_RegionCode_IN, 0};
    presetMap.region_groups[13] = {meshtastic_Config_LoRaConfig_RegionCode_NZ_865, 0};
    presetMap.region_groups[14] = {meshtastic_Config_LoRaConfig_RegionCode_TH, 0};
    presetMap.region_groups[15] = {meshtastic_Config_LoRaConfig_RegionCode_UA_433, 0};
    presetMap.region_groups[16] = {meshtastic_Config_LoRaConfig_RegionCode_MY_433, 0};
    presetMap.region_groups[17] = {meshtastic_Config_LoRaConfig_RegionCode_MY_919, 0};
    presetMap.region_groups[18] = {meshtastic_Config_LoRaConfig_RegionCode_SG_923, 0};
    presetMap.region_groups[19] = {meshtastic_Config_LoRaConfig_RegionCode_PH_433, 0};
    presetMap.region_groups[20] = {meshtastic_Config_LoRaConfig_RegionCode_PH_868, 0};
    presetMap.region_groups[21] = {meshtastic_Config_LoRaConfig_RegionCode_PH_915, 0};
    presetMap.region_groups[22] = {meshtastic_Config_LoRaConfig_RegionCode_KZ_433, 0};
    presetMap.region_groups[23] = {meshtastic_Config_LoRaConfig_RegionCode_KZ_863, 0};
    presetMap.region_groups[24] = {meshtastic_Config_LoRaConfig_RegionCode_NP_865, 0};
    presetMap.region_groups[25] = {meshtastic_Config_LoRaConfig_RegionCode_BR_902, 0};
    presetMap.region_groups[26] = {meshtastic_Config_LoRaConfig_RegionCode_ITU1_2M, 4};
    presetMap.region_groups[27] = {meshtastic_Config_LoRaConfig_RegionCode_ITU2_2M, 4};
    presetMap.region_groups[28] = {meshtastic_Config_LoRaConfig_RegionCode_ITU3_2M, 4};
    presetMap.region_groups[29] = {meshtastic_Config_LoRaConfig_RegionCode_ITU2_125CM, 5};
    presetMap.region_groups[30] = {meshtastic_Config_LoRaConfig_RegionCode_ITU1_70CM, 5};
    presetMap.region_groups[31] = {meshtastic_Config_LoRaConfig_RegionCode_ITU2_70CM, 5};
    presetMap.region_groups[32] = {meshtastic_Config_LoRaConfig_RegionCode_ITU3_70CM, 5};
    presetMap.region_groups[33] = {meshtastic_Config_LoRaConfig_RegionCode_LORA_24, 0};
}
} // namespace
void desktopRadioInit()
{
    server = PacketServer::init();
    output.clear();
    nodes.clear();
    heardSlots.clear();
    statuses.clear();
    counts.clear();
    lora = {};
    bluetooth = {};
    network = {};
    position = {};
    device = {};
    security = {};
    ownStatus = {};
    channels = {};
    packetId = 0x60000000;
    welcomed = false;
    lastRequest = lastPort = lastAdmin = lastTelemetry = lastBroadcastHop = excludedModules = 0;
    version = "2.8.2.desktop-sim";
    nextText = nextPacket = telemetryMode = scenarioName = "normal";
    lastError.clear();
    lastText.clear();
    powerState = "running";
    delayedAck.reset();
    externalPower = broadcastWantResponse = false;
    lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    lora.use_preset = true;
    lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    lora.hop_limit = 3;
    lora.tx_enabled = true;
    lora.bandwidth = 250;
    lora.spread_factor = 11;
    lora.coding_rate = 0;
    device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    device.node_info_broadcast_secs = 3600;
    position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED;
    bluetooth.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
    bluetooth.fixed_pin = 123456;
    for (unsigned i = 0; i < 8; ++i) {
        channels[i].index = i;
        channels[i].role = meshtastic_Channel_Role_DISABLED;
        channels[i].has_settings = true;
    }
    channels[0].role = meshtastic_Channel_Role_PRIMARY;
    channels[0].settings.psk.size = 1;
    channels[0].settings.psk.bytes[0] = 1;
    channels[1].role = meshtastic_Channel_Role_SECONDARY;
    strcpy(channels[1].settings.name, "Sim private");
    channels[1].settings.psk.size = 32;
    memset(channels[1].settings.psk.bytes, 0x42, 32);
    strcpy(ownStatus.node_status, "SIMULATION - no LoRa radio");
    const char *names[] = {"Desktop Sim", "Sim Alice", "Sim Old Node", "Sim Keyless"};
    for (unsigned i = 0; i < 4; ++i) {
        meshtastic_NodeInfo n{};
        n.num = self + i;
        n.has_user = true;
        snprintf(n.user.id, sizeof n.user.id, "!%08x", n.num);
        snprintf(n.user.long_name, sizeof n.user.long_name, "%s", names[i]);
        snprintf(n.user.short_name, sizeof n.user.short_name, "S%u", i);
        n.user.hw_model = meshtastic_HardwareModel_T_DECK;
        n.user.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
        if (i != 3) {
            n.user.public_key.size = 32;
            memset(n.user.public_key.bytes, 0x10 + i, 32);
        }
        n.has_xeddsa_signed = i < 2;
        n.is_favorite = i == 1;
        n.last_heard = time(nullptr) - (i == 2 ? 86400 : 30);
        n.has_hops_away = true;
        n.hops_away = i == 1 ? 0 : 1;
        n.snr = 0;
        n.has_device_metrics = true;
        n.device_metrics.has_battery_level = n.device_metrics.has_voltage = true;
        n.device_metrics.battery_level = 87;
        n.device_metrics.voltage = 4.05;
        n.has_position = true;
        n.position.has_latitude_i = n.position.has_longitude_i = true;
        n.position.latitude_i = 523728000 + i * 10000;
        n.position.longitude_i = 48934000 + i * 10000;
        n.position.time = time(nullptr);
        nodes.push_back(n);
    }
    heardSlots[self] = heardSlots[peer] = currentSlot();
    refreshHeard();
    makePresetMap();
    statuses[peer] = "Simulated peer - no radio";
    for (const char *name : {"snapshots",
                             "packets",
                             "admin_requests",
                             "config_writes",
                             "lora_writes",
                             "device_writes",
                             "position_writes",
                             "network_writes",
                             "bluetooth_writes",
                             "security_writes",
                             "channel_writes",
                             "owner_writes",
                             "status_writes",
                             "contact_writes",
                             "favorite_writes",
                             "ignore_writes",
                             "mute_writes",
                             "fixed_position_writes",
                             "removals",
                             "telemetry_requests",
                             "traceroute_requests",
                             "metadata_requests",
                             "text_sends",
                             "broadcasts",
                             "nodeinfo_requests",
                             "userinfo_requests",
                             "position_requests",
                             "reboot_requests",
                             "shutdown_requests",
                             "rejections",
                             "unsupported",
                             "output_dropped",
                             "privacy_clamps",
                             "recipient_confirmations",
                             "relay_confirmations"})
        counts[name] = 0;
    fprintf(stderr, "[desktop-sim] LOCAL FIXTURE: no serial, Bluetooth, sockets, native cryptography or RF\n");
}
void desktopRadioLoop()
{
    if (!server)
        return;
    for (unsigned i = 0; i < 8 && server->hasData(); ++i) {
        auto item = server->receivePacket();
        if (!item)
            break;
        const auto value = static_cast<DataPacket<meshtastic_ToRadio> *>(item.get())->getData();
        if (value.which_payload_variant == meshtastic_ToRadio_want_config_id_tag)
            handshake(value.want_config_id);
        else if (value.which_payload_variant == meshtastic_ToRadio_packet_tag)
            outgoing(value.packet);
    }
    wada::UnheardRemoval request{};
    if (wada::takeUnheardRemoval(request)) {
        auto *n = find(request.node);
        auto result = wada::NodeRemovalResult::Removed;
        if (request.self != self ||
            encode(lora, meshtastic_Config_LoRaConfig_fields) != encode(request.lora, meshtastic_Config_LoRaConfig_fields) ||
            strcmp(request.primaryName, channels[primary()].settings.name))
            result = wada::NodeRemovalResult::Changed;
        else if (security.is_managed)
            result = wada::NodeRemovalResult::Managed;
        else if (!n)
            result = wada::NodeRemovalResult::Missing;
        else if (n->num == self)
            result = wada::NodeRemovalResult::Self;
        else if (n->is_favorite)
            result = wada::NodeRemovalResult::Favorite;
        else if (n->via_mqtt)
            result = wada::NodeRemovalResult::Mqtt;
        else if (n->heard_on_current_lora)
            result = wada::NodeRemovalResult::Heard;
        if (result == wada::NodeRemovalResult::Removed) {
            clearNode(request.node);
            ++counts["removals"];
        }
        notification(wada::nodeRemovalReason(result), request.id);
    }
    for (unsigned i = 0; i < 8 && !output.empty() && server->available(); ++i) {
        const auto value = output.front();
        if (!server->sendPacket(DataPacket<meshtastic_FromRadio>(value.id, value)))
            break;
        output.pop_front();
    }
}
namespace
{
std::string fingerprint(const uint8_t *bytes, size_t size)
{
    SHA256 hash;
    hash.update(bytes, size);
    uint8_t value[32];
    hash.finalize(value, sizeof value);
    char hex[65];
    for (unsigned i = 0; i < sizeof value; ++i)
        snprintf(hex + 2 * i, 3, "%02x", value[i]);
    return hex;
}
} // namespace
#include <ArduinoJson.h>
std::string desktopSimulationState()
{
    JsonDocument doc;
    doc["simulation"] = true;
    doc["native_crypto"] = false;
    doc["self"] = self;
    doc["peer"] = peer;
    doc["primary"] = primary();
    doc["node_count"] = nodes.size();
    doc["firmware"] = version;
    doc["excluded_modules"] = excludedModules;
    doc["managed"] = security.is_managed;
    doc["scenario"] = scenarioName;
    doc["power_state"] = powerState;
    doc["status"] = ownStatus.node_status;
    auto counters = doc["counts"].to<JsonObject>();
    for (const auto &v : counts) {
        doc[v.first] = v.second;
        counters[v.first] = v.second;
    }
    const auto *n = find(self);
    auto owner = doc["owner"].to<JsonObject>();
    owner["long_name"] = n ? n->user.long_name : "";
    owner["short_name"] = n ? n->user.short_name : "";
    owner["is_licensed"] = n && n->user.is_licensed;
    owner["node_status"] = ownStatus.node_status;
    auto l = doc["lora"].to<JsonObject>();
    l["region"] = int(lora.region);
    l["region_name"] = wada::regionName(lora.region);
    l["modem_preset"] = int(lora.modem_preset);
    l["preset_name"] = wada::presetName(lora.modem_preset);
    l["use_preset"] = lora.use_preset;
    l["bandwidth"] = lora.bandwidth;
    l["spread_factor"] = lora.spread_factor;
    l["coding_rate"] = lora.coding_rate;
    l["hop_limit"] = unsigned(lora.hop_limit);
    l["channel_num"] = lora.channel_num;
    l["override_frequency"] = lora.override_frequency;
    l["tx_power"] = int(lora.tx_power);
    l["tx_enabled"] = lora.tx_enabled;
    l["sx126x_rx_boosted_gain"] = lora.sx126x_rx_boosted_gain;
    l["ignore_mqtt"] = lora.ignore_mqtt;
    auto bt = doc["bluetooth"].to<JsonObject>();
    bt["enabled"] = bluetooth.enabled;
    bt["mode"] = int(bluetooth.mode);
    bt["fixed_pin"] = bluetooth.fixed_pin;
    auto net = doc["network"].to<JsonObject>();
    net["wifi_enabled"] = network.wifi_enabled;
    net["wifi_ssid"] = network.wifi_ssid;
    net["wifi_psk_length"] = strlen(network.wifi_psk);
    net["wifi_psk_id"] = fingerprint(reinterpret_cast<const uint8_t *>(network.wifi_psk), strlen(network.wifi_psk));
    auto pos = doc["position"].to<JsonObject>();
    pos["gps_mode"] = int(position.gps_mode);
    pos["fixed_position"] = position.fixed_position;
    pos["position_broadcast_secs"] = position.position_broadcast_secs;
    pos["has_position"] = n && n->has_position;
    pos["latitude_i"] = n ? n->position.latitude_i : 0;
    pos["longitude_i"] = n ? n->position.longitude_i : 0;
    auto dev = doc["device"].to<JsonObject>();
    dev["role"] = int(device.role);
    dev["node_info_broadcast_secs"] = device.node_info_broadcast_secs;
    auto last = doc["last"].to<JsonObject>();
    last["request_id"] = lastRequest;
    last["port"] = lastPort;
    last["admin"] = lastAdmin;
    last["telemetry"] = lastTelemetry;
    last["broadcast_hop_limit"] = lastBroadcastHop;
    last["broadcast_want_response"] = broadcastWantResponse;
    last["error"] = lastError;
    last["text"] = lastText;
    auto chs = doc["channels"].to<JsonArray>();
    for (unsigned i = 0; i < channels.size(); ++i) {
        const auto &c = channels[i];
        auto v = chs.add<JsonObject>();
        v["index"] = c.index;
        v["role"] = int(c.role);
        v["name"] = c.settings.name;
        v["psk_size"] = c.settings.psk.size;
        v["psk_id"] = fingerprint(c.settings.psk.bytes, c.settings.psk.size);
        v["public_key"] = keyPublic(i);
        v["inherits_key"] = c.role == meshtastic_Channel_Role_SECONDARY && !c.settings.psk.size;
        v["uplink_enabled"] = c.settings.uplink_enabled;
        v["downlink_enabled"] = c.settings.downlink_enabled;
        v["use_aead"] = c.settings.use_aead;
        v["position_precision"] = c.settings.has_module_settings ? c.settings.module_settings.position_precision : 0;
        v["is_muted"] = c.settings.has_module_settings && c.settings.module_settings.is_muted;
    }
    auto ns = doc["nodes"].to<JsonArray>();
    for (const auto &n : nodes) {
        auto v = ns.add<JsonObject>();
        v["num"] = n.num;
        v["long_name"] = n.user.long_name;
        v["hw_model"] = int(n.user.hw_model);
        v["key_size"] = n.user.public_key.size;
        v["is_favorite"] = n.is_favorite;
        v["is_ignored"] = n.is_ignored;
        v["is_muted"] = n.is_muted;
        v["is_key_manually_verified"] = n.is_key_manually_verified;
        v["has_xeddsa_signed"] = n.has_xeddsa_signed;
        v["heard_on_current_lora"] = n.heard_on_current_lora;
        v["via_mqtt"] = n.via_mqtt;
        auto it = statuses.find(n.num);
        v["status"] = n.num == self ? ownStatus.node_status : it == statuses.end() ? "" : it->second;
    }
    std::string text;
    serializeJson(doc, text);
    return text;
}
bool desktopSimulationScenario(const char *name)
{
    if (!name || !server)
        return false;
    const std::string mode(name);
    if (mode == "normal") {
        nextText = nextPacket = telemetryMode = "normal";
        externalPower = false;
        lastError.clear();
    } else if (mode == "reset-counters") {
        for (auto &entry : counts)
            entry.second = 0;
        lastError.clear();
    } else if (mode == "reject-next" || mode == "queue-reject-next")
        nextPacket = "reject";
    else if (mode == "no-ack-next")
        nextText = "no-ack";
    else if (mode == "relay-only-next")
        nextText = "relay";
    else if (mode == "late-ack-next")
        nextText = "late";
    else if (mode == "nak-no-channel-next")
        nextText = "nak-channel";
    else if (mode == "nak-too-large-next")
        nextText = "nak-large";
    else if (mode == "deliver-late-ack") {
        if (!delayedAck)
            return false;
        route(*delayedAck);
        echoText(*delayedAck);
        ++counts[delayedAck->to == broadcast ? "relay_confirmations" : "recipient_confirmations"];
        delayedAck.reset();
    } else if (mode == "managed" || mode == "unmanaged") {
        security.is_managed = mode == "managed";
        publishConfig();
    } else if (mode == "old-firmware" || mode == "current-firmware") {
        version = mode == "old-firmware" ? "2.7.20.sim" : "2.8.2.desktop-sim";
        publishMetadata();
    } else if (mode == "status-excluded" || mode == "status-supported") {
        excludedModules = mode == "status-excluded" ? meshtastic_ExcludedModules_STATUSMESSAGE_CONFIG : 0;
        publishMetadata();
    } else if (mode == "telemetry-missing" || mode == "telemetry-zero")
        telemetryMode = mode == "telemetry-zero" ? "zero" : "missing";
    else if (mode == "externally-powered") {
        externalPower = true;
        meshtastic_MeshPacket q{};
        q.to = self;
        q.decoded.portnum = meshtastic_PortNum_TELEMETRY_APP;
        meshtastic_Telemetry t{};
        t.which_variant = meshtastic_Telemetry_device_metrics_tag;
        auto bytes = encode(t, meshtastic_Telemetry_fields);
        memcpy(q.decoded.payload.bytes, bytes.data(), bytes.size());
        q.decoded.payload.size = bytes.size();
        telemetry(q);
    } else if (mode == "region-map" || mode == "no-region-map") {
        if (mode == "region-map")
            makePresetMap();
        else
            presetMap = {};
        publishMap();
    } else if (mode == "inherited-secondary") {
        for (auto &c : channels)
            if (c.role == meshtastic_Channel_Role_PRIMARY)
                c.role = meshtastic_Channel_Role_SECONDARY;
        channels[0].role = meshtastic_Channel_Role_PRIMARY;
        channels[1].role = meshtastic_Channel_Role_SECONDARY;
        channels[0].settings.psk.size = 32;
        memset(channels[0].settings.psk.bytes, 0x24, 32);
        channels[1].settings.psk = {};
        repairChannels();
        notification("WADAMESH configuration changed; resync");
    } else if (mode == "key-mismatch") {
        const auto *n = find(peer);
        if (!n)
            return false;
        auto user = n->user;
        user.public_key.size = 32;
        memset(user.public_key.bytes, 0x77, 32);
        meshtastic_MeshPacket q{};
        q.to = peer;
        auto p = reply(q, meshtastic_PortNum_NODEINFO_APP);
        p.to = broadcast;
        auto bytes = encode(user, meshtastic_User_fields);
        memcpy(p.decoded.payload.bytes, bytes.data(), bytes.size());
        p.decoded.payload.size = bytes.size();
        packet(p);
    } else if (mode == "receive-overflow") {
        ++counts["output_dropped"];
        notification("WADAMESH receive overflow: 1 dropped; resync required");
    } else if (mode == "reboot") {
        meshtastic_FromRadio r{};
        r.which_payload_variant = meshtastic_FromRadio_rebooted_tag;
        publish(r);
    } else {
        auto *n = find(peer);
        if (!n)
            return false;
        if (mode == "host-peer")
            n->user.hw_model = meshtastic_HardwareModel_PORTDUINO;
        else if (mode == "hear-peer") {
            n->via_mqtt = false;
            heardSlots[peer] = currentSlot();
        } else if (mode == "unheard-peer")
            heardSlots.erase(peer);
        else if (mode == "mqtt-peer")
            n->via_mqtt = true;
        else if (mode == "keyless-peer") {
            n->user.public_key = {};
            n->has_xeddsa_signed = false;
            n->is_key_manually_verified = false;
        } else if (mode == "signed-peer")
            n->has_xeddsa_signed = true;
        else if (mode == "manual-peer")
            n->is_key_manually_verified = true;
        else if (mode == "accept-new-key") {
            n->user.public_key.size = 32;
            memset(n->user.public_key.bytes, 0x55, 32);
            n->has_xeddsa_signed = false;
            n->is_key_manually_verified = false;
        } else if (mode == "unmessageable-peer") {
            n->user.has_is_unmessagable = true;
            n->user.is_unmessagable = true;
        } else
            return false;
        refreshHeard();
        node(*n);
    }
    scenarioName = mode;
    fprintf(stderr, "[desktop-sim] scenario %s (local fixture only)\n", name);
    return true;
}
