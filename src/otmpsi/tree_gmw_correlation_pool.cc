#include "otmpsi/tree_gmw_correlation_pool.h"

#include <limits>

namespace examples::otmpsi {
namespace {

constexpr size_t kSilentOtBatchSize = size_t{1} << 18;
constexpr size_t kOtsPerGmwCorrelation = 128;

bool RangesOverlap(const OtIndexRange &lhs, const OtIndexRange &rhs) {
  return lhs.begin < rhs.end() && rhs.begin < lhs.end();
}

oc::BitVector CorrectReceiverChoices(
    const oc::BitVector &random_choices,
    std::span<const uint8_t> desired_choices) {
  OTMPSI_ENFORCE(random_choices.size() == desired_choices.size(),
               "retained OT choice count mismatch");
  oc::BitVector correction(static_cast<oc::u64>(desired_choices.size()));
  for (size_t i = 0; i < desired_choices.size(); ++i) {
    OTMPSI_ENFORCE((desired_choices[i] & ~uint8_t{1}) == 0,
                 "Tree-X2A retained OT choice must be a bit");
    correction[static_cast<oc::u64>(i)] =
        static_cast<uint8_t>(random_choices[static_cast<oc::u64>(i)]) ^
        desired_choices[i];
  }
  return correction;
}

} // namespace

size_t OtIndexRange::end() const {
  OTMPSI_ENFORCE(count <= std::numeric_limits<size_t>::max() - begin,
               "OT index range overflow");
  return begin + count;
}

TreeGmwPairCorrelationPool PrecomputeTreeGmwPairCorrelationPool(
    size_t peer, bool sender_role, size_t gmw_correlations,
    std::span<const uint8_t> tree_choices, size_t num_threads, block seed,
    coproto::Socket &sock) {
  OTMPSI_ENFORCE(!tree_choices.empty(),
               "Tree/GMW correlation pool requires a non-empty Tree tail");
  OTMPSI_ENFORCE(
      gmw_correlations <=
          std::numeric_limits<size_t>::max() / kOtsPerGmwCorrelation,
      "Tree/GMW GMW-prefix OT count overflow");
  const size_t comm_before =
      static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());

  TreeGmwPairCorrelationPool pool;
  pool.gmw_prefix.peer = peer;
  pool.gmw_prefix.sender_role = sender_role;
  pool.gmw_range = {0, gmw_correlations * kOtsPerGmwCorrelation};
  pool.tree_range = {pool.gmw_range.end(), tree_choices.size()};
  OTMPSI_ENFORCE(!RangesOverlap(pool.gmw_range, pool.tree_range),
               "Tree and GMW OT ranges overlap");

  volePSI::SilentTripleGen gen;
  gen.init(static_cast<volePSI::u64>(pool.tree_range.end()),
           static_cast<volePSI::u64>(kSilentOtBatchSize),
           static_cast<volePSI::u64>(std::max<size_t>(1, num_threads)),
           sender_role ? volePSI::Sender : volePSI::Receiver, seed);
  gen.retainByteRandomOts(static_cast<volePSI::u64>(pool.tree_range.begin),
                          static_cast<volePSI::u64>(pool.tree_range.count));
  macoro::sync_wait(gen.generateBaseOts(sender_role ? 0 : 1, sock));
  macoro::sync_wait(gen.expand(sock));

  OTMPSI_ENFORCE(static_cast<size_t>(gen.multSize()) >= gmw_correlations &&
                   static_cast<size_t>(gen.addSize()) >= gmw_correlations,
               "combined Silent-OT expansion returned too few GMW correlations");
  pool.gmw_prefix.mult.resize(gmw_correlations);
  pool.gmw_prefix.add.resize(gmw_correlations);
  for (size_t i = 0; i < gmw_correlations; ++i) {
    pool.gmw_prefix.mult[i] = BlockToU128(gen.multData()[i]);
    pool.gmw_prefix.add[i] = BlockToU128(gen.addData()[i]);
  }

  if (sender_role) {
    auto retained = gen.takeRetainedByteOtSender();
    OTMPSI_ENFORCE(retained.pad0.size() == tree_choices.size() &&
                     retained.pad1.size() == tree_choices.size(),
                 "combined Silent-OT sender tail size mismatch");
    oc::BitVector correction(static_cast<oc::u64>(tree_choices.size()));
    macoro::sync_wait(sock.recv(correction));
    for (size_t i = 0; i < tree_choices.size(); ++i) {
      if (correction[static_cast<oc::u64>(i)] != 0) {
        std::swap(retained.pad0[i], retained.pad1[i]);
      }
    }
    pool.tree_sender.pad0 = std::move(retained.pad0);
    pool.tree_sender.pad1 = std::move(retained.pad1);
  } else {
    auto retained = gen.takeRetainedByteOtReceiver();
    OTMPSI_ENFORCE(retained.selectedPads.size() == tree_choices.size(),
                 "combined Silent-OT receiver tail size mismatch");
    auto correction =
        CorrectReceiverChoices(retained.randomChoices, tree_choices);
    macoro::sync_wait(sock.send(std::move(correction)));
    pool.tree_receiver.choices.assign(tree_choices.begin(), tree_choices.end());
    pool.tree_receiver.selected_pads = std::move(retained.selectedPads);
  }

  macoro::sync_wait(sock.flush());
  pool.gmw_prefix.preprocess_comm_bytes =
      static_cast<size_t>(sock.bytesSent() + sock.bytesReceived()) - comm_before;

  CheckTreeGmwCorrelationPool(pool, gmw_correlations, tree_choices.size());
  return pool;
}

void CheckTreeGmwCorrelationPool(const TreeGmwPairCorrelationPool &pool,
                                 size_t expected_gmw_correlations,
                                 size_t expected_tree_ots) {
  OTMPSI_ENFORCE(pool.gmw_range.begin == 0 &&
                   pool.gmw_range.count ==
                       expected_gmw_correlations * kOtsPerGmwCorrelation,
               "combined pool GMW range mismatch");
  OTMPSI_ENFORCE(pool.tree_range.begin == pool.gmw_range.end() &&
                   pool.tree_range.count == expected_tree_ots &&
                   !RangesOverlap(pool.gmw_range, pool.tree_range),
               "combined pool Tree range mismatch or overlap");
  OTMPSI_ENFORCE(pool.gmw_prefix.mult.size() == expected_gmw_correlations &&
                   pool.gmw_prefix.add.size() == expected_gmw_correlations,
               "combined pool GMW correlation count mismatch");
  if (pool.gmw_prefix.sender_role) {
    OTMPSI_ENFORCE(pool.tree_sender.pad0.size() == expected_tree_ots &&
                     pool.tree_sender.pad1.size() == expected_tree_ots &&
                     pool.tree_receiver.choices.empty() &&
                     pool.tree_receiver.selected_pads.empty(),
                 "combined pool sender Tree-tail mismatch");
  } else {
    OTMPSI_ENFORCE(pool.tree_receiver.choices.size() == expected_tree_ots &&
                     pool.tree_receiver.selected_pads.size() ==
                         expected_tree_ots &&
                     pool.tree_sender.pad0.empty() &&
                     pool.tree_sender.pad1.empty(),
                 "combined pool receiver Tree-tail mismatch");
  }
}

} // namespace examples::otmpsi
