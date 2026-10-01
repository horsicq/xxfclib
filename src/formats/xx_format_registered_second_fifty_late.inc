/* Reviewed bounded probes for the second group of fifty readers. */
{
    uint8_t header[512] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 8U && ((header[0] >= 48 && header[0] <= 57) || (header[0] >= 97 && header[0] <= 102) || (header[0] >= 65 && header[0] <= 70) || header[0] == 32 || header[0] == 9 || header[0] == 13 || header[0] == 10)) {
        xx_base16 reader;
        bool valid;
        xx_base16_init(&reader, dev, 0);
        valid = xx_base16_check_is_valid(&reader.format, NULL);
        xx_base16_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BASE16;
    }
    if (header_size >= 16U && (header[0] != 0 || header[1] != 0) && (uint64_t)(((uint32_t)header[2] << 24U) | ((uint32_t)header[3] << 16U) | ((uint32_t)header[4] << 8U) | header[5]) <= (uint64_t)total_size && (((uint32_t)header[2] << 24U) | ((uint32_t)header[3] << 16U) | ((uint32_t)header[4] << 8U) | header[5]) >= 6U + (((uint32_t)header[0] << 8U) | header[1]) * 10U) {
        xx_sar_ns reader;
        bool valid;
        xx_sar_ns_init(&reader, dev, 0);
        valid = xx_sar_ns_check_is_valid(&reader.format, NULL);
        xx_sar_ns_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SAR_NS;
    }
    if (header_size >= 4U && (xx_rt_memcmp(header + 0U, "\x52\x49\x46\x46", 4U) == 0)) {
        xx_riff reader;
        bool valid;
        xx_riff_init(&reader, dev, 0);
        valid = xx_riff_check_is_valid(&reader.format, NULL);
        xx_riff_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RIFF;
    }
}
