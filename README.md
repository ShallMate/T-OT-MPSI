# T-OT-MPSI

## Installation

On Ubuntu/Debian x86-64:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git \
  libssl-dev autoconf automake libtool patch python3 iproute2 util-linux
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

The first configuration downloads pinned dependencies from GitHub. For CPUs
without BMI2 or AVX-512F, add `-DOTMPSI_NATIVE_OPTIMIZATIONS=OFF`.

## Reproducing the Experiments

Preview the full matrix:

```bash
./scripts/run_all_benchmark_scripts.sh --dry-run
```

Run all experiments:

```bash
./scripts/run_all_benchmark_scripts.sh \
  --log-root bench_results/reproduction
```

Results are written under `bench_results/reproduction`. To resume an interrupted
run, repeat the same command; completed cases are skipped automatically.
