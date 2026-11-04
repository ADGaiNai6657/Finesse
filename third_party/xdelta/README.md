# Pinned Xdelta source

- Repository: https://github.com/jmacd/xdelta
- Revision: `5d936d2c30f78729375c3ae070248a05bef27bd2` (main, 2026-10-13).
- Source version: 3.2.2 plus the upstream empty-section/null-pointer fix.
- License: Apache 2.0; original text in `xdelta3/LICENSE`.
- `UPSTREAM.csv` records original Git blob IDs and byte lengths; `SOURCE-MANIFEST.csv` records SHA-256 for the 23 unchanged upstream files.

Finesse builds only the static core library via the unchanged upstream CMake project in an EXCLUDE_FROM_ALL subdirectory. CLI armor, LZMA/BLAKE3 downloads and upstream test executables are disabled. The supported aligned-access configuration uses memcpy-based portable reads; upstream algorithm sources are unchanged. Finesse Debug instrumentation also covers this C library.

The approved encoding profile is level 6, Adler32, no secondary compression, using `xd3_encode_memory` / `xd3_decode_memory`. See `docs/15-xdelta-pipeline-proposal.md` and `cmake/Xdelta.cmake` in the parent project.
