# APK/DEX and managed PE/CLI readers for bytecode tools.
#
# xxdecompiler selects this profile for its native x86 and ARM builds too, so
# the PE inspector here carries the AMD64 exception table reader as well: a
# native decompiler needs the functions an image describes, not only its
# managed metadata -- and ELF, for the same reason: a Linux or Android binary's
# own symbols are where its function list comes from.
set(XXFC_EMUL_DEFINITIONS XXFC_PROFILE_APK_EMUL XXFC_PROFILE_DOTNET_EMUL)
set(XXFC_EMUL_FORMAT_SOURCES
    src/algo/store/xx_store.c
    src/algo/md5/xx_md5.c
    src/algo/tlsh/xx_tlsh.c
    src/strings/xx_string_casefold.c
    src/algo/deflate/xx_deflate.c src/algo/deflate/xx_deflate_dec.c
    src/algo/deflate/xx_deflate_enc.c
    src/algo/deflate/platforms/xx_deflate_sse2.c
    src/algo/deflate/platforms/xx_deflate_avx2.c
    src/algo/deflate/platforms/xx_deflate_neon.c
    src/formats/archive/zip/xx_zip.c src/formats/archive/zip/xx_zip_inspect.c
    src/formats/installer/apk/xx_apk.c src/formats/installer/apk/xx_apk_inspect.c
    src/formats/executable/dex/xx_dex.c src/formats/executable/dex/xx_dex_inspect.c
    src/formats/executable/elf/xx_elf.c
    src/formats/executable/elf/xx_elf_inspect.c src/formats/executable/elf/xx_elf_symbols.c
    src/formats/executable/elf/xx_elf_yx.c
    src/formats/executable/pe/xx_pe.c
    src/formats/executable/pe/xx_pe_inspect.c
    src/formats/executable/pe/xx_pe_unwind_validate.c
    src/formats/executable/pe/xx_pe_stream_common.c src/formats/executable/pe/xx_pe_export.c
    src/formats/executable/pe/xx_pe_metadata.c src/formats/executable/pe/xx_pe_import.c
    src/formats/executable/pe/xx_pe_yx.c    src/formats/executable/pe/xx_pe_yx_authenticode.c
    src/formats/executable/dotnet/xx_dotnet.c
    src/formats/executable/dotnet/platforms/xx_dotnet_us_simd.h
    src/formats/executable/dotnet/platforms/xx_dotnet_us_sse2.c
    src/formats/executable/dotnet/platforms/xx_dotnet_us_avx2.c
    src/formats/executable/dotnet/xx_dotnet_inspect.c src/formats/executable/dotnet/xx_dotnet_reader.c)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_emul_common.cmake")
