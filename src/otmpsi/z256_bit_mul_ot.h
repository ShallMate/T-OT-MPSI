#pragma once

#include <span>

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

struct Z256BitMulOtSenderBatch {
  std::vector<uint8_t> pad0;
  std::vector<uint8_t> pad1;
};

struct Z256BitMulOtReceiverBatch {
  std::vector<uint8_t> choices;
  std::vector<uint8_t> selected_pads;
};

Z256BitMulOtSenderBatch
PrecomputeZ256BitMulOtSender(oc::SilentOtExtSender &sender, size_t num_ots,
                             oc::PRNG &prng, coproto::Socket &sock);

Z256BitMulOtReceiverBatch
PrecomputeZ256BitMulOtReceiver(oc::SilentOtExtReceiver &receiver,
                               std::span<const uint8_t> choices, oc::PRNG &prng,
                               coproto::Socket &sock);

std::vector<uint8_t> SendZ256BitMulShares(const Z256BitMulOtSenderBatch &batch,
                                          std::span<const uint8_t> input,
                                          coproto::Socket &sock);

std::vector<uint8_t>
ReceiveZ256BitMulShares(const Z256BitMulOtReceiverBatch &batch,
                        coproto::Socket &sock);

} // namespace examples::otmpsi
