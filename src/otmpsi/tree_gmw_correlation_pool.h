#pragma once

#include <span>

#include "otmpsi/nparty_gmw_preprocessing.h"
#include "otmpsi/otmpsi_types.h"
#include "otmpsi/z256_bit_mul_ot.h"

namespace examples::otmpsi {

struct OtIndexRange {
  size_t begin = 0;
  size_t count = 0;

  size_t end() const;
};

struct TreeGmwPairCorrelationPool {
  OtIndexRange gmw_range;
  OtIndexRange tree_range;
  PrecomputedGmwPairPrefix gmw_prefix;
  Z256BitMulOtSenderBatch tree_sender;
  Z256BitMulOtReceiverBatch tree_receiver;
};

// Generates one Silent-OT vector and partitions it into a block-triple prefix
// and a byte-OT tail. The prefix and tail are disjoint by construction. The
// receiver's random choices in the tail are corrected to tree_choices using
// the standard random-OT-to-chosen-choice transform.
TreeGmwPairCorrelationPool PrecomputeTreeGmwPairCorrelationPool(
    size_t peer, bool sender_role, size_t gmw_correlations,
    std::span<const uint8_t> tree_choices, size_t num_threads, block seed,
    coproto::Socket &sock);

void CheckTreeGmwCorrelationPool(const TreeGmwPairCorrelationPool &pool,
                                 size_t expected_gmw_correlations,
                                 size_t expected_tree_ots);

} // namespace examples::otmpsi
