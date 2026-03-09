// src/mesh/transport/Framer.h
//
// Framer — pure abstract interface for frame boundary encoding/decoding.
//
// A Framer sits between a raw byte stream and a packet-oriented transport.
// It knows how to wrap a discrete packet into a sequence of bytes that can
// be recovered from a continuous stream even if the stream is noisy or
// the receiver starts mid-stream.
//
// Concrete implementations: AX25UIFramer (AX.25 UI + HDLC), KISSFramer,
// SLIPFramer, COBSFramer, etc.

#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

class Framer
{
  public:
    virtual ~Framer() = default;

    // ---------------------------------------------------------------------------
    // Encoding (TX direction)
    // ---------------------------------------------------------------------------

    /// Encode one complete packet into frame bytes for writing to a byte-stream.
    ///
    ///   in/inLen   — raw packet payload bytes
    ///   out/maxOut — output buffer; at most maxOut bytes will be written
    ///
    /// Returns the number of bytes written to out, or 0 on failure (e.g. packet
    /// too large). The caller must provide a buffer large enough for the worst-
    /// case framed output; 512 bytes covers AX.25 UI + HDLC bit stuffing.
    virtual size_t encode(const uint8_t *in, size_t inLen, uint8_t *out, size_t maxOut) = 0;

    // ---------------------------------------------------------------------------
    // Decoding (RX direction)
    // ---------------------------------------------------------------------------

    /// Feed raw bytes arriving from the byte stream into the framer.
    /// The framer accumulates bytes internally and calls onFrame() whenever it
    /// has successfully decoded a complete, CRC-validated packet.
    /// Can be called with an arbitrary number of bytes per call.
    virtual void feed(const uint8_t *bytes, size_t len) = 0;

    /// Callback fired when a complete, validated packet has been decoded.
    /// Set this before calling feed(). The data pointer is only valid for the
    /// duration of the call — copy any bytes you need.
    std::function<void(const uint8_t *frame, size_t len)> onFrame;

    // ---------------------------------------------------------------------------
    // State reset
    // ---------------------------------------------------------------------------

    /// Reset internal decoder state (flush RX buffer, abandon any partial frame).
    /// Call on loss of sync, after a gap in audio, or whenever starting fresh.
    virtual void reset() = 0;
};
