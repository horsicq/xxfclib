/* Reviewed bounded probes for the seventh group of fifty readers. */
{
    uint8_t header[512] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_imp reader;
        bool valid;
        xx_sfx_imp_init(&reader, dev, 0);
        valid = xx_sfx_imp_check_is_valid(&reader.format, NULL);
        xx_sfx_imp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_IMP;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_red reader;
        bool valid;
        xx_sfx_red_init(&reader, dev, 0);
        valid = xx_sfx_red_check_is_valid(&reader.format, NULL);
        xx_sfx_red_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_RED;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_ha reader;
        bool valid;
        xx_sfx_ha_init(&reader, dev, 0);
        valid = xx_sfx_ha_check_is_valid(&reader.format, NULL);
        xx_sfx_ha_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_HA;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_lzx reader;
        bool valid;
        xx_sfx_lzx_init(&reader, dev, 0);
        valid = xx_sfx_lzx_check_is_valid(&reader.format, NULL);
        xx_sfx_lzx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_LZX;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_sqx reader;
        bool valid;
        xx_sfx_sqx_init(&reader, dev, 0);
        valid = xx_sfx_sqx_check_is_valid(&reader.format, NULL);
        xx_sfx_sqx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_SQX;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_ain reader;
        bool valid;
        xx_sfx_ain_init(&reader, dev, 0);
        valid = xx_sfx_ain_check_is_valid(&reader.format, NULL);
        xx_sfx_ain_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_AIN;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_hap reader;
        bool valid;
        xx_sfx_hap_init(&reader, dev, 0);
        valid = xx_sfx_hap_check_is_valid(&reader.format, NULL);
        xx_sfx_hap_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_HAP;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_zoo reader;
        bool valid;
        xx_sfx_zoo_init(&reader, dev, 0);
        valid = xx_sfx_zoo_check_is_valid(&reader.format, NULL);
        xx_sfx_zoo_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ZOO;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_cazip reader;
        bool valid;
        xx_sfx_cazip_init(&reader, dev, 0);
        valid = xx_sfx_cazip_check_is_valid(&reader.format, NULL);
        xx_sfx_cazip_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_CAZIP;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_tgcf reader;
        bool valid;
        xx_sfx_tgcf_init(&reader, dev, 0);
        valid = xx_sfx_tgcf_check_is_valid(&reader.format, NULL);
        xx_sfx_tgcf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_TGCF;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_starkit reader;
        bool valid;
        xx_sfx_starkit_init(&reader, dev, 0);
        valid = xx_sfx_starkit_check_is_valid(&reader.format, NULL);
        xx_sfx_starkit_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_STARKIT;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_alz reader;
        bool valid;
        xx_sfx_alz_init(&reader, dev, 0);
        valid = xx_sfx_alz_check_is_valid(&reader.format, NULL);
        xx_sfx_alz_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ALZ;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_chm reader;
        bool valid;
        xx_sfx_chm_init(&reader, dev, 0);
        valid = xx_sfx_chm_check_is_valid(&reader.format, NULL);
        xx_sfx_chm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_CHM;
    }
    if (header_size >= 4 && (xx_rt_memcmp(header+0,"\x45\x47\x47\x41",4) == 0)) {
        xx_egg reader;
        bool valid;
        xx_egg_init(&reader, dev, 0);
        valid = xx_egg_check_is_valid(&reader.format, NULL);
        xx_egg_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_EGG;
    }
    if (header_size >= 6 && (xx_rt_memcmp(header+0,"\x4e\xf5\x46\xe9\x6c\xe5",6) == 0)) {
        xx_nufx reader;
        bool valid;
        xx_nufx_init(&reader, dev, 0);
        valid = xx_nufx_check_is_valid(&reader.format, NULL);
        xx_nufx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NUFX;
    }
    if (total_size >= 256 && header_size >= 4U && header[0] == 0x00U && header[1] == 0x00U && header[2] == 0x01U && header[3] == 0x00U) {
        xx_nintendo_dol reader;
        bool valid;
        xx_nintendo_dol_init(&reader, dev, 0);
        valid = xx_nintendo_dol_check_is_valid(&reader.format, NULL);
        xx_nintendo_dol_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_DOL;
    }
    if (total_size >= 32 && header_size >= 8U && header[0] == 0x4aU && header[1] == 0x33U && header[2] == 0x44U && header[3] == 0x32U && header[4] == 0x62U && header[5] == 0x6dU && header[6] == 0x64U && header[7] == 0x33U) {
        xx_nintendo_j3d_bmd reader;
        bool valid;
        xx_nintendo_j3d_bmd_init(&reader, dev, 0);
        valid = xx_nintendo_j3d_bmd_check_is_valid(&reader.format, NULL);
        xx_nintendo_j3d_bmd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_J3D_BMD;
    }
    if (total_size >= 32 && header_size >= 8U && header[0] == 0x4aU && header[1] == 0x33U && header[2] == 0x44U && header[3] == 0x31U && header[4] == 0x62U && header[5] == 0x74U && header[6] == 0x6bU && header[7] == 0x31U) {
        xx_nintendo_j3d_btk reader;
        bool valid;
        xx_nintendo_j3d_btk_init(&reader, dev, 0);
        valid = xx_nintendo_j3d_btk_check_is_valid(&reader.format, NULL);
        xx_nintendo_j3d_btk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_J3D_BTK;
    }
    if (total_size >= 64 && header_size >= 4U && header[0] == 0x52U && header[1] == 0x53U && header[2] == 0x54U && header[3] == 0x4dU) {
        xx_nintendo_brstm reader;
        bool valid;
        xx_nintendo_brstm_init(&reader, dev, 0);
        valid = xx_nintendo_brstm_check_is_valid(&reader.format, NULL);
        xx_nintendo_brstm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BRSTM;
    }
    if (total_size >= 32 && header_size >= 4U && header[0] == 0x52U && header[1] == 0x57U && header[2] == 0x41U && header[3] == 0x56U) {
        xx_nintendo_brwav reader;
        bool valid;
        xx_nintendo_brwav_init(&reader, dev, 0);
        valid = xx_nintendo_brwav_check_is_valid(&reader.format, NULL);
        xx_nintendo_brwav_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BRWAV;
    }
    if (total_size >= 16 && header_size >= 4U && header[0] == 0x52U && header[1] == 0x4cU && header[2] == 0x59U && header[3] == 0x54U) {
        xx_nintendo_brlyt reader;
        bool valid;
        xx_nintendo_brlyt_init(&reader, dev, 0);
        valid = xx_nintendo_brlyt_check_is_valid(&reader.format, NULL);
        xx_nintendo_brlyt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BRLYT;
    }
    if (total_size >= 16 && header_size >= 4U && header[0] == 0x52U && header[1] == 0x4cU && header[2] == 0x41U && header[3] == 0x4eU) {
        xx_nintendo_brlan reader;
        bool valid;
        xx_nintendo_brlan_init(&reader, dev, 0);
        valid = xx_nintendo_brlan_check_is_valid(&reader.format, NULL);
        xx_nintendo_brlan_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BRLAN;
    }
    if (total_size >= 56 && header_size >= 4U && header[0] == 0x46U && header[1] == 0x53U && header[2] == 0x48U && header[3] == 0x41U) {
        xx_nintendo_bfsha reader;
        bool valid;
        xx_nintendo_bfsha_init(&reader, dev, 0);
        valid = xx_nintendo_bfsha_check_is_valid(&reader.format, NULL);
        xx_nintendo_bfsha_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BFSHA;
    }
    if (total_size >= 32 && header_size >= 4U && header[0] == 0x43U && header[1] == 0x52U && header[2] == 0x49U && header[3] == 0x44U) {
        xx_cri_usm reader;
        bool valid;
        xx_cri_usm_init(&reader, dev, 0);
        valid = xx_cri_usm_check_is_valid(&reader.format, NULL);
        xx_cri_usm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CRI_USM;
    }
    if (total_size >= 32 && header_size >= 4U && header[0] == 0x40U && header[1] == 0x55U && header[2] == 0x54U && header[3] == 0x46U) {
        xx_cri_utf reader;
        bool valid;
        xx_cri_utf_init(&reader, dev, 0);
        valid = xx_cri_utf_check_is_valid(&reader.format, NULL);
        xx_cri_utf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CRI_UTF;
    }
    if (total_size >= 124 && header_size >= 16U && header[0] == 0x49U && header[1] == 0x4eU && header[2] == 0x54U && header[3] == 0x45U && header[4] == 0x52U && header[5] == 0x51U && header[6] == 0x55U && header[7] == 0x41U && header[8] == 0x4bU && header[9] == 0x45U && header[10] == 0x4dU && header[11] == 0x4fU && header[12] == 0x44U && header[13] == 0x45U && header[14] == 0x4cU && header[15] == 0x00U) {
        xx_idtech_iqm reader;
        bool valid;
        xx_idtech_iqm_init(&reader, dev, 0);
        valid = xx_idtech_iqm_check_is_valid(&reader.format, NULL);
        xx_idtech_iqm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IDTECH_IQM;
    }
    if (total_size >= 32 && header_size >= 8U && header[0] == 0x41U && header[1] == 0x43U && header[2] == 0x54U && header[3] == 0x52U && header[4] == 0x48U && header[5] == 0x45U && header[6] == 0x41U && header[7] == 0x44U) {
        xx_unreal_psk reader;
        bool valid;
        xx_unreal_psk_init(&reader, dev, 0);
        valid = xx_unreal_psk_check_is_valid(&reader.format, NULL);
        xx_unreal_psk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UNREAL_PSK;
    }
    if (total_size >= 32 && header_size >= 8U && header[0] == 0x41U && header[1] == 0x4eU && header[2] == 0x49U && header[3] == 0x4dU && header[4] == 0x48U && header[5] == 0x45U && header[6] == 0x41U && header[7] == 0x44U) {
        xx_unreal_psa reader;
        bool valid;
        xx_unreal_psa_init(&reader, dev, 0);
        valid = xx_unreal_psa_check_is_valid(&reader.format, NULL);
        xx_unreal_psa_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UNREAL_PSA;
    }
    if (total_size >= 16 && header_size >= 2U && header[0] == 0x18U && header[1] == 0x00U) {
        xx_torque_dts reader;
        bool valid;
        xx_torque_dts_init(&reader, dev, 0);
        valid = xx_torque_dts_check_is_valid(&reader.format, NULL);
        xx_torque_dts_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TORQUE_DTS;
    }
    if (total_size >= 20 && header_size >= 4U && header[0] == 0x56U && header[1] == 0x4fU && header[2] == 0x58U && header[3] == 0x20U) {
        xx_magicavoxel_vox reader;
        bool valid;
        xx_magicavoxel_vox_init(&reader, dev, 0);
        valid = xx_magicavoxel_vox_check_is_valid(&reader.format, NULL);
        xx_magicavoxel_vox_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MAGICAVOXEL_VOX;
    }
    if (header_size>=24 && (!xx_rt_memcmp(header,".snd",4) ||
                            !xx_rt_memcmp(header,"dns.",4) ||
                            !xx_rt_memcmp(header,"\0ds.",4) ||
                            !xx_rt_memcmp(header,".sd\0",4))) {
        xx_audio_au reader;
        bool valid;
        xx_audio_au_init(&reader, dev, 0);
        valid = xx_audio_au_check_is_valid(&reader.format, NULL);
        xx_audio_au_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUDIO_AU;
    }
    if (header_size>=26 && !xx_rt_memcmp(header,"Creative Voice File\x1a",20)) {
        xx_creative_voc reader;
        bool valid;
        xx_creative_voc_init(&reader, dev, 0);
        valid = xx_creative_voc_check_is_valid(&reader.format, NULL);
        xx_creative_voc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CREATIVE_VOC;
    }
    if (header_size>=80 && !xx_rt_memcmp(header,"Extended Module: ",17) && header[37]==0x1a && header[58]==4 && header[59]==1) {
        xx_tracker_xm reader;
        bool valid;
        xx_tracker_xm_init(&reader, dev, 0);
        valid = xx_tracker_xm_check_is_valid(&reader.format, NULL);
        xx_tracker_xm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_XM;
    }
    if (header_size>=96 && header[28]==0x1a && header[29]==16 && !xx_rt_memcmp(header+44,"SCRM",4)) {
        xx_tracker_s3m reader;
        bool valid;
        xx_tracker_s3m_init(&reader, dev, 0);
        valid = xx_tracker_s3m_check_is_valid(&reader.format, NULL);
        xx_tracker_s3m_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_S3M;
    }
    if (header_size>=192 && !xx_rt_memcmp(header,"IMPM",4)) {
        xx_tracker_it reader;
        bool valid;
        xx_tracker_it_init(&reader, dev, 0);
        valid = xx_tracker_it_check_is_valid(&reader.format, NULL);
        xx_tracker_it_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_IT;
    }
    if (header_size>=66 && !xx_rt_memcmp(header,"MTM\x10",4)) {
        xx_tracker_mtm reader;
        bool valid;
        xx_tracker_mtm_init(&reader, dev, 0);
        valid = xx_tracker_mtm_check_is_valid(&reader.format, NULL);
        xx_tracker_mtm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_MTM;
    }
    if (header_size>=48 && !xx_rt_memcmp(header+20,"!Scream!",8) && header[28]==0x1a && header[29]==2 && header[30]==2 && header[31]==21) {
        xx_tracker_stm reader;
        bool valid;
        xx_tracker_stm_init(&reader, dev, 0);
        valid = xx_tracker_stm_check_is_valid(&reader.format, NULL);
        xx_tracker_stm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_STM;
    }
    if (header_size>=497 && ((header[0]==105 && header[1]==102) || (header[0]==74 && header[1]==78)) && header[110]<=64 && header[111]>0 && header[111]<=128) {
        xx_tracker_669 reader;
        bool valid;
        xx_tracker_669_init(&reader, dev, 0);
        valid = xx_tracker_669_check_is_valid(&reader.format, NULL);
        xx_tracker_669_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_669;
    }
    if (header_size>=48 && !xx_rt_memcmp(header,"MAS_UTrack_V004",15)) {
        xx_tracker_ult reader;
        bool valid;
        xx_tracker_ult_init(&reader, dev, 0);
        valid = xx_tracker_ult_check_is_valid(&reader.format, NULL);
        xx_tracker_ult_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_ULT;
    }
    if (header_size>=16 && !xx_rt_memcmp(header,"OKTASONG",8)) {
        xx_tracker_okt reader;
        bool valid;
        xx_tracker_okt_init(&reader, dev, 0);
        valid = xx_tracker_okt_check_is_valid(&reader.format, NULL);
        xx_tracker_okt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_OKT;
    }
    if (((header_size >= 12 && header[4] == 110 && header[5] == 43 && header[6] == 50 && header[7] == 0 && header[8] == 13 && header[9] == 10 && header[10] == 26 && header[11] == 10))) {
        xx_nifti2 reader;
        bool valid;
        xx_nifti2_init(&reader, dev, 0);
        valid = xx_nifti2_check_is_valid(&reader.format, NULL);
        xx_nifti2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NIFTI2;
    }
    if (((header_size >= 4 && header[0] == 76 && header[1] == 65 && header[2] == 83 && header[3] == 70))) {
        xx_lidar_las reader;
        bool valid;
        xx_lidar_las_init(&reader, dev, 0);
        valid = xx_lidar_las_check_is_valid(&reader.format, NULL);
        xx_lidar_las_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LIDAR_LAS;
    }
    if (((header_size >= 4 && header[0] == 0 && header[1] == 0 && header[2] == 39 && header[3] == 10))) {
        xx_esri_shp reader;
        bool valid;
        xx_esri_shp_init(&reader, dev, 0);
        valid = xx_esri_shp_check_is_valid(&reader.format, NULL);
        xx_esri_shp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ESRI_SHP;
    }
    if (((header_size >= 4 && header[0] == 112 && header[1] == 108 && header[2] == 121 && header[3] == 10) || (header_size >= 5 && header[0] == 112 && header[1] == 108 && header[2] == 121 && header[3] == 13 && header[4] == 10))) {
        xx_polygon_ply reader;
        bool valid;
        xx_polygon_ply_init(&reader, dev, 0);
        valid = xx_polygon_ply_check_is_valid(&reader.format, NULL);
        xx_polygon_ply_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_POLYGON_PLY;
    }
    if (((header_size >= 6 && header[0] == 35 && header[1] == 32 && header[2] == 46 && header[3] == 80 && header[4] == 67 && header[5] == 68) || (header_size >= 8 && header[0] == 86 && header[1] == 69 && header[2] == 82 && header[3] == 83 && header[4] == 73 && header[5] == 79 && header[6] == 78 && header[7] == 32))) {
        xx_pointcloud_pcd reader;
        bool valid;
        xx_pointcloud_pcd_init(&reader, dev, 0);
        valid = xx_pointcloud_pcd_check_is_valid(&reader.format, NULL);
        xx_pointcloud_pcd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_POINTCLOUD_PCD;
    }
    if (((header_size >= 8 && header[0] == 255 && header[1] == 66 && header[2] == 73 && header[3] == 79 && header[4] == 83 && header[5] == 69 && header[6] == 77 && header[7] == 73))) {
        xx_biomedical_bdf reader;
        bool valid;
        xx_biomedical_bdf_init(&reader, dev, 0);
        valid = xx_biomedical_bdf_check_is_valid(&reader.format, NULL);
        xx_biomedical_bdf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BIOMEDICAL_BDF;
    }
    if (((header_size >= 4 && header[0] == 70 && header[1] == 79 && header[2] == 82 && header[3] == 49))) {
        xx_erlang_beam reader;
        bool valid;
        xx_erlang_beam_init(&reader, dev, 0);
        valid = xx_erlang_beam_check_is_valid(&reader.format, NULL);
        xx_erlang_beam_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ERLANG_BEAM;
    }
    if (((header_size >= 4 && header[0] == 74 && header[1] == 77 && header[2] == 1 && header[3] == 0))) {
        xx_java_jmod reader;
        bool valid;
        xx_java_jmod_init(&reader, dev, 0);
        valid = xx_java_jmod_check_is_valid(&reader.format, NULL);
        xx_java_jmod_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_JAVA_JMOD;
    }
}
