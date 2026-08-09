#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "otmpsi/coproto_asio_globals.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "coproto/Socket/Socket.h"
#include "otmpsi/compat/error.h"

namespace examples::otmpsi {

struct LocalSocketEntry {
  std::optional<coproto::LocalAsyncSocket> socks[2];
  size_t claimed = 0;
};

inline bool UseTcpSockets() {
  const char *mode = std::getenv("OTMPSI_TRANSPORT");
  return mode != nullptr &&
         (std::string_view(mode) == "tcp" ||
          std::string_view(mode) == "tcp_async");
}

inline bool UseAsyncCoprotoTcpSockets() {
  const char *mode = std::getenv("OTMPSI_TRANSPORT");
  return mode != nullptr && std::string_view(mode) == "tcp_async";
}

inline std::vector<std::string> SplitSocketTag(const std::string &tag) {
  std::vector<std::string> parts;
  std::stringstream ss(tag);
  std::string part;
  while (std::getline(ss, part, '/')) {
    parts.push_back(part);
  }
  return parts;
}

inline bool UseAsyncCoprotoTcpSocketsForTag(const std::string &tag) {
  if (UseAsyncCoprotoTcpSockets()) {
    return true;
  }
  const char *channels_env = std::getenv("OTMPSI_TCP_ASYNC_CHANNELS");
  if (channels_env == nullptr || channels_env[0] == '\0') {
    return false;
  }
  const auto parts = SplitSocketTag(tag);
  if (parts.size() < 4) {
    return false;
  }
  const std::string_view channel = parts[3];
  std::stringstream ss(channels_env);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (item == channel) {
      return true;
    }
  }
  return false;
}

inline std::string SocketTagPathName(const std::string &tag) {
  std::string name = tag;
  for (auto &ch : name) {
    if (ch == '/' || ch == ':' || ch == ' ') {
      ch = '_';
    }
  }
  return name + ".addr";
}

inline std::optional<std::string> ReadMappedTcpAddressForTag(
    const std::string &tag) {
  const char *dir = std::getenv("OTMPSI_PORT_MAP_DIR");
  if (dir == nullptr || dir[0] == '\0') {
    return std::nullopt;
  }
  const std::string dir_path = dir;
  auto read_tag = [&](const std::string &candidate)
      -> std::optional<std::string> {
    std::ifstream in(dir_path + "/" + SocketTagPathName(candidate));
    if (!in) {
      return std::nullopt;
    }
    std::string address;
    std::getline(in, address);
    if (address.empty()) {
      return std::nullopt;
    }
    return address;
  };
  if (auto address = read_tag(tag)) {
    return address;
  }
  const auto parts = SplitSocketTag(tag);
  if (parts.size() >= 5 && (parts.size() > 5 || parts[4] != "0")) {
    return read_tag(parts[0] + "/" + parts[1] + "/" + parts[2] + "/" +
                    parts[3] + "/0");
  }
  return std::nullopt;
}

inline std::string TcpAddressForTag(const std::string &tag) {
  if (auto mapped = ReadMappedTcpAddressForTag(tag)) {
    return *mapped;
  }
  const auto parts = SplitSocketTag(tag);
  OTMPSI_ENFORCE(parts.size() >= 4 && parts[0] == "otmpsi",
               "unsupported TCP socket tag: {}", tag);
  const auto base = static_cast<uint32_t>(std::stoul(parts[1]));
  const auto party_or_pair = static_cast<uint32_t>(std::stoul(parts[2]));
  const auto channel = static_cast<uint32_t>(std::stoul(parts[3]));
  OTMPSI_ENFORCE(
      party_or_pair < 1512,
      "TCP socket tag {} needs OTMPSI_PORT_MAP_DIR because its pair id "
      "would collide in the fallback mapping",
      tag);
  const uint32_t port = base + channel * 512 + (party_or_pair % 512);
  OTMPSI_ENFORCE(port <= 65535, "TCP socket port overflow for tag {}: {}", tag,
               port);

  const char *host_env = std::getenv("OTMPSI_TCP_HOST");
  const std::string host =
      (host_env != nullptr && host_env[0] != '\0') ? host_env : "127.0.0.1";
  return host + ":" + std::to_string(port);
}

struct ParsedTcpAddress {
  std::string host;
  uint16_t port = 0;
};

inline ParsedTcpAddress ParseTcpAddress(const std::string &address) {
  const auto pos = address.rfind(':');
  OTMPSI_ENFORCE(pos != std::string::npos, "bad TCP address: {}", address);
  auto host = address.substr(0, pos);
  if (host == "localhost") {
    host = "127.0.0.1";
  }
  const auto port = std::stoul(address.substr(pos + 1));
  OTMPSI_ENFORCE(port <= 65535, "bad TCP port in {}", address);
  return {host, static_cast<uint16_t>(port)};
}

inline sockaddr_in MakeTcpSockAddr(const std::string &address) {
  const auto parsed = ParseTcpAddress(address);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(parsed.port);
  OTMPSI_ENFORCE(inet_pton(AF_INET, parsed.host.c_str(), &addr.sin_addr) == 1,
               "only IPv4 TCP addresses are supported: {}", address);
  return addr;
}

inline void CloseFd(int fd) {
  if (fd >= 0) {
    ::close(fd);
  }
}

inline void WriteAllFd(int fd, const void *data, size_t size) {
  const auto *ptr = reinterpret_cast<const uint8_t *>(data);
  while (size != 0) {
    const ssize_t n = ::send(fd, ptr, size, MSG_NOSIGNAL);
    OTMPSI_ENFORCE(n > 0, "TCP socket send failed");
    ptr += static_cast<size_t>(n);
    size -= static_cast<size_t>(n);
  }
}

inline void ReadAllFd(int fd, void *data, size_t size) {
  auto *ptr = reinterpret_cast<uint8_t *>(data);
  while (size != 0) {
    const ssize_t n = ::recv(fd, ptr, size, 0);
    OTMPSI_ENFORCE(n > 0, "TCP socket recv failed");
    ptr += static_cast<size_t>(n);
    size -= static_cast<size_t>(n);
  }
}

inline int ListenAndAccept(const std::string &address) {
  const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  OTMPSI_ENFORCE(listen_fd >= 0, "TCP socket creation failed");
  int yes = 1;
  ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  const auto addr = MakeTcpSockAddr(address);
  if (::bind(listen_fd, reinterpret_cast<const sockaddr *>(&addr),
             sizeof(addr)) != 0) {
    CloseFd(listen_fd);
    OTMPSI_THROW("failed to bind TCP socket {}", address);
  }
  if (::listen(listen_fd, 1) != 0) {
    CloseFd(listen_fd);
    OTMPSI_THROW("failed to listen on TCP socket {}", address);
  }
  const int fd = ::accept(listen_fd, nullptr, nullptr);
  CloseFd(listen_fd);
  OTMPSI_ENFORCE(fd >= 0, "TCP socket accept failed");
  return fd;
}

inline int ConnectFdWithRetry(const std::string &address) {
  const auto addr = MakeTcpSockAddr(address);
  constexpr int kMaxAttempts = 1000;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    OTMPSI_ENFORCE(fd >= 0, "TCP socket creation failed");
    if (::connect(fd, reinterpret_cast<const sockaddr *>(&addr),
                  sizeof(addr)) == 0) {
      return fd;
    }
    CloseFd(fd);
    if (attempt + 1 == kMaxAttempts) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  OTMPSI_THROW("failed to connect TCP socket {}", address);
}

class FdCoprotoSocket {
  struct State;

 public:
  explicit FdCoprotoSocket(int fd) : state_(std::make_shared<State>(fd)) {}

  void close() {
    if (state_) {
      std::lock_guard<std::mutex> lock(state_->fd_mu);
      CloseFd(state_->fd);
      state_->fd = -1;
    }
  }

  class Awaiter {
   public:
    Awaiter(std::shared_ptr<State> state, coproto::span<coproto::u8> data,
            bool is_send, macoro::stop_token token)
        : state_(std::move(state)),
          data_(data),
          is_send_(is_send),
          token_(std::move(token)) {}

    bool await_ready() { return false; }

    macoro::coroutine_handle<> await_suspend(macoro::coroutine_handle<> h) {
      Run();
      return h;
    }

#ifdef COPROTO_CPP20
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
      Run();
      return h;
    }
#endif

    std::pair<coproto::error_code, coproto::u64> await_resume() {
      return {ec_, transferred_};
    }

   private:
    void Run() {
      if (token_.stop_requested()) {
        ec_ = coproto::code::operation_aborted;
        return;
      }
      if (!state_) {
        ec_ = coproto::code::closed;
        return;
      }
      try {
        if (is_send_) {
          std::lock_guard<std::mutex> lock(state_->send_mu);
          WriteAllFd(state_->fd, data_.data(), data_.size());
          state_->sent.fetch_add(data_.size());
        } else {
          std::lock_guard<std::mutex> lock(state_->recv_mu);
          ReadAllFd(state_->fd, data_.data(), data_.size());
          state_->recv.fetch_add(data_.size());
        }
        transferred_ = data_.size();
        ec_ = coproto::code::success;
      } catch (...) {
        ec_ = coproto::code::ioError;
      }
    }

    std::shared_ptr<State> state_;
    coproto::span<coproto::u8> data_;
    bool is_send_ = false;
    macoro::stop_token token_;
    coproto::error_code ec_ = coproto::code::success;
    coproto::u64 transferred_ = 0;
  };

  Awaiter send(coproto::span<coproto::u8> data,
               macoro::stop_token token = {}) {
    return Awaiter(state_, data, true, std::move(token));
  }

  Awaiter recv(coproto::span<coproto::u8> data,
               macoro::stop_token token = {}) {
    return Awaiter(state_, data, false, std::move(token));
  }

  size_t bytesSent() const { return state_ ? state_->sent.load() : 0; }
  size_t bytesReceived() const { return state_ ? state_->recv.load() : 0; }

 private:
  struct State {
    explicit State(int fd_in) : fd(fd_in) {}
    ~State() { CloseFd(fd); }
    int fd = -1;
    std::mutex fd_mu;
    std::mutex send_mu;
    std::mutex recv_mu;
    std::atomic<size_t> sent{0};
    std::atomic<size_t> recv{0};
  };

  std::shared_ptr<State> state_;
};

inline coproto::Socket AcquireTcpSocket(const std::string &tag, int rank) {
  OTMPSI_ENFORCE(rank == 0 || rank == 1, "TCP socket rank must be 0 or 1");
  const auto address = TcpAddressForTag(tag);
  if (const char *debug = std::getenv("OTMPSI_DEBUG");
      debug != nullptr && std::string_view(debug) != "0") {
    std::cerr << "[otmpsi][tcp] tag=" << tag << " rank=" << rank
              << " address=" << address << '\n';
  }
  return coproto::asioConnect(address, rank == 0);
}

inline coproto::Socket AcquireLocalSocket(const std::string &tag, int rank) {
  if (UseTcpSockets()) {
    return AcquireTcpSocket(tag, rank);
  }

  static std::mutex mu;
  static std::unordered_map<std::string, LocalSocketEntry> registry;

  OTMPSI_ENFORCE(rank == 0 || rank == 1, "local socket rank must be 0 or 1");

  std::lock_guard<std::mutex> lock(mu);
  auto &entry = registry[tag];
  if (!entry.socks[0].has_value() && !entry.socks[1].has_value()) {
    auto pair = coproto::LocalAsyncSocket::makePair();
    entry.socks[0].emplace(std::move(pair[0]));
    entry.socks[1].emplace(std::move(pair[1]));
  }

  OTMPSI_ENFORCE(entry.socks[rank].has_value(), "socket tag={} rank={} reused",
               tag, rank);
  auto sock = std::move(*entry.socks[rank]);
  entry.socks[rank].reset();
  entry.claimed += 1;
  if (entry.claimed == 2) {
    registry.erase(tag);
  }
  return coproto::Socket(std::move(sock));
}

inline std::string MakeSocketTag(uint16_t namespace_id, size_t party_idx,
                                 size_t channel_idx, size_t shard_idx = 0) {
  return "otmpsi/" + std::to_string(namespace_id) + "/" +
         std::to_string(party_idx) + "/" + std::to_string(channel_idx) + "/" +
         std::to_string(shard_idx);
}

}
