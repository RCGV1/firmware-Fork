// Tests the local model over the actual shared PacketClient/PacketServer boundary.
// These assertions do not validate native routing, encryption or radio behavior.
#include "DesktopRadio.h"
#include "WadaChannelIdentity.h"
#include "WadaChannelIntent.h"
#include "WadaNodeActions.h"
#include "mesh/generated/meshtastic/admin.pb.h"
#include "mesh/generated/meshtastic/portnums.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include "wadacomms/PacketClient.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>
#include <vector>
void wadaCommsWarning(const char *message)
{
    fprintf(stderr, "%s\n", message);
}
namespace
{
constexpr uint32_t self = 0xd35c0001, peer = 0xd35c0002;
PacketClient client;
uint32_t id = 100;
std::vector<meshtastic_FromRadio> pump()
{
    std::vector<meshtastic_FromRadio> rows;
    for (unsigned i = 0; i < 40; ++i) {
        desktopRadioLoop();
        while (client.hasData())
            rows.push_back(client.receive());
    }
    return rows;
}
JsonDocument state()
{
    JsonDocument d;
    assert(deserializeJson(d, desktopSimulationState()) == DeserializationError::Ok);
    return d;
}
template <class T>
meshtastic_MeshPacket request(meshtastic_PortNum port, const T &data, const pb_msgdesc_t *fields, uint32_t target = self)
{
    meshtastic_MeshPacket p{};
    p.id = ++id;
    p.from = self;
    p.to = target;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.want_ack = true;
    p.decoded.want_response = true;
    p.decoded.portnum = port;
    auto s = pb_ostream_from_buffer(p.decoded.payload.bytes, sizeof p.decoded.payload.bytes);
    assert(pb_encode(&s, fields, &data));
    p.decoded.payload.size = s.bytes_written;
    return p;
}
std::vector<meshtastic_FromRadio> send(meshtastic_MeshPacket p)
{
    meshtastic_ToRadio t{};
    t.which_payload_variant = meshtastic_ToRadio_packet_tag;
    t.packet = p;
    assert(client.send(std::move(t)));
    return pump();
}
std::vector<meshtastic_FromRadio> admin(meshtastic_AdminMessage a, uint32_t target = self)
{
    return send(request(meshtastic_PortNum_ADMIN_APP, a, meshtastic_AdminMessage_fields, target));
}
template <class T>
T payload(const std::vector<meshtastic_FromRadio> &rows, meshtastic_PortNum port, const pb_msgdesc_t *fields,
          uint32_t requestId = 0)
{
    for (const auto &r : rows)
        if (r.which_payload_variant == meshtastic_FromRadio_packet_tag && r.packet.decoded.portnum == port &&
            (!requestId || r.packet.decoded.request_id == requestId)) {
            T t{};
            auto s = pb_istream_from_buffer(r.packet.decoded.payload.bytes, r.packet.decoded.payload.size);
            assert(pb_decode(&s, fields, &t));
            return t;
        }
    assert(!"Expected correlated response missing");
    return {};
}
void confirmedFrom(const std::vector<meshtastic_FromRadio> &rows, uint32_t requestId, uint32_t sender)
{
    unsigned seen = 0;
    for (const auto &r : rows)
        if (r.which_payload_variant == meshtastic_FromRadio_packet_tag &&
            r.packet.decoded.portnum == meshtastic_PortNum_ROUTING_APP && r.packet.decoded.request_id == requestId) {
            assert(r.packet.from == sender && r.packet.to == self);
            auto message = payload<meshtastic_Routing>({r}, meshtastic_PortNum_ROUTING_APP, meshtastic_Routing_fields, requestId);
            assert(message.which_variant == meshtastic_Routing_error_reason_tag &&
                   message.error_reason == meshtastic_Routing_Error_NONE);
            ++seen;
        }
    assert(seen == 1);
}
meshtastic_Channel channel(unsigned index)
{
    meshtastic_ToRadio t{};
    t.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    t.want_config_id = 123;
    assert(client.send(std::move(t)));
    for (const auto &r : pump())
        if (r.which_payload_variant == meshtastic_FromRadio_channel_tag && unsigned(r.channel.index) == index)
            return r.channel;
    assert(false);
    return {};
}
void configure(meshtastic_Config c)
{
    meshtastic_AdminMessage a{};
    a.which_payload_variant = meshtastic_AdminMessage_set_config_tag;
    a.set_config = c;
    admin(a);
}
} // namespace
int main()
{
    desktopRadioInit();
    client.init();
    meshtastic_ToRadio init{};
    init.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    init.want_config_id = 0x12345678;
    assert(client.send(std::move(init)));
    unsigned nodes = 0, channels = 0, complete = 0;
    for (const auto &r : pump()) {
        nodes += r.which_payload_variant == meshtastic_FromRadio_node_info_tag;
        channels += r.which_payload_variant == meshtastic_FromRadio_channel_tag;
        if (r.which_payload_variant == meshtastic_FromRadio_config_complete_id_tag) {
            assert(r.config_complete_id == 0x12345678);
            ++complete;
        }
    }
    assert(nodes == 4 && channels == 8 && complete == 1);
    assert(state()["simulation"] == true && state()["native_crypto"] == false);
    // Writes are authoritative readback, including exclusive primary role and public precision caps.
    auto ch = channel(1);
    ch.role = meshtastic_Channel_Role_PRIMARY;
    meshtastic_AdminMessage a{};
    a.which_payload_variant = meshtastic_AdminMessage_set_channel_tag;
    a.set_channel = ch;
    admin(a);
    assert(state()["primary"] == 1);
    assert(state()["channels"][0]["role"] == 2);
    ch = channel(0);
    ch.settings.has_module_settings = true;
    ch.settings.module_settings.position_precision = 32;
    a.set_channel = ch;
    admin(a);
    assert(state()["channels"][0]["position_precision"] == 15);
    assert(desktopSimulationScenario("inherited-secondary"));
    pump();
    assert(state()["channels"][1]["inherits_key"] == true);
    ch = channel(1);
    auto parent = channel(0);
    ch.role = meshtastic_Channel_Role_PRIMARY;
    ch.settings.psk = parent.settings.psk;
    a.set_channel = ch;
    admin(a);
    assert(state()["channels"][1]["psk_size"] == 32 && state()["channels"][0]["role"] == 2);
    // Owner identity retains its public key; rejected writes leave names unchanged.
    a = {};
    a.which_payload_variant = meshtastic_AdminMessage_set_owner_tag;
    strcpy(a.set_owner.long_name, "Protocol Test");
    strcpy(a.set_owner.short_name, "TEST");
    admin(a);
    assert(state()["owner"]["long_name"] == "Protocol Test");
    assert(state()["nodes"][0]["key_size"] == 32);
    assert(desktopSimulationScenario("queue-reject-next"));
    strcpy(a.set_owner.long_name, "Must Not Apply");
    auto rejected = admin(a);
    bool queueError = false;
    for (auto &r : rejected)
        queueError |= r.which_payload_variant == meshtastic_FromRadio_queueStatus_tag && r.queueStatus.res == 32;
    assert(queueError && state()["owner"]["long_name"] == "Protocol Test");
    // Fixed positions include valid zero coordinates; removing them clears native model state.
    a = {};
    a.which_payload_variant = meshtastic_AdminMessage_set_fixed_position_tag;
    a.set_fixed_position.has_latitude_i = a.set_fixed_position.has_longitude_i = true;
    admin(a);
    assert(state()["position"]["fixed_position"] == true && state()["position"]["latitude_i"] == 0);
    a = {};
    a.which_payload_variant = meshtastic_AdminMessage_remove_fixed_position_tag;
    a.remove_fixed_position = true;
    admin(a);
    assert(state()["position"]["fixed_position"] == false && state()["position"]["has_position"] == false);
    // Device interval normalization mirrors native firmware, never a host setting.
    meshtastic_Config c{};
    c.which_payload_variant = meshtastic_Config_device_tag;
    c.payload_variant.device.node_info_broadcast_secs = 1;
    configure(c);
    assert(state()["device"]["node_info_broadcast_secs"] == 3600);
    // Bluetooth/network/position fields round-trip; invalid fixed PIN cannot silently apply.
    c = {};
    c.which_payload_variant = meshtastic_Config_bluetooth_tag;
    c.payload_variant.bluetooth.enabled = true;
    c.payload_variant.bluetooth.mode = meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN;
    c.payload_variant.bluetooth.fixed_pin = 456789;
    configure(c);
    assert(state()["bluetooth"]["fixed_pin"] == 456789);
    c.payload_variant.bluetooth.fixed_pin = 1;
    configure(c);
    assert(state()["bluetooth"]["fixed_pin"] == 456789);
    c = {};
    c.which_payload_variant = meshtastic_Config_network_tag;
    c.payload_variant.network.wifi_enabled = true;
    strcpy(c.payload_variant.network.wifi_ssid, "Fixture SSID");
    strcpy(c.payload_variant.network.wifi_psk, "fixture-password");
    configure(c);
    assert(state()["network"]["wifi_enabled"] == true && state()["network"]["wifi_ssid"] == "Fixture SSID" &&
           state()["network"]["wifi_psk_length"] == 16);
    c = {};
    c.which_payload_variant = meshtastic_Config_position_tag;
    c.payload_variant.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT;
    c.payload_variant.position.position_broadcast_secs = 120;
    configure(c);
    assert(state()["position"]["gps_mode"] == 2 && state()["position"]["position_broadcast_secs"] == 120);
    a = {};
    a.which_payload_variant = meshtastic_AdminMessage_set_module_config_tag;
    a.set_module_config.which_payload_variant = meshtastic_ModuleConfig_statusmessage_tag;
    strcpy(a.set_module_config.payload_variant.statusmessage.node_status, "Protocol fixture only");
    admin(a);
    assert(state()["owner"]["node_status"] == "Protocol fixture only");
    // Current-LoRa membership excludes dormant fields and restores the previously heard slot.
    assert(desktopSimulationScenario("hear-peer"));
    pump();
    c = {};
    c.which_payload_variant = meshtastic_Config_lora_tag;
    auto &lc = c.payload_variant.lora;
    lc.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    lc.use_preset = true;
    lc.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    lc.hop_limit = 7;
    lc.bandwidth = 31.25;
    configure(c);
    assert(state()["nodes"][1]["heard_on_current_lora"] == true);
    lc.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST;
    configure(c);
    assert(state()["nodes"][1]["heard_on_current_lora"] == false);
    lc.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    configure(c);
    assert(state()["nodes"][1]["heard_on_current_lora"] == true);
    lc.region = meshtastic_Config_LoRaConfig_RegionCode_ITU2_70CM;
    lc.use_preset = true;
    lc.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW;
    configure(c);
    assert(state()["lora"]["region"] == int(meshtastic_Config_LoRaConfig_RegionCode_US));
    lc.region = meshtastic_Config_LoRaConfig_RegionCode_ITU2_2M;
    lc.use_preset = false;
    lc.spread_factor = 7;
    lc.coding_rate = 5;
    configure(c);
    assert(state()["lora"]["region"] == int(meshtastic_Config_LoRaConfig_RegionCode_US));
    // Conservative high-band policy rejects SX128x-only 1625 kHz on the T-Deck fixture.
    lc.region = meshtastic_Config_LoRaConfig_RegionCode_LORA_24;
    lc.use_preset = false;
    lc.bandwidth = 1600;
    lc.spread_factor = 7;
    lc.coding_rate = 5;
    configure(c);
    assert(state()["lora"]["region"] == int(meshtastic_Config_LoRaConfig_RegionCode_US));
    for (auto absent : {meshtastic_Config_LoRaConfig_RegionCode_UA_868, meshtastic_Config_LoRaConfig_RegionCode_EU_874,
                        meshtastic_Config_LoRaConfig_RegionCode_EU_917}) {
        lc.region = absent;
        lc.use_preset = true;
        lc.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
        configure(c);
        assert(state()["lora"]["region"] == int(meshtastic_Config_LoRaConfig_RegionCode_US));
    }
    // Node actions mutate authoritative state, preserving known keys on keyless contact updates.
    a = {};
    a.which_payload_variant = meshtastic_AdminMessage_remove_favorite_node_tag;
    a.remove_favorite_node = peer;
    admin(a);
    assert(state()["nodes"][1]["is_favorite"] == false);
    a = {};
    a.which_payload_variant = meshtastic_AdminMessage_set_favorite_node_tag;
    a.set_favorite_node = peer;
    admin(a);
    assert(state()["nodes"][1]["is_favorite"] == true);
    a = {};
    a.which_payload_variant = meshtastic_AdminMessage_toggle_muted_node_tag;
    a.toggle_muted_node = peer;
    admin(a);
    assert(state()["nodes"][1]["is_muted"] == true);
    a = {};
    a.which_payload_variant = meshtastic_AdminMessage_set_ignored_node_tag;
    a.set_ignored_node = peer;
    admin(a);
    assert(state()["nodes"][1]["is_ignored"] == true);
    meshtastic_Telemetry blocked{};
    blocked.which_variant = meshtastic_Telemetry_device_metrics_tag;
    auto ignoredRows = send(request(meshtastic_PortNum_TELEMETRY_APP, blocked, meshtastic_Telemetry_fields, peer));
    for (const auto &r : ignoredRows)
        assert(r.which_payload_variant != meshtastic_FromRadio_packet_tag || r.packet.from != peer);
    assert(state()["ignored_packets_dropped"] == 1);
    a = {};
    a.which_payload_variant = meshtastic_AdminMessage_remove_ignored_node_tag;
    a.remove_ignored_node = peer;
    admin(a);
    assert(state()["nodes"][1]["is_ignored"] == false);
    a = {};
    a.which_payload_variant = meshtastic_AdminMessage_add_contact_tag;
    a.add_contact.node_num = peer;
    a.add_contact.has_user = true;
    strcpy(a.add_contact.user.long_name, "Local alias");
    admin(a);
    assert(state()["nodes"][1]["key_size"] == 32 && state()["nodes"][1]["long_name"] == "Local alias");
    a.add_contact.manually_verified = true;
    memset(a.add_contact.user.public_key.bytes, 0x11, 32);
    a.add_contact.user.public_key.size = 32;
    admin(a);
    assert(state()["nodes"][1]["is_key_manually_verified"] == true);
    a.add_contact.manually_verified = false;
    memset(a.add_contact.user.public_key.bytes, 0x77, 32);
    strcpy(a.add_contact.user.long_name, "Untrusted replacement");
    admin(a);
    assert(state()["nodes"][1]["long_name"] == "Local alias");
    // Unheard removals revalidate favorite/heard/managed state and correlate their result.
    wada::UnheardRemoval removal{};
    removal.id = ++id;
    removal.node = peer;
    removal.self = self;
    removal.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    removal.lora.use_preset = true;
    removal.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    removal.lora.hop_limit = 7;
    removal.lora.bandwidth = 31.25;
    strcpy(removal.primaryName, "Sim private");
    assert(wada::queueUnheardRemoval(removal));
    bool kept = false;
    for (auto &r : pump())
        if (r.which_payload_variant == meshtastic_FromRadio_clientNotification_tag && r.clientNotification.reply_id == removal.id)
            kept = true;
    assert(kept && state()["node_count"] == 4);
    // All six typed variants retain correlation and optional zero/missing distinctions.
    const unsigned tags[] = {meshtastic_Telemetry_device_metrics_tag,      meshtastic_Telemetry_environment_metrics_tag,
                             meshtastic_Telemetry_air_quality_metrics_tag, meshtastic_Telemetry_power_metrics_tag,
                             meshtastic_Telemetry_local_stats_tag,         meshtastic_Telemetry_host_metrics_tag};
    assert(desktopSimulationScenario("host-peer"));
    pump();
    for (unsigned tag : tags) {
        meshtastic_Telemetry q{};
        q.which_variant = tag;
        auto p = request(meshtastic_PortNum_TELEMETRY_APP, q, meshtastic_Telemetry_fields, peer);
        auto result = payload<meshtastic_Telemetry>(send(p), meshtastic_PortNum_TELEMETRY_APP, meshtastic_Telemetry_fields, p.id);
        assert(result.which_variant == tag && result.time);
    }
    meshtastic_Telemetry q{};
    q.which_variant = meshtastic_Telemetry_device_metrics_tag;
    assert(desktopSimulationScenario("telemetry-zero"));
    auto zero =
        payload<meshtastic_Telemetry>(send(request(meshtastic_PortNum_TELEMETRY_APP, q, meshtastic_Telemetry_fields, peer)),
                                      meshtastic_PortNum_TELEMETRY_APP, meshtastic_Telemetry_fields);
    assert(zero.variant.device_metrics.has_battery_level && zero.variant.device_metrics.battery_level == 0);
    assert(desktopSimulationScenario("telemetry-missing"));
    auto missing =
        payload<meshtastic_Telemetry>(send(request(meshtastic_PortNum_TELEMETRY_APP, q, meshtastic_Telemetry_fields, peer)),
                                      meshtastic_PortNum_TELEMETRY_APP, meshtastic_Telemetry_fields);
    assert(!missing.variant.device_metrics.has_battery_level);
    // Forward and return routes use independent missing sentinels; zero SNR is real.
    meshtastic_RouteDiscovery rd{};
    auto p = request(meshtastic_PortNum_TRACEROUTE_APP, rd, meshtastic_RouteDiscovery_fields, peer);
    auto route =
        payload<meshtastic_RouteDiscovery>(send(p), meshtastic_PortNum_TRACEROUTE_APP, meshtastic_RouteDiscovery_fields, p.id);
    assert(route.route_count == 2 && route.route_back_count == 1 && route.snr_towards[1] == INT8_MIN &&
           route.snr_towards[2] == 0 && route.snr_back[0] == INT8_MIN);
    // Read-only metadata is simulated verified-decrypt state; never tested encryption.
    a = {};
    a.which_payload_variant = meshtastic_AdminMessage_get_device_metadata_request_tag;
    a.get_device_metadata_request = true;
    auto rows = admin(a, peer);
    bool correlated = false;
    for (auto &r : rows)
        if (r.which_payload_variant == meshtastic_FromRadio_packet_tag &&
            r.packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP) {
            assert(r.packet.from == peer && r.packet.pki_encrypted && r.packet.public_key.size == 32);
            correlated = true;
        }
    assert(correlated);
    // Direct sends distinguish relay confirmation, late recipient ACK and explicit NAK.
    p = {};
    p.id = ++id;
    p.to = peer;
    p.from = self;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p.decoded.payload.size = 4;
    memcpy(p.decoded.payload.bytes, "test", 4);
    p.want_ack = true;
    assert(desktopSimulationScenario("late-ack-next"));
    auto relayRows = send(p);
    confirmedFrom(relayRows, p.id, self);
    auto relay = payload<meshtastic_Routing>(relayRows, meshtastic_PortNum_ROUTING_APP, meshtastic_Routing_fields, p.id);
    assert(relay.error_reason == meshtastic_Routing_Error_NONE);
    assert(desktopSimulationScenario("deliver-late-ack"));
    auto lateRows = pump();
    confirmedFrom(lateRows, p.id, peer);
    auto late = payload<meshtastic_Routing>(lateRows, meshtastic_PortNum_ROUTING_APP, meshtastic_Routing_fields, p.id);
    assert(late.error_reason == meshtastic_Routing_Error_NONE);
    assert(desktopSimulationScenario("nak-no-channel-next"));
    p.id = ++id;
    auto nak = payload<meshtastic_Routing>(send(p), meshtastic_PortNum_ROUTING_APP, meshtastic_Routing_fields, p.id);
    assert(nak.error_reason == meshtastic_Routing_Error_NO_CHANNEL);
    // Channel send sidecars are consumed on every send, avoiding a 32-message fixture leak.
    auto active = channel(1);
    auto other = channel(0);
    meshtastic_Channel set[8]{};
    set[0] = other;
    set[1] = active;
    meshtastic_Config_LoRaConfig current{};
    current.use_preset = true;
    current.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    uint8_t identity[32];
    assert(wada::channelIdentity(set, current, 1, identity));
    // Broadcast mesh confirmations are actual correlated self ROUTING packets, never recipient receipts.
    const auto recipientBefore = state()["recipient_confirmations"].as<unsigned>();
    const auto relayBefore = state()["relay_confirmations"].as<unsigned>();
    p.id = ++id;
    p.to = UINT32_MAX;
    p.channel = 1;
    assert(wada::rememberChannelIntent(p.id, 1, identity));
    confirmedFrom(send(p), p.id, self);
    assert(desktopSimulationScenario("relay-only-next"));
    p.id = ++id;
    assert(wada::rememberChannelIntent(p.id, 1, identity));
    confirmedFrom(send(p), p.id, self);
    assert(desktopSimulationScenario("late-ack-next"));
    p.id = ++id;
    assert(wada::rememberChannelIntent(p.id, 1, identity));
    confirmedFrom(send(p), p.id, self);
    assert(desktopSimulationScenario("deliver-late-ack"));
    confirmedFrom(pump(), p.id, self);
    assert(state()["recipient_confirmations"].as<unsigned>() == recipientBefore);
    assert(state()["relay_confirmations"].as<unsigned>() == relayBefore + 4);
    for (const auto &intent : wada::channelIntents)
        assert(!intent.id);
    // An intent for a different channel identity must not yield a successful broadcast confirmation.
    uint8_t staleIdentity[32];
    memcpy(staleIdentity, identity, 32);
    staleIdentity[0] ^= 1;
    p.id = ++id;
    assert(wada::rememberChannelIntent(p.id, 1, staleIdentity));
    auto stale = payload<meshtastic_Routing>(send(p), meshtastic_PortNum_ROUTING_APP, meshtastic_Routing_fields, p.id);
    assert(stale.error_reason == meshtastic_Routing_Error_NO_CHANNEL);
    for (unsigned i = 0; i < 40; ++i) {
        p.id = ++id;
        p.to = UINT32_MAX;
        p.channel = 1;
        assert(wada::rememberChannelIntent(p.id, 1, identity));
        send(p);
    }
    for (const auto &intent : wada::channelIntents)
        assert(!intent.id);
    // Power controls record requests and leave this test process alive.
    a = {};
    a.which_payload_variant = meshtastic_AdminMessage_shutdown_seconds_tag;
    a.shutdown_seconds = 5;
    admin(a);
    assert(state()["power_state"] == "shutdown_requested");
    // A full native-size contact list must retain snapshot completion within a bounded output queue.
    for (unsigned i = 0; i < 300; ++i) {
        a = {};
        a.which_payload_variant = meshtastic_AdminMessage_add_contact_tag;
        a.add_contact.node_num = 0xc0000000 + i;
        a.add_contact.has_user = true;
        snprintf(a.add_contact.user.long_name, sizeof a.add_contact.user.long_name, "Fixture %u", i);
        admin(a);
    }
    assert(state()["node_count"] == 256);
    meshtastic_ToRadio full{};
    full.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    full.want_config_id = 9988;
    assert(client.send(std::move(full)));
    unsigned fullNodes = 0;
    bool fullComplete = false;
    for (const auto &r : pump()) {
        fullNodes += r.which_payload_variant == meshtastic_FromRadio_node_info_tag;
        if (r.which_payload_variant == meshtastic_FromRadio_config_complete_id_tag && r.config_complete_id == 9988)
            fullComplete = true;
    }
    assert(fullNodes == 256 && fullComplete && state()["output_dropped"] == 0);
    assert(!desktopSimulationScenario("not-a-scenario"));
    puts("fixture protocol tests passed");
}
