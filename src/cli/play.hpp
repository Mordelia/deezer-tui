#pragma once

#include <string>

namespace cli {

struct PlayOptions {
  long long track_id = 0;
  std::string format;
};

int run_play(const PlayOptions& options);

}
