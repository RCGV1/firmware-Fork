// Exercises the production native status adapter and official protobuf codec.
// Status is current plain text, not a chat message or a successful send claim;
// UTF-8 overflow, stale replay, reconnect and wrong-node edits must not corrupt it.
#include "WadaMeshtasticBackend.h"
#include "WadaStatus.h"
#include "meshtastic/portnums.pb.h"
#include <cassert>
#include <cstring>
#include <memory>
#include <pb_decode.h>
#include <pb_encode.h>
#include <string>
extern uint32_t nowMs;
void push(meshtastic_FromRadio, WadaMeshtasticBackend &);
meshtastic_ToRadio take();
void drain();
meshtastic_FromRadio text(uint32_t, uint32_t, uint32_t, const char *);
namespace
{
constexpr uint32_t self = 0x11111111, peer = 0x22222222;
meshtastic_FromRadio status(uint32_t node, const char *value, uint32_t id = 0xa3000010)
{
    auto r = text(node, UINT32_MAX, id, "");
    r.packet.decoded.portnum = meshtastic_PortNum_NODE_STATUS_APP;
    meshtastic_StatusMessage message{};
    assert(strlen(value) < sizeof message.status);
    strcpy(message.status, value);
    auto stream = pb_ostream_from_buffer(r.packet.decoded.payload.bytes, sizeof r.packet.decoded.payload.bytes);
    assert(pb_encode(&stream, meshtastic_StatusMessage_fields, &message));
    r.packet.decoded.payload.size = stream.bytes_written;
    return r;
}
void node(WadaMeshtasticBackend &backend, uint32_t number)
{
    meshtastic_FromRadio r{};
    r.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    r.node_info.num = number;
    push(r, backend);
}
void version(WadaMeshtasticBackend &backend, const char *value, bool excluded = false)
{
    meshtastic_FromRadio r{};
    r.which_payload_variant = meshtastic_FromRadio_metadata_tag;
    strcpy(r.metadata.firmware_version, value);
    r.metadata.excluded_modules = excluded ? meshtastic_ExcludedModules_STATUSMESSAGE_CONFIG : 0;
    push(r, backend);
}
void complete(WadaMeshtasticBackend &backend, uint32_t nonce)
{
    meshtastic_FromRadio r{};
    r.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    r.config_complete_id = nonce;
    push(r, backend);
}
void module(WadaMeshtasticBackend &backend, const char *value)
{
    meshtastic_FromRadio r{};
    r.which_payload_variant = meshtastic_FromRadio_moduleConfig_tag;
    r.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_statusmessage_tag;
    strcpy(r.moduleConfig.payload_variant.statusmessage.node_status, value);
    push(r, backend);
}
} // namespace
void test_native_status(MeshBackendEvents &sink)
{
    drain();
    nowMs = 1000;
    auto backend = std::make_unique<WadaMeshtasticBackend>();
    NodePrefs prefs{};
    uint8_t key[32]{};
    backend->begin(sink, prefs, key);
    const auto config = take();
    meshtastic_FromRadio r{};
    r.which_payload_variant = meshtastic_FromRadio_my_info_tag;
    r.my_info.my_node_num = self;
    push(r, *backend);
    node(*backend, self);
    node(*backend, peer);
    version(*backend, "2.7.20");
    complete(*backend, config.want_config_id);
    assert(!backend->canEditStatus() && !backend->setStatusMessage("Old firmware"));
    version(*backend, "2.8", true);
    assert(!backend->canEditStatus());
    version(*backend, "bad");
    assert(!backend->canEditStatus());
    version(*backend, "2.8");
    assert(backend->canEditStatus() && backend->statusMaxBytes() == 79);
    assert(!backend->setOwner(std::string(25, 'a').c_str(), "SELF"));
    assert(!backend->setOwner("Bad\nname", "SELF"));
    assert(!backend->setOwner("Valid", "\xc0\x80"));
    assert(backend->setOwner(std::string(24, 'a').c_str(), "SELF"));
    auto named = take();
    meshtastic_AdminMessage nameAdmin{};
    auto namedInput = pb_istream_from_buffer(named.packet.decoded.payload.bytes, named.packet.decoded.payload.size);
    assert(pb_decode(&namedInput, meshtastic_AdminMessage_fields, &nameAdmin));
    assert(nameAdmin.which_payload_variant == meshtastic_AdminMessage_set_owner_tag &&
           strlen(nameAdmin.set_owner.long_name) == 24);
    assert(!backend->setStatusMessage(nullptr));
    assert(!backend->setStatusMessage(std::string(80, 'a').c_str()));
    assert(!backend->setStatusMessage("line\nbreak") && !backend->setStatusMessage("line\rbreak"));
    assert(!backend->setStatusMessage("line\xe2\x80\xa8"));
    for (const char *invalid : {"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82"})
        assert(!backend->setStatusMessage(invalid));
    std::string utf8;
    for (unsigned i = 0; i < 19; ++i)
        utf8 += "\xf0\x9f\x8c\xbe";
    assert(backend->setStatusMessage((utf8 + "abc").c_str()));
    auto outgoing = take();
    assert(outgoing.which_payload_variant == meshtastic_ToRadio_packet_tag && outgoing.packet.to == self);
    meshtastic_AdminMessage admin{};
    auto input = pb_istream_from_buffer(outgoing.packet.decoded.payload.bytes, outgoing.packet.decoded.payload.size);
    assert(pb_decode(&input, meshtastic_AdminMessage_fields, &admin));
    assert(admin.which_payload_variant == meshtastic_AdminMessage_set_module_config_tag &&
           admin.set_module_config.which_payload_variant == meshtastic_ModuleConfig_statusmessage_tag);
    assert(!strcmp(admin.set_module_config.payload_variant.statusmessage.node_status, (utf8 + "abc").c_str()));
    assert(!backend->nodeStatus(self)[0]); // Queue acceptance does not change the displayed value.
    assert(!backend->setStatusMessage((utf8 + "abcd").c_str()));
    push(status(self, "Broadcast fallback"), *backend);
    assert(!strcmp(backend->nodeStatus(self), "Broadcast fallback"));
    module(*backend, "Confirmed config");
    push(status(self, "Old echo"), *backend);
    assert(!strcmp(backend->nodeStatus(self), "Confirmed config"));
    assert(backend->setStatusMessage(""));
    outgoing = take();
    input = pb_istream_from_buffer(outgoing.packet.decoded.payload.bytes, outgoing.packet.decoded.payload.size);
    assert(pb_decode(&input, meshtastic_AdminMessage_fields, &admin));
    assert(!admin.set_module_config.payload_variant.statusmessage.node_status[0]);
    assert(!strcmp(backend->nodeStatus(self), "Confirmed config"));
    module(*backend, "");
    assert(!backend->nodeStatus(self)[0]);
    push(status(0x66666666, ""), *backend);
    assert(!backend->nodeInfo(0x66666666));
    push(status(0x66666666, "Before native removal"), *backend);
    assert(backend->nodeInfo(0x66666666));
    auto gone = meshtastic_FromRadio{};
    gone.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    gone.id = wada::NodeGone;
    gone.node_info.num = 0x66666666;
    push(gone, *backend);
    assert(!backend->nodeInfo(0x66666666));
    push(status(0x66666666, ""), *backend); // Native replay refresh of the deleted sender emits an empty value.
    assert(!backend->nodeInfo(0x66666666));
    const auto literal = status(peer, "#ff0000 <b>**plain**</b>");
    push(literal, *backend);
    assert(!strcmp(backend->nodeStatus(peer), "#ff0000 <b>**plain**</b>"));
    const auto revision = backend->nodeRevision();
    push(literal, *backend);
    assert(backend->nodeRevision() == revision);
    auto invalid = status(peer, "Bad");
    invalid.packet.decoded.payload.bytes[0] = 0xff;
    push(invalid, *backend);
    assert(backend->nodeRevision() == revision);
    auto wrong = status(peer, "Private to someone else");
    wrong.packet.to = 0x33333333;
    push(wrong, *backend);
    assert(backend->nodeRevision() == revision);
    push(status(peer, "\xe3\x80\x80\xc2\xa0"), *backend);
    assert(!wada::statusVisible(backend->nodeStatus(peer)));
    assert(!wada::statusVisible("") && !wada::statusVisible(" \t\n") && wada::statusVisible(" Status "));
    push(literal, *backend);
    backend->resync();
    backend->loop(nowMs);
    auto sync = take();
    assert(!backend->canEditStatus() && !strcmp(backend->nodeStatus(peer), "#ff0000 <b>**plain**</b>"));
    node(*backend, self);
    node(*backend, peer);
    version(*backend, "2.8.2");
    push(status(self, "Current without config"), *backend);
    complete(*backend, sync.want_config_id);
    assert(!strcmp(backend->nodeStatus(self), "Current without config"));
    assert(!backend->nodeStatus(peer)[0]); // An absent replay status must not retain an old nonempty value.
    push(literal, *backend);               // Replay IDs repeat between config snapshots and remain admissible.
    assert(!strcmp(backend->nodeStatus(peer), "#ff0000 <b>**plain**</b>"));
    backend->resync();
    backend->loop(nowMs);
    sync = take();
    node(*backend, self);
    node(*backend, peer);
    push(status(peer, "Live during snapshot"), *backend);
    version(*backend, "2.8.2");
    complete(*backend, sync.want_config_id);
    assert(!strcmp(backend->nodeStatus(peer), "Live during snapshot"));
    push(status(peer, ""), *backend);
    assert(!backend->nodeStatus(peer)[0]);
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_config_tag;
    r.config.which_payload_variant = meshtastic_Config_security_tag;
    r.config.payload_variant.security.is_managed = true;
    push(r, *backend);
    assert(!backend->canEditStatus() && !backend->setStatusMessage("Managed"));
    push(status(peer, "Last known"), *backend);
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_rebooted_tag;
    push(r, *backend);
    assert(!backend->canEditStatus() && !strcmp(backend->nodeStatus(peer), "Last known"));
    drain();
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_my_info_tag;
    r.my_info.my_node_num = 0x55555555;
    push(r, *backend);
    assert(!backend->nodeStatus(peer)[0]);
    drain();
}
