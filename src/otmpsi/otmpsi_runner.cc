#include "otmpsi/otmpsi_runner.h"

#include "otmpsi/band_okvs_params.h"
#include "otmpsi/public_t_compare.h"

namespace examples::otmpsi {
namespace {

void ValidateRunnableParams(const OtmpsiParams &params) {
  OTMPSI_ENFORCE(params.parties >= 2, "parties must be >= 2");
  OTMPSI_ENFORCE(params.parties <= kMaxPartiesForComparison,
               "fixed 8-bit comparison requires N < 128");
  OTMPSI_ENFORCE(params.threshold >= 1 && params.threshold <= params.parties,
               "threshold must satisfy 1 <= t <= N");
  if (params.target_output_size.has_value()) {
    OTMPSI_ENFORCE(*params.target_output_size <= params.set_size,
                 "target output size must not exceed set size");
    OTMPSI_ENFORCE(params.threshold > 1 ||
                     *params.target_output_size == params.set_size,
                 "threshold 1 makes every leader item an output; target "
                 "output size must equal set size");
  }
  OTMPSI_ENFORCE(!UsesTreeX2aRoute(params) || params.enable_b2a,
               "optimized Tree-X2A output requires B2A membership shares");
  OTMPSI_ENFORCE(!UsesTreeX2aRoute(params) ||
                   params.final_mask_backend == FinalMaskBackend::kArithmetic,
               "optimized Tree-X2A output requires --final-mask arithmetic");
  OTMPSI_ENFORCE(params.enable_b2a ||
                   params.final_mask_backend != FinalMaskBackend::kArithmetic,
               "arithmetic final mask requires B2A");
  OTMPSI_ENFORCE(params.final_output_mode == FinalOutputMode::kLeaderOnly ||
                   (params.enable_b2a &&
                    params.final_mask_backend == FinalMaskBackend::kArithmetic),
               "client-output-basis mode requires B2A + arithmetic final mask");
  (void)ChooseBandOkvsParams(params.set_size);
}

size_t TreeGmwCorrelationsPerPair(const OtmpsiParams &params) {
  const size_t compare_word_count = (params.set_size + 127) / 128;
  return kPublicTCompareAndGates * compare_word_count * 2;
}

} // namespace

OtmpsiRun RunOnePartyProcessWithStats(size_t party_id,
                                      const OtmpsiParams &params) {
  ValidateRunnableParams(params);
  OTMPSI_ENFORCE(party_id < params.parties, "party id out of range");
  const bool threshold_two_relaxed_fast =
      UsesThresholdTwoRelaxedFastPath(params);
  const bool tree_x2a = UsesTreeX2aRoute(params);
  const bool legacy_relaxed_open =
      !tree_x2a && !threshold_two_relaxed_fast &&
      params.final_output_mode == FinalOutputMode::kClientOutputBasis;
  const bool needs_b2a = params.enable_b2a && !threshold_two_relaxed_fast;
  OTMPSI_ENFORCE((params.enable_b2a &&
                params.final_mask_backend == FinalMaskBackend::kArithmetic) ||
                   threshold_two_relaxed_fast,
               "one-process-per-party mode currently supports the main route: "
               "optimized standard output or relaxed output");

  const auto x0 = BuildLeaderSet(params.set_size);
  OtmpsiStats stats;
  stats.client_ssbsmt.resize(params.parties);
  stats.client_zero.resize(params.parties);
  stats.client_b2a.resize(params.parties);
  stats.client_nparty_gmw.resize(params.parties);
  const auto protocol_start = std::chrono::steady_clock::now();

  std::vector<std::vector<uint128_t>> leader_shares(params.parties - 1);
  std::vector<std::vector<uint128_t>> leader_bit_shares(params.parties - 1);
  std::vector<std::vector<uint64_t>> leader_arith_shares(params.parties - 1);
  std::vector<uint128_t> party_share;
  std::vector<uint128_t> party_bit_share;
  std::vector<uint64_t> party_arith_share;
  std::future<TreeX2aWithGmwPrefixPartyRun> tree_mask_prep_job;
  if (tree_x2a) {
    const size_t correlations_per_pair = TreeGmwCorrelationsPerPair(params);
    tree_mask_prep_job = std::async(
        std::launch::async, [params, party_id, correlations_per_pair] {
          return RunTreeX2aMaskPrepPartyWithGmwPrefix(
              party_id, correlations_per_pair, params,
              "multiprocess-tree-x2a-edabit");
        });
  }

  if (party_id == 0 && EnvEnabled("OTMPSI_PIPELINE_2PC")) {
    struct LeaderTwoPartyPipelineOutput {
      std::vector<uint128_t> share;
      std::vector<uint128_t> bit_share;
      std::vector<uint64_t> arithmetic_share;
      RoleStats ssbsmt;
      RoleStats zero;
      RoleStats b2a;
    };

    LogDebug("multiprocess", "party=0 two-party pipeline begin");
    std::vector<std::future<LeaderTwoPartyPipelineOutput>> jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      jobs[party] = std::async(std::launch::async, [&, party] {
        LeaderTwoPartyPipelineOutput out;
        auto ssbsmt = SsBsmtLeader(x0, party, params);
        out.ssbsmt = ssbsmt.stats;
        out.share = std::move(ssbsmt.share);

        const auto zero_tag = MakeSocketTag(params.base_port, party, 2);
        auto zero = PairwiseZeroSide(out.share, party, 0, params, zero_tag);
        out.zero = zero.stats;
        out.bit_share = std::move(zero.share);

        if (needs_b2a) {
          const auto b2a_tag = MakeSocketTag(params.base_port, party, 3);
          auto b2a = B2aLeaderSide(out.bit_share, party, params, b2a_tag);
          out.b2a = b2a.stats;
          out.arithmetic_share = std::move(b2a.share);
        }
        return out;
      });
    }

    for (size_t party = 1; party < params.parties; ++party) {
      auto out = jobs[party].get();
      stats.ssbsmt.leader.comm_bytes += out.ssbsmt.comm_bytes;
      stats.pairwise_zero.leader.comm_bytes += out.zero.comm_bytes;
      stats.b2a.leader.comm_bytes += out.b2a.comm_bytes;
      stats.ssbsmt.leader.time_s =
          std::max(stats.ssbsmt.leader.time_s, out.ssbsmt.time_s);
      stats.pairwise_zero.leader.time_s =
          std::max(stats.pairwise_zero.leader.time_s, out.zero.time_s);
      stats.b2a.leader.time_s =
          std::max(stats.b2a.leader.time_s, out.b2a.time_s);
      leader_shares[party - 1] = std::move(out.share);
      leader_bit_shares[party - 1] = std::move(out.bit_share);
      leader_arith_shares[party - 1] = std::move(out.arithmetic_share);
    }
    stats.ssbsmt.time_s = stats.ssbsmt.leader.time_s;
    stats.pairwise_zero.time_s = stats.pairwise_zero.leader.time_s;
    stats.b2a.time_s = stats.b2a.leader.time_s;
    if (!needs_b2a) {
      LogDebug("multiprocess", "party=0 B2A skipped for boolean output");
    }
  } else {
    LogDebug("multiprocess",
             "party=" + std::to_string(party_id) + " SS-BSMT begin");
    const auto ssbsmt_start = std::chrono::steady_clock::now();
    if (party_id == 0) {
      std::vector<std::future<ShareWithStats>> jobs(params.parties);
      for (size_t party = 1; party < params.parties; ++party) {
        jobs[party] = std::async(std::launch::async, [&, party] {
          return SsBsmtLeader(x0, party, params);
        });
      }
      for (size_t party = 1; party < params.parties; ++party) {
        auto out = jobs[party].get();
        stats.ssbsmt.leader.comm_bytes += out.stats.comm_bytes;
        leader_shares[party - 1] = std::move(out.share);
      }
      stats.ssbsmt.leader.time_s = SecondsSince(ssbsmt_start);
    } else {
      const uint128_t key_seed = BlockToU128(osuCrypto::sysRandomSeed());
      auto set = BuildPartySet(party_id, params, x0);
      auto out = SsBsmtParty(set, x0.size(), party_id, key_seed, params);
      party_share = std::move(out.share);
      stats.client_ssbsmt[party_id] = out.stats;
      stats.ssbsmt.clients = out.stats;
    }
    stats.ssbsmt.time_s = SecondsSince(ssbsmt_start);

    LogDebug("multiprocess",
             "party=" + std::to_string(party_id) + " pairwise zero begin");
    const auto zero_start = std::chrono::steady_clock::now();
    if (party_id == 0) {
      std::vector<std::future<ShareWithStats>> jobs(params.parties);
      for (size_t party = 1; party < params.parties; ++party) {
        const std::string tag = MakeSocketTag(params.base_port, party, 2);
        jobs[party] = std::async(std::launch::async, [&, party, tag] {
          return PairwiseZeroSide(leader_shares[party - 1], party, 0, params,
                                  tag);
        });
      }
      for (size_t party = 1; party < params.parties; ++party) {
        auto out = jobs[party].get();
        stats.pairwise_zero.leader.comm_bytes += out.stats.comm_bytes;
        leader_bit_shares[party - 1] = std::move(out.share);
      }
      stats.pairwise_zero.leader.time_s = SecondsSince(zero_start);
    } else {
      const std::string tag = MakeSocketTag(params.base_port, party_id, 2);
      auto out = PairwiseZeroSide(party_share, party_id, 1, params, tag);
      party_bit_share = std::move(out.share);
      stats.client_zero[party_id] = out.stats;
      stats.pairwise_zero.clients = out.stats;
    }
    stats.pairwise_zero.time_s = SecondsSince(zero_start);

    if (needs_b2a) {
      LogDebug("multiprocess",
               "party=" + std::to_string(party_id) + " B2A begin");
      const auto b2a_start = std::chrono::steady_clock::now();
      if (party_id == 0) {
        std::vector<std::future<B2aShareWithStats>> jobs(params.parties);
        for (size_t party = 1; party < params.parties; ++party) {
          const std::string tag = MakeSocketTag(params.base_port, party, 3);
          jobs[party] = std::async(std::launch::async, [&, party, tag] {
            return B2aLeaderSide(leader_bit_shares[party - 1], party, params,
                                 tag);
          });
        }
        for (size_t party = 1; party < params.parties; ++party) {
          auto out = jobs[party].get();
          stats.b2a.leader.comm_bytes += out.stats.comm_bytes;
          leader_arith_shares[party - 1] = std::move(out.share);
        }
        stats.b2a.leader.time_s = SecondsSince(b2a_start);
      } else {
        const std::string tag = MakeSocketTag(params.base_port, party_id, 3);
        auto out = B2aPartySide(party_bit_share, party_id, params, tag);
        party_arith_share = std::move(out.share);
        stats.client_b2a[party_id] = out.stats;
        stats.b2a.clients = out.stats;
      }
      stats.b2a.time_s = SecondsSince(b2a_start);
    } else {
      LogDebug("multiprocess", "party=" + std::to_string(party_id) +
                                   " B2A skipped for boolean output");
    }
  }

  LogDebug("multiprocess",
           "party=" + std::to_string(party_id) + " output phase begin");
  const auto nparty_start = std::chrono::steady_clock::now();
  std::vector<OtmpsiResult> final_results;
  RoleStats final_output_stats;
  if (threshold_two_relaxed_fast) {
    auto relaxed = RunThresholdTwoRelaxedOpenOutputParty(
        party_id, x0, leader_bit_shares, party_bit_share, params);
    final_output_stats = relaxed.stats;
    final_results = std::move(relaxed.results);
  } else if (legacy_relaxed_open) {
    auto relaxed = RunRelaxedOpenOutputParty(party_id, x0, leader_arith_shares,
                                             party_arith_share, params);
    final_output_stats = relaxed.stats;
    final_results = std::move(relaxed.results);
  } else if (tree_x2a) {
    std::vector<uint64_t> count_share =
        party_id == 0
            ? LeaderCountMinusThresholdShares(leader_arith_shares, params)
            : party_arith_share;
    OTMPSI_ENFORCE(tree_mask_prep_job.valid(),
                 "Tree-X2A preprocessing job was not started");
    auto mask_prep_run = tree_mask_prep_job.get();
    auto masked_share = MaskCountShare(
        count_share, mask_prep_run.share.arithmetic_share, params);
    auto opened = OpenAdditiveMaskedValues(party_id, masked_share, params);
    auto gmw_input = MakePublicTMaskedCompareInput(
        party_id, opened.opened, mask_prep_run.share.boolean_share, params);
    auto selection_share = RunNPartySharedOutputGmwParty(
        gmw_input, party_id, params, "multiprocess-tree-x2a-compare",
        BuildPublicTMaskedCompareCircuit(), 1,
        std::move(mask_prep_run.gmw_prefixes));
    const std::vector<uint128_t> no_party_share;
    RoleStats selected_stats;
    if (params.final_output_mode == FinalOutputMode::kClientOutputBasis) {
      auto selected = RunRelaxedSelectorOpenParty(
          party_id, selection_share.output_share, x0, leader_bit_shares,
          party_id == 0 ? no_party_share : party_bit_share, params);
      selected_stats = selected.stats;
      final_results = std::move(selected.results);
    } else {
      auto selected = RunPrivateSelectorOtParty(
          party_id, selection_share.output_share, x0, leader_bit_shares,
          party_id == 0 ? no_party_share : party_bit_share, params);
      selected_stats = selected.stats;
      final_results = std::move(selected.results);
    }
    LogDebug("tree-profile",
             "party=" + std::to_string(party_id) +
                 " mask_s=" + std::to_string(mask_prep_run.tree_stats.time_s) +
                 " mask_bytes=" +
                 std::to_string(mask_prep_run.tree_stats.comm_bytes) +
                 " open_s=" + std::to_string(opened.stats.time_s) +
                 " open_bytes=" + std::to_string(opened.stats.comm_bytes) +
                 " compare_s=" +
                 std::to_string(selection_share.stats.time_s) +
                 " compare_bytes=" +
                 std::to_string(selection_share.stats.comm_bytes) +
                 " output_s=" + std::to_string(selected_stats.time_s) +
                 " output_bytes=" +
                 std::to_string(selected_stats.comm_bytes));
    final_output_stats.comm_bytes =
        mask_prep_run.tree_stats.comm_bytes + opened.stats.comm_bytes +
        selection_share.stats.comm_bytes + selected_stats.comm_bytes;
    final_output_stats.time_s =
        mask_prep_run.tree_stats.time_s + opened.stats.time_s +
        selection_share.stats.time_s + selected_stats.time_s;
  } else {
    OTMPSI_THROW("unsupported OT-MPSI output route");
  }
  stats.nparty_gmw.time_s = SecondsSince(nparty_start);
  if (party_id == 0) {
    stats.nparty_gmw.leader.comm_bytes = final_output_stats.comm_bytes;
    stats.nparty_gmw.leader.time_s = stats.nparty_gmw.time_s;
  } else {
    RoleStats client_nparty;
    client_nparty.comm_bytes = final_output_stats.comm_bytes;
    client_nparty.time_s = stats.nparty_gmw.time_s;
    stats.client_nparty_gmw[party_id] = client_nparty;
    stats.nparty_gmw.clients = client_nparty;
  }

  const double done_s = SecondsSince(protocol_start);
  stats.total_time_s = done_s;
  if (party_id == 0) {
    stats.leader.comm_bytes =
        stats.ssbsmt.leader.comm_bytes + stats.pairwise_zero.leader.comm_bytes +
        stats.b2a.leader.comm_bytes + stats.nparty_gmw.leader.comm_bytes;
    stats.leader.time_s = done_s;
    CheckDemoResults(final_results, ExpectedDemoResults(params, x0));
  } else {
    stats.clients.comm_bytes = stats.ssbsmt.clients.comm_bytes +
                               stats.pairwise_zero.clients.comm_bytes +
                               stats.b2a.clients.comm_bytes +
                               stats.nparty_gmw.clients.comm_bytes;
    stats.clients.time_s = done_s;
  }
  return {std::move(final_results), std::move(stats)};
}

OtmpsiRun RunDemoWithStats(const OtmpsiParams &params) {
  ValidateRunnableParams(params);
  const bool threshold_two_relaxed_fast =
      UsesThresholdTwoRelaxedFastPath(params);
  const bool tree_x2a = UsesTreeX2aRoute(params);
  const bool legacy_relaxed_open =
      !tree_x2a && !threshold_two_relaxed_fast &&
      params.final_output_mode == FinalOutputMode::kClientOutputBasis;
  const bool needs_b2a = params.enable_b2a && !threshold_two_relaxed_fast;

  const auto x0 = BuildLeaderSet(params.set_size);
  std::vector<std::vector<uint128_t>> party_sets(params.parties);
  for (size_t party_idx = 1; party_idx < params.parties; ++party_idx) {
    party_sets[party_idx] = BuildPartySet(party_idx, params, x0);
  }

  const auto protocol_start = std::chrono::steady_clock::now();
  std::future<TreeX2aWithGmwPrefixRun> tree_mask_prep_job;
  if (tree_x2a) {
    const size_t correlations_per_pair = TreeGmwCorrelationsPerPair(params);
    tree_mask_prep_job = std::async(
        std::launch::async, [params, correlations_per_pair] {
          return RunTreeX2aMaskPrepWithGmwPrefix(
              correlations_per_pair, params, "tree-x2a-edabit");
        });
  }
  std::vector<std::vector<uint128_t>> leader_shares(params.parties - 1);
  std::vector<std::vector<uint128_t>> party_shares(params.parties - 1);
  std::vector<std::vector<uint128_t>> leader_bit_shares(params.parties - 1);
  std::vector<std::vector<uint128_t>> party_bit_shares(params.parties - 1);
  std::vector<std::vector<uint64_t>> leader_arith_shares(params.parties - 1);
  std::vector<std::vector<uint64_t>> party_arith_shares(params.parties - 1);
  std::vector<volePSI::Matrix<volePSI::u8>> gmw_inputs(params.parties);
  OtmpsiStats stats;
  stats.client_ssbsmt.resize(params.parties);
  stats.client_zero.resize(params.parties);
  stats.client_b2a.resize(params.parties);
  stats.client_nparty_gmw.resize(params.parties);
  std::vector<std::future<ShareWithStats>> leader_jobs(params.parties);
  std::vector<std::future<ShareWithStats>> party_jobs(params.parties);

  LogDebug("stage", "SS-BSMT begin");
  const auto ssbsmt_start = std::chrono::steady_clock::now();
  for (size_t party_idx = 1; party_idx < params.parties; ++party_idx) {
    const uint128_t key_seed = BlockToU128(osuCrypto::sysRandomSeed());
    party_jobs[party_idx] =
        std::async(std::launch::async, [&, party_idx, key_seed] {
          return SsBsmtParty(party_sets[party_idx], x0.size(), party_idx,
                             key_seed, params);
        });
    leader_jobs[party_idx] = std::async(std::launch::async, [&, party_idx] {
      return SsBsmtLeader(x0, party_idx, params);
    });
  }

  for (size_t party_idx = 1; party_idx < params.parties; ++party_idx) {
    auto leader_out = leader_jobs[party_idx].get();
    auto party_out = party_jobs[party_idx].get();
    stats.ssbsmt.leader.comm_bytes += leader_out.stats.comm_bytes;
    stats.ssbsmt.clients.comm_bytes += party_out.stats.comm_bytes;
    stats.client_ssbsmt[party_idx] = party_out.stats;
    leader_shares[party_idx - 1] = std::move(leader_out.share);
    party_shares[party_idx - 1] = std::move(party_out.share);
  }
  stats.ssbsmt.time_s = SecondsSince(ssbsmt_start);
  stats.ssbsmt.leader.time_s = stats.ssbsmt.time_s;
  stats.ssbsmt.clients.time_s = stats.ssbsmt.time_s;
  LogDebug("stage",
           "SS-BSMT done time_s=" + std::to_string(stats.ssbsmt.time_s));

  LogDebug("stage", "pairwise IS ZERO begin");
  const auto zero_start = std::chrono::steady_clock::now();
  std::vector<std::future<ShareWithStats>> leader_zero_jobs(params.parties);
  std::vector<std::future<ShareWithStats>> party_zero_jobs(params.parties);
  for (size_t party_idx = 1; party_idx < params.parties; ++party_idx) {
    const std::string zero_tag = MakeSocketTag(params.base_port, party_idx, 2);
    leader_zero_jobs[party_idx] =
        std::async(std::launch::async, [&, party_idx, zero_tag] {
          return PairwiseZeroSide(leader_shares[party_idx - 1], party_idx, 0,
                                  params, zero_tag);
        });
    party_zero_jobs[party_idx] =
        std::async(std::launch::async, [&, party_idx, zero_tag] {
          return PairwiseZeroSide(party_shares[party_idx - 1], party_idx, 1,
                                  params, zero_tag);
        });
  }

  for (size_t party_idx = 1; party_idx < params.parties; ++party_idx) {
    auto leader_zero = leader_zero_jobs[party_idx].get();
    auto party_zero = party_zero_jobs[party_idx].get();
    stats.pairwise_zero.leader.comm_bytes += leader_zero.stats.comm_bytes;
    stats.pairwise_zero.clients.comm_bytes += party_zero.stats.comm_bytes;
    stats.client_zero[party_idx] = party_zero.stats;
    leader_bit_shares[party_idx - 1] = std::move(leader_zero.share);
    party_bit_shares[party_idx - 1] = std::move(party_zero.share);
  }
  stats.pairwise_zero.time_s = SecondsSince(zero_start);
  stats.pairwise_zero.leader.time_s = stats.pairwise_zero.time_s;
  stats.pairwise_zero.clients.time_s = stats.pairwise_zero.time_s;
  LogDebug("stage", "pairwise IS ZERO done time_s=" +
                        std::to_string(stats.pairwise_zero.time_s));

  if (needs_b2a) {
    LogDebug("stage", "B2A begin");
    const auto b2a_start = std::chrono::steady_clock::now();
    std::vector<std::future<B2aShareWithStats>> leader_b2a_jobs(params.parties);
    std::vector<std::future<B2aShareWithStats>> party_b2a_jobs(params.parties);
    for (size_t party_idx = 1; party_idx < params.parties; ++party_idx) {
      const std::string b2a_tag = MakeSocketTag(params.base_port, party_idx, 3);
      leader_b2a_jobs[party_idx] =
          std::async(std::launch::async, [&, party_idx, b2a_tag] {
            return B2aLeaderSide(leader_bit_shares[party_idx - 1], party_idx,
                                 params, b2a_tag);
          });
      party_b2a_jobs[party_idx] =
          std::async(std::launch::async, [&, party_idx, b2a_tag] {
            return B2aPartySide(party_bit_shares[party_idx - 1], party_idx,
                                params, b2a_tag);
          });
    }

    for (size_t party_idx = 1; party_idx < params.parties; ++party_idx) {
      auto leader_b2a = leader_b2a_jobs[party_idx].get();
      auto party_b2a = party_b2a_jobs[party_idx].get();
      stats.b2a.leader.comm_bytes += leader_b2a.stats.comm_bytes;
      stats.b2a.clients.comm_bytes += party_b2a.stats.comm_bytes;
      stats.client_b2a[party_idx] = party_b2a.stats;
      leader_arith_shares[party_idx - 1] = std::move(leader_b2a.share);
      party_arith_shares[party_idx - 1] = std::move(party_b2a.share);
    }
    if (EnvEnabled("OTMPSI_VERIFY_B2A")) {
      CheckB2aMembershipShares(leader_bit_shares, party_bit_shares,
                               leader_arith_shares, party_arith_shares, params);
    }
    stats.b2a.time_s = SecondsSince(b2a_start);
    stats.b2a.leader.time_s = stats.b2a.time_s;
    stats.b2a.clients.time_s = stats.b2a.time_s;
    LogDebug("stage", "B2A done time_s=" + std::to_string(stats.b2a.time_s));
  } else if (params.enable_b2a) {
    LogDebug("stage", "B2A skipped for boolean output");
  }

  LogDebug("stage", "output phase begin");
  const auto nparty_start = std::chrono::steady_clock::now();
  MaskedCountCompareData masked_count_data;
  TreeX2aWithGmwPrefixRun tree_mask_prep_run;
  if (tree_x2a) {
    LogDebug("n-party-gmw", "Tree-X2A masked count prep begin");
    OTMPSI_ENFORCE(tree_mask_prep_job.valid(),
                 "Tree-X2A preprocessing job was not started");
    tree_mask_prep_run = tree_mask_prep_job.get();
    masked_count_data = MakeMaskedCountCompareData(
        leader_arith_shares, party_arith_shares, tree_mask_prep_run.shares,
        params);
    if (EnvEnabled("OTMPSI_VERIFY_MASKED_COMPARE")) {
      CheckMaskedCountCompareData(masked_count_data, leader_arith_shares,
                                  party_arith_shares, params);
    }
    for (size_t party = 0; party < params.parties; ++party) {
      gmw_inputs[party] = MakePublicTMaskedCompareInput(
          party, masked_count_data.opened_masked_values,
          tree_mask_prep_run.shares[party].boolean_share, params);
    }
    LogDebug("n-party-gmw", "Tree-X2A masked count prep done");
  }

  std::vector<OtmpsiResult> gmw_results;
  RoleStats gmw_leader;
  RoleStats gmw_clients;
  std::vector<RoleStats> gmw_client_stats;
  if (threshold_two_relaxed_fast) {
    auto relaxed = RunThresholdTwoRelaxedOpenOutput(x0, leader_bit_shares,
                                                    party_bit_shares, params);
    gmw_results = std::move(relaxed.results);
    gmw_leader = relaxed.leader;
    gmw_clients = relaxed.clients;
    gmw_client_stats = std::move(relaxed.client_stats);
  } else if (legacy_relaxed_open) {
    auto relaxed = RunRelaxedOpenOutput(x0, leader_arith_shares,
                                        party_arith_shares, params);
    gmw_results = std::move(relaxed.results);
    gmw_leader = relaxed.leader;
    gmw_clients = relaxed.clients;
    gmw_client_stats = std::move(relaxed.client_stats);
  } else if (tree_x2a) {
    auto selection = RunNPartySharedOutputGmwWithPreprocessing(
        gmw_inputs, std::move(tree_mask_prep_run.gmw_prefixes), params,
        "tree-x2a-compare", BuildPublicTMaskedCompareCircuit(), 1);
    RoleStats selected_leader;
    RoleStats selected_clients;
    std::vector<RoleStats> selected_client_stats;
    if (params.final_output_mode == FinalOutputMode::kClientOutputBasis) {
      auto selected = RunRelaxedSelectorOpen(
          selection.output_shares, x0, leader_bit_shares, party_bit_shares,
          params);
      gmw_results = std::move(selected.results);
      selected_leader = selected.leader;
      selected_clients = selected.clients;
      selected_client_stats = std::move(selected.client_stats);
    } else {
      auto selected = RunPrivateSelectorOt(
          selection.output_shares, x0, leader_bit_shares, party_bit_shares,
          params);
      gmw_results = std::move(selected.results);
      selected_leader = selected.leader;
      selected_clients = selected.clients;
      selected_client_stats = std::move(selected.client_stats);
    }
    gmw_leader = selection.leader;
    gmw_leader += tree_mask_prep_run.leader_tree;
    gmw_leader += selected_leader;
    gmw_clients = selection.clients;
    gmw_clients += tree_mask_prep_run.clients_tree;
    gmw_clients += selected_clients;
    gmw_client_stats.assign(params.parties, {});
    for (size_t party = 1; party < params.parties; ++party) {
      gmw_client_stats[party] = selection.client_stats[party];
      gmw_client_stats[party] += tree_mask_prep_run.client_tree_stats[party];
      gmw_client_stats[party] += selected_client_stats[party];
    }
    if (masked_count_data.open_comm_bytes.size() == params.parties) {
      gmw_leader.comm_bytes += masked_count_data.open_comm_bytes[0];
      for (size_t party = 1; party < params.parties; ++party) {
        gmw_client_stats[party].comm_bytes +=
            masked_count_data.open_comm_bytes[party];
        gmw_clients.comm_bytes += masked_count_data.open_comm_bytes[party];
      }
    }
  } else {
    OTMPSI_THROW("unsupported OT-MPSI output route");
  }
  stats.nparty_gmw.time_s = SecondsSince(nparty_start);
  stats.nparty_gmw.leader = gmw_leader;
  stats.nparty_gmw.clients = gmw_clients;
  if (gmw_client_stats.size() == params.parties) {
    stats.client_nparty_gmw = std::move(gmw_client_stats);
  }
  stats.nparty_gmw.leader.time_s = stats.nparty_gmw.time_s;
  stats.nparty_gmw.clients.time_s = stats.nparty_gmw.time_s;
  LogDebug("stage", "N-party GMW done time_s=" +
                        std::to_string(stats.nparty_gmw.time_s));

  const double done_s = SecondsSince(protocol_start);
  stats.leader.comm_bytes =
      stats.ssbsmt.leader.comm_bytes + stats.pairwise_zero.leader.comm_bytes +
      stats.b2a.leader.comm_bytes + stats.nparty_gmw.leader.comm_bytes;
  stats.clients.comm_bytes =
      stats.ssbsmt.clients.comm_bytes + stats.pairwise_zero.clients.comm_bytes +
      stats.b2a.clients.comm_bytes + stats.nparty_gmw.clients.comm_bytes;
  stats.leader.time_s = done_s;
  stats.clients.time_s = done_s;
  stats.total_time_s = done_s;

  auto out = std::move(gmw_results);
  CheckDemoResults(out, ExpectedDemoResults(params, x0));
  return {std::move(out), std::move(stats)};
}

std::vector<OtmpsiResult> RunDemo(const OtmpsiParams &params) {
  return RunDemoWithStats(params).results;
}

} // namespace examples::otmpsi
