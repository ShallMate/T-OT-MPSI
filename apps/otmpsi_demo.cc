#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "otmpsi/otmpsi.h"

namespace {

size_t ParseSize(const std::string &s) {
  size_t pos = 0;
  const auto v = std::stoull(s, &pos, 0);
  if (pos != s.size()) {
    throw std::invalid_argument("invalid integer: " + s);
  }
  return static_cast<size_t>(v);
}

double BytesToMB(size_t bytes) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

double BytesToMB(double bytes) { return bytes / (1024.0 * 1024.0); }

double WireCommMB(size_t lhs_bytes, size_t rhs_bytes) {
  return BytesToMB(
      (static_cast<double>(lhs_bytes) + static_cast<double>(rhs_bytes)) / 2.0);
}

examples::otmpsi::RoleStats
StatAt(const std::vector<examples::otmpsi::RoleStats> &stats, size_t party) {
  if (party >= stats.size()) {
    return {};
  }
  return stats[party];
}

examples::otmpsi::RoleStats
ClientTotal(const examples::otmpsi::OtmpsiStats &stats, size_t party) {
  examples::otmpsi::RoleStats total;
  total += StatAt(stats.client_ssbsmt, party);
  total += StatAt(stats.client_zero, party);
  total += StatAt(stats.client_b2a, party);
  total += StatAt(stats.client_nparty_gmw, party);
  return total;
}

struct ClientSummary {
  double avg_time_s = 0.0;
  size_t sum_comm_bytes = 0;
  size_t min_comm_bytes = 0;
  size_t max_comm_bytes = 0;
  size_t count = 0;
};

ClientSummary
SummarizeClients(const std::vector<examples::otmpsi::RoleStats> &stats,
                 size_t parties) {
  ClientSummary out;
  if (parties <= 1) {
    return out;
  }
  out.count = parties - 1;
  out.min_comm_bytes = std::numeric_limits<size_t>::max();
  double sum_time_s = 0.0;
  for (size_t party = 1; party < parties; ++party) {
    const auto role = StatAt(stats, party);
    sum_time_s += role.time_s;
    out.sum_comm_bytes += role.comm_bytes;
    out.min_comm_bytes = std::min(out.min_comm_bytes, role.comm_bytes);
    out.max_comm_bytes = std::max(out.max_comm_bytes, role.comm_bytes);
  }
  out.avg_time_s = sum_time_s / static_cast<double>(parties - 1);
  return out;
}

ClientSummary SummarizeClientTotals(const examples::otmpsi::OtmpsiStats &stats,
                                    size_t parties) {
  std::vector<examples::otmpsi::RoleStats> totals(parties);
  for (size_t party = 1; party < parties; ++party) {
    totals[party] = ClientTotal(stats, party);
  }
  return SummarizeClients(totals, parties);
}

void PrintPhase(const char *name, const examples::otmpsi::PhaseStats &phase,
                const ClientSummary &clients) {
  std::cout << "stage_" << name << "_time_s=" << phase.time_s << "\n";
  std::cout << "stage_" << name << "_total_comm_mb="
            << WireCommMB(phase.leader.comm_bytes, clients.sum_comm_bytes)
            << "\n";
  std::cout << "stage_" << name
            << "_leader_comm_mb=" << BytesToMB(phase.leader.comm_bytes) << "\n";
  std::cout << "stage_" << name << "_client_avg_comm_mb="
            << BytesToMB(clients.count == 0
                             ? 0.0
                             : static_cast<double>(clients.sum_comm_bytes) /
                                   static_cast<double>(clients.count))
            << "\n";
  std::cout << "stage_" << name
            << "_client_max_comm_mb=" << BytesToMB(clients.max_comm_bytes)
            << "\n";
  std::cout << "stage_" << name
            << "_client_min_comm_mb=" << BytesToMB(clients.min_comm_bytes)
            << "\n";
  std::cout << "stage_" << name
            << "_clients_sum_comm_mb=" << BytesToMB(clients.sum_comm_bytes)
            << "\n";
}

struct CliConfig {
  examples::otmpsi::OtmpsiParams params;
  bool party_mode = false;
  size_t party_id = 0;
  bool generate_port_map = false;
  std::string port_map_dir;
};

CliConfig ParseArgs(int argc, char **argv) {
  CliConfig cfg;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto need_value = [&](const char *name) -> std::string {
      if (i + 1 >= argc) {
        throw std::invalid_argument(std::string("missing value for ") + name);
      }
      return argv[++i];
    };
    if (arg == "--parties" || arg == "-N") {
      cfg.params.parties = ParseSize(need_value(arg.c_str()));
    } else if (arg == "--set-size" || arg == "-m") {
      cfg.params.set_size = ParseSize(need_value(arg.c_str()));
    } else if (arg == "--threshold" || arg == "-t") {
      cfg.params.threshold = ParseSize(need_value(arg.c_str()));
    } else if (arg == "--target-output-size") {
      cfg.params.target_output_size = ParseSize(need_value(arg.c_str()));
    } else if (arg == "--threads") {
      cfg.params.threads = ParseSize(need_value(arg.c_str()));
    } else if (arg == "--base-port") {
      cfg.params.base_port =
          static_cast<uint16_t>(ParseSize(need_value(arg.c_str())));
    } else if (arg == "--party-id") {
      cfg.party_mode = true;
      cfg.party_id = ParseSize(need_value(arg.c_str()));
    } else if (arg == "--generate-port-map") {
      cfg.generate_port_map = true;
      cfg.port_map_dir = need_value(arg.c_str());
    } else if (arg == "--b2a-backend") {
      const auto value = need_value(arg.c_str());
      if (value == "silent_ot") {
        cfg.params.b2a_backend = examples::otmpsi::B2aBackend::kSilentOt;
      } else {
        throw std::invalid_argument("invalid --b2a-backend: " + value);
      }
    } else if (arg == "--final-output") {
      const auto value = need_value(arg.c_str());
      if (value == "leader_only" || value == "leader") {
        cfg.params.final_output_mode =
            examples::otmpsi::FinalOutputMode::kLeaderOnly;
      } else if (value == "client_output_basis" || value == "output_basis" ||
                 value == "client_selected") {
        cfg.params.final_output_mode =
            examples::otmpsi::FinalOutputMode::kClientOutputBasis;
      } else {
        throw std::invalid_argument("invalid --final-output: " + value);
      }
    } else if (arg == "--help" || arg == "-h") {
      std::cout
          << "Usage: otmpsi_demo [--parties N] [--set-size m]"
             " [--threshold t] [--target-output-size s]"
             " [--threads n] [--base-port p]"
             " [--party-id id]"
             " [--generate-port-map dir]"
             " [--b2a-backend silent_ot]"
             " [--final-output leader_only|client_output_basis]\n"
             "Default routes share Tree-X2A + combined Silent-OT + 6-AND "
             "comparison; only final output handling differs.\n"
             "Use --party-id 0..N-1 for one-process-per-party TCP mode.\n";
      std::exit(0);
    } else {
      throw std::invalid_argument("unknown argument: " + arg);
    }
  }

  return cfg;
}

} // namespace

int main(int argc, char **argv) {
  try {
    const auto cfg = ParseArgs(argc, argv);
    const auto &params = cfg.params;
    otmpsi_compat::SetNumThreads(
        static_cast<int>(std::max<size_t>(1, params.threads)));
    if (cfg.generate_port_map) {
      examples::otmpsi::GeneratePortMapFiles(cfg.port_map_dir, params);
      std::cout << "port_map_dir=" << cfg.port_map_dir << "\n";
      std::cout << "parties=" << params.parties << "\n";
      return 0;
    }

    if (cfg.party_mode) {
      setenv("OTMPSI_TRANSPORT", "tcp", 0);
      const auto run =
          examples::otmpsi::RunOnePartyProcessWithStats(cfg.party_id, params);
      const auto &stats = run.stats;
      const bool leader = cfg.party_id == 0;
      const auto total = leader ? stats.leader : stats.clients;
      const auto ssbsmt =
          leader ? stats.ssbsmt.leader : stats.client_ssbsmt[cfg.party_id];
      const auto zero =
          leader ? stats.pairwise_zero.leader : stats.client_zero[cfg.party_id];
      const auto b2a =
          leader ? stats.b2a.leader : stats.client_b2a[cfg.party_id];
      const auto nparty = leader ? stats.nparty_gmw.leader
                                 : stats.client_nparty_gmw[cfg.party_id];
      std::cout << "OT-MPSI party process finished\n";
      std::cout << "party_id=" << cfg.party_id << "\n";
      std::cout << "parties=" << params.parties << "\n";
      std::cout << "set_size=" << params.set_size << "\n";
      std::cout << "threshold=" << params.threshold << "\n";
      if (params.target_output_size.has_value()) {
        std::cout << "target_output_size=" << *params.target_output_size
                  << "\n";
      }
      std::cout << "peqt_bits=" << examples::otmpsi::PeqtBitWidth(params)
                << "\n";
      std::cout << "peqt_triple_batch_size="
                << examples::otmpsi::PeqtTripleBatchSize(params) << "\n";
      std::cout << "b2a=" << "on" << "\n";
      std::cout << "tree_x2a="
                << "on"
                << "\n";
      std::cout << "b2a_backend="
                << examples::otmpsi::B2aBackendName(params.b2a_backend) << "\n";
      std::cout << "final_mask_backend="
                << "arithmetic"
                << "\n";
      std::cout << "final_output_mode="
                << examples::otmpsi::FinalOutputModeName(
                       params.final_output_mode)
                << "\n";
      std::cout << "mask_preprocessing=" << "tree_x2a_z256_ot"
                << "\n";
      std::cout << "process_model=one_process_per_party\n";
      if (const char *transport = std::getenv("OTMPSI_TRANSPORT")) {
        std::cout << "transport=" << transport << "\n";
      }
      if (const char *async_channels =
              std::getenv("OTMPSI_TCP_ASYNC_CHANNELS")) {
        std::cout << "tcp_async_channels=" << async_channels << "\n";
      }
      std::cout << "two_party_schedule="
                << (examples::otmpsi::EnvEnabled("OTMPSI_PIPELINE_2PC")
                        ? "pipelined"
                        : "barrier")
                << "\n";
      std::cout << std::fixed << std::setprecision(6);
      std::cout << "local_total_time_s=" << stats.total_time_s << "\n";
      std::cout << "local_total_comm_mb=" << BytesToMB(total.comm_bytes)
                << "\n";
      std::cout << "stage_ssbsmt_time_s=" << ssbsmt.time_s << "\n";
      std::cout << "stage_ssbsmt_local_comm_mb=" << BytesToMB(ssbsmt.comm_bytes)
                << "\n";
      std::cout << "stage_pairwise_zero_time_s=" << zero.time_s << "\n";
      std::cout << "stage_pairwise_zero_local_comm_mb="
                << BytesToMB(zero.comm_bytes) << "\n";
      std::cout << "stage_b2a_time_s=" << b2a.time_s << "\n";
      std::cout << "stage_b2a_local_comm_mb=" << BytesToMB(b2a.comm_bytes)
                << "\n";
      std::cout << "stage_nparty_gmw_time_s=" << nparty.time_s << "\n";
      std::cout << "stage_nparty_gmw_local_comm_mb="
                << BytesToMB(nparty.comm_bytes) << "\n";
      if (!leader && run.client_output_size.has_value()) {
        std::cout << "output_size=" << *run.client_output_size << "\n";
      }
      if (leader) {
        std::cout << "output_size=" << run.results.size() << "\n";
        for (size_t i = 0; i < std::min<size_t>(run.results.size(), 8); ++i) {
          const auto &r = run.results[i];
          std::cout << "I[" << i << "]=0x" << examples::otmpsi::HexU128(r.item)
                    << ", count=" << r.count << ", holders={";
          for (size_t j = 0; j < r.holders.size(); ++j) {
            if (j != 0) {
              std::cout << ",";
            }
            std::cout << r.holders[j];
          }
          std::cout << "}\n";
        }
      }
      return 0;
    }

    const auto run = examples::otmpsi::RunDemoWithStats(params);
    const auto &results = run.results;
    const auto &stats = run.stats;
    std::cout << "OT-MPSI demo finished\n";
    std::cout << "parties=" << params.parties << "\n";
    std::cout << "set_size=" << params.set_size << "\n";
    std::cout << "threshold=" << params.threshold << "\n";
    if (params.target_output_size.has_value()) {
      std::cout << "target_output_size=" << *params.target_output_size << "\n";
    }
    std::cout << "peqt_bits=" << examples::otmpsi::PeqtBitWidth(params) << "\n";
    std::cout << "peqt_triple_batch_size="
              << examples::otmpsi::PeqtTripleBatchSize(params) << "\n";
    std::cout << "b2a=" << "on" << "\n";
    std::cout << "tree_x2a="
              << "on"
              << "\n";
    std::cout << "b2a_backend="
              << examples::otmpsi::B2aBackendName(params.b2a_backend) << "\n";
    std::cout << "final_mask_backend="
              << "arithmetic"
              << "\n";
    std::cout << "final_output_mode="
              << examples::otmpsi::FinalOutputModeName(params.final_output_mode)
              << "\n";
    std::cout << "mask_preprocessing=" << "tree_x2a_z256_ot" << "\n";
    std::cout << std::fixed << std::setprecision(6);
    const auto client_totals = SummarizeClientTotals(stats, params.parties);
    const auto ssbsmt_clients =
        SummarizeClients(stats.client_ssbsmt, params.parties);
    const auto zero_clients =
        SummarizeClients(stats.client_zero, params.parties);
    const auto b2a_clients = SummarizeClients(stats.client_b2a, params.parties);
    const auto nparty_clients =
        SummarizeClients(stats.client_nparty_gmw, params.parties);
    std::cout << "total_time_s=" << stats.total_time_s << "\n";
    std::cout << "total_comm_mb="
              << WireCommMB(stats.leader.comm_bytes,
                            client_totals.sum_comm_bytes)
              << "\n";
    std::cout << "leader_time_s=" << stats.leader.time_s << "\n";
    std::cout << "leader_comm_mb=" << BytesToMB(stats.leader.comm_bytes)
              << "\n";
    std::cout << "client_avg_time_s=" << client_totals.avg_time_s << "\n";
    std::cout << "client_avg_comm_mb="
              << BytesToMB(
                     client_totals.count == 0
                         ? 0.0
                         : static_cast<double>(client_totals.sum_comm_bytes) /
                               static_cast<double>(client_totals.count))
              << "\n";
    std::cout << "client_max_comm_mb="
              << BytesToMB(client_totals.max_comm_bytes) << "\n";
    std::cout << "client_min_comm_mb="
              << BytesToMB(client_totals.min_comm_bytes) << "\n";
    std::cout << "clients_sum_comm_mb="
              << BytesToMB(client_totals.sum_comm_bytes) << "\n";
    PrintPhase("ssbsmt", stats.ssbsmt, ssbsmt_clients);
    PrintPhase("pairwise_zero", stats.pairwise_zero, zero_clients);
    PrintPhase("b2a", stats.b2a, b2a_clients);
    PrintPhase("nparty_gmw", stats.nparty_gmw, nparty_clients);
    for (size_t party = 1; party < stats.client_ssbsmt.size(); ++party) {
      const auto client_total = ClientTotal(stats, party);
      std::cout << "client_" << party
                << "_total_comm_mb=" << BytesToMB(client_total.comm_bytes)
                << "\n";
      std::cout << "client_" << party << "_total_time_s=" << client_total.time_s
                << "\n";
      std::cout << "client_" << party << "_ssbsmt_comm_mb="
                << BytesToMB(stats.client_ssbsmt[party].comm_bytes) << "\n";
      std::cout << "client_" << party
                << "_ssbsmt_time_s=" << stats.client_ssbsmt[party].time_s
                << "\n";
      if (party < stats.client_zero.size()) {
        std::cout << "client_" << party << "_pairwise_zero_comm_mb="
                  << BytesToMB(stats.client_zero[party].comm_bytes) << "\n";
        std::cout << "client_" << party
                  << "_pairwise_zero_time_s=" << stats.client_zero[party].time_s
                  << "\n";
      }
      if (party < stats.client_b2a.size()) {
        std::cout << "client_" << party << "_b2a_comm_mb="
                  << BytesToMB(stats.client_b2a[party].comm_bytes) << "\n";
        std::cout << "client_" << party
                  << "_b2a_time_s=" << stats.client_b2a[party].time_s << "\n";
      }
      if (party < stats.client_nparty_gmw.size()) {
        std::cout << "client_" << party << "_nparty_gmw_comm_mb="
                  << BytesToMB(stats.client_nparty_gmw[party].comm_bytes)
                  << "\n";
        std::cout << "client_" << party << "_nparty_gmw_time_s="
                  << stats.client_nparty_gmw[party].time_s << "\n";
      }
    }
    std::cout << "output_size=" << results.size() << "\n";
    const size_t preview = std::min<size_t>(results.size(), 8);
    for (size_t i = 0; i < preview; ++i) {
      std::cout << "I[" << i << "]=0x"
                << examples::otmpsi::HexU128(results[i].item)
                << ", count=" << results[i].count << ", holders={";
      for (size_t j = 0; j < results[i].holders.size(); ++j) {
        if (j) {
          std::cout << ",";
        }
        std::cout << results[i].holders[j];
      }
      std::cout << "}\n";
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
