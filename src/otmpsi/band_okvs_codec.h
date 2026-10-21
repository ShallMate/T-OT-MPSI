#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

// Only public dimensions, a row-hash seed and the table cross the wire. Randomness
// used to fill free variables is private to the encoding party.
struct EncodedBandOkvs {
  size_t item_count = 0;
  int num_vars = 0;
  int band_length = 128;
  uint128_t hash_seed = 0;
  std::vector<uint128_t> table;
};

std::optional<EncodedBandOkvs>
EncodeBandOkvs(const std::vector<uint128_t> &keys,
              const std::vector<uint128_t> &values);
std::vector<uint128_t> DecodeBandOkvs(const EncodedBandOkvs &encoded,
                                    const std::vector<uint128_t> &keys);
void SendOkvs(coproto::Socket &sock,
              const std::optional<EncodedBandOkvs> &encoded);
std::optional<EncodedBandOkvs> RecvOkvs(coproto::Socket &sock);

} // namespace examples::otmpsi
