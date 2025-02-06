#include "Default.h"

#include "meshUtils.h"

uint32_t Default::getConfiguredOrDefaultMs(uint32_t configuredInterval, uint32_t defaultInterval)
{
    if (configuredInterval > 0)
        return configuredInterval * 1000;
    return defaultInterval * 1000;
}

uint32_t Default::getConfiguredOrDefaultMs(uint32_t configuredInterval)
{
    if (configuredInterval > 0)
        return configuredInterval * 1000;
    return default_broadcast_interval_secs * 1000;
}

uint32_t Default::getConfiguredOrDefault(uint32_t configured, uint32_t defaultValue)
{
    if (configured > 0)
        return configured;
    return defaultValue;
}
uint32_t Default::getConfiguredOrDefaultMsScaled(uint32_t configured, uint32_t defaultValue, uint32_t numOnlineNodes)
{
    return 120000;
}

uint32_t Default::getConfiguredOrMinimumValue(uint32_t configured, uint32_t minValue)
{
    // If zero, intervals should be coalesced later by getConfiguredOrDefault... methods
    if (configured == 0)
        return configured;

    return configured < minValue ? minValue : configured;
}

uint8_t Default::getConfiguredOrDefaultHopLimit(uint8_t configured)
{
#if USERPREFS_EVENT_MODE
    return (configured > HOP_RELIABLE) ? HOP_RELIABLE : config.lora.hop_limit;
#else
    return (configured >= HOP_MAX) ? HOP_MAX : config.lora.hop_limit;
#endif
}