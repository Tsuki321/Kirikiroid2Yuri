#!/usr/bin/env bash
set -euo pipefail
mkdir -p test-results/android
adb shell getprop > test-results/android/properties.txt
adb shell getconf PAGE_SIZE | tee test-results/android/page-size.txt
adb logcat -c
trap 'adb logcat -d > test-results/android/logcat.txt; adb exec-out screencap -p > test-results/android/screenshot.png' EXIT
adb install -r -g "$(find apk -name '*.apk' -print -quit)"
adb shell am start -W -n com.yuri.kirikiri2/.MainActivity
for attempt in $(seq 1 15); do
  sleep 2
  adb shell pidof com.yuri.kirikiri2 > test-results/android/pid.txt
done
if adb logcat -d | grep -E 'FATAL EXCEPTION|Fatal signal|Abort message:'; then
  echo 'Application crashed during launch.' >&2
  exit 1
fi
