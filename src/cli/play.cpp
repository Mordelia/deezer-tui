#include "cli/play.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <mutex>
#include <string>

#include "api/auth.hpp"
#include "api/stream.hpp"
#include "app/config.hpp"
#include "app/session.hpp"
#include "audio/player.hpp"

namespace cli {
namespace {

void print_formats(const deezer::Session& session) {
  std::printf("  available formats: ");
  for (std::size_t i = 0; i < session.formats.size(); ++i) {
    std::printf("%s%s", i == 0 ? "" : ", ", session.formats[i].c_str());
  }
  std::printf("\n");
}

long long now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}

int run_play(const PlayOptions& options) {
  std::signal(SIGPIPE, SIG_IGN);
  std::setvbuf(stdout, nullptr, _IOLBF, 0);

  std::string error;

  const app::ConfigLoad loaded = app::load_config();
  if (!loaded.warning.empty()) {
    std::fprintf(stderr, "warning: %s\n", loaded.warning.c_str());
  }
  if (!loaded.ok()) {
    std::fprintf(stderr, "error: %s\n", loaded.error.c_str());
    return 1;
  }

  std::printf("connecting to Deezer...\n");
  std::string configured_format;
  auto session = app::open_session(&configured_format, &error);
  if (!session) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  std::printf("  account %lld (%s) — %s\n", session->user_id,
              session->country.c_str(), session->account_name.c_str());
  print_formats(*session);

  std::string format = options.format;
  if (format.empty()) {
    format = configured_format;
  }
  if (format.empty()) {
    format = deezer::best_audio_format(*session);
  }
  std::printf("  format: %s\n", format.c_str());

  std::printf("resolving track %lld...\n", options.track_id);
  auto stream =
      deezer::get_track_stream(*session, options.track_id, format, &error);
  if (!stream) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  std::printf("  cipher: %s\n", stream->cipher.c_str());

  audio::Player player;
  if (!player.start(&error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }

  std::mutex mutex;
  std::condition_variable finished;
  bool done = false;
  std::atomic<long long> last_print_ms{0};

  player.set_on_change([&] {
    const long long now = now_ms();
    if (now - last_print_ms.load() < 500) {
      return;
    }
    last_print_ms = now;
    const audio::PlayerStatus status = player.status();
    std::printf("\r  %-8s %6.1f / %6.1f s",
                status.paused ? "paused" : "playing", status.position,
                status.duration);
    std::fflush(stdout);
  });

  player.set_on_track_finished([&] {
    std::lock_guard lock(mutex);
    done = true;
    finished.notify_one();
  });

  std::printf("playing...\n");
  player.play(*stream, options.track_id);

  {
    std::unique_lock lock(mutex);
    finished.wait(lock, [&] { return done; });
  }

  const audio::PlayerStatus status = player.status();
  std::printf("\r  finished (duration %.1f s)                 \n", status.duration);

  player.shutdown();
  return 0;
}

}
