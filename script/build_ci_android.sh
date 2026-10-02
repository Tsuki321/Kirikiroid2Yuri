#!/usr/bin/env bash
set -euo pipefail
ci_root="$(cd "$(dirname "$0")/.." && pwd)"
ci_keys="$RUNNER_TEMP/krkr-signing"
mkdir -p "$ci_keys" "$ci_root/test-results"
export CI_KEYSTORE_PATH="$ci_keys/development.p12"
if [[ -n "${CI_DEBUG_KEYSTORE_B64:-}" ]]; then
  printf '%s' "$CI_DEBUG_KEYSTORE_B64" | base64 --decode > "$CI_KEYSTORE_PATH"
else
  # Fork PRs cannot read repository secrets. Their disposable key is never released.
  keytool -genkeypair -keystore "$CI_KEYSTORE_PATH" -storetype PKCS12 \
    -storepass android -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 \
    -validity 10000 -dname 'CN=Kirikiroid2 pull request tests'
fi

if [[ -n "${RELEASE_KEYSTORE_B64:-}" ]]; then
  export SIGN_STORE_FILE="$ci_keys/release.jks"
  printf '%s' "$RELEASE_KEYSTORE_B64" | base64 --decode > "$SIGN_STORE_FILE"
elif [[ "$GITHUB_REF" == refs/tags/v* ]]; then
  echo 'Release tags require RELEASE_KEYSTORE_B64 and SIGN_KEY_ALIAS/SIGN_KEY_PASS/SIGN_STORE_PASS.' >&2
  exit 1
fi

export BUILD_VERSION_CODE=$((100000 + GITHUB_RUN_NUMBER))
export BUILD_VERSION_NAME="1.4.0beta-ci.$GITHUB_RUN_NUMBER"
if [[ "$GITHUB_REF" == refs/tags/v* ]]; then export BUILD_VERSION_NAME="${GITHUB_REF_NAME#v}"; fi

cd "$ci_root/project/android"
chmod +x gradlew
./gradlew :krkr2yuri:assembleDebug :krkr2yuri:assembleRelease :krkr2yuri:assembleDebugAndroidTest --no-daemon --max-workers=2
cd "$ci_root"
python3 tests/android/check_apk.py build_android/outputs/apk/debug/*.apk build_android/outputs/apk/release/*.apk

apksigner="$ANDROID_HOME/build-tools/33.0.2/apksigner"
mkdir -p build_android/outputs/apk/ci-release
release_apk="$(find build_android/outputs/apk/release -name '*.apk' -print -quit)"
"$apksigner" sign --ks "$CI_KEYSTORE_PATH" --ks-type PKCS12 --ks-pass pass:android \
  --key-pass pass:android --ks-key-alias androiddebugkey \
  --out build_android/outputs/apk/ci-release/krkr2yuri-ci-release.apk "$release_apk"
"$apksigner" verify --print-certs build_android/outputs/apk/ci-release/*.apk | tee test-results/ci-release-signing.txt
if [[ -n "${CI_DEBUG_KEYSTORE_B64:-}" ]]; then
  grep -F "$(cat tests/android/ci-signing-cert.sha256)" test-results/ci-release-signing.txt
fi
if [[ "$GITHUB_REF" == refs/tags/v* ]]; then "$apksigner" verify "$release_apk"; fi
