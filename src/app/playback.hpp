#pragma once

#include <functional>
#include <memory>
#include <string>

#include "api/types.hpp"
#include "mpris/server.hpp"

namespace app {

struct PlaybackSnapshot {
  long long track_id = 0;
  std::string label;
  bool loading = false;
  bool active = false;
  bool paused = false;
  double position = 0.0;
  double duration = 0.0;
  std::string error;
};

class PlaybackService {
 public:
  using Notify = std::function<void()>;

  explicit PlaybackService(Notify notify);
  ~PlaybackService();

  PlaybackService(const PlaybackService&) = delete;
  PlaybackService& operator=(const PlaybackService&) = delete;

  void play(const deezer::Item& item);

  void toggle_pause();
  void set_paused(bool paused);
  void stop();
  void seek_relative(double seconds);

  void set_mpris(mpris::Server* server);

  [[nodiscard]] PlaybackSnapshot snapshot() const;

  bool take_finished();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  void worker_loop();
};

}
