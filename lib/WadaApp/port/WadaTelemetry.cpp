#include "WadaTelemetry.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>
#include <time.h>

namespace
{
class Text
{
  public:
    Text(char *buffer, size_t capacity) : out(buffer), cap(capacity)
    {
        if (cap)
            out[0] = 0;
    }
    void add(const char *format, ...)
    {
        if (!cap || at >= cap - 1)
            return;
        va_list args;
        va_start(args, format);
        int n = vsnprintf(out + at, cap - at, format, args);
        va_end(args);
        if (n > 0)
            at += std::min(size_t(n), cap - at - 1);
    }
    void number(const char *label, bool present, double value, unsigned digits, const char *unit)
    {
        if (present && std::isfinite(value))
            add("%s: %.*f%s\n", label, int(digits), value, unit);
    }
    size_t size() const { return at; }

  private:
    char *out;
    size_t cap, at = 0;
};
uint32_t crc(const uint8_t *bytes, size_t size)
{
    uint32_t value = ~0u;
    for (size_t i = 0; i < size; ++i) {
        value ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1)));
    }
    return ~value;
}
void put(uint8_t *out, uint32_t value, unsigned width)
{
    for (unsigned i = 0; i < width; ++i)
        out[i] = value >> (8 * i);
}
uint32_t get(const uint8_t *in, unsigned width)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < width; ++i)
        value |= uint32_t(in[i]) << (8 * i);
    return value;
}
void reading(Text &text, const meshtastic_Telemetry &t, bool imperial)
{
    const char *temperatureUnit = imperial ? " F" : " C";
    auto temperature = [&](float v) { return imperial ? v * 1.8 + 32 : double(v); };
#define METRIC(F, LABEL, DIGITS, UNIT) text.number(LABEL, m.has_##F, m.F, DIGITS, UNIT)
    switch (t.which_variant) {
    case meshtastic_Telemetry_device_metrics_tag: {
        const auto &m = t.variant.device_metrics;
        if (m.has_battery_level) {
            if (m.battery_level > 100)
                text.add("Battery: externally powered\n");
            else
                text.add("Battery: %u%%\n", unsigned(m.battery_level));
        }
        METRIC(voltage, "Voltage", 2, " V");
        METRIC(channel_utilization, "Channel utilization", 1, "%");
        METRIC(air_util_tx, "TX airtime", 1, "%");
        if (m.has_uptime_seconds)
            text.add("Uptime: %u s\n", unsigned(m.uptime_seconds));
        break;
    }
    case meshtastic_Telemetry_environment_metrics_tag: {
        const auto &m = t.variant.environment_metrics;
        text.number("Temperature", m.has_temperature, temperature(m.temperature), 1, temperatureUnit);
        METRIC(relative_humidity, "Humidity", 0, "%");
        METRIC(barometric_pressure, "Pressure", 1, " hPa");
        METRIC(gas_resistance, "Gas resistance", 2, " MOhm");
        METRIC(voltage, "Voltage", 2, " V");
        METRIC(current, "Current", 1, " mA");
        if (m.has_iaq) {
            const char *band = m.iaq <= 50    ? "Excellent"
                               : m.iaq <= 100 ? "Good"
                               : m.iaq <= 150 ? "Lightly Polluted"
                               : m.iaq <= 200 ? "Moderately Polluted"
                               : m.iaq <= 250 ? "Heavily Polluted"
                               : m.iaq <= 350 ? "Severely Polluted"
                                              : "Extremely Polluted";
            text.add("IAQ: %u (%s)\n", unsigned(m.iaq), band);
        }
        METRIC(distance, "Distance", 1, " mm");
        METRIC(lux, "Light", 0, " lux");
        METRIC(white_lux, "White light", 0, " lux");
        METRIC(ir_lux, "Infrared light", 0, " lux");
        METRIC(uv_lux, "UV light", 0, " lux");
        METRIC(wind_direction, "Wind direction", 0, " deg");
        const double speed = imperial ? 2.2369362921 : 3.6;
        const char *speedUnit = imperial ? " mph" : " km/h";
        text.number("Wind", m.has_wind_speed, m.wind_speed * speed, 1, speedUnit);
        text.number("Wind gust", m.has_wind_gust, m.wind_gust * speed, 1, speedUnit);
        text.number("Wind lull", m.has_wind_lull, m.wind_lull * speed, 1, speedUnit);
        text.number("Weight", m.has_weight, m.weight * (imperial ? 2.2046226218 : 1), 1, imperial ? " lb" : " kg");
        METRIC(radiation, "Radiation", 1, " uR/h");
        text.number("Rain (1 h)", m.has_rainfall_1h, m.rainfall_1h / (imperial ? 25.4 : 1), 1, imperial ? " in" : " mm");
        text.number("Rain (24 h)", m.has_rainfall_24h, m.rainfall_24h / (imperial ? 25.4 : 1), 1, imperial ? " in" : " mm");
        METRIC(soil_moisture, "Soil moisture", 0, "%");
        text.number("Soil temperature", m.has_soil_temperature, temperature(m.soil_temperature), 1, temperatureUnit);
#define WADA_ADC(N) text.number("ADC " #N, m.has_adc_voltage_ch##N, m.adc_voltage_ch##N, 2, " V")
        WADA_ADC(0);
        WADA_ADC(1);
        WADA_ADC(2);
        WADA_ADC(3);
        WADA_ADC(4);
        WADA_ADC(5);
        WADA_ADC(6);
        WADA_ADC(7);
#undef WADA_ADC
#define WADA_ONEWIRE(N)                                                                                                          \
    text.number("Temperature " #N, m.has_one_wire_temperature_ch##N, temperature(m.one_wire_temperature_ch##N), 1,               \
                temperatureUnit)
        WADA_ONEWIRE(0);
        WADA_ONEWIRE(1);
        WADA_ONEWIRE(2);
        WADA_ONEWIRE(3);
        WADA_ONEWIRE(4);
        WADA_ONEWIRE(5);
        WADA_ONEWIRE(6);
        WADA_ONEWIRE(7);
#undef WADA_ONEWIRE
        METRIC(lightning_strike_count_1h, "Lightning strikes (1 h)", 0, "");
        text.number("Lightning distance", m.has_lightning_distance_km, m.lightning_distance_km * (imperial ? 0.6213711922 : 1), 1,
                    imperial ? " mi" : " km");
        break;
    }
    case meshtastic_Telemetry_air_quality_metrics_tag: {
        const auto &m = t.variant.air_quality_metrics;
        METRIC(pm10_standard, "PM1.0 standard", 0, " ug/m3");
        METRIC(pm25_standard, "PM2.5 standard", 0, " ug/m3");
        METRIC(pm40_standard, "PM4.0 standard", 0, " ug/m3");
        METRIC(pm100_standard, "PM10 standard", 0, " ug/m3");
        METRIC(pm10_environmental, "PM1.0 environmental", 0, " ug/m3");
        METRIC(pm25_environmental, "PM2.5 environmental", 0, " ug/m3");
        METRIC(pm100_environmental, "PM10 environmental", 0, " ug/m3");
        if (m.has_co2)
            text.add("CO2: %u ppm (%s)\n", unsigned(m.co2),
                     m.co2 < 1000    ? "Good"
                     : m.co2 < 2000  ? "Stuffy"
                     : m.co2 < 5000  ? "Poor"
                     : m.co2 < 30000 ? "Unsafe"
                                     : "Evacuate");
        text.number("CO2 temperature", m.has_co2_temperature, temperature(m.co2_temperature), 1, temperatureUnit);
        METRIC(co2_humidity, "CO2 humidity", 0, "%");
        METRIC(form_formaldehyde, "Formaldehyde", 1, " ppb");
        text.number("Formaldehyde temperature", m.has_form_temperature, temperature(m.form_temperature), 1, temperatureUnit);
        METRIC(form_humidity, "Formaldehyde humidity", 0, "%");
        text.number("PM temperature", m.has_pm_temperature, temperature(m.pm_temperature), 1, temperatureUnit);
        METRIC(pm_humidity, "PM humidity", 0, "%");
        METRIC(pm_voc_idx, "VOC index", 0, "");
        METRIC(pm_nox_idx, "NOx index", 0, "");
        METRIC(particles_tps, "Typical particle size", 1, " um");
#define WADA_PARTICLES(N, LABEL) text.number("Particles " LABEL, m.has_particles_##N##um, m.particles_##N##um, 0, " /0.1L")
        WADA_PARTICLES(03, "0.3um");
        WADA_PARTICLES(05, "0.5um");
        WADA_PARTICLES(10, "1.0um");
        WADA_PARTICLES(25, "2.5um");
        WADA_PARTICLES(40, "4.0um");
        WADA_PARTICLES(50, "5.0um");
        WADA_PARTICLES(100, "10um");
#undef WADA_PARTICLES
        if (m.has_pm_status_flags)
            text.add("Sensor status: 0x%x\n", unsigned(m.pm_status_flags));
        break;
    }
    case meshtastic_Telemetry_power_metrics_tag: {
        const auto &m = t.variant.power_metrics;
#define WADA_POWER(N)                                                                                                            \
    text.number("Channel " #N " voltage", m.has_ch##N##_voltage, m.ch##N##_voltage, 2, " V");                                    \
    text.number("Channel " #N " current", m.has_ch##N##_current, m.ch##N##_current, 1, " mA")
        WADA_POWER(1);
        WADA_POWER(2);
        WADA_POWER(3);
        WADA_POWER(4);
        WADA_POWER(5);
        WADA_POWER(6);
        WADA_POWER(7);
        WADA_POWER(8);
#undef WADA_POWER
        break;
    }
    case meshtastic_Telemetry_local_stats_tag: {
        const auto &m = t.variant.local_stats;
        text.add("Uptime: %u s\nPackets TX: %u\nPackets RX: %u\nBad RX: %u\nNodes online: %u\nNodes total: %u\n",
                 unsigned(m.uptime_seconds), unsigned(m.num_packets_tx), unsigned(m.num_packets_rx),
                 unsigned(m.num_packets_rx_bad), unsigned(m.num_online_nodes), unsigned(m.num_total_nodes));
        text.number("Channel utilization", true, m.channel_utilization, 1, "%");
        text.number("TX airtime", true, m.air_util_tx, 1, "%");
        text.add("Duplicate RX: %u\nRelayed TX: %u\nRelay canceled: %u\nDropped TX: %u\nHeap: %u / %u bytes free\n",
                 unsigned(m.num_rx_dupe), unsigned(m.num_tx_relay), unsigned(m.num_tx_relay_canceled), unsigned(m.num_tx_dropped),
                 unsigned(m.heap_free_bytes), unsigned(m.heap_total_bytes));
        text.add("Noise floor: %d dBm\n", int(m.noise_floor));
        break;
    }
    case meshtastic_Telemetry_host_metrics_tag: {
        const auto &m = t.variant.host_metrics;
        text.add("Uptime: %u s\nMemory free: %llu bytes\nDisk free: %llu bytes\n", unsigned(m.uptime_seconds),
                 (unsigned long long)m.freemem_bytes, (unsigned long long)m.diskfree1_bytes);
        if (m.has_diskfree2_bytes)
            text.add("Disk 2 free: %llu bytes\n", (unsigned long long)m.diskfree2_bytes);
        if (m.has_diskfree3_bytes)
            text.add("Disk 3 free: %llu bytes\n", (unsigned long long)m.diskfree3_bytes);
        text.add("Load 1/5/15 min: %.2f / %.2f / %.2f\n", m.load1 / 100., m.load5 / 100., m.load15 / 100.);
        if (m.has_user_string)
            text.add("Host: %.*s\n", int(sizeof(m.user_string)), m.user_string);
        break;
    }
    }
#undef METRIC
}
} // namespace

namespace wada
{
const char *telemetryName(TelemetryKind kind)
{
    static constexpr const char *names[] = {"Device", "Environment", "Air quality", "Power", "Local stats", "Host"};
    return unsigned(kind) < 6 ? names[unsigned(kind)] : "Unknown";
}
pb_size_t telemetryTag(TelemetryKind kind)
{
    static constexpr pb_size_t tags[] = {
        meshtastic_Telemetry_device_metrics_tag,      meshtastic_Telemetry_environment_metrics_tag,
        meshtastic_Telemetry_air_quality_metrics_tag, meshtastic_Telemetry_power_metrics_tag,
        meshtastic_Telemetry_local_stats_tag,         meshtastic_Telemetry_host_metrics_tag};
    return unsigned(kind) < 6 ? tags[unsigned(kind)] : 0;
}
bool telemetryKind(pb_size_t tag, TelemetryKind &kind)
{
    for (unsigned i = 0; i < 6; ++i)
        if (telemetryTag(TelemetryKind(i)) == tag) {
            kind = TelemetryKind(i);
            return true;
        }
    return false;
}
bool TelemetryLog::append(uint32_t node, uint32_t received, const meshtastic_Telemetry &data)
{
    TelemetryKind kind;
    if (!node || node == UINT32_MAX || !telemetryKind(data.which_variant, kind))
        return false;
    if (data.time)
        for (size_t i = 0; i < count; ++i) {
            auto &r = records[i];
            if (r.node == node && r.data.which_variant == data.which_variant && r.data.time == data.time) {
                r.data = data;
                return true;
            }
        }
    if (count == Capacity) {
        std::move(records.begin() + 1, records.end(), records.begin());
        --count;
    }
    records[count++] = {node, received, data};
    return true;
}
size_t TelemetryLog::format(uint32_t node, TelemetryKind kind, char *out, size_t cap, bool imperial) const
{
    Text text(out, cap);
    bool found = false;
    for (size_t i = count; i > 0; --i) {
        const auto &r = records[i - 1];
        if (r.node != node || r.data.which_variant != telemetryTag(kind))
            continue;
        found = true;
        const uint32_t stamp = r.data.time ? r.data.time : r.received;
        const time_t when = stamp;
        tm utc{};
        char date[32] = {};
        if (stamp && gmtime_r(&when, &utc))
            strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S UTC", &utc);
        text.add("%s%s\n", date[0] ? date : "Time unknown", !r.data.time && stamp ? " (received)" : "");
        const size_t before = text.size();
        reading(text, r.data, imperial);
        if (before == text.size())
            text.add("No readings reported\n");
        text.add("\n");
    }
    if (!found)
        text.add("No %s readings stored for this node.\nRequest a reading to ask the node for its metrics.", telemetryName(kind));
    if (cap > 32 && text.size() == cap - 1) {
        constexpr char more[] = "\nOlder readings omitted.\n";
        memcpy(out + cap - sizeof(more), more, sizeof(more));
    }
    return text.size();
}
size_t TelemetryLog::encode(uint8_t *out, size_t cap, uint32_t generation) const
{
    if (cap < 16)
        return 0;
    memcpy(out, "WTL1", 4);
    put(out + 4, generation, 4);
    put(out + 8, count, 2);
    put(out + 10, 0, 2);
    size_t at = 12;
    for (size_t i = 0; i < count; ++i) {
        if (at + 14 > cap)
            return 0;
        auto stream = pb_ostream_from_buffer(out + at + 10, cap - at - 14);
        if (!pb_encode(&stream, meshtastic_Telemetry_fields, &records[i].data))
            return 0;
        put(out + at, records[i].node, 4);
        put(out + at + 4, records[i].received, 4);
        put(out + at + 8, stream.bytes_written, 2);
        at += 10 + stream.bytes_written;
    }
    put(out + at, crc(out, at), 4);
    return at + 4;
}
bool TelemetryLog::restore(const uint8_t *bytes, size_t size, uint32_t &generation)
{
    if (size < 16 || size > MaxBytes || memcmp(bytes, "WTL1", 4) || get(bytes + 10, 2) ||
        get(bytes + size - 4, 4) != crc(bytes, size - 4))
        return false;
    const size_t n = get(bytes + 8, 2);
    if (n > Capacity)
        return false;
    for (unsigned pass = 0; pass < 2; ++pass) {
        size_t at = 12;
        for (size_t i = 0; i < n; ++i) {
            if (at + 10 > size - 4)
                return false;
            const size_t len = get(bytes + at + 8, 2);
            if (len > meshtastic_Telemetry_size || len > size - 4 - at - 10)
                return false;
            TelemetryRecord r{};
            r.node = get(bytes + at, 4);
            r.received = get(bytes + at + 4, 4);
            auto stream = pb_istream_from_buffer(bytes + at + 10, len);
            TelemetryKind kind;
            if (!r.node || r.node == UINT32_MAX || !pb_decode(&stream, meshtastic_Telemetry_fields, &r.data) ||
                !telemetryKind(r.data.which_variant, kind))
                return false;
            if (pass)
                records[i] = r;
            at += 10 + len;
        }
        if (at != size - 4)
            return false;
    }
    count = n;
    generation = get(bytes + 4, 4);
    return true;
}
} // namespace wada
