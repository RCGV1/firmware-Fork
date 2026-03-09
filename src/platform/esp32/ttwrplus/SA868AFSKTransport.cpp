// src/platform/esp32/ttwrplus/SA868AFSKTransport.cpp
//
// SA868AFSKTransport — AFSK1200 software modem using LEDC/PWM and ADC.
// Based on LilyGo T-TWR example which uses LEDC for tone generation.

#include "SA868AFSKTransport.h"
#include "configuration.h"
#include <cmath>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef AFSK_TX_PIN
#error "AFSK_TX_PIN not defined. Ensure ttwrplus-afsk-uhf variant is active."
#endif

#ifndef AFSK_RX_PIN
#error "AFSK_RX_PIN not defined. Ensure ttwrplus-afsk-uhf variant is active."
#endif

// LEDC channel for TX tone (use channel 0)
#define AFSK_LEDC_CHANNEL LEDC_CHANNEL_0

// ---------------------------------------------------------------------------
// DDS Sine Table (512 entries)
// ---------------------------------------------------------------------------
static constexpr size_t SIN_LEN = 512;
static const uint8_t sin_table[SIN_LEN] = {
    128, 129, 131, 132, 134, 135, 137, 138, 140, 142, 143, 145, 146, 148, 149, 151, 152, 154, 155, 157, 158, 160,
    162, 163, 165, 166, 167, 169, 170, 172, 173, 175, 176, 178, 179, 181, 182, 183, 185, 186, 188, 189, 190, 192,
    193, 194, 196, 197, 198, 200, 201, 202, 203, 205, 206, 207, 208, 210, 211, 212, 213, 214, 215, 216, 217, 218,
    219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 234, 235, 236, 237, 238, 238,
    239, 240, 241, 241, 242, 243, 243, 244, 245, 245, 246, 246, 247, 248, 248, 249, 249, 250, 250, 250, 251, 251,
    252, 252, 252, 253, 253, 253, 253, 254, 254, 254, 254, 254, 255, 255, 255, 255, 255, 255};

static inline uint8_t sinSample(uint16_t i)
{
    uint16_t newI = i % (SIN_LEN / 2);
    newI = (newI >= (SIN_LEN / 4)) ? (SIN_LEN / 2 - newI - 1) : newI;
    uint8_t sine = pgm_read_byte(&sin_table[newI]);
    return (i >= (SIN_LEN / 2)) ? (255 - sine) : sine;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
SA868AFSKTransport::SA868AFSKTransport(AX25UIFramer *framer, SA868Driver *driver)
    : StreamTransport(framer), _driver(driver), _ax25Framer(framer)
{
}

SA868AFSKTransport::~SA868AFSKTransport()
{
    deinitLEDC();
    deinitADC();
    if (_rxTaskHandle) {
        vTaskDelete(_rxTaskHandle);
    }
}

// ---------------------------------------------------------------------------
// init() — Setup LEDC, ADC and RX task
// ---------------------------------------------------------------------------
bool SA868AFSKTransport::init()
{
    if (!_driver->isReady()) {
        LOG_ERROR("SA868Driver not ready");
        return false;
    }

    // Make sure LEDC is disabled at start (no noise)
    ledcWrite(AFSK_LEDC_CHANNEL, 0);

    initLEDC();
    initADC();

    // Start FreeRTOS task for continuous AFSK demodulation
    xTaskCreateUniversal(&rxTask, "AFSK_RX", 4096, this, 3, &_rxTaskHandle, APP_CPU_NUM);

    LOG_INFO("AFSK1200 transport initialized (TX=GPIO%d, RX=GPIO%d)", AFSK_TX_PIN, AFSK_RX_PIN);
    return true;
}

// ---------------------------------------------------------------------------
// LEDC TX Setup - Using LEDC for tone generation like LilyGo example
// ---------------------------------------------------------------------------
bool SA868AFSKTransport::initLEDC()
{
    // Setup LEDC for tone generation on GPIO 18
    // Using 8-bit resolution, ~38kHz base frequency
    ledc_timer_config_t timer_conf = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                      .duty_resolution = LEDC_TIMER_8_BIT,
                                      .timer_num = LEDC_TIMER_0,
                                      .freq_hz = 38000, // Base frequency
                                      .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t channel_conf = {.gpio_num = AFSK_TX_PIN,
                                          .speed_mode = LEDC_LOW_SPEED_MODE,
                                          .channel = AFSK_LEDC_CHANNEL,
                                          .intr_type = LEDC_INTR_DISABLE,
                                          .timer_sel = LEDC_TIMER_0,
                                          .duty = 0, // Start at 0 duty
                                          .hpoint = 0};
    ledc_channel_config(&channel_conf);

    _ledcInitialized = true;
    LOG_INFO("LEDC initialized on GPIO %d", AFSK_TX_PIN);
    return true;
}

void SA868AFSKTransport::deinitLEDC()
{
    if (_ledcInitialized) {
        ledcWrite(AFSK_LEDC_CHANNEL, 0);
        ledc_stop(LEDC_LOW_SPEED_MODE, AFSK_LEDC_CHANNEL, 0);
        _ledcInitialized = false;
    }
}

// Set the tone frequency for AFSK
void SA868AFSKTransport::setTXFrequency(float freqHz)
{
    if (_ledcInitialized) {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, (uint32_t)freqHz);
    }
}

// ---------------------------------------------------------------------------
// ADC RX Setup
// ---------------------------------------------------------------------------
void SA868AFSKTransport::initADC()
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_12);
    LOG_INFO("ADC initialized on GPIO 1 (channel 0)");
}

void SA868AFSKTransport::deinitADC()
{
    // ADC doesn't need explicit deinit
}

uint16_t SA868AFSKTransport::readADC()
{
    return adc1_get_raw(ADC1_CHANNEL_0);
}

// ---------------------------------------------------------------------------
// isClear() — Listen-before-talk
// ---------------------------------------------------------------------------
bool SA868AFSKTransport::isClear()
{
    return _driver->isChannelClear();
}

// ---------------------------------------------------------------------------
// loop() — Called periodically
// ---------------------------------------------------------------------------
void SA868AFSKTransport::loop()
{
    // Nothing needed - RX runs in dedicated task
}

// ---------------------------------------------------------------------------
// writeBytes() — Transmit a packet using LEDC-based AFSK
// ---------------------------------------------------------------------------
bool SA868AFSKTransport::writeBytes(const uint8_t *data, size_t len)
{
    // Wait for channel to be clear (CSMA)
    uint32_t startWait = millis();
    while (!isClear()) {
        if (millis() - startWait > 500) {
            LOG_WARN("AFSK: channel busy, aborting TX");
            return false;
        }
        delay(10);
    }

    // Key up the transmitter
    _driver->setTransmit(true);

    // Make sure LEDC is outputting silence before preamble
    ledcWrite(AFSK_LEDC_CHANNEL, 0);

    delay(50); // Allow TX to stabilize

    // Generate preamble: 300ms of continuous mark tone (1200 Hz)
    LOG_DEBUG("AFSK: transmitting preamble");
    setTXFrequency(1200.0f);

    for (uint32_t i = 0; i < 50; i++) {
        // 1200 Hz mark tone - full duty cycle (50%)
        ledcWrite(AFSK_LEDC_CHANNEL, 128);
        delayMicroseconds(833); // ~1200 Hz period
    }

    // Transmit frame bits with NRZI encoding
    LOG_DEBUG("AFSK: transmitting %d bytes", len);
    uint8_t currentTone = 1; // 1 = mark (1200 Hz), 0 = space (2200 Hz)

    for (size_t i = 0; i < len; i++) {
        for (int bit = 0; bit < 8; bit++) {
            uint8_t txBit = (data[i] >> bit) & 0x01;

            // NRZI: 0 = toggle tone, 1 = keep same tone
            if (txBit == 0) {
                currentTone = !currentTone;
            }

            // Output tone for one bit period (1200 baud = 833us)
            float freq = currentTone ? 1200.0f : 2200.0f;
            setTXFrequency(freq);

            // Full sine wave approximation with PWM
            int samplesPerBit = currentTone ? 8 : 4;
            for (int w = 0; w < samplesPerBit; w++) {
                ledcWrite(AFSK_LEDC_CHANNEL, 128); // 50% duty
                delayMicroseconds(100);
            }
        }
    }

    // Transmit trailer: 50ms of mark tone
    setTXFrequency(1200.0f);
    for (uint32_t i = 0; i < 6; i++) {
        ledcWrite(AFSK_LEDC_CHANNEL, 128);
        delayMicroseconds(833);
    }

    // Silence and release PTT
    ledcWrite(AFSK_LEDC_CHANNEL, 0);
    delay(50);
    _driver->setTransmit(false);

    LOG_DEBUG("AFSK: TX complete");
    return true;
}

// ---------------------------------------------------------------------------
// RX Task — Continuous demodulation (placeholder for now)
// ---------------------------------------------------------------------------
void SA868AFSKTransport::rxTask(void *param)
{
    static_cast<SA868AFSKTransport *>(param)->processRX();
}

void SA868AFSKTransport::processRX()
{
    LOG_INFO("AFSK RX task started");

    // Simplified RX - just read ADC samples for now
    // Full Goertzel demodulation would go here

    while (true) {
        uint16_t sample = readADC();
        // Process sample...
        delay(1);
    }
}
