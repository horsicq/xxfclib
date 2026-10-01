/* Reviewed bounded probes for the sixth group of fifty readers. */
{
    uint8_t header[512] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (total_size >= 67584) {
        xx_xbox_xdvdfs reader;
        bool valid;
        xx_xbox_xdvdfs_init(&reader, dev, 0);
        valid = xx_xbox_xdvdfs_check_is_valid(&reader.format, NULL);
        xx_xbox_xdvdfs_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_XBOX_XDVDFS;
    }
    if (((header_size >= 4 && header[0] == 0 && header[1] == 0 && header[2] == 0 && header[3] == 1))) {
        xx_freesurfer_mgh reader;
        bool valid;
        xx_freesurfer_mgh_init(&reader, dev, 0);
        valid = xx_freesurfer_mgh_check_is_valid(&reader.format, NULL);
        xx_freesurfer_mgh_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FREESURFER_MGH;
    }
    if (((header_size >= 16 && header[4]==0 && header[5]==0 && header[6]==0 && header[7]==0))) {
        xx_tensorflow_tfrecord reader;
        bool valid;
        xx_tensorflow_tfrecord_init(&reader, dev, 0);
        valid = xx_tensorflow_tfrecord_check_is_valid(&reader.format, NULL);
        xx_tensorflow_tfrecord_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TENSORFLOW_TFRECORD;
    }
}
