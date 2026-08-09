#include "otmpsi/packed_chosen_ot.h"

#include <limits>

namespace examples::otmpsi {
namespace {

void ValidateMessageBits(size_t message_bits) {
  OTMPSI_ENFORCE(message_bits == 1 || message_bits == 8,
               "packed chosen OT supports only 1-bit or 8-bit messages");
}

uint8_t MessageMask(size_t message_bits) {
  return message_bits == 8 ? uint8_t{0xff} : uint8_t{0x01};
}

void WriteCorrection(std::vector<uint8_t> &packed, size_t field,
                     size_t message_bits, uint8_t value) {
  if (message_bits == 8) {
    packed[field] = value;
    return;
  }
  if ((value & 1U) != 0) {
    packed[field >> 3U] |= static_cast<uint8_t>(1U << (field & 7U));
  }
}

uint8_t ReadCorrection(const std::vector<uint8_t> &packed, size_t field,
                       size_t message_bits) {
  if (message_bits == 8) {
    return packed[field];
  }
  return static_cast<uint8_t>((packed[field >> 3U] >> (field & 7U)) & 1U);
}

} // namespace

size_t PackedChosenOtCorrectionBytes(size_t num_ots, size_t message_bits) {
  ValidateMessageBits(message_bits);
  OTMPSI_ENFORCE(num_ots <= std::numeric_limits<size_t>::max() / 2,
               "packed chosen OT correction size overflow");
  const size_t fields = 2 * num_ots;
  return message_bits == 8 ? fields : (fields + 7U) / 8U;
}

void SendPackedChosenOt(oc::SilentOtExtSender &sender,
                        const std::vector<std::array<uint8_t, 2>> &messages,
                        size_t message_bits, oc::PRNG &prng,
                        coproto::Socket &sock) {
  const uint8_t mask = MessageMask(message_bits);
  oc::AlignedUnVector<std::array<block, 2>> random_messages(messages.size());
  macoro::sync_wait(sender.send(random_messages, prng, sock));

  std::vector<uint8_t> corrections(
      PackedChosenOtCorrectionBytes(messages.size(), message_bits), 0);
  for (size_t i = 0; i < messages.size(); ++i) {
    for (size_t choice = 0; choice < 2; ++choice) {
      const uint8_t pad =
          static_cast<uint8_t>(BlockToU64(random_messages[i][choice])) & mask;
      const uint8_t correction =
          static_cast<uint8_t>((messages[i][choice] & mask) ^ pad);
      WriteCorrection(corrections, 2 * i + choice, message_bits, correction);
    }
  }
  // coproto may complete a span send after this awaitable has only been
  // queued. Move the owning buffer into the socket so its storage remains
  // alive until the queued send completes.
  macoro::sync_wait(sock.send(std::move(corrections)));
}

std::vector<uint8_t> ReceivePackedChosenOt(oc::SilentOtExtReceiver &receiver,
                                           const oc::BitVector &choices,
                                           size_t message_bits, oc::PRNG &prng,
                                           coproto::Socket &sock) {
  const uint8_t mask = MessageMask(message_bits);
  oc::AlignedUnVector<block> random_messages(choices.size());
  macoro::sync_wait(receiver.receive(choices, random_messages, prng, sock));

  std::vector<uint8_t> corrections(
      PackedChosenOtCorrectionBytes(choices.size(), message_bits), 0);
  macoro::sync_wait(sock.recv(
      coproto::span<uint8_t>(corrections.data(), corrections.size())));
  std::vector<uint8_t> received(choices.size(), 0);
  for (size_t i = 0; i < received.size(); ++i) {
    const size_t choice = static_cast<size_t>(choices[i]);
    const uint8_t pad =
        static_cast<uint8_t>(BlockToU64(random_messages[i])) & mask;
    received[i] = static_cast<uint8_t>(
        pad ^ ReadCorrection(corrections, 2 * i + choice, message_bits));
  }
  return received;
}

} // namespace examples::otmpsi
