/* Reviewed bounded probes for the fifth group of fifty readers. */
{
    uint8_t header[512] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_arc reader;
        bool valid;
        xx_sfx_arc_init(&reader, dev, 0);
        valid = xx_sfx_arc_check_is_valid(&reader.format, NULL);
        xx_sfx_arc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ARC;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_arj reader;
        bool valid;
        xx_sfx_arj_init(&reader, dev, 0);
        valid = xx_sfx_arj_check_is_valid(&reader.format, NULL);
        xx_sfx_arj_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ARJ;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_bsn reader;
        bool valid;
        xx_sfx_bsn_init(&reader, dev, 0);
        valid = xx_sfx_bsn_check_is_valid(&reader.format, NULL);
        xx_sfx_bsn_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_BSN;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_arq reader;
        bool valid;
        xx_sfx_arq_init(&reader, dev, 0);
        valid = xx_sfx_arq_check_is_valid(&reader.format, NULL);
        xx_sfx_arq_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ARQ;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_gxl reader;
        bool valid;
        xx_sfx_gxl_init(&reader, dev, 0);
        valid = xx_sfx_gxl_check_is_valid(&reader.format, NULL);
        xx_sfx_gxl_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_GXL;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_asymetrix reader;
        bool valid;
        xx_sfx_asymetrix_init(&reader, dev, 0);
        valid = xx_sfx_asymetrix_check_is_valid(&reader.format, NULL);
        xx_sfx_asymetrix_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ASYMETRIX;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_rta reader;
        bool valid;
        xx_sfx_rta_init(&reader, dev, 0);
        valid = xx_sfx_rta_check_is_valid(&reader.format, NULL);
        xx_sfx_rta_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_RTA;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_rtpatch reader;
        bool valid;
        xx_sfx_rtpatch_init(&reader, dev, 0);
        valid = xx_sfx_rtpatch_check_is_valid(&reader.format, NULL);
        xx_sfx_rtpatch_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_RTPATCH;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_kwaj reader;
        bool valid;
        xx_sfx_kwaj_init(&reader, dev, 0);
        valid = xx_sfx_kwaj_check_is_valid(&reader.format, NULL);
        xx_sfx_kwaj_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_KWAJ;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_sqz reader;
        bool valid;
        xx_sfx_sqz_init(&reader, dev, 0);
        valid = xx_sfx_sqz_check_is_valid(&reader.format, NULL);
        xx_sfx_sqz_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_SQZ;
    }
}
