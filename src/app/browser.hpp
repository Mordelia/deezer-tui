#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "api/types.hpp"
#include "app/state.hpp"

namespace app {

struct LoadRequest {
  enum class Kind {
    Search,
    Charts,
    AlbumTracks,
    ArtistTop,
    PlaylistTracks,
    UserPlaylists,
    UserFavoriteAlbums,
    UserFavoriteArtists,
    UserLovedTracks,
    Profiles,
    SwitchProfile,
  };

  Kind kind = Kind::Search;
  std::string query;
  deezer::ItemKind item_kind = deezer::ItemKind::Track;
  long long id = 0;
};

class BrowserService {
 public:
  using Notify = std::function<void()>;

  explicit BrowserService(Notify notify);
  ~BrowserService();

  BrowserService(const BrowserService&) = delete;
  BrowserService& operator=(const BrowserService&) = delete;

  std::uint64_t load(const LoadRequest& request);

  bool apply_pending(State& state);

  bool take_profile_switch();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  void worker_loop();
};

}
