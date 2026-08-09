#include <future>
#include <iostream>

#include "otmpsi/public_t_compare.h"

namespace examples::otmpsi {
namespace {

bool IsNonlinear(oc::GateType type) {
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

void SetCircuitInput(oc::BitVector &input, uint8_t public_t, uint8_t mask) {
  const auto layout = MakePublicTCompareLayout();
  for (size_t bit = 0; bit < kComparisonBits; ++bit) {
    const bool t_bit = ((public_t >> bit) & 1U) != 0;
    const bool r_bit = ((mask >> bit) & 1U) != 0;
    input[layout.propagate_offset + bit] = t_bit ^ r_bit;
    if (bit + 1 < kComparisonBits) {
      input[layout.generate_offset + bit] = t_bit && r_bit;
    }
  }
  input[layout.one_offset] = true;
}

void CheckCircuitExhaustively() {
  auto cir = BuildPublicTMaskedCompareCircuit();
  size_t nonlinear = 0;
  for (const auto &gate : cir.mGates) {
    nonlinear += IsNonlinear(gate.mType) ? 1 : 0;
  }
  OTMPSI_ENFORCE(nonlinear == kPublicTCompareAndGates,
               "public-T compare nonlinear gate mismatch: {}", nonlinear);

  const auto layout = MakePublicTCompareLayout();
  for (size_t public_t = 0; public_t < 256; ++public_t) {
    for (size_t mask = 0; mask < 256; ++mask) {
      oc::BitVector input(layout.input_bits);
      oc::BitVector output(1);
      SetCircuitInput(input, static_cast<uint8_t>(public_t),
                      static_cast<uint8_t>(mask));
      cir.evaluate({&input, 1}, {&output, 1}, false);
      const bool expected = (((public_t + mask) & 0x80U) == 0);
      OTMPSI_ENFORCE(output[0] == expected,
                   "public-T circuit mismatch for T={} R={}", public_t, mask);
    }
  }
}

std::vector<std::vector<uint64_t>>
MakeMaskShares(size_t parties, const std::vector<uint64_t> &mask) {
  std::vector<std::vector<uint64_t>> shares(
      parties, std::vector<uint64_t>(mask.size(), 0));
  for (size_t row = 0; row < mask.size(); ++row) {
    uint64_t acc = 0;
    for (size_t party = 0; party + 1 < parties; ++party) {
      const uint64_t share =
          (row * 73 + party * 151 + parties * 19 + 0x5aU) & 0xffU;
      shares[party][row] = share;
      acc ^= share;
    }
    shares[parties - 1][row] = acc ^ mask[row];
  }
  return shares;
}

void CheckPackedInputsForEveryPartyCount() {
  constexpr size_t rows = 129;
  std::vector<uint64_t> opened(rows);
  std::vector<uint64_t> mask(rows);
  for (size_t row = 0; row < rows; ++row) {
    opened[row] = (37 * row + 11) & 0xffU;
    mask[row] = (109 * row + 7) & 0xffU;
  }

  const auto layout = MakePublicTCompareLayout();
  for (size_t parties = 2; parties <= 127; ++parties) {
    OtmpsiParams params;
    params.parties = parties;
    params.set_size = rows;
    auto mask_shares = MakeMaskShares(parties, mask);
    std::vector<volePSI::Matrix<volePSI::u8>> inputs;
    inputs.reserve(parties);
    for (size_t party = 0; party < parties; ++party) {
      inputs.push_back(MakePublicTMaskedCompareInput(
          party, opened, mask_shares[party], params));
    }

    for (size_t row = 0; row < rows; ++row) {
      for (size_t bit = 0; bit < kComparisonBits; ++bit) {
        bool propagate = false;
        bool generate = false;
        for (size_t party = 0; party < parties; ++party) {
          propagate ^= GetPackedBit(inputs[party][row].data(),
                                    layout.propagate_offset + bit);
          if (bit + 1 < kComparisonBits) {
            generate ^= GetPackedBit(inputs[party][row].data(),
                                     layout.generate_offset + bit);
          }
        }
        const bool t_bit = ((opened[row] >> bit) & 1U) != 0;
        const bool r_bit = ((mask[row] >> bit) & 1U) != 0;
        OTMPSI_ENFORCE(propagate == (t_bit ^ r_bit),
                     "public-T propagate sharing mismatch N={} row={} bit={}",
                     parties, row, bit);
        if (bit + 1 < kComparisonBits) {
          OTMPSI_ENFORCE(generate == (t_bit && r_bit),
                       "public-T generate sharing mismatch N={} row={} bit={}",
                       parties, row, bit);
        }
      }
      bool one = false;
      for (size_t party = 0; party < parties; ++party) {
        one ^= GetPackedBit(inputs[party][row].data(), layout.one_offset);
      }
      OTMPSI_ENFORCE(one, "public-T one sharing mismatch N={} row={}", parties,
                   row);
    }
  }
}

void CheckDistributedRun(size_t parties, size_t rows, uint16_t base_port) {
  OtmpsiParams params;
  params.parties = parties;
  params.set_size = rows;
  params.threads = 1;
  params.base_port = base_port;

  std::vector<uint64_t> opened(rows);
  std::vector<uint64_t> mask(rows);
  for (size_t row = 0; row < rows; ++row) {
    opened[row] = (29 * row + 0xf3U) & 0xffU;
    mask[row] = (67 * row + 0x31U) & 0xffU;
  }
  auto mask_shares = MakeMaskShares(parties, mask);

  std::vector<std::future<DistributedSharedOutput>> jobs(parties);
  for (size_t party = 0; party < parties; ++party) {
    jobs[party] = std::async(std::launch::async, [&, party] {
      return RunPublicTMaskedCompareGmwParty(party, opened, mask_shares[party],
                                             params, "public-t-compare-smoke");
    });
  }

  std::vector<DistributedSharedOutput> runs(parties);
  for (size_t party = 0; party < parties; ++party) {
    runs[party] = jobs[party].get();
    OTMPSI_ENFORCE(runs[party].output_share.size() == rows,
                 "public-T output share row mismatch");
  }
  for (size_t row = 0; row < rows; ++row) {
    uint64_t opened_output = 0;
    for (size_t party = 0; party < parties; ++party) {
      opened_output ^= runs[party].output_share[row] & 1U;
    }
    const uint64_t expected = (((opened[row] + mask[row]) & 0x80U) == 0);
    OTMPSI_ENFORCE(opened_output == expected,
                 "distributed public-T compare mismatch N={} row={} T={} R={} "
                 "got={} expected={} shares=({}, {})",
                 parties, row, opened[row], mask[row], opened_output, expected,
                 runs[0].output_share[row], runs[1].output_share[row]);
  }
}

} // namespace
} // namespace examples::otmpsi

int main() {
  examples::otmpsi::CheckCircuitExhaustively();
  examples::otmpsi::CheckPackedInputsForEveryPartyCount();
  examples::otmpsi::CheckDistributedRun(2, 127, 25000);
  examples::otmpsi::CheckDistributedRun(3, 128, 25100);
  examples::otmpsi::CheckDistributedRun(5, 129, 25200);
  std::cout << "public_t_compare_smoke=ok\n";
  std::cout << "comparison_bits=" << examples::otmpsi::kComparisonBits << "\n";
  std::cout << "nonlinear_gates=" << examples::otmpsi::kPublicTCompareAndGates
            << "\n";
  return 0;
}
