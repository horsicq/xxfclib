/* Reviewed bounded probes for the sixteenth group of fifty readers. */
{
    uint8_t header[4100] = {0};
    size_t header_size = total_size < (int64_t)sizeof(header) ? (size_t)total_size : sizeof(header);
    if (xx_io_seek64(dev, 0, SEEK_SET) != 0 ||
        xx_io_read(dev, header, header_size) != (ssize_t)header_size) header_size = 0;
    (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
    if (header_size >= 9 && header[2] == 0 && header[3] == 0 && header[6] > 0 && header[6] < 248 && (header[7] == 3 || header[7] == 5 || header[7] == 6 || header[7] == 16 || (header[7] >= 129 && header[7] <= 144))) {
        xx_modbus_tcp reader;
        bool valid;
        xx_modbus_tcp_init(&reader, dev, 0);
        valid = xx_modbus_tcp_check_is_valid(&reader.format, NULL);
        xx_modbus_tcp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MODBUS_TCP;
    }
    if (header_size >= 17 && header[12] == 1 && header[13] > 0 && (header[14] <= 2 || header[14] == 128 || header[14] == 129)) {
        xx_someip_message reader;
        bool valid;
        xx_someip_message_init(&reader, dev, 0);
        valid = xx_someip_message_check_is_valid(&reader.format, NULL);
        xx_someip_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_SOMEIP_MESSAGE;
    }
    if (header_size >= 24 && header[0] == 2 && header[1] == 2 && header[2] == 0 && header[3] == 0) {
        xx_rip_message reader;
        bool valid;
        xx_rip_message_init(&reader, dev, 0);
        valid = xx_rip_message_check_is_valid(&reader.format, NULL);
        xx_rip_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_RIP_MESSAGE;
    }
    if (header_size >= 20 && header[0] == 33 && header[1] > 0 && header[3] > 0 && header[4] == 0) {
        xx_vrrp_message reader;
        bool valid;
        xx_vrrp_message_init(&reader, dev, 0);
        valid = xx_vrrp_message_check_is_valid(&reader.format, NULL);
        xx_vrrp_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VRRP_MESSAGE;
    }
    if (header_size >= 16 && header[0] == 34 && header[1] == 0 && header[4] == 0 && header[5] == 0) {
        xx_igmp_message reader;
        bool valid;
        xx_igmp_message_init(&reader, dev, 0);
        valid = xx_igmp_message_check_is_valid(&reader.format, NULL);
        xx_igmp_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IGMP_MESSAGE;
    }
    if (header_size >= 10 && header[0] == 32 && header[1] == 0) {
        xx_pim_message reader;
        bool valid;
        xx_pim_message_init(&reader, dev, 0);
        valid = xx_pim_message_check_is_valid(&reader.format, NULL);
        xx_pim_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_PIM_MESSAGE;
    }
    if (header_size >= 26 && header[0] == 0 && header[1] == 1 && header[8] == 0 && header[9] == 0) {
        xx_ldp_message reader;
        bool valid;
        xx_ldp_message_init(&reader, dev, 0);
        valid = xx_ldp_message_check_is_valid(&reader.format, NULL);
        xx_ldp_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LDP_MESSAGE;
    }
    if (header_size >= 32 && (header[0] & 79) == 0 && header[1] == 0 && ((header[2] == 8 && header[3] == 0) || (header[2] == 134 && header[3] == 221))) {
        xx_gre_packet reader;
        bool valid;
        xx_gre_packet_init(&reader, dev, 0);
        valid = xx_gre_packet_check_is_valid(&reader.format, NULL);
        xx_gre_packet_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GRE_PACKET;
    }
    if (header_size >= 52 && header[0] == 200 && header[1] == 2) {
        xx_l2tp_packet reader;
        bool valid;
        xx_l2tp_packet_init(&reader, dev, 0);
        valid = xx_l2tp_packet_check_is_valid(&reader.format, NULL);
        xx_l2tp_packet_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_L2TP_PACKET;
    }
    if (header_size >= 14 && (header[0] & 254) == 2) {
        xx_lldp_message reader;
        bool valid;
        xx_lldp_message_init(&reader, dev, 0);
        valid = xx_lldp_message_check_is_valid(&reader.format, NULL);
        xx_lldp_message_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_LLDP_MESSAGE;
    }
    if (header_size >= 72 && header[0] == 0 && header[1] == 5) {
        xx_netflow_datagram reader;
        bool valid;
        xx_netflow_datagram_init(&reader, dev, 0);
        valid = xx_netflow_datagram_check_is_valid(&reader.format, NULL);
        xx_netflow_datagram_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NETFLOW_DATAGRAM;
    }
    if (header_size >= 24 && header[0] == 5 && header[1] == 0 && (header[2] == 0 || header[2] == 2 || header[2] == 11) && header[3] == 3) {
        xx_dcerpc_pdu reader;
        bool valid;
        xx_dcerpc_pdu_init(&reader, dev, 0);
        valid = xx_dcerpc_pdu_check_is_valid(&reader.format, NULL);
        xx_dcerpc_pdu_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DCERPC_PDU;
    }
    if (header_size >= 58 && header[0] >= 248 && header[0] <= 251) {
        xx_ethereum_rlp reader;
        bool valid;
        xx_ethereum_rlp_init(&reader, dev, 0);
        valid = xx_ethereum_rlp_check_is_valid(&reader.format, NULL);
        xx_ethereum_rlp_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ETHEREUM_RLP;
    }
    if (total_size >= 12 && header_size >= 8 && header[0]==0 && header[1]==0 && (header[2]||header[3]) && header[2]<=32 && (header[2]!=32||header[3]==0) && header[4]==0 && header[5]==0 && (header[6]||header[7]) && header[6]<=15) {
        xx_avs_image reader;
        bool valid;
        xx_avs_image_init(&reader, dev, 0);
        valid = xx_avs_image_check_is_valid(&reader.format, NULL);
        xx_avs_image_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AVS_IMAGE;
    }
    if (total_size >= 7 && header_size >= 5 && header[0]>='1' && header[0]<='9') {
        xx_mtv_image reader;
        bool valid;
        xx_mtv_image_init(&reader, dev, 0);
        valid = xx_mtv_image_check_is_valid(&reader.format, NULL);
        xx_mtv_image_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_MTV_IMAGE;
    }
    if (total_size >= 5 && header_size >= 5 && ((header[0]==0 && header[1] && header[2] && header[3]==1)||(header_size>=6 && header[0]==16 && (header[1]||header[2]) && header[1]<=32 && (header[3]||header[4]) && header[3]<=15 && header[5]==1))) {
        xx_nokia_ota_bitmap reader;
        bool valid;
        xx_nokia_ota_bitmap_init(&reader, dev, 0);
        valid = xx_nokia_ota_bitmap_check_is_valid(&reader.format, NULL);
        xx_nokia_ota_bitmap_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NOKIA_OTA_BITMAP;
    }
    if (total_size >= 124 && ((header_size>=14 && !xx_rt_memcmp(header+10,"\x00\x11\x02\xff",4))||(header_size>=526 && total_size>=636 && !xx_rt_memcmp(header,"\0\0\0\0\0\0\0\0\0\0\0\0\0\0",14) && !xx_rt_memcmp(header+522,"\x00\x11\x02\xff",4)))) {
        xx_apple_pict reader;
        bool valid;
        xx_apple_pict_init(&reader, dev, 0);
        valid = xx_apple_pict_check_is_valid(&reader.format, NULL);
        xx_apple_pict_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_APPLE_PICT;
    }
    if (total_size >= 405 && header_size >= 80 && header[72]=='S' && header[79]=='1') {
        xx_iges_model reader;
        bool valid;
        xx_iges_model_init(&reader, dev, 0);
        valid = xx_iges_model_check_is_valid(&reader.format, NULL);
        xx_iges_model_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IGES_MODEL;
    }
    if (total_size >= 32 && header_size >= 1 && (header[0]=='<'||header[0]==32||header[0]==9||header[0]==10||header[0]==13)) {
        xx_ufo_glif reader;
        bool valid;
        xx_ufo_glif_init(&reader, dev, 0);
        valid = xx_ufo_glif_check_is_valid(&reader.format, NULL);
        xx_ufo_glif_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UFO_GLIF;
    }
    if (total_size >= 38 && header_size >= 7 && ((header[0]>='0'&&header[0]<='9')||(header[0]>='A'&&header[0]<='F')||(header[0]>='a'&&header[0]<='f')) && ((header[1]>='0'&&header[1]<='9')||(header[1]>='A'&&header[1]<='F')||(header[1]>='a'&&header[1]<='f')) && ((header[2]>='0'&&header[2]<='9')||(header[2]>='A'&&header[2]<='F')||(header[2]>='a'&&header[2]<='f')) && ((header[3]>='0'&&header[3]<='9')||(header[3]>='A'&&header[3]<='F')||(header[3]>='a'&&header[3]<='f')) && (header[4]==':'||(((header[4]>='0'&&header[4]<='9')||(header[4]>='A'&&header[4]<='F')||(header[4]>='a'&&header[4]<='f'))&&header[5]==':')||(((header[4]>='0'&&header[4]<='9')||(header[4]>='A'&&header[4]<='F')||(header[4]>='a'&&header[4]<='f'))&&((header[5]>='0'&&header[5]<='9')||(header[5]>='A'&&header[5]<='F')||(header[5]>='a'&&header[5]<='f'))&&header[6]==':'))) {
        xx_unifont_hex reader;
        bool valid;
        xx_unifont_hex_init(&reader, dev, 0);
        valid = xx_unifont_hex_check_is_valid(&reader.format, NULL);
        xx_unifont_hex_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_UNIFONT_HEX;
    }
    if ((header_size>=82 && header[16]>0 && header[16]<=240 && header[32]<=9 && header[48]==0 && header[49]==0 && header[64]<=63 && (header[80]||header[81]))) {
        xx_ken_ksm reader;
        bool valid;
        xx_ken_ksm_init(&reader, dev, 0);
        valid = xx_ken_ksm_check_is_valid(&reader.format, NULL);
        xx_ken_ksm_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_KEN_KSM;
    }
    if ((header_size>=70 && header[0]==1 && header[1]==0 && header[2]==0 && header[3]==0 && header[4]==0 && header[5]==0 && header[36]>0 && header[37]>0)) {
        xx_implay_music reader;
        bool valid;
        xx_implay_music_init(&reader, dev, 0);
        valid = xx_implay_music_check_is_valid(&reader.format, NULL);
        xx_implay_music_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_IMPLAY_MUSIC;
    }
    if ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))) {
        xx_vasp_poscar reader;
        bool valid;
        xx_vasp_poscar_init(&reader, dev, 0);
        valid = xx_vasp_poscar_check_is_valid(&reader.format, NULL);
        xx_vasp_poscar_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_VASP_POSCAR;
    }
    if ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))) {
        xx_quantum_espresso_input reader;
        bool valid;
        xx_quantum_espresso_input_init(&reader, dev, 0);
        valid = xx_quantum_espresso_input_check_is_valid(&reader.format, NULL);
        xx_quantum_espresso_input_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_QUANTUM_ESPRESSO_INPUT;
    }
    if ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))) {
        xx_cp2k_input reader;
        bool valid;
        xx_cp2k_input_init(&reader, dev, 0);
        valid = xx_cp2k_input_check_is_valid(&reader.format, NULL);
        xx_cp2k_input_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_CP2K_INPUT;
    }
    if ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))) {
        xx_nwchem_input reader;
        bool valid;
        xx_nwchem_input_init(&reader, dev, 0);
        valid = xx_nwchem_input_check_is_valid(&reader.format, NULL);
        xx_nwchem_input_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_NWCHEM_INPUT;
    }
    if ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))) {
        xx_gamess_input reader;
        bool valid;
        xx_gamess_input_init(&reader, dev, 0);
        valid = xx_gamess_input_check_is_valid(&reader.format, NULL);
        xx_gamess_input_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GAMESS_INPUT;
    }
    if ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))) {
        xx_gaussian_input reader;
        bool valid;
        xx_gaussian_input_init(&reader, dev, 0);
        valid = xx_gaussian_input_check_is_valid(&reader.format, NULL);
        xx_gaussian_input_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_GAUSSIAN_INPUT;
    }
    if ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))) {
        xx_abinit_input reader;
        bool valid;
        xx_abinit_input_init(&reader, dev, 0);
        valid = xx_abinit_input_check_is_valid(&reader.format, NULL);
        xx_abinit_input_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ABINIT_INPUT;
    }
    if ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))) {
        xx_aims_geometry reader;
        bool valid;
        xx_aims_geometry_init(&reader, dev, 0);
        valid = xx_aims_geometry_check_is_valid(&reader.format, NULL);
        xx_aims_geometry_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_AIMS_GEOMETRY;
    }
    if ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))) {
        xx_orca_input reader;
        bool valid;
        xx_orca_input_init(&reader, dev, 0);
        valid = xx_orca_input_check_is_valid(&reader.format, NULL);
        xx_orca_input_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_ORCA_INPUT;
    }
    if ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))) {
        xx_demon_input reader;
        bool valid;
        xx_demon_input_init(&reader, dev, 0);
        valid = xx_demon_input_check_is_valid(&reader.format, NULL);
        xx_demon_input_destroy(&reader);
        (void)xx_io_seek64(dev, orig_pos, SEEK_SET);
        if (valid) return XX_FILE_TYPE_DEMON_INPUT;
    }
}
