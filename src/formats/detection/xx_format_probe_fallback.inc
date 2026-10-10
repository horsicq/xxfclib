/* SPDX-License-Identifier: MIT
 * Weak signatures after specifically identified formats.
 * Ordered X-macro source: keep predicate and header boundaries in order.
 */

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(ntp_message, XX_FILE_TYPE_NTP_MESSAGE,
    (header_size >= 48 && (header[0] >> 3 & 7) == 4 && ((header[0] & 7) == 3 || (header[0] & 7) == 4)))
XX_FORMAT_READER_PROBE(rtp_rtcp, XX_FILE_TYPE_RTP_RTCP,
    (header_size >= 24 && (header[0] >> 6) == 2 && ((header[1] >= 200 && header[1] <= 204) || ((header[1] & 127) >= 96 && (header[0] & 16) && (header[0] & 15)))))
XX_FORMAT_READER_PROBE(ospf_packet, XX_FILE_TYPE_OSPF_PACKET,
    (header_size >= 44 && header[0] == 2 && header[1] == 1))
XX_FORMAT_READER_PROBE(sctp_packet, XX_FILE_TYPE_SCTP_PACKET,
    (header_size >= 32 && (header[0] || header[1]) && (header[2] || header[3])))
XX_FORMAT_READER_PROBE(isakmp_message, XX_FILE_TYPE_ISAKMP_MESSAGE,
    (header_size >= 48 && header[17] == 16 && header[18] == 2 && header[16] == 1))
XX_FORMAT_READER_PROBE(smtp_transcript, XX_FILE_TYPE_SMTP_TRANSCRIPT,
    (header_size >= 32 && ((header[0] | 32) == 101) && ((header[1] | 32) == 104)))
XX_FORMAT_READER_PROBE(pkcs8_private_key, XX_FILE_TYPE_PKCS8_PRIVATE_KEY,
    (header_size >= 48 && header[0] == 48))
XX_FORMAT_READER_PROBE(openssh_certificate, XX_FILE_TYPE_OPENSSH_CERTIFICATE,
    (header_size >= 64 && header[0] == 0 && header[1] == 0 && header[2] == 0 && header[3] == 32))
XX_FORMAT_READER_PROBE(safetensors, XX_FILE_TYPE_SAFETENSORS,
    (header_size >= 32 && header[8] == 123))
XX_FORMAT_READER_PROBE(cdb_database, XX_FILE_TYPE_CDB_DATABASE,
    (header_size >= 2048))
XX_FORMAT_READER_PROBE(opengex_model, XX_FILE_TYPE_OPENGEX_MODEL,
    (total_size >= 24 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U))
XX_FORMAT_READER_PROBE(gts_surface, XX_FILE_TYPE_GTS_SURFACE,
    (total_size >= 12 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U))
XX_FORMAT_READER_PROBE(nastran_bulk, XX_FILE_TYPE_NASTRAN_BULK,
    (total_size >= 24 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U))
XX_FORMAT_READER_PROBE(abaqus_input, XX_FILE_TYPE_ABAQUS_INPUT,
    (total_size >= 24 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U))
XX_FORMAT_READER_PROBE(usgs_dem, XX_FILE_TYPE_USGS_DEM,
    (total_size >= 1024 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U))
XX_FORMAT_READER_PROBE(genomics_genbank, XX_FILE_TYPE_GENOMICS_GENBANK,
    (((header_size >= 12 && header[0] == 76 && header[1] == 79 && header[2] == 67 && header[3] == 85 && header[4] == 83 && header[5] == 32 && header[6] == 32 && header[7] == 32 && header[8] == 32 && header[9] == 32 && header[10] == 32 && header[11] == 32))))
XX_FORMAT_READER_PROBE(genomics_embl, XX_FILE_TYPE_GENOMICS_EMBL,
    (((header_size >= 5 && header[0] == 73 && header[1] == 68 && header[2] == 32 && header[3] == 32 && header[4] == 32))))
XX_FORMAT_READER_PROBE(genomics_swissprot, XX_FILE_TYPE_GENOMICS_SWISSPROT,
    (((header_size >= 5 && header[0] == 73 && header[1] == 68 && header[2] == 32 && header[3] == 32 && header[4] == 32))))
XX_FORMAT_READER_PROBE(alignment_clustal, XX_FILE_TYPE_ALIGNMENT_CLUSTAL,
    (((header_size >= 7 && header[0] == 67 && header[1] == 76 && header[2] == 85 && header[3] == 83 && header[4] == 84 && header[5] == 65 && header[6] == 76))))
XX_FORMAT_READER_PROBE(alignment_stockholm, XX_FILE_TYPE_ALIGNMENT_STOCKHOLM,
    (((header_size >= 15 && header[0] == 35 && header[1] == 32 && header[2] == 83 && header[3] == 84 && header[4] == 79 && header[5] == 67 && header[6] == 75 && header[7] == 72 && header[8] == 79 && header[9] == 76 && header[10] == 77 && header[11] == 32 && header[12] == 49 && header[13] == 46 && header[14] == 48))))
XX_FORMAT_READER_PROBE(alignment_phylip, XX_FILE_TYPE_ALIGNMENT_PHYLIP,
    ((header_size>=8 && (header[0]==32 || header[0]==9 || (header[0]>=48 && header[0]<=57)))))
XX_FORMAT_READER_PROBE(alignment_maf, XX_FILE_TYPE_ALIGNMENT_MAF,
    (((header_size >= 15 && header[0] == 35 && header[1] == 35 && header[2] == 109 && header[3] == 97 && header[4] == 102 && header[5] == 32 && header[6] == 118 && header[7] == 101 && header[8] == 114 && header[9] == 115 && header[10] == 105 && header[11] == 111 && header[12] == 110 && header[13] == 61 && header[14] == 49))))
XX_FORMAT_READER_PROBE(alignment_mauve, XX_FILE_TYPE_ALIGNMENT_MAUVE,
    (((header_size >= 21 && header[0] == 35 && header[1] == 70 && header[2] == 111 && header[3] == 114 && header[4] == 109 && header[5] == 97 && header[6] == 116 && header[7] == 86 && header[8] == 101 && header[9] == 114 && header[10] == 115 && header[11] == 105 && header[12] == 111 && header[13] == 110 && header[14] == 32 && header[15] == 77 && header[16] == 97 && header[17] == 117 && header[18] == 118 && header[19] == 101 && header[20] == 49))))
XX_FORMAT_READER_PROBE(ucsc_nib, XX_FILE_TYPE_UCSC_NIB,
    (((header_size >= 4 && header[0] == 107 && header[1] == 233 && header[2] == 61 && header[3] == 58) || (header_size >= 4 && header[0] == 58 && header[1] == 61 && header[2] == 233 && header[3] == 107))))
XX_FORMAT_READER_PROBE(assembly_gfa, XX_FILE_TYPE_ASSEMBLY_GFA,
    ((header_size>=8 && (header[0]==35 || ((header[0]==72 || header[0]==83 || header[0]==76 || header[0]==80) && header[1]==9)))))

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(http1_message, XX_FILE_TYPE_HTTP1_MESSAGE,
    (header_size >= 24 && (header[0] == 71 || header[0] == 80 || header[0] == 72 || header[0] == 68 || header[0] == 79)))
XX_FORMAT_READER_PROBE(websocket_frames, XX_FILE_TYPE_WEBSOCKET_FRAMES,
    (header_size >= 8 && (header[0] & 128) && !(header[0] & 112)))
XX_FORMAT_READER_PROBE(coap_message, XX_FILE_TYPE_COAP_MESSAGE,
    (header_size >= 8 && (header[0] >> 6) == 1 && (header[0] & 15) <= 8))
XX_FORMAT_READER_PROBE(stun_message, XX_FILE_TYPE_STUN_MESSAGE,
    (header_size >= 20 && header[4] == 0x21 && header[5] == 0x12 && header[6] == 0xa4 && header[7] == 0x42))
XX_FORMAT_READER_PROBE(dhcp_message, XX_FILE_TYPE_DHCP_MESSAGE,
    (header_size >= 240 && (header[0] == 1 || header[0] == 2) && header[236] == 99 && header[237] == 130 && header[238] == 83 && header[239] == 99))
XX_FORMAT_READER_PROBE(radius_packet, XX_FILE_TYPE_RADIUS_PACKET,
    (header_size >= 22 && header[0] >= 1 && header[0] <= 11))
XX_FORMAT_READER_PROBE(snmp_message, XX_FILE_TYPE_SNMP_MESSAGE,
    (header_size >= 24 && header[0] == 48))
XX_FORMAT_READER_PROBE(ldap_message, XX_FILE_TYPE_LDAP_MESSAGE,
    (header_size >= 24 && header[0] == 48))
XX_FORMAT_READER_PROBE(tls_records, XX_FILE_TYPE_TLS_RECORDS,
    (header_size >= 32 && header[0] == 22 && header[1] == 3))
XX_FORMAT_READER_PROBE(x509_crl, XX_FILE_TYPE_X509_CRL,
    (header_size >= 32 && header[0] == 48))
XX_FORMAT_READER_PROBE(ocsp_response, XX_FILE_TYPE_OCSP_RESPONSE,
    (header_size >= 32 && header[0] == 48))
XX_FORMAT_READER_PROBE(lmdb_data, XX_FILE_TYPE_LMDB_DATA,
    (header_size >= 24 && header[16] == 0xde && header[17] == 0xc0 && header[18] == 0xef && header[19] == 0xbe))
XX_FORMAT_READER_PROBE(x11_xbm, XX_FILE_TYPE_X11_XBM,
    (total_size >= 12 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U))
XX_FORMAT_READER_PROBE(esri_ascii_grid, XX_FILE_TYPE_ESRI_ASCII_GRID,
    (total_size >= 24 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U))
XX_FORMAT_READER_PROBE(gxf_grid, XX_FILE_TYPE_GXF_GRID,
    (total_size >= 16 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U))
XX_FORMAT_READER_PROBE(ogc_wkt, XX_FILE_TYPE_OGC_WKT,
    (total_size >= 12 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U))
XX_FORMAT_READER_PROBE(gerber_rs274x, XX_FILE_TYPE_GERBER_RS274X,
    (total_size >= 24 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U))
XX_FORMAT_READER_PROBE(renderman_rib, XX_FILE_TYPE_RENDERMAN_RIB,
    (total_size >= 24 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U))
XX_FORMAT_READER_PROBE(lammps_data, XX_FILE_TYPE_LAMMPS_DATA,
    ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))))
XX_FORMAT_READER_PROBE(lammps_dump, XX_FILE_TYPE_LAMMPS_DUMP,
    ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))))
XX_FORMAT_READER_PROBE(shelx_res, XX_FILE_TYPE_SHELX_RES,
    ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))))
XX_FORMAT_READER_PROBE(turbomole_coord, XX_FILE_TYPE_TURBOMOLE_COORD,
    ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))))
XX_FORMAT_READER_PROBE(charmm_crd, XX_FILE_TYPE_CHARMM_CRD,
    ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))))
XX_FORMAT_READER_PROBE(castep_cell, XX_FILE_TYPE_CASTEP_CELL,
    ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))))
XX_FORMAT_READER_PROBE(crystal_fort34, XX_FILE_TYPE_CRYSTAL_FORT34,
    ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))))
XX_FORMAT_READER_PROBE(siesta_xv, XX_FILE_TYPE_SIESTA_XV,
    ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))))
XX_FORMAT_READER_PROBE(harwell_boeing, XX_FILE_TYPE_HARWELL_BOEING,
    ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))))
XX_FORMAT_READER_PROBE(openfoam_points, XX_FILE_TYPE_OPENFOAM_POINTS,
    ((header_size>=16 && (header[0]==9 || header[0]==10 || header[0]==13 || (header[0]>=32 && header[0]<=126)))))

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(minecraft_nbt, XX_FILE_TYPE_MINECRAFT_NBT,
    (header_size >= 1 && !xx_rt_memcmp(header,"\x0a",1)))
XX_FORMAT_READER_PROBE(leveldb_log, XX_FILE_TYPE_LEVELDB_LOG,
    (header_size >= 19 && header[6] == 1))
XX_FORMAT_READER_PROBE(dns_message, XX_FILE_TYPE_DNS_MESSAGE,
    (header_size >= 12 && header[4] == 0 && header[5] == 1))
XX_FORMAT_READER_PROBE(mongodb_wire, XX_FILE_TYPE_MONGODB_WIRE,
    (header_size >= 21 && header[12] == 0xdd && header[13] == 7 && !header[14] && !header[15]))
XX_FORMAT_READER_PROBE(redis_resp, XX_FILE_TYPE_REDIS_RESP,
    (header_size >= 24 && header[0] == '*'))
XX_FORMAT_READER_PROBE(mqtt_packets, XX_FILE_TYPE_MQTT_PACKETS,
    (header_size >= 20 && header[0] == 0x10))
XX_FORMAT_READER_PROBE(thrift_compact, XX_FILE_TYPE_THRIFT_COMPACT,
    (header_size >= 8 && header[0] == 0x82 && (header[1] & 31) == 1))
XX_FORMAT_READER_PROBE(x509_certificate, XX_FILE_TYPE_X509_CERTIFICATE,
    (header_size >= 32 && header[0] == 0x30))
XX_FORMAT_READER_PROBE(pkcs10_csr, XX_FILE_TYPE_PKCS10_CSR,
    (header_size >= 24 && header[0] == 0x30))
XX_FORMAT_READER_PROBE(pkcs12_pfx, XX_FILE_TYPE_PKCS12_PFX,
    (header_size >= 24 && header[0] == 0x30))
XX_FORMAT_READER_PROBE(iridas_cube_lut, XX_FILE_TYPE_IRIDAS_CUBE_LUT,
    (total_size >= 8 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U))
XX_FORMAT_READER_PROBE(hpgl_plot, XX_FILE_TYPE_HPGL_PLOT,
    (total_size >= 4 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U))
XX_FORMAT_READER_PROBE(tiled_tmx, XX_FILE_TYPE_TILED_TMX,
    (total_size >= 8 && header_size >= 1U && header[0] >= 9U && header[0] <= 126U))
XX_FORMAT_READER_PROBE(gaussian_cube, XX_FILE_TYPE_GAUSSIAN_CUBE,
    ((header_size>=16 && header[0]>=32 && header[0]<=126)))
XX_FORMAT_READER_PROBE(molecule_xyz, XX_FILE_TYPE_MOLECULE_XYZ,
    ((header_size>=16 && header[0]>=32 && header[0]<=126)))
XX_FORMAT_READER_PROBE(mdl_molfile, XX_FILE_TYPE_MDL_MOLFILE,
    ((header_size>=16 && header[0]>=32 && header[0]<=126)))
XX_FORMAT_READER_PROBE(tripos_mol2, XX_FILE_TYPE_TRIPOS_MOL2,
    (((header_size >= 17 && header[0] == 64 && header[1] == 60 && header[2] == 84 && header[3] == 82 && header[4] == 73 && header[5] == 80 && header[6] == 79 && header[7] == 83 && header[8] == 62 && header[9] == 77 && header[10] == 79 && header[11] == 76 && header[12] == 69 && header[13] == 67 && header[14] == 85 && header[15] == 76 && header[16] == 69))))
XX_FORMAT_READER_PROBE(xcrysden_xsf, XX_FILE_TYPE_XCRYSDEN_XSF,
    (((header_size >= 7 && header[0] == 67 && header[1] == 82 && header[2] == 89 && header[3] == 83 && header[4] == 84 && header[5] == 65 && header[6] == 76) || (header_size >= 5 && header[0] == 65 && header[1] == 84 && header[2] == 79 && header[3] == 77 && header[4] == 83) || (header_size >= 1 && header[0] == 35))))
XX_FORMAT_READER_PROBE(amber_prmtop, XX_FILE_TYPE_AMBER_PRMTOP,
    (((header_size >= 8 && header[0] == 37 && header[1] == 86 && header[2] == 69 && header[3] == 82 && header[4] == 83 && header[5] == 73 && header[6] == 79 && header[7] == 78))))
XX_FORMAT_READER_PROBE(amber_restart, XX_FILE_TYPE_AMBER_RESTART,
    ((header_size>=16 && header[0]>=32 && header[0]<=126)))
XX_FORMAT_READER_PROBE(gaussian_fchk, XX_FILE_TYPE_GAUSSIAN_FCHK,
    ((header_size>=16 && header[0]>=32 && header[0]<=126)))
XX_FORMAT_READER_PROBE(jcamp_dx, XX_FILE_TYPE_JCAMP_DX,
    ((header_size>=16 && header[0]=='#' && header[1]=='#' && (header[2]=='T' || header[2]=='J'))))
XX_FORMAT_READER_PROBE(dl_poly_config, XX_FILE_TYPE_DL_POLY_CONFIG,
    ((header_size>=16 && header[0]>=32 && header[0]<=126)))

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(kafka_record_batch, XX_FILE_TYPE_KAFKA_RECORD_BATCH,
    (header_size >= 61 && header[16] == 2))
XX_FORMAT_READER_PROBE(msgpack, XX_FILE_TYPE_MSGPACK,
    (header_size >= 2 && ((header[0] >= 0x81 && header[0] <= 0x9f) || header[0] == 0xdc || header[0] == 0xdd || header[0] == 0xde || header[0] == 0xdf)))
XX_FORMAT_READER_PROBE(ubjson, XX_FILE_TYPE_UBJSON,
    (header_size >= 3 && (header[0] == '{' || header[0] == '[')))
XX_FORMAT_READER_PROBE(bittorrent_metainfo, XX_FILE_TYPE_BITTORRENT_METAINFO,
    (header_size >= 16 && header[0] == 'd'))
XX_FORMAT_READER_PROBE(capnproto_message, XX_FILE_TYPE_CAPNPROTO_MESSAGE,
    (header_size >= 16 && !xx_rt_memcmp(header, "\0\0\0\0", 4)))
XX_FORMAT_READER_PROBE(dbus_message, XX_FILE_TYPE_DBUS_MESSAGE,
    (header_size >= 16 && (header[0] == 'l' || header[0] == 'B') && header[1] >= 1 && header[1] <= 4 && header[3] == 1))
XX_FORMAT_READER_PROBE(pkcs7_cms, XX_FILE_TYPE_PKCS7_CMS,
    (header_size >= 16 && header[0] == 0x30))
XX_FORMAT_READER_PROBE(wavefront_obj, XX_FILE_TYPE_WAVEFRONT_OBJ,
    (total_size >= 24 && header_size >= 24U && (header[0]==35U || header[0]==118U || header[0]==111U || header[0]==103U || header[0]==109U || header[0]==117U || header[0]==115U || header[0]==32U || header[0]==9U || header[0]==10U || header[0]==13U)))
XX_FORMAT_READER_PROBE(alias_pix, XX_FILE_TYPE_ALIAS_PIX,
    (total_size >= 14 && header_size >= 10U && (header[8]==0U && (header[9]==8U || header[9]==24U))))
XX_FORMAT_READER_PROBE(font_gem_fnt, XX_FILE_TYPE_FONT_GEM_FNT,
    (total_size >= 90 && header_size >= 68U && (header[66]==0U || header[67]==0U)))
XX_FORMAT_READER_PROBE(genomics_fasta, XX_FILE_TYPE_GENOMICS_FASTA,
    (((header_size >= 1 && header[0] == 62))))
XX_FORMAT_READER_PROBE(genomics_fastq, XX_FILE_TYPE_GENOMICS_FASTQ,
    (((header_size >= 1 && header[0] == 64))))
XX_FORMAT_READER_PROBE(genomics_sam, XX_FILE_TYPE_GENOMICS_SAM,
    (((header_size >= 4 && header[0] == 64 && header[1] == 72 && header[2] == 68 && header[3] == 9))))
XX_FORMAT_READER_PROBE(opendx_field, XX_FILE_TYPE_OPENDX_FIELD,
    (((header_size >= 1 && header[0] == 35) || (header_size >= 7 && header[0] == 111 && header[1] == 98 && header[2] == 106 && header[3] == 101 && header[4] == 99 && header[5] == 116 && header[6] == 32))))
XX_FORMAT_READER_PROBE(genomics_vcf, XX_FILE_TYPE_GENOMICS_VCF,
    (((header_size >= 19 && header[0] == 35 && header[1] == 35 && header[2] == 102 && header[3] == 105 && header[4] == 108 && header[5] == 101 && header[6] == 102 && header[7] == 111 && header[8] == 114 && header[9] == 109 && header[10] == 97 && header[11] == 116 && header[12] == 61 && header[13] == 86 && header[14] == 67 && header[15] == 70 && header[16] == 118 && header[17] == 52 && header[18] == 46))))
XX_FORMAT_READER_PROBE(genomics_gff3, XX_FILE_TYPE_GENOMICS_GFF3,
    (((header_size >= 15 && header[0] == 35 && header[1] == 35 && header[2] == 103 && header[3] == 102 && header[4] == 102 && header[5] == 45 && header[6] == 118 && header[7] == 101 && header[8] == 114 && header[9] == 115 && header[10] == 105 && header[11] == 111 && header[12] == 110 && header[13] == 32 && header[14] == 51))))
XX_FORMAT_READER_PROBE(protein_pdb, XX_FILE_TYPE_PROTEIN_PDB,
    (((header_size >= 6 && header[0] == 72 && header[1] == 69 && header[2] == 65 && header[3] == 68 && header[4] == 69 && header[5] == 82) || (header_size >= 6 && header[0] == 65 && header[1] == 84 && header[2] == 79 && header[3] == 77 && header[4] == 32 && header[5] == 32) || (header_size >= 6 && header[0] == 72 && header[1] == 69 && header[2] == 84 && header[3] == 65 && header[4] == 84 && header[5] == 77))))
XX_FORMAT_READER_PROBE(protein_mmcif, XX_FILE_TYPE_PROTEIN_MMCIF,
    (((header_size >= 5 && header[0] == 100 && header[1] == 97 && header[2] == 116 && header[3] == 97 && header[4] == 95))))
XX_FORMAT_READER_PROBE(matrix_market, XX_FILE_TYPE_MATRIX_MARKET,
    (((header_size >= 22 && header[0] == 37 && header[1] == 37 && header[2] == 77 && header[3] == 97 && header[4] == 116 && header[5] == 114 && header[6] == 105 && header[7] == 120 && header[8] == 77 && header[9] == 97 && header[10] == 114 && header[11] == 107 && header[12] == 101 && header[13] == 116 && header[14] == 32 && header[15] == 109 && header[16] == 97 && header[17] == 116 && header[18] == 114 && header[19] == 105 && header[20] == 120 && header[21] == 32))))
XX_FORMAT_READER_PROBE(gromacs_gro, XX_FILE_TYPE_GROMACS_GRO,
    ((header_size>=32 && header[0]>=32 && header[0]<=126)))

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(mongodb_bson, XX_FILE_TYPE_MONGODB_BSON,
    (header_size >= 5))
XX_FORMAT_READER_PROBE(leveldb_sstable, XX_FILE_TYPE_LEVELDB_SSTABLE,
    (header_size >= 48))
XX_FORMAT_READER_PROBE(tex_tfm, XX_FILE_TYPE_TEX_TFM,
    (total_size >= 52 && header_size >= 24U && total_size == ((uint32_t)header[0]*256U + header[1])*4U && ((uint32_t)header[2]*256U + header[3]) >= 2U))
XX_FORMAT_READER_PROBE(processing_vlw, XX_FILE_TYPE_PROCESSING_VLW,
    (total_size >= 62 && header_size >= 24U && header[4] == 0U && header[5] == 0U && header[6] == 0U && header[7] == 11U && header[0] == 0U && header[1] == 0U))
XX_FORMAT_READER_PROBE(phylo_newick, XX_FILE_TYPE_PHYLO_NEWICK,
    ((header_size>=6 && (header[0]=='(' || header[0]=='['))))
XX_FORMAT_READER_PROBE(python_marshal, XX_FILE_TYPE_PYTHON_MARSHAL,
    ((header_size>=10 && (header[0]==219 || header[0]==251 || header[0]==169 || header[0]==168 || header[0]==188 || header[0]==190))))

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(stereolithography_stl, XX_FILE_TYPE_STEREOLITHOGRAPHY_STL,
    (total_size >= 134 && header_size >= 84U && ((uint32_t)header[80] | ((uint32_t)header[81]<<8) | ((uint32_t)header[82]<<16) | ((uint32_t)header[83]<<24)) > 0 && ((uint32_t)header[80] | ((uint32_t)header[81]<<8) | ((uint32_t)header[82]<<16) | ((uint32_t)header[83]<<24)) <= 1000000U))
XX_FORMAT_READER_PROBE(seismic_sac, XX_FILE_TYPE_SEISMIC_SAC,
    ((header_size >= 632 && ((header[304]==6 && header[305]==0 && header[306]==0 && header[307]==0) || (header[304]==0 && header[305]==0 && header[306]==0 && header[307]==6)))))
XX_FORMAT_READER_PROBE(mountainsort_mda, XX_FILE_TYPE_MOUNTAINSORT_MDA,
    ((header_size >= 20 && header[0]>=248 && header[1]==255 && header[2]==255 && header[3]==255)))
XX_FORMAT_READER_PROBE(microscopy_spider, XX_FILE_TYPE_MICROSCOPY_SPIDER,
    ((header_size>=108 && ((header[16]==0 && header[17]==0 && header[18]==128 && header[19]==63) || (header[16]==63 && header[17]==128 && header[18]==0 && header[19]==0) || (header[16]==0 && header[17]==0 && header[18]==64 && header[19]==64) || (header[16]==64 && header[17]==64 && header[18]==0 && header[19]==0)))))

XX_FORMAT_PROBE_READ(1084U)
XX_FORMAT_READER_PROBE(lightwave_mdd, XX_FILE_TYPE_LIGHTWAVE_MDD,
    (total_size >= 8))

XX_FORMAT_PROBE_READ(512U)
XX_FORMAT_READER_PROBE(matlab_mat4, XX_FILE_TYPE_MATLAB_MAT4,
    ((header_size >= 20 && total_size >= 22)))
XX_FORMAT_READER_PROBE(seismic_segy, XX_FILE_TYPE_SEISMIC_SEGY,
    ((header_size >= 80 && total_size >= 3841)))

XX_FORMAT_PROBE_READ(512U)
XX_FORMAT_READER_PROBE(xbox_xdvdfs, XX_FILE_TYPE_XBOX_XDVDFS,
    (total_size >= 67584))
XX_FORMAT_READER_PROBE(freesurfer_mgh, XX_FILE_TYPE_FREESURFER_MGH,
    (((header_size >= 4 && header[0] == 0 && header[1] == 0 && header[2] == 0 && header[3] == 1))))
XX_FORMAT_READER_PROBE(tensorflow_tfrecord, XX_FILE_TYPE_TENSORFLOW_TFRECORD,
    (((header_size >= 16 && header[4]==0 && header[5]==0 && header[6]==0 && header[7]==0))))

XX_FORMAT_PROBE_READ(512U)
XX_FORMAT_READER_PROBE(dbase_dbf, XX_FILE_TYPE_DBASE_DBF,
    (((header_size >= 1 && header[0] == 3))))

XX_FORMAT_PROBE_READ(512U)
XX_FORMAT_READER_PROBE(base16, XX_FILE_TYPE_BASE16,
    (header_size >= 8U && ((header[0] >= 48 && header[0] <= 57) || (header[0] >= 97 && header[0] <= 102) || (header[0] >= 65 && header[0] <= 70) || header[0] == 32 || header[0] == 9 || header[0] == 13 || header[0] == 10)))
XX_FORMAT_READER_PROBE(sar_ns, XX_FILE_TYPE_SAR_NS,
    (header_size >= 16U && (header[0] != 0 || header[1] != 0) && (uint64_t)(((uint32_t)header[2] << 24U) | ((uint32_t)header[3] << 16U) | ((uint32_t)header[4] << 8U) | header[5]) <= (uint64_t)total_size && (((uint32_t)header[2] << 24U) | ((uint32_t)header[3] << 16U) | ((uint32_t)header[4] << 8U) | header[5]) >= 6U + (((uint32_t)header[0] << 8U) | header[1]) * 10U))
XX_FORMAT_READER_PROBE(riff, XX_FILE_TYPE_RIFF,
    (header_size >= 4U && (xx_rt_memcmp(header + 0U, "\x52\x49\x46\x46", 4U) == 0)))

XX_FORMAT_PROBE_MAGIC()
XX_FORMAT_READER_PROBE(base64, XX_FILE_TYPE_BASE64,
    (header_size >= 16U && ((header[0] >= 0x41U && header[0] <= 0x5AU) || (header[0] >= 0x61U && header[0] <= 0x7AU) || (header[0] >= 0x30U && header[0] <= 0x39U) || header[0] == 0x2BU || header[0] == 0x2FU)))

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(ethernet_frame, XX_FILE_TYPE_ETHERNET_FRAME,
    (header_size >= 42 && ((header[12] == 8 && header[13] == 0) || (header[12] == 134 && header[13] == 221))))
XX_FORMAT_READER_PROBE(ip_packet, XX_FILE_TYPE_IP_PACKET,
    (header_size >= 28 && ((header[0] >> 4) == 4 || (header[0] >> 4) == 6)))
XX_FORMAT_READER_PROBE(arp_packet, XX_FILE_TYPE_ARP_PACKET,
    (header_size >= 28 && header[0] == 0 && header[1] == 1 && header[2] == 8 && header[3] == 0 && header[4] == 6 && header[5] == 4))
XX_FORMAT_READER_PROBE(icmp_message, XX_FILE_TYPE_ICMP_MESSAGE,
    (header_size >= 9 && (header[0] == 8 || header[0] == 0) && header[1] == 0))
XX_FORMAT_READER_PROBE(diameter_message, XX_FILE_TYPE_DIAMETER_MESSAGE,
    (header_size >= 20 && header[0] == 1 && header[4] == 128 && header[5] == 0 && header[6] == 1 && header[7] == 1))
XX_FORMAT_READER_PROBE(tacacs_packet, XX_FILE_TYPE_TACACS_PACKET,
    (header_size >= 20 && (header[0] == 192 || header[0] == 193) && header[1] == 1 && header[2] == 1 && header[3] == 1))
XX_FORMAT_READER_PROBE(gtp_message, XX_FILE_TYPE_GTP_MESSAGE,
    (header_size >= 36 && header[0] == 48 && header[1] == 255))
XX_FORMAT_READER_PROBE(pfcp_message, XX_FILE_TYPE_PFCP_MESSAGE,
    (header_size >= 21 && header[0] == 32 && header[1] == 5))
XX_FORMAT_READER_PROBE(pptp_message, XX_FILE_TYPE_PPTP_MESSAGE,
    (header_size >= 32 && header[0] == 0 && header[1] == 156 && header[4] == 26 && header[5] == 43 && header[6] == 60 && header[7] == 77))
XX_FORMAT_READER_PROBE(rsvp_message, XX_FILE_TYPE_RSVP_MESSAGE,
    (header_size >= 20 && header[0] == 16 && header[1] == 20 && header[6] == 0 && header[7] == 20))
XX_FORMAT_READER_PROBE(kerberos_ccache, XX_FILE_TYPE_KERBEROS_CCACHE,
    (header_size >= 32 && header[0] == 5 && header[1] == 4))
XX_FORMAT_READER_PROBE(jose_jws, XX_FILE_TYPE_JOSE_JWS,
    (header_size >= 32 && header[0] == 101 && header[1] == 121))
XX_FORMAT_READER_PROBE(wbmp_image, XX_FILE_TYPE_WBMP_IMAGE,
    (total_size >= 4 && header_size >= 2U && !xx_rt_memcmp(header,"\x00\x00",2U)))
XX_FORMAT_READER_PROBE(palm_bitmap, XX_FILE_TYPE_PALM_BITMAP,
    (total_size >= 16 && header_size >= 16U && (header[0] || header[1]) && (header[2] || header[3]) && header[0] <= 32 && header[2] <= 32 && (header[8] == 1 || header[8] == 2 || header[8] == 4 || header[8] == 8) && header[9] <= 2))
XX_FORMAT_READER_PROBE(adobe_acv, XX_FILE_TYPE_ADOBE_ACV,
    (total_size >= 6 && header_size >= 4U && header[0] == 0 && (header[1] == 1 || header[1] == 4) && header[2] == 0 && header[3] >= 1 && header[3] <= 16))
XX_FORMAT_READER_PROBE(collada_dae, XX_FILE_TYPE_COLLADA_DAE,
    (total_size >= 32 && header_size >= 1U && (header[0] == '<' || header[0] == 32 || header[0] == 9 || header[0] == 10 || header[0] == 13)))
XX_FORMAT_READER_PROBE(dsn6_density, XX_FILE_TYPE_DSN6_DENSITY,
    (total_size >= 1024 && header_size >= 38U && ((!xx_rt_memcmp(header,":-)",3U)) || (header[36] == 0 && header[37] == 100) || (header[36] == 100 && header[37] == 0))))
XX_FORMAT_READER_PROBE(tetgen_mesh, XX_FILE_TYPE_TETGEN_MESH,
    (total_size >= 4 && header_size >= 1U && (header[0] == '#' || header[0] == 32 || header[0] == 9 || header[0] == 10 || header[0] == 13 || (header[0] >= '0' && header[0] <= '9'))))
XX_FORMAT_READER_PROBE(qchem_input, XX_FILE_TYPE_QCHEM_INPUT,
    (total_size >= 32 && header_size >= 1U && (header[0] == '$' || header[0] == 32 || header[0] == 10 || header[0] == 13)))
XX_FORMAT_READER_PROBE(adlib_lds, XX_FILE_TYPE_ADLIB_LDS,
    (header_size >= 17 && header[0] <= 2 && (header[1] || header[2]) && header[3] && header[4] && (header[15] || header[16])))
XX_FORMAT_READER_PROBE(adlib_rol, XX_FILE_TYPE_ADLIB_ROL,
    (header_size >= 203 && header[0] == 0 && header[1] == 0 && header[2] == 4 && header[3] == 0 && header[53] <= 1))
XX_FORMAT_READER_PROBE(genomics_bed, XX_FILE_TYPE_GENOMICS_BED,
    ((header_size>=8 && (header[0]==35 || (header[0]>=32 && header[0]<=126)))))
XX_FORMAT_READER_PROBE(genomics_wiggle, XX_FILE_TYPE_GENOMICS_WIGGLE,
    ((header_size>=8 && (header[0]==35 || (header[0]>=32 && header[0]<=126)))))
XX_FORMAT_READER_PROBE(genomics_gtf, XX_FILE_TYPE_GENOMICS_GTF,
    ((header_size>=8 && (header[0]==35 || (header[0]>=32 && header[0]<=126)))))
XX_FORMAT_READER_PROBE(genomics_agp, XX_FILE_TYPE_GENOMICS_AGP,
    ((header_size>=8 && (header[0]==35 || (header[0]>=32 && header[0]<=126)))))
XX_FORMAT_READER_PROBE(adobe_act, XX_FILE_TYPE_ADOBE_ACT,
    ((total_size == 768 || total_size == 772) && header_size >= 1U))

XX_FORMAT_PROBE_READ(4100U)
XX_FORMAT_READER_PROBE(modbus_tcp, XX_FILE_TYPE_MODBUS_TCP,
    (header_size >= 9 && header[2] == 0 && header[3] == 0 && header[6] > 0 && header[6] < 248 && (header[7] == 3 || header[7] == 5 || header[7] == 6 || header[7] == 16 || (header[7] >= 129 && header[7] <= 144))))
XX_FORMAT_READER_PROBE(someip_message, XX_FILE_TYPE_SOMEIP_MESSAGE,
    (header_size >= 17 && header[12] == 1 && header[13] > 0 && (header[14] <= 2 || header[14] == 128 || header[14] == 129)))
XX_FORMAT_READER_PROBE(rip_message, XX_FILE_TYPE_RIP_MESSAGE,
    (header_size >= 24 && header[0] == 2 && header[1] == 2 && header[2] == 0 && header[3] == 0))
XX_FORMAT_READER_PROBE(vrrp_message, XX_FILE_TYPE_VRRP_MESSAGE,
    (header_size >= 20 && header[0] == 33 && header[1] > 0 && header[3] > 0 && header[4] == 0))
XX_FORMAT_READER_PROBE(igmp_message, XX_FILE_TYPE_IGMP_MESSAGE,
    (header_size >= 16 && header[0] == 34 && header[1] == 0 && header[4] == 0 && header[5] == 0))
XX_FORMAT_READER_PROBE(pim_message, XX_FILE_TYPE_PIM_MESSAGE,
    (header_size >= 10 && header[0] == 32 && header[1] == 0))
XX_FORMAT_READER_PROBE(ldp_message, XX_FILE_TYPE_LDP_MESSAGE,
    (header_size >= 26 && header[0] == 0 && header[1] == 1 && header[8] == 0 && header[9] == 0))
XX_FORMAT_READER_PROBE(gre_packet, XX_FILE_TYPE_GRE_PACKET,
    (header_size >= 32 && (header[0] & 79) == 0 && header[1] == 0 && ((header[2] == 8 && header[3] == 0) || (header[2] == 134 && header[3] == 221))))
XX_FORMAT_READER_PROBE(l2tp_packet, XX_FILE_TYPE_L2TP_PACKET,
    (header_size >= 52 && header[0] == 200 && header[1] == 2))
XX_FORMAT_READER_PROBE(lldp_message, XX_FILE_TYPE_LLDP_MESSAGE,
    (header_size >= 14 && (header[0] & 254) == 2))
XX_FORMAT_READER_PROBE(netflow_datagram, XX_FILE_TYPE_NETFLOW_DATAGRAM,
    (header_size >= 72 && header[0] == 0 && header[1] == 5))
XX_FORMAT_READER_PROBE(dcerpc_pdu, XX_FILE_TYPE_DCERPC_PDU,
    (header_size >= 24 && header[0] == 5 && header[1] == 0 && (header[2] == 0 || header[2] == 2 || header[2] == 11) && header[3] == 3))
XX_FORMAT_READER_PROBE(ethereum_rlp, XX_FILE_TYPE_ETHEREUM_RLP,
    (header_size >= 58 && header[0] >= 248 && header[0] <= 251))
XX_FORMAT_READER_PROBE(avs_image, XX_FILE_TYPE_AVS_IMAGE,
    (total_size >= 12 && header_size >= 8 && header[0]==0 && header[1]==0 && (header[2]||header[3]) && header[2]<=32 && (header[2]!=32||header[3]==0) && header[4]==0 && header[5]==0 && (header[6]||header[7]) && header[6]<=15))
XX_FORMAT_READER_PROBE(mtv_image, XX_FILE_TYPE_MTV_IMAGE,
    (total_size >= 7 && header_size >= 5 && header[0]>='1' && header[0]<='9'))
XX_FORMAT_READER_PROBE(nokia_ota_bitmap, XX_FILE_TYPE_NOKIA_OTA_BITMAP,
    (total_size >= 5 && header_size >= 5 && ((header[0]==0 && header[1] && header[2] && header[3]==1)||(header_size>=6 && header[0]==16 && (header[1]||header[2]) && header[1]<=32 && (header[3]||header[4]) && header[3]<=15 && header[5]==1))))
XX_FORMAT_READER_PROBE(apple_pict, XX_FILE_TYPE_APPLE_PICT,
    (total_size >= 124 && ((header_size>=14 && !xx_rt_memcmp(header+10,"\x00\x11\x02\xff",4))||(header_size>=526 && total_size>=636 && !xx_rt_memcmp(header,"\0\0\0\0\0\0\0\0\0\0\0\0\0\0",14) && !xx_rt_memcmp(header+522,"\x00\x11\x02\xff",4)))))
XX_FORMAT_READER_PROBE(iges_model, XX_FILE_TYPE_IGES_MODEL,
    (total_size >= 405 && header_size >= 80 && header[72]=='S' && header[79]=='1'))
XX_FORMAT_READER_PROBE(ufo_glif, XX_FILE_TYPE_UFO_GLIF,
    (total_size >= 32 && header_size >= 1 && (header[0]=='<'||header[0]==32||header[0]==9||header[0]==10||header[0]==13)))
XX_FORMAT_READER_PROBE(unifont_hex, XX_FILE_TYPE_UNIFONT_HEX,
    (total_size >= 38 && header_size >= 7 && ((header[0]>='0'&&header[0]<='9')||(header[0]>='A'&&header[0]<='F')||(header[0]>='a'&&header[0]<='f')) && ((header[1]>='0'&&header[1]<='9')||(header[1]>='A'&&header[1]<='F')||(header[1]>='a'&&header[1]<='f')) && ((header[2]>='0'&&header[2]<='9')||(header[2]>='A'&&header[2]<='F')||(header[2]>='a'&&header[2]<='f')) && ((header[3]>='0'&&header[3]<='9')||(header[3]>='A'&&header[3]<='F')||(header[3]>='a'&&header[3]<='f')) && (header[4]==':'||(((header[4]>='0'&&header[4]<='9')||(header[4]>='A'&&header[4]<='F')||(header[4]>='a'&&header[4]<='f'))&&header[5]==':')||(((header[4]>='0'&&header[4]<='9')||(header[4]>='A'&&header[4]<='F')||(header[4]>='a'&&header[4]<='f'))&&((header[5]>='0'&&header[5]<='9')||(header[5]>='A'&&header[5]<='F')||(header[5]>='a'&&header[5]<='f'))&&header[6]==':'))))
XX_FORMAT_READER_PROBE(ken_ksm, XX_FILE_TYPE_KEN_KSM,
    ((header_size>=82 && header[16]>0 && header[16]<=240 && header[32]<=9 && header[48]==0 && header[49]==0 && header[64]<=63 && (header[80]||header[81]))))
XX_FORMAT_READER_PROBE(implay_music, XX_FILE_TYPE_IMPLAY_MUSIC,
    ((header_size>=70 && header[0]==1 && header[1]==0 && header[2]==0 && header[3]==0 && header[4]==0 && header[5]==0 && header[36]>0 && header[37]>0)))
XX_FORMAT_READER_PROBE(vasp_poscar, XX_FILE_TYPE_VASP_POSCAR,
    ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))))
XX_FORMAT_READER_PROBE(quantum_espresso_input, XX_FILE_TYPE_QUANTUM_ESPRESSO_INPUT,
    ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))))
XX_FORMAT_READER_PROBE(cp2k_input, XX_FILE_TYPE_CP2K_INPUT,
    ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))))
XX_FORMAT_READER_PROBE(nwchem_input, XX_FILE_TYPE_NWCHEM_INPUT,
    ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))))
XX_FORMAT_READER_PROBE(gamess_input, XX_FILE_TYPE_GAMESS_INPUT,
    ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))))
XX_FORMAT_READER_PROBE(gaussian_input, XX_FILE_TYPE_GAUSSIAN_INPUT,
    ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))))
XX_FORMAT_READER_PROBE(abinit_input, XX_FILE_TYPE_ABINIT_INPUT,
    ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))))
XX_FORMAT_READER_PROBE(aims_geometry, XX_FILE_TYPE_AIMS_GEOMETRY,
    ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))))
XX_FORMAT_READER_PROBE(orca_input, XX_FILE_TYPE_ORCA_INPUT,
    ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))))
XX_FORMAT_READER_PROBE(demon_input, XX_FILE_TYPE_DEMON_INPUT,
    ((header_size>=8 && (header[0]==35 || header[0]==33 || header[0]==36 || header[0]==38 || header[0]==37 || header[0]==32 || (header[0]>=65 && header[0]<=122)))))
