// Compiles the production native node UI helpers and removal callback. A confirmed
// clear must resnapshot local favorites, keep every protected node, and report
// removal only after the backend result. Renderer assertions keep the five security
// glyphs distinct and both verified states green; map filters use independent facts.
#include "WadaNodeState.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "ui_icons.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>
#define WADA_MESHTASTIC 1
#define ESP32 1
#define TR(text) (text)
#define LV_MIN(a, b) std::min(a, b)
constexpr unsigned LV_PART_MAIN = 0, LV_OBJ_FLAG_SCROLLABLE = 1, LV_OBJ_FLAG_CLICKABLE = 2, LV_STATE_DISABLED = 4;
constexpr int LV_SIZE_CONTENT = -1, LV_RADIUS_CIRCLE = 99, LV_OPA_COVER = 255, LV_EVENT_CLICKED = 1;
constexpr int LV_ANIM_OFF = 0;
constexpr int LV_FLEX_FLOW_COLUMN = 1, LV_FLEX_FLOW_ROW = 2, LV_LABEL_LONG_WRAP = 1, LV_ALIGN_TOP_LEFT = 1,
              LV_ALIGN_BOTTOM_RIGHT = 2;
constexpr uint32_t COLOR_STATUS_DANGER_TEXT = 0xff0000, COLOR_STATUS_WARN = 0xffff00, COLOR_STATUS_WARN_TEXT = 0x775500,
                   COLOR_STATUS_OK_TEXT = 0x00ff00, COLOR_BG = 0, COLOR_TEXT = 0xffffff, COLOR_PANEL = 0x111111;
constexpr unsigned TOUCH_FAVORITES_MAX = 32, TOUCH_FAVORITE_KEY_BYTES = 6;
int g_font_12, g_font_14, g_font_semi_14;
struct lv_point_t {
    int x, y;
};
struct lv_obj_t {
    std::string text, tip, type;
    unsigned flags = LV_OBJ_FLAG_CLICKABLE, states = 0;
    uint32_t color = 0;
    int scroll = 0;
    std::vector<lv_obj_t *> children;
};
struct lv_event_t {
    int code;
    void *user = nullptr;
};
static std::vector<std::unique_ptr<lv_obj_t>> objects;
static lv_obj_t *object(lv_obj_t *parent = nullptr, const char *type = "obj")
{
    objects.push_back(std::make_unique<lv_obj_t>());
    auto *p = objects.back().get();
    p->type = type;
    if (parent)
        parent->children.push_back(p);
    return p;
}
static lv_obj_t *lv_obj_create(lv_obj_t *p)
{
    return object(p);
}
static lv_obj_t *lv_label_create(lv_obj_t *p)
{
    return object(p, "label");
}
static lv_obj_t *lv_btn_create(lv_obj_t *p)
{
    return object(p, "button");
}
static lv_obj_t *lv_line_create(lv_obj_t *p)
{
    return object(p, "line");
}
static void lv_obj_remove_style_all(lv_obj_t *) {}
static void lv_obj_set_size(lv_obj_t *, int, int) {}
static void lv_obj_set_width(lv_obj_t *, int) {}
static void lv_obj_set_pos(lv_obj_t *, int, int) {}
static void lv_obj_set_flex_flow(lv_obj_t *, int) {}
static void lv_obj_set_flex_grow(lv_obj_t *, int) {}
static void lv_obj_set_style_pad_all(lv_obj_t *, int, int) {}
static void lv_obj_set_style_pad_row(lv_obj_t *, int, int) {}
static void lv_obj_set_style_pad_column(lv_obj_t *, int, int) {}
static void lv_obj_set_style_text_font(lv_obj_t *, int *, int) {}
static void lv_obj_set_style_text_color(lv_obj_t *p, uint32_t c, int)
{
    p->color = c;
}
static void lv_obj_set_style_border_color(lv_obj_t *p, uint32_t c, int)
{
    p->color = c;
}
static void lv_obj_set_style_line_color(lv_obj_t *p, uint32_t c, int)
{
    p->color = c;
}
static void lv_obj_set_style_bg_color(lv_obj_t *, uint32_t, int) {}
static void lv_obj_set_style_bg_opa(lv_obj_t *, int, int) {}
static void lv_obj_set_style_radius(lv_obj_t *, int, int) {}
static void lv_obj_set_style_border_width(lv_obj_t *, int, int) {}
static void lv_obj_set_style_line_width(lv_obj_t *, int, int) {}
static void lv_line_set_points(lv_obj_t *, const lv_point_t *, unsigned) {}
static void lv_obj_align(lv_obj_t *, int, int, int) {}
static void lv_obj_center(lv_obj_t *) {}
static int lv_obj_get_scroll_y(lv_obj_t *obj)
{
    return obj->scroll;
}
static void lv_obj_update_layout(lv_obj_t *) {}
static void lv_obj_scroll_to_y(lv_obj_t *obj, int y, int)
{
    obj->scroll = y;
}

static void lv_obj_clear_flag(lv_obj_t *p, unsigned f)
{
    p->flags &= ~f;
}
static void lv_obj_add_flag(lv_obj_t *p, unsigned f)
{
    p->flags |= f;
}
static void lv_obj_add_state(lv_obj_t *p, unsigned f)
{
    p->states |= f;
}
static void lv_obj_add_event_cb(lv_obj_t *, void (*)(lv_event_t *), int, void *) {}
static int lv_event_get_code(lv_event_t *e)
{
    return e->code;
}
static void *lv_event_get_user_data(lv_event_t *e)
{
    return e->user;
}
static void lv_label_set_text(lv_obj_t *p, const char *t)
{
    p->text = t;
}
template <class... A> static void lv_label_set_text_fmt(lv_obj_t *p, const char *f, A... a)
{
    char b[300];
    snprintf(b, sizeof(b), f, a...);
    p->text = b;
}
static void lv_label_set_long_mode(lv_obj_t *, int) {}
static void setIconLabel(lv_obj_t *p, const char *text)
{
    p->text = text;
}
static void navSetTip(lv_obj_t *p, const char *text)
{
    p->tip = text;
}
static uint32_t lv_color_hex(uint32_t c)
{
    return c;
}
static int lv_pct(int p)
{
    return p;
}
static void styleButton(lv_obj_t *) {}
static void styleSurface(lv_obj_t *, unsigned, int) {}
enum class SettingsModalKind { NativeSecurity, NativeNodeDetails };
static struct {
    lv_obj_t *root = nullptr;
    lv_obj_t *body = nullptr;
    SettingsModalKind kind = SettingsModalKind::NativeSecurity;
} g_set_modal;
static constexpr int MAP_TAB_INDEX = 4;
static int getActiveTab()
{
    return 2;
}
static void renderMapMarkers() {}
static uint32_t selectedNode = 1;
static unsigned detailsBuilds = 0;
static uint32_t wadaSelectedNode()
{
    return selectedNode;
}
static bool identityPopupActive = false;
static bool wadaIdentityPopupActive()
{
    return identityPopupActive;
}
static void wadaNodeDetails();
static void closeSettingsModal()
{
    g_set_modal.root = nullptr;
}
static lv_obj_t *createSettingsModal(const char *, SettingsModalKind)
{
    return object();
}
static lv_obj_t *setBox(lv_obj_t *parent)
{
    return object(parent);
}
static lv_obj_t *setNote(lv_obj_t *parent, const char *text)
{
    auto *p = object(parent);
    p->text = text;
    return p;
}
struct ContactInfo {
    uint8_t flags = 0;
    struct {
        uint8_t pub_key[32]{};
    } id;
};
struct WadaMeshtasticBackend {
    bool ready = true, supported = true, busy = false, managed = false;
    uint32_t self = 1;
    std::vector<meshtastic_NodeInfo> nodes;
    std::vector<uint32_t> requests;
    std::set<uint32_t> reject, mismatch;
    bool nodeSnapshotReady() const { return ready; }
    bool isManaged() const { return managed; }
    bool reportsHeardOnCurrentLora() const { return supported; }
    bool requestUserInfo(uint32_t n)
    {
        userInfoRequests.push_back(n);
        return accept;
    }
    std::vector<uint32_t> userInfoRequests;
    bool accept = true;
    uint32_t nodeNumber() const { return self; }
    uint32_t revision = 0;
    uint32_t nodeRevision() const { return revision; }
    const meshtastic_NodeInfo *nodeInfo(uint32_t n) const
    {
        for (auto &x : nodes)
            if (x.num == n)
                return &x;
        return nullptr;
    }
    bool unheardOnCurrentLora(uint32_t n) const
    {
        auto *x = nodeInfo(n);
        return ready && supported && x && n != self && !x->via_mqtt && !x->heard_on_current_lora;
    }
    bool removingUnheardNodes() const { return busy; }
    bool removeUnheardNode(uint32_t n)
    {
        if (reject.count(n))
            return false;
        requests.push_back(n);
        busy = true;
        return true;
    }
    wada::NodeSecurity nodeSecurity(uint32_t n) const
    {
        const auto *node = nodeInfo(n);
        return wada::resolveNodeSecurity(mismatch.count(n), n == self, node && node->is_key_manually_verified,
                                         node && node->has_xeddsa_signed, node && node->user.public_key.size == 32);
    }
    const char *nodeFirmwareVersion(uint32_t) const { return ""; }
    std::string status;
    const char *nodeStatus(uint32_t) const { return status.c_str(); }
    bool canMessage(uint32_t n) const
    {
        const auto *node = nodeInfo(n);
        return node && node->user.public_key.size == 32;
    }
    const char *directMessageReason(uint32_t) const { return "No public key"; }
    bool nodeSigned(uint32_t n) const
    {
        auto *p = nodeInfo(n);
        return p && p->has_xeddsa_signed;
    }
    bool nodeEncrypted(uint32_t n) const
    {
        auto *p = nodeInfo(n);
        return p && p->user.public_key.size == 32 && !mismatch.count(n);
    }
    static void nodeKey(uint32_t n, uint8_t *key)
    {
        memset(key, 0, 32);
        for (unsigned i = 0; i < 4; ++i)
            key[i] = n >> (i * 8);
        memcpy(key + 4, "MSHT", 4);
    }
    static uint32_t keyNode(const uint8_t *key)
    {
        uint32_t n = 0;
        for (unsigned i = 0; i < 4; ++i)
            n |= uint32_t(key[i]) << (i * 8);
        return n;
    }
};
static WadaMeshtasticBackend backend;
static WadaMeshtasticBackend *wadaBackend = &backend;
static std::set<uint32_t> localFavorites;
static int touchPrefsCopyFavorites(uint8_t *out)
{
    unsigned i = 0;
    for (auto n : localFavorites) {
        uint8_t key[32];
        WadaMeshtasticBackend::nodeKey(n, key);
        memcpy(out + i++ * 6, key, 6);
    }
    return i;
}
static bool touchPrefsFavoritesSnapshotContains(const uint8_t *keys, int count, const uint8_t *key)
{
    for (int i = 0; i < count; ++i)
        if (!memcmp(keys + i * 6, key, 6))
            return true;
    return false;
}
static bool touchPrefsIsFavorite(const uint8_t *key)
{
    return localFavorites.count(WadaMeshtasticBackend::keyNode(key));
}
static bool touchPrefsSetFavorite(const uint8_t *key, bool wanted)
{
    const auto n = WadaMeshtasticBackend::keyNode(key);
    if (wanted)
        localFavorites.insert(n);
    else
        localFavorites.erase(n);
    return wanted;
}
struct Mesh {
    bool accept = true;
    unsigned favoriteRequests = 0, removalRequests = 0, fakeAdds = 0;
    bool uiSetContactFavorite(const uint8_t *, bool)
    {
        ++favoriteRequests;
        return accept;
    }
    bool uiRemoveContact(const ContactInfo &)
    {
        ++removalRequests;
        return accept;
    }
    bool uiAddManualContact(const uint8_t *, const char *)
    {
        ++fakeAdds;
        return accept;
    }
    std::vector<ContactInfo> contacts;
    int getNumContacts() const { return contacts.size(); }
    bool getContactByIdx(int i, ContactInfo &out) const
    {
        if (i < 0 || size_t(i) >= contacts.size())
            return false;
        out = contacts[i];
        return true;
    }
};
static Mesh the_mesh;
class UITask
{
  public:
    std::vector<std::string> alerts;
    void showAlert(const char *text, unsigned) { alerts.emplace_back(text); }
    void onMeshtasticNodeRemoval(uint32_t, bool, const char *);
};
static UITask task;
static struct {
    UITask *task = &::task;
    bool dirty_contacts = false;
    unsigned contacts_filter = 0;
} g_lv;
static bool s_ct_list_force = false;
static void ctSortSheetClose() {}
static void (*confirmed)() = nullptr;
static void showConfirm(const char *, const char *, void (*callback)())
{
    confirmed = callback;
}

#include "WadaNativeNodes.inc"

static void wadaNodeDetails()
{
    ++detailsBuilds;
    g_set_modal.body = object();
    s_native_details_node = selectedNode;
    s_native_details_stamp = wadaNodeDetailsStamp(selectedNode);
}

static lv_obj_t *s_ct_filter_btn_lbl = nullptr;
constexpr const char *LV_SYMBOL_DOWN = "down";
#include "native_node_filter_label.inc"

#include "WadaNativeClear.inc"

#include "native_node_removal.inc"

#include "native_node_tick.inc"
static lv_obj_t *s_addct_pub_ta = nullptr, *s_addct_name_ta = nullptr, *s_addct_error_l = nullptr;
static unsigned s_action_sheet_mesh_idx = 0;
static const char *lv_textarea_get_text(lv_obj_t *p)
{
    return p->text.c_str();
}
static void kbMirrorSyncToReal() {}
static void closeActionSheet() {}
static void contactsListForceRefresh() {}
static void refreshContactsList() {}
#include "native_node_actions.inc"

static void add(uint32_t n, bool favorite = false, bool mqtt = false, bool heard = false)
{
    meshtastic_NodeInfo node{};
    node.num = n;
    node.is_favorite = favorite;
    node.via_mqtt = mqtt;
    node.heard_on_current_lora = heard;
    backend.nodes.push_back(node);
    ContactInfo c{};
    WadaMeshtasticBackend::nodeKey(n, c.id.pub_key);
    the_mesh.contacts.push_back(c);
}
static void reset()
{
    backend = WadaMeshtasticBackend{};
    the_mesh.contacts.clear();
    task.alerts.clear();
    localFavorites.clear();
    confirmed = nullptr;
    s_native_clear_count = s_native_clear_waiting = s_native_clear_removed = s_native_clear_kept = 0;
    memset(s_native_clear_pending, 0, sizeof(s_native_clear_pending));
    s_native_clear_reason[0] = 0;
}
int main()
{
    ContactInfo favorite{};
    WadaMeshtasticBackend::nodeKey(2, favorite.id.pub_key);
    the_mesh.contacts.push_back(favorite);
    lv_event_t click{LV_EVENT_CLICKED};
    actionSheetFavoriteCb(&click);
    assert(the_mesh.favoriteRequests == 1 && !wadaContactIsFavorite(the_mesh.contacts[0]));
    assert(localFavorites.count(2) && task.alerts.back() == "Favorite update requested");
    the_mesh.contacts[0].flags = 1;
    assert(wadaContactIsFavorite(the_mesh.contacts[0]));
    the_mesh.accept = false;
    actionSheetFavoriteCb(&click);
    assert(localFavorites.count(2) && task.alerts.back() == "Could not queue favorite update");
    actionSheetDeleteCb(&click);
    assert(the_mesh.contacts.size() == 1 && task.alerts.back() == "Could not queue node removal");
    the_mesh.accept = true;
    actionSheetDeleteCb(&click);
    assert(the_mesh.contacts.size() == 1 && task.alerts.back() == "Node removal requested");
    s_addct_pub_ta = object();
    s_addct_error_l = object();
    s_addct_pub_ta->text = "!00000000";
    addContactSubmitCb(&click);
    assert(backend.userInfoRequests.empty());
    s_addct_pub_ta->text = "!ffffffff";
    addContactSubmitCb(&click);
    assert(backend.userInfoRequests.empty());
    s_addct_pub_ta->text = "!00000001";
    addContactSubmitCb(&click);
    assert(backend.userInfoRequests.empty());
    s_addct_pub_ta->text = "!1234abcd";
    addContactSubmitCb(&click);
    assert(backend.userInfoRequests.size() == 1 && backend.userInfoRequests[0] == 0x1234abcd && the_mesh.fakeAdds == 0);
    assert(task.alerts.back() == "Node info requested; the node appears when heard");

    for (unsigned i = 0; i < 5; ++i) {
        auto state = wada::NodeSecurity(i);
        auto *glyph = wadaSecurityGlyph(nullptr, state);
        assert(glyph->tip == wada::nodeSecurityName(state));
        assert(!glyph->children.empty());
    }
    auto *verified = wadaSecurityGlyph(nullptr, wada::NodeSecurity::VerifiedContact);
    auto *signedNode = wadaSecurityGlyph(nullptr, wada::NodeSecurity::SignedNode);
    assert(verified->children[0]->text == UI_ICON_USER && signedNode->children[0]->text == UI_ICON_RADIO);
    assert(verified->children[0]->color == COLOR_STATUS_OK_TEXT && signedNode->children[0]->color == COLOR_STATUS_OK_TEXT);
    assert(verified->children.size() == 2 && signedNode->children.size() == 2);
    auto *lock = wadaSecurityGlyph(nullptr, wada::NodeSecurity::PublicKey);
    auto *open = wadaSecurityGlyph(nullptr, wada::NodeSecurity::NoPublicKey);
    assert(lock->children[0]->text == UI_ICON_LOCK && open->children[0]->text == UI_ICON_LOCK_OPEN);
    assert(open->children[0]->color == COLOR_STATUS_WARN_TEXT);
    auto *list = object();
    wadaUnheardBanner(list, 4, 2);
    assert(list->children.front()->children.front()->color == COLOR_STATUS_WARN_TEXT);
    wadaSecurityLegend();

    reset();
    s_native_hide_unheard = true;
    assert(std::string(contactsFilterShortLabel()) == "All +LoRa");
    g_lv.contacts_filter = 3;
    assert(std::string(contactsFilterShortLabel()) == "Fav +LoRa");
    backend.supported = false;
    assert(std::string(contactsFilterShortLabel()) == "Fav");
    backend.supported = true;
    s_native_hide_unheard = false;
    g_lv.contacts_filter = 0;
    add(1);
    add(2);
    add(3);
    add(4, true);
    add(5, false, true);
    add(6);
    localFavorites.insert(6);
    assert(wadaCollectUnheard(nullptr, 0, false) == 4);
    assert(wadaCollectUnheard(nullptr, 0, true) == 2);
    uint32_t limited[2] = {0, 0x1234};
    assert(wadaCollectUnheard(limited, 1, true) == 1 && limited[1] == 0x1234);
    backend.ready = false;
    assert(!wadaCollectUnheard(nullptr, 0, true));
    backend.ready = true;
    backend.supported = false;
    assert(!wadaCollectUnheard(nullptr, 0, false));
    backend.supported = true;
    wadaConfirmClearUnheard();
    assert(confirmed && backend.requests.empty());
    localFavorites.insert(3);
    confirmed();
    assert(backend.requests == std::vector<uint32_t>{2} && s_native_clear_waiting == 1);
    assert(backend.nodeInfo(2) && s_native_clear_removed == 0);
    assert(task.alerts.back().find("Waiting for the node database") != std::string::npos);
    task.onMeshtasticNodeRemoval(99, true, nullptr);
    assert(s_native_clear_waiting == 1);
    backend.nodes.erase(std::remove_if(backend.nodes.begin(), backend.nodes.end(), [](auto &n) { return n.num == 2; }),
                        backend.nodes.end());
    backend.busy = false;
    task.onMeshtasticNodeRemoval(2, true, nullptr);
    assert(s_native_clear_waiting == 0 && s_native_clear_removed == 1 && s_native_clear_kept == 1);
    assert(task.alerts.back().find("Removed 1 nodes; 1 kept or unconfirmed.") != std::string::npos);
    task.onMeshtasticNodeRemoval(2, true, nullptr);
    assert(s_native_clear_removed == 1);

    reset();
    add(2);
    wadaConfirmClearUnheard();
    confirmed();
    backend.busy = false;
    task.onMeshtasticNodeRemoval(2, false, "LoRa settings changed");
    assert(s_native_clear_removed == 0 && s_native_clear_kept == 1 && backend.nodeInfo(2));
    assert(task.alerts.back().find("LoRa settings changed") != std::string::npos);
    reset();
    add(2);
    backend.managed = true;
    wadaConfirmClearUnheard();
    assert(!confirmed && backend.requests.empty());
    assert(task.alerts.back().find("managed") != std::string::npos);

    reset();
    add(2);
    backend.reject.insert(2);
    wadaConfirmClearUnheard();
    confirmed();
    assert(!s_native_clear_waiting && s_native_clear_kept == 1 && s_native_clear_removed == 0);

    reset();
    add(2);
    add(3);
    backend.nodes[0].has_xeddsa_signed = true;
    backend.nodes[0].is_key_manually_verified = true;
    backend.nodes[0].user.public_key.size = 32;
    s_native_map_signed = true;
    s_native_map_encrypted = false;
    assert(wadaMapAllowsNode(2) && !wadaMapAllowsNode(3));
    s_native_map_encrypted = true;
    assert(wadaMapAllowsNode(2));
    backend.mismatch.insert(2);
    assert(!wadaMapAllowsNode(2));
    s_native_map_signed = s_native_map_encrypted = false;
    assert(wadaMapAllowsNode(3));
    lv_event_t signedFilter{LV_EVENT_CLICKED, (void *)uintptr_t(1)};
    lv_event_t encryptedFilter{LV_EVENT_CLICKED, (void *)uintptr_t(2)};
    wadaSecurityFilterCb(&signedFilter);
    assert(wadaNodeListAllowsNode(2) && !wadaNodeListAllowsNode(3));
    assert(wadaMapAllowsNode(3));
    assert(std::string(contactsFilterShortLabel()) == "All +Sig");
    wadaSecurityFilterCb(&encryptedFilter);
    assert(!wadaNodeListAllowsNode(2));
    assert(std::string(contactsFilterShortLabel()) == "All +2");
    backend.mismatch.clear();
    assert(wadaNodeListAllowsNode(2));
    s_native_hide_unheard = true;
    assert(std::string(contactsFilterShortLabel()) == "All +3");
    backend.supported = false;
    assert(std::string(contactsFilterShortLabel()) == "All +2");
    backend.supported = true;
    s_native_hide_unheard = false;
    wadaSecurityFilterCb(&signedFilter);
    wadaSecurityFilterCb(&encryptedFilter);

    wadaNativeNodeTick();
    g_lv.dirty_contacts = false;
    backend.busy = true;
    wadaNativeNodeTick();
    assert(g_lv.dirty_contacts);
    g_lv.dirty_contacts = false;
    wadaNativeNodeTick();
    assert(!g_lv.dirty_contacts);
    backend.busy = false;
    wadaNativeNodeTick();
    assert(g_lv.dirty_contacts);
    selectedNode = 2;
    g_set_modal.root = object();
    g_set_modal.kind = SettingsModalKind::NativeNodeDetails;
    wadaNodeDetails();
    g_set_modal.body->scroll = 74;
    auto *previousBody = g_set_modal.body;
    const unsigned before = detailsBuilds;
    ++backend.revision;
    wadaNativeNodeTick();
    assert(detailsBuilds == before && g_set_modal.body == previousBody && g_set_modal.body->scroll == 74);
    backend.nodes[0].has_hops_away = true;
    backend.nodes[0].hops_away = 2;
    ++backend.revision;
    wadaNativeNodeTick();
    assert(detailsBuilds == before + 1 && g_set_modal.body->scroll == 74);
    identityPopupActive = true;
    backend.status = "Late status";
    ++backend.revision;
    wadaNativeNodeTick();
    assert(detailsBuilds == before + 1 && g_set_modal.body->scroll == 74);
    identityPopupActive = false;
    wadaNativeNodeTick();
    assert(detailsBuilds == before + 2 && g_set_modal.body->scroll == 74);
    puts("PASS: native node security glyphs, fact filters and confirmed protected-node removal");
}
