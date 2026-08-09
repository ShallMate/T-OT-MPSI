#include "otmpsi/otmpsi_ports.h"

namespace examples::otmpsi {

std::vector<std::string> ProtocolSocketTags(const OtmpsiParams &params) {
  std::vector<std::string> tags;
  for (size_t party = 1; party < params.parties; ++party) {
    const size_t ssoprf_shards =
        std::min(params.set_size, std::max<size_t>(1, params.threads));
    const auto ssoprf_tag = MakeSocketTag(params.base_port, party, 0);
    if (ssoprf_shards <= 1) {
      tags.push_back(ssoprf_tag);
    } else {
      for (size_t shard = 0; shard < ssoprf_shards; ++shard) {
        tags.push_back(ssoprf_tag + "/shard/" + std::to_string(shard));
      }
    }
    for (size_t channel :
         {1UL, 2UL, 3UL, 4UL, 9UL, 10UL, 11UL, 12UL, 13UL}) {
      tags.push_back(MakeSocketTag(params.base_port, party, channel));
    }
    if (UsesTreeX2aRoute(params)) {
      const size_t output_channel =
          params.final_output_mode == FinalOutputMode::kLeaderOnly ? 16 : 18;
      tags.push_back(MakeSocketTag(params.base_port, party, output_channel));
    }
  }

  size_t pair_id = 0;
  for (size_t i = 0; i < params.parties; ++i) {
    for (size_t j = i + 1; j < params.parties; ++j) {
      tags.push_back(MakeSocketTag(params.base_port, 1000 + pair_id, 7));
      tags.push_back(MakeSocketTag(params.base_port, 1000 + pair_id, 8));
      if (UsesTreeX2aRoute(params)) {
        tags.push_back(MakeSocketTag(params.base_port, 1000 + pair_id, 14));
        if (j == i + 1) {
          tags.push_back(MakeSocketTag(params.base_port, 1000 + pair_id, 15));
        }
        if (params.final_output_mode == FinalOutputMode::kLeaderOnly) {
          tags.push_back(MakeSocketTag(params.base_port, 1000 + pair_id, 17));
        }
      }
      ++pair_id;
    }
  }
  return tags;
}

bool CanBindTcpPort(uint16_t port) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }
  int yes = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
    CloseFd(fd);
    return false;
  }
  const bool ok =
      ::bind(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == 0;
  CloseFd(fd);
  return ok;
}

uint16_t FindAvailableProtocolPort(uint16_t start,
                                   std::vector<uint8_t> &used) {
  OTMPSI_ENFORCE(used.size() == 65536,
               "TCP port usage bitmap must contain 65536 entries");
  uint32_t candidate = start;
  for (size_t attempts = 0; attempts < 32000; ++attempts) {
    if (candidate < 1024 || candidate >= 32000) {
      candidate = 1024;
    }
    const auto port = static_cast<uint16_t>(candidate++);
    if (used[port] != 0) {
      continue;
    }
    if (CanBindTcpPort(port)) {
      used[port] = 1;
      return port;
    }
  }
  OTMPSI_THROW("could not find an available TCP port for OT-MPSI");
}

void GeneratePortMapFiles(const std::string &dir, const OtmpsiParams &params) {
  (void)CompareBitsForPartyCount(params.parties);
  std::filesystem::create_directories(dir);
  uint16_t next_port = params.base_port;
  if (const char *env = std::getenv("OTMPSI_PORT_MAP_BASE")) {
    if (env[0] != '\0') {
      next_port = static_cast<uint16_t>(std::stoul(env));
    }
  }
  if (next_port < 1024 || next_port >= 32000) {
    next_port = 12000;
  }

  std::vector<uint8_t> used(65536, 0);
  std::unordered_map<std::string, uint16_t> canonical_ports;
  for (const auto &tag : ProtocolSocketTags(params)) {
    const auto parts = SplitSocketTag(tag);
    OTMPSI_ENFORCE(parts.size() >= 5, "invalid protocol socket tag {}", tag);
    const auto canonical =
        parts.size() > 5
            ? tag
            : MakeSocketTag(static_cast<uint16_t>(std::stoul(parts[1])),
                            std::stoull(parts[2]), std::stoull(parts[3]),
                            std::stoull(parts[4]));
    auto [it, inserted] = canonical_ports.emplace(canonical, 0);
    if (inserted) {
      it->second = FindAvailableProtocolPort(next_port, used);
      next_port = static_cast<uint16_t>(it->second + 1);
    }
    const auto port = it->second;
    std::ofstream out(std::filesystem::path(dir) / SocketTagPathName(tag));
    OTMPSI_ENFORCE(out.good(), "failed to write port map for {}", tag);
    out << "127.0.0.1:" << port << "\n";
  }
}

} // namespace examples::otmpsi
