# Packed metadata compatibility

`Scripts.loadDataPack(filename[, options])` reads TJS/ns0 and TJS/4s0 data packs.
It is available as a native Scripts extension without loading a Windows DLL.
Supported values are void, null, UTF-16 strings, octets, signed 64-bit integers,
binary64 reals, arrays and dictionaries. Integer/header byte order may be little
or big endian; string code units remain UTF-16LE. Data is never evaluated as TJS.

The container reader handles stored or framed LZ4 payloads, checks the value-tag
sequence and final checksum, and requires complete input consumption. The six
ChaCha-derived cipher modes and optional `outeriv` string/octet parameter are
implemented. String IVs use UTF-8. These format checks are not cryptographic
authentication. File/output/allocation sizes, value counts and nesting are
bounded. Unsupported formats and malformed inputs raise an exception.

This interface is a reader. DataPack writing, thumbnail containers and digest
generation are not advertised; callers can continue using their ordinary
structured-save fallback. BMP/TLG saving and PSB resources are separate APIs.

Wire-format facts were cross-checked against the primary implementations in
[AetherKiri's inspector](https://github.com/AetherKiri/AetherKiri/blob/8d0dd68e4cc10424e86a719f477929373d98b23e/tools/inspect_tjs_ns0.py),
[Kirakira's ns0 decoder](https://github.com/liulifox233/Kirakira/blob/c451918023477620ff9055a2eb8a6714b947d9c1/crates/krkr-tjs2/src/runtime/tjs_ns0.rs)
and its [container notes](https://github.com/liulifox233/Kirakira/blob/c451918023477620ff9055a2eb8a6714b947d9c1/crates/krkr-plugins/src/packinone.rs).
This implementation uses its own bounded value tree and the engine's existing
BLAKE2s primitive. The synthetic fixture generator uses Python's hashlib for
the keyed hash so native and generator hash implementations are independent.

Native sanitizer tests cover every cipher mode across multiple batches/frames,
both byte orders, octets, signed values, external IVs, truncation, forged counts,
invalid tags/checksums and LZ4 overlap/history. Android fixtures additionally
exercise conversion to actual TJS values, dialog result mappings and menu icon
properties. Fixtures contain only generated data. All compilation runs in GitHub
Actions.
