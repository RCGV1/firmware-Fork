// src/mesh/transport/StreamTransport.h
//
// StreamTransport — base class for transports whose underlying medium is a
// continuous byte stream (audio, UART, TCP, etc.).
//
// Bridges the PacketTransport (discrete-packet) interface and the Framer
// (byte-stream frame-boundary) interface:
//
//   TX: sendPacket() → Framer::encode() → writeBytes()
//   RX: subclass calls feedBytes() → Framer::feed() → onPacket callback
//
// Subclasses only need to implement:
//   - writeBytes()  — deliver encoded bytes to the physical medium
//   - init() / loop() — hardware lifecycle
//
// The Framer pointer is owned externally; this class does not delete it.

#pragma once
#include "Framer.h"
#include "PacketTransport.h"

// Maximum encoded frame size: AX.25 with HDLC bit stuffing on a 256-byte INFO
// field can be at most roughly (256+18) * 8/7 bytes of stuffed bits packed into
// bytes ≈ 308 bytes, plus preamble/tail flags (12+2 = 14 flag bytes = 14 bytes).
// 512 bytes gives comfortable headroom.
static constexpr size_t STREAM_TRANSPORT_ENCODE_BUF = 512;

class StreamTransport : public PacketTransport
{
  public:
    /// framer must remain valid for the lifetime of this object.
    explicit StreamTransport(Framer *framer) : _framer(framer)
    {
        // Wire framer output to our onPacket callback.
        _framer->onFrame = [this](const uint8_t *data, size_t len) {
            if (onPacket)
                onPacket(data, len);
        };
    }

    // ---------------------------------------------------------------------------
    // PacketTransport::sendPacket — encodes via framer, then writes to stream
    // ---------------------------------------------------------------------------
    bool sendPacket(const uint8_t *data, size_t len) override
    {
        uint8_t encoded[STREAM_TRANSPORT_ENCODE_BUF];
        size_t encodedLen = _framer->encode(data, len, encoded, sizeof(encoded));
        if (encodedLen == 0) {
            return false; // framer rejected packet (too large, etc.)
        }
        return writeBytes(encoded, encodedLen);
    }

  protected:
    // ---------------------------------------------------------------------------
    // Subclass API
    // ---------------------------------------------------------------------------

    /// Called by the subclass when raw bytes arrive from the medium.
    /// Passes them to the framer which calls onPacket when a frame is complete.
    void feedBytes(const uint8_t *bytes, size_t len) { _framer->feed(bytes, len); }

    /// Write encoded bytes to the underlying medium.
    /// Returns true if the bytes were accepted for transmission.
    /// The bytes represent one complete encoded frame (or part of one, if the
    /// medium is truly streaming) — the framer decides the exact boundary.
    virtual bool writeBytes(const uint8_t *data, size_t len) = 0;

    Framer *_framer;
};
