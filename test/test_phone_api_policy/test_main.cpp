#include "PhoneAPI.h"
#include "TestUtil.h"
#include <unity.h>

static meshtastic_MeshPacket makePacket(meshtastic_PortNum port, NodeNum to)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded.portnum = port;
    packet.to = to;
    return packet;
}

static void test_textMessageFromPhone_usesReliableDelivery(void)
{
    meshtastic_MeshPacket packet = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, NODENUM_BROADCAST);
    TEST_ASSERT_TRUE(PhoneAPI::shouldUseReliableDeliveryForPhonePacket(packet));
}

static void test_compressedTextMessageFromPhone_usesReliableDelivery(void)
{
    meshtastic_MeshPacket packet = makePacket(meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP, 0x12345678);
    TEST_ASSERT_TRUE(PhoneAPI::shouldUseReliableDeliveryForPhonePacket(packet));
}

static void test_directTracerouteFromPhone_usesReliableDelivery(void)
{
    meshtastic_MeshPacket packet = makePacket(meshtastic_PortNum_TRACEROUTE_APP, 0x12345678);
    TEST_ASSERT_TRUE(PhoneAPI::shouldUseReliableDeliveryForPhonePacket(packet));
}

static void test_broadcastTracerouteFromPhone_doesNotUseReliableDelivery(void)
{
    meshtastic_MeshPacket packet = makePacket(meshtastic_PortNum_TRACEROUTE_APP, NODENUM_BROADCAST);
    TEST_ASSERT_FALSE(PhoneAPI::shouldUseReliableDeliveryForPhonePacket(packet));
}

static void test_telemetryFromPhone_doesNotUseReliableDelivery(void)
{
    meshtastic_MeshPacket packet = makePacket(meshtastic_PortNum_TELEMETRY_APP, NODENUM_BROADCAST);
    TEST_ASSERT_FALSE(PhoneAPI::shouldUseReliableDeliveryForPhonePacket(packet));
}

void setup()
{
    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_textMessageFromPhone_usesReliableDelivery);
    RUN_TEST(test_compressedTextMessageFromPhone_usesReliableDelivery);
    RUN_TEST(test_directTracerouteFromPhone_usesReliableDelivery);
    RUN_TEST(test_broadcastTracerouteFromPhone_doesNotUseReliableDelivery);
    RUN_TEST(test_telemetryFromPhone_doesNotUseReliableDelivery);
    exit(UNITY_END());
}

void loop() {}
