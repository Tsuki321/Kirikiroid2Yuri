#!/usr/bin/env bash
set -euo pipefail
mkdir -p test-results/android
adb root
adb wait-for-device
test "$(adb shell id -u | tr -d '\r')" = 0
adb shell getprop > test-results/android/properties.txt
adb shell getconf PAGE_SIZE | tee test-results/android/page-size.txt
adb logcat -c
trap 'adb logcat -d > test-results/android/logcat.txt; adb exec-out screencap -p > test-results/android/screenshot.png; adb exec-out tar -C /data/user/0/com.yuri.kirikiri2 -cf - files/engine-ci files/dump > test-results/android/runtime-files.tar 2>/dev/null || true' EXIT
adb install -r -g "$(find apk -name '*.apk' -print -quit)"
adb install -r -g "$(find test-apk -name '*.apk' -print -quit)"
adb shell am instrument -w -r com.yuri.kirikiri2.test/androidx.test.runner.AndroidJUnitRunner \
  | tee test-results/android/instrumentation.txt
grep -E '^OK \([0-9]+ tests?\)' test-results/android/instrumentation.txt
adb shell cat /data/user/0/com.yuri.kirikiri2/files/engine-ci-cases.txt \
  | tr -d '\r' > test-results/android/cases.txt
failures=0
executed=0
expected=$(wc -l < test-results/android/cases.txt)
test "$expected" -ge 4
# adb shell reads stdin. Keep the manifest on another descriptor so starting
# the first activity cannot consume the remaining fixture rows.
while IFS=$'\t' read -r -u 3 name storage output; do
  executed=$((executed + 1))
  adb shell am force-stop com.yuri.kirikiri2
  if ! adb shell am start -W -n com.yuri.kirikiri2/.MainActivity --es startupPath "$storage"; then
    echo "Cannot start engine fixture: $name" >&2
    failures=$((failures + 1))
    continue
  fi
  passed=false
  for attempt in $(seq 1 60); do
    sleep 2
    if adb shell cat "$output/result.txt" > "test-results/android/$name-engine.raw" 2>/dev/null; then
      python3 tests/android/decode_result.py "test-results/android/$name-engine.raw" > "test-results/android/$name-engine.txt"
      cat "test-results/android/$name-engine.txt"
      if grep -q ENGINE_CI_PASS "test-results/android/$name-engine.txt"; then passed=true; fi
      break
    fi
    if ! adb shell pidof com.yuri.kirikiri2 > "test-results/android/$name-pid.txt"; then
      echo "Engine exited before writing its result: $name" >&2
      break
    fi
  done
  adb exec-out screencap -p > "test-results/android/$name.png"
  if [ "$passed" != true ]; then
    echo "Engine fixture failed or timed out: $name" >&2
    failures=$((failures + 1))
    continue
  fi
  if before="$(adb shell pidof com.yuri.kirikiri2)"; then
    adb shell input keyevent KEYCODE_HOME
    sleep 2
    adb shell am start -W --activity-reorder-to-front -n com.yuri.kirikiri2/.MainActivity
    sleep 2
    if [ "$(adb shell pidof com.yuri.kirikiri2)" != "$before" ]; then
      echo "Activity did not survive background/resume: $name" >&2
      failures=$((failures + 1))
    fi
  fi
done 3< test-results/android/cases.txt
echo "Engine fixtures executed: $executed/$expected; failures: $failures" \
  | tee test-results/android/engine-summary.txt
test "$executed" -eq "$expected"
exit "$failures"
