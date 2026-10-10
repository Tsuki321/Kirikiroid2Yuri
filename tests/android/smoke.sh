#!/usr/bin/env bash
set -euo pipefail
mkdir -p test-results/android
collect_diagnostics() {
  timeout 20s adb logcat -d > test-results/android/logcat.txt || true
  timeout 20s adb exec-out screencap -p > test-results/android/screenshot.png || true
  timeout 30s adb exec-out tar -C /data/user/0/com.yuri.kirikiri2 -cf - files/engine-ci files/dump \
    > test-results/android/runtime-files.tar 2>/dev/null || true
  timeout 30s adb pull /data/tombstones test-results/android/tombstones >/dev/null 2>&1 || true
  timeout 30s adb pull /data/user/0/com.yuri.kirikiri2/files/ui-evidence test-results/android/ui-evidence >/dev/null 2>&1 || true
  timeout 20s adb exec-out cat /data/user/0/com.yuri.kirikiri2/files/input-ci/events.txt > test-results/android/input-events.raw || true
}
trap collect_diagnostics EXIT
# adbd disconnects when switching to root, including occasionally returning a
# failing status after the restart already succeeded. Check the resulting UID.
for attempt in 1 2 3 4 5; do
  adb root || true
  if timeout 30s adb wait-for-device && [ "$(adb shell id -u | tr -d '\r')" = 0 ]; then break; fi
  sleep 2
done
test "$(adb shell id -u | tr -d '\r')" = 0
adb shell settings put secure immersive_mode_confirmations confirmed
adb shell getprop > test-results/android/properties.txt
adb shell getconf PAGE_SIZE | tee test-results/android/page-size.txt
adb logcat -c
adb install -r -g "$(find apk -name '*.apk' -print -quit)"
adb install -r -g "$(find test-apk -name '*.apk' -print -quit)"
adb shell dumpsys package com.yuri.kirikiri2 > test-results/android/package.txt
failures=0
profile=${ANDROID_TEST_PROFILE:-full}
excluded=org.tvp.kirikiri2.NativeControlsTest
if [ "$profile" = release ]; then
  # Picker CRUD/rotation remains in the full suite. The native controls test
  # still opens a real document tree, imports it and launches the engine.
  excluded+=,org.tvp.kirikiri2.LibraryUiTest
fi
adb shell am instrument -w -r -e notClass "$excluded" com.yuri.kirikiri2.test/androidx.test.runner.AndroidJUnitRunner \
  | tee test-results/android/instrumentation.txt
if ! grep -E '^OK \([0-9]+ tests?\)' test-results/android/instrumentation.txt; then
  failures=$((failures + 1))
fi
adb shell cat /data/user/0/com.yuri.kirikiri2/files/engine-ci-cases.txt \
  | tr -d '\r' > test-results/android/cases.txt
executed=0
expected=$(wc -l < test-results/android/cases.txt)
test "$expected" -eq 26
if [ "$profile" = release ]; then
  # Exercise both storage paths, cold bytecode startup and archive loading.
  # These fixtures also cover preferences, plugins, PSB, datapack and KAG.
  awk -F '\t' '$1 ~ /^(local|documents|compiled-local|compiled-documents|archive-local|archive-documents)$/' \
    test-results/android/cases.txt > test-results/android/release-cases.txt
  cp test-results/android/release-cases.txt test-results/android/cases.txt
  expected=$(wc -l < test-results/android/cases.txt)
  test "$expected" -eq 6
fi
# adb shell reads stdin. Keep the manifest on another descriptor so starting
# the first activity cannot consume the remaining fixture rows.
while IFS=$'\t' read -r -u 3 name storage output; do
  executed=$((executed + 1))
  adb shell am force-stop com.yuri.kirikiri2
  adb shell rm -f "$output/result.txt"
  if ! adb shell am start -W -n com.yuri.kirikiri2/.MainActivity --es startupPath "$storage" \
      --esa args '-ci-launch=kept,-ci-zero=0,-ci-flag,-ci-empty=,-ci-equals=left=right,-ci-duplicate=old,-ci-duplicate=new'; then
    echo "Cannot start engine fixture: $name" >&2
    failures=$((failures + 1))
    continue
  fi
  pid="$(adb shell pidof com.yuri.kirikiri2 | tr -d '\r' || true)"
  if [[ "$pid" =~ ^[0-9]+$ ]]; then
    adb exec-out cat "/proc/$pid/maps" > "test-results/android/$name-maps.txt" 2>/dev/null || true
  fi
  if [[ "$name" == dialogs-* ]] && ! python3 tests/android/dialogs.py "$name" "$output"; then
    adb exec-out screencap -p > "test-results/android/$name.png"
    adb logcat -d > "test-results/android/$name-logcat.txt"
    echo "Dialog driver failed: $name" >&2
    failures=$((failures + 1))
    continue
  fi
  if [[ "$name" == presentation-* ]] && ! python3 tests/android/presentation.py "$name" "$output" > "test-results/android/$name-display-driver.txt" 2>&1; then
    cat "test-results/android/$name-display-driver.txt"
    adb exec-out screencap -p > "test-results/android/$name.png"
    adb logcat -d > "test-results/android/$name-logcat.txt"
    echo "Display driver failed: $name" >&2
    failures=$((failures + 1))
    continue
  fi
  if [[ "$name" == movie* ]] && ! python3 tests/android/movie.py "$name" "$output" > "test-results/android/$name-display-driver.txt" 2>&1; then
    cat "test-results/android/$name-display-driver.txt"
    adb exec-out screencap -p > "test-results/android/$name.png"
    adb logcat -d > "test-results/android/$name-logcat.txt"
    echo "Movie display driver failed: $name" >&2
    failures=$((failures + 1))
    continue
  fi
  passed=false
  # Document-backed plugin/codec checks can take over two minutes under ARM64
  # translation. Keep a bounded wait while requiring the complete PASS result.
  # Transition fixtures can spend several minutes in ARM64 translation on the
  # 4 KB Android 16 release image. Keep the wait bounded, but do not classify a
  # still-running fixture as failed at the shorter debug-time limit used by the
  # other cases.
  wait_attempts=90
  if [[ "$name" == transitions* ]]; then wait_attempts=180; fi
  for attempt in $(seq 1 "$wait_attempts"); do
    sleep 2
    if adb shell cat "$output/result.txt" > "test-results/android/$name-engine.raw" 2>/dev/null; then
      python3 tests/android/decode_result.py "test-results/android/$name-engine.raw" > "test-results/android/$name-engine.txt"
      cat "test-results/android/$name-engine.txt"
      if grep -q ENGINE_CI_PASS "test-results/android/$name-engine.txt"; then
        passed=true
        if [[ "$name" == compiled-* ]] && ! grep -q 'PASS cold compiled startup preserves bootstrap globals' "test-results/android/$name-engine.txt"; then
          passed=false
        fi
      fi
      break
    fi
    if ! adb shell pidof com.yuri.kirikiri2 > "test-results/android/$name-pid.txt"; then
      echo "Engine exited before writing its result: $name" >&2
      break
    fi
  done
  adb exec-out screencap -p > "test-results/android/$name.png"
  if [[ "$pid" =~ ^[0-9]+$ ]]; then
    adb logcat -d --pid="$pid" > "test-results/android/$name-logcat.txt"
    if [[ "$name" == preferences-* ]]; then
      expected_status='Invalid preference XML'
      if [[ "$name" == preferences-missing-* ]]; then expected_status='Not found'; fi
      if ! grep -q "Game preferences: $expected_status" "test-results/android/$name-logcat.txt"; then
        echo "Preference failure was not reported: $name" >&2
        passed=false
      fi
    fi
    if [[ "$name" == transitions* || "$name" == movie* || "$name" == presentation-* ]]; then
      renderer=software
      if [[ "$name" == *opengl* ]]; then renderer=opengl; fi
      if ! grep -q "Render manager selected: $renderer" "test-results/android/$name-logcat.txt"; then
        echo "Engine did not select the requested renderer: $name / $renderer" >&2
        passed=false
      fi
    fi
  fi
  if adb shell cat "$output/progress.txt" > "test-results/android/$name-progress.raw" 2>/dev/null; then
    python3 tests/android/decode_result.py "test-results/android/$name-progress.raw" \
      > "test-results/android/$name-progress.txt"
  fi
  if [ "$passed" != true ]; then
    echo "Engine fixture failed or timed out: $name" >&2
    failures=$((failures + 1))
    continue
  fi
  # Startup fixtures with no Window can exit normally after saving their
  # result. NativeControlsTest verifies background/resume with an interactive
  # Window and confirms held keys are released without losing that game.
done 3< test-results/android/cases.txt
echo "Engine fixtures executed: $executed/$expected; failures: $failures" \
  | tee test-results/android/engine-summary.txt
test "$executed" -eq "$expected"
adb shell am force-stop com.yuri.kirikiri2
# Report assertions before the instrumentation runner tears down the legacy
# engine activity, whose onDestroy intentionally exits its process.
adb shell am instrument -w -r -e waitForActivitiesToComplete false -e class org.tvp.kirikiri2.NativeControlsTest com.yuri.kirikiri2.test/androidx.test.runner.AndroidJUnitRunner \
  | tee test-results/android/native-controls.txt
if grep -E '^OK \([0-9]+ tests?\)' test-results/android/native-controls.txt; then
  python3 tests/android/library_return.py --profile "$profile" | tee test-results/android/library-return.txt || failures=$((failures + 1))
else
  failures=$((failures + 1))
fi
exit "$failures"
