#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${OTMPSI_BUILD_DIR:-${project_root}/build}"
cmake -S "$project_root" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release -DOTMPSI_BUILD_TESTS=OFF
cmake --build "$build_dir" --target otmpsi_demo \
  -j "${OTMPSI_BUILD_JOBS:-8}"

args=()
parties=4
base_port=19350
single_process=0
party_mode=0

while (($#)); do
  case "$1" in
    --single-process)
      single_process=1
      shift
      ;;
    --party-id)
      party_mode=1
      args+=("$1" "$2")
      shift 2
      ;;
    --parties|-N)
      parties="$2"
      args+=("$1" "$2")
      shift 2
      ;;
    --base-port)
      base_port="$2"
      args+=("$1" "$2")
      shift 2
      ;;
    --b2a-backend)
      args+=("$1" "$2")
      shift 2
      ;;
    *)
      args+=("$1")
      shift
      ;;
  esac
done

bin="${OTMPSI_BIN:-${build_dir}/otmpsi_demo}"
if ((single_process || party_mode)); then
  exec "$bin" "${args[@]}"
fi

log_root="${OTMPSI_LOG_ROOT:-${HOME}/otmpsi_runs}"
log_dir="${OTMPSI_LOG_DIR:-${log_root}/otmpsi_one_process_per_party_${base_port}_$(date +%s)}"
mkdir -p "$log_dir"
export OTMPSI_TRANSPORT="${OTMPSI_TRANSPORT:-tcp}"
export OTMPSI_PIPELINE_2PC="${OTMPSI_PIPELINE_2PC:-1}"

port_map_dir="$log_dir/port_map"
"$bin" "${args[@]}" --generate-port-map "$port_map_dir" >"$log_dir/port_map.log"
export OTMPSI_PORT_MAP_DIR="$port_map_dir"

pids=()
for ((party = 0; party < parties; ++party)); do
  "$bin" "${args[@]}" --party-id "$party" >"$log_dir/party_${party}.log" 2>&1 &
  pids+=("$!")
done

status=0
for pid in "${pids[@]}"; do
  if ! wait "$pid"; then
    status=1
  fi
done

echo "logs=$log_dir"
if ((status != 0)); then
  for ((party = 0; party < parties; ++party)); do
    echo "===== party ${party} ====="
    tail -n 80 "$log_dir/party_${party}.log" || true
  done
  exit "$status"
fi

cat "$log_dir/party_0.log"
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
  END {
    printf("aggregate_max_time_s=%.6f\n", max_time)
    printf("aggregate_wire_total_comm_mb=%.6f\n", sum_comm / 2.0)
    printf("aggregate_leader_comm_mb=%.6f\n", leader_comm)
    if (client_count > 0) {
      printf("aggregate_client_avg_comm_mb=%.6f\n", client_sum / client_count)
      printf("aggregate_clients_sum_comm_mb=%.6f\n", client_sum)
    }
    printf("aggregate_stage_ssbsmt_wire_comm_mb=%.6f\n", ssbsmt / 2.0)
    printf("aggregate_stage_pairwise_zero_wire_comm_mb=%.6f\n", zero / 2.0)
    printf("aggregate_stage_b2a_wire_comm_mb=%.6f\n", b2a / 2.0)
    printf("aggregate_stage_nparty_gmw_wire_comm_mb=%.6f\n", gmw / 2.0)
  }
' "$log_dir"/party_*.log
