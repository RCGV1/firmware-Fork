#include "WadaNativeConfig.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <strings.h>
namespace wada
{
bool supportedRegion(unsigned region)
{
    return region < _meshtastic_Config_LoRaConfig_RegionCode_ARRAYSIZE &&
           region != meshtastic_Config_LoRaConfig_RegionCode_UA_868 && region != meshtastic_Config_LoRaConfig_RegionCode_EU_874 &&
           region != meshtastic_Config_LoRaConfig_RegionCode_EU_917;
}
const char *regionName(unsigned i)
{
    return i < sizeof(regions) / sizeof(*regions) ? regions[i] : "UNKNOWN";
}
const char *presetName(unsigned i)
{
    return i < sizeof(presets) / sizeof(*presets) ? presets[i] : "UNKNOWN";
}
// Display names and coding rates mirror DisplayFormatters.cpp and MeshRadio.h.
const char *effectivePresetName(unsigned preset, bool usePreset)
{
    static constexpr const char *names[] = {"LongFast",   "LongSlow",   "Invalid",    "MediumSlow", "MediumFast", "ShortSlow",
                                            "ShortFast",  "LongMod",    "ShortTurbo", "LongTurbo",  "LiteFast",   "LiteSlow",
                                            "NarrowFast", "NarrowSlow", "TinyFast",   "TinySlow",   "MedTurbo"};
    return !usePreset ? "Custom" : preset < sizeof(names) / sizeof(*names) ? names[preset] : "Invalid";
}
unsigned presetCodingRate(unsigned preset)
{
    static constexpr uint8_t rates[] = {5, 8, 5, 5, 5, 5, 5, 8, 5, 8, 5, 5, 6, 6, 5, 6, 5};
    return preset < sizeof(rates) ? rates[preset] : 5;
}
unsigned normalizedCodingRate(unsigned preset, unsigned rate)
{
    return rate > presetCodingRate(preset) && rate <= 8 ? rate : 0;
}
double bandwidthKHz(unsigned code)
{
    // src/mesh/MeshRadio.h bwCodeToKHz; zero uses LONG_FAST's default.
    switch (code) {
    case 0:
        return 250;
    case 8:
        return 7.8;
    case 10:
        return 10.4;
    case 16:
        return 15.6;
    case 21:
        return 20.8;
    case 31:
        return 31.25;
    case 42:
        return 41.7;
    case 62:
        return 62.5;
    case 200:
        return 203.125;
    case 400:
        return 406.25;
    case 800:
        return 812.5;
    case 1600:
        return 1625;
    default:
        return code;
    }
}
bool bandwidthCode(const char *text, bool wide, unsigned &out)
{
    if (!text || !*text)
        return false;
    char *end = nullptr;
    const double value = strtod(text, &end);
    if (*end || !std::isfinite(value))
        return false;
    static constexpr unsigned subGhz[] = {8, 10, 16, 21, 31, 42, 62, 125, 250, 500};
    if (value == 0) {
        out = 0;
        return true;
    }
    static constexpr unsigned wideGhz[] = {200, 400, 800};
    const auto *codes = wide ? wideGhz : subGhz;
    const unsigned count = wide ? sizeof(wideGhz) / sizeof(*wideGhz) : sizeof(subGhz) / sizeof(*subGhz);
    for (unsigned i = 0; i < count; ++i)
        if (std::fabs(value - bandwidthKHz(codes[i])) < 0.0001) {
            out = codes[i];
            return true;
        }
    return false;
}
static bool same(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b) {
        char ca = *a == '-' ? '_' : *a;
        char cb = *b == '-' ? '_' : *b;
        if (toupper((unsigned char)ca) != toupper((unsigned char)cb))
            return false;
    }
    return !*a && !*b;
}
bool unsignedValue(const char *s, unsigned max, unsigned &out)
{
    if (!s || !*s)
        return false;
    unsigned n = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9')
            return false;
        unsigned d = *s - '0';
        if (d > max || n > (max - d) / 10)
            return false;
        n = n * 10 + d;
    }
    out = n;
    return true;
}
int namedRegion(const char *s)
{
    if (!s)
        return -1;
    for (unsigned i = 0; i < sizeof(regions) / sizeof(*regions); ++i)
        if (supportedRegion(i) && same(s, regions[i]))
            return i;
    unsigned i;
    return unsignedValue(s, sizeof(regions) / sizeof(*regions) - 1, i) && supportedRegion(i) ? int(i) : -1;
}
int namedPreset(const char *s)
{
    if (!s)
        return -1;
    for (unsigned i = 0; i < sizeof(presets) / sizeof(*presets); ++i)
        if (same(s, presets[i]))
            return i;
    unsigned i;
    return unsignedValue(s, sizeof(presets) / sizeof(*presets) - 1, i) ? int(i) : -1;
}
bool channelKey(const char *s, bool base64, meshtastic_ChannelSettings_psk_t &out)
{
    if (!s || !*s)
        return false;
    meshtastic_ChannelSettings_psk_t key = {};
    size_t n = strlen(s);
    if (!base64) {
        if (n % 2 || n > 64)
            return false;
        for (size_t i = 0; i < n; i += 2) {
            unsigned v = 0;
            for (unsigned j = 0; j < 2; ++j) {
                const unsigned char c = s[i + j];
                if (!isxdigit(c))
                    return false;
                v = v * 16 + (c <= '9' ? c - '0' : tolower(c) - 'a' + 10);
            }
            key.bytes[key.size++] = v;
        }
    } else {
        if (n % 4 || n > 44)
            return false;
        const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        unsigned bits = 0, used = 0, padding = 0;
        for (size_t i = 0; i < n; ++i) {
            char c = s[i];
            if (c == '=') {
                ++padding;
                if (padding > 2 || i < n - 2)
                    return false;
                continue;
            }
            if (padding)
                return false;
            const char *at = strchr(alphabet, c);
            if (!at)
                return false;
            bits = (bits << 6) | unsigned(at - alphabet);
            used += 6;
            if (used >= 8) {
                used -= 8;
                if (key.size == 32)
                    return false;
                key.bytes[key.size++] = bits >> used;
            }
        }
        if (used && ((bits & ((1u << used) - 1)) != 0))
            return false;
        if ((key.size % 3 == 1 && padding != 2) || (key.size % 3 == 2 && padding != 1) || (key.size % 3 == 0 && padding))
            return false;
    }
    if (key.size != 1 && key.size != 16 && key.size != 32)
        return false;
    out = key;
    return true;
}
} // namespace wada
