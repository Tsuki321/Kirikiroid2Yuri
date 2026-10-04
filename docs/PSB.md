# PSB data and image resources

The built-in `psbfile.dll` plugin reads unencrypted PSB v2/v3 dictionaries,
including scene data and PIMG layer metadata. `new PSBFile(path)` and
`new PSBFile().load(path)` expose the decoded dictionary through `root`.
Integers, reals, strings, arrays and dictionaries become ordinary TJS values;
null becomes `void`. UTF-8 strings and keys preserve UTF-16 surrogate pairs.

Resource members produce octets when accessed. Reading metadata does not read
resource payloads. A retained root stays usable after the PSBFile is reloaded or
invalidated. Failed loads preserve its previous contents.

Loading a file registers its lowercase basename as a storage domain. For
example, loading `art/portrait.pimg` makes its `0.tlg` member available through
`psb://portrait.pimg/0.tlg`. These read-only streams use the engine's normal PNG
and TLG decoders. The source can be a local file, an Android document, or an
archive member. Domains follow the basename lookup used by the framework;
loading another file with the same basename replaces that domain's source.

The parser limits metadata to 16 MiB, decoded allocations to 64 MiB, the tree
to one million values and 128 nesting levels, decoded text to 16 MiB, individual
names to 4,096 bytes, and each resource to 256 MiB. It rejects malformed ranges,
invalid UTF-8, missing resource/string references and invalid name tables.
Resource streams recheck file size when opened. Compressed MDF wrappers,
encrypted PSB headers, v1/v4 and nested PSB storage URLs are unsupported and
produce errors. This is a data reader; it does not implement Emote animation.

`script/create_psb_fixtures.py` produces the original synthetic files in
`tests/fixtures/psb`. Host tests cover metadata-only reads, malformed offsets,
truncation, invalid text, shared/nested container expansion and 4,000 deterministic
mutations. Android fixtures exercise TJS conversions, native object lifetimes,
storage lookup, auto-path enumeration, read-only behavior and actual PNG/TLG
pixels through both local and document storage. All compilation and Android
execution run in GitHub Actions.

Format references: [FreeMote](https://github.com/UlyssesWu/FreeMote) and
[psbfile](https://github.com/number201724/psbfile). Fixtures contain no game data.
