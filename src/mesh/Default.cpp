#include "Default.h"
#include "meshUtils.h"

uint32_t Default::getConfiguredOrDefaultMs(uint32_t configuredInterval, uint32_t defaultInterval)
{
    // If a configured interval is provided, use it; otherwise, use the default interval.
    return (configuredInterval > 0 ? configuredInterval : defaultInterval) * 1000;
}

uint32_t Default::getConfiguredOrDefaultMs(uint32_t configuredInterval)
{
    // If a configured interval is provided, use it; otherwise, use the default broadcast interval.
    return (configuredInterval > 0 ? configuredInterval : default_broadcast_interval_secs) * 1000;
}

uint32_t Default::getConfiguredOrDefault(uint32_t configured, uint32_t defaultValue)
{
    // If a configured value is provided, use it; otherwise, use the default value.
    return (configured > 0 ? configured : defaultValue);
}

uint32_t Default::getConfiguredOrDefaultMsScaled(uint32_t configured, uint32_t defaultValue, uint32_t numOnlineNodes)
{
    // Baymesh intentionally omits congestion scaling: nodes have hops to spare and
    // scaling creates unpredictable intervals that complicate network management.
    return getConfiguredOrDefaultMs(configured, defaultValue);
}

uint32_t Default::getConfiguredOrMinimumValue(uint32_t configured, uint32_t minValue)
{
    // Preserve zero so downstream default-resolution can still treat the interval as disabled.
    if (configured == 0)
        return 0;

    return (configured < minValue ? minValue : configured);
}

uint8_t Default::getConfiguredOrDefaultHopLimit(uint8_t configured)
{
    // Use the configured hop limit if provided; otherwise use config, capped at HOP_MAX.
    uint8_t limit = (configured > 0 ? configured : (config.lora.hop_limit > 0 ? (uint8_t)config.lora.hop_limit : HOP_RELIABLE));
    return (limit > HOP_MAX ? HOP_MAX : limit);
}

uint8_t Default::getConfiguredOrDefaultBroadcastHopLimit(uint8_t configured)
{
    // Use the configured broadcast hop limit if provided; otherwise use config or HOP_BROADCAST default.
    uint8_t limit =
        (configured > 0 ? configured
                        : (config.lora.broadcast_hop_limit > 0 ? (uint8_t)config.lora.broadcast_hop_limit : HOP_BROADCAST));
    return (limit > HOP_MAX ? HOP_MAX : limit);
}
