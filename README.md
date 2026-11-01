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
│       ├── formats/                    # One header per reader, in <category>/<name>/
│       ├── algo/                       # One header per codec, in <name>/
│       ├── die_engine/                 # die_engine.h
│       ├── scan/                       # Common scanning interface and options
│       └── io/ data/ buf/ ...          # One library-level header each
├── src/
│   ├── formats/                        # Native format readers and extractors
│   │   ├── xx_format.c                 # File-type detection and reader dispatch
│   │   ├── xx_memory_map.c             # Offset/address mapping shared by readers
│   │   ├── xx_data_signature.c         # Signature notation and matching
│   │   └── <category>/<name>/xx_<name>.c  # archive/zip, installer/nsis, filesystem/ntfs, ...
│   │                                   # (categories: docs/FORMAT_LAYOUT.md)
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
│   ├── xfileunpacker/                  # Shared XFileUnpacker archive engine
│   ├── fs/                             # Paths and directory listing
│   ├── var/ list/ tree/                # Variant values, lists and rooted trees
│   ├── json/ xml/                      # JSON and XML
│   ├── js/                             # Script engine used by die_engine
│   ├── regex/                          # Shared regex core and JS/DIE adapters
│   └── global/                         # Buffer sizes, CPU/terminal features and settings pointer
├── samples/
│   ├── console/                        # xxfc_dump: structure and metadata dumper
│   └── unpack/                         # xxfc_unpack: 7-Zip-style archive unpacker
├── tests/                              # Linkage and verification tests
└── packaging/
    └── windows/
        └── build_portable_windows.cmd  # Packaging script for Windows
```

## Regular expressions

`<xxfclib/regex/xx_regex.h>` exposes the reusable regex compiler and matcher.
The implementation and JavaScript/DIE adapters live in [`src/regex`](src/regex/README.md).
The core links without the JavaScript interpreter and supports concurrent
execution with separate capture arrays. See the module notes for byte-string
semantics, reference ownership and focused tests.

## Process memory

`<xxfclib/io/xx_io.h>` exposes process address spaces through `xx_io_device`.
Use `xx_io_process_open_ro(pid)` for inspection or
`xx_io_process_open_ex(pid, XX_IO_PROCESS_READ_WRITE)` for explicit read/write
access. `XX_IO_PROCESS_WRITE` supports write-only access, and PID zero means
the current process. The legacy `xx_io_process_open` and `io_process_open`
remain available with their read/write attempt and read-only fallback.

`xx_io_process_read_at` and `xx_io_process_write_at` transfer bytes at a
`uint64_t` virtual address and return the actual byte count or `-1`. Their
`read_exact_at` and `write_exact_at` variants return `bool` and retry short
transfers. All four preserve the device cursor. The generic stream API also
works: its seek position is an absolute address limited to `INT64_MAX`, and
the device reports an unknown total size. `xx_io_is_process` identifies these
devices. Close them with `xx_io_process_close` or the generic `xx_io_close`.

```c
xx_io_device *memory = xx_io_process_open_ro(pid);
if (memory) {
    bool complete = xx_io_process_read_exact_at(memory, address, bytes, size);
    /* Use bytes only when complete is true; failed reads can be partial. */
    (void)complete;
    xx_io_process_close(memory);
}
```

`xx_io_process_open_handle_ex(native, access)` borrows an existing object:
a Windows `HANDLE`, a Linux `/proc/<pid>/mem` descriptor cast through
`intptr_t` (including descriptor zero), or a macOS Mach task port cast through
`uintptr_t`. Closing the device leaves a borrowed object open. PID constructors
own their native handles. Keep the device and any borrowed handle alive during
concurrent reads; synchronize stream operations and closing.

The native implementation uses Windows process-memory APIs, Linux `pread` /
`pwrite`, and macOS `mach_vm_read_overwrite` / `mach_vm_write`. Explicit access
modes are enforced by the device and OS permissions still apply. Errors use
`errno` on POSIX and `GetLastError` on Windows. Failed transfers can update
part of the buffer or process memory; they do not roll back, suspend the target
or change page protections. Invalid modes, address overflow and writes through
a read-only device fail before transferring bytes.

For a small standalone consumer, include `xxprocessio.cmake`, compile
`XXPROCESSIO_SOURCES`, add `XXPROCESSIO_INCLUDE_DIR`, define `XXFC_STATIC` for
a static target, and link `XXPROCESSIO_LIBRARIES`. This subset supplies the
process-specific APIs without the format engines or filesystem device module;
generic I/O wrappers require the complete I/O module. Its portable source is
`src/io/xx_io_process.c`, with native implementations under
`src/io/platforms/xx_io_process_windows.inc` and `xx_io_process_posix.inc`.

## PE streams

PE32/PE64 readers provide incremental import, export, resource, metadata and symbol
cursors through `Abstractformat` callbacks and the common `xx_format_` API.
See [PE streaming API](docs/PE_STREAMS.md) for ownership, examples, supported
records and focused tests. Rebuild library consumers when using the enlarged
`Abstractformat` structure.

## Unpack handles

`xx_file_type_t` identifies a file or container. `xx_handle_type_t` identifies
an unpacking interpretation and uses independent `XX_HANDLE_TYPE_*` values.
A PE32 image can therefore offer `XX_HANDLE_TYPE_PE_ZIPSFX`, while a compressed
TAR can offer both `XX_HANDLE_TYPE_GZ` and `XX_HANDLE_TYPE_TAR_GZ`.

```c
xx_list_t *handles = xx_format_get_unpack_handle_types_device(device, true);
if (handles) {
    for (size_t i = 0; i < xx_list_count(handles); ++i) {
        xx_handle_type_t handle = *(const xx_handle_type_t *)xx_list_at(handles, i);
        /* Display xx_format_handle_type_to_string(handle), or select a reader. */
    }
    xx_list_destroy(handles);
}
```

Detection checks signature rules and native fixed anchors first, including
all applicable executable SFX/installer readers. It checks filename extensions
only when those probes find no unpack handle. `deep=true` enables structural
fallback when the preceding stages still found none. Profile-dependent raw
imports require explicit selection. Returned lists are deduplicated and owned
by the caller; the device and its original position are preserved.

`Abstractformat.handle_type` records the reader's unpack interpretation. Native
factories and successful common base-info handling populate it, and
`xx_format_get_handle_type()` refreshes it. Readers without the complete archive
streaming callbacks, or whose parsed contents are not unpackable, report
`XX_HANDLE_TYPE_UNKNOWN`. Use
`xx_format_create_handle_reader()` and `xx_format_free_reader()` to manage a
native handle reader; set passwords/options before validating it. Rebuild
consumers after this change to the public structure layout.
`NUMBERED_SPLIT` is handled by the unpacker's multi-file backend and has no
single-device native reader factory.

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

## Threads

`<xxfclib/threads/xx_threads.h>` provides threads (`xx_thread_create`,
`xx_thread_join`, `xx_thread_detach`), recursive mutexes, condition
variables, counting semaphores, `xx_once` and `long`/pointer atomics. All
objects are opaque handles, so the ABI does not depend on platform lock
sizes. Waits take a millisecond timeout or `XX_WAIT_INFINITE` and return
`XX_WAIT_OK`, `XX_WAIT_TIMEOUT` or `XX_WAIT_ERROR`. The Windows backend uses
KERNEL32 only and links in the CRT-free builds; condition variables are
resolved at run time, so on Windows XP `xx_cond_create` returns NULL. Other
platforms use POSIX threads.

`<xxfclib/threads/xx_thread_pool.h>` adds a fixed-size worker pool with a FIFO
task queue. `xx_thread_pool_create(0)` uses the logical CPU count;
`xx_thread_pool_create_ex(workers, max_pending, stack_size)` also sets an
optional queue limit and worker stack size. Submission never waits for queue
space. The pool uses semaphores, so it also works on Windows XP.

```c
#include <xxfclib/threads/xx_thread_pool.h>

static void square(void *arg)
{
    int *value = (int *)arg;
    *value *= *value;
}

/* Keep values alive until the pool has finished using them. */
static bool square_values(int *values, size_t count)
{
    xx_thread_pool_t *pool = xx_thread_pool_create(4);
    if (!pool) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!xx_thread_pool_submit(pool, square, &values[i])) {
            xx_thread_pool_destroy(pool, XX_THREAD_POOL_DRAIN);
            return false;
        }
    }
    return xx_thread_pool_destroy(pool, XX_THREAD_POOL_DRAIN);
}
```

`xx_thread_pool_wait` waits for an idle point with the same timeout/result
conventions as semaphore waits. `xx_thread_pool_get_stats` reports queued and
active tasks, plus an `idle` flag that includes cancellation cleanup.
`xx_thread_pool_submit_ex` accepts an optional cleanup callback,
called once for each accepted task after execution or cancellation; a rejected
submission leaves ownership with the caller. Idle waits include cleanup.
Callbacks can submit tasks and query statistics, but cannot wait for, shut down
or destroy their own pool.

`XX_THREAD_POOL_DRAIN` completes accepted work; `XX_THREAD_POOL_CANCEL_PENDING`
discards queued work and lets active tasks finish. Shutdown stops submissions;
destroy also joins workers and releases the pool. The owner serializes shutdown
calls and prevents other client calls or waits from overlapping destruction.
FIFO controls dequeue order, not completion order across workers.

For the full library, enable `XXFC_BUILD_THREAD_TESTS` to register the threading
tests. A focused build uses the same production sources without the format
readers:

```sh
cmake -S tests/threads -B build/thread-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/thread-tests
ctest --test-dir build/thread-tests --output-on-failure
```

## Formats and algorithms subset

Standalone consumers can include `xxformats.cmake` to build the format readers,
extractors and algorithms with their required I/O, memory, data and runtime
support. It excludes `die_engine`, its legacy parsers, the JavaScript
interpreter and its cdisasm dependency. Add `XXFORMATS_SOURCES` to a
static target, define `XXFC_STATIC` publicly, add `XXFORMATS_INCLUDE_DIR`
publicly and `XXFORMATS_INCLUDE_DIRS` privately, and link
`XXFORMATS_LIBRARIES`. XFileUnpacker uses this subset.

`cmake/xxfclib_xfileunpacker.cmake` adds the optional
`xxfclib::xfileunpacker_core` component. Its build-only engine headers,
implementation and source-built FreeArc SFX stub are under
`src/xfileunpacker`. The component links only the local
formats-profile library and platform libraries, and compiles all runtime
helper paths out with `XXFC_NO_EXTERNAL_HELPERS`.

## Focused DIE engine archive writing

`XXFC_DIE_ENGINE_TAR_ONLY_WRITING=ON` registers only plain TAR creation in
`xxfclib::die_engine`. Other supported archives remain readable and extractable.
This lets scanner consumers discard compression encoders while retaining source
and precompiled TAR signature databases. The option defaults to `OFF`; the full
xxfclib targets keep their normal archive-writing support.

## Disassembler dependency

The DIE script API uses the sibling `cdisasm` source tree for x86 decoding and
Intel text formatting. CMake defaults `XXFC_CDISASM_DIR` to `../cdisasm`; set
that cache path when using a different source layout. It builds cdisasm as a
static dependency of xxfclib, so CDiE executables need no separate decoder
DLL. CMake propagates the decoder archive to static xxfclib consumers;
manual linker commands must include cdisasm after xxfclib.
`cdisasm` is MIT licensed; its generated-data notices and license texts are in
its source tree.

## Terminal output

`xxfclib/terminal/xx_terminal.h` provides stdout/stderr output without format
string interpretation, console initialization and cleanup, and raw Windows
console attributes. The Windows implementation uses Win32 directly and does
not require the CRT. ANSI sequences and RGB/name conversion belong to the
caller; XOptions implements those color rules in `XColorString`.

The same module owns the common JSON and XML string writers used by the
console tools. `xx_terminal_writer` accepts a byte-sink callback, keeps the
first output error, validates shortest-form UTF-8, escapes JSON controls, and
filters characters forbidden by XML 1.0. Separate XML text and attribute
functions preserve attribute whitespace correctly. The callback design works
with standard streams, temporary staging files, memory buffers, and CRT-free
consumers without exposing a `FILE *` in the library ABI.

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

INI saves patch only the changed keys' lines of the current file (comments,
order, line endings and values xx cannot represent survive) and replace it
atomically.
Loads are transactional: malformed input preserves the current snapshot.
Native saves update only changed keys; a failed registry/preferences save can
have applied earlier keys, and retains dirty values for retry. `clear()` removes
keys present in the current snapshot. Concurrent access needs external
synchronization; load-merge-replace does not lock against simultaneous writers.
Files/encoded values are capped at 16 MiB. INI values use `@XX...` markers for
types and quoting/escaping for text; text beginning with `@` is escaped.
Header-less INI files are read with QSettings' escaping rules, and
`xx_settings_set_qt_compatible()` also writes the QSettings dialect so a file
can be shared with a Qt program; see `docs/settings.md`.

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

### ARM PMULL CRC acceleration

CRC32 (IEEE) and CRC32C use a three-stream CRC32 + PMULL backend for buffers
of at least 3072 bytes when both `xx_is_arm_crc32_enabled()` and
`xx_is_arm_pmull_enabled()` are true. Smaller buffers retain their existing
backend. MPEG-2 CRC and CRC64 are unchanged.

`xx_has_arm_pmull()`, `xx_set_arm_pmull_enabled(bool)`,
`xx_enable_arm_pmull(bool)`, and `xx_is_arm_pmull_enabled()` follow the existing
acceleration API, including the `xx_global_*` aliases. PMULL defaults on when
supported. Detection uses macOS sysctl, Linux/Android HWCAP, or Windows ARM
crypto capability; optional instruction flags remain local to the backend.

Console options: `--armpmull`, `--noarmpmull`, `--arm-pmull`, and
`--no-arm-pmull`. The shared optimization widget exposes **Use Arm PMULL**.
See `../xxfclib_tests/README.md` for correctness tests and measured speed.

### Explicit DEFLATE packing candidate count

ZIP DEFLATE (method 8) and Deflate64 (method 9) packing accept the optional
integer `XX_META_ID_COMPRESSION_CANDIDATES` (alias
`XX_META_ID_OPT_COMPRESSION_CANDIDATES`). It can be supplied in the writer's
options list, per archive record, or as a format extra parameter. Precedence
is record > writer options > format parameter. A record value of zero resets
an inherited count to level-based behavior.

```c
xx_archive_record_set_meta_u64(&record, XX_META_ID_COMPRESSION_CANDIDATES, 128);
```

A positive count overrides the compression level: it sets the maximum
hash-chain candidates searched at each position and selects the normal
compression path, even if level 0 was supplied. Zero or absence keeps the
existing level mapping (0=stored blocks, 1–3=4 candidates, 4–6=16,
7–9=128). For Deflate64 an explicit count also uses the normal long-match
limit instead of the low-level 258-byte cap. Explicit ZIP STORE (method 0)
remains stored. Other ZIP codecs keep their existing level handling.
Negative, floating-point, string, and nonrepresentable counts are rejected
before the member is written. This option is a packing control, not an
on-disk ZIP metadata field. It is local to the operation and also applies
to encrypted ZIP entries.

Raw DEFLATE APIs provide the same control through `xx_deflate_pack_options`
and the new `xx_deflate_pack_device_with_options`,
`xx_deflate_pack_source_with_options`, and
`xx_deflate_compress_memory_with_options` functions:

```c
xx_deflate_pack_options options = {128};
xx_deflate_pack_device_with_options(source, 0, source_size, destination,
                                   6, false, 0, &options, pd);
```

Passing `NULL` options, or `{0}`, preserves level-based packing. Existing
APIs remain available and delegate with `NULL`; there is no global candidate
setting. The candidate budget does not enlarge the DEFLATE history window.


ZIP DEFLATE and Deflate64 also accept `XX_META_ID_COMPRESSION_LAZY_MATCH`
(alias `XX_META_ID_OPT_COMPRESSION_LAZY_MATCH`). This nonnegative integer
sets a match-length threshold for one-byte lookahead. Zero or absence keeps
greedy parsing; 16 checks the next position when the current match is shorter
than 16 bytes and emits a literal if the next match is longer. Larger thresholds
apply this check to longer matches; they do not increase lookahead distance or
guarantee smaller output. Values beyond the codec's maximum match length are
equivalent to that maximum.

```c
xx_archive_record_set_meta_u64(&record, XX_META_ID_COMPRESSION_CANDIDATES, 128);
xx_archive_record_set_meta_u64(&record, XX_META_ID_COMPRESSION_LAZY_MATCH, 16);
```

Resolution is record > writer options > format parameter. An explicit zero
disables inherited lazy matching. Invalid types, negative values and values
beyond `SIZE_MAX` are rejected before member data is written. Lazy matching
alone does not override levels: level 0 still stores blocks unless a positive
candidate count selects compression. The setting applies to encrypted entries
as well; ZIP STORE and other codecs are unaffected.

For raw streams use `xx_deflate_pack_options_ex options = {128, 16}` with
`xx_deflate_pack_device_with_options_ex`,
`xx_deflate_pack_source_with_options_ex`, or
`xx_deflate_compress_memory_with_options_ex`. The extended structure contains
`max_candidates` and `lazy_match_limit`. The original options structure and
APIs retain their layout and greedy behavior. Both options are local to each
packing operation.


`XX_META_ID_COMPRESSION_SHORT_MATCH_FILTER` is a boolean ZIP DEFLATE/Deflate64
packing option (alias `XX_META_ID_OPT_COMPRESSION_SHORT_MATCH_FILTER`). With
`true`, a selected match of exactly 3 bytes is discarded if its distance is
more than 4096 bytes; the encoder emits a literal instead. Matches at distance
4096, and matches longer than 3 bytes, remain eligible. This is a heuristic,
not an exact Huffman bit-cost calculation, and does not guarantee smaller output.

```c
xx_archive_record_set_meta_bool(&record, XX_META_ID_COMPRESSION_SHORT_MATCH_FILTER, true);
```

It combines independently with candidate count and lazy matching. Default or
`false` preserves existing behavior. Resolution is record > writer options >
format parameter; explicit `false` disables inherited `true`. Only boolean
metadata is accepted, with invalid types rejected before member data is written.
Encrypted entries use the same option; explicit STORE and other codecs remain
unchanged. It does not override the compression level.

Raw extended packing options now include `bool short_match_filter`:
`xx_deflate_pack_options_ex options = {128, 16, true}`. Rebuild callers using
this extended struct with the updated header and library. The original
`xx_deflate_pack_options` and original APIs remain unchanged, with filtering off.


ZIP DEFLATE/Deflate64 also accept the nonnegative integer packing thresholds
`XX_META_ID_COMPRESSION_NICE_MATCH` and `XX_META_ID_COMPRESSION_GOOD_MATCH`
(with corresponding `XX_META_ID_OPT_*` aliases). Zero or absence disables
each shortcut and preserves the existing search. Both use record > writer >
format precedence; explicit zero disables inherited values. Invalid types,
negative values and values greater than `SIZE_MAX` fail before member output.

* `nice_match`: stop the main hash-chain search when the best match reaches
  this length (or the maximum available match length).
* `good_match`: once the best match reaches this length, reduce the total
  candidate budget for that position to `max(1, max_candidates / 4)`, using
  the effective level-based budget if no explicit count was supplied. Already
  examined candidates count towards that total. Reduction occurs only once.

Both shortcuts apply only to valid matches of at least 3 bytes; thresholds
1 or 2 therefore act as 3. Values above the codec's maximum match length
cannot trigger. Neither shortcut changes the separate one-byte lazy proof
search, compression level selection, or ZIP STORE behavior. Both apply to
encrypted entries and can be combined with lazy matching and short-match
filtering. Smaller thresholds can save search work but may increase output
size. These are explicit xxfclib semantics, not a complete Info-ZIP profile.

```c
xx_archive_record_set_meta_u64(&record, XX_META_ID_COMPRESSION_NICE_MATCH, 128);
xx_archive_record_set_meta_u64(&record, XX_META_ID_COMPRESSION_GOOD_MATCH, 8);
```

Raw `xx_deflate_pack_options_ex` gains `size_t nice_match` and
`size_t good_match` after `short_match_filter`:
`xx_deflate_pack_options_ex options = {128, 16, true, 128, 8}`.
Rebuild users of this extended struct with the updated header and library.
The original options struct and original APIs retain their defaults.

## LZFSE, LZ4 and LZMA packing

All three algorithms now support encoding and decoding without an external
codec library. LZMA/LZMA2 already had native encoders and decoders; the new
work adds LZFSE and LZ4 encoders alongside their existing bounded decoders.

* `<xxfclib/algo/lzfse/xx_lzfse.h>`: `xx_lzfse_compress_bound`,
  `xx_lzfse_compress_memory`, `xx_lzfse_pack_device`, and the existing
  `xx_lzfse_decompress_memory`. Encodes real LZFSE/LZVN blocks with raw fallback,
  not just a wrapper around uncompressed bytes. Encoding accepts at most
  `INT32_MAX-1` input bytes per call. Output capacity is capped internally at
  `source_size+12`, ensuring incompressible input can fall back to a raw block.
* `<xxfclib/algo/lz4/xx_lz4.h>`: `xx_lz4_compress_block` encodes raw blocks;
  `xx_lz4_compress_memory` / `xx_lz4_pack_device` produce standard LZ4 frames
  with independent blocks up to 4 MiB, content size, header checksum and
  content checksum. Incompressible blocks are stored. Existing block/frame
  decoders handle the output. `xx_lz4_compress_bound` bounds frame size;
  raw-block bound is `n+n/255+16`, within the documented source-size limits.
* `<xxfclib/algo/lzma/xx_lzma.h>`: existing `xx_lzma_compress_memory`,
  `xx_lzma_pack_device` and decode APIs use explicit LZMA properties. Existing
  LZMA-Alone format writer produces `.lzma`; XZ and ZIP use their own wrappers.

Memory encoders accept NULL source only for empty input, reject invalid size /
capacity, and reset optional `out_written` to zero on failure. Input and output
must not overlap. The new device encoders buffer the complete known-size input
and output; they support partial I/O, check cancellation between I/O chunks and
before/after encoding, and never close caller devices. LZ4 device packing is
limited to `INT32_MAX` input bytes; LZFSE to `INT32_MAX-1`. They do not provide
incremental/resumable encoding.

LZ4 **standard frames** differ from Apple's `COMPRESSION_LZ4` framing. The raw
block API is interoperable with `COMPRESSION_LZ4_RAW`. LZFSE streams interoperate
with `COMPRESSION_LZFSE`. These codec APIs do not create Apple Archive `.aar`
containers; container handling is a separate layer.

LZFSE encoding incorporates Apple's BSD-3-Clause reference encoder directly in
the build, with namespaced symbols and xxfclib allocation/runtime functions.
The existing native decoder remains in use. Source provenance, the local FSE
undefined-shift fix and the full redistribution notice are under
[src/algo/lzfse/reference](src/algo/lzfse/reference/README.md), including
[LICENSE](src/algo/lzfse/reference/LICENSE). Retain that notice with binary
redistributions containing the encoder. Main CMake and the formats-only source
profile both include the encoder; macOS Compression is linked only by tests.

## Standalone generic I/O subset

`xxio.cmake` supplies `XXIO_SOURCES`, `XXIO_INCLUDE_DIR`,
`XXIO_COMPILE_DEFINITIONS` and `XXIO_LIBRARIES` for file, buffer, memory,
subdevice, multivolume and process I/O. Add these sources to a C11 static
target, define `XXFC_STATIC` publicly, apply the feature definitions privately,
and link the listed native libraries. This includes the small native runtime
needed by generic I/O without the executable format parsers.
`xxprocessio.cmake` remains available when only process I/O is needed.

`xx_io_device` has an optional `resize(device, int64_t new_size)` callback.
Use `xx_io_resize` or `io_resize`: they reject negative sizes, return `0` on
success or `-1` on unsupported input/failure, preserve the cursor even beyond
the new EOF, and invalidate attached read caches after success. Writable
whole regular files and disk temporary streams expose it; read-only files,
memory devices and fixed-size views leave it null. Existing bytes before
the new EOF are retained; extension contents follow the native backend.
Memory-only scopes also prohibit resizing a disk file opened beforehand.
`xx_io_can_read_write_at` checks random-access read/write capability. Built-in
files require update mode (`+`) without append, so callers shifting file bytes
can reject append streams even when their resize callback is present. Read-only
memory and subdevices are respected; unknown-size devices are unavailable, and every nonempty
multivolume child must allow writing. Custom
devices must honor seek/read/write semantics; operations can still fail.
Zero-initialize custom devices and rebuild consumers together with the
library after this public structure change. The focused regression can be
built independently from [tests/io_resize](tests/io_resize/README.md).

## Reusable undo/redo history

`<xxfclib/history/xx_history.h>` provides a command history independent of UI,
I/O and threads. The algorithm and integration notes are in
[src/history](src/history/README.md); `xxhistory.cmake` supplies its sources for
standalone consumers. Full, formats and bytecode/emulation profiles include it.

Each command supplies undo/redo callbacks, an owned payload, an optional disposer
and a copied label. A linked list holds the commands and a cursor identifies the
last applied command. Undo/redo move that cursor only after a successful replay.
New commits discard the redo branch; an optional entry limit evicts old commands.
Cleanup occurs exactly once for aborted, evicted, cleared or destroyed commands.

Call `xx_history_prepare` before changing external data to reserve the command's
storage. If the change succeeds, `xx_history_commit` publishes it without an
allocation; otherwise roll back the change and call `xx_history_abort`. A failed
preparation or aborted change preserves redo. `xx_history_push_applied` is also
available for changes already applied by their caller. Replay receives a caller
context for progress or detailed errors, and failed callbacks leave history at
the same position. Callbacks own rollback of their external state.

The caller serializes access; callbacks cannot reenter mutation of the same
history. Payload storage is application-defined. HexView uses temporary byte
streams, while another tool can retain object/property changes instead.
Focused tests, including deterministic allocation failure and reentrancy, are
in [tests/history](tests/history/README.md).

## Process structure snapshots

`<xxfclib/process/xx_process_structs.h>` provides read-only native process
structure discovery and decoded field snapshots. `xx_process_structs_read`
accepts a PID, optional recorded start identity and bounded discovery limits.
`xx_process_structs_free` releases the result. Field descriptions reuse
`xx_data_struct_field_desc`; unsigned target addresses have explicit validity
flags and unreadable fields remain unavailable. Query results have no target
address. Windows readers provide full named PEB32/64 and TEB32/64 symbol
profiles for builds 19045 and 26100 on I386/AMD64, including WOW64, with
ImageBaseAddress, ProcessHeap, ClientId, LastErrorValue, nested members and
decoded flag bits. Unlisted builds/architectures use labeled SDK fallback
views; stable named core aliases require an established x86/AMD64 ABI.
ARM64 and x64 emulation on ARM64 do not reuse AMD64 private profiles. Layout
sources and exact extents are documented beside the descriptor tables in
src/process/platforms/xx_process_peb_layouts.h and xx_process_teb_layouts.h.
Windows also covers NT_TIB/TLS slots, loader/list entries, native CLIENT_ID
records, I/O/memory counters and FILETIME queries. Additional Windows records
include full versioned loader/process-parameter views, current-directory and
drive-slot records, direct loader DDAG/balanced-node/service-tag records,
critical sections/debug records, activation stacks/active frames, GDI batches
and remote ClientId views. Full loader profiles cover 19045 I386/AMD64 and
26100 AMD64; 26100 WOW64 retains SDK loader views and uses a labeled phnt32
process-parameter prefix. Parameter reads respect the declared block length
and follow only normalized text pointers. VirtualQueryEx adds common
MEMORY_BASIC_INFORMATION query fields for the PEB, process heap and image.
Heap discovery follows PEB.ProcessHeap and the initial ProcessHeaps array,
deduplicating addresses without a fixed heap-count cap. Signature-checked
HEAP32/64 and SEGMENT_HEAP32/64 header schemas cover native I386 build 19045
and AMD64 builds 19045/26100. Full WOW64 guest schemas cover NTDLL components
19041.2965 and 26100.1591 on those respective OS builds, using verified
Microsoft `wntdll.pdb` type records. Loaded guest PE32 CodeView GUID/age must
match the profile. `HEAP32` is `0x258` bytes; `SEGMENT_HEAP32` is `0x580` or
`0x740` bytes respectively. Unknown module identities and inaccessible
metadata keep explicitly labeled short classification prefixes. Unknown or
unreadable signatures appear as HEAP_HEADER32/64 probe records. Signatures
and heap-array metadata are rechecked; changed heap field snapshots are
discarded. Full layouts live in xx_process_heap_layouts.h and
xx_process_segment_heap_layouts.h; xx_process_heap_discovery.h supplies the
shared GUI/TUI/console collection. Full guest schemas and identity selection
live in the `xx_process_wow64_*` heap headers. Existing structure limits apply
to heaps.
Standalone HEAP_ENTRY32/64 and HEAP_SEGMENT32/64 child records expose the
legacy heap's encoding storage, validated segment headers and their embedded
and first stored entries. Segment heap child records expose
HEAP_VS_CONTEXT32/64 and HEAP_LFH_CONTEXT32/64 with their own addresses and
rebased field metadata. Child views inherit the parent's native/WOW64 ABI
selection, bounds and signature checks; prefix-only fallbacks do not infer
private children. Legacy segment lists use backlink, owner, cycle and range
checks without a fixed segment-count cap. Entries retain encoded raw storage;
no allocation chain is derived from their size fields. Segment and parent
identities are rechecked after child reads. `xx_process_heap_legacy_children.h`
and `xx_process_heap_context_children.h` supply this shared collection.
The suballocator schemas add HEAP_VS_SUBSEGMENT32/64, HEAP_LFH_SUBSEGMENT32/64
and HEAP_LFH_BUCKET32/64 for the same selected native and exact WOW64 profiles.
The shared collector follows the VS subsegment list, initialized untagged LFH
buckets and their ordinary owner lists, including 19045 affinity slots. It
checks target spans, bucket/owner metadata, backlinks, cycles and changed
snapshots. Low-bit on-demand bucket counters, encoded active-subsegment
references and 26100 private-slot references are not dereferenced. Only the
declared fixed subsegment header is shown, including the first bitmap element;
allocation blocks and variable bitmaps are not walked. These schemas and the
collector live in `xx_process_heap_suballocator_layouts.h`,
`xx_process_wow64_heap_suballocator_layouts.h` and
`xx_process_heap_suballocator_discovery.h`.
HEAP_VS_CHUNK_HEADER32/64 and HEAP_VS_CHUNK_FREE_HEADER32/64 provide separate
stored views of members reached through an unencoded VS FreeChunkTree.
The fixed schemas preserve header-size unions, the overlapping free-header
Node, tagged parent storage and named encoded aliases without byte-unit or
followable-pointer properties. Native and exact WOW64 profile selection is
inherited from the parent. The collector checks target spans, node container
offsets and parent links, detects cycles, honors structure limits and
rechecks captured headers/tree state. Encoded trees and first/busy chunks
remain unexpanded with explicit warnings; encoded sizes are never used to
step through allocations. Metadata and collection live in
`xx_process_heap_vs_chunk_layouts.h`,
`xx_process_wow64_heap_vs_chunk_layouts.h` and
`xx_process_heap_vs_chunk_discovery.h`.
AMD64 and WOW64 chunk-size bit positions are PDB-derived. Native I386's four
unverified size-bit aliases retain their physical four-byte storage with
explicit position-unverified labels, rather than guessed extraction.
Direct child discovery does not recursively traverse dependency, tree,
activation-frame or heap-allocation graphs. Other platforms cover
macOS process/thread/dyld information;
Linux proc/AUXV, mapped ELF headers/program headers/dynamic entries and loader
records; and FreeBSD kinfo_proc/kinfo_vmentry, AUXV and ps_strings records.
FreeBSD sysctl query metadata does not require procfs; remote process-memory
reads use native procfs when mounted and permitted. Inspection does not suspend
the process and reports partial results through warnings. Collection is
unlimited by default; positive options impose explicit caps. FreeBSD VM-map
records use the structure cap, while the loader module cap is inapplicable.

The full library registers these sources. Standalone hosted C11 consumers can
include `xxprocessstructs.cmake` with `xxio.cmake`. CXNTSV uses this subset for
its GUI, TUI and console tools without building the full format library.

## PO translations

The reusable C11 module in `src/translation` loads UTF-8 GNU PO catalogs with
contexts, plurals and English fallback. Include
`<xxfclib/translation/xx_translation.h>`; see
[translation documentation](src/translation/README.md) for API and standalone builds.
