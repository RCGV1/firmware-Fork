#pragma once
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace wada
{
enum class TelemetryKind : uint8_t { Device, Environment, AirQuality, Power, LocalStats, Host };
const char *telemetryName(TelemetryKind);
pb_size_t telemetryTag(TelemetryKind);
bool telemetryKind(pb_size_t tag, TelemetryKind &);
struct TelemetryRecord {
    uint32_t node = 0, received = 0;
    meshtastic_Telemetry data{};
};
class TelemetryLog
{
  public:
    static constexpr size_t Capacity = 48;
    static constexpr size_t MaxBytes = 16 + Capacity * (10 + meshtastic_Telemetry_size);
    bool append(uint32_t node, uint32_t received, const meshtastic_Telemetry &);
    size_t format(uint32_t node, TelemetryKind, char *out, size_t cap, bool imperial = false) const;
    size_t encode(uint8_t *out, size_t cap, uint32_t generation) const;
    bool restore(const uint8_t *bytes, size_t size, uint32_t &generation);
    size_t size() const { return count; }

  private:
    std::array<TelemetryRecord, Capacity> records{};
    size_t count = 0;
};
} // namespace wada

size_t wadaTelemetryRead(unsigned slot, uint8_t *out, size_t cap);
bool wadaTelemetryWrite(unsigned slot, const uint8_t *bytes, size_t size);
