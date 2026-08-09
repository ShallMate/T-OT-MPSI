#pragma once

#include "otmpsi/nparty_gmw.h"

namespace examples::otmpsi {

// The fixed-width masked comparison opens T = D - R mod 2^8 and keeps R
// Boolean shared. Since T is public, each party can locally derive XOR shares
// of the propagate and generate bits used by the carry circuit.
struct PublicTCompareLayout {
  size_t propagate_offset = 0;
  size_t generate_offset = kComparisonBits;
  size_t one_offset = 2 * kComparisonBits - 1;
  size_t input_bits = one_offset + 1;
};

inline constexpr size_t kPublicTCompareAndGates = kComparisonBits - 2;

PublicTCompareLayout MakePublicTCompareLayout();

volePSI::BetaCircuit BuildPublicTMaskedCompareCircuit();

volePSI::Matrix<volePSI::u8> MakePublicTMaskedCompareInput(
    size_t party_id, const std::vector<uint64_t> &opened_values,
    const std::vector<uint64_t> &mask_bit_share, const OtmpsiParams &params);

DistributedSharedOutput RunPublicTMaskedCompareGmwParty(
    size_t party_id, const std::vector<uint64_t> &opened_values,
    const std::vector<uint64_t> &mask_bit_share, const OtmpsiParams &params,
    const std::string &scope);

} // namespace examples::otmpsi
