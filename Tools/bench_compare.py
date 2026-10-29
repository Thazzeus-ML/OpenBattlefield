#!/usr/bin/env python3
"""Compare two ob_bench JSON result files.

Usage:
    bench_compare.py BASELINE.json CANDIDATE.json [--threshold 0.10] [--metric median_ns]

Rules enforced:
  * Workload parameters (preset, sizes, seed, chunk size) must match exactly, otherwise the
    comparison is refused: changed workloads are not comparable.
  * State hashes must match for every workload that reports one. A hash mismatch means the
    candidate changed authoritative behaviour and is reported as a correctness failure, not a
    performance regression.
  * Latency metrics are compared as candidate / baseline ratios. A ratio above 1 + threshold is a
    regression. The exit code is non-zero when a regression or a hash mismatch is found.
  * Results measured on different platforms, compilers or build types are compared with a loud
    warning and never treated as a controlled comparison.
"""
import argparse
import json
import sys


def load(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("baseline")
    ap.add_argument("candidate")
    ap.add_argument("--threshold", type=float, default=0.10, help="relative slowdown treated as regression")
    ap.add_argument("--metric", default="median_ns", help="latency field to compare (median_ns, p95_ns, p99_ns)")
    args = ap.parse_args()

    base = load(args.baseline)
    cand = load(args.candidate)

    warnings = []
    for key in ("platform", "arch", "compiler", "compiler_version", "build_type", "cxx_standard"):
        if base.get("build", {}).get(key) != cand.get("build", {}).get(key):
            warnings.append(f"build.{key} differs: {base['build'].get(key)} vs {cand['build'].get(key)}")
    if warnings:
        print("WARNING: these runs are NOT a controlled comparison:")
        for w in warnings:
            print("  -", w)

    base_wl = {w["name"]: w for w in base.get("workloads", [])}
    cand_wl = {w["name"]: w for w in cand.get("workloads", [])}
    common = [n for n in base_wl if n in cand_wl]
    if not common:
        print("no common workloads")
        return 2

    regressions = 0
    hash_failures = 0
    print(f"{'workload':18} {'series':34} {'baseline':>12} {'candidate':>12} {'ratio':>7}")
    for name in common:
        b, c = base_wl[name], cand_wl[name]
        if b.get("params") != c.get("params"):
            print(f"REFUSED: workload '{name}' parameters differ:\n  {b.get('params')}\n  {c.get('params')}")
            return 2
        for hname, hval in b.get("hashes", {}).items():
            if c.get("hashes", {}).get(hname) != hval:
                hash_failures += 1
                print(f"HASH MISMATCH in {name}.{hname}: {hval} vs {c.get('hashes', {}).get(hname)}")
        for series, stats in b.get("latency", {}).items():
            cs = c.get("latency", {}).get(series)
            if not cs or args.metric not in stats or args.metric not in cs:
                continue
            bv, cv = float(stats[args.metric]), float(cs[args.metric])
            ratio = cv / bv if bv > 0 else float("inf")
            flag = ""
            if ratio > 1.0 + args.threshold:
                flag = "  REGRESSION"
                regressions += 1
            elif ratio < 1.0 - args.threshold:
                flag = "  improvement"
            print(f"{name:18} {series:34} {bv:12.0f} {cv:12.0f} {ratio:7.3f}{flag}")

    print()
    print(f"regressions: {regressions}, hash mismatches: {hash_failures}, threshold: {args.threshold:.0%} on {args.metric}")
    return 1 if (regressions or hash_failures) else 0


if __name__ == "__main__":
    sys.exit(main())
