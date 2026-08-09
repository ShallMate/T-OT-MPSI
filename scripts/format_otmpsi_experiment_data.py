#!/usr/bin/env python3
"""Format OT-MPSI experiment rows into plot-ready CSV data (no plots)."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Iterable


COMMON_COLUMNS = [
    "timestamp",
    "network",
    "network_label",
    "protocol",
    "parties",
    "input_size",
    "threshold",
    "output_ratio",
    "target_output_size",
    "actual_output_size",
    "repeat",
    "status",
    "total_time_s",
    "total_comm_mb",
    "wall_time_s",
    "log_dir",
]

TOTAL_COMPARE_KEY = [
    "network",
    "network_label",
    "parties",
    "input_size",
    "threshold",
    "output_ratio",
    "target_output_size",
    "repeat",
]

TOTAL_COMPARE_COLUMNS = TOTAL_COMPARE_KEY + [
    "relaxed_status",
    "standard_status",
    "relaxed_actual_output_size",
    "standard_actual_output_size",
    "relaxed_time_s",
    "standard_time_s",
    "relaxed_comm_mb",
    "standard_comm_mb",
]

PHASE_COLUMNS = COMMON_COLUMNS + [
    "stage_order",
    "stage",
    "stage_time_s",
    "stage_comm_mb",
]

PHASE_COMPARE_KEY = TOTAL_COMPARE_KEY + ["stage_order", "stage"]
PHASE_COMPARE_COLUMNS = PHASE_COMPARE_KEY + [
    "relaxed_status",
    "standard_status",
    "relaxed_time_s",
    "standard_time_s",
    "relaxed_comm_mb",
    "standard_comm_mb",
]

STAGES = [
    (
        "1",
        "SS-BSMT",
        ["aggregate_stage_ssbsmt_max_time_s"],
        ["aggregate_stage_ssbsmt_wire_comm_mb"],
    ),
    (
        "2",
        "PEQT/B2A",
        ["aggregate_stage_peqt_b2a_max_time_s"],
        [
            "aggregate_stage_pairwise_zero_wire_comm_mb",
            "aggregate_stage_b2a_wire_comm_mb",
        ],
    ),
    (
        "3",
        "Threshold and Output",
        ["aggregate_stage_nparty_gmw_max_time_s"],
        ["aggregate_stage_nparty_gmw_wire_comm_mb"],
    ),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create experiment-specific OT-MPSI CSV data without plotting."
    )
    parser.add_argument(
        "--experiment",
        required=True,
        choices=["output-size", "phase", "scalability"],
    )
    parser.add_argument("--raw-summary", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    return parser.parse_args()


def normalize_protocol(mode: str) -> str:
    return {"relax": "relaxed", "std": "standard"}.get(mode, mode)


def common_row(row: dict[str, str]) -> dict[str, str]:
    return {
        "timestamp": row.get("timestamp", ""),
        "network": row.get("profile", ""),
        "network_label": row.get("profile_label", ""),
        "protocol": normalize_protocol(row.get("mode", "")),
        "parties": row.get("parties", ""),
        "input_size": row.get("set_size", ""),
        "threshold": row.get("threshold", ""),
        "output_ratio": row.get("output_ratio", ""),
        "target_output_size": row.get("target_output_size", ""),
        "actual_output_size": row.get("output_size", ""),
        "repeat": row.get("repeat", ""),
        "status": row.get("status", ""),
        "total_time_s": row.get("aggregate_max_time_s", ""),
        "total_comm_mb": row.get("aggregate_wire_total_comm_mb", ""),
        "wall_time_s": row.get("wall_time_s", ""),
        "log_dir": row.get("log_dir", ""),
    }


def sum_values(row: dict[str, str], keys: Iterable[str]) -> str:
    values: list[float] = []
    for key in keys:
        value = row.get(key, "")
        if value == "":
            continue
        try:
            values.append(float(value))
        except ValueError:
            return ""
    if not values:
        return ""
    return f"{sum(values):.6f}"


def phase_rows(raw_rows: Iterable[dict[str, str]]) -> list[dict[str, str]]:
    output: list[dict[str, str]] = []
    for raw in raw_rows:
        common = common_row(raw)
        for order, stage, time_keys, comm_keys in STAGES:
            output.append(
                common
                | {
                    "stage_order": order,
                    "stage": stage,
                    "stage_time_s": sum_values(raw, time_keys),
                    "stage_comm_mb": sum_values(raw, comm_keys),
                }
            )
    return output


def total_compare_rows(rows: Iterable[dict[str, str]]) -> list[dict[str, str]]:
    grouped: dict[tuple[str, ...], dict[str, dict[str, str]]] = {}
    for row in rows:
        key = tuple(row[column] for column in TOTAL_COMPARE_KEY)
        grouped.setdefault(key, {})[row["protocol"]] = row

    output: list[dict[str, str]] = []
    for key, protocols in grouped.items():
        relaxed = protocols.get("relaxed", {})
        standard = protocols.get("standard", {})
        item = dict(zip(TOTAL_COMPARE_KEY, key))
        item.update(
            {
                "relaxed_status": relaxed.get("status", ""),
                "standard_status": standard.get("status", ""),
                "relaxed_actual_output_size": relaxed.get("actual_output_size", ""),
                "standard_actual_output_size": standard.get("actual_output_size", ""),
                "relaxed_time_s": relaxed.get("total_time_s", ""),
                "standard_time_s": standard.get("total_time_s", ""),
                "relaxed_comm_mb": relaxed.get("total_comm_mb", ""),
                "standard_comm_mb": standard.get("total_comm_mb", ""),
            }
        )
        output.append(item)
    return output


def phase_compare_rows(rows: Iterable[dict[str, str]]) -> list[dict[str, str]]:
    grouped: dict[tuple[str, ...], dict[str, dict[str, str]]] = {}
    for row in rows:
        key = tuple(row[column] for column in PHASE_COMPARE_KEY)
        grouped.setdefault(key, {})[row["protocol"]] = row

    output: list[dict[str, str]] = []
    for key, protocols in grouped.items():
        relaxed = protocols.get("relaxed", {})
        standard = protocols.get("standard", {})
        item = dict(zip(PHASE_COMPARE_KEY, key))
        item.update(
            {
                "relaxed_status": relaxed.get("status", ""),
                "standard_status": standard.get("status", ""),
                "relaxed_time_s": relaxed.get("stage_time_s", ""),
                "standard_time_s": standard.get("stage_time_s", ""),
                "relaxed_comm_mb": relaxed.get("stage_comm_mb", ""),
                "standard_comm_mb": standard.get("stage_comm_mb", ""),
            }
        )
        output.append(item)
    return output


def write_csv(path: Path, columns: list[str], rows: Iterable[dict[str, str]]) -> None:
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    with args.raw_summary.open(newline="") as handle:
        raw_rows = list(csv.DictReader(handle))
    common = [common_row(row) for row in raw_rows]

    if args.experiment == "phase":
        summary = phase_rows(raw_rows)
        write_csv(args.out_dir / "summary.csv", PHASE_COLUMNS, summary)
        write_csv(
            args.out_dir / "summary_compare.csv",
            PHASE_COMPARE_COLUMNS,
            phase_compare_rows(summary),
        )
    else:
        write_csv(args.out_dir / "summary.csv", COMMON_COLUMNS, common)
        write_csv(
            args.out_dir / "summary_compare.csv",
            TOTAL_COMPARE_COLUMNS,
            total_compare_rows(common),
        )

    write_csv(
        args.out_dir / "failures.csv",
        COMMON_COLUMNS,
        (row for row in common if row["status"] != "success"),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
