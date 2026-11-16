#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
struct QrPopupLayout {
  int size, top, captionTop, captionHeight, width;
  static QrPopupLayout make(int cardWidth, int cardHeight, int fontHeight, int maximum) {
    const int width = std::max(0, cardWidth - 22), caption = std::max(1, fontHeight);
    const int captionTop = cardHeight - 22 - caption - 2;
    const int size = std::max(0, std::min({maximum, width, cardHeight - 88, captionTop - 32 - 8}));
    return {size, 32, captionTop, caption, width};
  }
};
inline bool qrPopupUrl(const char* url, char* out, size_t capacity) {
  if (!url || !out || !capacity) return false;
  const size_t length = strnlen(url, capacity);
  if (length == capacity) return false;
  const bool http = length >= 4 && (url[0] == 'h' || url[0] == 'H') && (url[1] == 't' || url[1] == 'T') &&
    (url[2] == 't' || url[2] == 'T') && (url[3] == 'p' || url[3] == 'P');
  if (length + (http ? 0 : 8) >= capacity) return false;
  snprintf(out, capacity, "%s%s", http ? "" : "https://", url);
  return true;
}
