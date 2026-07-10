#pragma once

#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include <cstring>
#include <pb_decode.h>

enum class TestPacketInjectorResult { NOT_CONTROL, REJECTED, ACCEPTED };

inline TestPacketInjectorResult decodeTestPacketInjectorControl(const meshtastic_MeshPacket &control,
                                                                meshtastic_MeshPacket &injected)
{
    static constexpr uint8_t magic[] = {'M', 'T', 'I', '1'};
    if (control.which_payload_variant != meshtastic_MeshPacket_decoded_tag ||
        control.decoded.portnum != meshtastic_PortNum_PRIVATE_APP || control.decoded.payload.size < sizeof(magic) ||
        memcmp(control.decoded.payload.bytes, magic, sizeof(magic)) != 0) {
        return TestPacketInjectorResult::NOT_CONTROL;
    }

    if (control.from != 0 || control.decoded.payload.size == sizeof(magic))
        return TestPacketInjectorResult::REJECTED;

    injected = meshtastic_MeshPacket_init_zero;
    const auto encodedLength = control.decoded.payload.size - sizeof(magic);
    if (!pb_decode_from_bytes(control.decoded.payload.bytes + sizeof(magic), encodedLength, &meshtastic_MeshPacket_msg,
                              &injected)) {
        return TestPacketInjectorResult::REJECTED;
    }

    if (injected.which_payload_variant != meshtastic_MeshPacket_encrypted_tag || injected.from == 0 || injected.to == 0 ||
        injected.id == 0 || injected.hop_limit != 0 || injected.encrypted.size == 0) {
        return TestPacketInjectorResult::REJECTED;
    }
    return TestPacketInjectorResult::ACCEPTED;
}
