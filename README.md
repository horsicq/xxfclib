# xxfclib

A portable C11 library for binary analysis and file handling on Windows,
Linux and macOS. Provides file-format readers, archive extraction, compression,
hashes, I/O, strings and the Detect It Easy signature engine.
Format detection does not imply full reading, extraction or writing support.

## Build

See [build instructions](docs/BUILD.md).

## Integration

Add this directory with CMake's `add_subdirectory()` and link
`xxfclib::xxfclib`. Scanner applications can use the focused
`xxfclib::die_engine` target. Public headers are in [include/xxfclib](include/xxfclib/).

Native archive consumers can choose [format profiles](docs/FORMAT_PROFILES.md):
ALL, DEFAULT, SCANENGINEMIN, or individual `XFU_SUPPORT_FORMAT_*` families.

MIT license. See [LICENSE](LICENSE); bundled components retain their own notices.
