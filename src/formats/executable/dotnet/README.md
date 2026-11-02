# Managed PE / CLI readers

`xx_dotnet_inspect` parses the PE/CLI metadata directory, tables and heaps.
It rejects invalid table extents, duplicate stream names, overlapping streams,
unterminated heap strings and noncanonical compressed user-string lengths.

`xxfclib/formats/executable/dotnet/xx_dotnet_reader.h` provides allocation-free bounded
queries on a successfully parsed `xx_dotnet_inspection`:

- `xx_dotnet_inspect_table_row`: one-based metadata RID to row offset/size.
- `xx_dotnet_inspect_blob`: blob index to payload offset/size.
- `xx_dotnet_metadata_table_name`: names for all 45 CLI tables and the eight
  Portable PDB table schemas (reserved table numbers return null).
- `xx_dotnet_inspect_decode_row`: every named column of one metadata row,
  preserving its raw value, resolved token, heap/reference range, validity,
  and bounded text. Strings, GUIDs, ordinary signatures, and constants are
  decoded. Attribute, marshaling, security, and producer-specific debug
  payloads retain their bytes as bounded hexadecimal previews. Invalid
  column values are diagnostic records; an unreadable row returns false.
- `xx_dotnet_inspect_method_body`: RVA to tiny/fat method header, code extent,
  local-signature token and exception-section metadata.

Offsets are relative to the format base. The inspection and its borrowed
device must remain alive and unchanged. Queries preserve both the inspection
device's position and its parent device's position; failure clears outputs.
Method bodies must fit one raw-backed PE range. Local-signature/catch tokens,
exception ranges and chained sections are checked. These are structural
readers; instruction verification and exception execution belong to a runtime.

`xxfclib/formats/executable/dotnet/xx_dotnet_cil.h` adds pure C operations over
borrowed CIL byte buffers: operand widths, instruction decode, relative
branch/switch targets, complete instruction-boundary validation and safe
replacement-span checks. These allocate nothing; validation uses a disjoint
caller-owned boundary map. Status codes distinguish bad arguments, truncated
operands, invalid opcodes, oversized switches and invalid control-flow targets.
This validates byte structure and control flow, without verifying the managed
evaluation stack or executing instructions.

`xxfclib/formats/executable/dotnet/xx_dotnet_edit.h` provides:

- `xx_dotnet_utf8_to_utf16`: canonical UTF8 decoding, with a size-query mode.
- `xx_dotnet_us_encode`: an ECMA-335 `#US` entry encoder with compressed length,
  UTF16 units and the terminal special-character flag; lone surrogates remain.
- `xx_dotnet_rebuild_user_strings`: a new owned PE buffer with relocated CLI
  metadata and a replacement `#US` heap, adding its stream header if absent.
- `xx_dotnet_edit_free`: releases the library-owned rebuild buffer.

The codecs allocate nothing and reject overlapping buffers before writes.
Capacity/invalid-input failures clear result sizes and preserve output. The
rebuilder independently validates its unsigned IL-only PE32/PE32+ container,
stream layout and metadata-table extents using the existing inspection reader.
It preserves metadata tables, other heaps and method bodies, and updates PE/CLI
sizes within an explicit caller limit. It does not modify identifiers, decode
an obfuscator or execute code. Returned storage must be released through the
library's release function, including when crossing DLL/CRT boundaries.

These APIs use xxfclib's allocator/runtime conventions and are included in the
`full`, `formats` and `dotnet_emul` source profiles. The `DotNet.*` JavaScript
object in xxdebugscript binds to these operations while retaining its script
argument handling, snapshots and debugger-owned buffers.

The focused hosted static C11 build uses the `dotnet_emul` source profile:

```powershell
cmake -S D:/qt5/_mylibs/xxfclib -B build/xxfclib-dotnet -DXXFC_BUILD_PROFILE=dotnet_emul
cmake --build build/xxfclib-dotnet --config Release
```

Add `-DXXFC_BUILD_DOTNET_PRIMITIVE_TESTS=ON` to build the standalone
`test_dotnet_primitives` test. Run it with `ctest --test-dir build/xxfclib-dotnet
--output-on-failure`. Its synthetic CIL, Unicode and PE32/PE32+ fixtures require
neither JavaScript nor a CLR. It checks rebuilding with and without an existing
`#US` heap and reads the result through the independent inspection API.

It exports the `xxfclib::xxfclib` CMake target. The default `full` profile keeps
the complete library source catalog. Shared and CRT-free builds require `full`.

The pure-C `C:/tmp_build/qt5/XDotNetEmul` tests exercise these APIs with synthetic
PE/CLI images, malformed inputs, embedded images and `D:/files/pe/msbuild.exe`.
