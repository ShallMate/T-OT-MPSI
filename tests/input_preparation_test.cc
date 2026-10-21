#include <algorithm>
#include <array>
#include <iostream>

#include "blake3.h"
#include "otmpsi/demo_data.h"
#include "otmpsi/input_preparation.h"

namespace examples::otmpsi {
namespace {

void CheckHashEncoding() {
  // A fixed byte sequence checks the domain, endian convention, and truncation
  // independently of the integer serialization in HashSsBsmtInputs.
  constexpr unsigned char message[] =
      "T-OT-MPSI/SS-BSMT/H/v1"
      "\x00\x01\x02\x03\x04\x05\x06\x07"
      "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f";
  const uint128_t item =
      (static_cast<uint128_t>(0x0f0e0d0c0b0a0908ULL) << 64) |
      0x0706050403020100ULL;
  std::array<uint8_t, 32> reference;
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, message, sizeof(message) - 1);
  blake3_hasher_finalize(&hasher, reference.data(), reference.size());

  const auto hashed = HashSsBsmtInputs({item, 0, item});
  OTMPSI_ENFORCE(hashed.size() == 3 && hashed[0] == hashed[2],
                 "H must preserve row order and repeated inputs");
  for (size_t byte = 0; byte < sizeof(uint128_t); ++byte) {
    OTMPSI_ENFORCE(static_cast<uint8_t>(hashed[0] >> (8 * byte)) ==
                       reference[byte],
                   "H encoding or truncation mismatch at byte {}", byte);
  }
  OTMPSI_ENFORCE(hashed[0] != item && hashed[0] != hashed[1],
                 "H must not return raw inputs or a constant");
  OTMPSI_ENFORCE(HashSsBsmtInputs({}).empty(), "H empty input mismatch");
}

void CheckPermutationAndOutputComparison() {
  const auto original = BuildLeaderSet(1024);
  auto first = original;
  auto second = original;
  PrivatelyPermuteLeaderInput(first);
  PrivatelyPermuteLeaderInput(second);
  // With fresh 128-bit seeds, accidental equality is negligible.
  OTMPSI_ENFORCE(first != original && first != second,
                 "leader permutation missing or reused across runs");
  auto sorted_original = original;
  std::sort(sorted_original.begin(), sorted_original.end());
  std::sort(first.begin(), first.end());
  std::sort(second.begin(), second.end());
  OTMPSI_ENFORCE(first == sorted_original && second == sorted_original,
                 "leader permutation changed the input set");

  const std::vector<OtmpsiResult> expected = {
      {original[0], 2, {0, 1}}, {original[1], 3, {0, 1, 2}}};
  auto reordered = expected;
  std::reverse(reordered.begin(), reordered.end());
  CheckDemoResults(reordered, expected);
  reordered[0].holders[2] = 3;
  bool rejected = false;
  try {
    CheckDemoResults(reordered, expected);
  } catch (const std::exception &) {
    rejected = true;
  }
  OTMPSI_ENFORCE(rejected, "output comparison ignored an incorrect holder");
}

} // namespace
} // namespace examples::otmpsi

int main() {
  try {
    examples::otmpsi::CheckHashEncoding();
    examples::otmpsi::CheckPermutationAndOutputComparison();
    std::cout << "input preparation checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
