#include <cassert>
#include <cstdint>
#include <cstdio>
#define WADA_MESHTASTIC 1
using lv_coord_t = int;
constexpr int LV_COORD_MAX = 32767, LV_ANIM_OFF = 0;
struct Obj {
    int scroll = 0;
};
struct LvChatPanel {
    Obj *msgs;
    bool follow_own_send = false;
};
struct {
    bool pending_scroll = false, pending_scroll_bottom = false;
    int pending_scroll_y = 0, last_i0 = 0, last_i1 = 0;
} s_chat_virt;
void chatVirtRefreshScrollArea(LvChatPanel *) {}
void lv_obj_update_layout(Obj *) {}
int chatVirtBottomScrollY(LvChatPanel *)
{
    return 20;
}
int chatVirtMaxVirtTop(LvChatPanel *)
{
    return 20;
}
void lv_obj_scroll_to_y(Obj *o, int y, int)
{
    o->scroll = y;
}
void chatVirtSyncScrollState(LvChatPanel *, int, int = -1) {}
void chatVirtResetInputForMsgs(LvChatPanel *) {}
void chatVirtCancelRenderTimer() {}
void chatVirtQueueScroll(LvChatPanel *, int target)
{
    s_chat_virt.pending_scroll = true;
    s_chat_virt.pending_scroll_bottom = target == LV_COORD_MAX;
    s_chat_virt.pending_scroll_y = target;
}
#include "native_follow_scroll.inc"
int main()
{
    Obj scroller;
    LvChatPanel panel{&scroller, true};
    queueRefresh(panel, 0);
    assert(panel.follow_own_send && s_chat_virt.pending_scroll_bottom);
    // A fast ACK schedules a second refresh before the asynchronous scroll executes.
    queueRefresh(panel, 0);
    assert(panel.follow_own_send && s_chat_virt.pending_scroll_bottom);
    chatVirtApplyPendingScroll(&panel);
    assert(scroller.scroll == 20 && !panel.follow_own_send);
    queueRefresh(panel, 7);
    assert(!s_chat_virt.pending_scroll_bottom);
    chatVirtApplyPendingScroll(&panel);
    assert(scroller.scroll == 7 && !panel.follow_own_send);
    puts("PASS: local-send bottom anchor survives ACK refresh; incoming preserves manual scroll");
}
