#include "otmpsi/input_preparation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "blake3.h"
#include "cryptoTools/Crypto/PRNG.h"

namespace examples::otmpsi {

std::vector<uint128_t>
HashSsBsmtInputs(const std::vector<uint128_t> &items) {
  constexpr char domain[] = "T-OT-MPSI/SS-BSMT/H/v1";
  constexpr size_t domain_size = sizeof(domain) - 1;
  constexpr size_t item_bytes = sizeof(uint128_t);
  std::array<uint8_t, domain_size + item_bytes> input{};
  std::memcpy(input.data(), domain, domain_size);
  std::vector<uint128_t> hashed(items.size());
  for (size_t i = 0; i < items.size(); ++i) {
    for (size_t byte = 0; byte < item_bytes; ++byte) {
      input[domain_size + byte] =
          static_cast<uint8_t>(items[i] >> (8 * byte));
    }
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, input.data(), input.size());
    std::array<uint8_t, item_bytes> digest;
    blake3_hasher_finalize(&hasher, digest.data(), digest.size());
    uint128_t value = 0;
    for (size_t byte = 0; byte < item_bytes; ++byte) {
      value |= static_cast<uint128_t>(digest[byte]) << (8 * byte);
    }
    hashed[i] = value;
  }
  return hashed;
}

void PrivatelyPermuteLeaderInput(std::vector<uint128_t> &items) {
  osuCrypto::PRNG prng(osuCrypto::sysRandomSeed());
  std::shuffle(items.begin(), items.end(), prng);
}

} // namespace examples::otmpsi
