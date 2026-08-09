#pragma once

#include "otmpsi/otmpsi_types.h"
#include "otmpsi/tree_gmw_correlation_pool.h"

namespace examples::otmpsi {

struct TreeX2aMaskPrepRun {
  std::vector<MaskPrepShare> shares;
  RoleStats leader;
  RoleStats clients;
  std::vector<RoleStats> client_stats;
};

struct TreeX2aMaskPrepPartyRun {
  MaskPrepShare share;
  RoleStats stats;
};

struct TreeX2aWithGmwPrefixPartyRun {
  MaskPrepShare share;
  RoleStats tree_stats;
  std::vector<PrecomputedGmwPairPrefix> gmw_prefixes;
};

struct TreeX2aWithGmwPrefixRun {
  std::vector<MaskPrepShare> shares;
  std::vector<std::vector<PrecomputedGmwPairPrefix>> gmw_prefixes;
  RoleStats leader_tree;
  RoleStats clients_tree;
  std::vector<RoleStats> client_tree_stats;
};

size_t TreeX2aOtCountPerPair(size_t rows);

TreeX2aMaskPrepPartyRun RunTreeX2aMaskPrepParty(size_t party_id,
                                                const OtmpsiParams &params,
                                                const std::string &scope);

TreeX2aWithGmwPrefixPartyRun RunTreeX2aMaskPrepPartyWithGmwPrefix(
    size_t party_id, size_t gmw_correlations_per_pair,
    const OtmpsiParams &params, const std::string &scope);

TreeX2aMaskPrepRun RunTreeX2aMaskPrep(const OtmpsiParams &params,
                                      const std::string &scope);

TreeX2aWithGmwPrefixRun RunTreeX2aMaskPrepWithGmwPrefix(
    size_t gmw_correlations_per_pair, const OtmpsiParams &params,
    const std::string &scope);

void CheckTreeX2aMaskPrep(const std::vector<MaskPrepShare> &shares,
                          const OtmpsiParams &params);

} // namespace examples::otmpsi
