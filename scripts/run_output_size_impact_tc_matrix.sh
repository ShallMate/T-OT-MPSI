#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RUNNER="${SCRIPT_DIR}/run_relax_standard_tc_matrix.sh"
FORMATTER="${SCRIPT_DIR}/format_otmpsi_experiment_data.py"
LOG_ROOT="${OTMPSI_OUTPUT_SIZE_LOG_ROOT:-${SCRIPT_DIR}/bench_results/otmpsi_output_size_impact_$(date +%Y%m%d_%H%M%S)}"
FORWARD_ARGS=()

usage() {
  cat <<'EOF'
Usage:
  scripts/run_output_size_impact_tc_matrix.sh [runner options]

Records the output-size impact matrix (no plotting):
  N=10, n=2^14, t=5
  s/n=0,1/16,1/4,1/2,1
  relaxed and standard
  LAN and WAN 50Mbps/40ms RTT

Outputs:
  LOG_ROOT/raw_summary.csv   full per-case measurements and failures
  LOG_ROOT/summary.csv       long-form data for plotting/tables
  LOG_ROOT/summary_compare.csv
  LOG_ROOT/failures.csv

Useful options: --repeats N, --threads N, --case-timeout-seconds N,
--skip-build, --rerun-existing, --stop-on-error, --dry-run, --no-unshare.
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
  --set-powers 14 \
  --output-ratios 0,1/16,1/4,1/2,1 \
  --profiles lan,wan50 \
  --modes relaxed,standard \
  --log-root "${LOG_ROOT}" \
  "${FORWARD_ARGS[@]}"
runner_rc=$?
set -e

if [[ -s "${LOG_ROOT}/raw_summary.csv" ]]; then
  python3 "${FORMATTER}" --experiment output-size \
    --raw-summary "${LOG_ROOT}/raw_summary.csv" --out-dir "${LOG_ROOT}"
fi
exit "${runner_rc}"
