#include "otmpsi/private_selector_ot.h"

#include "otmpsi/packed_chosen_ot.h"
#include "otmpsi/silent_ot_utils.h"

namespace examples::otmpsi {
namespace {

constexpr size_t kPrivateSelectorChannel = 16;

std::vector<uint8_t> PackBits(const std::vector<uint64_t> &bits,
                              size_t expected_size) {
  OTMPSI_ENFORCE(bits.size() == expected_size,
               "private-selector bit-share row count mismatch");
  std::vector<uint8_t> packed((expected_size + 7U) / 8U, 0);
  for (size_t row = 0; row < expected_size; ++row) {
    if ((bits[row] & 1U) != 0) {
      packed[row >> 3U] |= static_cast<uint8_t>(1U << (row & 7U));
    }
  }
  return packed;
}

bool PackedBit(const std::vector<uint8_t> &packed, size_t row) {
  return ((packed[row >> 3U] >> (row & 7U)) & 1U) != 0;
}

std::vector<OtmpsiResult> DecodeSelectedMembership(
    const std::vector<uint8_t> &selection,
    const std::vector<std::vector<uint8_t>> &received_membership,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_membership_shares,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(selection.size() == params.set_size &&
                   x0.size() == params.set_size,
               "private-selector leader row count mismatch");
  OTMPSI_ENFORCE(leader_membership_shares.size() == params.parties - 1 &&
                   received_membership.size() == params.parties,
               "private-selector leader party count mismatch");

  std::vector<OtmpsiResult> results;
  results.reserve(params.set_size);
  for (size_t row = 0; row < params.set_size; ++row) {
    if (selection[row] == 0) {
      continue;
    }
    std::vector<size_t> holders{0};
    holders.reserve(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      OTMPSI_ENFORCE(leader_membership_shares[party - 1].size() ==
                           params.set_size &&
                       received_membership[party].size() == params.set_size,
                   "private-selector membership row count mismatch");
      const bool leader_share =
          (leader_membership_shares[party - 1][row] & 1U) != 0;
      const bool client_share = received_membership[party][row] != 0;
      if (leader_share ^ client_share) {
        holders.push_back(party);
      }
    }
    OTMPSI_ENFORCE(holders.size() >= params.threshold,
                 "private-selector selected row below threshold");
    results.push_back({x0[row], holders.size(), std::move(holders)});
  }
  return results;
}

} // namespace

PrivateSelectorOtPartyRun RunPrivateSelectorOtParty(
    size_t party_id, const std::vector<uint64_t> &selection_share,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_membership_shares,
    const std::vector<uint128_t> &party_membership_share,
    const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  OTMPSI_ENFORCE(party_id < params.parties,
               "private-selector party index out of range");
  OTMPSI_ENFORCE(selection_share.size() == params.set_size,
               "private-selector selection-share row count mismatch");

  PrivateSelectorOtPartyRun run;
  size_t comm_bytes = 0;
  const size_t packed_size = (params.set_size + 7U) / 8U;
  // The GMW result is already a fresh XOR output sharing. Sending those
  // shares directly to party zero is the designated-output reconstruction.

  if (party_id == 0) {
    OTMPSI_ENFORCE(x0.size() == params.set_size &&
                     leader_membership_shares.size() == params.parties - 1,
                 "private-selector leader input mismatch");

    struct PeerState {
      std::vector<uint8_t> packed_selection_share;
      std::unique_ptr<coproto::Socket> socket;
    };
    std::vector<PeerState> peers(params.parties);
    std::vector<std::future<PeerState>> recv_jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      const auto tag = MakeSocketTag(params.base_port, party,
                                     kPrivateSelectorChannel);
      recv_jobs[party] = std::async(std::launch::async, [tag, packed_size] {
        PeerState state;
        state.socket =
            std::make_unique<coproto::Socket>(AcquireLocalSocket(tag, 0));
        state.packed_selection_share.resize(packed_size);
        macoro::sync_wait(state.socket->recv(coproto::span<uint8_t>(
            state.packed_selection_share.data(), packed_size)));
        return state;
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      peers[party] = recv_jobs[party].get();
    }

    std::vector<uint8_t> selection(params.set_size, 0);
    for (size_t row = 0; row < params.set_size; ++row) {
      bool bit = (selection_share[row] & 1U) != 0;
      for (size_t party = 1; party < params.parties; ++party) {
        bit ^= PackedBit(peers[party].packed_selection_share, row);
      }
      selection[row] = static_cast<uint8_t>(bit);
    }

    std::vector<std::vector<uint8_t>> received_membership(params.parties);
    std::vector<std::future<std::pair<std::vector<uint8_t>, size_t>>> ot_jobs(
        params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      ot_jobs[party] = std::async(std::launch::async, [&, party] {
        auto &sock = *peers[party].socket;
        oc::SilentOtExtReceiver receiver;
        ConfigureSilentOtReceiver(receiver, params.set_size, params.threads);
        oc::PRNG prng(oc::sysRandomSeed());
        macoro::sync_wait(receiver.genBaseOts(prng, sock));
        oc::BitVector choices(static_cast<oc::u64>(params.set_size));
        for (size_t row = 0; row < params.set_size; ++row) {
          choices[static_cast<oc::u64>(row)] = selection[row];
        }
        auto messages =
            ReceivePackedChosenOt(receiver, choices, 1, prng, sock);
        std::vector<uint8_t> membership(params.set_size, 0);
        for (size_t row = 0; row < params.set_size; ++row) {
          membership[row] = static_cast<uint8_t>(messages[row] & 1U);
        }
        macoro::sync_wait(sock.flush());
        const size_t bytes =
            static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
        macoro::sync_wait(sock.close());
        return std::make_pair(std::move(membership), bytes);
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      auto [membership, bytes] = ot_jobs[party].get();
      received_membership[party] = std::move(membership);
      comm_bytes += bytes;
    }
    run.results = DecodeSelectedMembership(
        selection, received_membership, x0, leader_membership_shares, params);
  } else {
    OTMPSI_ENFORCE(party_membership_share.size() == params.set_size,
                 "private-selector client membership row count mismatch");
    const auto tag =
        MakeSocketTag(params.base_port, party_id, kPrivateSelectorChannel);
    auto sock = AcquireLocalSocket(tag, 1);
    auto packed_selection = PackBits(selection_share, params.set_size);
    macoro::sync_wait(sock.send(coproto::span<const uint8_t>(
        packed_selection.data(), packed_selection.size())));

    oc::SilentOtExtSender sender;
    ConfigureSilentOtSender(sender, params.set_size, params.threads);
    oc::PRNG prng(oc::sysRandomSeed());
    macoro::sync_wait(sender.genBaseOts(prng, sock));
    std::vector<std::array<uint8_t, 2>> messages(params.set_size);
    for (size_t row = 0; row < params.set_size; ++row) {
      messages[row][0] = 0;
      messages[row][1] =
          static_cast<uint8_t>(party_membership_share[row] & 1U);
    }
    SendPackedChosenOt(sender, messages, 1, prng, sock);
    macoro::sync_wait(sock.flush());
    comm_bytes +=
        static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
    macoro::sync_wait(sock.close());
  }

  run.stats = RoleStats{SecondsSince(start), comm_bytes};
  return run;
}

PrivateSelectorOtRun RunPrivateSelectorOt(
    const std::vector<std::vector<uint64_t>> &selection_shares,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_membership_shares,
    const std::vector<std::vector<uint128_t>> &party_membership_shares,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(selection_shares.size() == params.parties &&
                   party_membership_shares.size() == params.parties - 1,
               "private-selector all-party input mismatch");
  std::vector<std::future<PrivateSelectorOtPartyRun>> jobs(params.parties);
  for (size_t party = 0; party < params.parties; ++party) {
    jobs[party] = std::async(std::launch::async, [&, party] {
      const std::vector<uint128_t> empty;
      return RunPrivateSelectorOtParty(
          party, selection_shares[party], x0, leader_membership_shares,
          party == 0 ? empty : party_membership_shares[party - 1], params);
    });
  }

  PrivateSelectorOtRun run;
  run.client_stats.resize(params.parties);
  for (size_t party = 0; party < params.parties; ++party) {
    auto party_run = jobs[party].get();
    if (party == 0) {
      run.results = std::move(party_run.results);
      run.leader = party_run.stats;
    } else {
      run.client_stats[party] = party_run.stats;
      run.clients.comm_bytes += party_run.stats.comm_bytes;
      run.clients.time_s = std::max(run.clients.time_s, party_run.stats.time_s);
    }
  }
  return run;
}

} // namespace examples::otmpsi
