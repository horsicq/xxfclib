# Sources of the DIE signature engine and everything it links against.
# xxfclib's CMakeLists.txt builds them as the die_engine static library
# (die_engine.lib / libdie_engine.a, alias xxfclib::die_engine), a focused
# subset of the full archive for consumers that scan with DIE signatures.
#
# The list is the engine's exact link closure: starting from
# src/die_engine/*.c, every object whose symbols they (transitively) reference
# and nothing else. Outside it the engine needs only the OS libraries, cdisasm
# and compiler support. When the engine starts calling into another xxfclib
# module, add that module's sources here, or consumers of die_engine fail to
# link with unresolved xx_* symbols.
set(XXFC_DIE_ENGINE_SOURCES
    # Engine
    ${CMAKE_CURRENT_LIST_DIR}/src/die_engine/xx_die_engine_api.c
    ${CMAKE_CURRENT_LIST_DIR}/src/die_engine/xx_die_engine_bin.c
    ${CMAKE_CURRENT_LIST_DIR}/src/die_engine/xx_die_engine_compat.c
    ${CMAKE_CURRENT_LIST_DIR}/src/die_engine/xx_die_engine_db.c
    ${CMAKE_CURRENT_LIST_DIR}/src/die_engine/xx_die_engine_inflate.c
    ${CMAKE_CURRENT_LIST_DIR}/src/die_engine/xx_die_engine_literal_search.c
    ${CMAKE_CURRENT_LIST_DIR}/src/die_engine/xx_die_engine_result.c
    ${CMAKE_CURRENT_LIST_DIR}/src/die_engine/xx_die_engine_scan.c
    ${CMAKE_CURRENT_LIST_DIR}/src/die_engine/xx_die_engine_xdisasm.c
    ${CMAKE_CURRENT_LIST_DIR}/src/scan/xx_scan_types.c

    # Script interpreter
    ${CMAKE_CURRENT_LIST_DIR}/src/js/xx_js_builtins.c
    ${CMAKE_CURRENT_LIST_DIR}/src/js/xx_js_bytecode.c
    ${CMAKE_CURRENT_LIST_DIR}/src/js/xx_js_deps.c
    ${CMAKE_CURRENT_LIST_DIR}/src/js/xx_js_engine.c
    ${CMAKE_CURRENT_LIST_DIR}/src/js/xx_js_interp.c
    ${CMAKE_CURRENT_LIST_DIR}/src/js/xx_js_lex.c
    ${CMAKE_CURRENT_LIST_DIR}/src/js/xx_js_parse.c
    ${CMAKE_CURRENT_LIST_DIR}/src/js/xx_js_regexp.c
    ${CMAKE_CURRENT_LIST_DIR}/src/js/xx_js_value.c

    # Native format readers and inspection APIs used by signatures.
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/apk/xx_apk.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/apk/xx_apk_inspect.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/atarist/xx_atarist.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/dex/xx_dex.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/dex/xx_dex_inspect.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/dos16m/xx_dos16m.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/elf/xx_elf_inspect.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/gz/xx_gz.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/ipa/xx_ipa.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/iso9660/xx_iso9660.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/iso9660/xx_iso_zisofs_native.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/jar/xx_jar.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/jpeg/xx_jpeg.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/macho/xx_macho_inspect.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/npm/xx_npm.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/pdf/xxpdf.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/pdf/xxpdf_decode.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/pe/xx_pe_inspect.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/pe/xx_pe_stream_symbols.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/pe/xx_pe_stream_coff.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/pe/xx_pe_stream_metadata.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/pe/xx_pe_stream_common.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/pe/xx_pe.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/pe/xx_pe_data.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/dotnet/xx_dotnet.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/dotnet/xx_dotnet_inspect.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/dotnet/xx_dotnet_data.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/png/xx_png.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/pyc/xx_pyc.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/tar/xx_tar.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/tar_common/xx_tar_common.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/tar_gz/xx_tar_gz.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/zip/xx_zip.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/zip/xx_zip_inspect.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/zip/xx_zip_legacy_encoder.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/xz/xx_xz.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/xz/xx_xz_writer.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/7zip/xx_7zip_branch.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/xx_data_signature.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/xx_memory_map.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/xx_format_base.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/xx_format_streams.c

    # Algorithms
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/aes/xx_aes.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/aes_winzip/xx_aes_winzip.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/adler32/xx_adler32.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/adler32/platforms/xx_adler32_sse2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/adler32/platforms/xx_adler32_avx2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/bzip2/xx_bzip2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/bzip2/xx_bzip2_bwt.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/bzip2/xx_bzip2_dec.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/bzip2/xx_bzip2_enc.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/bzip2/xx_bzip2_huffman.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/bzip2/xx_bzip2_mtf.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/cmpsc/xx_cmpsc.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/cmpsc/xx_cmpsc_enc.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/crc/xx_crc.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/crc/xx_crc8.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/crc/xx_crc16.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/crc/xx_crc32.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/crc/xx_crc64.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/crc/platforms/xx_crc64_sse2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/crc/platforms/xx_crc64_avx2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/dcl/xx_dcl.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/deflate/xx_deflate.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/deflate/xx_deflate_dec.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/deflate/xx_deflate_enc.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/deflate/xx_zlib_stream.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/deflate/platforms/xx_deflate_sse2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/deflate/platforms/xx_deflate_avx2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/entropy/xx_entropy.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/entropy/platforms/xx_entropy_sse2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/entropy/platforms/xx_entropy_avx2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/hash/xx_hash.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/implode/xx_implode.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/kpa/xx_kpa.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/lzma/xx_lzma.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/lzma/xx_lzma_dec.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/lzma/xx_lzma_enc.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/lzma/xx_lzma_stream.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/lzma/xx_lzma2_filters.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/lzma/platforms/xx_lzma_sse2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/lzma/platforms/xx_lzma_avx2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/packmp3/xx_packmp3.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/ppmd7/xx_ppmd7.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/ppmd7/xx_ppmd7_common.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/ppmd7/xx_ppmd7_dec.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/ppmd7/xx_ppmd7_enc.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/ppmd8/xx_ppmd8.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/ppmd8/xx_ppmd8_common.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/ppmd8/xx_ppmd8_dec.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/ppmd8/xx_ppmd8_enc.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/reduce/xx_reduce.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/sha/xx_sha.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/sha/xx_sha_simd.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/shrink/xx_shrink.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/store/xx_store.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/wavpack/xx_wavpack.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/wavpack/xx_wavpack_dec.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/winzipjpeg/xx_winzipjpeg.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/zipcrypto/xx_zipcrypto.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/zstd/xx_zstd.c
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/zstd/xx_zstd_dec.c

    # Runtime
    ${CMAKE_CURRENT_LIST_DIR}/src/rt/xx_rt.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rt/xx_rt_fp.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rt/xx_rt_math.c
    ${CMAKE_CURRENT_LIST_DIR}/src/buf/xx_buf.c
    ${CMAKE_CURRENT_LIST_DIR}/src/json/xx_json.c
    ${CMAKE_CURRENT_LIST_DIR}/src/list/xx_list.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fs/xx_fs.c
    ${CMAKE_CURRENT_LIST_DIR}/src/global/xx_global.c
    ${CMAKE_CURRENT_LIST_DIR}/src/global/platforms/xx_global_cpu.c
    ${CMAKE_CURRENT_LIST_DIR}/src/terminal/xx_terminal.c
    ${CMAKE_CURRENT_LIST_DIR}/src/io/xx_io.c
    ${CMAKE_CURRENT_LIST_DIR}/src/io/xx_io_file.c
    ${CMAKE_CURRENT_LIST_DIR}/src/io/xx_io_mem.c
    ${CMAKE_CURRENT_LIST_DIR}/src/io/xx_io_memory_only.c
    ${CMAKE_CURRENT_LIST_DIR}/src/io/xx_io_sub.c
    ${CMAKE_CURRENT_LIST_DIR}/src/memory/xx_memory.c
    ${CMAKE_CURRENT_LIST_DIR}/src/memory/xx_memory_rt.c
    ${CMAKE_CURRENT_LIST_DIR}/src/memory/platforms/xx_memory_sse2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/memory/platforms/xx_memory_avx2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/strings/xx_string.c
    ${CMAKE_CURRENT_LIST_DIR}/src/var/xx_var.c
    ${CMAKE_CURRENT_LIST_DIR}/src/data/xx_pd.c
    ${CMAKE_CURRENT_LIST_DIR}/src/data/xx_data_raw.c
    ${CMAKE_CURRENT_LIST_DIR}/src/data/xx_data_io.c
    ${CMAKE_CURRENT_LIST_DIR}/src/data/platforms/xx_data_sse2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/data/platforms/xx_data_avx2.c
)

if(WIN32)
    list(APPEND XXFC_DIE_ENGINE_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/die_engine/platforms/xx_die_engine_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/io/platforms/xx_io_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/memory/platforms/xx_memory_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/strings/platforms/xx_string_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/fs/platforms/xx_fs_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/rt/platforms/xx_rt_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/global/platforms/xx_global_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/terminal/platforms/xx_terminal_windows.c
    )
else()
    list(APPEND XXFC_DIE_ENGINE_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/die_engine/platforms/xx_die_engine_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/io/platforms/xx_io_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/memory/platforms/xx_memory_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/strings/platforms/xx_string_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/fs/platforms/xx_fs_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/rt/platforms/xx_rt_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/global/platforms/xx_global_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/terminal/platforms/xx_terminal_posix.c
    )
endif()
