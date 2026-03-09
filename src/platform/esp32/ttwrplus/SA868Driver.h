// src/platform/esp32/ttwrplus/SA868Driver.h
//
// SA868Driver — AT-command driver for the SA868 UHF FM radio module.
//
// The SA868 is a self-contained transceiver integrated on the LilyGo T-TWR Plus
// REV 2.1. It communicates via UART AT commands (for configuration) and has
// separate GPIO lines for PTT and squelch indication.
//
// Usage:
//   SA868Driver radio;
//   radio.init();
//   radio.setFrequency(446.500f);
//   radio.assertPTT();   // start transmitting
//   // ... output audio ...
//   radio.deassertPTT(); // stop transmitting
//
// Ham-mode enforcement:
//   assertPTT() checks devicestate.owner.is_licensed before keying the
//   transmitter. If is_licensed is false, it logs an error and returns false.
//   This satisfies FCC Part 97.113 requirements for this amateur-band transport.

#pragma once
#include "configuration.h"
#include <Arduino.h>
#include <HardwareSerial.h>

class SA868Driver
{
  public:
    SA868Driver() = default;

    /// Initialise UART, power up SA868, verify AT comms (DMOCONNECT).
    /// Returns true on success.
    bool init();

    /// Configure operating frequency (MHz), CTCSS tone (0 = off), squelch (0-8).
    /// Both TX and RX are set to the same simplex frequency.
    /// Returns true on success.
    bool setFrequency(float freqMHz, uint16_t ctcss = 0, uint8_t squelch = 0);

    /// Set receive audio volume (1–8). Start with 5 and calibrate.
    bool setVolume(uint8_t vol = 5);

    /// Set transmit power (0 = low, 1 = high). Use low power for bench testing.
    bool setTxPower(uint8_t power = 0);

    /// Switch SA868 to transmit mode (via AT command)
    bool setModeTX();

    /// Switch SA868 to receive mode (via AT command)
    bool setModeRX();

    /// Assert PTT (begin transmitting). Active-low GPIO.
    /// Performs ham-mode check (is_licensed) before keying. Returns false if
    /// the operator is not a licensed amateur or SA868 is not initialised.
    bool assertPTT();

    /// Deassert PTT (return to receive). Always safe to call.
    void deassertPTT();

    /// Returns true if PTT is currently asserted.
    bool isPTTActive() const { return _pttActive; }

    /// Returns true if the SA868 squelch/carrier-detect pin is HIGH
    /// (i.e. a signal is being received above the squelch threshold).
    bool isBusy() const;

    /// Alias for isBusy() - returns true if channel is clear (no signal).
    bool isChannelClear() const { return !isBusy(); }

    /// Alias for assertPTT() - start transmitting.
    void setTransmit(bool tx)
    {
        if (tx)
            assertPTT();
        else
            deassertPTT();
    }

    /// Returns true if init() succeeded.
    bool isReady() const { return _ready; }

  private:
    bool _ready = false;
    bool _pttActive = false;
    HardwareSerial *_serial = nullptr;

    /// Send an AT command and wait up to timeoutMs for a response containing
    /// the expected reply string. Returns true if response was received.
    bool sendATCommand(const char *cmd, const char *expectedReply, uint32_t timeoutMs = 1000);

    /// Flush all pending input from the SA868 UART.
    void flushRx();
};
