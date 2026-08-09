#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

LOG_ROOT="${OTMPSI_ALL_BENCH_LOG_ROOT:-${SCRIPT_DIR}/bench_results/otmpsi_all_benchmarks_$(date +%Y%m%d_%H%M%S)}"
ONLY="relax_standard,n_sweep_t1_2p14,n10_t5,n10_threshold_sweep,output_size_impact,phase_breakdown,party_scalability_2p14"
CONTINUE_ON_ERROR=0
SKIP_BUILD=0
DRY_RUN=0
PAPER_FAITHFUL=0
ENABLE_T2_FAST_PATH=0
FORWARD_ARGS=()

usage() {
  cat <<'EOF'
Usage:
  scripts/run_all_benchmark_scripts.sh [options] [runner options]

Runs the seven OT-MPSI benchmark scripts one after another:
  1. run_relax_standard_tc_matrix.sh
  2. run_n_sweep_t1_2p14_tc_matrix.sh
  3. run_n10_t5_tc_matrix.sh
  4. run_n10_threshold_sweep_tc_matrix.sh
  5. run_output_size_impact_tc_matrix.sh
  6. run_phase_breakdown_tc_matrix.sh
  7. run_party_scalability_2p14_tc_matrix.sh

The last three scripts record CSV data only; they do not generate plots.

Each benchmark writes to a subdirectory under LOG_ROOT:
  LOG_ROOT/relax_standard/
  LOG_ROOT/n_sweep_t1_2p14/
  LOG_ROOT/n10_t5/
  LOG_ROOT/n10_threshold_sweep/
  LOG_ROOT/output_size_impact/
  LOG_ROOT/phase_breakdown/
  LOG_ROOT/party_scalability_2p14/

Options handled by this wrapper:
  --log-root DIR          parent output directory
  --only LIST             comma list of benchmark ids to run
                          default: all seven benchmark ids listed above
  --continue-on-error     keep running later benchmark scripts after a failure
  --skip-build            forward --skip-build to every benchmark
  --dry-run               forward --dry-run to every benchmark
  -h, --help              show this help

Default protocol setting:
  relaxed and standard share Tree-X2A + combined Silent-OT + 6-AND comparison.
  relaxed opens the selection and skips unselected rows; standard uses private
  selector OT. --paper-faithful and --enable-t2-fast-path affect relaxed only.

Common runner options are forwarded to each benchmark script, for example:
  --profiles lan
  --modes relaxed,standard
  --repeats 1
  --threads 1
  --base-port 19350
  --case-timeout-seconds N
  --peq-triple-batch-size N
  --gmw-simd avx2
  --paper-faithful
  --enable-t2-fast-path
  --rerun-existing
  --stop-on-error
  --no-unshare

The wrapper automatically adds --skip-build after the first selected benchmark
so the CMake binary is built at most once.
EOF
}

log() {
  printf '[otmpsi-all-bench] %s\n' "$*" >&2
}

die() {
  printf '[otmpsi-all-bench] ERROR: %s\n' "$*" >&2
  exit 1
}

split_csv_list() {
  local value="$1"
  tr ',' '\n' <<<"${value}" | sed '/^$/d'
}

selected_benchmark() {
  local needle="$1"
  local item
  for item in $(split_csv_list "${ONLY}"); do
    [[ "${item}" == "${needle}" ]] && return 0
  done
  return 1
}

known_benchmark() {
  case "$1" in
    relax_standard | n_sweep_t1_2p14 | n10_t5 | n10_threshold_sweep | output_size_impact | phase_breakdown | party_scalability_2p14)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

csv_escape() {
  local value="${1:-}"
  value="${value//\"/\"\"}"
  printf '"%s"' "${value}"
}

write_run_row() {
  local csv="$1"
  local started_at="$2"
  local id="$3"
  local script="$4"
  local status="$5"
  local exit_code="$6"
  local elapsed_s="$7"
  local log_root="$8"

  {
    csv_escape "${started_at}"
    printf ','
    csv_escape "${id}"
    printf ','
    csv_escape "${script}"
    printf ','
    csv_escape "${status}"
    printf ','
    csv_escape "${exit_code}"
    printf ','
    csv_escape "${elapsed_s}"
    printf ','
    csv_escape "${log_root}"
    printf '\n'
  } >>"${csv}"
}

run_one() {
  local index="$1"
  local id="$2"
  local script="$3"
  local child_log_root="${LOG_ROOT}/${id}"
  local script_path="${SCRIPT_DIR}/${script}"

  [[ -x "${script_path}" ]] || die "benchmark script is not executable: ${script_path}"

  local args=(--log-root "${child_log_root}" "${FORWARD_ARGS[@]}")
  if ((SKIP_BUILD || index > 0)); then
    args+=(--skip-build)
  fi

  local started_at start_ns end_ns elapsed_s rc
  started_at="$(date -Is)"
  start_ns="$(date +%s%N)"
  log "start ${id}: ${script}"

  set +e
  "${script_path}" "${args[@]}"
  rc=$?
  set -e

  end_ns="$(date +%s%N)"
  elapsed_s="$(awk -v s="${start_ns}" -v e="${end_ns}" 'BEGIN { printf "%.6f", (e - s) / 1000000000.0 }')"

  if ((rc == 0)); then
    log "done ${id}: elapsed=${elapsed_s}s log_root=${child_log_root}"
    if ((DRY_RUN == 0)); then
      write_run_row "${RUNS_CSV}" "${started_at}" "${id}" "${script}" success "${rc}" "${elapsed_s}" "${child_log_root}"
    fi
    return 0
  fi

  log "failed ${id}: exit=${rc} elapsed=${elapsed_s}s log_root=${child_log_root}"
  if ((DRY_RUN == 0)); then
    write_run_row "${RUNS_CSV}" "${started_at}" "${id}" "${script}" failed "${rc}" "${elapsed_s}" "${child_log_root}"
  fi
  return "${rc}"
}

while (($#)); do
  case "$1" in
    --log-root)
      [[ $# -ge 2 ]] || die "missing value for --log-root"
      LOG_ROOT="$2"
      shift 2
      ;;
    --only)
      [[ $# -ge 2 ]] || die "missing value for --only"
      ONLY="$2"
      shift 2
      ;;
    --continue-on-error)
      CONTINUE_ON_ERROR=1
      shift
      ;;
    --skip-build)
      SKIP_BUILD=1
      FORWARD_ARGS+=("$1")
      shift
      ;;
    --dry-run)
      DRY_RUN=1
      FORWARD_ARGS+=("$1")
      shift
      ;;
    --paper-faithful)
      PAPER_FAITHFUL=1
      FORWARD_ARGS+=("$1")
      shift
      ;;
    --enable-t2-fast-path)
      ENABLE_T2_FAST_PATH=1
      FORWARD_ARGS+=("$1")
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      FORWARD_ARGS+=("$1")
      shift
      ;;
  esac
done

if ((PAPER_FAITHFUL == 0 && ENABLE_T2_FAST_PATH == 0)); then
  unset OTMPSI_DISABLE_RELAXED_BOOLEAN_GMW
  unset OTMPSI_DISABLE_T2_RELAXED_FAST
  unset OTMPSI_ENABLE_T2_FAST
  unset OTMPSI_ENABLE_T2_RELAXED_FAST
fi

BENCH_IDS=(
  relax_standard
  n_sweep_t1_2p14
  n10_t5
  n10_threshold_sweep
  output_size_impact
  phase_breakdown
  party_scalability_2p14
)
BENCH_SCRIPTS=(
  run_relax_standard_tc_matrix.sh
  run_n_sweep_t1_2p14_tc_matrix.sh
  run_n10_t5_tc_matrix.sh
  run_n10_threshold_sweep_tc_matrix.sh
  run_output_size_impact_tc_matrix.sh
  run_phase_breakdown_tc_matrix.sh
  run_party_scalability_2p14_tc_matrix.sh
)

for requested_id in $(split_csv_list "${ONLY}"); do
  known_benchmark "${requested_id}" || die "unknown benchmark id in --only: ${requested_id}"
done

selected_count=0
for id in "${BENCH_IDS[@]}"; do
  if selected_benchmark "${id}"; then
    selected_count=$((selected_count + 1))
  fi
done
((selected_count > 0)) || die "no benchmarks selected by --only=${ONLY}"

if ((DRY_RUN == 0)); then
  mkdir -p "${LOG_ROOT}"
  RUNS_CSV="${LOG_ROOT}/runs.csv"
  printf 'started_at,benchmark_id,script,status,exit_code,elapsed_s,log_root\n' >"${RUNS_CSV}"
else
  RUNS_CSV=""
fi

log "log_root=${LOG_ROOT}"
log "selected=${ONLY}"
if ((PAPER_FAITHFUL)); then
  log "protocol_default=standard_tree_x2a_relaxed_paper"
elif ((ENABLE_T2_FAST_PATH)); then
  log "protocol_default=standard_tree_x2a_relaxed_t2_fast"
else
  log "protocol_default=standard_tree_x2a"
fi

run_index=0
failures=0
for i in "${!BENCH_IDS[@]}"; do
  id="${BENCH_IDS[$i]}"
  script="${BENCH_SCRIPTS[$i]}"
  selected_benchmark "${id}" || continue

  if run_one "${run_index}" "${id}" "${script}"; then
    :
  else
    failures=$((failures + 1))
    if ((CONTINUE_ON_ERROR == 0)); then
      log "stopping after first failed benchmark; use --continue-on-error to keep going"
      exit 1
    fi
  fi
  run_index=$((run_index + 1))
done

if ((failures > 0)); then
  log "completed with ${failures} failed benchmark script(s)"
  exit 1
fi

if ((DRY_RUN == 0)); then
  log "runs_csv=${RUNS_CSV}"
fi
log "all selected benchmark scripts completed"
