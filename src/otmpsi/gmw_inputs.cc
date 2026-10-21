#include "otmpsi/gmw_inputs.h"

namespace examples::otmpsi {
void SetPackedBit(volePSI::u8 *row, size_t bit_idx, bool value) {
  const auto mask = static_cast<volePSI::u8>(1U << (bit_idx & 7U));
  if (value) {
    row[bit_idx >> 3U] |= mask;
  } else {
    row[bit_idx >> 3U] &= static_cast<volePSI::u8>(~mask);
  }
}

bool GetPackedBit(const volePSI::u8 *row, size_t bit_idx) {
  return ((row[bit_idx >> 3U] >> (bit_idx & 7U)) & 1U) != 0;
}

void CheckMaskedCountCompareData(
    const MaskedCountCompareData &data,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint64_t>> &party_arith_shares,
    const OtmpsiParams &params) {
  const size_t compare_bits = CompareBitsForPartyCount(params.parties);
  const uint64_t mask = LowBitsMask(compare_bits);
  OTMPSI_ENFORCE(data.compare_bits == compare_bits &&
                   data.opened_masked_values.size() == params.set_size &&
                   data.mask_bit_shares.size() == params.parties,
               "masked count compare data shape mismatch");

  for (size_t row = 0; row < params.set_size; ++row) {
    uint64_t random_mask = 0;
    for (size_t party = 0; party < params.parties; ++party) {
      OTMPSI_ENFORCE(data.mask_bit_shares[party].size() == params.set_size,
                   "masked count bit-share row count mismatch");
      random_mask ^= data.mask_bit_shares[party][row];
    }
    random_mask &= mask;

    uint64_t count_minus_threshold =
        (uint64_t{1} - static_cast<uint64_t>(params.threshold)) & mask;
    for (size_t party = 1; party < params.parties; ++party) {
      count_minus_threshold =
          (count_minus_threshold + leader_arith_shares[party - 1][row] +
           party_arith_shares[party - 1][row]) &
          mask;
    }

    const uint64_t recovered =
        (data.opened_masked_values[row] + random_mask) & mask;
    OTMPSI_ENFORCE(recovered == count_minus_threshold,
                 "masked count comparison data mismatch at row {}", row);
  }
}

} // namespace examples::otmpsi
