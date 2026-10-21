#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

void SetPackedBit(volePSI::u8 *row, size_t bit_idx, bool value);
bool GetPackedBit(const volePSI::u8 *row, size_t bit_idx);
void CheckMaskedCountCompareData(
    const MaskedCountCompareData &data,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint64_t>> &party_arith_shares,
    const OtmpsiParams &params);

} // namespace examples::otmpsi
