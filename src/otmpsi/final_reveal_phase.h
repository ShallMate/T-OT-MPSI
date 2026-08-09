#pragma once

#include "otmpsi/nparty_gmw.h"

namespace examples::otmpsi {

struct OpenMaskedValuesOutput {
  std::vector<uint64_t> opened;
  RoleStats stats;
};

struct FinalRevealOutput {
  std::vector<OtmpsiResult> results;
  RoleStats stats;
};

struct RelaxedOpenOutput {
  std::vector<OtmpsiResult> results;
  RoleStats leader;
  RoleStats clients;
  std::vector<RoleStats> client_stats;
};

struct ThresholdTwoLeaderOutput {
  std::vector<OtmpsiResult> results;
  RoleStats leader;
  RoleStats clients;
  std::vector<RoleStats> client_stats;
};

struct DistributedRelaxedOpenOutput {
  std::vector<OtmpsiResult> results;
  RoleStats stats;
};

size_t ArithmeticFinalRevealBytesPerClient(const OtmpsiParams &params);
size_t AdditiveMaskedOpenPayloadBytesPerClient(const OtmpsiParams &params);
void AddArithmeticFinalRevealStats(NPartyCompareGmwOutput &run,
                                   const OtmpsiParams &params);
std::vector<OtmpsiResult> DecodeArithmeticMaskedOutput(
    const std::vector<uint8_t> &over_threshold,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint64_t>> &party_arith_shares,
    const OtmpsiParams &params);
volePSI::Matrix<volePSI::u8> MakeOriginalMaskedCompareInput(
    size_t party_id, const std::vector<uint64_t> &opened_masked_values,
    const std::vector<uint64_t> &mask_bit_share, const OtmpsiParams &params);
volePSI::Matrix<volePSI::u8> MakeOriginalMaskedOutputLeaderInput(
    const std::vector<uint64_t> &opened_masked_values,
    const std::vector<uint64_t> &mask_bit_share,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const OtmpsiParams &params);
volePSI::Matrix<volePSI::u8> MakeOriginalMaskedOutputPartyInput(
    size_t party_id, const std::vector<uint64_t> &mask_bit_share,
    const std::vector<uint128_t> &party_bit_share, const OtmpsiParams &params);
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
std::vector<size_t> SelectedRows(const std::vector<uint8_t> &bits);
FinalRevealOutput ArithmeticFinalRevealParty(
    size_t party_id, const std::vector<uint8_t> &over_threshold,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<uint64_t> &party_arith_share, const OtmpsiParams &params);
RelaxedOpenOutput RunRelaxedOpenOutput(
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint64_t>> &party_arith_shares,
    const OtmpsiParams &params);
DistributedRelaxedOpenOutput RunRelaxedOpenOutputParty(
    size_t party_id, const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<uint64_t> &party_arith_share, const OtmpsiParams &params);
DistributedRelaxedOpenOutput RunRelaxedBooleanGmwOutputParty(
    size_t party_id, const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const std::vector<uint128_t> &party_bit_share, const OtmpsiParams &params);
ThresholdTwoLeaderOutput RunThresholdTwoLeaderOutput(
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const std::vector<std::vector<uint128_t>> &party_bit_shares,
    const OtmpsiParams &params);
FinalRevealOutput RunThresholdTwoLeaderOutputParty(
    size_t party_id, const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const std::vector<uint128_t> &party_bit_share, const OtmpsiParams &params);
RelaxedOpenOutput RunThresholdTwoRelaxedOpenOutput(
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const std::vector<std::vector<uint128_t>> &party_bit_shares,
    const OtmpsiParams &params);
DistributedRelaxedOpenOutput RunThresholdTwoRelaxedOpenOutputParty(
    size_t party_id, const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const std::vector<uint128_t> &party_bit_share, const OtmpsiParams &params);

} // namespace examples::otmpsi
