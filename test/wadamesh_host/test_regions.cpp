// Pins firmware-advertised LoRa constraints and Android's first-setup selection policy.
// A region change must not queue an illegal preset; missing/empty maps stay unconstrained
// as required by LoRaRegionPresetMap, and a vendor-pinned UNSET preset must survive setup.
#include "WadaMeshtasticBackend.h"
#include <cassert>
#include <pb_decode.h>
void push(meshtastic_FromRadio, WadaMeshtasticBackend &);
meshtastic_ToRadio take();
void drain();
void test_native_regions(WadaMeshtasticBackend &backend)
{
    constexpr auto unset = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    constexpr auto us = meshtastic_Config_LoRaConfig_RegionCode_US;
    constexpr auto eu = meshtastic_Config_LoRaConfig_RegionCode_EU_866;
    constexpr auto ham = meshtastic_Config_LoRaConfig_RegionCode_ITU1_2M;
    constexpr auto fast = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    constexpr auto turbo = meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO;
    constexpr auto lite = meshtastic_Config_LoRaConfig_ModemPreset_LITE_FAST;
    drain();
    meshtastic_Config_LoRaConfig_ModemPreset value = fast;
    assert(!backend.defaultPreset(us, value));
    assert(backend.validPreset(us, turbo));
    assert(!backend.validPreset(us, meshtastic_Config_LoRaConfig_ModemPreset(31)));
    auto r = meshtastic_FromRadio{};
    r.which_payload_variant = meshtastic_FromRadio_region_presets_tag;
    auto &map = r.region_presets;
    map.groups_count = 3;
    map.groups[0].presets_count = 2;
    map.groups[0].presets[0] = fast;
    map.groups[0].presets[1] = turbo;
    map.groups[0].default_preset = fast;
    map.groups[1].presets_count = 1;
    map.groups[1].presets[0] = lite;
    map.groups[1].default_preset = lite;
    map.groups[2] = map.groups[1];
    map.groups[2].licensed_only = true;
    map.region_groups_count = 3;
    map.region_groups[0] = {us, 0};
    map.region_groups[1] = {eu, 1};
    map.region_groups[2] = {ham, 2};
    push(r, backend);
    assert(backend.validPreset(us, fast) && !backend.validPreset(eu, fast));
    assert(backend.defaultPreset(eu, value) && value == lite);
    assert(backend.presetForRegionChange(unset, us, fast) == turbo);
    assert(backend.presetForRegionChange(us, us, fast) == fast);
    assert(backend.presetForRegionChange(us, eu, turbo) == lite);
    assert(backend.presetForRegionChange(unset, us, turbo) == turbo);
    assert(backend.regionLicensedOnly(ham) && !backend.regionLicensedOnly(us));
    assert(backend.validPreset(meshtastic_Config_LoRaConfig_RegionCode_JP, turbo));
    map.region_groups[3] = {unset, 0};
    map.region_groups_count = 4;
    push(r, backend);
    assert(backend.presetForRegionChange(unset, us, fast) == fast);
    assert(backend.validPreset(unset, lite));
    assert(!backend.regionLicensedOnly(unset));
    map.groups[1].default_preset = fast;
    push(r, backend);
    assert(backend.defaultPreset(eu, value) && value == lite);

    meshtastic_Config config{};
    config.which_payload_variant = meshtastic_Config_lora_tag;
    config.payload_variant.lora = backend.loraConfig();
    auto &lora = config.payload_variant.lora;
    lora.region = eu;
    lora.use_preset = true;
    lora.modem_preset = fast;
    assert(!backend.setConfig(config));
    lora.modem_preset = lite;
    assert(backend.setConfig(config));
    auto queued = take();
    meshtastic_AdminMessage admin{};
    auto input = pb_istream_from_buffer(queued.packet.decoded.payload.bytes, queued.packet.decoded.payload.size);
    assert(pb_decode(&input, meshtastic_AdminMessage_fields, &admin));
    assert(admin.which_payload_variant == meshtastic_AdminMessage_set_config_tag &&
           admin.set_config.payload_variant.lora.modem_preset == lite);
    lora.use_preset = false;
    lora.modem_preset = fast;
    lora.bandwidth = 125;
    lora.spread_factor = 9;
    lora.coding_rate = 7;
    assert(backend.setConfig(config));
    queued = take();
    input = pb_istream_from_buffer(queued.packet.decoded.payload.bytes, queued.packet.decoded.payload.size);
    assert(pb_decode(&input, meshtastic_AdminMessage_fields, &admin));
    assert(!admin.set_config.payload_variant.lora.use_preset && admin.set_config.payload_variant.lora.bandwidth == 125 &&
           admin.set_config.payload_variant.lora.spread_factor == 9 && admin.set_config.payload_variant.lora.coding_rate == 7);
    lora.region = meshtastic_Config_LoRaConfig_RegionCode_LORA_24;
    lora.bandwidth = 1600;
    assert(!backend.setConfig(config));
    for (unsigned supported : {0u, 200u, 400u, 800u}) {
        lora.bandwidth = supported;
        assert(backend.setConfig(config));
        queued = take();
        input = pb_istream_from_buffer(queued.packet.decoded.payload.bytes, queued.packet.decoded.payload.size);
        assert(pb_decode(&input, meshtastic_AdminMessage_fields, &admin));
        assert(admin.set_config.payload_variant.lora.bandwidth == supported);
    }
    for (auto unsupported : {meshtastic_Config_LoRaConfig_RegionCode_UA_868, meshtastic_Config_LoRaConfig_RegionCode_EU_874,
                             meshtastic_Config_LoRaConfig_RegionCode_EU_917}) {
        lora.region = unsupported;
        assert(!wada::supportedRegion(unsupported) && !backend.setConfig(config));
    }
    lora.region = ham;
    assert(!backend.ownUser().is_licensed && !backend.setConfig(config));

    map.groups[1].presets_count = 0;
    push(r, backend);
    assert(backend.validPreset(eu, turbo) && !backend.defaultPreset(eu, value));
    map.region_groups[1].group_index = 255;
    push(r, backend);
    assert(backend.validPreset(eu, turbo));
    r.region_presets = {};
    push(r, backend);
    drain();
}
