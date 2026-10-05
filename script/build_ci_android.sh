#!/usr/bin/env bash
set -euo pipefail
ci_root="$(cd "$(dirname "$0")/.." && pwd)"
ci_keys="$RUNNER_TEMP/krkr-signing"
mkdir -p "$ci_keys" "$ci_root/test-results"
python3 "$ci_root/script/create_xp3_fixtures.py" "$ci_root/tests/fixtures/archives"
python3 "$ci_root/script/create_psb_fixtures.py" "$ci_root/tests/fixtures/psb"
python3 "$ci_root/script/create_datapack_fixtures.py" "$ci_root/tests/fixtures/datapack"
ffmpeg -nostdin -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=64x64:rate=10:duration=2 \
  -an -c:v mpeg4 -pix_fmt yuv420p -threads 1 "$ci_root/tests/fixtures/engine/test.avi"
ffmpeg -nostdin -hide_banner -loglevel error -y -f lavfi -i sine=frequency=440:sample_rate=44100:duration=1 \
  -c:a pcm_s16le "$ci_root/tests/fixtures/engine/tone.wav"
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
./gradlew :krkr2yuri:assembleDebug :krkr2yuri:assembleDebugAndroidTest --no-daemon --max-workers=2
./gradlew -PCI_TEST_BUILD_TYPE=release :krkr2yuri:assembleRelease :krkr2yuri:assembleReleaseAndroidTest --no-daemon --max-workers=2
cd "$ci_root"
python3 tests/android/check_apk.py build_android/outputs/apk/debug/*.apk build_android/outputs/apk/release/*.apk

apksigner="$ANDROID_HOME/build-tools/33.0.2/apksigner"
mkdir -p build_android/outputs/apk/ci-release
release_apk="$(find build_android/outputs/apk/release -name '*.apk' -print -quit)"
"$apksigner" sign --ks "$CI_KEYSTORE_PATH" --ks-type PKCS12 --ks-pass pass:android \
  --key-pass pass:android --ks-key-alias androiddebugkey \
  --out build_android/outputs/apk/ci-release/krkr2yuri-ci-release.apk "$release_apk"
"$apksigner" verify --print-certs build_android/outputs/apk/ci-release/*.apk | tee test-results/ci-release-signing.txt
# Each test APK is built against its target variant, including R8's release
# mapping and dependencies, and must share the target app's CI certificate.
for variant in debug release; do
  test_apk="$(find "build_android/outputs/apk/androidTest/$variant" -name '*.apk' -print -quit)"
  test -n "$test_apk"
  "$apksigner" sign --ks "$CI_KEYSTORE_PATH" --ks-type PKCS12 --ks-pass pass:android \
    --key-pass pass:android --ks-key-alias androiddebugkey "$test_apk"
  "$apksigner" verify "$test_apk"
done
if [[ -n "${CI_DEBUG_KEYSTORE_B64:-}" ]]; then
  grep -F "$(cat tests/android/ci-signing-cert.sha256)" test-results/ci-release-signing.txt
fi
if [[ "$GITHUB_REF" == refs/tags/v* ]]; then "$apksigner" verify "$release_apk"; fi
