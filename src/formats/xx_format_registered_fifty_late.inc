/* Signatureless text decoding follows all specifically identified formats. */
{
    const uint8_t *header = magic;
    size_t header_size = magic_size;
    if (header_size >= 16U && ((header[0] >= 0x41U && header[0] <= 0x5AU) || (header[0] >= 0x61U && header[0] <= 0x7AU) || (header[0] >= 0x30U && header[0] <= 0x39U) || header[0] == 0x2BU || header[0] == 0x2FU)) {
        xx_base64 reader;
        bool valid;
        xx_base64_init(&reader, dev, 0);
        valid = xx_base64_check_is_valid(&reader.format, NULL);
        xx_base64_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BASE64;
    }
}
