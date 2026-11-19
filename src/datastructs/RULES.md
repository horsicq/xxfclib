# Rules for `.xxds` DataStructs scripts

A `.xxds` file is a JavaScript-like script (the xxfclib JS engine, `src/js`)
that describes the on-disk data structures of one file type. Everything the
DataStructs stream reports for a file type lives in its script: which
structures exist, their fields, where they occur in a file, and how values are
commented. There is no per-format C provider.

    src/datastructs/RULES.md            this document
    src/datastructs/_prelude.xxds       constants and field builders, loaded first
    src/datastructs/<format>.xxds       one script per file type family
    src/datastructs/sx/                 the schema scripts: sx_<family>.xxds (143 families, about 1,900 file
                                        types), their libraries _sx_<family>[_<format>].xxds, the runtime
                                        _sx.xxds and _sx_types.xxds. Generated once from the retired C schema
                                        engine; now source like every other script. A script directory is
                                        searched first, then its sx/ sub-folder; embedded copies of sx/ are
                                        stored deflated
    src/datastructs/xx_xxds.c           the host (script runtime, C API bridge)
    src/datastructs/xx_xxds_scripts.inc generated: the scripts, embedded

After changing or adding a script run `python tools/generate_xxds_registry.py`
(`--check` verifies the embedded copy is current).

## 1. File rules

1. One script per file type family, named after the reader in lower case:
   `pe.xxds`, `elf.xxds`, `zip.xxds`. Names starting with `_` are libraries
   that other scripts `include()`; they serve no file type themselves.
2. ASCII only, LF line endings, 4-space indentation, no tabs.
3. The language is ES5-level JavaScript: `var`, functions, object and array
   literals. No `let`/`const`, arrow functions, classes or template strings.
4. A script keeps no per-file state in globals. One script context is reused
   for many files and many calls; anything derived from the current file is
   computed inside the function that needs it (helpers may re-read headers).
5. Scripts are deterministic: no `Date`, no `Math.random`, no output.

## 2. Required functions

Every format script defines these three functions.

### `fileTypes()`

Returns the file types the script serves, as a literal array of `XX_FILE_TYPE_`
suffixes. The generator reads this function textually, so it must be exactly
one `return` of string literals:

    function fileTypes() { return ["PE32", "PE64"]; }

### `structs()`

Returns **every structure this file type can contain** - the catalog. It is
called once per script context and must not read the file.

    function structs() {
        return [
            { id: 1, name: "MEMBER_HEADER", fields: [
                u8("id1", ID), u8("id2", ID), u8("compression_method"),
                u8("flags", FLAGS), u32("modification_time", TIMESTAMP),
                u8("extra_flags", FLAGS), u8("operating_system") ] },
            { id: 7, name: "TRAILER", fields: [
                u32("crc32", CHECKSUM), u32("input_size", SIZE) ] },
            { id: 2, name: "EXTRA_FIELD", fields: extraFieldFields }
        ];
    }

Catalog entry:

| key      | meaning |
|----------|---------|
| `id`     | Structure id, unique in the script, 1..0xFFFEFFFF. Ids are public API: never renumber or reuse one. `0xFFFF0001` is the built-in `RAW_DATA`. |
| `name`   | Canonical name, unique in the script (`IMAGE_DOS_HEADER`, `MEMBER_HEADER`). |
| `fields` | An array of fields, or a function `(ds)` returning one for layouts that depend on the occurrence (see 4). Omit for structures without decoded fields. |
| `endian` | `LE`, `BE` or `FILE` (the reader's byte order). Default `LE`. |
| `aliases` | Optional array of further names that a name lookup resolves to this id. |
| `description` | Optional: what the structure is, one or two plain sentences ("The first structure in every PE file. A leftover from MS-DOS compatibility."). |

### `scan()`

Reports the occurrences in the opened file, in the order they should be
listed, by calling `Structs.*` (see 5). `scan()` may return `false` to report
that the file has no readable structures; throwing has the same effect.

## 3. Fields

A field is an object:

| key       | meaning |
|-----------|---------|
| `name`    | Field name as shown to the user. |
| `type`    | `uint8/16/32/64`, `int8/16/24/32/40/48/56/64`, `float32`, `float64`, `char[N]`, `uint8[N]`, `bytes`, or any `x[N]` array text. |
| `offset`  | Byte offset inside the structure. |
| `size`    | Size in bytes. Scalars are 1..8 bytes. |
| `role`    | Bitwise OR of role constants (see 6). Default `NONE`. |
| `comment` | Optional: a string, a map `{ value: "NAME" }`, or a function `(value, ds)` returning a string or `undefined`. |
| `display` | Optional function `(value, ds)` returning the display string, replacing the standard one. |
| `logical` | Optional function `(value, ds)` for a bit field of a packed word: returns the field's own value (number or boolean). The raw value stays the packed word; the record value, the standard display and the location formatting use the returned value, and the standard comment is dropped. |
| `description` | Optional: what the field means ("File address of the new exe header"). It is shown on request next to the comment; a comment explains this value, a description explains the field. |
| `utf8`    | Optional `true`: the `display` result is UTF-8 text and is decoded to Unicode; invalid UTF-8 fails the record. |
| `endian`  | Optional per-field override of the structure byte order. |

The prelude builders write fields without spelling offsets. Inside one
`fields` array every builder is placed after the previous one:

    u8(name[, role[, comment]])   u16  u32  u64        unsigned
    i8(name[, role[, comment]])   i16  i32  i64        signed
    chars(name, n[, role])        char[n], role STRING by default
    bytes(name, n[, role])        uint8[n]
    blob(name, n[, role])         type "bytes" (variable-length payload)
    field(name, type, size[, role[, comment]])          any other type
    pad(n)                        skip n bytes without a field
    at(offset, f)                 place field f at an explicit offset

`layout(fields)` returns the same array with offsets assigned; `structs()`
entries are laid out automatically, and a `fields` function must return
`layout([...])` (or fields with explicit `offset`).

The host formats values exactly as the C API always did: roles `ID`, `FLAGS`,
`SIGNATURE`, `CHECKSUM` display as hexadecimal, `VA`/`OFFSET`/`POINTER`
display as padded hexadecimal locations, `SIZE` gets a byte-count comment.
A script `comment` replaces the standard comment (a hook returning `null`
removes it); a script `display` replaces
the standard display value.

## 4. Occurrence-dependent layouts

When the layout depends on the occurrence, `fields` is a function. It gets the
occurrence and may read the file:

    function extraFieldFields(ds) {
        var n = File.u16(ds.offset);
        return layout([ u16("xlen", SIZE), blob("data", n) ]);
    }

`ds` has `id`, `name`, `offset`, `address` (-1 when unmapped), `entrySize`,
`totalSize`, `count`. A caller may pass any descriptor, not only one that
`scan()` reported, so a `fields` function validates what it reads; the host
rejects the occurrence when a field does not lie inside `totalSize`. Returning `undefined` or an empty array means the
occurrence has no decodable fields.

## 5. Host objects

Offsets are relative to the start of the format inside its device.

`File` - bounded reads from the opened file:

    File.size()                      bytes available
    File.valid(offset, size)         true when the range is inside the file
    File.u8(o) .u16(o[,be]) .u32(o[,be]) .u64(o[,be])
    File.i8(o) .i16(o[,be]) .i32(o[,be]) .i64(o[,be])
    File.u64hi(o[,be]) .u64lo(o[,be])   exact halves of a 64-bit value
    File.bytes(o, n)                 n bytes as a byte string
    File.read(o, n, array[, at])     n bytes as numbers into array[at...]; the bulk form for buffers
    File.cstr(o, max)                bytes up to (not including) the first NUL
    File.cstrSize(o, max)            length including the NUL, -1 if none
    File.find(o, n, bytes)           offset of a byte string in [o, o+n), or -1
    File.crc32(o, n)                 CRC-32 (IEEE) of n bytes, for self-checking structures

A read outside the file throws, and a throw makes the whole call fail. Check
`File.valid()` first wherever a damaged region must be skipped instead. Numbers are doubles: values
above 2^53 lose precision, so use `u64hi`/`u64lo`, or let the host convert a
stored pointer with `Format.vaAt()`.

`Format` - facts the reader already established:

    Format.type()                    file type display name ("PE64")
    Format.typeName()                the XX_FILE_TYPE_ suffix ("PE64", "TRACKER_MOD"): use this to branch
    Format.size()                    size of the format's own data, or -1
    Format.ensure()                  true once the reader has parsed the file and found it valid
    Format.isBigEndian()             reader byte order
    Format.isMapped()                true when file offsets are addresses
    Format.vaToOffset(va)            virtual address -> file offset, -1 if none
    Format.rvaToOffset(rva)          relative address -> file offset, -1 if none
    Format.offsetToVa(offset)        file offset -> address, -1 if none
    Format.vaAt(o, size[, be])       read a size-byte pointer at o, convert it
    Format.rvaRange(rva, size)       offset of size file-backed bytes at an RVA, -1 if any is not
    Format.vaRange(va, size)         the same for a virtual address
    Format.vaRangeAt(o, ptrSize, size[, be])   the same for a 4/8-byte pointer stored at o
    Format.imageBase()               module address of the memory map, -1 without a map
    Format.mapRecord(rva)            memory-map record of an RVA or null:
                                     { offset, address, size, part, partNumber, name, isVirtual }
    Format.table(name)               rows of a reader fact table (see 7)
    Format.baseShift()               offset of the format inside the base the script runs on; 0 except for a
                                     reader with an authenticated outer container (PSA_DISK, BINSH_STARKIT),
                                     whose script offsets start at the container
    Format.version()                 the reader's version string ("" when none)
    Format.extra(metaId)             a string extra parameter of the reader, or null

The `*Range` functions answer -1 unless every byte of the range is mapped to
file data; use them for anything that will be read or reported.

`Structs` - the occurrence list built by `scan()`:

    Structs.add(name, offset, size[, kind[, address]])        one occurrence
    Structs.table(name, offset, entrySize, count[, kind[, address]])
    Structs.raw(offset, size[, name])   unparsed bytes; default name RAW_DATA
    Structs.count()                     occurrences reported so far

`name` is a catalog name (or id). `kind` is `STRUCT` (default), `ENTRY`,
`FOOTER`, `LOCATOR` or `RAW`. `address` defaults to the offset when the format
is mapped and to -1 otherwise; pass `MAPPED` to have the host translate the
offset through the reader's memory map (exact for 64-bit addresses). An occurrence must lie inside the file; one
that does not throws. At most 262144 occurrences are accepted.

`Util`:

    Util.utc(seconds)                "yyyy-MM-dd HH:mm:ss" (UTC), undefined when out of range
    Util.hex(value[, digits])        "0x..." upper case, zero-padded to digits
    Util.indexOf(array, v, from[, to])  first index in [from, to) whose element is the number v, or -1
    Util.crc(kind, seed, array, at, n)  "crc32", "crc32c" or "crc16xmodem" of n byte elements, continued from seed
    Util.heapSort(array, from, n, compare[, boxed])  sorts n elements in place with the library's heap sort
                                     (the order of equal elements is that of the C library on Windows);
                                     boxed: compare gets one-element arrays

`include(name)` evaluates another script in the current context so its
functions can be reused. Function declarations are global, so a script that
is meant to be included defines its catalog and scanner under prefixed names
(`peStructs()`, `peScan()`) and implements `structs()`/`scan()` by calling
them. Shared helpers go to a `_name.xxds` library.

The including script's own function declarations are hoisted before
`include()` runs, so the included script's `structs()`/`scan()` replace them.
The including script therefore writes its versions under prefixed names and
installs them by assignment after the include (see `dotnet.xxds`):

    include("pe");
    function fileTypes() { return ["DOTNET"]; }   // kept: the generator reads it
    function dnStructs() { ... peStructs() ... }
    function dnScan() { return peScan(...); }
    fileTypes = dnFileTypes; structs = dnStructs; scan = dnScan;

## 6. Constants

Roles: `NONE`, `ID`, `VA`, `OFFSET`, `SIZE`, `COUNT`, `POINTER`, `FLAGS`,
`TIMESTAMP`, `STRING`, `RESERVED`, `SIGNATURE`, `CHECKSUM`.
Kinds: `STRUCT`, `ENTRY`, `FOOTER`, `LOCATOR`, `RAW`.
Byte order: `LE`, `BE`, `FILE`.

## 7. What stays in C

Scripts describe structures; they do not re-implement readers or codecs.

* Decompression, decryption and checksums stay in the readers. When a
  structure boundary is only known after decoding (the end of a deflate
  stream, a solid block), the reader publishes it in a fact table and the
  script reads it with `Format.table(name)`: an array of row objects whose keys
  are documented at the top of the script that uses them.
* Address translation uses the reader's own memory map (`Format.vaToOffset`).
* A wrapper type (`TAR_GZ`, `TAR_XZ`, ...) is served by the script of the
  format it contains; the host hands that script the decoded inner format.

## 8. Robustness

* Every loop is bounded by a count read from the file **and** by the file
  size; a table of `n` entries is checked with `File.valid()` before it is
  walked.
* Never trust a size or offset from the file without a range check.
* Do not report zero-sized structures; do not report the same occurrence
  twice.
* A script failure makes the file report no structures; it must never be
  used to signal "format not recognized" - the reader decided that already.

## 9. Verifying a script

`xxfclib_xx_datastructs_dump <file> [--xxds-dir src/datastructs]` prints every
occurrence and every record. `--xxds-dir` loads scripts from disk, so a script
can be edited and re-run without rebuilding. In that mode the host also reads
`fileTypes()` of every script in the directory (names not starting with `_`,
stubs skipped), so a new script serves its file types before it is embedded;
directory scripts take precedence over embedded ones. A ported script is correct when
its dumps equal the reference dumps for the corpus of its format.
