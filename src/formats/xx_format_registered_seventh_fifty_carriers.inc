/* Reviewed bounded probes for the seventh group of fifty readers. */
{
    uint8_t header[512] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_imp reader;
        bool valid;
        xx_sfx_imp_init(&reader, dev, 0);
        valid = xx_sfx_imp_check_is_valid(&reader.format, NULL);
        xx_sfx_imp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_IMP;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_red reader;
        bool valid;
        xx_sfx_red_init(&reader, dev, 0);
        valid = xx_sfx_red_check_is_valid(&reader.format, NULL);
        xx_sfx_red_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_RED;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_ha reader;
        bool valid;
        xx_sfx_ha_init(&reader, dev, 0);
        valid = xx_sfx_ha_check_is_valid(&reader.format, NULL);
        xx_sfx_ha_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_HA;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_lzx reader;
        bool valid;
        xx_sfx_lzx_init(&reader, dev, 0);
        valid = xx_sfx_lzx_check_is_valid(&reader.format, NULL);
        xx_sfx_lzx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_LZX;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_sqx reader;
        bool valid;
        xx_sfx_sqx_init(&reader, dev, 0);
        valid = xx_sfx_sqx_check_is_valid(&reader.format, NULL);
        xx_sfx_sqx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_SQX;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_ain reader;
        bool valid;
        xx_sfx_ain_init(&reader, dev, 0);
        valid = xx_sfx_ain_check_is_valid(&reader.format, NULL);
        xx_sfx_ain_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_AIN;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_hap reader;
        bool valid;
        xx_sfx_hap_init(&reader, dev, 0);
        valid = xx_sfx_hap_check_is_valid(&reader.format, NULL);
        xx_sfx_hap_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_HAP;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_zoo reader;
        bool valid;
        xx_sfx_zoo_init(&reader, dev, 0);
        valid = xx_sfx_zoo_check_is_valid(&reader.format, NULL);
        xx_sfx_zoo_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ZOO;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_cazip reader;
        bool valid;
        xx_sfx_cazip_init(&reader, dev, 0);
        valid = xx_sfx_cazip_check_is_valid(&reader.format, NULL);
        xx_sfx_cazip_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_CAZIP;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_tgcf reader;
        bool valid;
        xx_sfx_tgcf_init(&reader, dev, 0);
        valid = xx_sfx_tgcf_check_is_valid(&reader.format, NULL);
        xx_sfx_tgcf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_TGCF;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_starkit reader;
        bool valid;
        xx_sfx_starkit_init(&reader, dev, 0);
        valid = xx_sfx_starkit_check_is_valid(&reader.format, NULL);
        xx_sfx_starkit_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_STARKIT;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_alz reader;
        bool valid;
        xx_sfx_alz_init(&reader, dev, 0);
        valid = xx_sfx_alz_check_is_valid(&reader.format, NULL);
        xx_sfx_alz_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ALZ;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_chm reader;
        bool valid;
        xx_sfx_chm_init(&reader, dev, 0);
        valid = xx_sfx_chm_check_is_valid(&reader.format, NULL);
        xx_sfx_chm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_CHM;
    }
}
