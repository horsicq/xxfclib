/* Reviewed bounded probes for the sixth group of fifty readers. */
{
    uint8_t header[512] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0))) {
        xx_sfx_arcv2 reader;
        bool valid;
        xx_sfx_arcv2_init(&reader, dev, 0);
        valid = xx_sfx_arcv2_check_is_valid(&reader.format, NULL);
        xx_sfx_arcv2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ARCV2;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0))) {
        xx_sfx_chz reader;
        bool valid;
        xx_sfx_chz_init(&reader, dev, 0);
        valid = xx_sfx_chz_check_is_valid(&reader.format, NULL);
        xx_sfx_chz_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_CHZ;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_szdd reader;
        bool valid;
        xx_sfx_szdd_init(&reader, dev, 0);
        valid = xx_sfx_szdd_check_is_valid(&reader.format, NULL);
        xx_sfx_szdd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_SZDD;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_mpq reader;
        bool valid;
        xx_sfx_mpq_init(&reader, dev, 0);
        valid = xx_sfx_mpq_check_is_valid(&reader.format, NULL);
        xx_sfx_mpq_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_MPQ;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0))) {
        xx_sfx_swag reader;
        bool valid;
        xx_sfx_swag_init(&reader, dev, 0);
        valid = xx_sfx_swag_check_is_valid(&reader.format, NULL);
        xx_sfx_swag_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_SWAG;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_zpak reader;
        bool valid;
        xx_sfx_zpak_init(&reader, dev, 0);
        valid = xx_sfx_zpak_check_is_valid(&reader.format, NULL);
        xx_sfx_zpak_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ZPAK;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_diskexpress reader;
        bool valid;
        xx_sfx_diskexpress_init(&reader, dev, 0);
        valid = xx_sfx_diskexpress_check_is_valid(&reader.format, NULL);
        xx_sfx_diskexpress_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_DISKEXPRESS;
    }
    if (is_mz && (header_size >= 4 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x7f\x45\x4c\x46",4) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_bzip2 reader;
        bool valid;
        xx_sfx_bzip2_init(&reader, dev, 0);
        valid = xx_sfx_bzip2_check_is_valid(&reader.format, NULL);
        xx_sfx_bzip2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_BZIP2;
    }
    if (is_mz && (header_size >= 4 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x7f\x45\x4c\x46",4) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_gzip reader;
        bool valid;
        xx_sfx_gzip_init(&reader, dev, 0);
        valid = xx_sfx_gzip_check_is_valid(&reader.format, NULL);
        xx_sfx_gzip_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_GZIP;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_tar reader;
        bool valid;
        xx_sfx_tar_init(&reader, dev, 0);
        valid = xx_sfx_tar_check_is_valid(&reader.format, NULL);
        xx_sfx_tar_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_TAR;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_cab reader;
        bool valid;
        xx_sfx_cab_init(&reader, dev, 0);
        valid = xx_sfx_cab_check_is_valid(&reader.format, NULL);
        xx_sfx_cab_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_CAB;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x18\x0a",2) == 0 || xx_rt_memcmp(header+0,"\x18\x79",2) == 0 || xx_rt_memcmp(header+0,"\xeb\x18",2) == 0))) {
        xx_pmarc_sfx reader;
        bool valid;
        xx_pmarc_sfx_init(&reader, dev, 0);
        valid = xx_pmarc_sfx_check_is_valid(&reader.format, NULL);
        xx_pmarc_sfx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PMARC_SFX;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_7zip reader;
        bool valid;
        xx_sfx_7zip_init(&reader, dev, 0);
        valid = xx_sfx_7zip_check_is_valid(&reader.format, NULL);
        xx_sfx_7zip_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_7ZIP;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_ace reader;
        bool valid;
        xx_sfx_ace_init(&reader, dev, 0);
        valid = xx_sfx_ace_check_is_valid(&reader.format, NULL);
        xx_sfx_ace_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ACE;
    }
    if (is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_zipcentral reader;
        bool valid;
        xx_sfx_zipcentral_init(&reader, dev, 0);
        valid = xx_sfx_zipcentral_check_is_valid(&reader.format, NULL);
        xx_sfx_zipcentral_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ZIPCENTRAL;
    }
}
