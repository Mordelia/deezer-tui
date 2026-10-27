#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace audio {

class ByteBuffer {
 public:
  static constexpr std::size_t kDefaultCapacity = 4u << 20;

  explicit ByteBuffer(std::size_t capacity = kDefaultCapacity);
  ~ByteBuffer() = default;

  ByteBuffer(const ByteBuffer&) = delete;
  ByteBuffer& operator=(const ByteBuffer&) = delete;

  [[nodiscard]] std::uint64_t epoch() const;

  bool push(const char* data, std::size_t size, std::uint64_t epoch);

  void finish(std::uint64_t epoch);

  void cancel();

  void reset();

  std::size_t read(char* out, std::size_t size);

 private:
  [[nodiscard]] std::size_t available() const;

  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;

  std::string buffer_;
  std::size_t offset_ = 0;
  const std::size_t capacity_;
  std::uint64_t epoch_ = 0;
  bool finished_ = false;
  bool cancelled_ = false;
};

}
