#include "otmpsi/public_t_compare.h"

#include <cstring>

namespace examples::otmpsi {

PublicTCompareLayout MakePublicTCompareLayout() {
  static_assert(kComparisonBits == 8,
                "public-T carry circuit is specialized for 8-bit comparison");
  return {};
}

volePSI::BetaCircuit BuildPublicTMaskedCompareCircuit() {
  const auto layout = MakePublicTCompareLayout();
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

  // For public T and Boolean-shared R, p_i = T_i xor R_i and
  // g_i = T_i & R_i are available as XOR shares without interaction.
  // c_1 = g_0 and c_{i+1} = g_i xor (p_i & c_i). Only carry into the
  // sign bit is needed, so i ranges from 1 through 6 for an 8-bit value.
  auto carry = input[layout.generate_offset];
  for (size_t bit = 1; bit + 1 < kComparisonBits; ++bit) {
    const auto propagated = make_gate(input[layout.propagate_offset + bit],
                                      carry, oc::GateType::And);
    carry = make_gate(input[layout.generate_offset + bit], propagated,
                      oc::GateType::Xor);
  }

  const auto sign =
      make_gate(input[layout.propagate_offset + kComparisonBits - 1], carry,
                oc::GateType::Xor);
  cd.addGate(sign, input[layout.one_offset], oc::GateType::Xor, output[0]);
  cd.levelByAndDepth();
  return cd;
}

volePSI::Matrix<volePSI::u8> MakePublicTMaskedCompareInput(
    size_t party_id, const std::vector<uint64_t> &opened_values,
    const std::vector<uint64_t> &mask_bit_share, const OtmpsiParams &params) {
  OTMPSI_ENFORCE(party_id < params.parties, "party index out of range");
  OTMPSI_ENFORCE(CompareBitsForPartyCount(params.parties) == kComparisonBits,
               "public-T comparison requires fixed 8-bit comparison");
  OTMPSI_ENFORCE(opened_values.size() == params.set_size &&
                   mask_bit_share.size() == params.set_size,
               "public-T comparison input row mismatch");

  const auto layout = MakePublicTCompareLayout();
  volePSI::Matrix<volePSI::u8> input;
  input.resize(params.set_size, (layout.input_bits + 7) / 8);
  if (input.size() != 0) {
    std::memset(input.data(), 0, input.size() * sizeof(volePSI::u8));
  }

  otmpsi_compat::ParallelFor(0, params.set_size, [&](int64_t begin, int64_t end) {
    for (int64_t r = begin; r < end; ++r) {
      const size_t row = static_cast<size_t>(r);
      auto *packed = input[row].data();
      const uint64_t public_t = opened_values[row] & 0xffU;
      const uint64_t r_share = mask_bit_share[row] & 0xffU;
      for (size_t bit = 0; bit < kComparisonBits; ++bit) {
        const bool t_bit = ((public_t >> bit) & 1U) != 0;
        const bool r_bit = ((r_share >> bit) & 1U) != 0;
        // Party zero holds the canonical public XOR share. XORing all party
        // inputs therefore reconstructs T_i xor R_i for every party count.
        SetPackedBit(packed, layout.propagate_offset + bit,
                     r_bit ^ (party_id == 0 && t_bit));
        if (bit + 1 < kComparisonBits) {
          // Multiplication by a public bit is local on every XOR share.
          SetPackedBit(packed, layout.generate_offset + bit, r_bit && t_bit);
        }
      }
      SetPackedBit(packed, layout.one_offset, party_id == 0);
    }
  });
  return input;
}

DistributedSharedOutput RunPublicTMaskedCompareGmwParty(
    size_t party_id, const std::vector<uint64_t> &opened_values,
    const std::vector<uint64_t> &mask_bit_share, const OtmpsiParams &params,
    const std::string &scope) {
  auto input = MakePublicTMaskedCompareInput(party_id, opened_values,
                                             mask_bit_share, params);
  return RunNPartySharedOutputGmwParty(input, party_id, params, scope,
                                       BuildPublicTMaskedCompareCircuit(), 1);
}

} // namespace examples::otmpsi
