# Traditional ZipCrypto implementation provenance

This module is an independent C11 implementation written for xxfclib in 2026.
It was implemented from the traditional PKWARE encryption description in the
ZIP File Format Application Note and interoperability behavior documented by
Info-ZIP. No source code or lookup tables were copied from XArchive or another
third-party implementation.

The implementation uses the standardized three-key state machine, computes the
reflected CRC-32 byte transition directly, creates and verifies the 12-byte
header, and supports the data-descriptor modification-time verifier rule. The
encryption API accepts caller-supplied random header bytes and does not embed a
random-number generator.

The source and public header are distributed under the MIT license contained in
their file headers, with Copyright (c) 2026 hors<horsicq@gmail.com>.
