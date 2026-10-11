#!/usr/bin/env bash
set -euo pipefail

# The emulator runner starts one device; this wrapper only swaps installed APKs.
if [ "$*" != debug ] && [ "$*" != 'debug release' ]; then
  echo 'Usage: smoke_variants.sh debug [release]' >&2
  exit 2
fi
for path in apk test-apk test-results/android test-results/android-variants; do
  if [ -e "$path" ] || [ -L "$path" ]; then
    echo "Refusing to reuse existing smoke work directory: $path" >&2
    exit 2
  fi
done
mkdir -p apk test-apk test-results/android-variants

run_variant() {
  variant=$ANDROID_TEST_VARIANT
  mapfile -d '' -t app_apks < <(find "apk-variants/$variant/apk" -type f -name '*.apk' -print0)
  mapfile -d '' -t test_apks < <(find "apk-variants/$variant/test-apk" -type f -name '*.apk' -print0)
  test "${#app_apks[@]}" -eq 1
  test "${#test_apks[@]}" -eq 1
  sha256sum "${app_apks[0]}" "${test_apks[0]}" > test-results/android/apk-sha256.txt
  ln -sfnT "$PWD/${app_apks[0]}" apk/selected.apk
  ln -sfnT "$PWD/${test_apks[0]}" test-apk/selected.apk

  collect_package_provenance() {
    timeout 20s adb shell dumpsys package com.yuri.kirikiri2 \
      > test-results/android/final-package.txt 2>&1 || true
    timeout 20s adb shell dumpsys package com.yuri.kirikiri2.test \
      > test-results/android/test-package.txt 2>&1 || true
  }
  trap collect_package_provenance EXIT
  timeout 30s adb wait-for-device
  timeout 20s adb devices -l > test-results/android/device.txt
  packages=$(timeout 30s adb shell pm list packages | tr -d '\r')
  for package in com.yuri.kirikiri2 com.yuri.kirikiri2.test; do
    if grep -Fxq "package:$package" <<< "$packages"; then
      timeout 20s adb shell am force-stop "$package"
      # Reset preferences, files and document grants for both UIDs. smoke.sh's
      # install -g and the instrumentation provider recreate permissions/fixtures.
      result=$(timeout 30s adb shell pm clear "$package" | tr -d '\r')
      printf '%s: %s\n' "$package" "$result" | tee -a test-results/android/reset.txt
      test "$result" = Success
    else
      printf '%s: not installed\n' "$package" | tee -a test-results/android/reset.txt
    fi
  done
  bash tests/android/smoke.sh
}
export -f run_variant

overall_status=0
for variant in "$@"; do
  output="test-results/android-variants/$variant"
  mkdir -p "$output"
  # Persist directly in the upload directory, including if the job is cancelled.
  # All existing smoke drivers write through this path, including EXIT traps.
  ln -sfnT "android-variants/$variant" test-results/android
  printf 'variant=%s\napk_build_run=%s\nworkflow_run=%s\nworkflow_sha=%s\ntest_profile=%s\n' \
    "$variant" "${ANDROID_APK_BUILD_RUN:-unknown}" "${GITHUB_RUN_ID:-local}" \
    "${GITHUB_SHA:-unknown}" "${ANDROID_TEST_PROFILE:-full}" > "$output/provenance.txt"

  # A failed or hung debug suite must still leave time for the release suite.
  # The child shell keeps errexit active while the outer shell captures status.
  set +e
  ANDROID_TEST_VARIANT="$variant" timeout --signal=TERM --kill-after=3m 30m \
    bash -euo pipefail -c run_variant 2>&1 | tee "$output/smoke-console.txt"
  statuses=("${PIPESTATUS[@]}")
  set -e
  status=${statuses[0]}
  printf '%s\n' "$status" > "$output/exit-status.txt"
  printf '%s\n' "${statuses[1]}" > "$output/console-exit-status.txt"
  printf '%s: suite exit %s; console exit %s\n' "$variant" "$status" "${statuses[1]}" \
    | tee -a test-results/android-variants/summary.txt
  if [ "$status" -ne 0 ] || [ "${statuses[1]}" -ne 0 ]; then
    overall_status=1
  fi
done

exit "$overall_status"
