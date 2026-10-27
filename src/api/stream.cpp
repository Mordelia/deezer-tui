#include "api/stream.hpp"

#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

#include "api/http.hpp"
#include "crypto/blowfish.hpp"

namespace deezer {
namespace {

constexpr const char* kGetUrlEndpoint = "https://media.deezer.com/v1/get_url";
constexpr const char* kBlowfishCipher = "BF_CBC_STRIPE";

std::string json_string(const nlohmann::json& object, const char* key) {
  const auto it = object.find(key);
  if (it == object.end() || !it->is_string()) {
    return {};
  }
  return it->get<std::string>();
}

bool forward_to_sink(const std::function<bool(const char*, std::size_t)>& sink,
                     const std::string& out, bool* interrupted) {
  if (out.empty()) {
    return true;
  }
  if (sink(out.data(), out.size())) {
    return true;
  }
  *interrupted = true;
  return false;
}

}

std::optional<TrackStream> get_track_stream(Session& session,
                                            long long track_id,
                                            const std::string& format,
                                            std::string* error) {
  nlohmann::json data_payload = {
      {"sng_id", track_id},
      {"array_default", nlohmann::json::array({"TRACK_TOKEN"})},
  };
  auto data = gw_light(session, "song.getData", data_payload, error);
  if (!data) {
    return std::nullopt;
  }

  std::string track_token = data->value("TRACK_TOKEN", "");
  if (track_token.empty() && data->contains("DATA")) {
    track_token = json_string((*data)["DATA"], "TRACK_TOKEN");
  }
  if (track_token.empty()) {
    if (error != nullptr) {
      *error = "track token missing (track unavailable?)";
    }
    return std::nullopt;
  }

  nlohmann::json media_entry = {
      {"type", "FULL"},
      {"formats", nlohmann::json::array(
                      {nlohmann::json{{"cipher", kBlowfishCipher},
                                      {"format", format}}})},
  };
  nlohmann::json body = {
      {"license_token", session.license_token},
      {"media", nlohmann::json::array({media_entry})},
      {"track_tokens", nlohmann::json::array({track_token})},
  };

  http::Request request;
  request.method = "POST";
  request.url = kGetUrlEndpoint;
  request.headers = {"Content-Type: application/json"};
  request.body = body.dump();

  auto response = http::request(request, error);
  if (!response) {
    return std::nullopt;
  }
  if (!response->ok()) {
    if (error != nullptr) {
      *error = "HTTP " + std::to_string(response->status) +
               " from media.deezer.com";
    }
    return std::nullopt;
  }

  nlohmann::json document;
  try {
    document = nlohmann::json::parse(response->body);
  } catch (const std::exception& exception) {
    if (error != nullptr) {
      *error = std::string("reponse JSON invalide : ") + exception.what();
    }
    return std::nullopt;
  }

  const auto& entries = document.value("data", nlohmann::json::array());
  if (entries.empty() || !entries[0].contains("media") ||
      entries[0]["media"].empty()) {
    if (error != nullptr) {
      *error = "no source returned (insufficient rights on this track?)";
    }
    return std::nullopt;
  }

  const auto& media = entries[0]["media"][0];
  const auto& sources = media.value("sources", nlohmann::json::array());
  if (sources.empty()) {
    if (error != nullptr) {
      *error = "no URL in the media.deezer.com response";
    }
    return std::nullopt;
  }

  TrackStream stream;
  stream.url = json_string(sources[0], "url");
  stream.format = json_string(media, "format");
  stream.size = media.value("filesize", 0LL);

  const auto cipher = media.find("cipher");
  if (cipher != media.end()) {
    if (cipher->is_object()) {
      stream.cipher = json_string(*cipher, "type");
    } else if (cipher->is_string()) {
      stream.cipher = cipher->get<std::string>();
    }
  }

  if (stream.url.empty()) {
    if (error != nullptr) {
      *error = "empty URL in the media.deezer.com response";
    }
    return std::nullopt;
  }
  return stream;
}

bool download_stream(const TrackStream& stream, long long track_id,
                     long long offset,
                     const std::function<bool(const char*, std::size_t)>& sink,
                     std::string* error) {
  const bool encrypted = stream.cipher == kBlowfishCipher;
  if (!encrypted && stream.cipher != "NONE") {
    if (error != nullptr) {
      *error = "unsupported cipher: '" + stream.cipher + "'";
    }
    return false;
  }

  if (offset < 0) {
    if (error != nullptr) {
      *error = "negative offset";
    }
    return false;
  }

  std::vector<std::string> headers;
  if (offset > 0) {
    headers.push_back("Range: bytes=" + std::to_string(offset) + "-");
  }

  const auto first_block = static_cast<std::uint64_t>(offset) /
                           crypto::StripeDecryptor::kBlockSize;
  crypto::StripeDecryptor decryptor(track_id, first_block);
  bool interrupted = false;

  const auto forward = [&](const char* data, std::size_t size) {
    if (!encrypted) {
      return forward_to_sink(sink, std::string(data, size), &interrupted);
    }
    return forward_to_sink(sink, decryptor.update(data, size), &interrupted);
  };

  if (!http::download(stream.url, headers, forward, error)) {
    if (interrupted && error != nullptr) {
      *error = "playback interrupted";
    }
    return false;
  }

  if (encrypted && !forward_to_sink(sink, decryptor.finish(), &interrupted)) {
    if (error != nullptr) {
      *error = "playback interrupted";
    }
    return false;
  }
  return true;
}

}
