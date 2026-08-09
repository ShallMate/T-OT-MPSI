#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "otmpsi/local_socket.h"
#include "otmpsi/securejoin_oc_compat.h"
#include "otmpsi/compat/error.h"
#include "otmpsi/compat/int128.h"

#include "macoro/inline_scheduler.h"
#include "macoro/start_on.h"
#include "macoro/sync_wait.h"
#include "macoro/thread_pool.h"
#include "macoro/when_all.h"
#include "secure-join/CorGenerator/CorGenerator.h"
#include <cryptoTools/Common/block.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <secure-join/Prf/AltModPrf.h>
#include <secure-join/Prf/AltModPrfProto.h>

namespace examples::otmpsi {

namespace {

inline void SsoprfDebugLog(const std::string &message) {
  if (const char *env = std::getenv("OTMPSI_DEBUG")) {
    if (std::string_view(env) != "0") {
      std::cerr << "[ssoprf] " << message << std::endl;
    }
  }
}

inline void FlushSsoprfSocket(coproto::Socket &sock, const std::string &tag,
                              std::string_view role) {
  SsoprfDebugLog(std::string(role) + " flush start tag=" + tag);
  coproto::sync_wait(sock.flush());
  SsoprfDebugLog(std::string(role) + " flush done tag=" + tag);
}

} // namespace

} // namespace examples::otmpsi

namespace coproto {

#ifdef COPROTO_ASIO_LOG
inline std::mutex ggMtx;
inline std::vector<std::string> ggLog;
#endif

} // namespace coproto

namespace examples::otmpsi {

using osuCrypto::block;
using osuCrypto::PRNG;
using osuCrypto::u64;
using osuCrypto::u8;
using secJoin::AltModPrf;

inline block U128ToBlock(uint128_t v) {
  block b;
  static_assert(sizeof(b) == sizeof(v));
  std::memcpy(&b, &v, sizeof(v));
  return b;
}

inline uint128_t BlockToU128(const block &b) {
  uint128_t v = 0;
  static_assert(sizeof(v) == sizeof(b));
  std::memcpy(&v, &b, sizeof(b));
  return v;
}

inline AltModPrf::KeyType ExpandKey512FromSeed(uint128_t seed) {
  PRNG prng(U128ToBlock(seed));
  AltModPrf::KeyType key{};
  for (u64 i = 0; i < key.size(); ++i) {
    key[i] = prng.get<block>();
  }
  return key;
}

inline std::vector<uint128_t> EvalAltModPrf(const AltModPrf::KeyType &key,
                                            const std::vector<uint128_t> &xs) {
  static_assert(sizeof(block) == sizeof(uint128_t));
  AltModPrf prf(key);
  std::vector<block> inputs(xs.size());
  std::vector<block> outputs(xs.size());
  if (!xs.empty()) {
    std::memcpy(inputs.data(), xs.data(), xs.size() * sizeof(uint128_t));
  }
  prf.eval(inputs, outputs);

  std::vector<uint128_t> ret(xs.size());
  if (!outputs.empty()) {
    std::memcpy(ret.data(), outputs.data(), outputs.size() * sizeof(block));
  }
  return ret;
}

inline uint128_t EvalAltModPrf(uint128_t seed, uint128_t x) {
  AltModPrf prf(ExpandKey512FromSeed(seed));
  return BlockToU128(prf.eval(U128ToBlock(x)));
}

struct SsoprfResult {
  std::vector<uint128_t> share;
  size_t bytes = 0;
};

inline size_t RunSsoprfReceiverRoleSingleInto(
    const uint128_t *xs, size_t input_size, uint128_t *out_share,
    coproto::Socket sock, const std::string &tag, size_t num_threads = 1) {
  const u64 n = static_cast<u64>(input_size);
  const u64 nt = static_cast<u64>(std::max<size_t>(1, num_threads));
  const u64 log_batch = 18;
  const bool use_mod2f4_ot = true;

  if (n == 0) {
    return 0;
  }

  {
    std::ostringstream oss;
    oss << "receiver single start tag=" << tag << ", n=" << n
        << ", num_threads=" << nt;
    SsoprfDebugLog(oss.str());
  }

  macoro::thread_pool pool;
  auto work = pool.make_work();
  pool.create_threads(static_cast<std::size_t>(nt));
  sock.setExecutor(pool);

  PRNG prng_x(osuCrypto::sysRandomSeed());
  std::vector<block> inputs(n);
  std::memcpy(inputs.data(), xs, input_size * sizeof(uint128_t));
  std::vector<block> share_x(n);

  auto party_x = [&]() -> macoro::task<void> {
    osuCrypto::SilentOtExtSender key_ot_sender;
    key_ot_sender.configure(AltModPrf::KeySize);
    std::vector<std::array<block, 2>> sk(AltModPrf::KeySize);
    SsoprfDebugLog("receiver single key_ot send start tag=" + tag);
    co_await key_ot_sender.send(sk, prng_x, sock);
    SsoprfDebugLog("receiver single key_ot send done tag=" + tag);

    secJoin::CorGenerator ole1;
    ole1.init(sock.fork(), prng_x, 1, nt, 1ull << log_batch, 1);

    secJoin::AltModWPrfReceiver receiver;
    receiver.mUseMod2F4Ot = use_mod2f4_ot;
    receiver.init(n, ole1, secJoin::AltModPrfKeyMode::SenderOnly,
                  secJoin::AltModPrfInputMode::ReceiverOnly, {}, sk);

    SsoprfDebugLog("receiver single evaluate start tag=" + tag);
    co_await macoro::when_all_ready(
        ole1.start(), receiver.evaluate(inputs, share_x, sock, prng_x));
    SsoprfDebugLog("receiver single evaluate done tag=" + tag);
    co_return;
  };

  SsoprfDebugLog("receiver single sync_wait start tag=" + tag);
  coproto::sync_wait(party_x() | macoro::start_on(pool));
  SsoprfDebugLog("receiver single sync_wait done tag=" + tag);
  FlushSsoprfSocket(sock, tag, "receiver");

  for (u64 i = 0; i < n; ++i) {
    out_share[i] = BlockToU128(share_x[i]);
  }
  const size_t bytes =
      static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
  {
    std::ostringstream oss;
    oss << "receiver single done tag=" << tag << ", bytes=" << bytes;
    SsoprfDebugLog(oss.str());
  }
  static_cast<void>(work);
  return bytes;
}

inline SsoprfResult
RunSsoprfReceiverRoleSingle(const std::vector<uint128_t> &xs,
                            const std::string &tag, size_t num_threads = 1) {
  SsoprfResult result;
  result.share.assign(xs.size(), 0);
  if (xs.empty()) {
    return result;
  }
  auto sock = AcquireLocalSocket(tag, 0);
  result.bytes =
      RunSsoprfReceiverRoleSingleInto(xs.data(), xs.size(), result.share.data(),
                                      std::move(sock), tag, num_threads);
  return result;
}

inline size_t
RunSsoprfSenderRoleSingleInto(size_t input_size, uint128_t key_seed,
                              uint128_t *out_share, coproto::Socket sock,
                              const std::string &tag, size_t num_threads = 1) {
  const u64 n = static_cast<u64>(input_size);
  const u64 nt = static_cast<u64>(std::max<size_t>(1, num_threads));
  const u64 log_batch = 18;
  const bool use_mod2f4_ot = true;

  if (n == 0) {
    return 0;
  }

  {
    std::ostringstream oss;
    oss << "sender single start tag=" << tag << ", n=" << n
        << ", num_threads=" << nt;
    SsoprfDebugLog(oss.str());
  }

  macoro::thread_pool pool;
  auto work = pool.make_work();
  pool.create_threads(static_cast<std::size_t>(nt));
  sock.setExecutor(pool);

  PRNG prng_k(osuCrypto::sysRandomSeed());
  AltModPrf prf;
  auto key = ExpandKey512FromSeed(key_seed);
  prf.setKey(key);
  std::vector<block> share_k(n);

  auto party_k = [&]() -> macoro::task<void> {
    osuCrypto::SilentOtExtReceiver key_ot_receiver;
    key_ot_receiver.configure(AltModPrf::KeySize);
    std::vector<block> rk(AltModPrf::KeySize);

    osuCrypto::BitVector key_bits;
    key_bits.append(reinterpret_cast<u8 *>(prf.getKey().data()),
                    AltModPrf::KeySize);
    SsoprfDebugLog("sender single key_ot recv start tag=" + tag);
    co_await key_ot_receiver.receive(key_bits, rk, prng_k, sock);
    SsoprfDebugLog("sender single key_ot recv done tag=" + tag);

    secJoin::CorGenerator ole0;
    ole0.init(sock.fork(), prng_k, 0, nt, 1ull << log_batch, 1);

    secJoin::AltModWPrfSender sender;
    sender.mUseMod2F4Ot = use_mod2f4_ot;
    sender.init(n, ole0, secJoin::AltModPrfKeyMode::SenderOnly,
                secJoin::AltModPrfInputMode::ReceiverOnly, prf.getKey(), rk);

    SsoprfDebugLog("sender single evaluate start tag=" + tag);
    co_await macoro::when_all_ready(ole0.start(),
                                    sender.evaluate({}, share_k, sock, prng_k));
    SsoprfDebugLog("sender single evaluate done tag=" + tag);
    co_return;
  };

  SsoprfDebugLog("sender single sync_wait start tag=" + tag);
  coproto::sync_wait(party_k() | macoro::start_on(pool));
  SsoprfDebugLog("sender single sync_wait done tag=" + tag);
  FlushSsoprfSocket(sock, tag, "sender");

  for (u64 i = 0; i < n; ++i) {
    out_share[i] = BlockToU128(share_k[i]);
  }
  const size_t bytes =
      static_cast<size_t>(sock.bytesSent() + sock.bytesReceived());
  {
    std::ostringstream oss;
    oss << "sender single done tag=" << tag << ", bytes=" << bytes;
    SsoprfDebugLog(oss.str());
  }
  static_cast<void>(work);
  return bytes;
}

inline SsoprfResult RunSsoprfSenderRoleSingle(size_t input_size,
                                              uint128_t key_seed,
                                              const std::string &tag,
                                              size_t num_threads = 1) {
  SsoprfResult result;
  result.share.assign(input_size, 0);
  if (input_size == 0) {
    return result;
  }
  auto sock = AcquireLocalSocket(tag, 1);
  result.bytes =
      RunSsoprfSenderRoleSingleInto(input_size, key_seed, result.share.data(),
                                    std::move(sock), tag, num_threads);
  return result;
}

inline SsoprfResult RunSsoprfReceiverRole(const std::vector<uint128_t> &xs,
                                          const std::string &tag,
                                          size_t num_threads = 1) {
  const size_t shard_count =
      std::min(xs.size(), std::max<size_t>(1, num_threads));
  {
    std::ostringstream oss;
    oss << "receiver start tag=" << tag << ", n=" << xs.size()
        << ", shard_count=" << shard_count << ", num_threads=" << num_threads;
    SsoprfDebugLog(oss.str());
  }
  if (xs.empty() || shard_count <= 1) {
    return RunSsoprfReceiverRoleSingle(xs, tag, 1);
  }

  SsoprfResult result;
  result.share.assign(xs.size(), 0);
  std::vector<std::future<size_t>> jobs;
  jobs.reserve(shard_count);
  std::vector<size_t> offsets(shard_count + 1, 0);
  for (size_t shard = 0; shard < shard_count; ++shard) {
    offsets[shard] = shard * xs.size() / shard_count;
  }
  offsets[shard_count] = xs.size();

  for (size_t shard = 0; shard < shard_count; ++shard) {
    const size_t begin = offsets[shard];
    const size_t end = offsets[shard + 1];
    const size_t chunk_size = end - begin;
    const auto *chunk_ptr = xs.data() + begin;
    auto *share_ptr = result.share.data() + begin;
    const std::string shard_tag = tag + "/shard/" + std::to_string(shard);
    {
      std::ostringstream oss;
      oss << "receiver shard launch shard=" << shard << ", begin=" << begin
          << ", end=" << end << ", tag=" << shard_tag;
      SsoprfDebugLog(oss.str());
    }
    jobs.emplace_back(std::async(std::launch::async, [chunk_ptr, chunk_size,
                                                      share_ptr, shard_tag]() {
      auto sock = AcquireLocalSocket(shard_tag, 0);
      return RunSsoprfReceiverRoleSingleInto(chunk_ptr, chunk_size, share_ptr,
                                             std::move(sock), shard_tag, 1);
    }));
  }

  for (size_t shard = 0; shard < jobs.size(); ++shard) {
    auto &job = jobs[shard];
    const size_t partial_bytes = job.get();
    const size_t chunk_size = offsets[shard + 1] - offsets[shard];
    {
      std::ostringstream oss;
      oss << "receiver shard done shard=" << shard
          << ", chunk_size=" << chunk_size << ", bytes=" << partial_bytes;
      SsoprfDebugLog(oss.str());
    }
    result.bytes += partial_bytes;
  }
  {
    std::ostringstream oss;
    oss << "receiver done tag=" << tag << ", total_bytes=" << result.bytes;
    SsoprfDebugLog(oss.str());
  }
  return result;
}

inline SsoprfResult RunSsoprfSenderRole(size_t input_size, uint128_t key_seed,
                                        const std::string &tag,
                                        size_t num_threads = 1) {
  const size_t shard_count =
      std::min(input_size, std::max<size_t>(1, num_threads));
  {
    std::ostringstream oss;
    oss << "sender start tag=" << tag << ", n=" << input_size
        << ", shard_count=" << shard_count << ", num_threads=" << num_threads;
    SsoprfDebugLog(oss.str());
  }
  if (input_size == 0 || shard_count <= 1) {
    return RunSsoprfSenderRoleSingle(input_size, key_seed, tag, 1);
  }

  SsoprfResult result;
  result.share.assign(input_size, 0);
  std::vector<std::future<size_t>> jobs;
  jobs.reserve(shard_count);
  std::vector<size_t> offsets(shard_count + 1, 0);
  for (size_t shard = 0; shard < shard_count; ++shard) {
    offsets[shard] = shard * input_size / shard_count;
  }
  offsets[shard_count] = input_size;

  for (size_t shard = 0; shard < shard_count; ++shard) {
    const size_t begin = offsets[shard];
    const size_t end = offsets[shard + 1];
    const size_t chunk_size = end - begin;
    auto *share_ptr = result.share.data() + begin;
    const std::string shard_tag = tag + "/shard/" + std::to_string(shard);
    {
      std::ostringstream oss;
      oss << "sender shard launch shard=" << shard << ", begin=" << begin
          << ", end=" << end << ", tag=" << shard_tag;
      SsoprfDebugLog(oss.str());
    }
    jobs.emplace_back(std::async(
        std::launch::async, [chunk_size, key_seed, share_ptr, shard_tag]() {
          auto sock = AcquireLocalSocket(shard_tag, 1);
          return RunSsoprfSenderRoleSingleInto(chunk_size, key_seed, share_ptr,
                                               std::move(sock), shard_tag, 1);
        }));
  }

  for (size_t shard = 0; shard < jobs.size(); ++shard) {
    auto &job = jobs[shard];
    const size_t partial_bytes = job.get();
    const size_t chunk_size = offsets[shard + 1] - offsets[shard];
    {
      std::ostringstream oss;
      oss << "sender shard done shard=" << shard
          << ", chunk_size=" << chunk_size << ", bytes=" << partial_bytes;
      SsoprfDebugLog(oss.str());
    }
    result.bytes += partial_bytes;
  }
  {
    std::ostringstream oss;
    oss << "sender done tag=" << tag << ", total_bytes=" << result.bytes;
    SsoprfDebugLog(oss.str());
  }
  return result;
}

} // namespace examples::otmpsi
