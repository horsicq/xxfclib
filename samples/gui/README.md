# Windows archive browser

`xxfc_gui` is a pure WinAPI sample. Browse for a file (opens immediately), or
enter a path and press Open. It detects the archive type and displays member
names, unpacked sizes and packed sizes. Parsing runs on a worker thread.
Unicode paths and member names are supported. Tab moves between controls;
Enter opens the typed path. The listing is selectable and can be copied.

Only **KERNEL32.dll** appears in the executable's import table. The sample
statically links the CRT-free library and supplies compiler support routines
from `xx_entry_windows.c`. USER32.dll and COMDLG32.dll are loaded from the system
directory with LoadLibraryExW/GetProcAddress; no Qt, CRT or GUI import libraries.

Build from an x64 Visual Studio developer prompt, starting in `xxfclib`:

```bat
cmake -S . -B build/gui -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DXXFC_STATIC_NO_CRT=ON -DXXFC_BUILD_GUI=ON
cmake --build build/gui --target xxfc_gui
build\gui\samples\gui\xxfc_gui.exe
```

MSVC x64 is required. Use BUILD_SHARED_LIBS=OFF (default), or XXFC_BUILD_BOTH=ON
which links the static target explicitly. GUI builds are independently enabled
with XXFC_BUILD_GUI; XXFC_BUILD_SAMPLES still enables the console samples.

This sample views archive contents; it does not extract, preview or modify
members, or prompt for passwords. Supported formats depend on xxfclib's readers.
Unknown/invalid files produce a status message. To bound display memory, the
listing stops at 100,000 members or approximately four million UTF-16 code units
and reports truncation. Unknown sizes are displayed as `?`. Closing the window exits immediately, including while
an archive is being parsed.

Twenty previously unregistered installer formats are now built and detected:
SBX, AnalogX FFS, KRZIP, WarpIN, HCI Instalit/Shadow (including data volumes),
Clickteam Multimedia Fusion, ABBYY FineObjects, FlashJester Jugglor,
JGsoft DeployMaster, and ARDI diskette images; plus Nullsoft PiMP, Sydex diskette
images, Compaq SoftPaq v1, WASP, Wise Installation System, Eschalon EPSF,
Gentee, Clickteam Install Creator, CreateInstall INSTCRIN, and SFXSTART.
Their containers are parsed
without executing the installer. The library unpack API is available to the
headless smoke harness and the console sample.

A further fifty readers are now registered: more installer families (including
Inno Setup, InstallShield and PyInstaller), disk containers, CHM, RPM, StuffIt5,
text encodings and other archives. See `../../docs/registered_fifty_formats.md`
for the complete list, their own reader/extractor folders and version/codec
limits. CUE sheets attach supported companion data files from the sheet's
directory; the reader validates referenced basenames and opens them read-only.

Each of these twenty formats has its own `src/formats/<name>/` folder with
`xx_<name>.c` and `xx_<name>_extractor.c`, following the library's naming
convention. The companion searches are built into the library and registered
with `xx_format_extractor_get()`. They validate candidates through the reader
and detector. Executable carriers use the MZ anchor; FFS also uses `FFS!`, and
WarpIN uses its package header. Standalone Instalit volumes have no fixed
prefix, so their search probes them at offset zero. Readers that locate an
EOF trailer require that trailer at the end of the searched device.

WASP and SFXSTART expose the files stored in their installation kits, including
KWAJ and SZDD compressed members; unpacking the outer container does not
recursively unpack those files. Gentee can recover members from an incomplete
container; the GUI marks that listing as incomplete and the extraction smoke
test reports `PartialUnpack` even when every recovered member was written.
The same completeness reporting now covers the additional readers that expose
truncated or damaged chains, omitted members or unavailable external data.

Every GUI build checks the import table with dumpbin and fails if any DLL other
than KERNEL32.dll appears. To inspect it manually in a developer prompt:

```bat
dumpbin /imports build\gui\samples\gui\xxfc_gui.exe
```

## GUI smoke tests

Configure with `-DXXFC_GUI_BUILD_SMOKE_TESTS=ON`, then build the
`test_xxfc_gui_smoke` target. This uses a hidden window with the same GUI source,
Open command, archive worker and completion timer. It checks that opening each
file finishes, restores the controls and displays either a listing or a known
error. Each file runs in its own process with a timeout; crashes fail the test.

From the library directory in PowerShell:

```powershell
cmake -S . -B build/gui -DXXFC_GUI_BUILD_SMOKE_TESTS=ON
cmake --build build/gui --target test_xxfc_gui_smoke
./tests/check_gui_samples.ps1 -Executable ./build/gui/samples/gui/test_xxfc_gui_smoke.exe -ReportPath ./build/gui/gui-samples-smoke.csv
```

The runner requires PowerShell 7. It recursively snapshots all files under `samples`, including ignored
object files. Use `-InputRoot <directory>` for another corpus. An unsupported
file counts as handled when the GUI returns its documented error without
crashing or hanging; it does not count as a successfully opened archive.

For a large corpus, add `-Concurrency 12`. Results are appended to the CSV as
files finish, and every input uses a separate process. Existing report files are
not overwritten; select a new report path for each run.

## Open and unpack corpus smoke tests

Build `test_xxfc_unpack_smoke` with `XXFC_GUI_BUILD_SMOKE_TESTS=ON`. This is a
headless integration harness using the same reader dispatch as the GUI. It
opens and validates each file, then attempts to extract each archive record to
an isolated directory using the library's unpack API. The GUI itself remains
an archive listing sample.

```powershell
cmake --build build/gui --target test_xxfc_unpack_smoke
./tests/check_unpack_samples.ps1 -Executable ./build/gui/samples/gui/test_xxfc_unpack_smoke.exe -InputRoot F:/ARC/ARC -RunRoot ./build/unpack-arc-run -Concurrency 12 -TimeoutSeconds 60
```

The run directory must be new. It contains a complete input manifest,
`results.csv`, and member failure logs under `members`. Extracted payloads are
counted and deleted after each case; they are never executed. Results distinguish
open rejection, complete/partial/failed unpacking, empty archives, unsafe paths,
resource limits, crashes and deadlines. `-OnlyFiles <absolute-path>` reruns a
selected case with a new run directory and optional longer deadline.

The harness skips rooted/traversal/device paths and unsafe link targets. It
limits declared member sizes to 1 GiB, declared archive output to 2 GiB, and
records to 100,000; it passes a 512 MiB codec buffer budget where supported.
The runner terminates a process exceeding 1 GiB private memory or its deadline.
Such cases are reported separately from successful unpacking. Successful API
results and output file counts are smoke checks, not a comparison against an
independent reference extractor.

Use `-PayloadRoot <new-temporary-directory>` to place extracted payloads on
a faster local drive while keeping reports under `RunRoot`. The payload
directory must be new; per-case payloads are cleaned up after recording counts.

To verify the twenty installer additions against the corpus, run:

```powershell
./tests/check_registered_installers.ps1 -Executable ./build/gui/samples/gui/test_xxfc_unpack_smoke.exe -GuiExecutable ./build/gui/samples/gui/test_xxfc_gui_smoke.exe -InputRoot F:/ARC/ARC -RunRoot ./build/installer-smoke -PayloadRoot "$env:TEMP/xxfc-installer-smoke"
```

The checked-in manifest covers 183 real files: 180 complete extractions and
three known partial extractions. Instalit `PSCHED1.001` rejects `MARKER.SLB`;
EPSF `99_yzhbqcqojmmxubri_Setup.exe` contains a final member spanning missing
continuation volumes; Gentee `99_wzlyqhyxjqthagnz_bspocket.exe` lacks the
container's closing marker. The
script asserts reader selection, record counts and actual disk output counts
and sizes, and container completeness where recorded, then exercises the GUI
Open command for each of the twenty families.
It requires the named corpus fixtures; existing run/payload directories are
not overwritten. The default deadline is 600 seconds because the largest Wise
fixture needs about two minutes; use `-TimeoutSeconds` to change it. With
`XXFC_BUILD_TESTS=ON`, nineteen existing synthetic installer
tests are also available through CTest (KRZIP is covered by the corpus tests).

`test_registered_installer_extractors` checks all twenty search registrations,
real corpus searches at offset zero and behind prefixes (including a signature
across a 64 KiB scan boundary), malformed signatures, cancellation, and the
standalone FFS and Instalit variants. Build it with `XXFC_BUILD_TESTS=ON` and
run `ctest --test-dir build/gui -R "test_(format_extractor|registered_installer_extractors)" --output-on-failure`.
It defaults to `F:/ARC/ARC`; invoke the test executable with another corpus-root
argument if needed. CTest reports a skip when the external fixtures are absent.

Replay the matching corpus files for the fifty-reader batch using the same
script with `-ManifestPath ./tests/fifty_formats_corpus.json`. Its GUI checks
also verify the incomplete-listing notice on representative recovered cases.
`test_registered_fifty_formats` checks all fifty reader types and extractor
registrations; each family also has its own synthetic extraction test.

Another fifty formats (public values 506â€“555) are registered in the same GUI,
reader and extractor interfaces. The complete list, supported subsets and
repeat commands are in [registered_second_fifty_formats.md](../../docs/registered_second_fifty_formats.md).
`tests/check_second_fifty_formats.ps1` generates fixtures for all fifty families,
verifies actual native unpacking and GUI opens, and checks partial results for
damaged PNG CRC and a missing VPK volume. The real ARC replay manifest is
`tests/second_fifty_formats_corpus.json`; its two large VDI images intentionally
retain the smoke harness's `Limited` result. Encoded component readers export
their embedded bytes without decoding media or executing firmware.

[Third fifty format readers](../../docs/registered_third_fifty_formats.md) documents the next fifty supported families and their extraction limits.

[Fourth fifty format readers](../../docs/registered_fourth_fifty_formats.md) documents public types 606-655, their supported extraction limits, primary references and repeatable verification commands.

[Fifth fifty format readers](../../docs/registered_fifth_fifty_formats.md) documents public types 656-705, their supported extraction limits, primary references and repeatable verification commands.

[Sixth fifty format readers](../../docs/registered_sixth_fifty_formats.md) documents public types 706-755, their supported extraction limits, primary references and repeatable verification commands.

[Seventh fifty format readers](../../docs/registered_seventh_fifty_formats.md) documents public types 756-805, their supported extraction limits, primary references and repeatable verification commands.

[Eighth fifty format readers](../../docs/registered_eighth_fifty_formats.md) documents public types 806-855, their supported extraction limits, primary references and repeatable verification commands.

[Ninth fifty format readers](../../docs/registered_ninth_fifty_formats.md) documents types 856-905, supported variants, primary references and repeatable checks.
