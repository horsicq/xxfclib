/* Reviewed bounded probes for the seventh group of fifty readers. */
{
    uint8_t header[512] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if ((header_size >= 20 && total_size >= 22)) {
        xx_matlab_mat4 reader;
        bool valid;
        xx_matlab_mat4_init(&reader, dev, 0);
        valid = xx_matlab_mat4_check_is_valid(&reader.format, NULL);
        xx_matlab_mat4_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MATLAB_MAT4;
    }
    if ((header_size >= 80 && total_size >= 3841)) {
        xx_seismic_segy reader;
        bool valid;
        xx_seismic_segy_init(&reader, dev, 0);
        valid = xx_seismic_segy_check_is_valid(&reader.format, NULL);
        xx_seismic_segy_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SEISMIC_SEGY;
    }
}
