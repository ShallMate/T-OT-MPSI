#include "otmpsi/band_okvs_codec.h"

#include "otmpsi/band_okvs_params.h"

namespace examples::otmpsi {

std::optional<EncodedBandOkvs> EncodeBandOkvs(const std::vector<uint128_t> &keys,
                               const std::vector<uint128_t> &values) {
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
  band_okvs::BandOkvs okvs;
  const auto hash_seed = osuCrypto::sysRandomSeed();
  okvs.Init(static_cast<int>(keys.size()), okvs_params.num_vars,
            okvs_params.band_length, osuCrypto::sysRandomSeed(), hash_seed);
  std::vector<oc::block> table_blocks(static_cast<size_t>(okvs.Size()),
                                     oc::ZeroBlock);
  if (!okvs.Encode(key_blocks.data(), value_blocks.data(), table_blocks.data())) {
    return std::nullopt;
  }
  EncodedBandOkvs encoded;
  encoded.item_count = keys.size();
  encoded.num_vars = okvs_params.num_vars;
  encoded.band_length = okvs_params.band_length;
  encoded.hash_seed = BlockToU128(hash_seed);
  encoded.table.resize(static_cast<size_t>(encoded.num_vars));
  std::memcpy(encoded.table.data(), table_blocks.data(),
              encoded.table.size() * sizeof(oc::block));
  return encoded;
}

std::vector<uint128_t> DecodeBandOkvs(const EncodedBandOkvs &encoded,
                                      const std::vector<uint128_t> &keys) {
  std::vector<oc::block> key_blocks(keys.size());
  for (size_t i = 0; i < keys.size(); ++i) {
    key_blocks[i] = U128ToBlock(keys[i]);
  }
  band_okvs::BandOkvs okvs;
  okvs.Init(static_cast<int>(keys.size()), encoded.num_vars,
            encoded.band_length, oc::ZeroBlock, U128ToBlock(encoded.hash_seed));
  std::vector<oc::block> table_blocks(static_cast<size_t>(okvs.Size()),
                                      oc::ZeroBlock);
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

void SendOkvs(coproto::Socket &sock,
              const std::optional<EncodedBandOkvs> &encoded) {
  // A zero item count is the encoding-failure symbol; valid sets have n >= 1024.
  std::vector<uint64_t> header(6, 0);
  if (encoded) {
    header = {static_cast<uint64_t>(encoded->item_count),
              static_cast<uint64_t>(encoded->num_vars),
              static_cast<uint64_t>(encoded->band_length),
              detail::Low64(encoded->hash_seed),
              detail::High64(encoded->hash_seed),
              static_cast<uint64_t>(encoded->table.size())};
  }
  macoro::sync_wait(sock.send(std::move(header)));
  if (encoded) {
    macoro::sync_wait(sock.send(coproto::span<const uint128_t>(
        encoded->table.data(), encoded->table.size())));
  }
}

std::optional<EncodedBandOkvs> RecvOkvs(coproto::Socket &sock) {
  std::vector<uint64_t> header(6);
  macoro::sync_wait(sock.recv(coproto::span<uint64_t>(header.data(), header.size())));
  if (header[0] == 0) {
    return std::nullopt;
  }
  EncodedBandOkvs encoded;
  encoded.item_count = static_cast<size_t>(header[0]);
  encoded.num_vars = static_cast<int>(header[1]);
  encoded.band_length = static_cast<int>(header[2]);
  encoded.hash_seed = (static_cast<uint128_t>(header[4]) << 64) | header[3];
  encoded.table.resize(static_cast<size_t>(header[5]));
  macoro::sync_wait(sock.recv(coproto::span<uint128_t>(
      encoded.table.data(), encoded.table.size())));
  return encoded;
}

} // namespace examples::otmpsi
