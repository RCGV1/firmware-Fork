#include "WadaStatus.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <strings.h>
#include <vector>
#define WADA_MESHTASTIC 1
#define TR(x) x
extern std::string error;
constexpr unsigned PUB_KEY_SIZE = 32;
constexpr uint8_t MSG_META_HAS_RX = 1, MSG_META_IS_FLOOD = 2;
enum class UIEventType { none, contactMessage, channelMessage, roomMessage };
UIEventType g_last_event = UIEventType::none;
bool ignored = false;
bool touchPrefsIsIgnored(const uint8_t *)
{
    return ignored;
}
struct Node {
    bool is_muted = false;
};
struct WadaMeshtasticBackend {
    Node node;
    struct Channel {
        struct Settings {
            bool has_module_settings = true;
            struct Module {
                bool is_muted = false;
            } module_settings;
        } settings;
    } configuredChannel;
    const Channel &channelConfig(unsigned) const { return configuredChannel; }
    const Node *nodeInfo(uint32_t) { return &node; }
    static uint32_t keyNode(const uint8_t *) { return 42; }
} backend, *wadaBackend = &backend;
struct ChannelDetails {
    char name[32] = "LongFast [0]";
};
struct Mesh {
    struct Prefs {
        const char *node_name = "Desktop Sim";
    } prefs;
    Prefs *getNodePrefs() { return &prefs; }
    bool getChannel(unsigned index, ChannelDetails &) { return index == 0; }
    void runLocalCli(const char *);
} the_mesh;
struct Thread {
    bool channel = true;
    int mesh_contact_idx = 0;
    uint8_t mesh_contact_pub[32]{}, mesh_contact_key6[6]{};
    std::string name = "LongFast [0]";
    unsigned messages = 0;
};
struct UITask {
    static constexpr size_t MAX_MSG_TEXT = 233;
    Mesh::Prefs *_node_prefs = the_mesh.getNodePrefs();
    char _compose_buf[234] = "draft";
    unsigned resets = 0;
    bool sendComposerToActiveThread(const char *);
    void showAlert(const char *text, unsigned) { error = text; }
    void setComposerMode(bool) {}
    void composerReset()
    {
        ++resets;
        _compose_buf[0] = 0;
    }
    void composerAppendChar(char ch)
    {
        size_t n = strlen(_compose_buf);
        if (n < 233) {
            _compose_buf[n] = ch;
            _compose_buf[n + 1] = 0;
        }
    }
    bool composerSend() { return sendComposerToActiveThread(nullptr); }
    Thread _ui_threads[2];
    int _active_thread_idx = 0;
    bool _active_dm_contact_set = false, channelMuted = false;
    uint8_t _active_dm_contact_pub[32]{}, lastMeta = 0;
    unsigned threadCount = 1, toasts = 0;
    int _msgcount = 0;
    void refreshNativeChannels() {}
    void recordDelivery(const char *, bool, uint8_t, bool);
    void newMsgFromPubWithMeta(uint8_t, bool, const uint8_t *, const char *, const char *, int, int8_t, int8_t);
    void newMsgImpl(uint8_t, const char *, const char *, int, uint8_t, int8_t, int8_t, const char *, bool);
    int findThreadByName(const char *name, bool channel)
    {
        for (unsigned i = 0; i < threadCount; ++i)
            if (_ui_threads[i].name == name && _ui_threads[i].channel == channel)
                return i;
        return -1;
    }
    void syncThreadMeshSlots(const char *, bool) {}
    void persistHistoryNow() {}
};
// Storage and UI-effects spy after the actual production routing/mute prefix.
void UITask::recordDelivery(const char *name, bool channel, uint8_t meta, bool native_muted)
{
    int index = findThreadByName(name, channel);
    if (index < 0) {
        index = threadCount++;
        _ui_threads[index].name = name;
        _ui_threads[index].channel = channel;
    }
    ++_ui_threads[index].messages;
    lastMeta = meta;
    if (!native_muted)
        ++toasts;
}
#include "native_receive_dispatch.inc"
#include "native_receive_ui.inc"

std::string input, submitted, cli, error;
void *s_term_input_ta = &input;
struct {
    UITask *task = nullptr;
    void *keyboard = nullptr;
    bool dirty_threads = false;
} g_lv;
constexpr unsigned TERM_C_INPUT = 1, TERM_C_ERR = 2;
void kbMirrorSyncToReal() {}
const char *lv_textarea_get_text(void *)
{
    return input.c_str();
}
void lv_textarea_set_text(void *, const char *text)
{
    input = text;
}
void kbMirrorBind(void *) {}
void termLogAppendC(unsigned color, const char *, const char *text)
{
    if (color == TERM_C_ERR)
        error = text;
}
bool terminalRunChatCommand(const char *text)
{
    submitted = text;
    return true;
}
void Mesh::runLocalCli(const char *text)
{
    cli = text;
}
#include "native_terminal_submit.inc"
constexpr size_t MAX_TEXT_LEN = 233;
unsigned sendCalls = 0;
#include "native_terminal_send_guard.inc"
unsigned luaSendCalls = 0;
#include "native_message_guards.inc"
struct LvChatPanel {
    void *composer_ta = &input;
};
struct lv_event_t {
    LvChatPanel *panel;
};
constexpr unsigned LV_EVENT_CLICKED = 1;
unsigned lv_event_get_code(lv_event_t *)
{
    return LV_EVENT_CLICKED;
}
void *lv_event_get_user_data(lv_event_t *e)
{
    return e->panel;
}
unsigned hides = 0;
void hideKb()
{
    ++hides;
}
void refreshChatDetailAsync(LvChatPanel &) {}
#include "native_composer_input_guards.inc"

int main()
{
    UITask ui;
    g_last_event = UIEventType::contactMessage;
    backend.configuredChannel.settings.module_settings.is_muted = true;
    ui.newMsgFromPubWithMeta(0xff, false, nullptr, "LongFast [0]", "Alice: hello", 1, INT8_MIN, 0);
    assert(ui.threadCount == 1 && ui._ui_threads[0].messages == 1 && ui._ui_threads[0].channel);
    assert(ui.toasts == 0 && ui.lastMeta == 0);
    uint8_t key[32] = {42};
    g_last_event = UIEventType::channelMessage;
    ui.newMsgFromPubWithMeta(1, true, key, "Alice", "direct", 2, 4, -80);
    assert(ui.threadCount == 2 && !ui._ui_threads[1].channel && ui._ui_threads[1].messages == 1);
    assert(ui._ui_threads[0].messages == 1 && ui.toasts == 1);
    assert(!memcmp(ui._ui_threads[1].mesh_contact_pub, key, 32));
    ignored = true;
    ui.newMsgFromPubWithMeta(0, false, key, "Alice", "ignored", 3, 0, 0);
    assert(ui._ui_threads[1].messages == 1 && ui.toasts == 1);
    ignored = false;
    backend.node.is_muted = true;
    ui.newMsgFromPubWithMeta(0, false, key, "Alice", "muted", 4, 0, -80);
    assert(ui._ui_threads[1].messages == 2 && ui.toasts == 1);
    std::string cjk;
    for (int i = 0; i < 50; ++i)
        cjk += "界";
    const std::string full = "public " + cjk;
    input = full;
    terminalSubmit();
    assert(submitted == full && input.empty() && error.empty());
    submitted.clear();
    input = std::string(512, 'x');
    terminalSubmit();
    assert(submitted.empty() && input.size() == 512 && !error.empty());
    error.clear();
    input = "public \xe7\x95";
    terminalSubmit();
    assert(submitted.empty() && !error.empty() && !input.empty());
    error.clear();
    termDoSend(true, nullptr, 0, "LongFast [0]", cjk.c_str());
    assert(sendCalls == 1 && error.empty());
    std::string oversize;
    for (int i = 0; i < 80; ++i)
        oversize += "界";
    termDoSend(true, nullptr, 0, "LongFast [0]", oversize.c_str());
    assert(sendCalls == 1 && !error.empty());
    error.clear();
    std::string ligatures;
    for (int i = 0; i < 77; ++i)
        ligatures += "\xef\xbb\xbb";
    termDoSend(true, nullptr, 0, "LongFast [0]", ligatures.c_str());
    assert(sendCalls == 1 && !error.empty());
    error.clear();
    termDoSend(true, nullptr, 0, "LongFast [0]", "\xef\xbb\xbb");
    assert(sendCalls == 2 && error.empty());
    char normalized[8];
    arabicUnshape("\xef\xbb\xbb", normalized, sizeof normalized);
    assert(!strcmp(normalized, "\xd9\x84\xd8\xa7"));
    input = "public one\ntwo";
    terminalSubmit();
    assert(submitted.empty() && !error.empty());
    assert(!luaHostMeshSendDM("Alice", ligatures.c_str(), nullptr));
    assert(!luaHostMeshSendChannel("LongFast [0]", oversize.c_str()));
    assert(luaSendCalls == 0);
    assert(luaHostMeshSendDM("Alice", "line one\nline two", nullptr));
    assert(luaHostMeshSendChannel("LongFast [0]", "\xef\xbb\xbb"));
    assert(luaSendCalls == 2);
    assert(!ui.sendComposerToActiveThread(ligatures.c_str()));
    assert(!ui.sendComposerToActiveThread(oversize.c_str()));
    assert(ui.sendComposerToActiveThread("line one\nline two"));
    std::string shrinking;
    for (int i = 0; i < 78; ++i)
        shrinking += "\xef\xba\x8f";
    char normalizedBody[234];
    assert(shrinking.size() == 234 && wadaNormalizeMessage(shrinking.c_str(), normalizedBody, sizeof normalizedBody, false));
    assert(strlen(normalizedBody) == 156);
    g_lv.task = &ui;
    LvChatPanel panel;
    lv_event_t event{&panel};
    input = std::string(233, 'a') + "suffix";
    const std::string original = input;
    strcpy(ui._compose_buf, "draft");
    composerSendFromPanel(&panel);
    assert(input == original && !strcmp(ui._compose_buf, "draft") && ui.resets == 0 && hides == 0);
    sendFromPanelCb(&event);
    assert(input == original && !strcmp(ui._compose_buf, "draft") && ui.resets == 0 && hides == 0);
}
