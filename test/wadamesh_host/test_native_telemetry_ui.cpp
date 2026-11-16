// Exercises the extracted native telemetry functions in UITask.cpp against a minimal
// LVGL/backend fixture. Node details must offer per-type logs and requests without
// requesting on open. Late replies must refresh only the matching live log, and
// closing or changing the modal must never write through its stale label pointer.
#include "WadaTelemetry.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace wada
{
const char *telemetryName(TelemetryKind)
{
    return "Telemetry";
}
} // namespace wada

using std::isfinite;

#define TR(value) (value)
constexpr int LV_EVENT_CLICKED = 1, LV_FLEX_FLOW_COLUMN = 2, LV_PART_MAIN = 0, SET_PRIMARY = 1;
constexpr const char *UI_ICON_REFRESH_CW = "refresh";
constexpr const char *LV_SYMBOL_LEFT = "left";
int g_font_14;
struct lv_obj_t {
    std::string text;
};
struct lv_event_t {
    int code;
    void *user_data = nullptr;
};
using lv_event_cb_t = void (*)(lv_event_t *);
static int lv_event_get_code(lv_event_t *e)
{
    return e->code;
}
static void *lv_event_get_user_data(lv_event_t *e)
{
    return e->user_data;
}
static void lv_obj_set_flex_flow(lv_obj_t *, int) {}
static void lv_obj_set_style_pad_row(lv_obj_t *, int, int) {}
static void lv_obj_set_style_text_font(lv_obj_t *, int *, int) {}
static void lv_label_set_text(lv_obj_t *obj, const char *text)
{
    obj->text = text;
}
enum class SettingsModalKind { None, SystemInfo, NativeTelemetry };
static struct {
    lv_obj_t *root = nullptr;
    SettingsModalKind kind = SettingsModalKind::None;
} g_set_modal;
static std::vector<std::unique_ptr<lv_obj_t>> objects;
static lv_obj_t *object(const char *text = "")
{
    objects.push_back(std::make_unique<lv_obj_t>());
    objects.back()->text = text;
    return objects.back().get();
}
struct Button {
    std::string title;
    lv_event_cb_t callback;
    void *data;
};
static std::vector<Button> buttons;
static lv_obj_t *createSettingsModal(const char *, SettingsModalKind kind)
{
    buttons.clear();
    g_set_modal.root = object();
    g_set_modal.kind = kind;
    return g_set_modal.root;
}
static lv_obj_t *setNote(lv_obj_t *, const char *text)
{
    return object(text);
}
static lv_obj_t *setButton(lv_obj_t *, const char *, const char *title, lv_event_cb_t cb, void *data = nullptr, int = 0)
{
    buttons.push_back({title, cb, data});
    return object(title);
}
static void click(unsigned index)
{
    const Button button = buttons.at(index);
    lv_event_t event{LV_EVENT_CLICKED, button.data};
    button.callback(&event);
}
struct Node {
    bool has_user = true;
    struct {
        char long_name[40] = "Test node";
    } user;
};
struct Backend {
    bool host = false, accepted = true;
    unsigned reads = 0;
    std::string readings;
    std::vector<std::pair<uint32_t, wada::TelemetryKind>> requests;
    Node node;
    const Node *nodeInfo(uint32_t) const { return &node; }
    bool telemetryAvailable(uint32_t node, wada::TelemetryKind kind) const
    {
        return node && (kind != wada::TelemetryKind::Host || host);
    }
    bool requestTelemetry(uint32_t node, wada::TelemetryKind kind)
    {
        requests.push_back({node, kind});
        return accepted;
    }
    size_t telemetryLog(uint32_t, wada::TelemetryKind, char *text, size_t cap, bool imperial = false)
    {
        assert(!imperial);
        ++reads;
        snprintf(text, cap, "%s", readings.c_str());
        return readings.size();
    }
};
static Backend backend;
static Backend *wadaBackend = &backend;
class UITask
{
  public:
    std::vector<std::string> alerts;
    void showAlert(const char *text, unsigned) { alerts.emplace_back(text); }
    void onMeshtasticTelemetry(uint32_t node, uint8_t kind);
    void onMeshtasticTelemetryStatus(uint32_t node, uint8_t kind, const char *message);
};
static uint32_t s_native_telem_node = 0;
static uint8_t s_native_telem_kind = UINT8_MAX;
static lv_obj_t *s_native_telem_log = nullptr;
static void wadaTelemetryPicker(uint32_t node);

#include "native_telemetry_ui.inc"

int main()
{
    char detail[96], value[24];
    meshtastic_NodeInfo node{};
    assert(!wadaDeviceWidgetReading(&node, detail, sizeof(detail), value, sizeof(value)));
    node.has_device_metrics = true;
    assert(wadaDeviceWidgetReading(&node, detail, sizeof(detail), value, sizeof(value)));
    assert(std::string(detail) == "No device readings reported" && !value[0]);
    node.device_metrics.has_voltage = node.device_metrics.has_battery_level = true;
    assert(wadaDeviceWidgetReading(&node, detail, sizeof(detail), value, sizeof(value)));
    assert(std::string(detail) == "0.00 V \xC2\xB7 0% battery");
    node.device_metrics.battery_level = 101;
    node.device_metrics.voltage = NAN;
    wadaDeviceWidgetReading(&node, detail, sizeof(detail), value, sizeof(value));
    assert(std::string(detail) == "External power" && std::string(value) == "External power");

    UITask ui;
    wadaTelemetryPicker(0x12345678);
    assert(buttons.size() == 5);
    assert(backend.requests.empty());
    click(2);
    assert(s_native_telem_kind == uint8_t(wada::TelemetryKind::AirQuality));
    assert(s_native_telem_node == 0x12345678);
    assert(s_native_telem_log->text.find("No readings yet") != std::string::npos);
    assert(backend.requests.empty());
    click(0);
    assert(backend.requests.size() == 1);
    assert(backend.requests.back().first == 0x12345678);
    assert(backend.requests.back().second == wada::TelemetryKind::AirQuality);
    assert(ui.alerts.empty());

    backend.readings = "PM2.5: 0 ug/m3";
    unsigned reads = backend.reads;
    ui.onMeshtasticTelemetry(0x87654321, s_native_telem_kind);
    ui.onMeshtasticTelemetry(s_native_telem_node, uint8_t(wada::TelemetryKind::Device));
    assert(backend.reads == reads);
    ui.onMeshtasticTelemetry(s_native_telem_node, s_native_telem_kind);
    assert(backend.reads == reads + 1);
    assert(s_native_telem_log->text == backend.readings);

    // A destroyed log remains referenced in fixture memory so any accidental stale
    // update is observable; the real LVGL object would already have been freed.
    lv_obj_t *previous = s_native_telem_log;
    createSettingsModal("Node details", SettingsModalKind::SystemInfo);
    backend.readings = "PM2.5: 7 ug/m3";
    ui.onMeshtasticTelemetry(s_native_telem_node, s_native_telem_kind);
    assert(previous->text == "PM2.5: 0 ug/m3");
    assert(backend.reads == reads + 1);
    lv_event_t event{LV_EVENT_CLICKED};
    wadaTelemetryRequestCb(&event);
    assert(backend.requests.size() == 1);
    ui.onMeshtasticTelemetryStatus(0x12345678, uint8_t(wada::TelemetryKind::AirQuality),
                                   "Air quality request from Test node failed: No route");
    assert(ui.alerts.size() == 1);
    ui.onMeshtasticTelemetryStatus(0x12345678, UINT8_MAX, "bad");
    ui.onMeshtasticTelemetryStatus(0x12345678, 0, nullptr);
    assert(ui.alerts.size() == 1);

    wadaTelemetryPicker(0x87654321);
    click(0);
    reads = backend.reads;
    ui.onMeshtasticTelemetry(0x12345678, 0);
    assert(backend.reads == reads);
    g_set_modal.root = nullptr;
    ui.onMeshtasticTelemetry(0x87654321, 0);
    wadaTelemetryRequestCb(&event);
    assert(backend.reads == reads && backend.requests.size() == 1);

    backend.host = true;
    wadaTelemetryPicker(0x87654321);
    assert(buttons.size() == 6);
    click(5);
    assert(s_native_telem_kind == uint8_t(wada::TelemetryKind::Host));
    click(0);
    assert(backend.requests.back().second == wada::TelemetryKind::Host);
    backend.host = false;
    wadaTelemetryPicker(0x87654321);
    wadaTelemetryOpenLog(uint8_t(wada::TelemetryKind::Host));
    assert(s_native_telem_kind == UINT8_MAX && buttons.size() == 5);
    wadaTelemetryOpenLog(UINT8_MAX);
    assert(s_native_telem_kind == UINT8_MAX);

    wadaBackend = nullptr;
    wadaTelemetryPicker(0x12345678);
    wadaTelemetryRefresh();
    wadaTelemetryRequestCb(&event);
    puts("PASS: native telemetry UI lifecycle and typed requests");
}
