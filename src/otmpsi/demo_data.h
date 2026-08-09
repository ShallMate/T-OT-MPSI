#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

std::vector<uint128_t> BuildLeaderSet(size_t set_size);
std::vector<uint128_t> BuildPartySet(size_t party_idx,
                                     const OtmpsiParams &params,
                                     const std::vector<uint128_t> &x0);
std::vector<OtmpsiResult> ExpectedDemoResults(
    const OtmpsiParams &params, const std::vector<uint128_t> &x0);
void CheckDemoResults(const std::vector<OtmpsiResult> &actual,
                      const std::vector<OtmpsiResult> &expected);

} // namespace examples::otmpsi
