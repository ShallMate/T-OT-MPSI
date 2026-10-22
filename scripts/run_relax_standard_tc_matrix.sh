#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_PATH="${SCRIPT_DIR}/$(basename -- "${BASH_SOURCE[0]}")"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${OTMPSI_BUILD_DIR:-${PROJECT_ROOT}/build}"
BIN="${OTMPSI_BIN:-${BUILD_DIR}/otmpsi_demo}"

SET_POWERS="12,16,20"
PARTIES_LIST="5,10"
MODES="relaxed,standard"
PROFILES="lan,wan200,wan100,wan50"
REPEATS=1
THRESHOLD=2
OUTPUT_RATIOS="default"
THREADS=1
BASE_PORT_START=19350
CASE_TIMEOUT_SECONDS="${OTMPSI_CASE_TIMEOUT_SECONDS:-0}"
LOG_ROOT="${OTMPSI_BENCH_LOG_ROOT:-${PROJECT_ROOT}/bench_results/otmpsi_relax_standard_tc_$(date +%Y%m%d_%H%M%S)}"
SUMMARY_CSV=""
RAW_SUMMARY_CSV=""
USE_UNSHARE="${OTMPSI_USE_UNSHARE:-1}"
SKIP_BUILD=0
SKIP_EXISTING=1
STOP_ON_ERROR=0
DRY_RUN=0
GMW_SIMD="${OTMPSI_GMW_SIMD:-}"

usage() {
  cat <<'EOF'
Usage:
  scripts/run_relax_standard_tc_matrix.sh [options]

Benchmarks OT-MPSI relaxed and standard modes for:
  set sizes: 2^12, 2^16, 2^20
  parties:   5, 10
  networks:  LAN 10Gbps/0.02ms RTT, 200Mbps/40ms RTT,
             100Mbps/40ms RTT, 50Mbps/40ms RTT

Modes used by this script:
  relaxed  -> --final-output client_output_basis
              Default implementation: Tree-X2A + combined Silent-OT + 6-AND
              comparison, then open o_i and reveal only selected rows.
  standard -> --final-output leader_only
              Always uses the optimized Tree-X2A + combined Silent-OT +
              6-AND public-T comparison + private-selector OT route.

Both default modes share the optimized threshold-comparison path for every
threshold, including t=1.
Each party process and all its asynchronous threads are pinned to one CPU.
Distinct physical cores are preferred; excess parties share cores round-robin.

Options:
  --set-powers LIST       comma list, default: 12,16,20
  --parties-list LIST     comma list, default: 5,10
  --modes LIST            comma list: relaxed,standard; relax/std aliases work
  --profiles LIST         comma list: lan,wan200,wan100,wan50
  --repeats N             default: 1
  --threshold N           default: 2
                          also accepts n-1 for a party-dependent threshold
  --output-ratios LIST    exact output ratios, for example: 0,1/4,1
                          default: preserve the demo data distribution
  --threads N             worker threads, default: 1; CPU limit stays one per party
  --base-port N           default: 19350
  --case-timeout-seconds N
                          per-case timeout; 0 disables it (default: 0)
  --peq-triple-batch-size N
                          override PEQT silent-triple batch size
                          default: automatic, 32768 to 262144 by set size
  --gmw-simd MODE         N-party GMW word SIMD: scalar, avx2, or avx512
                          default: current OTMPSI_GMW_SIMD environment
  --log-root DIR          default: bench_results/otmpsi_relax_standard_tc_...
  --summary-csv FILE      compact table, default: LOG_ROOT/summary.csv
  --raw-summary-csv FILE  raw internal table, default: LOG_ROOT/raw_summary.csv
  --skip-build            reuse the existing CMake binary
  --no-unshare            apply tc in the current network namespace
  --rerun-existing        rerun cases even if result_row.csv exists
  --stop-on-error         stop at the first failed case
  --dry-run               print the matrix and exit
  -h, --help              show this help

Output:
  summary.csv is the compact runtime/communication table:
    network,protocol,parties,input_size,time_s,comm_mb
  summary_compare.csv is the compact relaxed-vs-standard table.
  raw_summary.csv keeps the full internal fields for troubleshooting.

Notes:
  tc netem takes one-way delay. The script converts RTT to half-RTT:
    0.02ms RTT -> 10us one-way
    40ms RTT   -> 20ms one-way

  By default it runs each case inside:
    unshare --user --map-root-user --net
  so tc does not need host sudo and does not touch host networking.
EOF
}

log() {
  printf '[otmpsi-bench] %s\n' "$*" >&2
}

die() {
  printf '[otmpsi-bench] ERROR: %s\n' "$*" >&2
  exit 1
}

split_csv_list() {
  local value="$1"
  tr ',' '\n' <<<"${value}" | sed '/^$/d'
}

profile_spec() {
  case "$1" in
    lan)
      printf '%s %s %s\n' "10gbit" "0.02" "LAN 10Gbps 0.02ms RTT"
      ;;
    wan200 | 200mbps | 200m)
      printf '%s %s %s\n' "200mbit" "40" "WAN 200Mbps 40ms RTT"
      ;;
    wan100 | 100mbps | 100m)
      printf '%s %s %s\n' "100mbit" "40" "WAN 100Mbps 40ms RTT"
      ;;
    wan50 | 50mbps | 50m)
      printf '%s %s %s\n' "50mbit" "40" "WAN 50Mbps 40ms RTT"
      ;;
    *)
      die "unknown profile: $1"
      ;;
  esac
}

mode_args_for() {
  case "$1" in
    relaxed | relax)
      printf '%s\n' "--final-output client_output_basis"
      ;;
    standard | std)
      printf '%s\n' "--final-output leader_only"
      ;;
    *)
      die "unknown mode: $1"
      ;;
  esac
}

rtt_to_one_way_delay() {
  awk -v rtt_ms="$1" 'BEGIN {
    one_way_ms = rtt_ms / 2.0;
    if (one_way_ms < 1.0) {
      printf "%.0fus", one_way_ms * 1000.0;
    } else if (one_way_ms == int(one_way_ms)) {
      printf "%.0fms", one_way_ms;
    } else {
      printf "%.3fms", one_way_ms;
    }
  }'
}

pow2() {
  local p="$1"
  printf '%d\n' "$((1 << p))"
}

threshold_for_parties() {
  local parties="$1"
  case "${THRESHOLD}" in
    n-1 | N-1)
      printf '%d\n' "$((parties - 1))"
      ;;
    *)
      [[ "${THRESHOLD}" =~ ^[0-9]+$ ]] || die "invalid threshold: ${THRESHOLD}"
      printf '%s\n' "${THRESHOLD}"
      ;;
  esac
}

target_output_size_for_ratio() {
  local ratio="$1"
  local set_size="$2"
  if [[ "${ratio}" == "default" ]]; then
    printf '\n'
    return
  fi

  local numerator denominator
  if [[ "${ratio}" =~ ^([0-9]+)/([0-9]+)$ ]]; then
    numerator="${BASH_REMATCH[1]}"
    denominator="${BASH_REMATCH[2]}"
  elif [[ "${ratio}" =~ ^[0-9]+$ ]]; then
    numerator="${ratio}"
    denominator=1
  else
    die "invalid output ratio: ${ratio}"
  fi
  ((denominator > 0)) || die "output ratio denominator must be positive: ${ratio}"
  ((numerator <= denominator)) || die "output ratio must be in [0,1]: ${ratio}"
  local scaled=$((set_size * numerator))
  ((scaled % denominator == 0)) ||
    die "output ratio ${ratio} is not exact for set size ${set_size}"
  printf '%d\n' "$((scaled / denominator))"
}

ratio_path_component() {
  local ratio="$1"
  ratio="${ratio//\//_of_}"
  printf '%s\n' "${ratio}"
}

csv_escape() {
  local value="${1:-}"
  value="${value//\"/\"\"}"
  printf '"%s"' "${value}"
}

csv_join_row() {
  local first=1
  local field
  for field in "$@"; do
    if ((first)); then
      first=0
    else
      printf ','
    fi
    csv_escape "${field}"
  done
  printf '\n'
}

write_summary_header() {
  local csv="$1"
  mkdir -p "$(dirname -- "${csv}")"
  csv_join_row \
    timestamp profile profile_label rate rtt_ms one_way_delay mode mode_args \
    parties set_power set_size threshold threads repeat base_port status \
    wall_time_s aggregate_max_time_s aggregate_wire_total_comm_mb \
    aggregate_leader_comm_mb aggregate_client_avg_comm_mb \
    aggregate_clients_sum_comm_mb aggregate_stage_ssbsmt_wire_comm_mb \
    aggregate_stage_pairwise_zero_wire_comm_mb \
    aggregate_stage_b2a_wire_comm_mb aggregate_stage_nparty_gmw_wire_comm_mb \
    aggregate_stage_ssbsmt_max_time_s \
    aggregate_stage_pairwise_zero_max_time_s \
    aggregate_stage_b2a_max_time_s aggregate_stage_peqt_b2a_max_time_s \
    aggregate_stage_nparty_gmw_max_time_s \
    output_ratio target_output_size output_size log_dir >"${csv}"
}

extract_kv() {
  local file="$1"
  local key="$2"
  awk -F= -v key="${key}" '$1 == key { print $2; exit }' "${file}" 2>/dev/null || true
}

result_row_status() {
  local row_file="$1"
  awk -F, '{ value=$16; gsub(/^"|"$/, "", value); print value; exit }' \
    "${row_file}" 2>/dev/null || true
}

build_demo() {
  if ((SKIP_BUILD)); then
    [[ -x "${BIN}" ]] || die "--skip-build was set but binary is missing: ${BIN}"
    return
  fi

  log "configuring and building CMake target otmpsi_demo"
  cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release -DOTMPSI_BUILD_TESTS=OFF
  cmake --build "${BUILD_DIR}" --target otmpsi_demo \
    -j "${OTMPSI_BUILD_JOBS:-8}"
}

format_protocol_tables() {
  if command -v python3 >/dev/null 2>&1; then
    local output_dir
    output_dir="$(dirname -- "${SUMMARY_CSV}")"
    if python3 "${SCRIPT_DIR}/format_protocol_runtime_comm_tables.py" \
      "${RAW_SUMMARY_CSV}" --out-dir "${output_dir}" >/dev/null; then
      :
    else
      log "failed to format runtime/communication tables"
    fi
  else
    log "python3 not found; skipped runtime/communication table formatting"
  fi
}

apply_tc_profile() {
  local rate="$1"
  local one_way_delay="$2"
  local case_dir="$3"
  local queue_limit="${OTMPSI_NETEM_LIMIT_PACKETS:-100000}"

  ip link set lo up
  tc qdisc del dev lo root >/dev/null 2>&1 || true
  tc qdisc replace dev lo root netem limit "${queue_limit}" \
    delay "${one_way_delay}" rate "${rate}"
  tc -s qdisc show dev lo >"${case_dir}/tc_before.log" 2>&1 || true
}

cleanup_tc_profile() {
  tc qdisc del dev lo root >/dev/null 2>&1 || true
}

aggregate_party_logs() {
  local case_dir="$1"
  awk -F= '
    /party_id=0/ { leader_file[FILENAME] = 1 }
    /local_total_time_s=/ {
      if ($2 > max_time) max_time = $2
    }
    /local_total_comm_mb=/ {
      sum_comm += $2
      if (leader_file[FILENAME]) leader_comm = $2
      else { client_sum += $2; client_count += 1 }
    }
    /stage_ssbsmt_local_comm_mb=/ { ssbsmt += $2 }
    /stage_pairwise_zero_local_comm_mb=/ { zero += $2 }
    /stage_b2a_local_comm_mb=/ { b2a += $2 }
    /stage_nparty_gmw_local_comm_mb=/ { gmw += $2 }
    $1 == "stage_ssbsmt_time_s" {
      if ($2 > ssbsmt_time) ssbsmt_time = $2
    }
    $1 == "stage_pairwise_zero_time_s" {
      zero_time_by_party[FILENAME] = $2
      stage_party[FILENAME] = 1
      if ($2 > zero_time) zero_time = $2
    }
    $1 == "stage_b2a_time_s" {
      b2a_time_by_party[FILENAME] = $2
      stage_party[FILENAME] = 1
      if ($2 > b2a_time) b2a_time = $2
    }
    $1 == "stage_nparty_gmw_time_s" {
      if ($2 > gmw_time) gmw_time = $2
    }
    END {
      for (party_file in stage_party) {
        peqt_b2a_time = zero_time_by_party[party_file] + b2a_time_by_party[party_file]
        if (peqt_b2a_time > peqt_b2a_max_time) {
          peqt_b2a_max_time = peqt_b2a_time
        }
      }
      printf("aggregate_max_time_s=%.6f\n", max_time)
      printf("aggregate_wire_total_comm_mb=%.6f\n", sum_comm / 2.0)
      printf("aggregate_leader_comm_mb=%.6f\n", leader_comm)
      if (client_count > 0) {
        printf("aggregate_client_avg_comm_mb=%.6f\n", client_sum / client_count)
        printf("aggregate_clients_sum_comm_mb=%.6f\n", client_sum)
      } else {
        printf("aggregate_client_avg_comm_mb=0.000000\n")
        printf("aggregate_clients_sum_comm_mb=0.000000\n")
      }
      printf("aggregate_stage_ssbsmt_wire_comm_mb=%.6f\n", ssbsmt / 2.0)
      printf("aggregate_stage_pairwise_zero_wire_comm_mb=%.6f\n", zero / 2.0)
      printf("aggregate_stage_b2a_wire_comm_mb=%.6f\n", b2a / 2.0)
      printf("aggregate_stage_nparty_gmw_wire_comm_mb=%.6f\n", gmw / 2.0)
      printf("aggregate_stage_ssbsmt_max_time_s=%.6f\n", ssbsmt_time)
      printf("aggregate_stage_pairwise_zero_max_time_s=%.6f\n", zero_time)
      printf("aggregate_stage_b2a_max_time_s=%.6f\n", b2a_time)
      printf("aggregate_stage_peqt_b2a_max_time_s=%.6f\n", peqt_b2a_max_time)
      printf("aggregate_stage_nparty_gmw_max_time_s=%.6f\n", gmw_time)
    }
  ' "${case_dir}"/party_*.log >"${case_dir}/aggregate.log"
}

write_result_row() {
  local row_file="$1"
  local status="$2"
  local timestamp="$3"
  local profile="$4"
  local profile_label="$5"
  local rate="$6"
  local rtt_ms="$7"
  local one_way_delay="$8"
  local mode="$9"
  local mode_args="${10}"
  local parties="${11}"
  local set_power="${12}"
  local set_size="${13}"
  local threshold="${14}"
  local output_ratio="${15}"
  local target_output_size="${16}"
  local threads="${17}"
  local repeat="${18}"
  local base_port="${19}"
  local wall_time_s="${20}"
  local case_dir="${21}"

  local aggregate_file="${case_dir}/aggregate.log"
  local leader_log="${case_dir}/party_0.log"
  local output_size=""
  local aggregate_max_time_s=""
  local aggregate_wire_total_comm_mb=""
  local aggregate_leader_comm_mb=""
  local aggregate_client_avg_comm_mb=""
  local aggregate_clients_sum_comm_mb=""
  local aggregate_stage_ssbsmt_wire_comm_mb=""
  local aggregate_stage_pairwise_zero_wire_comm_mb=""
  local aggregate_stage_b2a_wire_comm_mb=""
  local aggregate_stage_nparty_gmw_wire_comm_mb=""
  local aggregate_stage_ssbsmt_max_time_s=""
  local aggregate_stage_pairwise_zero_max_time_s=""
  local aggregate_stage_b2a_max_time_s=""
  local aggregate_stage_peqt_b2a_max_time_s=""
  local aggregate_stage_nparty_gmw_max_time_s=""

  if [[ -f "${aggregate_file}" ]]; then
    aggregate_max_time_s="$(extract_kv "${aggregate_file}" aggregate_max_time_s)"
    aggregate_wire_total_comm_mb="$(extract_kv "${aggregate_file}" aggregate_wire_total_comm_mb)"
    aggregate_leader_comm_mb="$(extract_kv "${aggregate_file}" aggregate_leader_comm_mb)"
    aggregate_client_avg_comm_mb="$(extract_kv "${aggregate_file}" aggregate_client_avg_comm_mb)"
    aggregate_clients_sum_comm_mb="$(extract_kv "${aggregate_file}" aggregate_clients_sum_comm_mb)"
    aggregate_stage_ssbsmt_wire_comm_mb="$(extract_kv "${aggregate_file}" aggregate_stage_ssbsmt_wire_comm_mb)"
    aggregate_stage_pairwise_zero_wire_comm_mb="$(extract_kv "${aggregate_file}" aggregate_stage_pairwise_zero_wire_comm_mb)"
    aggregate_stage_b2a_wire_comm_mb="$(extract_kv "${aggregate_file}" aggregate_stage_b2a_wire_comm_mb)"
    aggregate_stage_nparty_gmw_wire_comm_mb="$(extract_kv "${aggregate_file}" aggregate_stage_nparty_gmw_wire_comm_mb)"
    aggregate_stage_ssbsmt_max_time_s="$(extract_kv "${aggregate_file}" aggregate_stage_ssbsmt_max_time_s)"
    aggregate_stage_pairwise_zero_max_time_s="$(extract_kv "${aggregate_file}" aggregate_stage_pairwise_zero_max_time_s)"
    aggregate_stage_b2a_max_time_s="$(extract_kv "${aggregate_file}" aggregate_stage_b2a_max_time_s)"
    aggregate_stage_peqt_b2a_max_time_s="$(extract_kv "${aggregate_file}" aggregate_stage_peqt_b2a_max_time_s)"
    aggregate_stage_nparty_gmw_max_time_s="$(extract_kv "${aggregate_file}" aggregate_stage_nparty_gmw_max_time_s)"
  fi
  if [[ -f "${leader_log}" ]]; then
    output_size="$(extract_kv "${leader_log}" output_size)"
  fi

  csv_join_row \
    "${timestamp}" "${profile}" "${profile_label}" "${rate}" "${rtt_ms}" \
    "${one_way_delay}" "${mode}" "${mode_args}" "${parties}" "${set_power}" \
    "${set_size}" "${threshold}" "${threads}" "${repeat}" "${base_port}" \
    "${status}" "${wall_time_s}" "${aggregate_max_time_s}" \
    "${aggregate_wire_total_comm_mb}" "${aggregate_leader_comm_mb}" \
    "${aggregate_client_avg_comm_mb}" "${aggregate_clients_sum_comm_mb}" \
    "${aggregate_stage_ssbsmt_wire_comm_mb}" \
    "${aggregate_stage_pairwise_zero_wire_comm_mb}" \
    "${aggregate_stage_b2a_wire_comm_mb}" \
    "${aggregate_stage_nparty_gmw_wire_comm_mb}" \
    "${aggregate_stage_ssbsmt_max_time_s}" \
    "${aggregate_stage_pairwise_zero_max_time_s}" \
    "${aggregate_stage_b2a_max_time_s}" \
    "${aggregate_stage_peqt_b2a_max_time_s}" \
    "${aggregate_stage_nparty_gmw_max_time_s}" "${output_ratio}" \
    "${target_output_size}" "${output_size}" \
    "${case_dir}" >"${row_file}"
}

run_case_payload() {
  local profile="$1"
  local profile_label="$2"
  local rate="$3"
  local rtt_ms="$4"
  local one_way_delay="$5"
  local mode="$6"
  local parties="$7"
  local set_power="$8"
  local set_size="$9"
  local threshold="${10}"
  local output_ratio="${11}"
  local target_output_size="${12}"
  local threads="${13}"
  local repeat="${14}"
  local base_port="${15}"
  local case_dir="${16}"
  local row_file="${17}"

  mkdir -p "${case_dir}"
  cd "${PROJECT_ROOT}"
  rm -f "${case_dir}/cpu_affinity.confirmed"

  command -v taskset >/dev/null 2>&1 || die "taskset not found (util-linux is required)"
  local cpu_list
  cpu_list="$(python3 "${SCRIPT_DIR}/party_cpu_affinity.py" "${parties}")" || return 1
  local -a party_cpus
  mapfile -t party_cpus <<<"${cpu_list}"

  local mode_args_string
  mode_args_string="$(mode_args_for "${mode}")"
  read -r -a mode_args <<<"${mode_args_string}"
  local target_output_args=()
  if [[ -n "${target_output_size}" ]]; then
    target_output_args=(--target-output-size "${target_output_size}")
  fi

  local timestamp
  timestamp="$(date -Is)"
  local start_ns
  start_ns="$(date +%s%N)"
  local status="success"

  {
    log "tc ${profile}: rate=${rate}, rtt=${rtt_ms}ms, one-way=${one_way_delay}, queue_limit=${OTMPSI_NETEM_LIMIT_PACKETS:-100000}"
    apply_tc_profile "${rate}" "${one_way_delay}" "${case_dir}"

    export OTMPSI_TRANSPORT="${OTMPSI_TRANSPORT:-tcp}"
    export OTMPSI_PIPELINE_2PC="${OTMPSI_PIPELINE_2PC:-1}"
    export OMP_NUM_THREADS="${threads}"
    export OPENBLAS_NUM_THREADS=1
    unset GOMP_CPU_AFFINITY KMP_AFFINITY OMP_PLACES
    export OMP_PROC_BIND=false
    export OTMPSI_LOG_ROOT="${case_dir}"
    log "PEQT triple batch size=${OTMPSI_PEQ_TRIPLE_BATCH_SIZE:-auto}"

    local port_map_dir="${case_dir}/port_map"
    mkdir -p "${port_map_dir}"
    "${BIN}" \
      --parties "${parties}" \
      --set-size "${set_size}" \
      --threshold "${threshold}" \
      "${target_output_args[@]}" \
      --threads "${threads}" \
      --base-port "${base_port}" \
      "${mode_args[@]}" \
      --generate-port-map "${port_map_dir}" >"${case_dir}/port_map.log" 2>&1
    export OTMPSI_PORT_MAP_DIR="${port_map_dir}"

    local pids=()
    local party
    printf 'party_id\tcpu_id\n' >"${case_dir}/cpu_affinity.tsv"
    for ((party = 0; party < parties; ++party)); do
      printf '%s\t%s\n' "${party}" "${party_cpus[party]}" >>"${case_dir}/cpu_affinity.tsv"
      log "party=${party} cpu=${party_cpus[party]} (all threads pinned)"
      (
        set +e
        local party_pid=""
        trap '[[ -z "${party_pid}" ]] || kill -TERM "${party_pid}" 2>/dev/null || true' \
          TERM INT
        taskset --cpu-list "${party_cpus[party]}" "${BIN}" \
          --parties "${parties}" \
          --set-size "${set_size}" \
          --threshold "${threshold}" \
          "${target_output_args[@]}" \
          --threads "${threads}" \
          --base-port "${base_port}" \
          "${mode_args[@]}" \
          --party-id "${party}" >"${case_dir}/party_${party}.log" 2>&1 &
        party_pid=$!
        printf '%s\n' "${party_pid}" >"${case_dir}/party_${party}.pid"
        wait "${party_pid}"
        local party_rc=$?
        trap - TERM INT
        printf '%s\n' "${party_rc}" >"${case_dir}/party_${party}.exit_code"
        exit "${party_rc}"
      ) &
      pids+=("$!")
    done

    local wait_status=0
    local pid
    local remaining_count=${#pids[@]}
    while ((remaining_count > 0)); do
      if wait -n; then
        remaining_count=$((remaining_count - 1))
      else
        wait_status=1
        # One failed party makes progress impossible. Stop the peers now so
        # the outer case timeout records the real failure without a long wait.
        kill -TERM "${pids[@]}" 2>/dev/null || true
        for pid in "${pids[@]}"; do
          wait "${pid}" 2>/dev/null || true
        done
        break
      fi
    done
    if ((wait_status != 0)); then
      for ((party = 0; party < parties; ++party)); do
        {
          printf '===== party %d tail =====\n' "${party}"
          tail -n 120 "${case_dir}/party_${party}.log" || true
        } >>"${case_dir}/failure_tail.log"
      done
    else
      aggregate_party_logs "${case_dir}"
      if [[ -n "${target_output_size}" ]]; then
        local actual_output_size
        actual_output_size="$(extract_kv "${case_dir}/party_0.log" output_size)"
        if [[ "${actual_output_size}" != "${target_output_size}" ]]; then
          printf 'target_output_size=%s actual_output_size=%s\n' \
            "${target_output_size}" "${actual_output_size}" \
            >"${case_dir}/output_size_mismatch.log"
          wait_status=1
        fi
      fi
    fi
    tc -s qdisc show dev lo >"${case_dir}/tc_after.log" 2>&1 || true
    ((wait_status == 0))
  } >"${case_dir}/runner.log" 2>&1 || status="failed"

  local end_ns
  end_ns="$(date +%s%N)"
  local wall_time_s
  wall_time_s="$(awk -v s="${start_ns}" -v e="${end_ns}" 'BEGIN { printf "%.6f", (e - s) / 1000000000.0 }')"

  cleanup_tc_profile
  write_result_row \
    "${row_file}" "${status}" "${timestamp}" "${profile}" "${profile_label}" \
    "${rate}" "${rtt_ms}" "${one_way_delay}" "${mode}" "${mode_args_string}" \
    "${parties}" "${set_power}" "${set_size}" "${threshold}" \
    "${output_ratio}" "${target_output_size}" "${threads}" "${repeat}" \
    "${base_port}" "${wall_time_s}" "${case_dir}"

  if [[ "${status}" == "success" ]]; then
    printf 'one_cpu_per_party\n' >"${case_dir}/cpu_affinity.confirmed"
  fi
  [[ "${status}" == "success" ]]
}

run_case_inner() {
  run_case_payload "$@"
}

if [[ "${1:-}" == "__run_case_inner" ]]; then
  shift
  run_case_inner "$@"
  exit $?
fi

while (($#)); do
  case "$1" in
    --set-powers)
      SET_POWERS="$2"
      shift 2
      ;;
    --parties-list)
      PARTIES_LIST="$2"
      shift 2
      ;;
    --modes)
      MODES="$2"
      shift 2
      ;;
    --profiles)
      PROFILES="$2"
      shift 2
      ;;
    --repeats)
      REPEATS="$2"
      shift 2
      ;;
    --threshold)
      THRESHOLD="$2"
      shift 2
      ;;
    --output-ratios)
      OUTPUT_RATIOS="$2"
      shift 2
      ;;
    --threads)
      THREADS="$2"
      shift 2
      ;;
    --base-port)
      BASE_PORT_START="$2"
      shift 2
      ;;
    --case-timeout-seconds)
      CASE_TIMEOUT_SECONDS="$2"
      shift 2
      ;;
    --peq-triple-batch-size)
      export OTMPSI_PEQ_TRIPLE_BATCH_SIZE="$2"
      shift 2
      ;;
    --gmw-simd)
      [[ $# -ge 2 ]] || die "missing value for --gmw-simd"
      case "$2" in
        scalar | avx2 | avx512)
          GMW_SIMD="$2"
          export OTMPSI_GMW_SIMD="${GMW_SIMD}"
          ;;
        *)
          die "unknown --gmw-simd mode: $2"
          ;;
      esac
      shift 2
      ;;
    --log-root)
      LOG_ROOT="$2"
      shift 2
      ;;
    --summary-csv)
      SUMMARY_CSV="$2"
      shift 2
      ;;
    --raw-summary-csv)
      RAW_SUMMARY_CSV="$2"
      shift 2
      ;;
    --skip-build)
      SKIP_BUILD=1
      shift
      ;;
    --no-unshare)
      USE_UNSHARE=0
      shift
      ;;
    --rerun-existing)
      SKIP_EXISTING=0
      shift
      ;;
    --stop-on-error)
      STOP_ON_ERROR=1
      shift
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

SUMMARY_CSV="${SUMMARY_CSV:-${LOG_ROOT}/summary.csv}"
RAW_SUMMARY_CSV="${RAW_SUMMARY_CSV:-${LOG_ROOT}/raw_summary.csv}"

if ((USE_UNSHARE)); then
  command -v unshare >/dev/null 2>&1 || die "unshare not found; use --no-unshare with root privileges"
else
  [[ "${EUID}" -eq 0 ]] || die "--no-unshare requires root because tc will touch the current network namespace"
fi
command -v ip >/dev/null 2>&1 || die "ip command not found"
command -v tc >/dev/null 2>&1 || die "tc command not found"
command -v taskset >/dev/null 2>&1 || die "taskset not found (util-linux is required)"
command -v python3 >/dev/null 2>&1 || die "python3 not found"
[[ "${CASE_TIMEOUT_SECONDS}" =~ ^[0-9]+$ ]] ||
  die "--case-timeout-seconds must be a non-negative integer"
if ((CASE_TIMEOUT_SECONDS > 0)); then
  command -v timeout >/dev/null 2>&1 || die "timeout command not found"
fi

if [[ -n "${GMW_SIMD}" ]]; then
  log "gmw_simd=${GMW_SIMD}"
fi

if ((DRY_RUN)); then
  log "cpu_binding=one_cpu_per_party; excess parties share physical cores round-robin"
  log "dry run matrix:"
  for profile in $(split_csv_list "${PROFILES}"); do
    read -r rate rtt_ms profile_label <<<"$(profile_spec "${profile}")"
    one_way_delay="$(rtt_to_one_way_delay "${rtt_ms}")"
    for parties in $(split_csv_list "${PARTIES_LIST}"); do
      threshold="$(threshold_for_parties "${parties}")"
      for set_power in $(split_csv_list "${SET_POWERS}"); do
        set_size="$(pow2 "${set_power}")"
        for output_ratio in $(split_csv_list "${OUTPUT_RATIOS}"); do
          target_output_size="$(target_output_size_for_ratio "${output_ratio}" "${set_size}")"
          for mode in $(split_csv_list "${MODES}"); do
            mode_args="$(mode_args_for "${mode}")"
            for ((repeat = 1; repeat <= REPEATS; ++repeat)); do
              printf '%s mode=%s parties=%s set=2^%s threshold=%s output_ratio=%s target_output_size=%s rate=%s rtt_ms=%s one_way=%s repeat=%s timeout_s=%s gmw_simd=%s args=%s\n' \
                "${profile_label}" "${mode}" "${parties}" "${set_power}" \
                "${threshold}" "${output_ratio}" "${target_output_size:-default}" \
                "${rate}" "${rtt_ms}" "${one_way_delay}" "${repeat}" \
                "${CASE_TIMEOUT_SECONDS}" "${GMW_SIMD:-default}" "${mode_args}"
            done
          done
        done
      done
    done
  done
  exit 0
fi

mkdir -p "${LOG_ROOT}"
write_summary_header "${RAW_SUMMARY_CSV}"
format_protocol_tables
build_demo

case_index=0
failures=0

for profile in $(split_csv_list "${PROFILES}"); do
  read -r rate rtt_ms profile_label <<<"$(profile_spec "${profile}")"
  one_way_delay="$(rtt_to_one_way_delay "${rtt_ms}")"
  for parties in $(split_csv_list "${PARTIES_LIST}"); do
    threshold="$(threshold_for_parties "${parties}")"
    for set_power in $(split_csv_list "${SET_POWERS}"); do
      set_size="$(pow2 "${set_power}")"
      for output_ratio in $(split_csv_list "${OUTPUT_RATIOS}"); do
        target_output_size="$(target_output_size_for_ratio "${output_ratio}" "${set_size}")"
        ratio_path="$(ratio_path_component "${output_ratio}")"
        for mode in $(split_csv_list "${MODES}"); do
          mode_args="$(mode_args_for "${mode}")"
          for ((repeat = 1; repeat <= REPEATS; ++repeat)); do
            case_dir="${LOG_ROOT}/${profile}/${mode}/N${parties}/m2p${set_power}"
            if [[ "${output_ratio}" != "default" ]]; then
              case_dir+="/output_${ratio_path}"
            fi
            case_dir+="/repeat_${repeat}"
            row_file="${case_dir}/result_row.csv"
            base_port=$((BASE_PORT_START + (case_index % 1000) * 20))
            case_index=$((case_index + 1))

            if ((SKIP_EXISTING)) && [[ -s "${row_file}" ]]; then
              existing_status="$(result_row_status "${row_file}")"
              if [[ "${existing_status}" == "success" && -s "${case_dir}/cpu_affinity.tsv" && -s "${case_dir}/cpu_affinity.confirmed" ]]; then
                log "skip existing: profile=${profile} mode=${mode} N=${parties} m=2^${set_power} output_ratio=${output_ratio} repeat=${repeat}"
                cat "${row_file}" >>"${RAW_SUMMARY_CSV}"
                format_protocol_tables
                continue
              fi
              log "rerun case: status=${existing_status:-unknown}, missing single-CPU binding record or previous failure; profile=${profile} mode=${mode} N=${parties} m=2^${set_power} output_ratio=${output_ratio} repeat=${repeat}"
            fi

            log "run profile=${profile} mode=${mode} N=${parties} m=2^${set_power} threshold=${threshold} output_ratio=${output_ratio} target_output_size=${target_output_size:-default} repeat=${repeat}"
            inner_command=(
              "${SCRIPT_PATH}" __run_case_inner
              "${profile}" "${profile_label}" "${rate}" "${rtt_ms}"
              "${one_way_delay}" "${mode}" "${parties}" "${set_power}"
              "${set_size}" "${threshold}" "${output_ratio}"
              "${target_output_size}" "${THREADS}" "${repeat}"
              "${base_port}" "${case_dir}" "${row_file}"
            )
            if ((USE_UNSHARE)); then
              case_command=(unshare --user --map-root-user --net "${inner_command[@]}")
            else
              case_command=("${inner_command[@]}")
            fi
            if ((CASE_TIMEOUT_SECONDS > 0)); then
              case_command=(timeout --signal=TERM --kill-after=10s \
                "${CASE_TIMEOUT_SECONDS}s" "${case_command[@]}")
            fi

            case_rc=0
            if "${case_command[@]}"; then
              :
            else
              case_rc=$?
            fi
            if ((USE_UNSHARE == 0)); then
              cleanup_tc_profile
            fi

            if ((case_rc != 0)) && [[ ! -s "${row_file}" ]]; then
              mkdir -p "${case_dir}"
              failure_status="failed"
              if ((case_rc == 124 || case_rc == 137)); then
                failure_status="timeout"
              fi
              printf 'status=%s\nexit_code=%s\ntimeout_seconds=%s\n' \
                "${failure_status}" "${case_rc}" "${CASE_TIMEOUT_SECONDS}" \
                >"${case_dir}/failure_status.log"
              write_result_row \
                "${row_file}" "${failure_status}" "$(date -Is)" \
                "${profile}" "${profile_label}" "${rate}" "${rtt_ms}" \
                "${one_way_delay}" "${mode}" "${mode_args}" "${parties}" \
                "${set_power}" "${set_size}" "${threshold}" "${output_ratio}" \
                "${target_output_size}" "${THREADS}" "${repeat}" "${base_port}" \
                "${CASE_TIMEOUT_SECONDS}" "${case_dir}"
            fi

            [[ -s "${row_file}" ]] && cat "${row_file}" >>"${RAW_SUMMARY_CSV}"
            format_protocol_tables
            if ((case_rc != 0)); then
              failures=$((failures + 1))
              log "failed exit=${case_rc}; logs=${case_dir}"
              if ((STOP_ON_ERROR)); then
                exit 1
              fi
            fi
          done
        done
      done
    done
  done
done

format_protocol_tables
log "summary_csv=${SUMMARY_CSV}"
log "summary_compare_csv=$(dirname -- "${SUMMARY_CSV}")/summary_compare.csv"
log "raw_summary_csv=${RAW_SUMMARY_CSV}"
log "log_root=${LOG_ROOT}"
if ((failures > 0)); then
  log "completed with ${failures} failed case(s)"
  exit 1
fi
log "completed successfully"
