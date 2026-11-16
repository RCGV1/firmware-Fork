#if defined(WADA_MESHTASTIC)
static bool s_native_identity_name = false;
static void wadaStatusEditorOpen();
static void wadaOwnIdentityMenu();
static bool wadaIdentityPopupActive();
static void wadaStatusPromptClosed();
static bool wadaStatusSubmit();
static lv_obj_t* wadaStatusNotes(lv_obj_t* parent) {
  auto* icon = lv_obj_create(parent);
  lv_obj_remove_style_all(icon);
  lv_obj_set_size(icon, 16, 16);
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  static const lv_point_t first[] = {{1, 3}, {15, 3}};
  static const lv_point_t second[] = {{1, 7}, {15, 7}};
  static const lv_point_t third[] = {{1, 11}, {10, 11}};
  for (const lv_point_t* points : {first, second, third}) {
    auto* line = lv_line_create(icon);
    lv_line_set_points(line, points, 2);
    lv_obj_set_style_line_width(line, 2, LV_PART_MAIN);
    lv_obj_set_style_line_color(line, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  }
  return icon;
}
static int wadaStatusCardHeight(const char* text, int width) {
  if (!wada::statusVisible(text)) return 0;
  lv_point_t size;
  lv_txt_get_size(&size, text, &g_font_14, 0, 0, LV_MAX(1, width - 22), LV_TEXT_FLAG_NONE);
  return LV_MAX(16, LV_MIN(int(size.y), 2 * int(lv_font_get_line_height(&g_font_14))));
}
static lv_obj_t* wadaStatusRow(lv_obj_t* parent, const char* text, int width, bool details) {
  if (!wada::statusVisible(text)) return nullptr;
  auto* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, width);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  if (details) {
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(row, 4, LV_PART_MAIN);
    auto* heading = lv_obj_create(row);
    lv_obj_remove_style_all(heading);
    lv_obj_set_size(heading, width, 16);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(heading, 6, LV_PART_MAIN);
    lv_obj_clear_flag(heading, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    wadaStatusNotes(heading);
    auto* title = lv_label_create(heading);
    lv_label_set_text(title, TR("Status Message"));
    lv_obj_set_style_text_font(title, &g_font_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  } else {
    lv_obj_set_height(row, wadaStatusCardHeight(text, width));
    auto* icon = wadaStatusNotes(row);
    lv_obj_set_pos(icon, 0, 0);
  }
  auto* value = lv_label_create(row);
  lv_label_set_recolor(value, false);
  lv_label_set_text(value, text);
  lv_obj_set_style_text_font(value, &g_font_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(value, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_line_space(value, 0, LV_PART_MAIN);
  lv_obj_set_width(value, details ? width : LV_MAX(1, width - 22));
  lv_label_set_long_mode(value, details ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
  if (details) lv_obj_set_height(value, LV_SIZE_CONTENT);
  else {
    lv_obj_set_pos(value, 22, 0);
    lv_obj_set_height(value, wadaStatusCardHeight(text, width));
  }
  return row;
}
#endif
