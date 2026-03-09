// src/mesh/SA868AFSKInterface.h
//
// SA868AFSKInterface — Meshtastic RadioInterface implementation.
//
// This is the top-level class registered in RadioInterface.cpp. It adapts the
// Meshtastic RadioInterface abstraction to the AFSK1200 / AX.25 transport.
//
// It owns the SA868Driver, AX25UIFramer, and SA868AFSKTransport objects,
// manages the deduplication cache for AX.25 UI frames, and handles
// serialisation/deserialisation of the Meshtastic `meshtastic_MeshPacket`.

#pragma once

#include "RadioInterface.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "platform/esp32/ttwrplus/SA868AFSKTransport.h"
#include "platform/esp32/ttwrplus/SA868Driver.h"
#include "transport/AX25UIFramer.h"
#include <memory>

class SA868AFSKInterface : public RadioInterface
{
  public:
    SA868AFSKInterface();
    virtual ~SA868AFSKInterface();

    // ---- RadioInterface overrides ----
    bool init() override;
    bool reconfigure() override;
    ErrorCode send(meshtastic_MeshPacket *p) override;
    uint32_t getPacketTime(uint32_t totalPacketLen, bool received = false) override;
    bool wideLora() override { return false; }
    bool canSleep() override { return false; } // AFSK RX runs continuously, cannot deep sleep

  private:
    SA868Driver _driver;
    AX25UIFramer _framer;
    SA868AFSKTransport _transport;

    // Meshtastic magic header (prepended to nanopb payload before AX.25 framing)
    static constexpr uint8_t MESH_MAGIC[4] = {0x94, 0xC3, 0x00, 0x00};

    // Deduplication cache for received packets.
    // AX.25 UI frames don't have built-in dedup, and we don't use digipeater
    // paths. We may hear our own transmissions or immediate rebroadcasts.
    // We cache packet IDs here before passing to the Router.
    static constexpr size_t SEEN_CACHE_SIZE = 64;
    uint32_t _seenPacketIds[SEEN_CACHE_SIZE];
    size_t _seenHead = 0;

    bool isSeen(uint32_t packetId);
    void markSeen(uint32_t packetId);

    // Determines the correct operating frequency (user override or variant default)
    float getOperatingFrequencyMHz() const;

    // AX.25 transport receive callback
    void handleIncomingPacket(const uint8_t *data, size_t len);
};
