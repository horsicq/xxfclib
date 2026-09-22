# xxfclib

A lightweight, portable, and safe **C library** template for data handling and binary buffer manipulation, supporting both **static** and **dynamic / shared (DLL)** builds.

## Project Layout

```
xxfclib/
├── CMakeLists.txt                      # Main CMake project configuration
├── README.md                           # Project documentation
├── release_version.txt                 # Semantic release version
├── include/
│   └── xxfclib/                        # Mirrors src/, folder for folder
│       ├── xxfclib.h                   # Umbrella header
│       ├── xxfc_defs.h                 # Status codes, macros, XXFC_API export/import
│       ├── formats/                    # One header per reader, in <name>/
│       ├── algo/                       # One header per codec, in <name>/
│       ├── die_engine/                 # die_engine.h
│       └── io/ data/ buf/ ...          # One library-level header each
├── src/
│   ├── formats/                        # One directory per format, 405 compiled
│   │   ├── xx_format.c                 # File-type detection and reader dispatch
│   │   ├── xx_memory_map.c             # Offset/address mapping shared by readers
│   │   ├── xx_data_signature.c         # Signature notation and matching
│   │   └── <name>/xx_<name>.c          # zip, tar, rar, 7zip, lha, cab, iso, ...
│   ├── algo/                           # 116 codec modules (deflate, lzma, lzh, dcl, ...)
│   ├── die_engine/                     # Detect-It-Easy scan engine and its script API
│   ├── io/                             # File, memory and multi-volume I/O devices
│   ├── platforms/                      # Windows / POSIX I/O backends
│   ├── data/                           # Binary data, search, xx_pd
│   ├── buf/                            # Byte buffers
│   ├── memory/                         # Allocation
│   ├── strings/                        # String functions
│   ├── rt/                             # CRT-free runtime (memcpy, memset, snprintf, ...)
│   ├── fs/                             # Paths and directory listing
│   ├── var/ list/                      # Variant values and generic lists
│   ├── json/ xml/                      # JSON and XML
│   ├── js/                             # Script engine used by die_engine
│   └── global/                         # Buffer and file buffer size options
├── samples/
│   ├── console/                        # xxfc_dump: structure and metadata dumper
│   └── unpack/                         # xxfc_unpack: 7-Zip-style archive unpacker
├── tests/                              # Linkage and verification tests
└── packaging/
    └── windows/
        └── build_portable_windows.cmd  # Packaging script for Windows
```
