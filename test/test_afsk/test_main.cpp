// test/test_afsk/test_main.cpp
#include "transport/AX25UIFramer.h"
#include <cstring>
#include <iostream>
#include <unity.h>
#include <vector>

// Helper to convert binary string (e.g. "0110") to byte array
void hexToBytes(const char *hex, uint8_t *out)
{
    size_t len = strlen(hex);
    for (size_t i = 0; i < len; i += 2) {
        char byteString[3] = {hex[i], hex[i + 1], '\0'};
        out[i / 2] = (uint8_t)strtol(byteString, NULL, 16);
    }
}

void test_ax25_callsign_encoding()
{
    // We can't easily test private static methods, but we can test the effect via encode.
    // However, for unit testing, it's better to verify address encoding if we can.
    // Since AX25UIFramer::encodeAddress is private, we will test it via a public round-trip.

    AX25UIFramer framer("NOCALL", 15);
    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t out[512];
    size_t len = framer.encode(payload, sizeof(payload), out, sizeof(out));

    TEST_ASSERT_TRUE(len > 20); // Flag(12) + Dest(7) + Src(7) + CTRL(1) + PID(1) + Payload(4) + CRC(2) + Tail(2)
}

void test_hdlc_bit_padding()
{
    // Test that five 1s are followed by a 0 in the bitstream.
    AX25UIFramer framer("TEST", 0);
    // Payload with many 1s: 0xFF is 8 ones.
    uint8_t payload[] = {0xFF, 0xFF};
    uint8_t out[512];
    size_t len = framer.encode(payload, sizeof(payload), out, sizeof(out));

    // We verify by decoding it back.
    static std::vector<uint8_t> received;
    received.clear();
    framer.onFrame = [](const uint8_t *data, size_t len) {
        for (size_t i = 0; i < len; i++)
            received.push_back(data[i]);
    };

    // Feed the encoded frame back bit by bit (or byte by byte if handleable)
    // feed() takes raw bits packed into bytes (NRZI decoded).
    // encode() output IS already bit-stuffed bytes.
    framer.feed(out, len);

    TEST_ASSERT_EQUAL_INT(2, received.size());
    TEST_ASSERT_EQUAL_HEX8(0xFF, received[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, received[1]);
}

void test_crc_ccitt_correctness()
{
    // Test vector from X.25 spec or similar
    // "123456789" -> 0x29B1 (or 0xE5CC depending on endianness/init)
    // AX.25 uses 0xFFFF init, bit-reversed.

    // We'll test a full round trip with a known payload and verify no CRC error (frame delivered)
    AX25UIFramer framer("NOCALL", 0);
    uint8_t payload[] = {'A', 'B', 'C', 'D'};
    uint8_t out[512];
    size_t len = framer.encode(payload, sizeof(payload), out, sizeof(out));

    bool called = false;
    framer.onFrame = [&](const uint8_t *data, size_t len) {
        called = true;
        TEST_ASSERT_EQUAL_INT(4, len);
        TEST_ASSERT_EQUAL_INT(0, memcmp(payload, data, 4));
    };

    framer.feed(out, len);
    TEST_ASSERT_TRUE(called);
}

void test_oversized_packet_rejection()
{
    AX25UIFramer framer("NOCALL", 0);
    uint8_t largePayload[300];
    memset(largePayload, 0x42, sizeof(largePayload));

    uint8_t out[1024];
    size_t len = framer.encode(largePayload, sizeof(largePayload), out, sizeof(out));

    // encode should return 0 for oversized packets (> 256 bytes info field)
    TEST_ASSERT_EQUAL_INT(0, len);
}

void test_multiple_frames_in_stream()
{
    AX25UIFramer framer("SRC", 1);
    uint8_t p1[] = {0x11, 0x22};
    uint8_t p2[] = {0x33, 0x44};

    uint8_t stream[1024];
    size_t l1 = framer.encode(p1, sizeof(p1), stream, 512);
    size_t l2 = framer.encode(p2, sizeof(p2), stream + l1, 512);

    int count = 0;
    framer.onFrame = [&](const uint8_t *data, size_t len) {
        count++;
        if (count == 1) {
            TEST_ASSERT_EQUAL_INT(2, len);
            TEST_ASSERT_EQUAL_HEX8(0x11, data[0]);
        } else if (count == 2) {
            TEST_ASSERT_EQUAL_INT(2, len);
            TEST_ASSERT_EQUAL_HEX8(0x33, data[0]);
        }
    };

    framer.feed(stream, l1 + l2);
    TEST_ASSERT_EQUAL_INT(2, count);
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_ax25_callsign_encoding);
    RUN_TEST(test_hdlc_bit_padding);
    RUN_TEST(test_crc_ccitt_correctness);
    RUN_TEST(test_oversized_packet_rejection);
    RUN_TEST(test_multiple_frames_in_stream);
    return UNITY_END();
}
