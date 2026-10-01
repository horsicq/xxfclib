/* Reviewed bounded probes for the eighth group of fifty readers. */
{
    uint8_t header[1084] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 14 && xx_rt_memcmp(header,"Liquid Module:",14)==0) {
        xx_tracker_liquid reader;
        bool valid;
        xx_tracker_liquid_init(&reader, dev, 0);
        valid = xx_tracker_liquid_check_is_valid(&reader.format, NULL);
        xx_tracker_liquid_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_LIQUID;
    }
    if (header_size >= 4 && xx_rt_memcmp(header+0,"\x44\x44\x4d\x46",4)==0) {
        xx_tracker_dmf reader;
        bool valid;
        xx_tracker_dmf_init(&reader, dev, 0);
        valid = xx_tracker_dmf_check_is_valid(&reader.format, NULL);
        xx_tracker_dmf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_DMF;
    }
    if (header_size >= 48 && xx_rt_memcmp(header+44,"\x50\x54\x4d\x46",4)==0) {
        xx_tracker_ptm reader;
        bool valid;
        xx_tracker_ptm_init(&reader, dev, 0);
        valid = xx_tracker_ptm_check_is_valid(&reader.format, NULL);
        xx_tracker_ptm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_PTM;
    }
    if (header_size >= 7 && xx_rt_memcmp(header+0,"\x45\x78\x74\x72\x65\x6d\x65",7)==0) {
        xx_tracker_ams reader;
        bool valid;
        xx_tracker_ams_init(&reader, dev, 0);
        valid = xx_tracker_ams_check_is_valid(&reader.format, NULL);
        xx_tracker_ams_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_AMS;
    }
    if (header_size >= 20 && xx_rt_memcmp(header+0,"\x44\x49\x47\x49\x20\x42\x6f\x6f\x73\x74\x65\x72\x20\x6d\x6f\x64\x75\x6c\x65\x00",20)==0) {
        xx_tracker_digi reader;
        bool valid;
        xx_tracker_digi_init(&reader, dev, 0);
        valid = xx_tracker_digi_check_is_valid(&reader.format, NULL);
        xx_tracker_digi_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_DIGI;
    }
    if (header_size >= 12 && xx_rt_memcmp(header+0,"\x46\x4f\x52\x4d",4)==0 && xx_rt_memcmp(header+8,"\x45\x4d\x4f\x44",4)==0) {
        xx_tracker_emod reader;
        bool valid;
        xx_tracker_emod_init(&reader, dev, 0);
        valid = xx_tracker_emod_check_is_valid(&reader.format, NULL);
        xx_tracker_emod_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_EMOD;
    }
    if (header_size >= 4 && xx_rt_memcmp(header+0,"\x4d\x54\x32\x30",4)==0) {
        xx_tracker_mt2 reader;
        bool valid;
        xx_tracker_mt2_init(&reader, dev, 0);
        valid = xx_tracker_mt2_check_is_valid(&reader.format, NULL);
        xx_tracker_mt2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_MT2;
    }
    if (header_size >= 4 && xx_rt_memcmp(header+0,"\x44\x53\x44\x20",4)==0) {
        xx_audio_dsf reader;
        bool valid;
        xx_audio_dsf_init(&reader, dev, 0);
        valid = xx_audio_dsf_check_is_valid(&reader.format, NULL);
        xx_audio_dsf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUDIO_DSF;
    }
    if (header_size >= 16 && xx_rt_memcmp(header+0,"\x46\x52\x4d\x38",4)==0 && xx_rt_memcmp(header+12,"\x44\x53\x44\x20",4)==0) {
        xx_audio_dff reader;
        bool valid;
        xx_audio_dff_init(&reader, dev, 0);
        valid = xx_audio_dff_check_is_valid(&reader.format, NULL);
        xx_audio_dff_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUDIO_DFF;
    }
    if (header_size >= 16 && xx_rt_memcmp(header+0,"\x72\x69\x66\x66\x2e\x91\xcf\x11\xa5\xd6\x28\xdb\x04\xc1\x00\x00",16)==0) {
        xx_audio_wave64 reader;
        bool valid;
        xx_audio_wave64_init(&reader, dev, 0);
        valid = xx_audio_wave64_check_is_valid(&reader.format, NULL);
        xx_audio_wave64_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUDIO_WAVE64;
    }
    if (header_size >= 2 && xx_rt_memcmp(header+0,"\x80\x00",2)==0) {
        xx_audio_adx reader;
        bool valid;
        xx_audio_adx_init(&reader, dev, 0);
        valid = xx_audio_adx_check_is_valid(&reader.format, NULL);
        xx_audio_adx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUDIO_ADX;
    }
    if (header_size >= 4 && xx_rt_memcmp(header+0,"\x53\x54\x52\x4d",4)==0) {
        xx_audio_ast reader;
        bool valid;
        xx_audio_ast_init(&reader, dev, 0);
        valid = xx_audio_ast_check_is_valid(&reader.format, NULL);
        xx_audio_ast_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUDIO_AST;
    }
    if (header_size >= 4 && xx_rt_memcmp(header+0,"\x48\x43\x41\x00",4)==0) {
        xx_audio_hca reader;
        bool valid;
        xx_audio_hca_init(&reader, dev, 0);
        valid = xx_audio_hca_check_is_valid(&reader.format, NULL);
        xx_audio_hca_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUDIO_HCA;
    }
    if (header_size >= 12 && xx_rt_memcmp(header+0,"\x46\x4f\x52\x4d",4)==0 && xx_rt_memcmp(header+8,"\x38\x53\x56\x58",4)==0) {
        xx_iff_8svx reader;
        bool valid;
        xx_iff_8svx_init(&reader, dev, 0);
        valid = xx_iff_8svx_check_is_valid(&reader.format, NULL);
        xx_iff_8svx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IFF_8SVX;
    }
    if (header_size >= 4 && xx_rt_memcmp(header+0,"\x77\x76\x70\x6b",4)==0) {
        xx_audio_wavpack reader;
        bool valid;
        xx_audio_wavpack_init(&reader, dev, 0);
        valid = xx_audio_wavpack_check_is_valid(&reader.format, NULL);
        xx_audio_wavpack_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUDIO_WAVPACK;
    }
    if (total_size >= 12 && header_size >= 7U && header[0] == 0x42U && header[1] == 0x4cU && header[2] == 0x45U && header[3] == 0x4eU && header[4] == 0x44U && header[5] == 0x45U && header[6] == 0x52U) {
        xx_blender_blend reader;
        bool valid;
        xx_blender_blend_init(&reader, dev, 0);
        valid = xx_blender_blend_check_is_valid(&reader.format, NULL);
        xx_blender_blend_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BLENDER_BLEND;
    }
    if (total_size >= 27 && header_size >= 23U && header[0] == 0x4bU && header[1] == 0x61U && header[2] == 0x79U && header[3] == 0x64U && header[4] == 0x61U && header[5] == 0x72U && header[6] == 0x61U && header[7] == 0x20U && header[8] == 0x46U && header[9] == 0x42U && header[10] == 0x58U && header[11] == 0x20U && header[12] == 0x42U && header[13] == 0x69U && header[14] == 0x6eU && header[15] == 0x61U && header[16] == 0x72U && header[17] == 0x79U && header[18] == 0x20U && header[19] == 0x20U && header[20] == 0x00U && header[21] == 0x1aU && header[22] == 0x00U) {
        xx_autodesk_fbx reader;
        bool valid;
        xx_autodesk_fbx_init(&reader, dev, 0);
        valid = xx_autodesk_fbx_check_is_valid(&reader.format, NULL);
        xx_autodesk_fbx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUTODESK_FBX;
    }
    if (total_size >= 6 && header_size >= 2U && header[0] == 0x4dU && header[1] == 0x4dU) {
        xx_autodesk_3ds reader;
        bool valid;
        xx_autodesk_3ds_init(&reader, dev, 0);
        valid = xx_autodesk_3ds_check_is_valid(&reader.format, NULL);
        xx_autodesk_3ds_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AUTODESK_3DS;
    }
    if (total_size >= 12 && header_size >= 4U && header[0] == 0x46U && header[1] == 0x4fU && header[2] == 0x52U && header[3] == 0x4dU) {
        xx_lightwave_lwo2 reader;
        bool valid;
        xx_lightwave_lwo2_init(&reader, dev, 0);
        valid = xx_lightwave_lwo2_check_is_valid(&reader.format, NULL);
        xx_lightwave_lwo2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LIGHTWAVE_LWO2;
    }
    if (total_size >= 40 && header_size >= 4U && header[0] == 0x00U && header[1] == 0x50U && header[2] == 0x42U && header[3] == 0x50U) {
        xx_sony_psp_pbp reader;
        bool valid;
        xx_sony_psp_pbp_init(&reader, dev, 0);
        valid = xx_sony_psp_pbp_check_is_valid(&reader.format, NULL);
        xx_sony_psp_pbp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SONY_PSP_PBP;
    }
    if (total_size >= 9 && header_size >= 3U && header[0] == 0x46U && header[1] == 0x4cU && header[2] == 0x56U) {
        xx_flash_video_flv reader;
        bool valid;
        xx_flash_video_flv_init(&reader, dev, 0);
        valid = xx_flash_video_flv_check_is_valid(&reader.format, NULL);
        xx_flash_video_flv_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FLASH_VIDEO_FLV;
    }
    if (total_size >= 4096 && header_size >= 4U && header[0] == 0x80U && header[1] == 0x37U && header[2] == 0x12U && header[3] == 0x40U) {
        xx_nintendo_n64_rom reader;
        bool valid;
        xx_nintendo_n64_rom_init(&reader, dev, 0);
        valid = xx_nintendo_n64_rom_check_is_valid(&reader.format, NULL);
        xx_nintendo_n64_rom_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_N64_ROM;
    }
    if (total_size >= 336 && header_size >= 268U && header[260] == 0xceU && header[261] == 0xedU && header[262] == 0x66U && header[263] == 0x66U && header[264] == 0xccU && header[265] == 0x0dU && header[266] == 0x00U && header[267] == 0x0bU) {
        xx_nintendo_gb_rom reader;
        bool valid;
        xx_nintendo_gb_rom_init(&reader, dev, 0);
        valid = xx_nintendo_gb_rom_check_is_valid(&reader.format, NULL);
        xx_nintendo_gb_rom_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_GB_ROM;
    }
    if (total_size >= 192 && header_size >= 12U && header[4] == 0x24U && header[5] == 0xffU && header[6] == 0xaeU && header[7] == 0x51U && header[8] == 0x69U && header[9] == 0x9aU && header[10] == 0xa2U && header[11] == 0x21U) {
        xx_nintendo_gba_rom reader;
        bool valid;
        xx_nintendo_gba_rom_init(&reader, dev, 0);
        valid = xx_nintendo_gba_rom_check_is_valid(&reader.format, NULL);
        xx_nintendo_gba_rom_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_GBA_ROM;
    }
    if (total_size >= 512 && header_size >= 260U && header[256] == 0x53U && header[257] == 0x45U && header[258] == 0x47U && header[259] == 0x41U) {
        xx_sega_megadrive_rom reader;
        bool valid;
        xx_sega_megadrive_rom_init(&reader, dev, 0);
        valid = xx_sega_megadrive_rom_check_is_valid(&reader.format, NULL);
        xx_sega_megadrive_rom_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SEGA_MEGADRIVE_ROM;
    }
    if (total_size >= 52 && header_size >= 12U && header[0] == 0x53U && header[1] == 0x70U && header[2] == 0x72U && header[3] == 0x69U && header[4] == 0x6eU && header[5] == 0x67U && header[6] == 0x20U && header[7] == 0x75U && header[8] == 0x6eU && header[9] == 0x69U && header[10] == 0x74U && header[11] == 0x00U) {
        xx_spring_s3o reader;
        bool valid;
        xx_spring_s3o_init(&reader, dev, 0);
        valid = xx_spring_s3o_check_is_valid(&reader.format, NULL);
        xx_spring_s3o_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SPRING_S3O;
    }
    if (total_size >= 10 && header_size >= 3U && header[0] == 0x58U && header[1] == 0x4eU && header[2] == 0x42U) {
        xx_xna_xnb reader;
        bool valid;
        xx_xna_xnb_init(&reader, dev, 0);
        valid = xx_xna_xnb_check_is_valid(&reader.format, NULL);
        xx_xna_xnb_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_XNA_XNB;
    }
    if (total_size >= 12 && header_size >= 5U && header[0] == 0x1bU && header[1] == 0x4cU && header[2] == 0x75U && header[3] == 0x61U && header[4] == 0x51U) {
        xx_lua_bytecode51 reader;
        bool valid;
        xx_lua_bytecode51_init(&reader, dev, 0);
        valid = xx_lua_bytecode51_check_is_valid(&reader.format, NULL);
        xx_lua_bytecode51_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LUA_BYTECODE51;
    }
    if (total_size >= 10 && header_size >= 10U && header[0] == 0x4dU && header[1] == 0x44U && header[2] == 0x35U && header[3] == 0x56U && header[4] == 0x65U && header[5] == 0x72U && header[6] == 0x73U && header[7] == 0x69U && header[8] == 0x6fU && header[9] == 0x6eU) {
        xx_quake_md5mesh reader;
        bool valid;
        xx_quake_md5mesh_init(&reader, dev, 0);
        valid = xx_quake_md5mesh_check_is_valid(&reader.format, NULL);
        xx_quake_md5mesh_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_QUAKE_MD5MESH;
    }
    if ((header_size>=1084 && header[1080]==0x4d && header[1081]==0x2e && header[1082]==0x4b && header[1083]==0x2e) || (header_size>=1084 && header[1080]==0x4d && header[1081]==0x21 && header[1082]==0x4b && header[1083]==0x21) || (header_size>=1084 && header[1080]==0x34 && header[1081]==0x43 && header[1082]==0x48 && header[1083]==0x4e) || (header_size>=1084 && header[1080]==0x46 && header[1081]==0x4c && header[1082]==0x54 && header[1083]==0x34)) {
        xx_tracker_mod reader;
        bool valid;
        xx_tracker_mod_init(&reader, dev, 0);
        valid = xx_tracker_mod_check_is_valid(&reader.format, NULL);
        xx_tracker_mod_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_MOD;
    }
    if ((header_size>=4 && header[0]==0x46 && header[1]==0x41 && header[2]==0x52 && header[3]==0xfe)) {
        xx_tracker_far reader;
        bool valid;
        xx_tracker_far_init(&reader, dev, 0);
        valid = xx_tracker_far_check_is_valid(&reader.format, NULL);
        xx_tracker_far_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_FAR;
    }
    if ((header_size>=4 && header[0]==0x44 && header[1]==0x4d && header[2]==0x44 && header[3]==0x4c)) {
        xx_tracker_mdl reader;
        bool valid;
        xx_tracker_mdl_init(&reader, dev, 0);
        valid = xx_tracker_mdl_check_is_valid(&reader.format, NULL);
        xx_tracker_mdl_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_MDL;
    }
    if ((header_size>=4 && header[0]==0x47 && header[1]==0x44 && header[2]==0x4d && header[3]==0xfe) && (header_size>=75 && header[71]==0x47 && header[72]==0x4d && header[73]==0x46 && header[74]==0x53)) {
        xx_tracker_gdm reader;
        bool valid;
        xx_tracker_gdm_init(&reader, dev, 0);
        valid = xx_tracker_gdm_check_is_valid(&reader.format, NULL);
        xx_tracker_gdm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_GDM;
    }
    if ((header_size>=4 && header[0]==0x44 && header[1]==0x42 && header[2]==0x4d && header[3]==0x30)) {
        xx_tracker_dbm reader;
        bool valid;
        xx_tracker_dbm_init(&reader, dev, 0);
        valid = xx_tracker_dbm_check_is_valid(&reader.format, NULL);
        xx_tracker_dbm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_DBM;
    }
    if ((header_size>=4 && header[0]==0x4d && header[1]==0x4d && header[2]==0x44 && header[3]==0x30) || (header_size>=4 && header[0]==0x4d && header[1]==0x4d && header[2]==0x44 && header[3]==0x31)) {
        xx_tracker_med reader;
        bool valid;
        xx_tracker_med_init(&reader, dev, 0);
        valid = xx_tracker_med_check_is_valid(&reader.format, NULL);
        xx_tracker_med_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_MED;
    }
    if ((header_size>=64 && header[60]==0x49 && header[61]==0x4d && header[62]==0x31 && header[63]==0x30)) {
        xx_tracker_imf reader;
        bool valid;
        xx_tracker_imf_init(&reader, dev, 0);
        valid = xx_tracker_imf_check_is_valid(&reader.format, NULL);
        xx_tracker_imf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_IMF;
    }
    if ((header_size>=4 && header[0]==0x41 && header[1]==0x4d && header[2]==0x46 && header[3]==0x0e)) {
        xx_tracker_amf reader;
        bool valid;
        xx_tracker_amf_init(&reader, dev, 0);
        valid = xx_tracker_amf_check_is_valid(&reader.format, NULL);
        xx_tracker_amf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_AMF;
    }
    if ((header_size>=4 && header[0]==0x50 && header[1]==0x53 && header[2]==0x4d && header[3]==0x20)) {
        xx_tracker_psm reader;
        bool valid;
        xx_tracker_psm_init(&reader, dev, 0);
        valid = xx_tracker_psm_check_is_valid(&reader.format, NULL);
        xx_tracker_psm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_PSM;
    }
    if ((header_size>=4 && header[0]==0x43 && header[1]==0x63 && header[2]==0x6e && header[3]==0x4b)) {
        xx_steinberg_fxb reader;
        bool valid;
        xx_steinberg_fxb_init(&reader, dev, 0);
        valid = xx_steinberg_fxb_check_is_valid(&reader.format, NULL);
        xx_steinberg_fxb_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_STEINBERG_FXB;
    }
    if (((header_size >= 14 && header[0] == 76 && header[1] == 85 && header[2] == 67 && header[3] == 65 && header[4] == 77 && header[5] == 45 && header[6] == 82 && header[7] == 69 && header[8] == 67 && header[9] == 79 && header[10] == 82 && header[11] == 68 && header[12] == 69 && header[13] == 82))) {
        xx_astronomy_ser reader;
        bool valid;
        xx_astronomy_ser_init(&reader, dev, 0);
        valid = xx_astronomy_ser_check_is_valid(&reader.format, NULL);
        xx_astronomy_ser_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ASTRONOMY_SER;
    }
    if (((header_size >= 8 && header[0] == 80 && header[1] == 81 && header[2] == 84 && header[3] == 84 && header[4] == 84 && header[5] == 82 && header[6] == 0 && header[7] == 0))) {
        xx_photontiming_ptu reader;
        bool valid;
        xx_photontiming_ptu_init(&reader, dev, 0);
        valid = xx_photontiming_ptu_check_is_valid(&reader.format, NULL);
        xx_photontiming_ptu_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PHOTONTIMING_PTU;
    }
    if (((header_size >= 8 && header[0] == 80 && header[1] == 81 && header[2] == 72 && header[3] == 73 && header[4] == 83 && header[5] == 84 && header[6] == 79 && header[7] == 0))) {
        xx_photontiming_phu reader;
        bool valid;
        xx_photontiming_phu_init(&reader, dev, 0);
        valid = xx_photontiming_phu_check_is_valid(&reader.format, NULL);
        xx_photontiming_phu_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PHOTONTIMING_PHU;
    }
    if (((header_size >= 8 && header[0] == 84 && header[1] == 0 && header[2] == 0 && header[3] == 0 && header[4] == 67 && header[5] == 79 && header[6] == 82 && header[7] == 68) || (header_size >= 8 && header[0] == 0 && header[1] == 0 && header[2] == 0 && header[3] == 84 && header[4] == 67 && header[5] == 79 && header[6] == 82 && header[7] == 68))) {
        xx_charmm_dcd reader;
        bool valid;
        xx_charmm_dcd_init(&reader, dev, 0);
        valid = xx_charmm_dcd_check_is_valid(&reader.format, NULL);
        xx_charmm_dcd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CHARMM_DCD;
    }
    if (((header_size >= 4 && header[0] == 0 && header[1] == 0 && header[2] == 7 && header[3] == 201))) {
        xx_gromacs_trr reader;
        bool valid;
        xx_gromacs_trr_init(&reader, dev, 0);
        valid = xx_gromacs_trr_check_is_valid(&reader.format, NULL);
        xx_gromacs_trr_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GROMACS_TRR;
    }
    if (((header_size >= 17 && header[0] == 9 && header[1] == 10 && header[2] == 105 && header[3] == 99 && header[4] == 115 && header[5] == 95 && header[6] == 118 && header[7] == 101 && header[8] == 114 && header[9] == 115 && header[10] == 105 && header[11] == 111 && header[12] == 110 && header[13] == 9 && header[14] == 50 && header[15] == 46 && header[16] == 48))) {
        xx_microscopy_ics reader;
        bool valid;
        xx_microscopy_ics_init(&reader, dev, 0);
        valid = xx_microscopy_ics_check_is_valid(&reader.format, NULL);
        xx_microscopy_ics_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MICROSCOPY_ICS;
    }
    if (((header_size >= 8 && header[0] == 35 && header[1] == 33 && header[2] == 84 && header[3] == 68 && header[4] == 86 && header[5] == 49 && header[6] == 49 && header[7] == 50))) {
        xx_tecplot_plt reader;
        bool valid;
        xx_tecplot_plt_init(&reader, dev, 0);
        valid = xx_tecplot_plt_check_is_valid(&reader.format, NULL);
        xx_tecplot_plt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TECPLOT_PLT;
    }
    if (((header_size >= 16 && header[0] == 70 && header[1] == 85 && header[2] == 74 && header[3] == 73 && header[4] == 70 && header[5] == 73 && header[6] == 76 && header[7] == 77 && header[8] == 67 && header[9] == 67 && header[10] == 68 && header[11] == 45 && header[12] == 82 && header[13] == 65 && header[14] == 87 && header[15] == 32))) {
        xx_fujifilm_raf reader;
        bool valid;
        xx_fujifilm_raf_init(&reader, dev, 0);
        valid = xx_fujifilm_raf_check_is_valid(&reader.format, NULL);
        xx_fujifilm_raf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FUJIFILM_RAF;
    }
    if (((header_size >= 4 && header[0] == 70 && header[1] == 79 && header[2] == 86 && header[3] == 98))) {
        xx_sigma_x3f reader;
        bool valid;
        xx_sigma_x3f_init(&reader, dev, 0);
        valid = xx_sigma_x3f_check_is_valid(&reader.format, NULL);
        xx_sigma_x3f_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SIGMA_X3F;
    }
    if (((header_size >= 4 && header[0] == 0 && header[1] == 77 && header[2] == 82 && header[3] == 77))) {
        xx_minolta_mrw reader;
        bool valid;
        xx_minolta_mrw_init(&reader, dev, 0);
        valid = xx_minolta_mrw_check_is_valid(&reader.format, NULL);
        xx_minolta_mrw_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MINOLTA_MRW;
    }
}
