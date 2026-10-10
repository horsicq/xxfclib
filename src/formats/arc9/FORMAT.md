# Native ARC9 readers

The public constructors, abstract extractors/detectors, reader registry and type
catalog are generated from `include/xxfclib/formats/arc9/xx_arc9_rows.inc`.
These readers enumerate and decode member data. They never execute the input,
launch an installer or require U3/UniExtract/vendor extraction executables.
TEST reads and decompresses into memory through the ordinary archive API.
Extraction writes only when an output path is supplied.
FAT and Starkit extraction now honors the overwrite option; the default uses
an exclusive file open so an existing destination is preserved.

## Implemented layouts

| Group | Formats | Decoding and integrity |
| --- | --- | --- |
| Streams | ZXML, ZISOFS, NVP, SQZE, ZIP PSC, CRX | Bounded zlib/deflate chunks, typed NVP resources, SQZE LZSS/XOR, transformed ZIP device, CRX2/3 header and protobuf framing; existing ZIP payload CRC and password handling |
| Compressed | DCS, MSKN1/2/3, CSQ, Zoot1, RDFZ, ZBEOS, Solaris compressed packages | LZMS, skin pixel/theme data, native bounded stream decoding, CPIO package members; declared sizes and available checksums |
| Games/resources | GTA IMG v2, PBO, PIRS/LIVE/CON STFS, PAM PAK, PFPK, PX, SBPAK, Titan Quest ARC, CGJP, Birdies, XUIZ | Actual stored/compressed resource bytes; PBO additive checksum, STFS SHA1 tree and block hashes, Titan Quest Adler32, bounded tables and normalized paths |
| Vendor containers | Shell-prefixed Starkit, EVD, DeskSoft, MetaProducts, VisualWare, Lyme SFX, Audials CMP, PSA diagnostic disk, WebExe | Embedded native Starkit/FAT, image slices, installer file tables, LH1, legacy and transformed gzip containers, zlib and ZIP local-record payloads; available CRC32/Adler32 and strict framing |
| Legacy compression | ASD, CFD, RDC, Fox SQZ, SKF, OSL2000, LDS | Native decoding, framing/length validation and available checksums; Fox SQZ embedded keys retained as password metadata |
| Disk wrappers | DCP, Sony TCJN images | Geometry/header validation, complete stored or RDC-decoded floppy images, Sony zlib payload and Adler32 validation |

DCS uses the Windows system Cabinet decompression API with raw LZMS. It has no
external executable dependency; DCS decoding is currently Windows-specific.
Other readers use the library's native codecs.

RDC has no magic signature. Generic automatic detection requires the original
`.??_` source-name hint and a successful structural probe. An unnamed memory
device can be opened with the explicit RDC reader.

Fox SQZ credential metadata uses `embedded-key:HEX` for its recovered binary
XOR key. This does not claim recovery of the original user-entered password.
The reader uses the embedded binary key automatically, regardless of whether
the caller supplies a password option.

WebExe contains successive ZIP local records followed by an opaque vendor
index. Opening its final embedded ZIP alone can report success for only a tiny
placeholder. The WebExe reader instead consumes the complete declared local
record region and verifies every payload CRC. Repeated placeholder names become
unique numbered portable leaves. Original website/document names from the
vendor index are not recovered.

PX exports every named resource value, including thumbnail/icon/style data and
intact resource envelopes. EVD exports its actual JPEG/PNG slices. DCP exports
the complete logical disk image; filesystem interpretation remains a separate
operation on that image. These are data-bearing results, not metadata stubs.

The two tested extended Atools Lyme variants, one older Fox SQZ variant, one
OSL2000 checksum mismatch, and one unsupported LDS variant remain rejected.
A file mislabeled CFD containing RSS is not treated as a CFD stream. A rejection
does not by itself establish that a file is corrupt.

## Validation evidence

Development evidence is preserved under the build directory:

- `arc9-native-streams`: all 2,469 corpus inputs, independent hashes of 13,865
  decoded members, plus malformed, checksum, cancellation and memory controls.
- `arc9-native-compressed`: 375 compressed inputs and 24 supported legacy
  inputs, independent decoded size/CRC checks and 74 negative controls.
- `arc9-tests/games-probe`: 151 game/resource/PSARC inputs, independent names,
  sizes and SHA256 for 20,215 records, plus 46 negative controls.
- `arc9-additions-20261009`: vendor-container tests, independent Python hashes
  of 3,248 members, U3 extraction comparison for all 286 MetaProducts members,
  RAM-only tests, extraction and traversal/checksum/cancellation controls.

Positive corpus checks retain original source hashes. Archive TEST checks use
blocked temporary-directory paths as well as the library's RAM-only I/O scope.
Reference extraction comparisons use dedicated output directories and never
execute extracted members.

Layout evidence came from the input bytes, independent U3/7-Zip/UniExtract
results, recovered U3 routine structure and existing native codec behavior.
Relevant primary specifications include Chromium's
[CRX3 protobuf](https://chromium.googlesource.com/chromium/src.git/+/62.0.3202.58/components/crx_file/crx3.proto)
and the [Metakit documentation](https://www.equi4.com/metakit/docs.html).
