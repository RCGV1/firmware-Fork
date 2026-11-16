// Headless test driver only: operates public LVGL widget APIs in the preview process.
#include "ScriptUI.h"
#include "DesktopRadio.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <lvgl.h>
#include <sstream>
#include <unordered_map>
#include <vector>
namespace
{
std::unordered_map<lv_obj_t *, unsigned> ids;
unsigned nextId = 1;
unsigned id(lv_obj_t *o)
{
    auto it = ids.find(o);
    if (it != ids.end())
        return it->second;
    return ids[o] = nextId++;
}
bool exposed(lv_obj_t *o)
{
    lv_area_t screen;
    lv_obj_get_coords(lv_scr_act(), &screen);
    for (auto *p = o; p; p = lv_obj_get_parent(p)) {
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN))
            return false;
        lv_area_t area;
        lv_obj_get_coords(p, &area);
        if (area.x2 < screen.x1 || area.x1 > screen.x2)
            return false;
    }
    return true;
}
std::vector<lv_obj_t *> objects()
{
    lv_obj_update_layout(lv_scr_act());
    lv_obj_update_layout(lv_layer_top());
    std::vector<lv_obj_t *> out;
    auto walk = [&](auto &&self, lv_obj_t *o) -> void {
        if (!o || !exposed(o) || out.size() >= 8192)
            return;
        out.push_back(o);
        id(o);
        for (unsigned i = 0; i < lv_obj_get_child_cnt(o); ++i)
            self(self, lv_obj_get_child(o, i));
    };
    walk(walk, lv_scr_act());
    walk(walk, lv_layer_top());
    return out;
}
const char *kind(lv_obj_t *o)
{
    if (lv_obj_check_type(o, &lv_textarea_class))
        return "textarea";
    if (lv_obj_check_type(o, &lv_dropdown_class))
        return "dropdown";
    if (lv_obj_check_type(o, &lv_switch_class))
        return "switch";
    if (lv_obj_check_type(o, &lv_checkbox_class))
        return "checkbox";
    if (lv_obj_check_type(o, &lv_label_class))
        return "label";
    if (lv_obj_check_type(o, &lv_btn_class))
        return "button";
    if (lv_obj_check_type(o, &lv_slider_class))
        return "slider";
    return "object";
}
std::string text(lv_obj_t *o)
{
    if (lv_obj_check_type(o, &lv_label_class))
        return lv_label_get_text(o);
    if (lv_obj_check_type(o, &lv_textarea_class))
        return lv_textarea_get_text(o);
    if (lv_obj_check_type(o, &lv_checkbox_class))
        return lv_checkbox_get_text(o);
    if (lv_obj_check_type(o, &lv_dropdown_class)) {
        char b[256];
        lv_dropdown_get_selected_str(o, b, sizeof b);
        return b;
    }
    return "";
}
bool isKind(lv_obj_t *o, const char *wanted)
{
    const std::string type = kind(o);
    return !wanted || type == wanted ||
           (std::string(wanted) == "value" &&
            (type == "textarea" || type == "dropdown" || type == "switch" || type == "checkbox"));
}
void descendants(lv_obj_t *o, const char *wanted, std::vector<lv_obj_t *> &out)
{
    if (!o || !exposed(o))
        return;
    if (isKind(o, wanted))
        out.push_back(o);
    for (unsigned i = 0; i < lv_obj_get_child_cnt(o); ++i)
        descendants(lv_obj_get_child(o, i), wanted, out);
}
lv_obj_t *resolve(const std::string &selector, const char *wanted, std::string &error)
{
    auto all = objects();
    std::vector<lv_obj_t *> matched;
    if (selector.size() > 1 && selector[0] == '@') {
        for (auto *o : all)
            if (std::to_string(id(o)) == selector.substr(1) && isKind(o, wanted))
                matched.push_back(o);
    } else {
        for (auto *o : all)
            if (text(o) == selector)
                matched.push_back(o);
        auto top = [](lv_obj_t *o) {
            for (auto *p = o; p; p = lv_obj_get_parent(p))
                if (p == lv_layer_top())
                    return true;
            return false;
        };
        if (std::any_of(matched.begin(), matched.end(), top))
            matched.erase(std::remove_if(matched.begin(), matched.end(), [&](auto *o) { return !top(o); }), matched.end());
        if (wanted) {
            std::vector<lv_obj_t *> targets;
            for (auto *o : matched) {
                if (isKind(o, wanted)) {
                    targets.push_back(o);
                    continue;
                }
                auto *branch = o;
                for (auto *p = lv_obj_get_parent(o); p; branch = p, p = lv_obj_get_parent(p)) {
                    std::vector<lv_obj_t *> children;
                    descendants(p, wanted, children);
                    if (children.size() == 1) {
                        targets.push_back(children[0]);
                        break;
                    }
                    if (children.size() > 1) {
                        for (unsigned i = 0; i + 1 < lv_obj_get_child_cnt(p); ++i)
                            if (lv_obj_get_child(p, i) == branch) {
                                std::vector<lv_obj_t *> next;
                                descendants(lv_obj_get_child(p, i + 1), wanted, next);
                                if (next.size() == 1)
                                    targets.push_back(next[0]);
                                break;
                            }
                        break;
                    }
                }
            }
            matched = std::move(targets);
        }
    }
    std::sort(matched.begin(), matched.end());
    matched.erase(std::unique(matched.begin(), matched.end()), matched.end());
    if (matched.size() != 1) {
        error = "Selector must match one " + std::string(wanted ? wanted : "object") + ": " + selector + " (matches " +
                std::to_string(matched.size()) + ")";
        return nullptr;
    }
    if (lv_obj_has_state(matched[0], LV_STATE_DISABLED)) {
        error = "Disabled control: " + selector;
        return nullptr;
    }
    return matched[0];
}
std::filesystem::path outputPath(const std::string &dir, const std::string &name)
{
    std::filesystem::path p(name);
    return p.is_absolute() ? p : std::filesystem::path(dir) / p;
}
bool writeFile(const std::string &dir, const std::string &name, const std::string &body, std::string &error)
{
    auto p = outputPath(dir, name);
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p);
    f << body;
    if (!f) {
        error = "Cannot write " + p.string();
        return false;
    }
    return true;
}
std::string dump()
{
    lv_obj_update_layout(lv_scr_act());
    lv_obj_update_layout(lv_layer_top());
    JsonDocument doc;
    auto list = doc["objects"].to<JsonArray>();
    for (auto *o : objects()) {
        auto v = list.add<JsonObject>();
        v["id"] = id(o);
        v["parent"] = lv_obj_get_parent(o) ? id(lv_obj_get_parent(o)) : 0;
        v["kind"] = kind(o);
        v["text"] = text(o);
        v["checked"] = lv_obj_has_state(o, LV_STATE_CHECKED);
        v["disabled"] = lv_obj_has_state(o, LV_STATE_DISABLED);
        v["visible"] = lv_obj_is_visible(o);
        lv_area_t a;
        lv_obj_get_coords(o, &a);
        v["x"] = a.x1;
        v["y"] = a.y1;
        v["w"] = lv_area_get_width(&a);
        v["h"] = lv_area_get_height(&a);
        if (lv_obj_check_type(o, &lv_dropdown_class))
            v["options"] = lv_dropdown_get_options(o);
    }
    std::string out;
    serializeJsonPretty(doc, out);
    return out;
}
std::string arg(std::istream &in)
{
    std::string s;
    in >> std::quoted(s);
    return s;
}
} // namespace
bool desktopUiCommand(const std::string &cmd, std::istream &in, const std::string &dir, std::string &error)
{
    if (cmd == "dump-ui") {
        writeFile(dir, arg(in), dump(), error);
        return true;
    }
    if (cmd == "state") {
        writeFile(dir, arg(in), desktopSimulationState(), error);
        return true;
    }
    if (cmd == "scenario") {
        auto value = arg(in);
        if (!desktopSimulationScenario(value.c_str()))
            error = "Unknown scenario: " + value;
        return true;
    }
    if (cmd == "expect-text" || cmd == "expect-no-text") {
        auto value = arg(in);
        bool found = false;
        for (auto *o : objects())
            if (text(o) == value)
                found = true;
        if (found != (cmd == "expect-text"))
            error = "Text assertion failed: " + value;
        return true;
    }
    if (cmd == "expect-state") {
        const auto path = arg(in), expected = arg(in);
        JsonDocument doc;
        auto e = deserializeJson(doc, desktopSimulationState());
        if (e) {
            error = "Invalid fixture JSON state";
            return true;
        }
        JsonVariantConst value = doc.as<JsonVariantConst>();
        std::istringstream segments(path);
        std::string key;
        while (std::getline(segments, key, '.')) {
            if (value.is<JsonArrayConst>()) {
                char *end = nullptr;
                auto index = strtoul(key.c_str(), &end, 10);
                if (!end || *end) {
                    value = JsonVariantConst();
                    break;
                }
                value = value[index];
            } else
                value = value[key];
        }
        std::string actual;
        if (value.is<const char *>())
            actual = value.as<const char *>();
        else
            serializeJson(value, actual);
        if (actual != expected)
            error = "State assertion " + path + ": expected " + expected + ", got " + actual;
        return true;
    }
    if (cmd == "scroll-to") {
        auto *o = resolve(arg(in), nullptr, error);
        if (o)
            lv_obj_scroll_to_view_recursive(o, LV_ANIM_OFF);
        return true;
    }
    if (cmd == "click-text" || cmd == "long-text") {
        const auto selector = arg(in);
        auto *o = resolve(selector, nullptr, error);
        if (!o)
            return true;
        while (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE))
            o = lv_obj_get_parent(o);
        if (!o || lv_obj_has_state(o, LV_STATE_DISABLED)) {
            error = "No enabled clickable ancestor: " + selector;
            return true;
        }
        lv_obj_scroll_to_view_recursive(o, LV_ANIM_OFF);
        lv_event_send(o, cmd == "long-text" ? LV_EVENT_LONG_PRESSED : LV_EVENT_CLICKED, nullptr);
        return true;
    }
    if (cmd == "set-text") {
        auto selector = arg(in), value = arg(in);
        auto *o = resolve(selector, "textarea", error);
        if (!o)
            return true;
        lv_textarea_set_text(o, value.c_str());
        lv_event_send(o, LV_EVENT_FOCUSED, nullptr);
        return true;
    }
    if (cmd == "choose") {
        auto selector = arg(in), value = arg(in);
        auto *o = resolve(selector, "dropdown", error);
        if (!o)
            return true;
        std::istringstream opts(lv_dropdown_get_options(o));
        std::string option;
        unsigned index = 0;
        bool found = false;
        while (std::getline(opts, option)) {
            if (option == value) {
                found = true;
                break;
            }
            ++index;
        }
        if (!found) {
            error = "Dropdown option missing: " + value;
            return true;
        }
        lv_dropdown_set_selected(o, index);
        lv_event_send(o, LV_EVENT_VALUE_CHANGED, nullptr);
        return true;
    }
    if (cmd == "toggle") {
        auto selector = arg(in), value = arg(in);
        auto *o = resolve(selector, "switch", error);
        if (!o) {
            error.clear();
            o = resolve(selector, "checkbox", error);
        }
        if (!o)
            return true;
        bool checked = lv_obj_has_state(o, LV_STATE_CHECKED);
        if (value == "on" || value == "true")
            checked = true;
        else if (value == "off" || value == "false")
            checked = false;
        else if (value.empty())
            checked = !checked;
        else {
            error = "Toggle expects on/off";
            return true;
        }
        if (checked)
            lv_obj_add_state(o, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(o, LV_STATE_CHECKED);
        lv_event_send(o, LV_EVENT_VALUE_CHANGED, nullptr);
        return true;
    }
    if (cmd == "expect-value") {
        auto selector = arg(in), expected = arg(in);
        auto *o = resolve(selector, "value", error);
        if (!o)
            return true;
        const auto actual = (std::string(kind(o)) == "switch" || std::string(kind(o)) == "checkbox")
                                ? (lv_obj_has_state(o, LV_STATE_CHECKED) ? "on" : "off")
                                : text(o);
        if (actual != expected)
            error = "Widget assertion " + selector + ": expected " + expected + ", got " + actual;
        return true;
    }
    return false;
}
