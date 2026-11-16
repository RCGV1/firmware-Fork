// The real popup layout must keep every caption below its QR, and its URL
// preparation must retain full multi-channel links instead of the old260-byte prefix.
#include "QrPopupLayout.h"
#include <cassert>
#include <cstdio>
#include <string>
int main()
{
    for (int width : {230, 320, 420})
        for (int height : {135, 214, 250})
            for (int font : {12, 16, 24}) {
                const auto layout = QrPopupLayout::make(width, height, font, 160);
                assert(layout.size >= 0 && layout.size <= width - 22);
                assert(layout.top + layout.size + 8 <= layout.captionTop);
                assert(layout.captionTop + layout.captionHeight <= height - 22);
            }
    std::string url = "https://meshtastic.org/e/#" + std::string(700, 'A');
    char out[1100];
    assert(qrPopupUrl(url.c_str(), out, sizeof out));
    assert(url == out);
    assert(qrPopupUrl("www.example.com", out, sizeof out));
    assert(std::string(out) == "https://www.example.com");
    assert(!qrPopupUrl(std::string(1100, 'a').c_str(), out, sizeof out));
    assert(!qrPopupUrl(url.c_str(), out, 260));
    puts("PASS: nonoverlapping QR/caption geometry and full-length channel URLs");
}
