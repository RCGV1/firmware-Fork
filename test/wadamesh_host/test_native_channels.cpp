// Compiles the production channel reconciliation, boot repair and record codec bodies.
// Only storage, preferences and the native snapshot are substituted. The fixtures
// exercise crash ordering, rekeys, slot moves and duplicates so a friendly channel
// label can never attach messages to a different encryption identity.
#include "SenderExtField.h"
#include "WadaDeliveryUI.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#define WADA_MESHTASTIC 1
#define ESP32 1
struct WdtHeavyGuard {
};
static bool uiHistWaitWorkerIdle()
{
    return false;
}
static size_t wadaTestStrlcpy(char *dst, const char *src, size_t capacity)
{
    const size_t size = strlen(src);
    if (capacity) {
        const size_t n = std::min(size, capacity - 1);
        memcpy(dst, src, n);
        dst[n] = 0;
    }
    return size;
}
#define strlcpy wadaTestStrlcpy
static uint32_t millis()
{
    return 100;
}
static void *psAlloc(size_t size)
{
    return calloc(1, size);
}
static uint64_t randomId = 1;
static void esp_fill_random(void *out, size_t size)
{
    auto *bytes = static_cast<uint8_t *>(out);
    const uint64_t value = ++randomId;
    for (size_t i = 0; i < size; ++i)
        bytes[i] = value >> (i * 8);
}
struct ContactInfo {
    struct {
        uint8_t pub_key[32]{};
    } id;
};
struct ChannelDetails {
    char name[33]{};
};
struct NativeSnapshot {
    bool ready = true;
    bool configured = true;
    bool enabled[8]{};
    uint8_t identities[8][32]{};
    ChannelDetails channels[8]{};
    bool channelsReady() const { return ready && configured; }
    bool ownsPrefs() const { return configured; }
    bool channelIdentity(unsigned slot, uint8_t *out) const
    {
        if (!channelsReady() || slot >= 8 || !enabled[slot])
            return false;
        memcpy(out, identities[slot], 32);
        return true;
    }
    bool canMessage(uint32_t) const { return false; }
    static uint32_t keyNode(const uint8_t *) { return 0; }
    bool getChannel(unsigned slot, ChannelDetails &out) const
    {
        if (slot >= 8 || !enabled[slot])
            return false;
        out = channels[slot];
        return true;
    }
    void set(unsigned slot, uint8_t identity, const char *name)
    {
        enabled[slot] = true;
        memset(identities[slot], identity, 32);
        strlcpy(channels[slot].name, name, sizeof(channels[slot].name));
    }
};
using WadaMeshtasticBackend = NativeSnapshot;
static NativeSnapshot snapshot;
static NativeSnapshot *wadaBackend = &snapshot;
#define the_mesh snapshot
static struct {
    bool dirty_threads = false, dirty_timeline = false;
} g_lv;
static unsigned s_deliv_gen = 0;
static std::map<std::string, uint8_t> mutes;
static std::map<std::string, std::string> emojis;
static uint8_t touchPrefsGetChannelMute(const char *name)
{
    return mutes[name];
}
static void touchPrefsSetChannelMute(const char *name, uint8_t mute)
{
    mutes[name] = mute;
}
static bool touchPrefsGetChannelEmoji(const char *name, char *out, size_t capacity)
{
    strlcpy(out, emojis[name].c_str(), capacity);
    return out[0];
}
static bool touchPrefsSetChannelEmoji(const char *name, const char *emoji)
{
    emojis[name] = emoji;
    return true;
}
static void segMarkSeqDirty(uint32_t) {}
class File
{
  public:
    std::vector<uint8_t> bytes;
    size_t cursor = 0;
    int readBytes(char *out, size_t size)
    {
        size = std::min(size, bytes.size() - cursor);
        memcpy(out, bytes.data() + cursor, size);
        cursor += size;
        return size;
    }
    bool seek(size_t offset)
    {
        if (offset > bytes.size())
            return false;
        cursor = offset;
        return true;
    }
    size_t position() const { return cursor; }
};
class UITask
{
  public:
    static const int MAX_UI_THREADS = 48, MAX_THREAD_NAME = 32, MAX_MSG_TEXT = 233, MAX_SENDER_NAME = 31, MAX_UI_PATH = 32;
#include "native_channel_records.inc"
    UIThread threads[MAX_UI_THREADS]{};
    UIMessage messages[64]{};
    UIThread *_ui_threads = threads;
    UIMessage *_ui_msgs = messages;
    int _ui_msg_cap = 64, _ui_msg_count = 0, _ui_msg_head = 0, _active_thread_idx = -1;
    bool _active_thread_is_channel = false, _thread_hist_dirty = true, _threads_dirty = false;
    unsigned _next_threads_flush_ms = 0;
    bool storeSucceeds = true;
    std::vector<std::string> saves;
    bool saveMsgsToStorage()
    {
        saves.push_back("messages");
        return storeSucceeds;
    }
    bool saveThreadsToStorage()
    {
        saves.push_back("threads");
        return true;
    }
    bool removeThread(int);
    unsigned dirtyThreads = 0, dirtyMessages = 0;
    void markThreadsDirty(unsigned) { ++dirtyThreads; }
    void markMsgsDirty() { ++dirtyMessages; }
    bool lookupActiveContact(ContactInfo &) const { return false; }
    bool threadHasMessageHistory(int index) const
    {
        for (int i = 0; i < _ui_msg_count; ++i)
            if (messages[i].channel == threads[index].channel && !strcmp(messages[i].thread, threads[index].name))
                return true;
        return false;
    }
    bool channelThreadAvailable(int) const;
    bool ensureNativeChannelConversation(int);
    void repairNativeChannelHistory();
    void refreshNativeChannels();
    uint32_t newestThreadMessageTs(const char *, bool) const;
    int findOrCreateThread(const char *, bool);
    void sortThreadsByRecent(bool, int[], int &) const;
    int active(unsigned slot) const
    {
        for (int i = 0; i < MAX_UI_THREADS; ++i)
            if (threads[i].used && threads[i].channel && threads[i].mesh_channel_slot == int(slot))
                return i;
        return -1;
    }
    int identity(uint8_t value) const
    {
        for (int i = 0; i < MAX_UI_THREADS; ++i)
            if (threads[i].used && threads[i].channel && threads[i].mesh_contact_pub[0] == value)
                return i;
        return -1;
    }
    void post(int thread, const char *text)
    {
        auto &m = messages[_ui_msg_head];
        m = {};
        m.channel = true;
        m.seq = _ui_msg_count + 1;
        m.ts = 1700000000 + m.seq;
        strlcpy(m.thread, threads[thread].name, sizeof(m.thread));
        strlcpy(m.text, text, sizeof(m.text));
        memcpy(m.native_channel_identity, threads[thread].mesh_contact_pub, 32);
        memcpy(m.native_conversation_id, threads[thread].mesh_contact_key6, 6);
        ++_ui_msg_count;
        _ui_msg_head = _ui_msg_count;
    }
};
// Storage types require the complete UITask definition.
#include "native_channel_types.inc"

// Production policy and codec bodies require the storage types above.
#include "native_channel_policy.inc"
static void reset()
{
    snapshot = {};
    mutes.clear();
    emojis.clear();
}
static void assertOwner(const UITask &ui, unsigned message, int thread)
{
    assert(thread >= 0);
    assert(!strcmp(ui.messages[message].thread, ui.threads[thread].name));
    assert(!memcmp(ui.messages[message].native_channel_identity, ui.threads[thread].mesh_contact_pub, 32));
    assert(!memcmp(ui.messages[message].native_conversation_id, ui.threads[thread].mesh_contact_key6, 6));
}
int main()
{
    reset();
    snapshot.set(0, 1, "Public [0]");
    UITask ui;
    const int legacy = ui.findOrCreateThread("Public [0]", true);
    ui.post(legacy, "legacy key unknown");
    ui.repairNativeChannelHistory();
    ui.refreshNativeChannels();
    assert(ui.threads[legacy].mesh_channel_slot == -1);
    assert(!ui.channelThreadAvailable(legacy));
    const int first = ui.active(0);
    assert(first >= 0 && first != legacy);
    assertOwner(ui, 0, legacy);
    ui.post(first, "key A");
    mutes["Public [0]"] = 3;
    emojis["Public [0]"] = "A";
    const auto oldFirst = ui.threads[first];
    snapshot.set(0, 2, "Public [0]");
    ui.refreshNativeChannels();
    assert(ui.threads[first].mesh_channel_slot == -1 && !ui.channelThreadAvailable(first));
    const int second = ui.active(0);
    assert(second != first);
    assertOwner(ui, 1, first);
    assert(mutes[ui.threads[first].name] == 3 && mutes["Public [0]"] == 0);
    ui.post(second, "key B");
    // Old metadata + newly written records: rebuild B from its tags, never name-match B to A.
    UITask oldMetadata;
    oldMetadata.threads[first] = oldFirst;
    oldMetadata._ui_msg_count = oldMetadata._ui_msg_head = 3;
    for (unsigned i = 0; i < 3; ++i)
        oldMetadata.messages[i] = ui.messages[i];
    oldMetadata.repairNativeChannelHistory();
    oldMetadata.refreshNativeChannels();
    assertOwner(oldMetadata, 1, oldMetadata.identity(1));
    assertOwner(oldMetadata, 2, oldMetadata.identity(2));
    assert(oldMetadata.identity(1) != oldMetadata.identity(2));
    // New metadata + old message labels: identity and conversation IDs repair the labels.
    UITask newMetadata;
    std::copy(std::begin(ui.threads), std::end(ui.threads), std::begin(newMetadata.threads));
    newMetadata._ui_msg_count = newMetadata._ui_msg_head = 3;
    std::copy(std::begin(ui.messages), std::end(ui.messages), std::begin(newMetadata.messages));
    strlcpy(newMetadata.messages[1].thread, "Public [0]", sizeof(newMetadata.messages[1].thread));
    newMetadata.repairNativeChannelHistory();
    assertOwner(newMetadata, 1, first);
    assertOwner(newMetadata, 2, second);
    // A zero-tail v7 record must remain isolated even when its name equals a new encrypted channel.
    newMetadata.messages[0].native_channel_identity[0] = 0;
    memset(newMetadata.messages[0].native_conversation_id, 0, 6);
    memset(newMetadata.messages[0].native_channel_identity, 0, 32);
    strlcpy(newMetadata.messages[0].thread, "Public [0]", sizeof(newMetadata.messages[0].thread));
    newMetadata.repairNativeChannelHistory();
    assert(!wadaBytesPresent(newMetadata.messages[0].native_channel_identity, 32));
    assert(strcmp(newMetadata.messages[0].thread, newMetadata.threads[second].name));
    // Slot move and swap preserve the conversation IDs, messages, local mute and emoji.
    reset();
    snapshot.set(0, 3, "Alpha [0]");
    snapshot.set(1, 4, "Beta [1]");
    UITask moved;
    moved.refreshNativeChannels();
    const int a = moved.active(0), b = moved.active(1);
    moved.post(a, "alpha");
    moved.post(b, "beta");
    mutes["Alpha [0]"] = 1;
    emojis["Alpha [0]"] = "alpha";
    mutes["Beta [1]"] = 2;
    emojis["Beta [1]"] = "beta";
    snapshot.set(0, 4, "Beta [0]");
    snapshot.set(1, 3, "Alpha [1]");
    moved.refreshNativeChannels();
    assert(moved.active(1) == a && moved.active(0) == b);
    assertOwner(moved, 0, a);
    assertOwner(moved, 1, b);
    assert(mutes["Alpha [1]"] == 1 && emojis["Beta [0]"] == "beta");
    snapshot.enabled[1] = false;
    moved.refreshNativeChannels();
    assert(moved.threads[a].mesh_channel_slot == -1);
    snapshot.set(5, 3, "Alpha [5]");
    moved.refreshNativeChannels();
    assert(moved.active(5) == a);
    assertOwner(moved, 0, a);
    // Partial snapshots must not archive or rebind existing history.
    snapshot.ready = false;
    const auto before = moved.threads[a];
    snapshot.set(5, 8, "Changed [5]");
    moved.refreshNativeChannels();
    assert(!memcmp(&before, &moved.threads[a], sizeof(before)));
    assert(!moved.channelThreadAvailable(a));
    // Identical channels are still distinct conversations; preserve pinned slot ownership.
    reset();
    snapshot.set(0, 5, "Same [0]");
    snapshot.set(1, 5, "Same [1]");
    UITask duplicates;
    duplicates.refreshNativeChannels();
    const int d0 = duplicates.active(0), d1 = duplicates.active(1);
    assert(d0 != d1 && memcmp(duplicates.threads[d0].mesh_contact_key6, duplicates.threads[d1].mesh_contact_key6, 6));
    duplicates.post(d0, "zero");
    duplicates.post(d1, "one");
    duplicates.refreshNativeChannels();
    assert(duplicates.active(0) == d0 && duplicates.active(1) == d1);
    assertOwner(duplicates, 0, d0);
    assertOwner(duplicates, 1, d1);
    // Live channel order is slot order; archives follow by recency.
    snapshot.enabled[0] = false;
    duplicates.refreshNativeChannels();
    int order[48], count = 0;
    duplicates.sortThreadsByRecent(true, order, count);
    assert(count == 2 && order[0] == d1 && order[1] == d0);
    // Production record codec keeps all old offsets, appends the identity, and zero-fills a v7 tail.
    static_assert(sizeof(UiHistoryMsg) == 307 && offsetof(UiSegMsg, seq) == 307 && offsetof(UiSegMsg, sender_ext) == 311);
    UiSegMsg record{};
    uiSegMarshal(ui.messages[2], &record);
    File full;
    full.bytes.resize(sizeof(record));
    memcpy(full.bytes.data(), &record, sizeof(record));
    UITask::UIMessage decoded{};
    uint32_t seq;
    assert(uiSegReadRec(full, sizeof(record), &decoded, &seq));
    assert(!memcmp(decoded.native_channel_identity, ui.messages[2].native_channel_identity, 32));
    assert(!memcmp(decoded.native_conversation_id, ui.messages[2].native_conversation_id, 6));
    File old;
    old.bytes.assign(full.bytes.begin(), full.bytes.begin() + offsetof(UiSegMsg, native_channel_identity));
    assert(uiSegReadRec(old, old.bytes.size(), &decoded, &seq));
    assert(!wadaBytesPresent(decoded.native_channel_identity, 32) && !wadaBytesPresent(decoded.native_conversation_id, 6));
    // Metadata without history must not consume every slot needed to recover tagged records.
    UITask crowded;
    for (unsigned i = 0; i < 48; ++i) {
        crowded.threads[i].used = true;
        snprintf(crowded.threads[i].name, 33, "empty DM %u", i);
    }
    crowded.messages[0] = ui.messages[2];
    crowded._ui_msg_count = crowded._ui_msg_head = 1;
    crowded.repairNativeChannelHistory();
    assert(crowded.identity(2) >= 0);
    assertOwner(crowded, 0, crowded.identity(2));
    // Thread deletion cannot finish before tagged records have been durably purged.
    UITask deleting;
    snapshot.ready = true;
    snapshot.set(0, 9, "Delete [0]");
    deleting.refreshNativeChannels();
    const int doomed = deleting.active(0);
    deleting.post(doomed, "delete me");
    deleting.storeSucceeds = false;
    assert(!deleting.removeThread(doomed));
    assert(deleting.threads[doomed].used);
    deleting.storeSucceeds = true;
    deleting.saves.clear();
    assert(deleting.removeThread(doomed));
    assert(deleting.saves.size() == 2 && deleting.saves[0] == "messages" && deleting.saves[1] == "threads");
    static_assert(offsetof(UiSegMsg, native_channel_identity) == offsetof(UiSegMsg, sender_ext) + sizeof(UiSegMsg::sender_ext));
    puts("PASS: production native channel reconciliation, identity persistence and crash-order recovery");
}
