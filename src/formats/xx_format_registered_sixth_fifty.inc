/* Reviewed bounded probes for the sixth group of fifty readers. */
{
    uint8_t header[512] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0))) {
        xx_sfx_arcv2 reader;
        bool valid;
        xx_sfx_arcv2_init(&reader, dev, 0);
        valid = xx_sfx_arcv2_check_is_valid(&reader.format, NULL);
        xx_sfx_arcv2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ARCV2;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0))) {
        xx_sfx_chz reader;
        bool valid;
        xx_sfx_chz_init(&reader, dev, 0);
        valid = xx_sfx_chz_check_is_valid(&reader.format, NULL);
        xx_sfx_chz_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_CHZ;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_szdd reader;
        bool valid;
        xx_sfx_szdd_init(&reader, dev, 0);
        valid = xx_sfx_szdd_check_is_valid(&reader.format, NULL);
        xx_sfx_szdd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_SZDD;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_mpq reader;
        bool valid;
        xx_sfx_mpq_init(&reader, dev, 0);
        valid = xx_sfx_mpq_check_is_valid(&reader.format, NULL);
        xx_sfx_mpq_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_MPQ;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0))) {
        xx_sfx_swag reader;
        bool valid;
        xx_sfx_swag_init(&reader, dev, 0);
        valid = xx_sfx_swag_check_is_valid(&reader.format, NULL);
        xx_sfx_swag_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_SWAG;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_zpak reader;
        bool valid;
        xx_sfx_zpak_init(&reader, dev, 0);
        valid = xx_sfx_zpak_check_is_valid(&reader.format, NULL);
        xx_sfx_zpak_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ZPAK;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_diskexpress reader;
        bool valid;
        xx_sfx_diskexpress_init(&reader, dev, 0);
        valid = xx_sfx_diskexpress_check_is_valid(&reader.format, NULL);
        xx_sfx_diskexpress_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_DISKEXPRESS;
    }
    if (!is_mz && (header_size >= 4 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x7f\x45\x4c\x46",4) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_bzip2 reader;
        bool valid;
        xx_sfx_bzip2_init(&reader, dev, 0);
        valid = xx_sfx_bzip2_check_is_valid(&reader.format, NULL);
        xx_sfx_bzip2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_BZIP2;
    }
    if (!is_mz && (header_size >= 4 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x7f\x45\x4c\x46",4) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_gzip reader;
        bool valid;
        xx_sfx_gzip_init(&reader, dev, 0);
        valid = xx_sfx_gzip_check_is_valid(&reader.format, NULL);
        xx_sfx_gzip_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_GZIP;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_tar reader;
        bool valid;
        xx_sfx_tar_init(&reader, dev, 0);
        valid = xx_sfx_tar_check_is_valid(&reader.format, NULL);
        xx_sfx_tar_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_TAR;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_cab reader;
        bool valid;
        xx_sfx_cab_init(&reader, dev, 0);
        valid = xx_sfx_cab_check_is_valid(&reader.format, NULL);
        xx_sfx_cab_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_CAB;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x18\x0a",2) == 0 || xx_rt_memcmp(header+0,"\x18\x79",2) == 0 || xx_rt_memcmp(header+0,"\xeb\x18",2) == 0))) {
        xx_pmarc_sfx reader;
        bool valid;
        xx_pmarc_sfx_init(&reader, dev, 0);
        valid = xx_pmarc_sfx_check_is_valid(&reader.format, NULL);
        xx_pmarc_sfx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PMARC_SFX;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_7zip reader;
        bool valid;
        xx_sfx_7zip_init(&reader, dev, 0);
        valid = xx_sfx_7zip_check_is_valid(&reader.format, NULL);
        xx_sfx_7zip_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_7ZIP;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_ace reader;
        bool valid;
        xx_sfx_ace_init(&reader, dev, 0);
        valid = xx_sfx_ace_check_is_valid(&reader.format, NULL);
        xx_sfx_ace_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ACE;
    }
    if (!is_mz && (header_size >= 2 && (xx_rt_memcmp(header+0,"\x4d\x5a",2) == 0 || xx_rt_memcmp(header+0,"\x5a\x4d",2) == 0 || xx_rt_memcmp(header+0,"\x60\x1a",2) == 0))) {
        xx_sfx_zipcentral reader;
        bool valid;
        xx_sfx_zipcentral_init(&reader, dev, 0);
        valid = xx_sfx_zipcentral_check_is_valid(&reader.format, NULL);
        xx_sfx_zipcentral_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SFX_ZIPCENTRAL;
    }
    if (total_size >= 2048 && header_size >= 8U && header[0] == 0x50U && header[1] == 0x53U && header[2] == 0x2dU && header[3] == 0x58U && header[4] == 0x20U && header[5] == 0x45U && header[6] == 0x58U && header[7] == 0x45U) {
        xx_sony_psx_exe reader;
        bool valid;
        xx_sony_psx_exe_init(&reader, dev, 0);
        valid = xx_sony_psx_exe_check_is_valid(&reader.format, NULL);
        xx_sony_psx_exe_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SONY_PSX_EXE;
    }
    if (total_size >= 16 && header_size >= 4U && header[0] == 0x50U && header[1] == 0x53U && header[2] == 0x46U && header[3] == 0x01U) {
        xx_sony_psf reader;
        bool valid;
        xx_sony_psf_init(&reader, dev, 0);
        valid = xx_sony_psf_check_is_valid(&reader.format, NULL);
        xx_sony_psf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SONY_PSF;
    }
    if (total_size >= 512 && header_size >= 4U && header[0] == 0x57U && header[1] == 0x42U && header[2] == 0x46U && header[3] == 0x53U) {
        xx_nintendo_wbfs reader;
        bool valid;
        xx_nintendo_wbfs_init(&reader, dev, 0);
        valid = xx_nintendo_wbfs_check_is_valid(&reader.format, NULL);
        xx_nintendo_wbfs_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_WBFS;
    }
    if (total_size >= 52 && header_size >= 4U && header[0] == 0x47U && header[1] == 0x53U && header[2] == 0x54U && header[3] == 0x32U) {
        xx_godot_ctex reader;
        bool valid;
        xx_godot_ctex_init(&reader, dev, 0);
        valid = xx_godot_ctex_check_is_valid(&reader.format, NULL);
        xx_godot_ctex_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GODOT_CTEX;
    }
    if (total_size >= 48 && header_size >= 16U && header[0] == 0x00U && header[1] == 0x00U && header[2] == 0x00U && header[3] == 0x00U && header[4] == 0x00U && header[5] == 0x00U && header[6] == 0x00U && header[7] == 0x00U && header[8] == 0x00U && header[9] == 0x00U && header[10] == 0x00U && header[11] == 0x16U && header[12] == 0x00U && header[13] == 0x00U && header[14] == 0x00U && header[15] == 0x00U) {
        xx_unity_serialized reader;
        bool valid;
        xx_unity_serialized_init(&reader, dev, 0);
        valid = xx_unity_serialized_check_is_valid(&reader.format, NULL);
        xx_unity_serialized_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UNITY_SERIALIZED;
    }
    if (total_size >= 84 && header_size >= 4U && header[0] == 0x49U && header[1] == 0x44U && header[2] == 0x50U && header[3] == 0x4fU) {
        xx_idtech_mdl reader;
        bool valid;
        xx_idtech_mdl_init(&reader, dev, 0);
        valid = xx_idtech_mdl_check_is_valid(&reader.format, NULL);
        xx_idtech_mdl_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IDTECH_MDL;
    }
    if (total_size >= 244 && header_size >= 4U && header[0] == 0x49U && header[1] == 0x44U && header[2] == 0x53U && header[3] == 0x54U) {
        xx_valve_studio_mdl reader;
        bool valid;
        xx_valve_studio_mdl_init(&reader, dev, 0);
        valid = xx_valve_studio_mdl_check_is_valid(&reader.format, NULL);
        xx_valve_studio_mdl_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VALVE_STUDIO_MDL;
    }
    if (total_size >= 12 && header_size >= 4U && header[0] == 0x42U && header[1] == 0x42U && header[2] == 0x33U && header[3] == 0x44U) {
        xx_blitz3d_b3d reader;
        bool valid;
        xx_blitz3d_b3d_init(&reader, dev, 0);
        valid = xx_blitz3d_b3d_check_is_valid(&reader.format, NULL);
        xx_blitz3d_b3d_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BLITZ3D_B3D;
    }
    if (total_size >= 16 && header_size >= 10U && header[0] == 0x4dU && header[1] == 0x53U && header[2] == 0x33U && header[3] == 0x44U && header[4] == 0x30U && header[5] == 0x30U && header[6] == 0x30U && header[7] == 0x30U && header[8] == 0x30U && header[9] == 0x30U) {
        xx_milkshape_ms3d reader;
        bool valid;
        xx_milkshape_ms3d_init(&reader, dev, 0);
        valid = xx_milkshape_ms3d_check_is_valid(&reader.format, NULL);
        xx_milkshape_ms3d_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MILKSHAPE_MS3D;
    }
    if (total_size >= 68 && header_size >= 4U && header[0] == 0x42U && header[1] == 0x43U && header[2] == 0x48U && header[3] == 0x00U) {
        xx_nintendo_bch reader;
        bool valid;
        xx_nintendo_bch_init(&reader, dev, 0);
        valid = xx_nintendo_bch_check_is_valid(&reader.format, NULL);
        xx_nintendo_bch_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BCH;
    }
    if (total_size >= 156 && header_size >= 4U && header[0] == 0x43U && header[1] == 0x47U && header[2] == 0x46U && header[3] == 0x58U) {
        xx_nintendo_cgfx reader;
        bool valid;
        xx_nintendo_cgfx_init(&reader, dev, 0);
        valid = xx_nintendo_cgfx_check_is_valid(&reader.format, NULL);
        xx_nintendo_cgfx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_CGFX;
    }
    if (total_size >= 16 && header_size >= 4 && ((header[0] == 0x59 && header[1] == 0x42 && header[2] == 2 && header[3] == 0) || (header[0] == 0x42 && header[1] == 0x59 && header[2] == 0 && header[3] == 2))) {
        xx_nintendo_byaml reader;
        bool valid;
        xx_nintendo_byaml_init(&reader, dev, 0);
        valid = xx_nintendo_byaml_check_is_valid(&reader.format, NULL);
        xx_nintendo_byaml_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_BYAML;
    }
    if (total_size >= 36 && header_size >= 16U && header[0] == 0x52U && header[1] == 0x65U && header[2] == 0x6cU && header[3] == 0x69U && header[4] == 0x63U && header[5] == 0x20U && header[6] == 0x43U && header[7] == 0x68U && header[8] == 0x75U && header[9] == 0x6eU && header[10] == 0x6bU && header[11] == 0x79U && header[12] == 0x0dU && header[13] == 0x0aU && header[14] == 0x1aU && header[15] == 0x00U) {
        xx_relic_chunky reader;
        bool valid;
        xx_relic_chunky_init(&reader, dev, 0);
        valid = xx_relic_chunky_check_is_valid(&reader.format, NULL);
        xx_relic_chunky_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RELIC_CHUNKY;
    }
    if (total_size >= 33 && header_size >= 26U && header[0] == 0x00U && header[1] == 0x10U && header[2] == 0x5bU && header[3] == 0x4dU && header[4] == 0x65U && header[5] == 0x73U && header[6] == 0x68U && header[7] == 0x53U && header[8] == 0x65U && header[9] == 0x72U && header[10] == 0x69U && header[11] == 0x61U && header[12] == 0x6cU && header[13] == 0x69U && header[14] == 0x7aU && header[15] == 0x65U && header[16] == 0x72U && header[17] == 0x5fU && header[18] == 0x76U && header[19] == 0x31U && header[20] == 0x2eU && header[21] == 0x31U && header[22] == 0x30U && header[23] == 0x30U && header[24] == 0x5dU && header[25] == 0x0aU) {
        xx_ogre_mesh reader;
        bool valid;
        xx_ogre_mesh_init(&reader, dev, 0);
        valid = xx_ogre_mesh_check_is_valid(&reader.format, NULL);
        xx_ogre_mesh_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OGRE_MESH;
    }
    if (header_size>=12 && !xx_rt_memcmp(header,"ASEF\0\1\0\0",8)) {
        xx_adobe_ase reader;
        bool valid;
        xx_adobe_ase_init(&reader, dev, 0);
        valid = xx_adobe_ase_check_is_valid(&reader.format, NULL);
        xx_adobe_ase_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADOBE_ASE;
    }
    if (header_size>=14 && header[0]==0 && header[1]==1 && (header[2] || header[3]) && header[2]<=16 && header[4]==0 && (header[5]<=2 || header[5]==7 || header[5]==8)) {
        xx_adobe_aco reader;
        bool valid;
        xx_adobe_aco_init(&reader, dev, 0);
        valid = xx_adobe_aco_check_is_valid(&reader.format, NULL);
        xx_adobe_aco_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADOBE_ACO;
    }
    if (header_size>=28 && header[4]==0 && header[5]==0 && header[6]==0 && header[7]==2 && !xx_rt_memcmp(header+20,"GIMP",4)) {
        xx_gimp_gbr reader;
        bool valid;
        xx_gimp_gbr_init(&reader, dev, 0);
        valid = xx_gimp_gbr_check_is_valid(&reader.format, NULL);
        xx_gimp_gbr_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GIMP_GBR;
    }
    if (xx_gimp_gih_probe_header(header,header_size)) {
        xx_gimp_gih reader;
        bool valid;
        xx_gimp_gih_init(&reader, dev, 0);
        valid = xx_gimp_gih_check_is_valid(&reader.format, NULL);
        xx_gimp_gih_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GIMP_GIH;
    }
    if (header_size>=24 && header[4]==0 && header[5]==0 && header[6]==0 && header[7]==1 && !xx_rt_memcmp(header+20,"GPAT",4)) {
        xx_gimp_pat reader;
        bool valid;
        xx_gimp_pat_init(&reader, dev, 0);
        valid = xx_gimp_pat_check_is_valid(&reader.format, NULL);
        xx_gimp_pat_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GIMP_PAT;
    }
    if (header_size>=9 && !xx_rt_memcmp(header,"\x97JB2\r\n\x1A\n",8)) {
        xx_jbig2 reader;
        bool valid;
        xx_jbig2_init(&reader, dev, 0);
        valid = xx_jbig2_check_is_valid(&reader.format, NULL);
        xx_jbig2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_JBIG2;
    }
    if (header_size>=16 && !xx_rt_memcmp(header,"AT&TFORM",8) && !xx_rt_memcmp(header+12,"DJVU",4)) {
        xx_djvu reader;
        bool valid;
        xx_djvu_init(&reader, dev, 0);
        valid = xx_djvu_check_is_valid(&reader.format, NULL);
        xx_djvu_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DJVU;
    }
    if (header_size>=88 && header[0]==1 && header[1]==0 && header[2]==0 && header[3]==0 && !xx_rt_memcmp(header+40," EMF",4)) {
        xx_emf reader;
        bool valid;
        xx_emf_init(&reader, dev, 0);
        valid = xx_emf_check_is_valid(&reader.format, NULL);
        xx_emf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_EMF;
    }
    if (header_size>=40 && header[0]==0xD7 && header[1]==0xCD && header[2]==0xC6 && header[3]==0x9A) {
        xx_wmf reader;
        bool valid;
        xx_wmf_init(&reader, dev, 0);
        valid = xx_wmf_check_is_valid(&reader.format, NULL);
        xx_wmf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WMF;
    }
    if (header_size>=9 && !xx_rt_memcmp(header,"#FIG 3.2",8) && (header[8]==10 || header[8]==13)) {
        xx_xfig reader;
        bool valid;
        xx_xfig_init(&reader, dev, 0);
        valid = xx_xfig_check_is_valid(&reader.format, NULL);
        xx_xfig_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_XFIG;
    }
    if (((header_size >= 348 && header[344] == 110 && header[345] == 43 && header[346] == 49 && header[347] == 0))) {
        xx_nifti1 reader;
        bool valid;
        xx_nifti1_init(&reader, dev, 0);
        valid = xx_nifti1_check_is_valid(&reader.format, NULL);
        xx_nifti1_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NIFTI1;
    }
    if (((header_size >= 7 && header[0] == 78 && header[1] == 82 && header[2] == 82 && header[3] == 68 && header[4] == 48 && header[5] == 48 && header[6] == 48))) {
        xx_nrrd reader;
        bool valid;
        xx_nrrd_init(&reader, dev, 0);
        valid = xx_nrrd_check_is_valid(&reader.format, NULL);
        xx_nrrd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NRRD;
    }
    if (((header_size >= 212 && header[208] == 77 && header[209] == 65 && header[210] == 80 && header[211] == 32))) {
        xx_mrc reader;
        bool valid;
        xx_mrc_init(&reader, dev, 0);
        valid = xx_mrc_check_is_valid(&reader.format, NULL);
        xx_mrc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MRC;
    }
    if (((header_size >= 10 && header[0] == 79 && header[1] == 98 && header[2] == 106 && header[3] == 101 && header[4] == 99 && header[5] == 116 && header[6] == 84 && header[7] == 121 && header[8] == 112 && header[9] == 101))) {
        xx_metaimage reader;
        bool valid;
        xx_metaimage_init(&reader, dev, 0);
        valid = xx_metaimage_check_is_valid(&reader.format, NULL);
        xx_metaimage_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_METAIMAGE;
    }
    if (((header_size >= 23 && header[0] == 35 && header[1] == 32 && header[2] == 118 && header[3] == 116 && header[4] == 107 && header[5] == 32 && header[6] == 68 && header[7] == 97 && header[8] == 116 && header[9] == 97 && header[10] == 70 && header[11] == 105 && header[12] == 108 && header[13] == 101 && header[14] == 32 && header[15] == 86 && header[16] == 101 && header[17] == 114 && header[18] == 115 && header[19] == 105 && header[20] == 111 && header[21] == 110 && header[22] == 32))) {
        xx_vtk_legacy reader;
        bool valid;
        xx_vtk_legacy_init(&reader, dev, 0);
        valid = xx_vtk_legacy_check_is_valid(&reader.format, NULL);
        xx_vtk_legacy_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VTK_LEGACY;
    }
    if (((header_size >= 256 && header[252] == 239 && header[253] == 255 && header[254] == 233 && header[255] == 176) || (header_size >= 256 && header[252] == 176 && header[253] == 233 && header[254] == 255 && header[255] == 239) || (header_size >= 256 && header[252] == 42 && header[253] == 227 && header[254] == 137 && header[255] == 184) || (header_size >= 256 && header[252] == 184 && header[253] == 137 && header[254] == 227 && header[255] == 42))) {
        xx_gipl reader;
        bool valid;
        xx_gipl_init(&reader, dev, 0);
        valid = xx_gipl_check_is_valid(&reader.format, NULL);
        xx_gipl_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GIPL;
    }
    if (((header_size >= 8 && header[0] == 48 && header[1] == 32 && header[2] == 32 && header[3] == 32 && header[4] == 32 && header[5] == 32 && header[6] == 32 && header[7] == 32))) {
        xx_edf reader;
        bool valid;
        xx_edf_init(&reader, dev, 0);
        valid = xx_edf_check_is_valid(&reader.format, NULL);
        xx_edf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_EDF;
    }
    if (((header_size >= 6 && header[0] == 70 && header[1] == 67 && header[2] == 83 && header[3] == 51 && header[4] == 46 && header[5] == 48) || (header_size >= 6 && header[0] == 70 && header[1] == 67 && header[2] == 83 && header[3] == 51 && header[4] == 46 && header[5] == 49))) {
        xx_fcs reader;
        bool valid;
        xx_fcs_init(&reader, dev, 0);
        valid = xx_fcs_check_is_valid(&reader.format, NULL);
        xx_fcs_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FCS;
    }
}
