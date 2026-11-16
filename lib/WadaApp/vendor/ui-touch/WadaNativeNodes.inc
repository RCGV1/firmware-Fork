#if defined(WADA_MESHTASTIC)
static bool wadaContactIsFavorite(const ContactInfo& c) { return (c.flags & 0x01) != 0; }
static bool s_native_hide_unheard = false;
static bool s_native_map_signed = false, s_native_map_encrypted = false;
static bool s_native_nodes_signed = false, s_native_nodes_encrypted = false;
static const char* wadaSecurityTitle(wada::NodeSecurity state) { return TR(wada::nodeSecurityName(state)); }
static const char* wadaSecurityHelp(wada::NodeSecurity state) { return TR(wada::nodeSecurityHelp(state)); }
static bool wadaNodeFactsMatch(uint32_t node, bool signedOnly, bool encryptedOnly) {
  return wadaBackend && (!signedOnly || wadaBackend->nodeSigned(node)) &&
    (!encryptedOnly || wadaBackend->nodeEncrypted(node));
}
static bool wadaMapAllowsNode(uint32_t node) {
  return wadaNodeFactsMatch(node, s_native_map_signed, s_native_map_encrypted);
}
static bool wadaNodeListAllowsNode(uint32_t node) {
  return wadaNodeFactsMatch(node, s_native_nodes_signed, s_native_nodes_encrypted);
}
static uint32_t s_native_details_node = 0, s_native_details_stamp = 0;
static uint32_t wadaNodeDetailsStamp(uint32_t number) {
  const auto* node = wadaBackend ? wadaBackend->nodeInfo(number) : nullptr;
  if (!node) return 0;
  uint32_t hash = 2166136261u;
  auto bytes = [&hash](const void* value, size_t length) {
    const auto* data = static_cast<const uint8_t*>(value);
    for (size_t i = 0; i < length; ++i) hash = (hash ^ data[i]) * 16777619u;
  };
  auto text = [&bytes](const char* value) { if (value) bytes(value, strlen(value) + 1); };
  text(node->user.long_name);
  text(node->user.short_name);
  text(wadaBackend->nodeStatus(number));
  bytes(&node->user.public_key.size, sizeof(node->user.public_key.size));
  bytes(node->user.public_key.bytes, sizeof(node->user.public_key.bytes));
  const uint32_t facts[] = {uint32_t(wadaBackend->nodeSecurity(number)), uint32_t(wadaBackend->unheardOnCurrentLora(number)),
    uint32_t(node->has_hops_away ? node->hops_away : 256u), uint32_t(node->via_mqtt), uint32_t(wadaBackend->canMessage(number))};
  bytes(facts, sizeof(facts));
  text(wadaBackend->nodeFirmwareVersion(number));
  if (!wadaBackend->canMessage(number)) text(wadaBackend->directMessageReason(number));
  return hash;
}
static void wadaSecurityExplain(wada::NodeSecurity state) {
  auto* body = createSettingsModal(wadaSecurityTitle(state), SettingsModalKind::NativeSecurity);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, 8, LV_PART_MAIN);
  setNote(body, wadaSecurityHelp(state));
}
static void wadaSecurityGlyphCb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  const uintptr_t value = uintptr_t(lv_event_get_user_data(event));
  if (value < 1 || value > 5) return;
  wadaSecurityExplain(wada::NodeSecurity(value - 1));
}
static lv_obj_t* wadaSecurityGlyph(lv_obj_t* parent, wada::NodeSecurity state, bool clickable = true) {
  auto* root = lv_obj_create(parent);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, 22, 22);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  const uint32_t color = state == wada::NodeSecurity::KeyMismatch ? COLOR_STATUS_DANGER_TEXT :
    state == wada::NodeSecurity::NoPublicKey ? COLOR_STATUS_WARN_TEXT : COLOR_STATUS_OK_TEXT;
  if (clickable) {
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, wadaSecurityGlyphCb, LV_EVENT_CLICKED, (void*)(uintptr_t(state) + 1));
    navSetTip(root, wadaSecurityTitle(state));
  } else lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);
  if (state == wada::NodeSecurity::KeyMismatch) {
    auto* ring = lv_obj_create(root);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 7, 7);
    lv_obj_set_pos(ring, 2, 2);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(ring, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(ring, lv_color_hex(color), LV_PART_MAIN);
    static const lv_point_t key[] = {{7, 8}, {17, 18}, {20, 15}, {17, 12}, {14, 15}};
    static const lv_point_t slash[] = {{2, 20}, {20, 2}};
    auto* shaft = lv_line_create(root);
    lv_line_set_points(shaft, key, sizeof(key) / sizeof(*key));
    lv_obj_set_style_line_color(shaft, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_line_width(shaft, 2, LV_PART_MAIN);
    auto* line = lv_line_create(root);
    lv_line_set_points(line, slash, sizeof(slash) / sizeof(*slash));
    lv_obj_set_style_line_color(line, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_line_width(line, 2, LV_PART_MAIN);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  } else {
    auto* glyph = lv_label_create(root);
    setIconLabel(glyph, state == wada::NodeSecurity::VerifiedContact ? UI_ICON_USER :
      state == wada::NodeSecurity::SignedNode ? UI_ICON_RADIO :
      state == wada::NodeSecurity::PublicKey ? UI_ICON_LOCK : UI_ICON_LOCK_OPEN);
    lv_obj_set_style_text_color(glyph, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_align(glyph, LV_ALIGN_TOP_LEFT, 0, 0);
    if (state == wada::NodeSecurity::VerifiedContact || state == wada::NodeSecurity::SignedNode) {
      auto* badge = lv_obj_create(root);
      lv_obj_remove_style_all(badge);
      lv_obj_set_size(badge, 10, 10);
      lv_obj_align(badge, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
      lv_obj_set_style_bg_color(badge, lv_color_hex(COLOR_BG), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
      static const lv_point_t shield[] = {{1, 1}, {8, 1}, {8, 6}, {5, 9}, {1, 6}, {1, 1}};
      static const lv_point_t check[] = {{2, 4}, {4, 6}, {7, 3}};
      auto* outline = lv_line_create(badge);
      lv_line_set_points(outline, shield, sizeof(shield) / sizeof(*shield));
      lv_obj_set_style_line_color(outline, lv_color_hex(color), LV_PART_MAIN);
      lv_obj_set_style_line_width(outline, 1, LV_PART_MAIN);
      auto* tick = lv_line_create(badge);
      lv_line_set_points(tick, check, sizeof(check) / sizeof(*check));
      lv_obj_set_style_line_color(tick, lv_color_hex(color), LV_PART_MAIN);
      lv_obj_set_style_line_width(tick, 1, LV_PART_MAIN);
    }
  }
  return root;
}
static void wadaSecurityLegend() {
  auto* body = createSettingsModal(TR("Security"), SettingsModalKind::NativeSecurity);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, 10, LV_PART_MAIN);
  const wada::NodeSecurity states[] = {wada::NodeSecurity::VerifiedContact, wada::NodeSecurity::SignedNode,
    wada::NodeSecurity::PublicKey, wada::NodeSecurity::NoPublicKey, wada::NodeSecurity::KeyMismatch};
  for (auto state : states) {
    auto* row = setBox(body);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
    wadaSecurityGlyph(row, state, false);
    auto* label = lv_label_create(row);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, wadaSecurityTitle(state));
    lv_obj_set_style_text_font(label, &g_font_semi_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    setNote(body, wadaSecurityHelp(state));
    setNote(body, state == wada::NodeSecurity::KeyMismatch || state == wada::NodeSecurity::VerifiedContact ?
      TR("Applies at every firmware version.") : state == wada::NodeSecurity::SignedNode ?
      TR("Firmware 2.8 or newer, or a signature verified by the radio.") :
      TR("Older or unknown firmware with no verification or heard signature."));
  }
}
#endif
