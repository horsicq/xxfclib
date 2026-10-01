/* Reviewed bounded probes for the sixteenth group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 24 && !xx_rt_memcmp(header,"RTPS",4)) {
        xx_dds_rtps reader;
        bool valid;
        xx_dds_rtps_init(&reader, dev, 0);
        valid = xx_dds_rtps_check_is_valid(&reader.format, NULL);
        xx_dds_rtps_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DDS_RTPS;
    }
    if (header_size >= 32 && !xx_rt_memcmp(header,"NTLMSSP",7) && header[7] == 0) {
        xx_ntlm_message reader;
        bool valid;
        xx_ntlm_message_init(&reader, dev, 0);
        valid = xx_ntlm_message_check_is_valid(&reader.format, NULL);
        xx_ntlm_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NTLM_MESSAGE;
    }
    if (total_size >= 32 && header_size >= 14U && !xx_rt_memcmp(header,"\x69\x64\x3d\x49\x6d\x61\x67\x65\x4d\x61\x67\x69\x63\x6b",14U)) {
        xx_imagemagick_miff reader;
        bool valid;
        xx_imagemagick_miff_init(&reader, dev, 0);
        valid = xx_imagemagick_miff_check_is_valid(&reader.format, NULL);
        xx_imagemagick_miff_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IMAGEMAGICK_MIFF;
    }
    if (total_size >= 53 && header_size >= 16 && ((!xx_rt_memcmp(header,"iiii",4))||(!xx_rt_memcmp(header,"mmmm",4))) && !xx_rt_memcmp(header+8,"100fdata",8)) {
        xx_scanalytics_iplab reader;
        bool valid;
        xx_scanalytics_iplab_init(&reader, dev, 0);
        valid = xx_scanalytics_iplab_check_is_valid(&reader.format, NULL);
        xx_scanalytics_iplab_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SCANALYTICS_IPLAB;
    }
    if (total_size >= 20 && header_size >= 4U && !xx_rt_memcmp(header,"\xff\x57\x50\x43",4U)) {
        xx_wordperfect_wpg reader;
        bool valid;
        xx_wordperfect_wpg_init(&reader, dev, 0);
        valid = xx_wordperfect_wpg_check_is_valid(&reader.format, NULL);
        xx_wordperfect_wpg_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WORDPERFECT_WPG;
    }
    if (total_size >= 64 && header_size >= 8U && !xx_rt_memcmp(header,"\x4c\x42\x4c\x53\x49\x5a\x45\x3d",8U)) {
        xx_nasa_vicar reader;
        bool valid;
        xx_nasa_vicar_init(&reader, dev, 0);
        valid = xx_nasa_vicar_check_is_valid(&reader.format, NULL);
        xx_nasa_vicar_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NASA_VICAR;
    }
    if (total_size >= 1025 && header_size >= 8 && header[0]==171 && header[1]==1 && header[2]==1 && header[3]==3 && (header[4]==2||header[4]==4||header[4]==8) && header[5]==0 && header[6]==0 && header[7]==0) {
        xx_khoros_viff reader;
        bool valid;
        xx_khoros_viff_init(&reader, dev, 0);
        valid = xx_khoros_viff_check_is_valid(&reader.format, NULL);
        xx_khoros_viff_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_KHOROS_VIFF;
    }
    if (total_size >= 32 && header_size >= 20U && !xx_rt_memcmp(header,"\x70\x75\x73\x68\x20\x67\x72\x61\x70\x68\x69\x63\x2d\x63\x6f\x6e\x74\x65\x78\x74",20U)) {
        xx_imagemagick_mvg reader;
        bool valid;
        xx_imagemagick_mvg_init(&reader, dev, 0);
        valid = xx_imagemagick_mvg_check_is_valid(&reader.format, NULL);
        xx_imagemagick_mvg_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IMAGEMAGICK_MVG;
    }
    if (total_size >= 32 && header_size >= 9U && !xx_rt_memcmp(header,"\x2f\x2a\x20\x55\x49\x4c\x20\x2a\x2f",9U)) {
        xx_motif_uil reader;
        bool valid;
        xx_motif_uil_init(&reader, dev, 0);
        valid = xx_motif_uil_check_is_valid(&reader.format, NULL);
        xx_motif_uil_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MOTIF_UIL;
    }
    if (total_size >= 16 && header_size >= 9U && !xx_rt_memcmp(header,"\x23\x75\x73\x64\x61\x20\x31\x2e\x30",9U)) {
        xx_openusd_usda reader;
        bool valid;
        xx_openusd_usda_init(&reader, dev, 0);
        valid = xx_openusd_usda_check_is_valid(&reader.format, NULL);
        xx_openusd_usda_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OPENUSD_USDA;
    }
    if ((header_size>=7 && header[0]==115 && header[1]==111 && header[2]==112 && header[3]==101 && header[4]==112 && header[5]==111 && header[6]==115)) {
        xx_adlib_sop reader;
        bool valid;
        xx_adlib_sop_init(&reader, dev, 0);
        valid = xx_adlib_sop_check_is_valid(&reader.format, NULL);
        xx_adlib_sop_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_SOP;
    }
    if ((header_size>=16 && header[0]==60 && header[1]==67 && header[2]==85 && header[3]==68 && header[4]==45 && header[5]==70 && header[6]==77 && header[7]==45 && header[8]==70 && header[9]==105 && header[10]==108 && header[11]==101 && header[12]==62 && header[13]==26 && header[14]==222 && header[15]==224)) {
        xx_cudfm_cff reader;
        bool valid;
        xx_cudfm_cff_init(&reader, dev, 0);
        valid = xx_cudfm_cff_check_is_valid(&reader.format, NULL);
        xx_cudfm_cff_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CUDFM_CFF;
    }
    if ((header_size>=32 && header[0]==2 && header[1]==0)) {
        xx_adlib_jbm reader;
        bool valid;
        xx_adlib_jbm_init(&reader, dev, 0);
        valid = xx_adlib_jbm_check_is_valid(&reader.format, NULL);
        xx_adlib_jbm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_JBM;
    }
    if ((header_size>=16 && header[0]==67 && header[1]==101 && header[2]==114 && header[3]==101 && header[4]==115 && header[5]==32 && header[6]==19 && header[7]==32 && header[8]==77 && header[9]==83 && header[10]==67 && header[11]==112 && header[12]==108 && header[13]==97 && header[14]==121 && header[15]==32)) {
        xx_ceres_msc reader;
        bool valid;
        xx_ceres_msc_init(&reader, dev, 0);
        valid = xx_ceres_msc_check_is_valid(&reader.format, NULL);
        xx_ceres_msc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CERES_MSC;
    }
    if ((header_size>=6 && header[0]==111 && header[1]==102 && header[2]==84 && header[3]==65 && header[4]==90 && header[5]==33)) {
        xx_adlib_xsm reader;
        bool valid;
        xx_adlib_xsm_init(&reader, dev, 0);
        valid = xx_adlib_xsm_check_is_valid(&reader.format, NULL);
        xx_adlib_xsm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_XSM;
    }
    if ((header_size>=10 && header[0]==77 && header[1]==84 && header[2]==82 && header[3]==65 && header[4]==67 && header[5]==75 && header[6]==32 && header[7]==78 && header[8]==67 && header[9]==32)) {
        xx_adlib_mtr reader;
        bool valid;
        xx_adlib_mtr_init(&reader, dev, 0);
        valid = xx_adlib_mtr_check_is_valid(&reader.format, NULL);
        xx_adlib_mtr_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_MTR;
    }
    if ((header_size>=8 && header[0]==82 && header[1]==65 && header[2]==87 && header[3]==65 && header[4]==68 && header[5]==65 && header[6]==84 && header[7]==65)) {
        xx_rdos_raw reader;
        bool valid;
        xx_rdos_raw_init(&reader, dev, 0);
        valid = xx_rdos_raw_check_is_valid(&reader.format, NULL);
        xx_rdos_raw_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RDOS_RAW;
    }
    if ((header_size>=4 && header[0]==77 && header[1]==65 && header[2]==68 && header[3]==43)) {
        xx_mad_tracker reader;
        bool valid;
        xx_mad_tracker_init(&reader, dev, 0);
        valid = xx_mad_tracker_check_is_valid(&reader.format, NULL);
        xx_mad_tracker_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MAD_TRACKER;
    }
}
