#!/usr/bin/env bash
set -euo pipefail
mkdir -p test-results/android
adb shell getprop > test-results/android/properties.txt
adb shell getconf PAGE_SIZE | tee test-results/android/page-size.txt
adb logcat -c
trap 'adb logcat -d > test-results/android/logcat.txt; adb exec-out screencap -p > test-results/android/screenshot.png' EXIT
adb install -r -g "$(find apk -name '*.apk' -print -quit)"
adb install -r -g "$(find test-apk -name '*.apk' -print -quit)"
adb shell am instrument -w -r com.yuri.kirikiri2.test/androidx.test.runner.AndroidJUnitRunner \
  | tee test-results/android/instrumentation.txt
grep -E '^OK \([0-9]+ tests?\)' test-results/android/instrumentation.txt
adb shell run-as com.yuri.kirikiri2 cat files/engine-ci-cases.txt \
  | tr -d '\r' > test-results/android/cases.txt
while IFS=$'\t' read -r name storage output; do
  adb shell am force-stop com.yuri.kirikiri2
  adb shell am start -W -n com.yuri.kirikiri2/.MainActivity --es startupPath "$storage"
  passed=false
  for attempt in $(seq 1 60); do
    sleep 2
    adb shell pidof com.yuri.kirikiri2 > "test-results/android/$name-pid.txt"
    if adb shell run-as com.yuri.kirikiri2 cat "$output/result.txt" > "test-results/android/$name-engine.txt" 2>/dev/null; then
      cat "test-results/android/$name-engine.txt"
      grep -q ENGINE_CI_PASS "test-results/android/$name-engine.txt"
      passed=true
      break
    fi
  done
  if [ "$passed" != true ]; then echo "Engine fixture timed out: $name" >&2; exit 1; fi
done < test-results/android/cases.txt
