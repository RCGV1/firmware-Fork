// Pins native node-state capability gates, first-wins identity and asynchronous
// clear results. A local firmware version must never grant peer signing, and a
// dropped result must not strand a clear or optimistically erase a contact.
#include "WadaMeshtasticBackend.h"
#include "WadaNodeActions.h"
#include "WadaNodeState.h"
#include "meshtastic/portnums.pb.h"
#include "ui-touch/UITask.h"
#include <cassert>
#include <cstring>
#include <memory>
#include <pb_encode.h>
#include <string>
#include <vector>
extern UITask ui;
extern uint32_t nowMs;
void push(meshtastic_FromRadio, WadaMeshtasticBackend &);
meshtastic_ToRadio take();
void drain();
meshtastic_FromRadio text(uint32_t, uint32_t, uint32_t, const char *);
namespace
{
std::vector<uint8_t> files[2];
bool failWrite = false;
meshtastic_FromRadio metadata(const char *version)
{
    meshtastic_FromRadio r{};
    r.which_payload_variant = meshtastic_FromRadio_metadata_tag;
    snprintf(r.metadata.firmware_version, sizeof r.metadata.firmware_version, "%s", version);
    return r;
}
meshtastic_FromRadio userClaim(uint32_t number, uint8_t key)
{
    static uint32_t serial = 0xbb100000;
    auto r = text(number, UINT32_MAX, ++serial, "");
    r.packet.decoded.portnum = meshtastic_PortNum_NODEINFO_APP;
    meshtastic_User user{};
    user.public_key.size = 32;
    memset(user.public_key.bytes, key, 32);
    strcpy(user.long_name, "Refused claimed name");
    user.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    auto s = pb_ostream_from_buffer(r.packet.decoded.payload.bytes, sizeof r.packet.decoded.payload.bytes);
    assert(pb_encode(&s, meshtastic_User_fields, &user));
    r.packet.decoded.payload.size = s.bytes_written;
    return r;
}
} // namespace
size_t wadaNodeStateRead(unsigned slot, uint8_t *out, size_t cap)
{
    assert(slot < 2);
    if (files[slot].size() > cap)
        return 0;
    if (!files[slot].empty())
        memcpy(out, files[slot].data(), files[slot].size());
    return files[slot].size();
}
bool wadaNodeStateWrite(unsigned slot, const uint8_t *bytes, size_t size)
{
    assert(slot < 2);
    files[slot].assign(bytes, bytes + (failWrite ? size / 2 : size));
    return !failWrite;
}
void test_native_nodes(WadaMeshtasticBackend &backend)
{
    using Security = wada::NodeSecurity;
    constexpr uint32_t peer = 0x22222222, self = 0x11111111;
    for (const char *unknown : {"", "bad", "2.x", "2.8x"})
        assert(!wada::firmwareAtLeast(unknown, 2, 8));
    assert(wada::firmwareAtLeast("2.8", 2, 8) && !wada::firmwareAtLeast("2.8", 2, 8, 1));
    assert(wada::firmwareAtLeast("2.8.1.abc", 2, 8, 1) && wada::firmwareAtLeast("3.0", 2, 8, 1));
    assert(wada::resolveNodeSecurity(true, false, true, true, true, "2.8") == Security::KeyMismatch);
    assert(wada::resolveNodeSecurity(false, false, true, false, true, "2.7") == Security::VerifiedContact);
    assert(wada::resolveNodeSecurity(false, false, false, true, false, "") == Security::SignedNode);
    assert(wada::resolveNodeSecurity(false, false, false, false, true, "") == Security::PublicKey);
    assert(wada::resolveNodeSecurity(false, false, false, false, false, "") == Security::NoPublicKey);
    drain();
    auto r = meshtastic_FromRadio{};
    r.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    r.node_info = *backend.nodeInfo(peer);
    r.node_info.user.public_key.size = 32;
    memset(r.node_info.user.public_key.bytes, 6, 32);
    push(r, backend);
    memset(r.node_info.user.public_key.bytes, 7, 32);
    r.node_info.is_favorite = false;
    r.node_info.via_mqtt = false;
    r.node_info.heard_on_current_lora = false;
    r.node_info.has_xeddsa_signed = false;
    r.node_info.is_key_manually_verified = false;
    push(r, backend);
    assert(!backend.nodeKeyMismatch(peer));
    push(metadata(""), backend);
    assert(!backend.reportsHeardOnCurrentLora() && !backend.unheardOnCurrentLora(peer));
    push(metadata("2.8.0"), backend);
    assert(!backend.reportsHeardOnCurrentLora());
    push(metadata("2.8.1"), backend);
    assert(backend.reportsHeardOnCurrentLora() && backend.unheardOnCurrentLora(peer));
    assert(!backend.unheardOnCurrentLora(self) && !backend.unheardOnCurrentLora(0x77777777));
    r.node_info.via_mqtt = true;
    push(r, backend);
    assert(!backend.unheardOnCurrentLora(peer));
    r.node_info.via_mqtt = false;
    r.node_info.is_favorite = true;
    push(r, backend);
    assert(backend.unheardOnCurrentLora(peer) && !backend.removeUnheardNode(peer));
    r.node_info.is_favorite = false;
    push(r, backend);
    assert(backend.nodeSecurity(peer) == Security::PublicKey && !backend.nodeSigned(peer));
    const auto count = backend.mirroredNodes();
    const auto revision = backend.nodeRevision();
    r.node_info.heard_on_current_lora = true;
    push(r, backend);
    assert(backend.mirroredNodes() == count && backend.nodeRevision() != revision && !backend.unheardOnCurrentLora(peer));
    r.node_info.heard_on_current_lora = false;
    push(r, backend);
    push(text(peer, UINT32_MAX, 0xbb000001, "raw reception"), backend);
    assert(backend.unheardOnCurrentLora(peer));
    push(userClaim(peer, 0), backend);
    assert(!backend.nodeKeyMismatch(peer));
    const std::string trustedName = backend.nodeInfo(peer)->user.long_name;
    push(userClaim(peer, 8), backend);
    assert(backend.nodeKeyMismatch(peer) && backend.nodeSecurity(peer) == Security::KeyMismatch);
    assert(!memcmp(backend.nodeInfo(peer)->user.public_key.bytes, r.node_info.user.public_key.bytes, 32));
    assert(trustedName == backend.nodeInfo(peer)->user.long_name && backend.canMessage(peer) && !backend.nodeEncrypted(peer));
    push(userClaim(peer, 7), backend);
    push(userClaim(peer, 0), backend);
    push(r, backend);
    assert(backend.nodeKeyMismatch(peer));
    r.node_info.has_xeddsa_signed = true;
    r.node_info.is_key_manually_verified = true;
    push(r, backend);
    assert(backend.nodeSigned(peer) && backend.nodeSecurity(peer) == Security::KeyMismatch);
    r.node_info.user.public_key.size = 0;
    push(r, backend);
    assert(backend.nodeKeyMismatch(peer));
    r.node_info.user.public_key.size = 32;
    memset(r.node_info.user.public_key.bytes, 9, 32);
    push(r, backend);
    assert(!backend.nodeKeyMismatch(peer) && backend.nodeSecurity(peer) == Security::VerifiedContact);
    r.node_info.is_key_manually_verified = false;
    push(r, backend);
    assert(backend.nodeSecurity(peer) == Security::SignedNode);
    r.node_info.has_xeddsa_signed = false;
    push(r, backend);
    assert(backend.nodeSecurity(peer) == Security::PublicKey);
    assert(backend.removeUnheardNode(peer));
    ContactInfo favorite{};
    WadaMeshtasticBackend::nodeKey(peer, favorite.id.pub_key);
    favorite.flags = 1;
    const auto favoritePending = ui.removals.size();
    assert(backend.contactChanged(favorite));
    backend.loop(nowMs);
    assert(ui.removals.size() == favoritePending + 1 && !ui.removals.back().removed && !backend.removingUnheardNodes());
    drain();
    assert(backend.removeUnheardNode(peer));
    backend.loop(nowMs);
    const auto favoriteQueued = ui.removals.size();
    assert(backend.contactChanged(favorite));
    backend.loop(nowMs);
    wada::UnheardRemoval cancelled{};
    assert(!wada::takeUnheardRemoval(cancelled) && ui.removals.size() == favoriteQueued + 1 && !backend.removingUnheardNodes());
    drain();
    drain();
    assert(backend.requestNodeMetadata(peer));
    auto request = take();
    assert(request.packet.to == peer && request.packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP);
    auto response = text(0x33333333, self, 0xcc000001, "");
    response.packet.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    response.packet.decoded.request_id = request.packet.id;
    meshtastic_AdminMessage admin{};
    admin.which_payload_variant = meshtastic_AdminMessage_get_device_metadata_response_tag;
    strcpy(admin.get_device_metadata_response.firmware_version, "2.8");
    auto stream = pb_ostream_from_buffer(response.packet.decoded.payload.bytes, sizeof response.packet.decoded.payload.bytes);
    assert(pb_encode(&stream, meshtastic_AdminMessage_fields, &admin));
    response.packet.decoded.payload.size = stream.bytes_written;
    push(response, backend);
    assert(!backend.nodeFirmwareVersion(peer)[0]);
    response.packet.from = peer;
    response.packet.id++;
    response.packet.decoded.request_id++;
    push(response, backend);
    assert(!backend.nodeFirmwareVersion(peer)[0]);
    response.packet.id++;
    response.packet.decoded.request_id--;
    push(response, backend);
    assert(!backend.nodeFirmwareVersion(peer)[0]);
    response.packet.id++;
    response.packet.pki_encrypted = true;
    response.packet.public_key.size = 32;
    memset(response.packet.public_key.bytes, 9, 32);
    push(response, backend);
    assert(!strcmp(backend.nodeFirmwareVersion(peer), "2.8") && backend.nodeSecurity(peer) == Security::SignedNode &&
           !backend.nodeSigned(peer));
    assert(backend.requestNodeMetadata(peer));
    const auto oldKeyRequest = take();
    r.node_info.user.public_key.size = 0;
    push(r, backend);
    r.node_info.user.public_key.size = 32;
    memset(r.node_info.user.public_key.bytes, 10, 32);
    push(r, backend);
    assert(!backend.nodeFirmwareVersion(peer)[0] && backend.nodeSecurity(peer) == Security::PublicKey);
    response.packet.id++;
    response.packet.decoded.request_id = oldKeyRequest.packet.id;
    memset(response.packet.public_key.bytes, 10, 32);
    push(response, backend);
    assert(!backend.nodeFirmwareVersion(peer)[0]);
    assert(backend.nodeSecurity(self) == Security::VerifiedContact);
    assert(backend.removeUnheardNode(peer) && backend.removingUnheardNodes());
    backend.loop(nowMs);
    wada::UnheardRemoval intent{};
    assert(wada::takeUnheardRemoval(intent) && intent.node == peer && intent.self == self);
    assert(backend.nodeInfo(peer));
    auto result = meshtastic_FromRadio{};
    result.which_payload_variant = meshtastic_FromRadio_clientNotification_tag;
    result.clientNotification.has_reply_id = true;
    result.clientNotification.reply_id = intent.id + 1;
    strcpy(result.clientNotification.message, wada::nodeRemovalReason(wada::NodeRemovalResult::Favorite));
    const auto before = ui.removals.size();
    push(result, backend);
    assert(ui.removals.size() == before && backend.removingUnheardNodes());
    result.clientNotification.reply_id = intent.id;
    push(result, backend);
    assert(ui.removals.size() == before + 1 && !ui.removals.back().removed && backend.nodeInfo(peer));
    assert(!backend.removingUnheardNodes());
    assert(backend.removeUnheardNode(peer));
    backend.loop(nowMs);
    assert(wada::takeUnheardRemoval(intent));
    result.clientNotification.reply_id = intent.id;
    strcpy(result.clientNotification.message, wada::nodeRemovalReason(wada::NodeRemovalResult::Removed));
    push(result, backend);
    assert(!backend.nodeInfo(peer) && ui.removals.back().removed);
    r.id = wada::NodeDelta;
    r.node_info.heard_on_current_lora = true;
    push(r, backend);
    assert(backend.nodeInfo(peer) && !backend.unheardOnCurrentLora(peer));
    r.node_info.heard_on_current_lora = false;
    push(r, backend);
    auto second = r;
    second.node_info.num = 0x33333333;
    push(second, backend);
    assert(backend.removeUnheardNode(peer) && backend.removeUnheardNode(0x33333333));
    backend.loop(nowMs);
    assert(wada::takeUnheardRemoval(intent));
    const auto pending = ui.removals.size();
    auto overflow = meshtastic_FromRadio{};
    overflow.which_payload_variant = meshtastic_FromRadio_clientNotification_tag;
    strcpy(overflow.clientNotification.message, "WADAMESH receive overflow: 1 dropped; resync required");
    push(overflow, backend);
    assert(!backend.removingUnheardNodes() && ui.removals.size() == pending + 2);
    assert(!ui.removals.back().removed && !backend.nodeSnapshotReady());
    auto config = take();
    assert(config.which_payload_variant == meshtastic_ToRadio_want_config_id_tag);
    push(r, backend);
    push(second, backend);
    push(metadata("2.8.2"), backend);
    auto complete = meshtastic_FromRadio{};
    complete.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    complete.config_complete_id = config.want_config_id;
    push(complete, backend);
    assert(backend.nodeSnapshotReady() && backend.removeUnheardNode(peer));
    backend.loop(nowMs);
    assert(wada::takeUnheardRemoval(intent));
    auto reboot = meshtastic_FromRadio{};
    reboot.which_payload_variant = meshtastic_FromRadio_rebooted_tag;
    push(reboot, backend);
    assert(!backend.removingUnheardNodes());
    config = take();
    push(r, backend);
    push(second, backend);
    push(metadata("2.8.2"), backend);
    complete.config_complete_id = config.want_config_id;
    push(complete, backend);
    auto state = std::make_unique<wada::NodeState>();
    uint8_t key[32];
    memset(key, 7, sizeof key);
    assert(state->markMismatch(peer, key) && state->mismatch(peer));
    assert(!state->acceptedKey(peer, key, false) && state->mismatch(peer));
    uint8_t missing[32]{};
    assert(!state->acceptedKey(peer, missing, false) && state->mismatch(peer));
    uint8_t bytes[wada::NodeState::MaxBytes];
    const size_t size = state->encode(bytes, sizeof bytes, self, UINT32_MAX);
    auto restored = std::make_unique<wada::NodeState>();
    uint32_t scope = 0, generation = 0;
    assert(restored->restore(bytes, size, scope, generation) && scope == self && generation == UINT32_MAX &&
           restored->mismatch(peer));
    for (size_t i = 0; i < size; ++i) {
        bytes[i] ^= 1;
        assert(!restored->restore(bytes, size, scope, generation));
        bytes[i] ^= 1;
    }
    assert(!restored->restore(bytes, size - 1, scope, generation) && restored->mismatch(peer));
    key[0] = 8;
    assert(restored->acceptedKey(peer, key, false) && !restored->mismatch(peer));
    assert(state->acceptedKey(peer, nullptr, true) && !state->mismatch(peer));
    drain();
}

void test_native_node_restart(MeshBackendEvents &sink)
{
    constexpr uint32_t self = 0x11111111, peer = 0x22222222;
    auto state = std::make_unique<wada::NodeState>();
    uint8_t key[32];
    memset(key, 7, sizeof key);
    assert(state->markMismatch(peer, key));
    uint8_t bytes[wada::NodeState::MaxBytes];
    const size_t size = state->encode(bytes, sizeof bytes, self, 20);
    files[0].assign(bytes, bytes + size);
    const size_t newer = state->encode(bytes, sizeof bytes, self, 21);
    files[1].assign(bytes, bytes + newer / 2);
    auto backend = std::make_unique<WadaMeshtasticBackend>();
    NodePrefs prefs{};
    uint8_t own[32];
    backend->begin(sink, prefs, own);
    const auto config = take();
    auto r = meshtastic_FromRadio{};
    r.which_payload_variant = meshtastic_FromRadio_my_info_tag;
    r.my_info.my_node_num = self;
    push(r, *backend);
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    r.node_info.num = peer;
    r.node_info.has_user = true;
    r.node_info.user.public_key.size = 32;
    memcpy(r.node_info.user.public_key.bytes, key, 32);
    push(r, *backend);
    assert(backend->nodeKeyMismatch(peer));
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    r.config_complete_id = config.want_config_id;
    push(r, *backend);
    push(metadata("2.8.2"), *backend);
    assert(backend->requestNodeMetadata(peer));
    const auto request = take();
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_my_info_tag;
    r.my_info.my_node_num = 0x55555555;
    push(r, *backend);
    assert(!backend->nodeKeyMismatch(peer));
    auto response = text(peer, 0x55555555, 0xcc000020, "");
    response.packet.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    response.packet.decoded.request_id = request.packet.id;
    meshtastic_AdminMessage admin{};
    admin.which_payload_variant = meshtastic_AdminMessage_get_device_metadata_response_tag;
    strcpy(admin.get_device_metadata_response.firmware_version, "2.8");
    auto stream = pb_ostream_from_buffer(response.packet.decoded.payload.bytes, sizeof response.packet.decoded.payload.bytes);
    assert(pb_encode(&stream, meshtastic_AdminMessage_fields, &admin));
    response.packet.decoded.payload.size = stream.bytes_written;
    push(response, *backend);
    assert(!backend->nodeFirmwareVersion(peer)[0]);
    drain();
}
