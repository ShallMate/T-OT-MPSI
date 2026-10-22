#include "otmpsi/pairwise_zero_phase.h"

#include <cstdlib>
#include <limits>

#include "otmpsi/gmw_inputs.h"

namespace examples::otmpsi {
namespace {

size_t ParsePositiveEnvSize(const char *name, size_t fallback) {
  const char *env = std::getenv(name);
  if (env == nullptr || env[0] == '\0') {
    return fallback;
  }
  char *end = nullptr;
  const auto value = std::strtoull(env, &end, 10);
  OTMPSI_ENFORCE(end != env && *end == '\0' && value > 0,
               "{} must be a positive integer, got {}", name, env);
  return static_cast<size_t>(value);
}

size_t CeilLog2(size_t value) {
  if (value <= 1) {
    return 0;
  }
  --value;
  size_t bits = 0;
  while (value != 0) {
    value >>= 1U;
    ++bits;
  }
  return bits;
}

} // namespace

volePSI::Matrix<volePSI::u8> MakeGmwInput(const std::vector<uint128_t> &shares,
                                          size_t bit_width) {
  const size_t num_bytes = (bit_width + 7) / 8;
  volePSI::Matrix<volePSI::u8> input;
  input.resize(shares.size(), num_bytes);
  if (input.size() != 0) {
    std::memset(input.data(), 0, input.size() * sizeof(volePSI::u8));
  }
  otmpsi_compat::ParallelFor(0, shares.size(), [&](int64_t begin, int64_t end) {
    for (int64_t i = begin; i < end; ++i) {
      std::memcpy(input[static_cast<size_t>(i)].data(),
                  &shares[static_cast<size_t>(i)], num_bytes);
    }
  });
  return input;
}

size_t PeqtBitWidth(const OtmpsiParams &params) {
  constexpr size_t kDefaultStatisticalSecurity = 48;
  const size_t security = ParsePositiveEnvSize("OTMPSI_BANDOKVS_SECURITY",
                                               kDefaultStatisticalSecurity);
  const size_t pair_count = std::max<size_t>(1, params.parties - 1);
  OTMPSI_ENFORCE(params.set_size <=
                   std::numeric_limits<size_t>::max() / pair_count,
               "PEQT bit-width calculation overflow");
  const size_t default_bits = security + CeilLog2(params.set_size * pair_count);
  const size_t peqt_bits = ParsePositiveEnvSize(
      "OTMPSI_PEQ_BITS", std::max<size_t>(8, default_bits));
  OTMPSI_ENFORCE(peqt_bits <= 128, "PEQT bit width must be <= 128, got {}",
               peqt_bits);
  OTMPSI_ENFORCE(peqt_bits >= default_bits,
               "PEQT bit width {} is below the default security bound {}",
               peqt_bits, default_bits);
  return peqt_bits;
}

size_t PeqtTripleBatchSize(const OtmpsiParams &params) {
  // Keep small instances cache-sized. For the paper's power-of-two set sizes,
  // the chosen batch divides 2*n*AND_gates without redistributing the batch
  // into non-power-of-two encodings. Larger instances amortize base-OT setup.
  const size_t batch = std::max<size_t>(
      size_t{1} << 15, 2 * std::min<size_t>(params.set_size, size_t{1} << 17));
  return ParsePositiveEnvSize("OTMPSI_PEQ_TRIPLE_BATCH_SIZE", batch);
}

std::vector<uint8_t>
DecodeBitShareOutput(const volePSI::Matrix<volePSI::u8> &out,
                     size_t row_count) {
  OTMPSI_ENFORCE(static_cast<size_t>(out.rows()) == row_count,
               "bit output row count mismatch");
  std::vector<uint8_t> bits(row_count, 0);
  for (size_t row = 0; row < row_count; ++row) {
    bits[row] = GetPackedBit(out[row].data(), 0) ? 1 : 0;
  }
  return bits;
}

ShareWithStats PairwiseZeroSide(const std::vector<uint128_t> &shares,
                                size_t party_idx, int rank,
                                const OtmpsiParams &params,
                                const std::string &tag) {
  const auto start = std::chrono::steady_clock::now();
  auto sock = AcquireLocalSocket(tag, rank);
  const size_t peqt_bits = PeqtBitWidth(params);
  const auto input_start = std::chrono::steady_clock::now();
  auto input = MakeGmwInput(shares, peqt_bits);
  const double input_time_s = SecondsSince(input_start);
  const auto init_start = std::chrono::steady_clock::now();
  auto cir = volePSI::isZeroCircuit(peqt_bits);

  volePSI::Gmw gmw;
  gmw.init(params.set_size, cir, std::max<size_t>(1, params.threads),
           static_cast<volePSI::u64>(rank), oc::sysRandomSeed());
  gmw.setInput(0, input);
  const double init_time_s = SecondsSince(init_start);
  const auto triple_start = std::chrono::steady_clock::now();
  const size_t triple_batch_size = PeqtTripleBatchSize(params);
  macoro::sync_wait(gmw.generateTriple(
      static_cast<volePSI::u64>(triple_batch_size),
      std::max<size_t>(1, params.threads), sock));
  macoro::sync_wait(sock.flush());
  const double triple_time_s = SecondsSince(triple_start);
  const auto run_start = std::chrono::steady_clock::now();
  macoro::sync_wait(gmw.run(sock));
  macoro::sync_wait(sock.flush());
  const double run_time_s = SecondsSince(run_start);

  const auto output_start = std::chrono::steady_clock::now();
  volePSI::Matrix<volePSI::u8> out;
  out.resize(params.set_size, 1);
  gmw.getOutput(0, out);
  auto bit_shares = DecodeBitShareOutput(out, params.set_size);
  std::vector<uint128_t> packed(bit_shares.size(), 0);
  for (size_t i = 0; i < bit_shares.size(); ++i) {
    packed[i] = bit_shares[i];
  }
  const double output_time_s = SecondsSince(output_start);
  LogDebug("pairwise-zero", "party=" + std::to_string(party_idx) +
                                ", rank=" + std::to_string(rank) +
                                ", peqt_bits=" + std::to_string(peqt_bits) +
                                ", triple_batch_size=" +
                                std::to_string(triple_batch_size) +
                                ", input_s=" + std::to_string(input_time_s) +
                                ", init_s=" + std::to_string(init_time_s) +
                                ", triple_s=" + std::to_string(triple_time_s) +
                                ", run_s=" + std::to_string(run_time_s) +
                                ", output_s=" + std::to_string(output_time_s) +
                                " done");
  return {
      std::move(packed),
      RoleStats{SecondsSince(start),
                static_cast<size_t>(sock.bytesSent() + sock.bytesReceived())}};
}

} // namespace examples::otmpsi
