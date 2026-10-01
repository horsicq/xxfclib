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
│       ├── scan/                       # Common scanning interface and options
│       └── io/ data/ buf/ ...          # One library-level header each
├── src/
│   ├── formats/                        # Native format readers and extractors
│   │   ├── xx_format.c                 # File-type detection and reader dispatch
│   │   ├── xx_memory_map.c             # Offset/address mapping shared by readers
│   │   ├── xx_data_signature.c         # Signature notation and matching
│   │   └── <name>/xx_<name>.c          # zip, tar, rar, 7zip, lha, cab, iso, ...
│   ├── algo/                           # Native codecs (deflate, lzma, lzh, dcl, ...)
│   ├── die_engine/                     # Detect-It-Easy scan engine and its script API
│   ├── scan/                           # Callback-based interface for scanning engines
│   ├── io/                             # File, memory and multi-volume I/O devices
│   ├── platforms/                      # Windows / POSIX I/O backends
│   ├── data/                           # Binary data, search, xx_pd
│   ├── buf/                            # Byte buffers
│   ├── memory/                         # Allocation
│   ├── strings/                        # String functions
│   ├── rt/                             # CRT-free runtime (memcpy, memset, snprintf, ...)
│   ├── terminal/                       # Console output and platform mode restoration
│   ├── settings/                       # Typed INI and current-user native settings
│   ├── fs/                             # Paths and directory listing
│   ├── var/ list/ tree/                # Variant values, lists and rooted trees
│   ├── json/ xml/                      # JSON and XML
│   ├── js/                             # Script engine used by die_engine
│   └── global/                         # Buffer sizes, CPU/terminal features and settings pointer
├── samples/
│   ├── console/                        # xxfc_dump: structure and metadata dumper
│   └── unpack/                         # xxfc_unpack: 7-Zip-style archive unpacker
├── tests/                              # Linkage and verification tests
└── packaging/
    └── windows/
        └── build_portable_windows.cmd  # Packaging script for Windows
```

## Generic tree

`<xxfclib/tree/xx_tree.h>` provides an ordered, rooted tree of fixed-size
values alongside `xx_list`. Create it with `xx_tree_create(sizeof(value),
optional_destructor)`, add a root and children with `xx_tree_set_root` and
`xx_tree_append_child`, and release it with `xx_tree_destroy`. Nodes expose
parent/child/sibling navigation; `xx_tree_foreach` visits them in preorder.
`xx_tree_move` reparents a subtree without changing node pointers, and
`xx_tree_remove` destroys a subtree in postorder. Deep trees are traversed and
freed iteratively. Stack-allocated trees use `xx_tree_init` and
`xx_tree_cleanup`.

## Formats and algorithms subset

Standalone consumers can include `xxformats.cmake` to build the format readers,
extractors and algorithms with their required I/O, memory, data and runtime
support. It excludes `die_engine`, its legacy parsers, the JavaScript
interpreter and the bundled Capstone decoder. Add `XXFORMATS_SOURCES` to a
static target, define `XXFC_STATIC` publicly, add `XXFORMATS_INCLUDE_DIR`
publicly and `XXFORMATS_INCLUDE_DIRS` privately, and link
`XXFORMATS_LIBRARIES`. XFileUnpacker uses this subset.

## Disassembler dependency

The DIE script API embeds the vendored Capstone x86 decoder from the sibling
XCapstone tree. CMake defaults `XXFC_CAPSTONE_ROOT` to
`../XCapstone/3rdparty/Capstone/src`; set that cache path to another compatible
Capstone source tree when using a different layout. `XXFC_CAPSTONE_X86_REDUCE`
defaults to `ON` to match diec's reduced x86 configuration. Capstone objects are
included directly in xxfclib, so consumers do not link a separate archive.

## Terminal output

`xxfclib/terminal/xx_terminal.h` provides stdout/stderr output without format
string interpretation, console initialization and cleanup, and raw Windows
console attributes. The Windows implementation uses Win32 directly and does
not require the CRT. ANSI sequences and RGB/name conversion belong to the
caller; XOptions implements those color rules in `XColorString`.

`xx_get_terminal_type()` detects stdout capability once, alongside CPU features
in `xx_global_init_features_once()`: `XX_TERMINAL_TYPE_NONE` for plain or
redirected output, `XX_TERMINAL_TYPE_ANSI`, or `XX_TERMINAL_TYPE_WINDOWS` for
native Windows colors. Stderr is checked independently when initialized.
`xx_set_terminal_type()` overrides stdout's preference; initialization still
keeps redirected output plain. `xx_set_color_output_enabled(false)` disables
all XOptions color output, including an already initialized console state.
The default color setting is enabled.

```c
xx_terminal_state terminal = xx_terminal_init(XX_TERMINAL_STDOUT);
xx_terminal_print(&terminal, "Progress: 100%\n");
xx_terminal_flush(&terminal);
xx_terminal_finish(&terminal);
```

Consumers needing only these services can compile the source list from
`xxterminal.cmake` with `XXFC_STATIC` and `XXTERMINAL_INCLUDE_DIR`, or include
`xxterminal.pri` in qmake. These compile the terminal and global modules
without the format readers or codecs. Consumers linking the full library
already have these symbols and should use that library's global state.

## Settings

`xxfclib/settings/xx_settings.h` provides memory, INI and native stores. Values
have explicit enum types: boolean, signed/unsigned 64-bit integer, double,
UTF-8 string, bytes, string list and opaque consumer data. `xx_settings_set()`
copies its value; `xx_settings_get()` returns a borrowed value until the next
store mutation. Strings and bytes have explicit lengths, including embedded
nulls. String-list items are null-terminated UTF-8 strings.

The optional `xxfclib/global/xx_settings_global.h` module declares the global
`xx_get_settings()` pointer, which starts at `NULL` (settings disabled).
Attach a store with `xx_set_settings()`. Loading/saving a null store succeeds
without I/O; destroying an attached store resets the global pointer. No store
is created by CPU/terminal feature initialization. Creation does not load,
and destruction does not save. Store ownership remains with the caller.
The pointer implementation lives in `src/global/xx_settings_global.c` and is
included by the settings source lists. `xx_global.h`/`xx_global.c` and the
terminal-only source lists do not depend on settings.

```c
xx_settings *settings = xx_settings_create_ini("config/application.ini");
if (settings && xx_settings_load(settings) == XXFC_OK) {
    xx_settings_value value = {0};
    value.type = XX_SETTINGS_VALUE_BOOL;
    value.data.boolean = true;
    xx_set_settings(settings);
    if (xx_settings_set(settings, "view/dark", &value) == XXFC_OK) {
        xxfc_status_t status = xx_settings_save(settings);
        /* Handle status before discarding the store. */
        (void)status;
    }
}
xx_settings_destroy(settings);
```

`xx_settings_create_native(organization, application)` uses HKCU
`Software\\organization\\application` on Windows, the CFPreferences domain
`organization.application` on macOS, and
`$XDG_CONFIG_HOME/organization/application.conf` on Linux/FreeBSD. A missing or
relative XDG_CONFIG_HOME falls back to `$HOME/.config`. These are current-user
settings. Native Windows strings use UTF-16; C paths and names use UTF-8.
macOS maps slash-separated groups to native dot-separated preference keys,
escaping literal dots/middle dots to retain distinct key names.
The Windows backend resolves registry APIs from System32 dynamically to retain
the existing CRT-free, kernel32-only import constraint. Windows registry keys
and value names follow the registry's case-insensitive naming rules.

INI saves merge changed keys into the current file and replace it atomically.
Loads are transactional: malformed input preserves the current snapshot.
Native saves update only changed keys; a failed registry/preferences save can
have applied earlier keys, and retains dirty values for retry. `clear()` removes
keys present in the current snapshot. Concurrent access needs external
synchronization; load-merge-replace does not lock against simultaneous writers.
Files/encoded values are capped at 16 MiB. INI values use `@XX...` markers for
types and quoting/escaping for text; text beginning with `@` is escaped.

For standalone consumers, `xxsettings.cmake` supplies `XXSETTINGS_SOURCES`,
`XXSETTINGS_INCLUDE_DIR` and `XXSETTINGS_LIBRARIES`; define `XXFC_STATIC` on the
consumer. qmake consumers can include `xxsettings.pri`. Remove duplicate common
sources when combining the terminal/settings lists, as XOptions does.

Configure `tests/settings` for the focused C tests, including Windows CRT-free
linkage. Native tests create and remove isolated test application settings.

`xxfclib/settings/xx_shortcuts.h` loads named keyboard bindings from
`shortcuts.ini` using the same INI reader. `xx_shortcuts_load(path, defaults,
count, &list)` copies defaults and applies `[shortcuts]` overrides. A null path
uses `shortcuts.ini` in the current directory; missing files preserve defaults.
Empty values disable bindings. Additional action names are retained for the
consumer to map, and unrelated INI sections are ignored. The loader never
writes the file. Errors return a null result. Access owned entries with
`xx_shortcuts_count()` / `xx_shortcuts_at()` and release them with
`xx_shortcuts_destroy()`. The module is included in both settings source lists
and supports the CRT-free runtime. Consumers validate actual key combinations.

```ini
[shortcuts]
open=Ctrl+O
extract_selected=Ctrl+Shift+E
refresh=F5
```

Additional archive, disk-image and filesystem readers, explicit reader selection, and supported subsets: [native format expansion](docs/additional_native_formats.md).

ZIP extraction and creation methods, supported profiles, limits and focused tests:
[ZIP method support](docs/zip_methods.md).
The latest native listing/extraction work, reference projects, independent
fixtures and remaining variant gaps are documented in
[native reader expansion](docs/native_expansion_20260929.md).
