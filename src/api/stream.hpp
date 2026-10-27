#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

#include "api/auth.hpp"

namespace deezer {

struct TrackStream {
  std::string url;
  std::string cipher;
  std::string format;
  long long size = 0;
};

std::optional<TrackStream> get_track_stream(Session& session,
                                            long long track_id,
                                            const std::string& format,
                                            std::string* error);

bool download_stream(const TrackStream& stream, long long track_id,
                     long long offset,
                     const std::function<bool(const char*, std::size_t)>& sink,
                     std::string* error);

}
