# Explicit PE/CLI reader sources for managed assembly consumers.
set(XXFC_EMUL_DEFINITIONS XXFC_PROFILE_DOTNET_EMUL)
set(XXFC_EMUL_FORMAT_SOURCES
    src/formats/executable/pe/xx_pe.c src/formats/executable/pe/xx_pe_datastructs.c
    src/formats/executable/pe/xx_pe_inspect.c
    src/formats/executable/pe/xx_pe_make.c
    src/formats/executable/pe/xx_pe_stream_common.c src/formats/executable/pe/xx_pe_export.c
    src/formats/executable/pe/xx_pe_metadata.c src/formats/executable/pe/xx_pe_import.c
    src/formats/executable/dotnet/xx_dotnet.c src/formats/executable/dotnet/xx_dotnet_datastructs.c
    src/formats/executable/dotnet/platforms/xx_dotnet_us_simd.h
    src/formats/executable/dotnet/platforms/xx_dotnet_us_sse2.c
    src/formats/executable/dotnet/platforms/xx_dotnet_us_avx2.c
    src/formats/executable/dotnet/xx_dotnet_inspect.c src/formats/executable/dotnet/xx_dotnet_reader.c
    src/formats/executable/dotnet/xx_dotnet_cil.c src/formats/executable/dotnet/xx_dotnet_edit.c
    src/formats/executable/dotnet/xx_dotnet_resource_reader.c
    src/algo/sha/xx_sha.c src/algo/sha/xx_sha_simd.c
    src/algo/sha/platforms/xx_sha_neon.c src/algo/sha/platforms/xx_sha_arm_crypto.c
    src/algo/deflate/xx_deflate.c src/algo/deflate/xx_zlib_stream.c
    src/algo/deflate/xx_deflate_dec.c src/algo/deflate/xx_deflate_enc.c
    src/algo/deflate/platforms/xx_deflate_sse2.c
    src/algo/deflate/platforms/xx_deflate_avx2.c
    src/algo/deflate/platforms/xx_deflate_neon.c)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_emul_common.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_dotnet_primitive_tests.cmake")
