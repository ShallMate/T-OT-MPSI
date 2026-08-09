#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

// One party's half of the correlations for a single N-party GMW peer. The
// preprocessing communication is carried with the prefix so a later online
// evaluator can account for it exactly once.
struct PrecomputedGmwPairPrefix {
  size_t peer = 0;
  bool sender_role = false;
  std::vector<uint128_t> mult;
  std::vector<uint128_t> add;
  size_t preprocess_comm_bytes = 0;
};

} // namespace examples::otmpsi
