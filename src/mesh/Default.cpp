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
    // Use the configured interval if provided; otherwise, use the default interval.
    // No scaling based on the number of online nodes.
    return (configured > 0 ? configured : defaultValue) * 1000;
}

uint32_t Default::getConfiguredOrMinimumValue(uint32_t configured, uint32_t minValue)
{
    // Use the configured value if provided; otherwise, use the minimum value.
    return (configured > 0 ? configured : minValue);
}

uint8_t Default::getConfiguredOrDefaultHopLimit(uint8_t configured)
{
    // Use the configured hop limit if provided; otherwise, use the default hop limit.
    return (configured > 0 ? configured : config.lora.hop_limit);
}
