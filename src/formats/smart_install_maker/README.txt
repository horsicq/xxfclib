Smart Install Maker native reader
=================================

The independent MIT C reader implements the published Binary Refinery xtsim
EOF-footer layout. Runtime code uses xxfclib IO/codecs and Win32 codepage
conversion; no Python runtime, external helper, installer execution, or input
script execution is required. Primary BSD-3-Clause research attribution is
retained in LICENSE.refinery.BSD-3-Clause.txt and upstream-provenance.json.

The reader emits setup/strings.bin, runtime payloads, and stored data/ or decoded
content/ members. Absolute installation paths become symbolic relative paths.
It restores borrowed source cursors, clones retained options, rejects unsafe
paths/duplicates, and uses the existing atomic extraction staging only when an
output path is requested. NULL-output TEST performs all reads/decodes in RAM.

Input and decoded folder/member sizes are capped at 64 MiB. The operation budget
defaults to, and cannot exceed, 256 MiB; smaller OPT_MEMORY_LIMIT and
OPT_MAX_MEMBER_SIZE values are respected. Input image, retained records, names,
owned options, decoded buffers, and codec workspaces are charged. A fixed 128 KiB
reserve covers active buffers/stack and metadata conversions.

Stored, MSZIP and LZX cabinet paths have independent fixture-byte comparisons
with Binary Refinery 0.11.2. Those LZX fixtures contain uncompressed LZX blocks.
Quantum is wired to the existing decoder but has no independent SIM fixture.
Multipart/continued cabinets, non-EOF footers, unknown layouts/encodings and
encryption variants are unsupported. No real independent SIM producer binary
was used. Non-Windows builds currently require ASCII source names.

Build/verify through the app smart_install_maker_probe target and runner:
  python tests/smart_install_maker_regression.py --probe <probe.exe> --root <reports>
The default runner requires only the Python standard library.
For optional independent oracle checks, install pinned binary-refinery==0.11.2
into a separate research directory and add --reference <that-directory>.
The oracle performs only a bytes/memoryview representation normalization for
upstream pure-Python StructReader compatibility. Each report records whether
the independent reference comparison ran. See docs/smart-install-maker-coverage.json
for frozen reports and precise remaining gaps.
