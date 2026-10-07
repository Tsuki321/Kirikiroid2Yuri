This directory contains the unmodified `include/DspFilters/*.h` and
`source/*.cpp` files from Vinnie Falco's DSPFilters, pinned at commit
`acc49170e79a94fcb9c04b8a2116e9f8dffd1c7d`:

https://github.com/vinniefalco/DSPFilters/tree/acc49170e79a94fcb9c04b8a2116e9f8dffd1c7d/shared/DSPFilters

Each source and header retains its original MIT license and copyright notice.
`source/Documentation.cpp` retains upstream's algorithm references and credits.
Only the library is vendored; its demonstration program and GUI dependencies
are excluded.

The engine's `wfTypicalDSP.dll` implementation uses these original filter
designs and parameter metadata. `wfBasicEffect.dll` uses the RBJ biquad for its
graphic equalizer. Neither module loads a Windows DLL.
