#include "UITask.h"
#include "WadaMeshtasticBackend.h"
#include "WadaStorage.h"
#include "WebInput.h"
#include "WebMesh.h"
#include "target.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <Wire.h>
#include <helpers/BaseSerialInterface.h>
void desktopRadioInit();
void desktopRadioLoop();
void wadaWifiPoll();
#include "helpers/esp32/TouchPrefsStore.h"
#include "wada_host.h"
#include <ctime>

namespace
{
class HostRTC : public mesh::RTCClock
{
  public:
    uint32_t getCurrentTime() override { return uint32_t(time(nullptr)); }
    void setCurrentTime(uint32_t) override {}
} baseClock;
class NoRadioInterface : public BaseSerialInterface
{
  public:
    void enable() override {}
    void disable() override {}
    bool isEnabled() const override { return false; }
    bool isConnected() const override { return false; }
    bool isWriteBusy() const override { return false; }
    size_t writeFrame(const uint8_t *, size_t) override { return 0; }
    size_t checkRecvFrame(uint8_t *) override { return 0; }
    bool isTcpEnabled() const override { return false; }
    bool hasBleCapability() const override { return true; }
} serialInterface;
} // namespace
WebBoard board;
WebRadio radio_driver;
ClockFloorRTC rtc_clock(baseClock);
WebSensorManager sensors;
WebDisplay display;
TwoWire Wire(0), Wire1(1);
SPIClass SPI;
WiFiClass WiFi;
UITask *wadaUi = nullptr;
MyMesh *wadaMesh = nullptr;
bool g_identity_restored_from_copy = false;
uint8_t bleLastFailReason()
{
    return 0;
}
void AutoDiscoverRTCClock::begin(TwoWire &) {}
uint32_t AutoDiscoverRTCClock::getCurrentTime()
{
    return uint32_t(time(nullptr));
}
void AutoDiscoverRTCClock::setCurrentTime(uint32_t) {}
void WebBoard::reboot()
{
    esp_restart();
}
void WebBoard::powerOff()
{
    esp_restart();
}
SPIClass *tdeckSharedSPI()
{
    return &SPI;
}
void wadaBoardBegin()
{
    Serial.println("WADAMESH / Meshtastic desktop SIMULATION: no radio or hardware connection");
    SPIFFS.begin(true);
    LittleFS.begin(true);
    rtc_clock.setCurrentTime(uint32_t(time(nullptr)));
    touchPrefsSetSetupDone(true);
    desktopRadioInit();
    wadaUi = new UITask(&board, &serialInterface);
    wadaMesh = new MyMesh(rtc_clock, wadaUi);
    wadaBackend = new WadaMeshtasticBackend;
    the_mesh.begin(wadaBackend);
    wadaUi->begin(&display, &sensors, the_mesh.getNodePrefs());
    Serial.println("[SIMULATION] modified Meshtastic UI ready");
}
void wadaBoardStep()
{
    webWheelTick();
    desktopRadioLoop();
    the_mesh.loop();
    wadaWifiPoll();
    wadaUi->loop();
}
void wadaBoardFlush()
{
    if (wadaUi)
        wadaUi->persistHistoryNow();
}
