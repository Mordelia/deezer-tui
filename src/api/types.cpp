#include "api/types.hpp"

#include <cstdio>

namespace deezer {

std::string format_duration(int seconds) {
  if (seconds <= 0) {
    return "--:--";
  }
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%d:%02d", seconds / 60, seconds % 60);
  return buffer;
}

std::string format_item(const Item& item) {
  switch (item.kind) {
    case ItemKind::Track:
      return item.subtitle.empty() ? item.title
                                   : item.subtitle + " — " + item.title;
    case ItemKind::Album:
      return item.subtitle.empty() ? item.title
                                   : item.title + " — " + item.subtitle;
    case ItemKind::Playlist:
      return item.subtitle.empty() ? item.title
                                   : item.title + " — " + item.subtitle;
    case ItemKind::Profile:
      return item.subtitle.empty() ? item.title
                                   : item.title + "  (" + item.subtitle + ")";
    case ItemKind::Artist:
    case ItemKind::Section:
      return item.title;
  }
  return item.title;
}

std::string cover_url(const Item& item) {
  if (item.cover.empty()) {
    return {};
  }
  return "https://cdn-images.dzcdn.net/images/cover/" + item.cover +
         "/500x500-000000-80-0-0.jpg";
}

std::string format_item_detail(const Item& item) {
  if (item.kind == ItemKind::Track) {
    return format_duration(item.duration);
  }
  if (item.kind == ItemKind::Section || item.kind == ItemKind::Profile) {
    return {};
  }
  if (item.count < 0) {
    return {};
  }

  const std::string noun = item.kind == ItemKind::Artist ? "album" : "track";
  return std::to_string(item.count) + " " + noun +
         (item.count == 1 ? "" : "s");
}

}
