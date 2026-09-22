# xxfclib dump console sample

`xxfc_dump` detects a file format, creates the matching xxfclib format object,
and prints selected parser information without changing or extracting the file.

## Build

```console
cmake -S ../.. -B ../../build -DXXFC_BUILD_SAMPLES=ON
cmake --build ../../build --config Release --target xxfc_dump
```

## Commands

```console
xxfc_dump --dump-map <file>
xxfc_dump --dump-data-structs <file>
xxfc_dump --dump-resources <file>
xxfc_dump --dump-imports <file>
xxfc_dump --dump-exports <file>
xxfc_dump --dump-archive-records <file>
```

`--dump-structs` is a short alias for `--dump-data-structs`.

Memory maps and data structures use the common `Abstractformat` callbacks.
Archive records include names, offsets, sizes, compression metadata, flags,
and encryption state. The common import/export/resource interface supplies
counts. For PE files the sample also safely walks the corresponding directory
to list imported DLLs and symbols, exported functions, and resource leaves.
