# xxfc_unpack

Unpacks archives with xxfclib, driven with 7-Zip's command letters.

```
xxfc_unpack x <archive> [-o<dir>]     extract, keeping stored paths
xxfc_unpack l <archive>               list contents
xxfc_unpack t <archive>               test: extract to a scratch dir
xxfc_unpack a <archive> <file>...     add files to a new archive
```

```console
$ xxfc_unpack l photos.zip
photos.zip: ZIP
             0  tst/
            75  tst/1.txt
          7042  tst/crypted.pdf
3 member(s)

$ xxfc_unpack x photos.zip -oout
$ xxfc_unpack a backup.tar out/tst/1.txt out/tst/crypted.pdf
```

## What it reads, and what it writes

Reading covers the formats compiled into the library, including the twenty
registered installer families listed in `../gui/README.md` and the fifty
additional readers in `../../docs/registered_fifty_formats.md`. Writing covers eight containers, because that is all
xxfclib implements a packer for:

```
.tar  .tar.gz/.tgz  .tar.bz2/.tbz2  .tar.xz/.txz  .tar.zst  .tar.lz4
.zip  .cpio
```

`a` picks the container from the output file's extension and says so plainly
when the extension names something it cannot write.

## Extension-first fast detection

`xx_format_get_file_type_device_fast(device, source_path)` tries a static,
case-insensitive extension table first, with compound extensions before their
final suffix. Known extensions require no I/O, allocations or reader creation.
Unknown and ambiguous extensions use the unchanged content detector
`xx_format_get_file_type_device(device)`. Extension results are provisional;
the caller must validate the selected reader. Passing NULL for `source_path`
uses a file-backed device's source path. The extension-only API is
`xx_format_get_file_type_extension(source_path)`.

The table is generated from metadata of every registered reader. After adding
or changing reader extensions, build the offline `xxfc_dump_extensions` target
and run `python samples/unpack/generate_fast_extensions.py <dump-executable>`
from the library directory. XFileUnpacker also provides the offline target
`xfileunpacker_dump_extensions`. Reader constructors run only in that utility,
never in the extension detector.

## Two things worth knowing before copying this code

**The library has no "open whatever this is" call.** `xx_format_get_file_type_device()`
tells you what a file is, and `xx_zip_create()` / `xx_tar_create()` / â€¦ build
readers by name, but nothing joins the two. Any program handed an arbitrary
file has to supply that join, which is what `xxfc_readers.c` is: one entry per
reader, plus `xxfc_open()`.

That file is generated. Rather than writing down which `XX_FILE_TYPE_*` each
reader answers to â€” a mapping that is wrong for the readers whose enumerator
is spelled differently from their directory, and that goes stale as readers
are added â€” the table asks. Each reader is created once against an empty
device and queried for its file type. It costs one pass at startup and cannot
drift. A reader that settles its type only after parsing reports `UNKNOWN`
and never matches; that is visible rather than silently wrong.

**`e` is missing on purpose.** 7-Zip's `e` extracts without paths. xxfclib
places extracted files itself, from the names inside the archive, and this
program gets no say in the layout â€” so `e` would either be a lie or an alias
for `x`. It is neither.

`t` extracts to a scratch directory rather than decoding to nowhere: asking a
reader to unpack with no destination is answered "fine" without touching the
payload, so a test that skipped the write would pass on a corrupt archive.

## Building

Built with the rest of the samples when `XXFCLIB_BUILD_SAMPLES` is on.

```console
cmake -S . -B build -DXXFCLIB_BUILD_SAMPLES=ON
cmake --build build --target xxfc_unpack
```

## Regenerating the reader table

The next fifty formats (values 506â€“555), their supported subsets and integration
tests are listed in [registered_second_fifty_formats.md](../../docs/registered_second_fifty_formats.md).
Use `tests/check_second_fifty_formats.ps1` for a generated fixture replay of
every new family and `tests/second_fifty_formats_corpus.json` for the available
real archives. Component readers export encoded payloads rather than decode
media. Missing volumes/backing disks are reported conservatively.

`xxfc_readers.c` lists the readers the library actually compiles, which is not
the same as the headers it ships â€” some formats have a public header
but no entry in `CMakeLists.txt`, so their `xx_*_create` symbols do not exist
to link against. The table is derived from the `src/formats/<name>/xx_<name>.c`
entries in `CMakeLists.txt` for that reason.

[Third fifty format readers](../../docs/registered_third_fifty_formats.md) documents the next fifty supported families and their extraction limits.

[Fourth fifty format readers](../../docs/registered_fourth_fifty_formats.md) documents public types 606-655, their supported extraction limits, primary references and repeatable verification commands.

[Fifth fifty format readers](../../docs/registered_fifth_fifty_formats.md) documents public types 656-705, their supported extraction limits, primary references and repeatable verification commands.

[Sixth fifty format readers](../../docs/registered_sixth_fifty_formats.md) documents public types 706-755, their supported extraction limits, primary references and repeatable verification commands.

[Seventh fifty format readers](../../docs/registered_seventh_fifty_formats.md) documents public types 756-805, their supported extraction limits, primary references and repeatable verification commands.

[Eighth fifty format readers](../../docs/registered_eighth_fifty_formats.md) documents public types 806-855, their supported extraction limits, primary references and repeatable verification commands.

[Ninth fifty format readers](../../docs/registered_ninth_fifty_formats.md) documents types 856-905, supported variants, primary references and repeatable checks.
