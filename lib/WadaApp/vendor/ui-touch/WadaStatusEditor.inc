#if defined(WADA_MESHTASTIC)
static bool s_status_edit = false, s_status_changing = false, s_status_clearable = false;
static uint32_t s_status_self = 0;
static size_t s_status_limit = 0;
static char s_status_initial[81] = {}, s_status_accepted[81] = {};
static lv_obj_t* s_status_clear = nullptr;
static lv_obj_t* s_status_budget = nullptr;
static lv_obj_t* s_status_error = nullptr;
static bool wadaIdentityPopupActive() { return s_fm_prompt != nullptr; }
static void wadaStatusPromptClosed() {
  s_status_edit = false;
  s_status_clear = s_status_budget = s_status_error = nullptr;
}
static void wadaStatusBudget() {
  if (s_status_budget)
    lv_label_set_text_fmt(s_status_budget, TR("%u / %u bytes"), unsigned(strlen(s_status_accepted)), unsigned(s_status_limit));
  if (s_status_clear) {
    if (s_status_accepted[0] || s_status_clearable) lv_obj_clear_flag(s_status_clear, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_status_clear, LV_OBJ_FLAG_HIDDEN);
  }
}
static void wadaStatusChanged(lv_event_t*) {
  if (!s_status_edit || s_status_changing || !s_fm_prompt_ta) return;
  const char* text = lv_textarea_get_text(s_fm_prompt_ta);
  if (wada::statusText(text, s_status_limit, true)) {
    memcpy(s_status_accepted, text, strlen(text) + 1);
    s_status_clearable = false;
    if (s_status_error) lv_label_set_text(s_status_error, "");
  } else {
    // Reject the complete edit rather than silently cutting a UTF-8 value.
    s_status_changing = true;
    setTextareaSynced(s_fm_prompt_ta, s_status_accepted);
    s_status_changing = false;
    if (s_status_error) lv_label_set_text(s_status_error, TR("Edit exceeds the byte limit or contains a line break."));
  }
  wadaStatusBudget();
}
static void wadaStatusClearCb(lv_event_t*) {
  s_status_changing = true;
  setTextareaSynced(s_fm_prompt_ta, "");
  s_status_accepted[0] = '\0';
  s_status_clearable = false;
  s_status_changing = false;
  lv_label_set_text(s_status_error, "");
  wadaStatusBudget();
}
static bool wadaStatusSubmit() {
  if (!s_status_edit) return false;
  kbMirrorSyncToReal();
  if (!wadaBackend || s_status_self != wadaBackend->nodeNumber() || !wadaBackend->canEditStatus()) {
    if (s_status_error) lv_label_set_text(s_status_error, TR("Reconnect to this node to update its status."));
    return true;
  }
  const char* text = lv_textarea_get_text(s_fm_prompt_ta);
  if (!wada::statusText(text, s_status_limit, true)) return true;
  if (strcmp(text, s_status_initial) == 0) { fmPromptClose(); return true; }
  if (!wadaBackend->setStatusMessage(text)) {
    if (s_status_error) lv_label_set_text(s_status_error, TR("Status update could not be queued. Try again."));
    return true;
  }
  fmPromptClose();
  if (g_lv.task) g_lv.task->showAlert(TR("Status update requested"), 2500);
  return true;
}
static void wadaStatusEditorOpen() {
  if (!wadaBackend || !wadaBackend->canEditStatus()) return;
  const uint32_t self = wadaBackend->nodeNumber();
  const size_t limit = LV_MIN(size_t(80), wadaBackend->statusMaxBytes());
  const char* value = wadaBackend->nodeStatus(self);
  if (!wada::statusText(value, limit, false)) value = "";
  const bool needsReplacement = !wada::statusText(value, limit, true);
  // The prompt owns its draft. Node/config refreshes never replace edited text.
  fmTextPrompt(TR("Status Message"), needsReplacement ? "" : value, nullptr, 0);
  s_status_edit = true;
  s_status_self = self;
  s_status_limit = limit;
  memcpy(s_status_initial, value, strlen(value) + 1);
  const char* draft = needsReplacement ? "" : value;
  memcpy(s_status_accepted, draft, strlen(draft) + 1);
  s_status_clearable = needsReplacement && value[0];
  auto* card = lv_obj_get_parent(s_fm_prompt_ta);
  lv_obj_set_height(card, 184);
  auto* notes = wadaStatusNotes(card);
  lv_obj_align(notes, LV_ALIGN_TOP_RIGHT, -2, 0);
  lv_obj_set_width(s_fm_prompt_ta, lv_obj_get_width(card) - 58);
  lv_obj_align(s_fm_prompt_ta, LV_ALIGN_TOP_LEFT, 0, 24);
  s_status_clear = lv_btn_create(card);
  lv_obj_set_size(s_status_clear, 32, 32);
  lv_obj_align(s_status_clear, LV_ALIGN_TOP_RIGHT, 0, 24);
  styleButton(s_status_clear);
  navSetTip(s_status_clear, TR("Clear status message"));
  auto* clear = lv_label_create(s_status_clear);
  lv_label_set_text(clear, LV_SYMBOL_CLOSE);
  lv_obj_center(clear);
  lv_obj_add_event_cb(s_status_clear, wadaStatusClearCb, LV_EVENT_CLICKED, nullptr);
  s_status_budget = lv_label_create(card);
  lv_obj_align(s_status_budget, LV_ALIGN_TOP_LEFT, 0, 62);
  lv_obj_set_style_text_font(s_status_budget, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_status_budget, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  auto* capacity = lv_label_create(card);
  lv_label_set_text_fmt(capacity, TR("This firmware supports %u bytes."), unsigned(limit));
  lv_obj_set_style_text_font(capacity, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(capacity, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_align(capacity, LV_ALIGN_TOP_LEFT, 0, 80);
  s_status_error = lv_label_create(card);
  lv_obj_set_width(s_status_error, lv_obj_get_width(card) - 20);
  lv_label_set_long_mode(s_status_error, LV_LABEL_LONG_WRAP);
  lv_label_set_text(s_status_error, needsReplacement ? TR("Current status has line breaks. Replace it, or save empty to clear.") : "");
  lv_obj_set_style_text_font(s_status_error, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_status_error, lv_color_hex(COLOR_STATUS_DANGER_TEXT), LV_PART_MAIN);
  lv_obj_align(s_status_error, LV_ALIGN_TOP_LEFT, 0, 98);
  lv_obj_add_event_cb(s_fm_prompt_ta, wadaStatusChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s_fm_prompt_ta, [](lv_event_t*) { wadaStatusSubmit(); }, LV_EVENT_READY, nullptr);
  wadaStatusBudget();
}
static uint32_t s_identity_name_self = 0;
static void wadaOwnNameApply(const char* text) {
  if (!wadaBackend || (!wadaBackend->channelsReady() || wadaBackend->isManaged()) || s_identity_name_self != wadaBackend->nodeNumber()) return;
  if (!wada::statusVisible(text) || !wada::statusText(text, 24, true)) {
    fmTextPrompt(TR("Display name"), text, wadaOwnNameApply, 0);
    if (g_lv.task) g_lv.task->showAlert(TR("Use a name of at most 24 UTF-8 bytes."), 2500);
    return;
  }
  const bool queued = wadaBackend->setOwner(text, wadaBackend->ownUser().short_name);
  if (g_lv.task) g_lv.task->showAlert(queued ? TR("Name update requested") : TR("Could not update name"), 2500);
}
static void wadaOwnIdentityMenu() {
  if (!wadaBackend) return;
  wadaMenuNode = wadaBackend->nodeNumber();
  ActItem actions[3];
  int count = 0;
  if (wadaBackend->channelsReady() && !wadaBackend->isManaged()) {
    actions[count++] = {UI_ICON_PENCIL, TR("Edit display name"), [] {
      s_identity_name_self = wadaBackend->nodeNumber();
      fmTextPrompt(TR("Display name"), wadaBackend->ownUser().long_name, wadaOwnNameApply, 0);
    }, false};
  }
  if (wadaBackend->canEditStatus())
    actions[count++] = {nullptr, TR("Update status"), wadaStatusEditorOpen, false, wadaStatusNotes};
  actions[count++] = {UI_ICON_INFO, TR("Node details"), wadaNodeDetails, false};
  openActionList(wadaBackend->ownUser().long_name, actions, count);
}
#endif
