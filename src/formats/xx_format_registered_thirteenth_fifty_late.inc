/* Reviewed bounded probes for the thirteenth group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 24 && (header[0] == 71 || header[0] == 80 || header[0] == 72 || header[0] == 68 || header[0] == 79)) {
        xx_http1_message reader;
        bool valid;
        xx_http1_message_init(&reader, dev, 0);
        valid = xx_http1_message_check_is_valid(&reader.format, NULL);
        xx_http1_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_HTTP1_MESSAGE;
    }
    if (header_size >= 8 && (header[0] & 128) && !(header[0] & 112)) {
        xx_websocket_frames reader;
        bool valid;
        xx_websocket_frames_init(&reader, dev, 0);
        valid = xx_websocket_frames_check_is_valid(&reader.format, NULL);
        xx_websocket_frames_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WEBSOCKET_FRAMES;
    }
    if (header_size >= 8 && (header[0] >> 6) == 1 && (header[0] & 15) <= 8) {
        xx_coap_message reader;
        bool valid;
        xx_coap_message_init(&reader, dev, 0);
        valid = xx_coap_message_check_is_valid(&reader.format, NULL);
        xx_coap_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_COAP_MESSAGE;
    }
    if (header_size >= 20 && header[4] == 0x21 && header[5] == 0x12 && header[6] == 0xa4 && header[7] == 0x42) {
        xx_stun_message reader;
        bool valid;
        xx_stun_message_init(&reader, dev, 0);
        valid = xx_stun_message_check_is_valid(&reader.format, NULL);
        xx_stun_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_STUN_MESSAGE;
    }
    if (header_size >= 240 && (header[0] == 1 || header[0] == 2) && header[236] == 99 && header[237] == 130 && header[238] == 83 && header[239] == 99) {
        xx_dhcp_message reader;
        bool valid;
        xx_dhcp_message_init(&reader, dev, 0);
        valid = xx_dhcp_message_check_is_valid(&reader.format, NULL);
        xx_dhcp_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DHCP_MESSAGE;
    }
    if (header_size >= 22 && header[0] >= 1 && header[0] <= 11) {
        xx_radius_packet reader;
        bool valid;
        xx_radius_packet_init(&reader, dev, 0);
        valid = xx_radius_packet_check_is_valid(&reader.format, NULL);
        xx_radius_packet_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RADIUS_PACKET;
    }
    if (header_size >= 24 && header[0] == 48) {
        xx_snmp_message reader;
        bool valid;
        xx_snmp_message_init(&reader, dev, 0);
        valid = xx_snmp_message_check_is_valid(&reader.format, NULL);
        xx_snmp_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SNMP_MESSAGE;
    }
    if (header_size >= 24 && header[0] == 48) {
        xx_ldap_message reader;
        bool valid;
        xx_ldap_message_init(&reader, dev, 0);
        valid = xx_ldap_message_check_is_valid(&reader.format, NULL);
        xx_ldap_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LDAP_MESSAGE;
    }
    if (header_size >= 32 && header[0] == 22 && header[1] == 3) {
        xx_tls_records reader;
        bool valid;
        xx_tls_records_init(&reader, dev, 0);
        valid = xx_tls_records_check_is_valid(&reader.format, NULL);
        xx_tls_records_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TLS_RECORDS;
    }
    if (header_size >= 32 && header[0] == 48) {
        xx_x509_crl reader;
        bool valid;
        xx_x509_crl_init(&reader, dev, 0);
        valid = xx_x509_crl_check_is_valid(&reader.format, NULL);
        xx_x509_crl_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_X509_CRL;
    }
    if (header_size >= 32 && header[0] == 48) {
        xx_ocsp_response reader;
        bool valid;
        xx_ocsp_response_init(&reader, dev, 0);
        valid = xx_ocsp_response_check_is_valid(&reader.format, NULL);
        xx_ocsp_response_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OCSP_RESPONSE;
    }
    if (header_size >= 24 && header[16] == 0xde && header[17] == 0xc0 && header[18] == 0xef && header[19] == 0xbe) {
        xx_lmdb_data reader;
        bool valid;
        xx_lmdb_data_init(&reader, dev, 0);
        valid = xx_lmdb_data_check_is_valid(&reader.format, NULL);
        xx_lmdb_data_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LMDB_DATA;
    }
    if (total_size >= 12 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U) {
        xx_x11_xbm reader;
        bool valid;
        xx_x11_xbm_init(&reader, dev, 0);
        valid = xx_x11_xbm_check_is_valid(&reader.format, NULL);
        xx_x11_xbm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_X11_XBM;
    }
    if (total_size >= 24 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U) {
        xx_esri_ascii_grid reader;
        bool valid;
        xx_esri_ascii_grid_init(&reader, dev, 0);
        valid = xx_esri_ascii_grid_check_is_valid(&reader.format, NULL);
        xx_esri_ascii_grid_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ESRI_ASCII_GRID;
    }
    if (total_size >= 16 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U) {
        xx_gxf_grid reader;
        bool valid;
        xx_gxf_grid_init(&reader, dev, 0);
        valid = xx_gxf_grid_check_is_valid(&reader.format, NULL);
        xx_gxf_grid_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GXF_GRID;
    }
    if (total_size >= 12 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U) {
        xx_ogc_wkt reader;
        bool valid;
        xx_ogc_wkt_init(&reader, dev, 0);
        valid = xx_ogc_wkt_check_is_valid(&reader.format, NULL);
        xx_ogc_wkt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OGC_WKT;
    }
    if (total_size >= 24 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U) {
        xx_gerber_rs274x reader;
        bool valid;
        xx_gerber_rs274x_init(&reader, dev, 0);
        valid = xx_gerber_rs274x_check_is_valid(&reader.format, NULL);
        xx_gerber_rs274x_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GERBER_RS274X;
    }
    if (total_size >= 24 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U) {
        xx_renderman_rib reader;
        bool valid;
        xx_renderman_rib_init(&reader, dev, 0);
        valid = xx_renderman_rib_check_is_valid(&reader.format, NULL);
        xx_renderman_rib_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RENDERMAN_RIB;
    }
    if ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))) {
        xx_lammps_data reader;
        bool valid;
        xx_lammps_data_init(&reader, dev, 0);
        valid = xx_lammps_data_check_is_valid(&reader.format, NULL);
        xx_lammps_data_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LAMMPS_DATA;
    }
    if ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))) {
        xx_lammps_dump reader;
        bool valid;
        xx_lammps_dump_init(&reader, dev, 0);
        valid = xx_lammps_dump_check_is_valid(&reader.format, NULL);
        xx_lammps_dump_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LAMMPS_DUMP;
    }
    if ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))) {
        xx_shelx_res reader;
        bool valid;
        xx_shelx_res_init(&reader, dev, 0);
        valid = xx_shelx_res_check_is_valid(&reader.format, NULL);
        xx_shelx_res_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SHELX_RES;
    }
    if ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))) {
        xx_turbomole_coord reader;
        bool valid;
        xx_turbomole_coord_init(&reader, dev, 0);
        valid = xx_turbomole_coord_check_is_valid(&reader.format, NULL);
        xx_turbomole_coord_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TURBOMOLE_COORD;
    }
    if ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))) {
        xx_charmm_crd reader;
        bool valid;
        xx_charmm_crd_init(&reader, dev, 0);
        valid = xx_charmm_crd_check_is_valid(&reader.format, NULL);
        xx_charmm_crd_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CHARMM_CRD;
    }
    if ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))) {
        xx_castep_cell reader;
        bool valid;
        xx_castep_cell_init(&reader, dev, 0);
        valid = xx_castep_cell_check_is_valid(&reader.format, NULL);
        xx_castep_cell_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CASTEP_CELL;
    }
    if ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))) {
        xx_crystal_fort34 reader;
        bool valid;
        xx_crystal_fort34_init(&reader, dev, 0);
        valid = xx_crystal_fort34_check_is_valid(&reader.format, NULL);
        xx_crystal_fort34_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CRYSTAL_FORT34;
    }
    if ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))) {
        xx_siesta_xv reader;
        bool valid;
        xx_siesta_xv_init(&reader, dev, 0);
        valid = xx_siesta_xv_check_is_valid(&reader.format, NULL);
        xx_siesta_xv_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SIESTA_XV;
    }
    if ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))) {
        xx_harwell_boeing reader;
        bool valid;
        xx_harwell_boeing_init(&reader, dev, 0);
        valid = xx_harwell_boeing_check_is_valid(&reader.format, NULL);
        xx_harwell_boeing_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_HARWELL_BOEING;
    }
    if ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))) {
        xx_openfoam_points reader;
        bool valid;
        xx_openfoam_points_init(&reader, dev, 0);
        valid = xx_openfoam_points_check_is_valid(&reader.format, NULL);
        xx_openfoam_points_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OPENFOAM_POINTS;
    }
}
