/* Reviewed bounded probes for the second group of fifty readers. */
{
    uint8_t header[512] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 93U && total_size >= 143360 && header[0x0E] == 0 && header[0x0F] == 2 && header[0x50] == 0 && header[0x51] == 2 && header[0x52] != 0 && header[0x55] >= 1 && header[0x55] <= 2) {
        xx_act_apricot_pc_xi_raw reader;
        bool valid;
        xx_act_apricot_pc_xi_raw_init(&reader, dev, 0);
        valid = xx_act_apricot_pc_xi_raw_check_is_valid(&reader.format, NULL);
        xx_act_apricot_pc_xi_raw_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ACT_APRICOT_PC_XI_RAW;
    }
    if (total_size >= 163840 && xx_adam_detection_hint(dev)) {
        xx_adam reader;
        bool valid;
        xx_adam_init(&reader, dev, 0);
        valid = xx_adam_check_is_valid(&reader.format, NULL);
        xx_adam_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADAM;
    }
    if (total_size == 348160 || total_size == 368640) {
        xx_bondwell_2_disk reader;
        bool valid;
        xx_bondwell_2_disk_init(&reader, dev, 0);
        valid = xx_bondwell_2_disk_check_is_valid(&reader.format, NULL);
        xx_bondwell_2_disk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BONDWELL_2_DISK;
    }
    if (total_size == 1310720 && header_size >= 129U && header[12] == 0 && header[13] == 0 && header[14] == 2 && header[15] == 0 && header[28] == 0 && header[29] == 0 && header[30] == 0 && header[31] == 0 && (header[128] & 3U) == 3U) {
        xx_casio_fz_1_disk reader;
        bool valid;
        xx_casio_fz_1_disk_init(&reader, dev, 0);
        valid = xx_casio_fz_1_disk_check_is_valid(&reader.format, NULL);
        xx_casio_fz_1_disk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CASIO_FZ_1_DISK;
    }
    if (magic_size >= 4U && magic[0] == 'I' && magic[1] == 's' && magic[2] == 'Z' && magic[3] == '!') {
        xx_isz reader;
        bool valid;
        xx_isz_init(&reader, dev, 0);
        valid = xx_isz_check_is_valid(&reader.format, NULL);
        xx_isz_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ISZ;
    }
    if (magic_size >= 16U && (xx_rt_memcmp(magic, "MAMEFLOPPYIMAGE", 14U) == 0 || xx_rt_memcmp(magic, "MESSFLOPPYIMAGE", 14U) == 0)) {
        xx_mame_floppy_image_mfi reader;
        bool valid;
        xx_mame_floppy_image_mfi_init(&reader, dev, 0);
        valid = xx_mame_floppy_image_mfi_check_is_valid(&reader.format, NULL);
        xx_mame_floppy_image_mfi_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MAME_FLOPPY_IMAGE_MFI;
    }
    if (magic_size >= 16U && (xx_rt_memcmp(magic, "WithoutFreeSpace", 16U) == 0 || xx_rt_memcmp(magic, "WithouFreSpacExt", 16U) == 0)) {
        xx_parallels_hdd reader;
        bool valid;
        xx_parallels_hdd_init(&reader, dev, 0);
        valid = xx_parallels_hdd_check_is_valid(&reader.format, NULL);
        xx_parallels_hdd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PARALLELS_HDD;
    }
    if (magic_size >= 3U && magic[0] == 'P' && magic[1] == 'C' && magic[2] == 'M') {
        xx_pc_magazine_flp reader;
        bool valid;
        xx_pc_magazine_flp_init(&reader, dev, 0);
        valid = xx_pc_magazine_flp_check_is_valid(&reader.format, NULL);
        xx_pc_magazine_flp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PC_MAGAZINE_FLP;
    }
    if (header_size >= 20U && header[16] == 0x5A && header[17] == 0xA5 && header[18] == 0xF0 && header[19] == 0x0F) {
        xx_pchrom reader;
        bool valid;
        xx_pchrom_init(&reader, dev, 0);
        valid = xx_pchrom_check_is_valid(&reader.format, NULL);
        xx_pchrom_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PCHROM;
    }
    if (magic_size >= 11U && xx_rt_memcmp(magic, "-----BEGIN ", 11U) == 0) {
        xx_pem reader;
        bool valid;
        xx_pem_init(&reader, dev, 0);
        valid = xx_pem_check_is_valid(&reader.format, NULL);
        xx_pem_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PEM;
    }
    if (total_size >= 1536 && xx_prodos_detection_hint(dev)) {
        xx_prodos reader;
        bool valid;
        xx_prodos_init(&reader, dev, 0);
        valid = xx_prodos_check_is_valid(&reader.format, NULL);
        xx_prodos_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PRODOS;
    }
    if (magic_size >= 4U && magic[0] == 'Q' && magic[1] == 'E' && magic[2] == 'D' && magic[3] == 0) {
        xx_qemu_enhanced_disk reader;
        bool valid;
        xx_qemu_enhanced_disk_init(&reader, dev, 0);
        valid = xx_qemu_enhanced_disk_check_is_valid(&reader.format, NULL);
        xx_qemu_enhanced_disk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_QEMU_ENHANCED_DISK;
    }
    if (total_size >= 4672 && ((magic_size >= 16U && magic[0] == 0 && magic[1] == 0xFF && magic[10] == 0xFF && magic[11] == 0 && (magic[15] == 1 || magic[15] == 2)) || total_size % 2336 == 0 || total_size % 2352 == 0 || total_size % 2448 == 0)) {
        xx_rawcd reader;
        bool valid;
        xx_rawcd_init(&reader, dev, 0);
        valid = xx_rawcd_check_is_valid(&reader.format, NULL);
        xx_rawcd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RAWCD;
    }
    if (total_size >= 82944 && total_size <= 2350336) {
        xx_rsdos_fs reader;
        bool valid;
        xx_rsdos_fs_init(&reader, dev, 0);
        valid = xx_rsdos_fs_check_is_valid(&reader.format, NULL);
        xx_rsdos_fs_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RSDOS_FS;
    }
    if (magic_size >= 8U && (magic[0] == 'F' || magic[0] == 'C' || magic[0] == 'Z') && magic[1] == 'W' && magic[2] == 'S') {
        xx_swf reader;
        bool valid;
        xx_swf_init(&reader, dev, 0);
        valid = xx_swf_check_is_valid(&reader.format, NULL);
        xx_swf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SWF;
    }
    if (magic_size >= 64U && magic[0] == 'C' && magic[1] == '6' && magic[2] == '4') {
        xx_t64 reader;
        bool valid;
        xx_t64_init(&reader, dev, 0);
        valid = xx_t64_check_is_valid(&reader.format, NULL);
        xx_t64_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_T64;
    }
    if (magic_size >= 9U && (xx_rt_memcmp(magic, "begin ", 6U) == 0 || (magic[0] == 0xEF && magic[1] == 0xBB && magic[2] == 0xBF && xx_rt_memcmp(magic + 3U, "begin ", 6U) == 0))) {
        xx_uue reader;
        bool valid;
        xx_uue_init(&reader, dev, 0);
        valid = xx_uue_check_is_valid(&reader.format, NULL);
        xx_uue_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UUE;
    }
    if (header_size >= 72U && header[64] == 0x7F && header[65] == 0x10 && header[66] == 0xDA && header[67] == 0xBE) {
        xx_vdi reader;
        bool valid;
        xx_vdi_init(&reader, dev, 0);
        valid = xx_vdi_check_is_valid(&reader.format, NULL);
        xx_vdi_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VDI;
    }
    if (header_size >= 2U && (xx_rt_memcmp(header + 0U, "\x42\x4D", 2U) == 0)) {
        xx_bmp reader;
        bool valid;
        xx_bmp_init(&reader, dev, 0);
        valid = xx_bmp_check_is_valid(&reader.format, NULL);
        xx_bmp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BMP;
    }
    if (header_size >= 36U && (xx_rt_memcmp(header + 28U, "\x43\x46\x45\x31\x43\x46\x45\x31", 8U) == 0)) {
        xx_cfe reader;
        bool valid;
        xx_cfe_init(&reader, dev, 0);
        valid = xx_cfe_check_is_valid(&reader.format, NULL);
        xx_cfe_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CFE;
    }
    if (header_size >= 4U && (xx_rt_memcmp(header + 0U, "\x44\x58\x42\x43", 4U) == 0)) {
        xx_dxbc reader;
        bool valid;
        xx_dxbc_init(&reader, dev, 0);
        valid = xx_dxbc_check_is_valid(&reader.format, NULL);
        xx_dxbc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DXBC;
    }
    if (header_size >= 6U && (xx_rt_memcmp(header + 0U, "\x47\x49\x46\x38\x37\x61", 6U) == 0 || xx_rt_memcmp(header + 0U, "\x47\x49\x46\x38\x39\x61", 6U) == 0)) {
        xx_gif reader;
        bool valid;
        xx_gif_init(&reader, dev, 0);
        valid = xx_gif_check_is_valid(&reader.format, NULL);
        xx_gif_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GIF;
    }
    if (xx_jpeg_check_magic(header, header_size)) {
        xx_jpeg reader;
        bool valid;
        xx_jpeg_init(&reader, dev, 0);
        valid = xx_jpeg_check_is_valid(&reader.format, NULL);
        xx_jpeg_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_JPEG;
    }
    if (header_size >= 60U && (xx_rt_memcmp(header + 56U, "\x41\x52\x4D\x64", 4U) == 0)) {
        xx_linuxarm64 reader;
        bool valid;
        xx_linuxarm64_init(&reader, dev, 0);
        valid = xx_linuxarm64_check_is_valid(&reader.format, NULL);
        xx_linuxarm64_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LINUX_ARM64;
    }
    if (header_size >= 16U && (xx_rt_memcmp(header + 0U, "\xB8\xC0\x07\x8E\xD8\xB8\x00\x90\x8E\xC0\xB9\x00\x01\x29\xF6\x29", 16U) == 0)) {
        xx_linuxboot reader;
        bool valid;
        xx_linuxboot_init(&reader, dev, 0);
        valid = xx_linuxboot_check_is_valid(&reader.format, NULL);
        xx_linuxboot_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LINUXBOOT;
    }
    if (header_size >= 40U && (xx_rt_memcmp(header + 36U, "\x18\x28\x6F\x01", 4U) == 0 || xx_rt_memcmp(header + 36U, "\x01\x6F\x28\x18", 4U) == 0)) {
        xx_linuxzimage reader;
        bool valid;
        xx_linuxzimage_init(&reader, dev, 0);
        valid = xx_linuxzimage_check_is_valid(&reader.format, NULL);
        xx_linuxzimage_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LINUX_ZIMAGE;
    }
    if (header_size >= 4U && (xx_rt_memcmp(header + 0U, "\x0A\x0D\x0D\x0A", 4U) == 0)) {
        xx_pcapng reader;
        bool valid;
        xx_pcapng_init(&reader, dev, 0);
        valid = xx_pcapng_check_is_valid(&reader.format, NULL);
        xx_pcapng_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PCAPNG;
    }
    if (xx_pjl_check_magic(header, header_size)) {
        xx_pjl reader;
        bool valid;
        xx_pjl_init(&reader, dev, 0);
        valid = xx_pjl_check_is_valid(&reader.format, NULL);
        xx_pjl_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PJL;
    }
    if (xx_png_check_magic(header, header_size)) {
        xx_png reader;
        bool valid;
        xx_png_init(&reader, dev, 0);
        valid = xx_png_check_is_valid(&reader.format, NULL);
        xx_png_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PNG;
    }
    if (xx_svg_check_magic(header, header_size)) {
        xx_svg reader;
        bool valid;
        xx_svg_init(&reader, dev, 0);
        valid = xx_svg_check_is_valid(&reader.format, NULL);
        xx_svg_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SVG;
    }
    if (header_size >= 4U && ((header[0] == 0x50 && header[1] == 0x41 && header[2] == 0x43 && header[3] == 0x4B))) {
        xx_quake_pak reader;
        bool valid;
        xx_quake_pak_init(&reader, dev, 0);
        valid = xx_quake_pak_check_is_valid(&reader.format, NULL);
        xx_quake_pak_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_QUAKE_PAK;
    }
    if (header_size >= 4U && ((header[0] == 0x49 && header[1] == 0x57 && header[2] == 0x41 && header[3] == 0x44) || (header[0] == 0x50 && header[1] == 0x57 && header[2] == 0x41 && header[3] == 0x44))) {
        xx_doom_wad reader;
        bool valid;
        xx_doom_wad_init(&reader, dev, 0);
        valid = xx_doom_wad_check_is_valid(&reader.format, NULL);
        xx_doom_wad_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DOOM_WAD;
    }
    if (header_size >= 4U && ((header[0] == 0x57 && header[1] == 0x41 && header[2] == 0x44 && header[3] == 0x32))) {
        xx_quake_wad2 reader;
        bool valid;
        xx_quake_wad2_init(&reader, dev, 0);
        valid = xx_quake_wad2_check_is_valid(&reader.format, NULL);
        xx_quake_wad2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_QUAKE_WAD2;
    }
    if (header_size >= 4U && ((header[0] == 0x57 && header[1] == 0x41 && header[2] == 0x44 && header[3] == 0x33))) {
        xx_halflife_wad3 reader;
        bool valid;
        xx_halflife_wad3_init(&reader, dev, 0);
        valid = xx_halflife_wad3_check_is_valid(&reader.format, NULL);
        xx_halflife_wad3_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_HALFLIFE_WAD3;
    }
    if (header_size >= 12U && ((header[0] == 0x4B && header[1] == 0x65 && header[2] == 0x6E && header[3] == 0x53 && header[4] == 0x69 && header[5] == 0x6C && header[6] == 0x76 && header[7] == 0x65 && header[8] == 0x72 && header[9] == 0x6D && header[10] == 0x61 && header[11] == 0x6E))) {
        xx_build_grp reader;
        bool valid;
        xx_build_grp_init(&reader, dev, 0);
        valid = xx_build_grp_check_is_valid(&reader.format, NULL);
        xx_build_grp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BUILD_GRP;
    }
    if (header_size >= 4U && ((header[0] == 0x41 && header[1] == 0x46 && header[2] == 0x53 && header[3] == 0x00))) {
        xx_cri_afs reader;
        bool valid;
        xx_cri_afs_init(&reader, dev, 0);
        valid = xx_cri_afs_check_is_valid(&reader.format, NULL);
        xx_cri_afs_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CRI_AFS;
    }
    if (header_size >= 4U && ((header[0] == 0x41 && header[1] == 0x46 && header[2] == 0x53 && header[3] == 0x32))) {
        xx_cri_awb reader;
        bool valid;
        xx_cri_awb_init(&reader, dev, 0);
        valid = xx_cri_awb_check_is_valid(&reader.format, NULL);
        xx_cri_awb_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CRI_AWB;
    }
    if (header_size >= 4U && ((header[0] == 0x34 && header[1] == 0x12 && header[2] == 0xAA && header[3] == 0x55))) {
        xx_valve_vpk reader;
        bool valid;
        xx_valve_vpk_init(&reader, dev, 0);
        valid = xx_valve_vpk_check_is_valid(&reader.format, NULL);
        xx_valve_vpk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VALVE_VPK;
    }
    if (header_size >= 4U && ((header[0] == 0x55 && header[1] == 0xAA && header[2] == 0x38 && header[3] == 0x2D) || (header[0] == 0x2D && header[1] == 0x38 && header[2] == 0xAA && header[3] == 0x55))) {
        xx_nintendo_u8 reader;
        bool valid;
        xx_nintendo_u8_init(&reader, dev, 0);
        valid = xx_nintendo_u8_check_is_valid(&reader.format, NULL);
        xx_nintendo_u8_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_U8;
    }
    if (header_size >= 4U && ((header[0] == 0x52 && header[1] == 0x41 && header[2] == 0x52 && header[3] == 0x43) || (header[0] == 0x43 && header[1] == 0x52 && header[2] == 0x41 && header[3] == 0x52))) {
        xx_nintendo_rarc reader;
        bool valid;
        xx_nintendo_rarc_init(&reader, dev, 0);
        valid = xx_nintendo_rarc_check_is_valid(&reader.format, NULL);
        xx_nintendo_rarc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_RARC;
    }
    if (header_size >= 15U && xx_rt_memcmp(header, "\x41\x4e\x44\x52\x4f\x49\x44\x20\x42\x41\x43\x4b\x55\x50\x0a", 15U) == 0) {
        xx_android_ab reader;
        bool valid;
        xx_android_ab_init(&reader, dev, 0);
        valid = xx_android_ab_check_is_valid(&reader.format, NULL);
        xx_android_ab_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ANDROID_AB;
    }
    if (header_size >= 4U && xx_rt_memcmp(header, "\x4e\x45\x53\x1a", 4U) == 0) {
        xx_nes_rom reader;
        bool valid;
        xx_nes_rom_init(&reader, dev, 0);
        valid = xx_nes_rom_check_is_valid(&reader.format, NULL);
        xx_nes_rom_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NES_ROM;
    }
    if (header_size >= 4U && xx_rt_memcmp(header, "\x4c\x59\x4e\x58", 4U) == 0) {
        xx_lynx_lnx reader;
        bool valid;
        xx_lynx_lnx_init(&reader, dev, 0);
        valid = xx_lynx_lnx_check_is_valid(&reader.format, NULL);
        xx_lynx_lnx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LYNX_LNX;
    }
    if (header_size >= 16U && xx_rt_memcmp(header, "\x43\x36\x34\x20\x43\x41\x52\x54\x52\x49\x44\x47\x45\x20\x20\x20", 16U) == 0) {
        xx_commodore_crt reader;
        bool valid;
        xx_commodore_crt_init(&reader, dev, 0);
        valid = xx_commodore_crt_check_is_valid(&reader.format, NULL);
        xx_commodore_crt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_COMMODORE_CRT;
    }
    if (header_size >= 8U && xx_rt_memcmp(header, "\x55\x46\x32\x0a\x57\x51\x5d\x9e", 8U) == 0) {
        xx_uf2 reader;
        bool valid;
        xx_uf2_init(&reader, dev, 0);
        valid = xx_uf2_check_is_valid(&reader.format, NULL);
        xx_uf2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UF2;
    }
    if (header_size >= 4U && xx_rt_memcmp(header, "\x00\x00\x01\x00", 4U) == 0) {
        xx_ico reader;
        bool valid;
        xx_ico_init(&reader, dev, 0);
        valid = xx_ico_check_is_valid(&reader.format, NULL);
        xx_ico_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ICO;
    }
    if (header_size >= 4U && xx_rt_memcmp(header, "\x4d\x54\x68\x64", 4U) == 0) {
        xx_midi reader;
        bool valid;
        xx_midi_init(&reader, dev, 0);
        valid = xx_midi_check_is_valid(&reader.format, NULL);
        xx_midi_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MIDI;
    }
}
