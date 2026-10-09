# Managed PE / CLI readers

`xx_dotnet_inspect` parses the PE/CLI metadata directory, tables and heaps.
It rejects invalid table extents, duplicate stream names, overlapping streams,
unterminated heap strings and noncanonical compressed user-string lengths.

`xxfclib/formats/dotnet/xx_dotnet_reader.h` provides allocation-free bounded
queries on a successfully parsed `xx_dotnet_inspection`:

- `xx_dotnet_inspect_table_row`: one-based metadata RID to row offset/size.
- `xx_dotnet_inspect_blob`: blob index to payload offset/size.
- `xx_dotnet_inspect_method_body`: RVA to tiny/fat method header, code extent,
  local-signature token and exception-section metadata.

Offsets are relative to the format base. The inspection and its borrowed
device must remain alive and unchanged. Queries preserve both the inspection
device's position and its parent device's position; failure clears outputs.
Method bodies must fit one raw-backed PE range. Local-signature/catch tokens,
exception ranges and chained sections are checked. These are structural
readers; instruction verification and exception execution belong to a runtime.

The focused hosted static C11 build contains 50 C sources:

```powershell
cmake -S D:/qt5/_mylibs/xxfclib -B build/xxfclib-dotnet -DXXFC_BUILD_PROFILE=dotnet_emul
cmake --build build/xxfclib-dotnet --config Release
```

It exports the `xxfclib::xxfclib` CMake target. The default `full` profile keeps
the complete library source catalog. Shared and CRT-free builds require `full`.

The pure-C `C:/tmp_build/qt5/XDotNetEmul` tests exercise these APIs with synthetic
PE/CLI images, malformed inputs, embedded images and `D:/files/pe/msbuild.exe`.
