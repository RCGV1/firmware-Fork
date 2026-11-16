#include "UptimeClock.h"
#include "WadaMeshtasticBackend.h"
#include "WadaNodeActions.h"
#include "mesh/Throttle.h"
#include "mesh/generated/meshtastic/portnums.pb.h"
#include "ui-touch/UITask.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <pb_decode.h>
#include <pb_encode.h>
extern UITask *wadaUi;

WadaMeshtasticBackend::PeerMetadata *WadaMeshtasticBackend::metadataFor(uint32_t number, bool create)
{
    PeerMetadata *free = nullptr;
    for (auto &entry : peerMetadata) {
        if (entry.node == number && number)
            return &entry;
        if (!free && (!entry.node || !nodeInfo(entry.node)))
            free = &entry;
    }
    if (!create || !number || !free)
        return nullptr;
    *free = {};
    free->node = number;
    return free;
}
bool WadaMeshtasticBackend::unheardOnCurrentLora(uint32_t number) const
{
    const auto *node = nodeInfo(number);
    return nodeSnapshotReady() && reportsHeardOnCurrentLora() && self && number != self && node && !node->via_mqtt &&
           !node->heard_on_current_lora;
}
bool WadaMeshtasticBackend::nodeKeyMismatch(uint32_t number) const
{
    if (number == self)
        return false;
    for (const auto &entry : peerMetadata)
        if (entry.node == number && entry.mismatch)
            return true;
    return nodeState.mismatch(number);
}
const char *WadaMeshtasticBackend::nodeFirmwareVersion(uint32_t number) const
{
    if (number == self)
        return metadataKnown ? metadataVersion : "";
    for (const auto &entry : peerMetadata)
        if (entry.node == number)
            return entry.version;
    return "";
}
bool WadaMeshtasticBackend::nodeSigned(uint32_t number) const
{
    const auto *node = nodeInfo(number);
    return node && node->has_xeddsa_signed;
}
bool WadaMeshtasticBackend::nodeEncrypted(uint32_t number) const
{
    const auto *node = nodeInfo(number);
    if (!node || !node->has_user || node->user.public_key.size != 32 || nodeKeyMismatch(number))
        return false;
    uint8_t any = 0;
    for (auto byte : node->user.public_key.bytes)
        any |= byte;
    return any;
}
wada::NodeSecurity WadaMeshtasticBackend::nodeSecurity(uint32_t number) const
{
    const auto *node = nodeInfo(number);
    return wada::resolveNodeSecurity(nodeKeyMismatch(number), number && number == self, node && node->is_key_manually_verified,
                                     nodeSigned(number), nodeEncrypted(number), nodeFirmwareVersion(number));
}
void WadaMeshtasticBackend::noteKeyMismatch(uint32_t number, const uint8_t trusted[32])
{
    if (!number || number == self)
        return;
    const bool already = nodeKeyMismatch(number);
    if (auto *entry = metadataFor(number))
        entry->mismatch = true;
    const bool saved = nodeState.markMismatch(number, trusted);
    nodeStateDirty = true;
    ++nodesRevision;
    if (!already && events)
        events->beTerminalLine(
            saved ? "Node public key differs; the stored key was kept"
                  : "Node public key differs; warning storage is full, so this warning may not survive a restart");
}
void WadaMeshtasticBackend::forgetNode(uint32_t number, bool forgetWarning, bool notify)
{
    for (size_t i = 0; i < nodeCount; ++i)
        if (nodes[i].num == number) {
            for (size_t j = i + 1; j < nodeCount; ++j) {
                nodes[j - 1] = nodes[j];
                snapshotNodeSeen[j - 1] = snapshotNodeSeen[j];
            }
            --nodeCount;
            nodes[nodeCount] = {};
            snapshotNodeSeen[nodeCount] = false;
            break;
        }
    for (auto &entry : peerMetadata)
        if (entry.node == number)
            entry = {};
    for (auto &status : statuses)
        if (status.node == number)
            status = {};
    if (forgetWarning && nodeState.forget(number)) {
        nodeStateDirty = true;
        saveNodeState(Time::getMillis(), true);
    }
    uint8_t key[32];
    nodeKey(number, key);
    if (notify && events)
        events->beContactRemoved(key);
    ++nodesRevision;
}
bool WadaMeshtasticBackend::removeUnheardNode(uint32_t number)
{
    const auto *node = nodeInfo(number);
    if (managed || !unheardOnCurrentLora(number) || !node || node->is_favorite || removalCount == removals.size())
        return false;
    for (unsigned i = 0; i < removalCount; ++i)
        if (removals[i] == number)
            return false;
    if (!removalCount) {
        removalLora = lora;
        snprintf(removalPrimary, sizeof removalPrimary, "%s", channels[primaryIndex()].settings.name);
        removalNext = 0;
    }
    removals[removalCount++] = number;
    return true;
}
void WadaMeshtasticBackend::nodeActions()
{
    if (removalActive)
        return;
    while (removalNext < removalCount) {
        const uint32_t number = removals[removalNext];
        if (!number) {
            ++removalNext;
            continue;
        }
        const auto *node = nodeInfo(number);
        if (managed || !unheardOnCurrentLora(number) || !node || node->is_favorite) {
            ++removalNext;
            if (wadaUi)
                wadaUi->onMeshtasticNodeRemoval(number, false, "Node kept; its state changed before removal");
            continue;
        }
        wada::UnheardRemoval request{};
        request.id = newId();
        request.node = number;
        request.self = self;
        request.lora = removalLora;
        snprintf(request.primaryName, sizeof request.primaryName, "%s", removalPrimary);
        if (!wada::queueUnheardRemoval(request))
            return;
        ++removalNext;
        removalActive = request.id;
        removalActiveNode = number;
        return;
    }
    removalCount = removalNext = 0;
}
void WadaMeshtasticBackend::cancelNodeRemovals(const char *reason)
{
    if (removalActive) {
        wada::cancelUnheardRemoval(removalActive);
        if (wadaUi)
            wadaUi->onMeshtasticNodeRemoval(removalActiveNode, false, reason);
        removalActive = removalActiveNode = 0;
    }
    while (removalNext < removalCount) {
        const uint32_t number = removals[removalNext++];
        if (number && wadaUi)
            wadaUi->onMeshtasticNodeRemoval(number, false, reason);
    }
    removalCount = removalNext = 0;
}
void WadaMeshtasticBackend::protectNodeRemoval(uint32_t number)
{
    if (removalActiveNode == number && wada::cancelUnheardRemoval(removalActive)) {
        removalActive = removalActiveNode = 0;
        if (wadaUi)
            wadaUi->onMeshtasticNodeRemoval(number, false, "Favorite kept");
    }
    for (unsigned i = removalNext; i < removalCount; ++i)
        if (removals[i] == number) {
            removals[i] = 0;
            if (wadaUi)
                wadaUi->onMeshtasticNodeRemoval(number, false, "Favorite kept");
        }
}
bool WadaMeshtasticBackend::nodeNotification(const meshtastic_ClientNotification &message)
{
    if (!message.has_reply_id || !removalActive || message.reply_id != removalActive)
        return false;
    bool known = false;
    for (unsigned i = 0; i <= unsigned(wada::NodeRemovalResult::Unavailable); ++i)
        known |= !strcmp(message.message, wada::nodeRemovalReason(wada::NodeRemovalResult(i)));
    if (!known)
        return false;
    const bool removed = !strcmp(message.message, wada::nodeRemovalReason(wada::NodeRemovalResult::Removed)) ||
                         !strcmp(message.message, wada::nodeRemovalReason(wada::NodeRemovalResult::Missing));
    const uint32_t number = removalActiveNode;
    removalActive = removalActiveNode = 0;
    if (removed)
        forgetNode(number, true);
    if (wadaUi)
        wadaUi->onMeshtasticNodeRemoval(number, removed, message.message);
    return true;
}
bool WadaMeshtasticBackend::requestNodeMetadata(uint32_t number)
{
    if (!configured || !number || number == UINT32_MAX)
        return false;
    if (number == self)
        return metadataKnown;
    if (!canMessage(number))
        return false;
    MetadataRequest *slot = nullptr;
    for (auto &request : metadataRequests)
        if (!request.id) {
            slot = &request;
            break;
        }
    if (!slot)
        return false;
    meshtastic_AdminMessage admin{};
    admin.which_payload_variant = meshtastic_AdminMessage_get_device_metadata_request_tag;
    admin.get_device_metadata_request = true;
    uint8_t encoded[meshtastic_AdminMessage_size];
    auto stream = pb_ostream_from_buffer(encoded, sizeof encoded);
    if (!pb_encode(&stream, meshtastic_AdminMessage_fields, &admin))
        return false;
    const uint32_t id = sendData(number, primaryIndex(), meshtastic_PortNum_ADMIN_APP, encoded, stream.bytes_written, true, true);
    if (!id)
        return false;
    *slot = {};
    slot->id = id;
    slot->node = number;
    slot->started = Time::getMillis();
    const auto *node = nodeInfo(number);
    if (node && node->user.public_key.size == 32)
        memcpy(slot->key, node->user.public_key.bytes, 32);
    return true;
}
bool WadaMeshtasticBackend::nodeAdmin(const meshtastic_MeshPacket &packet)
{
    if (packet.to != self || !packet.decoded.request_id)
        return false;
    for (auto &request : metadataRequests)
        if (request.id == packet.decoded.request_id && request.node == packet.from) {
            if (!owner.is_licensed &&
                (!packet.pki_encrypted || packet.public_key.size != 32 || memcmp(packet.public_key.bytes, request.key, 32)))
                return false;
            meshtastic_AdminMessage admin{};
            auto stream = pb_istream_from_buffer(packet.decoded.payload.bytes, packet.decoded.payload.size);
            if (!pb_decode(&stream, meshtastic_AdminMessage_fields, &admin) ||
                admin.which_payload_variant != meshtastic_AdminMessage_get_device_metadata_response_tag)
                return false;
            if (auto *entry = metadataFor(packet.from)) {
                snprintf(entry->version, sizeof entry->version, "%s", admin.get_device_metadata_response.firmware_version);
                ++nodesRevision;
            }
            request = {};
            if (wadaUi)
                wadaUi->showAlert("Device information received", 2000);
            return true;
        }
    return false;
}
void WadaMeshtasticBackend::nodeRequestFailure(uint32_t id, const char *reason)
{
    for (auto &request : metadataRequests)
        if (id && request.id == id) {
            request = {};
            char message[200];
            snprintf(message, sizeof message, "Device information request failed: %s", reason);
            if (events)
                events->beTerminalLine(message);
            if (wadaUi)
                wadaUi->showAlert(message, 3500);
        }
}
void WadaMeshtasticBackend::loadNodeState()
{
    std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[wada::NodeState::MaxBytes]);
    std::unique_ptr<wada::NodeState> candidate(new (std::nothrow) wada::NodeState);
    if (!bytes || !candidate)
        return;
    bool have = false;
    for (unsigned slot = 0; slot < 2; ++slot) {
        const size_t size = wadaNodeStateRead(slot, bytes.get(), wada::NodeState::MaxBytes);
        uint32_t own, generation;
        if (candidate->restore(bytes.get(), size, own, generation) && (!have || int32_t(generation - nodeStateGeneration) > 0)) {
            nodeState = *candidate;
            nodeStateSelf = own;
            nodeStateGeneration = generation;
            nodeStateSlot = slot;
            have = true;
        }
    }
}
void WadaMeshtasticBackend::saveNodeState(uint32_t now, bool force)
{
    if (!self || !nodeStateDirty || (!force && lastNodeStateSave && !Throttle::hasElapsed(lastNodeStateSave, 1000)))
        return;
    lastNodeStateSave = now ? now : 1;
    std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[wada::NodeState::MaxBytes]);
    if (!bytes)
        return;
    const size_t size = nodeState.encode(bytes.get(), wada::NodeState::MaxBytes, self, nodeStateGeneration + 1);
    if (size && wadaNodeStateWrite(1 - nodeStateSlot, bytes.get(), size)) {
        ++nodeStateGeneration;
        nodeStateSlot = 1 - nodeStateSlot;
        nodeStateDirty = false;
    } else if (events)
        events->beTerminalLine("Node security warnings could not be saved; check UI storage");
}

void WadaMeshtasticBackend::explicitRemovalFailure(uint32_t id, const char *reason)
{
    for (auto &request : explicitRemovals)
        if (request.id && request.id == id) {
            request = {};
            char message[220];
            snprintf(message, sizeof message, "Node removal not confirmed: %s", reason);
            if (events)
                events->beTerminalLine(message);
            if (wadaUi)
                wadaUi->showAlert(message, 3500);
        }
}
void WadaMeshtasticBackend::cancelExplicitRemovals(const char *reason)
{
    for (auto &request : explicitRemovals)
        if (request.id)
            explicitRemovalFailure(request.id, reason);
}
bool WadaMeshtasticBackend::confirmExplicitRemoval(uint32_t number)
{
    bool explicitRequest = false;
    for (auto &request : explicitRemovals)
        if (request.id && request.node == number) {
            request = {};
            explicitRequest = true;
        }
    return explicitRequest;
}
void WadaMeshtasticBackend::finishExplicitRemovals()
{
    for (auto &request : explicitRemovals)
        if (request.id) {
            if (!nodeInfo(request.node)) {
                const uint32_t number = request.node;
                request = {};
                forgetNode(number, true, false);
            } else if (request.acknowledged) {
                explicitRemovalFailure(request.id, "The node is still present after refreshing");
            }
        }
}
void WadaMeshtasticBackend::traceFailure(uint32_t id, const char *reason)
{
    if (!traceId || traceId != id)
        return;
    traceId = 0;
    char message[220];
    snprintf(message, sizeof message, "Traceroute request failed: %s", reason);
    if (events)
        events->beTerminalLine(message);
    if (wadaUi)
        wadaUi->onMeshtasticTrace(message);
}
