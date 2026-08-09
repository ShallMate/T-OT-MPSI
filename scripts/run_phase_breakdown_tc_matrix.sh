#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RUNNER="${SCRIPT_DIR}/run_relax_standard_tc_matrix.sh"
FORMATTER="${SCRIPT_DIR}/format_otmpsi_experiment_data.py"
LOG_ROOT="${OTMPSI_PHASE_BREAKDOWN_LOG_ROOT:-${SCRIPT_DIR}/bench_results/otmpsi_phase_breakdown_$(date +%Y%m%d_%H%M%S)}"
FORWARD_ARGS=()

usage() {
  cat <<'EOF'
Usage:
  scripts/run_phase_breakdown_tc_matrix.sh [runner options]

Records protocol-stage time and communication data (no plotting):
  N=10, t=5, n=2^14 and 2^18, s/n=1
  relaxed and standard
  LAN and WAN 50Mbps/40ms RTT

Stage boundaries in summary.csv:
  SS-BSMT
  PEQT/B2A
  Threshold and Output

Stage time is the maximum across parties. Stage communication is the sum of
sent and received bytes across parties divided by two.

Outputs:
  LOG_ROOT/raw_summary.csv   full per-case measurements and failures
  LOG_ROOT/summary.csv       long-form stage data
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

set +e
"${RUNNER}" \
  --parties-list 10 \
  --threshold 5 \
  --set-powers 14,18 \
  --output-ratios 1 \
  --profiles lan,wan50 \
  --modes relaxed,standard \
  --log-root "${LOG_ROOT}" \
  "${FORWARD_ARGS[@]}"
runner_rc=$?
set -e

if [[ -s "${LOG_ROOT}/raw_summary.csv" ]]; then
  python3 "${FORMATTER}" --experiment phase \
    --raw-summary "${LOG_ROOT}/raw_summary.csv" --out-dir "${LOG_ROOT}"
fi
exit "${runner_rc}"
