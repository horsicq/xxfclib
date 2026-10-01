/* Reviewed bounded probes for the thirteenth group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 8 && !xx_rt_memcmp(header,"\xfe\xed\xfe\xed\x00\x00\x00\x02",8)) {
        xx_jks_keystore reader;
        bool valid;
        xx_jks_keystore_init(&reader, dev, 0);
        valid = xx_jks_keystore_check_is_valid(&reader.format, NULL);
        xx_jks_keystore_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_JKS_KEYSTORE;
    }
    if (header_size >= 4 && !xx_rt_memcmp(header,"\xac\xed\x00\x05",4)) {
        xx_java_serialization reader;
        bool valid;
        xx_java_serialization_init(&reader, dev, 0);
        valid = xx_java_serialization_check_is_valid(&reader.format, NULL);
        xx_java_serialization_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_JAVA_SERIALIZATION;
    }
    if (header_size >= 28 && !xx_rt_memcmp(header,"\x23\x20\x47\x44\x42\x4d\x20\x64\x75\x6d\x70\x20\x66\x69\x6c\x65\x20\x63\x72\x65\x61\x74\x65\x64\x20\x62\x79\x20",28)) {
        xx_gdbm_dump reader;
        bool valid;
        xx_gdbm_dump_init(&reader, dev, 0);
        valid = xx_gdbm_dump_check_is_valid(&reader.format, NULL);
        xx_gdbm_dump_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GDBM_DUMP;
    }
    if (total_size >= 32 && header_size >= 4U && !xx_rt_memcmp(header,"\x38\x42\x43\x42",4U)) {
        xx_adobe_acb reader;
        bool valid;
        xx_adobe_acb_init(&reader, dev, 0);
        valid = xx_adobe_acb_check_is_valid(&reader.format, NULL);
        xx_adobe_acb_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADOBE_ACB;
    }
    if (total_size >= 16 && header_size >= 8U && !xx_rt_memcmp(header,"\x4a\x41\x53\x43\x2d\x50\x41\x4c",8U)) {
        xx_jasc_palette reader;
        bool valid;
        xx_jasc_palette_init(&reader, dev, 0);
        valid = xx_jasc_palette_check_is_valid(&reader.format, NULL);
        xx_jasc_palette_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_JASC_PALETTE;
    }
    if (total_size >= 14 && header_size >= 3U && !xx_rt_memcmp(header,"\x50\x47\x20",3U)) {
        xx_jpeg2000_pgx reader;
        bool valid;
        xx_jpeg2000_pgx_init(&reader, dev, 0);
        valid = xx_jpeg2000_pgx_check_is_valid(&reader.format, NULL);
        xx_jpeg2000_pgx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_JPEG2000_PGX;
    }
    if (total_size >= 78 && header_size >= 4U && !xx_rt_memcmp(header,"\xe3\x10\x00\x01",4U)) {
        xx_amiga_diskobject reader;
        bool valid;
        xx_amiga_diskobject_init(&reader, dev, 0);
        valid = xx_amiga_diskobject_check_is_valid(&reader.format, NULL);
        xx_amiga_diskobject_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AMIGA_DISKOBJECT;
    }
    if (total_size >= 12 && header_size >= 2U && !xx_rt_memcmp(header,"\xf7\xca",2U)) {
        xx_tex_vf reader;
        bool valid;
        xx_tex_vf_init(&reader, dev, 0);
        valid = xx_tex_vf_check_is_valid(&reader.format, NULL);
        xx_tex_vf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TEX_VF;
    }
    if (total_size >= 24 && header_size >= 4U && !xx_rt_memcmp(header,"\x44\x53\x41\x41",4U)) {
        xx_surfer_grid reader;
        bool valid;
        xx_surfer_grid_init(&reader, dev, 0);
        valid = xx_surfer_grid_check_is_valid(&reader.format, NULL);
        xx_surfer_grid_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SURFER_GRID;
    }
    if (total_size >= 24 && header_size >= 13U && !xx_rt_memcmp(header,"\x49\x53\x4f\x2d\x31\x30\x33\x30\x33\x2d\x32\x31\x3b",13U)) {
        xx_step_part21 reader;
        bool valid;
        xx_step_part21_init(&reader, dev, 0);
        valid = xx_step_part21_check_is_valid(&reader.format, NULL);
        xx_step_part21_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_STEP_PART21;
    }
    if (total_size >= 16 && header_size >= 3U && !xx_rt_memcmp(header,"\x4d\x34\x38",3U)) {
        xx_excellon_drill reader;
        bool valid;
        xx_excellon_drill_init(&reader, dev, 0);
        valid = xx_excellon_drill_check_is_valid(&reader.format, NULL);
        xx_excellon_drill_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_EXCELLON_DRILL;
    }
    if (total_size >= 24 && header_size >= 15U && !xx_rt_memcmp(header,"\x23\x56\x52\x4d\x4c\x20\x56\x32\x2e\x30\x20\x75\x74\x66\x38",15U)) {
        xx_vrml_scene reader;
        bool valid;
        xx_vrml_scene_init(&reader, dev, 0);
        valid = xx_vrml_scene_check_is_valid(&reader.format, NULL);
        xx_vrml_scene_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VRML_SCENE;
    }
    if ((header_size >= 24 && xx_rt_memcmp(header + 0, "\x41\x53\x59\x4c\x55\x4d\x20\x4d\x75\x73\x69\x63\x20\x46\x6f\x72\x6d\x61\x74\x20\x56\x31\x2e\x30", 24) == 0)) {
        xx_asylum_amf reader;
        bool valid;
        xx_asylum_amf_init(&reader, dev, 0);
        valid = xx_asylum_amf_check_is_valid(&reader.format, NULL);
        xx_asylum_amf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ASYLUM_AMF;
    }
    if (((header_size >= 28 && xx_rt_memcmp(header + 20, "\x21\x53\x63\x72\x65\x61\x6d\x21", 8) == 0) || (header_size >= 28 && xx_rt_memcmp(header + 20, "\x42\x4d\x4f\x44\x32\x53\x54\x4d", 8) == 0)) && header_size >= 64 && xx_rt_memcmp(header + 60, "SCRM", 4) == 0) {
        xx_tracker_stx reader;
        bool valid;
        xx_tracker_stx_init(&reader, dev, 0);
        valid = xx_tracker_stx_check_is_valid(&reader.format, NULL);
        xx_tracker_stx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_STX;
    }
    if ((header_size >= 4 && xx_rt_memcmp(header + 0, "\x44\x2e\x54\x2e", 4) == 0)) {
        xx_tracker_dtm reader;
        bool valid;
        xx_tracker_dtm_init(&reader, dev, 0);
        valid = xx_tracker_dtm_check_is_valid(&reader.format, NULL);
        xx_tracker_dtm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_DTM;
    }
    if ((header_size >= 64 && xx_rt_memcmp(header + 60, "\x53\x4f\x4e\x47", 4) == 0) || (header_size >= 128 && xx_rt_memcmp(header + 124, "\x53\x4f\x4e\x47", 4) == 0)) {
        xx_tracker_soundfx reader;
        bool valid;
        xx_tracker_soundfx_init(&reader, dev, 0);
        valid = xx_tracker_soundfx_check_is_valid(&reader.format, NULL);
        xx_tracker_soundfx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_SOUNDFX;
    }
    if ((header_size >= 4 && xx_rt_memcmp(header + 0, "\x46\x75\x6e\x6b", 4) == 0)) {
        xx_tracker_funk reader;
        bool valid;
        xx_tracker_funk_init(&reader, dev, 0);
        valid = xx_tracker_funk_check_is_valid(&reader.format, NULL);
        xx_tracker_funk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_FUNK;
    }
    if ((header_size >= 4 && xx_rt_memcmp(header + 0, "\x4d\x55\x53\x58", 4) == 0)) {
        xx_tracker_archimedes reader;
        bool valid;
        xx_tracker_archimedes_init(&reader, dev, 0);
        valid = xx_tracker_archimedes_check_is_valid(&reader.format, NULL);
        xx_tracker_archimedes_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_ARCHIMEDES;
    }
    if ((header_size >= 4 && xx_rt_memcmp(header + 0, "\x50\x53\x49\x20", 4) == 0)) {
        xx_pce_psi reader;
        bool valid;
        xx_pce_psi_init(&reader, dev, 0);
        valid = xx_pce_psi_check_is_valid(&reader.format, NULL);
        xx_pce_psi_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PCE_PSI;
    }
    if (header_size >= 32 && header[16] == 0 && header[17] == 0 && (header[26] == 0 || header[26] == 16) && (header[27] == 0 || header[27] == 16 || header[27] == 32)) {
        xx_pc98_d88 reader;
        bool valid;
        xx_pc98_d88_init(&reader, dev, 0);
        valid = xx_pc98_d88_check_is_valid(&reader.format, NULL);
        xx_pc98_d88_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PC98_D88;
    }
    if ((header_size >= 7 && xx_rt_memcmp(header + 0, "\x48\x58\x43\x4d\x46\x4d\x00", 7) == 0)) {
        xx_hxc_mfm reader;
        bool valid;
        xx_hxc_mfm_init(&reader, dev, 0);
        valid = xx_hxc_mfm_check_is_valid(&reader.format, NULL);
        xx_hxc_mfm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_HXC_MFM;
    }
    if ((header_size >= 10 && xx_rt_memcmp(header + 0, "\x3c\x43\x50\x4d\x5f\x44\x69\x73\x6b\x3e", 10) == 0)) {
        xx_yaze_ydsk reader;
        bool valid;
        xx_yaze_ydsk_init(&reader, dev, 0);
        valid = xx_yaze_ydsk_check_is_valid(&reader.format, NULL);
        xx_yaze_ydsk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_YAZE_YDSK;
    }
}
