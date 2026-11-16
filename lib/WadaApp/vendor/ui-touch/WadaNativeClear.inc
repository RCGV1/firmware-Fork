#if defined(WADA_MESHTASTIC)
static uint32_t s_native_clear_candidates[256] = {}, s_native_clear_pending[256] = {};
static unsigned s_native_clear_count = 0, s_native_clear_waiting = 0, s_native_clear_removed = 0, s_native_clear_kept = 0;
static char s_native_clear_reason[120] = {};
static bool wadaUnheardRemovable(uint32_t number, bool localFavorite) {
  if (!wadaBackend || !wadaBackend->nodeSnapshotReady() || !wadaBackend->reportsHeardOnCurrentLora() ||
      !number || number == wadaBackend->nodeNumber() || localFavorite || wadaBackend->isManaged()) return false;
  const auto* node = wadaBackend->nodeInfo(number);
  return node && !node->via_mqtt && !node->is_favorite && wadaBackend->unheardOnCurrentLora(number);
}
static unsigned wadaCollectUnheard(uint32_t* out, unsigned capacity, bool removableOnly) {
  if (!wadaBackend || !wadaBackend->nodeSnapshotReady() || !wadaBackend->reportsHeardOnCurrentLora()) return 0;
  unsigned count = 0;
#if defined(ESP32)
  uint8_t favorites[TOUCH_FAVORITES_MAX * TOUCH_FAVORITE_KEY_BYTES];
  const int favoritesCount = touchPrefsCopyFavorites(favorites);
#endif
  for (int i = 0; i < the_mesh.getNumContacts(); ++i) {
    ContactInfo contact{};
    if (!the_mesh.getContactByIdx(i, contact)) continue;
    const uint32_t number = WadaMeshtasticBackend::keyNode(contact.id.pub_key);
    if (!wadaBackend->unheardOnCurrentLora(number)) continue;
#if defined(ESP32)
    const bool localFavorite = touchPrefsFavoritesSnapshotContains(favorites, favoritesCount, contact.id.pub_key);
#else
    const bool localFavorite = false;
#endif
    if (removableOnly && !wadaUnheardRemovable(number, localFavorite)) continue;
    if (out && count < capacity) out[count] = number;
    ++count;
  }
  return out ? LV_MIN(count, capacity) : count;
}
static void wadaClearFinished() {
  char text[280];
  snprintf(text, sizeof(text), TR("Removed %u nodes; %u kept or unconfirmed.%s%s"), s_native_clear_removed, s_native_clear_kept,
    s_native_clear_reason[0] ? "\n" : "", s_native_clear_reason);
  if (g_lv.task) g_lv.task->showAlert(text, 5000);
  s_ct_list_force = true;
  g_lv.dirty_contacts = true;
}
static void wadaQueueUnheardRemoval() {
  if (!wadaBackend || wadaBackend->removingUnheardNodes() || s_native_clear_waiting) return;
  s_native_clear_removed = s_native_clear_kept = 0;
  s_native_clear_reason[0] = '\0';
  if (wadaBackend->isManaged()) snprintf(s_native_clear_reason, sizeof(s_native_clear_reason), "%s", TR("Node is managed; local removal is unavailable."));
  memset(s_native_clear_pending, 0, sizeof(s_native_clear_pending));
#if defined(ESP32)
  uint8_t favorites[TOUCH_FAVORITES_MAX * TOUCH_FAVORITE_KEY_BYTES];
  const int favoritesCount = touchPrefsCopyFavorites(favorites);
#endif
  for (unsigned i = 0; i < s_native_clear_count; ++i) {
    const uint32_t number = s_native_clear_candidates[i];
    uint8_t key[32]; WadaMeshtasticBackend::nodeKey(number, key);
#if defined(ESP32)
    const bool localFavorite = touchPrefsFavoritesSnapshotContains(favorites, favoritesCount, key);
#else
    const bool localFavorite = false;
#endif
    if (!wadaUnheardRemovable(number, localFavorite) || !wadaBackend->removeUnheardNode(number)) {
      ++s_native_clear_kept;
      continue;
    }
    s_native_clear_pending[s_native_clear_waiting++] = number;
  }
  if (!s_native_clear_waiting) wadaClearFinished();
  else if (g_lv.task) {
    char text[120];
    snprintf(text, sizeof(text), TR("Removal requested for %u nodes. Waiting for the node database to confirm."), s_native_clear_waiting);
    g_lv.task->showAlert(text, 3000);
  }
  s_ct_list_force = true;
  g_lv.dirty_contacts = true;
}
static void wadaConfirmClearUnheard() {
  if (wadaBackend && wadaBackend->isManaged()) {
    if (g_lv.task) g_lv.task->showAlert(TR("Node is managed. Remove nodes through an authorized Meshtastic administrator."), 3500);
    return;
  }
  if (!wadaBackend || wadaBackend->removingUnheardNodes() || s_native_clear_waiting) {
    if (g_lv.task) g_lv.task->showAlert(TR("Node removal is still awaiting confirmation"), 2500);
    return;
  }
  s_native_clear_count = wadaCollectUnheard(s_native_clear_candidates, 256, true);
  if (!s_native_clear_count) {
    if (g_lv.task) g_lv.task->showAlert(TR("No eligible nodes to remove. Favorites are kept."), 2000);
    return;
  }
  char text[220];
  snprintf(text, sizeof(text), TR("Remove %u nodes not heard on current LoRa? Favorites, MQTT nodes and history stay. Nodes may reappear."), s_native_clear_count);
  showConfirm(text, TR("Remove"), wadaQueueUnheardRemoval);
}
static void wadaSecurityFilterCb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  const uintptr_t filter = uintptr_t(lv_event_get_user_data(event));
  if (filter == 1) s_native_nodes_signed = !s_native_nodes_signed;
  else if (filter == 2) s_native_nodes_encrypted = !s_native_nodes_encrypted;
  else return;
  if (s_ct_filter_btn_lbl) lv_label_set_text_fmt(s_ct_filter_btn_lbl, "%s %s", LV_SYMBOL_DOWN, contactsFilterShortLabel());
  ctSortSheetClose();
  s_ct_list_force = true;
  g_lv.dirty_contacts = true;
}
static void wadaHideUnheardCb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  s_native_hide_unheard = !s_native_hide_unheard;
  if (s_ct_filter_btn_lbl) lv_label_set_text_fmt(s_ct_filter_btn_lbl, "%s %s", LV_SYMBOL_DOWN, contactsFilterShortLabel());
  ctSortSheetClose();
  s_ct_list_force = true;
  g_lv.dirty_contacts = true;
}
static void wadaUnheardBanner(lv_obj_t* list, unsigned count, unsigned eligible) {
  if (!count && !s_native_clear_waiting) return;
  auto* card = lv_obj_create(list);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
  styleSurface(card, COLOR_PANEL, 8);
  lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 6, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  auto* text = lv_label_create(card);
  lv_obj_set_width(text, lv_pct(100));
  lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
  if (count) lv_label_set_text_fmt(text, TR("%u nodes not heard on your current LoRa settings"), count);
  else lv_label_set_text_fmt(text, TR("Waiting for %u node removal results"), s_native_clear_waiting);
  lv_obj_set_style_text_font(text, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(text, lv_color_hex(COLOR_STATUS_WARN_TEXT), LV_PART_MAIN);
  auto* button = lv_btn_create(card);
  styleButton(button);
  lv_obj_set_size(button, LV_SIZE_CONTENT, 30);
  if (!eligible || s_native_clear_waiting || wadaBackend->removingUnheardNodes()) lv_obj_add_state(button, LV_STATE_DISABLED);
  lv_obj_add_event_cb(button, [](lv_event_t*) { wadaConfirmClearUnheard(); }, LV_EVENT_CLICKED, nullptr);
  auto* label = lv_label_create(button);
  lv_label_set_text(label, s_native_clear_waiting ? TR("Waiting for confirmation") : TR("Clear eligible nodes"));
  lv_obj_set_style_text_font(label, &g_font_12, LV_PART_MAIN);
  lv_obj_center(label);
}
#endif
