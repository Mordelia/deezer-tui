#pragma once

#include <optional>
#include <string>
#include <vector>

#include "api/types.hpp"

namespace deezer {

std::optional<std::vector<Item>> search(const std::string& query, ItemKind kind,
                                        std::string* error);

std::optional<std::vector<Item>> charts(std::string* error);

std::optional<std::vector<Item>> album_tracks(long long album_id,
                                              std::string* error);
std::optional<std::vector<Item>> artist_top(long long artist_id,
                                            std::string* error);
std::optional<std::vector<Item>> playlist_tracks(long long playlist_id,
                                                 std::string* error);

}
