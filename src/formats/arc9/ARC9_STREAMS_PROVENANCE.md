# Native ARC9 stream readers

Independent MIT C implementations; no installer or source script executes, and
no U3/7zip/Python/helper executable is required at runtime.

| Reader | Container grammar | Integrity and output |
| --- | --- | --- |
| ZXML | signature, packed/plain lengths, zlib | exact consumption/output, Adler32 |
| ZISOFS | zisofs header, complete block pointer table | independent zlib blocks, sparse zero blocks, exact output, Adler32 |
| SQZE | version1 LZSS stream and FILE records | packed CRC32, explicit end marker, exact record bounds, recovered names |
| NVP | version1 typed configuration/resource records | all record boundaries and EOF, meaningful typed payloads and embedded JPEG resources |
| ZIP PSC | original ZIP bytes complemented starting at0x3180 | borrowed seekable source view delegates to native ZIP codecs/password/CRC |
| CRX2/3 | exact Chromium header followed by ZIP | validated lengths/protobuf framing, native ZIP codecs/password/CRC |

CRX extraction verifies ZIP payload integrity; publisher signatures are not
authenticated. NVP has no known checksum. Bounded decoding and TEST operate
entirely in RAM. PM readers cap aggregate buffers at256MiB; ZIP views stream
through the established native reader. Borrowed devices remain owned by callers.

Primary grammar evidence:

- Linux zisofs-tools1.0.8 `mkzftree.c`: https://kernel.googlesource.com/pub/scm/fs/zisofs/zisofs-tools/+/zisofs-tools-1.0.8/mkzftree.c
- Chromium CRX3 schema: https://chromium.googlesource.com/chromium/src.git/+/62.0.3202.58/components/crx_file/crx3.proto
- Recovered U3 vendor-parser behavior, functions0064d770(ZXML),0060bfe0(zisofs),0056b0f0/0056b670(SQZE),0063ad80(NVP),0072b600(PSC). Algorithms and grammar were implemented independently.

The 2026-10-09 ARC9 corpus has2,469 files in these six groups. Every file passed
native RAM-only TEST and detection. All13,865 members/307,935,982 decoded bytes
matched independent Python zlib/zipfile and separately implemented SQZE/NVP
parsers by SHA256; all source-file hashes remained unchanged. Thirty-one
positive, corruption, size-limit and cancellation controls passed. These include
compressed empty ZIP directories, malformed CRCs, truncated/trailing/nonempty
empty-directory streams, sparse zisofs blocks and CRX protobuf framing.

Research harness, reports and fixtures are in the build tree's
`arc9-native-streams` directory; they are not runtime dependencies.
