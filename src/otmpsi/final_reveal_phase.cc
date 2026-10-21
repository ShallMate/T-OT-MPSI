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

} // namespace

size_t AdditiveMaskedOpenPayloadBytesPerClient(const OtmpsiParams &params) {
  return 2 * params.set_size * CompareShareBytes(params);
}

OpenMaskedValuesOutput
OpenAdditiveMaskedValues(size_t party_id,
                         const std::vector<uint64_t> &masked_share,
                         const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  const size_t compare_bits = CompareBitsForPartyCount(params.parties);
  const uint64_t mask = LowBitsMask(compare_bits);
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
  const size_t compare_bits = CompareBitsForPartyCount(params.parties);
  const uint64_t mask = LowBitsMask(compare_bits);
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
  const size_t compare_bits = CompareBitsForPartyCount(params.parties);
  const uint64_t mask = LowBitsMask(compare_bits);
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

  const size_t compare_bits = CompareBitsForPartyCount(params.parties);
  const uint64_t mask = LowBitsMask(compare_bits);
  MaskedCountCompareData data;
  data.compare_bits = compare_bits;
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

} // namespace examples::otmpsi
