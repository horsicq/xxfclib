# APK/DEX and managed PE/CLI readers for bytecode tools.
set(XXFC_EMUL_DEFINITIONS XXFC_PROFILE_APK_EMUL XXFC_PROFILE_DOTNET_EMUL)
set(XXFC_EMUL_FORMAT_SOURCES
    src/algo/store/xx_store.c
    src/algo/deflate/xx_deflate.c src/algo/deflate/xx_deflate_dec.c
    src/algo/deflate/xx_deflate_enc.c
    src/algo/deflate/platforms/xx_deflate_sse2.c
    src/algo/deflate/platforms/xx_deflate_avx2.c
    src/formats/zip/xx_zip.c src/formats/zip/xx_zip_inspect.c
    src/formats/apk/xx_apk.c src/formats/apk/xx_apk_inspect.c
    src/formats/dex/xx_dex.c src/formats/dex/xx_dex_inspect.c
    src/formats/pe/xx_pe.c src/formats/pe/xx_pe_data.c
    src/formats/pe/xx_pe_inspect.c
    src/formats/pe/xx_pe_stream_common.c src/formats/pe/xx_pe_stream_coff.c
    src/formats/pe/xx_pe_stream_metadata.c src/formats/pe/xx_pe_stream_symbols.c
    src/formats/dotnet/xx_dotnet.c src/formats/dotnet/xx_dotnet_data.c
    src/formats/dotnet/xx_dotnet_inspect.c src/formats/dotnet/xx_dotnet_reader.c)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_emul_common.cmake")
