#!/usr/bin/env python3
"""Compare current wave DSP with an actual git baseline, in GitHub Actions only.

Example: python3 script/benchmark_wave_effects.py --compiler clang++ \
    --baseline ee6ffb0 --output test-results/wave-benchmark.json
Timing is informational; sample compatibility is enforced independently.
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--baseline", default="ee6ffb0")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if os.environ.get("GITHUB_ACTIONS") != "true":
        parser.error("Repository policy permits compilation only in GitHub Actions")
    root = Path(__file__).resolve().parents[1]
    revision = subprocess.check_output(
        ["git", "rev-parse", "--verify", f"{args.baseline}^{{commit}}"], cwd=root, text=True
    ).strip()
    original = subprocess.check_output(
        ["git", "show", f"{revision}:src/plugins/WaveEffectDSP.h"], cwd=root, text=True
    )
    if original.count("namespace krkr {") != 1:
        raise RuntimeError("Baseline wave DSP namespace was not recognized")
    with tempfile.TemporaryDirectory(prefix="krkr-wave-benchmark-") as directory:
        temporary = Path(directory)
        (temporary / "WaveEffectDSP_baseline.h").write_text(
            original.replace("namespace krkr {", "namespace krkr_before {"), encoding="utf-8"
        )
        executable = temporary / "benchmark_wave_effects"
        subprocess.run(
            [args.compiler, "-std=c++14", "-O3", "-DNDEBUG", "-Wall", "-Wextra",
             "-I", str(temporary), "-I", str(root / "src/plugins"),
             "-I", str(root / "src/plugins/thirdparty/dspfilters/include"),
             str(root / "tests/benchmark_wave_effects.cpp"), "-o", str(executable)],
            cwd=root, check=True
        )
        result = json.loads(subprocess.check_output([str(executable)], text=True, timeout=120))
    result["baseline"] = revision
    result["compiler"] = subprocess.check_output([args.compiler, "--version"], text=True).splitlines()[0]
    output = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    print(output, end="")


if __name__ == "__main__":
    main()
