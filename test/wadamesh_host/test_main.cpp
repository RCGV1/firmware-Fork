// Exercises the compiled WadaMeshtasticBackend with the extracted PacketClient/PacketServer
// and official nanopb messages. Hardware and UI records alone are substituted. This guards
// against wrong packet destinations/ports, false delivery claims, lost config nonces,
// stale peer identities, queue rejection and duplicate received chat messages.
#include "WadaChannelIdentity.h"
#include "WadaChannelIntent.h"
#include "WadaDeliveryUI.h"
#include "WadaMeshtasticBackend.h"
#include "meshtastic/apponly.pb.h"
#include "meshtastic/portnums.pb.h"
#include "ui-touch/UITask.h"
#include "wadacomms/PacketServer.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <pb_decode.h>
#include <pb_encode.h>
#include <string>
#include <vector>
uint32_t nowMs = 100;
uint32_t millis()
{
    return nowMs;
}
namespace Time
{
uint32_t getMillis()
{
    return nowMs;
}
} // namespace Time
uint32_t esp_random()
{
    return 0x12345678;
}
UITask ui;
UITask *wadaUi = &ui;
struct Sink : MeshBackendEvents {
    unsigned contacts = 0, direct = 0, channel = 0, acks = 0, configures = 0, positions = 0, channels = 0, outgoing = 0,
             removed = 0;
    std::string channelLabels[8];
    ChannelDetails channelDetails[8]{};
    std::string lastText;
    bool gpsEnabled = false;
    ContactInfo lastContact{};
    uint32_t timestamp = 0;
    uint8_t lastHops = 0;
    int8_t lastSnr = 0, lastRssi = 0;
    void beContact(const ContactInfo &c, bool, uint8_t) override
    {
        ++contacts;
        lastContact = c;
    }
    void beDiscovered(const ContactInfo &, uint8_t) override {}
    void beOutgoingDirect(const uint8_t *, const char *, uint32_t) override { ++outgoing; }
    void beOutgoingChannel(int, const char *, uint32_t) override { ++outgoing; }
    void beChannel(int i, const ChannelDetails &c) override
    {
        ++channels;
        channelLabels[i] = c.name;
        channelDetails[i] = c;
    }
    void beDirectMessage(const uint8_t *, uint32_t ts, const char *text, uint8_t, bool, int8_t, int8_t) override
    {
        ++direct;
        lastText = text;
        timestamp = ts;
    }
    void beChannelMessage(int, uint32_t ts, const char *text, uint8_t hops, int8_t snr, int8_t rssi) override
    {
        ++channel;
        lastHops = hops;
        lastSnr = snr;
        lastRssi = rssi;
        lastText = text;
        timestamp = ts;
    }
    void beRoomMessage(const uint8_t *, const char *, uint32_t, const char *, uint8_t, int8_t, int8_t) override {}
    void beAck(uint32_t) override { ++acks; }
    void beRepeatHeard(uint32_t, const uint8_t *, uint8_t) override {}
    void beStatusReply(const uint8_t *, const uint8_t *, size_t) override {}
    void beTelemetryReply(const uint8_t *, const uint8_t *, size_t) override {}
    void beTraceResult(uint32_t, int8_t, int8_t, uint8_t, const int8_t *) override {}
    void beSignal(int8_t, int8_t) override {}
    void beSelfInfo(const NodePrefs &prefs, const uint8_t *, int32_t, int32_t) override { gpsEnabled = prefs.gps_enabled; }
    void beSelfPosition(int32_t, int32_t) override { ++positions; }
    void beContactRemoved(const uint8_t *) override { ++removed; }
    void beRxRaw(int8_t, int8_t, const uint8_t *, size_t) override {}
    void beLoginResult(const uint8_t *, bool, uint8_t, uint32_t) override {}
    void beLoginInfo(const uint8_t *, uint8_t, uint8_t) override {}
    void beBinaryReply(const uint8_t *, const uint8_t *, size_t) override {}
    void beCliReply(const uint8_t *, const char *) override {}
    void beRegionsReply(const uint8_t *, const uint8_t *, size_t) override {}
    void beLinkState(bool up, const char *) override
    {
        if (up)
            ++configures;
    }
    void beTerminalLine(const char *) override {}
};
PacketServer *server;
void push(meshtastic_FromRadio r, WadaMeshtasticBackend &backend)
{
    assert(server->sendPacket(DataPacket<meshtastic_FromRadio>(1, r)));
    backend.loop(nowMs);
}
meshtastic_ToRadio take()
{
    auto p = server->receivePacket();
    assert(p);
    return static_cast<DataPacket<meshtastic_ToRadio> *>(p.get())->getData();
}
void drain()
{
    while (server->hasData())
        take();
}
std::vector<uint8_t> unbase64(const char *value)
{
    std::vector<uint8_t> out;
    uint32_t bits = 0;
    unsigned count = 0;
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    for (; *value; ++value) {
        const auto n = alphabet.find(*value);
        assert(n != std::string::npos);
        bits = (bits << 6) | n;
        count += 6;
        if (count >= 8) {
            count -= 8;
            out.push_back(bits >> count);
        }
    }
    return out;
}
meshtastic_FromRadio text(uint32_t from, uint32_t to, uint32_t id, const char *message)
{
    meshtastic_FromRadio r = {};
    r.which_payload_variant = meshtastic_FromRadio_packet_tag;
    auto &p = r.packet;
    p.id = id;
    p.from = from;
    p.to = to;
    p.channel = 0;
    p.has_rx_time = true;
    p.rx_time = 1793220000;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p.decoded.payload.size = strlen(message);
    memcpy(p.decoded.payload.bytes, message, strlen(message));
    return r;
}
meshtastic_FromRadio ack(uint32_t from, uint32_t id, meshtastic_Routing_Error error = meshtastic_Routing_Error_NONE)
{
    auto r = text(from, 0x11111111, 10 + id, "");
    auto &d = r.packet.decoded;
    d.portnum = meshtastic_PortNum_ROUTING_APP;
    d.request_id = id;
    meshtastic_Routing route = {};
    route.which_variant = meshtastic_Routing_error_reason_tag;
    route.error_reason = error;
    auto s = pb_ostream_from_buffer(d.payload.bytes, sizeof(d.payload.bytes));
    assert(pb_encode(&s, meshtastic_Routing_fields, &route));
    d.payload.size = s.bytes_written;
    return r;
}
void test_native_telemetry(WadaMeshtasticBackend &, MeshBackendEvents &);
void test_native_regions(WadaMeshtasticBackend &);
void test_native_nodes(WadaMeshtasticBackend &);
void test_native_wifi_bridge(WadaMeshtasticBackend &);
void test_native_node_restart(MeshBackendEvents &);
void test_native_status(MeshBackendEvents &);
int main()
{
    using DS = WadaDelivery::State;
    for (const auto state : {DS::Pending, DS::Sent, DS::Delivered, DS::Failed, DS::Mesh, DS::Relayed, DS::Unconfirmed})
        assert(WadaDeliveryUI::state(WadaDeliveryUI::stored(state)) == state);
    assert(WadaDeliveryUI::state(0) == DS::Unconfirmed);
    assert(WadaDeliveryUI::restored(1) == 3 && WadaDeliveryUI::restored(4) == 3);
    for (uint8_t state : {2, 3, 5, 6, 7})
        assert(WadaDeliveryUI::restored(state) == state);
    assert(WadaDeliveryUI::state(2) == DS::Delivered); // Existing v7 receipt values remain valid.
    assert(!strcmp(WadaDelivery::label(DS::Pending), "Sending..."));
    assert(!strcmp(WadaDelivery::label(DS::Sent), "Sending..."));
    assert(!strcmp(WadaDelivery::label(DS::Mesh), "Delivered to mesh"));
    assert(!strcmp(WadaDelivery::label(DS::Relayed), "Relayed, not confirmed by recipient"));
    assert(!strcmp(WadaDelivery::label(DS::Delivered), "Delivered to recipient"));
    assert(!strcmp(WadaDelivery::label(DS::Failed, 5), "Failed to deliver to mesh"));
    assert(!strcmp(WadaDelivery::label(DS::Failed, 7), "Message is too large to send"));
    assert(!WadaDelivery::retryable(DS::Failed, 7));
    assert(WadaDelivery::retryable(DS::Failed, 250));
    assert(WadaDelivery::retryable(DS::Failed, WadaDelivery::Interrupted));
    assert(WadaDelivery::retryable(DS::Failed, WadaDelivery::TimedOut));
    assert(!strcmp(WadaDelivery::label(DS::Failed, WadaDelivery::TimedOut), "Delivery not confirmed"));
    assert(!strcmp(WadaDelivery::label(DS::Failed, WadaDelivery::ChannelChanged), "Channel changed before sending"));
    const std::pair<uint8_t, const char *> labels[] = {
        {4, "No radio interface"},     {6, "Channel/key mismatch"},      {8, "No app response"},
        {9, "Duty cycle limit"},       {32, "Invalid request"},          {33, "Not authorized"},
        {36, "Admin session expired"}, {37, "Admin key not authorized"}, {38, "Rate limited"}};
    for (const auto &entry : labels) {
        assert(!strcmp(WadaDelivery::label(DS::Failed, entry.first), entry.second));
        assert(WadaDelivery::retryable(DS::Failed, entry.first));
    }
    assert(!WadaDelivery::retryable(DS::Delivered));
    assert(WadaDelivery::retryable(DS::Relayed));
    server = PacketServer::init();
    Sink sink;
    auto backend = std::make_unique<WadaMeshtasticBackend>();
    NodePrefs prefs = {};
    uint8_t selfKey[32];
    backend->begin(sink, prefs, selfKey);
    auto config = take();
    assert(config.which_payload_variant == meshtastic_ToRadio_want_config_id_tag);
    assert(config.want_config_id);
    ContactInfo peer{};
    WadaMeshtasticBackend::nodeKey(0x22222222, peer.id.pub_key);
    uint32_t a, t, fp;
    assert(!backend->sendDirect(peer, 0, "early", a, t, fp));
    meshtastic_FromRadio r = {};
    r.which_payload_variant = meshtastic_FromRadio_my_info_tag;
    r.my_info.my_node_num = 0x11111111;
    push(r, *backend);
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_config_tag;
    r.config.which_payload_variant = meshtastic_Config_lora_tag;
    r.config.payload_variant.lora.use_preset = true;
    r.config.payload_variant.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    push(r, *backend);
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    r.config_complete_id = config.want_config_id + 1;
    push(r, *backend);
    assert(!backend->ownsPrefs());
    r.config_complete_id = config.want_config_id;
    push(r, *backend);
    assert(backend->ownsPrefs());
    assert(sink.configures == 1);
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_channel_tag;
    r.channel.index = 0;
    r.channel.role = meshtastic_Channel_Role_PRIMARY;
    r.channel.has_settings = true;
    strcpy(r.channel.settings.name, "Public");
    push(r, *backend);
    assert(sink.channels == 1);
    r.channel.index = 1;
    r.channel.role = meshtastic_Channel_Role_SECONDARY;
    push(r, *backend);
    assert(sink.channelLabels[0] == "Public [0]" && sink.channelLabels[1] == "Public [1]");
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    r.node_info.num = 0x22222222;
    r.node_info.has_user = true;
    strcpy(r.node_info.user.long_name, "Peer");
    push(r, *backend);
    assert(sink.contacts == 1);
    assert(WadaMeshtasticBackend::keyNode(sink.lastContact.id.pub_key) == 0x22222222);
    assert(!backend->canMessage(0x22222222));
    assert(!backend->sendDirect(peer, 0, "missing key", a, t, fp));
    char keylessQr[512];
    assert(!backend->contactUrl(0x22222222, keylessQr, sizeof keylessQr));
    assert(backend->requestUserInfo(0x22222222));
    auto infoRequest = take();
    assert(infoRequest.packet.to == 0x22222222 && infoRequest.packet.decoded.portnum == meshtastic_PortNum_NODEINFO_APP &&
           infoRequest.packet.decoded.want_response && !infoRequest.packet.want_ack);
    uint8_t stable[32];
    memcpy(stable, sink.lastContact.id.pub_key, 32);
    r.node_info.user.public_key.size = 32;
    memset(r.node_info.user.public_key.bytes, 7, 32);
    push(r, *backend);
    assert(!memcmp(stable, sink.lastContact.id.pub_key, 32));
    assert(backend->sendDirect(peer, 0, "hello", a, t, fp));
    assert(a && a == fp);
    auto sent = take();
    assert(sent.packet.to == 0x22222222);
    assert(sent.packet.from == 0);
    assert(sent.packet.want_ack);
    assert(sent.packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP);
    assert(sent.packet.decoded.payload.size == 5);
    uint8_t wire[512];
    auto os = pb_ostream_from_buffer(wire, sizeof(wire));
    assert(pb_encode(&os, meshtastic_ToRadio_fields, &sent));
    meshtastic_ToRadio roundtrip = {};
    auto is = pb_istream_from_buffer(wire, os.bytes_written);
    assert(pb_decode(&is, meshtastic_ToRadio_fields, &roundtrip));
    assert(roundtrip.packet.id == a);
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
    r.queueStatus.mesh_packet_id = a;
    push(r, *backend);
    assert(ui.deliveries.back().second == 1); // Queue acceptance is still Sending.
    push(ack(0x11111111, a), *backend);
    assert(sink.acks == 0);
    assert(ui.deliveries.back().second == 5); // An implicit DM ACK is not recipient delivery.
    push(r, *backend);
    assert(ui.deliveries.back().second == 5); // Late queue status cannot undo relay evidence.
    r.queueStatus.res = 1;
    push(r, *backend);
    assert(ui.deliveries.back().second == 5); // Nor can a stale generic queue error.
    push(ack(0x11111111, a, meshtastic_Routing_Error_MAX_RETRANSMIT), *backend);
    assert(ui.deliveries.back().second == 5);
    push(ack(0x99999999, a), *backend);
    assert(sink.acks == 0);
    auto invalidAck = ack(0x22222222, a);
    invalidAck.packet.ack_proof_status = meshtastic_MeshPacket_AckProofStatus_ACK_PROOF_INVALID;
    push(invalidAck, *backend);
    assert(ui.deliveries.back().second == 5);
    push(ack(0x22222222, a), *backend);
    assert(sink.acks == 1);
    assert(ui.deliveries.back().second == 2);
    push(ack(0x11111111, a, meshtastic_Routing_Error_NO_ROUTE), *backend);
    assert(ui.deliveries.back().second == 2);
    push(text(0x22222222, 0x11111111, 101, "incoming"), *backend);
    assert(sink.direct == 1 && sink.lastText == "incoming");
    assert(sink.timestamp == 1793220000);
    push(text(0x22222222, 0x11111111, 101, "incoming"), *backend);
    assert(sink.direct == 1);
    push(text(0x33333333, 0x11111111, 101, "same id, different sender"), *backend);
    assert(sink.direct == 2);
    push(text(0x22222222, 0x44444444, 102, "not for this node"), *backend);
    assert(sink.direct == 2);
    push(text(0x22222222, 0xffffffff, 103, "public"), *backend);
    assert(sink.channel == 1 && sink.lastText == "Peer: public");
    r = text(0x22222222, 0x11111111, 104, "");
    r.packet.decoded.portnum = meshtastic_PortNum_POSITION_APP;
    meshtastic_Position position = {};
    position.has_latitude_i = position.has_longitude_i = true;
    position.latitude_i = 373000000;
    position.longitude_i = -1220000000;
    auto positionStream = pb_ostream_from_buffer(r.packet.decoded.payload.bytes, sizeof(r.packet.decoded.payload.bytes));
    assert(pb_encode(&positionStream, meshtastic_Position_fields, &position));
    r.packet.decoded.payload.size = positionStream.bytes_written;
    push(r, *backend);
    assert(sink.lastContact.gps_lat == 37300000 && sink.lastContact.gps_lon == -122000000);
    char contactQr[768];
    assert(backend->contactUrl(0x22222222, contactQr, sizeof(contactQr)));
    assert(strstr(contactQr, "https://meshtastic.org/v/#") == contactQr);
    auto contactBytes = unbase64(strchr(contactQr, '#') + 1);
    meshtastic_SharedContact shared = {};
    auto contactStream = pb_istream_from_buffer(contactBytes.data(), contactBytes.size());
    assert(pb_decode(&contactStream, meshtastic_SharedContact_fields, &shared));
    assert(shared.node_num == 0x22222222 && shared.user.public_key.size == 32);
    char channelQr[1024];
    assert(backend->channelUrl(0, channelQr, sizeof(channelQr)));
    assert(!backend->channelUrl(8, channelQr, sizeof(channelQr)));
    assert(!backend->contactUrl(0, contactQr, sizeof(contactQr)));
    ChannelDetails channel = sink.channelDetails[0];
    assert(backend->sendChannel(0, channel, 0, "Me", "public send", fp));
    sent = take();
    assert(sent.packet.to == 0xffffffff && sent.packet.channel == 0 && sent.packet.want_ack);
    assert(!memcmp(sent.packet.decoded.payload.bytes, "public send", 11));
    const auto channelId = fp;
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
    r.queueStatus.mesh_packet_id = channelId;
    push(r, *backend);
    assert(ui.deliveries.back().second == 1);
    const auto beforeWrongRelay = ui.deliveries.size();
    push(ack(0x99999999, channelId), *backend);
    assert(ui.deliveries.size() == beforeWrongRelay);
    push(ack(0x11111111, channelId), *backend);
    assert(ui.deliveries.back().second == 4);
    push(r, *backend);
    assert(ui.deliveries.back().second == 4);
    assert(sink.acks == 1); // Channel delivery never triggers recipient-ACK callbacks.
    assert(!backend->sendChannel(8, channel, 0, "Me", "bad", fp));
    const std::string maxText(233, 'x');
    assert(backend->sendDirect(peer, 0, maxText.c_str(), a, t, fp));
    sent = take();
    assert(sent.packet.decoded.payload.size == 233);
    const std::string longText(234, 'x');
    assert(!backend->sendDirect(peer, 0, longText.c_str(), a, t, fp));
    assert(backend->sendDirect(peer, 0, "fail", a, t, fp));
    take();
    push(ack(0x33333333, a, meshtastic_Routing_Error_NO_ROUTE), *backend);
    assert(ui.deliveries.back().second == 3);
    assert(ui.errors.back() == meshtastic_Routing_Error_NO_ROUTE); // Intermediate NAKs are legitimate.
    const auto failedId = a;
    assert(backend->sendDirect(peer, 0, "next message", a, t, fp));
    take();
    push(ack(0x22222222, failedId), *backend);
    assert(ui.deliveries.back().second == 2); // A late real receipt can resolve a failure.
    assert(backend->sendDirect(peer, 0, "queue", a, t, fp));
    take();
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
    r.queueStatus.mesh_packet_id = a;
    r.queueStatus.res = 1;
    push(r, *backend);
    assert(ui.deliveries.back().second == 3);
    const auto outgoingBefore = sink.outgoing;
    push(text(0x11111111, 0xffffffff, 0x70000000, "phone send"), *backend);
    assert(sink.outgoing == outgoingBefore + 1);
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
    r.queueStatus.mesh_packet_id = 0x70000000;
    push(r, *backend);
    assert(ui.deliveries.back().first == 0x70000000 && ui.deliveries.back().second == 6);
    assert(backend->sendDirect(peer, 0, "oversized on wire", a, t, fp));
    take();
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
    r.queueStatus.mesh_packet_id = a;
    r.queueStatus.res = 32; // Internal ERRNO_UNKNOWN is not Routing.BAD_REQUEST.
    push(r, *backend);
    assert(ui.errors.back() == 0);
    push(ack(0x11111111, a, meshtastic_Routing_Error_TOO_LARGE), *backend);
    assert(ui.errors.back() == meshtastic_Routing_Error_TOO_LARGE);
    assert(!WadaDelivery::retryable(WadaDelivery::State::Failed, ui.errors.back()));
    assert(backend->sendDirect(peer, 0, "peer lacks key", a, t, fp));
    take();
    push(ack(0x11111111, a), *backend);
    push(ack(0x22222222, a, meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY), *backend);
    assert(ui.deliveries.back().second == 3 && ui.errors.back() == 35);
    assert(!strcmp(WadaDelivery::label(WadaDelivery::State::Failed, 35), "Recipient needs your key"));
    assert(!strcmp(WadaDelivery::label(WadaDelivery::State::Failed, 39), "Recipient key unavailable"));
    assert(backend->setBlePin(123456));
    sent = take();
    assert(sent.packet.to == 0x11111111 && sent.packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP);
    meshtastic_AdminMessage admin = {};
    is = pb_istream_from_buffer(sent.packet.decoded.payload.bytes, sent.packet.decoded.payload.size);
    assert(pb_decode(&is, meshtastic_AdminMessage_fields, &admin));
    assert(admin.which_payload_variant == meshtastic_AdminMessage_set_config_tag &&
           admin.set_config.payload_variant.bluetooth.fixed_pin == 123456);
    drain();
    assert(backend->sendAdvert(false));
    sent = take();
    assert(sent.packet.to == 0xffffffff && sent.packet.decoded.portnum == meshtastic_PortNum_NODEINFO_APP &&
           !sent.packet.decoded.want_response && sent.packet.hop_limit == 1);
    drain();
    assert(backend->sendNodeInfo(false, true));
    sent = take();
    assert(sent.packet.decoded.want_response && sent.packet.hop_limit == 0);
    meshtastic_ChannelSettings_psk_t key = {};
    assert(wada::channelKey("AQ==", true, key) && key.size == 1 && key.bytes[0] == 1);
    assert(wada::channelKey("AAAAAAAAAAAAAAAAAAAAAA==", true, key) && key.size == 16);
    assert(wada::channelKey("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", true, key) && key.size == 32);
    assert(!wada::channelKey("AR==", true, key));
    assert(!wada::channelKey("AQ===", true, key));
    assert(!wada::channelKey("0000", false, key));
    assert(wada::channelKey("01", false, key) && key.size == 1);
    assert(wada::namedRegion("US") == 1 && wada::namedPreset("long-fast") == 0);
    unsigned numeric = 0;
    assert(!wada::unsignedValue("4294967296", 65535, numeric));
    assert(!wada::unsignedValue("-1", 7, numeric));
    assert(backend->runNodeCli("lora hop 4"));
    sent = take();
    is = pb_istream_from_buffer(sent.packet.decoded.payload.bytes, sent.packet.decoded.payload.size);
    admin = {};
    assert(pb_decode(&is, meshtastic_AdminMessage_fields, &admin));
    assert(admin.set_config.which_payload_variant == meshtastic_Config_lora_tag &&
           admin.set_config.payload_variant.lora.hop_limit == 4);
    assert(backend->runNodeCli("lora hop 8"));
    assert(!server->hasData());
    assert(backend->runNodeCli("nodeinfo interval 3600"));
    sent = take();
    is = pb_istream_from_buffer(sent.packet.decoded.payload.bytes, sent.packet.decoded.payload.size);
    admin = {};
    assert(pb_decode(&is, meshtastic_AdminMessage_fields, &admin));
    assert(admin.set_config.which_payload_variant == meshtastic_Config_device_tag &&
           admin.set_config.payload_variant.device.node_info_broadcast_secs == 3600);
    assert(backend->runNodeCli("nodeinfo interval 0"));
    auto defaultInterval = take();
    auto defaultStream =
        pb_istream_from_buffer(defaultInterval.packet.decoded.payload.bytes, defaultInterval.packet.decoded.payload.size);
    assert(pb_decode(&defaultStream, meshtastic_AdminMessage_fields, &admin));
    assert(admin.which_payload_variant == meshtastic_AdminMessage_set_config_tag &&
           admin.set_config.payload_variant.device.node_info_broadcast_secs == wada::nodeInfoDefaultSeconds);
    assert(backend->runNodeCli("nodeinfo interval 5"));
    assert(!server->hasData());
    auto presence = text(0x22222222, 0xffffffff, 0x91000001, "unknown hops");
    push(presence, *backend);
    assert(sink.lastHops == 0xff && !backend->nodeInfo(0x22222222)->has_hops_away);
    presence.packet.id++;
    presence.packet.decoded.has_bitfield = true;
    push(presence, *backend);
    assert(sink.lastHops == 0 && backend->nodeInfo(0x22222222)->has_hops_away);
    presence.packet.id++;
    presence.packet.via_mqtt = true;
    push(presence, *backend);
    assert(sink.lastHops == 0xff && sink.lastSnr == INT8_MIN && sink.lastRssi == 0);
    assert(backend->runNodeCli("position interval 10"));
    sent = take();
    admin = {};
    is = pb_istream_from_buffer(sent.packet.decoded.payload.bytes, sent.packet.decoded.payload.size);
    assert(pb_decode(&is, meshtastic_AdminMessage_fields, &admin) &&
           admin.set_config.payload_variant.position.position_broadcast_secs == 10);
    assert(!backend->setOwner("A valid long name", "12345"));
    // Effective channel identity follows firmware expansion, not slot or region.
    meshtastic_Channel fixture[8] = {};
    fixture[0].has_settings = true;
    fixture[0].role = meshtastic_Channel_Role_PRIMARY;
    fixture[0].settings.psk.size = 1;
    fixture[0].settings.psk.bytes[0] = 1;
    meshtastic_Config_LoRaConfig radio = {};
    radio.use_preset = true;
    uint8_t original[32], changed[32];
    assert(wada::channelIdentity(fixture, radio, 0, original));
    const uint8_t expanded[] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};
    fixture[0].settings.psk.size = 16;
    memcpy(fixture[0].settings.psk.bytes, expanded, 16);
    assert(wada::channelIdentity(fixture, radio, 0, changed) && !memcmp(original, changed, 32));
    fixture[0].settings.use_aead = true;
    assert(wada::channelIdentity(fixture, radio, 0, changed) && memcmp(original, changed, 32));
    assert(wada::rememberChannelIntent(0xf0000002, 0, original));
    assert(!wada::consumeChannelIntent(0xf0000002, 0, changed));
    fixture[0].settings.use_aead = false;
    radio.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    assert(wada::channelIdentity(fixture, radio, 0, changed) && !memcmp(original, changed, 32));
    radio.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW;
    assert(wada::channelIdentity(fixture, radio, 0, changed) && memcmp(original, changed, 32));
    radio.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    fixture[1] = fixture[0];
    fixture[1].index = 1;
    fixture[1].role = meshtastic_Channel_Role_SECONDARY;
    fixture[1].settings.psk.size = 0;
    assert(wada::channelIdentity(fixture, radio, 1, changed) && !memcmp(original, changed, 32));
    fixture[0].settings.psk.size = 1;
    fixture[0].settings.psk.bytes[0] = 0;
    assert(wada::channelIdentity(fixture, radio, 0, original));
    fixture[0].settings.psk.size = 0;
    assert(wada::channelIdentity(fixture, radio, 0, changed) && !memcmp(original, changed, 32));
    assert(!wada::channelIdentity(fixture, radio, 8, changed));
    assert(wada::namedRegion("UA_868") == -1 && wada::namedRegion("EU_874") == -1 && wada::namedRegion("31") == -1);
    assert(wada::namedRegion("16") == 16 && wada::namedRegion("32") == 32);
    unsigned bandwidth = 0;
    assert(wada::bandwidthCode("31.25", false, bandwidth) && bandwidth == 31);
    assert(wada::bandwidthCode("7.8", false, bandwidth) && bandwidth == 8);
    assert(!wada::bandwidthCode("31", false, bandwidth) && !wada::bandwidthCode("nan", false, bandwidth));
    assert(wada::bandwidthCode("812.5", true, bandwidth) && bandwidth == 800);
    assert(!wada::bandwidthCode("1625", true, bandwidth));
    assert(wada::bandwidthCode("0", true, bandwidth) && bandwidth == 0);
    assert(wada::bandwidthCode("0", false, bandwidth) && bandwidth == 0);
    assert(!wada::bandwidthCode("812.5", false, bandwidth) && wada::bandwidthKHz(0) == 250);
    assert(wada::presetCodingRate(0) == 5 && wada::presetCodingRate(1) == 8 && wada::presetCodingRate(12) == 6);
    assert(wada::normalizedCodingRate(0, 8) == 8 && wada::normalizedCodingRate(1, 8) == 0 &&
           wada::normalizedCodingRate(12, 5) == 0);
    assert(backend->runNodeCli("lora cr 8"));
    sent = take();
    admin = {};
    is = pb_istream_from_buffer(sent.packet.decoded.payload.bytes, sent.packet.decoded.payload.size);
    assert(pb_decode(&is, meshtastic_AdminMessage_fields, &admin) && admin.set_config.payload_variant.lora.coding_rate == 8);
    assert(backend->runNodeCli("lora hop 0"));
    sent = take();
    // The private queue sidecar rejects channel changes at dispatch, then frees the entry.
    assert(wada::rememberChannelIntent(0xf0000001, 0, original));
    assert(!wada::consumeChannelIntent(0xf0000001, 1, original));
    assert(wada::rememberChannelIntent(0xf0000001, 0, original));
    changed[0] ^= 1;
    assert(!wada::consumeChannelIntent(0xf0000001, 0, changed));
    assert(wada::rememberChannelIntent(0xf0000001, 0, original));
    assert(wada::consumeChannelIntent(0xf0000001, 0, original));
    assert(!WadaDelivery::retryable(WadaDelivery::State::Failed, WadaDelivery::ChannelChanged));
    // Add shares carry only channels, never the sender's LoRa configuration.
    assert(backend->channelUrl(0, channelQr, sizeof channelQr, true));
    assert(strstr(channelQr, "?add=true#"));
    auto addBytes = unbase64(strchr(channelQr, '#') + 1);
    meshtastic_ChannelSet addSet = {};
    is = pb_istream_from_buffer(addBytes.data(), addBytes.size());
    assert(pb_decode(&is, meshtastic_ChannelSet_fields, &addSet) && !addSet.has_lora_config && addSet.settings_count == 1);
    assert(backend->channelUrl(1, channelQr, sizeof channelQr, false));
    auto replaceBytes = unbase64(strchr(channelQr, '#') + 1);
    meshtastic_ChannelSet replaceSet = {};
    is = pb_istream_from_buffer(replaceBytes.data(), replaceBytes.size());
    assert(pb_decode(&is, meshtastic_ChannelSet_fields, &replaceSet) && replaceSet.has_lora_config &&
           replaceSet.settings_count == 2);
    // A trace response must match BOTH the request and destination. Missing SNR is not zero.
    drain();
    const uint32_t trace = backend->tracePing(peer.id.pub_key);
    assert(trace);
    take();
    assert(!backend->tracePing(peer.id.pub_key));
    meshtastic_RouteDiscovery discovery = {};
    discovery.route_count = 1;
    discovery.route[0] = 0xffffffff;
    discovery.snr_towards_count = 2;
    discovery.snr_towards[0] = INT8_MIN;
    discovery.snr_towards[1] = 0;
    discovery.route_back_count = 1;
    discovery.route_back[0] = 0x44444444;
    discovery.snr_back_count = 2;
    discovery.snr_back[0] = 4;
    discovery.snr_back[1] = -8;
    auto traceResponse = text(0x33333333, 0x11111111, 0x90000001, "");
    traceResponse.packet.decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP;
    traceResponse.packet.decoded.request_id = trace;
    auto routeStream =
        pb_ostream_from_buffer(traceResponse.packet.decoded.payload.bytes, sizeof traceResponse.packet.decoded.payload.bytes);
    assert(pb_encode(&routeStream, meshtastic_RouteDiscovery_fields, &discovery));
    traceResponse.packet.decoded.payload.size = routeStream.bytes_written;
    const unsigned beforeTrace = ui.traceCount;
    push(traceResponse, *backend);
    assert(ui.traceCount == beforeTrace);
    traceResponse.packet.from = 0x22222222;
    traceResponse.packet.id++;
    nowMs += 1200;
    push(traceResponse, *backend);
    assert(ui.traceCount == beforeTrace + 1 && ui.trace.find("Outbound") != std::string::npos &&
           ui.trace.find("Return") != std::string::npos);
    assert(ui.trace.find("Unknown hop") != std::string::npos && ui.trace.find("!ffffffff") == std::string::npos);
    assert(ui.trace.find("Signal unknown") != std::string::npos && ui.trace.find("SNR 0.00 dB") != std::string::npos);
    assert(ui.trace.find("!44444444") != std::string::npos && ui.trace.find("Round trip 1.20 s") != std::string::npos);
    // Correlated transport failures settle one trace immediately; unrelated failures do not.
    for (unsigned failure = 0; failure < 3; ++failure) {
        nowMs += 30001;
        backend->loop(nowMs);
        drain();
        const auto request = backend->tracePing(peer.id.pub_key);
        assert(request);
        take();
        const auto before = ui.traceCount;
        push(ack(0x22222222, request + 1, meshtastic_Routing_Error_NO_ROUTE), *backend);
        assert(ui.traceCount == before);
        meshtastic_FromRadio error{};
        if (failure == 0) {
            error.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
            error.queueStatus.res = 1;
            error.queueStatus.mesh_packet_id = request;
        } else if (failure == 1) {
            error.which_payload_variant = meshtastic_FromRadio_clientNotification_tag;
            error.clientNotification.level = meshtastic_LogRecord_Level_ERROR;
            error.clientNotification.has_reply_id = true;
            error.clientNotification.reply_id = request;
            strcpy(error.clientNotification.message, "Request rejected");
        } else {
            error = ack(0x22222222, request, meshtastic_Routing_Error_NO_ROUTE);
        }
        push(error, *backend);
        assert(ui.traceCount == before + 1 && ui.trace.find("Traceroute request failed:") != std::string::npos);
        push(error, *backend);
        assert(ui.traceCount == before + 1);
    }
    nowMs += 30001;
    backend->loop(nowMs);
    drain();
    assert(backend->tracePing(peer.id.pub_key));
    take();
    const auto beforeExpiry = ui.traceCount;
    nowMs += 180001;
    backend->loop(nowMs);
    assert(ui.traceCount == beforeExpiry + 1 && ui.trace.find("No response was received") != std::string::npos);
    drain();
    test_native_telemetry(*backend, sink);
    test_native_regions(*backend);
    test_native_nodes(*backend);
    test_native_wifi_bridge(*backend);
    for (const auto mode :
         {meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT, meshtastic_Config_PositionConfig_GpsMode_ENABLED,
          meshtastic_Config_PositionConfig_GpsMode_DISABLED}) {
        r = {};
        r.which_payload_variant = meshtastic_FromRadio_config_tag;
        r.config.payload_variant.position.gps_mode = mode;
        r.config.which_payload_variant = meshtastic_Config_position_tag;
        push(r, *backend);
        assert(sink.gpsEnabled == (mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED));
    }

    // Authoritative rich snapshots clear missing facts; private thin deltas do not.
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    r.node_info = *backend->nodeInfo(0x11111111);
    r.node_info.has_position = true;
    r.node_info.position.has_latitude_i = true;
    r.node_info.position.latitude_i = 123456789;
    r.node_info.has_device_metrics = true;
    r.node_info.device_metrics.has_battery_level = true;
    r.node_info.device_metrics.battery_level = 81;
    push(r, *backend);
    r.id = wada::NodeDelta;
    r.node_info.has_position = false;
    r.node_info.has_device_metrics = false;
    push(r, *backend);
    assert(backend->nodeInfo(0x11111111)->has_position && backend->nodeInfo(0x11111111)->has_device_metrics);
    r.id = 0;
    push(r, *backend);
    assert(!backend->nodeInfo(0x11111111)->has_position && !backend->nodeInfo(0x11111111)->has_device_metrics);

    r = {};
    r.which_payload_variant = meshtastic_FromRadio_config_tag;
    r.config.which_payload_variant = meshtastic_Config_security_tag;
    r.config.payload_variant.security.is_managed = true;
    push(r, *backend);
    assert(backend->isManaged());
    assert(!backend->setBlePin(123456) && !backend->contactChanged(peer) && !backend->contactRemoved(peer.id.pub_key) &&
           !server->hasData());
    r.config.payload_variant.security.is_managed = false;
    push(r, *backend);
    assert(!backend->isManaged());
    for (unsigned i = 0; i < 25; ++i)
        assert(backend->sendAdvert(false));
    assert(!backend->sendDirect(peer, 0, "queue full", a, t, fp));
    assert(!a);
    drain();
    nowMs = 0xfffffff0;
    backend->loop(nowMs);
    drain();
    assert(backend->sendDirect(peer, 0, "rollover", a, t, fp));
    take();
    nowMs = uint32_t(nowMs + 900001);
    backend->loop(nowMs);
    assert(ui.deliveries.back().first == a && ui.deliveries.back().second == 3);
    assert(ui.errors.back() == WadaDelivery::TimedOut);
    push(ack(0x11111111, a), *backend);
    assert(ui.deliveries.back().second == 5); // Late relay evidence improves an expired DM status.
    push(ack(0x22222222, a), *backend);
    assert(ui.deliveries.back().second == 2); // Receipt can arrive after the fallback deadline.
    drain();
    assert(backend->sendChannel(0, channel, 0, "Me", "late channel relay", fp));
    take();
    nowMs = uint32_t(nowMs + 900001);
    backend->loop(nowMs);
    assert(ui.deliveries.back().first == fp && ui.deliveries.back().second == 3);
    push(ack(0x11111111, fp), *backend);
    assert(ui.deliveries.back().second == 4); // Late implicit channel ACK proves delivery to mesh.
    drain();
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_clientNotification_tag;
    strcpy(r.clientNotification.message, "WADAMESH receive overflow: 2 dropped; resync required");
    push(r, *backend);
    assert(!backend->ownsPrefs());
    config = take();
    assert(config.which_payload_variant == meshtastic_ToRadio_want_config_id_tag);
    const unsigned beforeUnsynced = ui.unsynced;
    push(text(0x22222222, 0xffffffff, 0x90000006, "during sync"), *backend);
    assert(ui.unsynced == beforeUnsynced + 1);
    // A complete native snapshot removes stale nodes, preserving existing conversations in the UI model.
    auto livePeer = *backend->nodeInfo(0x22222222);
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    r.node_info = livePeer;
    push(r, *backend);
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    r.config_complete_id = config.want_config_id;
    push(r, *backend);
    assert(backend->ownsPrefs() && backend->nodeInfo(0x22222222) && !backend->nodeInfo(0x33333333) && sink.removed > 0);
    // A pending explicit removal must not erase a persisted key-change warning.
    auto changedUser = livePeer.user;
    changedUser.public_key.size = 32;
    memset(changedUser.public_key.bytes, 0xa5, 32);
    auto claim = text(0x22222222, 0xffffffff, 0x99000001, "");
    claim.packet.decoded.portnum = meshtastic_PortNum_NODEINFO_APP;
    auto claimStream = pb_ostream_from_buffer(claim.packet.decoded.payload.bytes, sizeof claim.packet.decoded.payload.bytes);
    assert(pb_encode(&claimStream, meshtastic_User_fields, &changedUser));
    claim.packet.decoded.payload.size = claimStream.bytes_written;
    push(claim, *backend);
    assert(backend->nodeKeyMismatch(0x22222222));
    // Queue admission is not removal confirmation. Rejection must retain the native row.
    drain();
    const unsigned beforeRemove = sink.removed;
    assert(backend->contactRemoved(peer.id.pub_key));
    assert(backend->nodeInfo(0x22222222) && sink.removed == beforeRemove && backend->nodeKeyMismatch(0x22222222));
    auto removal = take();
    assert(removal.packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP);
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
    r.queueStatus.res = 1;
    r.queueStatus.mesh_packet_id = removal.packet.id;
    push(r, *backend);
    assert(backend->nodeInfo(0x22222222) && sink.removed == beforeRemove && backend->nodeKeyMismatch(0x22222222));
    assert(backend->contactRemoved(peer.id.pub_key));
    removal = take();
    push(ack(0x22222222, removal.packet.id), *backend);
    assert(backend->ownsPrefs() && backend->nodeInfo(0x22222222));
    push(ack(0x11111111, removal.packet.id + 1), *backend);
    assert(backend->ownsPrefs());
    // A successful local Admin acknowledgement requests a fresh authoritative snapshot.
    push(ack(0x11111111, removal.packet.id), *backend);
    assert(!backend->ownsPrefs());
    config = take();
    assert(config.which_payload_variant == meshtastic_ToRadio_want_config_id_tag);
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    r.config_complete_id = config.want_config_id;
    push(r, *backend);
    assert(backend->ownsPrefs() && !backend->nodeInfo(0x22222222) && sink.removed == beforeRemove + 1 &&
           !backend->nodeKeyMismatch(0x22222222));
    // Private native removal events also confirm an explicit request without a resync.
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    r.node_info = livePeer;
    push(r, *backend);
    drain();
    assert(backend->contactRemoved(peer.id.pub_key));
    take();
    r.id = wada::NodeGone;
    push(r, *backend);
    assert(!backend->nodeInfo(0x22222222) && sink.removed == beforeRemove + 2);
    drain();
    backend.reset();
    test_native_node_restart(sink);
    test_native_status(sink);
    puts("PASS: WADAMESH backend, PacketAPI queues and official protobuf round-trips");
}
