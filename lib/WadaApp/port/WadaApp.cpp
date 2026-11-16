#include "WadaMeshtasticBackend.h"
#include "WadaStorage.h"
#include "WebMesh.h"
#include "helpers/esp32/WifiRuntimeStore.h"
#include "target.h"
#include "ui-touch/UITask.h"
#include "wadacomms/PacketServer.h"
#include <SD.h>
#include <SPIFFS.h> // Keep the framework dependency visible to PlatformIO LDF.
#include <WadaApp.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>

extern void wadaWifiPoll();

namespace
{
std::atomic<bool> firmwareReady{false};
}
WadaStorageFS wadaStorage;
static size_t wadaReadSnapshot(const char *path, uint8_t *out, size_t cap)
{
    File file = SPIFFS.open(path, "r");
    if (!file)
        return 0;
    const size_t size = file.size();
    if (size > cap || file.read(out, size) != size)
        return 0;
    return size;
}
static bool wadaWriteSnapshot(const char *path, const uint8_t *bytes, size_t size)
{
    File file = SPIFFS.open(path, "w");
    if (!file)
        return false;
    const bool written = file.write(bytes, size) == size;
    file.flush();
    file.close();
    if (!written)
        return false;
    file = SPIFFS.open(path, "r");
    if (!file || file.size() != size)
        return false;
    uint8_t check[64];
    for (size_t at = 0; at < size;) {
        const size_t n = std::min(sizeof check, size - at);
        if (file.read(check, n) != n || memcmp(check, bytes + at, n))
            return false;
        at += n;
    }
    return true;
}
size_t wadaTelemetryRead(unsigned slot, uint8_t *out, size_t cap)
{
    return slot < 2 ? wadaReadSnapshot(slot ? "/native_telem_b" : "/native_telem_a", out, cap) : 0;
}
bool wadaTelemetryWrite(unsigned slot, const uint8_t *bytes, size_t size)
{
    return slot < 2 && wadaWriteSnapshot(slot ? "/native_telem_b" : "/native_telem_a", bytes, size);
}
size_t wadaNodeStateRead(unsigned slot, uint8_t *out, size_t cap)
{
    return slot < 2 ? wadaReadSnapshot(slot ? "/native_nodes_b" : "/native_nodes_a", out, cap) : 0;
}
bool wadaNodeStateWrite(unsigned slot, const uint8_t *bytes, size_t size)
{
    return slot < 2 && wadaWriteSnapshot(slot ? "/native_nodes_b" : "/native_nodes_a", bytes, size);
}
WadaBoard board;
SensorManager sensors;
ST7789LCDDisplay display;
WadaRadioStatistics radio_driver;
class FirmwareClock : public mesh::RTCClock
{
  public:
    uint32_t getCurrentTime() override { return wadaRtcNow(); }
    void setCurrentTime(uint32_t t) override { wadaSetRtc(t); }
};
FirmwareClock baseClock;
ClockFloorRTC rtc_clock(baseClock);
uint32_t AutoDiscoverRTCClock::getCurrentTime()
{
    return wadaRtcNow();
}
void AutoDiscoverRTCClock::setCurrentTime(uint32_t t)
{
    wadaSetRtc(t);
}
void WadaBoard::reboot()
{
    if (wadaBackend)
        wadaBackend->power(false);
}
uint32_t WadaRadioStatistics::getPacketsRecv() const
{
    return wadaBackend ? wadaBackend->packetsRecv() : 0;
}
uint32_t WadaRadioStatistics::getPacketsSent() const
{
    return wadaBackend ? wadaBackend->packetsSent() : 0;
}
class FirmwareInterfaces : public BaseSerialInterface
{
  public:
    void enable() override {}
    void disable() override {}
    bool isEnabled() const override { return true; }
    bool isConnected() const override { return wadaPhoneConnected(); }
    bool hasBleCapability() const override { return true; }
    bool isBleEnabled() const override { return wifiConfigGetBleEnabled(); }
    void enableBle() override { wifiConfigSetBleEnabled(true); }
    void disableBle() override { wifiConfigSetBleEnabled(false); }
    bool isWriteBusy() const override { return false; }
    size_t writeFrame(const uint8_t *, size_t) override { return 0; }
    size_t checkRecvFrame(uint8_t *) override { return 0; }
};
FirmwareInterfaces serialInterfaces;
UITask *wadaUi = nullptr;
MyMesh *wadaMesh = nullptr;
void wadaSignalFirmwareReady()
{
    firmwareReady.store(true);
}
static void runUi(void *)
{
    while (!firmwareReady.load())
        vTaskDelay(pdMS_TO_TICKS(20));
    if (!SPIFFS.begin(true, "/wada", 10, "wada")) {
        Serial.println("WADAMESH storage unavailable: format the dedicated wada partition before use");
    }
    display.begin();
    void *uiMemory = heap_caps_malloc(sizeof(UITask), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(uiMemory);
    wadaUi = new (uiMemory) UITask(&board, &serialInterfaces);
    void *meshMemory = heap_caps_malloc(sizeof(MyMesh), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(meshMemory);
    wadaMesh = new (meshMemory) MyMesh(rtc_clock, wadaUi);
    void *backendMemory = heap_caps_malloc(sizeof(WadaMeshtasticBackend), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(backendMemory);
    wadaBackend = new (backendMemory) WadaMeshtasticBackend;
    the_mesh.begin(wadaBackend);
    sdMountDiagSetMounted(SD.cardType() != CARD_NONE, 4000000);
    wadaUi->begin(&display, &sensors, the_mesh.getNodePrefs());
    for (;;) {
        the_mesh.loop();
        wadaWifiPoll();
        wadaUi->loop();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
void wadaStart()
{
    wadaInitPacketApi();
    BaseType_t ok = xTaskCreatePinnedToCore(runUi, "WADAMESH", 32768, nullptr, 1, nullptr, 0);
    assert(ok == pdPASS);
}
