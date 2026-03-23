#pragma once

#include "ProtobufModule.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/mesh_control.pb.h"

/**
 * MeshControlModule
 *
 * Receives broadcast MeshControlPacket messages from a trusted mesh administrator,
 * verifies their HMAC-SHA256 authentication, and applies the embedded settings
 * according to the node's configured accept policy.
 *
 * Security model:
 *  - Both controller and participating nodes share a 32-byte `control_key`.
 *  - The controller computes HMAC-SHA256(control_key, canonical_packet_bytes) and
 *    embeds the first 16 bytes in the `hmac` field.
 *  - Nodes with a matching control_key verify the HMAC before applying any changes.
 *  - A monotonically increasing `seq_num` (typically a Unix timestamp) prevents replay.
 *
 * Accept policies (Config.MeshControlConfig.accept_policy):
 *  - DISABLED: reject all mesh control packets.
 *  - PROMPT:   send a ClientNotification; the user must approve via the app.
 *  - AUTO:     apply validated settings immediately.
 *
 * Fine-grained allow_* flags further restrict which setting categories can change.
 */
class MeshControlModule : public ProtobufModule<meshtastic_MeshControlPacket>, private concurrency::OSThread
{
  public:
    MeshControlModule();

  protected:
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_MeshControlPacket *p) override;

    virtual int32_t runOnce() override;

  private:
    // Pending activation state (used when activation_delay_secs > 0)
    bool pendingActivation = false;
    uint32_t activateAtMs = 0;
    meshtastic_MeshControlSettings pendingSettings = meshtastic_MeshControlSettings_init_zero;

    /** Compute HMAC-SHA256(key, msg, msgLen) → out[32].
     *  Implemented using two calls to CryptoEngine::hash(). */
    static void hmacSha256(const uint8_t key[32], const uint8_t *msg, size_t msgLen, uint8_t out[32]);

    /** Serialize packet fields (excluding hmac) into buf; return bytes written, or 0 on error. */
    static size_t serializeForHmac(const meshtastic_MeshControlPacket &pkt, uint8_t *buf, size_t bufLen);

    /** Return true if the packet's HMAC is valid for the configured control_key. */
    bool verifyHmac(const meshtastic_MeshControlPacket &pkt) const;

    /** Apply settings that are permitted by the node's allow_* flags. */
    void applySettings(const meshtastic_MeshControlSettings &s);

    /** Send a ClientNotification asking the user to approve/reject pending settings. */
    void sendApprovalPrompt(const meshtastic_MeshControlPacket &pkt);

  public:
    /** Called by AdminModule when the user approves a PROMPT-mode update. */
    void applyPendingSettings();

    /** Called by AdminModule when the user rejects a PROMPT-mode update. */
    void discardPendingSettings();
};

extern MeshControlModule *meshControlModule;
