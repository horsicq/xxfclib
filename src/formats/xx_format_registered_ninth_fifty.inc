/* Reviewed bounded probes for the ninth group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 4 && (!xx_rt_memcmp(header, "\x43\x15\x41\x64", 4))) {
        xx_vice_x64 reader;
        bool valid;
        xx_vice_x64_init(&reader, dev, 0);
        valid = xx_vice_x64_check_is_valid(&reader.format, NULL);
        xx_vice_x64_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VICE_X64;
    }
    if (header_size >= 19 && (!xx_rt_memcmp(header, "\x56\x49\x43\x45\x20\x53\x6e\x61\x70\x73\x68\x6f\x74\x20\x46\x69\x6c\x65\x1a", 19))) {
        xx_vice_snapshot reader;
        bool valid;
        xx_vice_snapshot_init(&reader, dev, 0);
        valid = xx_vice_snapshot_check_is_valid(&reader.format, NULL);
        xx_vice_snapshot_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VICE_SNAPSHOT;
    }
    if (header_size >= 8 && (!xx_rt_memcmp(header, "\x47\x43\x52\x2d\x31\x35\x34\x31", 8))) {
        xx_commodore_g64 reader;
        bool valid;
        xx_commodore_g64_init(&reader, dev, 0);
        valid = xx_commodore_g64_check_is_valid(&reader.format, NULL);
        xx_commodore_g64_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_COMMODORE_G64;
    }
    if (header_size >= 8 && (!xx_rt_memcmp(header, "\x50\x36\x34\x2d\x31\x35\x34\x31", 8))) {
        xx_commodore_p64 reader;
        bool valid;
        xx_commodore_p64_init(&reader, dev, 0);
        valid = xx_commodore_p64_check_is_valid(&reader.format, NULL);
        xx_commodore_p64_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_COMMODORE_P64;
    }
    if (header_size >= 12 && (!xx_rt_memcmp(header, "\x43\x36\x34\x2d\x54\x41\x50\x45\x2d\x52\x41\x57", 12))) {
        xx_commodore_tap reader;
        bool valid;
        xx_commodore_tap_init(&reader, dev, 0);
        valid = xx_commodore_tap_check_is_valid(&reader.format, NULL);
        xx_commodore_tap_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_COMMODORE_TAP;
    }
    if (header_size >= 8 && (!xx_rt_memcmp(header, "\x5a\x58\x54\x61\x70\x65\x21\x1a", 8))) {
        xx_zx_spectrum_tzx reader;
        bool valid;
        xx_zx_spectrum_tzx_init(&reader, dev, 0);
        valid = xx_zx_spectrum_tzx_check_is_valid(&reader.format, NULL);
        xx_zx_spectrum_tzx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ZX_SPECTRUM_TZX;
    }
    if (header_size >= 4 && (!xx_rt_memcmp(header, "\x5a\x58\x53\x54", 4))) {
        xx_zx_spectrum_szx reader;
        bool valid;
        xx_zx_spectrum_szx_init(&reader, dev, 0);
        valid = xx_zx_spectrum_szx_check_is_valid(&reader.format, NULL);
        xx_zx_spectrum_szx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ZX_SPECTRUM_SZX;
    }
    if (header_size >= 34 && (!xx_rt_memcmp(header, "\x4d\x56\x20\x2d\x20\x43\x50\x43\x45\x4d\x55\x20\x44\x69\x73\x6b\x2d\x46\x69\x6c\x65\x0d\x0a\x44\x69\x73\x6b\x2d\x49\x6e\x66\x6f\x0d\x0a", 34) || !xx_rt_memcmp(header, "\x45\x58\x54\x45\x4e\x44\x45\x44\x20\x43\x50\x43\x20\x44\x53\x4b\x20\x46\x69\x6c\x65\x0d\x0a\x44\x69\x73\x6b\x2d\x49\x6e\x66\x6f\x0d\x0a", 34))) {
        xx_amstrad_cpc_dsk reader;
        bool valid;
        xx_amstrad_cpc_dsk_init(&reader, dev, 0);
        valid = xx_amstrad_cpc_dsk_check_is_valid(&reader.format, NULL);
        xx_amstrad_cpc_dsk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AMSTRAD_CPC_DSK;
    }
    if (header_size >= 2 && (!xx_rt_memcmp(header, "\x0e\x0f", 2))) {
        xx_atari_st_msa reader;
        bool valid;
        xx_atari_st_msa_init(&reader, dev, 0);
        valid = xx_atari_st_msa_check_is_valid(&reader.format, NULL);
        xx_atari_st_msa_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ATARI_ST_MSA;
    }
    if (header_size >= 3 && (!xx_rt_memcmp(header, "\x53\x43\x50", 3))) {
        xx_supercard_scp reader;
        bool valid;
        xx_supercard_scp_init(&reader, dev, 0);
        valid = xx_supercard_scp_check_is_valid(&reader.format, NULL);
        xx_supercard_scp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SUPERCARD_SCP;
    }
    if (header_size >= 8 && (!xx_rt_memcmp(header, "\x57\x4f\x5a\x31\xff\x0a\x0d\x0a", 8) || !xx_rt_memcmp(header, "\x57\x4f\x5a\x32\xff\x0a\x0d\x0a", 8))) {
        xx_apple_woz reader;
        bool valid;
        xx_apple_woz_init(&reader, dev, 0);
        valid = xx_apple_woz_check_is_valid(&reader.format, NULL);
        xx_apple_woz_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_APPLE_WOZ;
    }
    if (header_size >= 5 && (!xx_rt_memcmp(header, "\x4e\x45\x53\x4d\x1a", 5))) {
        xx_nintendo_nsf reader;
        bool valid;
        xx_nintendo_nsf_init(&reader, dev, 0);
        valid = xx_nintendo_nsf_check_is_valid(&reader.format, NULL);
        xx_nintendo_nsf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_NSF;
    }
    if (header_size >= 4 && (!xx_rt_memcmp(header, "\x56\x67\x6d\x20", 4))) {
        xx_vgm_log reader;
        bool valid;
        xx_vgm_log_init(&reader, dev, 0);
        valid = xx_vgm_log_check_is_valid(&reader.format, NULL);
        xx_vgm_log_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VGM_LOG;
    }
    if (header_size >= 4 && (!xx_rt_memcmp(header, "\x50\x53\x49\x44", 4) || !xx_rt_memcmp(header, "\x52\x53\x49\x44", 4))) {
        xx_psid_sid reader;
        bool valid;
        xx_psid_sid_init(&reader, dev, 0);
        valid = xx_psid_sid_check_is_valid(&reader.format, NULL);
        xx_psid_sid_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PSID_SID;
    }
    if (header_size >= 4 && (!xx_rt_memcmp(header, "\x48\x45\x53\x4d", 4))) {
        xx_hes_sound reader;
        bool valid;
        xx_hes_sound_init(&reader, dev, 0);
        valid = xx_hes_sound_check_is_valid(&reader.format, NULL);
        xx_hes_sound_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_HES_SOUND;
    }
    if (total_size >= 128 && header_size >= 2U && ((header[0] == 0x0bU && header[1] == 0x77U))) {
        xx_audio_dolby_ac3 reader;
        bool valid;
        xx_audio_dolby_ac3_init(&reader, dev, 0);
        valid = xx_audio_dolby_ac3_check_is_valid(&reader.format, NULL);
        xx_audio_dolby_ac3_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUDIO_DOLBY_AC3;
    }
    if (total_size >= 21 && header_size >= 3U && ((header[0] == 0x49U && header[1] == 0x44U && header[2] == 0x33U) || (header[0] == 0xffU && header[1] == 0xfbU))) {
        xx_audio_mpeg_mp3 reader;
        bool valid;
        xx_audio_mpeg_mp3_init(&reader, dev, 0);
        valid = xx_audio_mpeg_mp3_check_is_valid(&reader.format, NULL);
        xx_audio_mpeg_mp3_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUDIO_MPEG_MP3;
    }
    if (total_size >= 8 && header_size >= 2U && ((header[0] == 0xffU && header[1] == 0xf1U))) {
        xx_audio_aac_adts reader;
        bool valid;
        xx_audio_aac_adts_init(&reader, dev, 0);
        valid = xx_audio_aac_adts_check_is_valid(&reader.format, NULL);
        xx_audio_aac_adts_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUDIO_AAC_ADTS;
    }
    if (total_size >= 84 && header_size >= 4U && ((header[0] == 0x4dU && header[1] == 0x41U && header[2] == 0x43U && header[3] == 0x20U))) {
        xx_audio_monkeys_ape reader;
        bool valid;
        xx_audio_monkeys_ape_init(&reader, dev, 0);
        valid = xx_audio_monkeys_ape_check_is_valid(&reader.format, NULL);
        xx_audio_monkeys_ape_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUDIO_MONKEYS_APE;
    }
    if (total_size >= 564 && header_size >= 1U && ((header[0] == 0x47U))) {
        xx_mpeg_transport_stream reader;
        bool valid;
        xx_mpeg_transport_stream_init(&reader, dev, 0);
        valid = xx_mpeg_transport_stream_check_is_valid(&reader.format, NULL);
        xx_mpeg_transport_stream_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MPEG_TRANSPORT_STREAM;
    }
    if (total_size >= 18 && header_size >= 4U && ((header[0] == 0x00U && header[1] == 0x00U && header[2] == 0x01U && header[3] == 0xbaU))) {
        xx_mpeg_program_stream reader;
        bool valid;
        xx_mpeg_program_stream_init(&reader, dev, 0);
        valid = xx_mpeg_program_stream_check_is_valid(&reader.format, NULL);
        xx_mpeg_program_stream_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MPEG_PROGRAM_STREAM;
    }
    if (total_size >= 50 && header_size >= 4U && ((header[0] == 0x2eU && header[1] == 0x52U && header[2] == 0x4dU && header[3] == 0x46U))) {
        xx_realmedia_rm reader;
        bool valid;
        xx_realmedia_rm_init(&reader, dev, 0);
        valid = xx_realmedia_rm_check_is_valid(&reader.format, NULL);
        xx_realmedia_rm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_REALMEDIA_RM;
    }
    if (total_size >= 24 && header_size >= 6U && ((header[0] == 0x84U && header[1] == 0x10U && header[2] == 0xffU && header[3] == 0xffU && header[4] == 0xffU && header[5] == 0xffU))) {
        xx_idtech_roq reader;
        bool valid;
        xx_idtech_roq_init(&reader, dev, 0);
        valid = xx_idtech_roq_check_is_valid(&reader.format, NULL);
        xx_idtech_roq_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IDTECH_ROQ;
    }
    if (total_size >= 48 && header_size >= 3U && ((header[0] == 0x42U && header[1] == 0x49U && header[2] == 0x4bU))) {
        xx_rad_bink reader;
        bool valid;
        xx_rad_bink_init(&reader, dev, 0);
        valid = xx_rad_bink_check_is_valid(&reader.format, NULL);
        xx_rad_bink_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RAD_BINK;
    }
    if (total_size >= 109 && header_size >= 4U && ((header[0] == 0x53U && header[1] == 0x4dU && header[2] == 0x4bU && header[3] == 0x32U) || (header[0] == 0x53U && header[1] == 0x4dU && header[2] == 0x4bU && header[3] == 0x34U))) {
        xx_rad_smacker reader;
        bool valid;
        xx_rad_smacker_init(&reader, dev, 0);
        valid = xx_rad_smacker_check_is_valid(&reader.format, NULL);
        xx_rad_smacker_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RAD_SMACKER;
    }
    if (total_size >= 30 && header_size >= 19U && ((header[0] == 0x49U && header[1] == 0x6eU && header[2] == 0x74U && header[3] == 0x65U && header[4] == 0x72U && header[5] == 0x70U && header[6] == 0x6cU && header[7] == 0x61U && header[8] == 0x79U && header[9] == 0x20U && header[10] == 0x4dU && header[11] == 0x56U && header[12] == 0x45U && header[13] == 0x20U && header[14] == 0x46U && header[15] == 0x69U && header[16] == 0x6cU && header[17] == 0x65U && header[18] == 0x1aU))) {
        xx_interplay_mve reader;
        bool valid;
        xx_interplay_mve_init(&reader, dev, 0);
        valid = xx_interplay_mve_check_is_valid(&reader.format, NULL);
        xx_interplay_mve_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INTERPLAY_MVE;
    }
    if (total_size >= 62 && header_size >= 12U && ((header[8] == 0x57U && header[9] == 0x56U && header[10] == 0x51U && header[11] == 0x41U))) {
        xx_westwood_vqa reader;
        bool valid;
        xx_westwood_vqa_init(&reader, dev, 0);
        valid = xx_westwood_vqa_check_is_valid(&reader.format, NULL);
        xx_westwood_vqa_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WESTWOOD_VQA;
    }
    if (total_size >= 150 && header_size >= 6U && ((header[4] == 0x11U && header[5] == 0xafU) || (header[4] == 0x12U && header[5] == 0xafU))) {
        xx_autodesk_flic reader;
        bool valid;
        xx_autodesk_flic_init(&reader, dev, 0);
        valid = xx_autodesk_flic_check_is_valid(&reader.format, NULL);
        xx_autodesk_flic_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUTODESK_FLIC;
    }
    if (total_size >= 10 && header_size >= 10U && ((header[0] == 0x4dU && header[1] == 0x44U && header[2] == 0x35U && header[3] == 0x56U && header[4] == 0x65U && header[5] == 0x72U && header[6] == 0x73U && header[7] == 0x69U && header[8] == 0x6fU && header[9] == 0x6eU))) {
        xx_idtech_md5anim reader;
        bool valid;
        xx_idtech_md5anim_init(&reader, dev, 0);
        valid = xx_idtech_md5anim_check_is_valid(&reader.format, NULL);
        xx_idtech_md5anim_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IDTECH_MD5ANIM;
    }
    if (((header_size >= 12 && header[8] == 46 && header[9] == 70 && header[10] == 73 && header[11] == 84))) {
        xx_garmin_fit reader;
        bool valid;
        xx_garmin_fit_init(&reader, dev, 0);
        valid = xx_garmin_fit_check_is_valid(&reader.format, NULL);
        xx_garmin_fit_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GARMIN_FIT;
    }
    if (((header_size >= 13 && header[0] == 35 && header[1] == 82 && header[2] == 79 && header[3] == 83 && header[4] == 66 && header[5] == 65 && header[6] == 71 && header[7] == 32 && header[8] == 86 && header[9] == 50 && header[10] == 46 && header[11] == 48 && header[12] == 10))) {
        xx_rosbag1 reader;
        bool valid;
        xx_rosbag1_init(&reader, dev, 0);
        valid = xx_rosbag1_check_is_valid(&reader.format, NULL);
        xx_rosbag1_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ROSBAG1;
    }
    if (((header_size >= 8 && header[0] == 137 && header[1] == 77 && header[2] == 67 && header[3] == 65 && header[4] == 80 && header[5] == 48 && header[6] == 13 && header[7] == 10))) {
        xx_mcap reader;
        bool valid;
        xx_mcap_init(&reader, dev, 0);
        valid = xx_mcap_check_is_valid(&reader.format, NULL);
        xx_mcap_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MCAP;
    }
    if (((header_size >= 2 && header[0] == 85 && header[1] == 58) || (header_size >= 2 && header[0] == 58 && header[1] == 85))) {
        xx_seismic_seg2 reader;
        bool valid;
        xx_seismic_seg2_init(&reader, dev, 0);
        valid = xx_seismic_seg2_check_is_valid(&reader.format, NULL);
        xx_seismic_seg2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SEISMIC_SEG2;
    }
    if (((header_size >= 4 && header[0] == 26 && header[1] == 65 && header[2] == 39 && header[3] == 67) || (header_size >= 4 && header[0] == 67 && header[1] == 39 && header[2] == 65 && header[3] == 26))) {
        xx_ucsc_twobit reader;
        bool valid;
        xx_ucsc_twobit_init(&reader, dev, 0);
        valid = xx_ucsc_twobit_check_is_valid(&reader.format, NULL);
        xx_ucsc_twobit_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UCSC_TWOBIT;
    }
    if (((header_size >= 20 && header[16] == 98 && header[17] == 103 && header[18] == 101 && header[19] == 110))) {
        xx_genomics_bgen reader;
        bool valid;
        xx_genomics_bgen_init(&reader, dev, 0);
        valid = xx_genomics_bgen_check_is_valid(&reader.format, NULL);
        xx_genomics_bgen_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENOMICS_BGEN;
    }
    if (((header_size >= 7 && header[0] == 104 && header[1] == 101 && header[2] == 97 && header[3] == 100 && header[4] == 101 && header[5] == 114 && header[6] == 46))) {
        xx_openephys_continuous reader;
        bool valid;
        xx_openephys_continuous_init(&reader, dev, 0);
        valid = xx_openephys_continuous_check_is_valid(&reader.format, NULL);
        xx_openephys_continuous_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OPENEPHYS_CONTINUOUS;
    }
    if (((header_size >= 2 && header[0] == 5 && header[1] == 0) || (header_size >= 2 && header[0] == 0 && header[1] == 5))) {
        xx_igor_ibw reader;
        bool valid;
        xx_igor_ibw_init(&reader, dev, 0);
        valid = xx_igor_ibw_check_is_valid(&reader.format, NULL);
        xx_igor_ibw_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IGOR_IBW;
    }
    if (((header_size >= 4100 && header[4098] == 85 && header[4099] == 85))) {
        xx_princeton_spe reader;
        bool valid;
        xx_princeton_spe_init(&reader, dev, 0);
        valid = xx_princeton_spe_check_is_valid(&reader.format, NULL);
        xx_princeton_spe_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PRINCETON_SPE;
    }
    if (((header_size >= 4 && header[0] == 71 && header[1] == 82 && header[2] == 73 && header[3] == 66))) {
        xx_wmo_grib reader;
        bool valid;
        xx_wmo_grib_init(&reader, dev, 0);
        valid = xx_wmo_grib_check_is_valid(&reader.format, NULL);
        xx_wmo_grib_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WMO_GRIB;
    }
    if (((header_size >= 4 && header[0] == 66 && header[1] == 85 && header[2] == 70 && header[3] == 82))) {
        xx_wmo_bufr reader;
        bool valid;
        xx_wmo_bufr_init(&reader, dev, 0);
        valid = xx_wmo_bufr_check_is_valid(&reader.format, NULL);
        xx_wmo_bufr_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WMO_BUFR;
    }
    if (((header_size >= 22 && header[0] == 65 && header[1] == 117 && header[2] == 116 && header[3] == 111 && header[4] == 67 && header[5] == 65 && header[6] == 68 && header[7] == 32 && header[8] == 66 && header[9] == 105 && header[10] == 110 && header[11] == 97 && header[12] == 114 && header[13] == 121 && header[14] == 32 && header[15] == 68 && header[16] == 88 && header[17] == 70 && header[18] == 13 && header[19] == 10 && header[20] == 26 && header[21] == 0))) {
        xx_autocad_dxf reader;
        bool valid;
        xx_autocad_dxf_init(&reader, dev, 0);
        valid = xx_autocad_dxf_check_is_valid(&reader.format, NULL);
        xx_autocad_dxf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUTOCAD_DXF;
    }
    if (((header_size >= 8 && header[0] == 78 && header[1] == 69 && header[2] == 85 && header[3] == 82 && header[4] == 65 && header[5] == 76 && header[6] == 67 && header[7] == 68))) {
        xx_blackrock_nsx reader;
        bool valid;
        xx_blackrock_nsx_init(&reader, dev, 0);
        valid = xx_blackrock_nsx_check_is_valid(&reader.format, NULL);
        xx_blackrock_nsx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BLACKROCK_NSX;
    }
    if (((header_size >= 8 && header[0] == 78 && header[1] == 69 && header[2] == 85 && header[3] == 82 && header[4] == 65 && header[5] == 76 && header[6] == 69 && header[7] == 86))) {
        xx_blackrock_nev reader;
        bool valid;
        xx_blackrock_nev_init(&reader, dev, 0);
        valid = xx_blackrock_nev_check_is_valid(&reader.format, NULL);
        xx_blackrock_nev_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BLACKROCK_NEV;
    }
    if (((header_size >= 8 && header[0] == 87 && header[1] == 65 && header[2] == 86 && header[3] == 69 && header[4] == 68 && header[5] == 69 && header[6] == 83 && header[7] == 67))) {
        xx_lecroy_trc reader;
        bool valid;
        xx_lecroy_trc_init(&reader, dev, 0);
        valid = xx_lecroy_trc_check_is_valid(&reader.format, NULL);
        xx_lecroy_trc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LECROY_TRC;
    }
    if (((header_size >= 8 && header[0] == 58 && header[1] == 87 && header[2] == 70 && header[3] == 77 && header[4] == 80 && header[5] == 82 && header[6] == 69 && header[7] == 58))) {
        xx_tektronix_isf reader;
        bool valid;
        xx_tektronix_isf_init(&reader, dev, 0);
        valid = xx_tektronix_isf_check_is_valid(&reader.format, NULL);
        xx_tektronix_isf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TEKTRONIX_ISF;
    }
    if (((header_size >= 4 && header[0] == 83 && header[1] == 68 && header[2] == 73 && header[3] == 70))) {
        xx_ircam_sdif reader;
        bool valid;
        xx_ircam_sdif_init(&reader, dev, 0);
        valid = xx_ircam_sdif_check_is_valid(&reader.format, NULL);
        xx_ircam_sdif_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IRCAM_SDIF;
    }
}
