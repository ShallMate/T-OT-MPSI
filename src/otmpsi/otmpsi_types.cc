#include "otmpsi/otmpsi_types.h"

namespace examples::otmpsi {

namespace detail {

uint64_t SplitMix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

uint64_t Low64(uint128_t v) { return static_cast<uint64_t>(v); }

uint64_t High64(uint128_t v) { return static_cast<uint64_t>(v >> 64); }

}

std::string_view B2aBackendName(B2aBackend backend) {
  switch (backend) {
  case B2aBackend::kSilentOt:
    return "silent_ot";
  }
  return "unknown";
}

std::string_view FinalMaskBackendName(FinalMaskBackend backend) {
  switch (backend) {
  case FinalMaskBackend::kBooleanGmw:
    return "boolean_gmw";
  case FinalMaskBackend::kArithmetic:
    return "arithmetic";
  }
  return "unknown";
}

std::string_view FinalOutputModeName(FinalOutputMode mode) {
  switch (mode) {
  case FinalOutputMode::kLeaderOnly:
    return "leader_only";
  case FinalOutputMode::kClientOutputBasis:
    return "client_output_basis";
  }
  return "unknown";
}

RoleStats &operator+=(RoleStats &lhs, const RoleStats &rhs) {
  lhs.time_s += rhs.time_s;
  lhs.comm_bytes += rhs.comm_bytes;
  return lhs;
}

uint128_t MakeItem(uint64_t domain, uint64_t idx) {
  const uint64_t lo =
      detail::SplitMix64(domain ^ (idx * 0x9e3779b97f4a7c15ULL));
  const uint64_t hi = detail::SplitMix64((domain + 0xd1b54a32d192ed03ULL) ^
                                         (idx * 0xbf58476d1ce4e5b9ULL));
  return (static_cast<uint128_t>(hi) << 64) | lo;
}

block U64ToBlock(uint64_t v) { return U128ToBlock(static_cast<uint128_t>(v)); }

uint64_t BlockToU64(const block &b) {
  return static_cast<uint64_t>(BlockToU128(b));
}

std::string HexU128(uint128_t v) {
  std::ostringstream oss;
  oss << std::hex << detail::High64(v);
  oss.width(16);
  oss.fill('0');
  oss << detail::Low64(v);
  return oss.str();
}

void LogDebug(const std::string &scope, const std::string &msg) {
  if (const char *env = std::getenv("OTMPSI_DEBUG")) {
    if (std::string_view(env) != "0") {
      std::cerr << "[" << scope << "] " << msg << std::endl;
    }
  }
}

bool EnvEnabled(const char *name) {
  if (const char *env = std::getenv(name)) {
    return env[0] != '\0' && !(env[0] == '0' && env[1] == '\0');
  }
  return false;
}

bool UsesThresholdTwoRelaxedFastPath(const OtmpsiParams &params) {
  return params.threshold == 2 &&
         params.final_output_mode == FinalOutputMode::kClientOutputBasis &&
         !EnvEnabled("OTMPSI_DISABLE_T2_RELAXED_FAST") &&
         (EnvEnabled("OTMPSI_ENABLE_T2_FAST") ||
          EnvEnabled("OTMPSI_ENABLE_T2_RELAXED_FAST"));
}

bool UsesTreeX2aRoute(const OtmpsiParams &params) {
  if (params.final_output_mode == FinalOutputMode::kLeaderOnly) {
    return true;
  }
  return !UsesThresholdTwoRelaxedFastPath(params) &&
         !EnvEnabled("OTMPSI_DISABLE_RELAXED_BOOLEAN_GMW");
}

double SecondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
      .count();
}

size_t BitsForPartyCount(size_t parties) {
  size_t bits = 0;
  size_t cap = 1;
  while (cap <= parties) {
    cap <<= 1;
    ++bits;
  }
  return std::max<size_t>(1, bits);
}

size_t CompareBitsForPartyCount(size_t parties) {
  OTMPSI_ENFORCE(parties >= 2 && parties <= kMaxPartiesForComparison,
               "fixed 8-bit comparison requires 2 <= N < 128");
  return kComparisonBits;
}

uint64_t LowBitsMask(size_t bits) {
  OTMPSI_ENFORCE(bits > 0 && bits <= 63, "unsupported bit width {}", bits);
  return (uint64_t{1} << bits) - 1;
}

PaperGmwLayout MakePaperGmwLayout(size_t parties) {
  PaperGmwLayout layout;
  layout.parties = parties;
  layout.count_bits = BitsForPartyCount(parties);
  layout.one_offset = (parties - 1) * layout.rho_bits;
  layout.threshold_offset = layout.one_offset + 1;
  layout.zero_offset = layout.threshold_offset + layout.count_bits;
  layout.input_bits = layout.zero_offset + 1;
  layout.output_bits = 1 + layout.count_bits + parties;
  return layout;
}

MembershipGmwLayout MakeMembershipGmwLayout(size_t parties) {
  MembershipGmwLayout layout;
  layout.parties = parties;
  layout.count_bits = BitsForPartyCount(parties);
  layout.membership_offset = 0;
  layout.one_offset = parties - 1;
  layout.threshold_offset = layout.one_offset + 1;
  layout.zero_offset = layout.threshold_offset + layout.count_bits;
  layout.input_bits = layout.zero_offset + 1;
  layout.output_bits = 1 + layout.count_bits + parties;
  return layout;
}

ArithmeticCountGmwLayout MakeArithmeticCountGmwLayout(size_t parties) {
  ArithmeticCountGmwLayout layout;
  layout.parties = parties;
  layout.count_bits = BitsForPartyCount(parties);
  layout.count_share_offset = 0;
  layout.membership_offset = parties * layout.count_bits;
  layout.one_offset = layout.membership_offset + parties - 1;
  layout.threshold_offset = layout.one_offset + 1;
  layout.zero_offset = layout.threshold_offset + layout.count_bits;
  layout.input_bits = layout.zero_offset + 1;
  layout.output_bits = 1 + layout.count_bits + parties;
  return layout;
}

MaskedCountGmwLayout MakeMaskedCountGmwLayout(size_t parties) {
  MaskedCountGmwLayout layout;
  layout.parties = parties;
  layout.count_bits = BitsForPartyCount(parties);
  layout.compare_bits = CompareBitsForPartyCount(parties);
  layout.mask_bit_offset = 0;
  layout.opened_mask_offset = layout.mask_bit_offset + layout.compare_bits;
  layout.membership_offset = layout.opened_mask_offset + layout.compare_bits;
  layout.one_offset = layout.membership_offset + parties - 1;
  layout.threshold_offset = layout.one_offset + 1;
  layout.zero_offset = layout.threshold_offset + layout.compare_bits;
  layout.input_bits = layout.zero_offset + 1;
  layout.output_bits = 1 + layout.count_bits + parties;
  return layout;
}

AdditiveMaskedCompareLayout MakeAdditiveMaskedCompareLayout(size_t parties) {
  AdditiveMaskedCompareLayout layout;
  layout.parties = parties;
  layout.compare_bits = CompareBitsForPartyCount(parties);
  layout.opened_mask_offset = 0;
  layout.random_share_offset = layout.opened_mask_offset + layout.compare_bits;
  layout.one_offset =
      layout.random_share_offset + parties * layout.compare_bits;
  layout.zero_offset = layout.one_offset + 1;
  layout.input_bits = layout.zero_offset + 1;
  return layout;
}

}
