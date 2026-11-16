// The production quick toggles must use native readback, refuse managed/stale
// writes, and bound queue-full retries. Pending intent must not survive a lockout.
#include "WadaMeshtasticBackend.h"
#include "wadacomms/PacketServer.h"
#include <cassert>
#include <cstring>
#include <pb_decode.h>
extern uint32_t nowMs;
extern PacketServer *server;
void push(meshtastic_FromRadio, WadaMeshtasticBackend &);
meshtastic_ToRadio take();
void drain();
void wadaWifiPoll();
bool wifiConfigGetRadioEnabled();
bool wifiConfigGetBleEnabled();
void wifiConfigSetRadioEnabled(bool);
void wifiConfigSetBleEnabled(bool);
bool wifiConfigSetSsid(const char *);
bool wifiConfigSetPwd(const char *);
void test_native_wifi_bridge(WadaMeshtasticBackend &backend)
{
    drain();
    auto *previous = wadaBackend;
    wadaBackend = &backend;
    auto r = meshtastic_FromRadio{};
    r.which_payload_variant = meshtastic_FromRadio_config_tag;
    r.config.which_payload_variant = meshtastic_Config_network_tag;
    strcpy(r.config.payload_variant.network.wifi_ssid, "Known network");
    push(r, backend);
    wadaWifiPoll();
    assert(!wifiConfigGetRadioEnabled());
    wifiConfigSetRadioEnabled(true);
    wadaWifiPoll();
    auto outgoing = take();
    meshtastic_AdminMessage admin{};
    auto s = pb_istream_from_buffer(outgoing.packet.decoded.payload.bytes, outgoing.packet.decoded.payload.size);
    assert(pb_decode(&s, meshtastic_AdminMessage_fields, &admin));
    assert(admin.set_config.which_payload_variant == meshtastic_Config_network_tag &&
           admin.set_config.payload_variant.network.wifi_enabled);
    assert(!strcmp(admin.set_config.payload_variant.network.wifi_ssid, "Known network"));
    assert(!wifiConfigGetRadioEnabled());
    r.config.payload_variant.network.wifi_enabled = true;
    push(r, backend);
    wadaWifiPoll();
    assert(wifiConfigGetRadioEnabled());
    wifiConfigSetBleEnabled(true);
    nowMs += 250;
    wadaWifiPoll();
    outgoing = take();
    s = pb_istream_from_buffer(outgoing.packet.decoded.payload.bytes, outgoing.packet.decoded.payload.size);
    assert(pb_decode(&s, meshtastic_AdminMessage_fields, &admin));
    assert(admin.set_config.which_payload_variant == meshtastic_Config_bluetooth_tag &&
           admin.set_config.payload_variant.bluetooth.enabled);
    drain();
    wifiConfigSetRadioEnabled(false);
    r = {};
    r.which_payload_variant = meshtastic_FromRadio_config_tag;
    r.config.which_payload_variant = meshtastic_Config_security_tag;
    r.config.payload_variant.security.is_managed = true;
    push(r, backend);
    for (unsigned i = 0; i < 100; ++i) {
        wifiConfigSetBleEnabled(true);
        wadaWifiPoll();
    }
    assert(!wifiConfigSetSsid("Rejected") && !wifiConfigSetPwd("Rejected"));
    assert(!server->hasData());
    r.config.payload_variant.security.is_managed = false;
    push(r, backend);
    nowMs += 300;
    wadaWifiPoll(); // Discarded pending Wi-Fi intent does not resume after managed mode.
    assert(!server->hasData());
    drain();
    for (unsigned i = 0; i < 25; ++i)
        assert(backend.sendAdvert(false));
    wifiConfigSetRadioEnabled(false);
    nowMs += 300;
    wadaWifiPoll(); // Full queue retains a bounded retry.
    drain();
    wadaWifiPoll(); // Same tick cannot retry.
    assert(!server->hasData());
    nowMs += 250;
    wadaWifiPoll();
    outgoing = take();
    s = pb_istream_from_buffer(outgoing.packet.decoded.payload.bytes, outgoing.packet.decoded.payload.size);
    assert(pb_decode(&s, meshtastic_AdminMessage_fields, &admin));
    assert(admin.set_config.which_payload_variant == meshtastic_Config_network_tag &&
           !admin.set_config.payload_variant.network.wifi_enabled);
    drain();
    wadaBackend = previous;
}
