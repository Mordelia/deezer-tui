#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace http {

struct Response {
  long status = 0;
  std::string body;
  std::multimap<std::string, std::string> headers;

  [[nodiscard]] bool ok() const { return status >= 200 && status < 300; }

  [[nodiscard]] std::vector<std::string> header_values(
      std::string_view name) const;
};

class CookieJar {
 public:
  void absorb(const Response& response);

  [[nodiscard]] std::string cookie_header() const;

  [[nodiscard]] std::size_t size() const { return cookies_.size(); }

 private:
  std::map<std::string, std::string> cookies_;
};

struct Request {
  std::string method = "GET";
  std::string url;
  std::vector<std::string> headers;
  std::string body;
};

void init();

std::optional<Response> request(const Request& request, std::string* error);

std::optional<Response> get(const std::string& url, std::string* error = nullptr);

std::string url_encode(const std::string& value);

bool download(const std::string& url,
              const std::vector<std::string>& headers,
              const std::function<bool(const char*, std::size_t)>& sink,
              std::string* error);

}
