# Native DGCA reader

The reader is independently written C. It reads borrowed positional input,
retains decoded metadata and payloads in caller-budgeted memory, and uses no
filesystem, process, external decoder, platform crypto or compression library.
Every dynamic allocation is supplied and released through `dg_callbacks`.
Cancellation is polled while reading, checksumming, initializing probability
models, decoding symbols and matches, transforming blocks, and copying output.
The Abstractformat adapter supplies the default 256 MiB budget for the decoder's
retained data and workspace, plus a 65,535 member limit. Generic reading-state
and record metadata, and an explicitly requested extraction STORE buffer, are
accounted separately. The core accepts explicit member and count limits.

## Container

Chunks have a 32-byte little-endian header: FourCC, header size, 64-bit payload
size, flags, alignment, payload CRC32, and header CRC32. Alignment is 16 bytes.
The IEEE header CRC32 includes the payload CRC32 and replaces only the final
header CRC32 field with zero. Payload CRC32 excludes padding after that chunk;
container payload CRC32 includes the nested chunks and their padding.

`DGCA` contains `DATA` and `INFO`. `INFO` contains an 80-byte `IARC` and two
nested `DGCC` streams, `IFDT` and `IFNM`. Accepted IARC versions are exactly
`" 001"`, `" 990"` and `" a90"`. IFDT stores 64-byte records transposed by byte plane;
IFNM stores 32-bit byte lengths followed by UTF-8 member names. Member records
provide CRC32, Windows attributes, FILETIME, raw size, packed allocation size,
DATA-relative solid group offset, and uncompressed offset within that group.
Packed allocation attribution need not equal the span of the first DGCC block.
The reader decodes successive DGCC blocks to satisfy a member slice, including
slices crossing blocks and empty blocks. A single charged decoded-block cache
reuses a block shared by successive members; an unusable block is released
before allocating the next member. It verifies each member CRC32 and the
aggregate IARC raw size, and rejects invalid/truncated sizes or trailing names.
Path safety is enforced by the Abstractformat adapter before extraction.

## Payload methods

* Method 0 contains a 32-bit raw size and stored bytes.
* Method 1 contains raw size, flags, packed parameter nibbles, BWT primary
  index, literal count, and two stream lengths. The first stream uses a 31-bit
  range coder and adaptive binary probability trees. It combines global and
  previous-byte trees with adaptive mixture weights. Depending on parameters,
  it suppresses the preceding byte and represents repetitions with an adaptive
  magnitude code, or uses flat byte coding. A requested inverse BWT transforms
  decoded literals. The second stream drives a 65,536-entry byte-context LZP
  dictionary and overlap-safe match copies.
* Method 2 contains a stride and one method 0/1 unit per plane. Unequal plane
  lengths are validated. Up to two cumulative byte-integration passes apply
  to each plane, then the planes are interleaved to reconstruct the output.

The printable words sometimes seen in method 1 are packed numerical
parameters, not signatures. Current producer cases cover literal/run/flat
models, BWT, LZP, byte integration, and stride planes. Unknown methods and
invalid flag/filter combinations fail explicitly.

## Password handling

The independent cipher implements standard SHA-512 and MT19937 plus the
archive's byte-feedback transformation. UTF-8 password bytes are used without
an ANSI conversion. Encrypted IARC bytes 32..79 use an eight-zero-byte seed;
the first eight decrypted bytes verify the password. An encrypted DGCC header
uses the same zero seed. Its body starts a fresh cipher seeded with the clear
32-byte header. Header and body CRCs then verify the decrypted data. Outer
container CRCs verify the original ciphertext. A missing password fails before
encrypted stream decode; an incorrect password fails validation. Empty strings
remain explicit password attempts. Cipher state is cleared before release.

## Provenance and licensing

All native modules are new implementations under the repository's MIT license.
No original DGCAC/DGCA executable, SDK binary/resource, proprietary source,
translated instruction sequence, or GPL example source is included or required.
Format facts were established from archive bytes, controlled original-producer
experiments, intermediate-memory observations, and narrow interoperability
analysis of an unchanged locally owned decoder. Mathematical facts were then
implemented independently and checked against exact decoded data.

The author's historical GCA description identifies block sorting and a modified
adaptive range encoder. Its [archived SDK page](https://web.archive.org/web/20020204033847id_/http://www.emit.jp/gca/gcasdk.html)
does not provide a reusable source grant. The [author's DGCA download page](https://web.archive.org/web/20030623034015id_/http://www.emit.jp/dgca/dgca_e.html)
and archived sample data establish format provenance. Public ERISA/GARbro and
Schindler example implementations were researched as candidate explanations;
their stock models did not decode DGCA and their source is not included here.

## Verification and limits

`full-native-verification.json` records 1,122 checks and 383 complete archive
cases: six original stored/solid/non-solid/password/Unicode fixtures with
1/17/4096-byte reads, 33 stored generated inputs, 45 compression patterns,
256 constant-byte patterns, 24 stored cipher vectors, synthetic cross-block
solid groups, empty archives/files/directories, and malformed/cancel/limit
controls. Every retained name and payload is compared exactly where a fixture
expectation is available. Callback allocations are checked for complete cleanup.
`codec-review` adds independent mutation, canary and protected-page verification;
`container-review` checks repaired-CRC invalid records and bounded positional IO.
`historical-native-verification.json` adds 45 checks over four unchanged
2002–2003 first-party author archives with 17/4096-byte reads and verifies all
member names, sizes, payload hashes and stored CRCs. Their versions `" 990"`
and `" a90"` share the validated 80-byte IARC and 64-byte transposed record
layout. The 55-member genji sample now decodes its shared 2,513,560-byte block
once, reducing callback cancellation polls from 86.7 million to 1.61 million.
Unknown version tags still fail before payload decode.

Recovery repair, split volume assembly, key-file credentials, archive writing, and
self-extracting wrapper discovery are not implemented by this reader. The
adapter retains a decoded archive cache; large archives require a matching
memory budget. Groups spanning multiple compressed blocks may require decoding
earlier blocks again for later members; cancellation remains available throughout.
