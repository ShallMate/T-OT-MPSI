#include <iostream>
#include <vector>

#include "libOTe/Tools/BlkAccCode/BlkAccCode.h"
#include "libOTe/TwoChooseOne/ConfigureCode.h"
#include "otmpsi/compat/error.h"

namespace {
using namespace osuCrypto;

std::vector<block> Input(size_t n, u64 seed) {
  PRNG prng(toBlock(seed));
  std::vector<block> values(n);
  prng.get(values.data(), values.size());
  return values;
}

void CheckAccumulator() {
  for (u64 n : {8ULL, 64ULL, 1024ULL, 65536ULL, 524288ULL}) {
    Accumulator<Feistel2KPerm> code(n);
    for (u64 seed : {41ULL, 71ULL}) {
      auto input = Input(n, seed);
      std::vector<block> actual(n), reference(n);
      code.dualEncode<block>(input.data(), actual.data(), CoeffCtxGF2{});
      // Non-pointer iterators select the unchanged generic implementation.
      code.dualEncode<block>(input.begin(), reference.begin(), CoeffCtxGF2{});
      OTMPSI_ENFORCE(actual == reference, "cached accumulator mismatch n={}", n);
    }
  }
}

void CheckBlockDiagonal() {
  for (u64 k : {32ULL, 33ULL, 96ULL, 32768ULL, 262144ULL}) {
    for (u64 seed : {91ULL, 101ULL}) {
      BlockDiagonal<> code(k, 2 * k, 32, toBlock(seed));
      for (u64 input_seed : {131ULL, 151ULL}) {
        auto actual = Input(2 * k, input_seed);
        auto reference = actual;
        code.dualEncode<block>(actual.data(), CoeffCtxGF2{});
        code.dualEncode<block>(reference.begin(), CoeffCtxGF2{});
        OTMPSI_ENFORCE(actual == reference,
                       "subset-XOR block code mismatch k={} seed={}", k, seed);
      }
    }
  }
}

void CheckCompleteCode() {
  for (u64 k : {1024ULL, 32768ULL, 262144ULL}) {
    BlkAccCode code;
    code.init(k, 2 * k, 32, 3);
    auto actual = Input(2 * k, k + 17);
    auto reference = actual;
    code.dualEncode<block>(actual.data(), CoeffCtxGF2{});
    std::vector<block> next(2 * k);
    for (u64 round = 0; round < 2; ++round) {
      Accumulator<Feistel2KPerm> accumulator(
          2 * k, Feistel2KPerm(2 * k, code.subseed(round)));
      accumulator.dualEncode<block>(reference.begin(), next.begin(), CoeffCtxGF2{});
      reference.swap(next);
    }
    BlockDiagonal<> diagonal(k, 2 * k, 32, code.subseed(2));
    diagonal.dualEncode<block>(reference.begin(), CoeffCtxGF2{});
    OTMPSI_ENFORCE(actual == reference, "complete code mismatch k={}", k);
  }
}
} // namespace

int main() {
  try {
    osuCrypto::u64 scaler = 0, weight = 0;
    double distance = 0;
    osuCrypto::EAConfigure(osuCrypto::MultType::ExAcc11, scaler, weight, distance);
    OTMPSI_ENFORCE(scaler == 5 && weight == 11 && distance == 0.15,
                   "EA parameter order mismatch");
    CheckAccumulator();
    CheckBlockDiagonal();
    CheckCompleteCode();
    std::cout << "Silent-OT code equivalence checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
