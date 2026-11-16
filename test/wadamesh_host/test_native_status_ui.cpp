// Compiles the real status UI. UTF-8 overflow must reject a whole edit, late
// updates must preserve drafts, and queue acceptance must not change displayed status.
#include "WadaStatus.h"
#include "ui_icons.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#define WADA_MESHTASTIC 1
#define TR(x) (x)
#define LV_MIN(a, b) std::min(a, b)
#define LV_MAX(a, b) std::max(a, b)
constexpr int LV_OBJ_FLAG_CLICKABLE = 1, LV_OBJ_FLAG_SCROLLABLE = 2, LV_OBJ_FLAG_HIDDEN = 4, LV_PART_MAIN = 0;
constexpr int LV_FLEX_FLOW_COLUMN = 1, LV_FLEX_FLOW_ROW = 2, LV_SIZE_CONTENT = -1, LV_LABEL_LONG_WRAP = 1, LV_LABEL_LONG_DOT = 2;
constexpr int LV_ALIGN_TOP_LEFT = 1, LV_ALIGN_TOP_RIGHT = 2, LV_EVENT_CLICKED = 1, LV_EVENT_VALUE_CHANGED = 2, LV_EVENT_READY = 3,
              LV_TEXT_FLAG_NONE = 0;
constexpr unsigned COLOR_TEXT = 0xffffff, COLOR_SUB = 0x999999, COLOR_STATUS_DANGER_TEXT = 0xaa0000;
#define LV_SYMBOL_CLOSE "x"
struct Font {
    int height;
};
static Font g_font_12{12}, g_font_14{14};
struct lv_point_t {
    int x, y;
};
struct lv_event_t {
};
struct lv_obj_t {
    std::string text, tip;
    int width = 280, height = 0, mode = 0, flags = 3, maxLength = -1;
    unsigned color = 0;
    bool recolor = true, oneLine = false;
    lv_obj_t *parent = nullptr;
    std::vector<lv_obj_t *> children;
};
static std::vector<std::unique_ptr<lv_obj_t>> objects;
static lv_obj_t *lv_obj_create(lv_obj_t *parent)
{
    objects.push_back(std::make_unique<lv_obj_t>());
    auto *p = objects.back().get();
    p->parent = parent;
    if (parent)
        parent->children.push_back(p);
    return p;
}
static lv_obj_t *lv_label_create(lv_obj_t *p)
{
    return lv_obj_create(p);
}
static lv_obj_t *lv_line_create(lv_obj_t *p)
{
    return lv_obj_create(p);
}
static lv_obj_t *lv_btn_create(lv_obj_t *p)
{
    return lv_obj_create(p);
}
static void lv_obj_remove_style_all(lv_obj_t *) {}
static void lv_obj_set_size(lv_obj_t *p, int w, int h)
{
    p->width = w;
    p->height = h;
}
static void lv_obj_set_width(lv_obj_t *p, int w)
{
    p->width = w;
}
static int lv_obj_get_width(lv_obj_t *p)
{
    return p->width;
}
static void lv_obj_set_height(lv_obj_t *p, int h)
{
    p->height = h;
}
static void lv_obj_clear_flag(lv_obj_t *p, int f)
{
    p->flags &= ~f;
}
static void lv_obj_add_flag(lv_obj_t *p, int f)
{
    p->flags |= f;
}
static void lv_obj_set_pos(lv_obj_t *, int, int) {}
static void lv_obj_set_flex_flow(lv_obj_t *, int) {}
static void lv_obj_set_style_pad_row(lv_obj_t *, int, int) {}
static void lv_obj_set_style_pad_column(lv_obj_t *, int, int) {}
static void lv_obj_set_style_line_width(lv_obj_t *, int, int) {}
static void lv_obj_set_style_line_color(lv_obj_t *, unsigned, int) {}
static void lv_line_set_points(lv_obj_t *, const lv_point_t *, int) {}
static void lv_obj_set_style_text_font(lv_obj_t *, Font *, int) {}
static void lv_obj_set_style_text_color(lv_obj_t *p, unsigned c, int)
{
    p->color = c;
}
static void lv_obj_set_style_text_line_space(lv_obj_t *, int, int) {}
static void lv_obj_align(lv_obj_t *, int, int, int) {}
static void lv_obj_center(lv_obj_t *) {}
static void lv_obj_add_event_cb(lv_obj_t *, void (*)(lv_event_t *), int, void *) {}
static lv_obj_t *lv_obj_get_parent(lv_obj_t *p)
{
    return p->parent;
}
static void lv_label_set_text(lv_obj_t *p, const char *t)
{
    p->text = t;
}
static void lv_label_set_recolor(lv_obj_t *p, bool r)
{
    p->recolor = r;
}
static void lv_label_set_long_mode(lv_obj_t *p, int m)
{
    p->mode = m;
}
template <class... A> static void lv_label_set_text_fmt(lv_obj_t *p, const char *f, A... a)
{
    char b[200];
    snprintf(b, sizeof b, f, a...);
    p->text = b;
}
static void lv_txt_get_size(lv_point_t *p, const char *t, Font *f, int, int, int w, int)
{
    p->x = w;
    p->y = std::max(1, (int(strlen(t)) * 7 + w - 1) / w) * f->height;
}
static int lv_font_get_line_height(Font *f)
{
    return f->height;
}
static unsigned lv_color_hex(unsigned c)
{
    return c;
}
static void styleButton(lv_obj_t *) {}
static void navSetTip(lv_obj_t *p, const char *t)
{
    p->tip = t;
}
static const char *lv_textarea_get_text(lv_obj_t *p)
{
    return p->text.c_str();
}
static void setTextareaSynced(lv_obj_t *p, const char *t)
{
    p->text = t;
}
static void kbMirrorSyncToReal() {}
struct Task {
    std::string alert;
    void showAlert(const char *t, int) { alert = t; }
};
static Task task;
static struct {
    Task *task = &::task;
} g_lv;
struct Backend {
    uint32_t self = 1;
    bool editable = true, accept = true, managed = false, ready = true;
    unsigned submitted = 0;
    std::string status = "Initial status", queued;
    struct {
        char long_name[40] = "My node", short_name[5] = "SELF";
    } owner;
    uint32_t nodeNumber() const { return self; }
    bool canEditStatus() const { return editable; }
    bool channelsReady() const { return ready; }
    bool isManaged() const { return managed; }
    static constexpr size_t statusMaxBytes() { return 79; }
    const char *nodeStatus(uint32_t) const { return status.c_str(); }
    bool setStatusMessage(const char *t)
    {
        ++submitted;
        if (!accept)
            return false;
        queued = t;
        return true;
    }
    bool setOwner(const char *, const char *) { return true; }
    const auto &ownUser() const { return owner; }
} backend;
static Backend *wadaBackend = &backend;
struct ActItem {
    const char *icon;
    const char *label;
    void (*fn)();
    bool danger;
    lv_obj_t *(*glyph)(lv_obj_t *) = nullptr;
};
static std::vector<ActItem> actions;
static void openActionList(const char *, ActItem *a, int n)
{
    actions.assign(a, a + n);
}
static uint32_t wadaMenuNode = 0;
static void wadaNodeDetails() {}
static lv_obj_t *s_fm_prompt = nullptr, *s_fm_prompt_ta = nullptr;
static void wadaStatusPromptClosed();
static void fmPromptClose()
{
    wadaStatusPromptClosed();
    s_fm_prompt = s_fm_prompt_ta = nullptr;
}
static void fmTextPrompt(const char *, const char *initial, void (*)(const char *), uint32_t max_len = 63)
{
    fmPromptClose();
    s_fm_prompt = lv_obj_create(nullptr);
    auto *card = lv_obj_create(s_fm_prompt);
    s_fm_prompt_ta = lv_obj_create(card);
    s_fm_prompt_ta->text = initial;
    s_fm_prompt_ta->maxLength = max_len;
    s_fm_prompt_ta->oneLine = true;
}
#include "WadaStatusUI.inc"

// Editor depends on the renderer and its declarations.
#include "WadaStatusEditor.inc"
int main()
{
    auto *parent = lv_obj_create(nullptr);
    assert(!wadaStatusRow(parent, "", 180, false));
    assert(!wadaStatusRow(parent, " \t\xc2\xa0\xe3\x80\x80", 180, false));
    const char *raw = "#ff0000 <b>https://example.com @[name]";
    auto *card = wadaStatusRow(parent, raw, 100, false);
    auto *value = card->children.back();
    assert(value->text == raw && !value->recolor && value->color == COLOR_TEXT);
    assert(value->mode == LV_LABEL_LONG_DOT && value->height == 28 && card->height == 28);
    auto *details = wadaStatusRow(parent, raw, 100, true);
    value = details->children.back();
    assert(value->text == raw && value->mode == LV_LABEL_LONG_WRAP && value->height == LV_SIZE_CONTENT && !value->recolor);
    assert(details->children.front()->children.back()->text == "Status Message");
    wadaOwnIdentityMenu();
    assert(actions.size() == 3 && actions[0].label == std::string("Edit display name") && actions[1].glyph == wadaStatusNotes);
    wadaStatusEditorOpen();
    assert(s_status_edit && s_status_accepted == std::string("Initial status"));
    assert(s_fm_prompt_ta->maxLength == 0 && s_fm_prompt_ta->oneLine && s_status_budget->text == "14 / 79 bytes");
    std::string boundary(75, 'a');
    boundary += "\xf0\x9f\x98\x80";
    s_fm_prompt_ta->text = boundary;
    wadaStatusChanged(nullptr);
    assert(s_status_accepted == boundary);
    s_fm_prompt_ta->text = boundary + "x";
    wadaStatusChanged(nullptr);
    assert(s_fm_prompt_ta->text == boundary && !s_status_error->text.empty());
    s_fm_prompt_ta->text = "line\nbreak";
    wadaStatusChanged(nullptr);
    assert(s_fm_prompt_ta->text == boundary);
    backend.status = "Late broadcast/config";
    assert(s_fm_prompt_ta->text == boundary);
    backend.self = 2;
    assert(wadaStatusSubmit() && backend.submitted == 0 && s_status_edit);
    backend.self = 1;
    backend.editable = false;
    wadaStatusSubmit();
    assert(backend.submitted == 0 && s_status_edit);
    backend.editable = true;
    backend.accept = false;
    wadaStatusSubmit();
    assert(backend.submitted == 1 && s_status_edit);
    backend.accept = true;
    wadaStatusSubmit();
    assert(!s_status_edit && backend.queued == boundary && backend.status == "Late broadcast/config");
    wadaStatusEditorOpen();
    s_fm_prompt_ta->text = "";
    wadaStatusChanged(nullptr);
    assert(s_status_clear->flags & LV_OBJ_FLAG_HIDDEN);
    assert(s_status_budget->text == "0 / 79 bytes");
    wadaStatusSubmit();
    assert(backend.queued.empty() && backend.status == "Late broadcast/config");
    wadaStatusEditorOpen();
    auto submitted = backend.submitted;
    wadaStatusSubmit();
    assert(backend.submitted == submitted && !s_status_edit);
    backend.status = "a\nb";
    wadaStatusEditorOpen();
    assert(s_status_initial == std::string("a\nb") && s_fm_prompt_ta->text.empty());
    assert(!(s_status_clear->flags & LV_OBJ_FLAG_HIDDEN) && !s_status_error->text.empty());
    wadaStatusClearCb(nullptr);
    wadaStatusSubmit();
    assert(backend.queued.empty() && backend.status == "a\nb");
    backend.editable = false;
    wadaOwnIdentityMenu();
    assert(actions.size() == 2);
    wadaStatusEditorOpen();
    assert(!s_status_edit);
    backend.managed = true;
    wadaOwnIdentityMenu();
    assert(actions.size() == 1);
    puts("PASS: native status raw display, byte-safe popup, stale-self gates and queued-only updates");
}
