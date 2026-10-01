/* Reviewed bounded probes for the eleventh group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 61 && header[16] == 2) {
        xx_kafka_record_batch reader;
        bool valid;
        xx_kafka_record_batch_init(&reader, dev, 0);
        valid = xx_kafka_record_batch_check_is_valid(&reader.format, NULL);
        xx_kafka_record_batch_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_KAFKA_RECORD_BATCH;
    }
    if (header_size >= 2 && ((header[0] >= 0x81 && header[0] <= 0x9f) || header[0] == 0xdc || header[0] == 0xdd || header[0] == 0xde || header[0] == 0xdf)) {
        xx_msgpack reader;
        bool valid;
        xx_msgpack_init(&reader, dev, 0);
        valid = xx_msgpack_check_is_valid(&reader.format, NULL);
        xx_msgpack_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MSGPACK;
    }
    if (header_size >= 3 && (header[0] == '{' || header[0] == '[')) {
        xx_ubjson reader;
        bool valid;
        xx_ubjson_init(&reader, dev, 0);
        valid = xx_ubjson_check_is_valid(&reader.format, NULL);
        xx_ubjson_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UBJSON;
    }
    if (header_size >= 16 && header[0] == 'd') {
        xx_bittorrent_metainfo reader;
        bool valid;
        xx_bittorrent_metainfo_init(&reader, dev, 0);
        valid = xx_bittorrent_metainfo_check_is_valid(&reader.format, NULL);
        xx_bittorrent_metainfo_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BITTORRENT_METAINFO;
    }
    if (header_size >= 16 && !xx_rt_memcmp(header, "\0\0\0\0", 4)) {
        xx_capnproto_message reader;
        bool valid;
        xx_capnproto_message_init(&reader, dev, 0);
        valid = xx_capnproto_message_check_is_valid(&reader.format, NULL);
        xx_capnproto_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CAPNPROTO_MESSAGE;
    }
    if (header_size >= 16 && (header[0] == 'l' || header[0] == 'B') && header[1] >= 1 && header[1] <= 4 && header[3] == 1) {
        xx_dbus_message reader;
        bool valid;
        xx_dbus_message_init(&reader, dev, 0);
        valid = xx_dbus_message_check_is_valid(&reader.format, NULL);
        xx_dbus_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DBUS_MESSAGE;
    }
    if (header_size >= 16 && header[0] == 0x30) {
        xx_pkcs7_cms reader;
        bool valid;
        xx_pkcs7_cms_init(&reader, dev, 0);
        valid = xx_pkcs7_cms_check_is_valid(&reader.format, NULL);
        xx_pkcs7_cms_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PKCS7_CMS;
    }
    if (total_size >= 24 && header_size >= 24U && (header[0]==35U || header[0]==118U || header[0]==111U || header[0]==103U || header[0]==109U || header[0]==117U || header[0]==115U || header[0]==32U || header[0]==9U || header[0]==10U || header[0]==13U)) {
        xx_wavefront_obj reader;
        bool valid;
        xx_wavefront_obj_init(&reader, dev, 0);
        valid = xx_wavefront_obj_check_is_valid(&reader.format, NULL);
        xx_wavefront_obj_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WAVEFRONT_OBJ;
    }
    if (total_size >= 14 && header_size >= 10U && (header[8]==0U && (header[9]==8U || header[9]==24U))) {
        xx_alias_pix reader;
        bool valid;
        xx_alias_pix_init(&reader, dev, 0);
        valid = xx_alias_pix_check_is_valid(&reader.format, NULL);
        xx_alias_pix_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ALIAS_PIX;
    }
    if (total_size >= 90 && header_size >= 68U && (header[66]==0U || header[67]==0U)) {
        xx_font_gem_fnt reader;
        bool valid;
        xx_font_gem_fnt_init(&reader, dev, 0);
        valid = xx_font_gem_fnt_check_is_valid(&reader.format, NULL);
        xx_font_gem_fnt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FONT_GEM_FNT;
    }
    if (((header_size >= 1 && header[0] == 62))) {
        xx_genomics_fasta reader;
        bool valid;
        xx_genomics_fasta_init(&reader, dev, 0);
        valid = xx_genomics_fasta_check_is_valid(&reader.format, NULL);
        xx_genomics_fasta_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENOMICS_FASTA;
    }
    if (((header_size >= 1 && header[0] == 64))) {
        xx_genomics_fastq reader;
        bool valid;
        xx_genomics_fastq_init(&reader, dev, 0);
        valid = xx_genomics_fastq_check_is_valid(&reader.format, NULL);
        xx_genomics_fastq_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENOMICS_FASTQ;
    }
    if (((header_size >= 4 && header[0] == 64 && header[1] == 72 && header[2] == 68 && header[3] == 9))) {
        xx_genomics_sam reader;
        bool valid;
        xx_genomics_sam_init(&reader, dev, 0);
        valid = xx_genomics_sam_check_is_valid(&reader.format, NULL);
        xx_genomics_sam_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENOMICS_SAM;
    }
    if (((header_size >= 1 && header[0] == 35) || (header_size >= 7 && header[0] == 111 && header[1] == 98 && header[2] == 106 && header[3] == 101 && header[4] == 99 && header[5] == 116 && header[6] == 32))) {
        xx_opendx_field reader;
        bool valid;
        xx_opendx_field_init(&reader, dev, 0);
        valid = xx_opendx_field_check_is_valid(&reader.format, NULL);
        xx_opendx_field_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OPENDX_FIELD;
    }
    if (((header_size >= 19 && header[0] == 35 && header[1] == 35 && header[2] == 102 && header[3] == 105 && header[4] == 108 && header[5] == 101 && header[6] == 102 && header[7] == 111 && header[8] == 114 && header[9] == 109 && header[10] == 97 && header[11] == 116 && header[12] == 61 && header[13] == 86 && header[14] == 67 && header[15] == 70 && header[16] == 118 && header[17] == 52 && header[18] == 46))) {
        xx_genomics_vcf reader;
        bool valid;
        xx_genomics_vcf_init(&reader, dev, 0);
        valid = xx_genomics_vcf_check_is_valid(&reader.format, NULL);
        xx_genomics_vcf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENOMICS_VCF;
    }
    if (((header_size >= 15 && header[0] == 35 && header[1] == 35 && header[2] == 103 && header[3] == 102 && header[4] == 102 && header[5] == 45 && header[6] == 118 && header[7] == 101 && header[8] == 114 && header[9] == 115 && header[10] == 105 && header[11] == 111 && header[12] == 110 && header[13] == 32 && header[14] == 51))) {
        xx_genomics_gff3 reader;
        bool valid;
        xx_genomics_gff3_init(&reader, dev, 0);
        valid = xx_genomics_gff3_check_is_valid(&reader.format, NULL);
        xx_genomics_gff3_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GENOMICS_GFF3;
    }
    if (((header_size >= 6 && header[0] == 72 && header[1] == 69 && header[2] == 65 && header[3] == 68 && header[4] == 69 && header[5] == 82) || (header_size >= 6 && header[0] == 65 && header[1] == 84 && header[2] == 79 && header[3] == 77 && header[4] == 32 && header[5] == 32) || (header_size >= 6 && header[0] == 72 && header[1] == 69 && header[2] == 84 && header[3] == 65 && header[4] == 84 && header[5] == 77))) {
        xx_protein_pdb reader;
        bool valid;
        xx_protein_pdb_init(&reader, dev, 0);
        valid = xx_protein_pdb_check_is_valid(&reader.format, NULL);
        xx_protein_pdb_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PROTEIN_PDB;
    }
    if (((header_size >= 5 && header[0] == 100 && header[1] == 97 && header[2] == 116 && header[3] == 97 && header[4] == 95))) {
        xx_protein_mmcif reader;
        bool valid;
        xx_protein_mmcif_init(&reader, dev, 0);
        valid = xx_protein_mmcif_check_is_valid(&reader.format, NULL);
        xx_protein_mmcif_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PROTEIN_MMCIF;
    }
    if (((header_size >= 22 && header[0] == 37 && header[1] == 37 && header[2] == 77 && header[3] == 97 && header[4] == 116 && header[5] == 114 && header[6] == 105 && header[7] == 120 && header[8] == 77 && header[9] == 97 && header[10] == 114 && header[11] == 107 && header[12] == 101 && header[13] == 116 && header[14] == 32 && header[15] == 109 && header[16] == 97 && header[17] == 116 && header[18] == 114 && header[19] == 105 && header[20] == 120 && header[21] == 32))) {
        xx_matrix_market reader;
        bool valid;
        xx_matrix_market_init(&reader, dev, 0);
        valid = xx_matrix_market_check_is_valid(&reader.format, NULL);
        xx_matrix_market_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MATRIX_MARKET;
    }
    if ((header_size>=32 && header[0]>=32 && header[0]<=126)) {
        xx_gromacs_gro reader;
        bool valid;
        xx_gromacs_gro_init(&reader, dev, 0);
        valid = xx_gromacs_gro_check_is_valid(&reader.format, NULL);
        xx_gromacs_gro_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GROMACS_GRO;
    }
}
