/* Reviewed bounded probes for the fifth group of fifty readers. */
{
    uint8_t header[512] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (((header_size >= 1 && header[0] == 3))) {
        xx_dbase_dbf reader;
        bool valid;
        xx_dbase_dbf_init(&reader, dev, 0);
        valid = xx_dbase_dbf_check_is_valid(&reader.format, NULL);
        xx_dbase_dbf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DBASE_DBF;
    }
}
