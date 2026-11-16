// Compile actual settings Apply callbacks. Invalid coordinates and over-budget
// UTF-8 credentials must never queue an unintended native change; untouched GPS
// NOT_PRESENT, existing secrets and all channel role/key choices must survive.
#include "WadaNativeConfig.h"
#include "WadaStatus.h"
#include "mesh/PositionPrecision.h"
#include "mesh/generated/meshtastic/admin.pb.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define TR(value) (value)
using std::isfinite;
using std::llround;
constexpr unsigned LV_STATE_CHECKED = 1, LV_STATE_DISABLED = 2, LV_OBJ_FLAG_HIDDEN = 1;
struct lv_obj_t {
    std::string text;
    unsigned selected = 0, states = 0, flags = 0;
};
struct lv_event_t {
    void *data = nullptr;
};
static const char *lv_textarea_get_text(lv_obj_t *p)
{
    return p->text.c_str();
}
static void lv_textarea_set_text(lv_obj_t *p, const char *t)
{
    p->text = t;
}
static void setTextareaSynced(lv_obj_t *p, const char *t)
{
    p->text = t;
}
static bool lv_obj_has_state(lv_obj_t *p, unsigned state)
{
    return p->states & state;
}
static void lv_obj_add_state(lv_obj_t *p, unsigned state)
{
    p->states |= state;
}
static void lv_obj_clear_state(lv_obj_t *p, unsigned state)
{
    p->states &= ~state;
}
static unsigned lv_dropdown_get_selected(lv_obj_t *p)
{
    return p->selected;
}
static void lv_dropdown_set_selected(lv_obj_t *p, unsigned selected)
{
    p->selected = selected;
}
static lv_obj_t *lv_obj_get_parent(lv_obj_t *p)
{
    return p;
}
static void lv_obj_clear_flag(lv_obj_t *p, unsigned flag)
{
    p->flags &= ~flag;
}
static void lv_obj_add_flag(lv_obj_t *p, unsigned flag)
{
    p->flags |= flag;
}
static void lv_label_set_text(lv_obj_t *p, const char *t)
{
    p->text = t;
}
static void *lv_event_get_user_data(lv_event_t *e)
{
    return e->data;
}
static void esp_fill_random(uint8_t *p, size_t n)
{
    memset(p, 0x5a, n);
}
struct Task {
    std::string alert;
    unsigned flushes = 0;
    void showAlert(const char *t, int) { alert = t; }
    void persistHistoryNow() { ++flushes; }
} task;
static struct {
    Task *task = &::task;
} g_lv;
struct Backend {
    bool ready = true, managed = false;
    bool channelsReady() const { return ready; }
    bool isManaged() const { return managed; }
    meshtastic_User owner{};
    std::vector<std::pair<std::string, std::string>> names;
    const auto &ownUser() const { return owner; }
    bool setOwner(const char *l, const char *s)
    {
        names.emplace_back(l, s);
        return true;
    }
    meshtastic_Config_NetworkConfig network{};
    meshtastic_Config_BluetoothConfig bluetooth{};
    meshtastic_Config_PositionConfig position{};
    meshtastic_Config_DeviceConfig device{};
    meshtastic_Channel channels[8]{};
    meshtastic_Config_LoRaConfig lora{};
    std::vector<meshtastic_Config> configs;
    std::vector<meshtastic_AdminMessage> admins;
    std::vector<meshtastic_Channel> channelSaves;
    const auto &networkConfig() const { return network; }
    const auto &bluetoothConfig() const { return bluetooth; }
    const auto &positionSettings() const { return position; }
    const auto &deviceSettings() const { return device; }
    const auto &channelConfig(unsigned i) const { return channels[i]; }
    const auto &loraConfig() const { return lora; }
    unsigned primaryIndex() const
    {
        for (unsigned i = 0; i < 8; ++i)
            if (channels[i].role == meshtastic_Channel_Role_PRIMARY)
                return i;
        return 0;
    }
    bool setConfig(const meshtastic_Config &c)
    {
        configs.push_back(c);
        return true;
    }
    bool sendAdmin(const meshtastic_AdminMessage &a)
    {
        admins.push_back(a);
        return true;
    }
    bool saveChannel(const meshtastic_Channel &c)
    {
        channelSaves.push_back(c);
        return true;
    }
    bool sendNodeInfo(bool, bool) { return true; }
} backend;
static Backend *wadaBackend = &backend;
static void wadaSettingsResult(bool success)
{
    assert(success);
}
static void wadaBeforeChange()
{
    task.persistHistoryNow();
}
static bool wadaUnsignedField(lv_obj_t *p, unsigned max, unsigned &value)
{
    return wada::unsignedValue(p->text.c_str(), max, value);
}
static void wadaFieldNumber(lv_obj_t *p, unsigned n)
{
    p->text = std::to_string(n);
}
static lv_obj_t wifiEnabled, wifiSsid, wifiPsk, btEnabled, btPin, gpsMode, latitude, longitude, nodeInterval, positionInterval;
static lv_obj_t channelSlot, channelName, channelPsk, channelRole, keyMode, uplink, downlink, precision, muted, keyStatus;
static lv_obj_t *wadaWifiEnabled = &wifiEnabled, *wadaWifiSsid = &wifiSsid, *wadaWifiPsk = &wifiPsk, *wadaBtEnabled = &btEnabled,
                *wadaBtPin = &btPin;
static lv_obj_t *wadaGpsMode = &gpsMode, *wadaLatitude = &latitude, *wadaLongitude = &longitude,
                *wadaNodeInterval = &nodeInterval, *wadaPositionInterval = &positionInterval;
static lv_obj_t *wadaChannelSlot = &channelSlot, *wadaChannelName = &channelName, *wadaChannelPsk = &channelPsk,
                *wadaChannelRole = &channelRole, *wadaKeyMode = &keyMode;
static lv_obj_t *wadaChannelUplink = &uplink, *wadaChannelDownlink = &downlink, *wadaChannelPrecision = &precision,
                *wadaChannelMuted = &muted, *wadaKeyStatus = &keyStatus;
static lv_obj_t longName, shortName;
static lv_obj_t *wadaLongName = &longName, *wadaShortName = &shortName;
static unsigned wadaSelectedChannel = 0;
#include "native_settings_actions.inc"
struct SerialStub {
    void enableBle() {}
    bool isBleEnabled() const { return false; }
};
class UITask : public Task
{
  public:
    SerialStub *_serial = nullptr;
    bool getGPSState();
    void toggleGPS();
    bool isBleEnabled() const;
    void disableBle();
    bool enableBle();
};
#include "native_quick_ui.inc"
int main()
{
    longName.text = " ";
    shortName.text = "S";
    wadaSaveIdentity(nullptr);
    assert(backend.names.empty());
    longName.text = std::string(13, 'x') + std::string(6, 'a');
    shortName.text = "S";
    wadaSaveIdentity(nullptr);
    assert(backend.names.size() == 1);
    longName.text = std::string(25, 'x');
    wadaSaveIdentity(nullptr);
    assert(backend.names.size() == 1);
    longName.text = "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9";
    shortName.text = "\xc3\xa9\xc3\xa9";
    wadaSaveIdentity(nullptr);
    assert(backend.names.size() == 2);
    shortName.text += "\xc3\xa9";
    wadaSaveIdentity(nullptr);
    assert(backend.names.size() == 2);

    strcpy(backend.network.wifi_ssid, "Stored network");
    strcpy(backend.network.wifi_psk, "Stored password");
    wifiEnabled.states = LV_STATE_CHECKED;
    wifiSsid.text = "Mesh network";
    wifiPsk.text = "";
    wadaSaveWifi(nullptr);
    assert(backend.configs.size() == 1);
    assert(std::string(backend.configs.back().payload_variant.network.wifi_psk) == "Stored password");
    wifiSsid.text = std::string(31, 's') + "\xc3\xa9";
    wadaSaveWifi(nullptr);
    assert(backend.configs.size() == 1);
    wifiSsid.text = std::string(30, 's') + "\xc3\xa9";
    wifiPsk.text = std::string(64, 'a');
    wadaSaveWifi(nullptr);
    assert(backend.configs.size() == 2 && strlen(backend.configs.back().payload_variant.network.wifi_ssid) == 32);
    assert(strlen(backend.configs.back().payload_variant.network.wifi_psk) == 64);
    wifiPsk.text = std::string(63, 'p') + "\xc3\xa9";
    wadaSaveWifi(nullptr);
    assert(backend.configs.size() == 2);
    wifiEnabled.states = 0;
    wadaWifiFieldsChanged(nullptr);
    assert(wifiSsid.flags & LV_OBJ_FLAG_HIDDEN);
    wadaSaveWifi(nullptr);
    assert(backend.configs.size() == 3 && !backend.configs.back().payload_variant.network.wifi_enabled);
    assert(std::string(backend.configs.back().payload_variant.network.wifi_ssid) == "Stored network");
    wifiEnabled.states = 1;
    wadaWifiFieldsChanged(nullptr);
    assert(!wifiSsid.flags);
    backend.bluetooth.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
    backend.bluetooth.fixed_pin = 123456;
    btEnabled.states = 1;
    btPin.text = "";
    wadaSaveBluetooth(nullptr);
    assert(backend.configs.back().payload_variant.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN);
    auto count = backend.configs.size();
    btPin.text = "000123";
    wadaSaveBluetooth(nullptr);
    assert(backend.configs.size() == count);
    btPin.text = "123456";
    wadaSaveBluetooth(nullptr);
    assert(backend.configs.back().payload_variant.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN);
    backend.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT;
    gpsMode.selected = 2;
    count = backend.configs.size();
    wadaApplyGps(nullptr);
    assert(backend.configs.size() == count && task.alert == "No changes");
    gpsMode.selected = 1;
    wadaApplyGps(nullptr);
    assert(backend.configs.back().payload_variant.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED);
    gpsMode.selected = 0;
    wadaApplyGps(nullptr);
    assert(backend.configs.back().payload_variant.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_DISABLED);
    latitude.text = "";
    longitude.text = "10";
    wadaSavePosition(nullptr);
    assert(backend.admins.empty());
    latitude.text = "10";
    longitude.text = "";
    wadaSavePosition(nullptr);
    assert(backend.admins.empty());
    latitude.text = " ";
    longitude.text = "10";
    wadaSavePosition(nullptr);
    assert(backend.admins.empty());
    latitude.text = "nan";
    wadaSavePosition(nullptr);
    assert(backend.admins.empty());
    latitude.text = "90.1";
    wadaSavePosition(nullptr);
    assert(backend.admins.empty());
    latitude.text = "0";
    longitude.text = "0";
    wadaSavePosition(nullptr);
    assert(backend.admins.empty());
    latitude.text = "0";
    longitude.text = "-180";
    wadaSavePosition(nullptr);
    assert(backend.admins.size() == 1);
    assert(backend.admins.back().set_fixed_position.has_latitude_i && backend.admins.back().set_fixed_position.latitude_i == 0);
    assert(backend.admins.back().set_fixed_position.longitude_i == -1800000000);
    wadaUseLiveGps(nullptr);
    assert(backend.admins.back().which_payload_variant == meshtastic_AdminMessage_remove_fixed_position_tag);
    backend.device.node_info_broadcast_secs = 0;
    backend.position.position_broadcast_secs = 0;
    nodeInterval.text = "0";
    positionInterval.text = "30";
    count = backend.configs.size();
    wadaSaveBroadcasts(nullptr);
    assert(backend.configs.size() == count + 1 && backend.configs.back().which_payload_variant == meshtastic_Config_position_tag);
    nodeInterval.text = "600";
    wadaSaveBroadcasts(nullptr);
    assert(backend.configs.size() == count + 1);
    nodeInterval.text = "3600";
    wadaSaveBroadcasts(nullptr);
    assert(backend.configs.size() == count + 3);
    backend.device.node_info_broadcast_secs = 3600;
    nodeInterval.text = "0";
    wadaSaveBroadcasts(nullptr);
    assert(backend.configs.back().which_payload_variant == meshtastic_Config_position_tag &&
           backend.configs[backend.configs.size() - 2].payload_variant.device.node_info_broadcast_secs == 0);
    backend.channels[0].role = meshtastic_Channel_Role_PRIMARY;
    backend.channels[1].role = meshtastic_Channel_Role_SECONDARY;
    channelSlot.selected = 1;
    channelRole.selected = meshtastic_Channel_Role_SECONDARY;
    precision.text = "13";
    channelName.text = "Test";
    for (unsigned mode = 0; mode <= 5; ++mode) {
        keyMode.selected = mode;
        channelPsk.text = mode == 3 ? "AQ==" : std::string(64, 'a');
        wadaSaveChannel(nullptr);
    }
    assert(backend.channelSaves.size() == 6);
    assert(backend.channelSaves[0].settings.psk.size == 0);
    assert(backend.channelSaves[1].settings.psk.size == 1 && backend.channelSaves[1].settings.psk.bytes[0] == 1);
    assert(backend.channelSaves[2].settings.psk.size == 32 && backend.channelSaves[2].settings.psk.bytes[0] == 0x5a);
    assert(backend.channelSaves[3].settings.psk.size == 1 && backend.channelSaves[3].settings.psk.bytes[0] == 1);
    assert(backend.channelSaves[4].settings.psk.size == 32 && backend.channelSaves[4].settings.psk.bytes[0] == 0xaa);
    assert(backend.channelSaves[5].settings.psk.size == 1 && backend.channelSaves[5].settings.psk.bytes[0] == 0);
    backend.channels[0].settings.psk.size = 32;
    memset(backend.channels[0].settings.psk.bytes, 0x42, 32);
    keyMode.selected = 0;
    channelRole.selected = meshtastic_Channel_Role_PRIMARY;
    uplink.states = downlink.states = muted.states = 1;
    wadaSaveChannel(nullptr);
    assert(backend.channelSaves.back().settings.psk.size == 32 && backend.channelSaves.back().settings.psk.bytes[0] == 0x42);
    assert(backend.channelSaves.back().role == meshtastic_Channel_Role_PRIMARY &&
           backend.channelSaves.back().settings.uplink_enabled && backend.channelSaves.back().settings.downlink_enabled);
    assert(backend.channelSaves.back().settings.module_settings.is_muted &&
           backend.channelSaves.back().settings.module_settings.position_precision == 13);
    channelRole.selected = 0;
    wadaSaveChannel(nullptr);
    assert(backend.channelSaves.back().role == meshtastic_Channel_Role_DISABLED);
    auto saves = backend.channelSaves.size();
    channelSlot.selected = 0;
    channelRole.selected = 0;
    wadaSaveChannel(nullptr);
    assert(backend.channelSaves.size() == saves);
    channelSlot.selected = 1;
    channelRole.selected = 2;
    channelName.text = std::string(10, 'a') + "\xc3\xa9";
    wadaSaveChannel(nullptr);
    assert(backend.channelSaves.size() == saves);
    channelName.text = "Good";
    precision.text = "33";
    wadaSaveChannel(nullptr);
    assert(backend.channelSaves.size() == saves);
    wadaLoadChannel(nullptr);
    assert(keyStatus.text.find("inherits primary") != std::string::npos);
    wadaToggleWifi();
    assert(backend.configs.back().payload_variant.network.wifi_enabled && !backend.network.wifi_enabled);
    channelName.text = "Good";
    precision.text = "32";
    keyMode.selected = 5;
    wadaSaveChannel(nullptr);
    assert(backend.channelSaves.back().settings.module_settings.position_precision == 15 && precision.text == "15");
    precision.text = "32";
    keyMode.selected = 2;
    wadaSaveChannel(nullptr);
    assert(backend.channelSaves.back().settings.module_settings.position_precision == 32 && precision.text == "32");
    precision.text = "32";
    keyMode.selected = 4;
    channelPsk.text = "d4f1bb3a20290759f0bcffabcf4e69ff";
    wadaSaveChannel(nullptr);
    assert(backend.channelSaves.back().settings.module_settings.position_precision == 15);
    UITask ui;
    backend.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_DISABLED;
    auto quickCount = backend.configs.size();
    ui.toggleGPS();
    assert(backend.configs.size() == quickCount + 1 && !ui.getGPSState());
    assert(backend.configs.back().payload_variant.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED);
    backend.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED;
    assert(ui.getGPSState());
    ui.toggleGPS();
    assert(backend.configs.back().payload_variant.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_DISABLED);
    backend.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT;
    quickCount = backend.configs.size();
    assert(!ui.getGPSState());
    ui.toggleGPS();
    assert(backend.configs.size() == quickCount);
    backend.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_DISABLED;
    backend.managed = true;
    ui.toggleGPS();
    assert(backend.configs.size() == quickCount);
    backend.managed = false;
    backend.ready = false;
    ui.toggleGPS();
    assert(backend.configs.size() == quickCount);
    backend.ready = true;
    backend.bluetooth.enabled = false;
    assert(!ui.isBleEnabled());
    assert(ui.enableBle());
    assert(!ui.isBleEnabled());
    assert(backend.configs.back().payload_variant.bluetooth.enabled);
    backend.bluetooth.enabled = true;
    assert(ui.isBleEnabled());
    ui.disableBle();
    assert(ui.isBleEnabled());
    assert(!backend.configs.back().payload_variant.bluetooth.enabled);
    puts("PASS: native settings Apply controls, byte bounds, GPS/BLE quick commands, broadcasts and channel key/role payloads");
}
