#include "crypto/blowfish.hpp"

#include <openssl/evp.h>

#include <algorithm>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <openssl/blowfish.h>

namespace crypto {
namespace {

constexpr char kSecret[] = "g4el58wc0zvf9na1";

constexpr std::uint8_t kIv[8] = {0, 1, 2, 3, 4, 5, 6, 7};

std::string md5_hex(const std::string& input) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  EVP_Digest(input.data(), input.size(), digest, &length, EVP_md5(), nullptr);

  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(static_cast<std::size_t>(length) * 2);
  for (unsigned int i = 0; i < length; ++i) {
    out.push_back(kHex[digest[i] >> 4]);
    out.push_back(kHex[digest[i] & 0x0F]);
  }
  return out;
}

void decrypt_block(const std::array<std::uint8_t, 16>& key, const char* in,
                   char* out) {
  BF_KEY schedule;
  BF_set_key(&schedule, static_cast<int>(key.size()), key.data());

  std::uint8_t iv[8];
  std::copy(std::begin(kIv), std::end(kIv), iv);

  BF_cbc_encrypt(reinterpret_cast<const std::uint8_t*>(in),
                 reinterpret_cast<std::uint8_t*>(out),
                 static_cast<long>(StripeDecryptor::kBlockSize), &schedule, iv,
                 BF_DECRYPT);
}

}

std::array<std::uint8_t, 16> StripeDecryptor::derive_key(long long track_id) {
  const std::string hash = md5_hex(std::to_string(track_id));

  std::array<std::uint8_t, 16> key{};
  for (std::size_t i = 0; i < key.size(); ++i) {
    key[i] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(hash[i]) ^
                                       static_cast<std::uint8_t>(hash[i + 16]) ^
                                       static_cast<std::uint8_t>(kSecret[i]));
  }
  return key;
}

StripeDecryptor::StripeDecryptor(long long track_id, std::uint64_t first_block)
    : key_(derive_key(track_id)), block_index_(first_block) {}

std::string StripeDecryptor::update(const char* data, std::size_t size) {
  pending_.append(data, size);

  std::string out;
  while (pending_.size() >= kBlockSize) {
    if (block_index_ % 3 == 0) {
      std::string decrypted(kBlockSize, '\0');
      decrypt_block(key_, pending_.data(), decrypted.data());
      out += decrypted;
    } else {
      out.append(pending_.data(), kBlockSize);
    }
    pending_.erase(0, kBlockSize);
    ++block_index_;
  }
  return out;
}

std::string StripeDecryptor::finish() {
  std::string out;
  out.swap(pending_);
  return out;
}

}

#pragma GCC diagnostic pop
