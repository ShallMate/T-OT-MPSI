#pragma once

#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

// Implements chosen 1-out-of-2 OT for 1-bit or 8-bit messages from random OT.
// Only the small XOR corrections are sent on the wire; random OT pads remain
// full blocks inside libOTe.
size_t PackedChosenOtCorrectionBytes(size_t num_ots, size_t message_bits);

void SendPackedChosenOt(oc::SilentOtExtSender &sender,
                        const std::vector<std::array<uint8_t, 2>> &messages,
                        size_t message_bits, oc::PRNG &prng,
                        coproto::Socket &sock);

std::vector<uint8_t> ReceivePackedChosenOt(oc::SilentOtExtReceiver &receiver,
                                           const oc::BitVector &choices,
                                           size_t message_bits, oc::PRNG &prng,
                                           coproto::Socket &sock);

} // namespace examples::otmpsi
