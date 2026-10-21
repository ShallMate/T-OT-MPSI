#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/run_b2a_backend_compare.sh [options]

Every default protocol run uses the optimized Tree/Silent-OT comparison route.

Options:
  --parties N              Number of OT-MPSI parties. Default: 10
  --set-size M             Set size. Default: 4096
  --threshold T            Threshold. Default: 5
  --threads N              OT-MPSI worker threads. Default: 1
  --base-port PORT         OT-MPSI local socket namespace. Default: 19350
  --log-dir DIR            Output log directory. Default: $HOME/otmpsi_runs/...
  --build                  Build otmpsi_demo before running. Default
  --no-build               Reuse existing binary
  -h, --help               Show this message
EOF
}

die() {
  echo "[b2a-backend-compare] $*" >&2
  exit 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${OTMPSI_BUILD_DIR:-${PROJECT_ROOT}/build}"

PARTIES=10
SET_SIZE=4096
THRESHOLD=5
THREADS=1
BASE_PORT=19350
LOG_DIR=""
DO_BUILD=yes

while [[ $# -gt 0 ]]; do
  case "$1" in
    --parties)
      [[ $# -ge 2 ]] || die "missing value for --parties"
      PARTIES="$2"
      shift 2
      ;;
    --set-size)
      [[ $# -ge 2 ]] || die "missing value for --set-size"
      SET_SIZE="$2"
      shift 2
      ;;
    --threshold)
      [[ $# -ge 2 ]] || die "missing value for --threshold"
      THRESHOLD="$2"
      shift 2
      ;;
    --threads)
      [[ $# -ge 2 ]] || die "missing value for --threads"
      THREADS="$2"
      shift 2
      ;;
    --base-port)
      [[ $# -ge 2 ]] || die "missing value for --base-port"
      BASE_PORT="$2"
      shift 2
      ;;
    --log-dir)
      [[ $# -ge 2 ]] || die "missing value for --log-dir"
      LOG_DIR="$2"
      shift 2
      ;;
    --build)
      DO_BUILD=yes
      shift
      ;;
    --no-build)
      DO_BUILD=no
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

for value_name in PARTIES SET_SIZE THRESHOLD THREADS BASE_PORT; do
  value="${!value_name}"
  [[ "$value" =~ ^[0-9]+$ ]] || die "${value_name} must be an integer"
done

(( PARTIES >= 2 )) || die "--parties must be at least 2"
(( THRESHOLD > 1 && THRESHOLD <= PARTIES )) || die "--threshold must satisfy 1 < T <= N"
(( SET_SIZE >= 4096 )) || die "--set-size starts at 4096 for this runner"
(( BASE_PORT > 0 && BASE_PORT < 65536 )) || die "--base-port out of range"

if [[ -z "$LOG_DIR" ]]; then
  LOG_ROOT="${OTMPSI_LOG_ROOT:-${HOME}/otmpsi_runs}"
  LOG_DIR="${LOG_ROOT}/otmpsi_b2a_backend_compare_$(date +%Y%m%d_%H%M%S)_$$"
fi
mkdir -p "$LOG_DIR"

OTMPSI_BIN="${OTMPSI_BIN:-${BUILD_DIR}/otmpsi_demo}"
OTMPSI_LOG="${LOG_DIR}/otmpsi_ot_b2a.log"
SUMMARY_CSV="${LOG_DIR}/summary.csv"
OLE_COUNT=$(((PARTIES - 1) * SET_SIZE))

extract_kv() {
  local file="$1"
  local key="$2"
  awk -F= -v key="$key" '
    $1 ~ key {
      value = $2
      sub(/,.*/, "", value)
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
      print value
      exit
    }
  ' "$file"
}

require_value() {
  local name="$1"
  local value="$2"
  [[ -n "$value" ]] || die "missing ${name} in ${OTMPSI_LOG}"
}

if [[ "$DO_BUILD" == yes ]]; then
  echo "[b2a-backend-compare] building otmpsi_demo"
  (
    cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release -DOTMPSI_BUILD_TESTS=OFF
    cmake --build "$BUILD_DIR" --target otmpsi_demo \
      -j "${OTMPSI_BUILD_JOBS:-8}"
  )
fi

[[ -x "$OTMPSI_BIN" ]] || die "missing binary: $OTMPSI_BIN"

echo "[b2a-backend-compare] parties=${PARTIES}"
echo "[b2a-backend-compare] set_size=${SET_SIZE}"
echo "[b2a-backend-compare] threshold=${THRESHOLD}"
echo "[b2a-backend-compare] b2a_ole_count=${OLE_COUNT}"
echo "[b2a-backend-compare] log_dir=${LOG_DIR}"

"$OTMPSI_BIN" \
  --parties "$PARTIES" \
  --set-size "$SET_SIZE" \
  --threshold "$THRESHOLD" \
  --threads "$THREADS" \
  --base-port "$BASE_PORT" | tee "$OTMPSI_LOG"

ot_b2a_time="$(extract_kv "$OTMPSI_LOG" "stage_b2a_time_s")"
ot_b2a_leader_mb="$(extract_kv "$OTMPSI_LOG" "stage_b2a_leader_comm_mb")"
ot_b2a_clients_mb="$(extract_kv "$OTMPSI_LOG" "stage_b2a_clients_sum_comm_mb")"
ot_total_time="$(extract_kv "$OTMPSI_LOG" "total_time_s")"

require_value "stage_b2a_time_s" "$ot_b2a_time"
require_value "stage_b2a_leader_comm_mb" "$ot_b2a_leader_mb"
require_value "stage_b2a_clients_sum_comm_mb" "$ot_b2a_clients_mb"
require_value "total_time_s" "$ot_total_time"

ot_total_mb="$(awk -v a="$ot_b2a_leader_mb" -v b="$ot_b2a_clients_mb" 'BEGIN { printf "%.6f", a + b }')"

cat >"$SUMMARY_CSV" <<EOF
backend,mode,ole_count,time_s,leader_comm_mb,clients_comm_mb,total_comm_mb,full_protocol_time_s,note
silent_ot,functional_b2a,${OLE_COUNT},${ot_b2a_time},${ot_b2a_leader_mb},${ot_b2a_clients_mb},${ot_total_mb},${ot_total_time},input-consuming B2A used by otmpsi
EOF

echo "[b2a-backend-compare] summary:"
column -s, -t "$SUMMARY_CSV" 2>/dev/null || cat "$SUMMARY_CSV"
echo "[b2a-backend-compare] summary_csv=${SUMMARY_CSV}"
echo "[b2a-backend-compare] logs=${LOG_DIR}"
