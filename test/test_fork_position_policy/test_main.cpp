// PositionPrecision.cpp must retain the user's configured precision on publicly readable channels.
// The serialized packet assertions catch a restored transmit-time cap; precision zero must still
// strip coordinates so installing this fork never enables position sharing implicitly.
#include "Channels.h"
#include "NodeDB.h"
#include "PositionPrecision.h"
#include "TestUtil.h"
#include "mesh-pb-constants.h"
#include <unity.h>

static meshtastic_ChannelFile savedChannels;

void setUp(void)
{
    savedChannels = channelFile;
}
void tearDown(void)
{
    channelFile = savedChannels;
    channels.onConfigChanged();
}

static void test_public_channel_keeps_configured_coordinates()
{
    channels.initDefaults();
    auto &ch = channels.getByIndex(0);
    ch.settings.has_module_settings = true;
    for (unsigned key : {0U, 1U, 255U}) {
        ch.settings.psk.size = 1;
        ch.settings.psk.bytes[0] = key;
        for (unsigned bits : {0U, 15U, 16U, 19U, 32U}) {
            ch.settings.module_settings.position_precision = bits;
            TEST_ASSERT_EQUAL_UINT32(bits, getPositionPrecisionForChannel(uint8_t{0}));
            meshtastic_Position position = meshtastic_Position_init_default;
            position.has_latitude_i = position.has_longitude_i = true;
            position.latitude_i = 374221234;
            position.longitude_i = -1220845678;
            meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_default;
            packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            packet.decoded.portnum = meshtastic_PortNum_POSITION_APP;
            packet.decoded.payload.size = pb_encode_to_bytes(packet.decoded.payload.bytes, sizeof(packet.decoded.payload.bytes),
                                                             &meshtastic_Position_msg, &position);
            TEST_ASSERT_TRUE(applyPositionPrecisionForChannel(packet, 0));
            meshtastic_Position decoded = meshtastic_Position_init_default;
            TEST_ASSERT_TRUE(pb_decode_from_bytes(packet.decoded.payload.bytes, packet.decoded.payload.size,
                                                  &meshtastic_Position_msg, &decoded));
            if (bits == 0) {
                TEST_ASSERT_FALSE(decoded.has_latitude_i);
                TEST_ASSERT_FALSE(decoded.has_longitude_i);
            } else if (bits == 32) {
                TEST_ASSERT_EQUAL_INT32(position.latitude_i, decoded.latitude_i);
                TEST_ASSERT_EQUAL_INT32(position.longitude_i, decoded.longitude_i);
                TEST_ASSERT_EQUAL_UINT32(32, decoded.precision_bits);
            }
        }
    }
}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_public_channel_keeps_configured_coordinates);
    exit(UNITY_END());
}
void loop() {}
}
