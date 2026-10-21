#pragma once

#include "otmpsi/gmw_inputs.h"
#include "otmpsi/nparty_gmw_preprocessing.h"
#include "otmpsi/pairwise_zero_phase.h"

namespace examples::otmpsi {

struct NPartySharedOutputGmwOutput {
  std::vector<std::vector<uint64_t>> output_shares;
  RoleStats leader;
  RoleStats clients;
  std::vector<RoleStats> client_stats;
};

struct DistributedSharedOutput {
  std::vector<uint64_t> output_share;
  RoleStats stats;
};

NPartySharedOutputGmwOutput RunNPartySharedOutputGmwWithPreprocessing(
    const std::vector<volePSI::Matrix<volePSI::u8>> &party_inputs,
    std::vector<std::vector<PrecomputedGmwPairPrefix>> precomputed_prefixes,
    const OtmpsiParams &params, const std::string &scope,
    volePSI::BetaCircuit cir, size_t output_bits);
DistributedSharedOutput
RunNPartySharedOutputGmwParty(const volePSI::Matrix<volePSI::u8> &party_input,
                              size_t party_id, const OtmpsiParams &params,
                              const std::string &scope,
                              volePSI::BetaCircuit cir, size_t output_bits,
                              std::vector<PrecomputedGmwPairPrefix>
                                  precomputed_prefixes = {});
} // namespace examples::otmpsi
