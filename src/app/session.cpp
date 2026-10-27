#include "app/session.hpp"

#include "app/config.hpp"

namespace app {

std::optional<deezer::Session> open_session(std::string* format,
                                            std::string* error) {
  const ConfigLoad loaded = load_config();
  if (!loaded.ok()) {
    if (error != nullptr) {
      *error = loaded.error;
    }
    return std::nullopt;
  }
  if (loaded.config.arl.empty()) {
    if (error != nullptr) {
      *error = "no ARL: set DEEZER_ARL or create " + config_path();
    }
    return std::nullopt;
  }

  auto session = deezer::login(loaded.config.arl, error);
  if (!session) {
    return std::nullopt;
  }

  const auto saved = load_selected_profile();
  if (saved.has_value() && *saved != 0) {
    std::string ignored;
    if (!deezer::select_profile(*session, *saved, &ignored)) {
      deezer::select_profile(*session, 0, &ignored);
    }
  }

  if (format != nullptr) {
    *format = loaded.config.format.empty() ? deezer::best_audio_format(*session)
                                           : loaded.config.format;
  }
  return session;
}

}
