// src/mesh/transport/PacketTransport.h
//
// PacketTransport — pure abstract interface for all discrete-packet transports.
//
// This interface makes the Meshtastic radio backend pluggable: any transport
// that can send and receive discrete packets (LoRa, AFSK/AX.25, KISS TNC, TCP,
// etc.) can be implemented as a subclass without touching upper-layer code.
//
// Packet semantics: each send/receive is exactly one complete, discrete packet.
// The transport handles framing internally if the underlying medium is a stream.
// The transport must NOT fragment packets across multiple sendPacket() calls.

#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

class PacketTransport
{
  public:
    virtual ~PacketTransport() = default;

    // ---------------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------------

    /// Initialise the transport and underlying hardware.
    /// Must be called before any other method. Returns true on success.
    virtual bool init() = 0;

    /// Periodic tick. Call from the main loop or a dedicated OSThread.
    /// May be a no-op on platforms with threaded transport implementations.
    virtual void loop() = 0;

    // ---------------------------------------------------------------------------
    // Transmit
    // ---------------------------------------------------------------------------

    /// Send one complete packet (data[0..len-1]).
    ///
    /// Returns true if the packet was accepted for transmission.
    /// Returns false if the medium is busy, an error occurred, or the packet
    /// exceeds size limits.  The bytes may not have been physically transmitted
    /// when this returns — only that the transport has accepted them.
    virtual bool sendPacket(const uint8_t *data, size_t len) = 0;

    // ---------------------------------------------------------------------------
    // Receive callback
    // ---------------------------------------------------------------------------

    /// The transport calls this when a complete, validated packet arrives.
    /// Set this callback before calling init(). The data pointer is only valid
    /// for the duration of the callback — copy any bytes you need.
    std::function<void(const uint8_t *data, size_t len)> onPacket;

    // ---------------------------------------------------------------------------
    // Channel state
    // ---------------------------------------------------------------------------

    /// Returns true if the medium appears clear (no ongoing transmission detected).
    /// Used for listen-before-talk. Default: always clear (suitable for full-duplex
    /// or transports that handle collision avoidance internally).
    virtual bool isClear() { return true; }

    // ---------------------------------------------------------------------------
    // Signal quality — most recently received packet
    // ---------------------------------------------------------------------------

    /// Received signal strength in dBm. Returns 0 if not applicable.
    virtual int8_t lastRSSI() { return 0; }

    /// Signal-to-noise ratio (transport-specific units). Returns 0 if not applicable.
    virtual uint8_t lastSNR() { return 0; }

    // ---------------------------------------------------------------------------
    // Identity
    // ---------------------------------------------------------------------------

    /// Human-readable name for this transport (used in log output).
    virtual const char *transportName() const = 0;
};
