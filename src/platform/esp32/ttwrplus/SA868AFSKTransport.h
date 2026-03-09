// src/platform/esp32/ttwrplus/SA868AFSKTransport.h
//
// SA868AFSKTransport — AFSK1200 software modem using ESP32 LEDC/PWM and ADC.
//
// Extends StreamTransport. Uses AX25UIFramer to frame/deframe the AFSK bit
// stream into discrete packets. Owns the SA868Driver for PTT control.
//
// Physical Layer:
//   - Modulation: AFSK1200 (1200 baud, mark=1200 Hz, space=2200 Hz).
//   - TX: LEDC/PWM output on GPIO 18, DDS sine generation.
//   - RX: ESP32 ADC on GPIO 1, Goertzel algorithm demodulation.
//   - CSMA: Listen-before-talk using SA868 SQUELCH/BUSY GPIO indicator.

#pragma once
#include "SA868Driver.h"
#include "transport/AX25UIFramer.h"
#include "transport/StreamTransport.h"
#include <driver/adc.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class SA868AFSKTransport : public StreamTransport
{
  public:
    SA868AFSKTransport(AX25UIFramer *framer, SA868Driver *driver);
    ~SA868AFSKTransport();

    // ---- PacketTransport interface ----
    bool init() override;
    void loop() override;
    bool isClear() override;
    const char *transportName() const override { return "AFSK1200/AX.25"; }

  protected:
    // ---- StreamTransport interface ----
    bool writeBytes(const uint8_t *data, size_t len) override;

  private:
    SA868Driver *_driver = nullptr;
    AX25UIFramer *_ax25Framer = nullptr;

    TaskHandle_t _rxTaskHandle = nullptr;
    bool _ledcInitialized = false;

    // ---- TX helpers ----
    bool initLEDC();
    void deinitLEDC();
    void setTXFrequency(float freqHz);

    // ---- RX helpers ----
    void initADC();
    void deinitADC();
    uint16_t readADC();

    // ---- DDS for tone generation ----
    float _txPhase = 0.0f;
    static constexpr float TONE_MARK_HZ = 1200.0f;
    static constexpr float TONE_SPACE_HZ = 2200.0f;
    // LEDC sample rate at 8-bit resolution
    static constexpr float SAMPLE_RATE = 38000.0f;
    static constexpr float PHASE_INC_MARK = TONE_MARK_HZ / SAMPLE_RATE;
    static constexpr float PHASE_INC_SPACE = TONE_SPACE_HZ / SAMPLE_RATE;

    // ---- Demodulation state ----
    int16_t _dcOffset = 2048;
    uint8_t _lastBit = 1;

    // ---- Task ----
    static void rxTask(void *param);
    void processRX();
};
