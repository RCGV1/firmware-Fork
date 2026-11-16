#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace wada
{
inline constexpr uint32_t NodeDelta = 0x57414441, NodeGone = 0x57414442;
enum class NodeSecurity : uint8_t { KeyMismatch, VerifiedContact, SignedNode, PublicKey, NoPublicKey };
bool firmwareAtLeast(const char *version, unsigned major, unsigned minor, unsigned patch = 0);
NodeSecurity resolveNodeSecurity(bool mismatch, bool own, bool verified, bool signedNode, bool hasKey,
                                 const char *version = nullptr);
const char *nodeSecurityName(NodeSecurity);
const char *nodeSecurityHelp(NodeSecurity);
class NodeState
{
  public:
    static constexpr size_t Capacity = 256;
    static constexpr size_t MaxBytes = 20 + Capacity * 36;
    bool mismatch(uint32_t node) const;
    bool markMismatch(uint32_t node, const uint8_t trusted[32]);
    bool acceptedKey(uint32_t node, const uint8_t *key, bool own);
    bool forget(uint32_t node);
    size_t encode(uint8_t *out, size_t cap, uint32_t self, uint32_t generation) const;
    bool restore(const uint8_t *, size_t size, uint32_t &self, uint32_t &generation);

  private:
    struct Entry {
        uint32_t node = 0;
        uint8_t trusted[32]{};
    };
    std::array<Entry, Capacity> entries{};
};
} // namespace wada
size_t wadaNodeStateRead(unsigned slot, uint8_t *out, size_t cap);
bool wadaNodeStateWrite(unsigned slot, const uint8_t *bytes, size_t size);
