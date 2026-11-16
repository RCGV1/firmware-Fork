#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
class UITask
{
  public:
    std::string trace;
    unsigned traceCount = 0, unsynced = 0;
    struct Removal {
        uint32_t node;
        bool removed;
        std::string reason;
    };
    std::vector<Removal> removals;
    void onMeshtasticNodeRemoval(uint32_t node, bool removed, const char *reason) { removals.push_back({node, removed, reason}); }
    std::vector<std::string> telemetryStatuses;
    std::vector<std::pair<uint32_t, uint8_t>> telemetryUpdates;
    void onMeshtasticTelemetry(uint32_t node, uint8_t kind) { telemetryUpdates.emplace_back(node, kind); }
    void onMeshtasticTelemetryStatus(uint32_t, uint8_t, const char *message) { telemetryStatuses.emplace_back(message); }
    void showAlert(const char *, unsigned) {}
    void onMeshtasticTrace(const char *report)
    {
        trace = report;
        ++traceCount;
    }
    void onMeshtasticUnsyncedChannel(const char *, bool, uint32_t) { ++unsynced; }
    std::vector<std::pair<uint32_t, uint8_t>> deliveries;
    std::vector<uint8_t> errors;
    void onMeshtasticDelivery(uint32_t id, uint8_t status, uint8_t error = 0)
    {
        deliveries.emplace_back(id, status);
        errors.push_back(error);
    }
};
