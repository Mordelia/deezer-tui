#include "audio/byte_buffer.hpp"

#include <algorithm>
#include <cstring>

namespace audio {
namespace {

constexpr std::size_t kCompactThreshold = 1u << 20;

}

ByteBuffer::ByteBuffer(std::size_t capacity) : capacity_(capacity) {}

std::size_t ByteBuffer::available() const {
  return buffer_.size() - offset_;
}

std::uint64_t ByteBuffer::epoch() const {
  std::lock_guard lock(mutex_);
  return epoch_;
}

bool ByteBuffer::push(const char* data, std::size_t size, std::uint64_t epoch) {
  if (size == 0) {
    return true;
  }

  std::unique_lock lock(mutex_);

  not_full_.wait(lock, [this, size, epoch] {
    return cancelled_ || epoch_ != epoch || available() == 0 ||
           available() + size <= capacity_;
  });

  if (cancelled_ || epoch_ != epoch) {
    return false;
  }

  buffer_.append(data, size);
  not_empty_.notify_all();
  return true;
}

void ByteBuffer::finish(std::uint64_t epoch) {
  std::lock_guard lock(mutex_);
  if (epoch_ != epoch) {
    return;
  }
  finished_ = true;
  not_empty_.notify_all();
}

void ByteBuffer::cancel() {
  std::lock_guard lock(mutex_);
  cancelled_ = true;
  not_empty_.notify_all();
  not_full_.notify_all();
}

void ByteBuffer::reset() {
  std::lock_guard lock(mutex_);
  ++epoch_;
  buffer_.clear();
  offset_ = 0;
  finished_ = false;
  not_empty_.notify_all();
  not_full_.notify_all();
}

std::size_t ByteBuffer::read(char* out, std::size_t size) {
  std::unique_lock lock(mutex_);
  not_empty_.wait(lock, [this] {
    return cancelled_ || available() > 0 || finished_;
  });

  if (cancelled_ || available() == 0) {
    return 0;
  }

  const std::size_t count = std::min(size, available());
  std::memcpy(out, buffer_.data() + offset_, count);
  offset_ += count;

  if (offset_ == buffer_.size()) {
    buffer_.clear();
    offset_ = 0;
  } else if (offset_ >= kCompactThreshold) {
    buffer_.erase(0, offset_);
    offset_ = 0;
  }

  not_full_.notify_all();
  return count;
}

}
