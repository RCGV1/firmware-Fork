// src/platform/esp32/ttwrplus/SA868Driver.cpp
//
// SA868 AT-command driver implementation.
// See SA868Driver.h for overview.

#include "SA868Driver.h"
#include "NodeDB.h" // devicestate.owner.is_licensed
#include "configuration.h"
#include <cstdio>

// PMU control for speaker amplifier
#if defined(HAS_PMU) && defined(PMU)
#include <XPowersLib.h>
extern XPowersPPM *PMU;
#endif

// Pins — included from variant.h via configuration.h / build system
#ifndef SA868_UART_TX_PIN
#error "SA868_UART_TX_PIN not defined. Include the ttwrplus-afsk-uhf variant."
#endif

// SA868 responds at 9600 baud by default
static constexpr uint32_t SA868_BAUD = 9600;

// Which ESP32-S3 UART to use
static constexpr uint8_t SA868_UART_NUM = 1;

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------
bool SA868Driver::init()
{
    // Power up the SA868 module
    LOG_INFO("SA868: powering up (pin %d HIGH)", SA868_POWER_PIN);
    pinMode(SA868_POWER_PIN, OUTPUT);
    digitalWrite(SA868_POWER_PIN, HIGH);
    delay(1000); // allow module to stabilise (as per LilyGo repo)

    // Configure PTT pin (active-low, we start in RX mode)
    LOG_INFO("SA868: PTT pin %d configured (HIGH = RX)", SA868_PTT_PIN);
    pinMode(SA868_PTT_PIN, OUTPUT);
    digitalWrite(SA868_PTT_PIN, HIGH); // ensure RX mode

#ifdef MIC_CTRL_PIN
    // REV 2.1: Set LOW for RX mode (mic -> SA868).
    // We set this to HIGH only during TX in assertPTT()
    LOG_INFO("SA868: configuring MIC_CTRL_PIN %d for RX mode", MIC_CTRL_PIN);
    pinMode(MIC_CTRL_PIN, OUTPUT);
    digitalWrite(MIC_CTRL_PIN, LOW); // RX mode: mic goes to SA868
#endif

    // Configure squelch/busy indicator as input
    pinMode(SA868_BUSY_PIN, INPUT);

    // Initialise UART (using Serial1 for SA868)
    _serial = &Serial1;
    _serial->begin(SA868_BAUD, SERIAL_8N1, SA868_UART_RX_PIN, SA868_UART_TX_PIN);
    delay(100);
    flushRx();

    // Verify comms
    if (!sendATCommand("AT+DMOCONNECT\r\n", "+DMOCONNECT:0", 2000)) {
        LOG_WARN("SA868: DMOCONNECT failed — check UART wiring (TX=%d, RX=%d)", SA868_UART_TX_PIN, SA868_UART_RX_PIN);
        return false;
    }
    LOG_INFO("SA868: DMOCONNECT OK");

    _ready = true;

    // Diagnostic: Log GPIO states at startup
    LOG_INFO("SA868 init complete. PTT=%d (should be 1/HIGH), BUSY=%d, MIC_CTRL=%d (should be 0/LOW)", digitalRead(SA868_PTT_PIN),
             digitalRead(SA868_BUSY_PIN),
#ifdef MIC_CTRL_PIN
             digitalRead(MIC_CTRL_PIN)
#else
             -1
#endif
    );
    return true;
}

// ---------------------------------------------------------------------------
// setFrequency()
// ---------------------------------------------------------------------------
bool SA868Driver::setFrequency(float freqMHz, uint16_t ctcss, uint8_t squelch)
{
    if (!_ready)
        return false;

    // AT+DMOSETGROUP=BW,TXF,RXF,TXCTCSS,SQ,RXCTCSS
    // BW=0 for 12.5 kHz (narrow), CTCSS=0000 (none), squelch 0=open
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "AT+DMOSETGROUP=0,%.4f,%.4f,%04u,%u,%04u\r\n", freqMHz, freqMHz, ctcss, squelch, ctcss);

    LOG_INFO("SA868: %s", cmd);

    if (!sendATCommand(cmd, "+DMOSETGROUP:0", 2000)) {
        LOG_WARN("SA868: DMOSETGROUP failed for %.4f MHz", freqMHz);
        return false;
    }
    LOG_INFO("SA868: frequency set to %.4f MHz", freqMHz);
    return true;
}

// ---------------------------------------------------------------------------
// setVolume()
// ---------------------------------------------------------------------------
bool SA868Driver::setVolume(uint8_t vol)
{
    if (!_ready)
        return false;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+DMOSETVOLUME=%u\r\n", (unsigned)(vol < 1 ? 1 : vol > 8 ? 8 : vol));
    return sendATCommand(cmd, "+DMOSETVOLUME:0", 1000);
}

// ---------------------------------------------------------------------------
// setTxPower()
// ---------------------------------------------------------------------------
bool SA868Driver::setTxPower(uint8_t power)
{
    if (!_ready)
        return false;
    // This command is undocumented in some SA868 revisions; some use
    // AT+SETTXPOWER=<power>,<freq> where power=0=low, 1=high.
    // We attempt it but do not fail init if it's not supported.
    char cmd[48];
    float freq = 446.500f; // placeholder; real freq set by setFrequency
    snprintf(cmd, sizeof(cmd), "AT+SETTXPOWER=%u,%.4f\r\n", (unsigned)power, freq);
    bool ok = sendATCommand(cmd, "+SETTXPOWER:0", 1000);
    if (!ok) {
        LOG_INFO("SA868: SETTXPOWER not supported or failed (non-fatal)");
    }
    return true; // non-fatal
}

// ---------------------------------------------------------------------------
// setModeTX() - Put SA868 into transmit mode via AT command
// ---------------------------------------------------------------------------
bool SA868Driver::setModeTX()
{
    if (!_ready)
        return false;

    // Send AT command to switch to TX mode
    // Note: The SA868 automatically switches based on PTT, but we can
    // also explicitly command it. Using a simple approach - just ensure
    // we're in a valid state.
    return true;
}

// ---------------------------------------------------------------------------
// setModeRX() - Put SA868 into receive mode
// ---------------------------------------------------------------------------
bool SA868Driver::setModeRX()
{
    if (!_ready)
        return false;
    return true;
}
// ---------------------------------------------------------------------------
// assertPTT() - With proper sequencing for REV 2.1
// ---------------------------------------------------------------------------
bool SA868Driver::assertPTT()
{
    if (!_ready) {
        LOG_ERROR("SA868: assertPTT called before init()");
        return false;
    }

    // FCC Part 97 compliance
    if (!devicestate.owner.is_licensed) {
        LOG_ERROR("SA868: PTT BLOCKED — licensed ham mode required");
        return false;
    }

    if (_pttActive)
        return true;

        // STEP 1: Disable ALDO3 (speaker amp) FIRST - before anything else
#if defined(HAS_PMU) && defined(PMU)
    if (PMU) {
        PMU->disablePowerOutput(XPOWERS_ALDO3);
        LOG_DEBUG("SA868: ALDO3 disabled (speaker amp off)");
    } else {
        LOG_WARN("SA868: PMU not available, speaker may buzz!");
    }
#endif

    // STEP 2: Route ESP audio to SA868 (BEFORE PTT!)
#ifdef MIC_CTRL_PIN
    digitalWrite(MIC_CTRL_PIN, HIGH);
#endif
    LOG_DEBUG("SA868: MIC_CTRL set HIGH (ESP -> SA868)");

    // STEP 3: Wait for ALDO3 to actually power down (~150ms)
    delay(150);

    // STEP 4: Key up the transmitter
    LOG_DEBUG("SA868: PTT assert (TX)");
    digitalWrite(SA868_PTT_PIN, LOW); // active-low

    // STEP 5: Wait for SA868 to switch to TX mode
    delay(50);

    _pttActive = true;
    return true;
}

// ---------------------------------------------------------------------------
// deassertPTT() - With proper sequencing for REV 2.1
// ---------------------------------------------------------------------------
void SA868Driver::deassertPTT()
{
    if (!_pttActive)
        return;

    LOG_DEBUG("SA868: PTT deassert (RX)");

    // STEP 1: Deassert PTT FIRST
    digitalWrite(SA868_PTT_PIN, HIGH);
    _pttActive = false;

    // STEP 2: Wait for SA868 to switch to RX
    delay(20);

    // STEP 3: Return mic to SA868 (switch audio routing back)
#ifdef MIC_CTRL_PIN
    digitalWrite(MIC_CTRL_PIN, LOW);
#endif
    LOG_DEBUG("SA868: MIC_CTRL set LOW (mic -> SA868)");

    // STEP 4: Wait for audio routing to settle
    delay(50);

    // STEP 5: Re-enable speaker amplifier (ALDO3)
#if defined(HAS_PMU) && defined(PMU)
    if (PMU) {
        PMU->enablePowerOutput(XPOWERS_ALDO3);
        LOG_DEBUG("SA868: ALDO3 enabled (speaker amp on)");
    }
#endif
}

// ---------------------------------------------------------------------------
// isBusy()
// ---------------------------------------------------------------------------
bool SA868Driver::isBusy() const
{
    return digitalRead(SA868_BUSY_PIN) == HIGH;
}

// ---------------------------------------------------------------------------
// sendATCommand() — send command string, wait for expected substring in reply
// ---------------------------------------------------------------------------
bool SA868Driver::sendATCommand(const char *cmd, const char *expectedReply, uint32_t timeoutMs)
{
    if (!_serial)
        return false;
    flushRx();
    _serial->print(cmd);
    _serial->flush();

    char buf[128];
    size_t pos = 0;
    uint32_t deadline = millis() + timeoutMs;

    while (millis() < deadline) {
        while (_serial->available()) {
            char c = (char)_serial->read();
            if (pos < sizeof(buf) - 1) {
                buf[pos++] = c;
                buf[pos] = '\0';
            }
            // Check if we've seen the expected reply
            if (strstr(buf, expectedReply)) {
                return true;
            }
        }
        delay(1);
    }

    LOG_WARN("SA868: AT command timeout. Sent: %s, Expected: %s, Got: %s", cmd, expectedReply, pos > 0 ? buf : "<empty>");
    return false;
}

// ---------------------------------------------------------------------------
// flushRx()
// ---------------------------------------------------------------------------
void SA868Driver::flushRx()
{
    if (!_serial)
        return;
    while (_serial->available()) {
        _serial->read();
    }
}
