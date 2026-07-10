#include "MeshTypes.h"
#include "TestUtil.h"
#include "mesh/TestPacketInjector.h"
#include <cstring>
#include <pb_encode.h>
#include <unity.h>

static constexpr uint8_t MAGIC[] = {'M', 'T', 'I', '1'};

static meshtastic_MeshPacket makeFixture()
{
    meshtastic_MeshPacket fixture = meshtastic_MeshPacket_init_zero;
    fixture.from = 0x01020304;
    fixture.to = 0x05060708;
    fixture.id = 0x11223344;
    fixture.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    fixture.encrypted.size = 4;
    memset(fixture.encrypted.bytes, 0xA5, fixture.encrypted.size);
    return fixture;
}

static meshtastic_MeshPacket makeControl(const meshtastic_MeshPacket &fixture)
{
    meshtastic_MeshPacket control = meshtastic_MeshPacket_init_zero;
    control.from = 0;
    control.to = 0x05060708;
    control.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    control.decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
    memcpy(control.decoded.payload.bytes, MAGIC, sizeof(MAGIC));
    const size_t encoded =
        pb_encode_to_bytes(control.decoded.payload.bytes + sizeof(MAGIC), sizeof(control.decoded.payload.bytes) - sizeof(MAGIC),
                           &meshtastic_MeshPacket_msg, &fixture);
    TEST_ASSERT_GREATER_THAN(0, encoded);
    control.decoded.payload.size = sizeof(MAGIC) + encoded;
    return control;
}

static TestPacketInjectorResult decode(const meshtastic_MeshPacket &control)
{
    meshtastic_MeshPacket injected = meshtastic_MeshPacket_init_zero;
    return decodeTestPacketInjectorControl(control, injected);
}

void test_accepts_valid_local_control()
{
    const auto fixture = makeFixture();
    const auto control = makeControl(fixture);
    meshtastic_MeshPacket injected = meshtastic_MeshPacket_init_zero;
    TEST_ASSERT_EQUAL(static_cast<int>(TestPacketInjectorResult::ACCEPTED),
                      static_cast<int>(decodeTestPacketInjectorControl(control, injected)));
    TEST_ASSERT_EQUAL_UINT32(fixture.from, injected.from);
    TEST_ASSERT_EQUAL_UINT32(fixture.to, injected.to);
    TEST_ASSERT_EQUAL_UINT32(fixture.id, injected.id);
    TEST_ASSERT_EQUAL(0, injected.hop_limit);
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_encrypted_tag, injected.which_payload_variant);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fixture.encrypted.bytes, injected.encrypted.bytes, fixture.encrypted.size);
}

void test_ignores_non_controls()
{
    meshtastic_MeshPacket control = makeControl(makeFixture());
    control.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    TEST_ASSERT_EQUAL(static_cast<int>(TestPacketInjectorResult::NOT_CONTROL), static_cast<int>(decode(control)));

    control = makeControl(makeFixture());
    control.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    TEST_ASSERT_EQUAL(static_cast<int>(TestPacketInjectorResult::NOT_CONTROL), static_cast<int>(decode(control)));

    control = makeControl(makeFixture());
    control.decoded.payload.bytes[0] = 'X';
    TEST_ASSERT_EQUAL(static_cast<int>(TestPacketInjectorResult::NOT_CONTROL), static_cast<int>(decode(control)));
}

void test_rejects_invalid_control_encoding()
{
    meshtastic_MeshPacket control = makeControl(makeFixture());
    control.from = 0x01020304;
    TEST_ASSERT_EQUAL(static_cast<int>(TestPacketInjectorResult::REJECTED), static_cast<int>(decode(control)));

    control = makeControl(makeFixture());
    control.decoded.payload.size = sizeof(MAGIC);
    TEST_ASSERT_EQUAL(static_cast<int>(TestPacketInjectorResult::REJECTED), static_cast<int>(decode(control)));

    control.decoded.payload.size++;
    control.decoded.payload.bytes[sizeof(MAGIC)] = 0xFF;
    TEST_ASSERT_EQUAL(static_cast<int>(TestPacketInjectorResult::REJECTED), static_cast<int>(decode(control)));
}

void test_rejects_each_unsafe_fixture_shape()
{
    meshtastic_MeshPacket fixture = makeFixture();
    fixture.which_payload_variant = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(TestPacketInjectorResult::REJECTED), static_cast<int>(decode(makeControl(fixture))));

    fixture = makeFixture();
    fixture.from = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(TestPacketInjectorResult::REJECTED), static_cast<int>(decode(makeControl(fixture))));

    fixture = makeFixture();
    fixture.to = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(TestPacketInjectorResult::REJECTED), static_cast<int>(decode(makeControl(fixture))));

    fixture = makeFixture();
    fixture.id = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(TestPacketInjectorResult::REJECTED), static_cast<int>(decode(makeControl(fixture))));

    fixture = makeFixture();
    fixture.hop_limit = 1;
    TEST_ASSERT_EQUAL(static_cast<int>(TestPacketInjectorResult::REJECTED), static_cast<int>(decode(makeControl(fixture))));

    fixture = makeFixture();
    fixture.encrypted.size = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(TestPacketInjectorResult::REJECTED), static_cast<int>(decode(makeControl(fixture))));
}

void setUp() {}
void tearDown() {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_accepts_valid_local_control);
    RUN_TEST(test_ignores_non_controls);
    RUN_TEST(test_rejects_invalid_control_encoding);
    RUN_TEST(test_rejects_each_unsafe_fixture_shape);
    exit(UNITY_END());
}

void loop() {}
