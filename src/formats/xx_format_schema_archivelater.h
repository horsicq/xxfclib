/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * The archivelater schema family: what its family unit (xx_format_schema_archivelater.c)
 * and its formats' data files share. Formats: ALDUS; AMPK; ARX; PACKIT; ASAR; PEA; STUFFIT5; DISKDOUBLER; XPK COMPRESSED FILE; PPMD; LRZIP; RZIP; MS DOS BACKUP; MS DOS BACKUP2; ESP archive; LPAK. */
#ifndef XX_FORMAT_SCHEMA_ARCHIVELATER_H
#define XX_FORMAT_SCHEMA_ARCHIVELATER_H
#include "xx_format_schema.h"
#include "xx_format_schema_internal.h"

#define SXAL_LIMIT 20000U

/* Defined in the family unit or in a format's data file. */
extern const sx_layout sxal_aldus_header;
extern const sx_layout sxal_aldus_timestamp;
extern const sx_layout sxal_aldus_stream;
extern const sx_layout sxal_aldus_block_size;
extern const sx_layout sxal_ampk_header;
extern const sx_layout sxal_ampk_record;
extern const sx_layout sxal_ampk_file;
extern const sx_layout sxal_arx_member;
extern const sx_layout sxal_arx_end;
extern const sx_layout sxal_dos_packit_header;
extern const sx_layout sxal_dos_packit_member;
extern const sx_layout sxal_dos_packit_end;
extern const sx_layout sxal_asar_header;
extern const sx_layout sxal_pea_header;
extern const sx_layout sxal_pea_trigger_le;
extern const sx_layout sxal_pea_stream_le;
extern const sx_layout sxal_pea_block_size_le;
extern const sx_layout sxal_pea_name_size_le;
extern const sx_layout sxal_pea_object_le;
extern const sx_layout sxal_pea_file_size_le;
extern const sx_layout sxal_pea_packed_size_le;
extern const sx_layout sxal_pea_last_size_le;
extern const sx_layout sxal_pea_trigger_be;
extern const sx_layout sxal_pea_stream_be;
extern const sx_layout sxal_pea_block_size_be;
extern const sx_layout sxal_pea_name_size_be;
extern const sx_layout sxal_pea_object_be;
extern const sx_layout sxal_pea_file_size_be;
extern const sx_layout sxal_pea_packed_size_be;
extern const sx_layout sxal_pea_last_size_be;
extern const sx_layout sxal_sit5_header;
extern const sx_layout sxal_sit5_file;
extern const sx_layout sxal_sit5_folder;
extern const sx_layout sxal_sit5_second_v1;
extern const sx_layout sxal_sit5_second_v3;
extern const sx_layout sxal_sit5_resource;
extern const sx_layout sxal_diskdoubler_file;
extern const sx_layout sxal_dda2_header;
extern const sx_layout sxal_dda2_packed;
extern const sx_layout sxal_dda2_folder;
extern const sx_layout sxal_dda2_stored;
extern const sx_layout sxal_dda2_end;
extern const sx_layout sxal_dda2_footer;
extern const sx_layout sxal_dda2_index_word;
extern const sx_layout sxal_ddar_header;
extern const sx_layout sxal_ddar_member;
extern const sx_layout sxal_xpk_header;
extern const sx_layout sxal_xpk_extension_size;
extern const sx_layout sxal_xpk_chunk2;
extern const sx_layout sxal_xpk_chunk4;
extern const sx_layout sxal_ppmd_header;
extern const sx_layout sxal_lrzip_header;
extern const sx_layout sxal_lrzip_chunk1;
extern const sx_layout sxal_lrzip_block1;
extern const sx_layout sxal_lrzip_chunk2;
extern const sx_layout sxal_lrzip_block2;
extern const sx_layout sxal_lrzip_chunk3;
extern const sx_layout sxal_lrzip_block3;
extern const sx_layout sxal_lrzip_chunk4;
extern const sx_layout sxal_lrzip_block4;
extern const sx_layout sxal_lrzip_chunk5;
extern const sx_layout sxal_lrzip_block5;
extern const sx_layout sxal_lrzip_chunk6;
extern const sx_layout sxal_lrzip_block6;
extern const sx_layout sxal_lrzip_chunk7;
extern const sx_layout sxal_lrzip_block7;
extern const sx_layout sxal_lrzip_chunk8;
extern const sx_layout sxal_lrzip_block8;
extern const sx_layout sxal_lrzip_md5;
extern const sx_layout sxal_rzip_header;
extern const sx_layout sxal_rzip_block;
extern const sx_layout sxal_dos_backup_header;
extern const sx_layout sxal_dos_backup2_header;
extern const sx_layout sxal_dos_backup2_directory;
extern const sx_layout sxal_dos_backup2_file;
extern const sx_layout sxal_esp_header;
extern const sx_layout sxal_lpak_header;
extern const sx_layout sxal_uharc_header;
extern const sx_layout sxal_is_stored_signature;
extern const sx_layout sxal_is_stored_marker;
extern const sx_layout sxal_tarx2_signature;
extern const sx_layout sxal_tarx2_marker;
bool sxal_span(sx_context *c,uint64_t p,uint64_t n);
bool sxal_read(sx_context *c,uint64_t p,void *b,size_t n);
uint64_t sxal_uint(const uint8_t *b,unsigned n,bool be);
uint16_t sxal16(const uint8_t *b,bool be);
uint32_t sxal32(const uint8_t *b,bool be);
bool sxal_add(sx_context *c,const sx_layout *l,uint64_t p);

/* Per-format walkers, called by the family's build. */
bool sx_aldus_build(sx_context *c);  /* archive/aldus/xx_aldus_data.c */
bool sx_ampk_build(sx_context *c);  /* archive/ampk/xx_ampk_data.c */
bool sx_arx_build(sx_context *c);  /* archive/arx/xx_arx_data.c */
bool sx_packit_build(sx_context *c);  /* archive/packit/xx_packit_data.c */
bool sx_asar_build(sx_context *c);  /* archive/asar/xx_asar_data.c */
bool sx_pea_build(sx_context *c);  /* archive/pea/xx_pea_data.c */
bool sx_stuffit5_build(sx_context *c);  /* archive/stuffit5/xx_stuffit5_data.c */
bool sx_diskdoubler_build(sx_context *c);  /* archive/diskdoubler/xx_diskdoubler_data.c */
bool sx_xpk_compressed_file_build(sx_context *c);  /* archive/xpk_compressed_file/xx_xpk_compressed_file_data.c */
bool sx_ppmd_build(sx_context *c);  /* archive/ppmd/xx_ppmd_data.c */
bool sx_lrzip_build(sx_context *c);  /* archive/lrzip/xx_lrzip_data.c */
bool sx_rzip_build(sx_context *c);  /* archive/rzip/xx_rzip_data.c */
bool sx_ms_dos_backup_build(sx_context *c);  /* archive/ms_dos_backup/xx_ms_dos_backup_data.c */
bool sx_ms_dos_backup2_build(sx_context *c);  /* archive/ms_dos_backup2/xx_ms_dos_backup2_data.c */
bool sx_esp_archive_build(sx_context *c);  /* archive/esp_archive/xx_esp_archive_data.c */
bool sx_lpak_build(sx_context *c);  /* archive/lpak/xx_lpak_data.c */
bool sx_uharc_build(sx_context *c);  /* archive/uharc/xx_uharc_data.c */
bool sx_is_stored_build(sx_context *c);  /* archive/is_stored/xx_is_stored_data.c */
bool sx_tarx2_build(sx_context *c);  /* archive/tarx2/xx_tarx2_data.c */

#endif
