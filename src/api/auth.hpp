#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "api/http.hpp"

namespace deezer {

struct Profile {
  long long id = 0;
  std::string name;
};

struct Session {
  std::string arl;
  long long user_id = 0;
  std::string api_token;
  std::string license_token;
  std::string country;
  std::vector<std::string> formats;

  std::string account_name;

  long long account_id = 0;

  std::vector<Profile> profiles;

  http::CookieJar cookies;
};

std::optional<Session> login(const std::string& arl, std::string* error);

bool select_profile(Session& session, long long account_id, std::string* error);

std::optional<nlohmann::json> gw_light(Session& session,
                                       const std::string& method,
                                       const nlohmann::json& payload,
                                       std::string* error);

std::string best_audio_format(const Session& session);

}
