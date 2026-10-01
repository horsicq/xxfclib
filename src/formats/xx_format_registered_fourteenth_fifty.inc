/* Reviewed bounded probes for the fourteenth group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 29 && header[0] == 255 && header[15] == 255 && header[18] == 1) {
        xx_bgp_messages reader;
        bool valid;
        xx_bgp_messages_init(&reader, dev, 0);
        valid = xx_bgp_messages_check_is_valid(&reader.format, NULL);
        xx_bgp_messages_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BGP_MESSAGES;
    }
    if (header_size >= 32 && !xx_rt_memcmp(header,"SSH-2.0-",8)) {
        xx_ssh_transport reader;
        bool valid;
        xx_ssh_transport_init(&reader, dev, 0);
        valid = xx_ssh_transport_check_is_valid(&reader.format, NULL);
        xx_ssh_transport_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SSH_TRANSPORT;
    }
    if (header_size >= 32 && !xx_rt_memcmp(header,"PuTTY-User-Key-File-",20)) {
        xx_putty_ppk reader;
        bool valid;
        xx_putty_ppk_init(&reader, dev, 0);
        valid = xx_putty_ppk_check_is_valid(&reader.format, NULL);
        xx_putty_ppk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PUTTY_PPK;
    }
    if (header_size >= 24 && !xx_rt_memcmp(header,"GGUF",4)) {
        xx_gguf reader;
        bool valid;
        xx_gguf_init(&reader, dev, 0);
        valid = xx_gguf_check_is_valid(&reader.format, NULL);
        xx_gguf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GGUF;
    }
    if (header_size >= 32 && (!xx_rt_memcmp(header,"STOMP",5) || !xx_rt_memcmp(header,"CONNECT",7))) {
        xx_stomp_frames reader;
        bool valid;
        xx_stomp_frames_init(&reader, dev, 0);
        valid = xx_stomp_frames_check_is_valid(&reader.format, NULL);
        xx_stomp_frames_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_STOMP_FRAMES;
    }
    if (total_size >= 24 && header_size >= 13U && !xx_rt_memcmp(header,"\x53\x70\x6c\x69\x6e\x65\x46\x6f\x6e\x74\x44\x42\x3a",13U)) {
        xx_fontforge_sfd reader;
        bool valid;
        xx_fontforge_sfd_init(&reader, dev, 0);
        valid = xx_fontforge_sfd_check_is_valid(&reader.format, NULL);
        xx_fontforge_sfd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FONTFORGE_SFD;
    }
    if (total_size >= 32 && header_size >= 12U && !xx_rt_memcmp(header,"\x46\x49\x4c\x45\x00\x00\x00\x04\x50\x46\x46\x32",12U)) {
        xx_grub_pff2 reader;
        bool valid;
        xx_grub_pff2_init(&reader, dev, 0);
        valid = xx_grub_pff2_check_is_valid(&reader.format, NULL);
        xx_grub_pff2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GRUB_PFF2;
    }
    if (total_size >= 24 && header_size >= 9U && !xx_rt_memcmp(header,"\x48\x49\x45\x52\x41\x52\x43\x48\x59",9U)) {
        xx_bvh_motion reader;
        bool valid;
        xx_bvh_motion_init(&reader, dev, 0);
        valid = xx_bvh_motion_check_is_valid(&reader.format, NULL);
        xx_bvh_motion_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BVH_MOTION;
    }
    if (total_size >= 32 && header_size >= 4U && !xx_rt_memcmp(header,"\x78\x6f\x66\x20",4U)) {
        xx_directx_x reader;
        bool valid;
        xx_directx_x_init(&reader, dev, 0);
        valid = xx_directx_x_check_is_valid(&reader.format, NULL);
        xx_directx_x_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DIRECTX_X;
    }
    if (total_size >= 24 && header_size >= 20U && !xx_rt_memcmp(header,"\x4d\x65\x73\x68\x56\x65\x72\x73\x69\x6f\x6e\x46\x6f\x72\x6d\x61\x74\x74\x65\x64",20U)) {
        xx_medit_mesh reader;
        bool valid;
        xx_medit_mesh_init(&reader, dev, 0);
        valid = xx_medit_mesh_check_is_valid(&reader.format, NULL);
        xx_medit_mesh_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MEDIT_MESH;
    }
    if (total_size >= 24 && header_size >= 6U && !xx_rt_memcmp(header,"\x47\x4f\x43\x41\x44\x20",6U)) {
        xx_gocad_model reader;
        bool valid;
        xx_gocad_model_init(&reader, dev, 0);
        valid = xx_gocad_model_check_is_valid(&reader.format, NULL);
        xx_gocad_model_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GOCAD_MODEL;
    }
    if (total_size >= 400 && header_size >= 8U && !xx_rt_memcmp(header,"\x43\x20\x42\x69\x6e\x61\x72\x79",8U)) {
        xx_ensight_gold_geometry reader;
        bool valid;
        xx_ensight_gold_geometry_init(&reader, dev, 0);
        valid = xx_ensight_gold_geometry_check_is_valid(&reader.format, NULL);
        xx_ensight_gold_geometry_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ENSIGHT_GOLD_GEOMETRY;
    }
    if (total_size >= 24 && header_size >= 8U && !xx_rt_memcmp(header,"\x67\x6d\x76\x69\x6e\x70\x75\x74",8U)) {
        xx_gmv_mesh reader;
        bool valid;
        xx_gmv_mesh_init(&reader, dev, 0);
        valid = xx_gmv_mesh_check_is_valid(&reader.format, NULL);
        xx_gmv_mesh_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GMV_MESH;
    }
    if (total_size >= 3428 && header_size >= 4U && !xx_rt_memcmp(header,"\x55\x48\x4c\x31",4U)) {
        xx_dted_elevation reader;
        bool valid;
        xx_dted_elevation_init(&reader, dev, 0);
        valid = xx_dted_elevation_check_is_valid(&reader.format, NULL);
        xx_dted_elevation_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DTED_ELEVATION;
    }
    if (total_size >= 24 && header_size >= 8U && !xx_rt_memcmp(header,"\x56\x65\x72\x73\x69\x6f\x6e\x20",8U)) {
        xx_mapinfo_mif reader;
        bool valid;
        xx_mapinfo_mif_init(&reader, dev, 0);
        valid = xx_mapinfo_mif_check_is_valid(&reader.format, NULL);
        xx_mapinfo_mif_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MAPINFO_MIF;
    }
    if (header_size >= 32 && (header[0] == 0x84 || header[0] == 0x88)) {
        xx_tracker_coconizer reader;
        bool valid;
        xx_tracker_coconizer_init(&reader, dev, 0);
        valid = xx_tracker_coconizer_check_is_valid(&reader.format, NULL);
        xx_tracker_coconizer_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_COCONIZER;
    }
    if ((header_size >= 5 && header[0] == 0x52 && header[1] == 0x54 && header[2] == 0x4d && header[3] == 0x4d && header[4] == 0x20)) {
        xx_tracker_real reader;
        bool valid;
        xx_tracker_real_init(&reader, dev, 0);
        valid = xx_tracker_real_check_is_valid(&reader.format, NULL);
        xx_tracker_real_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_REAL;
    }
    if (header_size >= 8 && header[0] == 0x4d && header[1] == 0x47 && header[2] == 0x54 && header[3] == 0x11 && header[4] == 0xbd && header[5] == 0x4d && header[6] == 0x43 && header[7] == 0x53) {
        xx_tracker_megatracker reader;
        bool valid;
        xx_tracker_megatracker_init(&reader, dev, 0);
        valid = xx_tracker_megatracker_check_is_valid(&reader.format, NULL);
        xx_tracker_megatracker_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRACKER_MEGATRACKER;
    }
    if (header_size >= 20 && header[0] == 0x41 && header[1] == 0x6d && header[2] == 0x42 && header[3] == 0x6b && header[4] == 0 && header[5] == 3 && header[12] == 0x4d && header[13] == 0x75 && header[14] == 0x73 && header[15] == 0x69 && header[16] == 0x63 && header[17] == 0x20 && header[18] == 0x20 && header[19] == 0x20) {
        xx_amos_music_bank reader;
        bool valid;
        xx_amos_music_bank_init(&reader, dev, 0);
        valid = xx_amos_music_bank_check_is_valid(&reader.format, NULL);
        xx_amos_music_bank_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AMOS_MUSIC_BANK;
    }
    if ((header_size >= 16 && header[0] == 0x52 && header[1] == 0x41 && header[2] == 0x44 && header[3] == 0x20 && header[4] == 0x62 && header[5] == 0x79 && header[6] == 0x20 && header[7] == 0x52 && header[8] == 0x45 && header[9] == 0x41 && header[10] == 0x4c && header[11] == 0x69 && header[12] == 0x54 && header[13] == 0x59 && header[14] == 0x21 && header[15] == 0x21)) {
        xx_adlib_rad reader;
        bool valid;
        xx_adlib_rad_init(&reader, dev, 0);
        valid = xx_adlib_rad_check_is_valid(&reader.format, NULL);
        xx_adlib_rad_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_RAD;
    }
    if ((header_size >= 1071 && header[1062] == 0x3c && header[1063] == 0x6f && header[1064] == 0xef && header[1065] == 0x51 && header[1066] == 0x55 && header[1067] == 0xee && header[1068] == 0x52 && header[1069] == 0x6f && header[1070] == 0x52) || (header_size >= 1071 && header[1062] == 0x4d && header[1063] == 0x61 && header[1064] == 0x44 && header[1065] == 0x6f && header[1066] == 0x4b && header[1067] == 0x61 && header[1068] == 0x4e && header[1069] == 0x39 && header[1070] == 0x36)) {
        xx_adlib_amd reader;
        bool valid;
        xx_adlib_amd_init(&reader, dev, 0);
        valid = xx_adlib_amd_check_is_valid(&reader.format, NULL);
        xx_adlib_amd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_AMD;
    }
    if (header_size >= 12 && header[8] <= 15 && header[9] <= 3 && header[10] <= 3) {
        xx_adlib_hsc reader;
        bool valid;
        xx_adlib_hsc_init(&reader, dev, 0);
        valid = xx_adlib_hsc_check_is_valid(&reader.format, NULL);
        xx_adlib_hsc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_HSC;
    }
    if (header_size >= 11 && header[0] == 0x4a && header[1] == 0x43 && header[2] == 0x48 && header[3] == 0x26 && header[4] == 0x02 && header[5] == 0x66 && header[6] == 0 && header[7] == 4 && header[8] != 0 && header[9] != 0 && header[10] == 0) {
        xx_adlib_d00 reader;
        bool valid;
        xx_adlib_d00_init(&reader, dev, 0);
        valid = xx_adlib_d00_check_is_valid(&reader.format, NULL);
        xx_adlib_d00_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_D00;
    }
    if ((header_size >= 8 && header[2] == 0x41 && header[3] == 0x44 && header[4] == 0x4c && header[5] == 0x49 && header[6] == 0x42 && header[7] == 0x2d)) {
        xx_adlib_bnk reader;
        bool valid;
        xx_adlib_bnk_init(&reader, dev, 0);
        valid = xx_adlib_bnk_check_is_valid(&reader.format, NULL);
        xx_adlib_bnk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_BNK;
    }
    if ((header_size >= 8 && header[0] == 0x44 && header[1] == 0x42 && header[2] == 0x52 && header[3] == 0x41 && header[4] == 0x57 && header[5] == 0x4f && header[6] == 0x50 && header[7] == 0x4c)) {
        xx_dosbox_dro reader;
        bool valid;
        xx_dosbox_dro_init(&reader, dev, 0);
        valid = xx_dosbox_dro_check_is_valid(&reader.format, NULL);
        xx_dosbox_dro_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DOSBOX_DRO;
    }
}
