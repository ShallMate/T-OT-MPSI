#include "otmpsi/final_reveal_phase.h"

namespace examples::otmpsi {
namespace {

size_t CompareShareBytes(const OtmpsiParams &params) {
  OTMPSI_ENFORCE(CompareBitsForPartyCount(params.parties) <= 8,
               "uint8 comparison shares require N < 128");
  return sizeof(uint8_t);
}

std::vector<uint8_t> PackCompareShares(const std::vector<uint64_t> &shares,
                                       const OtmpsiParams &params) {
  const size_t share_bytes = CompareShareBytes(params);
  OTMPSI_ENFORCE(share_bytes == sizeof(uint8_t),
               "relaxed share packing expects uint8 shares");
  const uint64_t mask = LowBitsMask(CompareBitsForPartyCount(params.parties));
  std::vector<uint8_t> packed(shares.size() * share_bytes);
  for (size_t row = 0; row < shares.size(); ++row) {
    packed[row] = static_cast<uint8_t>(shares[row] & mask);
  }
  return packed;
}

std::vector<uint64_t> UnpackCompareShares(const std::vector<uint8_t> &packed,
                                          const OtmpsiParams &params) {
  const size_t share_bytes = CompareShareBytes(params);
  OTMPSI_ENFORCE(share_bytes == sizeof(uint8_t),
               "relaxed share unpacking expects uint8 shares");
  OTMPSI_ENFORCE(packed.size() == params.set_size * share_bytes,
               "packed relaxed share size mismatch");
  const uint64_t mask = LowBitsMask(CompareBitsForPartyCount(params.parties));
  std::vector<uint64_t> shares(params.set_size);
  for (size_t row = 0; row < params.set_size; ++row) {
    shares[row] = static_cast<uint64_t>(packed[row]) & mask;
  }
  return shares;
}

void ValidateRelaxedArithmeticShares(
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint64_t>> &party_arith_shares,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(x0.size() == params.set_size,
               "relaxed output row count mismatch");
  OTMPSI_ENFORCE(leader_arith_shares.size() == params.parties - 1 &&
                   party_arith_shares.size() == params.parties - 1,
               "relaxed output share vector mismatch");
  for (size_t party = 1; party < params.parties; ++party) {
    OTMPSI_ENFORCE(leader_arith_shares[party - 1].size() == params.set_size &&
                     party_arith_shares[party - 1].size() == params.set_size,
                 "relaxed output share row mismatch");
  }
}

struct RelaxedSelectionAndResults {
  std::vector<uint8_t> packed_selection;
  std::vector<OtmpsiResult> results;
};

RelaxedSelectionAndResults BuildRelaxedSelectionAndResults(
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint64_t>> &party_arith_shares,
    const OtmpsiParams &params) {
  ValidateRelaxedArithmeticShares(x0, leader_arith_shares, party_arith_shares,
                                  params);
  OTMPSI_ENFORCE(params.parties < 128,
               "relaxed output stack holder buffer requires N < 128");
  RelaxedSelectionAndResults out;
  out.packed_selection.assign((params.set_size + 7) / 8, 0);
  out.results.reserve(params.set_size);
  for (size_t row = 0; row < params.set_size; ++row) {
    size_t count = 1;
    std::array<size_t, 128> holders_buf{};
    size_t holder_count = 1;
    holders_buf[0] = 0;
    for (size_t party = 1; party < params.parties; ++party) {
      const uint64_t opened_bit = leader_arith_shares[party - 1][row] +
                                  party_arith_shares[party - 1][row];
      if ((opened_bit & 1U) != 0) {
        holders_buf[holder_count++] = party;
      }
      count += static_cast<size_t>(opened_bit & 1U);
    }
    const bool selected = count >= params.threshold;
    if (selected) {
      out.packed_selection[row >> 3U] |= static_cast<uint8_t>(1U << (row & 7U));
      std::vector<size_t> holders(holders_buf.begin(),
                                  holders_buf.begin() + holder_count);
      out.results.push_back({x0[row], count, std::move(holders)});
    }
  }
  return out;
}

constexpr size_t kThresholdTwoLeaderChannel = 12;
constexpr size_t kRelaxedBooleanHolderChannel = 13;

std::vector<uint8_t> PackBooleanShares(const std::vector<uint128_t> &shares,
                                       const OtmpsiParams &params) {
  OTMPSI_ENFORCE(shares.size() == params.set_size,
               "boolean share row count mismatch");
  std::vector<uint8_t> packed((params.set_size + 7U) / 8U, 0);
  for (size_t row = 0; row < params.set_size; ++row) {
    if ((shares[row] & 1U) != 0) {
      packed[row >> 3U] |= static_cast<uint8_t>(1U << (row & 7U));
    }
  }
  return packed;
}

bool PackedBooleanBit(const std::vector<uint8_t> &packed, size_t row) {
  return ((packed[row >> 3U] >> (row & 7U)) & 1U) != 0;
}

std::vector<uint8_t>
PackSelectedBooleanShares(const std::vector<uint128_t> &shares,
                          const std::vector<size_t> &selected_rows) {
  std::vector<uint8_t> packed((selected_rows.size() + 7U) / 8U, 0);
  for (size_t idx = 0; idx < selected_rows.size(); ++idx) {
    if ((shares[selected_rows[idx]] & 1U) != 0) {
      packed[idx >> 3U] |= static_cast<uint8_t>(1U << (idx & 7U));
    }
  }
  return packed;
}

template <typename PartyBitFn>
std::vector<OtmpsiResult> BuildSelectedBooleanResults(
    const std::vector<uint8_t> &over_threshold,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const OtmpsiParams &params, PartyBitFn party_bit) {
  OTMPSI_ENFORCE(over_threshold.size() == params.set_size &&
                   x0.size() == params.set_size,
               "relaxed boolean output row count mismatch");
  OTMPSI_ENFORCE(leader_bit_shares.size() == params.parties - 1,
               "relaxed boolean leader share vector mismatch");
  OTMPSI_ENFORCE(params.parties < 128,
               "relaxed boolean output stack holder buffer requires N < 128");
  for (size_t party = 1; party < params.parties; ++party) {
    OTMPSI_ENFORCE(leader_bit_shares[party - 1].size() == params.set_size,
                 "relaxed boolean leader share row mismatch");
  }

  std::vector<OtmpsiResult> results;
  results.reserve(params.set_size);
  std::array<size_t, 128> holders_buf{};
  for (size_t row = 0; row < params.set_size; ++row) {
    if (!over_threshold[row]) {
      continue;
    }
    size_t holder_count = 1;
    holders_buf[0] = 0;
    for (size_t party = 1; party < params.parties; ++party) {
      const bool member = (((leader_bit_shares[party - 1][row] & 1U) != 0) ^
                           party_bit(party, row, results.size()));
      if (member) {
        holders_buf[holder_count++] = party;
      }
    }
    OTMPSI_ENFORCE(holder_count >= params.threshold,
                 "relaxed boolean selected row below threshold");
    std::vector<size_t> holders(holders_buf.begin(),
                                holders_buf.begin() + holder_count);
    results.push_back({x0[row], holder_count, std::move(holders)});
  }
  return results;
}

template <typename PartyBitFn>
RelaxedSelectionAndResults BuildThresholdTwoSelectionAndResults(
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const OtmpsiParams &params, PartyBitFn party_bit) {
  OTMPSI_ENFORCE(params.threshold == 2,
               "threshold-two fast output requires threshold=2");
  OTMPSI_ENFORCE(x0.size() == params.set_size,
               "threshold-two output leader row count mismatch");
  OTMPSI_ENFORCE(leader_bit_shares.size() == params.parties - 1,
               "threshold-two output leader share vector mismatch");
  OTMPSI_ENFORCE(params.parties < 128,
               "threshold-two output stack holder buffer requires N < 128");
  for (size_t party = 1; party < params.parties; ++party) {
    OTMPSI_ENFORCE(leader_bit_shares[party - 1].size() == params.set_size,
                 "threshold-two output leader share row mismatch");
  }

  RelaxedSelectionAndResults out;
  out.packed_selection.assign((params.set_size + 7U) / 8U, 0);
  out.results.reserve(params.set_size);
  std::array<size_t, 128> holders_buf{};
  for (size_t row = 0; row < params.set_size; ++row) {
    size_t holder_count = 1;
    holders_buf[0] = 0;
    for (size_t party = 1; party < params.parties; ++party) {
      const bool member = (((leader_bit_shares[party - 1][row] & 1U) != 0) ^
                           party_bit(party, row));
      if (member) {
        holders_buf[holder_count++] = party;
      }
    }
    if (holder_count >= 2) {
      out.packed_selection[row >> 3U] |= static_cast<uint8_t>(1U << (row & 7U));
      std::vector<size_t> holders(holders_buf.begin(),
                                  holders_buf.begin() + holder_count);
      out.results.push_back({x0[row], holder_count, std::move(holders)});
    }
  }
  return out;
}

template <typename PartyBitFn>
std::vector<OtmpsiResult> DecodeThresholdTwoLeaderResults(
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const OtmpsiParams &params, PartyBitFn party_bit) {
  return BuildThresholdTwoSelectionAndResults(x0, leader_bit_shares, params,
                                              party_bit)
      .results;
}

} // namespace

size_t ArithmeticFinalRevealBytesPerClient(const OtmpsiParams &params) {
  // Each non-leader party reveals two arithmetic shares per row to the leader:
  // its contribution to the count and its own holder bit.
  return params.set_size * 2 * sizeof(uint64_t);
}

size_t AdditiveMaskedOpenPayloadBytesPerClient(const OtmpsiParams &params) {
  return 2 * params.set_size * CompareShareBytes(params);
}

void AddArithmeticFinalRevealStats(NPartyCompareGmwOutput &run,
                                   const OtmpsiParams &params) {
  const size_t per_client = ArithmeticFinalRevealBytesPerClient(params);
  run.leader.comm_bytes += per_client * (params.parties - 1);
  for (size_t party = 1; party < params.parties; ++party) {
    run.client_stats[party].comm_bytes += per_client;
    run.clients.comm_bytes += per_client;
  }
}

std::vector<OtmpsiResult> DecodeArithmeticMaskedOutput(
    const std::vector<uint8_t> &over_threshold,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint64_t>> &party_arith_shares,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(over_threshold.size() == params.set_size &&
                   x0.size() == params.set_size,
               "arithmetic final output row count mismatch");
  OTMPSI_ENFORCE(leader_arith_shares.size() == params.parties - 1 &&
                   party_arith_shares.size() == params.parties - 1,
               "arithmetic final output share vector mismatch");
  for (size_t party = 1; party < params.parties; ++party) {
    OTMPSI_ENFORCE(leader_arith_shares[party - 1].size() == params.set_size &&
                     party_arith_shares[party - 1].size() == params.set_size,
                 "arithmetic final output share row count mismatch");
  }

  std::vector<OtmpsiResult> results;
  for (size_t row = 0; row < params.set_size; ++row) {
    if (!over_threshold[row]) {
      continue;
    }

    uint64_t count_share = 1;
    std::vector<size_t> holders;
    holders.reserve(params.parties);
    holders.push_back(0);
    for (size_t party = 1; party < params.parties; ++party) {
      const uint64_t opened_bit = leader_arith_shares[party - 1][row] +
                                  party_arith_shares[party - 1][row];
      count_share += opened_bit & 1U;
      if ((opened_bit & 1U) != 0) {
        holders.push_back(party);
      }
    }
    results.push_back(
        {x0[row], static_cast<size_t>(count_share), std::move(holders)});
  }
  return results;
}

volePSI::Matrix<volePSI::u8> MakeOriginalMaskedCompareInput(
    size_t party_id, const std::vector<uint64_t> &opened_masked_values,
    const std::vector<uint64_t> &mask_bit_share, const OtmpsiParams &params) {
  OTMPSI_ENFORCE(party_id < params.parties, "party index out of range");
  OTMPSI_ENFORCE(opened_masked_values.size() == params.set_size &&
                   mask_bit_share.size() == params.set_size,
               "masked compare input row mismatch");
  const auto layout = MakeMaskedCountGmwLayout(params.parties);
  volePSI::Matrix<volePSI::u8> input;
  input.resize(params.set_size, (layout.input_bits + 7) / 8);
  if (input.size() != 0) {
    std::memset(input.data(), 0, input.size() * sizeof(volePSI::u8));
  }

  otmpsi_compat::ParallelFor(0, params.set_size, [&](int64_t begin, int64_t end) {
    for (int64_t r = begin; r < end; ++r) {
      const size_t row_idx = static_cast<size_t>(r);
      auto *row = input[row_idx].data();
      WriteMaskedCountBits(row, layout.mask_bit_offset, mask_bit_share[row_idx],
                           layout.compare_bits);
      if (party_id == 0) {
        WriteMaskedCountBits(row, layout.opened_mask_offset,
                             opened_masked_values[row_idx],
                             layout.compare_bits);
        SetPackedBit(row, layout.one_offset, true);
      }
    }
  });
  return input;
}

volePSI::Matrix<volePSI::u8> MakeOriginalMaskedOutputLeaderInput(
    const std::vector<uint64_t> &opened_masked_values,
    const std::vector<uint64_t> &mask_bit_share,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(opened_masked_values.size() == params.set_size &&
                   mask_bit_share.size() == params.set_size,
               "masked output leader input row mismatch");
  OTMPSI_ENFORCE(leader_bit_shares.size() == params.parties - 1,
               "masked output leader membership vector mismatch");
  for (size_t party = 1; party < params.parties; ++party) {
    OTMPSI_ENFORCE(leader_bit_shares[party - 1].size() == params.set_size,
                 "masked output leader membership row mismatch");
  }

  const auto layout = MakeMaskedCountGmwLayout(params.parties);
  auto input = MakeMaskedCountGmwEmptyInput(params);
  otmpsi_compat::ParallelFor(0, params.set_size, [&](int64_t begin, int64_t end) {
    for (int64_t r = begin; r < end; ++r) {
      const size_t row_idx = static_cast<size_t>(r);
      auto *row = input[row_idx].data();
      WriteMaskedCountBits(row, layout.mask_bit_offset, mask_bit_share[row_idx],
                           layout.compare_bits);
      WriteMaskedCountBits(row, layout.opened_mask_offset,
                           opened_masked_values[row_idx], layout.compare_bits);
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

volePSI::Matrix<volePSI::u8> MakeOriginalMaskedOutputPartyInput(
    size_t party_id, const std::vector<uint64_t> &mask_bit_share,
    const std::vector<uint128_t> &party_bit_share, const OtmpsiParams &params) {
  OTMPSI_ENFORCE(party_id > 0 && party_id < params.parties,
               "masked output party index out of range");
  OTMPSI_ENFORCE(mask_bit_share.size() == params.set_size &&
                   party_bit_share.size() == params.set_size,
               "masked output party input row mismatch");

  const auto layout = MakeMaskedCountGmwLayout(params.parties);
  auto input = MakeMaskedCountGmwEmptyInput(params);
  otmpsi_compat::ParallelFor(0, params.set_size, [&](int64_t begin, int64_t end) {
    for (int64_t r = begin; r < end; ++r) {
      const size_t row_idx = static_cast<size_t>(r);
      auto *row = input[row_idx].data();
      WriteMaskedCountBits(row, layout.mask_bit_offset, mask_bit_share[row_idx],
                           layout.compare_bits);
      SetPackedBit(row, layout.membership_offset + party_id - 1,
                   (party_bit_share[row_idx] & 1U) != 0);
    }
  });
  return input;
}

OpenMaskedValuesOutput
OpenAdditiveMaskedValues(size_t party_id,
                         const std::vector<uint64_t> &masked_share,
                         const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  const auto layout = MakeAdditiveMaskedCompareLayout(params.parties);
  const uint64_t mask = LowBitsMask(layout.compare_bits);
  OTMPSI_ENFORCE(masked_share.size() == params.set_size,
               "masked share row count mismatch");
  OpenMaskedValuesOutput out;
  out.opened.assign(params.set_size, 0);
  size_t comm = 0;

  if (party_id == 0) {
    out.opened = masked_share;
    struct OpenRecvState {
      std::vector<uint8_t> packed_peer;
      std::unique_ptr<coproto::Socket> sock;
    };
    std::vector<std::future<OpenRecvState>> recv_jobs(params.parties);
    std::vector<std::unique_ptr<coproto::Socket>> socks(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      const auto tag = MakeSocketTag(params.base_port, party, 4);
      recv_jobs[party] = std::async(std::launch::async, [&, tag] {
        OpenRecvState state;
        state.sock =
            std::make_unique<coproto::Socket>(AcquireLocalSocket(tag, 0));
        state.packed_peer.resize(params.set_size * CompareShareBytes(params));
        macoro::sync_wait(state.sock->recv(coproto::span<uint8_t>(
            state.packed_peer.data(), state.packed_peer.size())));
        return state;
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      auto state = recv_jobs[party].get();
      auto peer = UnpackCompareShares(state.packed_peer, params);
      for (size_t row = 0; row < out.opened.size(); ++row) {
        out.opened[row] = (out.opened[row] + peer[row]) & mask;
      }
      socks[party] = std::move(state.sock);
    }
    auto packed_opened = PackCompareShares(out.opened, params);
    std::vector<std::future<size_t>> send_jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      send_jobs[party] = std::async(std::launch::async, [&, party] {
        auto &sock = *socks[party];
        macoro::sync_wait(sock.send(coproto::span<const uint8_t>(
            packed_opened.data(), packed_opened.size())));
        macoro::sync_wait(sock.flush());
        const auto bytes =
            static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
        macoro::sync_wait(sock.close());
        return bytes;
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      comm += send_jobs[party].get();
    }
  } else {
    const auto tag = MakeSocketTag(params.base_port, party_id, 4);
    auto sock = AcquireLocalSocket(tag, 1);
    auto packed_share = PackCompareShares(masked_share, params);
    std::vector<uint8_t> packed_opened(params.set_size *
                                       CompareShareBytes(params));
    macoro::sync_wait(sock.send(coproto::span<const uint8_t>(
        packed_share.data(), packed_share.size())));
    macoro::sync_wait(sock.recv(coproto::span<uint8_t>(
        packed_opened.data(), packed_opened.size())));
    macoro::sync_wait(sock.flush());
    out.opened = UnpackCompareShares(packed_opened, params);
    comm = static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
    macoro::sync_wait(sock.close());
  }

  out.stats = RoleStats{SecondsSince(start), comm};
  return out;
}

std::vector<uint64_t> LeaderCountMinusThresholdShares(
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const OtmpsiParams &params) {
  const auto layout = MakeAdditiveMaskedCompareLayout(params.parties);
  const uint64_t mask = LowBitsMask(layout.compare_bits);
  std::vector<uint64_t> shares(params.set_size,
                               (uint64_t{1} - params.threshold) & mask);
  OTMPSI_ENFORCE(leader_arith_shares.size() == params.parties - 1,
               "leader arithmetic share vector mismatch");
  for (size_t party = 1; party < params.parties; ++party) {
    OTMPSI_ENFORCE(leader_arith_shares[party - 1].size() == params.set_size,
                 "leader arithmetic share row count mismatch");
    for (size_t row = 0; row < params.set_size; ++row) {
      shares[row] = (shares[row] + leader_arith_shares[party - 1][row]) & mask;
    }
  }
  return shares;
}

std::vector<uint64_t> MaskCountShare(const std::vector<uint64_t> &count_share,
                                     const std::vector<uint64_t> &random_share,
                                     const OtmpsiParams &params) {
  const auto layout = MakeAdditiveMaskedCompareLayout(params.parties);
  const uint64_t mask = LowBitsMask(layout.compare_bits);
  OTMPSI_ENFORCE(count_share.size() == params.set_size &&
                   random_share.size() == params.set_size,
               "count/random share row mismatch");
  std::vector<uint64_t> masked(params.set_size);
  for (size_t row = 0; row < params.set_size; ++row) {
    masked[row] = (count_share[row] - random_share[row]) & mask;
  }
  return masked;
}

MaskedCountCompareData MakeMaskedCountCompareData(
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint64_t>> &party_arith_shares,
    const std::vector<MaskPrepShare> &mask_prep,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(mask_prep.size() == params.parties,
               "mask-prep party count mismatch");
  OTMPSI_ENFORCE(leader_arith_shares.size() == params.parties - 1,
               "expected one leader arithmetic share per non-leader party");
  OTMPSI_ENFORCE(party_arith_shares.size() == params.parties - 1,
               "expected one party arithmetic share per non-leader party");

  const auto layout = MakeMaskedCountGmwLayout(params.parties);
  const uint64_t mask = LowBitsMask(layout.compare_bits);
  MaskedCountCompareData data;
  data.compare_bits = layout.compare_bits;
  data.opened_masked_values.assign(params.set_size, 0);
  data.mask_bit_shares.assign(params.parties,
                              std::vector<uint64_t>(params.set_size, 0));

  const size_t client_open_bytes =
      AdditiveMaskedOpenPayloadBytesPerClient(params);
  data.open_comm_bytes.assign(params.parties, client_open_bytes);
  data.open_comm_bytes[0] = client_open_bytes * (params.parties - 1);

  for (size_t party = 0; party < params.parties; ++party) {
    OTMPSI_ENFORCE(mask_prep[party].arithmetic_share.size() == params.set_size &&
                     mask_prep[party].boolean_share.size() == params.set_size,
                 "mask-prep row count mismatch");
  }
  for (size_t party = 1; party < params.parties; ++party) {
    OTMPSI_ENFORCE(leader_arith_shares[party - 1].size() == params.set_size &&
                     party_arith_shares[party - 1].size() == params.set_size,
                 "masked count arithmetic-share row count mismatch");
  }

  for (size_t row = 0; row < params.set_size; ++row) {
    uint64_t opened =
        (uint64_t{1} - static_cast<uint64_t>(params.threshold)) & mask;
    for (size_t party = 1; party < params.parties; ++party) {
      opened = (opened + leader_arith_shares[party - 1][row]) & mask;
    }
    opened = (opened - mask_prep[0].arithmetic_share[row]) & mask;
    data.mask_bit_shares[0][row] = mask_prep[0].boolean_share[row] & mask;

    for (size_t party = 1; party < params.parties; ++party) {
      opened = (opened + party_arith_shares[party - 1][row] -
                mask_prep[party].arithmetic_share[row]) &
               mask;
      data.mask_bit_shares[party][row] =
          mask_prep[party].boolean_share[row] & mask;
    }
    data.opened_masked_values[row] = opened;
  }

  return data;
}

std::vector<size_t> SelectedRows(const std::vector<uint8_t> &bits) {
  std::vector<size_t> rows;
  for (size_t row = 0; row < bits.size(); ++row) {
    if (bits[row]) {
      rows.push_back(row);
    }
  }
  return rows;
}

FinalRevealOutput ArithmeticFinalRevealParty(
    size_t party_id, const std::vector<uint8_t> &over_threshold,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<uint64_t> &party_arith_share,
    const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  const auto selected = SelectedRows(over_threshold);
  size_t comm = 0;
  FinalRevealOutput out;

  if (party_id == 0) {
    OTMPSI_ENFORCE(x0.size() == params.set_size &&
                     leader_arith_shares.size() == params.parties - 1,
                 "leader final reveal input mismatch");
    std::vector<std::vector<uint64_t>> client_reveals(params.parties);
    std::vector<std::future<size_t>> recv_jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      const auto tag = MakeSocketTag(params.base_port, party, 10);
      recv_jobs[party] = std::async(std::launch::async, [&, party, tag] {
        auto sock = AcquireLocalSocket(tag, 0);
        client_reveals[party].resize(selected.size());
        macoro::sync_wait(sock.recv(coproto::span<uint64_t>(
            client_reveals[party].data(), client_reveals[party].size())));
        macoro::sync_wait(sock.flush());
        const auto bytes =
            static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
        macoro::sync_wait(sock.close());
        return bytes;
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      comm += recv_jobs[party].get();
    }

    out.results.reserve(selected.size());
    for (size_t idx = 0; idx < selected.size(); ++idx) {
      const size_t row = selected[idx];
      size_t count = 1;
      std::vector<size_t> holders = {0};
      for (size_t party = 1; party < params.parties; ++party) {
        const uint64_t opened_bit =
            leader_arith_shares[party - 1][row] + client_reveals[party][idx];
        if ((opened_bit & 1U) != 0) {
          holders.push_back(party);
        }
        count += static_cast<size_t>(opened_bit & 1U);
      }
      out.results.push_back({x0[row], count, std::move(holders)});
    }
  } else {
    const auto tag = MakeSocketTag(params.base_port, party_id, 10);
    auto sock = AcquireLocalSocket(tag, 1);
    std::vector<uint64_t> reveal;
    reveal.reserve(selected.size());
    for (const auto row : selected) {
      reveal.push_back(party_arith_share[row]);
    }
    macoro::sync_wait(
        sock.send(coproto::span<const uint64_t>(reveal.data(), reveal.size())));
    macoro::sync_wait(sock.flush());
    comm = static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
    macoro::sync_wait(sock.close());
  }

  out.stats = RoleStats{SecondsSince(start), comm};
  return out;
}

RelaxedOpenOutput RunRelaxedOpenOutput(
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint64_t>> &party_arith_shares,
    const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  auto decoded = BuildRelaxedSelectionAndResults(x0, leader_arith_shares,
                                                 party_arith_shares, params);
  RelaxedOpenOutput out;
  out.results = std::move(decoded.results);

  const size_t share_bytes = CompareShareBytes(params);
  const size_t selection_bytes = decoded.packed_selection.size();
  const size_t per_client = params.set_size * share_bytes + selection_bytes;
  out.leader.comm_bytes = per_client * (params.parties - 1);
  out.clients.comm_bytes = per_client * (params.parties - 1);
  out.client_stats.assign(params.parties, {});
  for (size_t party = 1; party < params.parties; ++party) {
    out.client_stats[party].comm_bytes = per_client;
  }

  const double elapsed = SecondsSince(start);
  out.leader.time_s = elapsed;
  out.clients.time_s = elapsed;
  for (size_t party = 1; party < out.client_stats.size(); ++party) {
    out.client_stats[party].time_s = elapsed;
  }
  return out;
}

DistributedRelaxedOpenOutput RunRelaxedOpenOutputParty(
    size_t party_id, const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<uint64_t> &party_arith_share,
    const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  const size_t share_bytes = CompareShareBytes(params);
  const size_t selection_bytes = (params.set_size + 7) / 8;
  size_t comm = 0;
  DistributedRelaxedOpenOutput out;

  if (party_id == 0) {
    OTMPSI_ENFORCE(leader_arith_shares.size() == params.parties - 1,
                 "relaxed leader share vector mismatch");
    std::vector<std::vector<uint64_t>> party_shares(params.parties - 1);
    struct RelaxedRecvState {
      std::vector<uint8_t> packed_share;
      std::unique_ptr<coproto::Socket> sock;
    };
    std::vector<std::future<RelaxedRecvState>> recv_jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      const auto tag = MakeSocketTag(params.base_port, party, 11);
      recv_jobs[party] = std::async(std::launch::async, [&, tag] {
        RelaxedRecvState state;
        state.sock =
            std::make_unique<coproto::Socket>(AcquireLocalSocket(tag, 0));
        state.packed_share.resize(params.set_size * share_bytes);
        macoro::sync_wait(state.sock->recv(coproto::span<uint8_t>(
            state.packed_share.data(), state.packed_share.size())));
        return state;
      });
    }

    std::vector<std::unique_ptr<coproto::Socket>> socks(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      auto state = recv_jobs[party].get();
      party_shares[party - 1] = UnpackCompareShares(state.packed_share, params);
      socks[party] = std::move(state.sock);
    }

    auto decoded = BuildRelaxedSelectionAndResults(x0, leader_arith_shares,
                                                   party_shares, params);
    auto packed_selection = std::move(decoded.packed_selection);
    out.results = std::move(decoded.results);

    std::vector<std::future<size_t>> send_jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      send_jobs[party] = std::async(std::launch::async, [&, party] {
        auto &sock = *socks[party];
        macoro::sync_wait(sock.send(coproto::span<const uint8_t>(
            packed_selection.data(), packed_selection.size())));
        macoro::sync_wait(sock.flush());
        const auto bytes =
            static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
        macoro::sync_wait(sock.close());
        return bytes;
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      comm += send_jobs[party].get();
    }
  } else {
    OTMPSI_ENFORCE(party_arith_share.size() == params.set_size,
                 "relaxed party share row mismatch");
    const auto tag = MakeSocketTag(params.base_port, party_id, 11);
    auto sock = AcquireLocalSocket(tag, 1);
    auto packed_share = PackCompareShares(party_arith_share, params);
    std::vector<uint8_t> packed_selection(selection_bytes, 0);
    macoro::sync_wait(sock.send(coproto::span<const uint8_t>(
        packed_share.data(), packed_share.size())));
    macoro::sync_wait(sock.recv(coproto::span<uint8_t>(
        packed_selection.data(), packed_selection.size())));
    macoro::sync_wait(sock.flush());
    comm = static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
    macoro::sync_wait(sock.close());
  }

  out.stats = RoleStats{SecondsSince(start), comm};
  return out;
}

DistributedRelaxedOpenOutput RunRelaxedBooleanGmwOutputParty(
    size_t party_id, const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const std::vector<uint128_t> &party_bit_share, const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  volePSI::Matrix<volePSI::u8> gmw_input =
      party_id == 0
          ? MakeMembershipGmwLeaderInput(leader_bit_shares, params)
          : MakeMembershipGmwPartyInput(party_id, party_bit_share, params);
  auto compare = RunNPartyMembershipCompareGmwParty(
      gmw_input, party_id, params, "multiprocess-gmw-relaxed-membership");

  const auto selected = SelectedRows(compare.over_threshold);
  const size_t selected_share_bytes = (selected.size() + 7U) / 8U;
  size_t comm = compare.stats.comm_bytes;
  DistributedRelaxedOpenOutput out;

  if (party_id == 0) {
    OTMPSI_ENFORCE(x0.size() == params.set_size &&
                     leader_bit_shares.size() == params.parties - 1,
                 "relaxed boolean leader input mismatch");
    struct RecvState {
      std::vector<uint8_t> packed_share;
      std::unique_ptr<coproto::Socket> sock;
    };
    std::vector<std::vector<uint8_t>> packed_party_shares(params.parties);
    std::vector<std::future<RecvState>> recv_jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      const auto tag =
          MakeSocketTag(params.base_port, party, kRelaxedBooleanHolderChannel);
      recv_jobs[party] = std::async(std::launch::async, [&, tag] {
        RecvState state;
        state.sock =
            std::make_unique<coproto::Socket>(AcquireLocalSocket(tag, 0));
        state.packed_share.resize(selected_share_bytes);
        macoro::sync_wait(state.sock->recv(coproto::span<uint8_t>(
            state.packed_share.data(), state.packed_share.size())));
        return state;
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      auto state = recv_jobs[party].get();
      packed_party_shares[party] = std::move(state.packed_share);
      macoro::sync_wait(state.sock->flush());
      comm += static_cast<size_t>(state.sock->bytesSent() +
                                  state.sock->bytesReceived());
      macoro::sync_wait(state.sock->close());
    }
    out.results = BuildSelectedBooleanResults(
        compare.over_threshold, x0, leader_bit_shares, params,
        [&](size_t party, size_t, size_t selected_idx) {
          return PackedBooleanBit(packed_party_shares[party], selected_idx);
        });
  } else {
    OTMPSI_ENFORCE(party_bit_share.size() == params.set_size,
                 "relaxed boolean party share row mismatch");
    const auto tag =
        MakeSocketTag(params.base_port, party_id, kRelaxedBooleanHolderChannel);
    auto sock = AcquireLocalSocket(tag, 1);
    auto packed_share = PackSelectedBooleanShares(party_bit_share, selected);
    macoro::sync_wait(sock.send(coproto::span<const uint8_t>(
        packed_share.data(), packed_share.size())));
    macoro::sync_wait(sock.flush());
    comm += static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
    macoro::sync_wait(sock.close());
  }

  out.stats = RoleStats{SecondsSince(start), comm};
  return out;
}

ThresholdTwoLeaderOutput RunThresholdTwoLeaderOutput(
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const std::vector<std::vector<uint128_t>> &party_bit_shares,
    const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  OTMPSI_ENFORCE(party_bit_shares.size() == params.parties - 1,
               "threshold-two output party share vector mismatch");
  std::vector<std::vector<uint8_t>> packed_party_shares(params.parties);
  for (size_t party = 1; party < params.parties; ++party) {
    packed_party_shares[party] =
        PackBooleanShares(party_bit_shares[party - 1], params);
  }

  ThresholdTwoLeaderOutput out;
  out.results = DecodeThresholdTwoLeaderResults(
      x0, leader_bit_shares, params, [&](size_t party, size_t row) {
        return PackedBooleanBit(packed_party_shares[party], row);
      });

  const size_t per_client = (params.set_size + 7U) / 8U;
  out.leader.comm_bytes = per_client * (params.parties - 1);
  out.clients.comm_bytes = per_client * (params.parties - 1);
  out.client_stats.assign(params.parties, {});
  const double elapsed = SecondsSince(start);
  out.leader.time_s = elapsed;
  out.clients.time_s = elapsed;
  for (size_t party = 1; party < params.parties; ++party) {
    out.client_stats[party] = RoleStats{elapsed, per_client};
  }
  return out;
}

FinalRevealOutput RunThresholdTwoLeaderOutputParty(
    size_t party_id, const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const std::vector<uint128_t> &party_bit_share, const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  OTMPSI_ENFORCE(params.threshold == 2,
               "threshold-two fast output requires threshold=2");
  OTMPSI_ENFORCE(party_id < params.parties,
               "threshold-two output party id out of range");
  const size_t packed_bytes = (params.set_size + 7U) / 8U;
  size_t comm = 0;
  FinalRevealOutput out;

  if (party_id == 0) {
    struct RecvState {
      std::vector<uint8_t> packed_share;
      size_t comm_bytes = 0;
    };
    std::vector<std::vector<uint8_t>> packed_party_shares(params.parties);
    std::vector<std::future<RecvState>> recv_jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      const auto tag =
          MakeSocketTag(params.base_port, party, kThresholdTwoLeaderChannel);
      recv_jobs[party] = std::async(std::launch::async, [&, tag] {
        auto sock = AcquireLocalSocket(tag, 0);
        RecvState state;
        state.packed_share.resize(packed_bytes);
        macoro::sync_wait(sock.recv(coproto::span<uint8_t>(
            state.packed_share.data(), state.packed_share.size())));
        state.comm_bytes =
            static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
        return state;
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      auto state = recv_jobs[party].get();
      comm += state.comm_bytes;
      packed_party_shares[party] = std::move(state.packed_share);
    }
    out.results = DecodeThresholdTwoLeaderResults(
        x0, leader_bit_shares, params, [&](size_t party, size_t row) {
          return PackedBooleanBit(packed_party_shares[party], row);
        });
  } else {
    const auto tag =
        MakeSocketTag(params.base_port, party_id, kThresholdTwoLeaderChannel);
    auto sock = AcquireLocalSocket(tag, 1);
    auto packed_share = PackBooleanShares(party_bit_share, params);
    macoro::sync_wait(sock.send(coproto::span<const uint8_t>(
        packed_share.data(), packed_share.size())));
    macoro::sync_wait(sock.flush());
    comm = static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
  }

  out.stats = RoleStats{SecondsSince(start), comm};
  return out;
}

RelaxedOpenOutput RunThresholdTwoRelaxedOpenOutput(
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const std::vector<std::vector<uint128_t>> &party_bit_shares,
    const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  OTMPSI_ENFORCE(party_bit_shares.size() == params.parties - 1,
               "threshold-two relaxed party share vector mismatch");
  std::vector<std::vector<uint8_t>> packed_party_shares(params.parties);
  for (size_t party = 1; party < params.parties; ++party) {
    packed_party_shares[party] =
        PackBooleanShares(party_bit_shares[party - 1], params);
  }

  auto decoded = BuildThresholdTwoSelectionAndResults(
      x0, leader_bit_shares, params, [&](size_t party, size_t row) {
        return PackedBooleanBit(packed_party_shares[party], row);
      });

  const size_t selection_bytes = decoded.packed_selection.size();
  const size_t per_client = 2 * selection_bytes;
  RelaxedOpenOutput out;
  out.results = std::move(decoded.results);
  out.leader.comm_bytes = per_client * (params.parties - 1);
  out.clients.comm_bytes = per_client * (params.parties - 1);
  out.client_stats.assign(params.parties, {});
  const double elapsed = SecondsSince(start);
  out.leader.time_s = elapsed;
  out.clients.time_s = elapsed;
  for (size_t party = 1; party < params.parties; ++party) {
    out.client_stats[party] = RoleStats{elapsed, per_client};
  }
  return out;
}

DistributedRelaxedOpenOutput RunThresholdTwoRelaxedOpenOutputParty(
    size_t party_id, const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const std::vector<uint128_t> &party_bit_share, const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  OTMPSI_ENFORCE(params.threshold == 2,
               "threshold-two relaxed fast output requires threshold=2");
  OTMPSI_ENFORCE(party_id < params.parties,
               "threshold-two relaxed party id out of range");
  const size_t selection_bytes = (params.set_size + 7U) / 8U;
  size_t comm = 0;
  DistributedRelaxedOpenOutput out;

  if (party_id == 0) {
    OTMPSI_ENFORCE(leader_bit_shares.size() == params.parties - 1,
                 "threshold-two relaxed leader share vector mismatch");
    struct RecvState {
      std::vector<uint8_t> packed_share;
      std::unique_ptr<coproto::Socket> sock;
    };
    std::vector<std::vector<uint8_t>> packed_party_shares(params.parties);
    std::vector<std::future<RecvState>> recv_jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      const auto tag = MakeSocketTag(params.base_port, party, 11);
      recv_jobs[party] = std::async(std::launch::async, [&, tag] {
        RecvState state;
        state.sock =
            std::make_unique<coproto::Socket>(AcquireLocalSocket(tag, 0));
        state.packed_share.resize(selection_bytes);
        macoro::sync_wait(state.sock->recv(coproto::span<uint8_t>(
            state.packed_share.data(), state.packed_share.size())));
        return state;
      });
    }

    std::vector<std::unique_ptr<coproto::Socket>> socks(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      auto state = recv_jobs[party].get();
      packed_party_shares[party] = std::move(state.packed_share);
      socks[party] = std::move(state.sock);
    }

    auto decoded = BuildThresholdTwoSelectionAndResults(
        x0, leader_bit_shares, params, [&](size_t party, size_t row) {
          return PackedBooleanBit(packed_party_shares[party], row);
        });
    auto packed_selection = std::move(decoded.packed_selection);
    out.results = std::move(decoded.results);

    std::vector<std::future<size_t>> send_jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      send_jobs[party] = std::async(std::launch::async, [&, party] {
        auto &sock = *socks[party];
        macoro::sync_wait(sock.send(coproto::span<const uint8_t>(
            packed_selection.data(), packed_selection.size())));
        macoro::sync_wait(sock.flush());
        const auto bytes =
            static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
        return bytes;
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      comm += send_jobs[party].get();
    }
  } else {
    const auto tag = MakeSocketTag(params.base_port, party_id, 11);
    auto sock = AcquireLocalSocket(tag, 1);
    auto packed_share = PackBooleanShares(party_bit_share, params);
    std::vector<uint8_t> packed_selection(selection_bytes, 0);
    macoro::sync_wait(sock.send(coproto::span<const uint8_t>(
        packed_share.data(), packed_share.size())));
    macoro::sync_wait(sock.recv(coproto::span<uint8_t>(
        packed_selection.data(), packed_selection.size())));
    comm = static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
  }

  out.stats = RoleStats{SecondsSince(start), comm};
  return out;
}

} // namespace examples::otmpsi
