#ifdef USE_PACKET_API

// First, in its own block so the include sorter keeps it there: configuration.h supplies the
// variant defines mesh-pb-constants.h needs (portduino resolves MAX_NUM_NODES at runtime).
#include "UptimeClock.h"
#include "configuration.h"

#include "MeshService.h"
#include "PowerFSM.h"
#include "RadioInterface.h"
#include "api/PacketAPI.h"
#include "meshUtils.h"
#include "modules/NodeInfoModule.h"
#ifdef WADA_MESHTASTIC
#include "../../../lib/WadaApp/port/WadaNodeState.h"
#include "TypeConversions.h"
#include "mesh/Throttle.h"
#include "modules/StatusMessageModule.h"
extern bool wadaProcessNodeActions();
#endif
#ifdef WADA_MESHTASTIC
#include <WadaApp.h>
#include <esp_heap_caps.h>
extern void wadaPollUiActivity();
#endif

#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
// receivePacket() dispatches ToRadio straight to MeshService, bypassing handleToRadioPacket and so
// the lockdown admin gate. Fail the build rather than silently ship an admin-auth bypass.
#error "USE_PACKET_API is incompatible with MESHTASTIC_PHONEAPI_ACCESS_CONTROL (PacketAPI bypasses the lockdown admin gate)"
#endif

PacketAPI *packetAPI = nullptr;

PacketAPI *PacketAPI::create(PacketServer *_server)
{
    if (!packetAPI) {
        packetAPI = new PacketAPI(_server);
    }
    return packetAPI;
}

PacketAPI::PacketAPI(PacketServer *_server)
    : concurrency::OSThread("PacketAPI"), isConnected(false), programmingMode(false), server(_server)
{
    api_type = TYPE_PACKET;
#ifdef WADA_MESHTASTIC
    auto *storage =
        static_cast<uint8_t *>(heap_caps_calloc(32, sizeof(meshtastic_FromRadio), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto *control = new StaticQueue_t;
    assert(storage && control);
    receiveQueue = xQueueCreateStatic(32, sizeof(meshtastic_FromRadio), storage, control);
    assert(receiveQueue);
#endif
}

int32_t PacketAPI::runOnce()
{
#ifdef WADA_MESHTASTIC
    wadaSignalFirmwareReady();
    wadaPollUiActivity();
    refreshNodeFacts();
    bool success = sendPacket();
#else
    bool success = false;
#ifndef ARCH_PORTDUINO
    if (config.bluetooth.enabled) {
        if (!programmingMode) {
            // in programmingMode we don't send any packets to the client except this one notify
            programmingMode = true;
            success = notifyProgrammingMode();
        }
    } else
#endif
    {
        success = sendPacket();
    }
#endif
    success |= receivePacket();
#ifdef WADA_MESHTASTIC
    success |= wadaProcessNodeActions();
#endif
    return success ? 10 : 50;
}

bool PacketAPI::receivePacket(void)
{
    bool data_received = false;
    for (unsigned budget = 0; budget < 8 && server->hasData(); ++budget) {
        isConnected = true;
        data_received = true;

#ifndef WADA_MESHTASTIC
        powerFSM.trigger(EVENT_INPUT);
#endif
        lastContactMsec = Time::skipZero(Time::getMillis());

        meshtastic_ToRadio *mr;
        auto received = server->receivePacket();
        if (!received)
            break;
        auto p = received->move();
        int id = p->getPacketId();
        LOG_DEBUG("Received packet id=%u", id);
        mr = (meshtastic_ToRadio *)&static_cast<DataPacket<meshtastic_ToRadio> *>(p.get())->getData();

#ifdef WADA_MESHTASTIC
        // Node-info announcements belong to the firmware module, including its rate/airtime limits.
        if (mr->which_payload_variant == meshtastic_ToRadio_packet_tag &&
            mr->packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
            mr->packet.decoded.portnum == meshtastic_PortNum_NODEINFO_APP && mr->packet.from == 0) {
            const bool sent = nodeInfoModule && nodeInfoModule->sendOurNodeInfo(mr->packet.to, mr->packet.decoded.want_response,
                                                                                mr->packet.channel, true, mr->packet.hop_limit);
            meshtastic_ClientNotification result = {};
            result.level = sent ? meshtastic_LogRecord_Level_INFO : meshtastic_LogRecord_Level_WARNING;
            snprintf(result.message, sizeof(result.message), "Node info: %s",
                     sent ? "accepted by Meshtastic" : "deferred by firmware rate or airtime limits");
            publishNotification(result);
            continue;
        }
        if (mr->which_payload_variant == meshtastic_ToRadio_packet_tag &&
            mr->packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag && mr->packet.to == NODENUM_BROADCAST &&
            mr->packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP &&
            !wadaValidateChannelIntent(mr->packet.id, mr->packet.channel)) {
            meshtastic_ClientNotification rejected = {};
            rejected.level = meshtastic_LogRecord_Level_ERROR;
            rejected.has_reply_id = true;
            rejected.reply_id = mr->packet.id;
            snprintf(rejected.message, sizeof rejected.message,
                     "Channel changed before send. Open the intended channel and compose again.");
            publishNotification(rejected);
            continue;
        }
        uint8_t encoded[MAX_TO_FROM_RADIO_SIZE];
        const size_t length = pb_encode_to_bytes(encoded, sizeof(encoded), &meshtastic_ToRadio_msg, mr);
        if (length)
            PhoneAPI::handleToRadio(encoded, length);
#else
        switch (mr->which_payload_variant) {
        case meshtastic_ToRadio_packet_tag: {
            meshtastic_MeshPacket *mp = &mr->packet;
            mp->transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_API;
            printPacket("PACKET FROM QUEUE", mp);
            service->handleToRadio(*mp);
            break;
        }
        case meshtastic_ToRadio_want_config_id_tag: {
            uint32_t config_nonce = mr->want_config_id;
            LOG_INFO("Screen wants config, nonce=%u", config_nonce);
            handleStartConfig();
            break;
        }
        case meshtastic_ToRadio_heartbeat_tag:
            if (mr->heartbeat.nonce == 1) {
                if (nodeInfoModule) {
                    LOG_INFO("Broadcasting nodeinfo ping");
                    nodeInfoModule->sendOurNodeInfo(NODENUM_BROADCAST, true, 0, true);
                }
            } else {
                LOG_DEBUG("Got client heartbeat");
            }
            break;
        default:
            LOG_ERROR("Unhandled meshtastic_ToRadio variant: %d", mr->which_payload_variant);
            break;
        }
#endif
    }
    return data_received;
}

bool PacketAPI::sendPacket(void)
{
#ifdef WADA_MESHTASTIC
    if (!server->available())
        return false;
    if (!pendingSend) {
        if (!getFromRadio(txBuf))
            return false;
        pendingFromRadio = fromRadioScratch;
        pendingSend = true;
    }
    refreshStatus(pendingFromRadio);
    if (!server->sendPacket(DataPacket<meshtastic_FromRadio>(pendingFromRadio.id, pendingFromRadio)))
        return false;
    pendingSend = false;
    return true;
#else
    if (server->available()) {
        // fill dummy buffer; we don't use it, we directly send the fromRadio structure
        uint32_t len = getFromRadio(txBuf);
        if (len != 0) {
            static uint32_t id = 0;
            fromRadioScratch.id = ++id;
            bool result = server->sendPacket(DataPacket<meshtastic_FromRadio>(id, fromRadioScratch));
            if (!result) {
                LOG_ERROR("send queue full");
            }
            return result;
        }
    }
    return false;
#endif
}

bool PacketAPI::notifyProgrammingMode(void)
{
    // tell the client we are in programming mode by sending only the bluetooth config state
    LOG_INFO("force client into programmingMode");
    memset(&fromRadioScratch, 0, sizeof(fromRadioScratch));
    fromRadioScratch.id = nodeDB->getNodeNum();
    fromRadioScratch.which_payload_variant = meshtastic_FromRadio_config_tag;
    fromRadioScratch.config.which_payload_variant = meshtastic_Config_bluetooth_tag;
    fromRadioScratch.config.payload_variant.bluetooth = config.bluetooth;
    return server->sendPacket(DataPacket<meshtastic_FromRadio>(0, fromRadioScratch));
}

/**
 * return true if we got (once!) contact from our client and the server send queue is not full
 */
bool PacketAPI::checkIsConnected()
{
    isConnected |= server->hasData();
    return isConnected;
}

#ifdef WADA_MESHTASTIC
void PacketAPI::refreshStatus(meshtastic_FromRadio &event)
{
    if (event.which_payload_variant == meshtastic_FromRadio_moduleConfig_tag &&
        event.moduleConfig.which_payload_variant == meshtastic_ModuleConfig_statusmessage_tag) {
#if !MESHTASTIC_EXCLUDE_STATUS
        if (statusMessageModule)
            snprintf(event.moduleConfig.payload_variant.statusmessage.node_status,
                     sizeof(event.moduleConfig.payload_variant.statusmessage.node_status), "%s",
                     statusMessageModule->currentStatus());
#endif
    } else if (event.which_payload_variant == meshtastic_FromRadio_packet_tag &&
               event.packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
               event.packet.decoded.portnum == meshtastic_PortNum_NODE_STATUS_APP) {
        meshtastic_StatusMessage status{};
        // Cached replay and live packets can wait behind a newer clear or node deletion.
        if (nodeDB->getMeshNode(event.packet.from))
            nodeDB->copyNodeStatus(event.packet.from, status);
        event.packet.decoded.payload.size =
            pb_encode_to_bytes(event.packet.decoded.payload.bytes, sizeof(event.packet.decoded.payload.bytes),
                               meshtastic_StatusMessage_fields, &status);
    }
}

void PacketAPI::publish(const meshtastic_FromRadio &message)
{
    if (xQueueSend(receiveQueue, &message, 0) != pdTRUE)
        droppedPackets.fetch_add(1, std::memory_order_relaxed);
}

void PacketAPI::publishPacket(const meshtastic_MeshPacket &packet)
{
    meshtastic_FromRadio event = {};
    if (const auto *node = nodeDB->getMeshNode(packet.from)) {
        event.which_payload_variant = meshtastic_FromRadio_node_info_tag;
        event.id = wada::NodeDelta;
        event.node_info = TypeConversions::ConvertToNodeInfoThin(node);
        publish(event);
        event = {};
    }
    event.which_payload_variant = meshtastic_FromRadio_packet_tag;
    event.packet = packet;
    publish(event);
}

void PacketAPI::publishQueueStatus(const meshtastic_QueueStatus &status)
{
    meshtastic_FromRadio event = {};
    event.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
    event.queueStatus = status;
    publish(event);
}

void PacketAPI::publishNotification(const meshtastic_ClientNotification &notification)
{
    meshtastic_FromRadio event = {};
    event.which_payload_variant = meshtastic_FromRadio_clientNotification_tag;
    event.clientNotification = notification;
    publish(event);
}

bool PacketAPI::readPrivateReceiveQueue(meshtastic_FromRadio &out, bool consume)
{
    if (!consume)
        return droppedPackets.load() || uxQueueMessagesWaiting(receiveQueue);
    if (const auto dropped = droppedPackets.exchange(0)) {
        out = {};
        out.which_payload_variant = meshtastic_FromRadio_clientNotification_tag;
        out.clientNotification.level = meshtastic_LogRecord_Level_WARNING;
        snprintf(out.clientNotification.message, sizeof(out.clientNotification.message),
                 "WADAMESH receive overflow: %u dropped; resync required", dropped);
        return true;
    }
    if (xQueueReceive(receiveQueue, &out, 0) != pdTRUE)
        return false;
    if (out.which_payload_variant == meshtastic_FromRadio_node_info_tag &&
        (out.id == wada::NodeDelta || out.id == wada::NodeGone)) {
        // Refresh at the native handoff so a queued delta cannot undo a newer config snapshot.
        const uint32_t number = out.node_info.num;
        const auto *node = nodeDB->getMeshNode(number);
        out.node_info = node ? TypeConversions::ConvertToNodeInfoThin(node) : meshtastic_NodeInfo{};
        out.node_info.num = number;
        out.id = node ? wada::NodeDelta : wada::NodeGone;
    }
    return true;
}

void PacketAPI::refreshNodeFacts()
{
    if (nodeFactsReady && !Throttle::hasElapsed(lastNodeFacts, 1000))
        return;
    lastNodeFacts = Time::getMillis();
    for (auto &entry : nodeFacts)
        entry.seen = false;
    uint32_t index = 0;
    for (unsigned budget = 0; budget < 256; ++budget) {
        const auto *node = nodeDB->readNextMeshNode(index);
        if (!node)
            break;
        const auto info = TypeConversions::ConvertToNodeInfoThin(node);
        uint8_t bytes[meshtastic_NodeInfo_size];
        const size_t size = pb_encode_to_bytes(bytes, sizeof bytes, meshtastic_NodeInfo_fields, &info);
        if (!size)
            continue;
        uint32_t hash = 2166136261u;
        for (size_t i = 0; i < size; ++i)
            hash = (hash ^ bytes[i]) * 16777619u;
        NodeFact *slot = nullptr;
        for (auto &entry : nodeFacts) {
            if (entry.node == info.num) {
                slot = &entry;
                break;
            }
            if (!slot && !entry.node)
                slot = &entry;
        }
        if (!slot)
            continue;
        if (nodeFactsReady && (slot->node != info.num || slot->hash != hash)) {
            meshtastic_FromRadio event{};
            event.id = wada::NodeDelta;
            event.which_payload_variant = meshtastic_FromRadio_node_info_tag;
            event.node_info.num = info.num;
            publish(event);
        }
        *slot = {info.num, hash, true};
    }
    for (auto &entry : nodeFacts)
        if (entry.node && !entry.seen) {
            meshtastic_FromRadio event{};
            event.id = wada::NodeGone;
            event.which_payload_variant = meshtastic_FromRadio_node_info_tag;
            event.node_info.num = entry.node;
            publish(event);
            entry = {};
        }
    nodeFactsReady = true;
}
#endif

#endif
