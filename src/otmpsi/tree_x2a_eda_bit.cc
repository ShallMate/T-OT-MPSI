#include "otmpsi/tree_x2a_eda_bit.h"

#include <limits>

#include "otmpsi/silent_ot_utils.h"
#include "otmpsi/z256_bit_mul_ot.h"

namespace examples::otmpsi {
namespace {

constexpr size_t kRingBits = kComparisonBits;
constexpr uint16_t kRingMask = (uint16_t{1} << kRingBits) - 1;
constexpr size_t kTreeMulChannel = 14;
constexpr size_t kTreeRefreshChannel = 15;
constexpr size_t kDefaultItemsPerChunk = size_t{1} << 18;

static_assert(kRingBits == 8);

uint8_t AddRing(uint8_t lhs, uint8_t rhs) {
  return static_cast<uint8_t>((static_cast<uint16_t>(lhs) + rhs) & kRingMask);
}

uint8_t SubRing(uint8_t lhs, uint8_t rhs) {
  return static_cast<uint8_t>(
      (static_cast<uint16_t>(lhs) + (uint16_t{1} << kRingBits) - rhs) &
      kRingMask);
}

size_t PairId(size_t lhs, size_t rhs, size_t parties) {
  OTMPSI_ENFORCE(lhs < rhs && rhs < parties, "invalid Tree-X2A party pair");
  return lhs * (2 * parties - lhs - 1) / 2 + (rhs - lhs - 1);
}

size_t ItemsPerChunk() {
  const char *env = std::getenv("OTMPSI_TREE_X2A_ITEMS_PER_CHUNK");
  if (env == nullptr || env[0] == '\0') {
    return kDefaultItemsPerChunk;
  }
  const auto value = static_cast<size_t>(std::stoull(env));
  OTMPSI_ENFORCE(value > 0, "OTMPSI_TREE_X2A_ITEMS_PER_CHUNK must be positive");
  return value;
}

struct PairBitOtState {
  bool sender_role = false;
  bool base_ots_ready = false;
  std::unique_ptr<coproto::Socket> socket;
  std::unique_ptr<oc::SilentOtExtSender> sender;
  std::unique_ptr<oc::SilentOtExtReceiver> receiver;
  std::unique_ptr<oc::PRNG> prng;
  Z256BitMulOtSenderBatch sender_batch;
  Z256BitMulOtReceiverBatch receiver_batch;
};

struct LiveTreeGmwPairPool {
  TreeGmwPairCorrelationPool correlations;
  std::unique_ptr<coproto::Socket> socket;
};

void PrecomputePairBitOtChunk(PairBitOtState &state, size_t party_id,
                              size_t peer,
                              const std::vector<uint8_t> &local_boolean_bits,
                              size_t item_begin, size_t item_count,
                              const OtmpsiParams &params) {
  const size_t lo = std::min(party_id, peer);
  const size_t hi = std::max(party_id, peer);
  state.sender_role = party_id == lo;
  if (!state.socket) {
    const auto tag =
        MakeSocketTag(params.base_port, 1000 + PairId(lo, hi, params.parties),
                      kTreeMulChannel);
    state.socket = std::make_unique<coproto::Socket>(
        AcquireLocalSocket(tag, state.sender_role ? 0 : 1));
    state.prng = std::make_unique<oc::PRNG>(oc::sysRandomSeed());
  }

  if (state.sender_role) {
    if (!state.sender) {
      state.sender = std::make_unique<oc::SilentOtExtSender>();
    }
    ConfigureSilentOtSender(*state.sender, item_count, params.threads);
    if (!state.base_ots_ready) {
      macoro::sync_wait(state.sender->genBaseOts(*state.prng, *state.socket));
      state.base_ots_ready = true;
    }
    state.sender_batch = PrecomputeZ256BitMulOtSender(
        *state.sender, item_count, *state.prng, *state.socket);
    return;
  }

  if (!state.receiver) {
    state.receiver = std::make_unique<oc::SilentOtExtReceiver>();
  }
  ConfigureSilentOtReceiver(*state.receiver, item_count, params.threads);
  if (!state.base_ots_ready) {
    macoro::sync_wait(state.receiver->genBaseOts(*state.prng, *state.socket));
    state.base_ots_ready = true;
  }
  state.receiver_batch = PrecomputeZ256BitMulOtReceiver(
      *state.receiver,
      std::span<const uint8_t>(local_boolean_bits.data() + item_begin,
                               item_count),
      *state.prng, *state.socket);
}

size_t ClosePairBitOtState(PairBitOtState &state) {
  if (!state.socket) {
    return 0;
  }
  macoro::sync_wait(state.socket->flush());
  const size_t bytes = static_cast<size_t>(state.socket->bytesSent() +
                                           state.socket->bytesReceived());
  macoro::sync_wait(state.socket->close());
  state.socket.reset();
  return bytes;
}

size_t RefreshAggregateShare(size_t party_id, std::vector<uint64_t> &share,
                             const OtmpsiParams &params) {
  size_t comm_bytes = 0;
  struct PairRefresh {
    std::vector<uint8_t> delta;
    size_t comm_bytes = 0;
  };
  std::vector<std::future<PairRefresh>> jobs(params.parties);
  for (size_t peer = 0; peer < params.parties; ++peer) {
    if (peer == party_id) {
      continue;
    }
    jobs[peer] = std::async(std::launch::async, [&, peer] {
      const size_t lo = std::min(party_id, peer);
      const size_t hi = std::max(party_id, peer);
      const bool sender_role = party_id == lo;
      const auto tag = MakeSocketTag(
          params.base_port, 1000 + PairId(lo, hi, params.parties),
          kTreeRefreshChannel);
      auto sock = AcquireLocalSocket(tag, sender_role ? 0 : 1);
      PairRefresh result;
      result.delta.resize(params.set_size);
      if (sender_role) {
        oc::PRNG prng(oc::sysRandomSeed());
        for (auto &value : result.delta) {
          value = static_cast<uint8_t>(BlockToU64(prng.get()) & kRingMask);
        }
        macoro::sync_wait(sock.send(coproto::span<const uint8_t>(
            result.delta.data(), result.delta.size())));
      } else {
        macoro::sync_wait(sock.recv(coproto::span<uint8_t>(
            result.delta.data(), result.delta.size())));
      }
      macoro::sync_wait(sock.flush());
      result.comm_bytes =
          static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
      macoro::sync_wait(sock.close());
      return result;
    });
  }

  for (size_t peer = 0; peer < params.parties; ++peer) {
    if (peer == party_id) {
      continue;
    }
    auto result = jobs[peer].get();
    comm_bytes += result.comm_bytes;
    for (size_t row = 0; row < params.set_size; ++row) {
      share[row] = party_id < peer
                       ? AddRing(static_cast<uint8_t>(share[row]),
                                 result.delta[row])
                       : SubRing(static_cast<uint8_t>(share[row]),
                                 result.delta[row]);
    }
  }
  return comm_bytes;
}

} // namespace

size_t TreeX2aOtCountPerPair(size_t rows) {
  OTMPSI_ENFORCE(rows <= std::numeric_limits<size_t>::max() / kRingBits,
               "Tree-X2A OT count overflow");
  return rows * kRingBits;
}

TreeX2aMaskPrepPartyRun RunTreeX2aMaskPrepParty(size_t party_id,
                                                const OtmpsiParams &params,
                                                const std::string &scope) {
  const auto start = std::chrono::steady_clock::now();
  OTMPSI_ENFORCE(party_id < params.parties, "Tree-X2A party index out of range");
  OTMPSI_ENFORCE(CompareBitsForPartyCount(params.parties) == kRingBits,
               "Tree-X2A requires fixed 8-bit comparison");

  TreeX2aMaskPrepPartyRun run;
  run.share.boolean_share.assign(params.set_size, 0);
  std::vector<uint8_t> arithmetic_bit_share(params.set_size * kRingBits, 0);
  oc::PRNG share_prng(oc::sysRandomSeed());
  for (size_t row = 0; row < params.set_size; ++row) {
    const uint8_t beta =
        static_cast<uint8_t>(BlockToU64(share_prng.get()) & kRingMask);
    run.share.boolean_share[row] = beta;
    for (size_t bit = 0; bit < kRingBits; ++bit) {
      arithmetic_bit_share[row * kRingBits + bit] =
          static_cast<uint8_t>((beta >> bit) & 1U);
    }
  }
  const std::vector<uint8_t> local_boolean_bits = arithmetic_bit_share;

  size_t comm_bytes = 0;
  std::vector<PairBitOtState> pair_states(params.parties);
  const size_t items_per_chunk = ItemsPerChunk();
  for (size_t item_begin = 0; item_begin < arithmetic_bit_share.size();
       item_begin += items_per_chunk) {
    const size_t item_end =
        std::min(arithmetic_bit_share.size(), item_begin + items_per_chunk);
    const size_t item_count = item_end - item_begin;

    // Silent-OT generation is input-independent. Prepare every pair in
    // parallel, then keep only the one-byte online corrections on the
    // dependency chain below.
    std::vector<std::future<void>> precompute_jobs(params.parties);
    for (size_t peer = 0; peer < params.parties; ++peer) {
      if (peer == party_id) {
        continue;
      }
      precompute_jobs[peer] = std::async(std::launch::async, [&, peer] {
        PrecomputePairBitOtChunk(pair_states[peer], party_id, peer,
                                 local_boolean_bits, item_begin, item_count,
                                 params);
      });
    }
    for (size_t peer = 0; peer < params.parties; ++peer) {
      if (peer != party_id) {
        precompute_jobs[peer].get();
      }
    }

    // Merge one new party at a time. The new party still holds a local bit,
    // so each old/new pair needs one OT per edaBit bit.
    for (size_t new_party = 1; new_party < params.parties; ++new_party) {
      if (party_id > new_party) {
        continue;
      }

      if (party_id < new_party) {
        auto product = SendZ256BitMulShares(
            pair_states[new_party].sender_batch,
            std::span<const uint8_t>(arithmetic_bit_share.data() + item_begin,
                                     item_count),
            *pair_states[new_party].socket);
        for (size_t local_item = 0; local_item < item_count; ++local_item) {
          const size_t item = item_begin + local_item;
          arithmetic_bit_share[item] =
              SubRing(arithmetic_bit_share[item],
                      static_cast<uint8_t>((uint16_t{2} * product[local_item]) &
                                           kRingMask));
        }
        continue;
      }

      std::vector<std::future<std::vector<uint8_t>>> receive_jobs(new_party);
      for (size_t peer = 0; peer < new_party; ++peer) {
        receive_jobs[peer] = std::async(std::launch::async, [&, peer] {
          return ReceiveZ256BitMulShares(pair_states[peer].receiver_batch,
                                         *pair_states[peer].socket);
        });
      }
      std::vector<uint8_t> product_sum(item_count, 0);
      for (size_t peer = 0; peer < new_party; ++peer) {
        auto product = receive_jobs[peer].get();
        for (size_t local_item = 0; local_item < item_count; ++local_item) {
          product_sum[local_item] =
              AddRing(product_sum[local_item], product[local_item]);
        }
      }
      for (size_t local_item = 0; local_item < item_count; ++local_item) {
        const size_t item = item_begin + local_item;
        arithmetic_bit_share[item] =
            SubRing(arithmetic_bit_share[item],
                    static_cast<uint8_t>(
                        (uint16_t{2} * product_sum[local_item]) & kRingMask));
      }
    }
  }

  for (size_t peer = 0; peer < params.parties; ++peer) {
    if (peer != party_id) {
      comm_bytes += ClosePairBitOtState(pair_states[peer]);
    }
  }

  run.share.arithmetic_share.assign(params.set_size, 0);
  for (size_t row = 0; row < params.set_size; ++row) {
    uint8_t aggregate = 0;
    for (size_t bit = 0; bit < kRingBits; ++bit) {
      aggregate = AddRing(
          aggregate,
          static_cast<uint8_t>((static_cast<uint16_t>(
                                    arithmetic_bit_share[row * kRingBits + bit])
                                << bit) &
                               kRingMask));
    }
    run.share.arithmetic_share[row] = aggregate;
  }

  comm_bytes +=
      RefreshAggregateShare(party_id, run.share.arithmetic_share, params);
  run.stats = RoleStats{SecondsSince(start), comm_bytes};
  LogDebug(scope, "party=" + std::to_string(party_id) + " Tree-X2A OTs=" +
                      std::to_string((params.parties - 1) *
                                     TreeX2aOtCountPerPair(params.set_size)));
  return run;
}

TreeX2aWithGmwPrefixPartyRun RunTreeX2aMaskPrepPartyWithGmwPrefix(
    size_t party_id, size_t gmw_correlations_per_pair,
    const OtmpsiParams &params, const std::string &scope) {
  const auto start = std::chrono::steady_clock::now();
  OTMPSI_ENFORCE(party_id < params.parties,
               "combined Tree-X2A party index out of range");
  OTMPSI_ENFORCE(CompareBitsForPartyCount(params.parties) == kRingBits,
               "combined Tree-X2A requires fixed 8-bit comparison");
  OTMPSI_ENFORCE(gmw_correlations_per_pair > 0,
               "combined Tree/GMW pool requires GMW correlations");

  TreeX2aWithGmwPrefixPartyRun run;
  run.share.boolean_share.assign(params.set_size, 0);
  std::vector<uint8_t> arithmetic_bit_share(params.set_size * kRingBits, 0);
  oc::PRNG share_prng(oc::sysRandomSeed());
  for (size_t row = 0; row < params.set_size; ++row) {
    const uint8_t beta =
        static_cast<uint8_t>(BlockToU64(share_prng.get()) & kRingMask);
    run.share.boolean_share[row] = beta;
    for (size_t bit = 0; bit < kRingBits; ++bit) {
      arithmetic_bit_share[row * kRingBits + bit] =
          static_cast<uint8_t>((beta >> bit) & 1U);
    }
  }
  const std::vector<uint8_t> local_boolean_bits = arithmetic_bit_share;

  std::vector<LiveTreeGmwPairPool> pools(params.parties);
  std::vector<std::future<LiveTreeGmwPairPool>> precompute_jobs(
      params.parties);
  for (size_t peer = 0; peer < params.parties; ++peer) {
    if (peer == party_id) {
      continue;
    }
    precompute_jobs[peer] = std::async(std::launch::async, [&, peer] {
      const size_t lo = std::min(party_id, peer);
      const size_t hi = std::max(party_id, peer);
      const bool sender_role = party_id == lo;
      const auto tag =
          MakeSocketTag(params.base_port,
                        1000 + PairId(lo, hi, params.parties), kTreeMulChannel);
      auto socket = std::make_unique<coproto::Socket>(
          AcquireLocalSocket(tag, sender_role ? 0 : 1));
      auto correlations = PrecomputeTreeGmwPairCorrelationPool(
          peer, sender_role, gmw_correlations_per_pair, local_boolean_bits,
          params.threads, oc::sysRandomSeed(), *socket);
      return LiveTreeGmwPairPool{std::move(correlations), std::move(socket)};
    });
  }
  for (size_t peer = 0; peer < params.parties; ++peer) {
    if (peer != party_id) {
      pools[peer] = precompute_jobs[peer].get();
    }
  }

  size_t tree_online_comm = 0;
  for (size_t new_party = 1; new_party < params.parties; ++new_party) {
    if (party_id > new_party) {
      continue;
    }

    if (party_id < new_party) {
      auto &live_pool = pools[new_party];
      OTMPSI_ENFORCE(live_pool.socket != nullptr,
                   "combined Tree/GMW sender socket missing");
      auto &sock = *live_pool.socket;
      const size_t comm_before =
          static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
      auto product = SendZ256BitMulShares(
          live_pool.correlations.tree_sender, arithmetic_bit_share, sock);
      for (size_t item = 0; item < arithmetic_bit_share.size(); ++item) {
        arithmetic_bit_share[item] =
            SubRing(arithmetic_bit_share[item],
                    static_cast<uint8_t>((uint16_t{2} * product[item]) &
                                         kRingMask));
      }
      macoro::sync_wait(sock.flush());
      tree_online_comm += static_cast<size_t>(sock.bytesSent() +
                                              sock.bytesReceived()) -
                          comm_before;
      macoro::sync_wait(sock.close());
      live_pool.socket.reset();
      continue;
    }

    struct ReceivedProduct {
      std::vector<uint8_t> share;
      size_t comm_bytes = 0;
    };
    std::vector<std::future<ReceivedProduct>> receive_jobs(new_party);
    for (size_t peer = 0; peer < new_party; ++peer) {
      receive_jobs[peer] = std::async(std::launch::async, [&, peer] {
        auto &live_pool = pools[peer];
        OTMPSI_ENFORCE(live_pool.socket != nullptr,
                     "combined Tree/GMW receiver socket missing");
        auto &sock = *live_pool.socket;
        const size_t comm_before =
            static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
        ReceivedProduct received;
        received.share = ReceiveZ256BitMulShares(
            live_pool.correlations.tree_receiver, sock);
        macoro::sync_wait(sock.flush());
        received.comm_bytes =
            static_cast<size_t>(sock.bytesSent() + sock.bytesReceived()) -
            comm_before;
        macoro::sync_wait(sock.close());
        live_pool.socket.reset();
        return received;
      });
    }
    std::vector<uint8_t> product_sum(arithmetic_bit_share.size(), 0);
    for (size_t peer = 0; peer < new_party; ++peer) {
      auto received = receive_jobs[peer].get();
      tree_online_comm += received.comm_bytes;
      for (size_t item = 0; item < product_sum.size(); ++item) {
        product_sum[item] = AddRing(product_sum[item], received.share[item]);
      }
    }
    for (size_t item = 0; item < arithmetic_bit_share.size(); ++item) {
      arithmetic_bit_share[item] =
          SubRing(arithmetic_bit_share[item],
                  static_cast<uint8_t>((uint16_t{2} * product_sum[item]) &
                                       kRingMask));
    }
  }

  run.share.arithmetic_share.assign(params.set_size, 0);
  for (size_t row = 0; row < params.set_size; ++row) {
    uint8_t aggregate = 0;
    for (size_t bit = 0; bit < kRingBits; ++bit) {
      aggregate = AddRing(
          aggregate,
          static_cast<uint8_t>((static_cast<uint16_t>(
                                    arithmetic_bit_share[row * kRingBits + bit])
                                << bit) &
                               kRingMask));
    }
    run.share.arithmetic_share[row] = aggregate;
  }

  tree_online_comm +=
      RefreshAggregateShare(party_id, run.share.arithmetic_share, params);
  run.tree_stats = RoleStats{SecondsSince(start), tree_online_comm};
  run.gmw_prefixes.reserve(params.parties - 1);
  for (size_t peer = 0; peer < params.parties; ++peer) {
    if (peer != party_id) {
      run.gmw_prefixes.push_back(
          std::move(pools[peer].correlations.gmw_prefix));
    }
  }
  LogDebug(scope,
           "party=" + std::to_string(party_id) +
               " combined Tree/GMW raw OTs per peer=" +
               std::to_string(gmw_correlations_per_pair * 128 +
                              TreeX2aOtCountPerPair(params.set_size)));
  return run;
}

TreeX2aMaskPrepRun RunTreeX2aMaskPrep(const OtmpsiParams &params,
                                      const std::string &scope) {
  TreeX2aMaskPrepRun run;
  run.shares.resize(params.parties);
  run.client_stats.resize(params.parties);
  std::vector<std::future<TreeX2aMaskPrepPartyRun>> jobs(params.parties);
  for (size_t party = 0; party < params.parties; ++party) {
    jobs[party] = std::async(std::launch::async, [&, party] {
      return RunTreeX2aMaskPrepParty(party, params, scope);
    });
  }
  for (size_t party = 0; party < params.parties; ++party) {
    auto party_run = jobs[party].get();
    run.shares[party] = std::move(party_run.share);
    if (party == 0) {
      run.leader = party_run.stats;
    } else {
      run.client_stats[party] = party_run.stats;
      run.clients.comm_bytes += party_run.stats.comm_bytes;
      run.clients.time_s = std::max(run.clients.time_s, party_run.stats.time_s);
    }
  }
  if (EnvEnabled("OTMPSI_VERIFY_TREE_X2A")) {
    CheckTreeX2aMaskPrep(run.shares, params);
  }
  return run;
}

TreeX2aWithGmwPrefixRun RunTreeX2aMaskPrepWithGmwPrefix(
    size_t gmw_correlations_per_pair, const OtmpsiParams &params,
    const std::string &scope) {
  TreeX2aWithGmwPrefixRun run;
  run.shares.resize(params.parties);
  run.gmw_prefixes.resize(params.parties);
  run.client_tree_stats.resize(params.parties);
  std::vector<std::future<TreeX2aWithGmwPrefixPartyRun>> jobs(params.parties);
  for (size_t party = 0; party < params.parties; ++party) {
    jobs[party] = std::async(std::launch::async, [&, party] {
      return RunTreeX2aMaskPrepPartyWithGmwPrefix(
          party, gmw_correlations_per_pair, params, scope);
    });
  }
  for (size_t party = 0; party < params.parties; ++party) {
    auto party_run = jobs[party].get();
    run.shares[party] = std::move(party_run.share);
    run.gmw_prefixes[party] = std::move(party_run.gmw_prefixes);
    if (party == 0) {
      run.leader_tree = party_run.tree_stats;
    } else {
      run.client_tree_stats[party] = party_run.tree_stats;
      run.clients_tree.comm_bytes += party_run.tree_stats.comm_bytes;
      run.clients_tree.time_s =
          std::max(run.clients_tree.time_s, party_run.tree_stats.time_s);
    }
  }
  if (EnvEnabled("OTMPSI_VERIFY_TREE_X2A")) {
    CheckTreeX2aMaskPrep(run.shares, params);
  }
  return run;
}

void CheckTreeX2aMaskPrep(const std::vector<MaskPrepShare> &shares,
                          const OtmpsiParams &params) {
  OTMPSI_ENFORCE(shares.size() == params.parties,
               "Tree-X2A mask-prep party count mismatch");
  for (const auto &share : shares) {
    OTMPSI_ENFORCE(share.arithmetic_share.size() == params.set_size &&
                     share.boolean_share.size() == params.set_size,
                 "Tree-X2A mask-prep row count mismatch");
  }
  for (size_t row = 0; row < params.set_size; ++row) {
    uint8_t arithmetic = 0;
    uint8_t boolean = 0;
    for (size_t party = 0; party < params.parties; ++party) {
      arithmetic = AddRing(
          arithmetic, static_cast<uint8_t>(shares[party].arithmetic_share[row] &
                                           kRingMask));
      boolean ^=
          static_cast<uint8_t>(shares[party].boolean_share[row] & kRingMask);
    }
    OTMPSI_ENFORCE(arithmetic == boolean,
                 "Tree-X2A edaBit mismatch at row {}: arithmetic={} boolean={}",
                 row, arithmetic, boolean);
  }
}

} // namespace examples::otmpsi
