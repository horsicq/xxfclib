# Explicit APK/DEX reader dependency closure; no registry or unrelated codecs.
set(XXFC_EMUL_DEFINITIONS XXFC_PROFILE_APK_EMUL)
set(XXFC_EMUL_FORMAT_SOURCES
    src/algo/store/xx_store.c
    src/algo/deflate/xx_deflate.c src/algo/deflate/xx_deflate_dec.c
    src/algo/deflate/xx_deflate_enc.c
    src/algo/deflate/platforms/xx_deflate_sse2.c
    src/algo/deflate/platforms/xx_deflate_avx2.c
    src/formats/zip/xx_zip.c src/formats/zip/xx_zip_inspect.c
    src/formats/apk/xx_apk.c src/formats/apk/xx_apk_inspect.c
    src/formats/dex/xx_dex.c src/formats/dex/xx_dex_inspect.c)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_emul_common.cmake")
set(XXFC_APK_EMUL_SOURCES ${XXFC_EMUL_SOURCES})
