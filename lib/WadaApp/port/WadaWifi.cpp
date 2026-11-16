// UI compatibility accessors. Meshtastic remains the owner of network settings.
#include "WadaMeshtasticBackend.h"
#include "helpers/esp32/WifiRuntimeStore.h"
#include "mesh/Throttle.h"
#include <Arduino.h>
#include <atomic>
void wifiConfigRequestApply();
namespace
{
std::atomic<bool> scanning{false}, applyRequested{false}, enabled{false}, hasCredentials{false};
std::atomic<int> bluetoothRequested{-1};
meshtastic_Config_NetworkConfig staged{};
bool editing = false, attempted = false;
uint32_t stagedSelf = 0, bluetoothSelf = 0, lastAttempt = 0;
bool writable()
{
    return wadaBackend && wadaBackend->ownsPrefs() && wadaBackend->nodeNumber() && !wadaBackend->isManaged();
}
meshtastic_Config_NetworkConfig current()
{
    return wadaBackend ? wadaBackend->networkConfig() : meshtastic_Config_NetworkConfig{};
}
auto &edit()
{
    if (!editing) {
        staged = current();
        stagedSelf = wadaBackend->nodeNumber();
        editing = true;
    }
    return staged;
}
} // namespace
void wifiConfigBegin() {}
bool wifiConfigHasRuntime()
{
    return hasCredentials.load();
}
void wifiConfigGetSsid(char *b, size_t n)
{
    snprintf(b, n, "%s", current().wifi_ssid);
}
void wifiConfigGetPwd(char *b, size_t n)
{
    snprintf(b, n, "%s", current().wifi_psk);
}
bool wifiConfigSetSsid(const char *s)
{
    if (!writable() || !s || strlen(s) >= sizeof(staged.wifi_ssid))
        return false;
    snprintf(edit().wifi_ssid, sizeof(staged.wifi_ssid), "%s", s);
    return true;
}
bool wifiConfigSetPwd(const char *s)
{
    if (!writable() || !s || strlen(s) >= sizeof(staged.wifi_psk))
        return false;
    snprintf(edit().wifi_psk, sizeof(staged.wifi_psk), "%s", s);
    return true;
}
void wifiConfigClear()
{
    if (!writable())
        return;
    edit().wifi_ssid[0] = 0;
    edit().wifi_psk[0] = 0;
    wifiConfigRequestApply();
}
void wifiConfigApply()
{
    wifiConfigRequestApply();
}
bool wifiConfigGetRadioEnabled()
{
    return enabled.load();
}
void wifiConfigSetRadioEnabled(bool e)
{
    if (!writable())
        return;
    edit().wifi_enabled = e;
    wifiConfigRequestApply();
}
bool wifiConfigGetBleEnabled()
{
    return wadaBackend && wadaBackend->bluetoothConfig().enabled;
}
void wifiConfigSetBleEnabled(bool e)
{
    if (!writable())
        return;
    bluetoothSelf = wadaBackend->nodeNumber();
    bluetoothRequested.store(e ? 1 : 0);
}
bool wifiConfigGetWifiChosen()
{
    return wifiConfigGetRadioEnabled();
}
void wifiConfigSetWifiChosen(bool e)
{
    wifiConfigSetRadioEnabled(e);
}
bool wifiConfigWantsWifi()
{
    return wifiConfigGetRadioEnabled();
}
void wifiConfigRequestApply()
{
    if (writable() && editing)
        applyRequested.store(true);
}
bool wifiConfigConsumeApplyRequest()
{
    return false;
}
void wifiScanSetActive(bool e)
{
    scanning.store(e);
}
bool wifiScanIsActive()
{
    return scanning.load();
}

// Called only by the UI task: network workers must not mutate the backend model.
void wadaWifiPoll()
{
    if (!wadaBackend)
        return;
    enabled.store(current().wifi_enabled);
    hasCredentials.store(current().wifi_ssid[0]);
    if (!writable() || (editing && stagedSelf != wadaBackend->nodeNumber())) {
        editing = false;
        applyRequested.store(false);
        bluetoothRequested.store(-1);
        attempted = false;
        return;
    }
    if (attempted && !Throttle::hasElapsed(lastAttempt, 250))
        return;
    if (applyRequested.load() || bluetoothRequested.load() >= 0) {
        attempted = true;
        lastAttempt = millis();
    }
    if (applyRequested.exchange(false) && editing) {
        meshtastic_Config c = {};
        c.which_payload_variant = meshtastic_Config_network_tag;
        c.payload_variant.network = staged;
        if (wadaBackend->setConfig(c))
            editing = false;
        else
            applyRequested.store(true);
    }
    const int want = bluetoothRequested.exchange(-1);
    if (want >= 0 && bluetoothSelf == wadaBackend->nodeNumber()) {
        meshtastic_Config c = {};
        c.which_payload_variant = meshtastic_Config_bluetooth_tag;
        c.payload_variant.bluetooth = wadaBackend->bluetoothConfig();
        c.payload_variant.bluetooth.enabled = want;
        if (!wadaBackend->setConfig(c))
            bluetoothRequested.store(want);
    }
}
