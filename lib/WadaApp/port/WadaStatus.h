#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wada
{
inline bool statusWhitespace(uint32_t c)
{
    return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
           c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
inline bool statusText(const char *text, size_t maxBytes, bool singleLine, bool *visible = nullptr)
{
    if (!text)
        return false;
    const size_t size = strnlen(text, maxBytes + 1);
    if (size > maxBytes)
        return false;
    bool content = false;
    for (size_t i = 0; i < size;) {
        const uint8_t first = uint8_t(text[i++]);
        uint32_t c = first;
        unsigned more = 0;
        uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            c &= 0x1f;
            more = 1;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            c &= 0x0f;
            more = 2;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            c &= 7;
            more = 3;
            minimum = 0x10000;
        } else if (first >= 0x80) {
            return false;
        }
        if (i + more > size)
            return false;
        for (unsigned j = 0; j < more; ++j) {
            const uint8_t next = uint8_t(text[i++]);
            if ((next & 0xc0) != 0x80)
                return false;
            c = (c << 6) | (next & 0x3f);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
            return false;
        if (singleLine && (c == '\n' || c == '\r' || c == 0x85 || c == 0x2028 || c == 0x2029))
            return false;
        content |= !statusWhitespace(c);
    }
    if (visible)
        *visible = content;
    return true;
}
inline bool statusVisible(const char *text)
{
    bool visible = false;
    return statusText(text, 80, false, &visible) && visible;
}
} // namespace wada
