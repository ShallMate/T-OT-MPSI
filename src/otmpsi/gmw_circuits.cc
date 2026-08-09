#include "otmpsi/gmw_circuits.h"

#include <algorithm>

namespace examples::otmpsi {
namespace {

bool IsNonlinearGate(oc::GateType type) {
  switch (type) {
  case oc::GateType::And:
  case oc::GateType::Nand:
  case oc::GateType::na_And:
  case oc::GateType::nb_And:
  case oc::GateType::Or:
  case oc::GateType::Nor:
  case oc::GateType::na_Or:
  case oc::GateType::nb_Or:
    return true;
  default:
    return false;
  }
}

size_t NonlinearGateCount(const volePSI::BetaCircuit &cir) {
  size_t count = 0;
  for (const auto &gate : cir.mGates) {
    if (IsNonlinearGate(gate.mType)) {
      ++count;
    }
  }
  return count;
}

} // namespace

volePSI::BetaCircuit BuildPaperThresholdCircuit(size_t parties) {
  const auto layout = MakePaperGmwLayout(parties);
  volePSI::BetaCircuit cd;
  volePSI::BetaBundle input(layout.input_bits);
  volePSI::BetaBundle output(layout.output_bits);
  cd.addInputBundle(input);
  cd.addOutputBundle(output);

  auto new_wire = [&]() {
    volePSI::BetaBundle tmp(1);
    cd.addTempWireBundle(tmp);
    return tmp[0];
  };
  auto make_not = [&](oc::BetaWire a) {
    auto out = new_wire();
    cd.addInvert(a, out);
    return out;
  };
  auto make_gate = [&](oc::BetaWire a, oc::BetaWire b, oc::GateType gate) {
    auto out = new_wire();
    cd.addGate(a, b, gate, out);
    return out;
  };

  const auto one = input[layout.one_offset];
  const auto zero = input[layout.zero_offset];

  std::vector<oc::BetaWire> membership(parties);
  membership[0] = one;
  for (size_t party = 1; party < parties; ++party) {
    const size_t offset = (party - 1) * layout.rho_bits;
    auto acc = make_not(input[offset]);
    for (size_t bit = 1; bit < layout.rho_bits; ++bit) {
      auto not_bit = make_not(input[offset + bit]);
      acc = make_gate(acc, not_bit, oc::GateType::And);
    }
    membership[party] = acc;
  }

  std::vector<oc::BetaWire> count(layout.count_bits, zero);
  count[0] = one;
  for (size_t party = 1; party < parties; ++party) {
    auto carry = membership[party];
    for (size_t bit = 0; bit < layout.count_bits; ++bit) {
      auto sum = make_gate(count[bit], carry, oc::GateType::Xor);
      auto next_carry = make_gate(count[bit], carry, oc::GateType::And);
      count[bit] = sum;
      carry = next_carry;
    }
  }

  auto greater = zero;
  auto equal = one;
  for (size_t rev = 0; rev < layout.count_bits; ++rev) {
    const size_t bit = layout.count_bits - 1 - rev;
    const auto threshold_bit = input[layout.threshold_offset + bit];
    const auto not_threshold_bit = make_not(threshold_bit);
    const auto count_gt_threshold_at_bit =
        make_gate(count[bit], not_threshold_bit, oc::GateType::And);
    const auto greater_here =
        make_gate(equal, count_gt_threshold_at_bit, oc::GateType::And);
    greater = make_gate(greater, greater_here, oc::GateType::Or);

    const auto diff = make_gate(count[bit], threshold_bit, oc::GateType::Xor);
    const auto equal_here = make_not(diff);
    equal = make_gate(equal, equal_here, oc::GateType::And);
  }
  const auto over_threshold = make_gate(greater, equal, oc::GateType::Or);

  cd.addCopy(over_threshold, output[0]);
  for (size_t bit = 0; bit < layout.count_bits; ++bit) {
    cd.addGate(over_threshold, count[bit], oc::GateType::And, output[1 + bit]);
  }
  cd.addCopy(over_threshold, output[1 + layout.count_bits]);
  for (size_t party = 1; party < parties; ++party) {
    cd.addGate(over_threshold, membership[party], oc::GateType::And,
               output[1 + layout.count_bits + party]);
  }

  cd.levelByAndDepth();
  return cd;
}

volePSI::BetaCircuit BuildMembershipThresholdCircuit(size_t parties) {
  const auto layout = MakeMembershipGmwLayout(parties);
  volePSI::BetaCircuit cd;
  volePSI::BetaBundle input(layout.input_bits);
  volePSI::BetaBundle output(layout.output_bits);
  cd.addInputBundle(input);
  cd.addOutputBundle(output);

  auto new_wire = [&]() {
    volePSI::BetaBundle tmp(1);
    cd.addTempWireBundle(tmp);
    return tmp[0];
  };
  auto make_not = [&](oc::BetaWire a) {
    auto out = new_wire();
    cd.addInvert(a, out);
    return out;
  };
  auto make_gate = [&](oc::BetaWire a, oc::BetaWire b, oc::GateType gate) {
    auto out = new_wire();
    cd.addGate(a, b, gate, out);
    return out;
  };

  const auto one = input[layout.one_offset];
  const auto zero = input[layout.zero_offset];

  std::vector<oc::BetaWire> membership(parties);
  membership[0] = one;
  for (size_t party = 1; party < parties; ++party) {
    membership[party] = input[layout.membership_offset + party - 1];
  }

  std::vector<oc::BetaWire> count(layout.count_bits, zero);
  count[0] = one;
  for (size_t party = 1; party < parties; ++party) {
    auto carry = membership[party];
    for (size_t bit = 0; bit < layout.count_bits; ++bit) {
      auto sum = make_gate(count[bit], carry, oc::GateType::Xor);
      auto next_carry = make_gate(count[bit], carry, oc::GateType::And);
      count[bit] = sum;
      carry = next_carry;
    }
  }

  auto greater = zero;
  auto equal = one;
  for (size_t rev = 0; rev < layout.count_bits; ++rev) {
    const size_t bit = layout.count_bits - 1 - rev;
    const auto threshold_bit = input[layout.threshold_offset + bit];
    const auto not_threshold_bit = make_not(threshold_bit);
    const auto count_gt_threshold_at_bit =
        make_gate(count[bit], not_threshold_bit, oc::GateType::And);
    const auto greater_here =
        make_gate(equal, count_gt_threshold_at_bit, oc::GateType::And);
    greater = make_gate(greater, greater_here, oc::GateType::Or);

    const auto diff = make_gate(count[bit], threshold_bit, oc::GateType::Xor);
    const auto equal_here = make_not(diff);
    equal = make_gate(equal, equal_here, oc::GateType::And);
  }
  const auto over_threshold = make_gate(greater, equal, oc::GateType::Or);

  cd.addCopy(over_threshold, output[0]);
  for (size_t bit = 0; bit < layout.count_bits; ++bit) {
    cd.addGate(over_threshold, count[bit], oc::GateType::And, output[1 + bit]);
  }
  cd.addCopy(over_threshold, output[1 + layout.count_bits]);
  for (size_t party = 1; party < parties; ++party) {
    cd.addGate(over_threshold, membership[party], oc::GateType::And,
               output[1 + layout.count_bits + party]);
  }

  cd.levelByAndDepth();
  return cd;
}

volePSI::BetaCircuit
BuildMembershipSelectionCountThresholdCircuit(size_t parties) {
  const auto layout = MakeMembershipGmwLayout(parties);
  volePSI::BetaCircuit cd;
  volePSI::BetaBundle input(layout.input_bits);
  volePSI::BetaBundle output(1);
  cd.addInputBundle(input);
  cd.addOutputBundle(output);

  auto new_wire = [&]() {
    volePSI::BetaBundle tmp(1);
    cd.addTempWireBundle(tmp);
    return tmp[0];
  };
  auto make_not = [&](oc::BetaWire a) {
    auto out = new_wire();
    cd.addInvert(a, out);
    return out;
  };
  auto make_gate = [&](oc::BetaWire a, oc::BetaWire b, oc::GateType gate) {
    auto out = new_wire();
    cd.addGate(a, b, gate, out);
    return out;
  };

  const auto one = input[layout.one_offset];
  const auto zero = input[layout.zero_offset];

  std::vector<oc::BetaWire> count(layout.count_bits, zero);
  count[0] = one;
  for (size_t party = 1; party < parties; ++party) {
    auto carry = input[layout.membership_offset + party - 1];
    for (size_t bit = 0; bit < layout.count_bits; ++bit) {
      auto sum = make_gate(count[bit], carry, oc::GateType::Xor);
      auto next_carry = make_gate(count[bit], carry, oc::GateType::And);
      count[bit] = sum;
      carry = next_carry;
    }
  }

  auto greater = zero;
  auto equal = one;
  for (size_t rev = 0; rev < layout.count_bits; ++rev) {
    const size_t bit = layout.count_bits - 1 - rev;
    const auto threshold_bit = input[layout.threshold_offset + bit];
    const auto not_threshold_bit = make_not(threshold_bit);
    const auto count_gt_threshold_at_bit =
        make_gate(count[bit], not_threshold_bit, oc::GateType::And);
    const auto greater_here =
        make_gate(equal, count_gt_threshold_at_bit, oc::GateType::And);
    greater = make_gate(greater, greater_here, oc::GateType::Or);

    const auto diff = make_gate(count[bit], threshold_bit, oc::GateType::Xor);
    const auto equal_here = make_not(diff);
    equal = make_gate(equal, equal_here, oc::GateType::And);
  }
  cd.addGate(greater, equal, oc::GateType::Or, output[0]);

  cd.levelByAndDepth();
  return cd;
}

volePSI::BetaCircuit
BuildMembershipSelectionSaturatingThresholdCircuit(size_t parties,
                                                   size_t threshold) {
  OTMPSI_ENFORCE(parties >= 2, "parties must be >= 2");
  OTMPSI_ENFORCE(threshold >= 1 && threshold <= parties,
               "threshold must satisfy 1 <= t <= N");

  const auto layout = MakeMembershipGmwLayout(parties);
  volePSI::BetaCircuit cd;
  volePSI::BetaBundle input(layout.input_bits);
  volePSI::BetaBundle output(1);
  cd.addInputBundle(input);
  cd.addOutputBundle(output);

  auto new_wire = [&]() {
    volePSI::BetaBundle tmp(1);
    cd.addTempWireBundle(tmp);
    return tmp[0];
  };
  auto make_gate = [&](oc::BetaWire a, oc::BetaWire b, oc::GateType gate) {
    auto out = new_wire();
    cd.addGate(a, b, gate, out);
    return out;
  };

  const auto one = input[layout.one_offset];
  const auto zero = input[layout.zero_offset];
  const size_t required_nonleader_hits = threshold - 1;

  oc::BetaWire over_threshold = one;
  if (required_nonleader_hits > 0) {
    std::vector<oc::BetaWire> at_least(required_nonleader_hits + 1, zero);
    at_least[0] = one;

    size_t processed = 0;
    for (size_t party = 1; party < parties; ++party) {
      const auto hit = input[layout.membership_offset + party - 1];
      auto next = at_least;
      const size_t max_k = std::min(required_nonleader_hits, processed + 1);
      for (size_t rev = 0; rev < max_k; ++rev) {
        const size_t k = max_k - rev;
        const auto candidate =
            k == 1 ? hit : make_gate(at_least[k - 1], hit, oc::GateType::And);
        next[k] = processed < k
                      ? candidate
                      : make_gate(at_least[k], candidate, oc::GateType::Or);
      }
      at_least = std::move(next);
      ++processed;
    }
    over_threshold = at_least[required_nonleader_hits];
  }

  cd.addCopy(over_threshold, output[0]);
  cd.levelByAndDepth();
  return cd;
}

volePSI::BetaCircuit
BuildMembershipSelectionThresholdCircuit(size_t parties, size_t threshold) {
  auto count_circuit = BuildMembershipSelectionCountThresholdCircuit(parties);
  auto saturating_circuit =
      BuildMembershipSelectionSaturatingThresholdCircuit(parties, threshold);
  if (NonlinearGateCount(saturating_circuit) <=
      NonlinearGateCount(count_circuit)) {
    return saturating_circuit;
  }
  return count_circuit;
}

volePSI::BetaCircuit
BuildMembershipMaskedHolderCountThresholdCircuit(size_t parties) {
  const auto layout = MakeMembershipGmwLayout(parties);
  volePSI::BetaCircuit cd;
  volePSI::BetaBundle input(layout.input_bits);
  volePSI::BetaBundle output(1 + parties);
  cd.addInputBundle(input);
  cd.addOutputBundle(output);

  auto new_wire = [&]() {
    volePSI::BetaBundle tmp(1);
    cd.addTempWireBundle(tmp);
    return tmp[0];
  };
  auto make_not = [&](oc::BetaWire a) {
    auto out = new_wire();
    cd.addInvert(a, out);
    return out;
  };
  auto make_gate = [&](oc::BetaWire a, oc::BetaWire b, oc::GateType gate) {
    auto out = new_wire();
    cd.addGate(a, b, gate, out);
    return out;
  };

  const auto one = input[layout.one_offset];
  const auto zero = input[layout.zero_offset];

  std::vector<oc::BetaWire> membership(parties);
  membership[0] = one;
  for (size_t party = 1; party < parties; ++party) {
    membership[party] = input[layout.membership_offset + party - 1];
  }

  std::vector<oc::BetaWire> count(layout.count_bits, zero);
  count[0] = one;
  for (size_t party = 1; party < parties; ++party) {
    auto carry = membership[party];
    for (size_t bit = 0; bit < layout.count_bits; ++bit) {
      auto sum = make_gate(count[bit], carry, oc::GateType::Xor);
      auto next_carry = make_gate(count[bit], carry, oc::GateType::And);
      count[bit] = sum;
      carry = next_carry;
    }
  }

  auto greater = zero;
  auto equal = one;
  for (size_t rev = 0; rev < layout.count_bits; ++rev) {
    const size_t bit = layout.count_bits - 1 - rev;
    const auto threshold_bit = input[layout.threshold_offset + bit];
    const auto not_threshold_bit = make_not(threshold_bit);
    const auto count_gt_threshold_at_bit =
        make_gate(count[bit], not_threshold_bit, oc::GateType::And);
    const auto greater_here =
        make_gate(equal, count_gt_threshold_at_bit, oc::GateType::And);
    greater = make_gate(greater, greater_here, oc::GateType::Or);

    const auto diff = make_gate(count[bit], threshold_bit, oc::GateType::Xor);
    const auto equal_here = make_not(diff);
    equal = make_gate(equal, equal_here, oc::GateType::And);
  }
  const auto over_threshold = make_gate(greater, equal, oc::GateType::Or);

  cd.addCopy(over_threshold, output[0]);
  cd.addCopy(over_threshold, output[1]);
  for (size_t party = 1; party < parties; ++party) {
    cd.addGate(over_threshold, membership[party], oc::GateType::And,
               output[1 + party]);
  }

  cd.levelByAndDepth();
  return cd;
}

volePSI::BetaCircuit
BuildMembershipMaskedHolderSaturatingThresholdCircuit(size_t parties,
                                                      size_t threshold) {
  OTMPSI_ENFORCE(parties >= 2, "parties must be >= 2");
  OTMPSI_ENFORCE(threshold >= 1 && threshold <= parties,
               "threshold must satisfy 1 <= t <= N");

  const auto layout = MakeMembershipGmwLayout(parties);
  volePSI::BetaCircuit cd;
  volePSI::BetaBundle input(layout.input_bits);
  volePSI::BetaBundle output(1 + parties);
  cd.addInputBundle(input);
  cd.addOutputBundle(output);

  auto new_wire = [&]() {
    volePSI::BetaBundle tmp(1);
    cd.addTempWireBundle(tmp);
    return tmp[0];
  };
  auto make_gate = [&](oc::BetaWire a, oc::BetaWire b, oc::GateType gate) {
    auto out = new_wire();
    cd.addGate(a, b, gate, out);
    return out;
  };

  const auto one = input[layout.one_offset];
  const auto zero = input[layout.zero_offset];
  const size_t required_nonleader_hits = threshold - 1;

  std::vector<oc::BetaWire> membership(parties);
  membership[0] = one;
  for (size_t party = 1; party < parties; ++party) {
    membership[party] = input[layout.membership_offset + party - 1];
  }

  oc::BetaWire over_threshold = one;
  if (required_nonleader_hits > 0) {
    std::vector<oc::BetaWire> at_least(required_nonleader_hits + 1, zero);
    at_least[0] = one;

    size_t processed = 0;
    for (size_t party = 1; party < parties; ++party) {
      const auto hit = membership[party];
      auto next = at_least;
      const size_t max_k = std::min(required_nonleader_hits, processed + 1);
      for (size_t rev = 0; rev < max_k; ++rev) {
        const size_t k = max_k - rev;
        const auto candidate =
            k == 1 ? hit : make_gate(at_least[k - 1], hit, oc::GateType::And);
        next[k] = processed < k
                      ? candidate
                      : make_gate(at_least[k], candidate, oc::GateType::Or);
      }
      at_least = std::move(next);
      ++processed;
    }
    over_threshold = at_least[required_nonleader_hits];
  }

  cd.addCopy(over_threshold, output[0]);
  cd.addCopy(over_threshold, output[1]);
  for (size_t party = 1; party < parties; ++party) {
    if (threshold == 1) {
      cd.addCopy(membership[party], output[1 + party]);
    } else {
      cd.addGate(over_threshold, membership[party], oc::GateType::And,
                 output[1 + party]);
    }
  }

  cd.levelByAndDepth();
  return cd;
}

volePSI::BetaCircuit
BuildMembershipMaskedHolderThresholdCircuit(size_t parties, size_t threshold) {
  auto count_circuit =
      BuildMembershipMaskedHolderCountThresholdCircuit(parties);
  auto saturating_circuit =
      BuildMembershipMaskedHolderSaturatingThresholdCircuit(parties, threshold);
  if (NonlinearGateCount(saturating_circuit) <=
      NonlinearGateCount(count_circuit)) {
    return saturating_circuit;
  }
  return count_circuit;
}

volePSI::BetaCircuit BuildArithmeticCountThresholdCircuit(size_t parties) {
  const auto layout = MakeArithmeticCountGmwLayout(parties);
  volePSI::BetaCircuit cd;
  volePSI::BetaBundle input(layout.input_bits);
  volePSI::BetaBundle output(layout.output_bits);
  cd.addInputBundle(input);
  cd.addOutputBundle(output);

  auto new_wire = [&]() {
    volePSI::BetaBundle tmp(1);
    cd.addTempWireBundle(tmp);
    return tmp[0];
  };
  auto make_not = [&](oc::BetaWire a) {
    auto out = new_wire();
    cd.addInvert(a, out);
    return out;
  };
  auto make_gate = [&](oc::BetaWire a, oc::BetaWire b, oc::GateType gate) {
    auto out = new_wire();
    cd.addGate(a, b, gate, out);
    return out;
  };

  const auto one = input[layout.one_offset];
  const auto zero = input[layout.zero_offset];

  std::vector<oc::BetaWire> membership(parties);
  membership[0] = one;
  for (size_t party = 1; party < parties; ++party) {
    membership[party] = input[layout.membership_offset + party - 1];
  }

  std::vector<oc::BetaWire> count(layout.count_bits);
  for (size_t bit = 0; bit < layout.count_bits; ++bit) {
    count[bit] = input[layout.count_share_offset + bit];
  }
  for (size_t party = 1; party < parties; ++party) {
    auto carry = zero;
    for (size_t bit = 0; bit < layout.count_bits; ++bit) {
      const auto rhs =
          input[layout.count_share_offset + party * layout.count_bits + bit];
      const auto sum_without_carry =
          make_gate(count[bit], rhs, oc::GateType::Xor);
      const auto carry_from_inputs =
          make_gate(count[bit], rhs, oc::GateType::And);
      if (bit == 0) {
        count[bit] = sum_without_carry;
        carry = carry_from_inputs;
        continue;
      }
      const auto sum = make_gate(sum_without_carry, carry, oc::GateType::Xor);
      const auto carry_from_chain =
          make_gate(sum_without_carry, carry, oc::GateType::And);
      carry = make_gate(carry_from_inputs, carry_from_chain, oc::GateType::Xor);
      count[bit] = sum;
    }
  }

  auto greater = zero;
  auto equal = one;
  for (size_t rev = 0; rev < layout.count_bits; ++rev) {
    const size_t bit = layout.count_bits - 1 - rev;
    const auto threshold_bit = input[layout.threshold_offset + bit];
    const auto not_threshold_bit = make_not(threshold_bit);
    const auto count_gt_threshold_at_bit =
        make_gate(count[bit], not_threshold_bit, oc::GateType::And);
    const auto greater_here =
        make_gate(equal, count_gt_threshold_at_bit, oc::GateType::And);
    greater = make_gate(greater, greater_here, oc::GateType::Or);

    const auto diff = make_gate(count[bit], threshold_bit, oc::GateType::Xor);
    const auto equal_here = make_not(diff);
    equal = make_gate(equal, equal_here, oc::GateType::And);
  }
  const auto over_threshold = make_gate(greater, equal, oc::GateType::Or);

  cd.addCopy(over_threshold, output[0]);
  for (size_t bit = 0; bit < layout.count_bits; ++bit) {
    cd.addGate(over_threshold, count[bit], oc::GateType::And, output[1 + bit]);
  }
  cd.addCopy(over_threshold, output[1 + layout.count_bits]);
  for (size_t party = 1; party < parties; ++party) {
    cd.addGate(over_threshold, membership[party], oc::GateType::And,
               output[1 + layout.count_bits + party]);
  }

  cd.levelByAndDepth();
  return cd;
}

volePSI::BetaCircuit BuildMaskedCountThresholdCircuit(size_t parties) {
  const auto layout = MakeMaskedCountGmwLayout(parties);
  volePSI::BetaCircuit cd;
  volePSI::BetaBundle input(layout.input_bits);
  volePSI::BetaBundle output(layout.output_bits);
  cd.addInputBundle(input);
  cd.addOutputBundle(output);

  auto new_wire = [&]() {
    volePSI::BetaBundle tmp(1);
    cd.addTempWireBundle(tmp);
    return tmp[0];
  };
  auto make_gate = [&](oc::BetaWire a, oc::BetaWire b, oc::GateType gate) {
    auto out = new_wire();
    cd.addGate(a, b, gate, out);
    return out;
  };
  auto add_mod = [&](const std::vector<oc::BetaWire> &lhs,
                     const std::vector<oc::BetaWire> &rhs, size_t out_bits) {
    OTMPSI_ENFORCE(lhs.size() >= out_bits && rhs.size() >= out_bits,
                 "adder input width mismatch");
    const auto zero = input[layout.zero_offset];
    std::vector<oc::BetaWire> out(out_bits);
    auto carry = zero;
    for (size_t bit = 0; bit < out_bits; ++bit) {
      const auto sum_without_carry =
          make_gate(lhs[bit], rhs[bit], oc::GateType::Xor);
      const auto carry_from_inputs =
          make_gate(lhs[bit], rhs[bit], oc::GateType::And);
      if (bit == 0) {
        out[bit] = sum_without_carry;
        carry = carry_from_inputs;
        continue;
      }
      out[bit] = make_gate(sum_without_carry, carry, oc::GateType::Xor);
      const auto carry_from_chain =
          make_gate(sum_without_carry, carry, oc::GateType::And);
      carry = make_gate(carry_from_inputs, carry_from_chain, oc::GateType::Xor);
    }
    return out;
  };

  const auto one = input[layout.one_offset];

  std::vector<oc::BetaWire> mask_bits(layout.compare_bits);
  std::vector<oc::BetaWire> opened_bits(layout.compare_bits);
  std::vector<oc::BetaWire> threshold_bits(layout.compare_bits);
  for (size_t bit = 0; bit < layout.compare_bits; ++bit) {
    mask_bits[bit] = input[layout.mask_bit_offset + bit];
    opened_bits[bit] = input[layout.opened_mask_offset + bit];
    threshold_bits[bit] = input[layout.threshold_offset + bit];
  }

  const auto diff_bits = add_mod(opened_bits, mask_bits, layout.compare_bits);
  const auto is_negative = diff_bits[layout.compare_bits - 1];
  const auto over_threshold = make_gate(is_negative, one, oc::GateType::Xor);

  const auto count_bits_full =
      add_mod(diff_bits, threshold_bits, layout.compare_bits);

  cd.addCopy(over_threshold, output[0]);
  for (size_t bit = 0; bit < layout.count_bits; ++bit) {
    cd.addGate(over_threshold, count_bits_full[bit], oc::GateType::And,
               output[1 + bit]);
  }

  cd.addCopy(over_threshold, output[1 + layout.count_bits]);
  for (size_t party = 1; party < parties; ++party) {
    cd.addGate(over_threshold, input[layout.membership_offset + party - 1],
               oc::GateType::And, output[1 + layout.count_bits + party]);
  }

  cd.levelByAndDepth();
  return cd;
}

volePSI::BetaCircuit BuildMaskedHolderThresholdCircuit(size_t parties) {
  const auto layout = MakeMaskedCountGmwLayout(parties);
  volePSI::BetaCircuit cd;
  volePSI::BetaBundle input(layout.input_bits);
  volePSI::BetaBundle output(1 + parties);
  cd.addInputBundle(input);
  cd.addOutputBundle(output);

  auto new_wire = [&]() {
    volePSI::BetaBundle tmp(1);
    cd.addTempWireBundle(tmp);
    return tmp[0];
  };
  auto make_gate = [&](oc::BetaWire a, oc::BetaWire b, oc::GateType gate) {
    auto out = new_wire();
    cd.addGate(a, b, gate, out);
    return out;
  };
  auto add_mod = [&](const std::vector<oc::BetaWire> &lhs,
                     const std::vector<oc::BetaWire> &rhs, size_t out_bits) {
    OTMPSI_ENFORCE(lhs.size() >= out_bits && rhs.size() >= out_bits,
                 "adder input width mismatch");
    const auto zero = input[layout.zero_offset];
    std::vector<oc::BetaWire> out(out_bits);
    auto carry = zero;
    for (size_t bit = 0; bit < out_bits; ++bit) {
      const auto sum_without_carry =
          make_gate(lhs[bit], rhs[bit], oc::GateType::Xor);
      const auto carry_from_inputs =
          make_gate(lhs[bit], rhs[bit], oc::GateType::And);
      if (bit == 0) {
        out[bit] = sum_without_carry;
        carry = carry_from_inputs;
        continue;
      }
      out[bit] = make_gate(sum_without_carry, carry, oc::GateType::Xor);
      const auto carry_from_chain =
          make_gate(sum_without_carry, carry, oc::GateType::And);
      carry = make_gate(carry_from_inputs, carry_from_chain, oc::GateType::Xor);
    }
    return out;
  };

  const auto one = input[layout.one_offset];

  std::vector<oc::BetaWire> mask_bits(layout.compare_bits);
  std::vector<oc::BetaWire> opened_bits(layout.compare_bits);
  for (size_t bit = 0; bit < layout.compare_bits; ++bit) {
    mask_bits[bit] = input[layout.mask_bit_offset + bit];
    opened_bits[bit] = input[layout.opened_mask_offset + bit];
  }

  const auto diff_bits = add_mod(opened_bits, mask_bits, layout.compare_bits);
  const auto is_negative = diff_bits[layout.compare_bits - 1];
  const auto over_threshold = make_gate(is_negative, one, oc::GateType::Xor);

  cd.addCopy(over_threshold, output[0]);
  cd.addCopy(over_threshold, output[1]);
  for (size_t party = 1; party < parties; ++party) {
    cd.addGate(over_threshold, input[layout.membership_offset + party - 1],
               oc::GateType::And, output[1 + party]);
  }

  cd.levelByAndDepth();
  return cd;
}

volePSI::BetaCircuit BuildMaskedCountCompareCircuit(size_t parties) {
  const auto layout = MakeMaskedCountGmwLayout(parties);
  volePSI::BetaCircuit cd;
  volePSI::BetaBundle input(layout.input_bits);
  volePSI::BetaBundle output(1);
  cd.addInputBundle(input);
  cd.addOutputBundle(output);

  auto new_wire = [&]() {
    volePSI::BetaBundle tmp(1);
    cd.addTempWireBundle(tmp);
    return tmp[0];
  };
  auto make_gate = [&](oc::BetaWire a, oc::BetaWire b, oc::GateType gate) {
    auto out = new_wire();
    cd.addGate(a, b, gate, out);
    return out;
  };

  const auto zero = input[layout.zero_offset];
  const auto one = input[layout.one_offset];
  std::vector<oc::BetaWire> mask_bits(layout.compare_bits);
  std::vector<oc::BetaWire> opened_bits(layout.compare_bits);
  for (size_t bit = 0; bit < layout.compare_bits; ++bit) {
    mask_bits[bit] = input[layout.mask_bit_offset + bit];
    opened_bits[bit] = input[layout.opened_mask_offset + bit];
  }

  auto carry = zero;
  std::vector<oc::BetaWire> diff_bits(layout.compare_bits);
  for (size_t bit = 0; bit < layout.compare_bits; ++bit) {
    const auto sum_without_carry =
        make_gate(opened_bits[bit], mask_bits[bit], oc::GateType::Xor);
    const auto carry_from_inputs =
        make_gate(opened_bits[bit], mask_bits[bit], oc::GateType::And);
    if (bit == 0) {
      diff_bits[bit] = sum_without_carry;
      carry = carry_from_inputs;
      continue;
    }
    diff_bits[bit] = make_gate(sum_without_carry, carry, oc::GateType::Xor);
    const auto carry_from_chain =
        make_gate(sum_without_carry, carry, oc::GateType::And);
    carry = make_gate(carry_from_inputs, carry_from_chain, oc::GateType::Xor);
  }

  const auto is_negative = diff_bits[layout.compare_bits - 1];
  const auto over_threshold = make_gate(is_negative, one, oc::GateType::Xor);
  cd.addCopy(over_threshold, output[0]);
  cd.levelByAndDepth();
  return cd;
}

volePSI::BetaCircuit BuildAdditiveMaskedCountCompareCircuit(size_t parties) {
  const auto layout = MakeAdditiveMaskedCompareLayout(parties);
  volePSI::BetaCircuit cd;
  volePSI::BetaBundle input(layout.input_bits);
  volePSI::BetaBundle output(1);
  cd.addInputBundle(input);
  cd.addOutputBundle(output);

  auto new_wire = [&]() {
    volePSI::BetaBundle tmp(1);
    cd.addTempWireBundle(tmp);
    return tmp[0];
  };
  auto make_gate = [&](oc::BetaWire a, oc::BetaWire b, oc::GateType gate) {
    auto out = new_wire();
    cd.addGate(a, b, gate, out);
    return out;
  };
  auto add_mod = [&](const std::vector<oc::BetaWire> &lhs,
                     const std::vector<oc::BetaWire> &rhs) {
    OTMPSI_ENFORCE(lhs.size() == layout.compare_bits &&
                     rhs.size() == layout.compare_bits,
                 "additive masked compare input width mismatch");
    const auto zero = input[layout.zero_offset];
    std::vector<oc::BetaWire> out(layout.compare_bits);
    auto carry = zero;
    for (size_t bit = 0; bit < layout.compare_bits; ++bit) {
      const auto sum_without_carry =
          make_gate(lhs[bit], rhs[bit], oc::GateType::Xor);
      const auto carry_from_inputs =
          make_gate(lhs[bit], rhs[bit], oc::GateType::And);
      if (bit == 0) {
        out[bit] = sum_without_carry;
        carry = carry_from_inputs;
        continue;
      }
      out[bit] = make_gate(sum_without_carry, carry, oc::GateType::Xor);
      const auto carry_from_chain =
          make_gate(sum_without_carry, carry, oc::GateType::And);
      carry = make_gate(carry_from_inputs, carry_from_chain, oc::GateType::Xor);
    }
    return out;
  };

  std::vector<oc::BetaWire> acc(layout.compare_bits);
  for (size_t bit = 0; bit < layout.compare_bits; ++bit) {
    acc[bit] = input[layout.opened_mask_offset + bit];
  }
  for (size_t party = 0; party < parties; ++party) {
    std::vector<oc::BetaWire> rhs(layout.compare_bits);
    for (size_t bit = 0; bit < layout.compare_bits; ++bit) {
      rhs[bit] =
          input[layout.random_share_offset + party * layout.compare_bits + bit];
    }
    acc = add_mod(acc, rhs);
  }

  const auto is_negative = acc[layout.compare_bits - 1];
  const auto over_threshold =
      make_gate(is_negative, input[layout.one_offset], oc::GateType::Xor);
  cd.addCopy(over_threshold, output[0]);
  cd.levelByAndDepth();
  return cd;
}

} // namespace examples::otmpsi
