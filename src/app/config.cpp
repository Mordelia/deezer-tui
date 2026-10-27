#include "app/config.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace app {
namespace {

std::string trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string unquote(const std::string& value) {
  if (value.size() < 2) {
    return value;
  }
  const char first = value.front();
  const char last = value.back();
  if ((first == '"' || first == '\'') && first == last) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

std::string config_dir() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME");
      xdg != nullptr && *xdg != '\0') {
    return std::string(xdg) + "/deezer-tui";
  }
  const char* home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    return {};
  }
  return std::string(home) + "/.config/deezer-tui";
}

std::string check_permissions(const std::string& path) {
  struct stat info {};
  if (::stat(path.c_str(), &info) != 0) {
    return {};
  }
  constexpr mode_t kLoose = S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  if ((info.st_mode & kLoose) == 0) {
    return {};
  }
  return "the config file is readable by the group or by everyone, "
         "run `chmod 600 " +
         path + "`";
}

std::map<std::string, std::string> parse_env(const std::string& content) {
  std::map<std::string, std::string> entries;

  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    const std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;
    }

    const auto separator = trimmed.find('=');
    if (separator == std::string::npos) {
      continue;
    }

    const std::string key = to_lower(trim(trimmed.substr(0, separator)));
    if (key.empty()) {
      continue;
    }
    entries[key] = unquote(trim(trimmed.substr(separator + 1)));
  }
  return entries;
}

std::string from_environment(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr ? trim(value) : std::string();
}

}

std::string config_path() {
  const std::string dir = config_dir();
  if (dir.empty()) {
    return {};
  }
  return dir + "/config";
}

namespace {

std::string state_dir() {
  if (const char* xdg = std::getenv("XDG_STATE_HOME");
      xdg != nullptr && *xdg != '\0') {
    return std::string(xdg) + "/deezer-tui";
  }
  const char* home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    return {};
  }
  return std::string(home) + "/.local/state/deezer-tui";
}

void make_directories(const std::string& path) {
  std::size_t start = 0;
  while (start <= path.size()) {
    const auto slash = path.find('/', start);
    const std::string partial =
        path.substr(0, slash == std::string::npos ? path.size() : slash);
    if (!partial.empty()) {
      ::mkdir(partial.c_str(), 0700);
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
}

}

std::string state_path() {
  const std::string dir = state_dir();
  if (dir.empty()) {
    return {};
  }
  return dir + "/profile";
}

void save_selected_profile(long long account_id) {
  const std::string path = state_path();
  if (path.empty()) {
    return;
  }
  make_directories(state_dir());

  std::ofstream file(path, std::ios::trunc);
  if (!file) {
    return;
  }
  file << account_id << "\n";
  file.close();

  ::chmod(path.c_str(), 0600);
}

std::optional<long long> load_selected_profile() {
  const std::string path = state_path();
  if (path.empty()) {
    return std::nullopt;
  }

  std::ifstream file(path);
  if (!file) {
    return std::nullopt;
  }

  long long account_id = 0;
  if (!(file >> account_id) || account_id == 0) {
    return std::nullopt;
  }
  return account_id;
}

ConfigLoad load_config() {
  ConfigLoad load;
  const std::string path = config_path();

  if (path.empty()) {
    load.error =
        "neither XDG_CONFIG_HOME nor HOME is set: cannot locate "
        "the configuration";
    return load;
  }

  std::ifstream file(path);
  if (file) {
    std::ostringstream buffer;
    buffer << file.rdbuf();

    load.warning = check_permissions(path);

    const auto entries = parse_env(buffer.str());
    for (const auto& [key, value] : entries) {
      if (key == "arl") {
        load.config.arl = value;
      } else if (key == "format") {
        load.config.format = value;
      } else {
        const std::string unknown = "unknown key in " + path + " : '" + key + "'";
        load.warning = load.warning.empty() ? unknown : load.warning + " ; " + unknown;
      }
    }
  }

  if (const std::string arl = from_environment("DEEZER_ARL"); !arl.empty()) {
    load.config.arl = arl;
  }
  if (const std::string format = from_environment("DEEZER_FORMAT");
      !format.empty()) {
    load.config.format = format;
  }

  return load;
}

}
