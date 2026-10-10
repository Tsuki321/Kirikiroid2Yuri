import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("benchmark_loading", ROOT / "tests/android/benchmark_loading.py")
benchmark = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(benchmark)


class LoadingBenchmark(unittest.TestCase):
    def result(self, final="ENGINE_CI_PASS"):
        return "\n".join(["ENGINE_CI_STARTED"] + [f"METRIC {name}=10 lookups=128"
                        for name in sorted(benchmark.REQUIRED_METRICS)] + [final])

    def test_result_requires_all_metrics_and_actual_pass(self):
        for encoding in ("utf-8", "utf-16"):
            self.assertEqual({name: 10 for name in benchmark.REQUIRED_METRICS},
                             benchmark.parse_metrics(self.result().encode(encoding)))
        for data in (self.result("ENGINE_CI_FAIL invalid archive"),
                     "ENGINE_CI_PASS\n", self.result() + "\nENGINE_CI_FAIL later error"):
            with self.assertRaises(ValueError):
                benchmark.parse_metrics(data.encode())
        duplicate = self.result() + "\nMETRIC loading_cached_lookup_ms=3"
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            benchmark.parse_metrics(duplicate.encode())

    def apk(self, path, extra=None):
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("assets/engine/archive-startup.tjs",
                             (ROOT / "tests/fixtures/engine/archive-startup.tjs").read_bytes())
            for name in ["loading.xp3", "loading-patch.xp3", "hxv4-late.xp3", "hxv4-late.xp3.hxidx"]:
                archive.writestr("assets/archives/" + name, b"synthetic placeholder")
            for index in range(20):
                archive.writestr(f"assets/archives/fixture-{index}.xp3", b"synthetic placeholder")
            archive.writestr("classes.dex", b"must never be installed or extracted")
            if extra:
                archive.writestr(extra, b"unexpected path")

    def test_fixture_uses_only_assets_and_preserves_case_sensitive_scenarios(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            apk, payload = root / "test.apk", root / "files"
            self.apk(apk)
            payload.mkdir()
            storage, output = benchmark.prepare_fixture(apk, payload, "before-1")
            fixture = payload / "loading-benchmark/before-1"
            startup = (fixture / "startup.tjs").read_text(encoding="utf-8")
            self.assertIn(storage, startup)
            self.assertIn(output, startup)
            self.assertNotIn("@@STORAGE@@", startup)
            self.assertNotIn("@@OUTPUT@@", startup)
            self.assertEqual("global.ciLoadingValue = 71;",
                             (fixture / "loading-paths/subdir/Mixed.TJS").read_text())
            self.assertTrue((fixture / "LoadingAncestor/NestedDir/CaseScene.TJS").is_file())
            self.assertTrue((fixture / "loading-loose").is_dir())
            self.assertFalse((fixture / "loading-paths/subdir/NewScene.TJS").exists())
            self.assertEqual([], list(payload.rglob("*.dex")))
            self.assertEqual((fixture / "Kirikiroid2Preference.xml").read_bytes(),
                             (payload / ".preference/GlobalPreference.xml").read_bytes())

    def test_fixture_refuses_archive_member_traversal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            apk, payload = root / "test.apk", root / "files"
            self.apk(apk, "assets/archives/../escaped")
            payload.mkdir()
            with self.assertRaisesRegex(ValueError, "Unsafe"):
                benchmark.prepare_fixture(apk, payload, "after-3")
            self.assertFalse((payload / "loading-benchmark/escaped").exists())

    def test_optional_rendering_runs_before_success_and_requires_metrics(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            apk, payload, render = root / "test.apk", root / "files", root / "render.tjs"
            self.apk(apk)
            payload.mkdir()
            render.write_text("ciCheck(true, 'original rendering workload');", encoding="utf-8")
            benchmark.prepare_fixture(apk, payload, "after-2", render)
            fixture = payload / "loading-benchmark/after-2"
            startup = (fixture / "startup.tjs").read_text(encoding="utf-8")
            self.assertLess(startup.index('Scripts.execStorage(ciStorage + "benchmark-rendering.tjs");'),
                            startup.index('ciMessages.add("ENGINE_CI_PASS");'))
            self.assertEqual(render.read_bytes(), (fixture / "benchmark-rendering.tjs").read_bytes())
        required = benchmark.REQUIRED_METRICS | benchmark.RENDER_METRICS
        with self.assertRaises(ValueError):
            benchmark.parse_metrics(self.result().encode(), required)
        text = self.result().replace("ENGINE_CI_PASS", "\n".join(
            [f"METRIC {name}=20 frames=24" for name in sorted(benchmark.RENDER_METRICS)] + ["ENGINE_CI_PASS"]))
        self.assertEqual(20, benchmark.parse_metrics(text.encode(), required)["render_stretch_copy_ms"])

    def test_medians_require_three_passed_trials_of_both_versions(self):
        metrics = benchmark.REQUIRED_METRICS | {"launch_to_result_wall_ms"}
        trials = [{"variant": variant, "trial": index + 1,
                   "metrics_ms": {name: value for name in metrics}}
                  for variant, values in (("before", [20, 400, 10]), ("after", [3, 5, 4]))
                  for index, value in enumerate(values)]
        result = benchmark.summarize(trials)
        self.assertEqual(20, result["loading_named_scripts_ms"]["before_median_ms"])
        self.assertEqual(4, result["loading_named_scripts_ms"]["after_median_ms"])
        self.assertEqual(5, result["loading_named_scripts_ms"]["speedup"])
        with self.assertRaises(ValueError):
            benchmark.summarize(trials[:-1])


if __name__ == "__main__":
    unittest.main()
