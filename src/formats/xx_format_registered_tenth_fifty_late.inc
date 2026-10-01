/* Reviewed bounded probes for the tenth group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 5) {
        xx_mongodb_bson reader;
        bool valid;
        xx_mongodb_bson_init(&reader, dev, 0);
        valid = xx_mongodb_bson_check_is_valid(&reader.format, NULL);
        xx_mongodb_bson_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MONGODB_BSON;
    }
    if (header_size >= 48) {
        xx_leveldb_sstable reader;
        bool valid;
        xx_leveldb_sstable_init(&reader, dev, 0);
        valid = xx_leveldb_sstable_check_is_valid(&reader.format, NULL);
        xx_leveldb_sstable_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LEVELDB_SSTABLE;
    }
    if (total_size >= 52 && header_size >= 24U && total_size == ((uint32_t)header[0]*256U + header[1])*4U && ((uint32_t)header[2]*256U + header[3]) >= 2U) {
        xx_tex_tfm reader;
        bool valid;
        xx_tex_tfm_init(&reader, dev, 0);
        valid = xx_tex_tfm_check_is_valid(&reader.format, NULL);
        xx_tex_tfm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TEX_TFM;
    }
    if (total_size >= 62 && header_size >= 24U && header[4] == 0U && header[5] == 0U && header[6] == 0U && header[7] == 11U && header[0] == 0U && header[1] == 0U) {
        xx_processing_vlw reader;
        bool valid;
        xx_processing_vlw_init(&reader, dev, 0);
        valid = xx_processing_vlw_check_is_valid(&reader.format, NULL);
        xx_processing_vlw_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PROCESSING_VLW;
    }
    if ((header_size>=6 && (header[0]=='(' || header[0]=='['))) {
        xx_phylo_newick reader;
        bool valid;
        xx_phylo_newick_init(&reader, dev, 0);
        valid = xx_phylo_newick_check_is_valid(&reader.format, NULL);
        xx_phylo_newick_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PHYLO_NEWICK;
    }
    if ((header_size>=10 && (header[0]==219 || header[0]==251 || header[0]==169 || header[0]==168 || header[0]==188 || header[0]==190))) {
        xx_python_marshal reader;
        bool valid;
        xx_python_marshal_init(&reader, dev, 0);
        valid = xx_python_marshal_check_is_valid(&reader.format, NULL);
        xx_python_marshal_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PYTHON_MARSHAL;
    }
}
