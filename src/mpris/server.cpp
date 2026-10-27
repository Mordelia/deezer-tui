#include "mpris/server.hpp"

#include <systemd/sd-bus.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

namespace mpris {
namespace {

constexpr const char* kBusName = "org.mpris.MediaPlayer2.deezer-tui";
constexpr const char* kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char* kRootInterface = "org.mpris.MediaPlayer2";
constexpr const char* kPlayerInterface = "org.mpris.MediaPlayer2.Player";

constexpr const char* kIdentity = "Deezer";

std::string bus_error(int code) { return std::string(strerror(-code)); }


int open_entry(sd_bus_message* reply, const char* key) {
  int r = sd_bus_message_open_container(reply, 'e', "sv");
  if (r < 0) {
    return r;
  }
  return sd_bus_message_append(reply, "s", key);
}

int append_string(sd_bus_message* reply, const char* key,
                  const std::string& value) {
  int r = open_entry(reply, key);
  if (r < 0) {
    return r;
  }
  if ((r = sd_bus_message_append(reply, "v", "s", value.c_str())) < 0) {
    return r;
  }
  return sd_bus_message_close_container(reply);
}

int append_object_path(sd_bus_message* reply, const char* key,
                       const std::string& value) {
  int r = open_entry(reply, key);
  if (r < 0) {
    return r;
  }
  if ((r = sd_bus_message_append(reply, "v", "o", value.c_str())) < 0) {
    return r;
  }
  return sd_bus_message_close_container(reply);
}

int append_int64(sd_bus_message* reply, const char* key, long long value) {
  int r = open_entry(reply, key);
  if (r < 0) {
    return r;
  }
  if ((r = sd_bus_message_append(reply, "v", "x",
                                 static_cast<int64_t>(value))) < 0) {
    return r;
  }
  return sd_bus_message_close_container(reply);
}

int append_string_array(sd_bus_message* reply, const char* key,
                        const std::string& value) {
  int r = open_entry(reply, key);
  if (r < 0) {
    return r;
  }
  const int count = value.empty() ? 0 : 1;
  if ((r = sd_bus_message_append(reply, "v", "as", count, value.c_str())) < 0) {
    return r;
  }
  return sd_bus_message_close_container(reply);
}

}

struct ServerImpl {
  sd_bus* bus = nullptr;
  Controls controls;

  mutable std::mutex mutex;
  bool active = false;
  bool paused = false;
  long long position_us = 0;
  Track track;

  std::thread loop;
  std::atomic<bool> running{false};

  [[nodiscard]] std::string playback_status() const {
    std::lock_guard lock(mutex);
    if (!active) {
      return "Stopped";
    }
    return paused ? "Paused" : "Playing";
  }

  void invoke(std::function<void()> Controls::*member) {
    std::function<void()> action;
    {
      std::lock_guard lock(mutex);
      action = controls.*member;
    }
    if (action) {
      action();
    }
  }

  void invoke_seek(double seconds) {
    std::function<void(double)> action;
    {
      std::lock_guard lock(mutex);
      action = controls.seek_relative;
    }
    if (action) {
      action(seconds);
    }
  }


  static int get_identity(sd_bus*, const char*, const char*, const char*,
                          sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", kIdentity);
  }

  static int get_playback_status(sd_bus*, const char*, const char*, const char*,
                                 sd_bus_message* reply, void* userdata,
                                 sd_bus_error*) {
    auto* self = static_cast<ServerImpl*>(userdata);
    return sd_bus_message_append(reply, "s", self->playback_status().c_str());
  }

  static int get_position(sd_bus*, const char*, const char*, const char*,
                          sd_bus_message* reply, void* userdata, sd_bus_error*) {
    auto* self = static_cast<ServerImpl*>(userdata);
    std::lock_guard lock(self->mutex);
    return sd_bus_message_append(reply, "x",
                                 static_cast<int64_t>(self->position_us));
  }

  static int get_metadata(sd_bus*, const char*, const char*, const char*,
                          sd_bus_message* reply, void* userdata, sd_bus_error*) {
    auto* self = static_cast<ServerImpl*>(userdata);
    std::lock_guard lock(self->mutex);

    const std::string track_path =
        (self->active && self->track.id != 0)
            ? "/org/mpris/MediaPlayer2/Track/t" + std::to_string(self->track.id)
            : "/org/mpris/MediaPlayer2/TrackList/NoTrack";

    int r = sd_bus_message_open_container(reply, 'a', "{sv}");
    if (r < 0) {
      return r;
    }
    if ((r = append_object_path(reply, "mpris:trackid", track_path)) < 0) {
      return r;
    }
    if ((r = append_int64(reply, "mpris:length", self->track.length_us)) < 0) {
      return r;
    }
    if ((r = append_string(reply, "xesam:title", self->track.title)) < 0) {
      return r;
    }
    if ((r = append_string(reply, "xesam:album", self->track.album)) < 0) {
      return r;
    }
    if ((r = append_string_array(reply, "xesam:artist", self->track.artist)) <
        0) {
      return r;
    }
    if ((r = append_string(reply, "mpris:artUrl", self->track.art_url)) < 0) {
      return r;
    }
    return sd_bus_message_close_container(reply);
  }

  static int get_true(sd_bus*, const char*, const char*, const char*,
                      sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "b", 1);
  }

  static int get_false(sd_bus*, const char*, const char*, const char*,
                       sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "b", 0);
  }

  static int get_rate(sd_bus*, const char*, const char*, const char*,
                      sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "d", 1.0);
  }

  static int get_loop_status(sd_bus*, const char*, const char*, const char*,
                             sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", "None");
  }

  static int get_no_strings(sd_bus*, const char*, const char*, const char*,
                            sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "as", 0);
  }


  static int method_noop(sd_bus_message* message, void*, sd_bus_error*) {
    return sd_bus_reply_method_return(message, nullptr);
  }

  static int method_play(sd_bus_message* message, void* userdata,
                         sd_bus_error*) {
    static_cast<ServerImpl*>(userdata)->invoke(&Controls::play);
    return sd_bus_reply_method_return(message, nullptr);
  }

  static int method_pause(sd_bus_message* message, void* userdata,
                          sd_bus_error*) {
    static_cast<ServerImpl*>(userdata)->invoke(&Controls::pause);
    return sd_bus_reply_method_return(message, nullptr);
  }

  static int method_play_pause(sd_bus_message* message, void* userdata,
                               sd_bus_error*) {
    static_cast<ServerImpl*>(userdata)->invoke(&Controls::play_pause);
    return sd_bus_reply_method_return(message, nullptr);
  }

  static int method_next(sd_bus_message* message, void* userdata,
                         sd_bus_error*) {
    static_cast<ServerImpl*>(userdata)->invoke(&Controls::next);
    return sd_bus_reply_method_return(message, nullptr);
  }

  static int method_previous(sd_bus_message* message, void* userdata,
                             sd_bus_error*) {
    static_cast<ServerImpl*>(userdata)->invoke(&Controls::previous);
    return sd_bus_reply_method_return(message, nullptr);
  }

  static int method_stop(sd_bus_message* message, void* userdata,
                         sd_bus_error*) {
    static_cast<ServerImpl*>(userdata)->invoke(&Controls::stop);
    return sd_bus_reply_method_return(message, nullptr);
  }

  static int method_seek(sd_bus_message* message, void* userdata,
                         sd_bus_error*) {
    int64_t offset_us = 0;
    const int r = sd_bus_message_read(message, "x", &offset_us);
    if (r < 0) {
      return r;
    }
    static_cast<ServerImpl*>(userdata)->invoke_seek(
        static_cast<double>(offset_us) / 1e6);
    return sd_bus_reply_method_return(message, nullptr);
  }

  static int method_set_position(sd_bus_message* message, void* userdata,
                                 sd_bus_error*) {
    auto* self = static_cast<ServerImpl*>(userdata);
    const char* track_id = nullptr;
    int64_t target_us = 0;
    const int r = sd_bus_message_read(message, "ox", &track_id, &target_us);
    if (r < 0) {
      return r;
    }

    std::function<long long()> provider;
    {
      std::lock_guard lock(self->mutex);
      provider = self->controls.current_position_us;
    }
    const long long current_us = provider ? provider() : 0;

    self->invoke_seek(static_cast<double>(target_us - current_us) / 1e6);
    return sd_bus_reply_method_return(message, nullptr);
  }

  void run() {
    while (running) {
      const int r = sd_bus_process(bus, nullptr);
      if (r < 0) {
        return;
      }
      if (r > 0) {
        continue;
      }
      sd_bus_wait(bus, 100000);
    }
  }
};

namespace {

const sd_bus_vtable kRootVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Identity", "s", ServerImpl::get_identity, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanQuit", "b", ServerImpl::get_false, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanRaise", "b", ServerImpl::get_false, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("HasTrackList", "b", ServerImpl::get_false, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("DesktopEntry", "s", ServerImpl::get_identity, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedUriSchemes", "as", ServerImpl::get_no_strings, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedMimeTypes", "as", ServerImpl::get_no_strings, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("Raise", nullptr, nullptr, ServerImpl::method_noop,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Quit", nullptr, nullptr, ServerImpl::method_noop,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END};

const sd_bus_vtable kPlayerVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("PlaybackStatus", "s", ServerImpl::get_playback_status, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("LoopStatus", "s", ServerImpl::get_loop_status, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Rate", "d", ServerImpl::get_rate, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Shuffle", "b", ServerImpl::get_false, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Metadata", "a{sv}", ServerImpl::get_metadata, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Volume", "d", ServerImpl::get_rate, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Position", "x", ServerImpl::get_position, 0, 0),
    SD_BUS_PROPERTY("MinimumRate", "d", ServerImpl::get_rate, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("MaximumRate", "d", ServerImpl::get_rate, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanGoNext", "b", ServerImpl::get_true, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanGoPrevious", "b", ServerImpl::get_true, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanPlay", "b", ServerImpl::get_true, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanPause", "b", ServerImpl::get_true, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanSeek", "b", ServerImpl::get_true, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanControl", "b", ServerImpl::get_true, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("Next", nullptr, nullptr, ServerImpl::method_next,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Previous", nullptr, nullptr, ServerImpl::method_previous,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Pause", nullptr, nullptr, ServerImpl::method_pause,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("PlayPause", nullptr, nullptr, ServerImpl::method_play_pause,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Stop", nullptr, nullptr, ServerImpl::method_stop,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Play", nullptr, nullptr, ServerImpl::method_play,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Seek", "x", nullptr, ServerImpl::method_seek,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetPosition", "ox", nullptr, ServerImpl::method_set_position,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("OpenUri", "s", nullptr, ServerImpl::method_noop,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("Seeked", "x", 0),
    SD_BUS_VTABLE_END};

}

Server::Server() : impl_(std::make_unique<ServerImpl>()) {}

Server::~Server() { stop(); }

bool Server::start(std::string* error) {
  if (impl_->bus != nullptr) {
    return true;
  }

  int r = sd_bus_default_user(&impl_->bus);
  if (r < 0) {
    if (error != nullptr) {
      *error = "session bus connection: " + bus_error(r);
    }
    return false;
  }

  const auto close_bus = [this] {
    sd_bus_flush_close_unref(impl_->bus);
    impl_->bus = nullptr;
  };

  r = sd_bus_add_object_vtable(impl_->bus, nullptr, kObjectPath, kRootInterface,
                               kRootVtable, impl_.get());
  if (r < 0) {
    if (error != nullptr) {
      *error = "registering " + std::string(kRootInterface) + " : " +
               bus_error(r);
    }
    close_bus();
    return false;
  }

  r = sd_bus_add_object_vtable(impl_->bus, nullptr, kObjectPath,
                               kPlayerInterface, kPlayerVtable, impl_.get());
  if (r < 0) {
    if (error != nullptr) {
      *error = "registering " + std::string(kPlayerInterface) + " : " +
               bus_error(r);
    }
    close_bus();
    return false;
  }

  r = sd_bus_request_name(impl_->bus, kBusName, 0);
  if (r < 0) {
    if (error != nullptr) {
      *error =
          "name " + std::string(kBusName) + " unavailable: " + bus_error(r);
    }
    close_bus();
    return false;
  }

  impl_->running = true;
  impl_->loop = std::thread([this] { impl_->run(); });
  return true;
}

void Server::stop() {
  if (impl_->bus == nullptr) {
    return;
  }

  impl_->running = false;
  if (impl_->loop.joinable()) {
    impl_->loop.join();
  }

  sd_bus_flush_close_unref(impl_->bus);
  impl_->bus = nullptr;

  std::lock_guard lock(impl_->mutex);
  impl_->controls = Controls{};
}

void Server::set_controls(Controls controls) {
  std::lock_guard lock(impl_->mutex);
  impl_->controls = std::move(controls);
}

void Server::publish(bool active, bool paused, long long position_us,
                     const Track& track) {
  bool status_changed = false;
  bool metadata_changed = false;
  {
    std::lock_guard lock(impl_->mutex);
    status_changed = impl_->active != active || impl_->paused != paused;
    metadata_changed = impl_->track.id != track.id ||
                       impl_->track.title != track.title ||
                       impl_->track.artist != track.artist ||
                       impl_->track.album != track.album ||
                       impl_->track.art_url != track.art_url ||
                       impl_->track.length_us != track.length_us;

    impl_->active = active;
    impl_->paused = paused;
    impl_->position_us = position_us;
    impl_->track = track;
  }

  if (!impl_->running) {
    return;
  }

  if (status_changed) {
    sd_bus_emit_properties_changed(impl_->bus, kObjectPath, kPlayerInterface,
                                   "PlaybackStatus", nullptr);
  }
  if (metadata_changed) {
    sd_bus_emit_properties_changed(impl_->bus, kObjectPath, kPlayerInterface,
                                   "Metadata", nullptr);
  }
}

void Server::notify_seeked(long long position_us) {
  if (!impl_->running) {
    return;
  }
  sd_bus_emit_signal(impl_->bus, kObjectPath, kPlayerInterface, "Seeked", "x",
                     static_cast<int64_t>(position_us));
}

}
