#pragma once

#include <optional>
#include <string>
#include <vector>

#include "api/auth.hpp"
#include "api/types.hpp"

namespace deezer {


std::optional<std::vector<Item>> user_playlists(Session& session,
                                                std::string* error);
std::optional<std::vector<Item>> user_favorite_albums(Session& session,
                                                      std::string* error);
std::optional<std::vector<Item>> user_favorite_artists(Session& session,
                                                       std::string* error);
std::optional<std::vector<Item>> user_loved_tracks(Session& session,
                                                   std::string* error);

std::optional<std::vector<Item>> gw_playlist_tracks(Session& session,
                                                    long long playlist_id,
                                                    std::string* error);

}
