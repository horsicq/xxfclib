# AES implementation provenance

This module is an independent C11 implementation written for xxfclib in 2026.
It follows the public WinZip AES Encryption Specification (AE-1/AE-2), FIPS 197
for AES, RFC 2104 for HMAC, and PBKDF2 as specified by PKCS #5/RFC 2898. No
source code or lookup tables were copied from XArchive or another third-party
implementation.

The AES substitution box is generated from the Rijndael finite-field inverse
and affine transform when a key is initialized; no third-party table is
embedded. The module creates and validates the two-byte password verifier and
truncated ten-byte HMAC-SHA1 authentication code, authenticates before
releasing decrypted compressed data, accepts zero-length encrypted payloads,
and clears internal secret state. The encryption API accepts a caller-supplied
salt and does not embed a random-number generator.

The 7-Zip path is also an independent C11 implementation. It implements the
documented 06 F1 07 01 coder property layout, UTF-16LE password key derivation
with SHA-256, and AES-256-CBC decryption with the coder output size used to
discard block padding. XArchive was consulted only as a behavioral reference;
no source code or lookup tables were copied. SHA-256 and inverse AES operations
were written specifically for this module, and the AES substitution tables are
still generated at run time.

The source and public header are distributed under the MIT license contained in
their file headers, with Copyright (c) 2026 hors<horsicq@gmail.com>.
