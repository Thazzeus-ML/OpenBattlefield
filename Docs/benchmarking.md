# Benchmarking

Benchmarks are first class: every workload is deterministic, checks its own correctness, and emits
machine-readable JSON.

## Running

```bash
cmake --preset release
cmake --build --preset release
./build/release/Benchmarks/Core/ob_bench --config Benchmarks/workloads/default.cfg --out results.json
python3 Tools/bench_compare.py baseline.json results.json --threshold 0.10
```

`ob_bench --list` prints workloads and options. Presets: `smoke` (CI, seconds), `default`
(about half a minute), `large` (minutes).

## Workloads

| Id | Name | What it does | Checks |
|---|---|---|---|
| A | `generation` | Generates a `region`² x 4 block of chunks from the seed | loaded-chunk hash stable across iterations |
| B | `isolated_crater` | One subtraction inside one chunk, then rebuilds dirty chunks | exactly one chunk; oracle match |
| C | `boundary_crater` | One subtraction centred on a corner shared by 8 chunks | 8 chunks modified; identical voxels on a world with a different chunk size |
| D | `artillery` | `ops` overlapping craters/rubble in a ±`region` voxel area, mesh flush every 16 ops | hash stable across iterations; no rejections |
| E | `reconstruction` | Builds D's world, then journal round trip + replay, snapshot round trip, and full compaction (`compact_ns`, compacted journal/snapshot bytes) | replay hash match; snapshot hash match; compacted snapshot hash match |
| F | `stress` | `ops` operations with periodic memory samples, mesh flush every 32 ops | replay hash match; journal complete; no rejections |

The artillery sequence is derived from `Mix64` so every run with the same parameters applies the
same operations.

## Metrics

Per series: count, min, median, mean, p95, p99, max (nanoseconds, nearest-rank percentiles).
Per workload: counts (operations, changed voxels, chunks), estimated world bytes, process RSS and
peak RSS, allocation count and bytes (global `operator new` hook in the harness), CPU time versus wall
time where a whole-iteration figure is reported, serialized sizes, state hashes, check results.

Throughput figures: `operations_per_second` includes the synchronous mesh flush;
`apply_only_operations_per_second` counts only `ApplyOperation`.

## Measurement protocol

* Fixed seed and parameters, recorded in the JSON (`params`). `bench_compare.py` refuses to compare
  runs whose parameters differ.
* Warm-up iterations are excluded; measured iterations are reported in full.
* Build metadata (commit, compiler, version, standard, build type, platform, arch) is embedded.
* Compare only runs from the same machine, compiler and build type. The comparison script prints a
  loud warning otherwise and never calls such a comparison controlled.
* A state-hash mismatch between baseline and candidate is a correctness failure, not a performance
  result.
* Never change workload sizes between compared runs; make a new workload file instead.

## Sample measurement (not a reference machine)

Environment: shared cloud container, 4 vCPU, Linux, GCC 13.3 `-O3`, C++17, commit of the initial
implementation, `default` preset, 32³ chunks. Numbers from a second run with Clang 18 on the same
container were within normal noise and produced identical state hashes. Treat these as orientation
only; the container is not a controlled benchmark host.

| Workload | Series | median | p95 | p99 |
|---|---|---|---|---|
| generation (144 chunks, 4.7 M voxels) | per chunk | 286 µs | 509 µs | 564 µs |
| isolated_crater (r = 6, 548 voxels) | apply | 6.4 µs | 12.6 µs | 29.6 µs |
| isolated_crater | dirty flush (1 chunk remesh) | 1.26 ms | 1.97 ms | 2.04 ms |
| boundary_crater (r = 8, 2109 voxels, 8 chunks) | apply | 22 µs | 40 µs | 72 µs |
| boundary_crater | dirty flush (8 chunk remeshes) | 8.2 ms | 14.3 ms | 16.0 ms |
| artillery (500 ops, r ≤ 8) | apply | 30 µs | 1.02 ms | 1.23 ms |
| artillery | flush per 16 ops | 36.6 ms | 50.2 ms | 54.1 ms |
| stress (10 000 ops) | apply | 13 µs | 529 µs | 1.11 ms |
| stress | flush per 32 ops | 72 ms | 110 ms | 120 ms |

Other figures from the same run: generation ≈ 3450 chunks/s; artillery changed 280 k voxels across
413 chunks; stress loaded 2344 chunks (2203 modified), estimated world memory 78 MB, process peak RSS
162 MB, zero rejections, replay hash matched; reconstruction journal 28 104 B vs snapshot
2 738 861 B, journal replay 131 ms vs snapshot load 23 ms.

### What the data says

* **Applying operations is cheap.** Median apply latency is tens of microseconds; p99 around a
  millisecond for the largest radii. Within a 33 ms server tick the authoritative update is not the
  problem.
* **Whole-chunk remeshing dominates** by two to three orders of magnitude. The reference path
  rebuilds every dirty chunk entirely (naive face culling, border sampled per voxel through
  `SampleMaterial`). The `DirtyRegion` AABB already exists but is not yet used to limit the rebuild.
  This is the first optimization target, and the first candidate for bounded parallelism.
* Apply p95 is much higher than the median in artillery/stress because operations that touch
  unloaded chunks pay for generation (~0.3 ms per chunk) inside `ApplyOperation`.
* Memory grows linearly with modified chunks (dense representation), about 33 KB per chunk.

These observations are measurements of this baseline, not claims about the research hypothesis.
