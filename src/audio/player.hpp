#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "api/stream.hpp"
#include "audio/byte_buffer.hpp"

struct mpv_handle;
struct mpv_stream_cb_info;

namespace audio {

struct PlayerStatus {
  long long track_id = 0;
  bool active = false;
  bool paused = false;
  double position = 0.0;
  double duration = 0.0;
};

class Player {
 public:
  Player() = default;
  ~Player();

  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  bool start(std::string* error);
  void shutdown();

  void play(const deezer::TrackStream& stream, long long track_id);

  void toggle_pause();
  void set_paused(bool paused);
  void stop();
  void seek_relative(double seconds);

  [[nodiscard]] PlayerStatus status() const;

  void set_on_change(std::function<void()> callback);
  void set_on_track_finished(std::function<void()> callback);

 private:
  struct StreamContext;

  static int OpenCallback(void* user_data, char* uri, mpv_stream_cb_info* info);
  static int64_t ReadCallback(void* cookie, char* buffer, uint64_t nbytes);
  static int64_t SeekCallback(void* cookie, int64_t offset);
  static int64_t SizeCallback(void* cookie);
  static void CloseCallback(void* cookie);
  static void CancelCallback(void* cookie);

  void event_loop();
  std::shared_ptr<StreamContext> take_pending_stream(long long track_id);
  void notify_change();

  mpv_handle* mpv_ = nullptr;

  mutable std::mutex mutex_;
  std::shared_ptr<StreamContext> pending_;
  PlayerStatus status_;
  std::function<void()> on_change_;
  std::function<void()> on_track_finished_;

  std::thread event_thread_;
  std::atomic<bool> running_{false};
};

}
