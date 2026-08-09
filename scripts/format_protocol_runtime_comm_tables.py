#!/usr/bin/env python3
"""Build compact OT-MPSI runtime/communication CSV tables from summary.csv."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Iterable


DETAIL_COLUMNS = ["network", "protocol", "parties", "input_size", "time_s", "comm_mb"]

COMPARE_COLUMNS = [
    "network",
    "parties",
    "input_size",
    "relaxed_time_s",
    "standard_time_s",
    "relaxed_comm_mb",
    "standard_comm_mb",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create compact OT-MPSI runtime and communication CSV tables."
    )
    parser.add_argument("summary_csv", type=Path, help="Path to summary.csv")
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Output directory. Default: raw summary parent",
    )
    return parser.parse_args()


def read_summary(path: Path) -> list[dict[str, str]]:
    with path.open("r", newline="") as handle:
        return list(csv.DictReader(handle))


def number(value: str | None) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except ValueError:
        return None


def normalize_mode(mode: str) -> str:
    if mode == "relax":
        return "relaxed"
    if mode == "std":
        return "standard"
    return mode


def detail_rows(rows: Iterable[dict[str, str]]) -> list[dict[str, str]]:
    details = []
    for row in rows:
        if row.get("status") != "success":
            continue
        runtime_s = row.get("aggregate_max_time_s") or row.get("wall_time_s") or ""
        details.append(
            {
                "network": row.get("profile", ""),
                "protocol": normalize_mode(row.get("mode", "")),
                "parties": row.get("parties", ""),
                "input_size": row.get("set_size", ""),
                "time_s": runtime_s,
                "comm_mb": row.get("aggregate_wire_total_comm_mb", ""),
            }
        )
    return details


def compare_rows(details: Iterable[dict[str, str]]) -> list[dict[str, str]]:
    grouped: dict[tuple[str, str, str], dict[str, dict[str, str]]] = {}
    for row in details:
        key = (
            row["network"],
            row["parties"],
            row["input_size"],
        )
        if key not in grouped:
            grouped[key] = {}
        grouped[key][row["protocol"]] = row

    output = []
    for key in grouped:
        network, parties, input_size = key
        relaxed = grouped[key].get("relaxed", {})
        standard = grouped[key].get("standard", {})
        output.append(
            {
                "network": network,
                "parties": parties,
                "input_size": input_size,
                "relaxed_time_s": relaxed.get("time_s", ""),
                "standard_time_s": standard.get("time_s", ""),
                "relaxed_comm_mb": relaxed.get("comm_mb", ""),
                "standard_comm_mb": standard.get("comm_mb", ""),
            }
        )
    return output


def write_csv(path: Path, fieldnames: list[str], rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def remove_old_tables(out_dir: Path) -> None:
    for name in [
        "runtime_comm.csv",
        "runtime_comm_compare.csv",
        "protocol_runtime_comm_compare.csv",
        "protocol_runtime_comm_compare.md",
        "protocol_runtime_comm_detail.csv",
        "protocol_runtime_comm_detail.md",
        "protocol_runtime_comm_report.md",
    ]:
        path = out_dir / name
        if path.exists():
            path.unlink()


def main() -> int:
    args = parse_args()
    summary_csv = args.summary_csv.resolve()
    out_dir = args.out_dir or summary_csv.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    remove_old_tables(out_dir)

    rows = read_summary(summary_csv)
    details = detail_rows(rows)
    comparisons = compare_rows(details)

    write_csv(out_dir / "summary.csv", DETAIL_COLUMNS, details)
    write_csv(out_dir / "summary_compare.csv", COMPARE_COLUMNS, comparisons)
    print(out_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
