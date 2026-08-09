#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

void SetPackedBit(volePSI::u8 *row, size_t bit_idx, bool value);
bool GetPackedBit(const volePSI::u8 *row, size_t bit_idx);
void CopyU128ToPacked(volePSI::u8 *row, size_t bit_idx, uint128_t v);

volePSI::Matrix<volePSI::u8> MakePaperGmwEmptyInput(
    const OtmpsiParams &params);
void WritePaperGmwShare(volePSI::Matrix<volePSI::u8> &input,
                        const std::vector<uint128_t> &shares,
                        size_t party_idx, const OtmpsiParams &params);
volePSI::Matrix<volePSI::u8> MakePaperGmwLeaderInput(
    const std::vector<std::vector<uint128_t>> &leader_shares,
    const OtmpsiParams &params);
volePSI::Matrix<volePSI::u8> MakePaperGmwPartyInput(
    size_t party_idx, const std::vector<uint128_t> &party_share,
    const OtmpsiParams &params);

void WriteMembershipShares(volePSI::Matrix<volePSI::u8> &input,
                           const std::vector<uint128_t> &bit_share,
                           size_t party_idx, const OtmpsiParams &params);
volePSI::Matrix<volePSI::u8> MakeMembershipGmwEmptyInput(
    const OtmpsiParams &params);
volePSI::Matrix<volePSI::u8> MakeMembershipGmwLeaderInput(
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const OtmpsiParams &params);
volePSI::Matrix<volePSI::u8> MakeMembershipGmwPartyInput(
    size_t party_idx, const std::vector<uint128_t> &party_bit_share,
    const OtmpsiParams &params);

void WriteArithmeticCountShareBits(volePSI::Matrix<volePSI::u8> &input,
                                   size_t party_idx, uint64_t share,
                                   uint8_t *row,
                                   const OtmpsiParams &params);
volePSI::Matrix<volePSI::u8> MakeArithmeticCountGmwEmptyInput(
    const OtmpsiParams &params);
volePSI::Matrix<volePSI::u8> MakeArithmeticCountGmwLeaderInput(
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const OtmpsiParams &params);
volePSI::Matrix<volePSI::u8> MakeArithmeticCountGmwPartyInput(
    size_t party_idx, const std::vector<uint64_t> &party_arith_share,
    const std::vector<uint128_t> &party_bit_share,
    const OtmpsiParams &params);

void CheckMaskedCountCompareData(
    const MaskedCountCompareData &data,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint64_t>> &party_arith_shares,
    const OtmpsiParams &params);
volePSI::Matrix<volePSI::u8> MakeMaskedCountGmwEmptyInput(
    const OtmpsiParams &params);
void WriteMaskedCountBits(volePSI::u8 *row, size_t offset,
                          uint64_t value, size_t bit_count);
volePSI::Matrix<volePSI::u8> MakeMaskedCountGmwLeaderInput(
    const MaskedCountCompareData &data,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const OtmpsiParams &params);
volePSI::Matrix<volePSI::u8> MakeMaskedCountGmwPartyInput(
    size_t party_idx, const MaskedCountCompareData &data,
    const std::vector<uint128_t> &party_bit_share,
    const OtmpsiParams &params);

volePSI::Matrix<volePSI::u8> MakeAdditiveMaskedCompareInput(
    size_t party_idx, const std::vector<uint64_t> &opened_masked_values,
    const std::vector<uint64_t> &random_shares, const OtmpsiParams &params);
std::vector<OtmpsiResult> DecodePaperGmwOutput(
    const volePSI::Matrix<volePSI::u8> &out,
    const std::vector<uint128_t> &x0, const OtmpsiParams &params);
std::vector<OtmpsiResult> DecodeMaskedHolderGmwOutput(
    const volePSI::Matrix<volePSI::u8> &out,
    const std::vector<uint128_t> &x0, const OtmpsiParams &params);

} // namespace examples::otmpsi
