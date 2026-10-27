#include "api/deezer.hpp"

#include <nlohmann/json.hpp>

#include "api/http.hpp"

namespace deezer {
namespace {

constexpr const char* kApiBase = "https://api.deezer.com";

std::string json_string(const nlohmann::json& object, const char* key) {
  const auto it = object.find(key);
  if (it == object.end() || !it->is_string()) {
    return {};
  }
  return it->get<std::string>();
}

std::optional<nlohmann::json> fetch_data(const std::string& url,
                                         std::string* error) {
  auto response = http::get(url, error);
  if (!response) {
    return std::nullopt;
  }
  if (!response->ok()) {
    if (error != nullptr) {
      *error = "HTTP " + std::to_string(response->status);
    }
    return std::nullopt;
  }

  nlohmann::json document;
  try {
    document = nlohmann::json::parse(response->body);
  } catch (const std::exception& exception) {
    if (error != nullptr) {
      *error = std::string("invalid JSON response: ") + exception.what();
    }
    return std::nullopt;
  }

  if (document.contains("error")) {
    const std::string message = json_string(document["error"], "message");
    if (error != nullptr) {
      *error = message.empty() ? "error returned by the Deezer API" : message;
    }
    return std::nullopt;
  }

  return document.value("data", nlohmann::json::array());
}

Item parse_track(const nlohmann::json& entry) {
  Item item;
  item.kind = ItemKind::Track;
  item.id = entry.value("id", 0LL);
  item.title = json_string(entry, "title");
  item.duration = entry.value("duration", 0);
  if (entry.contains("artist")) {
    item.subtitle = json_string(entry["artist"], "name");
  }
  if (entry.contains("album")) {
    item.album = json_string(entry["album"], "title");
    item.cover = json_string(entry["album"], "md5_image");
  }
  return item;
}

Item parse_album(const nlohmann::json& entry) {
  Item item;
  item.kind = ItemKind::Album;
  item.id = entry.value("id", 0LL);
  item.title = json_string(entry, "title");
  item.count = entry.value("nb_tracks", 0);
  if (entry.contains("artist")) {
    item.subtitle = json_string(entry["artist"], "name");
  }
  return item;
}

Item parse_artist(const nlohmann::json& entry) {
  Item item;
  item.kind = ItemKind::Artist;
  item.id = entry.value("id", 0LL);
  item.title = json_string(entry, "name");
  item.count = entry.value("nb_album", 0);
  return item;
}

Item parse_playlist(const nlohmann::json& entry) {
  Item item;
  item.kind = ItemKind::Playlist;
  item.id = entry.value("id", 0LL);
  item.title = json_string(entry, "title");
  item.count = entry.value("nb_tracks", 0);
  if (entry.contains("user")) {
    item.subtitle = json_string(entry["user"], "name");
  }
  return item;
}

std::vector<Item> parse_items(const nlohmann::json& data, ItemKind kind) {
  std::vector<Item> items;
  for (const auto& entry : data) {
    switch (kind) {
      case ItemKind::Track:
        items.push_back(parse_track(entry));
        break;
      case ItemKind::Album:
        items.push_back(parse_album(entry));
        break;
      case ItemKind::Artist:
        items.push_back(parse_artist(entry));
        break;
      case ItemKind::Playlist:
        items.push_back(parse_playlist(entry));
        break;
      case ItemKind::Section:
      case ItemKind::Profile:
        break;
    }
  }
  return items;
}

const char* search_path(ItemKind kind) {
  switch (kind) {
    case ItemKind::Track:
      return "/search?q=";
    case ItemKind::Album:
      return "/search/album?q=";
    case ItemKind::Artist:
      return "/search/artist?q=";
    case ItemKind::Playlist:
      return "/search/playlist?q=";
    case ItemKind::Section:
    case ItemKind::Profile:
      break;
  }
  return "/search?q=";
}

}

std::optional<std::vector<Item>> search(const std::string& query, ItemKind kind,
                                        std::string* error) {
  const std::string url = std::string(kApiBase) + search_path(kind) +
                          http::url_encode(query) + "&limit=50";

  auto data = fetch_data(url, error);
  if (!data) {
    return std::nullopt;
  }
  return parse_items(*data, kind);
}

std::optional<std::vector<Item>> charts(std::string* error) {
  auto data =
      fetch_data(std::string(kApiBase) + "/chart/0/tracks?limit=50", error);
  if (!data) {
    return std::nullopt;
  }
  return parse_items(*data, ItemKind::Track);
}

std::optional<std::vector<Item>> album_tracks(long long album_id,
                                              std::string* error) {
  auto data = fetch_data(std::string(kApiBase) + "/album/" +
                             std::to_string(album_id) + "/tracks?limit=200",
                         error);
  if (!data) {
    return std::nullopt;
  }
  return parse_items(*data, ItemKind::Track);
}

std::optional<std::vector<Item>> artist_top(long long artist_id,
                                            std::string* error) {
  auto data = fetch_data(std::string(kApiBase) + "/artist/" +
                             std::to_string(artist_id) + "/top?limit=50",
                         error);
  if (!data) {
    return std::nullopt;
  }
  return parse_items(*data, ItemKind::Track);
}

std::optional<std::vector<Item>> playlist_tracks(long long playlist_id,
                                                 std::string* error) {
  auto data = fetch_data(std::string(kApiBase) + "/playlist/" +
                             std::to_string(playlist_id) + "/tracks?limit=200",
                         error);
  if (!data) {
    return std::nullopt;
  }
  return parse_items(*data, ItemKind::Track);
}

}
