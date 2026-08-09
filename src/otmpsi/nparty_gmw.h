#pragma once

#include "otmpsi/gmw_circuits.h"
#include "otmpsi/gmw_inputs.h"
#include "otmpsi/nparty_gmw_preprocessing.h"
#include "otmpsi/pairwise_zero_phase.h"

namespace examples::otmpsi {

struct NPartyPaperGmwOutput {
  std::vector<OtmpsiResult> results;
  RoleStats leader;
  RoleStats clients;
  std::vector<RoleStats> client_stats;
};

struct NPartyCompareGmwOutput {
  std::vector<uint8_t> over_threshold;
  RoleStats leader;
  RoleStats clients;
  std::vector<RoleStats> client_stats;
};

struct NPartySharedOutputGmwOutput {
  std::vector<std::vector<uint64_t>> output_shares;
  RoleStats leader;
  RoleStats clients;
  std::vector<RoleStats> client_stats;
};

enum class NPartyGmwCircuitKind {
  kMembershipCount,
  kMembershipMaskedHolder,
  kMaskedArithmeticCount,
};

struct DistributedCompareOutput {
  std::vector<uint8_t> over_threshold;
  RoleStats stats;
};

struct DistributedSharedOutput {
  std::vector<uint64_t> output_share;
  RoleStats stats;
};

struct DistributedPaperGmwOutput {
  std::vector<OtmpsiResult> results;
  RoleStats stats;
};

NPartyPaperGmwOutput RunNPartyPaperGmw(
    const std::vector<volePSI::Matrix<volePSI::u8>> &party_inputs,
    const std::vector<uint128_t> &x0, const OtmpsiParams &params,
    const std::string &scope,
    NPartyGmwCircuitKind circuit_kind = NPartyGmwCircuitKind::kMembershipCount,
    const std::vector<size_t> &extra_party_comm_bytes = {});
NPartyCompareGmwOutput RunNPartyCompareGmw(
    const std::vector<volePSI::Matrix<volePSI::u8>> &party_inputs,
    const OtmpsiParams &params, const std::string &scope,
    const std::vector<size_t> &extra_party_comm_bytes = {});
NPartySharedOutputGmwOutput RunNPartySharedOutputGmw(
    const std::vector<volePSI::Matrix<volePSI::u8>> &party_inputs,
    const OtmpsiParams &params, const std::string &scope,
    volePSI::BetaCircuit cir, size_t output_bits);
NPartySharedOutputGmwOutput RunNPartySharedOutputGmwWithPreprocessing(
    const std::vector<volePSI::Matrix<volePSI::u8>> &party_inputs,
    std::vector<std::vector<PrecomputedGmwPairPrefix>> precomputed_prefixes,
    const OtmpsiParams &params, const std::string &scope,
    volePSI::BetaCircuit cir, size_t output_bits);
DistributedCompareOutput
RunNPartyCompareCircuitGmwParty(const volePSI::Matrix<volePSI::u8> &party_input,
                                size_t party_id, const OtmpsiParams &params,
                                const std::string &scope,
                                volePSI::BetaCircuit cir);
DistributedSharedOutput
RunNPartySharedOutputGmwParty(const volePSI::Matrix<volePSI::u8> &party_input,
                              size_t party_id, const OtmpsiParams &params,
                              const std::string &scope,
                              volePSI::BetaCircuit cir, size_t output_bits,
                              std::vector<PrecomputedGmwPairPrefix>
                                  precomputed_prefixes = {});
DistributedCompareOutput
RunNPartyMaskedCompareGmwParty(const volePSI::Matrix<volePSI::u8> &party_input,
                               size_t party_id, const OtmpsiParams &params,
                               const std::string &scope);
DistributedCompareOutput RunNPartyMembershipCompareGmwParty(
    const volePSI::Matrix<volePSI::u8> &party_input, size_t party_id,
    const OtmpsiParams &params, const std::string &scope);
DistributedCompareOutput RunNPartyAdditiveCompareGmwParty(
    const volePSI::Matrix<volePSI::u8> &party_input, size_t party_id,
    const OtmpsiParams &params, const std::string &scope);
DistributedPaperGmwOutput
RunNPartyMaskedOutputGmwParty(const volePSI::Matrix<volePSI::u8> &party_input,
                              size_t party_id, const std::vector<uint128_t> &x0,
                              const OtmpsiParams &params,
                              const std::string &scope);
DistributedPaperGmwOutput RunNPartyMembershipOutputGmwParty(
    const volePSI::Matrix<volePSI::u8> &party_input, size_t party_id,
    const std::vector<uint128_t> &x0, const OtmpsiParams &params,
    const std::string &scope);

} // namespace examples::otmpsi
