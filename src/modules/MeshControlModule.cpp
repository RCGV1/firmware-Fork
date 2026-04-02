#include "MeshControlModule.h"
#include "Channels.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "RadioInterface.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"
#include "mesh/CryptoEngine.h"
#include "mesh/generated/meshtastic/portnums.pb.h"
#include <pb_encode.h>
#include <string.h>

MeshControlModule *meshControlModule;

static bool meshControlPositionBroadcastUsesDefaultChannel()
{
    for (uint8_t i = 0; i < channels.getNumChannels(); i++) {
        const auto &channel = channels.getByIndex(i);
        if (channel.settings.has_module_settings && channel.settings.module_settings.position_precision != 0) {
            return channels.isDefaultChannel(i);
        }
    }

    return false;
}

static void clampMeshControlBeaconIntervals()
{
    if (channels.hasDefaultChannel()) {
        moduleConfig.telemetry.device_update_interval = Default::getConfiguredOrMinimumValue(
            moduleConfig.telemetry.device_update_interval, min_default_telemetry_interval_secs);
    }

    if (meshControlPositionBroadcastUsesDefaultChannel()) {
        config.position.position_broadcast_secs =
            Default::getConfiguredOrMinimumValue(config.position.position_broadcast_secs, min_default_broadcast_interval_secs);
    }

    config.device.node_info_broadcast_secs =
        Default::getConfiguredOrMinimumValue(config.device.node_info_broadcast_secs, min_node_info_broadcast_secs);
}

MeshControlModule::MeshControlModule()
    : ProtobufModule("mesh_control", meshtastic_PortNum_MESH_CONTROL_APP, &meshtastic_MeshControlPacket_msg),
      OSThread("MeshControl")
{
    isPromiscuous = true; // See all packets on this portnum
    disable();            // Idle until a packet arrives (runOnce is driven by pending activation)
}

// ---------------------------------------------------------------------------
// HMAC-SHA256 using two calls to CryptoEngine::hash()
// ---------------------------------------------------------------------------
void MeshControlModule::hmacSha256(const uint8_t key[32], const uint8_t *msg, size_t msgLen, uint8_t out[32])
{
    // HMAC-SHA256 block size for SHA-256 is 64 bytes
    constexpr size_t BLOCK = 64;
    constexpr size_t MAX_MSG = 256;

    // Use static buffers to avoid ~400 bytes of stack pressure on constrained nRF52 threads.
    // Meshtastic packet processing is single-threaded so this is safe.
    static uint8_t ipad[BLOCK], opad[BLOCK];
    memset(ipad, 0x36, BLOCK);
    memset(opad, 0x5C, BLOCK);
    // XOR the key into the pads
    for (size_t i = 0; i < 32; i++) {
        ipad[i] ^= key[i];
        opad[i] ^= key[i];
    }

    // inner = SHA256(ipad || msg)
    if (msgLen > MAX_MSG)
        msgLen = MAX_MSG;
    static uint8_t inner_buf[BLOCK + MAX_MSG];
    memcpy(inner_buf, ipad, BLOCK);
    memcpy(inner_buf + BLOCK, msg, msgLen);
    crypto->hash(inner_buf, BLOCK + msgLen); // first 32 bytes of inner_buf become the hash

    // outer = SHA256(opad || inner_hash[0:32])
    static uint8_t outer_buf[BLOCK + 32];
    memcpy(outer_buf, opad, BLOCK);
    memcpy(outer_buf + BLOCK, inner_buf, 32); // inner hash
    crypto->hash(outer_buf, BLOCK + 32);

    memcpy(out, outer_buf, 32);
}

// ---------------------------------------------------------------------------
// Serialize all packet fields EXCEPT the hmac field for HMAC computation.
// We encode into a scratch MeshControlPacket with hmac zeroed out.
// ---------------------------------------------------------------------------
size_t MeshControlModule::serializeForHmac(const meshtastic_MeshControlPacket &pkt, uint8_t *buf, size_t bufLen)
{
    meshtastic_MeshControlPacket scratch = pkt;
    // Zero out hmac so the same bytes are produced when the controller signs the packet
    memset(scratch.hmac.bytes, 0, sizeof(scratch.hmac.bytes));
    scratch.hmac.size = 0;

    pb_ostream_t stream = pb_ostream_from_buffer(buf, bufLen);
    if (!pb_encode(&stream, &meshtastic_MeshControlPacket_msg, &scratch))
        return 0;
    return stream.bytes_written;
}

// ---------------------------------------------------------------------------
bool MeshControlModule::verifyHmac(const meshtastic_MeshControlPacket &pkt) const
{
    const auto &mc = config.mesh_control;
    if (mc.control_key.size != 32)
        return false;

    // Static buffer avoids ~280 bytes of stack pressure. Safe because packet processing is single-threaded.
    static uint8_t canonical[280];
    size_t canonLen = serializeForHmac(pkt, canonical, sizeof(canonical));
    if (canonLen == 0)
        return false;

    static uint8_t computed[32];
    hmacSha256(mc.control_key.bytes, canonical, canonLen, computed);

    // Compare first 16 bytes of HMAC with the packet's hmac field
    if (pkt.hmac.size != 16)
        return false;
    return memcmp(computed, pkt.hmac.bytes, 16) == 0;
}

// ---------------------------------------------------------------------------
bool MeshControlModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_MeshControlPacket *p)
{
    const auto &mc = config.mesh_control;

    // 1. Must be opted in
    if (mc.accept_policy == meshtastic_Config_MeshControlConfig_AcceptPolicy_DISABLED) {
        LOG_DEBUG("MeshControl: disabled, ignoring packet");
        return false;
    }

    // 2. Must have a control key configured
    if (mc.control_key.size != 32) {
        LOG_DEBUG("MeshControl: no control key configured, ignoring");
        return false;
    }

    // 3. Check destination
    if (p->dest_node != 0 && p->dest_node != nodeDB->getNodeNum()) {
        LOG_DEBUG("MeshControl: packet not for us (dest=0x%08x)", p->dest_node);
        return false;
    }

    // 4. Verify HMAC
    if (!verifyHmac(*p)) {
        LOG_WARN("MeshControl: HMAC verification failed – dropping packet");
        return false;
    }

    // 5. Replay protection: seq_num must be non-zero and strictly greater than the last accepted.
    // seq_num == 0 is rejected unconditionally — it cannot be used to bypass replay protection.
    if (p->seq_num == 0) {
        LOG_WARN("MeshControl: seq_num=0 not permitted – dropping");
        return false;
    }
    if (p->seq_num <= devicestate.last_accepted_mesh_control_seq_num) {
        LOG_WARN("MeshControl: stale seq_num %u (last accepted %u) – dropping", p->seq_num,
                 devicestate.last_accepted_mesh_control_seq_num);
        return false;
    }

    // 6. Rate-limit: enforce min_interval_secs between accepted packets
    static uint32_t lastAcceptedMs = 0;
    uint32_t minIntervalMs = (mc.min_interval_secs ? mc.min_interval_secs : 60) * 1000UL;
    if (lastAcceptedMs && (millis() - lastAcceptedMs) < minIntervalMs) {
        LOG_WARN("MeshControl: rate-limited, ignoring packet");
        return false;
    }

    if (!p->has_settings) {
        LOG_DEBUG("MeshControl: packet has no settings payload");
        return false;
    }

    LOG_INFO("MeshControl: authenticated packet from 0x%08x (seq=%u, mesh='%s')", mp.from, p->seq_num, p->mesh_name);

    if (mc.accept_policy == meshtastic_Config_MeshControlConfig_AcceptPolicy_PROMPT) {
        // If a previous PROMPT is still awaiting user approval, reject the new packet to avoid
        // the user unknowingly approving settings that were silently swapped out.
        if (pendingSeqNum != 0) {
            LOG_WARN("MeshControl: PROMPT already pending (seq=%u), rejecting new packet (seq=%u)", pendingSeqNum, p->seq_num);
            return false;
        }
        pendingSettings = p->settings;
        pendingSeqNum = p->seq_num;
        pendingActivation = false; // PROMPT mode doesn't use the timer path
        devicestate.last_accepted_mesh_control_seq_num = p->seq_num;
        nodeDB->saveToDisk(SEGMENT_DEVICESTATE);
        lastAcceptedMs = millis();
        sendApprovalPrompt(*p);
        return false;
    }

    // AUTO mode: cancel any stale deferred activation before applying or scheduling new settings.
    // Without this, an old delayed packet would fire after the new one and revert the changes.
    if (pendingActivation) {
        LOG_INFO("MeshControl: cancelling pending deferred activation (seq=%u) for new packet (seq=%u)", pendingSeqNum,
                 p->seq_num);
        pendingActivation = false;
        pendingSettings = meshtastic_MeshControlSettings_init_zero;
        pendingSeqNum = 0;
        disable();
    }

    if (p->activation_delay_secs > 0) {
        LOG_INFO("MeshControl: will apply settings in %u s", p->activation_delay_secs);
        pendingSettings = p->settings;
        pendingSeqNum = p->seq_num;
        pendingActivation = true;
        devicestate.last_accepted_mesh_control_seq_num = p->seq_num;
        nodeDB->saveToDisk(SEGMENT_DEVICESTATE);
        lastAcceptedMs = millis();
        // Cap delay to avoid uint32_t overflow: activation_delay_secs * 1000 wraps above ~49.7 days.
        uint32_t delayMs = (uint32_t)min((uint64_t)p->activation_delay_secs * 1000ULL, (uint64_t)UINT32_MAX);
        activateAtMs = millis() + delayMs;
        setIntervalFromNow(delayMs);
        enabled = true;
    } else {
        devicestate.last_accepted_mesh_control_seq_num = p->seq_num;
        nodeDB->saveToDisk(SEGMENT_DEVICESTATE);
        lastAcceptedMs = millis();
        applySettings(p->settings);
    }

    return false;
}

// ---------------------------------------------------------------------------
int32_t MeshControlModule::runOnce()
{
    if (pendingActivation && millis() >= activateAtMs) {
        LOG_INFO("MeshControl: applying deferred settings (seq=%u)", pendingSeqNum);
        pendingActivation = false;
        pendingSeqNum = 0;
        applySettings(pendingSettings);
        pendingSettings = meshtastic_MeshControlSettings_init_zero;
        disable();
    }
    return INT32_MAX;
}

// ---------------------------------------------------------------------------
void MeshControlModule::applySettings(const meshtastic_MeshControlSettings &s)
{
    const auto &mc = config.mesh_control;
    bool loraChanged = false;
    bool intervalChanged = false;

    // --- LoRa settings ---
    if (mc.allow_lora_config) {
        if (s.has_modem_preset || s.has_override_frequency || s.has_channel_num) {
            // Apply to a scratch copy so we can validate before committing
            auto candidateLora = config.lora;
            if (s.has_modem_preset) {
                candidateLora.modem_preset = s.modem_preset;
                candidateLora.use_preset = true;
            }
            if (s.has_override_frequency)
                candidateLora.override_frequency = s.override_frequency;
            if (s.has_channel_num)
                candidateLora.channel_num = s.channel_num;

            // Clamp/validate — rejects invalid presets, illegal frequencies, etc.
            if (!RadioInterface::validateConfigLora(candidateLora)) {
                LOG_WARN("MeshControl: LoRa config invalid after applying settings, clamping");
                RadioInterface::clampConfigLora(candidateLora);
            }
            config.lora = candidateLora;
            loraChanged = true;
            LOG_INFO("MeshControl: applied LoRa settings (preset=%d freq=%.3f ch=%u)", config.lora.modem_preset,
                     config.lora.override_frequency, config.lora.channel_num);
        }
    }

    // --- Hop limits ---
    if (mc.allow_hop_limits) {
        if (s.has_hop_limit && s.hop_limit > 0) {
            config.lora.hop_limit = (s.hop_limit > HOP_MAX ? HOP_MAX : s.hop_limit);
            loraChanged = true;
            LOG_INFO("MeshControl: set hop_limit=%u", config.lora.hop_limit);
        }
        if (s.has_broadcast_hop_limit && s.broadcast_hop_limit > 0) {
            config.lora.broadcast_hop_limit = (s.broadcast_hop_limit > HOP_MAX ? HOP_MAX : s.broadcast_hop_limit);
            loraChanged = true;
            LOG_INFO("MeshControl: set broadcast_hop_limit=%u", config.lora.broadcast_hop_limit);
        }
    }

    // --- Position interval ---
    if (mc.allow_position_interval && s.has_position_broadcast_secs && s.position_broadcast_secs > 0) {
        config.position.position_broadcast_secs = s.position_broadcast_secs;
        intervalChanged = true;
        LOG_INFO("MeshControl: set position_broadcast_secs=%u", s.position_broadcast_secs);
    }

    // --- Telemetry interval ---
    if (mc.allow_telemetry_interval && s.has_device_telemetry_interval && s.device_telemetry_interval > 0) {
        moduleConfig.telemetry.device_update_interval = s.device_telemetry_interval;
        intervalChanged = true;
        LOG_INFO("MeshControl: set device_telemetry_interval=%u", s.device_telemetry_interval);
    }

    // --- Node info interval ---
    if (mc.allow_node_info_interval && s.has_node_info_broadcast_secs && s.node_info_broadcast_secs > 0) {
        config.device.node_info_broadcast_secs = s.node_info_broadcast_secs;
        intervalChanged = true;
        LOG_INFO("MeshControl: set node_info_broadcast_secs=%u", s.node_info_broadcast_secs);
    }

    if (intervalChanged) {
        clampMeshControlBeaconIntervals();
    }

    // Persist and (if LoRa changed) trigger a radio reconfiguration
    if (loraChanged || intervalChanged) {
        nodeDB->saveToDisk(SEGMENT_CONFIG | (intervalChanged ? SEGMENT_MODULECONFIG : 0));
    }
    if (loraChanged) {
        // Trigger radio reconfiguration via the config-changed observable
        service->reloadConfig(SEGMENT_CONFIG);
    }

    // Notify the connected app that settings were applied
    meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
    if (cn) {
        cn->level = meshtastic_LogRecord_Level_INFO;
        cn->time = getValidTime(RTCQualityFromNet);
        snprintf(cn->message, sizeof(cn->message), "Mesh Control settings applied");
        service->sendClientNotification(cn);
    }
}

// ---------------------------------------------------------------------------
void MeshControlModule::applyPendingSettings()
{
    if (pendingSeqNum != 0) {
        LOG_INFO("MeshControl: user approved pending settings (seq=%u)", pendingSeqNum);
        applySettings(pendingSettings);
        pendingSettings = meshtastic_MeshControlSettings_init_zero;
        pendingSeqNum = 0;
    } else {
        LOG_DEBUG("MeshControl: applyPendingSettings called but no settings pending");
    }
}

void MeshControlModule::discardPendingSettings()
{
    LOG_INFO("MeshControl: user rejected pending settings (seq=%u)", pendingSeqNum);
    pendingSettings = meshtastic_MeshControlSettings_init_zero;
    pendingSeqNum = 0;
    meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
    if (cn) {
        cn->level = meshtastic_LogRecord_Level_INFO;
        cn->time = getValidTime(RTCQualityFromNet);
        snprintf(cn->message, sizeof(cn->message), "Mesh Control update discarded");
        service->sendClientNotification(cn);
    }
}

// ---------------------------------------------------------------------------
void MeshControlModule::sendApprovalPrompt(const meshtastic_MeshControlPacket &pkt)
{
    meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
    if (!cn)
        return;
    cn->level = meshtastic_LogRecord_Level_WARNING;
    cn->time = getValidTime(RTCQualityFromNet);
    // The app sees this and can show a dialog.  The user then approves by sending
    // an AdminMessage with begin_mesh_control_apply = true.
    snprintf(cn->message, sizeof(cn->message),
             "Mesh Control update received from '%s' (seq=%u). Approve in app to apply settings.", pkt.mesh_name, pkt.seq_num);
    service->sendClientNotification(cn);
    LOG_INFO("MeshControl: approval prompt sent to app for seq=%u", pkt.seq_num);
}
