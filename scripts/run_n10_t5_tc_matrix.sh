#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RUNNER="${SCRIPT_DIR}/run_relax_standard_tc_matrix.sh"

LOG_ROOT="${OTMPSI_N10_T5_LOG_ROOT:-${SCRIPT_DIR}/bench_results/otmpsi_n10_t5_tc_$(date +%Y%m%d_%H%M%S)}"

usage() {
  cat <<'EOF'
Usage:
  scripts/run_n10_t5_tc_matrix.sh [runner options]

Runs the OT-MPSI N=10, threshold=5 benchmark matrix:
  input sizes: 2^10, 2^12, 2^14, 2^16, 2^18
  networks:    LAN 10Gbps/0.02ms RTT, 200Mbps/40ms RTT,
               100Mbps/40ms RTT, 50Mbps/40ms RTT
  modes:       relaxed, standard
               both share the optimized Tree/Silent-OT comparison route

Outputs:
  LOG_ROOT/summary.csv          compact runtime/communication table
  LOG_ROOT/summary_compare.csv  relaxed-vs-standard comparison
  LOG_ROOT/raw_summary.csv      full internal fields

Common options forwarded to the base runner:
  --skip-build
  --rerun-existing
  --stop-on-error
  --dry-run
  --modes relaxed|standard|relaxed,standard
  --log-root DIR
  --summary-csv FILE
  --raw-summary-csv FILE
  --gmw-simd scalar|avx2|avx512
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ ! -x "${RUNNER}" ]]; then
  echo "[otmpsi-n10-t5] ERROR: base runner is not executable: ${RUNNER}" >&2
  exit 1
fi

exec "${RUNNER}" \
  --parties-list 10 \
  --threshold 5 \
  --set-powers 10,12,14,16,18 \
  --profiles lan,wan200,wan100,wan50 \
  --modes relaxed,standard \
  --log-root "${LOG_ROOT}" \
  "$@"
