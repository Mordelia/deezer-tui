#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "api/types.hpp"

namespace app {

struct View {
  std::string title;

  std::string placeholder = "  No items.";

  std::vector<deezer::Item> items;

  std::vector<std::string> labels;

  int selected = 0;
  bool loading = false;
  std::string error;

  std::uint64_t token = 0;
};

enum class SearchMode { Tracks, Albums, Artists, Playlists, Charts, Library };

struct State {
  std::vector<View> views;

  SearchMode search_mode = SearchMode::Tracks;
  bool searched = false;

  std::vector<deezer::Item> queue;
  bool show_queue = true;

  std::string account_name;

  std::size_t profile_count = 0;

  std::vector<std::string> menu_labels;
  int menu_selected = 0;

  std::string status = "Ready.";

  [[nodiscard]] View& current() { return views.back(); }
  [[nodiscard]] const View& current() const { return views.back(); }
  [[nodiscard]] bool can_go_back() const { return views.size() > 1; }
};

void refresh_labels(View& view);

std::string mode_title(SearchMode mode);

View make_library_root();

bool has_tracks(const View& view);

}
