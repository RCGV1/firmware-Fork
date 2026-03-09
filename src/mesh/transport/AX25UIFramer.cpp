// src/mesh/transport/AX25UIFramer.cpp
//
// AX.25 Unnumbered Information framer implementation.
// See AX25UIFramer.h for architecture notes.

#include "AX25UIFramer.h"
#include "configuration.h" // for LOG_*
#include <cassert>
#include <cstring>

// ---------------------------------------------------------------------------
// HDLC flag byte
// ---------------------------------------------------------------------------
static constexpr uint8_t HDLC_FLAG = 0x7E;
static constexpr uint8_t HDLC_CONTROL_UI = 0x03;
static constexpr uint8_t AX25_PID_NO_LAYER3 = 0xF0;

// Preamble: 12 flag bytes before frame; tail: 2 flag bytes after
static constexpr size_t PREAMBLE_FLAGS = 12;
static constexpr size_t TAIL_FLAGS = 2;

// ---------------------------------------------------------------------------
// CRC-CCITT table-driven computation
// Poly=0x1021, init=0xFFFF, finalXOR=0xFFFF, bit-reversed I/O (CRC-16/AX25)
// ---------------------------------------------------------------------------

static const uint16_t CRC_TABLE[256] = {
    0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF, 0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E,
    0xF8F7, 0x1081, 0x0108, 0x3393, 0x221A, 0x56A5, 0x472C, 0x75B7, 0x643E, 0x9CC9, 0x8D40, 0xBFDB, 0xAE52, 0xDAED, 0xCB64,
    0xF9FF, 0xE876, 0x2102, 0x308B, 0x0210, 0x1399, 0x6726, 0x76AF, 0x4434, 0x55BD, 0xAD4A, 0xBCC3, 0x8E58, 0x9FD1, 0xEB6E,
    0xFAE7, 0xC87C, 0xD9F5, 0x3183, 0x200A, 0x1291, 0x0318, 0x77A7, 0x662E, 0x54B5, 0x453C, 0xBDCB, 0xAC42, 0x9ED9, 0x8F50,
    0xFBEF, 0xEA66, 0xD8FD, 0xC974, 0x4204, 0x538D, 0x6116, 0x709F, 0x0420, 0x15A9, 0x2732, 0x36BB, 0xCE4C, 0xDFC5, 0xED5E,
    0xFCD7, 0x8868, 0x99E1, 0xAB7A, 0xBAF3, 0x5285, 0x430C, 0x7197, 0x601E, 0x14A1, 0x0528, 0x37B3, 0x263A, 0xDECD, 0xCF44,
    0xFDDF, 0xEC56, 0x98E9, 0x8960, 0xBBFB, 0xAA72, 0x6306, 0x728F, 0x4014, 0x519D, 0x2522, 0x34AB, 0x0630, 0x17B9, 0xEF4E,
    0xFEC7, 0xCC5C, 0xDDD5, 0xA96A, 0xB8E3, 0x8A78, 0x9BF1, 0x7387, 0x620E, 0x5095, 0x411C, 0x35A3, 0x242A, 0x16B1, 0x0738,
    0xFFCF, 0xEE46, 0xDCDD, 0xCD54, 0xB9EB, 0xA862, 0x9AF9, 0x8B70, 0x8408, 0x9581, 0xA71A, 0xB693, 0xC22C, 0xD3A5, 0xE13E,
    0xF0B7, 0x0840, 0x19C9, 0x2B52, 0x3ADB, 0x4E64, 0x5FED, 0x6D76, 0x7CFF, 0x9489, 0x8500, 0xB79B, 0xA612, 0xD2AD, 0xC324,
    0xF1BF, 0xE036, 0x18C1, 0x0948, 0x3BD3, 0x2A5A, 0x5EE5, 0x4F6C, 0x7DF7, 0x6C7E, 0xA50A, 0xB483, 0x8618, 0x9791, 0xE32E,
    0xF2A7, 0xC03C, 0xD1B5, 0x2942, 0x38CB, 0x0A50, 0x1BD9, 0x6F66, 0x7EEF, 0x4C74, 0x5DFD, 0xB58B, 0xA402, 0x9699, 0x8710,
    0xF3AF, 0xE226, 0xD0BD, 0xC134, 0x39C3, 0x284A, 0x1AD1, 0x0B58, 0x7FE7, 0x6E6E, 0x5CF5, 0x4D7C, 0xC60C, 0xD785, 0xE51E,
    0xF497, 0x8028, 0x91A1, 0xA33A, 0xB2B3, 0x4A44, 0x5BCD, 0x6956, 0x78DF, 0x0C60, 0x1DE9, 0x2F72, 0x3EFB, 0xD68D, 0xC704,
    0xF59F, 0xE416, 0x90A9, 0x8120, 0xB3BB, 0xA232, 0x5AC5, 0x4B4C, 0x79D7, 0x685E, 0x1CE1, 0x0D68, 0x3FF3, 0x2E7A, 0xE70E,
    0xF687, 0xC41C, 0xD595, 0xA12A, 0xB0A3, 0x8238, 0x93B1, 0x6B46, 0x7ACF, 0x4854, 0x59DD, 0x2D62, 0x3CEB, 0x0E70, 0x1FF9,
    0xF78F, 0xE606, 0xD49D, 0xC514, 0xB1AB, 0xA022, 0x92B9, 0x8330, 0x7BC7, 0x6A4E, 0x58D5, 0x495C, 0x3DE3, 0x2C6A, 0x1EF1,
    0x0F78};

uint16_t AX25UIFramer::crcCCITT(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ CRC_TABLE[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFF;
}

// ---------------------------------------------------------------------------
// Constructor / setCallsign
// ---------------------------------------------------------------------------

AX25UIFramer::AX25UIFramer(const char *sourceCallsign, uint8_t sourceSsid)
{
    setCallsign(sourceCallsign, sourceSsid);
    reset();
}

void AX25UIFramer::setCallsign(const char *callsign, uint8_t ssid)
{
    memset(_srcCall, ' ', 6);
    _srcCall[6] = '\0';
    size_t len = strnlen(callsign, 6);
    memcpy(_srcCall, callsign, len);
    // Ensure uppercase
    for (size_t i = 0; i < 6; i++) {
        if (_srcCall[i] >= 'a' && _srcCall[i] <= 'z')
            _srcCall[i] = (char)(_srcCall[i] - 32);
    }
    _srcSsid = ssid & 0x0F;
}

// ---------------------------------------------------------------------------
// AX.25 address field encoding
//   call   — 6-char, space-padded callsign (upper case)
//   ssid   — 0..15
//   lastAddr — true if this is the last address field (source in UI w/o digi)
//   out    — must point to 7-byte buffer
// ---------------------------------------------------------------------------
void AX25UIFramer::encodeAddress(const char *call, uint8_t ssid, bool lastAddr, uint8_t *out)
{
    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)(call[i] << 1);
    }
    // SSID byte: 0b0SSSS110 | end-of-address bit
    out[6] = (uint8_t)(0x60 | ((ssid & 0x0F) << 1) | (lastAddr ? 0x01 : 0x00));
}

// ---------------------------------------------------------------------------
// HDLC bit stuffing
//   Processes inBits bits from 'in' (LSB first within each byte).
//   Inserts a 0-bit after every 5 consecutive 1-bits.
//   Returns number of bytes written to 'out'.
//   Output is also LSB-first within each byte.
// ---------------------------------------------------------------------------
size_t AX25UIFramer::bitStuff(const uint8_t *in, size_t inBits, uint8_t *out, size_t maxOutBytes)
{
    size_t outBitPos = 0;
    uint8_t onesRun = 0;

    auto writeBit = [&](uint8_t bit) -> bool {
        size_t byte = outBitPos / 8;
        size_t bitPos = outBitPos % 8;
        if (byte >= maxOutBytes)
            return false;
        if (bitPos == 0)
            out[byte] = 0;
        out[byte] |= (bit << bitPos);
        outBitPos++;
        return true;
    };

    for (size_t i = 0; i < inBits; i++) {
        uint8_t bit = (in[i / 8] >> (i % 8)) & 1;
        if (!writeBit(bit))
            return 0;
        if (bit == 1) {
            onesRun++;
            if (onesRun == 5) {
                // Insert a stuffed 0-bit
                if (!writeBit(0))
                    return 0;
                onesRun = 0;
            }
        } else {
            onesRun = 0;
        }
    }

    // Round up to complete byte
    size_t outBytes = (outBitPos + 7) / 8;
    return outBytes;
}

// ---------------------------------------------------------------------------
// encode() — build a complete HDLC-framed AX.25 UI packet
// ---------------------------------------------------------------------------
size_t AX25UIFramer::encode(const uint8_t *in, size_t inLen, uint8_t *out, size_t maxOut)
{
    if (inLen > AX25_MAX_INFO_BYTES) {
        LOG_WARN("AX25UIFramer: packet too large (%u bytes, max %u). Dropping.", (unsigned)inLen, (unsigned)AX25_MAX_INFO_BYTES);
        return 0;
    }

    // ----------------------------------------------------------------
    // Assemble the raw frame body (before bit stuffing):
    //   DEST(7) + SRC(7) + CTRL(1) + PID(1) + INFO(inLen) + FCS(2)
    // ----------------------------------------------------------------
    static constexpr size_t MAX_FRAME_BODY = 7 + 7 + 1 + 1 + AX25_MAX_INFO_BYTES + 2;
    uint8_t frameBody[MAX_FRAME_BODY];
    size_t pos = 0;

    // Destination: "MESHTA", SSID=0, not last address (source follows)
    char destPadded[7] = "MESHTA";
    encodeAddress(destPadded, 0, false, frameBody + pos);
    pos += 7;

    // Source: operator callsign, space-padded, SSID from config, IS last address
    char srcPadded[7];
    memset(srcPadded, ' ', 6);
    srcPadded[6] = '\0';
    memcpy(srcPadded, _srcCall, strnlen(_srcCall, 6));
    encodeAddress(srcPadded, _srcSsid, true, frameBody + pos);
    pos += 7;

    // Control: UI frame
    frameBody[pos++] = HDLC_CONTROL_UI;

    // PID: no layer-3 protocol
    frameBody[pos++] = AX25_PID_NO_LAYER3;

    // INFO field
    memcpy(frameBody + pos, in, inLen);
    pos += inLen;

    // FCS (CRC-CCITT over everything up to but not including FCS)
    uint16_t fcs = crcCCITT(frameBody, pos);
    frameBody[pos++] = (uint8_t)(fcs & 0xFF);        // low byte first
    frameBody[pos++] = (uint8_t)((fcs >> 8) & 0xFF); // high byte second

    size_t frameBits = pos * 8; // total data bits (no flags yet)

    // ----------------------------------------------------------------
    // Build output buffer: preamble + bit-stuffed frame + tail
    // ----------------------------------------------------------------
    size_t outPos = 0;

    auto requireBytes = [&](size_t n) -> bool { return (outPos + n) <= maxOut; };

    // Preamble flags (0x7E repeated)
    if (!requireBytes(PREAMBLE_FLAGS))
        return 0;
    for (size_t i = 0; i < PREAMBLE_FLAGS; i++) {
        out[outPos++] = HDLC_FLAG;
    }

    // Bit-stuffed frame body (flag bytes are NOT bit-stuffed)
    static constexpr size_t STUFF_BUF_SZ = 350; // worst case stuffed bits
    uint8_t stuffed[STUFF_BUF_SZ];
    size_t stuffedLen = bitStuff(frameBody, frameBits, stuffed, STUFF_BUF_SZ);
    if (stuffedLen == 0)
        return 0;
    if (!requireBytes(stuffedLen))
        return 0;
    memcpy(out + outPos, stuffed, stuffedLen);
    outPos += stuffedLen;

    // Tail flags
    if (!requireBytes(TAIL_FLAGS))
        return 0;
    for (size_t i = 0; i < TAIL_FLAGS; i++) {
        out[outPos++] = HDLC_FLAG;
    }

    return outPos;
}

// ---------------------------------------------------------------------------
// reset() — reset RX decoder state machine
// ---------------------------------------------------------------------------
void AX25UIFramer::reset()
{
    _rxState = RxState::IDLE;
    _rxBitBuf = 0;
    _rxBitCount = 0;
    _rawLen = 0;
    _onesRun = 0;
    _lastBit = false;
    _frameBitLen = 0;
    memset(_rawBuf, 0, sizeof(_rawBuf));
    memset(_frameBuf, 0, sizeof(_frameBuf));
}

// ---------------------------------------------------------------------------
// bitUnstuff — reverse of bitStuff
//   Processes inBits bits, removes stuffed 0s after 5 consecutive 1s.
//   Returns number of bytes written.
// ---------------------------------------------------------------------------
size_t AX25UIFramer::bitUnstuff(const uint8_t *in, size_t inBits, uint8_t *out, size_t maxOutBytes)
{
    size_t outBitPos = 0;
    uint8_t onesRun = 0;

    auto writeBit = [&](uint8_t bit) -> bool {
        size_t byte = outBitPos / 8;
        size_t bitPos = outBitPos % 8;
        if (byte >= maxOutBytes)
            return false;
        if (bitPos == 0)
            out[byte] = 0;
        out[byte] |= (bit << bitPos);
        outBitPos++;
        return true;
    };

    size_t i = 0;
    while (i < inBits) {
        uint8_t bit = (in[i / 8] >> (i % 8)) & 1;
        i++;
        if (bit == 1) {
            onesRun++;
            if (!writeBit(1))
                return 0;
            if (onesRun == 5 && i < inBits) {
                // Next bit should be a stuffed 0 — skip it
                uint8_t stuffed = (in[i / 8] >> (i % 8)) & 1;
                i++;
                if (stuffed == 1) {
                    // Not a stuffed bit — flag or abort; signal error
                    return 0;
                }
                onesRun = 0;
            }
        } else {
            onesRun = 0;
            if (!writeBit(0))
                return 0;
        }
    }
    return (outBitPos + 7) / 8;
}

// ---------------------------------------------------------------------------
// processFrame — validate and dispatch a received raw frame
// ---------------------------------------------------------------------------
void AX25UIFramer::processFrame()
{
    if (_rawLen < 18) {
        // Too short: minimum AX.25 UI frame is DEST(7)+SRC(7)+CTRL(1)+PID(1)+FCS(2) = 18
        return;
    }

    // Bit-unstuff the raw accumulated bits
    uint8_t destuffed[300];
    size_t destuffedBytes = bitUnstuff(_rawBuf, _rawLen * 8, destuffed, sizeof(destuffed));
    if (destuffedBytes < 18)
        return;

    // Validate FCS: CRC over everything except the trailing 2 FCS bytes
    size_t fcsOffset = destuffedBytes - 2;
    uint16_t computedFcs = crcCCITT(destuffed, fcsOffset);
    uint16_t receivedFcs = (uint16_t)(destuffed[fcsOffset]) | ((uint16_t)(destuffed[fcsOffset + 1]) << 8);
    if (computedFcs != receivedFcs) {
        LOG_DEBUG("AX25: FCS mismatch (got 0x%04X, expected 0x%04X)", receivedFcs, computedFcs);
        return;
    }

    // Check CTRL and PID bytes  [DEST:7][SRC:7][CTRL][PID][INFO...]
    uint8_t ctrl = destuffed[14];
    uint8_t pid = destuffed[15];
    if (ctrl != HDLC_CONTROL_UI || pid != AX25_PID_NO_LAYER3) {
        return; // Not a UI frame or wrong PID
    }

    // INFO field starts at byte 16, ends before FCS (last 2 bytes)
    size_t infoStart = 16;
    size_t infoEnd = fcsOffset;
    if (infoEnd <= infoStart)
        return;
    size_t infoLen = infoEnd - infoStart;

    LOG_DEBUG("AX25: received valid frame, INFO=%u bytes", (unsigned)infoLen);

    if (onFrame) {
        onFrame(destuffed + infoStart, infoLen);
    }
}

// ---------------------------------------------------------------------------
// rxBit — feed one decoded bit (post-NRZI) into the RX state machine
// ---------------------------------------------------------------------------
void AX25UIFramer::rxBit(uint8_t bit)
{
    // Shift bit into the bit buffer (LSB first)
    _rxBitBuf = (_rxBitBuf >> 1) | (bit << 7);
    _rxBitCount++;

    // Check for flag byte (0x7E = 0b01111110)
    // In the bit buffer (LSB first), flag arrives as: 0 1 1 1 1 1 1 0
    // In _rxBitBuf filled MSB-first from shift: after 8 bits = 0x7E
    if (_rxBitCount >= 8 && _rxBitBuf == HDLC_FLAG) {
        if (_rxState == RxState::IN_FRAME && _rawLen > 0) {
            // Flag marks end of frame
            processFrame();
        }
        // Start a new frame
        _rxState = RxState::IN_FRAME;
        _rawLen = 0;
        _onesRun = 0;
        _rxBitCount = 0;
        _rxBitBuf = 0;
        return;
    }

    if (_rxBitCount < 8)
        return; // Not a full byte yet

    // We have a full byte — store it if in a frame
    _rxBitCount = 0;
    uint8_t byteVal = _rxBitBuf;
    _rxBitBuf = 0;

    if (_rxState == RxState::IN_FRAME) {
        if (_rawLen < sizeof(_rawBuf)) {
            _rawBuf[_rawLen++] = byteVal;
        } else {
            // Frame too large — abandon
            reset();
        }
    }
}

// ---------------------------------------------------------------------------
// feed() — accept decoded bits from the AFSK demodulator
// ---------------------------------------------------------------------------
void AX25UIFramer::feed(const uint8_t *bytes, size_t len)
{
    // bytes here are BIT-packed decoded output from SA868AFSKTransport,
    // one bit per call represented as a packed byte. We accept them as
    // packed bits (LSB first within each byte) and feed bit by bit.
    for (size_t i = 0; i < len; i++) {
        for (int b = 0; b < 8; b++) {
            rxBit((bytes[i] >> b) & 1);
        }
    }
}
