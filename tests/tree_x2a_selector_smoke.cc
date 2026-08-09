#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_set>

#include <unistd.h>

#include "otmpsi/b2a_phase.h"
#include "otmpsi/otmpsi_ports.h"
#include "otmpsi/packed_chosen_ot.h"
#include "otmpsi/private_selector_ot.h"
#include "otmpsi/tree_x2a_eda_bit.h"

namespace examples::otmpsi {
namespace {

void CheckTreeX2aRuns() {
  OTMPSI_ENFORCE(PackedChosenOtCorrectionBytes(64, 8) == 128 &&
                   PackedChosenOtCorrectionBytes(64, 1) == 16,
               "packed chosen OT correction-size mismatch");
  OTMPSI_ENFORCE(CompareBitsForPartyCount(127) == 8,
               "fixed-width comparison rejected N=127");
  bool rejected_128 = false;
  try {
    (void)CompareBitsForPartyCount(128);
  } catch (const std::exception &) {
    rejected_128 = true;
  }
  OTMPSI_ENFORCE(rejected_128, "fixed-width comparison accepted N=128");

  for (size_t parties : {size_t{2}, size_t{3}, size_t{5}}) {
    OtmpsiParams params;
    params.parties = parties;
    params.set_size = 17;
    params.threshold = 2;
    params.base_port = static_cast<uint16_t>(24000 + parties * 32);
    auto run = RunTreeX2aMaskPrep(params, "tree-x2a-smoke");
    CheckTreeX2aMaskPrep(run.shares, params);
    OTMPSI_ENFORCE(TreeX2aOtCountPerPair(params.set_size) ==
                     8 * params.set_size,
                 "fixed-width Tree-X2A OT count mismatch");
  }
}

const PrecomputedGmwPairPrefix &FindGmwPrefix(
    const std::vector<PrecomputedGmwPairPrefix> &prefixes, size_t peer) {
  const auto it = std::find_if(
      prefixes.begin(), prefixes.end(),
      [peer](const PrecomputedGmwPairPrefix &prefix) {
        return prefix.peer == peer;
      });
  OTMPSI_ENFORCE(it != prefixes.end(), "missing combined GMW peer prefix");
  return *it;
}

void CheckCombinedTreeGmwPool() {
  OtmpsiParams params;
  params.parties = 3;
  params.set_size = 17;
  params.threshold = 2;
  params.base_port = 24600;

  // 2,048 packed correlations consume exactly 2^18 raw OTs. The retained
  // Tree tail therefore begins at a SilentTripleGen batch boundary and checks
  // the global-index extraction path used by large real workloads.
  constexpr size_t gmw_correlations_per_pair = size_t{1} << 11;
  auto run = RunTreeX2aMaskPrepWithGmwPrefix(
      gmw_correlations_per_pair, params, "tree-gmw-pool-smoke");
  CheckTreeX2aMaskPrep(run.shares, params);
  OTMPSI_ENFORCE(run.gmw_prefixes.size() == params.parties,
               "combined GMW prefix party count mismatch");

  for (size_t lo = 0; lo < params.parties; ++lo) {
    for (size_t hi = lo + 1; hi < params.parties; ++hi) {
      const auto &sender = FindGmwPrefix(run.gmw_prefixes[lo], hi);
      const auto &receiver = FindGmwPrefix(run.gmw_prefixes[hi], lo);
      OTMPSI_ENFORCE(sender.sender_role && !receiver.sender_role &&
                       sender.mult.size() == gmw_correlations_per_pair &&
                       receiver.mult.size() == gmw_correlations_per_pair &&
                       sender.preprocess_comm_bytes > 0 &&
                       receiver.preprocess_comm_bytes > 0,
                   "combined GMW prefix metadata mismatch");
      for (size_t i = 0; i < gmw_correlations_per_pair; ++i) {
        OTMPSI_ENFORCE((sender.mult[i] & receiver.mult[i]) ==
                         (sender.add[i] ^ receiver.add[i]),
                     "combined GMW correlation mismatch at pair ({},{}) index {}",
                     lo, hi, i);
      }
    }
  }
}

void CheckB2aTruthTable() {
  OtmpsiParams params;
  params.parties = 2;
  params.set_size = 4;
  params.base_port = 24700;
  const std::vector<uint128_t> leader_bits{0, 0, 1, 1};
  const std::vector<uint128_t> client_bits{0, 1, 0, 1};
  const auto tag = MakeSocketTag(params.base_port, 1, 3);
  auto leader_job = std::async(std::launch::async, [&] {
    return B2aLeaderSide(leader_bits, 1, params, tag);
  });
  auto client = B2aPartySide(client_bits, 1, params, tag);
  auto leader = leader_job.get();
  for (size_t row = 0; row < params.set_size; ++row) {
    const uint64_t expected =
        static_cast<uint64_t>((leader_bits[row] ^ client_bits[row]) & 1U);
    OTMPSI_ENFORCE(((leader.share[row] + client.share[row]) & 0xffU) == expected,
                 "B2A truth-table mismatch at row {}", row);
  }
}

void CheckLargeNPortTags() {
  OtmpsiParams params;
  params.parties = 127;
  const auto tags = ProtocolSocketTags(params);
  const size_t pairs = params.parties * (params.parties - 1) / 2;
  const size_t expected = 11 * (params.parties - 1) + 5 * pairs;
  OTMPSI_ENFORCE(tags.size() == expected,
               "Tree-X2A port-tag count mismatch: actual={} expected={}",
               tags.size(), expected);
  std::unordered_set<std::string> unique(tags.begin(), tags.end());
  OTMPSI_ENFORCE(unique.size() == tags.size(),
               "Tree-X2A port tags must be unique");
  OTMPSI_ENFORCE(tags.size() < 65536 - 1024,
               "Tree-X2A port map exceeds the TCP port space");
}

void CheckShardedSsoprfPortMap() {
  OtmpsiParams params;
  params.parties = 3;
  params.set_size = 17;
  params.threads = 2;
  params.base_port = 24800;

  const auto dir = std::filesystem::temp_directory_path() /
                   ("otmpsi_sharded_port_smoke_" +
                    std::to_string(static_cast<unsigned long>(::getpid())));
  std::filesystem::remove_all(dir);
  GeneratePortMapFiles(dir.string(), params);

  std::unordered_set<std::string> addresses;
  for (size_t party = 1; party < params.parties; ++party) {
    for (size_t shard = 0; shard < params.threads; ++shard) {
      const auto tag = MakeSocketTag(params.base_port, party, 0) + "/shard/" +
                       std::to_string(shard);
      std::ifstream in(dir / SocketTagPathName(tag));
      OTMPSI_ENFORCE(in.good(), "missing sharded SSOPRF port mapping {}", tag);
      std::string address;
      std::getline(in, address);
      OTMPSI_ENFORCE(!address.empty(), "empty SSOPRF port mapping {}", tag);
      addresses.insert(std::move(address));
    }
  }
  std::filesystem::remove_all(dir);
  OTMPSI_ENFORCE(addresses.size() ==
                     (params.parties - 1) * params.threads,
                 "concurrent SSOPRF shards must use distinct TCP ports");
}

std::vector<std::vector<uint64_t>> MakeSelectionShares(
    const std::vector<uint8_t> &selection, size_t parties) {
  std::vector<std::vector<uint64_t>> shares(
      parties, std::vector<uint64_t>(selection.size(), 0));
  for (size_t row = 0; row < selection.size(); ++row) {
    bool acc = false;
    for (size_t party = 0; party + 1 < parties; ++party) {
      const bool bit = ((row + 3 * party + 1) & 1U) != 0;
      shares[party][row] = static_cast<uint64_t>(bit);
      acc ^= bit;
    }
    shares[parties - 1][row] =
        static_cast<uint64_t>(acc ^ (selection[row] != 0));
  }
  return shares;
}

void CheckSelectorPattern(const std::vector<uint8_t> &selection,
                          uint16_t base_port, size_t expected_results,
                          std::vector<RoleStats> *stats_out) {
  OtmpsiParams params;
  params.parties = 4;
  params.set_size = selection.size();
  params.threshold = 2;
  params.base_port = base_port;

  std::vector<uint128_t> x0(params.set_size);
  std::vector<std::vector<uint128_t>> leader_shares(
      params.parties - 1, std::vector<uint128_t>(params.set_size));
  std::vector<std::vector<uint128_t>> party_shares(
      params.parties - 1, std::vector<uint128_t>(params.set_size));
  for (size_t row = 0; row < params.set_size; ++row) {
    x0[row] = static_cast<uint128_t>(row + 1);
    for (size_t party = 1; party < params.parties; ++party) {
      const bool member = ((row + party) % 3) != 0;
      const bool alpha = ((row * 5 + party) & 1U) != 0;
      leader_shares[party - 1][row] = static_cast<uint128_t>(alpha);
      party_shares[party - 1][row] =
          static_cast<uint128_t>(alpha ^ member);
    }
  }

  auto selection_shares = MakeSelectionShares(selection, params.parties);
  auto run = RunPrivateSelectorOt(selection_shares, x0, leader_shares,
                                  party_shares, params);
  OTMPSI_ENFORCE(run.results.size() == expected_results,
               "private-selector result count mismatch");
  size_t result_idx = 0;
  for (size_t row = 0; row < params.set_size; ++row) {
    if (selection[row] == 0) {
      continue;
    }
    std::vector<size_t> expected_holders{0};
    for (size_t party = 1; party < params.parties; ++party) {
      if (((row + party) % 3) != 0) {
        expected_holders.push_back(party);
      }
    }
    OTMPSI_ENFORCE(expected_holders.size() >= params.threshold,
                 "selector smoke selected a below-threshold row");
    OTMPSI_ENFORCE(run.results[result_idx].item == x0[row] &&
                     run.results[result_idx].holders == expected_holders,
                 "private-selector decoded holder mismatch");
    ++result_idx;
  }
  if (stats_out != nullptr) {
    *stats_out = std::move(run.client_stats);
  }
}

void CheckPrivateSelector() {
  constexpr size_t rows = 19;
  std::vector<uint8_t> none(rows, 0);
  std::vector<uint8_t> selected(rows, 0);
  size_t selected_count = 0;
  for (size_t row = 0; row < rows; ++row) {
    size_t count = 1;
    for (size_t party = 1; party < 4; ++party) {
      count += static_cast<size_t>(((row + party) % 3) != 0);
    }
    if (count >= 2) {
      selected[row] = 1;
      ++selected_count;
    }
  }

  std::vector<RoleStats> none_stats;
  std::vector<RoleStats> selected_stats;
  CheckSelectorPattern(none, 24800, 0, &none_stats);
  CheckSelectorPattern(selected, 24900, selected_count, &selected_stats);
  OTMPSI_ENFORCE(none_stats.size() == selected_stats.size(),
               "private-selector stats party count mismatch");
  for (size_t party = 1; party < none_stats.size(); ++party) {
    OTMPSI_ENFORCE(none_stats[party].comm_bytes ==
                     selected_stats[party].comm_bytes,
                 "private-selector communication depends on choices");
  }
}

} // namespace
} // namespace examples::otmpsi

int main() {
  examples::otmpsi::CheckB2aTruthTable();
  examples::otmpsi::CheckTreeX2aRuns();
  examples::otmpsi::CheckCombinedTreeGmwPool();
  examples::otmpsi::CheckLargeNPortTags();
  examples::otmpsi::CheckShardedSsoprfPortMap();
  examples::otmpsi::CheckPrivateSelector();
  std::cout << "tree_x2a_selector_smoke=ok\n";
  return 0;
}
