// src/mesh/transport/AX25UIFramer.h
//
// AX25UIFramer — AX.25 Unnumbered Information (UI) frame encoder/decoder.
//
// Implements the Framer interface for AFSK1200/AX.25 packet radio.
// Compatible with Direwolf, standard TNCs, and APRS infrastructure.
//
// Frame structure (no digipeater path — Meshtastic handles its own routing):
//   [DEST:7] [SRC:7] [CTRL:1=0x03] [PID:1=0xF0] [INFO:4-256] [FCS:2]
//   Surrounded by HDLC flag bytes (0x7E) for framing.
//
// encode() output: complete bit-packed byte array with:
//   - 12-byte preamble (flag 0x7E repeated)
//   - AX.25 frame with HDLC bit stuffing applied
//   - 2-byte tail (flag 0x7E repeated)
//   Ready to feed to the NRZI encoder in SA868AFSKTransport.
//
// feed() input: raw decoded bits (post-NRZI), one bit at a time.
//   Handles flag detection, HDLC de-stuffing, CRC validation.
//   Calls onFrame with just the INFO-field bytes (the Meshtastic payload).
//
// AX.25 address encoding: each ASCII character << 1 (multiply by 2).
// Callsign padded to exactly 6 characters with spaces on the right.
// SSID byte: bits 7-1 = 0b0SSSS110, bit 0 = end-of-address flag.
//
// CRC: CRC-CCITT (poly 0x1021, init 0xFFFF, final XOR 0xFFFF,
//      bit-reversed input and output), computed over DEST+SRC+CTRL+PID+INFO.
//      Appended little-endian (low byte first).

#pragma once
#include "Framer.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

// Hard limit per AX.25 spec INFO field (and universal TNC/Direwolf compat)
static constexpr size_t AX25_MAX_INFO_BYTES = 256;

// Meshtastic magic header overhead inside INFO field
static constexpr size_t MESH_PAYLOAD_OVERHEAD = 4;

// Maximum nanopb MeshPacket bytes that fit in one AX.25 UI frame INFO field
static constexpr size_t AX25_MAX_MESH_PAYLOAD = AX25_MAX_INFO_BYTES - MESH_PAYLOAD_OVERHEAD;

// Well-known AX.25 destination callsign for all Meshtastic AFSK nodes
static constexpr char AX25_DEST_CALLSIGN[] = "MESHTA";

class AX25UIFramer : public Framer
{
  public:
    /// sourceCallsign: the operator's amateur callsign (e.g. "KD9XYZ").
    ///                 Padded to 6 characters. Max 6 characters.
    /// sourceSsid:     0–15, disambiguates multiple nodes per operator.
    AX25UIFramer(const char *sourceCallsign, uint8_t sourceSsid = 0);

    // ---- Framer interface ----
    size_t encode(const uint8_t *in, size_t inLen, uint8_t *out, size_t maxOut) override;
    void feed(const uint8_t *bytes, size_t len) override;
    void reset() override;

    /// Update callsign at runtime (e.g. after ham mode is configured).
    void setCallsign(const char *callsign, uint8_t ssid = 0);

  private:
    // ---- Address fields ----
    char _srcCall[7]; // 6 chars + null terminator
    uint8_t _srcSsid;

    // ---- Encoding helpers ----
    static void encodeAddress(const char *call, uint8_t ssid, bool lastAddr, uint8_t *out);
    static uint16_t crcCCITT(const uint8_t *data, size_t len);
    static size_t bitStuff(const uint8_t *in, size_t inBits, uint8_t *out, size_t maxOutBytes);

    // ---- Decoding state machine ----
    enum class RxState {
        IDLE,     // Waiting for flag byte
        IN_FRAME, // Accumulating frame bits
    };

    RxState _rxState = RxState::IDLE;
    uint8_t _rxBitBuf = 0; // shift register for incoming bits
    uint8_t _rxBitCount = 0;
    uint8_t _rawBuf[400]; // accumulated raw frame bytes (pre-destuffing)
    size_t _rawLen = 0;
    uint8_t _onesRun = 0; // consecutive 1-bits for bit-destuffing
    bool _lastBit = false;

    // Working buffer large enough for one decoded byte after destuffing
    uint8_t _frameBuf[300];
    size_t _frameBitLen = 0;

    void rxBit(uint8_t bit);
    void processFrame();
    static size_t bitUnstuff(const uint8_t *in, size_t inBits, uint8_t *out, size_t maxOutBytes);
};
