#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

struct RelaxedSelectorOpenPartyRun {
  std::vector<OtmpsiResult> results;
  RoleStats stats;
  size_t output_size = 0;
};

struct RelaxedSelectorOpenRun {
  std::vector<OtmpsiResult> results;
  RoleStats leader;
  RoleStats clients;
  std::vector<RoleStats> client_stats;
};

// Opens the threshold-selection bit to every party, then reveals membership
// shares only for selected rows to the leader.
RelaxedSelectorOpenPartyRun RunRelaxedSelectorOpenParty(
    size_t party_id, const std::vector<uint64_t> &selection_share,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_membership_shares,
    const std::vector<uint128_t> &party_membership_share,
    const OtmpsiParams &params);

RelaxedSelectorOpenRun RunRelaxedSelectorOpen(
    const std::vector<std::vector<uint64_t>> &selection_shares,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_membership_shares,
    const std::vector<std::vector<uint128_t>> &party_membership_shares,
    const OtmpsiParams &params);

} // namespace examples::otmpsi
