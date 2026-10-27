
#include "app/tui.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "api/types.hpp"
#include "app/browser.hpp"
#include "app/playback.hpp"
#include "app/state.hpp"
#include "mpris/server.hpp"

using namespace ftxui;

namespace app {
namespace {

constexpr double kSeekStep = 5.0;
constexpr std::size_t kQueuePanelLimit = 5;
constexpr int kProgressWidth = 24;

constexpr const char* kHints =
    "1-6 tab   / search   Enter open   Esc back   space pause   "
    "n/p track   arrows +-5 s   a queue   f play   s stop   q quit ";

struct TabDef {
  const char* label;
  SearchMode mode;
};

constexpr TabDef kTabs[] = {
    {"1 tracks", SearchMode::Tracks},   {"2 albums", SearchMode::Albums},
    {"3 artists", SearchMode::Artists},  {"4 playlists", SearchMode::Playlists},
    {"5 charts", SearchMode::Charts}, {"6 library", SearchMode::Library},
};

deezer::ItemKind mode_item_kind(SearchMode mode) {
  switch (mode) {
    case SearchMode::Albums:
      return deezer::ItemKind::Album;
    case SearchMode::Artists:
      return deezer::ItemKind::Artist;
    case SearchMode::Playlists:
      return deezer::ItemKind::Playlist;
    default:
      return deezer::ItemKind::Track;
  }
}

Element TabBar(SearchMode active) {
  Elements elements;
  for (const TabDef& tab : kTabs) {
    Element element = text(std::string(" ") + tab.label + " ");
    if (tab.mode == active) {
      element |= bold;
      element |= inverted;
    } else {
      element |= dim;
    }
    elements.push_back(element);
  }
  return hbox(std::move(elements));
}

Element Breadcrumb(const State& state) {
  std::string path;
  for (const View& view : state.views) {
    if (!path.empty()) {
      path += " > ";
    }
    path += view.title.empty() ? "?" : view.title;
  }
  return text(" " + path) | dim;
}

Element PlaybackLine(const PlaybackSnapshot& playback) {
  if (!playback.error.empty()) {
    return hbox({text(" ! ") | bold | color(Color::Red),
                 text(playback.error) | color(Color::Red) | flex});
  }
  if (playback.loading) {
    return hbox({text(" … ") | dim,
                 text("loading: " + playback.label) | dim | flex});
  }
  if (!playback.active) {
    return hbox({text(" "), text("nothing playing") | dim | flex});
  }

  const auto ratio = static_cast<float>(
      playback.duration > 0.0 ? playback.position / playback.duration : 0.0);
  const std::string time =
      deezer::format_duration(static_cast<int>(playback.position)) + " / " +
      deezer::format_duration(static_cast<int>(playback.duration));

  return hbox({
      text(playback.paused ? " ‖ " : " ▶ ") | bold | color(Color::GreenLight),
      text(playback.label) | flex,
      text(" "),
      gauge(std::clamp(ratio, 0.0F, 1.0F)) | size(WIDTH, EQUAL, kProgressWidth),
      text(" " + time + " ") | dim,
  });
}

Element QueuePanel(const State& state) {
  Elements lines;
  const std::size_t shown = std::min(kQueuePanelLimit, state.queue.size());
  for (std::size_t i = 0; i < shown; ++i) {
    lines.push_back(text("  " + std::to_string(i + 1) + ". " +
                         deezer::format_item(state.queue[i])) |
                    dim);
  }
  if (state.queue.size() > shown) {
    lines.push_back(text("  … " + std::to_string(state.queue.size() - shown) +
                         " more") |
                    dim);
  }

  return vbox({
      hbox({
          text(" queue (" + std::to_string(state.queue.size()) + ") ") |
              bold,
          text("  f play   x clear   Q hide ") | dim,
      }),
      vbox(std::move(lines)),
  });
}

}

int run_tui() {
  mpris::Server mpris_server;

  auto screen = ScreenInteractive::Fullscreen();

  State state;
  state.views.push_back(View{});
  state.views.back().title = mode_title(SearchMode::Tracks);
  state.views.back().placeholder = "  Press / to search.";
  refresh_labels(state.views.back());

  BrowserService browser([&screen] { screen.PostEvent(Event::Custom); });
  PlaybackService playback([&screen] { screen.PostEvent(Event::Custom); });

  std::atomic<int> pending_track_step{0};

  mpris::Controls controls;
  controls.play = [&] { playback.set_paused(false); };
  controls.pause = [&] { playback.set_paused(true); };
  controls.play_pause = [&] { playback.toggle_pause(); };
  controls.stop = [&] { playback.stop(); };
  controls.seek_relative = [&](double seconds) {
    playback.seek_relative(seconds);
  };
  controls.current_position_us = [&] {
    return static_cast<long long>(playback.snapshot().position * 1e6);
  };
  controls.next = [&] {
    pending_track_step = 1;
    screen.PostEvent(Event::Custom);
  };
  controls.previous = [&] {
    pending_track_step = -1;
    screen.PostEvent(Event::Custom);
  };

  std::string mpris_error;
  if (!mpris_server.start(&mpris_error)) {
    std::fprintf(stderr, "warning: MPRIS unavailable (%s)\n",
                 mpris_error.c_str());
  } else {
    mpris_server.set_controls(std::move(controls));
    playback.set_mpris(&mpris_server);
  }

  std::string query;
  Component input;
  Component menu;

  const auto sync_menu = [&] {
    state.menu_labels = state.current().labels;
    state.menu_selected = state.current().selected;
  };
  const auto save_selection = [&] {
    state.current().selected = state.menu_selected;
  };

  const auto start_search = [&](SearchMode mode) {
    state.search_mode = mode;

    if (mode == SearchMode::Library) {
      state.views.clear();
      state.views.push_back(make_library_root());
      sync_menu();
      state.status = "Account library.";
      return;
    }

    state.views.resize(1);

    View& root = state.views.back();
    root.title = mode_title(mode);
    root.items.clear();
    root.selected = 0;
    root.error.clear();
    root.token = 0;

    const bool is_charts = mode == SearchMode::Charts;
    if (!is_charts && query.empty()) {
      root.loading = false;
      root.placeholder = "  Press / to search.";
      refresh_labels(root);
      sync_menu();
      state.status = "Type a search, then press Enter.";
      return;
    }

    root.loading = true;
    root.placeholder = "  Loading...";
    refresh_labels(root);
    sync_menu();

    LoadRequest request;
    if (is_charts) {
      request.kind = LoadRequest::Kind::Charts;
    } else {
      request.kind = LoadRequest::Kind::Search;
      request.query = query;
      request.item_kind = mode_item_kind(mode);
    }
    root.token = browser.load(request);
    state.searched = true;
  };

  const auto activate = [&](const deezer::Item& item) {
    if (item.is_track()) {
      playback.play(item);
      return;
    }

    View view;
    view.loading = true;
    view.placeholder = "  Loading...";

    LoadRequest request;
    if (item.kind == deezer::ItemKind::Section) {
      view.title = item.title;
      switch (item.section) {
        case deezer::LibrarySection::Playlists:
          request.kind = LoadRequest::Kind::UserPlaylists;
          break;
        case deezer::LibrarySection::Albums:
          request.kind = LoadRequest::Kind::UserFavoriteAlbums;
          break;
        case deezer::LibrarySection::Artists:
          request.kind = LoadRequest::Kind::UserFavoriteArtists;
          break;
        case deezer::LibrarySection::Loved:
          request.kind = LoadRequest::Kind::UserLovedTracks;
          break;
        case deezer::LibrarySection::Profiles:
          request.kind = LoadRequest::Kind::Profiles;
          view.placeholder = "  No profile (personal account).";
          break;
      }
    } else if (item.kind == deezer::ItemKind::Profile) {
      view.title = "Profiles";
      request.kind = LoadRequest::Kind::SwitchProfile;
      request.id = item.id;
    } else {
      switch (item.kind) {
        case deezer::ItemKind::Album:
          view.title = "Album: " + item.title;
          request.kind = LoadRequest::Kind::AlbumTracks;
          break;
        case deezer::ItemKind::Artist:
          view.title = "Artist: " + item.title;
          request.kind = LoadRequest::Kind::ArtistTop;
          break;
        case deezer::ItemKind::Playlist:
          view.title = "Playlist: " + item.title;
          request.kind = LoadRequest::Kind::PlaylistTracks;
          break;
        default:
          return;
      }
      request.id = item.id;
    }

    refresh_labels(view);
    view.token = browser.load(request);

    save_selection();
    state.views.push_back(std::move(view));
    sync_menu();
    state.status = "Opening " + item.title + "...";
  };

  const auto play_index = [&](int index) {
    View& view = state.current();
    if (index < 0 || index >= static_cast<int>(view.items.size())) {
      return;
    }
    const deezer::Item& item = view.items[index];
    if (!item.is_track()) {
      return;
    }
    state.menu_selected = index;
    view.selected = index;
    playback.play(item);
  };

  const auto play_next_queued = [&]() -> bool {
    if (state.queue.empty()) {
      return false;
    }
    const deezer::Item next = state.queue.front();
    state.queue.erase(state.queue.begin());
    playback.play(next);
    return true;
  };

  const auto queue_selected = [&] {
    const View& view = state.current();
    const int index = state.menu_selected;
    if (index < 0 || index >= static_cast<int>(view.items.size())) {
      return;
    }
    const deezer::Item& item = view.items[index];
    if (!item.is_track()) {
      state.status = "Only tracks can be queued.";
      return;
    }
    state.queue.push_back(item);
    state.status = "Queued: " + deezer::format_item(item) + " (" +
                   std::to_string(state.queue.size()) + " pending)";
  };

  InputOption input_option;
  input_option.placeholder = "Search...";
  input_option.on_enter = [&] {
    if (query.empty()) {
      return;
    }
    start_search(state.search_mode == SearchMode::Charts ? SearchMode::Tracks
                                                         : state.search_mode);
    if (menu) {
      menu->TakeFocus();
    }
  };
  input = Input(&query, input_option);

  MenuOption menu_option = MenuOption::Vertical();
  menu_option.entries_option.transform = [&](const EntryState& entry) {
    const View& view = state.current();
    if (view.items.empty()) {
      return text(entry.label) | dim;
    }

    const bool valid =
        entry.index >= 0 && entry.index < static_cast<int>(view.items.size());
    const std::string detail =
        valid ? deezer::format_item_detail(view.items[entry.index]) : "";

    Element element = hbox({
        text(entry.label) | flex,
        text(" " + detail + " ") | dim,
    });
    if (entry.active) {
      element |= bold | color(Color::GreenLight);
    }
    if (entry.focused) {
      element |= inverted;
    }
    return element;
  };
  menu_option.on_enter = [&] {
    const View& view = state.current();
    const int index = state.menu_selected;
    if (index < 0 || index >= static_cast<int>(view.items.size())) {
      return;
    }
    const deezer::Item item = view.items[index];
    activate(item);
  };
  menu = Menu(&state.menu_labels, &state.menu_selected, menu_option);

  auto container = Container::Vertical({input, menu});

  auto renderer = Renderer(container, [&] {
    const std::string status = state.current().loading ? "Loading..." : state.status;
    const PlaybackSnapshot now = playback.snapshot();

    Elements rows;
    const std::string account =
        state.account_name.empty() ? "offline" : state.account_name;
    rows.push_back(hbox({
        text(" deezer-tui ") | bold,
        filler(),
        text(" Deezer · " + account + " ") | dim,
    }));
    rows.push_back(hbox({TabBar(state.search_mode), filler()}));
    rows.push_back(separator());
    rows.push_back(hbox({
        text(" > ") | bold,
        input->Render() | flex,
    }) | border);
    rows.push_back(separator());
    rows.push_back(Breadcrumb(state));
    rows.push_back(menu->Render() | frame | flex);
    if (state.show_queue && !state.queue.empty()) {
      rows.push_back(separator());
      rows.push_back(QueuePanel(state));
    }
    rows.push_back(separator());
    rows.push_back(PlaybackLine(now));
    rows.push_back(hbox({text(" " + status) | flex}));
    rows.push_back(hbox({text(" " + std::string(kHints)) | dim | flex}));

    return vbox(std::move(rows)) | border;
  });

  renderer |= CatchEvent([&](Event event) {
    if (event == Event::Custom) {
      if (browser.apply_pending(state)) {
        sync_menu();
      }
      if (browser.take_profile_switch()) {
        start_search(SearchMode::Library);
        state.status = "Profile: " + state.account_name;
      }
      const int step = pending_track_step.exchange(0);
      if (step != 0) {
        play_index(state.menu_selected + step);
        state.status = step > 0 ? "Next track (shell)"
                                : "Previous track (shell)";
      }
      if (playback.take_finished()) {
        if (!play_next_queued()) {
          play_index(state.menu_selected + 1);
        }
      }
      return true;
    }

    if (event == Event::Escape) {
      if (state.can_go_back()) {
        save_selection();
        state.views.pop_back();
        sync_menu();
        state.status = "Back.";
      } else if (menu) {
        menu->TakeFocus();
      }
      return true;
    }

    if (event == Event::ArrowLeft) {
      playback.seek_relative(-kSeekStep);
      return true;
    }
    if (event == Event::ArrowRight) {
      playback.seek_relative(kSeekStep);
      return true;
    }

    if (!input->Focused() && event.is_character()) {
      const std::string character = event.character();

      if (character.size() == 1 && character[0] >= '1' &&
          character[0] <= '6') {
        const auto index = static_cast<std::size_t>(character[0] - '1');
        if (index < std::size(kTabs)) {
          start_search(kTabs[index].mode);
        }
        return true;
      }
      if (character == "q") {
        screen.Exit();
        return true;
      }
      if (character == "/") {
        input->TakeFocus();
        return true;
      }
      if (character == " ") {
        playback.toggle_pause();
        return true;
      }
      if (character == "n") {
        play_index(state.menu_selected + 1);
        return true;
      }
      if (character == "p") {
        play_index(state.menu_selected - 1);
        return true;
      }
      if (character == "h") {
        playback.seek_relative(-kSeekStep);
        return true;
      }
      if (character == "l") {
        playback.seek_relative(kSeekStep);
        return true;
      }
      if (character == "a") {
        queue_selected();
        return true;
      }
      if (character == "f") {
        if (!play_next_queued()) {
          state.status = "Queue is empty.";
        }
        return true;
      }
      if (character == "x") {
        state.queue.clear();
        state.status = "Queue cleared.";
        return true;
      }
      if (character == "Q") {
        state.show_queue = !state.show_queue;
        return true;
      }
      if (character == "s") {
        playback.stop();
        return true;
      }
    }
    return false;
  });

  sync_menu();
  menu->TakeFocus();

  LoadRequest probe;
  probe.kind = LoadRequest::Kind::Profiles;
  browser.load(probe);

  screen.Loop(renderer);

  mpris_server.stop();
  return 0;
}

}
