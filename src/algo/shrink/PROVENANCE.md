# ZIP Shrink decoder provenance

This decoder is an independent C11 implementation written for xxfclib. No
source code was copied from XArchive, OldUnzip, Info-ZIP, or another decoder.

The implementation was derived from the format behavior documented by:

- PKWARE, `.ZIP File Format Specification` (`APPNOTE.TXT`), method 1.
- Hans Wennborg, “Shrink, Reduce, and Implode: The Legacy Zip Compression
  Methods”, <https://www.hanshq.net/zip2.html>.

The corpus archives in `prepare/tests/formats/zip` are used only as black-box
compatibility fixtures. This module is released under the MIT license in its
source header, Copyright (c) 2026 hors<horsicq@gmail.com>.
