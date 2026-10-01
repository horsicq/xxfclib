/* Reviewed bounded probes for the fourteenth group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 48 && (header[0] >> 3 & 7) == 4 && ((header[0] & 7) == 3 || (header[0] & 7) == 4)) {
        xx_ntp_message reader;
        bool valid;
        xx_ntp_message_init(&reader, dev, 0);
        valid = xx_ntp_message_check_is_valid(&reader.format, NULL);
        xx_ntp_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NTP_MESSAGE;
    }
    if (header_size >= 24 && (header[0] >> 6) == 2 && ((header[1] >= 200 && header[1] <= 204) || ((header[1] & 127) >= 96 && (header[0] & 16) && (header[0] & 15)))) {
        xx_rtp_rtcp reader;
        bool valid;
        xx_rtp_rtcp_init(&reader, dev, 0);
        valid = xx_rtp_rtcp_check_is_valid(&reader.format, NULL);
        xx_rtp_rtcp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RTP_RTCP;
    }
    if (header_size >= 44 && header[0] == 2 && header[1] == 1) {
        xx_ospf_packet reader;
        bool valid;
        xx_ospf_packet_init(&reader, dev, 0);
        valid = xx_ospf_packet_check_is_valid(&reader.format, NULL);
        xx_ospf_packet_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OSPF_PACKET;
    }
    if (header_size >= 32 && (header[0] || header[1]) && (header[2] || header[3])) {
        xx_sctp_packet reader;
        bool valid;
        xx_sctp_packet_init(&reader, dev, 0);
        valid = xx_sctp_packet_check_is_valid(&reader.format, NULL);
        xx_sctp_packet_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SCTP_PACKET;
    }
    if (header_size >= 48 && header[17] == 16 && header[18] == 2 && header[16] == 1) {
        xx_isakmp_message reader;
        bool valid;
        xx_isakmp_message_init(&reader, dev, 0);
        valid = xx_isakmp_message_check_is_valid(&reader.format, NULL);
        xx_isakmp_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ISAKMP_MESSAGE;
    }
    if (header_size >= 32 && ((header[0] | 32) == 101) && ((header[1] | 32) == 104)) {
        xx_smtp_transcript reader;
        bool valid;
        xx_smtp_transcript_init(&reader, dev, 0);
        valid = xx_smtp_transcript_check_is_valid(&reader.format, NULL);
        xx_smtp_transcript_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SMTP_TRANSCRIPT;
    }
    if (header_size >= 48 && header[0] == 48) {
        xx_pkcs8_private_key reader;
        bool valid;
        xx_pkcs8_private_key_init(&reader, dev, 0);
        valid = xx_pkcs8_private_key_check_is_valid(&reader.format, NULL);
        xx_pkcs8_private_key_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PKCS8_PRIVATE_KEY;
    }
    if (header_size >= 64 && header[0] == 0 && header[1] == 0 && header[2] == 0 && header[3] == 32) {
        xx_openssh_certificate reader;
        bool valid;
        xx_openssh_certificate_init(&reader, dev, 0);
        valid = xx_openssh_certificate_check_is_valid(&reader.format, NULL);
        xx_openssh_certificate_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OPENSSH_CERTIFICATE;
    }
    if (header_size >= 32 && header[8] == 123) {
        xx_safetensors reader;
        bool valid;
        xx_safetensors_init(&reader, dev, 0);
        valid = xx_safetensors_check_is_valid(&reader.format, NULL);
        xx_safetensors_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SAFETENSORS;
    }
    if (header_size >= 2048) {
        xx_cdb_database reader;
        bool valid;
        xx_cdb_database_init(&reader, dev, 0);
        valid = xx_cdb_database_check_is_valid(&reader.format, NULL);
        xx_cdb_database_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CDB_DATABASE;
    }
    if (total_size >= 24 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U) {
        xx_opengex_model reader;
        bool valid;
        xx_opengex_model_init(&reader, dev, 0);
        valid = xx_opengex_model_check_is_valid(&reader.format, NULL);
        xx_opengex_model_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OPENGEX_MODEL;
    }
    if (total_size >= 12 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U) {
        xx_gts_surface reader;
        bool valid;
        xx_gts_surface_init(&reader, dev, 0);
        valid = xx_gts_surface_check_is_valid(&reader.format, NULL);
        xx_gts_surface_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GTS_SURFACE;
    }
    if (total_size >= 24 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U) {
        xx_nastran_bulk reader;
        bool valid;
        xx_nastran_bulk_init(&reader, dev, 0);
        valid = xx_nastran_bulk_check_is_valid(&reader.format, NULL);
        xx_nastran_bulk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NASTRAN_BULK;
    }
    if (total_size >= 24 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U) {
        xx_abaqus_input reader;
        bool valid;
        xx_abaqus_input_init(&reader, dev, 0);
        valid = xx_abaqus_input_check_is_valid(&reader.format, NULL);
        xx_abaqus_input_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ABAQUS_INPUT;
    }
    if (total_size >= 1024 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U) {
        xx_usgs_dem reader;
        bool valid;
        xx_usgs_dem_init(&reader, dev, 0);
        valid = xx_usgs_dem_check_is_valid(&reader.format, NULL);
        xx_usgs_dem_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_USGS_DEM;
    }
    if (((header_size >= 12 && header[0] == 76 && header[1] == 79 && header[2] == 67 && header[3] == 85 && header[4] == 83 && header[5] == 32 && header[6] == 32 && header[7] == 32 && header[8] == 32 && header[9] == 32 && header[10] == 32 && header[11] == 32))) {
        xx_genomics_genbank reader;
        bool valid;
        xx_genomics_genbank_init(&reader, dev, 0);
        valid = xx_genomics_genbank_check_is_valid(&reader.format, NULL);
        xx_genomics_genbank_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENOMICS_GENBANK;
    }
    if (((header_size >= 5 && header[0] == 73 && header[1] == 68 && header[2] == 32 && header[3] == 32 && header[4] == 32))) {
        xx_genomics_embl reader;
        bool valid;
        xx_genomics_embl_init(&reader, dev, 0);
        valid = xx_genomics_embl_check_is_valid(&reader.format, NULL);
        xx_genomics_embl_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENOMICS_EMBL;
    }
    if (((header_size >= 5 && header[0] == 73 && header[1] == 68 && header[2] == 32 && header[3] == 32 && header[4] == 32))) {
        xx_genomics_swissprot reader;
        bool valid;
        xx_genomics_swissprot_init(&reader, dev, 0);
        valid = xx_genomics_swissprot_check_is_valid(&reader.format, NULL);
        xx_genomics_swissprot_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENOMICS_SWISSPROT;
    }
    if (((header_size >= 7 && header[0] == 67 && header[1] == 76 && header[2] == 85 && header[3] == 83 && header[4] == 84 && header[5] == 65 && header[6] == 76))) {
        xx_alignment_clustal reader;
        bool valid;
        xx_alignment_clustal_init(&reader, dev, 0);
        valid = xx_alignment_clustal_check_is_valid(&reader.format, NULL);
        xx_alignment_clustal_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ALIGNMENT_CLUSTAL;
    }
    if (((header_size >= 15 && header[0] == 35 && header[1] == 32 && header[2] == 83 && header[3] == 84 && header[4] == 79 && header[5] == 67 && header[6] == 75 && header[7] == 72 && header[8] == 79 && header[9] == 76 && header[10] == 77 && header[11] == 32 && header[12] == 49 && header[13] == 46 && header[14] == 48))) {
        xx_alignment_stockholm reader;
        bool valid;
        xx_alignment_stockholm_init(&reader, dev, 0);
        valid = xx_alignment_stockholm_check_is_valid(&reader.format, NULL);
        xx_alignment_stockholm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ALIGNMENT_STOCKHOLM;
    }
    if ((header_size>=8 && (header[0]==32 || header[0]==9 || (header[0]>=48 && header[0]<=57)))) {
        xx_alignment_phylip reader;
        bool valid;
        xx_alignment_phylip_init(&reader, dev, 0);
        valid = xx_alignment_phylip_check_is_valid(&reader.format, NULL);
        xx_alignment_phylip_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ALIGNMENT_PHYLIP;
    }
    if (((header_size >= 15 && header[0] == 35 && header[1] == 35 && header[2] == 109 && header[3] == 97 && header[4] == 102 && header[5] == 32 && header[6] == 118 && header[7] == 101 && header[8] == 114 && header[9] == 115 && header[10] == 105 && header[11] == 111 && header[12] == 110 && header[13] == 61 && header[14] == 49))) {
        xx_alignment_maf reader;
        bool valid;
        xx_alignment_maf_init(&reader, dev, 0);
        valid = xx_alignment_maf_check_is_valid(&reader.format, NULL);
        xx_alignment_maf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ALIGNMENT_MAF;
    }
    if (((header_size >= 21 && header[0] == 35 && header[1] == 70 && header[2] == 111 && header[3] == 114 && header[4] == 109 && header[5] == 97 && header[6] == 116 && header[7] == 86 && header[8] == 101 && header[9] == 114 && header[10] == 115 && header[11] == 105 && header[12] == 111 && header[13] == 110 && header[14] == 32 && header[15] == 77 && header[16] == 97 && header[17] == 117 && header[18] == 118 && header[19] == 101 && header[20] == 49))) {
        xx_alignment_mauve reader;
        bool valid;
        xx_alignment_mauve_init(&reader, dev, 0);
        valid = xx_alignment_mauve_check_is_valid(&reader.format, NULL);
        xx_alignment_mauve_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ALIGNMENT_MAUVE;
    }
    if (((header_size >= 4 && header[0] == 107 && header[1] == 233 && header[2] == 61 && header[3] == 58) || (header_size >= 4 && header[0] == 58 && header[1] == 61 && header[2] == 233 && header[3] == 107))) {
        xx_ucsc_nib reader;
        bool valid;
        xx_ucsc_nib_init(&reader, dev, 0);
        valid = xx_ucsc_nib_check_is_valid(&reader.format, NULL);
        xx_ucsc_nib_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UCSC_NIB;
    }
    if ((header_size>=8 && (header[0]==35 || ((header[0]==72 || header[0]==83 || header[0]==76 || header[0]==80) && header[1]==9)))) {
        xx_assembly_gfa reader;
        bool valid;
        xx_assembly_gfa_init(&reader, dev, 0);
        valid = xx_assembly_gfa_check_is_valid(&reader.format, NULL);
        xx_assembly_gfa_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ASSEMBLY_GFA;
    }
}
