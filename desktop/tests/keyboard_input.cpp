#include "WebInput.h"
#include "helpers/input/CardKbKeyboard.h"
#include <cassert>
#include <cstdio>
int main()
{
    webKeyboardPresent(false);
    assert(!cardKbPresent());
    webTextPush("Rr retry \xc3\xa9\xf0\x9f\x99\x82");
    const int points[] = {'R', 'r', ' ', 'r', 'e', 't', 'r', 'y', ' ', 0xe9, 0x1f642};
    for (int cp : points)
        assert(cardKbReadKey() == (WADA_KEY_TEXT | cp));
    assert(!cardKbReadKey() && cardKbPresent());
    webKeyPush(8);
    webKeyPush(13);
    webKeyPush(0xb4);
    assert(cardKbReadKey() == 8 && cardKbReadKey() == 13 && cardKbReadKey() == 0xb4 && !cardKbReadKey());
    puts("PASS: committed ASCII/Unicode text bypasses hotkeys; navigation/control keys remain raw");
}
