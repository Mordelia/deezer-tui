#pragma once

#include <optional>
#include <string>

namespace app {

struct Config {
  std::string arl;
  std::string format;
};

struct ConfigLoad {
  Config config;
  std::string error;
  std::string warning;

  [[nodiscard]] bool ok() const { return error.empty(); }
};

std::string config_path();

std::string state_path();

void save_selected_profile(long long account_id);

std::optional<long long> load_selected_profile();

ConfigLoad load_config();

}
