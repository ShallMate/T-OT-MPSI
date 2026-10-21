#include "otmpsi/ssbsmt_phase.h"

#include "otmpsi/band_okvs_codec.h"
#include "otmpsi/input_preparation.h"

namespace examples::otmpsi {

ShareWithStats SsBsmtLeader(const std::vector<uint128_t> &query,
                            size_t party_idx, const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  const auto hashed_query = HashSsBsmtInputs(query);
  auto out = SsBsmtLeaderHashed(hashed_query, party_idx, params);
  out.stats.time_s = SecondsSince(start);
  return out;
}

ShareWithStats SsBsmtLeaderHashed(const std::vector<uint128_t> &hashed_query,
                                size_t party_idx, const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  const std::string ssoprf_tag = MakeSocketTag(params.base_port, party_idx, 0);
  const std::string okvs_tag = MakeSocketTag(params.base_port, party_idx, 1);
  auto ssoprf = RunSsoprfReceiverRole(hashed_query, ssoprf_tag, params.threads);

  auto okvs_sock = AcquireLocalSocket(okvs_tag, 0);
  auto okvs = RecvOkvs(okvs_sock);
  macoro::sync_wait(okvs_sock.flush());
  const auto okvs_comm =
      static_cast<size_t>(okvs_sock.bytesSent() + okvs_sock.bytesReceived());

  OTMPSI_ENFORCE(okvs.has_value(), "SS-BSMT peer aborted: Band OKVS encoding failed");
  auto decoded = DecodeBandOkvs(*okvs, hashed_query);
  std::vector<uint128_t> q0(hashed_query.size(), 0);
  for (size_t i = 0; i < hashed_query.size(); ++i) {
    q0[i] = ssoprf.share[i] ^ decoded[i];
  }
  return {std::move(q0),
          RoleStats{SecondsSince(start), ssoprf.bytes + okvs_comm}};
}

ShareWithStats SsBsmtParty(const std::vector<uint128_t> &set, size_t query_size,
                           size_t party_idx, uint128_t key_seed,
                           const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  const std::string ssoprf_tag = MakeSocketTag(params.base_port, party_idx, 0);
  const std::string okvs_tag = MakeSocketTag(params.base_port, party_idx, 1);
  auto ssoprf =
      RunSsoprfSenderRole(query_size, key_seed, ssoprf_tag, params.threads);

  auto key = ExpandKey512FromSeed(key_seed);
  const auto hashed_set = HashSsBsmtInputs(set);
  auto values = EvalAltModPrf(key, hashed_set);
  auto okvs = EncodeBandOkvs(hashed_set, values);

  auto okvs_sock = AcquireLocalSocket(okvs_tag, 1);
  SendOkvs(okvs_sock, okvs);
  macoro::sync_wait(okvs_sock.flush());
  OTMPSI_ENFORCE(okvs.has_value(), "SS-BSMT aborted: Band OKVS encoding failed");
  const auto okvs_comm =
      static_cast<size_t>(okvs_sock.bytesSent() + okvs_sock.bytesReceived());
  return {std::move(ssoprf.share),
          RoleStats{SecondsSince(start), ssoprf.bytes + okvs_comm}};
}

} // namespace examples::otmpsi
