/* Reviewed bounded probes for the twelfth group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 1 && !xx_rt_memcmp(header,"\x0a",1)) {
        xx_minecraft_nbt reader;
        bool valid;
        xx_minecraft_nbt_init(&reader, dev, 0);
        valid = xx_minecraft_nbt_check_is_valid(&reader.format, NULL);
        xx_minecraft_nbt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MINECRAFT_NBT;
    }
    if (header_size >= 19 && header[6] == 1) {
        xx_leveldb_log reader;
        bool valid;
        xx_leveldb_log_init(&reader, dev, 0);
        valid = xx_leveldb_log_check_is_valid(&reader.format, NULL);
        xx_leveldb_log_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LEVELDB_LOG;
    }
    if (header_size >= 12 && header[4] == 0 && header[5] == 1) {
        xx_dns_message reader;
        bool valid;
        xx_dns_message_init(&reader, dev, 0);
        valid = xx_dns_message_check_is_valid(&reader.format, NULL);
        xx_dns_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DNS_MESSAGE;
    }
    if (header_size >= 21 && header[12] == 0xdd && header[13] == 7 && !header[14] && !header[15]) {
        xx_mongodb_wire reader;
        bool valid;
        xx_mongodb_wire_init(&reader, dev, 0);
        valid = xx_mongodb_wire_check_is_valid(&reader.format, NULL);
        xx_mongodb_wire_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MONGODB_WIRE;
    }
    if (header_size >= 24 && header[0] == '*') {
        xx_redis_resp reader;
        bool valid;
        xx_redis_resp_init(&reader, dev, 0);
        valid = xx_redis_resp_check_is_valid(&reader.format, NULL);
        xx_redis_resp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_REDIS_RESP;
    }
    if (header_size >= 20 && header[0] == 0x10) {
        xx_mqtt_packets reader;
        bool valid;
        xx_mqtt_packets_init(&reader, dev, 0);
        valid = xx_mqtt_packets_check_is_valid(&reader.format, NULL);
        xx_mqtt_packets_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MQTT_PACKETS;
    }
    if (header_size >= 8 && header[0] == 0x82 && (header[1] & 31) == 1) {
        xx_thrift_compact reader;
        bool valid;
        xx_thrift_compact_init(&reader, dev, 0);
        valid = xx_thrift_compact_check_is_valid(&reader.format, NULL);
        xx_thrift_compact_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_THRIFT_COMPACT;
    }
    if (header_size >= 32 && header[0] == 0x30) {
        xx_x509_certificate reader;
        bool valid;
        xx_x509_certificate_init(&reader, dev, 0);
        valid = xx_x509_certificate_check_is_valid(&reader.format, NULL);
        xx_x509_certificate_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_X509_CERTIFICATE;
    }
    if (header_size >= 24 && header[0] == 0x30) {
        xx_pkcs10_csr reader;
        bool valid;
        xx_pkcs10_csr_init(&reader, dev, 0);
        valid = xx_pkcs10_csr_check_is_valid(&reader.format, NULL);
        xx_pkcs10_csr_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PKCS10_CSR;
    }
    if (header_size >= 24 && header[0] == 0x30) {
        xx_pkcs12_pfx reader;
        bool valid;
        xx_pkcs12_pfx_init(&reader, dev, 0);
        valid = xx_pkcs12_pfx_check_is_valid(&reader.format, NULL);
        xx_pkcs12_pfx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PKCS12_PFX;
    }
    if (total_size >= 8 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U) {
        xx_iridas_cube_lut reader;
        bool valid;
        xx_iridas_cube_lut_init(&reader, dev, 0);
        valid = xx_iridas_cube_lut_check_is_valid(&reader.format, NULL);
        xx_iridas_cube_lut_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IRIDAS_CUBE_LUT;
    }
    if (total_size >= 4 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U) {
        xx_hpgl_plot reader;
        bool valid;
        xx_hpgl_plot_init(&reader, dev, 0);
        valid = xx_hpgl_plot_check_is_valid(&reader.format, NULL);
        xx_hpgl_plot_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_HPGL_PLOT;
    }
    if (total_size >= 8 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U) {
        xx_tiled_tmx reader;
        bool valid;
        xx_tiled_tmx_init(&reader, dev, 0);
        valid = xx_tiled_tmx_check_is_valid(&reader.format, NULL);
        xx_tiled_tmx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TILED_TMX;
    }
    if ((header_size>=16 && header[0]>=32 && header[0]<=126)) {
        xx_gaussian_cube reader;
        bool valid;
        xx_gaussian_cube_init(&reader, dev, 0);
        valid = xx_gaussian_cube_check_is_valid(&reader.format, NULL);
        xx_gaussian_cube_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GAUSSIAN_CUBE;
    }
    if ((header_size>=16 && header[0]>=32 && header[0]<=126)) {
        xx_molecule_xyz reader;
        bool valid;
        xx_molecule_xyz_init(&reader, dev, 0);
        valid = xx_molecule_xyz_check_is_valid(&reader.format, NULL);
        xx_molecule_xyz_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MOLECULE_XYZ;
    }
    if ((header_size>=16 && header[0]>=32 && header[0]<=126)) {
        xx_mdl_molfile reader;
        bool valid;
        xx_mdl_molfile_init(&reader, dev, 0);
        valid = xx_mdl_molfile_check_is_valid(&reader.format, NULL);
        xx_mdl_molfile_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MDL_MOLFILE;
    }
    if (((header_size >= 17 && header[0] == 64 && header[1] == 60 && header[2] == 84 && header[3] == 82 && header[4] == 73 && header[5] == 80 && header[6] == 79 && header[7] == 83 && header[8] == 62 && header[9] == 77 && header[10] == 79 && header[11] == 76 && header[12] == 69 && header[13] == 67 && header[14] == 85 && header[15] == 76 && header[16] == 69))) {
        xx_tripos_mol2 reader;
        bool valid;
        xx_tripos_mol2_init(&reader, dev, 0);
        valid = xx_tripos_mol2_check_is_valid(&reader.format, NULL);
        xx_tripos_mol2_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TRIPOS_MOL2;
    }
    if (((header_size >= 7 && header[0] == 67 && header[1] == 82 && header[2] == 89 && header[3] == 83 && header[4] == 84 && header[5] == 65 && header[6] == 76) || (header_size >= 5 && header[0] == 65 && header[1] == 84 && header[2] == 79 && header[3] == 77 && header[4] == 83) || (header_size >= 1 && header[0] == 35))) {
        xx_xcrysden_xsf reader;
        bool valid;
        xx_xcrysden_xsf_init(&reader, dev, 0);
        valid = xx_xcrysden_xsf_check_is_valid(&reader.format, NULL);
        xx_xcrysden_xsf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_XCRYSDEN_XSF;
    }
    if (((header_size >= 8 && header[0] == 37 && header[1] == 86 && header[2] == 69 && header[3] == 82 && header[4] == 83 && header[5] == 73 && header[6] == 79 && header[7] == 78))) {
        xx_amber_prmtop reader;
        bool valid;
        xx_amber_prmtop_init(&reader, dev, 0);
        valid = xx_amber_prmtop_check_is_valid(&reader.format, NULL);
        xx_amber_prmtop_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AMBER_PRMTOP;
    }
    if ((header_size>=16 && header[0]>=32 && header[0]<=126)) {
        xx_amber_restart reader;
        bool valid;
        xx_amber_restart_init(&reader, dev, 0);
        valid = xx_amber_restart_check_is_valid(&reader.format, NULL);
        xx_amber_restart_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AMBER_RESTART;
    }
    if ((header_size>=16 && header[0]>=32 && header[0]<=126)) {
        xx_gaussian_fchk reader;
        bool valid;
        xx_gaussian_fchk_init(&reader, dev, 0);
        valid = xx_gaussian_fchk_check_is_valid(&reader.format, NULL);
        xx_gaussian_fchk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GAUSSIAN_FCHK;
    }
    if ((header_size>=16 && header[0]=='#' && header[1]=='#' && (header[2]=='T' || header[2]=='J'))) {
        xx_jcamp_dx reader;
        bool valid;
        xx_jcamp_dx_init(&reader, dev, 0);
        valid = xx_jcamp_dx_check_is_valid(&reader.format, NULL);
        xx_jcamp_dx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_JCAMP_DX;
    }
    if ((header_size>=16 && header[0]>=32 && header[0]<=126)) {
        xx_dl_poly_config reader;
        bool valid;
        xx_dl_poly_config_init(&reader, dev, 0);
        valid = xx_dl_poly_config_check_is_valid(&reader.format, NULL);
        xx_dl_poly_config_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DL_POLY_CONFIG;
    }
}
