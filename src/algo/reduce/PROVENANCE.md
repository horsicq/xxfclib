# ZIP Reduce decoder provenance

This directory contains an independent C11 implementation written for xxfclib
in 2026 and released under the MIT license with the copyright notice present in
each source file.

The implementation was derived from the public file-format descriptions in:

- PKWARE, *APPNOTE.TXT — .ZIP File Format Specification*, descriptions of
  compression methods 2 through 5.
- Hans Wennborg, *Zip Files: History, Explanation and Implementation*, sections
  describing Reduce's follower-set coding and DLE expansion.

The XArchive and oldunzip source trees were reviewed only to assess existing
behavior and provenance. No source text, tables, control flow, or implementation
structure was copied from them. This implementation uses xxfclib's own I/O,
memory, cancellation, and progress APIs and was written directly from the
format-level algorithm.
