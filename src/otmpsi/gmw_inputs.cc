#include "otmpsi/gmw_inputs.h"

#include "otmpsi/gmw_circuits.h"

namespace examples::otmpsi {
namespace {

uint64_t BitMaskForWidth(size_t bit_count) {
  return bit_count >= 64 ? ~uint64_t{0} : ((uint64_t{1} << bit_count) - 1);
}

} // namespace

void SetPackedBit(volePSI::u8 *row, size_t bit_idx, bool value) {
  const auto mask = static_cast<volePSI::u8>(1U << (bit_idx & 7U));
  if (value) {
    row[bit_idx >> 3U] |= mask;
  } else {
    row[bit_idx >> 3U] &= static_cast<volePSI::u8>(~mask);
  }
}

bool GetPackedBit(const volePSI::u8 *row, size_t bit_idx) {
  return ((row[bit_idx >> 3U] >> (bit_idx & 7U)) & 1U) != 0;
}

void CopyU128ToPacked(volePSI::u8 *row, size_t bit_idx, uint128_t v) {
  OTMPSI_ENFORCE(bit_idx % 8 == 0, "uint128 bit offset must be byte aligned");
  std::memcpy(row + bit_idx / 8, &v, sizeof(v));
}

volePSI::Matrix<volePSI::u8> MakePaperGmwEmptyInput(
    const OtmpsiParams &params) {
  const auto layout = MakePaperGmwLayout(params.parties);
  const size_t rows = params.set_size;
  const size_t bytes = (layout.input_bits + 7) / 8;

  volePSI::Matrix<volePSI::u8> input;
  input.resize(rows, bytes);
  if (input.size() != 0) {
    std::memset(input.data(), 0, input.size() * sizeof(volePSI::u8));
  }
  return input;
}

void WritePaperGmwShare(volePSI::Matrix<volePSI::u8> &input,
                               const std::vector<uint128_t> &share,
                               size_t party_idx,
                               const OtmpsiParams &params) {
  const auto layout = MakePaperGmwLayout(params.parties);
  OTMPSI_ENFORCE(party_idx > 0 && party_idx < params.parties,
               "client party index out of range");
  OTMPSI_ENFORCE(share.size() == params.set_size,
               "share vector has wrong row count");
  const size_t offset = (party_idx - 1) * layout.rho_bits;
  otmpsi_compat::ParallelFor(0, params.set_size, [&](int64_t begin, int64_t end) {
    for (int64_t r = begin; r < end; ++r) {
      auto *row = input[static_cast<size_t>(r)].data();
      CopyU128ToPacked(row, offset, share[static_cast<size_t>(r)]);
    }
  });
}

volePSI::Matrix<volePSI::u8> MakePaperGmwLeaderInput(
    const std::vector<std::vector<uint128_t>> &leader_shares,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(leader_shares.size() == params.parties - 1,
               "expected one leader share vector for every non-leader party");
  auto input = MakePaperGmwEmptyInput(params);
  const auto layout = MakePaperGmwLayout(params.parties);
  for (size_t party = 1; party < params.parties; ++party) {
    WritePaperGmwShare(input, leader_shares[party - 1], party, params);
  }
  otmpsi_compat::ParallelFor(0, params.set_size, [&](int64_t begin, int64_t end) {
    for (int64_t r = begin; r < end; ++r) {
      auto *row = input[static_cast<size_t>(r)].data();
      SetPackedBit(row, layout.one_offset, true);
      for (size_t b = 0; b < layout.count_bits; ++b) {
        SetPackedBit(row, layout.threshold_offset + b,
                     ((params.threshold >> b) & 1U) != 0);
      }
    }
  });

  return input;
}

volePSI::Matrix<volePSI::u8> MakePaperGmwPartyInput(
    size_t party_idx, const std::vector<uint128_t> &party_share,
    const OtmpsiParams &params) {
  auto input = MakePaperGmwEmptyInput(params);
  WritePaperGmwShare(input, party_share, party_idx, params);
  return input;
}

void WriteMembershipShares(volePSI::Matrix<volePSI::u8> &input,
                                  const std::vector<uint128_t> &bit_shares,
                                  size_t party_idx,
                                  const OtmpsiParams &params) {
  const auto layout = MakeMembershipGmwLayout(params.parties);
  OTMPSI_ENFORCE(party_idx > 0 && party_idx < params.parties,
               "membership party index out of range");
  OTMPSI_ENFORCE(bit_shares.size() == params.set_size,
               "membership share row count mismatch");
  const size_t bit_offset = layout.membership_offset + party_idx - 1;
  otmpsi_compat::ParallelFor(0, params.set_size, [&](int64_t begin, int64_t end) {
    for (int64_t r = begin; r < end; ++r) {
      SetPackedBit(input[static_cast<size_t>(r)].data(), bit_offset,
                   (bit_shares[static_cast<size_t>(r)] & 1U) != 0);
    }
  });
}

volePSI::Matrix<volePSI::u8> MakeMembershipGmwEmptyInput(
    const OtmpsiParams &params) {
  const auto layout = MakeMembershipGmwLayout(params.parties);
  volePSI::Matrix<volePSI::u8> input;
  input.resize(params.set_size, (layout.input_bits + 7) / 8);
  if (input.size() != 0) {
    std::memset(input.data(), 0, input.size() * sizeof(volePSI::u8));
  }
  return input;
}

volePSI::Matrix<volePSI::u8> MakeMembershipGmwLeaderInput(
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(leader_bit_shares.size() == params.parties - 1,
               "expected one leader membership share per non-leader party");
  auto input = MakeMembershipGmwEmptyInput(params);
  const auto layout = MakeMembershipGmwLayout(params.parties);
  for (size_t party = 1; party < params.parties; ++party) {
    WriteMembershipShares(input, leader_bit_shares[party - 1], party, params);
  }
  otmpsi_compat::ParallelFor(0, params.set_size, [&](int64_t begin, int64_t end) {
    for (int64_t r = begin; r < end; ++r) {
      auto *row = input[static_cast<size_t>(r)].data();
      SetPackedBit(row, layout.one_offset, true);
      for (size_t b = 0; b < layout.count_bits; ++b) {
        SetPackedBit(row, layout.threshold_offset + b,
                     ((params.threshold >> b) & 1U) != 0);
      }
    }
  });
  return input;
}

volePSI::Matrix<volePSI::u8> MakeMembershipGmwPartyInput(
    size_t party_idx, const std::vector<uint128_t> &party_bit_share,
    const OtmpsiParams &params) {
  auto input = MakeMembershipGmwEmptyInput(params);
  WriteMembershipShares(input, party_bit_share, party_idx, params);
  return input;
}

void WriteArithmeticCountShareBits(volePSI::Matrix<volePSI::u8> &input,
                                          size_t party_idx,
                                          uint64_t share,
                                          uint8_t *row,
                                          const OtmpsiParams &params) {
  const auto layout = MakeArithmeticCountGmwLayout(params.parties);
  OTMPSI_ENFORCE(party_idx < params.parties,
               "arithmetic count party index out of range");
  const uint64_t mask = (uint64_t{1} << layout.count_bits) - 1;
  share &= mask;
  (void)input;
  WriteMaskedCountBits(row,
                       layout.count_share_offset +
                           party_idx * layout.count_bits,
                       share, layout.count_bits);
}

volePSI::Matrix<volePSI::u8> MakeArithmeticCountGmwEmptyInput(
    const OtmpsiParams &params) {
  const auto layout = MakeArithmeticCountGmwLayout(params.parties);
  volePSI::Matrix<volePSI::u8> input;
  input.resize(params.set_size, (layout.input_bits + 7) / 8);
  if (input.size() != 0) {
    std::memset(input.data(), 0, input.size() * sizeof(volePSI::u8));
  }
  return input;
}

volePSI::Matrix<volePSI::u8> MakeArithmeticCountGmwLeaderInput(
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(leader_arith_shares.size() == params.parties - 1,
               "expected one leader arithmetic share per non-leader party");
  OTMPSI_ENFORCE(leader_bit_shares.size() == params.parties - 1,
               "expected one leader membership share per non-leader party");
  auto input = MakeArithmeticCountGmwEmptyInput(params);
  const auto layout = MakeArithmeticCountGmwLayout(params.parties);
  for (size_t party = 1; party < params.parties; ++party) {
    OTMPSI_ENFORCE(leader_arith_shares[party - 1].size() == params.set_size,
                 "leader arithmetic share row count mismatch");
    OTMPSI_ENFORCE(leader_bit_shares[party - 1].size() == params.set_size,
                 "leader membership share row count mismatch");
  }
  otmpsi_compat::ParallelFor(0, params.set_size, [&](int64_t begin, int64_t end) {
    for (int64_t r = begin; r < end; ++r) {
      const size_t row_idx = static_cast<size_t>(r);
      auto *row = input[row_idx].data();
      uint64_t count_share = 1;
      for (size_t party = 1; party < params.parties; ++party) {
        count_share += leader_arith_shares[party - 1][row_idx];
        SetPackedBit(row, layout.membership_offset + party - 1,
                     (leader_bit_shares[party - 1][row_idx] & 1U) != 0);
      }
      WriteArithmeticCountShareBits(input, 0, count_share, row, params);
      SetPackedBit(row, layout.one_offset, true);
      for (size_t b = 0; b < layout.count_bits; ++b) {
        SetPackedBit(row, layout.threshold_offset + b,
                     ((params.threshold >> b) & 1U) != 0);
      }
    }
  });
  return input;
}

volePSI::Matrix<volePSI::u8> MakeArithmeticCountGmwPartyInput(
    size_t party_idx, const std::vector<uint64_t> &party_arith_share,
    const std::vector<uint128_t> &party_bit_share,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(party_idx > 0 && party_idx < params.parties,
               "arithmetic count party index out of range");
  OTMPSI_ENFORCE(party_arith_share.size() == params.set_size &&
                   party_bit_share.size() == params.set_size,
               "party arithmetic-count row count mismatch");
  auto input = MakeArithmeticCountGmwEmptyInput(params);
  const auto layout = MakeArithmeticCountGmwLayout(params.parties);
  otmpsi_compat::ParallelFor(0, params.set_size, [&](int64_t begin, int64_t end) {
    for (int64_t r = begin; r < end; ++r) {
      const size_t row_idx = static_cast<size_t>(r);
      auto *row = input[row_idx].data();
      WriteArithmeticCountShareBits(input, party_idx,
                                    party_arith_share[row_idx], row, params);
      SetPackedBit(row, layout.membership_offset + party_idx - 1,
                   (party_bit_share[row_idx] & 1U) != 0);
    }
  });
  return input;
}

void CheckMaskedCountCompareData(
    const MaskedCountCompareData &data,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint64_t>> &party_arith_shares,
    const OtmpsiParams &params) {
  const auto layout = MakeMaskedCountGmwLayout(params.parties);
  const uint64_t mask = LowBitsMask(layout.compare_bits);
  OTMPSI_ENFORCE(data.compare_bits == layout.compare_bits &&
                   data.opened_masked_values.size() == params.set_size &&
                   data.mask_bit_shares.size() == params.parties,
               "masked count compare data shape mismatch");

  for (size_t row = 0; row < params.set_size; ++row) {
    uint64_t random_mask = 0;
    for (size_t party = 0; party < params.parties; ++party) {
      OTMPSI_ENFORCE(data.mask_bit_shares[party].size() == params.set_size,
                   "masked count bit-share row count mismatch");
      random_mask ^= data.mask_bit_shares[party][row];
    }
    random_mask &= mask;

    uint64_t count_minus_threshold =
        (uint64_t{1} - static_cast<uint64_t>(params.threshold)) & mask;
    for (size_t party = 1; party < params.parties; ++party) {
      count_minus_threshold =
          (count_minus_threshold + leader_arith_shares[party - 1][row] +
           party_arith_shares[party - 1][row]) &
          mask;
    }

    const uint64_t recovered =
        (data.opened_masked_values[row] + random_mask) & mask;
    OTMPSI_ENFORCE(recovered == count_minus_threshold,
                 "masked count comparison data mismatch at row {}", row);
  }
}

volePSI::Matrix<volePSI::u8> MakeMaskedCountGmwEmptyInput(
    const OtmpsiParams &params) {
  const auto layout = MakeMaskedCountGmwLayout(params.parties);
  volePSI::Matrix<volePSI::u8> input;
  input.resize(params.set_size, (layout.input_bits + 7) / 8);
  if (input.size() != 0) {
    std::memset(input.data(), 0, input.size() * sizeof(volePSI::u8));
  }
  return input;
}

void WriteMaskedCountBits(volePSI::u8 *row, size_t offset,
                                 uint64_t value, size_t bit_count) {
  if (bit_count == 0) {
    return;
  }
  const size_t bit_shift = offset & 7U;
  if (bit_count < 64 && bit_shift + bit_count <= 64) {
    const uint64_t value_mask = BitMaskForWidth(bit_count);
    const uint64_t field_mask = value_mask << bit_shift;
    const uint64_t field_value = (value & value_mask) << bit_shift;
    const size_t byte_offset = offset >> 3U;
    const size_t byte_count = (bit_shift + bit_count + 7U) >> 3U;
    for (size_t byte = 0; byte < byte_count; ++byte) {
      const auto mask_byte =
          static_cast<volePSI::u8>((field_mask >> (8U * byte)) & 0xffU);
      const auto value_byte =
          static_cast<volePSI::u8>((field_value >> (8U * byte)) & 0xffU);
      auto &dst = row[byte_offset + byte];
      dst = static_cast<volePSI::u8>(
          (dst & static_cast<volePSI::u8>(~mask_byte)) |
          (value_byte & mask_byte));
    }
    return;
  }

  for (size_t bit = 0; bit < bit_count; ++bit) {
    SetPackedBit(row, offset + bit, ((value >> bit) & 1U) != 0);
  }
}

volePSI::Matrix<volePSI::u8> MakeMaskedCountGmwLeaderInput(
    const MaskedCountCompareData &data,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(leader_bit_shares.size() == params.parties - 1,
               "expected one leader membership share per non-leader party");
  const auto layout = MakeMaskedCountGmwLayout(params.parties);
  OTMPSI_ENFORCE(data.compare_bits == layout.compare_bits &&
                   data.opened_masked_values.size() == params.set_size &&
                   data.mask_bit_shares.size() == params.parties,
               "masked count leader input data mismatch");

  auto input = MakeMaskedCountGmwEmptyInput(params);
  for (size_t party = 1; party < params.parties; ++party) {
    OTMPSI_ENFORCE(leader_bit_shares[party - 1].size() == params.set_size,
                 "leader masked-count membership row count mismatch");
  }

  otmpsi_compat::ParallelFor(0, params.set_size, [&](int64_t begin, int64_t end) {
    for (int64_t r = begin; r < end; ++r) {
      const size_t row_idx = static_cast<size_t>(r);
      auto *row = input[row_idx].data();
      WriteMaskedCountBits(row, layout.mask_bit_offset,
                           data.mask_bit_shares[0][row_idx],
                           layout.compare_bits);
      WriteMaskedCountBits(row, layout.opened_mask_offset,
                           data.opened_masked_values[row_idx],
                           layout.compare_bits);
      for (size_t party = 1; party < params.parties; ++party) {
        SetPackedBit(row, layout.membership_offset + party - 1,
                     (leader_bit_shares[party - 1][row_idx] & 1U) != 0);
      }
      SetPackedBit(row, layout.one_offset, true);
      WriteMaskedCountBits(row, layout.threshold_offset, params.threshold,
                           layout.compare_bits);
    }
  });
  return input;
}

volePSI::Matrix<volePSI::u8> MakeMaskedCountGmwPartyInput(
    size_t party_idx, const MaskedCountCompareData &data,
    const std::vector<uint128_t> &party_bit_share,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(party_idx > 0 && party_idx < params.parties,
               "masked count party index out of range");
  OTMPSI_ENFORCE(party_bit_share.size() == params.set_size,
               "party masked-count membership row count mismatch");
  const auto layout = MakeMaskedCountGmwLayout(params.parties);
  OTMPSI_ENFORCE(data.compare_bits == layout.compare_bits &&
                   data.mask_bit_shares.size() == params.parties,
               "masked count party input data mismatch");

  auto input = MakeMaskedCountGmwEmptyInput(params);
  otmpsi_compat::ParallelFor(0, params.set_size, [&](int64_t begin, int64_t end) {
    for (int64_t r = begin; r < end; ++r) {
      const size_t row_idx = static_cast<size_t>(r);
      auto *row = input[row_idx].data();
      WriteMaskedCountBits(row, layout.mask_bit_offset,
                           data.mask_bit_shares[party_idx][row_idx],
                           layout.compare_bits);
      SetPackedBit(row, layout.membership_offset + party_idx - 1,
                   (party_bit_share[row_idx] & 1U) != 0);
    }
  });
  return input;
}

volePSI::Matrix<volePSI::u8> MakeAdditiveMaskedCompareInput(
    size_t party_idx, const std::vector<uint64_t> &opened_masked_values,
    const std::vector<uint64_t> &random_shares, const OtmpsiParams &params) {
  OTMPSI_ENFORCE(party_idx < params.parties, "party index out of range");
  OTMPSI_ENFORCE(opened_masked_values.size() == params.set_size &&
                   random_shares.size() == params.set_size,
               "additive masked compare input row mismatch");
  const auto layout = MakeAdditiveMaskedCompareLayout(params.parties);
  volePSI::Matrix<volePSI::u8> input;
  input.resize(params.set_size, (layout.input_bits + 7) / 8);
  if (input.size() != 0) {
    std::memset(input.data(), 0, input.size() * sizeof(volePSI::u8));
  }

  otmpsi_compat::ParallelFor(0, params.set_size, [&](int64_t begin, int64_t end) {
    for (int64_t r = begin; r < end; ++r) {
      const size_t row_idx = static_cast<size_t>(r);
      auto *row = input[row_idx].data();
      if (party_idx == 0) {
        WriteMaskedCountBits(row, layout.opened_mask_offset,
                             opened_masked_values[row_idx],
                             layout.compare_bits);
        SetPackedBit(row, layout.one_offset, true);
      }
      WriteMaskedCountBits(row,
                           layout.random_share_offset +
                               party_idx * layout.compare_bits,
                           random_shares[row_idx], layout.compare_bits);
    }
  });
  return input;
}

std::vector<OtmpsiResult> DecodePaperGmwOutput(
    const volePSI::Matrix<volePSI::u8> &out, const std::vector<uint128_t> &x0,
    const OtmpsiParams &params) {
  const auto layout = MakePaperGmwLayout(params.parties);
  std::vector<OtmpsiResult> results;
  for (size_t row_idx = 0; row_idx < x0.size(); ++row_idx) {
    const auto *row = out[row_idx].data();
    const bool over_threshold = GetPackedBit(row, 0);
    size_t count = 0;
    for (size_t bit = 0; bit < layout.count_bits; ++bit) {
      if (GetPackedBit(row, 1 + bit)) {
        count |= size_t{1} << bit;
      }
    }
    std::vector<size_t> holders;
    for (size_t party = 0; party < params.parties; ++party) {
      if (GetPackedBit(row, 1 + layout.count_bits + party)) {
        holders.push_back(party);
      }
    }
    if (over_threshold) {
      results.push_back({x0[row_idx], count, std::move(holders)});
    } else {
      OTMPSI_ENFORCE(count == 0 && holders.empty(),
                   "masked GMW output leaked a below-threshold row {} count={} holders={}",
                   row_idx, count, holders.size());
    }
  }
  return results;
}

std::vector<OtmpsiResult> DecodeMaskedHolderGmwOutput(
    const volePSI::Matrix<volePSI::u8> &out, const std::vector<uint128_t> &x0,
    const OtmpsiParams &params) {
  std::vector<OtmpsiResult> results;
  for (size_t row_idx = 0; row_idx < x0.size(); ++row_idx) {
    const auto *row = out[row_idx].data();
    const bool over_threshold = GetPackedBit(row, 0);
    std::vector<size_t> holders;
    for (size_t party = 0; party < params.parties; ++party) {
      if (GetPackedBit(row, 1 + party)) {
        holders.push_back(party);
      }
    }
    if (over_threshold) {
      OTMPSI_ENFORCE(!holders.empty() && holders.front() == 0,
                   "masked holder output missing leader holder at row {}",
                   row_idx);
      results.push_back({x0[row_idx], holders.size(), std::move(holders)});
    } else {
      OTMPSI_ENFORCE(holders.empty(),
                   "masked holder output leaked a below-threshold row {} holders={}",
                   row_idx, holders.size());
    }
  }
  return results;
}

} // namespace examples::otmpsi
