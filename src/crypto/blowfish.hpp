#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace crypto {

class StripeDecryptor {
 public:
  static constexpr std::size_t kBlockSize = 2048;

  explicit StripeDecryptor(long long track_id, std::uint64_t first_block = 0);

  std::string update(const char* data, std::size_t size);

  std::string finish();

  static std::array<std::uint8_t, 16> derive_key(long long track_id);

 private:
  std::array<std::uint8_t, 16> key_;
  std::string pending_;
  std::uint64_t block_index_ = 0;
};

}
