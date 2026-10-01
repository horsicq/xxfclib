/* Reviewed bounded probes for the third group of fifty readers. */
{
    uint8_t header[512] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 4 && xx_rt_memcmp(header,"\x42\x53\x41\x00",4) == 0) {
        xx_bethesda_bsa reader;
        bool valid;
        xx_bethesda_bsa_init(&reader, dev, 0);
        valid = xx_bethesda_bsa_check_is_valid(&reader.format, NULL);
        xx_bethesda_bsa_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BETHESDA_BSA;
    }
    if (header_size >= 4 && xx_rt_memcmp(header,"\x42\x54\x44\x58",4) == 0) {
        xx_bethesda_ba2 reader;
        bool valid;
        xx_bethesda_ba2_init(&reader, dev, 0);
        valid = xx_bethesda_ba2_check_is_valid(&reader.format, NULL);
        xx_bethesda_ba2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BETHESDA_BA2;
    }
    if (header_size >= 8 && xx_rt_memcmp(header,"\x55\x6e\x69\x74\x79\x46\x53\x00",8) == 0) {
        xx_unityfs reader;
        bool valid;
        xx_unityfs_init(&reader, dev, 0);
        valid = xx_unityfs_check_is_valid(&reader.format, NULL);
        xx_unityfs_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UNITYFS;
    }
    if (header_size >= 4 && xx_rt_memcmp(header,"\x42\x49\x46\x46",4) == 0) {
        xx_bioware_biff reader;
        bool valid;
        xx_bioware_biff_init(&reader, dev, 0);
        valid = xx_bioware_biff_check_is_valid(&reader.format, NULL);
        xx_bioware_biff_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BIOWARE_BIFF;
    }
    if (header_size >= 4 && (xx_rt_memcmp(header,"ERF ",4)==0 || xx_rt_memcmp(header,"MOD ",4)==0 || xx_rt_memcmp(header,"HAK ",4)==0 || xx_rt_memcmp(header,"SAV ",4)==0)) {
        xx_bioware_erf reader;
        bool valid;
        xx_bioware_erf_init(&reader, dev, 0);
        valid = xx_bioware_erf_check_is_valid(&reader.format, NULL);
        xx_bioware_erf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BIOWARE_ERF;
    }
    if (header_size >= 4 && xx_rt_memcmp(header,"\x52\x49\x4d\x20",4) == 0) {
        xx_bioware_rim reader;
        bool valid;
        xx_bioware_rim_init(&reader, dev, 0);
        valid = xx_bioware_rim_check_is_valid(&reader.format, NULL);
        xx_bioware_rim_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BIOWARE_RIM;
    }
    if (header_size >= 4 && xx_rt_memcmp(header,"\x4c\x41\x42\x4e",4) == 0) {
        xx_lucas_lab reader;
        bool valid;
        xx_lucas_lab_init(&reader, dev, 0);
        valid = xx_lucas_lab_check_is_valid(&reader.format, NULL);
        xx_lucas_lab_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LUCAS_LAB;
    }
    if (header_size >= 4 && (xx_rt_memcmp(header,"LB83",4)==0 || xx_rt_memcmp(header,"LB23",4)==0)) {
        xx_lucas_bun reader;
        bool valid;
        xx_lucas_bun_init(&reader, dev, 0);
        valid = xx_lucas_bun_check_is_valid(&reader.format, NULL);
        xx_lucas_bun_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LUCAS_BUN;
    }
    if (header_size >= 4 && xx_rt_memcmp(header,"\x49\x42\x53\x50",4) == 0) {
        xx_idtech_bsp reader;
        bool valid;
        xx_idtech_bsp_init(&reader, dev, 0);
        valid = xx_idtech_bsp_check_is_valid(&reader.format, NULL);
        xx_idtech_bsp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IDTECH_BSP;
    }
    if (header_size >= 4 && xx_rt_memcmp(header,"\x56\x42\x53\x50",4) == 0) {
        xx_valve_bsp reader;
        bool valid;
        xx_valve_bsp_init(&reader, dev, 0);
        valid = xx_valve_bsp_check_is_valid(&reader.format, NULL);
        xx_valve_bsp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VALVE_BSP;
    }
    if (header_size >= 4 && xx_rt_memcmp(header,"\x49\x44\x50\x32",4) == 0) {
        xx_idtech_md2 reader;
        bool valid;
        xx_idtech_md2_init(&reader, dev, 0);
        valid = xx_idtech_md2_check_is_valid(&reader.format, NULL);
        xx_idtech_md2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IDTECH_MD2;
    }
    if (header_size >= 4 && xx_rt_memcmp(header,"\x49\x44\x50\x33",4) == 0) {
        xx_idtech_md3 reader;
        bool valid;
        xx_idtech_md3_init(&reader, dev, 0);
        valid = xx_idtech_md3_check_is_valid(&reader.format, NULL);
        xx_idtech_md3_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IDTECH_MD3;
    }
    if (header_size >= 4 && xx_rt_memcmp(header,"\x44\x14\x72\x12",4) == 0) {
        xx_idtech_qvm reader;
        bool valid;
        xx_idtech_qvm_init(&reader, dev, 0);
        valid = xx_idtech_qvm_check_is_valid(&reader.format, NULL);
        xx_idtech_qvm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IDTECH_QVM;
    }
    if (header_size >= 4 && xx_rt_memcmp(header,"\x4d\x48\x57\x4b",4) == 0) {
        xx_mohawk_mhk reader;
        bool valid;
        xx_mohawk_mhk_init(&reader, dev, 0);
        valid = xx_mohawk_mhk_check_is_valid(&reader.format, NULL);
        xx_mohawk_mhk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MOHAWK_MHK;
    }
    if (header_size >= 4 && xx_rt_memcmp(header,"\x49\x44\x53\x50",4) == 0) {
        xx_quake_sprite reader;
        bool valid;
        xx_quake_sprite_init(&reader, dev, 0);
        valid = xx_quake_sprite_check_is_valid(&reader.format, NULL);
        xx_quake_sprite_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_QUAKE_SPRITE;
    }
    if (total_size >= 16) {
        xx_nintendo_narc reader;
        bool valid;
        xx_nintendo_narc_init(&reader, dev, 0);
        valid = xx_nintendo_narc_check_is_valid(&reader.format, NULL);
        xx_nintendo_narc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_NARC;
    }
    if (total_size >= 16) {
        xx_nintendo_sarc reader;
        bool valid;
        xx_nintendo_sarc_init(&reader, dev, 0);
        valid = xx_nintendo_sarc_check_is_valid(&reader.format, NULL);
        xx_nintendo_sarc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_SARC;
    }
    if (total_size >= 16) {
        xx_nintendo_pfs0 reader;
        bool valid;
        xx_nintendo_pfs0_init(&reader, dev, 0);
        valid = xx_nintendo_pfs0_check_is_valid(&reader.format, NULL);
        xx_nintendo_pfs0_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_PFS0;
    }
    if (total_size >= 16) {
        xx_nintendo_hfs0 reader;
        bool valid;
        xx_nintendo_hfs0_init(&reader, dev, 0);
        valid = xx_nintendo_hfs0_check_is_valid(&reader.format, NULL);
        xx_nintendo_hfs0_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_HFS0;
    }
    if (total_size >= 16) {
        xx_nintendo_brres reader;
        bool valid;
        xx_nintendo_brres_init(&reader, dev, 0);
        valid = xx_nintendo_brres_check_is_valid(&reader.format, NULL);
        xx_nintendo_brres_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BRRES;
    }
    if (total_size >= 16) {
        xx_nintendo_bcstm reader;
        bool valid;
        xx_nintendo_bcstm_init(&reader, dev, 0);
        valid = xx_nintendo_bcstm_check_is_valid(&reader.format, NULL);
        xx_nintendo_bcstm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BCSTM;
    }
    if (total_size >= 16) {
        xx_nintendo_bfsar reader;
        bool valid;
        xx_nintendo_bfsar_init(&reader, dev, 0);
        valid = xx_nintendo_bfsar_check_is_valid(&reader.format, NULL);
        xx_nintendo_bfsar_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BFSAR;
    }
    if (total_size >= 16) {
        xx_nintendo_bcsar reader;
        bool valid;
        xx_nintendo_bcsar_init(&reader, dev, 0);
        valid = xx_nintendo_bcsar_check_is_valid(&reader.format, NULL);
        xx_nintendo_bcsar_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BCSAR;
    }
    if (total_size >= 16) {
        xx_sony_psarc reader;
        bool valid;
        xx_sony_psarc_init(&reader, dev, 0);
        valid = xx_sony_psarc_check_is_valid(&reader.format, NULL);
        xx_sony_psarc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SONY_PSARC;
    }
    if (total_size >= 16) {
        xx_ktx reader;
        bool valid;
        xx_ktx_init(&reader, dev, 0);
        valid = xx_ktx_check_is_valid(&reader.format, NULL);
        xx_ktx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_KTX;
    }
    if (total_size >= 16) {
        xx_ktx2 reader;
        bool valid;
        xx_ktx2_init(&reader, dev, 0);
        valid = xx_ktx2_check_is_valid(&reader.format, NULL);
        xx_ktx2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_KTX2;
    }
    if (total_size >= 16) {
        xx_dds reader;
        bool valid;
        xx_dds_init(&reader, dev, 0);
        valid = xx_dds_check_is_valid(&reader.format, NULL);
        xx_dds_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DDS;
    }
    if (total_size >= 16) {
        xx_pvr reader;
        bool valid;
        xx_pvr_init(&reader, dev, 0);
        valid = xx_pvr_check_is_valid(&reader.format, NULL);
        xx_pvr_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PVR;
    }
    if (total_size >= 16) {
        xx_valve_vtf reader;
        bool valid;
        xx_valve_vtf_init(&reader, dev, 0);
        valid = xx_valve_vtf_check_is_valid(&reader.format, NULL);
        xx_valve_vtf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VALVE_VTF;
    }
    if (total_size >= 16) {
        xx_xbox_xbe reader;
        bool valid;
        xx_xbox_xbe_init(&reader, dev, 0);
        valid = xx_xbox_xbe_check_is_valid(&reader.format, NULL);
        xx_xbox_xbe_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_XBOX_XBE;
    }
    if (magic_size >= 4 && !xx_rt_memcmp(magic,"fLaC",4)) {
        xx_flac reader;
        bool valid;
        xx_flac_init(&reader, dev, 0);
        valid = xx_flac_check_is_valid(&reader.format, NULL);
        xx_flac_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FLAC;
    }
    if (magic_size >= 5 && !xx_rt_memcmp(magic,"OggS",4) && magic[4] == 0) {
        xx_ogg reader;
        bool valid;
        xx_ogg_init(&reader, dev, 0);
        valid = xx_ogg_check_is_valid(&reader.format, NULL);
        xx_ogg_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OGG;
    }
    if (magic_size >= 12 && !xx_rt_memcmp(magic+4,"ftyp",4)) {
        xx_mp4 reader;
        bool valid;
        xx_mp4_init(&reader, dev, 0);
        valid = xx_mp4_check_is_valid(&reader.format, NULL);
        xx_mp4_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MP4;
    }
    if (magic_size >= 4 && magic[0]==0x1A && magic[1]==0x45 && magic[2]==0xDF && magic[3]==0xA3) {
        xx_matroska reader;
        bool valid;
        xx_matroska_init(&reader, dev, 0);
        valid = xx_matroska_check_is_valid(&reader.format, NULL);
        xx_matroska_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MATROSKA;
    }
    if (magic_size >= 12 && !xx_rt_memcmp(magic,"FORM",4) && (!xx_rt_memcmp(magic+8,"AIFF",4) || !xx_rt_memcmp(magic+8,"AIFC",4))) {
        xx_aiff reader;
        bool valid;
        xx_aiff_init(&reader, dev, 0);
        valid = xx_aiff_check_is_valid(&reader.format, NULL);
        xx_aiff_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AIFF;
    }
    if (magic_size >= 8 && !xx_rt_memcmp(magic,"caff",4) && magic[4]==0 && magic[5]==1) {
        xx_caf reader;
        bool valid;
        xx_caf_init(&reader, dev, 0);
        valid = xx_caf_check_is_valid(&reader.format, NULL);
        xx_caf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CAF;
    }
    if (magic_size >= 6 && !xx_rt_memcmp(magic,"8BPS",4) && magic[4]==0 && (magic[5]==1 || magic[5]==2)) {
        xx_photoshop_psd reader;
        bool valid;
        xx_photoshop_psd_init(&reader, dev, 0);
        valid = xx_photoshop_psd_check_is_valid(&reader.format, NULL);
        xx_photoshop_psd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PHOTOSHOP_PSD;
    }
    if (magic_size >= 4 && ((magic[0]==0x49 && magic[1]==0x49 && (magic[2]==42 || magic[2]==43) && magic[3]==0) || (magic[0]==0x4D && magic[1]==0x4D && magic[2]==0 && (magic[3]==42 || magic[3]==43)))) {
        xx_tiff reader;
        bool valid;
        xx_tiff_init(&reader, dev, 0);
        valid = xx_tiff_check_is_valid(&reader.format, NULL);
        xx_tiff_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TIFF;
    }
    if (magic_size >= 8 && magic[0]==0x76 && magic[1]==0x2F && magic[2]==0x31 && magic[3]==1 && magic[4]==2) {
        xx_openexr reader;
        bool valid;
        xx_openexr_init(&reader, dev, 0);
        valid = xx_openexr_check_is_valid(&reader.format, NULL);
        xx_openexr_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OPENEXR;
    }
    if (magic_size >= 12 && magic[0]==0 && magic[1]==0 && magic[2]==0 && magic[3]==12 && !xx_rt_memcmp(magic+4,"jP  ",4) && magic[8]==13 && magic[9]==10 && magic[10]==0x87 && magic[11]==10) {
        xx_jpeg2000_jp2 reader;
        bool valid;
        xx_jpeg2000_jp2_init(&reader, dev, 0);
        valid = xx_jpeg2000_jp2_check_is_valid(&reader.format, NULL);
        xx_jpeg2000_jp2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_JPEG2000_JP2;
    }
    if (header_size >= 8 && header[0] == 86 && header[1] == 78 && header[2] == 68 && header[3] == 82 && header[4] == 66 && header[5] == 79 && header[6] == 79 && header[7] == 84) {
        xx_android_vendor_boot reader;
        bool valid;
        xx_android_vendor_boot_init(&reader, dev, 0);
        valid = xx_android_vendor_boot_check_is_valid(&reader.format, NULL);
        xx_android_vendor_boot_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ANDROID_VENDOR_BOOT;
    }
    if (header_size >= 4 && header[0] == 215 && header[1] == 183 && header[2] == 171 && header[3] == 30) {
        xx_android_dtbo reader;
        bool valid;
        xx_android_dtbo_init(&reader, dev, 0);
        valid = xx_android_dtbo_check_is_valid(&reader.format, NULL);
        xx_android_dtbo_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ANDROID_DTBO;
    }
    if (header_size >= 4 && header[0] == 65 && header[1] == 86 && header[2] == 66 && header[3] == 48) {
        xx_android_vbmeta reader;
        bool valid;
        xx_android_vbmeta_init(&reader, dev, 0);
        valid = xx_android_vbmeta_check_is_valid(&reader.format, NULL);
        xx_android_vbmeta_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ANDROID_VBMETA;
    }
    if (header_size >= 24 && header[0] == 0xe9 && header[1] > 0 && header[1] <= 16 && header[12] == 0 && header[13] == 0) {
        xx_espressif_image reader;
        bool valid;
        xx_espressif_image_init(&reader, dev, 0);
        valid = xx_espressif_image_check_is_valid(&reader.format, NULL);
        xx_espressif_image_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ESPRESSIF_IMAGE;
    }
    if (header_size >= 8 && header[0] == 0 && header[1] == 97 && header[2] == 115 && header[3] == 109 && header[4] == 1 && header[5] == 0 && header[6] == 0 && header[7] == 0) {
        xx_wasm reader;
        bool valid;
        xx_wasm_init(&reader, dev, 0);
        valid = xx_wasm_check_is_valid(&reader.format, NULL);
        xx_wasm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WASM;
    }
    if (header_size >= 4 && header[0] == 222 && header[1] == 192 && header[2] == 23 && header[3] == 11) {
        xx_llvm_bitcode_wrapper reader;
        bool valid;
        xx_llvm_bitcode_wrapper_init(&reader, dev, 0);
        valid = xx_llvm_bitcode_wrapper_check_is_valid(&reader.format, NULL);
        xx_llvm_bitcode_wrapper_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LLVM_BITCODE_WRAPPER;
    }
    if (header_size >= 4 && header[0] == 66 && header[1] == 83 && header[2] == 74 && header[3] == 66) {
        xx_dotnet_metadata reader;
        bool valid;
        xx_dotnet_metadata_init(&reader, dev, 0);
        valid = xx_dotnet_metadata_check_is_valid(&reader.format, NULL);
        xx_dotnet_metadata_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DOTNET_METADATA;
    }
    if (header_size >= 12 && ((header[0] == 0 && header[1] == 1 && header[2] == 0 && header[3] == 0) || !xx_rt_memcmp(header,"OTTO",4) || !xx_rt_memcmp(header,"true",4) || !xx_rt_memcmp(header,"typ1",4))) {
        xx_sfnt reader;
        bool valid;
        xx_sfnt_init(&reader, dev, 0);
        valid = xx_sfnt_check_is_valid(&reader.format, NULL);
        xx_sfnt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFNT;
    }
    if (header_size >= 4 && header[0] == 119 && header[1] == 79 && header[2] == 70 && header[3] == 70) {
        xx_woff reader;
        bool valid;
        xx_woff_init(&reader, dev, 0);
        valid = xx_woff_check_is_valid(&reader.format, NULL);
        xx_woff_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WOFF;
    }
    if (header_size >= 4 && header[0] == 119 && header[1] == 79 && header[2] == 70 && header[3] == 50) {
        xx_woff2 reader;
        bool valid;
        xx_woff2_init(&reader, dev, 0);
        valid = xx_woff2_check_is_valid(&reader.format, NULL);
        xx_woff2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WOFF2;
    }
}
