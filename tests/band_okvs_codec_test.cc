#include <future>
#include <iostream>

#include "otmpsi/band_okvs_codec.h"
#include "otmpsi/input_preparation.h"

namespace examples::otmpsi {
namespace {

void CheckEncodingAndWireFormat() {
  std::vector<uint128_t> raw(1024), values(1024);
  for (size_t i = 0; i < raw.size(); ++i) {
    raw[i] = i + 1;
    values[i] = MakeItem(17, i);
  }
  const auto keys = HashSsBsmtInputs(raw);
  auto encoded = EncodeBandOkvs(keys, values);
  auto second = EncodeBandOkvs(keys, values);
  OTMPSI_ENFORCE(encoded && second, "test input failed to encode");
  OTMPSI_ENFORCE(encoded->table.size() == static_cast<size_t>(encoded->num_vars),
                 "SIMD padding must stay local");
  OTMPSI_ENFORCE(encoded->table != second->table,
                 "encoding did not use fresh private free variables");
  OTMPSI_ENFORCE(encoded->hash_seed != second->hash_seed,
                 "each encoding must randomize the row map");
  oc::PRNG public_seed_prng(U128ToBlock(encoded->hash_seed));
  for (auto value : encoded->table) {
    OTMPSI_ENFORCE(value != BlockToU128(public_seed_prng.get<block>()),
                   "public row seed reveals the free-variable padding");
  }
  OTMPSI_ENFORCE(DecodeBandOkvs(*encoded, keys) == values &&
                     DecodeBandOkvs(*second, keys) == values,
                 "decoding must work without the encoder's random seed");

  auto pair = coproto::LocalAsyncSocket::makePair();
  auto sender = std::async(std::launch::async, [&] {
    coproto::Socket sock(std::move(pair[0]));
    SendOkvs(sock, encoded);
    macoro::sync_wait(sock.flush());
  });
  coproto::Socket sock(std::move(pair[1]));
  // Only the independent row-map seed is public; the filler seed is absent.
  std::array<uint64_t, 6> header{};
  macoro::sync_wait(sock.recv(coproto::span<uint64_t>(header.data(), header.size())));
  OTMPSI_ENFORCE(header[0] == raw.size() &&
                     header[1] == static_cast<uint64_t>(encoded->num_vars) &&
                     header[2] == static_cast<uint64_t>(encoded->band_length) &&
                     header[3] == detail::Low64(encoded->hash_seed) &&
                     header[4] == detail::High64(encoded->hash_seed) &&
                     header[5] == encoded->table.size(),
                 "OKVS header contains unexpected metadata");
  std::vector<uint128_t> wire_table(header[5]);
  macoro::sync_wait(sock.recv(coproto::span<uint128_t>(
      wire_table.data(), wire_table.size())));
  sender.get();
  OTMPSI_ENFORCE(wire_table == encoded->table, "wire table mismatch");
}

void CheckEncodingFailure() {
  // Distinct keys can still generate dependent equations. This used to loop
  // forever when elimination produced a zero row.
  oc::AES aes(oc::ZeroBlock);
  std::vector<block> keys;
  for (uint128_t item = 1; keys.size() < 2; ++item) {
    const auto key = U128ToBlock(item);
    if ((aes.hashBlock(key).get<uint32_t>(0) & 1U) == 0) {
      keys.push_back(key);
    }
  }
  band_okvs::BandOkvs okvs;
  okvs.Init(2, 2, 1, oc::sysRandomSeed());
  std::array<block, 2> values{oc::ZeroBlock, oc::OneBlock};
  std::vector<block> table(okvs.Size(), oc::ZeroBlock);
  OTMPSI_ENFORCE(!okvs.Encode(keys.data(), values.data(), table.data()),
                 "rank-deficient encoding must fail");

  auto pair = coproto::LocalAsyncSocket::makePair();
  auto sender = std::async(std::launch::async, [&] {
    coproto::Socket sock(std::move(pair[0]));
    SendOkvs(sock, std::nullopt);
    macoro::sync_wait(sock.flush());
  });
  coproto::Socket sock(std::move(pair[1]));
  OTMPSI_ENFORCE(!RecvOkvs(sock).has_value(),
                 "encoding failure must reach the peer without a table read");
  sender.get();
}

} // namespace
} // namespace examples::otmpsi

int main() {
  try {
    examples::otmpsi::CheckEncodingAndWireFormat();
    examples::otmpsi::CheckEncodingFailure();
    std::cout << "Band OKVS privacy and failure checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
