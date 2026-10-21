#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

struct OpenMaskedValuesOutput {
  std::vector<uint64_t> opened;
  RoleStats stats;
};

OpenMaskedValuesOutput
OpenAdditiveMaskedValues(size_t party_id,
                         const std::vector<uint64_t> &masked_share,
                         const OtmpsiParams &params);
std::vector<uint64_t> LeaderCountMinusThresholdShares(
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const OtmpsiParams &params);
std::vector<uint64_t> MaskCountShare(const std::vector<uint64_t> &count_share,
                                     const std::vector<uint64_t> &random_share,
                                     const OtmpsiParams &params);
MaskedCountCompareData MakeMaskedCountCompareData(
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint64_t>> &party_arith_shares,
    const std::vector<MaskPrepShare> &mask_prep,
    const OtmpsiParams &params);
} // namespace examples::otmpsi
