#pragma once
#include "WadaNativeConfig.h"
#include <SHA256.h>
#include <cstring>

namespace wada
{
// Mirrors Channels::getName/getKey: channel identity excludes region and slot.
inline bool channelIdentity(const meshtastic_Channel *channels, const meshtastic_Config_LoRaConfig &lora, unsigned index,
                            uint8_t out[32])
{
    if (index >= 8 || channels[index].role == meshtastic_Channel_Role_DISABLED || !channels[index].has_settings)
        return false;
    const auto &ch = channels[index];
    const auto *psk = &ch.settings.psk;
    if (!psk->size && ch.role == meshtastic_Channel_Role_SECONDARY) {
        for (unsigned i = 0; i < 8; ++i)
            if (i != index && channels[i].role == meshtastic_Channel_Role_PRIMARY && channels[i].has_settings) {
                psk = &channels[i].settings.psk;
                break;
            }
    }
    uint8_t key[32] = {};
    size_t size = psk->size;
    if (size > sizeof key)
        return false;
    memcpy(key, psk->bytes, size);
    if (size == 1) {
        if (!key[0])
            size = 0;
        else {
            const uint8_t marker = key[0];
            // Official public defaultpsk from src/mesh/Channels.h.
            static constexpr uint8_t defaultKey[] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                                     0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};
            size = sizeof defaultKey;
            memcpy(key, defaultKey, size);
            key[size - 1] += marker - 1;
        }
    } else if (size && size < 16)
        size = 16;
    else if (size > 16 && size < 32)
        size = 32;
    const char *name = ch.settings.name[0] ? ch.settings.name : effectivePresetName(lora.modem_preset, lora.use_preset);
    SHA256 sha;
    const uint8_t nameLength = strlen(name), keyLength = size;
    sha.update(&nameLength, 1);
    sha.update(name, nameLength);
    sha.update(&keyLength, 1);
    sha.update(key, size);
    const uint8_t aead = ch.settings.use_aead ? 1 : 0;
    sha.update(&aead, 1); // Channels::hashFor separates AEAD and legacy channel traffic.
    sha.finalize(out, 32);
    return true;
}
inline void channelToken(const uint8_t identity[32], unsigned slot, uint8_t out[16])
{
    SHA256 sha;
    const uint8_t index = slot;
    sha.update(identity, 32);
    sha.update(&index, 1);
    sha.finalize(out, 16);
}
} // namespace wada
