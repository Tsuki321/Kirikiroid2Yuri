"""Fetch the pinned engine dependencies and assets used by GitHub Actions."""
import argparse
import hashlib
import json
import pathlib
import shutil
import tarfile
import tempfile
import time
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
DESTINATIONS = {"build": "thirdparty/build", "source": "thirdparty/port", "assets": "assets"}


def native_input_digest():
    digest = hashlib.sha256()
    paths = list((ROOT / "thirdparty/patch").rglob("*"))
    paths += [ROOT / "script/_androida64.sh", ROOT / "script/_fetch.sh", ROOT / "script/cross_androida64.sh"]
    for path in sorted((p for p in paths if p.is_file()), key=lambda p: p.relative_to(ROOT).as_posix()):
        digest.update(path.relative_to(ROOT).as_posix().encode())
        digest.update(b"\0")
        digest.update(path.read_bytes().replace(b"\r\n", b"\n"))
        digest.update(b"\0")
    return digest.hexdigest()


def fetch(kind, lock):
    entry = lock[kind]
    target = ROOT / DESTINATIONS[kind]
    stamp = target / ".ci-dependency-sha256"
    if kind != "assets" and native_input_digest() != lock["native_inputs_sha256"]:
        raise RuntimeError("Native dependency inputs changed. Rebuild the dependency bundles in GitHub Actions and update dependencies.lock.json.")
    if stamp.is_file() and stamp.read_text().strip() == entry["sha256"]:
        print(f"Using verified {kind} cache ({entry['sha256'][:12]})")
        return
    with tempfile.TemporaryDirectory(prefix="krkr-dependencies-") as temporary:
        archive = pathlib.Path(temporary) / "download"
        for attempt in range(4):
            try:
                digest = hashlib.sha256()
                size = 0
                with urllib.request.urlopen(entry["url"], timeout=90) as response, archive.open("wb") as output:
                    while chunk := response.read(1024 * 1024):
                        output.write(chunk)
                        digest.update(chunk)
                        size += len(chunk)
                if size != entry["size"] or digest.hexdigest() != entry["sha256"]:
                    raise ValueError(f"Dependency checksum mismatch: {kind}")
                break
            except (OSError, TimeoutError):
                if attempt == 3:
                    raise
                time.sleep(2 ** attempt)
        if kind == "assets":
            with zipfile.ZipFile(archive) as source:
                for member in source.infolist():
                    path = pathlib.PurePosixPath(member.filename)
                    if not path.parts or path.parts[0] != "assets":
                        continue
                    if path.is_absolute() or ".." in path.parts:
                        raise ValueError("Unsafe asset path")
                    output = ROOT.joinpath(*path.parts)
                    if member.is_dir():
                        output.mkdir(parents=True, exist_ok=True)
                    else:
                        output.parent.mkdir(parents=True, exist_ok=True)
                        with source.open(member) as src, output.open("wb") as dst:
                            shutil.copyfileobj(src, dst)
        else:
            with tarfile.open(archive, "r:gz") as source:
                prefix = pathlib.PurePosixPath(DESTINATIONS[kind]).parts
                for member in source.getmembers():
                    parts = pathlib.PurePosixPath(member.name).parts
                    if parts[:len(prefix)] != prefix and not (member.isdir() and prefix[:len(parts)] == parts):
                        raise ValueError(f"Unexpected bundle path: {member.name}")
                source.extractall(ROOT, filter="data")
        if not target.is_dir():
            raise ValueError(f"Bundle did not contain {DESTINATIONS[kind]}")
        stamp.write_text(entry["sha256"] + "\n", encoding="ascii")
        print(f"Verified and extracted {kind}: {entry['sha256']}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=DESTINATIONS)
    arguments = parser.parse_args()
    fetch(arguments.kind, json.loads((ROOT / "script/dependencies.lock.json").read_text()))
