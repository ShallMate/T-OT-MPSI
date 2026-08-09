#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

struct B2aShareWithStats {
  std::vector<uint64_t> share;
  RoleStats stats;
};

B2aShareWithStats B2aLeaderSide(const std::vector<uint128_t> &bit_share,
                                size_t party_idx, const OtmpsiParams &params,
                                const std::string &tag);
B2aShareWithStats B2aPartySide(const std::vector<uint128_t> &bit_share,
                               size_t party_idx, const OtmpsiParams &params,
                               const std::string &tag);
void CheckB2aMembershipShares(
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const std::vector<std::vector<uint128_t>> &party_bit_shares,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint64_t>> &party_arith_shares,
    const OtmpsiParams &params);

}
