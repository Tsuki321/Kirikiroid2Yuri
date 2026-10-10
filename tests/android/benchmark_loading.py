#!/usr/bin/env python3
"""Compare release APK loading with identical synthetic assets on one emulator.

The newer test APK supplies archive assets, and the checkout supplies the same
startup script for both versions. Instrumentation compiled with the newer R8
mapping is never executed against an older application. Timings are descriptive,
while every trial must complete the archive and cache compatibility assertions.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import statistics
import subprocess
import tempfile
import time
import zipfile

PACKAGE = "com.yuri.kirikiri2"
FILES = f"/data/user/0/{PACKAGE}/files"
FIXTURE_ROOT = Path(__file__).resolve().parents[1] / "fixtures/engine"
REQUIRED_METRICS = {
    "loading_missing_lookup_ms", "loading_incremental_paths_ms",
    "loading_named_scripts_ms", "loading_cached_lookup_ms",
    "loading_hxv4_repeated_miss_ms",
}
RENDER_METRICS = {"render_snapshot_piled_copy_ms", "render_stretch_copy_ms", "render_affine_copy_ms"}


def decode_text(data):
    return data.decode("utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8")


def parse_metrics(data, required_metrics=REQUIRED_METRICS):
    text = decode_text(data)
    lines = text.splitlines()
    if "ENGINE_CI_PASS" not in lines or any(line.startswith("ENGINE_CI_FAIL") for line in lines):
        raise ValueError("Archive fixture did not report ENGINE_CI_PASS")
    result = {}
    for line in lines:
        match = re.fullmatch(r"METRIC ([a-z0-9_]+)=(\d+)(?: [a-z0-9_]+=[a-z0-9_-]+)*", line)
        if match:
            if match[1] in result:
                raise ValueError(f"Duplicate metric: {match[1]}")
            result[match[1]] = int(match[2])
    if not required_metrics <= result.keys():
        raise ValueError(f"Missing benchmark metrics: {sorted(required_metrics - result.keys())}")
    return result


def prepare_fixture(test_apk, payload, case, render_script=None, startup_script=None):
    if not re.fullmatch(r"(?:before|after)-[1-3]", case):
        raise ValueError("Unexpected benchmark case name")
    storage = f"{FILES}/loading-benchmark/{case}"
    output = storage + "-result"
    directory = payload / "loading-benchmark" / case
    directory.mkdir(parents=True)
    (payload / "loading-benchmark" / (case + "-result")).mkdir()
    script = decode_text((startup_script or FIXTURE_ROOT / "archive-startup.tjs").read_bytes())
    if "@@STORAGE@@" not in script or "@@OUTPUT@@" not in script:
        raise ValueError("Archive startup fixture placeholders are missing")
    with zipfile.ZipFile(test_apk) as archive:
        count = 0
        for entry in archive.infolist():
            if not entry.filename.startswith("assets/archives/") or entry.is_dir():
                continue
            name = entry.filename[len("assets/archives/"):]
            if PurePosixPath(name).name != name or not re.fullmatch(r"[A-Za-z0-9_.-]+", name) or name in (".", ".."):
                raise ValueError("Unsafe archive fixture name")
            (directory / name).write_bytes(archive.read(entry))
            count += 1
        required = ("loading.xp3", "loading-patch.xp3", "hxv4-late.xp3", "hxv4-late.xp3.hxidx")
        if count < 21 or not all((directory / name).is_file() for name in required):
            raise ValueError("The newer test APK does not contain the loading fixtures")
    script = script.replace("@@STORAGE@@", storage).replace("@@OUTPUT@@", output)
    if render_script is not None:
        marker = 'ciMessages.add("ENGINE_CI_PASS");'
        if script.count(marker) != 1:
            raise ValueError("Cannot identify the archive fixture's successful completion")
        script = script.replace(marker, 'Scripts.execStorage(ciStorage + "benchmark-rendering.tjs");\n    ' + marker)
        (directory / "benchmark-rendering.tjs").write_bytes(render_script.read_bytes())
    (directory / "startup.tjs").write_text(script, encoding="utf-8")
    # Keep these files in sync with StorageAccessTest's archive fixture setup.
    (directory / "loading-loose").mkdir()
    (directory / "loading-paths/subdir").mkdir(parents=True)
    (directory / "LoadingAncestor/NestedDir").mkdir(parents=True)
    (directory / "loading-paths/subdir/Mixed.TJS").write_text("global.ciLoadingValue = 71;", encoding="utf-8")
    (directory / "LoadingAncestor/NestedDir/CaseScene.TJS").write_text("global.ciLoadingValue = 72;", encoding="utf-8")
    preferences = ("<GlobalPreference><Item key=\"renderer\" value=\"software\"/>"
                   "<Custom key=\"debugwin\" value=\"no\"/><Custom key=\"gpredetect\" value=\"0\"/>"
                   "<Custom key=\"used2d\" value=\"no\"/></GlobalPreference>\n")
    (directory / "Kirikiroid2Preference.xml").write_text(preferences, encoding="utf-8")
    (payload / ".preference").mkdir()
    (payload / ".preference/GlobalPreference.xml").write_text(preferences, encoding="utf-8")
    return storage, output


def apk_in(directory):
    apks = list(directory.rglob("*.apk"))
    if len(apks) != 1:
        raise ValueError(f"Expected exactly one APK in {directory}, found {len(apks)}")
    return apks[0]


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def adb(*args, check=True, timeout=30):
    return subprocess.run(["adb", *map(str, args)], check=check, timeout=timeout,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def capture(output, device_output):
    for filename, command in {
        "logcat.txt": ("logcat", "-d"),
        "screenshot.png": ("exec-out", "screencap", "-p"),
        "progress.raw": ("exec-out", "cat", device_output + "/progress.txt"),
        "package.txt": ("shell", "dumpsys", "package", PACKAGE),
    }.items():
        try:
            result = adb(*command, check=False)
            (output / filename).write_bytes(result.stdout)
            if result.returncode:
                (output / (filename + ".error")).write_bytes(result.stderr)
        except (OSError, subprocess.TimeoutExpired) as error:
            (output / (filename + ".error")).write_text(str(error), encoding="utf-8")


def run_trial(apk, fixtures, label, trial, results, render_script=None, startup_script=None):
    case = f"{label}-{trial}"
    output = results / case
    output.mkdir()
    adb("shell", "am", "force-stop", PACKAGE, check=False)
    # This script has already required the dedicated rooted CI emulator. Each
    # trial gets a clean installation, preferences and empty output directory.
    adb("uninstall", PACKAGE, check=False, timeout=90)
    installed = adb("install", "-g", apk, timeout=120)
    (output / "install.txt").write_bytes(installed.stdout)
    owner = adb("shell", "stat", "-c", "%u:%g", f"/data/user/0/{PACKAGE}").stdout.decode().strip()
    if not re.fullmatch(r"\d+:\d+", owner):
        raise ValueError("Unable to determine app directory ownership")
    with tempfile.TemporaryDirectory(prefix="krkr-loading-") as temporary:
        payload = Path(temporary) / "files"
        payload.mkdir()
        storage, device_output = prepare_fixture(fixtures, payload, case, render_script, startup_script)
        adb("shell", "mkdir", "-p", FILES)
        adb("push", str(payload) + "/.", FILES + "/", timeout=90)
        adb("shell", "chown", "-R", owner, FILES)
        adb("shell", "restorecon", "-RF", FILES)
    adb("logcat", "-c")
    started = time.monotonic()
    try:
        launch = adb("shell", "am", "start", "-W", "-n", PACKAGE + "/.MainActivity",
                     "--es", "startupPath", storage, "--esa", "args",
                     "-debugwin=no,-gpredetect=0,-used2d=no", timeout=45)
        (output / "launch.txt").write_bytes(launch.stdout)
        deadline = started + 240
        while time.monotonic() < deadline:
            result = adb("exec-out", "cat", device_output + "/result.txt", check=False, timeout=10)
            if result.returncode == 0 and result.stdout:
                # Array.save writes directly; a poll can see an incomplete
                # UTF-16 code unit or a prefix before its final status line.
                try:
                    decoded = decode_text(result.stdout)
                except UnicodeError:
                    decoded = ""
                if any(line == "ENGINE_CI_PASS" or line.startswith("ENGINE_CI_FAIL")
                       for line in decoded.splitlines()):
                    (output / "result.raw").write_bytes(result.stdout)
                    (output / "result.txt").write_text(decoded, encoding="utf-8")
                    required = REQUIRED_METRICS | RENDER_METRICS if render_script is not None else REQUIRED_METRICS
                    metrics = parse_metrics(result.stdout, required)
                    metrics["launch_to_result_wall_ms"] = round((time.monotonic() - started) * 1000)
                    record = {"variant": label, "trial": trial, "metrics_ms": metrics}
                    (output / "metrics.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
                    print(json.dumps(record), flush=True)
                    return record
            time.sleep(0.25)
        raise TimeoutError(f"{case}: archive fixture did not finish within 240 seconds")
    finally:
        capture(output, device_output)
        adb("shell", "am", "force-stop", PACKAGE, check=False)


def summarize(trials, required_metrics=REQUIRED_METRICS):
    metrics = required_metrics | {"launch_to_result_wall_ms"}
    report = {}
    for metric in sorted(metrics):
        values = {variant: [trial["metrics_ms"][metric] for trial in trials if trial["variant"] == variant]
                  for variant in ("before", "after")}
        if any(len(samples) != 3 for samples in values.values()):
            raise ValueError("Three completed trials of each APK are required")
        before, after = (statistics.median(values[variant]) for variant in ("before", "after"))
        report[metric] = {"before_trials_ms": values["before"], "after_trials_ms": values["after"],
                          "before_median_ms": before, "after_median_ms": after,
                          "speedup": before / after if after else None}
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before-dir", type=Path, required=True)
    parser.add_argument("--after-dir", type=Path, required=True)
    parser.add_argument("--fixtures-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--render-workload", action="store_true",
                        default=os.environ.get("INCLUDE_RENDER_WORKLOAD") == "true",
                        help="also measure the original 1280x720 zoom workload")
    args = parser.parse_args()
    if os.environ.get("GITHUB_ACTIONS") != "true":
        parser.error("Run this benchmark only on its dedicated GitHub Actions emulator")
    apks = {"before": apk_in(args.before_dir), "after": apk_in(args.after_dir)}
    fixtures = apk_in(args.fixtures_dir)
    startup_script = FIXTURE_ROOT / "archive-startup.tjs"
    if not startup_script.is_file():
        raise ValueError("The checked-out loading workload is missing")
    render_script = FIXTURE_ROOT / "benchmark-rendering.tjs" if args.render_workload else None
    if render_script is not None and not render_script.is_file():
        raise ValueError("The checked-out rendering workload is missing")
    args.output.mkdir(parents=True, exist_ok=True)
    if adb("shell", "getprop", "ro.kernel.qemu").stdout.strip() != b"1":
        raise ValueError("Refusing to reinstall applications on a non-emulator device")
    for _ in range(5):
        adb("root", check=False)
        adb("wait-for-device", timeout=30)
        if adb("shell", "id", "-u").stdout.strip() == b"0":
            break
        time.sleep(2)
    if adb("shell", "id", "-u").stdout.strip() != b"0":
        raise ValueError("The loading benchmark requires rooted adbd")
    if adb("shell", "getprop", "ro.build.version.sdk").stdout.strip() != b"36":
        raise ValueError("The comparison is pinned to API 36")
    if adb("shell", "getconf", "PAGE_SIZE").stdout.strip() != b"4096":
        raise ValueError("The comparison is pinned to 4 KB pages")
    (args.output / "properties.txt").write_bytes(adb("shell", "getprop").stdout)
    adb("shell", "settings", "put", "secure", "immersive_mode_confirmations", "confirmed")
    provenance = {variant: {"run_id": os.environ.get(variant.upper() + "_RUN"),
                            "apk_sha256": sha256(apk)} for variant, apk in apks.items()}
    provenance["fixtures"] = {"run_id": os.environ.get("AFTER_RUN"), "apk_sha256": sha256(fixtures)}
    provenance["loading_workload"] = {"script_sha256": sha256(startup_script)}
    if render_script is not None:
        provenance["render_workload"] = {"script_sha256": sha256(render_script)}
    (args.output / "apk-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    trials = []
    for trial in range(1, 4):
        for variant in ("before", "after"):
            trials.append(run_trial(apks[variant], fixtures, variant, trial, args.output, render_script, startup_script))
    required = REQUIRED_METRICS | RENDER_METRICS if render_script is not None else REQUIRED_METRICS
    report = {"provenance": provenance, "trials": trials, "medians": summarize(trials, required),
              "conditions": "Same API 36 / 4 KB emulator, fresh install per trial, software renderer, debug window disabled",
              "scope": "Synthetic loading, lookup and optional zoom timings; ARM translation and host scheduling affect elapsed time. No timing pass threshold."}
    (args.output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    rows = ["| Metric | Before median (ms) | After median (ms) | Ratio |",
            "| --- | ---: | ---: | ---: |"]
    for metric, values in report["medians"].items():
        ratio = f"{values['speedup']:.2f}x" if values["speedup"] is not None else "n/a"
        rows.append(f"| {metric} | {values['before_median_ms']} | {values['after_median_ms']} | {ratio} |")
    summary = "\n".join(rows) + "\n\n" + report["scope"] + "\n"
    (args.output / "comparison.md").write_text(summary, encoding="utf-8")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as stream:
            stream.write(summary)
    print(summary, flush=True)


if __name__ == "__main__":
    main()
