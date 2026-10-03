# Hxv4 archives

The Android reader supports Hxv4 archives with an offline `.hxidx` companion.
The original XP3 files stay unchanged. Put each companion beside its matching
archive, for example `data.xp3.hxidx` beside `data.xp3`, then select `data.xp3`
in the app. Local storage and granted Android document trees use the same reader.

Generate companions on a desktop using Python and statically recovered parameters:

```powershell
python -m pip install pycryptodome==3.23.0
python script/prepare_hxv4.py "C:\path\to\game" --keys "C:\private\key-material.json" --output "C:\private\companions"
```

The key JSON contains hex strings named `key` (32 bytes), `nonce0` and `nonce1`
(24 bytes each), `params` (22 bytes), and `control` (4096 bytes, actual table
values rather than GARbro's complemented storage). This script consumes keys
already recovered by static analysis; it does not recover keys from arbitrary
executables. `--media` defaults to `xp3hnp`. `--names` accepts an optional UTF-8
file containing one candidate resource name or directory per line.

Preparation authenticates every XChaCha20-Poly1305 Hxv4 table, validates its
records, and checks every decoded file against its stored Adler-32 checksum.
It streams payload reads and writes companions only after all files pass.
One known packer warning record may have deliberately inconsistent sizes; it is
omitted only when it is unprotected and has synthetic ID zero. Other size or
checksum failures stop preparation. A report records counts and companion hashes.

No executable, DLL, or game script runs during preparation. Script constant
strings and text are read as data to recover names. Companions contain per-file
lookup hashes, original XP3 offsets, and expanded XOR filters; they belong with
the private game installation. Generated companions and reports are ignored by Git.

The engine checks the archive size and a BLAKE2s fingerprint of the original
index plus encrypted Hxv4 table before accepting a companion. It also checks
the companion body digest, record bounds, segment ranges, duplicate names and
hashes, and filter fields. These hashes detect mismatches and corruption; they
are not signatures. File content checksums are verified by preparation, not
on every random read by the engine.

The reader decodes compressed and raw segments, partial reads, and multisegment
files using absolute uncompressed offsets. It resolves unrecovered filenames by
their directory and filename hashes and preserves auto-path priority. The real
hashed `startup.tjs` replaces an explicit placeholder startup when present.

Current limits:

- Standalone archives with one populated final index are supported. Empty
  continuation links are allowed; embedded EXE archives, multiple populated
  indices and cross-archive record locators are rejected.
- Supported name hashing is unkeyed BLAKE2s-256 and SipHash-2-4 with a zero key,
  over normalized UTF-16LE names plus the media identifier. Other Hxv4 variants
  need their own validated parameters and tests.
- Dynamically constructed filenames work when their directory is known by the
  script. Directory enumeration contains only statically recovered names and
  placeholders; code depending on a complete original directory listing may
  need an additional name dictionary.
- Reading archives does not supply Windows bootstrap APIs, native plugins,
  fonts, rendering features, or game-specific startup behavior. Passing static
  checksum checks is not proof that a game is playable.

CI uses original synthetic data only. Python tests cover authentication, key
errors, malformed tables, bounded decompression, independent GARbro filter
vectors, checksum failures, and preserving source archives. GCC and Clang run
the native companion parser and stream filter under sanitizers. Android debug
and release fixtures exercise raw/compressed/multisegment archives, empty index
links, dynamic filename lookup, auto paths, a synthetic startup script, and
missing/corrupt/mismatched companions on local storage and document trees.

The Hx/Cx filter reconstruction is based on the MIT-licensed
[GARbro Hx implementation](https://github.com/crskycode/GARbro/blob/master/ArcFormats/KiriKiri/HxCrypt.cs)
and [Cx implementation](https://github.com/crskycode/GARbro/blob/master/ArcFormats/KiriKiri/KiriKiriCx.cs).
Attribution is in `script/hxv4-LICENSE.txt`. BLAKE2s follows
[RFC 7693](https://www.rfc-editor.org/rfc/rfc7693).
