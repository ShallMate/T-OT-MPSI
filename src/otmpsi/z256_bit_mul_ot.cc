#include "otmpsi/z256_bit_mul_ot.h"

namespace examples::otmpsi {

Z256BitMulOtSenderBatch
PrecomputeZ256BitMulOtSender(oc::SilentOtExtSender &sender, size_t num_ots,
                             oc::PRNG &prng, coproto::Socket &sock) {
  oc::AlignedUnVector<std::array<block, 2>> random_messages(num_ots);
  macoro::sync_wait(sender.send(random_messages, prng, sock));

  Z256BitMulOtSenderBatch out;
  out.pad0.resize(num_ots);
  out.pad1.resize(num_ots);
  for (size_t i = 0; i < num_ots; ++i) {
    out.pad0[i] = static_cast<uint8_t>(BlockToU64(random_messages[i][0]));
    out.pad1[i] = static_cast<uint8_t>(BlockToU64(random_messages[i][1]));
  }
  return out;
}

Z256BitMulOtReceiverBatch
PrecomputeZ256BitMulOtReceiver(oc::SilentOtExtReceiver &receiver,
                               std::span<const uint8_t> choices, oc::PRNG &prng,
                               coproto::Socket &sock) {
  oc::BitVector choice_bits(static_cast<oc::u64>(choices.size()));
  for (size_t i = 0; i < choices.size(); ++i) {
    OTMPSI_ENFORCE((choices[i] & ~uint8_t{1}) == 0,
                 "Z256 bit-multiplication OT choice must be a bit");
    choice_bits[static_cast<oc::u64>(i)] = choices[i];
  }

  oc::AlignedUnVector<block> random_messages(choices.size());
  macoro::sync_wait(receiver.receive(choice_bits, random_messages, prng, sock));

  Z256BitMulOtReceiverBatch out;
  out.choices.assign(choices.begin(), choices.end());
  out.selected_pads.resize(choices.size());
  for (size_t i = 0; i < choices.size(); ++i) {
    out.selected_pads[i] = static_cast<uint8_t>(BlockToU64(random_messages[i]));
  }
  return out;
}

std::vector<uint8_t> SendZ256BitMulShares(const Z256BitMulOtSenderBatch &batch,
                                          std::span<const uint8_t> input,
                                          coproto::Socket &sock) {
  OTMPSI_ENFORCE(batch.pad0.size() == input.size() &&
                   batch.pad1.size() == input.size(),
               "Z256 bit-multiplication sender batch size mismatch");
  std::vector<uint8_t> corrections(input.size());
  std::vector<uint8_t> shares(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    corrections[i] =
        static_cast<uint8_t>(batch.pad0[i] + input[i] - batch.pad1[i]);
    shares[i] = static_cast<uint8_t>(uint8_t{0} - batch.pad0[i]);
  }
  macoro::sync_wait(sock.send(std::move(corrections)));
  return shares;
}

std::vector<uint8_t>
ReceiveZ256BitMulShares(const Z256BitMulOtReceiverBatch &batch,
                        coproto::Socket &sock) {
  OTMPSI_ENFORCE(batch.choices.size() == batch.selected_pads.size(),
               "Z256 bit-multiplication receiver batch size mismatch");
  std::vector<uint8_t> corrections(batch.choices.size());
  macoro::sync_wait(sock.recv(
      coproto::span<uint8_t>(corrections.data(), corrections.size())));
  std::vector<uint8_t> shares(batch.choices.size());
  for (size_t i = 0; i < shares.size(); ++i) {
    shares[i] = static_cast<uint8_t>(
        batch.selected_pads[i] +
        (batch.choices[i] != 0 ? corrections[i] : uint8_t{0}));
  }
  return shares;
}

} // namespace examples::otmpsi
