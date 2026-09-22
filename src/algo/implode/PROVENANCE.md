# ZIP Implode decoder provenance

This directory contains an independent C11 implementation written for xxfclib
in 2026 and released under the MIT license with the copyright notice present in
each source file.

The implementation was derived from the public file-format descriptions in:

- PKWARE, *APPNOTE.TXT — .ZIP File Format Specification*, description of
  compression method 6 and general-purpose flag bits 1 and 2.
- Hans Wennborg, *Zip Files: History, Explanation and Implementation*, sections
  describing Implode tree serialization, Shannon--Fano code ordering, tokens,
  distances, and lengths.

The XArchive and oldunzip source trees were reviewed only to assess existing
behavior and provenance. No source text, tables, control flow, or implementation
structure was copied from them. This implementation uses a small canonical-code
trie plus xxfclib's own I/O, memory, cancellation, and progress APIs, written
directly from the format-level algorithm.
