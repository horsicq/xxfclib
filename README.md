# xxfclib

A portable C11 library for binary analysis and file handling on Windows,
Linux and macOS. Provides file-format readers, archive extraction, compression,
hashes, I/O, strings and the Detect It Easy signature engine.
Format detection does not imply full reading, extraction or writing support.

## Build

Requires CMake 3.14+ and a C11 compiler. The default build uses the sibling
`cdisasm` source directory; set `XXFC_CDISASM_DIR` for another location.

Run from this directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The default library is static. Use `-DBUILD_SHARED_LIBS=ON` for a shared library
or `-DXXFC_BUILD_BOTH=ON` for both. Enable tests with `-DXXFC_BUILD_TESTS=ON`
and run `ctest --test-dir build -C Release --output-on-failure` after building.

## Integration

Add this directory with CMake's `add_subdirectory()` and link
`xxfclib::xxfclib`. Scanner applications can use the focused
`xxfclib::die_engine` target. Public headers are in [include/xxfclib](include/xxfclib/).

For a smaller build, select `XXFC_BUILD_PROFILE`: `formats`, `apk_emul`,
`dotnet_emul` or `bytecode`; the default is `full`.
See [format layout](docs/FORMAT_LAYOUT.md), [PE streams](docs/PE_STREAMS.md)
and [module documentation](docs/) for details.

MIT license. See [LICENSE](LICENSE); bundled components retain their own notices.
