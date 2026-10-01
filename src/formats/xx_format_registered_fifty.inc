/* Reviewed probes for newly registered containers; included inside detection. */
{
    uint8_t header[512] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (is_mz) {
        xx_advanced_installer_bootstrapper reader;
        bool valid;
        xx_advanced_installer_bootstrapper_init(&reader, dev, 0);
        valid = xx_advanced_installer_bootstrapper_check_is_valid(&reader.format, NULL);
        xx_advanced_installer_bootstrapper_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADVANCED_INSTALLER_BOOTSTRAPPER;
    }
    if (is_mz) {
        xx_ardi_installer reader;
        bool valid;
        xx_ardi_installer_init(&reader, dev, 0);
        valid = xx_ardi_installer_check_is_valid(&reader.format, NULL);
        xx_ardi_installer_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ARDI_INSTALLER;
    }
    if (is_mz) {
        xx_arni_installer_container reader;
        bool valid;
        xx_arni_installer_container_init(&reader, dev, 0);
        valid = xx_arni_installer_container_check_is_valid(&reader.format, NULL);
        xx_arni_installer_container_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ARNI_INSTALLER_CONTAINER;
    }
    if (is_mz) {
        xx_ej_technologies_install reader;
        bool valid;
        xx_ej_technologies_install_init(&reader, dev, 0);
        valid = xx_ej_technologies_install_check_is_valid(&reader.format, NULL);
        xx_ej_technologies_install_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_EJ_TECHNOLOGIES_INSTALL;
    }
    if (header_size >= 11U && header[0] == 0x01U && xx_rt_memcmp(header + 1U, "F Install ", 10U) == 0) {
        xx_finstall reader;
        bool valid;
        xx_finstall_init(&reader, dev, 0);
        valid = xx_finstall_check_is_valid(&reader.format, NULL);
        xx_finstall_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FINSTALL;
    }
    if (is_mz || (header_size >= 8U && xx_rt_memcmp(header, "\xC0\xDE\xCE\xCB\x8D\x8D\x8D\x8D", 8U) == 0)) {
        xx_ghost_installer reader;
        bool valid;
        xx_ghost_installer_init(&reader, dev, 0);
        valid = xx_ghost_installer_check_is_valid(&reader.format, NULL);
        xx_ghost_installer_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GHOST_INSTALLER;
    }
    if (is_mz) {
        xx_ibm_zpak_installer reader;
        bool valid;
        xx_ibm_zpak_installer_init(&reader, dev, 0);
        valid = xx_ibm_zpak_installer_check_is_valid(&reader.format, NULL);
        xx_ibm_zpak_installer_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IBM_ZPAK_INSTALLER;
    }
    if (is_mz || (header_size >= 4U && xx_rt_memcmp(header, "IFAH", 4U) == 0)) {
        xx_ifah_installer reader;
        bool valid;
        xx_ifah_installer_init(&reader, dev, 0);
        valid = xx_ifah_installer_check_is_valid(&reader.format, NULL);
        xx_ifah_installer_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IFAH_INSTALLER;
    }
    if (is_mz) {
        xx_inno_setup reader;
        bool valid;
        xx_inno_setup_init(&reader, dev, 0);
        valid = xx_inno_setup_check_is_valid(&reader.format, NULL);
        xx_inno_setup_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INNO_SETUP;
    }
    if (is_mz) {
        xx_installer_vise_windows reader;
        bool valid;
        xx_installer_vise_windows_init(&reader, dev, 0);
        valid = xx_installer_vise_windows_check_is_valid(&reader.format, NULL);
        xx_installer_vise_windows_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLER_VISE_WINDOWS;
    }
    if (is_mz) {
        xx_installshield_12_setup reader;
        bool valid;
        xx_installshield_12_setup_init(&reader, dev, 0);
        valid = xx_installshield_12_setup_check_is_valid(&reader.format, NULL);
        xx_installshield_12_setup_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLSHIELD_12_SETUP;
    }
    if (is_mz) {
        xx_installshield_3 reader;
        bool valid;
        xx_installshield_3_init(&reader, dev, 0);
        valid = xx_installshield_3_check_is_valid(&reader.format, NULL);
        xx_installshield_3_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLSHIELD_3;
    }
    if (is_mz) {
        xx_installshield_7_setup reader;
        bool valid;
        xx_installshield_7_setup_init(&reader, dev, 0);
        valid = xx_installshield_7_setup_check_is_valid(&reader.format, NULL);
        xx_installshield_7_setup_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLSHIELD_7_SETUP;
    }
    if (header_size >= 19U && header[0] >= 0x20U && header[0] != 0x7FU) {
        xx_installshield_7_setup2 reader;
        bool valid;
        xx_installshield_7_setup2_init(&reader, dev, 0);
        valid = xx_installshield_7_setup2_check_is_valid(&reader.format, NULL);
        xx_installshield_7_setup2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLSHIELD_7_SETUP2;
    }
    if (is_mz) {
        xx_installshield_developer reader;
        bool valid;
        xx_installshield_developer_init(&reader, dev, 0);
        valid = xx_installshield_developer_check_is_valid(&reader.format, NULL);
        xx_installshield_developer_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLSHIELD_DEVELOPER;
    }
    if (is_mz || (header_size >= 14U && xx_rt_memcmp(header, "ISSetupStream", 13U) == 0 && header[13] == 0U)) {
        xx_installshield_issetupstream reader;
        bool valid;
        xx_installshield_issetupstream_init(&reader, dev, 0);
        valid = xx_installshield_issetupstream_check_is_valid(&reader.format, NULL);
        xx_installshield_issetupstream_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLSHIELD_ISSETUPSTREAM;
    }
    if (xx_installshield_multiplatform_has_footer(dev)) {
        xx_installshield_multiplatform reader;
        bool valid;
        xx_installshield_multiplatform_init(&reader, dev, 0);
        valid = xx_installshield_multiplatform_check_is_valid(&reader.format, NULL);
        xx_installshield_multiplatform_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLSHIELD_MULTIPLATFORM;
    }
    if (header_size >= 4U && ((((header[0] >> 4U) | ((header[0] & 0x0FU) << 4U)) ^ 0xA2U) >= 0x20U) && ((((header[0] >> 4U) | ((header[0] & 0x0FU) << 4U)) ^ 0xA2U) <= 0x7FU)) {
        xx_installshield_skin reader;
        bool valid;
        xx_installshield_skin_init(&reader, dev, 0);
        valid = xx_installshield_skin_check_is_valid(&reader.format, NULL);
        xx_installshield_skin_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INSTALLSHIELD_SKIN;
    }
    if (header_size >= 7U && header[2] == 0x2DU && header[3] == 0x6CU && header[4] == 0x5AU && (header[5] == 0x30U || header[5] == 0x31U || header[5] == 0x35U) && header[6] == 0x2DU) {
        xx_microfox_put reader;
        bool valid;
        xx_microfox_put_init(&reader, dev, 0);
        valid = xx_microfox_put_check_is_valid(&reader.format, NULL);
        xx_microfox_put_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MICROFOX_PUT;
    }
    if (is_mz) {
        xx_o_setup reader;
        bool valid;
        xx_o_setup_init(&reader, dev, 0);
        valid = xx_o_setup_check_is_valid(&reader.format, NULL);
        xx_o_setup_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_O_SETUP;
    }
    if (is_mz) {
        xx_pc_install_setup reader;
        bool valid;
        xx_pc_install_setup_init(&reader, dev, 0);
        valid = xx_pc_install_setup_check_is_valid(&reader.format, NULL);
        xx_pc_install_setup_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PC_INSTALL_SETUP;
    }
    if (is_mz || xx_pyinstaller_one_executable_has_tail_cookie(dev)) {
        xx_pyinstaller_one_executable reader;
        bool valid;
        xx_pyinstaller_one_executable_init(&reader, dev, 0);
        valid = xx_pyinstaller_one_executable_check_is_valid(&reader.format, NULL);
        xx_pyinstaller_one_executable_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PYINSTALLER_ONE_EXECUTABLE;
    }
    if (is_mz) {
        xx_qsetup_installation_suite reader;
        bool valid;
        xx_qsetup_installation_suite_init(&reader, dev, 0);
        valid = xx_qsetup_installation_suite_check_is_valid(&reader.format, NULL);
        xx_qsetup_installation_suite_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_QSETUP_INSTALLATION_SUITE;
    }
    if (header_size >= 26U && header[14] >= 2U && header[14] <= 13U && header[15] == 0U && header_size >= 24U + header[14] && header[15U + header[14]] == 0U && header[16U + header[14]] == 0xB5U && header[17U + header[14]] == 0x9CU && header[18U + header[14]] <= 1U && header[19U + header[14]] == 0xFFU) {
        xx_rtpatch_setup_data reader;
        bool valid;
        xx_rtpatch_setup_data_init(&reader, dev, 0);
        valid = xx_rtpatch_setup_data_check_is_valid(&reader.format, NULL);
        xx_rtpatch_setup_data_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RTPATCH_SETUP_DATA;
    }
    if (is_mz) {
        xx_setup_factory reader;
        bool valid;
        xx_setup_factory_init(&reader, dev, 0);
        valid = xx_setup_factory_check_is_valid(&reader.format, NULL);
        xx_setup_factory_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SETUP_FACTORY;
    }
    if (is_mz) {
        xx_sfx_ebook_compiler_executables reader;
        bool valid;
        xx_sfx_ebook_compiler_executables_init(&reader, dev, 0);
        valid = xx_sfx_ebook_compiler_executables_check_is_valid(&reader.format, NULL);
        xx_sfx_ebook_compiler_executables_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_EBOOK_COMPILER_EXECUTABLES;
    }
    if (is_mz) {
        xx_spoon_installer reader;
        bool valid;
        xx_spoon_installer_init(&reader, dev, 0);
        valid = xx_spoon_installer_check_is_valid(&reader.format, NULL);
        xx_spoon_installer_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SPOON_INSTALLER;
    }
    if (is_mz) {
        xx_tarma_installer reader;
        bool valid;
        xx_tarma_installer_init(&reader, dev, 0);
        valid = xx_tarma_installer_check_is_valid(&reader.format, NULL);
        xx_tarma_installer_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TARMA_INSTALLER;
    }
    if (header_size >= 4U && ((header[0] == 0x44U && header[1] == 0x4FU && header[2] == 0x53U && header[3] <= 7U) || ((total_size == 901120 || total_size == 1802240) && header[0] == 0U && header[1] == 0U && header[2] == 0U && header[3] == 0U))) {
        xx_adf reader;
        bool valid;
        xx_adf_init(&reader, dev, 0);
        valid = xx_adf_check_is_valid(&reader.format, NULL);
        xx_adf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADF;
    }
    if (header_size >= 4U && header[0] == 0x45U && header[1] == 0x52U && (header[2] == 2U || header[2] == 4U || header[2] == 8U || header[2] == 16U) && header[3] == 0U) {
        xx_apm reader;
        bool valid;
        xx_apm_init(&reader, dev, 0);
        valid = xx_apm_check_is_valid(&reader.format, NULL);
        xx_apm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_APM;
    }
    if (header_size >= 8U && xx_rt_memcmp(header, "vhdxfile", 8U) == 0) {
        xx_vhdx reader;
        bool valid;
        xx_vhdx_init(&reader, dev, 0);
        valid = xx_vhdx_check_is_valid(&reader.format, NULL);
        xx_vhdx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VHDX;
    }
    if ((header_size >= 2U && header[0] == 0x3CU && header[1] == 0x7EU) || (header_size >= 6U && xx_rt_memcmp(header, "xbtoa", 5U) == 0)) {
        xx_btoa reader;
        bool valid;
        xx_btoa_init(&reader, dev, 0);
        valid = xx_btoa_check_is_valid(&reader.format, NULL);
        xx_btoa_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BTOA;
    }
    if (header_size >= 16U && xx_rt_memcmp(header, "MComprHD", 8U) == 0) {
        xx_chd reader;
        bool valid;
        xx_chd_init(&reader, dev, 0);
        valid = xx_chd_check_is_valid(&reader.format, NULL);
        xx_chd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CHD;
    }
    if (header_size >= 4U && xx_rt_memcmp(header, "ITSF", 4U) == 0) {
        xx_chm reader;
        bool valid;
        xx_chm_init(&reader, dev, 0);
        valid = xx_chm_check_is_valid(&reader.format, NULL);
        xx_chm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CHM;
    }
    if (header_size >= 15U && xx_rt_memcmp(header, "#!/bin/sh\n#", 11U) == 0 && header[13] == '.' && header[14] == '0' && (((header[11] == 'V' || header[11] == 'v') && header[12] == '2') || ((header[11] == 'L' || header[11] == 'l') && header[12] == '3') || ((header[11] == 'Z' || header[11] == 'z') && header[12] == '4'))) {
        xx_cloop reader;
        bool valid;
        xx_cloop_init(&reader, dev, 0);
        valid = xx_cloop_check_is_valid(&reader.format, NULL);
        xx_cloop_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CLOOP;
    }
    if (xx_cue_test_magic(header, header_size)) {
        xx_cue reader;
        bool valid;
        xx_cue_init(&reader, dev, 0);
        valid = xx_cue_check_is_valid(&reader.format, NULL);
        xx_cue_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CUE;
    }
    if (header_size >= 4U && header[0] == 'D' && header[1] == 'H' && header[2] == 3U && header[3] == 4U) {
        xx_dahuazip reader;
        bool valid;
        xx_dahuazip_init(&reader, dev, 0);
        valid = xx_dahuazip_check_is_valid(&reader.format, NULL);
        xx_dahuazip_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DAHUA_ZIP;
    }
    if (header_size >= 8U && header[0] == 'M' && header[1] == 'G' && xx_rt_memcmp(header + 4U, "0><1", 4U) == 0) {
        xx_dmsfw reader;
        bool valid;
        xx_dmsfw_init(&reader, dev, 0);
        valid = xx_dmsfw_check_is_valid(&reader.format, NULL);
        xx_dmsfw_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DMSFW;
    }
    if (header_size >= 8U && header[0] == 'E' && header[1] == 'V' && header[2] == 'F' && header[3] == 9U && header[4] == 13U && header[5] == 10U && header[6] == 255U && header[7] == 0U) {
        xx_ewf reader;
        bool valid;
        xx_ewf_init(&reader, dev, 0);
        valid = xx_ewf_check_is_valid(&reader.format, NULL);
        xx_ewf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_EWF;
    }
    if (xx_godot_engine_pck_probe_device(dev)) {
        xx_godot_engine_pck reader;
        bool valid;
        xx_godot_engine_pck_init(&reader, dev, 0);
        valid = xx_godot_engine_pck_check_is_valid(&reader.format, NULL);
        xx_godot_engine_pck_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GODOT_ENGINE_PCK;
    }
    if (header_size >= 2U && header[0] == 0xA3U && header[1] == 1U) {
        xx_gpgsigned reader;
        bool valid;
        xx_gpgsigned_init(&reader, dev, 0);
        valid = xx_gpgsigned_check_is_valid(&reader.format, NULL);
        xx_gpgsigned_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GPG_SIGNED;
    }
    if (xx_ihex_check_magic(magic, magic_size) || xx_ihex_probe_device(dev, 0)) {
        xx_ihex reader;
        bool valid;
        xx_ihex_init(&reader, dev, 0);
        valid = xx_ihex_check_is_valid(&reader.format, NULL);
        xx_ihex_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IHEX;
    }
    if (header_size >= 8U && header[0] == 'K' && header[1] == 'W' && header[2] == 'A' && header[3] == 'J' && header[4] == 0x88U && header[5] == 0xF0U && header[6] == 0x27U && header[7] == 0xD1U) {
        xx_kwaj reader;
        bool valid;
        xx_kwaj_init(&reader, dev, 0);
        valid = xx_kwaj_check_is_valid(&reader.format, NULL);
        xx_kwaj_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_KWAJ;
    }
    if (header_size >= 16U && header[0] == 0U && xx_rt_memcmp(header + 1U, "           ", 11U) == 0 && header[12] == 0U && header[13] == 0U && (header[14] != 0U || header[15] != 0U)) {
        xx_lbr reader;
        bool valid;
        xx_lbr_init(&reader, dev, 0);
        valid = xx_lbr_check_is_valid(&reader.format, NULL);
        xx_lbr_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LBR;
    }
    if (header_size >= 4U && header[0] == 'b' && header[1] == 'v' && header[2] == 'x' && (header[3] == '-' || header[3] == '1' || header[3] == '2' || header[3] == 'n')) {
        xx_lzfsestream reader;
        bool valid;
        xx_lzfsestream_init(&reader, dev, 0);
        valid = xx_lzfsestream_check_is_valid(&reader.format, NULL);
        xx_lzfsestream_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LZFSE;
    }
    if (xx_nrg_probe_device(dev)) {
        xx_nrg reader;
        bool valid;
        xx_nrg_init(&reader, dev, 0);
        valid = xx_nrg_check_is_valid(&reader.format, NULL);
        xx_nrg_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NRG;
    }
    if (header_size >= 4U && header[0] == 'P' && header[1] == 'M' && header[2] == 'a' && (header[3] == 'g' || header[3] == '4')) {
        xx_packit_mac reader;
        bool valid;
        xx_packit_mac_init(&reader, dev, 0);
        valid = xx_packit_mac_check_is_valid(&reader.format, NULL);
        xx_packit_mac_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PACKIT_MAC;
    }
    if (header_size >= 4U && header[0] == 0xEDU && header[1] == 0xABU && header[2] == 0xEEU && header[3] == 0xDBU) {
        xx_rpm reader;
        bool valid;
        xx_rpm_init(&reader, dev, 0);
        valid = xx_rpm_check_is_valid(&reader.format, NULL);
        xx_rpm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RPM;
    }
    if (header_size >= 16U && xx_rt_memcmp(header, "StuffIt (c)1997-", 16U) == 0) {
        xx_stuffit5 reader;
        bool valid;
        xx_stuffit5_init(&reader, dev, 0);
        valid = xx_stuffit5_check_is_valid(&reader.format, NULL);
        xx_stuffit5_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_STUFFIT5;
    }
}
