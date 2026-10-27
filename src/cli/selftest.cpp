#include "cli/selftest.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <openssl/blowfish.h>

#include "crypto/blowfish.hpp"

namespace cli {
namespace {

struct KeyVector {
  long long track_id;
  const char* expected_hex;
};

constexpr KeyVector kKeyVectors[] = {
    {3135556, "6c6c666b39662c37652575603c643439"},
    {67238732, "386f3332386f726a632a24676d696264"},
    {908604612, "6063366e683f263b337a7766346f6630"},
};

std::string to_hex(const std::array<std::uint8_t, 16>& bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  for (const std::uint8_t byte : bytes) {
    out.push_back(kHex[byte >> 4]);
    out.push_back(kHex[byte & 0x0F]);
  }
  return out;
}

std::string encrypt_like_deezer(long long track_id, const std::string& plain) {
  const auto key = crypto::StripeDecryptor::derive_key(track_id);

  BF_KEY schedule;
  BF_set_key(&schedule, static_cast<int>(key.size()), key.data());

  std::string out;
  std::uint64_t index = 0;
  for (std::size_t offset = 0; offset < plain.size();
       offset += crypto::StripeDecryptor::kBlockSize, ++index) {
    const std::size_t size =
        std::min(crypto::StripeDecryptor::kBlockSize, plain.size() - offset);

    if (index % 3 == 0 && size == crypto::StripeDecryptor::kBlockSize) {
      std::uint8_t iv[8] = {0, 1, 2, 3, 4, 5, 6, 7};
      std::string encrypted(size, '\0');
      BF_cbc_encrypt(
          reinterpret_cast<const std::uint8_t*>(plain.data() + offset),
          reinterpret_cast<std::uint8_t*>(encrypted.data()),
          static_cast<long>(size), &schedule, iv, BF_ENCRYPT);
      out += encrypted;
    } else {
      out.append(plain.data() + offset, size);
    }
  }
  return out;
}

int check_keys() {
  int failures = 0;
  for (const KeyVector& vector : kKeyVectors) {
    const std::string got =
        to_hex(crypto::StripeDecryptor::derive_key(vector.track_id));
    const bool ok = got == vector.expected_hex;
    std::printf("  [%s] key for track %-10lld %s\n", ok ? "ok" : "FAIL",
                vector.track_id, got.c_str());
    if (!ok) {
      std::printf("        expected %s\n", vector.expected_hex);
      ++failures;
    }
  }
  return failures;
}

int check_roundtrip() {
  constexpr long long kTrackId = 3135556;

  const std::string plain = std::string(2048, 'A') + std::string(2048, 'B') +
                            std::string(2048, 'C') + std::string(137, 'D');

  const std::string encrypted = encrypt_like_deezer(kTrackId, plain);

  crypto::StripeDecryptor decryptor(kTrackId);
  std::string got;
  constexpr std::size_t kChunk = 333;
  for (std::size_t offset = 0; offset < encrypted.size(); offset += kChunk) {
    const std::size_t size = std::min(kChunk, encrypted.size() - offset);
    got += decryptor.update(encrypted.data() + offset, size);
  }
  got += decryptor.finish();

  const bool ok = got == plain;
  std::printf("  [%s] round-trip %zu bytes\n", ok ? "ok" : "FAIL",
              plain.size());
  return ok ? 0 : 1;
}

}

int run_self_test() {
  std::printf("crypto self-test\n");
  const int failures = check_keys() + check_roundtrip();
  if (failures == 0) {
    std::printf("all checks passed.\n");
    return 0;
  }
  std::printf("%d check(s) failed.\n", failures);
  return 1;
}

}

#pragma GCC diagnostic pop
