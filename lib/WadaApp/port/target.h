#pragma once
#include "WadaPlatform.h"
#include "helpers/ClockFloorRTC.h"
#include "helpers/ui/ST7789LCDDisplay.h"
#include <Mesh.h>
#include <SPI.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/SensorManager.h>
#define PIN_TFT_DC 11
#define PIN_TFT_CS 12
class WadaBoard : public mesh::MainBoard
{
  public:
    uint16_t getBattMilliVolts() override { return wadaBatteryMillivolts(); }
    const char *getManufacturerName() const override { return "LilyGo T-Deck / Meshtastic"; }
    uint8_t getStartupReason() const override { return BD_STARTUP_NORMAL; }
    void reboot() override;
};
class WadaRadioStatistics
{
  public:
    const char *chipName() const { return "SX1262 / Meshtastic"; }
    uint32_t getPacketsRecv() const;
    uint32_t getPacketsSent() const;
    float getCurrentRSSI() const { return NAN; }
    int getNoiseFloor() const { return -128; }
    uint32_t getRxEvents() const { return getPacketsRecv(); }
    uint32_t getPacketsRecvErrors() const { return 0; }
    uint32_t getRxQueueDrops() const { return 0; }
    bool rxQueueEnabled() const { return true; }
    void rxQueueEnable(bool) {}
    bool isInRecvMode() const { return true; }
    bool isSendComplete() const { return true; }
    uint32_t getEstAirtimeFor(int) const { return 0; }
    void radioAcquire() {}
    void radioRelease() {}
    void rxQueueSuspend(bool) {}
    bool setParams(float, float, int, int) { return false; }
    bool setTxPower(int) { return false; }
    bool setRxBoostedGainMode(bool) { return false; }
    int recvRaw(uint8_t *, size_t) { return 0; }
};
extern WadaBoard board;
extern SensorManager sensors;
extern ClockFloorRTC rtc_clock;
extern ST7789LCDDisplay display;
extern WadaRadioStatistics radio_driver;
SPIClass *tdeckSharedSPI();
