#include "WadaNodeState.h"
#include <cstring>

namespace
{
uint32_t crc(const uint8_t *bytes, size_t size)
{
    uint32_t value = ~0u;
    for (size_t i = 0; i < size; ++i) {
        value ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1)));
    }
    return ~value;
}
void put(uint8_t *out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        out[i] = value >> (8 * i);
}
uint32_t get(const uint8_t *in)
{
    return uint32_t(in[0]) | uint32_t(in[1]) << 8 | uint32_t(in[2]) << 16 | uint32_t(in[3]) << 24;
}
bool part(const char *&at, unsigned &value)
{
    value = 0;
    if (!at || *at < '0' || *at > '9')
        return false;
    while (*at >= '0' && *at <= '9') {
        if (value > 1000)
            return false;
        value = value * 10 + unsigned(*at++ - '0');
    }
    return true;
}
} // namespace
namespace wada
{
bool firmwareAtLeast(const char *version, unsigned major, unsigned minor, unsigned patch)
{
    unsigned a, b, c = 0;
    const char *at = version;
    if (!part(at, a) || *at++ != '.' || !part(at, b))
        return false;
    if (*at == '.') {
        ++at;
        if (!part(at, c))
            return false;
    }
    if (*at && *at != '.' && *at != '-' && *at != '+')
        return false;
    return a != major ? a > major : b != minor ? b > minor : c >= patch;
}
NodeSecurity resolveNodeSecurity(bool mismatch, bool own, bool verified, bool signedNode, bool hasKey, const char *version)
{
    if (mismatch)
        return NodeSecurity::KeyMismatch;
    if (own || verified)
        return NodeSecurity::VerifiedContact;
    if (signedNode || firmwareAtLeast(version, 2, 8))
        return NodeSecurity::SignedNode;
    return hasKey ? NodeSecurity::PublicKey : NodeSecurity::NoPublicKey;
}
const char *nodeSecurityName(NodeSecurity state)
{
    switch (state) {
    case NodeSecurity::KeyMismatch:
        return "Key mismatch";
    case NodeSecurity::VerifiedContact:
        return "Verified contact";
    case NodeSecurity::SignedNode:
        return "Signed node";
    case NodeSecurity::PublicKey:
        return "Public key";
    case NodeSecurity::NoPublicKey:
        return "No public key";
    }
    return "No public key";
}
const char *nodeSecurityHelp(NodeSecurity state)
{
    switch (state) {
    case NodeSecurity::KeyMismatch:
        return "A different public key was heard. The stored key was kept. Verify the contact using a Meshtastic client before "
               "messaging it.";
    case NodeSecurity::VerifiedContact:
        return "The key was verified in person, or this is your connected radio.";
    case NodeSecurity::SignedNode:
        return "The radio verified a signed broadcast, or the node reports firmware 2.8 or newer.";
    case NodeSecurity::PublicKey:
        return "A public key is on file. This permits encrypted interactions but does not verify the node's identity.";
    case NodeSecurity::NoPublicKey:
        return "No public key is on file. The node may not be messageable; there is no fallback to channel encryption.";
    }
    return "";
}
bool NodeState::mismatch(uint32_t node) const
{
    for (const auto &entry : entries)
        if (entry.node == node && node)
            return true;
    return false;
}
bool NodeState::markMismatch(uint32_t node, const uint8_t trusted[32])
{
    if (!node || node == UINT32_MAX || !trusted)
        return false;
    uint8_t any = 0;
    for (unsigned i = 0; i < 32; ++i)
        any |= trusted[i];
    if (!any)
        return false;
    for (auto &entry : entries)
        if (entry.node == node)
            return true;
    for (auto &entry : entries)
        if (!entry.node) {
            entry.node = node;
            memcpy(entry.trusted, trusted, 32);
            return true;
        }
    return false;
}
bool NodeState::acceptedKey(uint32_t node, const uint8_t *key, bool own)
{
    uint8_t any = 0;
    if (key)
        for (unsigned i = 0; i < 32; ++i)
            any |= key[i];
    if (!any)
        key = nullptr;
    for (auto &entry : entries)
        if (entry.node == node && node && (own || (key && memcmp(entry.trusted, key, 32)))) {
            entry = {};
            return true;
        }
    return false;
}
bool NodeState::forget(uint32_t node)
{
    for (auto &entry : entries)
        if (entry.node == node && node) {
            entry = {};
            return true;
        }
    return false;
}
size_t NodeState::encode(uint8_t *out, size_t cap, uint32_t self, uint32_t generation) const
{
    if (cap < 20 || !self || self == UINT32_MAX)
        return 0;
    memcpy(out, "WNS1", 4);
    put(out + 4, generation);
    put(out + 8, self);
    size_t count = 0, at = 16;
    for (const auto &entry : entries)
        if (entry.node) {
            if (entry.node == self)
                return 0;
            if (at + 40 > cap)
                return 0;
            put(out + at, entry.node);
            memcpy(out + at + 4, entry.trusted, 32);
            at += 36;
            ++count;
        }
    put(out + 12, count);
    put(out + at, crc(out, at));
    return at + 4;
}
bool NodeState::restore(const uint8_t *bytes, size_t size, uint32_t &self, uint32_t &generation)
{
    if (size < 20 || size > MaxBytes || memcmp(bytes, "WNS1", 4) || get(bytes + size - 4) != crc(bytes, size - 4))
        return false;
    const size_t count = get(bytes + 12);
    const uint32_t scope = get(bytes + 8);
    if (!scope || scope == UINT32_MAX || count > Capacity || size != 20 + count * 36)
        return false;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t node = get(bytes + 16 + i * 36);
        if (!node || node == UINT32_MAX || node == scope)
            return false;
        uint8_t any = 0;
        for (unsigned k = 0; k < 32; ++k)
            any |= bytes[20 + i * 36 + k];
        if (!any)
            return false;
        for (size_t j = 0; j < i; ++j)
            if (get(bytes + 16 + j * 36) == node)
                return false;
    }
    entries = {};
    for (size_t i = 0; i < count; ++i) {
        entries[i].node = get(bytes + 16 + i * 36);
        memcpy(entries[i].trusted, bytes + 20 + i * 36, 32);
    }
    self = scope;
    generation = get(bytes + 4);
    return true;
}
} // namespace wada
