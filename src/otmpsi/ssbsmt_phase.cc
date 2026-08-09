#include "otmpsi/ssbsmt_phase.h"

#include "otmpsi/band_okvs_params.h"

namespace examples::otmpsi {

struct EncodedBandOkvs {
  size_t item_count = 0;
  int num_vars = 0;
  int band_length = 128;
  uint128_t seed = 0;
  std::vector<uint128_t> table;
};

EncodedBandOkvs EncodeBandOkvs(const std::vector<uint128_t> &keys,
                               const std::vector<uint128_t> &values,
                               uint128_t seed) {
  OTMPSI_ENFORCE(keys.size() == values.size(), "OKVS key/value size mismatch");
  OTMPSI_ENFORCE(keys.size() >= 1024,
               "Band OKVS backend requires at least 1024 items, got {}",
               keys.size());
  std::vector<oc::block> key_blocks(keys.size());
  std::vector<oc::block> value_blocks(values.size());
  for (size_t i = 0; i < keys.size(); ++i) {
    key_blocks[i] = U128ToBlock(keys[i]);
    value_blocks[i] = U128ToBlock(values[i]);
  }

  const auto okvs_params = ChooseBandOkvsParams(keys.size());
  LogDebug(
      "ssbsmt",
      "Band OKVS params: n=" + std::to_string(keys.size()) +
          ", fitted_n=" + std::to_string(okvs_params.fitted_item_count) +
          ", epsilon=" + std::to_string(okvs_params.epsilon) +
          ", m=" + std::to_string(okvs_params.num_vars) +
          ", band_length=" + std::to_string(okvs_params.band_length) +
          ", lambda=" + std::to_string(okvs_params.statistical_security) +
          ", fitted_lambda=" + std::to_string(okvs_params.fitted_security));
  for (int attempt = 0; attempt < 8; ++attempt) {
    band_okvs::BandOkvs okvs;
    okvs.Init(static_cast<int>(keys.size()), okvs_params.num_vars,
              okvs_params.band_length, U128ToBlock(seed + attempt));
    EncodedBandOkvs encoded;
    encoded.item_count = keys.size();
    encoded.num_vars = okvs_params.num_vars;
    encoded.band_length = okvs_params.band_length;
    encoded.seed = seed + attempt;
    std::vector<oc::block> table_blocks(static_cast<size_t>(okvs.Size()));
    if (okvs.Encode(key_blocks.data(), value_blocks.data(),
                    table_blocks.data())) {
      encoded.table.resize(table_blocks.size());
      if (!table_blocks.empty()) {
        std::memcpy(encoded.table.data(), table_blocks.data(),
                    table_blocks.size() * sizeof(oc::block));
      }
      return encoded;
    }
  }
  OTMPSI_THROW("Band OKVS encoding failed after retries");
}

std::vector<uint128_t> DecodeBandOkvs(const EncodedBandOkvs &encoded,
                                      const std::vector<uint128_t> &keys) {
  std::vector<oc::block> key_blocks(keys.size());
  for (size_t i = 0; i < keys.size(); ++i) {
    key_blocks[i] = U128ToBlock(keys[i]);
  }
  band_okvs::BandOkvs okvs;
  okvs.Init(static_cast<int>(keys.size()), encoded.num_vars,
            encoded.band_length, U128ToBlock(encoded.seed));
  std::vector<oc::block> table_blocks(encoded.table.size());
  if (!encoded.table.empty()) {
    std::memcpy(table_blocks.data(), encoded.table.data(),
                encoded.table.size() * sizeof(uint128_t));
  }
  std::vector<oc::block> out_blocks(keys.size());
  okvs.Decode(key_blocks.data(), table_blocks.data(), out_blocks.data());
  std::vector<uint128_t> out(keys.size(), 0);
  if (!out_blocks.empty()) {
    std::memcpy(out.data(), out_blocks.data(),
                out_blocks.size() * sizeof(oc::block));
  }
  return out;
}

void SendOkvs(coproto::Socket &sock, const EncodedBandOkvs &okvs) {
  std::vector<uint64_t> header = {
      static_cast<uint64_t>(okvs.item_count),
      static_cast<uint64_t>(okvs.num_vars),
      static_cast<uint64_t>(okvs.band_length),
      detail::Low64(okvs.seed),
      detail::High64(okvs.seed),
      static_cast<uint64_t>(okvs.table.size()),
  };
  auto header_span = coproto::span<uint64_t>(header.data(), header.size());
  macoro::sync_wait(sock.send(header_span));
  auto table_span =
      coproto::span<const uint128_t>(okvs.table.data(), okvs.table.size());
  macoro::sync_wait(sock.send(table_span));
}

EncodedBandOkvs RecvOkvs(coproto::Socket &sock) {
  std::vector<uint64_t> header(6);
  auto header_span = coproto::span<uint64_t>(header.data(), header.size());
  macoro::sync_wait(sock.recv(header_span));
  EncodedBandOkvs okvs;
  okvs.item_count = static_cast<size_t>(header[0]);
  okvs.num_vars = static_cast<int>(header[1]);
  okvs.band_length = static_cast<int>(header[2]);
  okvs.seed = (static_cast<uint128_t>(header[4]) << 64) | header[3];
  okvs.table.resize(static_cast<size_t>(header[5]));
  auto table_span =
      coproto::span<uint128_t>(okvs.table.data(), okvs.table.size());
  macoro::sync_wait(sock.recv(table_span));
  return okvs;
}

ShareWithStats SsBsmtLeader(const std::vector<uint128_t> &query,
                            size_t party_idx, const OtmpsiParams &params) {
  const auto start = std::chrono::steady_clock::now();
  const std::string ssoprf_tag = MakeSocketTag(params.base_port, party_idx, 0);
  const std::string okvs_tag = MakeSocketTag(params.base_port, party_idx, 1);
  auto ssoprf = RunSsoprfReceiverRole(query, ssoprf_tag, params.threads);

  auto okvs_sock = AcquireLocalSocket(okvs_tag, 0);
  auto okvs = RecvOkvs(okvs_sock);
  macoro::sync_wait(okvs_sock.flush());
  const auto okvs_comm =
      static_cast<size_t>(okvs_sock.bytesSent() + okvs_sock.bytesReceived());

  auto decoded = DecodeBandOkvs(okvs, query);
  std::vector<uint128_t> q0(query.size(), 0);
  for (size_t i = 0; i < query.size(); ++i) {
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
  auto values = EvalAltModPrf(key, set);
  auto okvs =
      EncodeBandOkvs(set, values, BlockToU128(osuCrypto::sysRandomSeed()));

  auto okvs_sock = AcquireLocalSocket(okvs_tag, 1);
  SendOkvs(okvs_sock, okvs);
  macoro::sync_wait(okvs_sock.flush());
  const auto okvs_comm =
      static_cast<size_t>(okvs_sock.bytesSent() + okvs_sock.bytesReceived());
  return {std::move(ssoprf.share),
          RoleStats{SecondsSince(start), ssoprf.bytes + okvs_comm}};
}

} // namespace examples::otmpsi
