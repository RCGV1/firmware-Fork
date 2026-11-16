static lv_obj_t *wadaRegion = nullptr, *wadaPreset = nullptr, *wadaHop = nullptr, *wadaPower = nullptr,
                *wadaFrequencySlot = nullptr;
static lv_obj_t *wadaUsePreset = nullptr, *wadaBandwidth = nullptr, *wadaSf = nullptr, *wadaCr = nullptr, *wadaOverride = nullptr;
static lv_obj_t *wadaTx = nullptr, *wadaRxBoost = nullptr, *wadaIgnoreMqtt = nullptr;
static lv_obj_t *wadaWifiSsid = nullptr, *wadaWifiPsk = nullptr, *wadaBtPin = nullptr, *wadaWifiEnabled = nullptr,
                *wadaBtEnabled = nullptr;
static lv_obj_t *wadaChannelSlot = nullptr, *wadaChannelName = nullptr, *wadaChannelPsk = nullptr, *wadaChannelRole = nullptr,
                *wadaKeyMode = nullptr;
static lv_obj_t *wadaChannelUplink = nullptr, *wadaChannelDownlink = nullptr, *wadaChannelPrecision = nullptr,
                *wadaChannelMuted = nullptr, *wadaKeyStatus = nullptr;
static lv_obj_t *wadaLongName = nullptr, *wadaShortName = nullptr, *wadaNodeInterval = nullptr, *wadaPositionInterval = nullptr;
static unsigned wadaSelectedChannel = 0;
static unsigned wadaCrValues[5] = {0}, wadaCrCount = 0;
static lv_obj_t *wadaCrHelp = nullptr;
static unsigned wadaPresetValues[_meshtastic_Config_LoRaConfig_ModemPreset_ARRAYSIZE + 1] = {}, wadaPresetCount = 0;
static unsigned wadaDraftPreset = 0;
static meshtastic_Config_LoRaConfig_RegionCode wadaDraftRegion = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
static lv_obj_t *wadaRegionNotice = nullptr;
static void wadaSettingsResult(bool queued)
{
    if (g_lv.task)
        g_lv.task->showAlert(queued ? "Request queued in Meshtastic" : "Could not queue request; wait for synchronization", 2400);
}
static void wadaBeforeChange()
{
    if (g_lv.task)
        g_lv.task->persistHistoryNow();
}
static bool wadaUnsignedField(lv_obj_t *field, unsigned max, unsigned &value)
{
    return field && wada::unsignedValue(lv_textarea_get_text(field), max, value);
}
static void wadaFieldNumber(lv_obj_t *field, unsigned number)
{
    char value[20];
    snprintf(value, sizeof(value), "%u", number);
    lv_textarea_set_text(field, value);
}
static void wadaSendInfo(lv_event_t *e)
{
    const intptr_t action = (intptr_t)lv_event_get_user_data(e);
    wadaSettingsResult(wadaBackend->sendNodeInfo(action == 1, action == 2));
}
static bool wadaManagedSettings()
{
    if (!wadaBackend || !wadaBackend->isManaged()) return false;
    auto *card = setCard("Managed node");
    setNote(card, "Configure this node through an authorized Meshtastic remote administrator. Local display and chat history settings remain available.");
    char current[180];
    snprintf(current, sizeof(current), "%s\n!%08x\n%s / %s", wadaBackend->ownUser().long_name,
             wadaBackend->nodeNumber(), wada::regionName(wadaBackend->loraConfig().region),
             wada::presetName(wadaBackend->loraConfig().modem_preset));
    setNote(card, current);
    return true;
}
static void wadaSaveIdentity(lv_event_t *)
{
    const char *longName = lv_textarea_get_text(wadaLongName), *shortName = lv_textarea_get_text(wadaShortName);
    if (!wada::statusVisible(longName) || !wada::statusText(longName, wada::longNameMaxBytes, true) ||
        !wada::statusText(shortName, sizeof(wadaBackend->ownUser().short_name) - 1, true)) {
        g_lv.task->showAlert("Use a nonblank long name up to 24 UTF-8 bytes and short name up to 4", 2600);
        return;
    }
    wadaBeforeChange();
    wadaSettingsResult(wadaBackend->setOwner(lv_textarea_get_text(wadaLongName), lv_textarea_get_text(wadaShortName)));
}
static void wadaBuildProfileSettings()
{
    if (wadaManagedSettings()) return;
    auto *c = setCard("Meshtastic identity");
    const auto &owner = wadaBackend->ownUser();
    wadaLongName = setField(c, "Long name (24 bytes)", nullptr, 0);
    lv_textarea_set_text(wadaLongName, owner.long_name);
    wadaShortName = setField(c, "Short name (4 bytes)", nullptr, 0);
    lv_textarea_set_text(wadaShortName, owner.short_name);
    setButton(c, LV_SYMBOL_SAVE, "Apply names", wadaSaveIdentity);
    char text[96];
    snprintf(text, sizeof(text), "Node !%08x", wadaBackend->nodeNumber());
    setNote(c, text);
    if (owner.public_key.size == 32) {
        for (unsigned i = 0; i < 32; ++i)
            snprintf(text + i * 2, 3, "%02x", owner.public_key.bytes[i]);
        setNote(c, "Meshtastic public key");
        setNote(c, text);
    }
    setActionRow(c, UI_ICON_QR_CODE, "Share contact QR", nullptr, [](lv_event_t *) { openShareMyContactPopup(); });
    setActionRow(c, UI_ICON_MAP_PIN, "Position settings", nullptr, [](lv_event_t *) { openSettingsCategory(CAT_GPS); });
}
static unsigned wadaRegionValues[_meshtastic_Config_LoRaConfig_RegionCode_ARRAYSIZE + 1] = {}, wadaRegionCount = 0;
static meshtastic_Config_LoRaConfig_RegionCode wadaSelectedRegion()
{
    const unsigned index = lv_dropdown_get_selected(wadaRegion);
    return index < wadaRegionCount ? static_cast<meshtastic_Config_LoRaConfig_RegionCode>(wadaRegionValues[index])
                                  : meshtastic_Config_LoRaConfig_RegionCode_UNSET;
}
static void wadaPopulateRegions(meshtastic_Config_LoRaConfig_RegionCode current)
{
    char options[640] = {};
    size_t length = 0;
    unsigned selected = 0;
    wadaRegionCount = 0;
    if (!wada::supportedRegion(current)) {
        length = snprintf(options, sizeof options, TR("Unavailable: %s"), wada::regionName(current));
        wadaRegionValues[wadaRegionCount++] = current;
    }
    for (unsigned region = 0; region < _meshtastic_Config_LoRaConfig_RegionCode_ARRAYSIZE; ++region) {
        if (!wada::supportedRegion(region)) continue;
        if (region == unsigned(current)) selected = wadaRegionCount;
        wadaRegionValues[wadaRegionCount++] = region;
        length += snprintf(options + length, sizeof(options) - length, "%s%s", length ? "\n" : "", wada::regionName(region));
    }
    lv_dropdown_set_options(wadaRegion, options);
    lv_dropdown_set_selected(wadaRegion, selected);
}
static void wadaPresetFieldsChanged(lv_event_t *);
static bool wadaPresetChoice(unsigned &value)
{
    const unsigned index = wadaPreset ? lv_dropdown_get_selected(wadaPreset) : wadaPresetCount;
    if (index >= wadaPresetCount) return false;
    value = wadaPresetValues[index];
    const auto region = wadaSelectedRegion();
    return value < _meshtastic_Config_LoRaConfig_ModemPreset_ARRAYSIZE &&
           value != meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW &&
           wadaBackend->validPreset(region, static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(value));
}
static void wadaPopulatePresets()
{
    const auto region = wadaSelectedRegion();
    const bool allowed = wadaDraftPreset != meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW &&
        wadaBackend->validPreset(region, static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(wadaDraftPreset));
    char options[512] = {};
    size_t length = 0;
    unsigned selected = 0;
    wadaPresetCount = 0;
    if (!allowed) {
        length = snprintf(options, sizeof(options), "%s", TR("Choose a valid preset"));
        wadaPresetValues[wadaPresetCount++] = UINT_MAX;
    }
    for (unsigned preset = 0; preset < _meshtastic_Config_LoRaConfig_ModemPreset_ARRAYSIZE; ++preset) {
        if (preset == meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW ||
            !wadaBackend->validPreset(region, static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(preset))) continue;
        if (preset == wadaDraftPreset) selected = wadaPresetCount;
        wadaPresetValues[wadaPresetCount++] = preset;
        length += snprintf(options + length, sizeof(options) - length, "%s%s", length ? "\n" : "", wada::effectivePresetName(preset));
    }
    if (!wadaPresetCount || (wadaPresetCount == 1 && wadaPresetValues[0] == UINT_MAX)) {
        snprintf(options, sizeof(options), "%s", TR("No available presets"));
        wadaPresetValues[0] = UINT_MAX;
        wadaPresetCount = 1;
    }
    lv_dropdown_set_options(wadaPreset, options);
    lv_dropdown_set_selected(wadaPreset, selected);
    if (wadaRegionNotice) {
        char notice[256] = {};
        if (!wada::supportedRegion(region))
            snprintf(notice, sizeof notice, "%s", TR("This stored region is unavailable in this firmware. Choose a supported region."));
        else if (wadaBackend->regionLicensedOnly(region))
            snprintf(notice, sizeof(notice), "%s", wadaBackend->ownUser().is_licensed ?
                TR("Licensed amateur band. Follow the license requirements for your location.") :
                TR("This region requires an amateur radio license and licensed operator configuration."));
        if (!allowed) {
            const size_t used = strlen(notice);
            snprintf(notice + used, sizeof(notice) - used, TR("%sPreset %s is unavailable in this region. Choose an allowed preset to use preset mode."),
                     used ? "\n" : "", wada::presetName(wadaDraftPreset));
        }
        lv_label_set_text(wadaRegionNotice, notice);
        if (notice[0]) lv_obj_clear_flag(wadaRegionNotice, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(wadaRegionNotice, LV_OBJ_FLAG_HIDDEN);
    }
}
static void wadaPresetChanged(lv_event_t *e)
{
    if (e && lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    unsigned selected;
    if (wadaPresetChoice(selected)) {
        wadaDraftPreset = selected;
        wadaPopulatePresets();
    }
    wadaPresetFieldsChanged(nullptr);
}
static void wadaRegionChanged(lv_event_t *e)
{
    if (e && lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    const auto region = wadaSelectedRegion();
    if (region != wadaDraftRegion && wadaUsePreset && lv_obj_has_state(wadaUsePreset, LV_STATE_CHECKED))
        wadaDraftPreset = wadaBackend->presetForRegionChange(wadaDraftRegion, region,
            static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(wadaDraftPreset));
    wadaDraftRegion = region;
    wadaPopulatePresets();
    wadaPresetFieldsChanged(nullptr);
}
static void wadaPresetFieldsChanged(lv_event_t *)
{
    const bool preset = lv_obj_has_state(wadaUsePreset, LV_STATE_CHECKED);
    for (auto *field : {wadaBandwidth, wadaSf}) {
        if (!field) continue;
        auto *row = lv_obj_get_parent(field);
        if (preset) lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
    if (wadaPreset) {
        if (preset) lv_obj_clear_state(wadaPreset, LV_STATE_DISABLED);
        else lv_obj_add_state(wadaPreset, LV_STATE_DISABLED);
    }
    if (wadaCr) {
        const unsigned previous = lv_dropdown_get_selected(wadaCr);
        const unsigned current = previous < wadaCrCount ? wadaCrValues[previous] : 0;
        const unsigned base = wada::presetCodingRate(wadaDraftPreset);
        char options[120] = {};
        size_t length = 0;
        wadaCrCount = 0;
        if (preset) {
            length = snprintf(options, sizeof(options), "Preset default (4/%u)", base);
            wadaCrValues[wadaCrCount++] = 0;
        }
        for (unsigned cr = preset ? base + 1 : 5; cr <= 8; ++cr) {
            length += snprintf(options + length, sizeof(options) - length, "%s4/%u", length ? "\n" : "", cr);
            wadaCrValues[wadaCrCount++] = cr;
        }
        lv_dropdown_set_options(wadaCr, options);
        unsigned selected = 0;
        for (unsigned index = 0; index < wadaCrCount; ++index)
            if (wadaCrValues[index] == current) selected = index;
        lv_dropdown_set_selected(wadaCr, selected);
        if (wadaCrHelp) {
            char note[256];
            if (preset)
                snprintf(note, sizeof(note), TR("This preset uses 4/%u. A higher coding rate adds error correction and lengthens transmissions, using more airtime. Peers on this preset can still receive it."), base);
            else
                snprintf(note, sizeof(note), "%s", TR("Custom radios need matching bandwidth, spreading factor and frequency. A higher coding rate adds error correction and uses more airtime."));
            lv_label_set_text(wadaCrHelp, note);
        }
    }
}
static void wadaSaveRadio(lv_event_t *)
{
    meshtastic_Config c = {};
    c.which_payload_variant = meshtastic_Config_lora_tag;
    c.payload_variant.lora = wadaBackend->loraConfig();
    auto &l = c.payload_variant.lora;
    unsigned hop, slot;
    const char *powerText = lv_textarea_get_text(wadaPower);
    char *end = nullptr;
    long power = strtol(powerText, &end, 10);
    if (!wadaUnsignedField(wadaHop, 7, hop) || !wadaUnsignedField(wadaFrequencySlot, 65535, slot) || !*powerText || *end ||
        power < -9 || power > 30) {
        g_lv.task->showAlert("Use hops 0-7, power -9..30, and a valid frequency slot", 2300);
        return;
    }
    l.region = wadaSelectedRegion();
    if (lv_dropdown_get_selected(wadaRegion) >= wadaRegionCount || !wada::supportedRegion(l.region)) {
        g_lv.task->showAlert("Choose a region supported by this firmware", 2300);
        return;
    }
    l.use_preset = lv_obj_has_state(wadaUsePreset, LV_STATE_CHECKED);
    if (wadaBackend->regionLicensedOnly(l.region) && !wadaBackend->ownUser().is_licensed) {
        g_lv.task->showAlert("This region requires licensed amateur radio operator configuration", 2400);
        return;
    }
    unsigned selectedPreset = wadaDraftPreset;
    if (l.use_preset && !wadaPresetChoice(selectedPreset)) {
        g_lv.task->showAlert("Choose a valid modem preset for this region", 2300);
        return;
    }
    l.modem_preset = static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(selectedPreset);
    unsigned bw = l.bandwidth, sf = l.spread_factor;
    const unsigned selectedCr = lv_dropdown_get_selected(wadaCr);
    const unsigned cr = selectedCr < wadaCrCount ? wadaCrValues[selectedCr] : 0;
    if ((!l.use_preset && (!wada::bandwidthCode(lv_textarea_get_text(wadaBandwidth),
             l.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24, bw) ||
             !wadaUnsignedField(wadaSf, 12, sf) || sf < 5 || cr < 5)) ||
        (l.use_preset && cr && cr <= wada::presetCodingRate(l.modem_preset))) {
        g_lv.task->showAlert("Choose a supported bandwidth, spreading factor and coding rate", 2300);
        return;
    }
    const char *frequencyText = lv_textarea_get_text(wadaOverride);
    char *frequencyEnd = nullptr;
    double frequency = strtod(frequencyText, &frequencyEnd);
    if (!*frequencyText || *frequencyEnd || !isfinite(frequency) || frequency < 0 || frequency > 2500) {
        g_lv.task->showAlert("Use 0 for automatic frequency, or a valid MHz value", 2300);
        return;
    }
    l.bandwidth = bw;
    l.spread_factor = sf;
    l.coding_rate = cr;
    l.override_frequency = frequency;
    if (l.use_preset && !wadaBackend->validPreset(l.region, l.modem_preset)) {
        g_lv.task->showAlert("Preset is unavailable in this region", 2000);
        return;
    }
    l.hop_limit = hop;
    l.tx_power = power;
    l.channel_num = slot;
    l.tx_enabled = lv_obj_has_state(wadaTx, LV_STATE_CHECKED);
    l.sx126x_rx_boosted_gain = lv_obj_has_state(wadaRxBoost, LV_STATE_CHECKED);
    l.ignore_mqtt = lv_obj_has_state(wadaIgnoreMqtt, LV_STATE_CHECKED);
    wadaBeforeChange();
    wadaSettingsResult(wadaBackend->setConfig(c));
}
static void wadaBuildRadioSettings()
{
    if (wadaManagedSettings()) return;
    const auto &l = wadaBackend->loraConfig();
    wadaDraftRegion = l.region;
    wadaDraftPreset = l.modem_preset;
    wadaUsePreset = wadaRegionNotice = nullptr;
    wadaCr = wadaCrHelp = wadaBandwidth = wadaSf = wadaOverride = nullptr;
    auto *c = setCard("Meshtastic LoRa");
    wadaRegion = setDropdownRow(c, "Region", nullptr, "", 0, wadaRegionChanged);
    wadaPopulateRegions(l.region);
    wadaPreset = setDropdownRow(c, "Modem preset", nullptr, "", 0, wadaPresetChanged);
    wadaRegionNotice = setNote(c, "");
    wadaTx = setSwitchRow(c, "Transmit enabled", nullptr, l.tx_enabled, nullptr);
    wadaHop = setField(c, "Hop limit", "0 = firmware default", 1);
    wadaFieldNumber(wadaHop, l.hop_limit);
    wadaPower = setField(c, "TX power (dBm)", "0 = automatic", 3);
    char p[12];
    snprintf(p, sizeof(p), "%d", l.tx_power);
    lv_textarea_set_text(wadaPower, p);
    wadaFrequencySlot = setField(c, "Frequency slot", "0 = derive from primary channel", 5);
    wadaFieldNumber(wadaFrequencySlot, l.channel_num);
    wadaRxBoost = setSwitchRow(c, "SX126x RX boosted gain", nullptr, l.sx126x_rx_boosted_gain, nullptr);
    wadaIgnoreMqtt = setSwitchRow(c, "Ignore MQTT traffic", nullptr, l.ignore_mqtt, nullptr);
    setNote(c, "Peers need matching region, modem preset and frequency slot. Channel encryption is configured in Channels.");
    c = setCard("Advanced LoRa");
    wadaUsePreset = setSwitchRow(c, "Use modem preset", nullptr, l.use_preset, wadaPresetFieldsChanged);
    wadaBandwidth = setField(c, "Custom bandwidth (kHz)", "0 = default", 10);
    char bandwidth[20];
    snprintf(bandwidth, sizeof(bandwidth), "%.6g", l.bandwidth ? wada::bandwidthKHz(l.bandwidth) : 0.0);
    lv_textarea_set_text(wadaBandwidth, bandwidth);
    wadaSf = setField(c, "Custom spreading factor", "5-12", 2);
    wadaFieldNumber(wadaSf, l.spread_factor);
    wadaCr = setDropdownRow(c, "Coding rate", nullptr, "Preset default", 0, nullptr);
    wadaCrValues[0] = l.coding_rate;
    wadaCrCount = 1;
    wadaCrHelp = setNote(c, "");
    wadaOverride = setField(c, "Frequency override (MHz)", "0 = automatic; applies with presets too", 12);
    char frequency[20];
    snprintf(frequency, sizeof(frequency), "%.6f", (double)l.override_frequency);
    lv_textarea_set_text(wadaOverride, frequency);
    setNote(
        c,
        "Turn off modem presets to choose custom radio parameters. Meshtastic validates and applies these settings.");
    wadaPopulatePresets();
    wadaPresetFieldsChanged(nullptr);
    setButton(c, LV_SYMBOL_SAVE, "Apply LoRa settings", wadaSaveRadio);
    setActionRow(c, UI_ICON_RADIO_TOWER, "Node info & broadcasts", nullptr,
                 [](lv_event_t *) { openSettingsCategory(CAT_NODEINFO); });
}
static void wadaSaveBroadcasts(lv_event_t *)
{
    unsigned node, position;
    if (!wadaUnsignedField(wadaNodeInterval, 604800, node) || !wadaUnsignedField(wadaPositionInterval, 604800, position)) {
        g_lv.task->showAlert("Enter a valid interval up to 604800 seconds", 2400);
        return;
    }
    const bool nodeChanged = node != wadaBackend->deviceSettings().node_info_broadcast_secs;
    const bool positionChanged = position != wadaBackend->positionSettings().position_broadcast_secs;
    if (!nodeChanged && !positionChanged) { g_lv.task->showAlert("No changes", 1200); return; }
    if (nodeChanged && node != 0 && node < 3600) {
        g_lv.task->showAlert("Use 0 for the default node-info interval, or at least 3600 seconds", 2400);
        return;
    }
    wadaBeforeChange();
    bool ok = true;
    meshtastic_Config c = {};
    if (nodeChanged) {
        c.which_payload_variant = meshtastic_Config_device_tag;
        c.payload_variant.device = wadaBackend->deviceSettings();
        c.payload_variant.device.node_info_broadcast_secs = node;
        ok = wadaBackend->setConfig(c);
    }
    if (positionChanged) {
        c = {};
        c.which_payload_variant = meshtastic_Config_position_tag;
        c.payload_variant.position = wadaBackend->positionSettings();
        c.payload_variant.position.position_broadcast_secs = position;
        ok = wadaBackend->setConfig(c) && ok;
    }
    wadaSettingsResult(ok);
}
static void wadaBuildNodeInfoSettings()
{
    if (wadaManagedSettings()) return;
    auto *c = setCard("Meshtastic node info");
    setNote(c, "Node-info packets announce your name, node ID and public key. Position reports use separate Meshtastic packets.");
    setButton(c, UI_ICON_RADIO_TOWER, "Broadcast node info", wadaSendInfo, (void *)0);
    setButton(c, UI_ICON_RADIO, "Send within one hop", wadaSendInfo, (void *)1);
    setButton(c, UI_ICON_REFRESH_CW, "Request nearby node info", wadaSendInfo, (void *)2);
    c = setCard("Firmware broadcast schedule");
    wadaNodeInterval = setField(c, "Node info interval (seconds)", "0 applies default (10800 s); otherwise at least 3600 s", 6);
    wadaFieldNumber(wadaNodeInterval, wadaBackend->deviceSettings().node_info_broadcast_secs);
    wadaPositionInterval = setField(c, "Position interval (seconds)", "0 = firmware default", 6);
    wadaFieldNumber(wadaPositionInterval, wadaBackend->positionSettings().position_broadcast_secs);
    setNote(c,
            "Meshtastic schedules these packets and applies its airtime limits. Position interval 0 uses the firmware default.");
    setButton(c, LV_SYMBOL_SAVE, "Apply broadcast intervals", wadaSaveBroadcasts);
}
static void wadaBuildNodeSettings()
{
    auto *c = setCard("Meshtastic node database");
    char text[96];
    snprintf(text, sizeof(text), "%u native nodes mirrored. Favorites and removals are saved by Meshtastic.",
             (unsigned)wadaBackend->mirroredNodes());
    setNote(c, text);
    setButton(c, UI_ICON_REFRESH_CW, "Refresh nodes and configuration", [](lv_event_t *) {
        wadaBackend->resync();
        g_lv.task->showAlert("Refreshing Meshtastic snapshot", 1500);
    });
    setActionRow(c, UI_ICON_RADIO_TOWER, "Node info & broadcasts", nullptr,
                 [](lv_event_t *) { openSettingsCategory(CAT_NODEINFO); });
}
static void wadaToggleWifi()
{
    if (!wadaBackend) return;
    meshtastic_Config c = {};
    c.which_payload_variant = meshtastic_Config_network_tag;
    c.payload_variant.network = wadaBackend->networkConfig();
    c.payload_variant.network.wifi_enabled = !c.payload_variant.network.wifi_enabled;
    wadaBeforeChange();
    wadaSettingsResult(wadaBackend->setConfig(c));
}
static void wadaWifiFieldsChanged(lv_event_t *)
{
    const bool enabled = lv_obj_has_state(wadaWifiEnabled, LV_STATE_CHECKED);
    for (auto *field : {wadaWifiSsid, wadaWifiPsk}) {
        if (!field) continue;
        auto *row = lv_obj_get_parent(field);
        if (enabled) lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
}
static void wadaSaveWifi(lv_event_t *)
{
    meshtastic_Config c = {};
    c.which_payload_variant = meshtastic_Config_network_tag;
    c.payload_variant.network = wadaBackend->networkConfig();
    auto &n = c.payload_variant.network;
    n.wifi_enabled = lv_obj_has_state(wadaWifiEnabled, LV_STATE_CHECKED);
    if (n.wifi_enabled) {
        const char *ssid = lv_textarea_get_text(wadaWifiSsid), *psk = lv_textarea_get_text(wadaWifiPsk);
        if (!wada::statusText(ssid, sizeof(n.wifi_ssid) - 1, true) ||
            !wada::statusText(psk, sizeof(n.wifi_psk) - 1, true)) {
            g_lv.task->showAlert("Use a network name up to 32 bytes and password up to 64 bytes", 2600);
            return;
        }
        memcpy(n.wifi_ssid, ssid, strlen(ssid) + 1);
        if (*psk) memcpy(n.wifi_psk, psk, strlen(psk) + 1);
    }
    if (g_lv.task)
        g_lv.task->persistHistoryNow();
    wadaSettingsResult(wadaBackend->setConfig(c));
}
static void wadaBuildWifiSettings()
{
    if (wadaManagedSettings()) return;
    auto *c = setCard("Meshtastic Wi-Fi");
    const auto &n = wadaBackend->networkConfig();
    wadaWifiEnabled = setSwitchRow(c, "Wi-Fi enabled", nullptr, n.wifi_enabled, wadaWifiFieldsChanged);
    wadaWifiSsid = setField(c, "Network name (32 bytes)", nullptr, 0);
    lv_textarea_set_text(wadaWifiSsid, n.wifi_ssid);
    wadaWifiPsk = setField(c, "Password (64 bytes)", "Leave empty to preserve", 0);
    lv_textarea_set_password_mode(wadaWifiPsk, true);
    wadaWifiFieldsChanged(nullptr);
    setButton(c, LV_SYMBOL_SAVE, "Apply Wi-Fi settings", wadaSaveWifi);
}
static void wadaSaveBluetooth(lv_event_t *)
{
    meshtastic_Config c = {};
    c.which_payload_variant = meshtastic_Config_bluetooth_tag;
    c.payload_variant.bluetooth = wadaBackend->bluetoothConfig();
    auto &b = c.payload_variant.bluetooth;
    b.enabled = lv_obj_has_state(wadaBtEnabled, LV_STATE_CHECKED);
    const char *text = lv_textarea_get_text(wadaBtPin);
    if (*text) {
        char *end = nullptr;
        unsigned long pin = strtoul(text, &end, 10);
        if (*end || pin > 999999 || pin < 100000) {
            g_lv.task->showAlert("Use a six-digit pairing PIN", 1800);
            return;
        }
        b.fixed_pin = pin;
        b.mode = meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN;
    }
    if (g_lv.task)
        g_lv.task->persistHistoryNow();
    wadaSettingsResult(wadaBackend->setConfig(c));
}
static void wadaBuildBluetoothSettings()
{
    if (wadaManagedSettings()) return;
    auto *c = setCard("Meshtastic Bluetooth");
    const auto &b = wadaBackend->bluetoothConfig();
    wadaBtEnabled = setSwitchRow(c, "Bluetooth enabled", nullptr, b.enabled, nullptr);
    wadaBtPin = setField(c, "Pairing PIN", "Leave empty to preserve", 6);
    setNote(c, "The WADAMESH display can remain active while a phone connects.");
    setButton(c, LV_SYMBOL_SAVE, "Apply Bluetooth settings", wadaSaveBluetooth);
}
static void wadaKeyModeChanged(lv_event_t *)
{
    const unsigned mode = lv_dropdown_get_selected(wadaKeyMode);
    if (mode == 3 || mode == 4)
        lv_obj_clear_state(wadaChannelPsk, LV_STATE_DISABLED);
    else
        lv_obj_add_state(wadaChannelPsk, LV_STATE_DISABLED);
}
static void wadaLoadChannel(lv_event_t *)
{
    wadaSelectedChannel = lv_dropdown_get_selected(wadaChannelSlot);
    const auto &ch = wadaBackend->channelConfig(wadaSelectedChannel);
    lv_textarea_set_text(wadaChannelName, ch.settings.name);
    lv_textarea_set_text(wadaChannelPsk, "");
    lv_dropdown_set_selected(wadaChannelRole, ch.role);
    lv_dropdown_set_selected(wadaKeyMode, 0);
    auto toggle = [](lv_obj_t *o, bool checked) {
        if (checked)
            lv_obj_add_state(o, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(o, LV_STATE_CHECKED);
    };
    toggle(wadaChannelUplink, ch.settings.uplink_enabled);
    toggle(wadaChannelDownlink, ch.settings.downlink_enabled);
    toggle(wadaChannelMuted, ch.settings.has_module_settings && ch.settings.module_settings.is_muted);
    wadaFieldNumber(wadaChannelPrecision, ch.settings.has_module_settings ? ch.settings.module_settings.position_precision : 0);
    char text[120];
    snprintf(text, sizeof(text), "Current key: %s. Empty primary names use the modem preset name.",
             ch.settings.psk.size == 0    ? (ch.role == meshtastic_Channel_Role_SECONDARY ? "inherits primary channel key" : "unencrypted")
             : ch.settings.psk.size == 1  ? (ch.settings.psk.bytes[0] == 0 ? "unencrypted" : "public shorthand")
             : ch.settings.psk.size == 16 ? "AES-128"
                                          : "AES-256");
    lv_label_set_text(wadaKeyStatus, text);
    wadaKeyModeChanged(nullptr);
}
static bool wadaChannelUsesPublicKey(const meshtastic_Channel &channel)
{
    if (channel.role == meshtastic_Channel_Role_DISABLED) return false;
    const auto *key = &channel.settings.psk;
    if (!key->size && channel.role == meshtastic_Channel_Role_SECONDARY) {
        const auto &primary = wadaBackend->channelConfig(wadaBackend->primaryIndex());
        if (primary.role != meshtastic_Channel_Role_PRIMARY) return true;
        key = &primary.settings.psk;
    }
    static constexpr uint8_t publicPrefix[] = {0xd4,0xf1,0xbb,0x3a,0x20,0x29,0x07,0x59,0xf0,0xbc,0xff,0xab,0xcf,0x4e,0x69};
    return key->size <= 1 || (key->size == 16 && !memcmp(key->bytes, publicPrefix, sizeof publicPrefix));
}
static void wadaSaveChannel(lv_event_t *)
{
    const unsigned index = lv_dropdown_get_selected(wadaChannelSlot);
    auto ch = wadaBackend->channelConfig(index);
    ch.index = index;
    ch.role = static_cast<meshtastic_Channel_Role>(lv_dropdown_get_selected(wadaChannelRole));
    ch.has_settings = true;
    if (wadaBackend->channelConfig(index).role == meshtastic_Channel_Role_PRIMARY && ch.role != meshtastic_Channel_Role_PRIMARY) {
        g_lv.task->showAlert("Make another channel primary before changing this channel's role", 2300);
        return;
    }
    if (strlen(lv_textarea_get_text(wadaChannelName)) >= sizeof(ch.settings.name)) {
        g_lv.task->showAlert("Channel names must fit in 11 UTF-8 bytes", 2200);
        return;
    }
    snprintf(ch.settings.name, sizeof(ch.settings.name), "%s", lv_textarea_get_text(wadaChannelName));
    unsigned precision;
    if (!wadaUnsignedField(wadaChannelPrecision, 32, precision)) {
        g_lv.task->showAlert("Position precision must be 0-32 bits", 1700);
        return;
    }
    const unsigned mode = lv_dropdown_get_selected(wadaKeyMode);
    if (mode == 0 && ch.role == meshtastic_Channel_Role_PRIMARY &&
        wadaBackend->channelConfig(index).role == meshtastic_Channel_Role_SECONDARY && !ch.settings.psk.size) {
        // A secondary's empty key inherits; after promotion it would mean cleartext.
        ch.settings.psk = wadaBackend->channelConfig(wadaBackend->primaryIndex()).settings.psk;
    }
    if (mode == 1) {
        ch.settings.psk = {};
        ch.settings.psk.size = 1;
        ch.settings.psk.bytes[0] = 1;
    } else if (mode == 2) {
        ch.settings.psk = {};
        ch.settings.psk.size = 32;
        esp_fill_random(ch.settings.psk.bytes, 32);
    } else if (mode == 3 || mode == 4) {
        if (!wada::channelKey(lv_textarea_get_text(wadaChannelPsk), mode == 3, ch.settings.psk)) {
            g_lv.task->showAlert("Use a 1, 16 or 32-byte Meshtastic key in the selected encoding", 2500);
            return;
        }
    } else if (mode == 5) {
        ch.settings.psk = {};
        ch.settings.psk.size = 1;
        ch.settings.psk.bytes[0] = 0;
    }
    if (wadaChannelUsesPublicKey(ch) && precision > MAX_POSITION_PRECISION_PUBLIC_KEY) {
        precision = MAX_POSITION_PRECISION_PUBLIC_KEY;
        char value[8];
        snprintf(value, sizeof value, "%u", precision);
        setTextareaSynced(wadaChannelPrecision, value);
    }
    ch.settings.uplink_enabled = lv_obj_has_state(wadaChannelUplink, LV_STATE_CHECKED);
    ch.settings.downlink_enabled = lv_obj_has_state(wadaChannelDownlink, LV_STATE_CHECKED);
    ch.settings.has_module_settings = true;
    ch.settings.module_settings.position_precision = precision;
    ch.settings.module_settings.is_muted = lv_obj_has_state(wadaChannelMuted, LV_STATE_CHECKED);
    wadaBeforeChange();
    wadaSettingsResult(wadaBackend->saveChannel(ch));
}
static void wadaBuildChannelSettings()
{
    if (wadaManagedSettings()) return;
    auto *c = setCard("Meshtastic channels");
    char slots[240] = {};
    size_t n = 0;
    for (unsigned i = 0; i < 8; ++i) {
        const auto &ch = wadaBackend->channelConfig(i);
        n += snprintf(slots + n, sizeof(slots) - n, "%s[%u] %s", i ? "\n" : "", i,
                      ch.role ? ch.settings.name[0] ? ch.settings.name : wada::presetName(wadaBackend->loraConfig().modem_preset)
                              : "Disabled");
    }
    wadaChannelSlot = setDropdownRow(c, "Channel slot", nullptr, slots, wadaSelectedChannel, wadaLoadChannel);
    wadaChannelRole = setDropdownRow(c, "Role", nullptr, "Disabled\nPrimary\nSecondary", 0, nullptr);
    wadaChannelName = setField(c, "Name (11 bytes)", "Empty = preset name", 11);
    wadaKeyStatus = setNote(c, "");
    wadaKeyMode = setDropdownRow(
        c, "Encryption key", nullptr,
        "Keep current key\nDefault channel key\nGenerate AES-256 key\nEnter base64 key\nEnter hex key\nNo encryption", 0,
        wadaKeyModeChanged);
    wadaChannelPsk = setField(c, "PSK", nullptr, 64);
    lv_textarea_set_password_mode(wadaChannelPsk, true);
    wadaChannelUplink = setSwitchRow(c, "MQTT uplink", nullptr, false, nullptr);
    wadaChannelDownlink = setSwitchRow(c, "MQTT downlink", nullptr, false, nullptr);
    wadaChannelPrecision = setField(c, "Position precision (bits)", "0 = position sharing off", 2);
    setNote(c, "Public or unencrypted keys limit position precision to 15 bits.");
    wadaChannelMuted = setSwitchRow(c, "Muted channel", nullptr, false, nullptr);
    setNote(c, "There is one primary channel. Making a channel primary replaces that role on the previous primary. Secondary channels share the same LoRa frequency. MQTT flags require the firmware "
               "MQTT module.");
    wadaLoadChannel(nullptr);
    setButton(c, LV_SYMBOL_SAVE, "Apply channel settings", wadaSaveChannel);
}
static lv_obj_t *wadaGpsMode = nullptr, *wadaLatitude = nullptr, *wadaLongitude = nullptr;
static void wadaSavePosition(lv_event_t *)
{
    if (!wadaBackend)
        return;
    char *latEnd = nullptr, *lonEnd = nullptr;
    const char *latText = lv_textarea_get_text(wadaLatitude), *lonText = lv_textarea_get_text(wadaLongitude);
    const double lat = strtod(latText, &latEnd), lon = strtod(lonText, &lonEnd);
    if (latEnd == latText || lonEnd == lonText || !isfinite(lat) || !isfinite(lon) || *latEnd || *lonEnd ||
        lat < -90 || lat > 90 || lon < -180 || lon > 180) {
        g_lv.task->showAlert("Enter valid latitude and longitude", 2000);
        return;
    }
    if (lat == 0 && lon == 0) {
        g_lv.task->showAlert("Use live GPS position to clear a fixed position", 2300);
        return;
    }
    meshtastic_AdminMessage admin = {};
    admin.which_payload_variant = meshtastic_AdminMessage_set_fixed_position_tag;
    admin.set_fixed_position.has_latitude_i = true;
    admin.set_fixed_position.latitude_i = llround(lat * 1e7);
    admin.set_fixed_position.has_longitude_i = true;
    admin.set_fixed_position.longitude_i = llround(lon * 1e7);
    if (g_lv.task)
        g_lv.task->persistHistoryNow();
    wadaSettingsResult(wadaBackend->sendAdmin(admin));
}
static void wadaApplyGps(lv_event_t *)
{
    meshtastic_Config c = {};
    c.which_payload_variant = meshtastic_Config_position_tag;
    c.payload_variant.position = wadaBackend->positionSettings();
    const unsigned mode = lv_dropdown_get_selected(wadaGpsMode);
    if (mode > meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT) return;
    if (mode == unsigned(c.payload_variant.position.gps_mode)) {
        g_lv.task->showAlert("No changes", 1200);
        return;
    }
    c.payload_variant.position.gps_mode = static_cast<meshtastic_Config_PositionConfig_GpsMode>(mode);
    if (g_lv.task)
        g_lv.task->persistHistoryNow();
    wadaSettingsResult(wadaBackend->setConfig(c));
}
static void wadaUseLiveGps(lv_event_t *)
{
    meshtastic_AdminMessage admin = {};
    admin.which_payload_variant = meshtastic_AdminMessage_remove_fixed_position_tag;
    admin.remove_fixed_position = true;
    if (g_lv.task)
        g_lv.task->persistHistoryNow();
    wadaSettingsResult(wadaBackend->sendAdmin(admin));
}
static void wadaBuildPositionSettings()
{
    if (wadaManagedSettings()) return;
    auto *c = setCard("Meshtastic position");
    wadaGpsMode = setDropdownRow(c, "GPS mode", nullptr, "Disabled\nEnabled\nNot present",
                                int(wadaBackend->positionSettings().gps_mode), nullptr);
    setButton(c, LV_SYMBOL_SAVE, "Apply GPS setting", wadaApplyGps);
    char value[32];
    wadaLatitude = setField(c, "Latitude", nullptr, 16);
    snprintf(value, sizeof(value), "%.7f", sensors.node_lat);
    lv_textarea_set_text(wadaLatitude, value);
    wadaLongitude = setField(c, "Longitude", nullptr, 16);
    snprintf(value, sizeof(value), "%.7f", sensors.node_lon);
    lv_textarea_set_text(wadaLongitude, value);
    setButton(c, LV_SYMBOL_GPS, "Set fixed position", wadaSavePosition);
    setButton(c, LV_SYMBOL_REFRESH, "Use live GPS position", wadaUseLiveGps);
}
