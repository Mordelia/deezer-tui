#include "app/state.hpp"

namespace app {

void refresh_labels(View& view) {
  view.labels.clear();
  view.labels.reserve(view.items.size());
  for (const deezer::Item& item : view.items) {
    view.labels.push_back(deezer::format_item(item));
  }

  if (view.labels.empty()) {
    view.labels.push_back(view.placeholder);
  }
}

std::string mode_title(SearchMode mode) {
  switch (mode) {
    case SearchMode::Tracks:
      return "Tracks";
    case SearchMode::Albums:
      return "Albums";
    case SearchMode::Artists:
      return "Artists";
    case SearchMode::Playlists:
      return "Playlists";
    case SearchMode::Charts:
      return "Charts";
    case SearchMode::Library:
      return "Library";
  }
  return "Search";
}

View make_library_root() {
  struct Section {
    const char* label;
    deezer::LibrarySection value;
  };
  static constexpr Section kSections[] = {
      {"My playlists", deezer::LibrarySection::Playlists},
      {"My favourite albums", deezer::LibrarySection::Albums},
      {"My favourite artists", deezer::LibrarySection::Artists},
      {"My loved tracks", deezer::LibrarySection::Loved},
      {"Switch profile", deezer::LibrarySection::Profiles},
  };

  View view;
  view.title = "Library";
  for (const Section& entry : kSections) {
    deezer::Item item;
    item.kind = deezer::ItemKind::Section;
    item.title = entry.label;
    item.section = entry.value;
    view.items.push_back(std::move(item));
  }
  refresh_labels(view);
  return view;
}

bool has_tracks(const View& view) {
  for (const deezer::Item& item : view.items) {
    if (item.is_track()) {
      return true;
    }
  }
  return false;
}

}
