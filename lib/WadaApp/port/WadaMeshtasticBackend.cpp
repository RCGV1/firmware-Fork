#include "WadaMeshtasticBackend.h"
#include "WadaChannelIdentity.h"
#include "WadaChannelIntent.h"
#include "WadaNodeActions.h"
#include "WadaStatus.h"
#include "WebMesh.h"
#include "mesh/generated/meshtastic/portnums.pb.h"
#include "ui-touch/UITask.h"

#include "mesh/Throttle.h"
#include "mesh/generated/meshtastic/apponly.pb.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <esp_system.h>
#include <memory>
#include <new>
#include <pb_decode.h>
#include <pb_encode.h>
#include <time.h>

WadaMeshtasticBackend *wadaBackend = nullptr;
extern UITask *wadaUi;
namespace
{
constexpr uint32_t broadcast = 0xffffffff;
constexpr uint32_t syncTimeout = 60000;
constexpr uint32_t deliveryTimeout = 15 * 60 * 1000;
static_assert(meshtastic_Routing_Error_NO_INTERFACE == 4 && meshtastic_Routing_Error_NO_CHANNEL == 6 &&
                  meshtastic_Routing_Error_TOO_LARGE == 7 && meshtastic_Routing_Error_DUTY_CYCLE_LIMIT == 9 &&
                  meshtastic_Routing_Error_BAD_REQUEST == 32 && meshtastic_Routing_Error_NOT_AUTHORIZED == 33 &&
                  meshtastic_Routing_Error_PKI_FAILED == 34 && meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY == 35 &&
                  meshtastic_Routing_Error_RATE_LIMIT_EXCEEDED == 38 && meshtastic_Routing_Error_PKI_SEND_FAIL_PUBLIC_KEY == 39,
              "WADAMESH delivery wording must follow the pinned Routing.Error schema");
template <class T> bool decode(const meshtastic_Data &data, const pb_msgdesc_t *descriptor, T &value)
{
    auto stream = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    return pb_decode(&stream, descriptor, &value);
}
void note(MeshBackendEvents *e, const char *text)
{
    if (e)
        e->beTerminalLine(text);
}
} // namespace
void WadaMeshtasticBackend::nodeKey(uint32_t node, uint8_t key[32])
{
    memset(key, 0, 32);
    for (unsigned i = 0; i < 4; ++i)
        key[i] = node >> (i * 8);
    memcpy(key + 4, "MSHT", 4);
}
uint32_t WadaMeshtasticBackend::keyNode(const uint8_t key[32])
{
    if (memcmp(key + 4, "MSHT", 4))
        return 0;
    return uint32_t(key[0]) | (uint32_t(key[1]) << 8) | (uint32_t(key[2]) << 16) | (uint32_t(key[3]) << 24);
}
uint32_t WadaMeshtasticBackend::newId()
{
    if (!++nextId)
        ++nextId;
    return nextId;
}
bool WadaMeshtasticBackend::submit(meshtastic_ToRadio &to)
{
    return client.isConnected() && client.send(std::move(to));
}
void WadaMeshtasticBackend::begin(MeshBackendEvents &ev, NodePrefs &initial, uint8_t key[32])
{
    events = &ev;
    prefs = initial;
    nextId = esp_random();
    memset(key, 0, 32);
    loadTelemetry();
    loadNodeState();
    client.init();
    requestConfig(millis());
}
void WadaMeshtasticBackend::requestConfig(uint32_t now)
{
    cancelNodeRemovals("Removal not confirmed; refreshing node list");
    traceFailure(traceId, "Connection or configuration is being refreshed");
    metadataKnown = false;
    statusConfigKnown = false;
    for (auto &status : statuses)
        status.seenInSnapshot = false;
    ++nodesRevision;
    meshtastic_ToRadio to = {};
    to.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    nonce = newId();
    if (nonce == 69420 || nonce == 69421)
        nonce = newId();
    to.want_config_id = nonce;
    if (submit(to)) {
        snapshotNodeSeen.fill(false);
        snapshotActive = true;
        syncPending = true;
        lastSync = now;
        events->beLinkState(false, "Synchronizing Meshtastic");
    }
}
void WadaMeshtasticBackend::loop(uint32_t now)
{
    saveTelemetry(now);
    saveNodeState(now);
    for (auto &request : telemetryRequests)
        if (request.id && Throttle::hasElapsed(request.started, 5 * 60 * 1000))
            request.id = 0;
    if (!client.isConnected()) {
        configured = false;
        client.connect();
        requestConfig(now);
    }
    for (unsigned budget = 0; budget < 12 && client.hasData(); ++budget)
        onRadio(client.receive());
    nodeActions();
    for (auto &request : explicitRemovals)
        if (request.id && Throttle::hasElapsed(request.started, 5 * 60 * 1000))
            explicitRemovalFailure(request.id, "No confirmation was received");
    for (auto &request : metadataRequests)
        if (request.id && Throttle::hasElapsed(request.started, 5 * 60 * 1000))
            request = {};
    if ((!configured && !syncPending) || (syncPending && Throttle::hasElapsed(lastSync, syncTimeout)))
        requestConfig(now);
    if (Throttle::hasElapsed(lastHeartbeat, 15000)) {
        meshtastic_ToRadio to = {};
        to.which_payload_variant = meshtastic_ToRadio_heartbeat_tag;
        to.heartbeat.nonce = 0;
        if (submit(to))
            lastHeartbeat = now;
    }
    if (traceId && Throttle::hasElapsed(traceAt, 180000)) {
        traceId = 0;
        if (wadaUi)
            wadaUi->onMeshtasticTrace("No response was received. The route may be unavailable or the reply may have been lost.");
        note(events, "Traceroute: no response");
    }
    for (auto &p : pending)
        if (p.id && (p.state == Delivery::Pending || (p.state == Delivery::Sent && p.expectAck)) &&
            Throttle::hasElapsed(p.started, deliveryTimeout))
            delivery(p.id, Delivery::Failed, WadaDelivery::TimedOut);
}
bool WadaMeshtasticBackend::busy() const
{
    for (const auto &p : pending)
        if (p.id && p.state == Delivery::Pending)
            return true;
    return syncPending;
}
meshtastic_NodeInfo *WadaMeshtasticBackend::findNode(uint32_t num, bool create)
{
    if (!num || num == broadcast)
        return nullptr;
    for (size_t i = 0; i < nodeCount; ++i)
        if (nodes[i].num == num) {
            if (snapshotActive && create)
                snapshotNodeSeen[i] = true;
            return &nodes[i];
        }
    if (!create || nodeCount == 256)
        return nullptr;
    if (snapshotActive)
        snapshotNodeSeen[nodeCount] = true;
    auto &n = nodes[nodeCount++];
    n = {};
    n.num = num;
    snprintf(n.user.id, sizeof(n.user.id), "!%08x", num);
    snprintf(n.user.long_name, sizeof(n.user.long_name), "Meshtastic %04x", num & 0xffff);
    snprintf(n.user.short_name, sizeof(n.user.short_name), "%04x", num & 0xffff);
    n.has_user = true;
    return &n;
}
void WadaMeshtasticBackend::onNode(const meshtastic_NodeInfo &n, bool full)
{
    auto *stored = findNode(n.num);
    if (!stored) {
        note(events, "Node mirror full (256); node unavailable in UI");
        return;
    }
    if (n.has_user) {
        uint8_t usable = 0;
        if (n.user.public_key.size == 32)
            for (auto byte : n.user.public_key.bytes)
                usable |= byte;
        const bool newKey = usable && (!stored->has_user || stored->user.public_key.size != 32 ||
                                       memcmp(stored->user.public_key.bytes, n.user.public_key.bytes, 32));
        const bool replacedWarning = nodeState.acceptedKey(n.num, usable ? n.user.public_key.bytes : nullptr, n.num == self);
        if (replacedWarning)
            nodeStateDirty = true;
        if (auto *meta = metadataFor(n.num)) {
            if (newKey || (replacedWarning && n.num != self)) {
                *meta = {};
                meta->node = n.num;
                for (auto &request : metadataRequests)
                    if (request.node == n.num)
                        request = {};
            }
            if (n.num == self)
                meta->mismatch = false;
        }
        stored->user = n.user;
        stored->has_user = true;
    }
    if (n.has_position) {
        stored->position = n.position;
        stored->has_position = true;
    } else if (full) {
        stored->position = {};
        stored->has_position = false;
    }
    if (n.has_device_metrics) {
        stored->device_metrics = n.device_metrics;
        stored->has_device_metrics = true;
    } else if (full) {
        stored->device_metrics = {};
        stored->has_device_metrics = false;
    }
    stored->last_heard = n.last_heard;
    stored->snr = n.snr;
    stored->is_favorite = n.is_favorite;
    stored->has_hops_away = n.has_hops_away;
    stored->hops_away = n.hops_away;
    stored->via_mqtt = n.via_mqtt;
    stored->has_xeddsa_signed = n.has_xeddsa_signed;
    stored->is_key_manually_verified = n.is_key_manually_verified;
    stored->is_ignored = n.is_ignored;
    stored->is_muted = n.is_muted;
    stored->heard_on_current_lora = n.heard_on_current_lora;
    ++nodesRevision;
    if (n.num == self) {
        owner = stored->user;
        updateSelf();
        return;
    }
    ContactInfo contact = {};
    nodeKey(n.num, contact.id.pub_key);
    snprintf(contact.name, sizeof(contact.name), "%s", stored->user.long_name[0] ? stored->user.long_name : stored->user.id);
    contact.type = ADV_TYPE_CHAT;
    contact.flags = n.is_favorite ? 1 : 0;
    contact.out_path_len = OUT_PATH_UNKNOWN;
    contact.lastmod = n.last_heard;
    contact.last_advert_timestamp = n.last_heard;
    if (stored->has_position) {
        contact.gps_lat = stored->position.latitude_i / 10;
        contact.gps_lon = stored->position.longitude_i / 10;
    }
    events->beContact(contact, false, stored->has_hops_away ? stored->hops_away : 0xff);
}
void WadaMeshtasticBackend::onChannel(const meshtastic_Channel &ch)
{
    if (ch.index < 0 || ch.index >= 8)
        return;
    channels[ch.index] = ch;
    ChannelDetails ui = {};
    if (ch.role != meshtastic_Channel_Role_DISABLED && ch.has_settings) {
        snprintf(ui.name, sizeof(ui.name), "%s [%d]",
                 ch.settings.name[0] ? ch.settings.name : wada::effectivePresetName(lora.modem_preset, lora.use_preset),
                 int(ch.index));
        uint8_t identity[32];
        if (wada::channelIdentity(channels, lora, ch.index, identity))
            wada::channelToken(identity, ch.index, ui.channel.secret);
    }
    events->beChannel(ch.index, ui);
}
void WadaMeshtasticBackend::updateSelf()
{
    if (!self)
        return;
    if (owner.long_name[0])
        snprintf(prefs.node_name, sizeof(prefs.node_name), "%s", owner.long_name);
    prefs.ble_pin = bluetooth.fixed_pin;
    prefs.tx_power_dbm = lora.tx_power;
    prefs.rx_boosted_gain = lora.sx126x_rx_boosted_gain;
    prefs.gps_enabled = positionConfig.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED;
    prefs.advert_loc_policy = positionConfig.position_broadcast_secs ? ADVERT_LOC_SHARE : ADVERT_LOC_NONE;
    uint8_t key[32];
    nodeKey(self, key);
    const auto *n = findNode(self, false);
    events->beSelfInfo(prefs, key, n && n->has_position ? n->position.latitude_i / 10 : 0,
                       n && n->has_position ? n->position.longitude_i / 10 : 0);
}
void WadaMeshtasticBackend::onRadio(const meshtastic_FromRadio &r)
{
    switch (r.which_payload_variant) {
    case meshtastic_FromRadio_region_presets_tag:
        regionPresets = r.region_presets;
        break;
    case meshtastic_FromRadio_my_info_tag:
        if (self && self != r.my_info.my_node_num) {
            configured = false;
            metadataKnown = false;
        }
        if ((self && self != r.my_info.my_node_num) || (nodeStateSelf && nodeStateSelf != r.my_info.my_node_num)) {
            peerMetadata = {};
            statuses = {};
            statusConfigKnown = false;
            metadataRequests = {};
            cancelNodeRemovals("Connected radio identity changed; node kept");
            cancelExplicitRemovals("Connected radio identity changed");
        }
        self = r.my_info.my_node_num;
        if (nodeStateSelf && nodeStateSelf != self) {
            nodeState = {};
            nodeStateDirty = true;
        }
        nodeStateSelf = self;
        nodeStateDirty |= nodeState.forget(self);
        ++nodesRevision;
        updateSelf();
        break;
    case meshtastic_FromRadio_metadata_tag:
        snprintf(metadataVersion, sizeof(metadataVersion), "%s", r.metadata.firmware_version);
        metadataKnown = true;
        excludedModules = r.metadata.excluded_modules;
        ++nodesRevision;
        break;
    case meshtastic_FromRadio_node_info_tag:
        if (r.id == wada::NodeGone)
            forgetNode(r.node_info.num, confirmExplicitRemoval(r.node_info.num));
        else
            onNode(r.node_info, r.id != wada::NodeDelta);
        break;
    case meshtastic_FromRadio_channel_tag:
        onChannel(r.channel);
        break;
    case meshtastic_FromRadio_packet_tag:
        onPacket(r.packet);
        break;
    case meshtastic_FromRadio_config_complete_id_tag:
        if (r.config_complete_id == nonce) {
            snapshotActive = false;
            size_t kept = 0;
            for (size_t i = 0; i < nodeCount; ++i) {
                if (snapshotNodeSeen[i] || nodes[i].num == self)
                    nodes[kept++] = nodes[i];
                else {
                    uint8_t key[32];
                    nodeKey(nodes[i].num, key);
                    events->beContactRemoved(key);
                }
            }
            nodeCount = kept;
            finishStatusSnapshot();
            finishExplicitRemovals();
            configured = true;
            syncPending = false;
            ++nodesRevision;
            updateSelf();
            events->beLinkState(true, "Meshtastic ready");
        }
        break;
    case meshtastic_FromRadio_moduleConfig_tag:
        if (r.moduleConfig.which_payload_variant == meshtastic_ModuleConfig_statusmessage_tag && self) {
            statusConfigKnown = true;
            onStatus(self, r.moduleConfig.payload_variant.statusmessage.node_status);
        }
        break;
    case meshtastic_FromRadio_config_tag:
        switch (r.config.which_payload_variant) {
        case meshtastic_Config_security_tag:
            managed = r.config.payload_variant.security.is_managed;
            break;
        case meshtastic_Config_device_tag:
            deviceConfig = r.config.payload_variant.device;
            break;
        case meshtastic_Config_lora_tag:
            lora = r.config.payload_variant.lora;
            haveLoRa = true;
            for (const auto &ch : channels)
                if (ch.has_settings)
                    onChannel(ch);
            break;
        case meshtastic_Config_bluetooth_tag:
            bluetooth = r.config.payload_variant.bluetooth;
            break;
        case meshtastic_Config_network_tag:
            network = r.config.payload_variant.network;
            break;
        case meshtastic_Config_position_tag:
            positionConfig = r.config.payload_variant.position;
            break;
        default:
            break;
        }
        updateSelf();
        break;
    case meshtastic_FromRadio_queueStatus_tag:
        if (r.queueStatus.res) {
            // QueueStatus.res is an internal ErrorCode, not a Routing.Error enum.
            delivery(r.queueStatus.mesh_packet_id, Delivery::Failed);
            telemetryFailure(r.queueStatus.mesh_packet_id, "Radio queue rejected the request");
            nodeRequestFailure(r.queueStatus.mesh_packet_id, "Radio queue rejected the request");
            explicitRemovalFailure(r.queueStatus.mesh_packet_id, "Radio queue rejected the request");
            traceFailure(r.queueStatus.mesh_packet_id, "Radio queue rejected the request");
        } else {
            for (const auto &p : pending)
                if (p.id == r.queueStatus.mesh_packet_id) {
                    delivery(p.id, p.expectAck ? Delivery::Sent : Delivery::Unconfirmed);
                    break;
                }
        }
        break;
    case meshtastic_FromRadio_clientNotification_tag:
        if (nodeNotification(r.clientNotification))
            break;
        note(events, r.clientNotification.message);
        if (wadaUi && !strncmp(r.clientNotification.message, "Node info:", 10))
            wadaUi->showAlert(r.clientNotification.message, 2500);
        // Routing NAKs carry the authoritative reason; notifications may precede a deferred retry.
        if (r.clientNotification.has_reply_id && r.clientNotification.level >= meshtastic_LogRecord_Level_ERROR) {
            delivery(r.clientNotification.reply_id, Delivery::Failed,
                     strstr(r.clientNotification.message, "Channel changed before send") ? WadaDelivery::ChannelChanged : 0);
            telemetryFailure(r.clientNotification.reply_id, r.clientNotification.message);
            nodeRequestFailure(r.clientNotification.reply_id, r.clientNotification.message);
            explicitRemovalFailure(r.clientNotification.reply_id, r.clientNotification.message);
            traceFailure(r.clientNotification.reply_id, r.clientNotification.message);
        }
        if (strstr(r.clientNotification.message, "WADAMESH configuration changed")) {
            configured = false;
            syncPending = false;
        }
        if (strstr(r.clientNotification.message, "WADAMESH receive overflow")) {
            cancelNodeRemovals("Removal not confirmed; refreshing node list");
            cancelExplicitRemovals("Receive overflow; refreshing node list");
            for (auto &p : pending)
                if (p.id && (p.state == Delivery::Pending || p.state == Delivery::Sent))
                    delivery(p.id, Delivery::Failed);
            configured = false;
            syncPending = false;
        }
        break;
    case meshtastic_FromRadio_rebooted_tag:
        cancelNodeRemovals("Removal not confirmed after restart; refreshing node list");
        cancelExplicitRemovals("Node restarted before confirmation");
        traceFailure(traceId, "Node restarted before a response");
        configured = false;
        syncPending = false;
        break;
    default:
        break;
    }
}
bool WadaMeshtasticBackend::reusable(const Pending &p)
{
    return !p.id || p.state == Delivery::Delivered || p.state == Delivery::Failed || p.state == Delivery::Mesh ||
           p.state == Delivery::Unconfirmed || (p.state == Delivery::Relayed && Throttle::hasElapsed(p.started, deliveryTimeout));
}
WadaMeshtasticBackend::Pending *WadaMeshtasticBackend::availableSlot()
{
    Pending *oldest = nullptr;
    const uint32_t now = millis();
    for (auto &p : pending) {
        if (!p.id)
            return &p;
        if (reusable(p) && (!oldest || uint32_t(now - p.started) > uint32_t(now - oldest->started)))
            oldest = &p;
    }
    return oldest;
}
void WadaMeshtasticBackend::delivery(uint32_t id, Delivery state, uint8_t error)
{
    if (!id)
        return;
    for (auto &p : pending)
        if (p.id == id) {
            if (p.state == Delivery::Relayed && state == Delivery::Failed &&
                (error == 0 || error == WadaDelivery::TimedOut || error == meshtastic_Routing_Error_TIMEOUT ||
                 error == meshtastic_Routing_Error_MAX_RETRANSMIT || error == meshtastic_Routing_Error_NO_ROUTE ||
                 error == meshtastic_Routing_Error_GOT_NAK))
                return;
            const bool lateRelay =
                (state == Delivery::Mesh || state == Delivery::Relayed) &&
                (p.error == 0 || p.error == WadaDelivery::TimedOut || p.error == meshtastic_Routing_Error_TIMEOUT ||
                 p.error == meshtastic_Routing_Error_MAX_RETRANSMIT || p.error == meshtastic_Routing_Error_NO_ROUTE ||
                 p.error == meshtastic_Routing_Error_GOT_NAK);
            if (p.state == Delivery::Delivered || p.state == Delivery::Mesh ||
                (p.state == Delivery::Failed && state != Delivery::Delivered && !lateRelay &&
                 !(state == Delivery::Failed && error && !p.error)) ||
                (p.state == Delivery::Relayed && state == Delivery::Sent) ||
                (p.state == Delivery::Unconfirmed && state == Delivery::Sent))
                return;
            p.state = state;
            p.error = error;
            if (state == Delivery::Delivered)
                events->beAck(id);
            if (wadaUi)
                wadaUi->onMeshtasticDelivery(id, uint8_t(state), error);
            return;
        }
}
void WadaMeshtasticBackend::onPacket(const meshtastic_MeshPacket &p)
{
    if (p.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return;
    const auto &d = p.decoded;
    if (d.portnum == meshtastic_PortNum_ADMIN_APP && nodeAdmin(p))
        return;
    if (d.portnum == meshtastic_PortNum_ROUTING_APP) {
        meshtastic_Routing routing = {};
        if (decode(d, meshtastic_Routing_fields, routing) && routing.which_variant == meshtastic_Routing_error_reason_tag) {
            if (p.to == self && p.from && routing.error_reason != meshtastic_Routing_Error_NONE) {
                telemetryFailure(d.request_id, WadaDelivery::label(Delivery::Failed, uint8_t(routing.error_reason)));
                nodeRequestFailure(d.request_id, WadaDelivery::label(Delivery::Failed, uint8_t(routing.error_reason)));
                explicitRemovalFailure(d.request_id, WadaDelivery::label(Delivery::Failed, uint8_t(routing.error_reason)));
                traceFailure(d.request_id, WadaDelivery::label(Delivery::Failed, uint8_t(routing.error_reason)));
            }
            if (routing.error_reason == meshtastic_Routing_Error_NONE && p.from == self && p.to == self)
                for (auto &request : explicitRemovals)
                    if (request.id && request.id == d.request_id) {
                        request.acknowledged = true;
                        configured = false;
                        syncPending = false;
                    }
            for (const auto &sent : pending)
                if (sent.id == d.request_id && p.to == self && p.from) {
                    if (routing.error_reason != meshtastic_Routing_Error_NONE)
                        delivery(d.request_id, Delivery::Failed, uint8_t(routing.error_reason));
                    else if (p.from == sent.to && sent.to != broadcast &&
                             p.ack_proof_status != meshtastic_MeshPacket_AckProofStatus_ACK_PROOF_INVALID)
                        delivery(d.request_id, Delivery::Delivered);
                    else if (p.from == self && sent.expectAck)
                        delivery(d.request_id, sent.to == broadcast ? Delivery::Mesh : Delivery::Relayed);
                    break;
                }
        }
        return;
    }
    if (d.portnum == meshtastic_PortNum_NODE_STATUS_APP) {
        meshtastic_StatusMessage status{};
        if (p.from && p.from != broadcast && (p.to == self || p.to == broadcast) &&
            decode(d, meshtastic_StatusMessage_fields, status)) {
            // Config is the connected node's authoritative edit value once it has been read.
            if (p.from != self || !statusConfigKnown)
                onStatus(p.from, status.status);
        }
        return;
    }
    if (p.id) {
        for (const auto &s : seen)
            if (s.from == p.from && s.id == p.id)
                return;
        seen[seenNext++ % seen.size()] = {p.from, p.id};
    }
    auto *node = findNode(p.from);
    if (!node)
        return;
    node->last_heard = p.has_rx_time ? p.rx_time : time(nullptr);
    node->via_mqtt = p.via_mqtt;
    const bool knownHops = !p.via_mqtt && p.hop_start >= p.hop_limit && (p.hop_start > 0 || d.has_bitfield);
    node->has_hops_away = knownHops;
    if (knownHops)
        node->hops_away = p.hop_start - p.hop_limit;
    // rx_snr measures our last RF link, not the originating node through relays.
    if (knownHops && node->hops_away == 0 && !p.via_mqtt)
        node->snr = p.rx_snr;
    const auto snr = p.via_mqtt ? INT8_MIN : int8_t(fmax(-127, fmin(127, p.rx_snr * 4)));
    const auto rssi = !p.via_mqtt && p.has_rx_rssi ? int8_t(std::clamp(int(p.rx_rssi), -128, 127)) : int8_t(0);
    if (p.from != self) {
        ++received;
        if (!p.via_mqtt)
            events->beSignal(snr, rssi);
    }
    if (d.portnum == meshtastic_PortNum_NODEINFO_APP) {
        meshtastic_User user = {};
        if (decode(d, meshtastic_User_fields, user)) {
            // NodeDB remains the authority for accepted keys. A raw NodeInfo
            // without a key cannot clear one that the native snapshot supplied.
            const auto heldKey = node->user.public_key;
            uint8_t incoming = 0, held = 0;
            if (user.public_key.size == 32)
                for (auto byte : user.public_key.bytes)
                    incoming |= byte;
            if (heldKey.size == 32)
                for (auto byte : heldKey.bytes)
                    held |= byte;
            if (p.from != self && incoming && held && memcmp(heldKey.bytes, user.public_key.bytes, 32))
                noteKeyMismatch(p.from, heldKey.bytes);
            // Accepted User fields arrive in native NodeInfo snapshots, never from a refused claim.
            onNode(*node);
        }
    } else if (d.portnum == meshtastic_PortNum_POSITION_APP) {
        meshtastic_Position pos = {};
        if (decode(d, meshtastic_Position_fields, pos)) {
            if (!pos.has_latitude_i || !pos.has_longitude_i || pos.latitude_i < -900000000 || pos.latitude_i > 900000000 ||
                pos.longitude_i < -1800000000 || pos.longitude_i > 1800000000)
                return;
            node->position = pos;
            node->has_position = true;
            onNode(*node);
            if (p.from == self)
                events->beSelfPosition(pos.latitude_i / 10, pos.longitude_i / 10);
        }
    } else if (d.portnum == meshtastic_PortNum_TELEMETRY_APP) {
        if (d.want_response)
            return;
        meshtastic_Telemetry telemetry = {};
        if (decode(d, meshtastic_Telemetry_fields, telemetry)) {
            wada::TelemetryKind kind;
            if (!wada::telemetryKind(telemetry.which_variant, kind))
                return;
            // NodeDB replays and unsolicited broadcasts are useful readings too.
            if (telemetryHistory.append(p.from, p.has_rx_time ? p.rx_time : 0, telemetry)) {
                telemetryDirty = true;
                if (wadaUi)
                    wadaUi->onMeshtasticTelemetry(p.from, uint8_t(kind));
            }
            for (auto &request : telemetryRequests)
                if (request.id == d.request_id && request.node == p.from && request.kind == kind && p.to == self)
                    request.id = 0;
            if (telemetry.which_variant == meshtastic_Telemetry_device_metrics_tag) {
                node->device_metrics = telemetry.variant.device_metrics;
                node->has_device_metrics = true;
                onNode(*node);
            }
        }
    } else if (d.portnum == meshtastic_PortNum_TRACEROUTE_APP && d.request_id) {
        meshtastic_RouteDiscovery route = {};
        if (decode(d, meshtastic_RouteDiscovery_fields, route)) {
            if (d.request_id != traceId || p.from != traceTo || p.to != self) {
                note(events, "Observed traceroute response (not requested by this display)");
                return;
            }
            char report[1536];
            size_t at = 0;
            auto append = [&](const char *format, ...) {
                if (at >= sizeof report - 1)
                    return;
                va_list args;
                va_start(args, format);
                const int n = vsnprintf(report + at, sizeof report - at, format, args);
                va_end(args);
                if (n > 0)
                    at += std::min(size_t(n), sizeof report - at - 1);
            };
            auto hop = [&](uint32_t number, int snr, bool reported) {
                const auto *n = nodeInfo(number);
                if (number == broadcast)
                    append("Unknown hop");
                else if (n && n->has_user && n->user.long_name[0])
                    append("%s (!%08x)", n->user.long_name, number);
                else
                    append("!%08x", number);
                if (reported && snr != INT8_MIN)
                    append("  SNR %.2f dB\n", double(snr) / 4.0);
                else
                    append("  Signal unknown\n");
            };
            append("Outbound\n");
            for (pb_size_t i = 0; i < route.route_count; ++i)
                hop(route.route[i], i < route.snr_towards_count ? route.snr_towards[i] : 0, i < route.snr_towards_count);
            hop(traceTo, route.snr_towards_count ? route.snr_towards[route.snr_towards_count - 1] : 0,
                route.snr_towards_count > route.route_count);
            append("\nReturn\n");
            for (pb_size_t i = 0; i < route.route_back_count; ++i)
                hop(route.route_back[i], i < route.snr_back_count ? route.snr_back[i] : 0, i < route.snr_back_count);
            hop(self, route.snr_back_count ? route.snr_back[route.snr_back_count - 1] : 0,
                route.snr_back_count > route.route_back_count);
            append("\nRound trip %.2f s", double(uint32_t(millis() - traceAt)) / 1000.0);
            traceId = 0;
            if (wadaUi)
                wadaUi->onMeshtasticTrace(report);
            note(events, report);
        }
    } else if (d.portnum == meshtastic_PortNum_ADMIN_APP && p.from == self) {
        meshtastic_AdminMessage admin = {};
        if (decode(d, meshtastic_AdminMessage_fields, admin)) {
            if (admin.which_payload_variant == meshtastic_AdminMessage_get_config_response_tag) {
                meshtastic_FromRadio r = {};
                r.which_payload_variant = meshtastic_FromRadio_config_tag;
                r.config = admin.get_config_response;
                onRadio(r);
            } else if (admin.which_payload_variant == meshtastic_AdminMessage_get_channel_response_tag)
                onChannel(admin.get_channel_response);
            else if (admin.which_payload_variant == meshtastic_AdminMessage_get_owner_response_tag) {
                owner = admin.get_owner_response;
                updateSelf();
            }
        }
    } else if (d.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        if (p.to != broadcast && p.to != self && p.from != self)
            return;
        if (p.channel >= 8)
            return;
        char text[234];
        const size_t length = std::min(size_t(d.payload.size), sizeof(text) - 1);
        memcpy(text, d.payload.bytes, length);
        text[length] = 0;
        onNode(*node);
        uint8_t key[32];
        nodeKey(p.from == self ? p.to : p.from, key);
        const uint32_t ts = p.has_rx_time ? p.rx_time : time(nullptr);
        if (p.to == broadcast && !configured) {
            char unsynced[300];
            snprintf(unsynced, sizeof unsynced, "%s: %s", node->user.long_name, text);
            if (wadaUi)
                wadaUi->onMeshtasticUnsyncedChannel(unsynced, p.from == self, p.id);
            return;
        }
        if (p.from == self) {
            for (const auto &sent : pending)
                if (sent.id == p.id)
                    return;
            if (auto *slot = availableSlot())
                *slot = {p.id, p.to, millis(), Delivery::Pending, p.want_ack};
            if (p.to == broadcast)
                events->beOutgoingChannel(p.channel, text, p.id);
            else {
                auto *dest = findNode(p.to);
                if (dest) {
                    onNode(*dest);
                    events->beOutgoingDirect(key, text, p.id);
                }
            }
        } else if (p.to == broadcast) {
            char labeled[280];
            snprintf(labeled, sizeof(labeled), "%s: %s", node->user.long_name, text);
            events->beChannelMessage(p.channel, ts, labeled, node->has_hops_away ? node->hops_away : 0xff, snr, rssi);
        } else
            events->beDirectMessage(key, ts, text, node->has_hops_away ? node->hops_away : 0xff,
                                    node->has_hops_away && node->hops_away > 0, snr, rssi);
    }
}
uint32_t WadaMeshtasticBackend::sendData(uint32_t to, uint8_t channel, meshtastic_PortNum port, const uint8_t *bytes, size_t len,
                                         bool ack, bool response, uint32_t hopLimit)
{
    if (!configured || !to || channel >= 8 || len > sizeof(meshtastic_Data{}.payload.bytes))
        return 0;
    meshtastic_ToRadio tr = {};
    tr.which_payload_variant = meshtastic_ToRadio_packet_tag;
    auto &p = tr.packet;
    p.to = to;
    p.channel = channel;
    p.id = newId();
    p.want_ack = ack;
    p.hop_limit = hopLimit;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = port;
    p.decoded.want_response = response;
    p.decoded.payload.size = len;
    memcpy(p.decoded.payload.bytes, bytes, len);
    const uint32_t id = p.id;
    if (to == broadcast && port == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        uint8_t identity[32];
        if (!channelIdentity(channel, identity) || !wada::rememberChannelIntent(id, channel, identity))
            return 0;
    }
    if (!submit(tr)) {
        wada::forgetChannelIntent(id);
        return 0;
    }
    if (port == meshtastic_PortNum_TEXT_MESSAGE_APP)
        seen[seenNext++ % seen.size()] = {self, id};
    ++transmitted;
    return id;
}
int WadaMeshtasticBackend::sendDirect(const ContactInfo &c, uint32_t, const char *text, uint32_t &ack, uint32_t &timeout,
                                      uint32_t &fp)
{
    ack = fp = timeout = 0;
    const uint32_t to = keyNode(c.id.pub_key);
    if (!to || to == self || to == broadcast || !text || !text[0])
        return MSG_SEND_FAILED;
    Pending *slot = availableSlot();
    if (!canMessage(to)) {
        note(events, directMessageReason(to));
        return MSG_SEND_FAILED;
    }
    if (!slot)
        return MSG_SEND_FAILED;
    const size_t len = strlen(text);
    if (len > 233)
        return MSG_SEND_FAILED;
    const uint32_t id = sendData(to, 0, meshtastic_PortNum_TEXT_MESSAGE_APP, reinterpret_cast<const uint8_t *>(text), len, true);
    if (!id)
        return MSG_SEND_FAILED;
    *slot = {id, to, millis(), Delivery::Pending};
    ack = fp = id;
    timeout = deliveryTimeout;
    return MSG_SEND_SENT_FLOOD;
}
bool WadaMeshtasticBackend::sendChannel(int index, const ChannelDetails &ui, uint32_t, const char *, const char *text,
                                        uint32_t &fp)
{
    fp = 0;
    if (index < 0 || index >= 8 || channels[index].role == meshtastic_Channel_Role_DISABLED || !text || !text[0] ||
        strlen(text) > 233)
        return false;
    Pending *slot = availableSlot();
    if (!slot)
        return false;
    uint8_t identity[32], token[16];
    if (!channelIdentity(index, identity))
        return false;
    wada::channelToken(identity, index, token);
    if (memcmp(token, ui.channel.secret, sizeof token))
        return false;
    fp = sendData(broadcast, index, meshtastic_PortNum_TEXT_MESSAGE_APP, reinterpret_cast<const uint8_t *>(text), strlen(text),
                  true);
    if (fp)
        *slot = {fp, broadcast, millis(), Delivery::Pending};
    return fp != 0;
}
bool WadaMeshtasticBackend::sendAdvert(bool flood)
{
    return sendNodeInfo(!flood, false);
}
bool WadaMeshtasticBackend::sendNodeInfo(bool oneHop, bool requestReplies)
{
    uint8_t encoded[233];
    auto stream = pb_ostream_from_buffer(encoded, sizeof(encoded));
    return pb_encode(&stream, meshtastic_User_fields, &owner) &&
           sendData(broadcast, 0, meshtastic_PortNum_NODEINFO_APP, encoded, stream.bytes_written, false, requestReplies,
                    oneHop ? 1 : 0);
}
bool WadaMeshtasticBackend::setOwner(const char *longName, const char *shortName)
{
    if (!longName || !*longName || !wada::statusText(longName, wada::longNameMaxBytes, true) ||
        !wada::statusText(shortName, sizeof(owner.short_name) - 1, true)) {
        note(events, "Use valid single-line names: long name up to 24 UTF-8 bytes, short name up to 4.");
        return false;
    }
    meshtastic_AdminMessage a = {};
    a.which_payload_variant = meshtastic_AdminMessage_set_owner_tag;
    a.set_owner = owner;
    snprintf(a.set_owner.long_name, sizeof(a.set_owner.long_name), "%s", longName);
    snprintf(a.set_owner.short_name, sizeof(a.set_owner.short_name), "%s", shortName);
    return sendAdmin(a);
}
bool WadaMeshtasticBackend::sendAdmin(const meshtastic_AdminMessage &admin)
{
    if (managed) {
        note(events, "Node is managed. Use an authorized Meshtastic remote administrator for configuration.");
        return false;
    }

    uint8_t encoded[233];
    auto stream = pb_ostream_from_buffer(encoded, sizeof(encoded));
    return pb_encode(&stream, meshtastic_AdminMessage_fields, &admin) &&
           sendData(self, 0, meshtastic_PortNum_ADMIN_APP, encoded, stream.bytes_written, true);
}
bool WadaMeshtasticBackend::setConfig(const meshtastic_Config &config)
{
    if (config.which_payload_variant == meshtastic_Config_lora_tag) {
        const auto &settings = config.payload_variant.lora;
        const bool unsupportedWide = !settings.use_preset && settings.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24 &&
                                     settings.bandwidth != 0 && settings.bandwidth != 200 && settings.bandwidth != 400 &&
                                     settings.bandwidth != 800;
        if (!wada::supportedRegion(settings.region) || unsupportedWide ||
            (settings.use_preset && !validPreset(settings.region, settings.modem_preset)) ||
            (regionLicensedOnly(settings.region) && !owner.is_licensed))
            return false;
    }
    meshtastic_AdminMessage a = {};
    a.which_payload_variant = meshtastic_AdminMessage_set_config_tag;
    a.set_config = config;
    if (a.set_config.which_payload_variant == meshtastic_Config_device_tag &&
        a.set_config.payload_variant.device.node_info_broadcast_secs == 0)
        a.set_config.payload_variant.device.node_info_broadcast_secs = wada::nodeInfoDefaultSeconds;
    return sendAdmin(a);
}
void WadaMeshtasticBackend::prefsChanged(const NodePrefs &next)
{
    if (!configured) {
        note(events, "Meshtastic configuration is still loading");
        return;
    }
    if (strcmp(next.node_name, prefs.node_name)) {
        meshtastic_AdminMessage a = {};
        a.which_payload_variant = meshtastic_AdminMessage_set_owner_tag;
        a.set_owner = owner;
        snprintf(a.set_owner.long_name, sizeof(a.set_owner.long_name), "%s", next.node_name);
        if (!sendAdmin(a))
            note(events, "Node name update could not be queued");
    }
    if (next.tx_power_dbm != prefs.tx_power_dbm || next.rx_boosted_gain != prefs.rx_boosted_gain) {
        meshtastic_Config c = {};
        c.which_payload_variant = meshtastic_Config_lora_tag;
        c.payload_variant.lora = lora;
        c.payload_variant.lora.tx_power = next.tx_power_dbm;
        c.payload_variant.lora.sx126x_rx_boosted_gain = next.rx_boosted_gain;
        if (!setConfig(c))
            note(events, "LoRa configuration could not be queued");
    }
    if (next.freq != prefs.freq || next.bw != prefs.bw || next.sf != prefs.sf || next.cr != prefs.cr)
        note(events, "Use Meshtastic region/modem preset settings; MeshCore RF presets are unavailable");
    prefs = next;
}
void WadaMeshtasticBackend::channelChanged(int idx, const ChannelDetails &ui)
{
    if (idx < 0 || idx >= 8)
        return;
    // The WADAMESH secret editor cannot represent Meshtastic 0/1/16/32-byte PSKs.
    note(events, "Use native Meshtastic channel configuration; existing PSKs were preserved");
    onChannel(channels[idx]);
    (void)ui;
}
void WadaMeshtasticBackend::advertPositionChanged(int32_t lat, int32_t lon)
{
    meshtastic_AdminMessage a = {};
    a.which_payload_variant = meshtastic_AdminMessage_set_fixed_position_tag;
    a.set_fixed_position.has_latitude_i = true;
    a.set_fixed_position.latitude_i = lat * 10;
    a.set_fixed_position.has_longitude_i = true;
    a.set_fixed_position.longitude_i = lon * 10;
    sendAdmin(a);
}
bool WadaMeshtasticBackend::contactChanged(const ContactInfo &c)
{
    meshtastic_AdminMessage a = {};
    a.which_payload_variant =
        c.isFav() ? meshtastic_AdminMessage_set_favorite_node_tag : meshtastic_AdminMessage_remove_favorite_node_tag;
    if (c.isFav())
        a.set_favorite_node = keyNode(c.id.pub_key);
    else
        a.remove_favorite_node = keyNode(c.id.pub_key);
    const bool accepted = sendAdmin(a);
    if (accepted && c.isFav())
        protectNodeRemoval(keyNode(c.id.pub_key));
    return accepted;
}
bool WadaMeshtasticBackend::contactRemoved(const uint8_t key[32])
{
    const uint32_t number = keyNode(key);
    if (!configured || managed || !number || number == self || number == broadcast || !nodeInfo(number))
        return false;
    ExplicitRemoval *slot = nullptr;
    for (auto &request : explicitRemovals) {
        if (request.id && request.node == number)
            return true;
        if (!request.id && !slot)
            slot = &request;
    }
    if (!slot)
        return false;
    meshtastic_AdminMessage a{};
    a.which_payload_variant = meshtastic_AdminMessage_remove_by_nodenum_tag;
    a.remove_by_nodenum = number;
    uint8_t bytes[233];
    auto stream = pb_ostream_from_buffer(bytes, sizeof bytes);
    if (!pb_encode(&stream, meshtastic_AdminMessage_fields, &a))
        return false;
    const uint32_t id = sendData(self, 0, meshtastic_PortNum_ADMIN_APP, bytes, stream.bytes_written, true);
    if (!id)
        return false;
    *slot = {id, number, millis(), false};
    return true;
}
bool WadaMeshtasticBackend::setBlePin(uint32_t pin)
{
    meshtastic_Config c = {};
    c.which_payload_variant = meshtastic_Config_bluetooth_tag;
    c.payload_variant.bluetooth = bluetooth;
    c.payload_variant.bluetooth.fixed_pin = pin;
    c.payload_variant.bluetooth.mode =
        pin ? meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN : meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
    return setConfig(c);
}
int WadaMeshtasticBackend::requestTelemetry(const ContactInfo &c)
{
    return requestTelemetry(keyNode(c.id.pub_key), wada::TelemetryKind::Device) ? MSG_SEND_SENT_FLOOD : MSG_SEND_FAILED;
}
bool WadaMeshtasticBackend::telemetryAvailable(uint32_t node, wada::TelemetryKind kind) const
{
    const auto *entry = nodeInfo(node);
    return entry && wada::telemetryTag(kind) &&
           (kind != wada::TelemetryKind::Host || (entry->has_user && entry->user.hw_model == meshtastic_HardwareModel_PORTDUINO));
}
void WadaMeshtasticBackend::telemetryStatus(const TelemetryRequest &request, const char *reason)
{
    const auto *entry = nodeInfo(request.node);
    char node[56];
    snprintf(node, sizeof(node), "%s (!%08x)", entry ? entry->user.long_name : "Node", request.node);
    char message[300];
    if (reason)
        snprintf(message, sizeof(message), "%s request to %s failed: %s", wada::telemetryName(request.kind), node, reason);
    else
        snprintf(message, sizeof(message), "Requesting %s from %s", wada::telemetryName(request.kind), node);
    note(events, message);
    if (wadaUi)
        wadaUi->onMeshtasticTelemetryStatus(request.node, uint8_t(request.kind), message);
}
void WadaMeshtasticBackend::telemetryFailure(uint32_t id, const char *reason)
{
    if (!id)
        return;
    for (auto &request : telemetryRequests)
        if (request.id == id) {
            telemetryStatus(request, reason);
            request.id = 0;
            return;
        }
}
bool WadaMeshtasticBackend::requestTelemetry(uint32_t node, wada::TelemetryKind kind)
{
    TelemetryRequest context{0, node, millis(), kind};
    if (!configured || !telemetryAvailable(node, kind)) {
        telemetryStatus(context, "Unavailable; wait for synchronization and check the node type");
        return false;
    }
    TelemetryRequest *slot = nullptr;
    for (auto &request : telemetryRequests)
        if (!request.id || Throttle::hasElapsed(request.started, 5 * 60 * 1000)) {
            slot = &request;
            break;
        }
    if (!slot) {
        telemetryStatus(context, "Too many outstanding requests; wait before trying again");
        return false;
    }
    meshtastic_Telemetry t = {};
    t.which_variant = wada::telemetryTag(kind);
    uint8_t encoded[64];
    auto s = pb_ostream_from_buffer(encoded, sizeof(encoded));
    if (!pb_encode(&s, meshtastic_Telemetry_fields, &t))
        return false;
    const uint32_t id = sendData(node, primaryIndex(), meshtastic_PortNum_TELEMETRY_APP, encoded, s.bytes_written, true, true);
    if (!id) {
        telemetryStatus(context, "Could not queue the request");
        return false;
    }
    context.id = id;
    *slot = context;
    telemetryStatus(context, nullptr);
    return true;
}
void WadaMeshtasticBackend::loadTelemetry()
{
    std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[wada::TelemetryLog::MaxBytes]);
    std::unique_ptr<wada::TelemetryLog> candidate(new (std::nothrow) wada::TelemetryLog);
    if (!bytes || !candidate)
        return;
    bool have = false;
    for (unsigned slot = 0; slot < 2; ++slot) {
        const size_t size = wadaTelemetryRead(slot, bytes.get(), wada::TelemetryLog::MaxBytes);
        uint32_t generation;
        if (candidate->restore(bytes.get(), size, generation) && (!have || int32_t(generation - telemetryGeneration) > 0)) {
            telemetryHistory = *candidate;
            telemetryGeneration = generation;
            telemetrySlot = slot;
            have = true;
        }
    }
}
void WadaMeshtasticBackend::saveTelemetry(uint32_t now)
{
    if (!telemetryDirty || !Throttle::hasElapsed(lastTelemetrySave, 10000))
        return;
    lastTelemetrySave = now;
    std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[wada::TelemetryLog::MaxBytes]);
    if (!bytes)
        return;
    const uint32_t generation = telemetryGeneration + 1;
    const size_t size = telemetryHistory.encode(bytes.get(), wada::TelemetryLog::MaxBytes, generation);
    if (size && wadaTelemetryWrite(1 - telemetrySlot, bytes.get(), size)) {
        telemetryGeneration = generation;
        telemetrySlot = 1 - telemetrySlot;
        telemetryDirty = false;
    } else {
        note(events, "Recent telemetry could not be saved; check UI storage space");
    }
}
uint32_t WadaMeshtasticBackend::tracePing(const uint8_t key[32])
{
    const uint32_t number = keyNode(key);
    if (!number || number == self || number == broadcast || !configured)
        return 0;
    if (lastTrace && !Throttle::hasElapsed(lastTrace, 30000)) {
        note(events, "Traceroute is rate limited. Wait 30 seconds between requests.");
        if (wadaUi)
            wadaUi->showAlert("Traceroute: wait 30 seconds between requests", 2500);
        return 0;
    }
    meshtastic_RouteDiscovery route = {};
    uint8_t encoded[233];
    auto s = pb_ostream_from_buffer(encoded, sizeof(encoded));
    const uint32_t id = pb_encode(&s, meshtastic_RouteDiscovery_fields, &route)
                            ? sendData(number, 0, meshtastic_PortNum_TRACEROUTE_APP, encoded, s.bytes_written, true, true)
                            : 0;
    if (id) {
        traceId = id;
        traceTo = number;
        traceAt = millis();
        lastTrace = traceAt ? traceAt : 1;
        note(events, "Traceroute: waiting for response");
    }
    return id;
}
bool WadaMeshtasticBackend::power(bool shutdown)
{
    meshtastic_AdminMessage a = {};
    a.which_payload_variant =
        shutdown ? meshtastic_AdminMessage_shutdown_seconds_tag : meshtastic_AdminMessage_reboot_seconds_tag;
    if (shutdown)
        a.shutdown_seconds = 2;
    else
        a.reboot_seconds = 2;
    return sendAdmin(a);
}
const meshtastic_NodeInfo *WadaMeshtasticBackend::nodeInfo(uint32_t number) const
{
    for (size_t i = 0; i < nodeCount; ++i)
        if (nodes[i].num == number)
            return &nodes[i];
    return nullptr;
}
const char *WadaMeshtasticBackend::directMessageReason(uint32_t number) const
{
    if (!number || number == self || number == broadcast)
        return "Choose another Meshtastic node";
    const auto *n = nodeInfo(number);
    if (n && n->has_user && n->user.has_is_unmessagable && n->user.is_unmessagable)
        return "This node does not accept messages";
    if (owner.is_licensed)
        return nullptr; // Native licensed mode sends cleartext.
    if (!n || !n->has_user || n->user.public_key.size != 32)
        return "No public key received. Use Exchange user info before messaging.";
    uint8_t nonzero = 0;
    for (auto byte : n->user.public_key.bytes)
        nonzero |= byte;
    return nonzero ? nullptr : "No public key received. Use Exchange user info before messaging.";
}
bool WadaMeshtasticBackend::canMessage(uint32_t number) const
{
    return directMessageReason(number) == nullptr;
}
bool WadaMeshtasticBackend::requestUserInfo(uint32_t number)
{
    if (!configured || !number || number == self || number == broadcast)
        return false;
    uint8_t encoded[meshtastic_User_size];
    auto stream = pb_ostream_from_buffer(encoded, sizeof encoded);
    return pb_encode(&stream, meshtastic_User_fields, &owner) &&
           sendData(number, 0, meshtastic_PortNum_NODEINFO_APP, encoded, stream.bytes_written, false, true);
}
bool WadaMeshtasticBackend::channelIdentity(unsigned index, uint8_t out[32]) const
{
    return configured && haveLoRa && wada::channelIdentity(channels, lora, index, out);
}
const meshtastic_LoRaPresetGroup *WadaMeshtasticBackend::presetGroup(meshtastic_Config_LoRaConfig_RegionCode region) const
{
    for (pb_size_t i = 0; i < regionPresets.region_groups_count; ++i) {
        const auto &r = regionPresets.region_groups[i];
        if (r.region != region || r.group_index >= regionPresets.groups_count)
            continue;
        const auto &group = regionPresets.groups[r.group_index];
        return group.presets_count ? &group : nullptr;
    }
    return nullptr;
}
bool WadaMeshtasticBackend::validPreset(meshtastic_Config_LoRaConfig_RegionCode region,
                                        meshtastic_Config_LoRaConfig_ModemPreset preset) const
{
    if (unsigned(region) >= _meshtastic_Config_LoRaConfig_RegionCode_ARRAYSIZE ||
        unsigned(preset) >= _meshtastic_Config_LoRaConfig_ModemPreset_ARRAYSIZE)
        return false;
    if (region == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        return true;
    const auto *group = presetGroup(region);
    if (group) {
        for (pb_size_t j = 0; j < group->presets_count; ++j)
            if (group->presets[j] == preset)
                return true;
        return false;
    }
    // An absent or malformed map is unconstrained, per the pinned protobuf contract.
    return true;
}
bool WadaMeshtasticBackend::defaultPreset(meshtastic_Config_LoRaConfig_RegionCode region,
                                          meshtastic_Config_LoRaConfig_ModemPreset &preset) const
{
    const auto *group = presetGroup(region);
    if (!group)
        return false;
    if (validPreset(region, group->default_preset)) {
        preset = group->default_preset;
        return true;
    }
    for (pb_size_t i = 0; i < group->presets_count; ++i)
        if (validPreset(region, group->presets[i])) {
            preset = group->presets[i];
            return true;
        }
    return false;
}
bool WadaMeshtasticBackend::regionLicensedOnly(meshtastic_Config_LoRaConfig_RegionCode region) const
{
    const auto *group = presetGroup(region);
    return region != meshtastic_Config_LoRaConfig_RegionCode_UNSET && group && group->licensed_only;
}
meshtastic_Config_LoRaConfig_ModemPreset
WadaMeshtasticBackend::presetForRegionChange(meshtastic_Config_LoRaConfig_RegionCode previous,
                                             meshtastic_Config_LoRaConfig_RegionCode next,
                                             meshtastic_Config_LoRaConfig_ModemPreset current) const
{
    const bool fresh = previous == meshtastic_Config_LoRaConfig_RegionCode_UNSET && !presetGroup(previous) &&
                       current == meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    auto preferred = current;
    if (fresh && next == meshtastic_Config_LoRaConfig_RegionCode_US && presetGroup(next) &&
        validPreset(next, meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO))
        preferred = meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO;
    else if (fresh)
        defaultPreset(next, preferred);
    if (!validPreset(next, preferred))
        defaultPreset(next, preferred);
    return preferred;
}
bool WadaMeshtasticBackend::saveChannel(const meshtastic_Channel &channel)
{
    if (channel.index < 0 || channel.index >= 8)
        return false;
    meshtastic_AdminMessage a = {};
    a.which_payload_variant = meshtastic_AdminMessage_set_channel_tag;
    a.set_channel = channel;
    return sendAdmin(a);
}

namespace
{
bool shareUrl(const char *prefix, const uint8_t *bytes, size_t n, char *out, size_t capacity)
{
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const size_t prefixLength = strlen(prefix);
    const size_t encodedLength = (n * 8 + 5) / 6;
    if (capacity <= prefixLength + encodedLength)
        return false;
    memcpy(out, prefix, prefixLength);
    size_t at = prefixLength;
    uint32_t bits = 0;
    unsigned count = 0;
    for (size_t i = 0; i < n; ++i) {
        bits = (bits << 8) | bytes[i];
        count += 8;
        while (count >= 6) {
            count -= 6;
            out[at++] = alphabet[(bits >> count) & 63];
        }
    }
    if (count)
        out[at++] = alphabet[(bits << (6 - count)) & 63];
    out[at] = 0;
    return true;
}
} // namespace
bool WadaMeshtasticBackend::contactUrl(uint32_t node, char *out, size_t capacity) const
{
    meshtastic_SharedContact contact = {};
    contact.node_num = node;
    for (size_t i = 0; i < nodeCount; ++i)
        if (nodes[i].num == node) {
            contact.has_user = nodes[i].has_user;
            contact.user = nodes[i].user;
            break;
        }
    if (node == self) {
        contact.manually_verified = true;
        contact.has_user = true;
        contact.user = owner;
    }
    if (!contact.node_num || !contact.has_user || contact.user.public_key.size != 32)
        return false;
    uint8_t nonzero = 0;
    for (auto byte : contact.user.public_key.bytes)
        nonzero |= byte;
    if (!nonzero)
        return false;
    uint8_t bytes[meshtastic_SharedContact_size];
    auto stream = pb_ostream_from_buffer(bytes, sizeof(bytes));
    return pb_encode(&stream, meshtastic_SharedContact_fields, &contact) &&
           shareUrl("https://meshtastic.org/v/#", bytes, stream.bytes_written, out, capacity);
}
bool WadaMeshtasticBackend::channelUrl(unsigned slot, char *out, size_t capacity, bool add) const
{
    if (slot >= 8 || !channels[slot].has_settings || channels[slot].role == meshtastic_Channel_Role_DISABLED)
        return false;
    meshtastic_ChannelSet set = {};
    set.settings_count = 1;
    set.settings[0] = channels[slot].settings;
    const unsigned primary = primaryIndex();
    if (!add && slot != primary) {
        // Keep the primary name/key: it selects the automatic RF frequency slot.
        // The share UI explains that Replace shares both channels and both keys.
        if (!channels[primary].has_settings || channels[primary].role != meshtastic_Channel_Role_PRIMARY)
            return false;
        set.settings_count = 2;
        set.settings[0] = channels[primary].settings;
        set.settings[1] = channels[slot].settings;
    }
    set.has_lora_config = !add;
    if (!add)
        set.lora_config = lora;
    uint8_t bytes[meshtastic_ChannelSet_size];
    auto stream = pb_ostream_from_buffer(bytes, sizeof(bytes));
    return pb_encode(&stream, meshtastic_ChannelSet_fields, &set) &&
           shareUrl(add ? "https://meshtastic.org/e/?add=true#" : "https://meshtastic.org/e/#", bytes, stream.bytes_written, out,
                    capacity);
}
