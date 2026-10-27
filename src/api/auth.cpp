#include "api/auth.hpp"

#include <algorithm>
#include <random>

#include "api/http.hpp"

namespace deezer {
namespace {

constexpr const char* kGwLightUrl = "https://www.deezer.com/ajax/gw-light.php";

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

std::vector<std::string> browser_headers() {
  return {
      "Content-Type: text/plain;charset=UTF-8",
      "Origin: https://www.deezer.com",
      "Referer: https://www.deezer.com/",
      "Accept-Language: en-US,en;q=0.9",
  };
}

std::string cookie_header(const Session& session) {
  std::string header = "arl=" + session.arl;
  if (session.account_id != 0) {
    header += "; account_id=" + std::to_string(session.account_id);
  }
  const std::string extra = session.cookies.cookie_header();
  if (!extra.empty()) {
    header += "; " + extra;
  }
  return header;
}

std::optional<nlohmann::json> extract_results(const std::string& body,
                                              std::string* error) {
  nlohmann::json document;
  try {
    document = nlohmann::json::parse(body);
  } catch (const std::exception& exception) {
    if (error != nullptr) {
      *error = std::string("invalid JSON response: ") + exception.what();
    }
    return std::nullopt;
  }

  const auto it = document.find("error");
  if (it != document.end() && it->is_object() && !it->empty()) {
    const auto first = it->begin();
    if (error != nullptr) {
      *error = first.key() + " : " + first.value().get<std::string>();
    }
    return std::nullopt;
  }

  const auto results = document.find("results");
  if (results == document.end()) {
    if (error != nullptr) {
      *error = "gw-light response without a 'results' field";
    }
    return std::nullopt;
  }
  return *results;
}

bool refresh(Session& session, std::string* error) {
  auto results =
      gw_light(session, "deezer.getUserData", nlohmann::json::object(), error);
  if (!results) {
    return false;
  }

  const auto user = results->find("USER");
  if (user == results->end() || !user->is_object()) {
    if (error != nullptr) {
      *error = "unexpected response from deezer.getUserData";
    }
    return false;
  }

  session.user_id = json_id(*user, "USER_ID");
  if (session.user_id == 0) {
    if (error != nullptr) {
      *error = "invalid or expired ARL (USER_ID = 0)";
    }
    return false;
  }

  session.api_token = results->value("checkForm", "");
  if (session.api_token.empty()) {
    if (error != nullptr) {
      *error = "session token missing from the response";
    }
    return false;
  }

  session.account_name = json_string(*user, "BLOG_NAME");
  session.country = results->value("COUNTRY", "");

  session.formats.clear();
  session.formats.push_back("MP3_128");
  const auto options = user->find("OPTIONS");
  if (options != user->end() && options->is_object()) {
    session.license_token = options->value("license_token", "");
    if (options->value("web_hq", false)) {
      session.formats.push_back("MP3_320");
    }
    if (options->value("web_lossless", false)) {
      session.formats.push_back("FLAC");
    }
  }
  return true;
}

void load_profiles(Session& session) {
  session.profiles.clear();

  std::string ignored;
  auto results = gw_light(session, "deezer.getChildAccounts",
                          nlohmann::json::object(), &ignored);
  if (!results || !results->is_array()) {
    return;
  }

  for (const auto& entry : *results) {
    Profile profile;
    profile.id = json_id(entry, "USER_ID");
    profile.name = json_string(entry, "BLOG_NAME");
    if (profile.id != 0) {
      session.profiles.push_back(std::move(profile));
    }
  }
}

}

std::optional<nlohmann::json> gw_light(Session& session,
                                       const std::string& method,
                                       const nlohmann::json& payload,
                                       std::string* error) {
  const std::string token =
      method == "deezer.getUserData" ? std::string() : session.api_token;

  static std::mt19937 generator(std::random_device{}());
  std::uniform_int_distribution<int> distribution(0, 1000000000);

  http::Request request;
  request.method = "POST";
  request.url = std::string(kGwLightUrl) + "?method=" + method +
                "&input=3&api_version=1.0" +
                "&api_token=" + http::url_encode(token) +
                "&cid=" + std::to_string(distribution(generator));
  request.headers = browser_headers();
  request.headers.push_back("Cookie: " + cookie_header(session));
  request.body = payload.dump();

  auto response = http::request(request, error);
  if (!response) {
    return std::nullopt;
  }
  if (!response->ok()) {
    if (error != nullptr) {
      *error = "HTTP " + std::to_string(response->status);
    }
    return std::nullopt;
  }

  session.cookies.absorb(*response);

  return extract_results(response->body, error);
}

std::optional<Session> login(const std::string& arl, std::string* error) {
  Session session;
  session.arl = arl;

  if (!refresh(session, error)) {
    return std::nullopt;
  }
  load_profiles(session);
  return session;
}

bool select_profile(Session& session, long long account_id, std::string* error) {
  if (session.account_id == account_id) {
    return true;
  }

  session.account_id = account_id;
  session.cookies = http::CookieJar{};
  session.license_token.clear();

  return refresh(session, error);
}

std::string best_audio_format(const Session& session) {
  for (const char* preferred : {"MP3_320", "MP3_128"}) {
    if (std::find(session.formats.begin(), session.formats.end(), preferred) !=
        session.formats.end()) {
      return preferred;
    }
  }
  return "MP3_128";
}

}
