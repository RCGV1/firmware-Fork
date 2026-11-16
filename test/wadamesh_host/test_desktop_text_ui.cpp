#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
constexpr int BLE_KEY_TEXT = 0x40000000;
struct lv_obj_t {
    std::string text;
} field, popup;
lv_obj_t *bound = &field;
bool s_nav_ta_editing = false, s_nav_show = false;
unsigned hotkeys = 0;
lv_obj_t *bleKbdTouchBound()
{
    return bound;
}
lv_obj_t *bleKbdTouchTypingTarget()
{
    return s_nav_ta_editing ? bound : nullptr;
}
bool bleKbdTabHotkey(int code)
{
    if (code == 'r' || code == 'R') {
        ++hotkeys;
        return true;
    }
    return false;
}
void txtMenuHide() {}
void lv_textarea_add_text(lv_obj_t *ta, const char *text)
{
    ta->text += text;
}
void composerMentionRefresh(lv_obj_t *) {}
#include "desktop_committed_ui.inc"
int main()
{
    dispatchCommitted(BLE_KEY_TEXT | 'R');
#if defined(WADA_DESKTOP)
    assert(field.text == "R" && hotkeys == 0 && s_nav_ta_editing);
    field.text.clear();
    s_nav_ta_editing = false;
    dispatchCommitted(BLE_KEY_TEXT | 'r');
    assert(field.text == "r" && hotkeys == 0);
    dispatchCommitted(BLE_KEY_TEXT | 0x1f642);
    assert(field.text == "r🙂");
    bound = &popup;
    s_nav_ta_editing = false;
    dispatchCommitted(BLE_KEY_TEXT | 'R');
    assert(popup.text == "R" && hotkeys == 0);
    puts("PASS: desktop committed R/r and Unicode prefer tapped chat/popup text");
#else
    assert(field.text.empty() && hotkeys == 1 && !s_nav_ta_editing);
    dispatchCommitted(BLE_KEY_TEXT | 'D');
    assert(field.text == "D" && s_nav_ta_editing);
    puts("PASS: native hardware text/hotkey precedence remains unchanged");
#endif
}
