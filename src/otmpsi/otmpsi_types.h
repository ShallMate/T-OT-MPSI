#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cryptoTools/Common/Defines.h"
#include "otmpsi/GMW/Circuit.h"
#include "otmpsi/GMW/Gmw.h"
#include "otmpsi/GMW/SilentTripleGen.h"
#include "otmpsi/bandokvs/band_okvs.h"
#include "otmpsi/local_socket.h"
#include "otmpsi/ssoprf.h"
#include "libOTe/TwoChooseOne/OTExtInterface.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtSender.h"
#include "macoro/sync_wait.h"
#include "otmpsi/compat/error.h"
#include "otmpsi/compat/int128.h"
#include "otmpsi/compat/parallel.h"

namespace examples::otmpsi {

inline constexpr size_t kComparisonBits = 8;
inline constexpr size_t kMaxPartiesForComparison = 127;

namespace detail {
uint64_t SplitMix64(uint64_t x);
uint64_t Low64(uint128_t v);
uint64_t High64(uint128_t v);
}

enum class B2aBackend {
  kSilentOt,
};

enum class FinalOutputMode {
  kLeaderOnly,
  kClientOutputBasis,
};

std::string_view B2aBackendName(B2aBackend backend);
std::string_view FinalOutputModeName(FinalOutputMode mode);

struct OtmpsiParams {
  size_t parties = 4;
  size_t set_size = 1 << 12;
  size_t threshold = 2;
  std::optional<size_t> target_output_size;
  size_t threads = 1;
  uint16_t base_port = 19350;
  B2aBackend b2a_backend = B2aBackend::kSilentOt;
  FinalOutputMode final_output_mode = FinalOutputMode::kLeaderOnly;
};

struct OtmpsiResult {
  uint128_t item = 0;
  size_t count = 0;
  std::vector<size_t> holders;
};

struct RoleStats {
  double time_s = 0.0;
  size_t comm_bytes = 0;
};

struct MaskPrepShare {
  std::vector<uint64_t> arithmetic_share;
  std::vector<uint64_t> boolean_share;
};

RoleStats &operator+=(RoleStats &lhs, const RoleStats &rhs);

struct PhaseStats {
  double time_s = 0.0;
  RoleStats leader;
  RoleStats clients;
};

struct OtmpsiStats {
  double total_time_s = 0.0;
  RoleStats leader;
  RoleStats clients;
  PhaseStats ssbsmt;
  PhaseStats pairwise_zero;
  PhaseStats b2a;
  PhaseStats nparty_gmw;
  std::vector<RoleStats> client_ssbsmt;
  std::vector<RoleStats> client_zero;
  std::vector<RoleStats> client_b2a;
  std::vector<RoleStats> client_nparty_gmw;
};

struct OtmpsiRun {
  std::vector<OtmpsiResult> results;
  OtmpsiStats stats;
  // Relaxed clients receive s; standard clients receive no output.
  std::optional<size_t> client_output_size;
};

struct MaskedCountCompareData {
  size_t compare_bits = 0;
  std::vector<uint64_t> opened_masked_values;
  std::vector<std::vector<uint64_t>> mask_bit_shares;
  std::vector<size_t> open_comm_bytes;
};

struct ShareWithStats {
  std::vector<uint128_t> share;
  RoleStats stats;
};

uint128_t MakeItem(uint64_t domain, uint64_t idx);
block U64ToBlock(uint64_t v);
uint64_t BlockToU64(const block &b);
std::string HexU128(uint128_t v);
void LogDebug(const std::string &scope, const std::string &msg);
bool EnvEnabled(const char *name);
double SecondsSince(std::chrono::steady_clock::time_point start);
size_t CompareBitsForPartyCount(size_t parties);
uint64_t LowBitsMask(size_t bits);

}
