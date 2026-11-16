#pragma once

#include "PhoneAPI.h"
#ifdef WADA_MESHTASTIC
#include "wadacomms/PacketServer.h"
#else
#include "comms/PacketServer.h"
#endif
#include "concurrency/OSThread.h"
#ifdef WADA_MESHTASTIC
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#endif

/**
 * A version of the phone API used for inter task communication based on protobuf packets, e.g.
 * between two tasks running on CPU0 and CPU1, respectively.
 *
 */
class PacketAPI : public PhoneAPI, public concurrency::OSThread
{
  public:
    static PacketAPI *create(PacketServer *_server);
    virtual ~PacketAPI() {};
    virtual int32_t runOnce();
#ifdef WADA_MESHTASTIC
    void publish(const meshtastic_FromRadio &message);
    void publishPacket(const meshtastic_MeshPacket &packet);
    void publishQueueStatus(const meshtastic_QueueStatus &status);
    void publishNotification(const meshtastic_ClientNotification &notification);
#endif
    // Check the current underlying physical queue to see if the client is fetching packets
    bool checkIsConnected() override;

  protected:
    explicit PacketAPI(PacketServer *_server);
#ifdef WADA_MESHTASTIC
    bool hasPrivateReceiveQueue() const override { return true; }
    bool readPrivateReceiveQueue(meshtastic_FromRadio &out, bool consume) override;
#endif

    void onNowHasData(uint32_t fromRadioNum) override {}
    void onConnectionChanged(bool connected) override {}

  private:
    bool receivePacket(void);
    bool sendPacket(void);
    bool notifyProgrammingMode(void);

#ifdef WADA_MESHTASTIC
    void refreshNodeFacts();
    void refreshStatus(meshtastic_FromRadio &);
    struct NodeFact {
        uint32_t node = 0, hash = 0;
        bool seen = false;
    };
    NodeFact nodeFacts[256]{};
    uint32_t lastNodeFacts = 0;
    bool nodeFactsReady = false;
    QueueHandle_t receiveQueue = nullptr;
    std::atomic<uint32_t> droppedPackets{0};
    meshtastic_FromRadio pendingFromRadio = {};
    bool pendingSend = false;
#endif
    bool isConnected;
    bool programmingMode;
    PacketServer *server;
    uint8_t txBuf[MAX_TO_FROM_RADIO_SIZE] = {0}; // dummy buf to obey PhoneAPI
};

extern PacketAPI *packetAPI;
