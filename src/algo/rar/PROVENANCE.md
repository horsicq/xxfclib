# RAR decompressor provenance

`xx_rarx` is an original C11 implementation released under the project's MIT
license and copyright. It is not copied from, mechanically translated from, or
derived from the proprietary UnRAR source code.

Compatibility behavior was checked against the local XArchive RAR decoder and
against archives produced by WinRAR and extracted by 7-Zip. The implementation
itself was designed from the public clean-room format descriptions in
[`bitplane/rar-research`](https://github.com/bitplane/rar-research) and checked
against the independently developed MIT/Apache-2.0
[`bitplane/rars`](https://github.com/bitplane/rars) project. The local BSD
libarchive readers were used only to cross-check container and error-handling
behavior.

The XArchive decoder closely follows UnRAR internally, so its source is not a
permitted implementation source for this module even though the local copy has
an MIT-style file header.

RAR3 PPMd-H reuses xxfclib's existing offset-based PPMd variant-H model core,
whose underlying algorithm and 7-Zip reference implementation are public
domain. `xx_rarx29.c` adds an independently written bounded adapter for RAR's
carry-less range coder and archive escape commands. Compatibility vectors and
the expanded archive corpus include Apache-2.0 fixtures from `bitplane/rars`;
their redistribution license is retained beside the fixtures.
