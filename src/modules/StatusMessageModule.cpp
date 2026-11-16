#if !MESHTASTIC_EXCLUDE_STATUS

#include "StatusMessageModule.h"
#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "ProtobufModule.h"

StatusMessageModule *statusMessageModule;

StatusMessageModule::StatusMessageModule()
    : SinglePortModule("statusMessage", meshtastic_PortNum_NODE_STATUS_APP), concurrency::OSThread("StatusMessage")
{
    if (moduleConfig.has_statusmessage)
        memcpy(committedStatus, moduleConfig.statusmessage.node_status, sizeof(committedStatus));
    committedStatus[sizeof(committedStatus) - 1] = '\0';
    meshtastic_StatusMessage status{};
    memcpy(status.status, committedStatus, sizeof(status.status));
    if (nodeDB)
        nodeDB->setNodeStatus(nodeDB->getNodeNum(), status);
    setInterval(committedStatus[0] ? 2 * 60 * 1000 : 12 * 60 * 60 * 1000);
}

void StatusMessageModule::onConfigChanged()
{
    const char *next = moduleConfig.has_statusmessage ? moduleConfig.statusmessage.node_status : "";
    if (!strcmp(committedStatus, next))
        return;
    memcpy(committedStatus, next, strlen(next) + 1);
    meshtastic_StatusMessage status{};
    memcpy(status.status, committedStatus, sizeof(status.status));
    nodeDB->setNodeStatus(nodeDB->getNodeNum(), status);
    announceChange = true;
    setIntervalFromNow(1000);
}

int32_t StatusMessageModule::runOnce()
{
    if (committedStatus[0] || announceChange) {
        meshtastic_StatusMessage status{};
        memcpy(status.status, committedStatus, sizeof(status.status));
        meshtastic_MeshPacket *p = allocDataPacket();
        if (!p)
            return 60 * 1000;
        p->decoded.payload.size = pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
                                                     meshtastic_StatusMessage_fields, &status);
        p->to = NODENUM_BROADCAST;
        p->decoded.want_response = false;
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        p->channel = channels.getPrimaryIndex();
        const auto result = service->sendToMesh(p);
        if (result != ERRNO_OK && result != ERRNO_SHOULD_RELEASE && announceChange)
            return 60 * 1000;
        announceChange = false;
    }
    return 1000 * 12 * 60 * 60;
}

ProcessMessage StatusMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        meshtastic_StatusMessage incomingMessage = meshtastic_StatusMessage_init_zero;

        if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, meshtastic_StatusMessage_fields,
                                 &incomingMessage)) {

            LOG_INFO("Received a NodeStatus message %s", incomingMessage.status);

            if (nodeDB)
                nodeDB->setNodeStatus(mp.from, incomingMessage);
        }
    }
    return ProcessMessage::CONTINUE;
}

#endif