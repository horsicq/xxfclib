# xxfclib

A portable C11 library for binary analysis and file handling. It includes
format readers, archive extraction, compression, hashes, file I/O, and the
Detect It Easy signature engine.

Build with CMake 3.14+ and a sibling cdisasm checkout. Link
`xxfclib::xxfclib` or use `xxfclib::die_engine` for signature scanning.
Select `XXFC_BUILD_PROFILE` to build a subset of the library.

```sh
cmake -S . -B build
cmake --build build --config Release
```

See the [public headers](include/xxfclib/). License: [MIT](LICENSE).
Bundled components retain their own notices.
