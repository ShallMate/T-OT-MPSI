#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "otmpsi/GMW/SilentTripleGen.h"
#include "otmpsi/local_socket.h"
#include "macoro/sync_wait.h"

namespace {

struct BenchConfig {
  volePSI::u64 num_ots = 1ull << 26;
  volePSI::u64 batch_size = 1ull << 18;
  volePSI::u64 threads = 1;
};

volePSI::u64 ParseU64(const char* s) {
  char* end = nullptr;
  const auto v = std::strtoull(s, &end, 0);
  if (!s[0] || (end && *end)) {
    throw std::invalid_argument(std::string("bad integer: ") + s);
  }
  return static_cast<volePSI::u64>(v);
}

BenchConfig ParseArgs(int argc, char** argv) {
  BenchConfig cfg;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto need_value = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        throw std::invalid_argument(std::string("missing value for ") + name);
      }
      return argv[++i];
    };
    if (arg == "--num-ots") {
      cfg.num_ots = ParseU64(need_value("--num-ots"));
    } else if (arg == "--batch-size") {
      cfg.batch_size = ParseU64(need_value("--batch-size"));
    } else if (arg == "--threads") {
      cfg.threads = ParseU64(need_value("--threads"));
    } else {
      throw std::invalid_argument("unknown argument: " + arg);
    }
  }
  if (cfg.num_ots == 0 || cfg.batch_size == 0 || cfg.threads == 0) {
    throw std::invalid_argument("num-ots, batch-size, and threads must be > 0");
  }
  return cfg;
}

struct PartyResult {
  double seconds = 0;
  std::uint64_t sent = 0;
  std::uint64_t received = 0;
  std::uint64_t mult_blocks = 0;
  std::uint64_t add_blocks = 0;
};

PartyResult RunParty(std::string tag, int rank, const BenchConfig& cfg) {
  auto sock = examples::otmpsi::AcquireLocalSocket(tag, rank);
  volePSI::SilentTripleGen gen;
  osuCrypto::PRNG prng(osuCrypto::block(rank + 1, rank + 2));
  const auto role = rank == 0 ? volePSI::Mode::Sender : volePSI::Mode::Receiver;

  const auto start = std::chrono::steady_clock::now();
  gen.init(cfg.num_ots, cfg.batch_size, cfg.threads, role, prng.get());
  macoro::sync_wait(gen.generateBaseOts(static_cast<volePSI::u64>(rank), sock));
  macoro::sync_wait(gen.expand(sock));
  macoro::sync_wait(sock.flush());
  const auto stop = std::chrono::steady_clock::now();

  PartyResult out;
  out.seconds = std::chrono::duration<double>(stop - start).count();
  out.sent = static_cast<std::uint64_t>(sock.bytesSent());
  out.received = static_cast<std::uint64_t>(sock.bytesReceived());
  out.mult_blocks = static_cast<std::uint64_t>(gen.multSize());
  out.add_blocks = static_cast<std::uint64_t>(gen.addSize());
  return out;
}

double ToMB(std::uint64_t bytes) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto cfg = ParseArgs(argc, argv);
    const auto tag = "otmpsi/gmw-triple-bench";
    const auto wall_start = std::chrono::steady_clock::now();
    auto p0 = std::async(std::launch::async, [&] { return RunParty(tag, 0, cfg); });
    auto p1 = std::async(std::launch::async, [&] { return RunParty(tag, 1, cfg); });
    const auto r0 = p0.get();
    const auto r1 = p1.get();
    const auto wall_stop = std::chrono::steady_clock::now();
    const auto wall = std::chrono::duration<double>(wall_stop - wall_start).count();

    std::cout << "num_ots=" << cfg.num_ots << "\n";
    std::cout << "batch_size=" << cfg.batch_size << "\n";
    std::cout << "threads=" << cfg.threads << "\n";
    std::cout << "wall_time_s=" << wall << "\n";
    std::cout << "leader_time_s=" << r0.seconds << "\n";
    std::cout << "leader_comm_MB=" << ToMB(r0.sent + r0.received) << "\n";
    std::cout << "leader_sent_MB=" << ToMB(r0.sent) << "\n";
    std::cout << "leader_recv_MB=" << ToMB(r0.received) << "\n";
    std::cout << "client_time_s=" << r1.seconds << "\n";
    std::cout << "client_comm_MB=" << ToMB(r1.sent + r1.received) << "\n";
    std::cout << "client_sent_MB=" << ToMB(r1.sent) << "\n";
    std::cout << "client_recv_MB=" << ToMB(r1.received) << "\n";
    std::cout << "leader_mult_blocks=" << r0.mult_blocks << "\n";
    std::cout << "client_mult_blocks=" << r1.mult_blocks << "\n";
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
