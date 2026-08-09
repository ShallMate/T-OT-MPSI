#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RUNNER="${SCRIPT_DIR}/run_relax_standard_tc_matrix.sh"

THRESHOLDS="1,3,5,7,9"
SET_POWER=14
PROFILES="lan,wan200,wan100,wan50"
MODES="relaxed,standard"
REPEATS=1
THREADS=1
BASE_PORT=19350
LOG_ROOT="${OTMPSI_N10_THRESHOLD_SWEEP_LOG_ROOT:-${SCRIPT_DIR}/bench_results/otmpsi_n10_threshold_sweep_2p14_tc_$(date +%Y%m%d_%H%M%S)}"
PEQ_TRIPLE_BATCH_SIZE="${OTMPSI_PEQ_TRIPLE_BATCH_SIZE:-}"
GMW_SIMD="${OTMPSI_GMW_SIMD:-}"
SKIP_BUILD=0
RERUN_EXISTING=0
STOP_ON_ERROR=0
DRY_RUN=0
NO_UNSHARE=0
PAPER_FAITHFUL=0
ENABLE_T2_FAST_PATH=0

usage() {
  cat <<'EOF'
Usage:
  scripts/run_n10_threshold_sweep_tc_matrix.sh [options]

Runs the OT-MPSI threshold-sweep benchmark matrix:
  parties:     N=10
  thresholds:  t=1,3,5,7,9
  input size:  2^14
  networks:    LAN 10Gbps/0.02ms RTT, 200Mbps/40ms RTT,
               100Mbps/40ms RTT, 50Mbps/40ms RTT
  modes:       relaxed, standard
               both modes share the optimized Tree/Silent-OT comparison route

Outputs:
  LOG_ROOT/summary_threshold.csv          compact table with threshold column
  LOG_ROOT/summary_threshold_compare.csv  relaxed-vs-standard table
  LOG_ROOT/t1/, LOG_ROOT/t3/, ...         per-threshold runner outputs

Options:
  --thresholds LIST      default: 1,3,5,7,9
  --set-power N          default: 14
  --profiles LIST        default: lan,wan200,wan100,wan50
  --modes LIST           default: relaxed,standard
  --repeats N            default: 1
  --threads N            default: 1
  --base-port N          default: 19350
  --peq-triple-batch-size N
                         override PEQT silent-triple batch size
  --gmw-simd MODE        N-party GMW word SIMD: scalar, avx2, or avx512
  --log-root DIR         default: bench_results/otmpsi_n10_threshold_sweep_2p14_tc_...
  --skip-build           reuse the existing CMake binary
  --rerun-existing       rerun cases even if result_row.csv exists
  --stop-on-error        stop at the first failed case
  --no-unshare           apply tc in the current network namespace
  --dry-run              print the matrix and exit
  --paper-faithful       use the arithmetic paper route for relaxed mode only
  --enable-t2-fast-path  enable the relaxed threshold=2 output fast path only
  -h, --help             show this help
EOF
}

die() {
  printf '[otmpsi-threshold-sweep] ERROR: %s\n' "$*" >&2
  exit 1
}

log() {
  printf '[otmpsi-threshold-sweep] %s\n' "$*" >&2
}

split_csv_list() {
  local value="$1"
  tr ',' '\n' <<<"${value}" | sed '/^$/d'
}

append_csv_with_threshold() {
  local threshold="$1"
  local src="$2"
  local dst="$3"
  [[ -s "${src}" ]] || return 0
  awk -v threshold="${threshold}" 'NR > 1 { sub(/\r$/, ""); print threshold "," $0 }' \
    "${src}" >>"${dst}"
}

write_summary_headers() {
  printf 'threshold,network,protocol,parties,input_size,time_s,comm_mb\n' \
    >"${LOG_ROOT}/summary_threshold.csv"
  printf 'threshold,network,parties,input_size,relaxed_time_s,standard_time_s,relaxed_comm_mb,standard_comm_mb\n' \
    >"${LOG_ROOT}/summary_threshold_compare.csv"
}

refresh_aggregate_summaries() {
  local tmp_summary="${LOG_ROOT}/.summary_threshold.tmp"
  local tmp_compare="${LOG_ROOT}/.summary_threshold_compare.tmp"
  printf 'threshold,network,protocol,parties,input_size,time_s,comm_mb\n' \
    >"${tmp_summary}"
  printf 'threshold,network,parties,input_size,relaxed_time_s,standard_time_s,relaxed_comm_mb,standard_comm_mb\n' \
    >"${tmp_compare}"

  local threshold
  for threshold in $(split_csv_list "${THRESHOLDS}"); do
    append_csv_with_threshold "${threshold}" "${LOG_ROOT}/t${threshold}/summary.csv" \
      "${tmp_summary}"
    append_csv_with_threshold "${threshold}" \
      "${LOG_ROOT}/t${threshold}/summary_compare.csv" "${tmp_compare}"
  done

  mv "${tmp_summary}" "${LOG_ROOT}/summary_threshold.csv"
  mv "${tmp_compare}" "${LOG_ROOT}/summary_threshold_compare.csv"
}

SUMMARY_MONITOR_PID=""
stop_summary_monitor() {
  if [[ -n "${SUMMARY_MONITOR_PID}" ]]; then
    kill "${SUMMARY_MONITOR_PID}" >/dev/null 2>&1 || true
    wait "${SUMMARY_MONITOR_PID}" >/dev/null 2>&1 || true
  fi
}

cleanup_summary_monitor() {
  stop_summary_monitor
  refresh_aggregate_summaries || true
}

while (($#)); do
  case "$1" in
    --thresholds)
      THRESHOLDS="$2"
      shift 2
      ;;
    --set-power)
      SET_POWER="$2"
      shift 2
      ;;
    --profiles)
      PROFILES="$2"
      shift 2
      ;;
    --modes)
      MODES="$2"
      shift 2
      ;;
    --repeats)
      REPEATS="$2"
      shift 2
      ;;
    --threads)
      THREADS="$2"
      shift 2
      ;;
    --base-port)
      BASE_PORT="$2"
      shift 2
      ;;
    --peq-triple-batch-size)
      PEQ_TRIPLE_BATCH_SIZE="$2"
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
    --skip-build)
      SKIP_BUILD=1
      shift
      ;;
    --rerun-existing)
      RERUN_EXISTING=1
      shift
      ;;
    --stop-on-error)
      STOP_ON_ERROR=1
      shift
      ;;
    --no-unshare)
      NO_UNSHARE=1
      shift
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    --paper-faithful)
      PAPER_FAITHFUL=1
      shift
      ;;
    --enable-t2-fast-path)
      ENABLE_T2_FAST_PATH=1
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

[[ -x "${RUNNER}" ]] || die "base runner is not executable: ${RUNNER}"

if ((DRY_RUN == 0)); then
  mkdir -p "${LOG_ROOT}"
  write_summary_headers
  trap cleanup_summary_monitor EXIT
  (
    while true; do
      sleep 10
      refresh_aggregate_summaries || true
    done
  ) &
  SUMMARY_MONITOR_PID="$!"
fi

case_index=0
for threshold in $(split_csv_list "${THRESHOLDS}"); do
  threshold_root="${LOG_ROOT}/t${threshold}"
  args=(
    --parties-list 10
    --threshold "${threshold}"
    --set-powers "${SET_POWER}"
    --profiles "${PROFILES}"
    --modes "${MODES}"
    --repeats "${REPEATS}"
    --threads "${THREADS}"
    --base-port "${BASE_PORT}"
    --log-root "${threshold_root}"
  )
  if [[ -n "${PEQ_TRIPLE_BATCH_SIZE}" ]]; then
    args+=(--peq-triple-batch-size "${PEQ_TRIPLE_BATCH_SIZE}")
  fi
  if [[ -n "${GMW_SIMD}" ]]; then
    args+=(--gmw-simd "${GMW_SIMD}")
  fi
  if ((SKIP_BUILD || case_index > 0)); then
    args+=(--skip-build)
  fi
  if ((RERUN_EXISTING)); then
    args+=(--rerun-existing)
  fi
  if ((STOP_ON_ERROR)); then
    args+=(--stop-on-error)
  fi
  if ((NO_UNSHARE)); then
    args+=(--no-unshare)
  fi
  if ((DRY_RUN)); then
    args+=(--dry-run)
  fi
  if ((PAPER_FAITHFUL)); then
    args+=(--paper-faithful)
  fi
  if ((ENABLE_T2_FAST_PATH)); then
    args+=(--enable-t2-fast-path)
  fi

  log "threshold=${threshold} set=2^${SET_POWER}"
  "${RUNNER}" "${args[@]}"

  if ((DRY_RUN == 0)); then
    refresh_aggregate_summaries
  fi
  case_index=$((case_index + 1))
done

if ((DRY_RUN == 0)); then
  refresh_aggregate_summaries
  log "summary_csv=${LOG_ROOT}/summary_threshold.csv"
  log "summary_compare_csv=${LOG_ROOT}/summary_threshold_compare.csv"
  log "log_root=${LOG_ROOT}"
fi
