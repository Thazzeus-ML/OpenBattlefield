# Benchmark workload configurations

Each `.cfg` file holds `key=value` lines using the same keys as the `ob_bench` command line
(`preset`, `chunk-size`, `seed`, `iterations`, `warmup`, `ops`, `radius`, `region`, `workload`).
Command-line options override file values. Presets (`smoke`, `default`, `large`) define the
per-workload iteration counts and sizes; see `PresetDefaults` in `Benchmarks/Core/ObBench.cpp`.

Never compare JSON results produced with different workload files or presets as if they were an
A/B comparison. `Tools/bench_compare.py` refuses to compare runs whose parameters differ.
