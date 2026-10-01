/* Reviewed bounded probes for the fifteenth group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 42 && ((header[12] == 8 && header[13] == 0) || (header[12] == 134 && header[13] == 221))) {
        xx_ethernet_frame reader;
        bool valid;
        xx_ethernet_frame_init(&reader, dev, 0);
        valid = xx_ethernet_frame_check_is_valid(&reader.format, NULL);
        xx_ethernet_frame_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ETHERNET_FRAME;
    }
    if (header_size >= 28 && ((header[0] >> 4) == 4 || (header[0] >> 4) == 6)) {
        xx_ip_packet reader;
        bool valid;
        xx_ip_packet_init(&reader, dev, 0);
        valid = xx_ip_packet_check_is_valid(&reader.format, NULL);
        xx_ip_packet_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IP_PACKET;
    }
    if (header_size >= 28 && header[0] == 0 && header[1] == 1 && header[2] == 8 && header[3] == 0 && header[4] == 6 && header[5] == 4) {
        xx_arp_packet reader;
        bool valid;
        xx_arp_packet_init(&reader, dev, 0);
        valid = xx_arp_packet_check_is_valid(&reader.format, NULL);
        xx_arp_packet_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ARP_PACKET;
    }
    if (header_size >= 9 && (header[0] == 8 || header[0] == 0) && header[1] == 0) {
        xx_icmp_message reader;
        bool valid;
        xx_icmp_message_init(&reader, dev, 0);
        valid = xx_icmp_message_check_is_valid(&reader.format, NULL);
        xx_icmp_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ICMP_MESSAGE;
    }
    if (header_size >= 20 && header[0] == 1 && header[4] == 128 && header[5] == 0 && header[6] == 1 && header[7] == 1) {
        xx_diameter_message reader;
        bool valid;
        xx_diameter_message_init(&reader, dev, 0);
        valid = xx_diameter_message_check_is_valid(&reader.format, NULL);
        xx_diameter_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DIAMETER_MESSAGE;
    }
    if (header_size >= 20 && (header[0] == 192 || header[0] == 193) && header[1] == 1 && header[2] == 1 && header[3] == 1) {
        xx_tacacs_packet reader;
        bool valid;
        xx_tacacs_packet_init(&reader, dev, 0);
        valid = xx_tacacs_packet_check_is_valid(&reader.format, NULL);
        xx_tacacs_packet_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TACACS_PACKET;
    }
    if (header_size >= 36 && header[0] == 48 && header[1] == 255) {
        xx_gtp_message reader;
        bool valid;
        xx_gtp_message_init(&reader, dev, 0);
        valid = xx_gtp_message_check_is_valid(&reader.format, NULL);
        xx_gtp_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GTP_MESSAGE;
    }
    if (header_size >= 21 && header[0] == 32 && header[1] == 5) {
        xx_pfcp_message reader;
        bool valid;
        xx_pfcp_message_init(&reader, dev, 0);
        valid = xx_pfcp_message_check_is_valid(&reader.format, NULL);
        xx_pfcp_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PFCP_MESSAGE;
    }
    if (header_size >= 32 && header[0] == 0 && header[1] == 156 && header[4] == 26 && header[5] == 43 && header[6] == 60 && header[7] == 77) {
        xx_pptp_message reader;
        bool valid;
        xx_pptp_message_init(&reader, dev, 0);
        valid = xx_pptp_message_check_is_valid(&reader.format, NULL);
        xx_pptp_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PPTP_MESSAGE;
    }
    if (header_size >= 20 && header[0] == 16 && header[1] == 20 && header[6] == 0 && header[7] == 20) {
        xx_rsvp_message reader;
        bool valid;
        xx_rsvp_message_init(&reader, dev, 0);
        valid = xx_rsvp_message_check_is_valid(&reader.format, NULL);
        xx_rsvp_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RSVP_MESSAGE;
    }
    if (header_size >= 32 && header[0] == 5 && header[1] == 4) {
        xx_kerberos_ccache reader;
        bool valid;
        xx_kerberos_ccache_init(&reader, dev, 0);
        valid = xx_kerberos_ccache_check_is_valid(&reader.format, NULL);
        xx_kerberos_ccache_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_KERBEROS_CCACHE;
    }
    if (header_size >= 32 && header[0] == 101 && header[1] == 121) {
        xx_jose_jws reader;
        bool valid;
        xx_jose_jws_init(&reader, dev, 0);
        valid = xx_jose_jws_check_is_valid(&reader.format, NULL);
        xx_jose_jws_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_JOSE_JWS;
    }
    if (total_size >= 4 && header_size >= 2U && !xx_rt_memcmp(header,"\x00\x00",2U)) {
        xx_wbmp_image reader;
        bool valid;
        xx_wbmp_image_init(&reader, dev, 0);
        valid = xx_wbmp_image_check_is_valid(&reader.format, NULL);
        xx_wbmp_image_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WBMP_IMAGE;
    }
    if (total_size >= 16 && header_size >= 16U && (header[0] || header[1]) && (header[2] || header[3]) && header[0] <= 32 && header[2] <= 32 && (header[8] == 1 || header[8] == 2 || header[8] == 4 || header[8] == 8) && header[9] <= 2) {
        xx_palm_bitmap reader;
        bool valid;
        xx_palm_bitmap_init(&reader, dev, 0);
        valid = xx_palm_bitmap_check_is_valid(&reader.format, NULL);
        xx_palm_bitmap_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PALM_BITMAP;
    }
    if (total_size >= 6 && header_size >= 4U && header[0] == 0 && (header[1] == 1 || header[1] == 4) && header[2] == 0 && header[3] >= 1 && header[3] <= 16) {
        xx_adobe_acv reader;
        bool valid;
        xx_adobe_acv_init(&reader, dev, 0);
        valid = xx_adobe_acv_check_is_valid(&reader.format, NULL);
        xx_adobe_acv_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADOBE_ACV;
    }
    if (total_size >= 32 && header_size >= 1U && (header[0] == '<' || header[0] == 32 || header[0] == 9 || header[0] == 10 || header[0] == 13)) {
        xx_collada_dae reader;
        bool valid;
        xx_collada_dae_init(&reader, dev, 0);
        valid = xx_collada_dae_check_is_valid(&reader.format, NULL);
        xx_collada_dae_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_COLLADA_DAE;
    }
    if (total_size >= 1024 && header_size >= 38U && ((!xx_rt_memcmp(header,":-)",3U)) || (header[36] == 0 && header[37] == 100) || (header[36] == 100 && header[37] == 0))) {
        xx_dsn6_density reader;
        bool valid;
        xx_dsn6_density_init(&reader, dev, 0);
        valid = xx_dsn6_density_check_is_valid(&reader.format, NULL);
        xx_dsn6_density_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DSN6_DENSITY;
    }
    if (total_size >= 4 && header_size >= 1U && (header[0] == '#' || header[0] == 32 || header[0] == 9 || header[0] == 10 || header[0] == 13 || (header[0] >= '0' && header[0] <= '9'))) {
        xx_tetgen_mesh reader;
        bool valid;
        xx_tetgen_mesh_init(&reader, dev, 0);
        valid = xx_tetgen_mesh_check_is_valid(&reader.format, NULL);
        xx_tetgen_mesh_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TETGEN_MESH;
    }
    if (total_size >= 32 && header_size >= 1U && (header[0] == '$' || header[0] == 32 || header[0] == 10 || header[0] == 13)) {
        xx_qchem_input reader;
        bool valid;
        xx_qchem_input_init(&reader, dev, 0);
        valid = xx_qchem_input_check_is_valid(&reader.format, NULL);
        xx_qchem_input_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_QCHEM_INPUT;
    }
    if (header_size >= 17 && header[0] <= 2 && (header[1] || header[2]) && header[3] && header[4] && (header[15] || header[16])) {
        xx_adlib_lds reader;
        bool valid;
        xx_adlib_lds_init(&reader, dev, 0);
        valid = xx_adlib_lds_check_is_valid(&reader.format, NULL);
        xx_adlib_lds_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_LDS;
    }
    if (header_size >= 203 && header[0] == 0 && header[1] == 0 && header[2] == 4 && header[3] == 0 && header[53] <= 1) {
        xx_adlib_rol reader;
        bool valid;
        xx_adlib_rol_init(&reader, dev, 0);
        valid = xx_adlib_rol_check_is_valid(&reader.format, NULL);
        xx_adlib_rol_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADLIB_ROL;
    }
    if ((header_size>=8 && (header[0]==35 || (header[0]>=32 && header[0]<=126)))) {
        xx_genomics_bed reader;
        bool valid;
        xx_genomics_bed_init(&reader, dev, 0);
        valid = xx_genomics_bed_check_is_valid(&reader.format, NULL);
        xx_genomics_bed_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENOMICS_BED;
    }
    if ((header_size>=8 && (header[0]==35 || (header[0]>=32 && header[0]<=126)))) {
        xx_genomics_wiggle reader;
        bool valid;
        xx_genomics_wiggle_init(&reader, dev, 0);
        valid = xx_genomics_wiggle_check_is_valid(&reader.format, NULL);
        xx_genomics_wiggle_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENOMICS_WIGGLE;
    }
    if ((header_size>=8 && (header[0]==35 || (header[0]>=32 && header[0]<=126)))) {
        xx_genomics_gtf reader;
        bool valid;
        xx_genomics_gtf_init(&reader, dev, 0);
        valid = xx_genomics_gtf_check_is_valid(&reader.format, NULL);
        xx_genomics_gtf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENOMICS_GTF;
    }
    if ((header_size>=8 && (header[0]==35 || (header[0]>=32 && header[0]<=126)))) {
        xx_genomics_agp reader;
        bool valid;
        xx_genomics_agp_init(&reader, dev, 0);
        valid = xx_genomics_agp_check_is_valid(&reader.format, NULL);
        xx_genomics_agp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENOMICS_AGP;
    }
    if ((total_size == 768 || total_size == 772) && header_size >= 1U) {
        xx_adobe_act reader;
        bool valid;
        xx_adobe_act_init(&reader, dev, 0);
        valid = xx_adobe_act_check_is_valid(&reader.format, NULL);
        xx_adobe_act_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ADOBE_ACT;
    }
}
