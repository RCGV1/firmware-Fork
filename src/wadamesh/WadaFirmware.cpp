#ifdef WADA_MESHTASTIC
// Keep native firmware headers in the firmware build environment, outside the UI library.
#include "../../lib/WadaApp/port/WadaChannelIdentity.h"
#include "../../lib/WadaApp/port/WadaChannelIntent.h"
#include "../../lib/WadaApp/port/WadaNativeConfig.h"
#include "../../lib/WadaApp/port/WadaNodeActions.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "PowerStatus.h"
#include "RTC.h"
#include "SPILock.h"
#include "api/PacketAPI.h"
#include "configuration.h"
#include "mesh/Channels.h"
#include "mesh/Default.h"
#include "meshUtils.h"
#include "wadacomms/PacketServer.h"
#include <SPI.h>
#include <atomic>
#include <pb_encode.h>
static_assert(wada::nodeInfoDefaultSeconds == default_node_info_broadcast_secs, "Keep node-info defaults aligned");
static_assert(wada::longNameMaxBytes == MAX_LONG_NAME_BYTES, "Keep name validation aligned with native storage");
namespace
{
std::atomic<bool> uiActivity{false}, phoneConnected{false};
}
extern meshtastic::PowerStatus *powerStatus;
void wadaInitPacketApi()
{
    PacketAPI::create(PacketServer::init());
}
void wadaCommsWarning(const char *message)
{
    LOG_WARN("WADAMESH: %s", message);
}
void wadaSpiLock()
{
    spiLock->lock();
}
void wadaSpiUnlock()
{
    spiLock->unlock();
}
uint16_t wadaBatteryMillivolts()
{
    return powerStatus ? powerStatus->getBatteryVoltageMv() : 0;
}
void wadaInputActivity()
{
    uiActivity.store(true);
}
bool wadaPhoneConnected()
{
    return phoneConnected.load();
}
void wadaPollUiActivity()
{
    if (uiActivity.exchange(false))
        powerFSM.trigger(EVENT_INPUT);
    phoneConnected.store(service && service->api_state != MeshService::STATE_DISCONNECTED);
}
uint32_t wadaRtcNow()
{
    return getValidTime(RTCQualityDevice);
}
void wadaSetRtc(uint32_t seconds)
{
    timeval t = {time_t(seconds), 0};
    perhapsSetRTC(RTCQualityDevice, &t);
}
SPIClass *tdeckSharedSPI()
{
    return &SPI;
}

bool wadaValidateChannelIntent(uint32_t packetId, uint8_t slot)
{
    meshtastic_Channel snapshot[8] = {};
    for (unsigned i = 0; i < 8; ++i)
        snapshot[i] = channels.getByIndex(i);
    uint8_t identity[32];
    const bool valid = wada::channelIdentity(snapshot, config.lora, slot, identity);
    return wada::consumeChannelIntent(packetId, slot, valid ? identity : nullptr);
}

bool wadaProcessNodeActions()
{
    wada::UnheardRemoval request;
    if (!wada::takeUnheardRemoval(request))
        return false;
    using Result = wada::NodeRemovalResult;
    Result result = Result::Unavailable;
    const auto self = nodeDB->getNodeNum();
    const auto *node = nodeDB->getMeshNode(request.node);
    uint8_t expected[meshtastic_Config_LoRaConfig_size], actual[meshtastic_Config_LoRaConfig_size];
    auto a = pb_ostream_from_buffer(expected, sizeof expected);
    auto b = pb_ostream_from_buffer(actual, sizeof actual);
    const auto &primary = channels.getByIndex(channels.getPrimaryIndex());
    const bool same = pb_encode(&a, meshtastic_Config_LoRaConfig_fields, &request.lora) &&
                      pb_encode(&b, meshtastic_Config_LoRaConfig_fields, &config.lora) && a.bytes_written == b.bytes_written &&
                      !memcmp(expected, actual, a.bytes_written) && !strcmp(request.primaryName, primary.settings.name);
    if (request.self != self || !same)
        result = Result::Changed;
    else if (config.security.is_managed)
        result = Result::Managed;
    else if (!request.node || request.node == self || request.node == NODENUM_BROADCAST)
        result = Result::Self;
    else if (!node)
        result = Result::Missing;
    else if (nodeInfoLiteIsFavorite(node))
        result = Result::Favorite;
    else if (nodeInfoLiteViaMqtt(node))
        result = Result::Mqtt;
    else if (nodeInfoLiteHeardOnSlot(node, nodeDB->committedLoraSlot()))
        result = Result::Heard;
    else {
        nodeDB->removeNodeByNum(request.node);
        result = nodeDB->getMeshNode(request.node) ? Result::Unavailable : Result::Removed;
    }
    meshtastic_ClientNotification reply = {};
    reply.has_reply_id = true;
    reply.reply_id = request.id;
    reply.level = meshtastic_LogRecord_Level_INFO;
    snprintf(reply.message, sizeof reply.message, "%s", wada::nodeRemovalReason(result));
    packetAPI->publishNotification(reply);
    return true;
}

#endif
