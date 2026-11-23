#!/usr/bin/env python3
"""Times how long the compiler takes on the oat compile-time probe.

  compile_time.py                      print the timings
  compile_time.py --record FILE        write the baseline to FILE
  compile_time.py --check FILE         fail if oat is more than 20% slower than FILE

Machines differ, and CI runners differ from run to run, so the number that is
recorded and checked is a ratio: the oat probe's time divided by the time of
the standard-library probe compiled on the same machine in the same run. The
std probe is the yardstick; only oat's side of the ratio changes when oat does.

Each file is compiled several times, alternating between the two, and the
fastest run of each counts, which filters out noise from a busy machine. With
Clang the time is read from -ftime-trace (the compiler's own total); other
compilers fall back to wall-clock time. Pass options that start with a dash
with an equals sign: --opt=-O2.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROBES = {
    "oat": os.path.join(ROOT, "bench", "compile_time", "oat_instantiate.cpp"),
    "std": os.path.join(ROOT, "bench", "compile_time", "std_instantiate.cpp"),
}


def compile_once(compiler, source, opt, tmp, is_clang):
    obj = os.path.join(tmp, "probe.o")
    trace = os.path.join(tmp, "probe.json")
    cmd = [compiler, "-std=c++23", "-fno-exceptions", "-fno-rtti", opt,
           "-I", os.path.join(ROOT, "include"), "-c", source, "-o", obj]
    if is_clang:
        cmd.append("-ftime-trace")
    start = time.perf_counter()
    subprocess.run(cmd, check=True)
    ms = (time.perf_counter() - start) * 1000.0
    if is_clang and os.path.exists(trace):
        with open(trace) as f:
            events = json.load(f)["traceEvents"]
        totals = [e["dur"] for e in events if e.get("name") == "Total ExecuteCompiler"]
        if totals:
            ms = totals[0] / 1000.0
    return ms


def measure(compiler, opt, runs):
    """Fastest time in ms for each probe, with the runs interleaved."""
    is_clang = "clang" in subprocess.run([compiler, "--version"], capture_output=True,
                                         text=True).stdout
    best = {}
    with tempfile.TemporaryDirectory() as tmp:
        for _ in range(runs):
            for name, source in PROBES.items():
                ms = compile_once(compiler, source, opt, tmp, is_clang)
                best[name] = min(best.get(name, ms), ms)
    return best


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--compiler", default=os.environ.get("CXX", "clang++"))
    ap.add_argument("--opt", default="-O0")
    ap.add_argument("--runs", type=int, default=7)
    ap.add_argument("--record", metavar="FILE")
    ap.add_argument("--check", metavar="FILE")
    ap.add_argument("--tolerance", type=float, default=0.20)
    args = ap.parse_args()

    results = measure(args.compiler, args.opt, args.runs)
    ratio = results["oat"] / results["std"]
    for name, source in PROBES.items():
        print(f"{name:4s} {results[name]:8.1f} ms   ({os.path.relpath(source, ROOT)}, {args.opt})")
    print(f"oat / std = {ratio:.4f}")

    if args.record:
        with open(args.record, "w") as f:
            json.dump({"oat_over_std": round(ratio, 4), "oat_ms": round(results["oat"], 1),
                       "std_ms": round(results["std"], 1), "compiler": args.compiler,
                       "opt": args.opt}, f, indent=2)
            f.write("\n")
        print(f"baseline recorded in {args.record}")
    if args.check:
        with open(args.check) as f:
            baseline = json.load(f)["oat_over_std"]
        limit = baseline * (1.0 + args.tolerance)
        print(f"baseline ratio {baseline:.4f}, limit {limit:.4f}")
        if ratio > limit:
            print("FAIL: relative to the std probe, the oat probe is more than "
                  f"{args.tolerance:.0%} slower than the baseline")
            return 1
        print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
