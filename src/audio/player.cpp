#include "audio/player.hpp"

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <utility>

#include "crypto/blowfish.hpp"

namespace audio {
namespace {

constexpr const char* kProtocol = "dzr";
constexpr const char* kProtocolPrefix = "dzr://";

constexpr auto kNotifyInterval = std::chrono::milliseconds(100);

bool debug_enabled() {
  const char* value = std::getenv("DEEZER_MPV_DEBUG");
  return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
}

void apply_extra_options(mpv_handle* mpv) {
  const char* value = std::getenv("DEEZER_MPV_OPTIONS");
  if (value == nullptr || *value == '\0') {
    return;
  }

  const std::string list = value;
  std::size_t start = 0;
  while (start < list.size()) {
    const auto end = list.find(';', start);
    const std::string item =
        list.substr(start, end == std::string::npos ? std::string::npos
                                                    : end - start);
    start = end == std::string::npos ? list.size() : end + 1;

    const auto equals = item.find('=');
    if (equals == std::string::npos) {
      continue;
    }
    const std::string name = item.substr(0, equals);
    const std::string option = item.substr(equals + 1);
    if (!name.empty()) {
      mpv_set_option_string(mpv, name.c_str(), option.c_str());
    }
  }
}

std::string mpv_error(int code) {
  const char* text = mpv_error_string(code);
  return text != nullptr ? std::string(text) : "code " + std::to_string(code);
}

long long parse_track_id(const char* uri) {
  const std::string value = uri != nullptr ? uri : "";
  const auto position = value.find(kProtocolPrefix);
  if (position == std::string::npos) {
    return 0;
  }
  try {
    return std::stoll(value.substr(position + std::strlen(kProtocolPrefix)));
  } catch (const std::exception&) {
    return 0;
  }
}

}

struct Player::StreamContext {
  long long track_id = 0;
  deezer::TrackStream stream;
  ByteBuffer buffer;
  std::thread downloader;
  long long base_offset = 0;

  ~StreamContext() {
    buffer.cancel();
    if (downloader.joinable()) {
      downloader.join();
    }
  }

  void restart(long long offset) {
    if (debug_enabled()) {
      std::fprintf(stderr, "[restart] resuming at %lld\n",
                   static_cast<long long>(offset));
    }
    buffer.reset();
    if (downloader.joinable()) {
      downloader.join();
    }

    base_offset = offset;
    const std::uint64_t epoch = buffer.epoch();

    downloader = std::thread([this, epoch] {
      std::string error;
      deezer::download_stream(
          stream, track_id, base_offset,
          [this, epoch](const char* data, std::size_t size) {
            return buffer.push(data, size, epoch);
          },
          &error);
      buffer.finish(epoch);
    });
  }
};

int Player::OpenCallback(void* user_data, char* uri, mpv_stream_cb_info* info) {
  auto* player = static_cast<Player*>(user_data);

  auto context = player->take_pending_stream(parse_track_id(uri));
  if (!context) {
    return MPV_ERROR_LOADING_FAILED;
  }

  context->restart(0);

  info->cookie = new std::shared_ptr<StreamContext>(std::move(context));
  info->read_fn = &Player::ReadCallback;
  info->close_fn = &Player::CloseCallback;
  info->cancel_fn = &Player::CancelCallback;
  info->seek_fn = &Player::SeekCallback;
  info->size_fn = &Player::SizeCallback;
  return 0;
}

int64_t Player::SeekCallback(void* cookie, int64_t offset) {
  auto* context = static_cast<std::shared_ptr<StreamContext>*>(cookie);
  StreamContext& stream_context = **context;

  if (debug_enabled()) {
    std::fprintf(stderr, "[seek] requested=%lld base=%lld\n",
                   static_cast<long long>(offset),
                   static_cast<long long>(stream_context.base_offset));
  }

  if (offset < 0) {
    return MPV_ERROR_UNSUPPORTED;
  }

  constexpr int64_t kBlock =
      static_cast<int64_t>(crypto::StripeDecryptor::kBlockSize);
  const int64_t aligned = (offset / kBlock) * kBlock;

  if (stream_context.stream.size > 0 &&
      aligned >= stream_context.stream.size) {
    if (debug_enabled()) {
      std::fprintf(stderr, "[seek] refused (past end of stream)\n");
    }
    return MPV_ERROR_UNSUPPORTED;
  }
  if (aligned == stream_context.base_offset) {
    if (debug_enabled()) {
      std::fprintf(stderr, "[seek] already at %lld\n",
                   static_cast<long long>(aligned));
    }
    return aligned;
  }

  stream_context.restart(aligned);
  if (debug_enabled()) {
    std::fprintf(stderr, "[seek] restarted at %lld\n",
                   static_cast<long long>(aligned));
  }
  return aligned;
}

int64_t Player::SizeCallback(void* cookie) {
  auto* context = static_cast<std::shared_ptr<StreamContext>*>(cookie);
  const long long size = (*context)->stream.size;
  return size > 0 ? static_cast<int64_t>(size)
                  : static_cast<int64_t>(MPV_ERROR_UNSUPPORTED);
}

int64_t Player::ReadCallback(void* cookie, char* buffer, uint64_t nbytes) {
  auto* context = static_cast<std::shared_ptr<StreamContext>*>(cookie);
  const std::size_t count =
      (*context)->buffer.read(buffer, static_cast<std::size_t>(nbytes));
  return static_cast<int64_t>(count);
}

void Player::CloseCallback(void* cookie) {
  delete static_cast<std::shared_ptr<StreamContext>*>(cookie);
}

void Player::CancelCallback(void* cookie) {
  auto* context = static_cast<std::shared_ptr<StreamContext>*>(cookie);
  (*context)->buffer.cancel();
}

std::shared_ptr<Player::StreamContext> Player::take_pending_stream(
    long long track_id) {
  std::lock_guard lock(mutex_);
  if (!pending_ || pending_->track_id != track_id) {
    return nullptr;
  }
  return std::exchange(pending_, nullptr);
}

bool Player::start(std::string* error) {
  if (mpv_ != nullptr) {
    return true;
  }

  mpv_ = mpv_create();
  if (mpv_ == nullptr) {
    if (error != nullptr) {
      *error = "mpv_create() failed";
    }
    return false;
  }

  mpv_set_option_string(mpv_, "vid", "no");
  mpv_set_option_string(mpv_, "audio-display", "no");
  mpv_set_option_string(mpv_, "terminal", "no");
  mpv_set_option_string(mpv_, "input-default-bindings", "no");
  mpv_set_option_string(mpv_, "input-vo-keyboard", "no");
  mpv_set_option_string(mpv_, "idle", "yes");
  mpv_set_option_string(mpv_, "keep-open", "no");
  apply_extra_options(mpv_);

  const int initialized = mpv_initialize(mpv_);
  if (initialized < 0) {
    if (error != nullptr) {
      *error = "mpv_initialize() : " + mpv_error(initialized);
    }
    mpv_terminate_destroy(mpv_);
    mpv_ = nullptr;
    return false;
  }

  const int registered =
      mpv_stream_cb_add_ro(mpv_, kProtocol, this, &Player::OpenCallback);
  if (registered < 0) {
    if (error != nullptr) {
      *error = "enregistrement du protocole dzr:// : " + mpv_error(registered);
    }
    mpv_terminate_destroy(mpv_);
    mpv_ = nullptr;
    return false;
  }

  mpv_observe_property(mpv_, 0, "pause", MPV_FORMAT_FLAG);
  mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE);
  mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE);
  mpv_request_log_messages(mpv_, debug_enabled() ? "debug" : "error");

  running_ = true;
  event_thread_ = std::thread(&Player::event_loop, this);
  return true;
}

void Player::shutdown() {
  if (mpv_ == nullptr) {
    return;
  }

  running_ = false;
  const char* command[] = {"quit", nullptr};
  mpv_command(mpv_, command);

  if (event_thread_.joinable()) {
    event_thread_.join();
  }

  mpv_terminate_destroy(mpv_);
  mpv_ = nullptr;

  std::lock_guard lock(mutex_);
  pending_ = nullptr;
}

Player::~Player() { shutdown(); }

void Player::play(const deezer::TrackStream& stream, long long track_id) {
  if (mpv_ == nullptr) {
    return;
  }

  auto context = std::make_shared<StreamContext>();
  context->track_id = track_id;
  context->stream = stream;

  {
    std::lock_guard lock(mutex_);
    pending_ = context;
    status_.track_id = track_id;
    status_.active = false;
    status_.paused = false;
    status_.position = 0.0;
    status_.duration = 0.0;
  }

  const std::string uri =
      std::string(kProtocolPrefix) + std::to_string(track_id);
  const char* command[] = {"loadfile", uri.c_str(), "replace", nullptr};

  if (mpv_command(mpv_, command) < 0) {
    std::lock_guard lock(mutex_);
    pending_ = nullptr;
  }
  notify_change();
}

void Player::toggle_pause() {
  if (mpv_ == nullptr) {
    return;
  }
  int paused = 0;
  if (mpv_get_property(mpv_, "pause", MPV_FORMAT_FLAG, &paused) < 0) {
    return;
  }
  paused = paused == 0 ? 1 : 0;
  mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &paused);
}

void Player::set_paused(bool paused) {
  if (mpv_ == nullptr) {
    return;
  }
  int value = paused ? 1 : 0;
  mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &value);
}

void Player::stop() {
  if (mpv_ == nullptr) {
    return;
  }
  const char* command[] = {"stop", nullptr};
  mpv_command(mpv_, command);
}

void Player::seek_relative(double seconds) {
  if (mpv_ == nullptr) {
    return;
  }
  const std::string value = std::to_string(seconds);
  const char* command[] = {"seek", value.c_str(), "relative", nullptr};
  mpv_command(mpv_, command);
}

PlayerStatus Player::status() const {
  std::lock_guard lock(mutex_);
  return status_;
}

void Player::set_on_change(std::function<void()> callback) {
  std::lock_guard lock(mutex_);
  on_change_ = std::move(callback);
}

void Player::set_on_track_finished(std::function<void()> callback) {
  std::lock_guard lock(mutex_);
  on_track_finished_ = std::move(callback);
}

void Player::notify_change() {
  std::function<void()> callback;
  {
    std::lock_guard lock(mutex_);
    callback = on_change_;
  }
  if (callback) {
    callback();
  }
}

void Player::event_loop() {
  auto last_notify = std::chrono::steady_clock::now();

  while (running_) {
    mpv_event* event = mpv_wait_event(mpv_, 0.1);
    if (event == nullptr) {
      continue;
    }

    switch (event->event_id) {
      case MPV_EVENT_NONE:
        break;

      case MPV_EVENT_LOG_MESSAGE: {
        if (!debug_enabled()) {
          break;
        }
        auto* message = static_cast<mpv_event_log_message*>(event->data);
        if (message != nullptr) {
          std::fprintf(stderr, "[mpv/%s] %s",
                       message->prefix != nullptr ? message->prefix : "?",
                       message->text != nullptr ? message->text : "");
        }
        break;
      }

      case MPV_EVENT_SHUTDOWN:
        running_ = false;
        return;

      case MPV_EVENT_PROPERTY_CHANGE: {
        auto* property = static_cast<mpv_event_property*>(event->data);
        if (property == nullptr || property->data == nullptr ||
            property->name == nullptr) {
          break;
        }

        bool throttled = false;
        {
          std::lock_guard lock(mutex_);
          const std::string name = property->name;
          if (name == "pause" && property->format == MPV_FORMAT_FLAG) {
            status_.paused = *static_cast<int*>(property->data) != 0;
          } else if (name == "time-pos" &&
                     property->format == MPV_FORMAT_DOUBLE) {
            status_.position = *static_cast<double*>(property->data);
            throttled = true;
          } else if (name == "duration" &&
                     property->format == MPV_FORMAT_DOUBLE) {
            status_.duration = *static_cast<double*>(property->data);
          }
        }

        if (throttled) {
          const auto now = std::chrono::steady_clock::now();
          if (now - last_notify < kNotifyInterval) {
            break;
          }
          last_notify = now;
        }
        notify_change();
        break;
      }

      case MPV_EVENT_START_FILE: {
        {
          std::lock_guard lock(mutex_);
          status_.active = true;
          status_.paused = false;
          status_.position = 0.0;
          status_.duration = 0.0;
        }
        notify_change();
        break;
      }

      case MPV_EVENT_END_FILE: {
        auto* end = static_cast<mpv_event_end_file*>(event->data);
        const bool natural =
            end != nullptr && end->reason == MPV_END_FILE_REASON_EOF;

        std::function<void()> callback;
        {
          std::lock_guard lock(mutex_);
          status_.active = false;
          status_.paused = false;
          status_.position = 0.0;
          if (natural) {
            callback = on_track_finished_;
          }
        }
        notify_change();
        if (callback) {
          callback();
        }
        break;
      }

      default:
        break;
    }
  }
}

}
