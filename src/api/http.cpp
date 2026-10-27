#include "api/http.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <mutex>

namespace http {
namespace {

std::once_flag g_init_once;

constexpr long kConnectTimeoutSeconds = 5;
constexpr long kTimeoutSeconds = 10;

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

size_t write_callback(char* data, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(data, size * nmemb);
  return size * nmemb;
}

size_t header_callback(char* data, size_t size, size_t nmemb, void* userdata) {
  auto* headers =
      static_cast<std::multimap<std::string, std::string>*>(userdata);

  const std::string line(data, size * nmemb);
  const auto colon = line.find(':');
  if (colon != std::string::npos) {
    headers->emplace(to_lower(trim(line.substr(0, colon))),
                     trim(line.substr(colon + 1)));
  }
  return size * nmemb;
}

struct WriteContext {
  std::string* body = nullptr;
  const std::function<bool(const char*, std::size_t)>* sink = nullptr;
};

size_t dispatch_callback(char* data, size_t size, size_t nmemb, void* userdata) {
  auto* context = static_cast<WriteContext*>(userdata);
  const std::size_t length = size * nmemb;

  if (context->sink != nullptr) {
    return (*context->sink)(data, length) ? length : 0;
  }
  context->body->append(data, length);
  return length;
}

void configure(CURL* curl, const std::string& url) {
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTimeoutSeconds);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "deezer-tui/0.1");
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
}

void apply_headers(CURL* curl, curl_slist** list,
                   const std::vector<std::string>& headers) {
  for (const std::string& header : headers) {
    *list = curl_slist_append(*list, header.c_str());
  }
  if (*list != nullptr) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, *list);
  }
}

std::string error_message(const char* buffer, CURLcode code) {
  return buffer[0] != '\0' ? std::string(buffer) : curl_easy_strerror(code);
}

}

std::string url_encode(const std::string& value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size());
  for (const unsigned char c : value) {
    if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else if (c == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0x0F]);
    }
  }
  return out;
}

std::vector<std::string> Response::header_values(std::string_view name) const {
  std::vector<std::string> values;
  const auto range = headers.equal_range(std::string(name));
  for (auto it = range.first; it != range.second; ++it) {
    values.push_back(it->second);
  }
  return values;
}

void CookieJar::absorb(const Response& response) {
  for (const std::string& value : response.header_values("set-cookie")) {
    const auto separator = value.find(';');
    const std::string pair =
        separator == std::string::npos ? value : value.substr(0, separator);

    const auto equals = pair.find('=');
    if (equals == std::string::npos) {
      continue;
    }
    const std::string name = trim(pair.substr(0, equals));
    if (name.empty()) {
      continue;
    }
    cookies_[name] = trim(pair.substr(equals + 1));
  }
}

std::string CookieJar::cookie_header() const {
  std::string out;
  for (const auto& [name, value] : cookies_) {
    if (!out.empty()) {
      out += "; ";
    }
    out += name + "=" + value;
  }
  return out;
}

void init() {
  std::call_once(g_init_once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::optional<Response> request(const Request& request, std::string* error) {
  init();

  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    if (error != nullptr) {
      *error = "could not initialise curl";
    }
    return std::nullopt;
  }

  Response response;
  curl_slist* header_list = nullptr;
  char error_buffer[CURL_ERROR_SIZE] = {0};

  configure(curl, request.url);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
  apply_headers(curl, &header_list, request.headers);

  if (request.method != "GET") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(request.body.size()));
  }

  const CURLcode code = curl_easy_perform(curl);

  if (code != CURLE_OK) {
    if (error != nullptr) {
      *error = error_message(error_buffer, code);
    }
    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return std::nullopt;
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
  curl_slist_free_all(header_list);
  curl_easy_cleanup(curl);
  return response;
}

std::optional<Response> get(const std::string& url, std::string* error) {
  Request req;
  req.url = url;
  return request(req, error);
}

bool download(const std::string& url,
              const std::vector<std::string>& headers,
              const std::function<bool(const char*, std::size_t)>& sink,
              std::string* error) {
  init();

  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    if (error != nullptr) {
      *error = "could not initialise curl";
    }
    return false;
  }

  curl_slist* header_list = nullptr;
  char error_buffer[CURL_ERROR_SIZE] = {0};
  WriteContext context;
  context.sink = &sink;

  configure(curl, url);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dispatch_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
  apply_headers(curl, &header_list, headers);

  const CURLcode code = curl_easy_perform(curl);

  if (code != CURLE_OK) {
    if (error != nullptr) {
      *error = error_message(error_buffer, code);
    }
    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return false;
  }

  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(header_list);
  curl_easy_cleanup(curl);

  if (status < 200 || status >= 300) {
    if (error != nullptr) {
      *error = "HTTP " + std::to_string(status);
    }
    return false;
  }
  return true;
}

}
