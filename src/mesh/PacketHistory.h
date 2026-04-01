#pragma once

#include "NodeDB.h"

// Number of relayers we keep track of.
#define NUM_RELAYERS 6
// hop_limit is stored as a uint16_t with two 7-bit fields:
#define HOP_LIMIT_HIGHEST_MASK 0x007F // Bits 0-6: highest hop limit seen for this packet
#define HOP_LIMIT_OUR_TX_MASK 0x3F80  // Bits 7-13: our hop limit when we first transmitted
#define HOP_LIMIT_OUR_TX_SHIFT 7

/**
 * This is a mixin that adds a record of past packets we have seen
 */
class PacketHistory
{
  private:
    struct PacketRecord { // A record of a recent message broadcast, no need to be visible outside this class.
        NodeNum sender;
        PacketId id;
        uint32_t rxTimeMsec;              // Unix time in msecs - the time we received it, 0 means empty
        NodeNum next_hop;                 // The next hop asked for this packet (full node ID)
        uint16_t hop_limit;               // bits 0-6: Highest hop limit observed, bits 7-13: our hop limit when transmitted
        uint16_t _pad;                    // padding for alignment
        NodeNum relayed_by[NUM_RELAYERS]; // Array of full node IDs that relayed this packet
    };                                    // 4B + 4B + 4B + 4B + 2B + 2B + (4B*6) = 40B

    uint32_t recentPacketsCapacity =
        0; // Can be set in constructor, no need to recompile. Used to allocate memory for mx_recentPackets.
    PacketRecord *recentPackets = NULL; // Simple and fixed in size. Debloat.

    /** Find a packet record in history.
     * @param sender NodeNum
     * @param id PacketId
     * @return pointer to PacketRecord if found, NULL if not found */
    PacketRecord *find(NodeNum sender, PacketId id);

    /** Insert/Replace oldest PacketRecord in mx_recentPackets.
     * @param r PacketRecord to insert or replace */
    void insert(const PacketRecord &r); // Insert or replace a packet record in the history

    /* Check if a certain node was a relayer of a packet in the history given iterator
     * If wasSole is not nullptr, it will be set to true if the relayer was the only relayer of that packet
     * @return true if node was indeed a relayer, false if not */
    bool wasRelayer(const NodeNum relayer, const PacketRecord &r, bool *wasSole = nullptr);

    uint8_t getHighestHopLimit(const PacketRecord &r);
    void setHighestHopLimit(PacketRecord &r, uint8_t hopLimit);
    uint8_t getOurTxHopLimit(const PacketRecord &r);
    void setOurTxHopLimit(PacketRecord &r, uint8_t hopLimit);

    PacketHistory(const PacketHistory &);            // non construction-copyable
    PacketHistory &operator=(const PacketHistory &); // non copyable
  public:
    explicit PacketHistory(uint32_t size = -1); // Constructor with size parameter, default is PACKETHISTORY_MAX
    ~PacketHistory();

    /**
     * Update recentBroadcasts and return true if we have already seen this packet
     *
     * @param withUpdate if true and not found we add an entry to recentPackets
     * @param wasFallback if not nullptr, packet will be checked for fallback to flooding and value will be set to true if so
     * @param weWereNextHop if not nullptr, packet will be checked for us being the next hop and value will be set to true if so
     * @param wasUpgraded if not nullptr, will be set to true if this packet has better hop_limit than previously seen
     */
    bool wasSeenRecently(const meshtastic_MeshPacket *p, bool withUpdate = true, bool *wasFallback = nullptr,
                         bool *weWereNextHop = nullptr, bool *wasUpgraded = nullptr);

    /* Check if a certain node was a relayer of a packet in the history given an ID and sender
     * If wasSole is not nullptr, it will be set to true if the relayer was the only relayer of that packet
     * @return true if node was indeed a relayer, false if not */
    bool wasRelayer(const NodeNum relayer, const uint32_t id, const NodeNum sender, bool *wasSole = nullptr);

    // Remove a relayer from the list of relayers of a packet in the history given an ID and sender
    void removeRelayer(const NodeNum relayer, const uint32_t id, const NodeNum sender);

    // To check if the PacketHistory was initialized correctly by constructor
    bool initOk(void) { return recentPackets != NULL && recentPacketsCapacity != 0; }
};
