#!/usr/bin/env python3
"""Choose one allowed logical CPU per party, preferring distinct physical cores."""

import argparse
import os
from pathlib import Path
import sys


def physical_core(cpu):
    topology = Path(f"/sys/devices/system/cpu/cpu{cpu}/topology")
    try:
        return (
            (topology / "physical_package_id").read_text().strip(),
            (topology / "core_id").read_text().strip(),
        )
    except OSError:
        return ("logical", str(cpu))


def select_party_cpus(parties):
    if parties < 1:
        raise ValueError("party count must be positive")
    representatives = {}
    for cpu in sorted(os.sched_getaffinity(0)):
        representatives.setdefault(physical_core(cpu), cpu)
    cpus = list(representatives.values())
    if not cpus:
        raise ValueError("no CPUs are available in the current affinity mask")
    return [cpus[party % len(cpus)] for party in range(parties)], len(cpus)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("parties", type=int)
    args = parser.parse_args()
    try:
        assignments, core_count = select_party_cpus(args.parties)
    except ValueError as error:
        parser.error(str(error))
    print(
        f"[cpu-affinity] policy=one_cpu_per_party available_cores={core_count} "
        f"parties={args.parties} shared_cores={'yes' if args.parties > core_count else 'no'}",
        file=sys.stderr,
    )
    print("\n".join(map(str, assignments)))


if __name__ == "__main__":
    main()
