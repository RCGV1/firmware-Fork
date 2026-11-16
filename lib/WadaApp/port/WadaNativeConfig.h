#pragma once
#include "mesh/generated/meshtastic/channel.pb.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include <cstddef>
namespace wada
{
inline constexpr size_t longNameMaxBytes = 24;
inline constexpr uint32_t nodeInfoDefaultSeconds = 10800;
inline constexpr const char *regions[] = {
    "UNSET",  "US",     "EU_433",   "EU_868",  "CN",        "JP",        "ANZ",       "KR",        "TW",      "RU",
    "IN",     "NZ_865", "TH",       "LORA_24", "UA_433",    "UA_868",    "MY_433",    "MY_919",    "SG_923",  "PH_433",
    "PH_868", "PH_915", "ANZ_433",  "KZ_433",  "KZ_863",    "NP_865",    "BR_902",    "ITU1_2M",   "ITU2_2M", "EU_866",
    "EU_874", "EU_917", "EU_N_868", "ITU3_2M", "ITU1_70CM", "ITU2_70CM", "ITU3_70CM", "ITU2_125CM"};
inline constexpr const char *presets[] = {"LONG_FAST",  "LONG_SLOW",   "VERY_LONG_SLOW", "MEDIUM_SLOW", "MEDIUM_FAST",
                                          "SHORT_SLOW", "SHORT_FAST",  "LONG_MODERATE",  "SHORT_TURBO", "LONG_TURBO",
                                          "LITE_FAST",  "LITE_SLOW",   "NARROW_FAST",    "NARROW_SLOW", "TINY_FAST",
                                          "TINY_SLOW",  "MEDIUM_TURBO"};
inline constexpr const char *regionOptions =
    "UNSET\nUS\nEU_433\nEU_868\nCN\nJP\nANZ\nKR\nTW\nRU\nIN\nNZ_865\nTH\nLORA_24\nUA_433\nUA_868\nMY_433\nMY_919\nSG_923\nPH_"
    "433\nPH_868\nPH_915\nANZ_433\nKZ_433\nKZ_863\nNP_865\nBR_902\nITU1_2M\nITU2_2M\nEU_866\nEU_874\nEU_917\nEU_N_868\nITU3_"
    "2M\nITU1_70CM\nITU2_70CM\nITU3_70CM\nITU2_125CM";
inline constexpr const char *presetOptions =
    "Long Fast\nLong Slow\nVery Long Slow\nMedium Slow\nMedium Fast\nShort Slow\nShort Fast\nLong Moderate\nShort Turbo\nLong "
    "Turbo\nLite Fast\nLite Slow\nNarrow Fast\nNarrow Slow\nTiny Fast\nTiny Slow\nMedium Turbo";
const char *regionName(unsigned);
bool supportedRegion(unsigned);
const char *presetName(unsigned);
const char *effectivePresetName(unsigned, bool usePreset = true);
unsigned presetCodingRate(unsigned);
unsigned normalizedCodingRate(unsigned preset, unsigned rate);
double bandwidthKHz(unsigned code);
bool bandwidthCode(const char *text, bool wide, unsigned &out);
int namedRegion(const char *);
int namedPreset(const char *);
bool unsignedValue(const char *, unsigned max, unsigned &value);
bool channelKey(const char *, bool base64, meshtastic_ChannelSettings_psk_t &out);
} // namespace wada

static_assert(sizeof(wada::regions) / sizeof(*wada::regions) == _meshtastic_Config_LoRaConfig_RegionCode_ARRAYSIZE,
              "Update native region names with the protobufs");
static_assert(sizeof(wada::presets) / sizeof(*wada::presets) == _meshtastic_Config_LoRaConfig_ModemPreset_ARRAYSIZE,
              "Update native presets with the protobufs");
