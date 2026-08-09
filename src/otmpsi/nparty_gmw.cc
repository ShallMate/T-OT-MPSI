#include "otmpsi/nparty_gmw.h"

#include <immintrin.h>

namespace examples::otmpsi {

struct NPartyGmwGateShape {
  bool invert_a = false;
  bool invert_b = false;
  bool invert_out = false;
};

bool NormalizeNPartyAndGate(oc::GateType type, NPartyGmwGateShape &shape) {
  switch (type) {
  case oc::GateType::And:
    shape = {};
    return true;
  case oc::GateType::Nand:
    shape = {false, false, true};
    return true;
  case oc::GateType::na_And:
    shape = {true, false, false};
    return true;
  case oc::GateType::nb_And:
    shape = {false, true, false};
    return true;
  case oc::GateType::Or:
    shape = {true, true, true};
    return true;
  case oc::GateType::Nor:
    shape = {true, true, false};
    return true;
  case oc::GateType::na_Or:
    shape = {false, true, true};
    return true;
  case oc::GateType::nb_Or:
    shape = {true, false, true};
    return true;
  default:
    return false;
  }
}

using NPartyWireShares = std::vector<std::vector<std::vector<uint128_t>>>;

uint128_t AllOneU128() { return ~uint128_t{0}; }

bool UseAvx512() {
#if defined(__x86_64__)
  if (const char *mode = std::getenv("OTMPSI_GMW_SIMD");
      mode == nullptr || std::string_view(mode) != "avx512") {
    return false;
  }
  static const bool enabled = __builtin_cpu_supports("avx512f");
  return enabled;
#else
  return false;
#endif
}

bool UseGmwWordSimd() {
  const char *mode = std::getenv("OTMPSI_GMW_SIMD");
  if (mode == nullptr) {
    return false;
  }
  const std::string_view value(mode);
  return value == "avx2" || value == "avx512";
}

void FillWords(std::vector<uint128_t> &dst, uint8_t byte) {
  if (!dst.empty()) {
    std::memset(dst.data(), byte, dst.size() * sizeof(uint128_t));
  }
}

void CopyWords(std::vector<uint128_t> &dst, const std::vector<uint128_t> &src) {
  if (dst.data() == src.data() || src.empty()) {
    return;
  }
  std::memcpy(dst.data(), src.data(), src.size() * sizeof(uint128_t));
}

void XorWords(uint128_t *dst, const uint128_t *lhs, const uint128_t *rhs,
              size_t word_count) {
  auto *dst64 = reinterpret_cast<uint64_t *>(dst);
  const auto *lhs64 = reinterpret_cast<const uint64_t *>(lhs);
  const auto *rhs64 = reinterpret_cast<const uint64_t *>(rhs);
  const size_t lanes = word_count * 2;
  size_t lane = 0;
  if (UseAvx512()) {
    for (; lane + 8 <= lanes; lane += 8) {
      const auto a =
          _mm512_loadu_si512(reinterpret_cast<const void *>(lhs64 + lane));
      const auto b =
          _mm512_loadu_si512(reinterpret_cast<const void *>(rhs64 + lane));
      _mm512_storeu_si512(reinterpret_cast<void *>(dst64 + lane),
                          _mm512_xor_si512(a, b));
    }
  }
  for (; lane + 4 <= lanes; lane += 4) {
    const auto a =
        _mm256_loadu_si256(reinterpret_cast<const __m256i *>(lhs64 + lane));
    const auto b =
        _mm256_loadu_si256(reinterpret_cast<const __m256i *>(rhs64 + lane));
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst64 + lane),
                        _mm256_xor_si256(a, b));
  }
  for (; lane < lanes; ++lane) {
    dst64[lane] = lhs64[lane] ^ rhs64[lane];
  }
}

void AndWords(uint128_t *dst, const uint128_t *lhs, const uint128_t *rhs,
              size_t word_count) {
  auto *dst64 = reinterpret_cast<uint64_t *>(dst);
  const auto *lhs64 = reinterpret_cast<const uint64_t *>(lhs);
  const auto *rhs64 = reinterpret_cast<const uint64_t *>(rhs);
  const size_t lanes = word_count * 2;
  size_t lane = 0;
  if (UseAvx512()) {
    for (; lane + 8 <= lanes; lane += 8) {
      const auto a =
          _mm512_loadu_si512(reinterpret_cast<const void *>(lhs64 + lane));
      const auto b =
          _mm512_loadu_si512(reinterpret_cast<const void *>(rhs64 + lane));
      _mm512_storeu_si512(reinterpret_cast<void *>(dst64 + lane),
                          _mm512_and_si512(a, b));
    }
  }
  for (; lane + 4 <= lanes; lane += 4) {
    const auto a =
        _mm256_loadu_si256(reinterpret_cast<const __m256i *>(lhs64 + lane));
    const auto b =
        _mm256_loadu_si256(reinterpret_cast<const __m256i *>(rhs64 + lane));
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst64 + lane),
                        _mm256_and_si256(a, b));
  }
  for (; lane < lanes; ++lane) {
    dst64[lane] = lhs64[lane] & rhs64[lane];
  }
}

void XorConstWords(uint128_t *dst, const uint128_t *src, uint64_t mask,
                   size_t word_count) {
  auto *dst64 = reinterpret_cast<uint64_t *>(dst);
  const auto *src64 = reinterpret_cast<const uint64_t *>(src);
  const auto mask_z = _mm512_set1_epi64(static_cast<int64_t>(mask));
  const auto mask_v = _mm256_set1_epi64x(static_cast<int64_t>(mask));
  const size_t lanes = word_count * 2;
  size_t lane = 0;
  if (UseAvx512()) {
    for (; lane + 8 <= lanes; lane += 8) {
      const auto v =
          _mm512_loadu_si512(reinterpret_cast<const void *>(src64 + lane));
      _mm512_storeu_si512(reinterpret_cast<void *>(dst64 + lane),
                          _mm512_xor_si512(v, mask_z));
    }
  }
  for (; lane + 4 <= lanes; lane += 4) {
    const auto v =
        _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src64 + lane));
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst64 + lane),
                        _mm256_xor_si256(v, mask_v));
  }
  for (; lane < lanes; ++lane) {
    dst64[lane] = src64[lane] ^ mask;
  }
}

void XorMaybeInvertWords(std::vector<uint128_t> &dst,
                         const std::vector<uint128_t> &lhs,
                         const std::vector<uint128_t> &rhs, bool invert) {
  XorWords(dst.data(), lhs.data(), rhs.data(), dst.size());
  if (invert) {
    XorConstWords(dst.data(), dst.data(), ~uint64_t{0}, dst.size());
  }
}

void CopyMaybeInvertWords(std::vector<uint128_t> &dst,
                          const std::vector<uint128_t> &src, bool invert) {
  if (invert) {
    XorConstWords(dst.data(), src.data(), ~uint64_t{0}, dst.size());
  } else {
    CopyWords(dst, src);
  }
}

template <typename InputWireBundle>
void PackInputMatrixIntoWireShares(const volePSI::Matrix<volePSI::u8> &input,
                                   const InputWireBundle &input_wires,
                                   size_t row_count,
                                   std::vector<std::vector<uint128_t>> &wires) {
  const size_t input_bits = input_wires.size();
  const size_t input_bytes = (input_bits + 7) / 8;
  OTMPSI_ENFORCE(static_cast<size_t>(input.rows()) == row_count,
               "GMW input row count mismatch");
  OTMPSI_ENFORCE(static_cast<size_t>(input.cols()) == input_bytes,
               "GMW input byte width mismatch");

  const unsigned int last_byte_mask =
      (input_bits & 7U) == 0 ? 0xffU : ((1U << (input_bits & 7U)) - 1U);
  for (size_t row_idx = 0; row_idx < row_count; ++row_idx) {
    const auto *row = input[row_idx].data();
    const size_t word_idx = row_idx / 128;
    const auto bit_mask = uint128_t{1} << (row_idx & 127U);
    for (size_t byte_idx = 0; byte_idx < input_bytes; ++byte_idx) {
      unsigned int bits = row[byte_idx];
      if (byte_idx + 1 == input_bytes) {
        bits &= last_byte_mask;
      }
      while (bits != 0) {
        const size_t bit_in_byte = static_cast<size_t>(__builtin_ctz(bits));
        const size_t bit = byte_idx * 8 + bit_in_byte;
        const auto wire = input_wires[bit];
        wires[wire][word_idx] |= bit_mask;
        bits &= bits - 1;
      }
    }
  }
}

struct SilentTripleRoleOutput {
  std::vector<uint128_t> mult;
  std::vector<uint128_t> add;
  size_t comm_bytes = 0;
};

SilentTripleRoleOutput RunSilentTripleRole(const std::string &tag, int rank,
                                           volePSI::Mode mode, size_t num_ots,
                                           block seed,
                                           const OtmpsiParams &params) {
  SilentTripleRoleOutput out;
  if (num_ots == 0) {
    return out;
  }

  auto sock = AcquireLocalSocket(tag, rank);
  volePSI::SilentTripleGen gen;
  gen.init(static_cast<volePSI::u64>(num_ots), 1 << 18,
           static_cast<volePSI::u64>(std::max<size_t>(1, params.threads)), mode,
           seed);
  macoro::sync_wait(gen.generateBaseOts(static_cast<volePSI::u64>(rank), sock));
  macoro::sync_wait(gen.expand(sock));
  macoro::sync_wait(sock.flush());

  out.mult.resize(static_cast<size_t>(gen.multSize()));
  out.add.resize(static_cast<size_t>(gen.addSize()));
  for (size_t i = 0; i < out.mult.size(); ++i) {
    out.mult[i] = BlockToU128(gen.multData()[i]);
  }
  for (size_t i = 0; i < out.add.size(); ++i) {
    out.add[i] = BlockToU128(gen.addData()[i]);
  }
  out.comm_bytes = static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
  return out;
}

struct NPartyPairTriples {
  size_t sender_party = 0;
  size_t receiver_party = 0;
  std::vector<uint128_t> sender_mult;
  std::vector<uint128_t> sender_add;
  std::vector<uint128_t> receiver_mult;
  std::vector<uint128_t> receiver_add;
  size_t offset = 0;
};

struct NPartyGmwTriples {
  size_t parties = 0;
  std::vector<NPartyPairTriples> pairs;
  std::vector<size_t> party_comm_bytes;

  NPartyPairTriples &Pair(size_t sender_party, size_t receiver_party) {
    OTMPSI_ENFORCE(sender_party < receiver_party, "pair order must be i < j");
    const size_t idx = sender_party * parties + receiver_party;
    OTMPSI_ENFORCE(idx < pairs.size(), "pair index out of range");
    return pairs[idx];
  }

  void Multiply(size_t sender_party, size_t receiver_party,
                uint128_t sender_input, uint128_t receiver_input,
                uint128_t &sender_share, uint128_t &receiver_share) {
    auto &pair = Pair(sender_party, receiver_party);
    OTMPSI_ENFORCE(pair.offset < pair.sender_mult.size() &&
                     pair.offset < pair.sender_add.size() &&
                     pair.offset < pair.receiver_mult.size() &&
                     pair.offset < pair.receiver_add.size(),
                 "not enough silent-OT triples for N-party GMW");

    const auto a = pair.sender_mult[pair.offset];
    const auto b = pair.sender_add[pair.offset];
    const auto c = pair.receiver_mult[pair.offset];
    const auto d = pair.receiver_add[pair.offset];
    ++pair.offset;

    const auto opened_u = a ^ sender_input;
    const auto opened_w = c ^ receiver_input;
    sender_share = (sender_input & opened_w) ^ b;
    receiver_share = (opened_u & c) ^ d;

    const size_t online_bytes = 2 * sizeof(uint128_t);
    party_comm_bytes[sender_party] += online_bytes;
    party_comm_bytes[receiver_party] += online_bytes;
  }
};

NPartyGmwTriples MakeNPartyGmwTriples(size_t parties,
                                      size_t correlations_per_pair,
                                      const OtmpsiParams &params,
                                      size_t chunk_idx) {
  NPartyGmwTriples triples;
  triples.parties = parties;
  triples.pairs.resize(parties * parties);
  triples.party_comm_bytes.assign(parties, 0);
  if (correlations_per_pair == 0) {
    return triples;
  }

  const size_t num_ots = correlations_per_pair * 128;
  size_t pair_id = 0;
  for (size_t i = 0; i < parties; ++i) {
    for (size_t j = i + 1; j < parties; ++j) {
      const auto tag =
          MakeSocketTag(params.base_port, 1000 + pair_id, 7, chunk_idx);
      const auto sender_seed = oc::sysRandomSeed();
      const auto receiver_seed = oc::sysRandomSeed();
      auto sender_job =
          std::async(std::launch::async, [&, tag, sender_seed, num_ots] {
            return RunSilentTripleRole(tag, 0, volePSI::Sender, num_ots,
                                       sender_seed, params);
          });
      auto receiver_job =
          std::async(std::launch::async, [&, tag, receiver_seed, num_ots] {
            return RunSilentTripleRole(tag, 1, volePSI::Receiver, num_ots,
                                       receiver_seed, params);
          });

      auto sender = sender_job.get();
      auto receiver = receiver_job.get();
      OTMPSI_ENFORCE(sender.mult.size() >= correlations_per_pair &&
                       sender.add.size() >= correlations_per_pair &&
                       receiver.mult.size() >= correlations_per_pair &&
                       receiver.add.size() >= correlations_per_pair,
                   "silent-OT triple generator returned too few blocks");

      auto &pair = triples.pairs[i * parties + j];
      pair.sender_party = i;
      pair.receiver_party = j;
      pair.sender_mult = std::move(sender.mult);
      pair.sender_add = std::move(sender.add);
      pair.receiver_mult = std::move(receiver.mult);
      pair.receiver_add = std::move(receiver.add);
      triples.party_comm_bytes[i] += sender.comm_bytes;
      triples.party_comm_bytes[j] += receiver.comm_bytes;
      ++pair_id;
    }
  }
  return triples;
}

size_t GmwTripleChunkGates(size_t parties, size_t nonlinear_gates,
                           size_t word_count) {
  const char *env = std::getenv("OTMPSI_GMW_TRIPLE_CHUNK_GATES");
  if (env != nullptr && env[0] != '\0') {
    char *end = nullptr;
    const auto parsed = std::strtoull(env, &end, 0);
    if ((end == nullptr || *end == '\0') && parsed != 0) {
      return static_cast<size_t>(parsed);
    }
  }

  const size_t pairs = parties * (parties - 1) / 2;
  const size_t bytes_per_gate = pairs * word_count * 2 * 4 * sizeof(uint128_t);
  const size_t total_bytes = bytes_per_gate * nonlinear_gates;

  size_t cache_limit_bytes = size_t{512} * 1024 * 1024;
  const char *limit_env = std::getenv("OTMPSI_GMW_TRIPLE_CACHE_MB");
  if (limit_env != nullptr && limit_env[0] != '\0') {
    char *end = nullptr;
    const auto parsed = std::strtoull(limit_env, &end, 0);
    if ((end == nullptr || *end == '\0') && parsed != 0) {
      cache_limit_bytes = static_cast<size_t>(parsed) * 1024 * 1024;
    }
  }

  if (bytes_per_gate == 0 || total_bytes <= cache_limit_bytes) {
    return std::max<size_t>(1, nonlinear_gates);
  }
  return std::max<size_t>(1, cache_limit_bytes / bytes_per_gate);
}

void AccumulatePartyComm(std::vector<size_t> &total,
                         const NPartyGmwTriples &triples) {
  if (triples.party_comm_bytes.empty()) {
    return;
  }
  OTMPSI_ENFORCE(total.size() == triples.party_comm_bytes.size(),
               "party communication vector mismatch");
  for (size_t party = 0; party < total.size(); ++party) {
    total[party] += triples.party_comm_bytes[party];
  }
}

NPartyWireShares PackNPartyGmwInputs(
    const std::vector<volePSI::Matrix<volePSI::u8>> &party_inputs,
    const volePSI::BetaCircuit &cir, const OtmpsiParams &params) {
  OTMPSI_ENFORCE(party_inputs.size() == params.parties,
               "expected one GMW input matrix per party");
  OTMPSI_ENFORCE(cir.mInputs.size() == 1,
               "paper threshold circuit must have one input bundle");

  const size_t word_count = (params.set_size + 127) / 128;
  NPartyWireShares wires(
      params.parties,
      std::vector<std::vector<uint128_t>>(
          cir.mWireCount, std::vector<uint128_t>(word_count, 0)));

  for (size_t party = 0; party < params.parties; ++party) {
    const auto &input = party_inputs[party];
    PackInputMatrixIntoWireShares(input, cir.mInputs[0], params.set_size,
                                  wires[party]);
  }

  return wires;
}

void EvalNPartyLinearGate(NPartyWireShares &wires, const oc::BetaGate &gate) {
  const size_t parties = wires.size();
  const size_t word_count = wires[0][gate.mOutput].size();
  const auto all_ones = AllOneU128();

  for (size_t party = 0; party < parties; ++party) {
    auto &out = wires[party][gate.mOutput];
    const auto &a = wires[party][gate.mInput[0]];
    const auto &b = wires[party][gate.mInput[1]];
    for (size_t word = 0; word < word_count; ++word) {
      switch (gate.mType) {
      case oc::GateType::Zero:
        out[word] = 0;
        break;
      case oc::GateType::One:
        out[word] = party == 0 ? all_ones : 0;
        break;
      case oc::GateType::a:
        out[word] = a[word];
        break;
      case oc::GateType::b:
        out[word] = b[word];
        break;
      case oc::GateType::na:
        out[word] = a[word] ^ (party == 0 ? all_ones : 0);
        break;
      case oc::GateType::nb:
        out[word] = b[word] ^ (party == 0 ? all_ones : 0);
        break;
      case oc::GateType::Xor:
        out[word] = a[word] ^ b[word];
        break;
      case oc::GateType::Nxor:
        out[word] = a[word] ^ b[word] ^ (party == 0 ? all_ones : 0);
        break;
      default:
        OTMPSI_THROW("unsupported linear GMW gate");
      }
    }
  }
}

void EvalNPartyAndGate(NPartyWireShares &wires, const oc::BetaGate &gate,
                       const NPartyGmwGateShape &shape,
                       NPartyGmwTriples &triples) {
  const size_t parties = wires.size();
  const size_t word_count = wires[0][gate.mOutput].size();
  const auto all_ones = AllOneU128();
  std::vector<uint128_t> x_shares(parties, 0);
  std::vector<uint128_t> y_shares(parties, 0);
  std::vector<uint128_t> out_shares(parties, 0);

  // Semi-honest N-party GMW multiplication. Each pair uses stationary-OT
  // correlations to share the two cross terms x_i*y_j and x_j*y_i.
  for (size_t word = 0; word < word_count; ++word) {
    for (size_t party = 0; party < parties; ++party) {
      x_shares[party] = wires[party][gate.mInput[0]][word];
      y_shares[party] = wires[party][gate.mInput[1]][word];
      if (party == 0 && shape.invert_a) {
        x_shares[party] ^= all_ones;
      }
      if (party == 0 && shape.invert_b) {
        y_shares[party] ^= all_ones;
      }
      out_shares[party] = x_shares[party] & y_shares[party];
    }

    for (size_t i = 0; i < parties; ++i) {
      for (size_t j = i + 1; j < parties; ++j) {
        uint128_t sender_share = 0;
        uint128_t receiver_share = 0;
        triples.Multiply(i, j, x_shares[i], y_shares[j], sender_share,
                         receiver_share);
        out_shares[i] ^= sender_share;
        out_shares[j] ^= receiver_share;

        triples.Multiply(i, j, y_shares[i], x_shares[j], sender_share,
                         receiver_share);
        out_shares[i] ^= sender_share;
        out_shares[j] ^= receiver_share;
      }
    }

    if (shape.invert_out) {
      out_shares[0] ^= all_ones;
    }
    for (size_t party = 0; party < parties; ++party) {
      wires[party][gate.mOutput][word] = out_shares[party];
    }
  }
}

volePSI::Matrix<volePSI::u8>
ReconstructNPartyGmwOutput(const NPartyWireShares &wires,
                           const volePSI::BetaCircuit &cir,
                           const OtmpsiParams &params, size_t output_bits) {
  OTMPSI_ENFORCE(cir.mOutputs.size() == 1,
               "paper threshold circuit must have one output bundle");
  OTMPSI_ENFORCE(cir.mOutputs[0].size() == output_bits,
               "GMW output bit width mismatch");
  volePSI::Matrix<volePSI::u8> out;
  out.resize(params.set_size, (output_bits + 7) / 8);
  if (out.size() != 0) {
    std::memset(out.data(), 0, out.size() * sizeof(volePSI::u8));
  }

  for (size_t row_idx = 0; row_idx < params.set_size; ++row_idx) {
    auto *row = out[row_idx].data();
    const size_t word_idx = row_idx / 128;
    const size_t bit_idx = row_idx & 127U;
    for (size_t bit = 0; bit < output_bits; ++bit) {
      uint128_t opened = 0;
      const auto wire = cir.mOutputs[0][bit];
      for (size_t party = 0; party < params.parties; ++party) {
        opened ^= wires[party][wire][word_idx];
      }
      SetPackedBit(row, bit, ((opened >> bit_idx) & 1U) != 0);
    }
  }
  return out;
}

volePSI::Matrix<volePSI::u8>
ReconstructNPartyGmwOutput(const NPartyWireShares &wires,
                           const volePSI::BetaCircuit &cir,
                           const OtmpsiParams &params) {
  return ReconstructNPartyGmwOutput(
      wires, cir, params, MakePaperGmwLayout(params.parties).output_bits);
}

std::vector<uint64_t>
DecodeOutputShareWords(const std::vector<uint128_t> &words, size_t row_count,
                       size_t output_bits) {
  OTMPSI_ENFORCE(output_bits <= 63, "output share width too large: {}",
               output_bits);
  const size_t word_count = (row_count + 127) / 128;
  OTMPSI_ENFORCE(words.size() == output_bits * word_count,
               "output share word count mismatch");
  std::vector<uint64_t> out(row_count, 0);
  for (size_t row = 0; row < row_count; ++row) {
    const size_t word_idx = row / 128;
    const size_t bit_idx = row & 127U;
    uint64_t value = 0;
    for (size_t bit = 0; bit < output_bits; ++bit) {
      const auto word = words[bit * word_count + word_idx];
      if (((word >> bit_idx) & 1U) != 0) {
        value |= uint64_t{1} << bit;
      }
    }
    out[row] = value;
  }
  return out;
}

std::vector<uint64_t>
LocalOutputShareValues(const std::vector<std::vector<uint128_t>> &party_wires,
                       const volePSI::BetaCircuit &cir,
                       const OtmpsiParams &params, size_t output_bits) {
  OTMPSI_ENFORCE(cir.mOutputs.size() == 1 &&
                   cir.mOutputs[0].size() == output_bits,
               "GMW shared-output bit width mismatch");
  std::vector<uint128_t> words;
  if (output_bits == 0) {
    return {};
  }
  const size_t word_count = party_wires[cir.mOutputs[0][0]].size();
  words.reserve(output_bits * word_count);
  for (size_t bit = 0; bit < output_bits; ++bit) {
    const auto wire = cir.mOutputs[0][bit];
    words.insert(words.end(), party_wires[wire].begin(),
                 party_wires[wire].end());
  }
  return DecodeOutputShareWords(words, params.set_size, output_bits);
}

NPartyPaperGmwOutput
RunNPartyPaperGmw(const std::vector<volePSI::Matrix<volePSI::u8>> &party_inputs,
                  const std::vector<uint128_t> &x0, const OtmpsiParams &params,
                  const std::string &scope, NPartyGmwCircuitKind circuit_kind,
                  const std::vector<size_t> &extra_party_comm_bytes) {
  const auto start = std::chrono::steady_clock::now();
  const bool masked_holder_output =
      circuit_kind == NPartyGmwCircuitKind::kMembershipMaskedHolder ||
      circuit_kind == NPartyGmwCircuitKind::kMaskedArithmeticCount;
  volePSI::BetaCircuit cir;
  switch (circuit_kind) {
  case NPartyGmwCircuitKind::kMembershipCount:
    cir = BuildMembershipThresholdCircuit(params.parties);
    break;
  case NPartyGmwCircuitKind::kMembershipMaskedHolder:
    cir = BuildMembershipMaskedHolderThresholdCircuit(params.parties,
                                                      params.threshold);
    break;
  case NPartyGmwCircuitKind::kMaskedArithmeticCount:
    cir = BuildMaskedHolderThresholdCircuit(params.parties);
    break;
  }
  auto wires = PackNPartyGmwInputs(party_inputs, cir, params);

  size_t nonlinear_gates = 0;
  for (const auto &gate : cir.mGates) {
    NPartyGmwGateShape shape;
    if (NormalizeNPartyAndGate(gate.mType, shape)) {
      ++nonlinear_gates;
    }
  }
  const size_t word_count = (params.set_size + 127) / 128;
  const size_t chunk_gates =
      GmwTripleChunkGates(params.parties, nonlinear_gates, word_count);
  std::vector<size_t> total_party_comm(params.parties, 0);
  NPartyGmwTriples triples;
  size_t chunk_remaining = 0;
  size_t chunk_idx = 0;
  size_t nonlinear_done = 0;

  for (const auto &gate : cir.mGates) {
    NPartyGmwGateShape shape;
    if (NormalizeNPartyAndGate(gate.mType, shape)) {
      if (chunk_remaining == 0) {
        AccumulatePartyComm(total_party_comm, triples);
        const size_t gates_in_chunk =
            std::min(chunk_gates, nonlinear_gates - nonlinear_done);
        LogDebug(scope, "N-party GMW triple chunk " +
                            std::to_string(chunk_idx + 1) + "/" +
                            std::to_string((nonlinear_gates + chunk_gates - 1) /
                                           chunk_gates) +
                            ", gates=" + std::to_string(gates_in_chunk));
        triples = MakeNPartyGmwTriples(params.parties,
                                       gates_in_chunk * word_count * 2, params,
                                       chunk_idx++);
        chunk_remaining = gates_in_chunk;
      }
      EvalNPartyAndGate(wires, gate, shape, triples);
      --chunk_remaining;
      ++nonlinear_done;
    } else {
      EvalNPartyLinearGate(wires, gate);
    }
  }
  AccumulatePartyComm(total_party_comm, triples);
  if (!extra_party_comm_bytes.empty()) {
    OTMPSI_ENFORCE(extra_party_comm_bytes.size() == params.parties,
                 "extra GMW communication vector mismatch");
    for (size_t party = 0; party < params.parties; ++party) {
      total_party_comm[party] += extra_party_comm_bytes[party];
    }
  }

  const size_t output_bits =
      masked_holder_output ? size_t{1} + params.parties
                           : MakePaperGmwLayout(params.parties).output_bits;
  auto out = ReconstructNPartyGmwOutput(wires, cir, params, output_bits);
  const double elapsed = SecondsSince(start);
  const size_t output_reveal_bytes = out.size() * sizeof(volePSI::u8);

  NPartyPaperGmwOutput run;
  run.results = masked_holder_output
                    ? DecodeMaskedHolderGmwOutput(out, x0, params)
                    : DecodePaperGmwOutput(out, x0, params);
  run.leader.time_s = elapsed;
  run.clients.time_s = elapsed;
  run.client_stats.assign(params.parties, RoleStats{elapsed, 0});
  run.leader.comm_bytes =
      total_party_comm[0] + output_reveal_bytes * (params.parties - 1);
  for (size_t party = 1; party < params.parties; ++party) {
    run.client_stats[party].comm_bytes =
        total_party_comm[party] + output_reveal_bytes;
    run.clients.comm_bytes += run.client_stats[party].comm_bytes;
  }
  LogDebug(scope,
           "N-party GMW nonlinear gates=" + std::to_string(nonlinear_gates) +
               ", triple_chunk_gates=" + std::to_string(chunk_gates));
  return run;
}

NPartyCompareGmwOutput RunNPartyCompareGmw(
    const std::vector<volePSI::Matrix<volePSI::u8>> &party_inputs,
    const OtmpsiParams &params, const std::string &scope,
    const std::vector<size_t> &extra_party_comm_bytes) {
  const auto start = std::chrono::steady_clock::now();
  auto cir = BuildMaskedCountCompareCircuit(params.parties);
  auto wires = PackNPartyGmwInputs(party_inputs, cir, params);

  size_t nonlinear_gates = 0;
  for (const auto &gate : cir.mGates) {
    NPartyGmwGateShape shape;
    if (NormalizeNPartyAndGate(gate.mType, shape)) {
      ++nonlinear_gates;
    }
  }
  const size_t word_count = (params.set_size + 127) / 128;
  const size_t chunk_gates =
      GmwTripleChunkGates(params.parties, nonlinear_gates, word_count);
  std::vector<size_t> total_party_comm(params.parties, 0);
  NPartyGmwTriples triples;
  size_t chunk_remaining = 0;
  size_t chunk_idx = 0;
  size_t nonlinear_done = 0;

  for (const auto &gate : cir.mGates) {
    NPartyGmwGateShape shape;
    if (NormalizeNPartyAndGate(gate.mType, shape)) {
      if (chunk_remaining == 0) {
        AccumulatePartyComm(total_party_comm, triples);
        const size_t gates_in_chunk =
            std::min(chunk_gates, nonlinear_gates - nonlinear_done);
        LogDebug(scope, "N-party compare GMW triple chunk " +
                            std::to_string(chunk_idx + 1) + "/" +
                            std::to_string((nonlinear_gates + chunk_gates - 1) /
                                           chunk_gates) +
                            ", gates=" + std::to_string(gates_in_chunk));
        triples = MakeNPartyGmwTriples(params.parties,
                                       gates_in_chunk * word_count * 2, params,
                                       chunk_idx++);
        chunk_remaining = gates_in_chunk;
      }
      EvalNPartyAndGate(wires, gate, shape, triples);
      --chunk_remaining;
      ++nonlinear_done;
    } else {
      EvalNPartyLinearGate(wires, gate);
    }
  }
  AccumulatePartyComm(total_party_comm, triples);
  if (!extra_party_comm_bytes.empty()) {
    OTMPSI_ENFORCE(extra_party_comm_bytes.size() == params.parties,
                 "extra GMW communication vector mismatch");
    for (size_t party = 0; party < params.parties; ++party) {
      total_party_comm[party] += extra_party_comm_bytes[party];
    }
  }

  auto out = ReconstructNPartyGmwOutput(wires, cir, params, 1);
  const double elapsed = SecondsSince(start);
  const size_t output_reveal_bytes = out.size() * sizeof(volePSI::u8);

  NPartyCompareGmwOutput run;
  run.over_threshold = DecodeBitShareOutput(out, params.set_size);
  run.leader.time_s = elapsed;
  run.clients.time_s = elapsed;
  run.client_stats.assign(params.parties, RoleStats{elapsed, 0});
  run.leader.comm_bytes =
      total_party_comm[0] + output_reveal_bytes * (params.parties - 1);
  for (size_t party = 1; party < params.parties; ++party) {
    run.client_stats[party].comm_bytes =
        total_party_comm[party] + output_reveal_bytes;
    run.clients.comm_bytes += run.client_stats[party].comm_bytes;
  }
  LogDebug(scope, "N-party compare GMW nonlinear gates=" +
                      std::to_string(nonlinear_gates) +
                      ", triple_chunk_gates=" + std::to_string(chunk_gates));
  return run;
}

NPartySharedOutputGmwOutput RunNPartySharedOutputGmw(
    const std::vector<volePSI::Matrix<volePSI::u8>> &party_inputs,
    const OtmpsiParams &params, const std::string &scope,
    volePSI::BetaCircuit cir, size_t output_bits) {
  const auto start = std::chrono::steady_clock::now();
  auto wires = PackNPartyGmwInputs(party_inputs, cir, params);

  size_t nonlinear_gates = 0;
  for (const auto &gate : cir.mGates) {
    NPartyGmwGateShape shape;
    if (NormalizeNPartyAndGate(gate.mType, shape)) {
      ++nonlinear_gates;
    }
  }
  const size_t word_count = (params.set_size + 127) / 128;
  const size_t chunk_gates =
      GmwTripleChunkGates(params.parties, nonlinear_gates, word_count);
  std::vector<size_t> total_party_comm(params.parties, 0);
  NPartyGmwTriples triples;
  size_t chunk_remaining = 0;
  size_t chunk_idx = 0;
  size_t nonlinear_done = 0;

  for (const auto &gate : cir.mGates) {
    NPartyGmwGateShape shape;
    if (NormalizeNPartyAndGate(gate.mType, shape)) {
      if (chunk_remaining == 0) {
        AccumulatePartyComm(total_party_comm, triples);
        const size_t gates_in_chunk =
            std::min(chunk_gates, nonlinear_gates - nonlinear_done);
        LogDebug(scope, "N-party shared-output GMW triple chunk " +
                            std::to_string(chunk_idx + 1) + "/" +
                            std::to_string((nonlinear_gates + chunk_gates - 1) /
                                           chunk_gates) +
                            ", gates=" + std::to_string(gates_in_chunk));
        triples = MakeNPartyGmwTriples(params.parties,
                                       gates_in_chunk * word_count * 2, params,
                                       chunk_idx++);
        chunk_remaining = gates_in_chunk;
      }
      EvalNPartyAndGate(wires, gate, shape, triples);
      --chunk_remaining;
      ++nonlinear_done;
    } else {
      EvalNPartyLinearGate(wires, gate);
    }
  }
  AccumulatePartyComm(total_party_comm, triples);

  NPartySharedOutputGmwOutput run;
  run.output_shares.resize(params.parties);
  for (size_t party = 0; party < params.parties; ++party) {
    run.output_shares[party] =
        LocalOutputShareValues(wires[party], cir, params, output_bits);
  }
  const double elapsed = SecondsSince(start);
  run.leader.time_s = elapsed;
  run.clients.time_s = elapsed;
  run.client_stats.assign(params.parties, RoleStats{elapsed, 0});
  run.leader.comm_bytes = total_party_comm[0];
  for (size_t party = 1; party < params.parties; ++party) {
    run.client_stats[party].comm_bytes = total_party_comm[party];
    run.clients.comm_bytes += run.client_stats[party].comm_bytes;
  }
  LogDebug(scope, "N-party shared-output GMW nonlinear gates=" +
                      std::to_string(nonlinear_gates) +
                      ", triple_chunk_gates=" + std::to_string(chunk_gates));
  return run;
}

std::vector<std::vector<uint128_t>>
PackLocalNPartyGmwInput(const volePSI::Matrix<volePSI::u8> &party_input,
                        const volePSI::BetaCircuit &cir,
                        const OtmpsiParams &params) {
  OTMPSI_ENFORCE(cir.mInputs.size() == 1,
               "distributed GMW expects one input bundle");
  const size_t input_bits = cir.mInputs[0].size();
  const size_t input_bytes = (input_bits + 7) / 8;
  const size_t word_count = (params.set_size + 127) / 128;
  OTMPSI_ENFORCE(static_cast<size_t>(party_input.rows()) == params.set_size,
               "distributed GMW input row count mismatch");
  OTMPSI_ENFORCE(static_cast<size_t>(party_input.cols()) == input_bytes,
               "distributed GMW input width mismatch");

  std::vector<std::vector<uint128_t>> wires(
      cir.mWireCount, std::vector<uint128_t>(word_count, 0));
  PackInputMatrixIntoWireShares(party_input, cir.mInputs[0], params.set_size,
                                wires);
  return wires;
}

void EvalNPartyLinearGateLocal(std::vector<std::vector<uint128_t>> &wires,
                               const oc::BetaGate &gate, size_t party_id) {
  const size_t word_count = wires[gate.mOutput].size();
  auto &out = wires[gate.mOutput];
  const auto &a = wires[gate.mInput[0]];
  const auto &b = wires[gate.mInput[1]];

  if (!UseGmwWordSimd()) {
    const auto all_ones = AllOneU128();
    for (size_t word = 0; word < word_count; ++word) {
      switch (gate.mType) {
      case oc::GateType::Zero:
        out[word] = 0;
        break;
      case oc::GateType::One:
        out[word] = party_id == 0 ? all_ones : 0;
        break;
      case oc::GateType::a:
        out[word] = a[word];
        break;
      case oc::GateType::b:
        out[word] = b[word];
        break;
      case oc::GateType::na:
        out[word] = a[word] ^ (party_id == 0 ? all_ones : 0);
        break;
      case oc::GateType::nb:
        out[word] = b[word] ^ (party_id == 0 ? all_ones : 0);
        break;
      case oc::GateType::Xor:
        out[word] = a[word] ^ b[word];
        break;
      case oc::GateType::Nxor:
        out[word] = a[word] ^ b[word] ^ (party_id == 0 ? all_ones : 0);
        break;
      default:
        OTMPSI_THROW("unsupported distributed linear GMW gate");
      }
    }
    return;
  }

  switch (gate.mType) {
  case oc::GateType::Zero:
    FillWords(out, 0);
    break;
  case oc::GateType::One:
    FillWords(out, party_id == 0 ? 0xff : 0);
    break;
  case oc::GateType::a:
    CopyWords(out, a);
    break;
  case oc::GateType::b:
    CopyWords(out, b);
    break;
  case oc::GateType::na:
    CopyMaybeInvertWords(out, a, party_id == 0);
    break;
  case oc::GateType::nb:
    CopyMaybeInvertWords(out, b, party_id == 0);
    break;
  case oc::GateType::Xor:
    XorWords(out.data(), a.data(), b.data(), word_count);
    break;
  case oc::GateType::Nxor:
    XorMaybeInvertWords(out, a, b, party_id == 0);
    break;
  default:
    OTMPSI_THROW("unsupported distributed linear GMW gate");
  }
}

struct LocalNPartyPairState {
  size_t peer = 0;
  bool sender = false;
  std::vector<uint128_t> mult;
  std::vector<uint128_t> add;
  size_t offset = 0;
  size_t comm_bytes = 0;
  std::unique_ptr<coproto::Socket> online_sock;
};

struct LocalNPartyGmwState {
  std::vector<LocalNPartyPairState> pairs;
  size_t comm_bytes = 0;
};

void CloseLocalNPartyGmwState(LocalNPartyGmwState &state) {
  for (auto &pair : state.pairs) {
    if (pair.online_sock) {
      macoro::sync_wait(pair.online_sock->flush());
      pair.comm_bytes += static_cast<size_t>(pair.online_sock->bytesSent() +
                                             pair.online_sock->bytesReceived());
      macoro::sync_wait(pair.online_sock->close());
      pair.online_sock.reset();
    }
    state.comm_bytes += pair.comm_bytes;
    pair.comm_bytes = 0;
  }
}

LocalNPartyGmwState MakeLocalNPartyGmwState(size_t party_id, size_t parties,
                                            size_t correlations_per_pair,
                                            const OtmpsiParams &params,
                                            size_t chunk_idx) {
  LocalNPartyGmwState state;
  if (correlations_per_pair == 0) {
    return state;
  }
  const size_t num_ots = correlations_per_pair * 128;

  struct PendingPair {
    LocalNPartyPairState pair;
    int rank = 0;
    std::string triple_tag;
    std::string online_tag;
    volePSI::Mode mode = volePSI::Sender;
    block seed = oc::sysRandomSeed();
    std::future<SilentTripleRoleOutput> triple_job;
    std::future<coproto::Socket> online_job;
  };

  std::vector<PendingPair> pending;
  size_t pair_id = 0;
  for (size_t i = 0; i < parties; ++i) {
    for (size_t j = i + 1; j < parties; ++j) {
      if (party_id == i || party_id == j) {
        const bool is_sender = party_id == i;
        const int rank = is_sender ? 0 : 1;
        PendingPair p;
        p.pair.peer = is_sender ? j : i;
        p.pair.sender = is_sender;
        p.rank = rank;
        p.triple_tag =
            MakeSocketTag(params.base_port, 1000 + pair_id, 7, chunk_idx);
        p.online_tag =
            MakeSocketTag(params.base_port, 1000 + pair_id, 8, chunk_idx);
        p.mode = is_sender ? volePSI::Sender : volePSI::Receiver;
        p.seed = oc::sysRandomSeed();
        p.triple_job =
            std::async(std::launch::async, [&, triple_tag = p.triple_tag, rank,
                                            mode = p.mode, seed = p.seed] {
              return RunSilentTripleRole(triple_tag, rank, mode, num_ots, seed,
                                         params);
            });
        pending.push_back(std::move(p));
      }
      ++pair_id;
    }
  }

  for (auto &p : pending) {
    auto triples = p.triple_job.get();
    OTMPSI_ENFORCE(triples.mult.size() >= correlations_per_pair &&
                     triples.add.size() >= correlations_per_pair,
                 "distributed GMW triple generator returned too few blocks");
    p.pair.mult = std::move(triples.mult);
    p.pair.add = std::move(triples.add);
    p.pair.comm_bytes = triples.comm_bytes;
    p.online_job =
        std::async(std::launch::async, [tag = p.online_tag, rank = p.rank] {
          return AcquireLocalSocket(tag, rank);
        });
  }

  state.pairs.reserve(pending.size());
  for (auto &p : pending) {
    auto sock = p.online_job.get();
    p.pair.online_sock = std::make_unique<coproto::Socket>(std::move(sock));
    state.pairs.push_back(std::move(p.pair));
  }
  return state;
}

size_t NPartyPairId(size_t lhs, size_t rhs, size_t parties) {
  OTMPSI_ENFORCE(lhs < rhs && rhs < parties, "invalid N-party GMW pair");
  return lhs * (2 * parties - lhs - 1) / 2 + (rhs - lhs - 1);
}

LocalNPartyGmwState MakeLocalNPartyGmwStateFromPrecomputed(
    size_t party_id, size_t parties, size_t correlations_per_pair,
    const OtmpsiParams &params,
    std::vector<PrecomputedGmwPairPrefix> prefixes, size_t chunk_idx) {
  OTMPSI_ENFORCE(party_id < parties, "N-party GMW party index out of range");
  OTMPSI_ENFORCE(prefixes.size() == parties - 1,
               "expected one precomputed GMW prefix per peer");

  struct PendingPair {
    LocalNPartyPairState pair;
    std::future<coproto::Socket> online_job;
  };

  std::vector<bool> seen(parties, false);
  std::vector<PendingPair> pending;
  pending.reserve(prefixes.size());
  for (auto &prefix : prefixes) {
    OTMPSI_ENFORCE(prefix.peer < parties && prefix.peer != party_id,
                 "invalid precomputed GMW peer");
    OTMPSI_ENFORCE(!seen[prefix.peer], "duplicate precomputed GMW peer");
    seen[prefix.peer] = true;
    const bool sender = party_id < prefix.peer;
    OTMPSI_ENFORCE(prefix.sender_role == sender,
                 "precomputed GMW sender role mismatch");
    OTMPSI_ENFORCE(prefix.mult.size() == correlations_per_pair &&
                     prefix.add.size() == correlations_per_pair,
                 "precomputed GMW correlation count mismatch");

    const size_t lo = std::min(party_id, prefix.peer);
    const size_t hi = std::max(party_id, prefix.peer);
    const auto online_tag =
        MakeSocketTag(params.base_port, 1000 + NPartyPairId(lo, hi, parties),
                      8, chunk_idx);

    PendingPair item;
    item.pair.peer = prefix.peer;
    item.pair.sender = sender;
    item.pair.mult = std::move(prefix.mult);
    item.pair.add = std::move(prefix.add);
    item.pair.comm_bytes = prefix.preprocess_comm_bytes;
    item.online_job =
        std::async(std::launch::async, [tag = online_tag, sender] {
          return AcquireLocalSocket(tag, sender ? 0 : 1);
        });
    pending.push_back(std::move(item));
  }

  LocalNPartyGmwState state;
  state.pairs.reserve(pending.size());
  for (auto &item : pending) {
    item.pair.online_sock = std::make_unique<coproto::Socket>(
        item.online_job.get());
    state.pairs.push_back(std::move(item.pair));
  }
  return state;
}

std::vector<uint128_t> ExchangeU128Vector(coproto::Socket &sock,
                                          const std::vector<uint128_t> &v,
                                          bool send_first) {
  std::vector<uint128_t> peer(v.size());
  if (send_first) {
    macoro::sync_wait(
        sock.send(coproto::span<const uint128_t>(v.data(), v.size())));
    macoro::sync_wait(sock.flush());
    macoro::sync_wait(
        sock.recv(coproto::span<uint128_t>(peer.data(), peer.size())));
  } else {
    macoro::sync_wait(
        sock.recv(coproto::span<uint128_t>(peer.data(), peer.size())));
    macoro::sync_wait(
        sock.send(coproto::span<const uint128_t>(v.data(), v.size())));
    macoro::sync_wait(sock.flush());
  }
  return peer;
}

void EvalNPartyAndGateLocal(std::vector<std::vector<uint128_t>> &wires,
                            const oc::BetaGate &gate,
                            const NPartyGmwGateShape &shape,
                            LocalNPartyGmwState &state, size_t party_id) {
  const size_t word_count = wires[gate.mOutput].size();
  const auto all_ones = AllOneU128();
  std::vector<uint128_t> x(word_count);
  std::vector<uint128_t> y(word_count);
  std::vector<uint128_t> out(word_count);

  for (size_t word = 0; word < word_count; ++word) {
    x[word] = wires[gate.mInput[0]][word];
    y[word] = wires[gate.mInput[1]][word];
    if (party_id == 0 && shape.invert_a) {
      x[word] ^= all_ones;
    }
    if (party_id == 0 && shape.invert_b) {
      y[word] ^= all_ones;
    }
    out[word] = x[word] & y[word];
  }

  for (auto &pair : state.pairs) {
    OTMPSI_ENFORCE(pair.online_sock != nullptr,
                 "distributed GMW online socket missing");
    std::vector<uint128_t> opened(2 * word_count);
    for (size_t word = 0; word < word_count; ++word) {
      const size_t off = pair.offset + 2 * word;
      OTMPSI_ENFORCE(off + 1 < pair.mult.size() && off + 1 < pair.add.size(),
                   "distributed GMW triple offset out of range");
      if (pair.sender) {
        opened[2 * word] = pair.mult[off] ^ x[word];
        opened[2 * word + 1] = pair.mult[off + 1] ^ y[word];
      } else {
        opened[2 * word] = pair.mult[off] ^ y[word];
        opened[2 * word + 1] = pair.mult[off + 1] ^ x[word];
      }
    }

    auto peer_opened =
        ExchangeU128Vector(*pair.online_sock, opened, pair.sender);
    for (size_t word = 0; word < word_count; ++word) {
      const size_t off = pair.offset + 2 * word;
      if (pair.sender) {
        out[word] ^= (x[word] & peer_opened[2 * word]) ^ pair.add[off];
        out[word] ^= (y[word] & peer_opened[2 * word + 1]) ^ pair.add[off + 1];
      } else {
        out[word] ^= (peer_opened[2 * word] & pair.mult[off]) ^ pair.add[off];
        out[word] ^= (peer_opened[2 * word + 1] & pair.mult[off + 1]) ^
                     pair.add[off + 1];
      }
    }
    pair.offset += 2 * word_count;
  }

  if (party_id == 0 && shape.invert_out) {
    for (auto &word : out) {
      word ^= all_ones;
    }
  }
  wires[gate.mOutput] = std::move(out);
}

struct LocalAndGateWork {
  const oc::BetaGate *gate = nullptr;
  NPartyGmwGateShape shape;
  std::vector<uint128_t> x;
  std::vector<uint128_t> y;
  std::vector<uint128_t> out;
};

void EvalNPartyAndGateLevelLocal(std::vector<std::vector<uint128_t>> &wires,
                                 const std::vector<const oc::BetaGate *> &gates,
                                 LocalNPartyGmwState &state, size_t party_id) {
  if (gates.empty()) {
    return;
  }
  const size_t word_count = wires[gates[0]->mOutput].size();
  const bool use_word_simd = UseGmwWordSimd();
  const auto all_ones = AllOneU128();
  std::vector<LocalAndGateWork> work;
  work.reserve(gates.size());

  for (const auto *gate : gates) {
    NPartyGmwGateShape shape;
    OTMPSI_ENFORCE(NormalizeNPartyAndGate(gate->mType, shape),
                 "expected nonlinear GMW gate");
    auto &item = work.emplace_back();
    item.gate = gate;
    item.shape = shape;
    item.x.resize(word_count);
    item.y.resize(word_count);
    item.out.resize(word_count);
    if (use_word_simd) {
      CopyMaybeInvertWords(item.x, wires[gate->mInput[0]],
                           party_id == 0 && shape.invert_a);
      CopyMaybeInvertWords(item.y, wires[gate->mInput[1]],
                           party_id == 0 && shape.invert_b);
      AndWords(item.out.data(), item.x.data(), item.y.data(), word_count);
    } else {
      for (size_t word = 0; word < word_count; ++word) {
        item.x[word] = wires[gate->mInput[0]][word];
        item.y[word] = wires[gate->mInput[1]][word];
        if (party_id == 0 && shape.invert_a) {
          item.x[word] ^= all_ones;
        }
        if (party_id == 0 && shape.invert_b) {
          item.y[word] ^= all_ones;
        }
        item.out[word] = item.x[word] & item.y[word];
      }
    }
  }

  std::vector<std::vector<uint128_t>> opened_by_pair(state.pairs.size());
  std::vector<std::future<std::vector<uint128_t>>> exchange_jobs;
  exchange_jobs.reserve(state.pairs.size());

  for (size_t pair_idx = 0; pair_idx < state.pairs.size(); ++pair_idx) {
    auto &pair = state.pairs[pair_idx];
    OTMPSI_ENFORCE(pair.online_sock != nullptr,
                 "distributed GMW online socket missing");
    auto &opened = opened_by_pair[pair_idx];
    opened.resize(2 * word_count * work.size());
    for (size_t gate_idx = 0; gate_idx < work.size(); ++gate_idx) {
      auto &item = work[gate_idx];
      for (size_t word = 0; word < word_count; ++word) {
        const size_t triple_off =
            pair.offset + 2 * (gate_idx * word_count + word);
        const size_t msg_off = 2 * (gate_idx * word_count + word);
        OTMPSI_ENFORCE(triple_off + 1 < pair.mult.size() &&
                         triple_off + 1 < pair.add.size(),
                     "distributed GMW triple offset out of range");
        if (pair.sender) {
          opened[msg_off] = pair.mult[triple_off] ^ item.x[word];
          opened[msg_off + 1] = pair.mult[triple_off + 1] ^ item.y[word];
        } else {
          opened[msg_off] = pair.mult[triple_off] ^ item.y[word];
          opened[msg_off + 1] = pair.mult[triple_off + 1] ^ item.x[word];
        }
      }
    }

    exchange_jobs.push_back(
        std::async(std::launch::async, [sock = pair.online_sock.get(), &opened,
                                        send_first = pair.sender] {
          return ExchangeU128Vector(*sock, opened, send_first);
        }));
  }

  for (size_t pair_idx = 0; pair_idx < state.pairs.size(); ++pair_idx) {
    auto &pair = state.pairs[pair_idx];
    auto peer_opened = exchange_jobs[pair_idx].get();
    for (size_t gate_idx = 0; gate_idx < work.size(); ++gate_idx) {
      auto &item = work[gate_idx];
      for (size_t word = 0; word < word_count; ++word) {
        const size_t triple_off =
            pair.offset + 2 * (gate_idx * word_count + word);
        const size_t msg_off = 2 * (gate_idx * word_count + word);
        if (pair.sender) {
          item.out[word] ^=
              (item.x[word] & peer_opened[msg_off]) ^ pair.add[triple_off];
          item.out[word] ^= (item.y[word] & peer_opened[msg_off + 1]) ^
                            pair.add[triple_off + 1];
        } else {
          item.out[word] ^= (peer_opened[msg_off] & pair.mult[triple_off]) ^
                            pair.add[triple_off];
          item.out[word] ^=
              (peer_opened[msg_off + 1] & pair.mult[triple_off + 1]) ^
              pair.add[triple_off + 1];
        }
      }
    }
    pair.offset += 2 * word_count * work.size();
  }

  for (auto &item : work) {
    if (party_id == 0 && item.shape.invert_out) {
      if (use_word_simd) {
        XorConstWords(item.out.data(), item.out.data(), ~uint64_t{0},
                      word_count);
      } else {
        for (auto &word : item.out) {
          word ^= all_ones;
        }
      }
    }
    wires[item.gate->mOutput] = std::move(item.out);
  }
}

std::vector<uint128_t>
LocalOutputWireShares(const std::vector<std::vector<uint128_t>> &wires,
                      const volePSI::BetaCircuit &cir, size_t output_bits) {
  OTMPSI_ENFORCE(cir.mOutputs.size() == 1 &&
                   cir.mOutputs[0].size() == output_bits,
               "distributed GMW output width mismatch");
  std::vector<uint128_t> out;
  if (output_bits == 0) {
    return out;
  }
  const size_t word_count = wires[cir.mOutputs[0][0]].size();
  out.reserve(output_bits * word_count);
  for (size_t bit = 0; bit < output_bits; ++bit) {
    const auto wire = cir.mOutputs[0][bit];
    out.insert(out.end(), wires[wire].begin(), wires[wire].end());
  }
  return out;
}

std::vector<uint8_t> DecodeOpenedBitWords(const std::vector<uint128_t> &words,
                                          size_t row_count) {
  std::vector<uint8_t> bits(row_count, 0);
  for (size_t row = 0; row < row_count; ++row) {
    bits[row] = ((words[row / 128] >> (row & 127U)) & 1U) != 0;
  }
  return bits;
}

volePSI::Matrix<volePSI::u8>
OpenedOutputWordsToMatrix(const std::vector<uint128_t> &words,
                          const OtmpsiParams &params, size_t output_bits) {
  const size_t word_count = (params.set_size + 127) / 128;
  OTMPSI_ENFORCE(words.size() == output_bits * word_count,
               "opened output word count mismatch");

  volePSI::Matrix<volePSI::u8> out;
  out.resize(params.set_size, (output_bits + 7) / 8);
  if (out.size() != 0) {
    std::memset(out.data(), 0, out.size() * sizeof(volePSI::u8));
  }

  for (size_t row_idx = 0; row_idx < params.set_size; ++row_idx) {
    auto *row = out[row_idx].data();
    const size_t word_idx = row_idx / 128;
    const size_t bit_idx = row_idx & 127U;
    for (size_t bit = 0; bit < output_bits; ++bit) {
      const auto word = words[bit * word_count + word_idx];
      SetPackedBit(row, bit, ((word >> bit_idx) & 1U) != 0);
    }
  }
  return out;
}

bool OpenedOutputWordBit(const std::vector<uint128_t> &words, size_t word_count,
                         size_t output_bit, size_t row_idx) {
  const auto word = words[output_bit * word_count + row_idx / 128];
  return ((word >> (row_idx & 127U)) & 1U) != 0;
}

std::vector<OtmpsiResult>
DecodeMaskedHolderOutputWords(const std::vector<uint128_t> &words,
                              const std::vector<uint128_t> &x0,
                              const OtmpsiParams &params) {
  const size_t output_bits = size_t{1} + params.parties;
  const size_t word_count = (params.set_size + 127) / 128;
  OTMPSI_ENFORCE(words.size() == output_bits * word_count,
               "opened masked-holder word count mismatch");
  OTMPSI_ENFORCE(x0.size() == params.set_size,
               "masked-holder output item count mismatch");

  std::vector<OtmpsiResult> results;
  results.reserve(params.set_size);
  for (size_t row_idx = 0; row_idx < params.set_size; ++row_idx) {
    const bool over_threshold =
        OpenedOutputWordBit(words, word_count, 0, row_idx);
    if (over_threshold) {
      std::vector<size_t> holders;
      holders.reserve(params.parties);
      for (size_t party = 0; party < params.parties; ++party) {
        if (OpenedOutputWordBit(words, word_count, 1 + party, row_idx)) {
          holders.push_back(party);
        }
      }
      OTMPSI_ENFORCE(!holders.empty() && holders.front() == 0,
                   "masked holder output missing leader holder at row {}",
                   row_idx);
      results.push_back({x0[row_idx], holders.size(), std::move(holders)});
      continue;
    }

    for (size_t party = 0; party < params.parties; ++party) {
      OTMPSI_ENFORCE(!OpenedOutputWordBit(words, word_count, 1 + party, row_idx),
                   "masked holder output leaked a below-threshold row {}",
                   row_idx);
    }
  }
  return results;
}

DistributedSharedOutput
RunNPartySharedOutputGmwParty(const volePSI::Matrix<volePSI::u8> &party_input,
                              size_t party_id, const OtmpsiParams &params,
                              const std::string &scope,
                              volePSI::BetaCircuit cir, size_t output_bits,
                              std::vector<PrecomputedGmwPairPrefix>
                                  precomputed_prefixes) {
  const auto start = std::chrono::steady_clock::now();
  auto wires = PackLocalNPartyGmwInput(party_input, cir, params);

  size_t nonlinear_gates = 0;
  for (const auto &gate : cir.mGates) {
    NPartyGmwGateShape shape;
    if (NormalizeNPartyAndGate(gate.mType, shape)) {
      ++nonlinear_gates;
    }
  }

  const size_t word_count = (params.set_size + 127) / 128;
  LogDebug(scope, "party=" + std::to_string(party_id) +
                      " distributed shared-output GMW nonlinear gates=" +
                      std::to_string(nonlinear_gates) +
                      ", levels=" + std::to_string(cir.mLevelCounts.size()));
  const size_t correlations_per_pair = nonlinear_gates * word_count * 2;
  auto triples = precomputed_prefixes.empty()
                     ? MakeLocalNPartyGmwState(
                           party_id, params.parties, correlations_per_pair,
                           params, 0)
                     : MakeLocalNPartyGmwStateFromPrecomputed(
                           party_id, params.parties, correlations_per_pair,
                           params, std::move(precomputed_prefixes), 0);
  size_t total_comm = 0;

  size_t gate_begin = 0;
  for (size_t level = 0; level < cir.mLevelCounts.size(); ++level) {
    const size_t level_count = cir.mLevelCounts[level];
    std::vector<const oc::BetaGate *> nonlinear_level;
    for (size_t idx = gate_begin; idx < gate_begin + level_count; ++idx) {
      const auto &gate = cir.mGates[idx];
      NPartyGmwGateShape shape;
      if (NormalizeNPartyAndGate(gate.mType, shape)) {
        nonlinear_level.push_back(&gate);
      } else {
        EvalNPartyLinearGateLocal(wires, gate, party_id);
      }
    }
    EvalNPartyAndGateLevelLocal(wires, nonlinear_level, triples, party_id);
    gate_begin += level_count;
  }
  CloseLocalNPartyGmwState(triples);
  total_comm += triples.comm_bytes;

  auto local_words = LocalOutputWireShares(wires, cir, output_bits);
  auto output_share =
      DecodeOutputShareWords(local_words, params.set_size, output_bits);
  return {std::move(output_share), RoleStats{SecondsSince(start), total_comm}};
}

NPartySharedOutputGmwOutput RunNPartySharedOutputGmwWithPreprocessing(
    const std::vector<volePSI::Matrix<volePSI::u8>> &party_inputs,
    std::vector<std::vector<PrecomputedGmwPairPrefix>> precomputed_prefixes,
    const OtmpsiParams &params, const std::string &scope,
    volePSI::BetaCircuit cir, size_t output_bits) {
  OTMPSI_ENFORCE(party_inputs.size() == params.parties &&
                   precomputed_prefixes.size() == params.parties,
               "precomputed shared-output GMW party count mismatch");

  std::vector<std::future<DistributedSharedOutput>> jobs(params.parties);
  for (size_t party = 0; party < params.parties; ++party) {
    auto party_cir = cir;
    auto prefixes = std::move(precomputed_prefixes[party]);
    jobs[party] = std::async(
        std::launch::async,
        [&, party, party_cir = std::move(party_cir),
         prefixes = std::move(prefixes)]() mutable {
          return RunNPartySharedOutputGmwParty(
              party_inputs[party], party, params, scope, std::move(party_cir),
              output_bits, std::move(prefixes));
        });
  }

  NPartySharedOutputGmwOutput run;
  run.output_shares.resize(params.parties);
  run.client_stats.assign(params.parties, {});
  for (size_t party = 0; party < params.parties; ++party) {
    auto party_run = jobs[party].get();
    run.output_shares[party] = std::move(party_run.output_share);
    if (party == 0) {
      run.leader = party_run.stats;
    } else {
      run.client_stats[party] = party_run.stats;
      run.clients.comm_bytes += party_run.stats.comm_bytes;
      run.clients.time_s =
          std::max(run.clients.time_s, party_run.stats.time_s);
    }
  }
  return run;
}

DistributedCompareOutput
RunNPartyCompareCircuitGmwParty(const volePSI::Matrix<volePSI::u8> &party_input,
                                size_t party_id, const OtmpsiParams &params,
                                const std::string &scope,
                                volePSI::BetaCircuit cir) {
  const auto start = std::chrono::steady_clock::now();
  auto wires = PackLocalNPartyGmwInput(party_input, cir, params);

  size_t nonlinear_gates = 0;
  for (const auto &gate : cir.mGates) {
    NPartyGmwGateShape shape;
    if (NormalizeNPartyAndGate(gate.mType, shape)) {
      ++nonlinear_gates;
    }
  }

  const size_t word_count = (params.set_size + 127) / 128;
  LogDebug(scope, "party=" + std::to_string(party_id) +
                      " distributed GMW nonlinear gates=" +
                      std::to_string(nonlinear_gates) +
                      ", levels=" + std::to_string(cir.mLevelCounts.size()));
  auto triples = MakeLocalNPartyGmwState(
      party_id, params.parties, nonlinear_gates * word_count * 2, params, 0);
  size_t total_comm = 0;

  size_t gate_begin = 0;
  for (size_t level = 0; level < cir.mLevelCounts.size(); ++level) {
    const size_t level_count = cir.mLevelCounts[level];
    std::vector<const oc::BetaGate *> nonlinear_level;
    for (size_t idx = gate_begin; idx < gate_begin + level_count; ++idx) {
      const auto &gate = cir.mGates[idx];
      NPartyGmwGateShape shape;
      if (NormalizeNPartyAndGate(gate.mType, shape)) {
        nonlinear_level.push_back(&gate);
      } else {
        EvalNPartyLinearGateLocal(wires, gate, party_id);
      }
    }
    EvalNPartyAndGateLevelLocal(wires, nonlinear_level, triples, party_id);
    gate_begin += level_count;
  }
  CloseLocalNPartyGmwState(triples);
  total_comm += triples.comm_bytes;

  auto local_words = LocalOutputWireShares(wires, cir, 1);
  std::vector<uint8_t> over_threshold(params.set_size, 0);
  if (party_id == 0) {
    auto opened_words = local_words;
    struct OutputRevealRecvState {
      std::vector<uint128_t> share_words;
      std::unique_ptr<coproto::Socket> sock;
    };
    std::vector<std::future<OutputRevealRecvState>> recv_jobs(params.parties);
    std::vector<std::unique_ptr<coproto::Socket>> reveal_socks(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      const auto tag = MakeSocketTag(params.base_port, party, 9);
      recv_jobs[party] = std::async(std::launch::async, [&, tag] {
        OutputRevealRecvState state;
        state.sock =
            std::make_unique<coproto::Socket>(AcquireLocalSocket(tag, 0));
        state.share_words.resize(local_words.size());
        macoro::sync_wait(state.sock->recv(coproto::span<uint128_t>(
            state.share_words.data(), state.share_words.size())));
        return state;
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      auto state = recv_jobs[party].get();
      for (size_t i = 0; i < opened_words.size(); ++i) {
        opened_words[i] ^= state.share_words[i];
      }
      reveal_socks[party] = std::move(state.sock);
    }
    over_threshold = DecodeOpenedBitWords(opened_words, params.set_size);
    std::vector<std::future<size_t>> send_jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      send_jobs[party] = std::async(std::launch::async, [&, party] {
        auto &sock = *reveal_socks[party];
        macoro::sync_wait(sock.send(coproto::span<const uint8_t>(
            over_threshold.data(), over_threshold.size())));
        macoro::sync_wait(sock.flush());
        const auto bytes =
            static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
        macoro::sync_wait(sock.close());
        return bytes;
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      total_comm += send_jobs[party].get();
    }
  } else {
    const auto tag = MakeSocketTag(params.base_port, party_id, 9);
    auto sock = AcquireLocalSocket(tag, 1);
    macoro::sync_wait(sock.send(coproto::span<const uint128_t>(
        local_words.data(), local_words.size())));
    macoro::sync_wait(sock.recv(
        coproto::span<uint8_t>(over_threshold.data(), over_threshold.size())));
    macoro::sync_wait(sock.flush());
    total_comm += static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
    macoro::sync_wait(sock.close());
  }

  return {std::move(over_threshold),
          RoleStats{SecondsSince(start), total_comm}};
}

DistributedPaperGmwOutput
RunNPartyMaskedOutputGmwParty(const volePSI::Matrix<volePSI::u8> &party_input,
                              size_t party_id, const std::vector<uint128_t> &x0,
                              const OtmpsiParams &params,
                              const std::string &scope) {
  const auto start = std::chrono::steady_clock::now();
  auto cir = BuildMaskedHolderThresholdCircuit(params.parties);
  auto wires = PackLocalNPartyGmwInput(party_input, cir, params);

  size_t nonlinear_gates = 0;
  for (const auto &gate : cir.mGates) {
    NPartyGmwGateShape shape;
    if (NormalizeNPartyAndGate(gate.mType, shape)) {
      ++nonlinear_gates;
    }
  }

  const size_t word_count = (params.set_size + 127) / 128;
  LogDebug(scope, "party=" + std::to_string(party_id) +
                      " distributed masked-holder GMW nonlinear gates=" +
                      std::to_string(nonlinear_gates) +
                      ", levels=" + std::to_string(cir.mLevelCounts.size()));
  auto triples = MakeLocalNPartyGmwState(
      party_id, params.parties, nonlinear_gates * word_count * 2, params, 0);
  size_t total_comm = 0;

  size_t gate_begin = 0;
  for (size_t level = 0; level < cir.mLevelCounts.size(); ++level) {
    const size_t level_count = cir.mLevelCounts[level];
    std::vector<const oc::BetaGate *> nonlinear_level;
    for (size_t idx = gate_begin; idx < gate_begin + level_count; ++idx) {
      const auto &gate = cir.mGates[idx];
      NPartyGmwGateShape shape;
      if (NormalizeNPartyAndGate(gate.mType, shape)) {
        nonlinear_level.push_back(&gate);
      } else {
        EvalNPartyLinearGateLocal(wires, gate, party_id);
      }
    }
    EvalNPartyAndGateLevelLocal(wires, nonlinear_level, triples, party_id);
    gate_begin += level_count;
  }
  CloseLocalNPartyGmwState(triples);
  total_comm += triples.comm_bytes;

  const auto output_bits = size_t{1} + params.parties;
  auto local_words = LocalOutputWireShares(wires, cir, output_bits);
  std::vector<OtmpsiResult> results;
  if (party_id == 0) {
    auto opened_words = local_words;
    struct OutputRevealRecvState {
      std::vector<uint128_t> share_words;
      std::unique_ptr<coproto::Socket> sock;
    };
    std::vector<std::future<OutputRevealRecvState>> recv_jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      const auto tag = MakeSocketTag(params.base_port, party, 9);
      recv_jobs[party] = std::async(std::launch::async, [&, tag] {
        OutputRevealRecvState state;
        state.sock =
            std::make_unique<coproto::Socket>(AcquireLocalSocket(tag, 0));
        state.share_words.resize(local_words.size());
        macoro::sync_wait(state.sock->recv(coproto::span<uint128_t>(
            state.share_words.data(), state.share_words.size())));
        return state;
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      auto state = recv_jobs[party].get();
      for (size_t i = 0; i < opened_words.size(); ++i) {
        opened_words[i] ^= state.share_words[i];
      }
      macoro::sync_wait(state.sock->flush());
      total_comm += static_cast<size_t>(state.sock->bytesSent() +
                                        state.sock->bytesReceived());
      macoro::sync_wait(state.sock->close());
    }
    results = DecodeMaskedHolderOutputWords(opened_words, x0, params);
  } else {
    const auto tag = MakeSocketTag(params.base_port, party_id, 9);
    auto sock = AcquireLocalSocket(tag, 1);
    macoro::sync_wait(sock.send(coproto::span<const uint128_t>(
        local_words.data(), local_words.size())));
    macoro::sync_wait(sock.flush());
    total_comm += static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
    macoro::sync_wait(sock.close());
  }

  return {std::move(results), RoleStats{SecondsSince(start), total_comm}};
}

DistributedPaperGmwOutput RunNPartyMembershipOutputGmwParty(
    const volePSI::Matrix<volePSI::u8> &party_input, size_t party_id,
    const std::vector<uint128_t> &x0, const OtmpsiParams &params,
    const std::string &scope) {
  const auto start = std::chrono::steady_clock::now();
  auto cir = BuildMembershipMaskedHolderThresholdCircuit(params.parties,
                                                         params.threshold);
  auto wires = PackLocalNPartyGmwInput(party_input, cir, params);

  size_t nonlinear_gates = 0;
  for (const auto &gate : cir.mGates) {
    NPartyGmwGateShape shape;
    if (NormalizeNPartyAndGate(gate.mType, shape)) {
      ++nonlinear_gates;
    }
  }

  const size_t word_count = (params.set_size + 127) / 128;
  LogDebug(scope, "party=" + std::to_string(party_id) +
                      " distributed membership-output GMW nonlinear gates=" +
                      std::to_string(nonlinear_gates) +
                      ", levels=" + std::to_string(cir.mLevelCounts.size()));
  auto triples = MakeLocalNPartyGmwState(
      party_id, params.parties, nonlinear_gates * word_count * 2, params, 0);
  size_t total_comm = 0;

  size_t gate_begin = 0;
  for (size_t level = 0; level < cir.mLevelCounts.size(); ++level) {
    const size_t level_count = cir.mLevelCounts[level];
    std::vector<const oc::BetaGate *> nonlinear_level;
    for (size_t idx = gate_begin; idx < gate_begin + level_count; ++idx) {
      const auto &gate = cir.mGates[idx];
      NPartyGmwGateShape shape;
      if (NormalizeNPartyAndGate(gate.mType, shape)) {
        nonlinear_level.push_back(&gate);
      } else {
        EvalNPartyLinearGateLocal(wires, gate, party_id);
      }
    }
    EvalNPartyAndGateLevelLocal(wires, nonlinear_level, triples, party_id);
    gate_begin += level_count;
  }
  CloseLocalNPartyGmwState(triples);
  total_comm += triples.comm_bytes;

  const auto output_bits = size_t{1} + params.parties;
  auto local_words = LocalOutputWireShares(wires, cir, output_bits);
  std::vector<OtmpsiResult> results;
  if (party_id == 0) {
    auto opened_words = local_words;
    struct OutputRevealRecvState {
      std::vector<uint128_t> share_words;
      std::unique_ptr<coproto::Socket> sock;
    };
    std::vector<std::future<OutputRevealRecvState>> recv_jobs(params.parties);
    for (size_t party = 1; party < params.parties; ++party) {
      const auto tag = MakeSocketTag(params.base_port, party, 9);
      recv_jobs[party] = std::async(std::launch::async, [&, tag] {
        OutputRevealRecvState state;
        state.sock =
            std::make_unique<coproto::Socket>(AcquireLocalSocket(tag, 0));
        state.share_words.resize(local_words.size());
        macoro::sync_wait(state.sock->recv(coproto::span<uint128_t>(
            state.share_words.data(), state.share_words.size())));
        return state;
      });
    }
    for (size_t party = 1; party < params.parties; ++party) {
      auto state = recv_jobs[party].get();
      for (size_t i = 0; i < opened_words.size(); ++i) {
        opened_words[i] ^= state.share_words[i];
      }
      macoro::sync_wait(state.sock->flush());
      total_comm += static_cast<size_t>(state.sock->bytesSent() +
                                        state.sock->bytesReceived());
      macoro::sync_wait(state.sock->close());
    }
    results = DecodeMaskedHolderOutputWords(opened_words, x0, params);
  } else {
    const auto tag = MakeSocketTag(params.base_port, party_id, 9);
    auto sock = AcquireLocalSocket(tag, 1);
    macoro::sync_wait(sock.send(coproto::span<const uint128_t>(
        local_words.data(), local_words.size())));
    macoro::sync_wait(sock.flush());
    total_comm += static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
    macoro::sync_wait(sock.close());
  }

  return {std::move(results), RoleStats{SecondsSince(start), total_comm}};
}

DistributedCompareOutput
RunNPartyMaskedCompareGmwParty(const volePSI::Matrix<volePSI::u8> &party_input,
                               size_t party_id, const OtmpsiParams &params,
                               const std::string &scope) {
  return RunNPartyCompareCircuitGmwParty(
      party_input, party_id, params, scope,
      BuildMaskedCountCompareCircuit(params.parties));
}

DistributedCompareOutput RunNPartyMembershipCompareGmwParty(
    const volePSI::Matrix<volePSI::u8> &party_input, size_t party_id,
    const OtmpsiParams &params, const std::string &scope) {
  return RunNPartyCompareCircuitGmwParty(
      party_input, party_id, params, scope,
      BuildMembershipSelectionThresholdCircuit(params.parties,
                                               params.threshold));
}

DistributedCompareOutput RunNPartyAdditiveCompareGmwParty(
    const volePSI::Matrix<volePSI::u8> &party_input, size_t party_id,
    const OtmpsiParams &params, const std::string &scope) {
  return RunNPartyCompareCircuitGmwParty(
      party_input, party_id, params, scope,
      BuildAdditiveMaskedCountCompareCircuit(params.parties));
}

} // namespace examples::otmpsi
