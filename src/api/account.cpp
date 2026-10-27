#include "api/account.hpp"

#include <algorithm>
#include <cstddef>

#include <nlohmann/json.hpp>

namespace deezer {
namespace {

std::string json_string(const nlohmann::json& object, const char* key) {
  const auto it = object.find(key);
  if (it == object.end() || !it->is_string()) {
    return {};
  }
  return it->get<std::string>();
}

long long json_id(const nlohmann::json& object, const char* key) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return 0;
  }
  if (it->is_number_integer()) {
    return it->get<long long>();
  }
  if (it->is_string()) {
    try {
      return std::stoll(it->get<std::string>());
    } catch (const std::exception&) {
      return 0;
    }
  }
  return 0;
}

Item parse_gw_track(const nlohmann::json& entry) {
  Item item;
  item.kind = ItemKind::Track;
  item.id = json_id(entry, "SNG_ID");
  item.title = json_string(entry, "SNG_TITLE");
  item.subtitle = json_string(entry, "ART_NAME");
  item.duration = static_cast<int>(json_id(entry, "DURATION"));
  item.album = json_string(entry, "ALB_TITLE");
  item.cover = json_string(entry, "ALB_PICTURE");
  return item;
}

std::optional<nlohmann::json> profile_tab(Session& session, const char* tab,
                                          int limit, std::string* error) {
  const nlohmann::json payload = {
      {"user_id", session.user_id},
      {"tab", tab},
      {"nb", limit},
  };

  auto results = gw_light(session, "deezer.pageProfile", payload, error);
  if (!results) {
    return std::nullopt;
  }

  const auto tabs = results->find("TAB");
  if (tabs == results->end()) {
    if (error != nullptr) {
      *error = "profile response has no tabs";
    }
    return std::nullopt;
  }

  const auto node = tabs->find(tab);
  if (node == tabs->end()) {
    return nlohmann::json::array();
  }
  return node->value("data", nlohmann::json::array());
}

}

std::optional<std::vector<Item>> user_playlists(Session& session,
                                                std::string* error) {
  auto data = profile_tab(session, "playlists", 500, error);
  if (!data) {
    return std::nullopt;
  }

  std::vector<Item> items;
  for (const auto& entry : *data) {
    Item item;
    item.kind = ItemKind::Playlist;
    item.id = json_id(entry, "PLAYLIST_ID");
    item.title = json_string(entry, "TITLE");
    item.count = static_cast<int>(json_id(entry, "NB_SONG"));
    items.push_back(std::move(item));
  }
  return items;
}

std::optional<std::vector<Item>> user_favorite_albums(Session& session,
                                                      std::string* error) {
  auto data = profile_tab(session, "albums", 500, error);
  if (!data) {
    return std::nullopt;
  }

  std::vector<Item> items;
  for (const auto& entry : *data) {
    Item item;
    item.kind = ItemKind::Album;
    item.id = json_id(entry, "ALB_ID");
    item.title = json_string(entry, "ALB_TITLE");
    item.subtitle = json_string(entry, "ART_NAME");
    items.push_back(std::move(item));
  }
  return items;
}

std::optional<std::vector<Item>> user_favorite_artists(Session& session,
                                                       std::string* error) {
  auto data = profile_tab(session, "artists", 500, error);
  if (!data) {
    return std::nullopt;
  }

  std::vector<Item> items;
  for (const auto& entry : *data) {
    Item item;
    item.kind = ItemKind::Artist;
    item.id = json_id(entry, "ART_ID");
    item.title = json_string(entry, "ART_NAME");
    items.push_back(std::move(item));
  }
  return items;
}

std::optional<std::vector<Item>> user_loved_tracks(Session& session,
                                                   std::string* error) {
  const nlohmann::json payload = {{"nb", 10000}, {"start", 0}};
  auto results = gw_light(session, "song.getFavoriteIds", payload, error);
  if (!results) {
    return std::nullopt;
  }

  const auto data = results->find("data");
  if (data == results->end()) {
    if (error != nullptr) {
      *error = "reponse sans favoris";
    }
    return std::nullopt;
  }

  std::vector<long long> ids;
  ids.reserve(data->size());
  for (const auto& entry : *data) {
    const long long id = json_id(entry, "SNG_ID");
    if (id != 0) {
      ids.push_back(id);
    }
  }
  if (ids.empty()) {
    return std::vector<Item>{};
  }

  constexpr std::size_t kChunk = 100;
  std::vector<Item> items;
  for (std::size_t offset = 0; offset < ids.size(); offset += kChunk) {
    const std::size_t count = std::min(kChunk, ids.size() - offset);

    nlohmann::json chunk = nlohmann::json::array();
    for (std::size_t i = 0; i < count; ++i) {
      chunk.push_back(ids[offset + i]);
    }

    auto tracks =
        gw_light(session, "song.getListData", {{"sng_ids", chunk}}, error);
    if (!tracks) {
      return std::nullopt;
    }
    const auto track_data = tracks->find("data");
    if (track_data == tracks->end()) {
      if (error != nullptr) {
        *error = "response has no tracks";
      }
      return std::nullopt;
    }
    for (const auto& entry : *track_data) {
      items.push_back(parse_gw_track(entry));
    }
  }
  return items;
}

std::optional<std::vector<Item>> gw_playlist_tracks(Session& session,
                                                    long long playlist_id,
                                                    std::string* error) {
  const nlohmann::json payload = {
      {"playlist_id", playlist_id},
      {"nb", -1},
  };

  auto results = gw_light(session, "playlist.getSongs", payload, error);
  if (!results) {
    return std::nullopt;
  }

  const auto data = results->find("data");
  if (data == results->end()) {
    if (error != nullptr) {
      *error = "playlist response has no tracks";
    }
    return std::nullopt;
  }

  std::vector<Item> items;
  for (const auto& entry : *data) {
    items.push_back(parse_gw_track(entry));
  }
  return items;
}

}
