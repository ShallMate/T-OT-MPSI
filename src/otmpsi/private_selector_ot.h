#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

struct PrivateSelectorOtRun {
  std::vector<OtmpsiResult> results;
  RoleStats leader;
  RoleStats clients;
  std::vector<RoleStats> client_stats;
};

struct PrivateSelectorOtPartyRun {
  std::vector<OtmpsiResult> results;
  RoleStats stats;
};

PrivateSelectorOtPartyRun RunPrivateSelectorOtParty(
    size_t party_id, const std::vector<uint64_t> &selection_share,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_membership_shares,
    const std::vector<uint128_t> &party_membership_share,
    const OtmpsiParams &params);

PrivateSelectorOtRun RunPrivateSelectorOt(
    const std::vector<std::vector<uint64_t>> &selection_shares,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_membership_shares,
    const std::vector<std::vector<uint128_t>> &party_membership_shares,
    const OtmpsiParams &params);

} // namespace examples::otmpsi
