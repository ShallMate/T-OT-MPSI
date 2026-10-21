#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

ShareWithStats SsBsmtLeader(const std::vector<uint128_t> &query,
                            size_t party_idx, const OtmpsiParams &params);
// Reuse H(X0^pi) across clients; inputs must already be HashSsBsmtInputs output.
ShareWithStats SsBsmtLeaderHashed(const std::vector<uint128_t> &hashed_query,
                                size_t party_idx, const OtmpsiParams &params);
ShareWithStats SsBsmtParty(const std::vector<uint128_t> &set,
                           size_t query_size, size_t party_idx,
                           uint128_t key_seed, const OtmpsiParams &params);

} // namespace examples::otmpsi
