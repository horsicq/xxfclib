/* Reviewed bounded probes for the tenth group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 32 && !xx_rt_memcmp(header, "\x4d\x69\x63\x72\x6f\x73\x6f\x66\x74\x20\x43\x2f\x43\x2b\x2b\x20\x4d\x53\x46\x20\x37\x2e\x30\x30\x0d\x0a\x1a\x44\x53\x00\x00\x00", 32)) {
        xx_microsoft_msf reader;
        bool valid;
        xx_microsoft_msf_init(&reader, dev, 0);
        valid = xx_microsoft_msf_check_is_valid(&reader.format, NULL);
        xx_microsoft_msf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MICROSOFT_MSF;
    }
    if (header_size >= 4 && !xx_rt_memcmp(header, "\x72\x65\x67\x66", 4)) {
        xx_windows_registry_hive reader;
        bool valid;
        xx_windows_registry_hive_init(&reader, dev, 0);
        valid = xx_windows_registry_hive_check_is_valid(&reader.format, NULL);
        xx_windows_registry_hive_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WINDOWS_REGISTRY_HIVE;
    }
    if (header_size >= 8 && !xx_rt_memcmp(header, "\x45\x6c\x66\x46\x69\x6c\x65\x00", 8)) {
        xx_windows_evtx reader;
        bool valid;
        xx_windows_evtx_init(&reader, dev, 0);
        valid = xx_windows_evtx_check_is_valid(&reader.format, NULL);
        xx_windows_evtx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_WINDOWS_EVTX;
    }
    if (header_size >= 8 && !xx_rt_memcmp(header, "\x62\x70\x6c\x69\x73\x74\x30\x30", 8)) {
        xx_binary_plist reader;
        bool valid;
        xx_binary_plist_init(&reader, dev, 0);
        valid = xx_binary_plist_check_is_valid(&reader.format, NULL);
        xx_binary_plist_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BINARY_PLIST;
    }
    if (header_size >= 3 && !xx_rt_memcmp(header, "\xd9\xd9\xf7", 3)) {
        xx_cbor reader;
        bool valid;
        xx_cbor_init(&reader, dev, 0);
        valid = xx_cbor_check_is_valid(&reader.format, NULL);
        xx_cbor_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CBOR;
    }
    if (header_size >= 4 && !xx_rt_memcmp(header, "\x5a\x49\x4d\x04", 4)) {
        xx_openzim reader;
        bool valid;
        xx_openzim_init(&reader, dev, 0);
        valid = xx_openzim_check_is_valid(&reader.format, NULL);
        xx_openzim_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OPENZIM;
    }
    if (header_size >= 3 && !xx_rt_memcmp(header, "\x4f\x52\x43", 3)) {
        xx_apache_orc reader;
        bool valid;
        xx_apache_orc_init(&reader, dev, 0);
        valid = xx_apache_orc_check_is_valid(&reader.format, NULL);
        xx_apache_orc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_APACHE_ORC;
    }
    if (header_size >= 4 && !xx_rt_memcmp(header, "\x53\x45\x51\x06", 4)) {
        xx_hadoop_sequencefile reader;
        bool valid;
        xx_hadoop_sequencefile_init(&reader, dev, 0);
        valid = xx_hadoop_sequencefile_check_is_valid(&reader.format, NULL);
        xx_hadoop_sequencefile_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_HADOOP_SEQUENCEFILE;
    }
    if (header_size >= 10 && !xx_rt_memcmp(header, "\xff\x06\x00\x00\x73\x4e\x61\x50\x70\x59", 10)) {
        xx_snappy_framed reader;
        bool valid;
        xx_snappy_framed_init(&reader, dev, 0);
        valid = xx_snappy_framed_check_is_valid(&reader.format, NULL);
        xx_snappy_framed_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SNAPPY_FRAMED;
    }
    if (header_size >= 2 && !xx_rt_memcmp(header, "\x5a\x56", 2)) {
        xx_lzf_stream reader;
        bool valid;
        xx_lzf_stream_init(&reader, dev, 0);
        valid = xx_lzf_stream_check_is_valid(&reader.format, NULL);
        xx_lzf_stream_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LZF_STREAM;
    }
    if (header_size >= 8 && !xx_rt_memcmp(header, "\x89\x36\x50\x4b\x0d\x0a\x1a\x0a", 8)) {
        xx_fastlz_sixpack reader;
        bool valid;
        xx_fastlz_sixpack_init(&reader, dev, 0);
        valid = xx_fastlz_sixpack_check_is_valid(&reader.format, NULL);
        xx_fastlz_sixpack_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FASTLZ_SIXPACK;
    }
    if (header_size >= 4 && !xx_rt_memcmp(header, "\x9f\xeb\x01\x00", 4)) {
        xx_linux_btf reader;
        bool valid;
        xx_linux_btf_init(&reader, dev, 0);
        valid = xx_linux_btf_check_is_valid(&reader.format, NULL);
        xx_linux_btf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LINUX_BTF;
    }
    if (header_size >= 8 && !xx_rt_memcmp(header, "\x66\x67\x62\x03\x66\x67\x62\x00", 8)) {
        xx_flatgeobuf reader;
        bool valid;
        xx_flatgeobuf_init(&reader, dev, 0);
        valid = xx_flatgeobuf_check_is_valid(&reader.format, NULL);
        xx_flatgeobuf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FLATGEOBUF;
    }
    if (total_size >= 32 && header_size >= 4U && ((header[0] == 0x13U && header[1] == 0xabU && header[2] == 0xa1U && header[3] == 0x5cU))) {
        xx_astc_texture reader;
        bool valid;
        xx_astc_texture_init(&reader, dev, 0);
        valid = xx_astc_texture_check_is_valid(&reader.format, NULL);
        xx_astc_texture_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ASTC_TEXTURE;
    }
    if (total_size >= 24 && header_size >= 6U && ((header[0] == 0x50U && header[1] == 0x4bU && header[2] == 0x4dU && header[3] == 0x20U && header[4] == 0x31U && header[5] == 0x30U) || (header[0] == 0x50U && header[1] == 0x4bU && header[2] == 0x4dU && header[3] == 0x20U && header[4] == 0x32U && header[5] == 0x30U))) {
        xx_pkm_texture reader;
        bool valid;
        xx_pkm_texture_init(&reader, dev, 0);
        valid = xx_pkm_texture_check_is_valid(&reader.format, NULL);
        xx_pkm_texture_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PKM_TEXTURE;
    }
    if (total_size >= 100 && header_size >= 2U && ((header[0] == 0x73U && header[1] == 0x42U))) {
        xx_basis_texture reader;
        bool valid;
        xx_basis_texture_init(&reader, dev, 0);
        valid = xx_basis_texture_check_is_valid(&reader.format, NULL);
        xx_basis_texture_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_BASIS_TEXTURE;
    }
    if (total_size >= 56 && header_size >= 4U && ((header[0] == 0x4fU && header[1] == 0x43U && header[2] == 0x54U && header[3] == 0x4dU))) {
        xx_openctm_mesh reader;
        bool valid;
        xx_openctm_mesh_init(&reader, dev, 0);
        valid = xx_openctm_mesh_check_is_valid(&reader.format, NULL);
        xx_openctm_mesh_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_OPENCTM_MESH;
    }
    if (total_size >= 70 && header_size >= 10U && ((header[0] == 0x53U && header[1] == 0x54U && header[2] == 0x41U && header[3] == 0x52U && header[4] == 0x54U && header[5] == 0x46U && header[6] == 0x4fU && header[7] == 0x4eU && header[8] == 0x54U && header[9] == 0x20U))) {
        xx_font_bdf reader;
        bool valid;
        xx_font_bdf_init(&reader, dev, 0);
        valid = xx_font_bdf_check_is_valid(&reader.format, NULL);
        xx_font_bdf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FONT_BDF;
    }
    if (total_size >= 68 && header_size >= 4U && ((header[0] == 0x01U && header[1] == 0x66U && header[2] == 0x63U && header[3] == 0x70U))) {
        xx_font_pcf reader;
        bool valid;
        xx_font_pcf_init(&reader, dev, 0);
        valid = xx_font_pcf_check_is_valid(&reader.format, NULL);
        xx_font_pcf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FONT_PCF;
    }
    if (total_size >= 33 && header_size >= 4U && ((header[0] == 0x36U && header[1] == 0x04U) || (header[0] == 0x72U && header[1] == 0xb5U && header[2] == 0x4aU && header[3] == 0x86U))) {
        xx_font_psf reader;
        bool valid;
        xx_font_psf_init(&reader, dev, 0);
        valid = xx_font_psf_check_is_valid(&reader.format, NULL);
        xx_font_psf_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FONT_PSF;
    }
    if (total_size >= 123 && header_size >= 2U && ((header[0] == 0x00U && header[1] == 0x02U) || (header[0] == 0x00U && header[1] == 0x03U))) {
        xx_font_windows_fnt reader;
        bool valid;
        xx_font_windows_fnt_init(&reader, dev, 0);
        valid = xx_font_windows_fnt_check_is_valid(&reader.format, NULL);
        xx_font_windows_fnt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FONT_WINDOWS_FNT;
    }
    if (total_size >= 24 && header_size >= 2U && ((header[0] == 0xf7U && header[1] == 0x59U))) {
        xx_tex_pk reader;
        bool valid;
        xx_tex_pk_init(&reader, dev, 0);
        valid = xx_tex_pk_check_is_valid(&reader.format, NULL);
        xx_tex_pk_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TEX_PK;
    }
    if (total_size >= 54 && header_size >= 2U && ((header[0] == 0xf7U && header[1] == 0x02U))) {
        xx_tex_dvi reader;
        bool valid;
        xx_tex_dvi_init(&reader, dev, 0);
        valid = xx_tex_dvi_check_is_valid(&reader.format, NULL);
        xx_tex_dvi_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_TEX_DVI;
    }
    if (total_size >= 14 && header_size >= 3U && ((header[0] == 0x50U && header[1] == 0x46U && header[2] == 0x0aU) || (header[0] == 0x50U && header[1] == 0x66U && header[2] == 0x0aU))) {
        xx_netpbm_pfm reader;
        bool valid;
        xx_netpbm_pfm_init(&reader, dev, 0);
        valid = xx_netpbm_pfm_check_is_valid(&reader.format, NULL);
        xx_netpbm_pfm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NETPBM_PFM;
    }
    if (total_size >= 76 && header_size >= 4U && ((header[0] == 0x56U && header[1] == 0x53U && header[2] == 0x54U && header[3] == 0x33U))) {
        xx_steinberg_vst3preset reader;
        bool valid;
        xx_steinberg_vst3preset_init(&reader, dev, 0);
        valid = xx_steinberg_vst3preset_check_is_valid(&reader.format, NULL);
        xx_steinberg_vst3preset_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_STEINBERG_VST3PRESET;
    }
    if (total_size >= 79 && header_size >= 4U && ((header[0] == 0x42U && header[1] == 0x4dU && header[2] == 0x46U && header[3] == 0x03U))) {
        xx_font_bmfont reader;
        bool valid;
        xx_font_bmfont_init(&reader, dev, 0);
        valid = xx_font_bmfont_check_is_valid(&reader.format, NULL);
        xx_font_bmfont_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_FONT_BMFONT;
    }
    if (header_size >= 33 && xx_rt_memcmp(header, "\x53\x4e\x45\x53\x2d\x53\x50\x43\x37\x30\x30\x20\x53\x6f\x75\x6e\x64\x20\x46\x69\x6c\x65\x20\x44\x61\x74\x61\x20\x76\x30\x2e\x33\x30", 33) == 0) {
        xx_snes_spc reader;
        bool valid;
        xx_snes_spc_init(&reader, dev, 0);
        valid = xx_snes_spc_check_is_valid(&reader.format, NULL);
        xx_snes_spc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SNES_SPC;
    }
    if (header_size >= 4 && xx_rt_memcmp(header, "\x47\x42\x53\x01", 4) == 0) {
        xx_gameboy_gbs reader;
        bool valid;
        xx_gameboy_gbs_init(&reader, dev, 0);
        valid = xx_gameboy_gbs_check_is_valid(&reader.format, NULL);
        xx_gameboy_gbs_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GAMEBOY_GBS;
    }
    if (header_size >= 5 && xx_rt_memcmp(header, "\x53\x47\x43\x1a\x01", 5) == 0) {
        xx_sega_sgc reader;
        bool valid;
        xx_sega_sgc_init(&reader, dev, 0);
        valid = xx_sega_sgc_check_is_valid(&reader.format, NULL);
        xx_sega_sgc_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SEGA_SGC;
    }
    if (header_size >= 4 && xx_rt_memcmp(header, "\x53\x39\x38\x33", 4) == 0) {
        xx_s98_log reader;
        bool valid;
        xx_s98_log_init(&reader, dev, 0);
        valid = xx_s98_log_check_is_valid(&reader.format, NULL);
        xx_s98_log_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_S98_LOG;
    }
    if (header_size >= 5 && xx_rt_memcmp(header, "\x53\x41\x50\x0d\x0a", 5) == 0) {
        xx_atari_sap reader;
        bool valid;
        xx_atari_sap_init(&reader, dev, 0);
        valid = xx_atari_sap_check_is_valid(&reader.format, NULL);
        xx_atari_sap_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ATARI_SAP;
    }
    if (header_size >= 56 && xx_rt_memcmp(header, "\x53\x43\x36\x38\x20\x4d\x75\x73\x69\x63\x2d\x66\x69\x6c\x65\x20\x2f\x20\x28\x63\x29\x20\x28\x42\x65\x4e\x29\x6a\x61\x6d\x69\x6e\x20\x47\x65\x72\x61\x72\x64\x20\x2f\x20\x53\x61\x73\x48\x69\x70\x41\x2d\x44\x65\x76\x20\x20\x00", 56) == 0) {
        xx_sc68_music reader;
        bool valid;
        xx_sc68_music_init(&reader, dev, 0);
        valid = xx_sc68_music_check_is_valid(&reader.format, NULL);
        xx_sc68_music_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SC68_MUSIC;
    }
    if (header_size >= 4 && xx_rt_memcmp(header, "\x50\x5a\x58\x54", 4) == 0) {
        xx_zx_spectrum_pzx reader;
        bool valid;
        xx_zx_spectrum_pzx_init(&reader, dev, 0);
        valid = xx_zx_spectrum_pzx_check_is_valid(&reader.format, NULL);
        xx_zx_spectrum_pzx_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ZX_SPECTRUM_PZX;
    }
    if (header_size >= 10 && xx_rt_memcmp(header, "\x55\x45\x46\x20\x46\x69\x6c\x65\x21\x00", 10) == 0) {
        xx_acorn_uef reader;
        bool valid;
        xx_acorn_uef_init(&reader, dev, 0);
        valid = xx_acorn_uef_check_is_valid(&reader.format, NULL);
        xx_acorn_uef_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ACORN_UEF;
    }
    if (header_size >= 4 && xx_rt_memcmp(header, "\x55\x4e\x49\x46", 4) == 0) {
        xx_nintendo_unif reader;
        bool valid;
        xx_nintendo_unif_init(&reader, dev, 0);
        valid = xx_nintendo_unif_check_is_valid(&reader.format, NULL);
        xx_nintendo_unif_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_UNIF;
    }
    if (header_size >= 4 && xx_rt_memcmp(header, "\x46\x44\x53\x1a", 4) == 0) {
        xx_nintendo_fds reader;
        bool valid;
        xx_nintendo_fds_init(&reader, dev, 0);
        valid = xx_nintendo_fds_check_is_valid(&reader.format, NULL);
        xx_nintendo_fds_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NINTENDO_FDS;
    }
    if (((header_size >= 4 && header[0] == 38 && header[1] == 252 && header[2] == 143 && header[3] == 136) || (header_size >= 4 && header[0] == 136 && header[1] == 143 && header[2] == 252 && header[3] == 38))) {
        xx_ucsc_bigwig reader;
        bool valid;
        xx_ucsc_bigwig_init(&reader, dev, 0);
        valid = xx_ucsc_bigwig_check_is_valid(&reader.format, NULL);
        xx_ucsc_bigwig_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UCSC_BIGWIG;
    }
    if (((header_size >= 4 && header[0] == 235 && header[1] == 242 && header[2] == 137 && header[3] == 135) || (header_size >= 4 && header[0] == 135 && header[1] == 137 && header[2] == 242 && header[3] == 235))) {
        xx_ucsc_bigbed reader;
        bool valid;
        xx_ucsc_bigbed_init(&reader, dev, 0);
        valid = xx_ucsc_bigbed_check_is_valid(&reader.format, NULL);
        xx_ucsc_bigbed_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UCSC_BIGBED;
    }
    if (((header_size >= 6 && header[0] == 35 && header[1] == 78 && header[2] == 69 && header[3] == 88 && header[4] == 85 && header[5] == 83) || (header_size >= 6 && header[0] == 35 && header[1] == 110 && header[2] == 101 && header[3] == 120 && header[4] == 117 && header[5] == 115))) {
        xx_phylo_nexus reader;
        bool valid;
        xx_phylo_nexus_init(&reader, dev, 0);
        valid = xx_phylo_nexus_check_is_valid(&reader.format, NULL);
        xx_phylo_nexus_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PHYLO_NEXUS;
    }
    if (((header_size >= 8 && header[0] == 217 && header[1] == 213 && header[2] == 5 && header[3] == 249 && header[4] == 32 && header[5] == 161 && header[6] == 99 && header[7] == 215))) {
        xx_sqlite_rollback_journal reader;
        bool valid;
        xx_sqlite_rollback_journal_init(&reader, dev, 0);
        valid = xx_sqlite_rollback_journal_check_is_valid(&reader.format, NULL);
        xx_sqlite_rollback_journal_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SQLITE_ROLLBACK_JOURNAL;
    }
    if (((header_size >= 11 && header[0] == 86 && header[1] == 101 && header[2] == 114 && header[3] == 115 && header[4] == 105 && header[5] == 111 && header[6] == 110 && header[7] == 32 && header[8] == 51 && header[9] == 46 && header[10] == 48))) {
        xx_neuroscan_cnt reader;
        bool valid;
        xx_neuroscan_cnt_init(&reader, dev, 0);
        valid = xx_neuroscan_cnt_check_is_valid(&reader.format, NULL);
        xx_neuroscan_cnt_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NEUROSCAN_CNT;
    }
    if (((header_size >= 11 && header[0] == 116 && header[1] == 114 && header[2] == 105 && header[3] == 97 && header[4] == 108 && header[5] == 95 && header[6] == 100 && header[7] == 97 && header[8] == 116 && header[9] == 101 && header[10] == 32))) {
        xx_axona_tetrode reader;
        bool valid;
        xx_axona_tetrode_init(&reader, dev, 0);
        valid = xx_axona_tetrode_check_is_valid(&reader.format, NULL);
        xx_axona_tetrode_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AXONA_TETRODE;
    }
    if (((header_size >= 2 && header[0] == 128 && header[1] == 2) || (header_size >= 2 && header[0] == 128 && header[1] == 3) || (header_size >= 2 && header[0] == 128 && header[1] == 4) || (header_size >= 2 && header[0] == 128 && header[1] == 5))) {
        xx_python_pickle reader;
        bool valid;
        xx_python_pickle_init(&reader, dev, 0);
        valid = xx_python_pickle_check_is_valid(&reader.format, NULL);
        xx_python_pickle_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PYTHON_PICKLE;
    }
    if (((header_size >= 14 && header[0] == 35 && header[1] == 33 && header[2] == 65 && header[3] == 69 && header[4] == 82 && header[5] == 45 && header[6] == 68 && header[7] == 65 && header[8] == 84 && header[9] == 50 && header[10] == 46 && header[11] == 48 && header[12] == 13 && header[13] == 10) || (header_size >= 13 && header[0] == 35 && header[1] == 33 && header[2] == 65 && header[3] == 69 && header[4] == 82 && header[5] == 45 && header[6] == 68 && header[7] == 65 && header[8] == 84 && header[9] == 50 && header[10] == 46 && header[11] == 48 && header[12] == 10))) {
        xx_inivation_aedat reader;
        bool valid;
        xx_inivation_aedat_init(&reader, dev, 0);
        valid = xx_inivation_aedat_check_is_valid(&reader.format, NULL);
        xx_inivation_aedat_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_INIVATION_AEDAT;
    }
}
