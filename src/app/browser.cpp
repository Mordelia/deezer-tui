#include "app/browser.hpp"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "api/account.hpp"
#include "api/auth.hpp"
#include "api/deezer.hpp"
#include "app/config.hpp"
#include "app/session.hpp"

namespace app {
namespace {

std::vector<deezer::Item> profile_items(const deezer::Session& session) {
  std::vector<deezer::Item> items;
  for (const deezer::Profile& profile : session.profiles) {
    deezer::Item item;
    item.kind = deezer::ItemKind::Profile;
    item.id = profile.id;
    item.title = profile.name;
    if (profile.id == session.account_id) {
      item.subtitle = "active";
    }
    items.push_back(std::move(item));
  }
  return items;
}

}

struct BrowserService::Impl {
  struct Pending {
    std::uint64_t token = 0;
    bool ok = false;
    std::vector<deezer::Item> items;
    std::string error;

    bool has_account_info = false;
    std::string account_name;
    std::size_t profile_count = 0;
  };

  std::mutex mutex;
  std::condition_variable condition;
  bool stop = false;

  std::optional<LoadRequest> job;
  std::uint64_t job_token = 0;
  std::uint64_t next_token = 1;

  std::optional<deezer::Session> session;

  std::optional<Pending> pending;
  bool switched = false;

  Notify notify;
  std::thread worker;

  ~Impl() {
    {
      std::lock_guard lock(mutex);
      stop = true;
    }
    condition.notify_all();
    if (worker.joinable()) {
      worker.join();
    }
  }
};

BrowserService::BrowserService(Notify notify)
    : impl_(std::make_unique<Impl>()) {
  impl_->notify = std::move(notify);
  impl_->worker = std::thread(&BrowserService::worker_loop, this);
}

BrowserService::~BrowserService() = default;

std::uint64_t BrowserService::load(const LoadRequest& request) {
  std::uint64_t token = 0;
  {
    std::lock_guard lock(impl_->mutex);
    token = impl_->next_token++;
    impl_->job = request;
    impl_->job_token = token;
  }
  impl_->condition.notify_one();
  impl_->notify();
  return token;
}

bool BrowserService::take_profile_switch() {
  std::lock_guard lock(impl_->mutex);
  const bool switched = impl_->switched;
  impl_->switched = false;
  return switched;
}

void BrowserService::worker_loop() {
  for (;;) {
    LoadRequest request;
    std::uint64_t token = 0;
    {
      std::unique_lock lock(impl_->mutex);
      impl_->condition.wait(lock, [this] {
        return impl_->stop || impl_->job.has_value();
      });
      if (impl_->stop) {
        return;
      }
      request = std::move(*impl_->job);
      impl_->job.reset();
      token = impl_->job_token;
    }

    std::string error;
    std::optional<std::vector<deezer::Item>> items;
    bool switched = false;

    const auto ensure_session = [&]() -> deezer::Session* {
      if (impl_->session) {
        return &*impl_->session;
      }
      auto session = open_session(nullptr, &error);
      if (!session) {
        return nullptr;
      }
      impl_->session = std::move(session);
      return &*impl_->session;
    };

    switch (request.kind) {
      case LoadRequest::Kind::Search:
        items = deezer::search(request.query, request.item_kind, &error);
        break;
      case LoadRequest::Kind::Charts:
        items = deezer::charts(&error);
        break;
      case LoadRequest::Kind::AlbumTracks:
        items = deezer::album_tracks(request.id, &error);
        break;
      case LoadRequest::Kind::ArtistTop:
        items = deezer::artist_top(request.id, &error);
        break;
      case LoadRequest::Kind::PlaylistTracks: {
        items = deezer::playlist_tracks(request.id, &error);
        if (!items) {
          if (deezer::Session* session = ensure_session(); session != nullptr) {
            items = deezer::gw_playlist_tracks(*session, request.id, &error);
          }
        }
        break;
      }
      case LoadRequest::Kind::UserPlaylists:
      case LoadRequest::Kind::UserFavoriteAlbums:
      case LoadRequest::Kind::UserFavoriteArtists:
      case LoadRequest::Kind::UserLovedTracks: {
        deezer::Session* session = ensure_session();
        if (session == nullptr) {
          break;
        }
        switch (request.kind) {
          case LoadRequest::Kind::UserPlaylists:
            items = deezer::user_playlists(*session, &error);
            break;
          case LoadRequest::Kind::UserFavoriteAlbums:
            items = deezer::user_favorite_albums(*session, &error);
            break;
          case LoadRequest::Kind::UserFavoriteArtists:
            items = deezer::user_favorite_artists(*session, &error);
            break;
          default:
            items = deezer::user_loved_tracks(*session, &error);
            break;
        }
        break;
      }
      case LoadRequest::Kind::Profiles: {
        deezer::Session* session = ensure_session();
        if (session == nullptr) {
          break;
        }
        items = profile_items(*session);
        break;
      }
      case LoadRequest::Kind::SwitchProfile: {
        deezer::Session* session = ensure_session();
        if (session == nullptr) {
          break;
        }
        if (deezer::select_profile(*session, request.id, &error)) {
          save_selected_profile(request.id);
          switched = true;
          items = profile_items(*session);
        }
        break;
      }
    }

    bool publish = false;
    {
      std::lock_guard lock(impl_->mutex);
      if (token == impl_->job_token) {
        Impl::Pending result;
        result.token = token;
        result.ok = items.has_value();
        if (items) {
          result.items = std::move(*items);
        } else {
          result.error = error.empty() ? "unknown error" : error;
        }
        if (impl_->session) {
          result.has_account_info = true;
          result.account_name = impl_->session->account_name;
          result.profile_count = impl_->session->profiles.size();
        }
        impl_->pending = std::move(result);
        if (switched) {
          impl_->switched = true;
        }
        publish = true;
      }
    }
    if (publish) {
      impl_->notify();
    }
  }
}

bool BrowserService::apply_pending(State& state) {
  std::lock_guard lock(impl_->mutex);
  if (!impl_->pending) {
    return false;
  }

  Impl::Pending result = std::move(*impl_->pending);
  impl_->pending.reset();

  bool changed = false;

  if (result.has_account_info) {
    state.account_name = result.account_name;
    state.profile_count = result.profile_count;
    changed = true;
  }

  for (View& view : state.views) {
    if (view.token != result.token) {
      continue;
    }
    view.token = 0;
    view.loading = false;

    if (result.ok) {
      view.items = std::move(result.items);
      view.error.clear();
      view.selected = 0;
      refresh_labels(view);
      state.status = std::to_string(view.items.size()) + " item(s)";
    } else {
      view.error = result.error;
      refresh_labels(view);
      state.status = "Error: " + result.error;
    }
    changed = true;
    break;
  }
  return changed;
}

}
