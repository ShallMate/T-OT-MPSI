#include "otmpsi/relaxed_selector_open.h"

#include "otmpsi/local_socket.h"

namespace examples::otmpsi {
namespace {

constexpr size_t kRelaxedSelectorOpenChannel = 18;

std::vector<uint8_t> PackBits(const std::vector<uint64_t> &bits,
                              size_t expected_size) {
  OTMPSI_ENFORCE(bits.size() == expected_size,
               "relaxed selector bit-share row count mismatch");
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

std::vector<size_t> SelectedRows(const std::vector<uint8_t> &opened,
                                 size_t row_count) {
  std::vector<size_t> selected;
  selected.reserve(row_count);
  for (size_t row = 0; row < row_count; ++row) {
    if (PackedBit(opened, row)) {
      selected.push_back(row);
    }
  }
  return selected;
}

std::vector<uint8_t>
PackSelectedMembership(const std::vector<uint128_t> &membership_share,
                       const std::vector<size_t> &selected,
                       size_t expected_rows) {
  OTMPSI_ENFORCE(membership_share.size() == expected_rows,
               "relaxed selector membership-share row count mismatch");
  std::vector<uint8_t> packed((selected.size() + 7U) / 8U, 0);
  for (size_t index = 0; index < selected.size(); ++index) {
    if ((membership_share[selected[index]] & uint128_t{1}) != 0) {
      packed[index >> 3U] |= static_cast<uint8_t>(1U << (index & 7U));
    }
  }
  return packed;
}

std::vector<OtmpsiResult> BuildResults(
    const std::vector<uint8_t> &opened_selection,
    const std::vector<std::vector<uint8_t>> &party_membership,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_membership_shares,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(x0.size() == params.set_size &&
                   leader_membership_shares.size() == params.parties - 1 &&
                   party_membership.size() == params.parties,
               "relaxed selector leader input mismatch");
  const auto selected = SelectedRows(opened_selection, params.set_size);
  std::vector<OtmpsiResult> results;
  results.reserve(selected.size());
  for (size_t selected_idx = 0; selected_idx < selected.size();
       ++selected_idx) {
    const size_t row = selected[selected_idx];
    std::vector<size_t> holders{0};
    for (size_t party = 1; party < params.parties; ++party) {
      OTMPSI_ENFORCE(leader_membership_shares[party - 1].size() ==
                       params.set_size,
                   "relaxed selector leader membership row mismatch");
      const bool member =
          ((leader_membership_shares[party - 1][row] & uint128_t{1}) != 0) ^
          PackedBit(party_membership[party], selected_idx);
      if (member) {
        holders.push_back(party);
      }
    }
    OTMPSI_ENFORCE(holders.size() >= params.threshold,
                 "relaxed selector opened a row below threshold");
    results.push_back({x0[row], holders.size(), std::move(holders)});
  }
  return results;
}

} // namespace

RelaxedSelectorOpenPartyRun RunRelaxedSelectorOpenParty(
    size_t party_id, const std::vector<uint64_t> &selection_share,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_membership_shares,
    const std::vector<uint128_t> &party_membership_share,
    const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  OTMPSI_ENFORCE(party_id < params.parties,
               "relaxed selector party index out of range");
  auto packed_selection = PackBits(selection_share, params.set_size);
  size_t comm_bytes = 0;
  RelaxedSelectorOpenPartyRun run;

  if (party_id == 0) {
    struct PeerState {
      std::vector<uint8_t> packed_selection;
      std::unique_ptr<coproto::Socket> socket;
    };
    std::vector<PeerState> peers(params.parties);
    std::vector<std::future<PeerState>> receive_jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      const auto tag =
          MakeSocketTag(params.base_port, party, kRelaxedSelectorOpenChannel);
      receive_jobs[party] = std::async(std::launch::async, [&, tag] {
        PeerState peer;
        peer.socket =
            std::make_unique<coproto::Socket>(AcquireLocalSocket(tag, 0));
        peer.packed_selection.resize(packed_selection.size());
        macoro::sync_wait(peer.socket->recv(coproto::span<uint8_t>(
            peer.packed_selection.data(), peer.packed_selection.size())));
        return peer;
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      peers[party] = receive_jobs[party].get();
      for (size_t byte = 0; byte < packed_selection.size(); ++byte) {
        packed_selection[byte] ^= peers[party].packed_selection[byte];
      }
    }

    const auto selected = SelectedRows(packed_selection, params.set_size);
    const size_t membership_bytes = (selected.size() + 7U) / 8U;
    std::vector<std::vector<uint8_t>> party_membership(params.parties);
    std::vector<std::future<size_t>> reveal_jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      reveal_jobs[party] = std::async(std::launch::async, [&, party] {
        auto &socket = *peers[party].socket;
        macoro::sync_wait(socket.send(coproto::span<const uint8_t>(
            packed_selection.data(), packed_selection.size())));
        macoro::sync_wait(socket.flush());
        party_membership[party].resize(membership_bytes);
        if (membership_bytes != 0) {
          macoro::sync_wait(socket.recv(coproto::span<uint8_t>(
              party_membership[party].data(),
              party_membership[party].size())));
        }
        macoro::sync_wait(socket.flush());
        const size_t bytes = static_cast<size_t>(
            socket.bytesSent() + socket.bytesReceived());
        macoro::sync_wait(socket.close());
        return bytes;
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      comm_bytes += reveal_jobs[party].get();
    }
    run.results = BuildResults(packed_selection, party_membership, x0,
                               leader_membership_shares, params);
  } else {
    const auto tag =
        MakeSocketTag(params.base_port, party_id, kRelaxedSelectorOpenChannel);
    auto socket = AcquireLocalSocket(tag, 1);
    macoro::sync_wait(socket.send(coproto::span<const uint8_t>(
        packed_selection.data(), packed_selection.size())));
    macoro::sync_wait(socket.flush());
    macoro::sync_wait(socket.recv(coproto::span<uint8_t>(
        packed_selection.data(), packed_selection.size())));
    const auto selected = SelectedRows(packed_selection, params.set_size);
    auto packed_membership = PackSelectedMembership(
        party_membership_share, selected, params.set_size);
    if (!packed_membership.empty()) {
      macoro::sync_wait(socket.send(coproto::span<const uint8_t>(
          packed_membership.data(), packed_membership.size())));
    }
    macoro::sync_wait(socket.flush());
    comm_bytes =
        static_cast<size_t>(socket.bytesSent() + socket.bytesReceived());
    macoro::sync_wait(socket.close());
  }

  run.stats = RoleStats{SecondsSince(start), comm_bytes};
  return run;
}

RelaxedSelectorOpenRun RunRelaxedSelectorOpen(
    const std::vector<std::vector<uint64_t>> &selection_shares,
    const std::vector<uint128_t> &x0,
    const std::vector<std::vector<uint128_t>> &leader_membership_shares,
    const std::vector<std::vector<uint128_t>> &party_membership_shares,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(selection_shares.size() == params.parties &&
                   party_membership_shares.size() == params.parties - 1,
               "relaxed selector party count mismatch");
  std::vector<std::future<RelaxedSelectorOpenPartyRun>> jobs(params.parties);
  const std::vector<uint128_t> empty_membership;
  const std::vector<std::vector<uint128_t>> empty_leader_membership;
  const std::vector<uint128_t> empty_x0;
  for (size_t party = 0; party < params.parties; ++party) {
    jobs[party] = std::async(std::launch::async, [&, party] {
      return RunRelaxedSelectorOpenParty(
          party, selection_shares[party], party == 0 ? x0 : empty_x0,
          party == 0 ? leader_membership_shares : empty_leader_membership,
          party == 0 ? empty_membership
                     : party_membership_shares[party - 1],
          params);
    });
  }

  RelaxedSelectorOpenRun run;
  run.client_stats.assign(params.parties, {});
  for (size_t party = 0; party < params.parties; ++party) {
    auto party_run = jobs[party].get();
    if (party == 0) {
      run.results = std::move(party_run.results);
      run.leader = party_run.stats;
    } else {
      run.client_stats[party] = party_run.stats;
      run.clients.comm_bytes += party_run.stats.comm_bytes;
      run.clients.time_s =
          std::max(run.clients.time_s, party_run.stats.time_s);
    }
  }
  return run;
}

} // namespace examples::otmpsi
