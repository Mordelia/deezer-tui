#pragma once

#include <string>

namespace deezer {

enum class ItemKind { Track, Album, Artist, Playlist, Section, Profile };

enum class LibrarySection { Playlists, Albums, Artists, Loved, Profiles };

inline constexpr int kUnknownCount = -1;

struct Item {
  ItemKind kind = ItemKind::Track;
  long long id = 0;
  std::string title;
  std::string subtitle;
  int duration = 0;
  int count = kUnknownCount;

  std::string album;
  std::string cover;

  LibrarySection section = LibrarySection::Playlists;

  [[nodiscard]] bool is_track() const { return kind == ItemKind::Track; }
};

std::string cover_url(const Item& item);

std::string format_item_detail(const Item& item);

std::string format_item(const Item& item);

std::string format_duration(int seconds);

}
