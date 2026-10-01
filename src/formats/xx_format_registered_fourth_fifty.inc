/* Reviewed bounded probes for the fourth group of fifty readers. */
{
    uint8_t header[512] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 9 && (xx_rt_memcmp(header+0,"\x23\x21\x2f\x62\x69\x6e\x2f\x73\x68",9) == 0)) {
        xx_makeself reader;
        bool valid;
        xx_makeself_init(&reader, dev, 0);
        valid = xx_makeself_check_is_valid(&reader.format, NULL);
        xx_makeself_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MAKESELF;
    }
    if (header_size >= 9 && (xx_rt_memcmp(header+0,"\x23\x21\x2f\x62\x69\x6e\x2f\x73\x68",9) == 0)) {
        xx_sun_java_binsh reader;
        bool valid;
        xx_sun_java_binsh_init(&reader, dev, 0);
        valid = xx_sun_java_binsh_check_is_valid(&reader.format, NULL);
        xx_sun_java_binsh_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SUN_JAVA_BINSH;
    }
    if (header_size >= 9 && (xx_rt_memcmp(header+0,"\x23\x21\x2f\x62\x69\x6e\x2f\x73\x68",9) == 0)) {
        xx_installanywhere_unix reader;
        bool valid;
        xx_installanywhere_unix_init(&reader, dev, 0);
        valid = xx_installanywhere_unix_check_is_valid(&reader.format, NULL);
        xx_installanywhere_unix_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLANYWHERE_UNIX;
    }
    if (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0)) {
        xx_sfx_packagefortheweb reader;
        bool valid;
        xx_sfx_packagefortheweb_init(&reader, dev, 0);
        valid = xx_sfx_packagefortheweb_check_is_valid(&reader.format, NULL);
        xx_sfx_packagefortheweb_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_PACKAGEFORTHEWEB;
    }
    if (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0)) {
        xx_sfx_spis reader;
        bool valid;
        xx_sfx_spis_init(&reader, dev, 0);
        valid = xx_sfx_spis_check_is_valid(&reader.format, NULL);
        xx_sfx_spis_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_SPIS;
    }
    if (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\xeb",1) == 0)) {
        xx_sfx_lha reader;
        bool valid;
        xx_sfx_lha_init(&reader, dev, 0);
        valid = xx_sfx_lha_check_is_valid(&reader.format, NULL);
        xx_sfx_lha_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_LHA;
    }
    if (header_size >= 9 && (xx_rt_memcmp(header+0,"\x08\x4c\x4d\x44\x53\x4c\x4c\x33\x30",9) == 0 || xx_rt_memcmp(header+0,"\x08\x4c\x4d\x44\x42\x4d\x4c\x33\x30",9) == 0)) {
        xx_lmd_container reader;
        bool valid;
        xx_lmd_container_init(&reader, dev, 0);
        valid = xx_lmd_container_check_is_valid(&reader.format, NULL);
        xx_lmd_container_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LMD_CONTAINER;
    }
    if (header_size >= 4 && (xx_rt_memcmp(header+0,"\x48\x41\x50\x49",4) == 0)) {
        xx_totalannihilation_hpi reader;
        bool valid;
        xx_totalannihilation_hpi_init(&reader, dev, 0);
        valid = xx_totalannihilation_hpi_check_is_valid(&reader.format, NULL);
        xx_totalannihilation_hpi_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TOTALANNIHILATION_HPI;
    }
    if (header_size >= 4 && (xx_rt_memcmp(header+0,"\x52\x46\x46\x1a",4) == 0)) {
        xx_ravensoft_rff reader;
        bool valid;
        xx_ravensoft_rff_init(&reader, dev, 0);
        valid = xx_ravensoft_rff_check_is_valid(&reader.format, NULL);
        xx_ravensoft_rff_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RAVENSOFT_RFF;
    }
    if (header_size >= 4 && (xx_rt_memcmp(header+0,"\x50\x4f\x44\x32",4) == 0)) {
        xx_terminalreality_pod reader;
        bool valid;
        xx_terminalreality_pod_init(&reader, dev, 0);
        valid = xx_terminalreality_pod_check_is_valid(&reader.format, NULL);
        xx_terminalreality_pod_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TERMINALREALITY_POD;
    }
    if (header_size >= 4 && (xx_rt_memcmp(header+0,"\xce\x0a\x89\x51",4) == 0 || xx_rt_memcmp(header+0,"\x51\x89\x0a\xce",4) == 0)) {
        xx_volition_vpp reader;
        bool valid;
        xx_volition_vpp_init(&reader, dev, 0);
        valid = xx_volition_vpp_check_is_valid(&reader.format, NULL);
        xx_volition_vpp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VOLITION_VPP;
    }
    if (header_size >= 11 && (xx_rt_memcmp(header+0,"\x58\x50\x33\x0d\x0a\x20\x0a\x1a\x8b\x67\x01",11) == 0)) {
        xx_kirikiri_xp3 reader;
        bool valid;
        xx_kirikiri_xp3_init(&reader, dev, 0);
        valid = xx_kirikiri_xp3_check_is_valid(&reader.format, NULL);
        xx_kirikiri_xp3_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_KIRIKIRI_XP3;
    }
    if (header_size >= 4 && (xx_rt_memcmp(header+0,"\x42\x4e\x44\x33",4) == 0 || xx_rt_memcmp(header+0,"\x42\x4e\x44\x34",4) == 0)) {
        xx_fromsoftware_binder reader;
        bool valid;
        xx_fromsoftware_binder_init(&reader, dev, 0);
        valid = xx_fromsoftware_binder_check_is_valid(&reader.format, NULL);
        xx_fromsoftware_binder_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FROMSOFTWARE_BINDER;
    }
    if (header_size >= 4 && (xx_rt_memcmp(header+0,"\x4d\x59\x50\x00",4) == 0)) {
        xx_mythic_myp reader;
        bool valid;
        xx_mythic_myp_init(&reader, dev, 0);
        valid = xx_mythic_myp_check_is_valid(&reader.format, NULL);
        xx_mythic_myp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MYTHIC_MYP;
    }
    if (header_size >= 18 && (xx_rt_memcmp(header+0,"\x0d\x0a\x52\x65\x7a\x4d\x67\x72\x20\x56\x65\x72\x73\x69\x6f\x6e\x20\x31",18) == 0)) {
        xx_lithtech_rez reader;
        bool valid;
        xx_lithtech_rez_init(&reader, dev, 0);
        valid = xx_lithtech_rez_check_is_valid(&reader.format, NULL);
        xx_lithtech_rez_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LITHTECH_REZ;
    }
    if (total_size >= 512 && header_size >= 260U && header[256] == 0x4eU && header[257] == 0x43U && header[258] == 0x43U && header[259] == 0x48U) {
        xx_nintendo_ncch reader;
        bool valid;
        xx_nintendo_ncch_init(&reader, dev, 0);
        valid = xx_nintendo_ncch_check_is_valid(&reader.format, NULL);
        xx_nintendo_ncch_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_NCCH;
    }
    if (total_size >= 512 && header_size >= 260U && header[256] == 0x4eU && header[257] == 0x43U && header[258] == 0x53U && header[259] == 0x44U) {
        xx_nintendo_ncsd reader;
        bool valid;
        xx_nintendo_ncsd_init(&reader, dev, 0);
        valid = xx_nintendo_ncsd_check_is_valid(&reader.format, NULL);
        xx_nintendo_ncsd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_NCSD;
    }
    if (total_size >= 8224 && header_size >= 8U && header[0] == 0x20U && header[1] == 0x20U && header[2] == 0x00U && header[3] == 0x00U && header[4] == 0x00U && header[5] == 0x00U && header[6] == 0x00U && header[7] == 0x00U) {
        xx_nintendo_cia reader;
        bool valid;
        xx_nintendo_cia_init(&reader, dev, 0);
        valid = xx_nintendo_cia_check_is_valid(&reader.format, NULL);
        xx_nintendo_cia_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_CIA;
    }
    if (total_size >= 512 && header_size >= 208U && header[192] == 0x24U && header[193] == 0xffU && header[194] == 0xaeU && header[195] == 0x51U && header[196] == 0x69U && header[197] == 0x9aU && header[198] == 0xa2U && header[199] == 0x21U && header[200] == 0x3dU && header[201] == 0x84U && header[202] == 0x82U && header[203] == 0x0aU && header[204] == 0x84U && header[205] == 0xe4U && header[206] == 0x09U && header[207] == 0xadU) {
        xx_nintendo_nds reader;
        bool valid;
        xx_nintendo_nds_init(&reader, dev, 0);
        valid = xx_nintendo_nds_check_is_valid(&reader.format, NULL);
        xx_nintendo_nds_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_NDS;
    }
    if (total_size >= 9312 && header_size >= 32U && header[28] == 0xc2U && header[29] == 0x33U && header[30] == 0x9fU && header[31] == 0x3dU) {
        xx_nintendo_gcm reader;
        bool valid;
        xx_nintendo_gcm_init(&reader, dev, 0);
        valid = xx_nintendo_gcm_check_is_valid(&reader.format, NULL);
        xx_nintendo_gcm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_GCM;
    }
    if (total_size >= 12 && header_size >= 4U && header[0] == 0x00U && header[1] == 0x20U && header[2] == 0xafU && header[3] == 0x30U) {
        xx_nintendo_tpl reader;
        bool valid;
        xx_nintendo_tpl_init(&reader, dev, 0);
        valid = xx_nintendo_tpl_check_is_valid(&reader.format, NULL);
        xx_nintendo_tpl_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_TPL;
    }
    if (total_size >= 20 && header_size >= 4U && header[0] == 0x10U && header[1] == 0x00U && header[2] == 0x00U && header[3] == 0x00U) {
        xx_sony_tim reader;
        bool valid;
        xx_sony_tim_init(&reader, dev, 0);
        valid = xx_sony_tim_check_is_valid(&reader.format, NULL);
        xx_sony_tim_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SONY_TIM;
    }
    if (total_size >= 48 && header_size >= 4U && header[0] == 0x56U && header[1] == 0x41U && header[2] == 0x47U && header[3] == 0x70U) {
        xx_sony_vag reader;
        bool valid;
        xx_sony_vag_init(&reader, dev, 0);
        valid = xx_sony_vag_check_is_valid(&reader.format, NULL);
        xx_sony_vag_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SONY_VAG;
    }
    if (total_size >= 24 && header_size >= 4U && header[0] == 0x4cU && header[1] == 0x53U && header[2] == 0x50U && header[3] == 0x4bU) {
        xx_larian_lspk reader;
        bool valid;
        xx_larian_lspk_init(&reader, dev, 0);
        valid = xx_larian_lspk_check_is_valid(&reader.format, NULL);
        xx_larian_lspk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LARIAN_LSPK;
    }
    if (total_size >= 52 && header_size >= 4U && header[0] == 0x4cU && header[1] == 0x53U && header[2] == 0x4fU && header[3] == 0x46U) {
        xx_larian_lsf reader;
        bool valid;
        xx_larian_lsf_init(&reader, dev, 0);
        valid = xx_larian_lsf_check_is_valid(&reader.format, NULL);
        xx_larian_lsf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LARIAN_LSF;
    }
    if (total_size >= 16 && header_size >= 4U && header[0] == 0x48U && header[1] == 0x50U && header[2] == 0x41U && header[3] == 0x4bU) {
        xx_valve_hpak reader;
        bool valid;
        xx_valve_hpak_init(&reader, dev, 0);
        valid = xx_valve_hpak_check_is_valid(&reader.format, NULL);
        xx_valve_hpak_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VALVE_HPAK;
    }
    if (total_size >= 34 && header_size >= 8U && header[0] == 0x52U && header[1] == 0x50U && header[2] == 0x41U && header[3] == 0x2dU && header[4] == 0x33U && header[5] == 0x2eU && header[6] == 0x30U && header[7] == 0x20U) {
        xx_renpy_rpa reader;
        bool valid;
        xx_renpy_rpa_init(&reader, dev, 0);
        valid = xx_renpy_rpa_check_is_valid(&reader.format, NULL);
        xx_renpy_rpa_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RENPY_RPA;
    }
    if (total_size >= 44 && header_size >= 4U && header[0] == 0xc1U && header[1] == 0x83U && header[2] == 0x2aU && header[3] == 0x9eU) {
        xx_unreal_package reader;
        bool valid;
        xx_unreal_package_init(&reader, dev, 0);
        valid = xx_unreal_package_check_is_valid(&reader.format, NULL);
        xx_unreal_package_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UNREAL_PACKAGE;
    }
    if (total_size >= 16 && header_size >= 4U && header[0] == 0x50U && header[1] == 0x56U && header[2] == 0x52U && header[3] == 0x54U) {
        xx_sega_pvr2 reader;
        bool valid;
        xx_sega_pvr2_init(&reader, dev, 0);
        valid = xx_sega_pvr2_check_is_valid(&reader.format, NULL);
        xx_sega_pvr2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SEGA_PVR2;
    }
    if (total_size >= 88 && header_size >= 8U && header[0] == 0x42U && header[1] == 0x4eU && header[2] == 0x54U && header[3] == 0x58U && header[4] == 0x00U && header[5] == 0x00U && header[6] == 0x00U && header[7] == 0x00U) {
        xx_nintendo_bntx reader;
        bool valid;
        xx_nintendo_bntx_init(&reader, dev, 0);
        valid = xx_nintendo_bntx_check_is_valid(&reader.format, NULL);
        xx_nintendo_bntx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BNTX;
    }
    if (header_size >= 8 && !xx_rt_memcmp(header,"icns",4)) {
        xx_icns reader;
        bool valid;
        xx_icns_init(&reader, dev, 0);
        valid = xx_icns_check_is_valid(&reader.format, NULL);
        xx_icns_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ICNS;
    }
    if (header_size >= 16 && !xx_rt_memcmp(header,"Xcur",4)) {
        xx_xcursor reader;
        bool valid;
        xx_xcursor_init(&reader, dev, 0);
        valid = xx_xcursor_check_is_valid(&reader.format, NULL);
        xx_xcursor_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_XCURSOR;
    }
    if (header_size >= 40 && !xx_rt_memcmp(header+36,"acsp",4) && (header[8]==2 || header[8]==4)) {
        xx_icc reader;
        bool valid;
        xx_icc_init(&reader, dev, 0);
        valid = xx_icc_check_is_valid(&reader.format, NULL);
        xx_icc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ICC;
    }
    if (header_size >= 14 && !xx_rt_memcmp(header,"qoif",4)) {
        xx_qoi reader;
        bool valid;
        xx_qoi_init(&reader, dev, 0);
        valid = xx_qoi_check_is_valid(&reader.format, NULL);
        xx_qoi_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_QOI;
    }
    if (header_size >= 16 && !xx_rt_memcmp(header,"farbfeld",8)) {
        xx_farbfeld reader;
        bool valid;
        xx_farbfeld_init(&reader, dev, 0);
        valid = xx_farbfeld_check_is_valid(&reader.format, NULL);
        xx_farbfeld_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FARBFELD;
    }
    if (header_size >= 3 && header[0]==80 && header[1]>=49 && header[1]<=55 && (header[2]==9 || header[2]==11 || header[2]==12 || header[2]==10 || header[2]==13 || header[2]==32)) {
        xx_pnm reader;
        bool valid;
        xx_pnm_init(&reader, dev, 0);
        valid = xx_pnm_check_is_valid(&reader.format, NULL);
        xx_pnm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PNM;
    }
    if (header_size >= 18 && (header[2]==1 || header[2]==2 || header[2]==3 || header[2]==9 || header[2]==10 || header[2]==11) && (header[12] || header[13]) && (header[14] || header[15]) && (header[16]==8 || header[16]==15 || header[16]==16 || header[16]==24 || header[16]==32) && !(header[17]&0xC0)) {
        xx_tga reader;
        bool valid;
        xx_tga_init(&reader, dev, 0);
        valid = xx_tga_check_is_valid(&reader.format, NULL);
        xx_tga_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TGA;
    }
    if (header_size >= 32 && header[0]==0x59 && header[1]==0xA6 && header[2]==0x6A && header[3]==0x95) {
        xx_sun_raster reader;
        bool valid;
        xx_sun_raster_init(&reader, dev, 0);
        valid = xx_sun_raster_check_is_valid(&reader.format, NULL);
        xx_sun_raster_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SUN_RASTER;
    }
    if (header_size >= 30 && !xx_rt_memcmp(header,"SIMPLE  =",9) && header[29]==84) {
        xx_fits reader;
        bool valid;
        xx_fits_init(&reader, dev, 0);
        valid = xx_fits_check_is_valid(&reader.format, NULL);
        xx_fits_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FITS;
    }
    if (header_size >= 132 && !xx_rt_memcmp(header+128,"DICM",4)) {
        xx_dicom reader;
        bool valid;
        xx_dicom_init(&reader, dev, 0);
        valid = xx_dicom_check_is_valid(&reader.format, NULL);
        xx_dicom_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DICOM;
    }
    if (((header_size >= 4 && header[0] == 212 && header[1] == 195 && header[2] == 178 && header[3] == 161) || (header_size >= 4 && header[0] == 161 && header[1] == 178 && header[2] == 195 && header[3] == 212) || (header_size >= 4 && header[0] == 77 && header[1] == 60 && header[2] == 178 && header[3] == 161) || (header_size >= 4 && header[0] == 161 && header[1] == 178 && header[2] == 60 && header[3] == 77))) {
        xx_pcap reader;
        bool valid;
        xx_pcap_init(&reader, dev, 0);
        valid = xx_pcap_check_is_valid(&reader.format, NULL);
        xx_pcap_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PCAP;
    }
    if (((header_size >= 8 && header[0] == 98 && header[1] == 116 && header[2] == 115 && header[3] == 110 && header[4] == 111 && header[5] == 111 && header[6] == 112 && header[7] == 0))) {
        xx_btsnoop reader;
        bool valid;
        xx_btsnoop_init(&reader, dev, 0);
        valid = xx_btsnoop_check_is_valid(&reader.format, NULL);
        xx_btsnoop_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BTSNOOP;
    }
    if (((header_size >= 4 && header[0] == 202 && header[1] == 254 && header[2] == 186 && header[3] == 190))) {
        xx_java_class reader;
        bool valid;
        xx_java_class_init(&reader, dev, 0);
        valid = xx_java_class_check_is_valid(&reader.format, NULL);
        xx_java_class_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_JAVA_CLASS;
    }
    if (((header_size >= 4 && header[0] == 116 && header[1] == 116 && header[2] == 99 && header[3] == 102))) {
        xx_sfnt_collection reader;
        bool valid;
        xx_sfnt_collection_init(&reader, dev, 0);
        valid = xx_sfnt_collection_check_is_valid(&reader.format, NULL);
        xx_sfnt_collection_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFNT_COLLECTION;
    }
    if (((header_size >= 16 && header[0] == 83 && header[1] == 81 && header[2] == 76 && header[3] == 105 && header[4] == 116 && header[5] == 101 && header[6] == 32 && header[7] == 102 && header[8] == 111 && header[9] == 114 && header[10] == 109 && header[11] == 97 && header[12] == 116 && header[13] == 32 && header[14] == 51 && header[15] == 0))) {
        xx_sqlite3 reader;
        bool valid;
        xx_sqlite3_init(&reader, dev, 0);
        valid = xx_sqlite3_check_is_valid(&reader.format, NULL);
        xx_sqlite3_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SQLITE3;
    }
    if (((header_size >= 4 && header[0] == 55 && header[1] == 127 && header[2] == 6 && header[3] == 130) || (header_size >= 4 && header[0] == 55 && header[1] == 127 && header[2] == 6 && header[3] == 131))) {
        xx_sqlite_wal reader;
        bool valid;
        xx_sqlite_wal_init(&reader, dev, 0);
        valid = xx_sqlite_wal_check_is_valid(&reader.format, NULL);
        xx_sqlite_wal_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SQLITE_WAL;
    }
    if (((header_size >= 4 && header[0] == 79 && header[1] == 98 && header[2] == 106 && header[3] == 1))) {
        xx_avro_object reader;
        bool valid;
        xx_avro_object_init(&reader, dev, 0);
        valid = xx_avro_object_check_is_valid(&reader.format, NULL);
        xx_avro_object_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AVRO_OBJECT;
    }
    if (((header_size >= 4 && header[0] == 103 && header[1] == 108 && header[2] == 84 && header[3] == 70))) {
        xx_glb reader;
        bool valid;
        xx_glb_init(&reader, dev, 0);
        valid = xx_glb_check_is_valid(&reader.format, NULL);
        xx_glb_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GLB;
    }
    if (((header_size >= 4 && header[0] == 3 && header[1] == 2 && header[2] == 35 && header[3] == 7) || (header_size >= 4 && header[0] == 7 && header[1] == 35 && header[2] == 2 && header[3] == 3))) {
        xx_spirv reader;
        bool valid;
        xx_spirv_init(&reader, dev, 0);
        valid = xx_spirv_check_is_valid(&reader.format, NULL);
        xx_spirv_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SPIRV;
    }
    if (((header_size >= 4 && header[0] == 67 && header[1] == 114 && header[2] == 50 && header[3] == 52))) {
        xx_crx reader;
        bool valid;
        xx_crx_init(&reader, dev, 0);
        valid = xx_crx_check_is_valid(&reader.format, NULL);
        xx_crx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CRX;
    }
}
