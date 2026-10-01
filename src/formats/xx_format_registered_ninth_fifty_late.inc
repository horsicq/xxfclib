/* Reviewed bounded probes for the ninth group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (total_size >= 134 && header_size >= 84U && ((uint32_t)header[80] | ((uint32_t)header[81]<<8) | ((uint32_t)header[82]<<16) | ((uint32_t)header[83]<<24)) > 0 && ((uint32_t)header[80] | ((uint32_t)header[81]<<8) | ((uint32_t)header[82]<<16) | ((uint32_t)header[83]<<24)) <= 1000000U) {
        xx_stereolithography_stl reader;
        bool valid;
        xx_stereolithography_stl_init(&reader, dev, 0);
        valid = xx_stereolithography_stl_check_is_valid(&reader.format, NULL);
        xx_stereolithography_stl_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_STEREOLITHOGRAPHY_STL;
    }
    if ((header_size >= 632 && ((header[304]==6 && header[305]==0 && header[306]==0 && header[307]==0) || (header[304]==0 && header[305]==0 && header[306]==0 && header[307]==6)))) {
        xx_seismic_sac reader;
        bool valid;
        xx_seismic_sac_init(&reader, dev, 0);
        valid = xx_seismic_sac_check_is_valid(&reader.format, NULL);
        xx_seismic_sac_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SEISMIC_SAC;
    }
    if ((header_size >= 20 && header[0]>=248 && header[1]==255 && header[2]==255 && header[3]==255)) {
        xx_mountainsort_mda reader;
        bool valid;
        xx_mountainsort_mda_init(&reader, dev, 0);
        valid = xx_mountainsort_mda_check_is_valid(&reader.format, NULL);
        xx_mountainsort_mda_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MOUNTAINSORT_MDA;
    }
    if ((header_size>=108 && ((header[16]==0 && header[17]==0 && header[18]==128 && header[19]==63) || (header[16]==63 && header[17]==128 && header[18]==0 && header[19]==0) || (header[16]==0 && header[17]==0 && header[18]==64 && header[19]==64) || (header[16]==64 && header[17]==64 && header[18]==0 && header[19]==0)))) {
        xx_microscopy_spider reader;
        bool valid;
        xx_microscopy_spider_init(&reader, dev, 0);
        valid = xx_microscopy_spider_check_is_valid(&reader.format, NULL);
        xx_microscopy_spider_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MICROSCOPY_SPIDER;
    }
}
