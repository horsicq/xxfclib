/* Reviewed bounded probes for the twelfth group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        !xx_format_read_probe_exact(dev, header, header_size)) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 4 && !xx_rt_memcmp(header,"\xe0\x01\x00\xea",4)) {
        xx_amazon_ion_binary reader;
        bool valid;
        xx_amazon_ion_binary_init(&reader, dev, 0);
        valid = xx_amazon_ion_binary_check_is_valid(&reader.format, NULL);
        xx_amazon_ion_binary_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AMAZON_ION_BINARY;
    }
    if (header_size >= 8 && !xx_rt_memcmp(header,"\x37\x8f\x24\x00\x01\x00\x00\x00",8)) {
        xx_rocksdb_blob reader;
        bool valid;
        xx_rocksdb_blob_init(&reader, dev, 0);
        valid = xx_rocksdb_blob_check_is_valid(&reader.format, NULL);
        xx_rocksdb_blob_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ROCKSDB_BLOB;
    }
    if (header_size >= 8 && !xx_rt_memcmp(header,"\x41\x4d\x51\x50\x00\x00\x09\x01",8)) {
        xx_amqp_frames reader;
        bool valid;
        xx_amqp_frames_init(&reader, dev, 0);
        valid = xx_amqp_frames_check_is_valid(&reader.format, NULL);
        xx_amqp_frames_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AMQP_FRAMES;
    }
    if (header_size >= 15 && !xx_rt_memcmp(header,"\x6f\x70\x65\x6e\x73\x73\x68\x2d\x6b\x65\x79\x2d\x76\x31\x00",15)) {
        xx_openssh_private_key reader;
        bool valid;
        xx_openssh_private_key_init(&reader, dev, 0);
        valid = xx_openssh_private_key_check_is_valid(&reader.format, NULL);
        xx_openssh_private_key_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OPENSSH_PRIVATE_KEY;
    }
    if (header_size >= 2 && !xx_rt_memcmp(header,"\x05\x02",2)) {
        xx_kerberos_keytab reader;
        bool valid;
        xx_kerberos_keytab_init(&reader, dev, 0);
        valid = xx_kerberos_keytab_check_is_valid(&reader.format, NULL);
        xx_kerberos_keytab_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_KERBEROS_KEYTAB;
    }
    if (total_size >= 12 && header_size >= 12U && !xx_rt_memcmp(header,"\x47\x49\x4d\x50\x20\x50\x61\x6c\x65\x74\x74\x65",12U)) {
        xx_gimp_gpl reader;
        bool valid;
        xx_gimp_gpl_init(&reader, dev, 0);
        valid = xx_gimp_gpl_check_is_valid(&reader.format, NULL);
        xx_gimp_gpl_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GIMP_GPL;
    }
    if (total_size >= 13 && header_size >= 13U && !xx_rt_memcmp(header,"\x47\x49\x4d\x50\x20\x47\x72\x61\x64\x69\x65\x6e\x74",13U)) {
        xx_gimp_ggr reader;
        bool valid;
        xx_gimp_ggr_init(&reader, dev, 0);
        valid = xx_gimp_ggr_check_is_valid(&reader.format, NULL);
        xx_gimp_ggr_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GIMP_GGR;
    }
    if (total_size >= 36 && header_size >= 27U && !xx_rt_memcmp(header,"\x50\x61\x69\x6e\x74\x20\x53\x68\x6f\x70\x20\x50\x72\x6f\x20\x49\x6d\x61\x67\x65\x20\x46\x69\x6c\x65\x0a\x1a",27U)) {
        xx_paintshop_psp reader;
        bool valid;
        xx_paintshop_psp_init(&reader, dev, 0);
        valid = xx_paintshop_psp_check_is_valid(&reader.format, NULL);
        xx_paintshop_psp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PAINTSHOP_PSP;
    }
    if (total_size >= 12 && header_size >= 4U && !xx_rt_memcmp(header,"\x38\x42\x50\x54",4U)) {
        xx_photoshop_pat reader;
        bool valid;
        xx_photoshop_pat_init(&reader, dev, 0);
        valid = xx_photoshop_pat_check_is_valid(&reader.format, NULL);
        xx_photoshop_pat_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PHOTOSHOP_PAT;
    }
    if (total_size >= 17 && header_size >= 4U && !xx_rt_memcmp(header,"\x50\x4d\x58\x20",4U)) {
        xx_mmd_pmx reader;
        bool valid;
        xx_mmd_pmx_init(&reader, dev, 0);
        valid = xx_mmd_pmx_check_is_valid(&reader.format, NULL);
        xx_mmd_pmx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MMD_PMX;
    }
    if (total_size >= 20 && header_size >= 20U && !xx_rt_memcmp(header,"\x4d\x65\x74\x61\x73\x65\x71\x75\x6f\x69\x61\x20\x44\x6f\x63\x75\x6d\x65\x6e\x74",20U)) {
        xx_metasequoia_mqo reader;
        bool valid;
        xx_metasequoia_mqo_init(&reader, dev, 0);
        valid = xx_metasequoia_mqo_check_is_valid(&reader.format, NULL);
        xx_metasequoia_mqo_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_METASEQUOIA_MQO;
    }
    if (total_size >= 6 && header_size >= 4U && !xx_rt_memcmp(header,"\x00\x06\x00\x02",4U)) {
        xx_calma_gdsii reader;
        bool valid;
        xx_calma_gdsii_init(&reader, dev, 0);
        valid = xx_calma_gdsii_check_is_valid(&reader.format, NULL);
        xx_calma_gdsii_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CALMA_GDSII;
    }
    if (total_size >= 20 && header_size >= 19U && !xx_rt_memcmp(header,"\x2a\x33\x44\x53\x4d\x41\x58\x5f\x41\x53\x43\x49\x49\x45\x58\x50\x4f\x52\x54",19U)) {
        xx_autodesk_ase reader;
        bool valid;
        xx_autodesk_ase_init(&reader, dev, 0);
        valid = xx_autodesk_ase_check_is_valid(&reader.format, NULL);
        xx_autodesk_ase_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUTODESK_ASE;
    }
    if (total_size >= 12 && header_size >= 3U && !xx_rt_memcmp(header,"\xff\xff\xfe",3U)) {
        xx_freesurfer_surface reader;
        bool valid;
        xx_freesurfer_surface_init(&reader, dev, 0);
        valid = xx_freesurfer_surface_check_is_valid(&reader.format, NULL);
        xx_freesurfer_surface_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FREESURFER_SURFACE;
    }
    if (total_size >= 12 && header_size >= 11U && !xx_rt_memcmp(header,"\x24\x4d\x65\x73\x68\x46\x6f\x72\x6d\x61\x74",11U)) {
        xx_gmsh_msh reader;
        bool valid;
        xx_gmsh_msh_init(&reader, dev, 0);
        valid = xx_gmsh_msh_check_is_valid(&reader.format, NULL);
        xx_gmsh_msh_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GMSH_MSH;
    }
    if (total_size >= 8 && header_size >= 6U && !xx_rt_memcmp(header,"\x6d\x65\x73\x68\x33\x64",6U)) {
        xx_netgen_vol reader;
        bool valid;
        xx_netgen_vol_init(&reader, dev, 0);
        valid = xx_netgen_vol_check_is_valid(&reader.format, NULL);
        xx_netgen_vol_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NETGEN_VOL;
    }
    if (total_size >= 16 && header_size >= 16U && !xx_rt_memcmp(header,"\x53\x74\x61\x72\x74\x46\x6f\x6e\x74\x4d\x65\x74\x72\x69\x63\x73",16U)) {
        xx_font_afm reader;
        bool valid;
        xx_font_afm_init(&reader, dev, 0);
        valid = xx_font_afm_check_is_valid(&reader.format, NULL);
        xx_font_afm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FONT_AFM;
    }
    if (header_size>=6 && !xx_rt_memcmp(header,"\x53\x44\x41\x54\xff\xfe",6)) {
        xx_nintendo_sdat reader;
        bool valid;
        xx_nintendo_sdat_init(&reader, dev, 0);
        valid = xx_nintendo_sdat_check_is_valid(&reader.format, NULL);
        xx_nintendo_sdat_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_SDAT;
    }
    if (header_size>=4 && !xx_rt_memcmp(header,"\x70\x42\x41\x56",4)) {
        xx_sony_vab reader;
        bool valid;
        xx_sony_vab_init(&reader, dev, 0);
        valid = xx_sony_vab_check_is_valid(&reader.format, NULL);
        xx_sony_vab_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SONY_VAB;
    }
    if (header_size>=12 && (!xx_rt_memcmp(header,"YM5!LeOnArD!",12) || !xx_rt_memcmp(header,"YM6!LeOnArD!",12))) {
        xx_yamaha_ym reader;
        bool valid;
        xx_yamaha_ym_init(&reader, dev, 0);
        valid = xx_yamaha_ym_check_is_valid(&reader.format, NULL);
        xx_yamaha_ym_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_YAMAHA_YM;
    }
    if (header_size>=8 && !xx_rt_memcmp(header,"\x5a\x58\x41\x59\x45\x4d\x55\x4c",8)) {
        xx_zx_ayemul reader;
        bool valid;
        xx_zx_ayemul_init(&reader, dev, 0);
        valid = xx_zx_ayemul_check_is_valid(&reader.format, NULL);
        xx_zx_ayemul_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ZX_AYEMUL;
    }
    if (header_size>=2 && !xx_rt_memcmp(header,"\x64\x6b",2)) {
        xx_dragon_vdk reader;
        bool valid;
        xx_dragon_vdk_init(&reader, dev, 0);
        valid = xx_dragon_vdk_check_is_valid(&reader.format, NULL);
        xx_dragon_vdk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DRAGON_VDK;
    }
    if (header_size>=8 &&
        (!xx_rt_memcmp(header,"\x41\x32\x52\x32\xff\x0a\x0d\x0a",8) ||
         !xx_rt_memcmp(header,"\x41\x32\x52\x33\xff\x0a\x0d\x0a",8))) {
        xx_apple_a2r reader;
        bool valid;
        xx_apple_a2r_init(&reader, dev, 0);
        valid = xx_apple_a2r_check_is_valid(&reader.format, NULL);
        xx_apple_a2r_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_APPLE_A2R;
    }
    if (header_size>=2 && !xx_rt_memcmp(header,"\x96\x02",2)) {
        xx_atari_atr reader;
        bool valid;
        xx_atari_atr_init(&reader, dev, 0);
        valid = xx_atari_atr_check_is_valid(&reader.format, NULL);
        xx_atari_atr_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ATARI_ATR;
    }
    if (header_size>=4 && !xx_rt_memcmp(header,"\x52\x53\x59\x00",4)) {
        xx_atari_pasti_stx reader;
        bool valid;
        xx_atari_pasti_stx_init(&reader, dev, 0);
        valid = xx_atari_pasti_stx_check_is_valid(&reader.format, NULL);
        xx_atari_pasti_stx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ATARI_PASTI_STX;
    }
    if (header_size>=4 && !xx_rt_memcmp(header,"\x43\x41\x50\x53",4)) {
        xx_amiga_ipf reader;
        bool valid;
        xx_amiga_ipf_init(&reader, dev, 0);
        valid = xx_amiga_ipf_check_is_valid(&reader.format, NULL);
        xx_amiga_ipf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AMIGA_IPF;
    }
    if (header_size>=4 && !xx_rt_memcmp(header,"\x44\x73\x6b\x54",4)) {
        xx_tracker_dtt reader;
        bool valid;
        xx_tracker_dtt_init(&reader, dev, 0);
        valid = xx_tracker_dtt_check_is_valid(&reader.format, NULL);
        xx_tracker_dtt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_DTT;
    }
}
