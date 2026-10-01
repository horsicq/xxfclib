/* Reviewed bounded probes for the fifteenth group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 32 && (!xx_rt_memcmp(header,"MESSAGE ",8) || !xx_rt_memcmp(header,"OPTIONS ",8))) {
        xx_sip_message reader;
        bool valid;
        xx_sip_message_init(&reader, dev, 0);
        valid = xx_sip_message_check_is_valid(&reader.format, NULL);
        xx_sip_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SIP_MESSAGE;
    }
    if (header_size >= 32 && (!xx_rt_memcmp(header,"OPTIONS ",8) || !xx_rt_memcmp(header,"DESCRIBE ",9))) {
        xx_rtsp_message reader;
        bool valid;
        xx_rtsp_message_init(&reader, dev, 0);
        valid = xx_rtsp_message_check_is_valid(&reader.format, NULL);
        xx_rtsp_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RTSP_MESSAGE;
    }
    if (header_size >= 32 && header[21] == 10 && !xx_rt_memcmp(header,"age-encryption.org/v1",21)) {
        xx_age_encrypted reader;
        bool valid;
        xx_age_encrypted_init(&reader, dev, 0);
        valid = xx_age_encrypted_check_is_valid(&reader.format, NULL);
        xx_age_encrypted_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AGE_ENCRYPTED;
    }
    if (total_size >= 12 && header_size >= 2U && !xx_rt_memcmp(header,"\x1b\x50",2U)) {
        xx_dec_sixel reader;
        bool valid;
        xx_dec_sixel_init(&reader, dev, 0);
        valid = xx_dec_sixel_check_is_valid(&reader.format, NULL);
        xx_dec_sixel_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DEC_SIXEL;
    }
    if (total_size >= 24 && header_size >= 2U && !xx_rt_memcmp(header,"\x00\x10",2U)) {
        xx_ogre_skeleton reader;
        bool valid;
        xx_ogre_skeleton_init(&reader, dev, 0);
        valid = xx_ogre_skeleton_check_is_valid(&reader.format, NULL);
        xx_ogre_skeleton_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OGRE_SKELETON;
    }
    if (total_size >= 12 && header_size >= 4U && !xx_rt_memcmp(header,"\x43\x53\x46\x00",4U)) {
        xx_cal3d_skeleton reader;
        bool valid;
        xx_cal3d_skeleton_init(&reader, dev, 0);
        valid = xx_cal3d_skeleton_check_is_valid(&reader.format, NULL);
        xx_cal3d_skeleton_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CAL3D_SKELETON;
    }
    if (total_size >= 8 && header_size >= 4U && !xx_rt_memcmp(header,"\x4c\x57\x53\x43",4U)) {
        xx_lightwave_scene reader;
        bool valid;
        xx_lightwave_scene_init(&reader, dev, 0);
        valid = xx_lightwave_scene_check_is_valid(&reader.format, NULL);
        xx_lightwave_scene_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LIGHTWAVE_SCENE;
    }
    if (total_size >= 80 && header_size >= 4U && !xx_rt_memcmp(header,"\x4d\x54\x5a\x20",4U)) {
        xx_crystallography_mtz reader;
        bool valid;
        xx_crystallography_mtz_init(&reader, dev, 0);
        valid = xx_crystallography_mtz_check_is_valid(&reader.format, NULL);
        xx_crystallography_mtz_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CRYSTALLOGRAPHY_MTZ;
    }
    if (total_size >= 64 && header_size >= 12U && !xx_rt_memcmp(header,"\x23\x20\x41\x6d\x69\x72\x61\x4d\x65\x73\x68\x20",12U)) {
        xx_amira_mesh reader;
        bool valid;
        xx_amira_mesh_init(&reader, dev, 0);
        valid = xx_amira_mesh_check_is_valid(&reader.format, NULL);
        xx_amira_mesh_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AMIRA_MESH;
    }
    if (total_size >= 16 && header_size >= 1U && !xx_rt_memcmp(header,"\x02",1U)) {
        xx_jedec_fuse reader;
        bool valid;
        xx_jedec_fuse_init(&reader, dev, 0);
        valid = xx_jedec_fuse_check_is_valid(&reader.format, NULL);
        xx_jedec_fuse_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_JEDEC_FUSE;
    }
    if (header_size >= 4 && !xx_rt_memcmp(header, "\x43\x42\x4d\x46", 4)) {
        xx_adlib_bam reader;
        bool valid;
        xx_adlib_bam_init(&reader, dev, 0);
        valid = xx_adlib_bam_check_is_valid(&reader.format, NULL);
        xx_adlib_bam_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_BAM;
    }
    if (header_size >= 6 && !xx_rt_memcmp(header, "\x42\x4d\x46\x31\x2e\x31", 6)) {
        xx_adlib_bmf reader;
        bool valid;
        xx_adlib_bmf_init(&reader, dev, 0);
        valid = xx_adlib_bmf_check_is_valid(&reader.format, NULL);
        xx_adlib_bmf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_BMF;
    }
    if (header_size >= 4 && !xx_rt_memcmp(header, "\x43\x54\x4d\x46", 4)) {
        xx_creative_cmf reader;
        bool valid;
        xx_creative_cmf_init(&reader, dev, 0);
        valid = xx_creative_cmf_check_is_valid(&reader.format, NULL);
        xx_creative_cmf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CREATIVE_CMF;
    }
    if (header_size >= 4 && !xx_rt_memcmp(header, "\x44\x46\x4d\x1a", 4)) {
        xx_adlib_dfm reader;
        bool valid;
        xx_adlib_dfm_init(&reader, dev, 0);
        valid = xx_adlib_dfm_check_is_valid(&reader.format, NULL);
        xx_adlib_dfm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_DFM;
    }
    if (header_size >= 6 && !xx_rt_memcmp(header, "\x4d\x4b\x4a\x61\x6d\x7a", 6)) {
        xx_adlib_mkj reader;
        bool valid;
        xx_adlib_mkj_init(&reader, dev, 0);
        valid = xx_adlib_mkj_check_is_valid(&reader.format, NULL);
        xx_adlib_mkj_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_MKJ;
    }
    if (header_size >= 5 && !xx_rt_memcmp(header, "\x53\x41\x64\x54\x09", 5)) {
        xx_adlib_sa2 reader;
        bool valid;
        xx_adlib_sa2_init(&reader, dev, 0);
        valid = xx_adlib_sa2_check_is_valid(&reader.format, NULL);
        xx_adlib_sa2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_SA2;
    }
    if (header_size >= 4 && !xx_rt_memcmp(header, "\x46\x4d\x43\x21", 4)) {
        xx_faust_fmc reader;
        bool valid;
        xx_faust_fmc_init(&reader, dev, 0);
        valid = xx_faust_fmc_check_is_valid(&reader.format, NULL);
        xx_faust_fmc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FAUST_FMC;
    }
    if (header_size >= 2 && !xx_rt_memcmp(header, "\xaa\x55", 2)) {
        xx_softstar_rix reader;
        bool valid;
        xx_softstar_rix_init(&reader, dev, 0);
        valid = xx_softstar_rix_check_is_valid(&reader.format, NULL);
        xx_softstar_rix_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SOFTSTAR_RIX;
    }
    if (((header_size >= 4 && header[0] == 65 && header[1] == 66 && header[2] == 73 && header[3] == 70))) {
        xx_sequencing_abif reader;
        bool valid;
        xx_sequencing_abif_init(&reader, dev, 0);
        valid = xx_sequencing_abif_check_is_valid(&reader.format, NULL);
        xx_sequencing_abif_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SEQUENCING_ABIF;
    }
    if (((header_size >= 4 && header[0] == 46 && header[1] == 115 && header[2] == 99 && header[3] == 102))) {
        xx_sequencing_scf reader;
        bool valid;
        xx_sequencing_scf_init(&reader, dev, 0);
        valid = xx_sequencing_scf_check_is_valid(&reader.format, NULL);
        xx_sequencing_scf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SEQUENCING_SCF;
    }
    if (((header_size >= 4 && header[0] == 46 && header[1] == 115 && header[2] == 102 && header[3] == 102))) {
        xx_genomics_sff reader;
        bool valid;
        xx_genomics_sff_init(&reader, dev, 0);
        valid = xx_genomics_sff_check_is_valid(&reader.format, NULL);
        xx_genomics_sff_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENOMICS_SFF;
    }
    if (((header_size >= 9 && header[0] == 86 && header[1] == 101 && header[2] == 114 && header[3] == 115 && header[4] == 105 && header[5] == 111 && header[6] == 110 && header[7] == 32 && header[8] == 49))) {
        xx_lut_spi1d reader;
        bool valid;
        xx_lut_spi1d_init(&reader, dev, 0);
        valid = xx_lut_spi1d_check_is_valid(&reader.format, NULL);
        xx_lut_spi1d_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LUT_SPI1D;
    }
    if (((header_size >= 10 && header[0] == 83 && header[1] == 80 && header[2] == 73 && header[3] == 76 && header[4] == 85 && header[5] == 84 && header[6] == 32 && header[7] == 49 && header[8] == 46 && header[9] == 48))) {
        xx_lut_spi3d reader;
        bool valid;
        xx_lut_spi3d_init(&reader, dev, 0);
        valid = xx_lut_spi3d_check_is_valid(&reader.format, NULL);
        xx_lut_spi3d_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LUT_SPI3D;
    }
    if (((header_size >= 10 && header[0] == 67 && header[1] == 83 && header[2] == 80 && header[3] == 76 && header[4] == 85 && header[5] == 84 && header[6] == 86 && header[7] == 49 && header[8] == 48 && header[9] == 48))) {
        xx_lut_cinespace_csp reader;
        bool valid;
        xx_lut_cinespace_csp_init(&reader, dev, 0);
        valid = xx_lut_cinespace_csp_check_is_valid(&reader.format, NULL);
        xx_lut_cinespace_csp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LUT_CINESPACE_CSP;
    }
}
