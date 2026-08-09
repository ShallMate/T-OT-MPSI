#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RUNNER="${SCRIPT_DIR}/run_relax_standard_tc_matrix.sh"
FORMATTER="${SCRIPT_DIR}/format_otmpsi_experiment_data.py"
LOG_ROOT="${OTMPSI_PARTY_SCALABILITY_LOG_ROOT:-${SCRIPT_DIR}/bench_results/otmpsi_party_scalability_2p14_$(date +%Y%m%d_%H%M%S)}"
SMOKE_TIMEOUT_SECONDS="${OTMPSI_SMOKE_TIMEOUT_SECONDS:-300}"
CASE_TIMEOUT_SECONDS="${OTMPSI_SCALABILITY_TIMEOUT_SECONDS:-1800}"
SKIP_SMOKE=0
SMOKE_ONLY=0
DRY_RUN=0
FORWARD_ARGS=()

usage() {
  cat <<'EOF'
Usage:
  scripts/run_party_scalability_2p14_tc_matrix.sh [options]

Records the party-scalability matrix (no plotting):
  N=10,20,30,40; n=2^14; t=N-1; s/n=1/4
  relaxed and standard
  LAN and WAN 50Mbps/40ms RTT

Before the full matrix, the script runs progressive LAN smoke cases at n=2^10
for N=10,20,30,40 and both protocols. A failed smoke stops the full matrix,
while all logs and failure rows remain under LOG_ROOT/smoke/.

Options:
  --log-root DIR
  --smoke-timeout-seconds N  default: 300
  --case-timeout-seconds N   default: 1800
  --skip-smoke
  --smoke-only
  plus common runner options such as --repeats, --threads, --skip-build,
  --rerun-existing, --dry-run, and --no-unshare.

Outputs:
  LOG_ROOT/resource_snapshot.txt
  LOG_ROOT/smoke/raw_summary.csv
  LOG_ROOT/raw_summary.csv
  LOG_ROOT/summary.csv
  LOG_ROOT/summary_compare.csv
  LOG_ROOT/failures.csv
EOF
}

while (($#)); do
  case "$1" in
    --log-root)
      [[ $# -ge 2 ]] || { echo "missing value for --log-root" >&2; exit 1; }
      LOG_ROOT="$2"
      shift 2
      ;;
    --smoke-timeout-seconds)
      SMOKE_TIMEOUT_SECONDS="$2"
      shift 2
      ;;
    --case-timeout-seconds)
      CASE_TIMEOUT_SECONDS="$2"
      shift 2
      ;;
    --skip-smoke)
      SKIP_SMOKE=1
      shift
      ;;
    --smoke-only)
      SMOKE_ONLY=1
      shift
      ;;
    --dry-run)
      DRY_RUN=1
      FORWARD_ARGS+=("$1")
      shift
      ;;
    --raw-summary-csv | --summary-csv)
      echo "$1 is managed by this experiment script; use --log-root" >&2
      exit 1
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

if ((SKIP_SMOKE && SMOKE_ONLY)); then
  echo "--skip-smoke and --smoke-only cannot be used together" >&2
  exit 1
fi
if ((DRY_RUN == 0)); then
  mkdir -p "${LOG_ROOT}"
  {
    printf 'timestamp=%s\n' "$(date -Is)"
    printf 'open_files_limit=%s\n' "$(ulimit -n)"
    printf 'max_user_processes=%s\n' "$(ulimit -u)"
    awk '/MemAvailable:/ {print "memory_available_kb=" $2}' /proc/meminfo
  } >"${LOG_ROOT}/resource_snapshot.txt"
fi

smoke_rc=0
if ((SKIP_SMOKE == 0)); then
  set +e
  "${RUNNER}" \
    --parties-list 10,20,30,40 \
    --threshold n-1 \
    --set-powers 10 \
    --output-ratios 1/4 \
    --profiles lan \
    --modes relaxed,standard \
    --case-timeout-seconds "${SMOKE_TIMEOUT_SECONDS}" \
    --log-root "${LOG_ROOT}/smoke" \
    "${FORWARD_ARGS[@]}"
  smoke_rc=$?
  set -e
  if [[ -s "${LOG_ROOT}/smoke/raw_summary.csv" ]]; then
    python3 "${FORMATTER}" --experiment scalability \
      --raw-summary "${LOG_ROOT}/smoke/raw_summary.csv" \
      --out-dir "${LOG_ROOT}/smoke"
  fi
  if ((DRY_RUN == 0)); then
    printf 'status,%s\nexit_code,%s\n' \
      "$([[ ${smoke_rc} -eq 0 ]] && printf success || printf failed)" \
      "${smoke_rc}" >"${LOG_ROOT}/smoke_status.csv"
  fi
  if ((smoke_rc != 0)); then
    echo "smoke test failed; full scalability matrix was not started" >&2
    exit "${smoke_rc}"
  fi
fi

if ((SMOKE_ONLY)); then
  exit 0
fi

full_args=("${FORWARD_ARGS[@]}")
if ((SKIP_SMOKE == 0)); then
  full_args+=(--skip-build)
fi

set +e
"${RUNNER}" \
  --parties-list 10,20,30,40 \
  --threshold n-1 \
  --set-powers 14 \
  --output-ratios 1/4 \
  --profiles lan,wan50 \
  --modes relaxed,standard \
  --case-timeout-seconds "${CASE_TIMEOUT_SECONDS}" \
  --log-root "${LOG_ROOT}" \
  "${full_args[@]}"
runner_rc=$?
set -e

if [[ -s "${LOG_ROOT}/raw_summary.csv" ]]; then
  python3 "${FORMATTER}" --experiment scalability \
    --raw-summary "${LOG_ROOT}/raw_summary.csv" --out-dir "${LOG_ROOT}"
fi
exit "${runner_rc}"
