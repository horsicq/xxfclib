/* SPDX-License-Identifier: MIT
 * Executable archive carriers before broad SFX detection.
 * Ordered X-macro source: keep predicate and header boundaries in order.
 */

XX_FORMAT_PROBE_READ(512U)
XX_FORMAT_READER_PROBE(sfx_arc, XX_FILE_TYPE_SFX_ARC,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_arj, XX_FILE_TYPE_SFX_ARJ,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_bsn, XX_FILE_TYPE_SFX_BSN,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_arq, XX_FILE_TYPE_SFX_ARQ,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_gxl, XX_FILE_TYPE_SFX_GXL,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_asymetrix, XX_FILE_TYPE_SFX_ASYMETRIX,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_rta, XX_FILE_TYPE_SFX_RTA,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_rtpatch, XX_FILE_TYPE_SFX_RTPATCH,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_kwaj, XX_FILE_TYPE_SFX_KWAJ,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_sqz, XX_FILE_TYPE_SFX_SQZ,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))

XX_FORMAT_PROBE_READ(512U)
XX_FORMAT_READER_PROBE(sfx_arcv2, XX_FILE_TYPE_SFX_ARCV2,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_chz, XX_FILE_TYPE_SFX_CHZ,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_szdd, XX_FILE_TYPE_SFX_SZDD,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_mpq, XX_FILE_TYPE_SFX_MPQ,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_swag, XX_FILE_TYPE_SFX_SWAG,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_zpak, XX_FILE_TYPE_SFX_ZPAK,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_diskexpress, XX_FILE_TYPE_SFX_DISKEXPRESS,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_bzip2, XX_FILE_TYPE_SFX_BZIP2,
    (is_mz && (header_size >= 4 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x7f\x45\x4c\x46",4) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_gzip, XX_FILE_TYPE_SFX_GZIP,
    (is_mz && (header_size >= 4 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x7f\x45\x4c\x46",4) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_tar, XX_FILE_TYPE_SFX_TAR,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_cab, XX_FILE_TYPE_SFX_CAB,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(pmarc_sfx, XX_FILE_TYPE_PMARC_SFX,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x18\x0a",2) == 0 || xx_rt_memcmp(header+0,"\x18\x79",2) == 0 || xx_rt_memcmp(header+0,"\xeb\x18",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_7zip, XX_FILE_TYPE_SFX_7ZIP,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_ace, XX_FILE_TYPE_SFX_ACE,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_zipcentral, XX_FILE_TYPE_SFX_ZIPCENTRAL,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))

XX_FORMAT_PROBE_READ(512U)
XX_FORMAT_READER_PROBE(sfx_imp, XX_FILE_TYPE_SFX_IMP,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_red, XX_FILE_TYPE_SFX_RED,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_ha, XX_FILE_TYPE_SFX_HA,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_lzx, XX_FILE_TYPE_SFX_LZX,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_sqx, XX_FILE_TYPE_SFX_SQX,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_ain, XX_FILE_TYPE_SFX_AIN,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_hap, XX_FILE_TYPE_SFX_HAP,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_zoo, XX_FILE_TYPE_SFX_ZOO,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_cazip, XX_FILE_TYPE_SFX_CAZIP,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_tgcf, XX_FILE_TYPE_SFX_TGCF,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_starkit, XX_FILE_TYPE_SFX_STARKIT,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_alz, XX_FILE_TYPE_SFX_ALZ,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
XX_FORMAT_READER_PROBE(sfx_chm, XX_FILE_TYPE_SFX_CHM,
    (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))))
