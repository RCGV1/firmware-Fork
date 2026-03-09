// src/mesh/SA868AFSKInterface.cpp
//
// SA868AFSKInterface implementation.
// See SA868AFSKInterface.h for details.

#include "SA868AFSKInterface.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <pb_decode.h>

constexpr uint8_t SA868AFSKInterface::MESH_MAGIC[4];

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
SA868AFSKInterface::SA868AFSKInterface()
    : RadioInterface(), _driver(), _framer("NOCALL"), // Placeholder, overridden in init()
      _transport(&_framer, &_driver)
{
    memset(_seenPacketIds, 0, sizeof(_seenPacketIds));
}

SA868AFSKInterface::~SA868AFSKInterface() {}

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------
bool SA868AFSKInterface::init()
{
    LOG_INFO("SA868AFSKInterface: initializing");

    // Must call base class init
    RadioInterface::init();

    // 1. Initialise the hardware driver (UART, PTT)
    if (!_driver.init()) {
        LOG_ERROR("SA868AFSKInterface: Driver init failed");
        return false;
    }

    // 2. Set operating parameters
    if (!reconfigure()) {
        LOG_ERROR("SA868AFSKInterface: Config failed");
        return false;
    }

    // 3. Setup receive callback from the transport
    _transport.onPacket = [this](const uint8_t *data, size_t len) { handleIncomingPacket(data, len); };

    // 4. Initialise transport (I2S, FreeRTOS tasks)
    if (!_transport.init()) {
        LOG_ERROR("SA868AFSKInterface: Transport init failed");
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// reconfigure()
// ---------------------------------------------------------------------------
bool SA868AFSKInterface::reconfigure()
{
    // Base class configuration properties (mostly unused for analog FM,
    // but handled for consistency)
    RadioInterface::reconfigure();

    // Determine operating frequency
    float freqMHz = getOperatingFrequencyMHz();

    // Apply to hardware
    _driver.setFrequency(freqMHz, 0, 0); // No CTCSS, squelch=0 (open/level 0)
    _driver.setVolume(7);                // Higher volume for better TX audio
    _driver.setTxPower(1);               // High power

    // Apply operator callsign to the AX.25 Framer
    // Enforce ham mode rules locally as well: if no license, use NOCALL.
    // The driver will block TX anyway, but RX will work.
    if (devicestate.owner.is_licensed && devicestate.owner.long_name[0] != '\0') {
        // Use lower nibble of node num as SSID
        uint8_t ssid = nodeDB->getNodeNum() & 0x0F;
        _framer.setCallsign(devicestate.owner.long_name, ssid);
    } else {
        _framer.setCallsign("NOCALL", 0);
    }

    return true;
}

// ---------------------------------------------------------------------------
// getOperatingFrequencyMHz()
// ---------------------------------------------------------------------------
float SA868AFSKInterface::getOperatingFrequencyMHz() const
{
    float overrideFreq = config.lora.override_frequency;

    // Check if the user set a valid UHF override
    if (overrideFreq > 400.0f && overrideFreq < 480.0f) {
        LOG_INFO("SA868: Using user override frequency %.4f MHz", overrideFreq);
        return overrideFreq;
    }

    // Fall back to variant default
    LOG_INFO("SA868: Using variant default frequency %.4f MHz", SA868_DEFAULT_FREQUENCY_MHZ);
    return SA868_DEFAULT_FREQUENCY_MHZ;
}

// ---------------------------------------------------------------------------
// send() — serialize and hand off to AFSK transport
// ---------------------------------------------------------------------------
ErrorCode SA868AFSKInterface::send(meshtastic_MeshPacket *p)
{
    // Base class populates `radioBuffer` with PacketHeader (binary) + encrypted payload bytes.
    // Note: The original brief suggested `sendPacket()` receives a raw protobuf, but the base
    // `RadioInterface` has already done `beginSending(p)` which converts it to a `RadioBuffer`.
    // The AFSK transport acts as a transparent byte-pipe for everything inside `INFO`.
    // It is easiest and most strictly conforming to existing codebase patterns to just
    // send the `radioBuffer` exactly as the LoRa backend does, prefixed with the magic bytes.

    size_t payloadLen = beginSending(p); // Serializes into radioBuffer

    if (payloadLen == 0) {
        return ERRNO_UNKNOWN;
    }

    // Check payload size against AX.25 INFO field limit (256 bytes)
    size_t totalInfoLen = sizeof(MESH_MAGIC) + payloadLen;
    if (totalInfoLen > 256) {
        LOG_ERROR("AFSK: Packet too large for AX.25 (%u bytes). max=256.", (unsigned)totalInfoLen);
        return ERRNO_UNKNOWN;
    }

    // Assemble final INFO buffer: [MAGIC] [RADIO_BUFFER]
    uint8_t infoBuf[256];
    memcpy(infoBuf, MESH_MAGIC, sizeof(MESH_MAGIC));
    memcpy(infoBuf + sizeof(MESH_MAGIC), &radioBuffer, payloadLen);

    LOG_DEBUG("AFSK: Sending frame (%u bytes)", (unsigned)totalInfoLen);

    // Non-blocking handoff to transport layer (transport handles CSMA/Wait internally)
    bool ok = _transport.sendPacket(infoBuf, totalInfoLen);

    // We clean up the sending state
    sendingPacket = nullptr;

    if (!ok) {
        LOG_WARN("AFSK: Transport rejected packet (channel busy or unlicensed)");
        return ERRNO_UNKNOWN;
    }

    return ERRNO_OK;
}

// ---------------------------------------------------------------------------
// handleIncomingPacket() — called by transport when AX.25 UI frame is complete
// ---------------------------------------------------------------------------
void SA868AFSKInterface::handleIncomingPacket(const uint8_t *data, size_t len)
{
    // data pointer belongs to the framer and points to the unpacked INFO field

    if (len < sizeof(MESH_MAGIC) + sizeof(PacketHeader)) {
        LOG_DEBUG("AFSK RX: Frame too short to contain Meshtastic header");
        return;
    }

    // Verify magic
    if (memcmp(data, MESH_MAGIC, sizeof(MESH_MAGIC)) != 0) {
        LOG_DEBUG("AFSK RX: Magic mismatch");
        return;
    }

    // Copy into `radioBuffer` exactly matching how SX126x handles RX
    size_t payloadLen = len - sizeof(MESH_MAGIC);
    if (payloadLen > sizeof(radioBuffer)) {
        LOG_WARN("AFSK RX: Payload exceeds max Meshtastic MTU");
        return;
    }

    memcpy(&radioBuffer, data + sizeof(MESH_MAGIC), payloadLen);

    // Extract basic routing info
    uint32_t packetId = radioBuffer.header.id;
    NodeNum from = radioBuffer.header.from;
    NodeNum to = radioBuffer.header.to;

    // Deduplicate
    if (isSeen(packetId)) {
        LOG_DEBUG("AFSK RX: Duplicate packet id 0x%08x from 0x%08x (ignored)", packetId, from);
        return;
    }
    markSeen(packetId);

    // Allocate a new protobuf packet and decode the binary payload into it
    meshtastic_MeshPacket *p = packetPool.allocZeroed();
    if (!p) {
        LOG_ERROR("AFSK RX: OOM allocating packet");
        return;
    }

    p->id = packetId;
    p->from = from;
    p->to = to;
    p->channel = radioBuffer.header.channel;
    p->next_hop = radioBuffer.header.next_hop;
    p->relay_node = radioBuffer.header.relay_node;
    p->hop_limit = radioBuffer.header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
    p->want_ack = (radioBuffer.header.flags & PACKET_FLAGS_WANT_ACK_MASK) != 0;
    p->via_mqtt = (radioBuffer.header.flags & PACKET_FLAGS_VIA_MQTT_MASK) != 0;
    p->hop_start = (radioBuffer.header.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;

    // The data following the PacketHeader is the opaque protocol buffer
    size_t pbLen = payloadLen - sizeof(PacketHeader);

    // We treat the packet as encrypted (the Router will decrypt or process it)
    p->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p->encrypted.size = pbLen;
    memcpy(p->encrypted.bytes, radioBuffer.payload, std::min((size_t)pbLen, sizeof(p->encrypted.bytes)));

    // Fill metadata
    p->rx_rssi = _transport.lastRSSI();
    p->rx_snr = _transport.lastSNR();
    p->rx_time = airTime->getSecondsSinceBoot();
    p->priority = meshtastic_MeshPacket_Priority_UNSET;

    printPacket("AFSK RX", p);

    // Hand to router
    deliverToReceiver(p);
}

// ---------------------------------------------------------------------------
// getPacketTime() — estimate airtime
// ---------------------------------------------------------------------------
uint32_t SA868AFSKInterface::getPacketTime(uint32_t totalPacketLen, bool received)
{
    // AFSK1200: 1200 baud = 1200 bits per second = ~833 us per bit
    // 1 byte = 8 bits -> 8 * 833 = 6.66 ms per byte.
    // AX.25 Overhead: minimum 18 bytes + bit stuffing (~15-20%) + Preamble (12) + Tail (2)
    // plus 150ms preamble tone + 50ms tail tone.
    // Simplifying: ~200ms fixed overhead + (totalPacketLen + ~32 bytes overhead) * 8 bits / 1.2
    float bits = (totalPacketLen + 32) * 8.0f;
    float stuffingBits = bits * 1.15f; // Add bit stuffing estimate
    return 200 + (uint32_t)(stuffingBits / 1.2f);
}

// ---------------------------------------------------------------------------
// Deduplication Cache
// ---------------------------------------------------------------------------
bool SA868AFSKInterface::isSeen(uint32_t packetId)
{
    for (size_t i = 0; i < SEEN_CACHE_SIZE; i++) {
        if (_seenPacketIds[i] == packetId) {
            return true;
        }
    }
    return false;
}

void SA868AFSKInterface::markSeen(uint32_t packetId)
{
    _seenPacketIds[_seenHead] = packetId;
    _seenHead = (_seenHead + 1) % SEEN_CACHE_SIZE;
}
