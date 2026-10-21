#include "otmpsi/demo_data.h"

namespace examples::otmpsi {
namespace {

bool UsesControlledOutput(const OtmpsiParams &params) {
  return params.target_output_size.has_value();
}

bool ControlledMembership(size_t party_idx, size_t row,
                          const OtmpsiParams &params) {
  OTMPSI_ENFORCE(party_idx > 0 && party_idx < params.parties,
               "controlled demo party index out of range");
  const bool selected = row < *params.target_output_size;
  const size_t client_holders = selected
                                    ? params.threshold - 1
                                    : (params.threshold > 1
                                           ? params.threshold - 2
                                           : 0);
  const size_t client_count = params.parties - 1;
  const size_t cyclic_position =
      (party_idx - 1 + client_count - (row % client_count)) % client_count;
  return cyclic_position < client_holders;
}

bool DemoMembership(size_t party_idx, size_t row,
                    const OtmpsiParams &params) {
  if (UsesControlledOutput(params)) {
    return ControlledMembership(party_idx, row, params);
  }
  return ((row + party_idx) % 3) != 0;
}

} // namespace

std::vector<uint128_t> BuildLeaderSet(size_t set_size) {
  std::vector<uint128_t> x0(set_size);
  for (size_t i = 0; i < set_size; ++i) {
    x0[i] = MakeItem(0, i + 1);
  }
  return x0;
}

std::vector<uint128_t> BuildPartySet(size_t party_idx,
                                     const OtmpsiParams &params,
                                     const std::vector<uint128_t> &x0) {
  std::vector<uint128_t> set;
  set.reserve(params.set_size);
  for (size_t i = 0; i < x0.size() && set.size() < params.set_size; ++i) {
    if (DemoMembership(party_idx, i, params)) {
      set.push_back(x0[i]);
    }
  }
  size_t filler = 0;
  while (set.size() < params.set_size) {
    set.push_back(MakeItem(1000 + party_idx, ++filler));
  }
  return set;
}

std::vector<OtmpsiResult>
ExpectedDemoResults(const OtmpsiParams &params,
                    const std::vector<uint128_t> &x0) {
  std::vector<OtmpsiResult> expected;
  for (size_t i = 0; i < x0.size(); ++i) {
    std::vector<size_t> holders = {0};
    for (size_t party = 1; party < params.parties; ++party) {
      if (DemoMembership(party, i, params)) {
        holders.push_back(party);
      }
    }
    if (holders.size() >= params.threshold) {
      expected.push_back({x0[i], holders.size(), std::move(holders)});
    }
  }
  return expected;
}

void CheckDemoResults(const std::vector<OtmpsiResult> &actual,
                             const std::vector<OtmpsiResult> &expected) {
  OTMPSI_ENFORCE(actual.size() == expected.size(),
               "OT-MPSI output size mismatch: actual={} expected={}",
               actual.size(), expected.size());
  // Protocol rows follow a fresh private permutation, while the reference
  // uses the original dataset. Compare the complete output tuples as a set.
  std::vector<const OtmpsiResult *> actual_by_item(actual.size());
  std::vector<const OtmpsiResult *> expected_by_item(expected.size());
  for (size_t i = 0; i < actual.size(); ++i) {
    actual_by_item[i] = &actual[i];
    expected_by_item[i] = &expected[i];
  }
  const auto by_item = [](const OtmpsiResult *a, const OtmpsiResult *b) {
    return a->item < b->item;
  };
  std::sort(actual_by_item.begin(), actual_by_item.end(), by_item);
  std::sort(expected_by_item.begin(), expected_by_item.end(), by_item);
  for (size_t i = 0; i < actual.size(); ++i) {
    OTMPSI_ENFORCE(actual_by_item[i]->item == expected_by_item[i]->item &&
                     actual_by_item[i]->count == expected_by_item[i]->count &&
                     actual_by_item[i]->holders == expected_by_item[i]->holders,
                 "OT-MPSI output mismatch at row {}", i);
  }
}

} // namespace examples::otmpsi
