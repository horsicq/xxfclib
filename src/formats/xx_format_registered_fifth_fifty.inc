/* Reviewed bounded probes for the fifth group of fifty readers. */
{
    uint8_t header[512] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_arc reader;
        bool valid;
        xx_sfx_arc_init(&reader, dev, 0);
        valid = xx_sfx_arc_check_is_valid(&reader.format, NULL);
        xx_sfx_arc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ARC;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_arj reader;
        bool valid;
        xx_sfx_arj_init(&reader, dev, 0);
        valid = xx_sfx_arj_check_is_valid(&reader.format, NULL);
        xx_sfx_arj_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ARJ;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_bsn reader;
        bool valid;
        xx_sfx_bsn_init(&reader, dev, 0);
        valid = xx_sfx_bsn_check_is_valid(&reader.format, NULL);
        xx_sfx_bsn_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_BSN;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_arq reader;
        bool valid;
        xx_sfx_arq_init(&reader, dev, 0);
        valid = xx_sfx_arq_check_is_valid(&reader.format, NULL);
        xx_sfx_arq_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ARQ;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_gxl reader;
        bool valid;
        xx_sfx_gxl_init(&reader, dev, 0);
        valid = xx_sfx_gxl_check_is_valid(&reader.format, NULL);
        xx_sfx_gxl_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_GXL;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_asymetrix reader;
        bool valid;
        xx_sfx_asymetrix_init(&reader, dev, 0);
        valid = xx_sfx_asymetrix_check_is_valid(&reader.format, NULL);
        xx_sfx_asymetrix_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ASYMETRIX;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_rta reader;
        bool valid;
        xx_sfx_rta_init(&reader, dev, 0);
        valid = xx_sfx_rta_check_is_valid(&reader.format, NULL);
        xx_sfx_rta_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_RTA;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_rtpatch reader;
        bool valid;
        xx_sfx_rtpatch_init(&reader, dev, 0);
        valid = xx_sfx_rtpatch_check_is_valid(&reader.format, NULL);
        xx_sfx_rtpatch_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_RTPATCH;
    }
    if (header_size >= 4 && (xx_rt_memcmp(header+0,"\x45\x53\x50\x3e",4) == 0 || xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0)) {
        xx_esp_archive reader;
        bool valid;
        xx_esp_archive_init(&reader, dev, 0);
        valid = xx_esp_archive_check_is_valid(&reader.format, NULL);
        xx_esp_archive_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ESP_ARCHIVE;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_kwaj reader;
        bool valid;
        xx_sfx_kwaj_init(&reader, dev, 0);
        valid = xx_sfx_kwaj_check_is_valid(&reader.format, NULL);
        xx_sfx_kwaj_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_KWAJ;
    }
    if (header_size >= 2 && (xx_rt_memcmp(header+0,"\x60\x1a",2) == 0)) {
        xx_gemdos_lha reader;
        bool valid;
        xx_gemdos_lha_init(&reader, dev, 0);
        valid = xx_gemdos_lha_check_is_valid(&reader.format, NULL);
        xx_gemdos_lha_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GEMDOS_LHA;
    }
    if (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0)) {
        xx_winimage_zip reader;
        bool valid;
        xx_winimage_zip_init(&reader, dev, 0);
        valid = xx_winimage_zip_check_is_valid(&reader.format, NULL);
        xx_winimage_zip_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WINIMAGE_ZIP;
    }
    if (header_size >= 8 && (xx_rt_memcmp(header+0,"\x52\x45\x43\x53\x49\x5a\x45\x3d",8) == 0)) {
        xx_hp3000_wrq reader;
        bool valid;
        xx_hp3000_wrq_init(&reader, dev, 0);
        valid = xx_hp3000_wrq_check_is_valid(&reader.format, NULL);
        xx_hp3000_wrq_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_HP3000_WRQ;
    }
    if (header_size >= 4 && (xx_rt_memcmp(header+2,"\xda\x27",2) == 0)) {
        xx_icu_data_package reader;
        bool valid;
        xx_icu_data_package_init(&reader, dev, 0);
        valid = xx_icu_data_package_check_is_valid(&reader.format, NULL);
        xx_icu_data_package_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ICU_DATA_PACKAGE;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_sqz reader;
        bool valid;
        xx_sfx_sqz_init(&reader, dev, 0);
        valid = xx_sfx_sqz_check_is_valid(&reader.format, NULL);
        xx_sfx_sqz_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_SQZ;
    }
    if (total_size >= 64 && header_size >= 4U && header[0] == 0x46U && header[1] == 0x53U && header[2] == 0x54U && header[3] == 0x4dU) {
        xx_nintendo_bfstm reader;
        bool valid;
        xx_nintendo_bfstm_init(&reader, dev, 0);
        valid = xx_nintendo_bfstm_check_is_valid(&reader.format, NULL);
        xx_nintendo_bfstm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BFSTM;
    }
    if (total_size >= 64 && header_size >= 4U && header[0] == 0x46U && header[1] == 0x57U && header[2] == 0x41U && header[3] == 0x56U) {
        xx_nintendo_bfwav reader;
        bool valid;
        xx_nintendo_bfwav_init(&reader, dev, 0);
        valid = xx_nintendo_bfwav_check_is_valid(&reader.format, NULL);
        xx_nintendo_bfwav_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BFWAV;
    }
    if (total_size >= 64 && header_size >= 4U && header[0] == 0x43U && header[1] == 0x57U && header[2] == 0x41U && header[3] == 0x56U) {
        xx_nintendo_bcwav reader;
        bool valid;
        xx_nintendo_bcwav_init(&reader, dev, 0);
        valid = xx_nintendo_bcwav_check_is_valid(&reader.format, NULL);
        xx_nintendo_bcwav_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BCWAV;
    }
    if (total_size >= 108 && header_size >= 4U && header[0] == 0x46U && header[1] == 0x52U && header[2] == 0x45U && header[3] == 0x53U) {
        xx_nintendo_bfres reader;
        bool valid;
        xx_nintendo_bfres_init(&reader, dev, 0);
        valid = xx_nintendo_bfres_check_is_valid(&reader.format, NULL);
        xx_nintendo_bfres_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BFRES;
    }
    if (total_size >= 20 && header_size >= 4U && header[0] == 0x46U && header[1] == 0x4cU && header[2] == 0x59U && header[3] == 0x54U) {
        xx_nintendo_bflyt reader;
        bool valid;
        xx_nintendo_bflyt_init(&reader, dev, 0);
        valid = xx_nintendo_bflyt_check_is_valid(&reader.format, NULL);
        xx_nintendo_bflyt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BFLYT;
    }
    if (total_size >= 20 && header_size >= 4U && header[0] == 0x43U && header[1] == 0x4cU && header[2] == 0x59U && header[3] == 0x54U) {
        xx_nintendo_bclyt reader;
        bool valid;
        xx_nintendo_bclyt_init(&reader, dev, 0);
        valid = xx_nintendo_bclyt_check_is_valid(&reader.format, NULL);
        xx_nintendo_bclyt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BCLYT;
    }
    if (total_size >= 20 && header_size >= 4U && header[0] == 0x46U && header[1] == 0x46U && header[2] == 0x4eU && header[3] == 0x54U) {
        xx_nintendo_bfnt reader;
        bool valid;
        xx_nintendo_bfnt_init(&reader, dev, 0);
        valid = xx_nintendo_bfnt_check_is_valid(&reader.format, NULL);
        xx_nintendo_bfnt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BFNT;
    }
    if (total_size >= 20 && header_size >= 4U && header[0] == 0x43U && header[1] == 0x46U && header[2] == 0x4eU && header[3] == 0x54U) {
        xx_nintendo_bcfnt reader;
        bool valid;
        xx_nintendo_bcfnt_init(&reader, dev, 0);
        valid = xx_nintendo_bcfnt_check_is_valid(&reader.format, NULL);
        xx_nintendo_bcfnt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BCFNT;
    }
    if (total_size >= 32 && header_size >= 4U && header[0] == 0x33U && header[1] == 0x44U && header[2] == 0x53U && header[3] == 0x58U) {
        xx_nintendo_3dsx reader;
        bool valid;
        xx_nintendo_3dsx_init(&reader, dev, 0);
        valid = xx_nintendo_3dsx_check_is_valid(&reader.format, NULL);
        xx_nintendo_3dsx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_3DSX;
    }
    if (total_size >= 16 && header_size >= 4U && header[0] == 0x54U && header[1] == 0x49U && header[2] == 0x4dU && header[3] == 0x32U) {
        xx_sony_tim2 reader;
        bool valid;
        xx_sony_tim2_init(&reader, dev, 0);
        valid = xx_sony_tim2_check_is_valid(&reader.format, NULL);
        xx_sony_tim2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SONY_TIM2;
    }
    if (total_size >= 184 && header_size >= 4U && header[0] == 0x50U && header[1] == 0x41U && header[2] == 0x4dU && header[3] == 0x46U) {
        xx_sony_pamf reader;
        bool valid;
        xx_sony_pamf_init(&reader, dev, 0);
        valid = xx_sony_pamf_check_is_valid(&reader.format, NULL);
        xx_sony_pamf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SONY_PAMF;
    }
    if (total_size >= 16 && header_size >= 4U && header[0] == 0x47U && header[1] == 0x56U && header[2] == 0x52U && header[3] == 0x54U) {
        xx_sega_gvr reader;
        bool valid;
        xx_sega_gvr_init(&reader, dev, 0);
        valid = xx_sega_gvr_check_is_valid(&reader.format, NULL);
        xx_sega_gvr_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SEGA_GVR;
    }
    if (total_size >= 52 && header_size >= 4U && header[0] == 0x57U && header[1] == 0x42U && header[2] == 0x4eU && header[3] == 0x44U) {
        xx_microsoft_xwb reader;
        bool valid;
        xx_microsoft_xwb_init(&reader, dev, 0);
        valid = xx_microsoft_xwb_check_is_valid(&reader.format, NULL);
        xx_microsoft_xwb_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MICROSOFT_XWB;
    }
    if (total_size >= 138 && header_size >= 4U && header[0] == 0x53U && header[1] == 0x44U && header[2] == 0x42U && header[3] == 0x4bU) {
        xx_microsoft_xsb reader;
        bool valid;
        xx_microsoft_xsb_init(&reader, dev, 0);
        valid = xx_microsoft_xsb_check_is_valid(&reader.format, NULL);
        xx_microsoft_xsb_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MICROSOFT_XSB;
    }
    if (total_size >= 152 && header_size >= 8U && header[0] == 0x5fU && header[1] == 0x41U && header[2] == 0x52U && header[3] == 0x43U && header[4] == 0x48U && header[5] == 0x49U && header[6] == 0x56U && header[7] == 0x45U) {
        xx_relic_sga reader;
        bool valid;
        xx_relic_sga_init(&reader, dev, 0);
        valid = xx_relic_sga_check_is_valid(&reader.format, NULL);
        xx_relic_sga_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RELIC_SGA;
    }
    if (header_size >= 9 && !xx_rt_memcmp(header,"/* XPM */",9)) {
        xx_xpm reader;
        bool valid;
        xx_xpm_init(&reader, dev, 0);
        valid = xx_xpm_check_is_valid(&reader.format, NULL);
        xx_xpm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_XPM;
    }
    if (header_size >= 68 && header[0]==10 && (header[1]==0 || (header[1]>=2 && header[1]<=5)) && header[2]==1 && (header[3]==1 || header[3]==2 || header[3]==4 || header[3]==8)) {
        xx_pcx reader;
        bool valid;
        xx_pcx_init(&reader, dev, 0);
        valid = xx_pcx_check_is_valid(&reader.format, NULL);
        xx_pcx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PCX;
    }
    if (header_size >= 12 && !xx_rt_memcmp(header,"FORM",4) && !xx_rt_memcmp(header+8,"ILBM",4)) {
        xx_iff_ilbm reader;
        bool valid;
        xx_iff_ilbm_init(&reader, dev, 0);
        valid = xx_iff_ilbm_check_is_valid(&reader.format, NULL);
        xx_iff_ilbm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IFF_ILBM;
    }
    if (header_size >= 16 && header[0]==0x52 && header[1]==0xCC && header[12]==8) {
        xx_utah_rle reader;
        bool valid;
        xx_utah_rle_init(&reader, dev, 0);
        valid = xx_utah_rle_check_is_valid(&reader.format, NULL);
        xx_utah_rle_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UTAH_RLE;
    }
    if (header_size >= 7 && ((!xx_rt_memcmp(header,"#?RGBE",6) && (header[6]==10 || header[6]==13)) || (header_size>=11 && !xx_rt_memcmp(header,"#?RADIANCE",10) && (header[10]==10 || header[10]==13)))) {
        xx_radiance_hdr reader;
        bool valid;
        xx_radiance_hdr_init(&reader, dev, 0);
        valid = xx_radiance_hdr_check_is_valid(&reader.format, NULL);
        xx_radiance_hdr_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RADIANCE_HDR;
    }
    if (header_size >= 36 && (!xx_rt_memcmp(header,"SDPX",4) || !xx_rt_memcmp(header,"XPDS",4))) {
        xx_dpx reader;
        bool valid;
        xx_dpx_init(&reader, dev, 0);
        valid = xx_dpx_check_is_valid(&reader.format, NULL);
        xx_dpx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DPX;
    }
    if (header_size >= 32 && ((header[0]==0x80 && header[1]==0x2A && header[2]==0x5F && header[3]==0xD7) || (header[0]==0xD7 && header[1]==0x5F && header[2]==0x2A && header[3]==0x80))) {
        xx_cineon reader;
        bool valid;
        xx_cineon_init(&reader, dev, 0);
        valid = xx_cineon_check_is_valid(&reader.format, NULL);
        xx_cineon_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CINEON;
    }
    if (header_size >= 100 && header[4]==0 && header[5]==0 && header[6]==0 && header[7]==7 && header[8]==0 && header[9]==0 && header[10]==0 && header[11]==2) {
        xx_xwd reader;
        bool valid;
        xx_xwd_init(&reader, dev, 0);
        valid = xx_xwd_check_is_valid(&reader.format, NULL);
        xx_xwd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_XWD;
    }
    if (header_size >= 12 && header[0]==1 && header[1]==0xDA && header[2]<=1 && (header[3]==1 || header[3]==2)) {
        xx_sgi_rgb reader;
        bool valid;
        xx_sgi_rgb_init(&reader, dev, 0);
        valid = xx_sgi_rgb_check_is_valid(&reader.format, NULL);
        xx_sgi_rgb_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SGI_RGB;
    }
    if (header_size >= 128 && header[4]==0xE0 && header[5]==0xA5 && (header[12]==8 || header[12]==16 || header[12]==32) && header[13]==0) {
        xx_aseprite reader;
        bool valid;
        xx_aseprite_init(&reader, dev, 0);
        valid = xx_aseprite_check_is_valid(&reader.format, NULL);
        xx_aseprite_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ASEPRITE;
    }
    if (((header_size >= 6 && header[0] == 147 && header[1] == 78 && header[2] == 85 && header[3] == 77 && header[4] == 80 && header[5] == 89))) {
        xx_numpy_npy reader;
        bool valid;
        xx_numpy_npy_init(&reader, dev, 0);
        valid = xx_numpy_npy_check_is_valid(&reader.format, NULL);
        xx_numpy_npy_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NUMPY_NPY;
    }
    if (((header_size >= 128 && header[126] == 73 && header[127] == 77) || (header_size >= 128 && header[126] == 77 && header[127] == 73))) {
        xx_matlab_mat5 reader;
        bool valid;
        xx_matlab_mat5_init(&reader, dev, 0);
        valid = xx_matlab_mat5_check_is_valid(&reader.format, NULL);
        xx_matlab_mat5_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MATLAB_MAT5;
    }
    if (((header_size >= 3 && header[0] == 67 && header[1] == 68 && header[2] == 70))) {
        xx_netcdf_classic reader;
        bool valid;
        xx_netcdf_classic_init(&reader, dev, 0);
        valid = xx_netcdf_classic_check_is_valid(&reader.format, NULL);
        xx_netcdf_classic_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NETCDF_CLASSIC;
    }
    if (((header_size >= 4 && header[0] == 14 && header[1] == 3 && header[2] == 19 && header[3] == 1))) {
        xx_hdf4 reader;
        bool valid;
        xx_hdf4_init(&reader, dev, 0);
        valid = xx_hdf4_check_is_valid(&reader.format, NULL);
        xx_hdf4_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_HDF4;
    }
    if (((header_size >= 48 && header[0] == 72 && header[1] == 69 && header[2] == 65 && header[3] == 68 && header[4] == 69 && header[5] == 82 && header[6] == 32 && header[7] == 82 && header[8] == 69 && header[9] == 67 && header[10] == 79 && header[11] == 82 && header[12] == 68 && header[13] == 42 && header[14] == 42 && header[15] == 42 && header[16] == 42 && header[17] == 42 && header[18] == 42 && header[19] == 42 && header[20] == 76 && header[21] == 73 && header[22] == 66 && header[23] == 82 && header[24] == 65 && header[25] == 82 && header[26] == 89 && header[27] == 32 && header[28] == 72 && header[29] == 69 && header[30] == 65 && header[31] == 68 && header[32] == 69 && header[33] == 82 && header[34] == 32 && header[35] == 82 && header[36] == 69 && header[37] == 67 && header[38] == 79 && header[39] == 82 && header[40] == 68 && header[41] == 33 && header[42] == 33 && header[43] == 33 && header[44] == 33 && header[45] == 33 && header[46] == 33 && header[47] == 33))) {
        xx_sas_xport reader;
        bool valid;
        xx_sas_xport_init(&reader, dev, 0);
        valid = xx_sas_xport_check_is_valid(&reader.format, NULL);
        xx_sas_xport_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SAS_XPORT;
    }
    if (((header_size >= 4 && header[0] == 36 && header[1] == 70 && header[2] == 76 && header[3] == 50))) {
        xx_spss_sav reader;
        bool valid;
        xx_spss_sav_init(&reader, dev, 0);
        valid = xx_spss_sav_check_is_valid(&reader.format, NULL);
        xx_spss_sav_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SPSS_SAV;
    }
    if (((header_size >= 11 && header[0] == 60 && header[1] == 115 && header[2] == 116 && header[3] == 97 && header[4] == 116 && header[5] == 97 && header[6] == 95 && header[7] == 100 && header[8] == 116 && header[9] == 97 && header[10] == 62))) {
        xx_stata_dta reader;
        bool valid;
        xx_stata_dta_init(&reader, dev, 0);
        valid = xx_stata_dta_check_is_valid(&reader.format, NULL);
        xx_stata_dta_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_STATA_DTA;
    }
    if (((header_size >= 6 && header[0] == 65 && header[1] == 82 && header[2] == 82 && header[3] == 79 && header[4] == 87 && header[5] == 49))) {
        xx_apache_arrow_file reader;
        bool valid;
        xx_apache_arrow_file_init(&reader, dev, 0);
        valid = xx_apache_arrow_file_check_is_valid(&reader.format, NULL);
        xx_apache_arrow_file_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_APACHE_ARROW_FILE;
    }
    if (((header_size >= 4 && header[0] == 80 && header[1] == 65 && header[2] == 82 && header[3] == 49))) {
        xx_apache_parquet reader;
        bool valid;
        xx_apache_parquet_init(&reader, dev, 0);
        valid = xx_apache_parquet_check_is_valid(&reader.format, NULL);
        xx_apache_parquet_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_APACHE_PARQUET;
    }
}
