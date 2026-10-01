/* Reviewed bounded probes for the eleventh group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 21 && !xx_rt_memcmp(header, "\x0d\x00\x00\x00\x00\x00\x00\x00\x6e\x69\x78\x2d\x61\x72\x63\x68\x69\x76\x65\x2d\x31", 21)) {
        xx_nix_nar reader;
        bool valid;
        xx_nix_nar_init(&reader, dev, 0);
        valid = xx_nix_nar_check_is_valid(&reader.format, NULL);
        xx_nix_nar_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NIX_NAR;
    }
    if (header_size >= 5 && !xx_rt_memcmp(header, "\x52\x45\x44\x49\x53", 5)) {
        xx_redis_rdb reader;
        bool valid;
        xx_redis_rdb_init(&reader, dev, 0);
        valid = xx_redis_rdb_check_is_valid(&reader.format, NULL);
        xx_redis_rdb_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_REDIS_RDB;
    }
    if (header_size >= 5 && !xx_rt_memcmp(header, "\x50\x47\x44\x4d\x50", 5)) {
        xx_postgres_custom reader;
        bool valid;
        xx_postgres_custom_init(&reader, dev, 0);
        valid = xx_postgres_custom_check_is_valid(&reader.format, NULL);
        xx_postgres_custom_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_POSTGRES_CUSTOM;
    }
    if (header_size >= 4 && !xx_rt_memcmp(header, "\xfe\x62\x69\x6e", 4)) {
        xx_mysql_binlog reader;
        bool valid;
        xx_mysql_binlog_init(&reader, dev, 0);
        valid = xx_mysql_binlog_check_is_valid(&reader.format, NULL);
        xx_mysql_binlog_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MYSQL_BINLOG;
    }
    if (header_size >= 4 && !xx_rt_memcmp(header, "\x03\x00\x08\x00", 4)) {
        xx_android_binary_xml reader;
        bool valid;
        xx_android_binary_xml_init(&reader, dev, 0);
        valid = xx_android_binary_xml_check_is_valid(&reader.format, NULL);
        xx_android_binary_xml_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ANDROID_BINARY_XML;
    }
    if (header_size >= 4 && !xx_rt_memcmp(header, "\x02\x00\x0c\x00", 4)) {
        xx_android_resources_arsc reader;
        bool valid;
        xx_android_resources_arsc_init(&reader, dev, 0);
        valid = xx_android_resources_arsc_check_is_valid(&reader.format, NULL);
        xx_android_resources_arsc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ANDROID_RESOURCES_ARSC;
    }
    if (header_size >= 1 && !xx_rt_memcmp(header, "\x83", 1)) {
        xx_erlang_external_term reader;
        bool valid;
        xx_erlang_external_term_init(&reader, dev, 0);
        valid = xx_erlang_external_term_check_is_valid(&reader.format, NULL);
        xx_erlang_external_term_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ERLANG_EXTERNAL_TERM;
    }
    if (header_size >= 20 && !xx_rt_memcmp(header, "\x4c\x00\x00\x00\x01\x14\x02\x00\x00\x00\x00\x00\xc0\x00\x00\x00\x00\x00\x00\x46", 20)) {
        xx_windows_shell_link reader;
        bool valid;
        xx_windows_shell_link_init(&reader, dev, 0);
        valid = xx_windows_shell_link_check_is_valid(&reader.format, NULL);
        xx_windows_shell_link_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WINDOWS_SHELL_LINK;
    }
    if (total_size >= 30 && header_size >= 3U && ((header[0]==0x4fU && header[1]==0x46U && header[2]==0x46U))) {
        xx_off_mesh reader;
        bool valid;
        xx_off_mesh_init(&reader, dev, 0);
        valid = xx_off_mesh_check_is_valid(&reader.format, NULL);
        xx_off_mesh_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OFF_MESH;
    }
    if (total_size >= 45 && header_size >= 5U && ((header[0]==0x41U && header[1]==0x43U && header[2]==0x33U && header[3]==0x44U && header[4]==0x62U))) {
        xx_ac3d_model reader;
        bool valid;
        xx_ac3d_model_init(&reader, dev, 0);
        valid = xx_ac3d_model_check_is_valid(&reader.format, NULL);
        xx_ac3d_model_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AC3D_MODEL;
    }
    if (total_size >= 54 && header_size >= 4U && ((header[0]==0x01U && header[1]==0x01U && header[2]==0x00U && header[3]==0x00U))) {
        xx_qubicle_qb reader;
        bool valid;
        xx_qubicle_qb_init(&reader, dev, 0);
        valid = xx_qubicle_qb_check_is_valid(&reader.format, NULL);
        xx_qubicle_qb_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_QUBICLE_QB;
    }
    if (total_size >= 40 && header_size >= 16U && ((header[0]==0x54U && header[1]==0x45U && header[2]==0x52U && header[3]==0x52U && header[4]==0x41U && header[5]==0x47U && header[6]==0x45U && header[7]==0x4eU && header[8]==0x54U && header[9]==0x45U && header[10]==0x52U && header[11]==0x52U && header[12]==0x41U && header[13]==0x49U && header[14]==0x4eU && header[15]==0x20U))) {
        xx_terragen_ter reader;
        bool valid;
        xx_terragen_ter_init(&reader, dev, 0);
        valid = xx_terragen_ter_check_is_valid(&reader.format, NULL);
        xx_terragen_ter_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TERRAGEN_TER;
    }
    if (total_size >= 50 && header_size >= 9U && ((header[0]==0x67U && header[1]==0x69U && header[2]==0x6dU && header[3]==0x70U && header[4]==0x20U && header[5]==0x78U && header[6]==0x63U && header[7]==0x66U && header[8]==0x20U))) {
        xx_gimp_xcf reader;
        bool valid;
        xx_gimp_xcf_init(&reader, dev, 0);
        valid = xx_gimp_xcf_check_is_valid(&reader.format, NULL);
        xx_gimp_xcf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GIMP_XCF;
    }
    if (total_size >= 42 && header_size >= 2U && ((header[0]==0x00U && header[1]==0x01U) || (header[0]==0x00U && header[1]==0x02U))) {
        xx_photoshop_abr reader;
        bool valid;
        xx_photoshop_abr_init(&reader, dev, 0);
        valid = xx_photoshop_abr_check_is_valid(&reader.format, NULL);
        xx_photoshop_abr_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PHOTOSHOP_ABR;
    }
    if (total_size >= 108 && header_size >= 4U && ((header[0]==0x53U && header[1]==0x80U && header[2]==0xf6U && header[3]==0x34U))) {
        xx_softimage_pic reader;
        bool valid;
        xx_softimage_pic_init(&reader, dev, 0);
        valid = xx_softimage_pic_check_is_valid(&reader.format, NULL);
        xx_softimage_pic_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SOFTIMAGE_PIC;
    }
    if (total_size >= 34 && header_size >= 4U && ((header[0]==0x51U && header[1]==0x50U && header[2]==0x49U && header[3]==0x43U))) {
        xx_qt_qpicture reader;
        bool valid;
        xx_qt_qpicture_init(&reader, dev, 0);
        valid = xx_qt_qpicture_check_is_valid(&reader.format, NULL);
        xx_qt_qpicture_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_QT_QPICTURE;
    }
    if (total_size >= 30 && header_size >= 2U && ((header[0]==0x80U && header[1]==0x01U))) {
        xx_font_type1_pfb reader;
        bool valid;
        xx_font_type1_pfb_init(&reader, dev, 0);
        valid = xx_font_type1_pfb_check_is_valid(&reader.format, NULL);
        xx_font_type1_pfb_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FONT_TYPE1_PFB;
    }
    if (total_size >= 55 && header_size >= 2U && ((header[0]==0xf7U && header[1]==0x83U))) {
        xx_tex_gf reader;
        bool valid;
        xx_tex_gf_init(&reader, dev, 0);
        valid = xx_tex_gf_check_is_valid(&reader.format, NULL);
        xx_tex_gf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TEX_GF;
    }
    if (total_size >= 12 && header_size >= 4U && ((header[0]==0x42U && header[1]==0x50U && header[2]==0x47U && header[3]==0xfbU))) {
        xx_bpg_image reader;
        bool valid;
        xx_bpg_image_init(&reader, dev, 0);
        valid = xx_bpg_image_check_is_valid(&reader.format, NULL);
        xx_bpg_image_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BPG_IMAGE;
    }
    if (total_size >= 70 && header_size >= 8U && ((header[0]==0x8aU && header[1]==0x4dU && header[2]==0x4eU && header[3]==0x47U && header[4]==0x0dU && header[5]==0x0aU && header[6]==0x1aU && header[7]==0x0aU))) {
        xx_mng_animation reader;
        bool valid;
        xx_mng_animation_init(&reader, dev, 0);
        valid = xx_mng_animation_check_is_valid(&reader.format, NULL);
        xx_mng_animation_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MNG_ANIMATION;
    }
    if (header_size>=128 && !xx_rt_memcmp(header+1,"ATARI7800",9) && !xx_rt_memcmp(header+100,"ACTUAL CART DATA STARTS HERE",28)) {
        xx_atari_7800_a78 reader;
        bool valid;
        xx_atari_7800_a78_init(&reader, dev, 0);
        valid = xx_atari_7800_a78_check_is_valid(&reader.format, NULL);
        xx_atari_7800_a78_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ATARI_7800_A78;
    }
    if (header_size>=26 && !xx_rt_memcmp(header,"C64File",8)) {
        xx_commodore_pc64 reader;
        bool valid;
        xx_commodore_pc64_init(&reader, dev, 0);
        valid = xx_commodore_pc64_check_is_valid(&reader.format, NULL);
        xx_commodore_pc64_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_COMMODORE_PC64;
    }
    if (header_size>=8 && !xx_rt_memcmp(header,"FUJI",4)) {
        xx_atari_cas reader;
        bool valid;
        xx_atari_cas_init(&reader, dev, 0);
        valid = xx_atari_cas_check_is_valid(&reader.format, NULL);
        xx_atari_cas_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ATARI_CAS;
    }
    if (header_size>=32 && !xx_rt_memcmp(header,"\x1f\xa6\xde\xba\xcc\x13\x7d\x74",8)) {
        xx_msx_cas reader;
        bool valid;
        xx_msx_cas_init(&reader, dev, 0);
        valid = xx_msx_cas_check_is_valid(&reader.format, NULL);
        xx_msx_cas_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MSX_CAS;
    }
    if (header_size>=14 && header[0]==0x16 && header[1]==0x16 && header[2]==0x16) {
        xx_oric_tap reader;
        bool valid;
        xx_oric_tap_init(&reader, dev, 0);
        valid = xx_oric_tap_check_is_valid(&reader.format, NULL);
        xx_oric_tap_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ORIC_TAP;
    }
    if (header_size>=21 && header[0]==0x55) {
        xx_dragon_cas reader;
        bool valid;
        xx_dragon_cas_init(&reader, dev, 0);
        valid = xx_dragon_cas_check_is_valid(&reader.format, NULL);
        xx_dragon_cas_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DRAGON_CAS;
    }
    if (header_size>=14 && !xx_rt_memcmp(header,"THX",3) && header[3]<=1) {
        xx_amiga_ahx reader;
        bool valid;
        xx_amiga_ahx_init(&reader, dev, 0);
        valid = xx_amiga_ahx_check_is_valid(&reader.format, NULL);
        xx_amiga_ahx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AMIGA_AHX;
    }
    if (header_size>=256 && !xx_rt_memcmp(header,"MV - SNA",8)) {
        xx_amstrad_cpc_sna reader;
        bool valid;
        xx_amstrad_cpc_sna_init(&reader, dev, 0);
        valid = xx_amstrad_cpc_sna_check_is_valid(&reader.format, NULL);
        xx_amstrad_cpc_sna_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AMSTRAD_CPC_SNA;
    }
    if (header_size>=24 && !xx_rt_memcmp(header,"VZF",3) && (header[3]=='0' || header[3]=='1')) {
        xx_vtech_vz reader;
        bool valid;
        xx_vtech_vz_init(&reader, dev, 0);
        valid = xx_vtech_vz_check_is_valid(&reader.format, NULL);
        xx_vtech_vz_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VTECH_VZ;
    }
    if (header_size>=17 && header[13]==0 && header[14]!=0 && (header[8]=='B' || header[8]=='C' || header[8]=='D')) {
        xx_zx_hobeta reader;
        bool valid;
        xx_zx_hobeta_init(&reader, dev, 0);
        valid = xx_zx_hobeta_check_is_valid(&reader.format, NULL);
        xx_zx_hobeta_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ZX_HOBETA;
    }
}
