# Reader-only extractor adapters

`tools/generate_reader_only_abstract_extractors.py` emits bindings beside each
owning reader as `xx_<module>_reader_adapters.c`. Several file types can share
one reader module. The detector-only ASF and Audible AA bindings live here in
`xx_format_detector_adapters.c`.

`xx_reader_adapter.h` supplies the common descriptor and constructor/destructor
recipe. Every normal binding chooses an explicit type policy:

- `XX_READER_TYPE_FILL_UNKNOWN`: supply the declared type only when the native
  constructor leaves it unknown.
- `XX_READER_TYPE_FORCE`: select a requested variant of a shared reader.
- `XX_READER_TYPE_PRESERVE`: retain the reader's own classification.

GOTEK keeps its explicit 720/1440 constructor retry. Detector-only adapters keep
their explicit detector-backed validity callback. These exceptions are part of
the binding contract.

Regenerate from the library root with:

```text
python tools/generate_reader_only_abstract_extractors.py
python tools/generate_reader_only_abstract_extractors.py --check
```

`--check` detects changed, missing and retired generated outputs without writing.
The generator records owned paths and content hashes in
`xx_reader_adapters_files.json`. It refuses to retire an escaped path, a file
without its generator marker, or a previously generated file whose contents
were modified. Unlisted files are not cleanup targets.

The retired monolithic adapter source can migrate without a prior manifest only
when its normalized text matches the known previous generated version. Other
unverified or modified legacy files are refused before any output is written.

Builds with explicit source lists include `xx_reader_adapters_sources.cmake` and
append `XXFC_READER_ADAPTER_SOURCES`. Recursive format-source builds discover
the units normally. The `_reader_adapters.c` suffix keeps these units distinct
from the legacy `*_extractor.c` source discovery rules.

Registry rows and public declarations remain generated from the enum and reader
mapping. Update that mapping and regenerate when adding a type; do not edit a
generated binding directly. See [the format layout guide](../../../docs/FORMAT_LAYOUT.md).
