# Zstandard codec provenance

This module was independently written for XXFCLIB in 2026 and is licensed
under the repository MIT license, with copyright held by
`hors<horsicq@gmail.com>`.

It contains no copied Zstandard implementation source and has no libzstd or
XArchive runtime dependency. The bounded C decoder implements standard and
skippable frame parsing, raw/RLE/compressed blocks, Huffman literals, FSE
sequence tables, repeated offsets, window checks, and frame checksums directly
from the public wire-format specification. Its streaming mode decodes block
by block into a sliding history buffer (one frame's window plus up to 16 MiB)
with an incremental XXH64; its window limit and header rules follow the
documented defaults of libzstd's streaming decoder, whose behaviour was
observed, not its source copied.

The encoder writes standards-compliant frames using raw blocks. This is a
portable baseline encoder: every requested compression level is accepted for
API compatibility, but the current implementation deliberately prioritizes a
small dependency-free implementation over entropy compression ratio.

The ZIP writer stores the resulting standard Zstandard frame as compression
method 93. The stream can be written directly or wrapped by the existing
traditional ZipCrypto or WinZip AES encryption implementations; no third-party
Zstandard or encryption implementation source is copied into XXFCLIB.

The wire-format reference is the official Zstandard compression format:
https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md
