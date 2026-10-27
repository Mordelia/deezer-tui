#include "app/playback.hpp"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "api/auth.hpp"
#include "api/stream.hpp"
#include "app/session.hpp"
#include "audio/player.hpp"

namespace app {
namespace {

struct Job {
  deezer::Item item;
};

}

struct PlaybackService::Impl {
  std::mutex mutex;
  std::condition_variable condition;
  bool stop = false;

  std::optional<Job> job;
  bool finished = false;

  PlaybackSnapshot snapshot;

  deezer::Item current;
  mpris::Server* mpris = nullptr;

  audio::Player player;
  bool player_ready = false;
  std::optional<deezer::Session> session;
  std::string format;

  Notify notify;
  std::thread worker;

  ~Impl() {
    {
      std::lock_guard lock(mutex);
      stop = true;
    }
    condition.notify_all();
    if (worker.joinable()) {
      worker.join();
    }
  }

  void publish_error(const std::string& error) {
    {
      std::lock_guard lock(mutex);
      snapshot.loading = false;
      snapshot.error = error;
    }
    notify();
  }

  void publish_mpris() {
    mpris::Server* server = nullptr;
    deezer::Item item;
    {
      std::lock_guard lock(mutex);
      server = mpris;
      item = current;
    }
    if (server == nullptr) {
      return;
    }

    const audio::PlayerStatus status = player.status();

    mpris::Track track;
    track.id = item.id;
    track.title = item.title;
    track.artist = item.subtitle;
    track.album = item.album;
    track.art_url = deezer::cover_url(item);
    track.length_us = static_cast<long long>(item.duration) * 1000000LL;
    if (track.length_us == 0 && status.duration > 0.0) {
      track.length_us = static_cast<long long>(status.duration * 1e6);
    }

    server->publish(status.active, status.paused,
                    static_cast<long long>(status.position * 1e6), track);
  }
};

PlaybackService::PlaybackService(Notify notify)
    : impl_(std::make_unique<Impl>()) {
  impl_->notify = std::move(notify);
  impl_->worker = std::thread(&PlaybackService::worker_loop, this);
}

PlaybackService::~PlaybackService() = default;

void PlaybackService::play(const deezer::Item& item) {
  {
    std::lock_guard lock(impl_->mutex);
    impl_->job = Job{item};
    impl_->current = item;
    impl_->snapshot.track_id = item.id;
    impl_->snapshot.label = deezer::format_item(item);
    impl_->snapshot.loading = true;
    impl_->snapshot.error.clear();
  }
  impl_->condition.notify_one();
  impl_->notify();
}

void PlaybackService::toggle_pause() {
  impl_->player.toggle_pause();
}

void PlaybackService::stop() { impl_->player.stop(); }

void PlaybackService::set_paused(bool paused) {
  impl_->player.set_paused(paused);
}

void PlaybackService::seek_relative(double seconds) {
  impl_->player.seek_relative(seconds);
  impl_->publish_mpris();

  mpris::Server* server = nullptr;
  {
    std::lock_guard lock(impl_->mutex);
    server = impl_->mpris;
  }
  if (server != nullptr) {
    const auto position_us =
        static_cast<long long>(impl_->player.status().position * 1e6);
    server->notify_seeked(position_us);
  }
}

void PlaybackService::set_mpris(mpris::Server* server) {
  {
    std::lock_guard lock(impl_->mutex);
    impl_->mpris = server;
  }
  impl_->publish_mpris();
}

PlaybackSnapshot PlaybackService::snapshot() const {
  std::lock_guard lock(impl_->mutex);
  PlaybackSnapshot result = impl_->snapshot;

  const audio::PlayerStatus status = impl_->player.status();
  if (status.track_id != 0) {
    result.active = status.active;
    result.paused = status.paused;
    result.position = status.position;
    result.duration = status.duration;
  }
  return result;
}

bool PlaybackService::take_finished() {
  std::lock_guard lock(impl_->mutex);
  const bool finished = impl_->finished;
  impl_->finished = false;
  return finished;
}

void PlaybackService::worker_loop() {
  for (;;) {
    Job job;
    {
      std::unique_lock lock(impl_->mutex);
      impl_->condition.wait(lock, [this] {
        return impl_->stop || impl_->job.has_value();
      });
      if (impl_->stop) {
        return;
      }
      job = std::move(*impl_->job);
      impl_->job.reset();
    }

    if (!impl_->player_ready) {
      std::string error;
      if (!impl_->player.start(&error)) {
        impl_->publish_error(error);
        continue;
      }
      impl_->player_ready = true;
      impl_->player.set_on_change([this] {
        impl_->publish_mpris();
        impl_->notify();
      });
      impl_->player.set_on_track_finished([this] {
        {
          std::lock_guard lock(impl_->mutex);
          impl_->finished = true;
        }
        impl_->notify();
      });
    }

    if (!impl_->session) {
      std::string error;
      auto session = open_session(&impl_->format, &error);
      if (!session) {
        impl_->publish_error(error);
        continue;
      }
      impl_->session = std::move(session);
    }

    std::string error;
    auto stream = deezer::get_track_stream(*impl_->session, job.item.id,
                                           impl_->format, &error);
    if (!stream) {
      impl_->publish_error(error);
      continue;
    }

    {
      std::lock_guard lock(impl_->mutex);
      impl_->snapshot.loading = false;
      impl_->snapshot.error.clear();
    }
    impl_->player.play(*stream, job.item.id);
    impl_->publish_mpris();
  }
}

}
