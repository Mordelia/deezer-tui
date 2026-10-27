#pragma once

#include <functional>
#include <memory>
#include <string>

namespace mpris {

struct Track {
  long long id = 0;
  std::string title;
  std::string artist;
  std::string album;
  std::string art_url;
  long long length_us = 0;
};

struct Controls {
  std::function<void()> play;
  std::function<void()> pause;
  std::function<void()> play_pause;
  std::function<void()> next;
  std::function<void()> previous;
  std::function<void()> stop;
  std::function<void(double seconds)> seek_relative;

  std::function<long long()> current_position_us;
};

struct ServerImpl;

class Server {
 public:
  Server();
  ~Server();

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  bool start(std::string* error);

  void stop();

  void set_controls(Controls controls);

  void publish(bool active, bool paused, long long position_us,
               const Track& track);

  void notify_seeked(long long position_us);

 private:
  std::unique_ptr<ServerImpl> impl_;
};

}
