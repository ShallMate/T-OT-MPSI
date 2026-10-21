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

double SecondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
      .count();
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

} // namespace examples::otmpsi
