#pragma once

#include <vector>

#include "otmpsi/compat/int128.h"

namespace examples::otmpsi {

// H(x): domain-separated BLAKE3, truncated to the 128-bit PRF/OKVS domain.
// Elements and digests use little-endian byte encoding.
std::vector<uint128_t>
HashSsBsmtInputs(const std::vector<uint128_t> &items);

// Called once by the leader before assigning protocol row indices. The seed
// and the resulting permutation stay local and are fresh for every run.
void PrivatelyPermuteLeaderInput(std::vector<uint128_t> &items);

} // namespace examples::otmpsi
