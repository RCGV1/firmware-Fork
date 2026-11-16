// Exercises the actual WadaSettings.inc preset callbacks and Apply handler with
// native protobuf types and production value/CR helpers. Filtering changes menu
// positions, never modem enum values. Region edits must remain drafts, preserve
// custom settings, and delegate default selection to the native radio policy.
#include "WadaNativeConfig.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
using Region = meshtastic_Config_LoRaConfig_RegionCode;
using Preset = meshtastic_Config_LoRaConfig_ModemPreset;
using std::isfinite;
#define TR(value) (value)
constexpr unsigned LV_STATE_CHECKED = 1, LV_STATE_DISABLED = 2, LV_OBJ_FLAG_HIDDEN = 1;
constexpr int LV_EVENT_VALUE_CHANGED = 1;
struct lv_obj_t {
    unsigned selected = 0, states = 0, flags = 0;
    std::string options, text;
};
struct lv_event_t {
    int code = LV_EVENT_VALUE_CHANGED;
};
static unsigned lv_dropdown_get_selected(lv_obj_t *obj)
{
    return obj->selected;
}
static void lv_dropdown_set_selected(lv_obj_t *obj, unsigned selected)
{
    obj->selected = selected;
}
static void lv_dropdown_set_options(lv_obj_t *obj, const char *options)
{
    obj->options = options;
}
static bool lv_obj_has_state(lv_obj_t *obj, unsigned flag)
{
    return obj->states & flag;
}
static void lv_obj_add_state(lv_obj_t *obj, unsigned flag)
{
    obj->states |= flag;
}
static void lv_obj_clear_state(lv_obj_t *obj, unsigned flag)
{
    obj->states &= ~flag;
}
static void lv_obj_add_flag(lv_obj_t *obj, unsigned flag)
{
    obj->flags |= flag;
}
static void lv_obj_clear_flag(lv_obj_t *obj, unsigned flag)
{
    obj->flags &= ~flag;
}
static lv_obj_t *lv_obj_get_parent(lv_obj_t *obj)
{
    return obj;
}
static void lv_label_set_text(lv_obj_t *obj, const char *text)
{
    obj->text = text;
}
static const char *lv_textarea_get_text(lv_obj_t *obj)
{
    return obj->text.c_str();
}
static int lv_event_get_code(lv_event_t *event)
{
    return event->code;
}
struct Backend {
    meshtastic_Config_LoRaConfig lora{};
    meshtastic_User owner{};
    std::array<bool, _meshtastic_Config_LoRaConfig_RegionCode_ARRAYSIZE> unconstrained{};
    std::array<std::vector<unsigned>, _meshtastic_Config_LoRaConfig_RegionCode_ARRAYSIZE> allowed;
    Region licensed = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    unsigned repairs = 0;
    Region previous = meshtastic_Config_LoRaConfig_RegionCode_UNSET, next = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    Preset from = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    Preset repairResult = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    std::vector<meshtastic_Config> saved;
    Backend() { unconstrained.fill(true); }
    bool validPreset(Region region, Preset preset) const
    {
        return unsigned(preset) < _meshtastic_Config_LoRaConfig_ModemPreset_ARRAYSIZE &&
               (unconstrained[region] ||
                std::find(allowed[region].begin(), allowed[region].end(), unsigned(preset)) != allowed[region].end());
    }
    bool regionLicensedOnly(Region region) const { return region == licensed; }
    const meshtastic_User &ownUser() const { return owner; }
    const meshtastic_Config_LoRaConfig &loraConfig() const { return lora; }
    Preset presetForRegionChange(Region old, Region region, Preset current)
    {
        ++repairs;
        previous = old;
        next = region;
        from = current;
        return repairResult;
    }
    bool setConfig(const meshtastic_Config &config)
    {
        saved.push_back(config);
        return true;
    }
    void constrain(Region region, std::vector<unsigned> presets)
    {
        unconstrained[region] = false;
        allowed[region] = std::move(presets);
    }
};
static Backend backend;
static Backend *wadaBackend = &backend;
struct Task {
    std::vector<std::string> alerts;
    void showAlert(const char *text, unsigned) { alerts.emplace_back(text); }
};
static Task task;
static struct {
    Task *task = &::task;
} g_lv;
static unsigned flushes = 0;
static void wadaBeforeChange()
{
    ++flushes;
}
static void wadaSettingsResult(bool success)
{
    assert(success);
}
static lv_obj_t *wadaRegion, *wadaPreset, *wadaHop, *wadaPower, *wadaFrequencySlot, *wadaUsePreset, *wadaBandwidth, *wadaSf,
    *wadaCr, *wadaOverride, *wadaTx, *wadaRxBoost, *wadaIgnoreMqtt, *wadaCrHelp, *wadaRegionNotice;
static unsigned wadaCrValues[5] = {}, wadaCrCount = 0;
static unsigned wadaPresetValues[_meshtastic_Config_LoRaConfig_ModemPreset_ARRAYSIZE + 1] = {}, wadaPresetCount = 0;
static unsigned wadaDraftPreset = 0;
static unsigned wadaRegionValues[_meshtastic_Config_LoRaConfig_RegionCode_ARRAYSIZE + 1] = {}, wadaRegionCount = 0;
static Region wadaDraftRegion = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
static void wadaPresetFieldsChanged(lv_event_t *);

#include "native_preset_ui.inc"

static std::vector<std::unique_ptr<lv_obj_t>> objects;
static lv_obj_t *object(const char *text = "")
{
    objects.push_back(std::make_unique<lv_obj_t>());
    objects.back()->text = text;
    return objects.back().get();
}
static void reset()
{
    objects.clear();
    backend = Backend();
    task.alerts.clear();
    flushes = 0;
    backend.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    backend.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    backend.lora.use_preset = true;
    backend.lora.bandwidth = 250;
    backend.lora.spread_factor = 7;
    wadaRegion = object();
    wadaPreset = object();
    wadaHop = object("3");
    wadaPower = object("0");
    wadaFrequencySlot = object("0");
    wadaUsePreset = object();
    wadaBandwidth = object("250");
    wadaSf = object("7");
    wadaCr = object();
    wadaOverride = object("0");
    wadaTx = object();
    wadaRxBoost = object();
    wadaIgnoreMqtt = object();
    wadaCrHelp = object();
    wadaRegionNotice = object();
    wadaUsePreset->states = LV_STATE_CHECKED;
    wadaPopulateRegions(backend.lora.region);
    wadaDraftRegion = backend.lora.region;
    wadaDraftPreset = backend.lora.modem_preset;
    wadaPresetCount = 0;
    wadaCrCount = 1;
    wadaCrValues[0] = 0;
}
static void choose(unsigned preset)
{
    for (unsigned i = 0; i < wadaPresetCount; ++i)
        if (wadaPresetValues[i] == preset) {
            wadaPreset->selected = i;
            wadaPresetChanged(nullptr);
            return;
        }
    assert(false);
}
int main()
{
    const Region us = meshtastic_Config_LoRaConfig_RegionCode_US;
    const Region eu = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    const Preset narrow = meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST;
    reset();
    backend.constrain(us, {4, 12});
    wadaDraftPreset = narrow;
    wadaPopulatePresets();
    wadaPresetFieldsChanged(nullptr);
    assert(wadaPresetCount == 2 && wadaPreset->selected == 1);
    assert(wadaPresetValues[1] == 12);
    assert(wadaCrCount == 3 && wadaCrValues[0] == 0 && wadaCrValues[1] == 7 && wadaCrValues[2] == 8);
    assert(wadaCr->options.find("4/6") != std::string::npos);
    assert(wadaCrHelp->text.find("Peers on this preset can still receive it") != std::string::npos);
    assert(backend.saved.empty() && flushes == 0);
    wadaSaveRadio(nullptr);
    assert(backend.saved.size() == 1 && flushes == 1);
    assert(backend.saved.back().payload_variant.lora.modem_preset == narrow);

    // Rebuilding the filtered list must retain an explicit CR override by value.
    wadaCr->selected = 1;
    choose(4);
    assert(wadaDraftPreset == 4 && wadaCrValues[wadaCr->selected] == 7);
    assert(wadaCr->options.find("4/5") != std::string::npos);

    reset();
    wadaPopulatePresets();
    wadaPresetFieldsChanged(nullptr);
    backend.constrain(eu, {4, 16});
    backend.repairResult = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO;
    wadaRegion->selected = eu;
    wadaRegionChanged(nullptr);
    assert(backend.repairs == 1 && backend.previous == us && backend.next == eu && backend.from == 0);
    assert(wadaDraftPreset == 16 && wadaPresetValues[wadaPreset->selected] == 16);
    assert(backend.saved.empty() && flushes == 0);
    wadaSaveRadio(nullptr);
    assert(backend.saved.size() == 1 && backend.saved.back().payload_variant.lora.modem_preset == 16);

    reset();
    wadaUsePreset->states = 0;
    backend.lora.use_preset = false;
    wadaDraftPreset = backend.lora.modem_preset = narrow;
    wadaCrValues[0] = 7;
    wadaBandwidth->text = "62.5";
    wadaSf->text = "9";
    backend.constrain(eu, {4, 16});
    wadaRegion->selected = eu;
    wadaRegionChanged(nullptr);
    assert(backend.repairs == 0 && wadaDraftPreset == narrow);
    assert(wadaCrHelp->text.find("matching bandwidth, spreading factor and frequency") != std::string::npos);
    assert(wadaCrHelp->text.find("preset") == std::string::npos);
    assert(wadaCrValues[wadaCr->selected] == 7 && wadaBandwidth->text == "62.5" && wadaSf->text == "9");
    assert(wadaPreset->states & LV_STATE_DISABLED);
    assert(backend.saved.empty());
    wadaSaveRadio(nullptr);
    const auto &custom = backend.saved.back().payload_variant.lora;
    assert(!custom.use_preset && custom.modem_preset == narrow && custom.bandwidth == 62 && custom.spread_factor == 9 &&
           custom.coding_rate == 7);

    reset();
    wadaDraftRegion = backend.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    wadaRegion->selected = wadaDraftRegion;
    wadaPopulatePresets();
    backend.constrain(us, {0, 9});
    backend.repairResult = meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO;
    wadaRegion->selected = us;
    wadaRegionChanged(nullptr);
    assert(backend.previous == meshtastic_Config_LoRaConfig_RegionCode_UNSET && backend.from == 0);
    assert(wadaDraftPreset == 9 && wadaPresetValues[wadaPreset->selected] == 9);
    assert(backend.saved.empty());

    reset();
    wadaDraftPreset = meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW;
    wadaPopulatePresets();
    wadaPresetFieldsChanged(nullptr);
    unsigned selected;
    assert(!wadaPresetChoice(selected));
    assert(wadaPreset->options.find("VeryLong") == std::string::npos);
    wadaSaveRadio(nullptr);
    assert(backend.saved.empty() && !task.alerts.empty());
    choose(0);
    assert(wadaPresetChoice(selected) && selected == 0);
    assert(wadaRegionNotice->flags & LV_OBJ_FLAG_HIDDEN);

    reset();
    backend.licensed = eu;
    backend.repairResult = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    wadaRegion->selected = eu;
    wadaRegionChanged(nullptr);
    assert(wadaRegionNotice->text.find("requires an amateur radio license") != std::string::npos);
    wadaSaveRadio(nullptr);
    assert(backend.saved.empty() && task.alerts.back().find("licensed") != std::string::npos);
    backend.owner.is_licensed = true;
    wadaPopulatePresets();
    wadaSaveRadio(nullptr);
    assert(backend.saved.size() == 1);

    reset();
    backend.constrain(us, {});
    wadaPopulatePresets();
    assert(wadaPreset->options == "No available presets" && !wadaPresetChoice(selected));
    reset();
    wadaPopulateRegions(meshtastic_Config_LoRaConfig_RegionCode_EU_917);
    assert(wadaRegion->options.find("Unavailable: EU_917") == 0);
    assert(wadaSelectedRegion() == meshtastic_Config_LoRaConfig_RegionCode_EU_917);
    wadaPopulatePresets();
    wadaSaveRadio(nullptr);
    assert(backend.saved.empty());
    wadaPopulateRegions(meshtastic_Config_LoRaConfig_RegionCode_EU_N_868);
    assert(wadaRegionValues[wadaRegion->selected] == 32 && wadaRegion->selected == 29);
    wadaPopulatePresets();
    wadaSaveRadio(nullptr);
    assert(backend.saved.back().payload_variant.lora.region == 32);
    assert(wadaRegion->options.find("UA_868") == std::string::npos && wadaRegion->options.find("EU_874") == std::string::npos &&
           wadaRegion->options.find("EU_917") == std::string::npos);
    reset();
    wadaPopulateRegions(meshtastic_Config_LoRaConfig_RegionCode_LORA_24);
    wadaUsePreset->states = 0;
    wadaPopulatePresets();
    wadaPresetFieldsChanged(nullptr);
    wadaBandwidth->text = "1625";
    wadaSaveRadio(nullptr);
    assert(backend.saved.empty());
    wadaBandwidth->text = "812.5";
    wadaSaveRadio(nullptr);
    assert(backend.saved.back().payload_variant.lora.bandwidth == 800);
    wadaBandwidth->text = "0";
    wadaSaveRadio(nullptr);
    assert(backend.saved.back().payload_variant.lora.bandwidth == 0);
    puts("PASS: native filtered presets, enum/CR mapping, draft-only region and custom preservation");
}
