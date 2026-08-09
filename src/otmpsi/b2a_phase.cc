#include "otmpsi/b2a_phase.h"

#include "otmpsi/packed_chosen_ot.h"
#include "otmpsi/silent_ot_utils.h"

namespace examples::otmpsi {

B2aShareWithStats B2aLeaderSide(const std::vector<uint128_t> &bit_share,
                                size_t party_idx,
                                const OtmpsiParams &params,
                                const std::string &tag) {
  const auto start = std::chrono::steady_clock::now();
  B2aShareWithStats out;
  out.share.resize(bit_share.size(), 0);
  if (bit_share.empty()) {
    return out;
  }

  auto sock = AcquireLocalSocket(tag, 0);
  oc::SilentOtExtSender sender;
  ConfigureSilentOtSender(sender, bit_share.size(), params.threads);
  oc::PRNG ot_prng(oc::sysRandomSeed());
  macoro::sync_wait(sender.genBaseOts(ot_prng, sock));

  oc::PRNG mask_prng(oc::sysRandomSeed());
  std::vector<std::array<uint8_t, 2>> messages(bit_share.size());
  for (size_t i = 0; i < bit_share.size(); ++i) {
    const uint8_t a = static_cast<uint8_t>(bit_share[i] & 1U);
    const uint8_t r = static_cast<uint8_t>(BlockToU64(mask_prng.get()));
    messages[i][0] = static_cast<uint8_t>(a - r);
    messages[i][1] = static_cast<uint8_t>(uint8_t{1} - a - r);
    out.share[i] = r;
  }

  SendPackedChosenOt(sender, messages, kComparisonBits, ot_prng, sock);
  macoro::sync_wait(sock.flush());
  out.stats = RoleStats{SecondsSince(start),
                        static_cast<size_t>(sock.bytesSent() +
                                            sock.bytesReceived())};
  LogDebug("b2a", "party=" + std::to_string(party_idx) +
                      ", rank=0 done");
  return out;
}

B2aShareWithStats B2aPartySide(const std::vector<uint128_t> &bit_share,
                               size_t party_idx,
                               const OtmpsiParams &params,
                               const std::string &tag) {
  const auto start = std::chrono::steady_clock::now();
  B2aShareWithStats out;
  out.share.resize(bit_share.size(), 0);
  if (bit_share.empty()) {
    return out;
  }

  auto sock = AcquireLocalSocket(tag, 1);
  oc::SilentOtExtReceiver receiver;
  ConfigureSilentOtReceiver(receiver, bit_share.size(), params.threads);
  oc::PRNG ot_prng(oc::sysRandomSeed());
  macoro::sync_wait(receiver.genBaseOts(ot_prng, sock));

  oc::BitVector choices(static_cast<oc::u64>(bit_share.size()));
  for (size_t i = 0; i < bit_share.size(); ++i) {
    choices[static_cast<oc::u64>(i)] =
        static_cast<uint8_t>((bit_share[i] & 1U) != 0);
  }
  auto recv_messages = ReceivePackedChosenOt(
      receiver, choices, kComparisonBits, ot_prng, sock);
  macoro::sync_wait(sock.flush());

  for (size_t i = 0; i < bit_share.size(); ++i) {
    out.share[i] = recv_messages[i];
  }
  out.stats = RoleStats{SecondsSince(start),
                        static_cast<size_t>(sock.bytesSent() +
                                            sock.bytesReceived())};
  LogDebug("b2a", "party=" + std::to_string(party_idx) +
                      ", rank=1 done");
  return out;
}

void CheckB2aMembershipShares(
    const std::vector<std::vector<uint128_t>> &leader_bit_shares,
    const std::vector<std::vector<uint128_t>> &party_bit_shares,
    const std::vector<std::vector<uint64_t>> &leader_arith_shares,
    const std::vector<std::vector<uint64_t>> &party_arith_shares,
    const OtmpsiParams &params) {
  OTMPSI_ENFORCE(leader_bit_shares.size() == params.parties - 1,
               "B2A leader bit share vector mismatch");
  OTMPSI_ENFORCE(party_bit_shares.size() == params.parties - 1,
               "B2A party bit share vector mismatch");
  OTMPSI_ENFORCE(leader_arith_shares.size() == params.parties - 1,
               "B2A leader arithmetic share vector mismatch");
  OTMPSI_ENFORCE(party_arith_shares.size() == params.parties - 1,
               "B2A party arithmetic share vector mismatch");

  for (size_t party = 1; party < params.parties; ++party) {
    const auto &lb = leader_bit_shares[party - 1];
    const auto &pb = party_bit_shares[party - 1];
    const auto &la = leader_arith_shares[party - 1];
    const auto &pa = party_arith_shares[party - 1];
    OTMPSI_ENFORCE(lb.size() == params.set_size && pb.size() == params.set_size &&
                     la.size() == params.set_size && pa.size() == params.set_size,
                 "B2A row count mismatch");
    for (size_t row = 0; row < params.set_size; ++row) {
      const uint64_t expected =
          static_cast<uint64_t>((lb[row] ^ pb[row]) & 1U);
      const uint64_t opened =
          (la[row] + pa[row]) & LowBitsMask(kComparisonBits);
      OTMPSI_ENFORCE(opened == expected,
                   "B2A conversion mismatch at party {} row {}", party, row);
    }
  }
}

}
