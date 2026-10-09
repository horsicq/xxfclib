# Focused emulator builds

The default `XXFC_BUILD_PROFILE=full` keeps the complete xxfclib build.
Select `apk_emul` to build the explicit source manifest in
`xxfclib_apk_emul.cmake`, without the format catalog, automatic detection
registry, DIE engine, or unrelated archive codecs.

```sh
cmake -S path/to/xxfclib -B build/xxfclib-apk -DXXFC_BUILD_PROFILE=apk_emul
cmake --build build/xxfclib-apk --config Release
```

For an embedded consumer:

```cmake
set(XXFC_BUILD_PROFILE apk_emul CACHE STRING "xxfclib source profile")
add_subdirectory(path/to/xxfclib "${CMAKE_CURRENT_BINARY_DIR}/xxfclib")
target_link_libraries(my_apk_loader PRIVATE xxfclib::xxfclib)
```

The profile provides a hosted, static C11 library with the usual include path,
`XXFC_STATIC` definition and platform dependencies. It supports direct ZIP,
APK and DEX reader APIs, binary Android manifest decoding, and ZIP stored or
deflate member extraction to caller-owned devices, with CRC and size checks.
It includes the core memory, I/O and metadata helpers those readers require.

The reusable readers are declared in `xxfclib/formats/apk/xx_apk.h` and
`xxfclib/formats/dex/xx_dex.h`. After `xx_apk_analyze`, the package name and
fully qualified launcher activity are available through
`xx_apk_get_package_name` and `xx_apk_get_launcher_activity`.
Metadata requires a complete manifest element tree; incomplete legacy text
inspection may still succeed with empty metadata queries.
`xx_apk_analyze_dex` builds a numeric inventory of root `classes*.dex` members;
`xx_apk_read_dex` extracts a selected member with a caller-supplied size limit.
Release extracted bytes with `xx_mem_free`. Inventory names and manifest
metadata are borrowed from the APK object and remain valid until destruction.

After `xx_dex_handle_base_info`, `xx_dex_validate_tables` checks the fixed
table extents. The `xx_dex_read_*` functions read string, type, prototype,
field, method, class, type-list and code-item metadata. String readers validate
MUTF-8 and its declared UTF-16 length within the DEX data section, using a
caller-supplied byte limit or buffer. These readers preserve the device
position, honor the DEX base offset and fixed-width byte order, and reject
out-of-range references. They do not verify executable bytecode or exception
handler semantics.

Archive writing, filesystem extraction, encrypted members, other compression
methods, automatic format detection and extractor registration require
`full`. The focused ZIP writer and filesystem extraction APIs fail explicitly;
their vtable writer callbacks are absent. Use `full` for shared, dual-library
or CRT-free builds and the complete xxfclib test suite.

For managed PE/CLI consumers, select `XXFC_BUILD_PROFILE=dotnet_emul`.
Its explicit source list is in `xxfclib_dotnet_emul.cmake`, using the same
hosted static infrastructure as the APK profile. It provides `xx_pe` and
`xx_dotnet` readers, metadata table inspection and PE stream helpers without
the full registry or unrelated formats. It is used by `XDotNetEmul`.

Consumers needing both families, including `xxdecompiler`, can select
`XXFC_BUILD_PROFILE=bytecode`. `xxfclib_bytecode.cmake` combines the APK/DEX
and PE/CLI manifests with one shared core (62 C sources on Windows).
