#include "WadaStatus.h"
#include "WadaMeshtasticBackend.h"
#include <cstring>

const char *WadaMeshtasticBackend::nodeStatus(uint32_t number) const
{
    for (const auto &record : statuses)
        if (record.node == number && number)
            return record.text;
    return "";
}
bool WadaMeshtasticBackend::canEditStatus() const
{
    return self && configured && !snapshotActive && client.isConnected() && !managed && metadataKnown &&
           !(excludedModules & meshtastic_ExcludedModules_STATUSMESSAGE_CONFIG) && wada::firmwareAtLeast(metadataVersion, 2, 8);
}
bool WadaMeshtasticBackend::setStatusMessage(const char *text)
{
    static_assert(statusMaxBytes() <= 80 && sizeof(meshtastic_StatusMessage::status) > statusMaxBytes(),
                  "Status schema capacity");
    if (!canEditStatus() || !wada::statusText(text, statusMaxBytes(), true))
        return false;
    meshtastic_AdminMessage admin{};
    admin.which_payload_variant = meshtastic_AdminMessage_set_module_config_tag;
    admin.set_module_config.which_payload_variant = meshtastic_ModuleConfig_statusmessage_tag;
    memcpy(admin.set_module_config.payload_variant.statusmessage.node_status, text, strlen(text) + 1);
    return sendAdmin(admin);
}
void WadaMeshtasticBackend::onStatus(uint32_t number, const char *text)
{
    if (!number || number == UINT32_MAX || !wada::statusText(text, statusMaxBytes(), false))
        return;
    StatusRecord *free = nullptr;
    for (auto &record : statuses) {
        if (record.node == number) {
            record.seenInSnapshot |= snapshotActive;
            if (strcmp(record.text, text)) {
                memcpy(record.text, text, strlen(text) + 1);
                ++nodesRevision;
            }
            return;
        }
        if (!free && !record.node)
            free = &record;
    }
    if (!text[0] || !free || !findNode(number))
        return;
    free->node = number;
    memcpy(free->text, text, strlen(text) + 1);
    free->seenInSnapshot = snapshotActive;
    ++nodesRevision;
}
void WadaMeshtasticBackend::finishStatusSnapshot()
{
    for (auto &record : statuses)
        if (record.node && (!nodeInfo(record.node) || (!record.seenInSnapshot && (record.node != self || !statusConfigKnown)))) {
            record = {};
            ++nodesRevision;
        }
}
