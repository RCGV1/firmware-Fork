// Pins WadaTelemetry's native codec and the backend's per-type request lifecycle.
// Optional zeros remain readings; request echoes and ACKs cannot fabricate a reply.
// Truncated inactive snapshots must leave an older valid log recoverable.
#include "WadaMeshtasticBackend.h"
#include "WadaTelemetry.h"
#include "meshtastic/portnums.pb.h"
#include "ui-touch/UITask.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <memory>
#include <pb_decode.h>
#include <pb_encode.h>
#include <string>
#include <vector>

extern UITask ui;
extern uint32_t nowMs;
void push(meshtastic_FromRadio, WadaMeshtasticBackend &);
meshtastic_ToRadio take();
void drain();
meshtastic_FromRadio text(uint32_t, uint32_t, uint32_t, const char *);
meshtastic_FromRadio ack(uint32_t, uint32_t, meshtastic_Routing_Error);
namespace
{
std::vector<uint8_t> files[2];
bool failWrite = false;
unsigned lastSlot = 0;
meshtastic_FromRadio reply(uint32_t from, uint32_t id, const meshtastic_Telemetry &telemetry)
{
    static uint32_t serial = 0xaa000000;
    auto r = text(from, 0x11111111, ++serial, "");
    auto &d = r.packet.decoded;
    d.portnum = meshtastic_PortNum_TELEMETRY_APP;
    d.request_id = id;
    auto stream = pb_ostream_from_buffer(d.payload.bytes, sizeof(d.payload.bytes));
    assert(pb_encode(&stream, meshtastic_Telemetry_fields, &telemetry));
    d.payload.size = stream.bytes_written;
    return r;
}
} // namespace
size_t wadaTelemetryRead(unsigned slot, uint8_t *out, size_t cap)
{
    assert(slot < 2);
    if (files[slot].size() > cap)
        return 0;
    if (!files[slot].empty())
        memcpy(out, files[slot].data(), files[slot].size());
    return files[slot].size();
}
bool wadaTelemetryWrite(unsigned slot, const uint8_t *bytes, size_t size)
{
    assert(slot < 2);
    lastSlot = slot;
    files[slot].assign(bytes, bytes + (failWrite ? size / 2 : size));
    return !failWrite;
}
void test_native_telemetry(WadaMeshtasticBackend &backend, MeshBackendEvents &sink)
{
    using Kind = wada::TelemetryKind;
    constexpr uint32_t peer = 0x22222222;
    drain();
    assert(!backend.telemetryAvailable(peer, Kind::Host));
    assert(!backend.telemetryAvailable(0, Kind::Device));
    assert(!backend.telemetryAvailable(peer, Kind(255)));
    auto node = meshtastic_FromRadio{};
    node.which_payload_variant = meshtastic_FromRadio_node_info_tag;
    node.node_info = *backend.nodeInfo(peer);
    node.node_info.user.hw_model = meshtastic_HardwareModel_PORTDUINO;
    push(node, backend);
    assert(backend.telemetryAvailable(peer, Kind::Host));
    uint32_t requestIds[6]{};
    for (unsigned i = 0; i < 6; ++i) {
        const auto kind = Kind(i);
        assert(backend.requestTelemetry(peer, kind));
        auto request = take();
        assert(request.packet.to == peer && request.packet.want_ack && request.packet.decoded.want_response &&
               request.packet.decoded.portnum == meshtastic_PortNum_TELEMETRY_APP);
        meshtastic_Telemetry decoded{};
        auto stream = pb_istream_from_buffer(request.packet.decoded.payload.bytes, request.packet.decoded.payload.size);
        assert(pb_decode(&stream, meshtastic_Telemetry_fields, &decoded));
        assert(decoded.which_variant == wada::telemetryTag(kind));
        requestIds[i] = request.packet.id;
        assert(ui.telemetryStatuses.back().find(wada::telemetryName(kind)) != std::string::npos);
        assert(ui.telemetryStatuses.back().find("Peer") != std::string::npos);
        const auto count = ui.telemetryUpdates.size();
        auto echo = text(0x11111111, peer, request.packet.id, "");
        echo.packet.decoded = request.packet.decoded;
        push(echo, backend);
        assert(ui.telemetryUpdates.size() == count);
    }
    char log[4096];
    meshtastic_Telemetry telemetry{};
    telemetry.which_variant = meshtastic_Telemetry_device_metrics_tag;
    telemetry.time = 1794700000;
    telemetry.variant.device_metrics.has_battery_level = true;
    telemetry.variant.device_metrics.battery_level = 0;
    telemetry.variant.device_metrics.has_voltage = true;
    push(reply(peer, requestIds[0], telemetry), backend);
    backend.telemetryLog(peer, Kind::Device, log, sizeof log);
    assert(strstr(log, "Battery: 0%") && strstr(log, "Voltage: 0.00 V") && !strstr(log, "TX airtime"));
    assert(backend.nodeInfo(peer)->device_metrics.has_battery_level);
    const auto statusAfterReply = ui.telemetryStatuses.size();
    push(ack(peer, requestIds[0], meshtastic_Routing_Error_NO_RESPONSE), backend);
    assert(ui.telemetryStatuses.size() == statusAfterReply);
    push(ack(peer, requestIds[1], meshtastic_Routing_Error_NO_CHANNEL), backend);
    assert(ui.telemetryStatuses.back().find("Environment request to Peer") != std::string::npos);
    assert(ui.telemetryStatuses.back().find("Channel/key mismatch") != std::string::npos);
    const size_t statuses = ui.telemetryStatuses.size();
    auto wrong = ack(peer, requestIds[2], meshtastic_Routing_Error_NO_RESPONSE);
    wrong.packet.to = 0x55555555;
    push(wrong, backend);
    assert(ui.telemetryStatuses.size() == statuses);
    push(ack(peer, requestIds[2], meshtastic_Routing_Error_NONE), backend);
    assert(ui.telemetryStatuses.size() == statuses);
    auto queue = meshtastic_FromRadio{};
    queue.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
    queue.queueStatus.mesh_packet_id = requestIds[3];
    queue.queueStatus.res = 32;
    push(queue, backend);
    assert(ui.telemetryStatuses.back().find("Power request to Peer") != std::string::npos);
    assert(ui.telemetryStatuses.back().find("Radio queue rejected") != std::string::npos);
    const auto beforeQuiet = ui.telemetryStatuses.size();
    nowMs += 300001;
    backend.loop(nowMs);
    drain();
    assert(ui.telemetryStatuses.size() == beforeQuiet);
    push(ack(peer, requestIds[4], meshtastic_Routing_Error_NO_RESPONSE), backend);
    assert(ui.telemetryStatuses.size() == beforeQuiet);
    auto records = std::make_unique<wada::TelemetryLog>();
    telemetry = {};
    telemetry.which_variant = meshtastic_Telemetry_environment_metrics_tag;
    auto &environment = telemetry.variant.environment_metrics;
    environment.has_temperature = true;
    environment.temperature = 0;
    environment.has_relative_humidity = true;
    environment.relative_humidity = 0;
    environment.has_radiation = true;
    environment.radiation = 0;
    environment.has_barometric_pressure = true;
    environment.barometric_pressure = 980.5;
    environment.has_wind_speed = true;
    environment.wind_speed = 1;
    environment.has_lux = true;
    environment.lux = std::numeric_limits<float>::quiet_NaN();
    assert(records->append(peer, 1794700000, telemetry));
    records->format(peer, Kind::Environment, log, sizeof log);
    assert(strstr(log, "Temperature: 0.0 C") && strstr(log, "Humidity: 0%") && strstr(log, "Radiation: 0.0 uR/h"));
    assert(strstr(log, "Pressure: 980.5 hPa") && strstr(log, "Wind: 3.6 km/h") && !strstr(log, "Light:"));
    records->format(peer, Kind::Environment, log, sizeof log, true);
    assert(strstr(log, "Temperature: 32.0 F") && strstr(log, "Wind: 2.2 mph") && strstr(log, "980.5 hPa"));
    telemetry = {};
    telemetry.which_variant = meshtastic_Telemetry_power_metrics_tag;
    telemetry.variant.power_metrics.has_ch1_current = true;
    records->append(peer, 0, telemetry);
    records->format(peer, Kind::Power, log, sizeof log);
    assert(strstr(log, "Channel 1 current: 0.0 mA") && !strstr(log, "voltage"));
    records->format(0x77777777, Kind::Power, log, sizeof log);
    assert(strstr(log, "No Power readings stored"));
    for (unsigned i = 0; i < 60; ++i) {
        telemetry.time = 1794700000 + i;
        assert(records->append(peer, 0, telemetry));
    }
    assert(records->size() == wada::TelemetryLog::Capacity);
    const auto count = records->size();
    records->append(peer, 0, telemetry);
    assert(records->size() == count);
    uint8_t bytes[wada::TelemetryLog::MaxBytes];
    const size_t size = records->encode(bytes, sizeof bytes, UINT32_MAX);
    assert(size && size <= sizeof bytes);
    auto restored = std::make_unique<wada::TelemetryLog>();
    uint32_t generation = 0;
    assert(restored->restore(bytes, size, generation) && generation == UINT32_MAX && restored->size() == count);
    for (size_t i = 0; i < size; ++i) {
        bytes[i] ^= 1;
        assert(!restored->restore(bytes, size, generation));
        bytes[i] ^= 1;
        assert(restored->size() == count);
    }
    assert(!restored->restore(bytes, size - 1, generation));
    char tiny[48];
    restored->format(peer, Kind::Power, tiny, sizeof tiny);
    assert(tiny[sizeof tiny - 1] == 0 && strstr(tiny, "Older readings omitted"));

    // A failed inactive-file write cannot destroy the previous saved reading.
    nowMs += 10001;
    backend.loop(nowMs);
    drain();
    assert(!files[0].empty() || !files[1].empty());
    const auto oldSlot = lastSlot;
    const auto good = files[oldSlot];
    telemetry = {};
    telemetry.time = 1794800000;
    telemetry.which_variant = meshtastic_Telemetry_device_metrics_tag;
    telemetry.variant.device_metrics.has_voltage = true;
    telemetry.variant.device_metrics.voltage = 4.2f;
    push(reply(peer, 0, telemetry), backend);
    failWrite = true;
    nowMs += 10001;
    backend.loop(nowMs);
    drain();
    assert(files[oldSlot] == good && files[1 - oldSlot].size() > 0);
    uint32_t gen = 0;
    assert(restored->restore(files[oldSlot].data(), files[oldSlot].size(), gen));
    assert(!restored->restore(files[1 - oldSlot].data(), files[1 - oldSlot].size(), gen));
    restored->format(peer, Kind::Device, log, sizeof log);
    assert(strstr(log, "0.00 V") && !strstr(log, "4.20 V"));
    failWrite = false;
    nowMs += 10001;
    backend.loop(nowMs);
    drain();
    assert(restored->restore(files[lastSlot].data(), files[lastSlot].size(), gen));
    restored->format(peer, Kind::Device, log, sizeof log);
    assert(strstr(log, "4.20 V"));
    {
        auto restart = std::make_unique<WadaMeshtasticBackend>();
        NodePrefs prefs{};
        uint8_t key[32];
        restart->begin(sink, prefs, key);
        take();
        restart->telemetryLog(peer, Kind::Device, log, sizeof log);
        assert(strstr(log, "4.20 V"));
    }
    files[lastSlot][0] ^= 1;
    {
        auto restart = std::make_unique<WadaMeshtasticBackend>();
        NodePrefs prefs{};
        uint8_t key[32];
        restart->begin(sink, prefs, key);
        take();
        restart->telemetryLog(peer, Kind::Device, log, sizeof log);
        assert(strstr(log, "0.00 V") && !strstr(log, "4.20 V"));
    }
    files[lastSlot][0] ^= 1;
}
