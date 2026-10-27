#include "cli/show_config.hpp"

#include <cstdio>
#include <string>

#include "app/config.hpp"

namespace cli {
namespace {

std::string redact(const std::string& secret) {
  if (secret.empty()) {
    return "(absent)";
  }
  if (secret.size() <= 12) {
    return std::string(secret.size(), '*');
  }
  return secret.substr(0, 6) + "..." + secret.substr(secret.size() - 4) + " (" +
         std::to_string(secret.size()) + " characters)";
}

}

int run_show_config() {
  const std::string path = app::config_path();
  std::printf("config file : %s\n", path.c_str());

  const app::ConfigLoad loaded = app::load_config();
  if (!loaded.ok()) {
    std::fprintf(stderr, "error: %s\n", loaded.error.c_str());
    return 1;
  }
  if (!loaded.warning.empty()) {
    std::fprintf(stderr, "warning: %s\n", loaded.warning.c_str());
  }

  std::printf("  arl    : %s\n", redact(loaded.config.arl).c_str());
  std::printf("  format : %s\n", loaded.config.format.empty()
                                     ? "(automatic)"
                                     : loaded.config.format.c_str());

  const std::string state = app::state_path();
  std::printf("state file  : %s\n", state.c_str());

  const auto profile = app::load_selected_profile();
  std::printf("  profile : %s\n",
              profile.has_value() ? std::to_string(*profile).c_str()
                                  : "(none, the ARL account)");
  return 0;
}

}
