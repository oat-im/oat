#!/usr/bin/env python3
"""Times how long the compiler takes on the oat compile-time probe.

  compile_time.py                      print the timings
  compile_time.py --record FILE        write the oat timing to FILE as the baseline
  compile_time.py --check FILE         fail if oat is more than 20% slower than FILE

Each file is compiled several times and the fastest run counts, which filters
out noise from a busy machine. With Clang the time is read from -ftime-trace
(the compiler's own total); other compilers fall back to wall-clock time.
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


def compile_ms(compiler, source, opt, runs):
    best = None
    with tempfile.TemporaryDirectory() as tmp:
        obj = os.path.join(tmp, "probe.o")
        cmd = [compiler, "-std=c++23", "-fno-exceptions", "-fno-rtti", opt,
               "-I", os.path.join(ROOT, "include"), "-c", source, "-o", obj]
        is_clang = "clang" in subprocess.run([compiler, "--version"], capture_output=True,
                                             text=True).stdout
        if is_clang:
            cmd.append("-ftime-trace")
        for _ in range(runs):
            start = time.perf_counter()
            subprocess.run(cmd, check=True)
            ms = (time.perf_counter() - start) * 1000.0
            trace = os.path.join(tmp, "probe.json")
            if is_clang and os.path.exists(trace):
                with open(trace) as f:
                    events = json.load(f)["traceEvents"]
                totals = [e["dur"] for e in events if e.get("name") == "Total ExecuteCompiler"]
                if totals:
                    ms = totals[0] / 1000.0
            best = ms if best is None else min(best, ms)
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
    ap.add_argument("--oat-only", action="store_true")
    args = ap.parse_args()

    results = {}
    for name, source in PROBES.items():
        if name != "oat" and (args.oat_only or args.check or args.record):
            continue
        results[name] = compile_ms(args.compiler, source, args.opt, args.runs)
        print(f"{name:4s} {results[name]:8.1f} ms   ({os.path.relpath(source, ROOT)}, {args.opt})")

    if args.record:
        with open(args.record, "w") as f:
            json.dump({"oat_ms": round(results["oat"], 1), "compiler": args.compiler,
                       "opt": args.opt}, f, indent=2)
            f.write("\n")
        print(f"baseline recorded in {args.record}")
    if args.check:
        with open(args.check) as f:
            baseline = json.load(f)["oat_ms"]
        limit = baseline * (1.0 + args.tolerance)
        print(f"baseline {baseline:.1f} ms, limit {limit:.1f} ms")
        if results["oat"] > limit:
            print("FAIL: the oat compile-time probe is more than "
                  f"{args.tolerance:.0%} slower than the baseline")
            return 1
        print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
