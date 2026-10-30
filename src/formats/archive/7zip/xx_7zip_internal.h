/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_7ZIP_INTERNAL_H
#define XXFCLIB_7ZIP_INTERNAL_H

/* Reader state shared by xx_7zip.c and xx_7zip_datastructs.c. */

#include "xxfclib/formats/archive/7zip/xx_7zip.h"

#include <stdint.h>
#include <wchar.h>

#define XX_7ZIP_MAX_PROPERTIES 64U

typedef struct xx_7zip_coder_s {
    uint64_t method;
    uint64_t num_in;
    uint64_t num_out;
    uint8_t properties[XX_7ZIP_MAX_PROPERTIES];
    size_t properties_size;
} xx_7zip_coder;

typedef struct xx_7zip_folder_s {
    xx_7zip_coder *coders;
    uint64_t *bind_inputs;
    uint64_t *bind_outputs;
    uint64_t *packed_indices;
    uint64_t *unpack_sizes;
    size_t coder_count;
    size_t bind_count;
    uint64_t total_in;
    uint64_t total_out;
    uint64_t final_output_index;
    uint64_t packed_count;
    uint64_t first_pack_index;
    uint64_t unpack_size;
    uint64_t num_substreams;
    uint64_t first_substream;
    uint32_t crc;
    bool crc_defined;
    bool encrypted;
    bool supported;
    uint64_t method;
} xx_7zip_folder;

typedef struct xx_7zip_streams_s {
    uint64_t pack_pos;
    uint64_t num_packs;
    uint64_t *pack_sizes;
    uint32_t *pack_crcs;
    uint8_t *pack_crc_defined;
    xx_7zip_folder *folders;
    uint64_t num_folders;
    uint64_t num_substreams;
    uint64_t *sub_sizes;
    uint32_t *sub_crcs;
    uint8_t *sub_crc_defined;
    bool substreams_ready;
} xx_7zip_streams;

typedef struct xx_7zip_file_s {
    wchar_t *name;
    uint64_t size;
    uint64_t offset_in_folder;
    int64_t folder_index;
    uint32_t crc;
    uint32_t attributes;
    uint64_t mtime;
    bool crc_defined;
    bool attributes_defined;
    bool mtime_defined;
    bool empty_stream;
    bool empty_file;
    bool anti;
    bool is_folder;
} xx_7zip_file;

typedef struct xx_7zip_private_s {
    xx_7zip_streams streams;
    xx_7zip_file *files;
    uint64_t num_files;
    int64_t physical_data_end;
} xx_7zip_private;

bool xx_7zip_pack_offset(const Abstractformat *self, const xx_7zip_streams *streams, uint64_t pack_index, int64_t *offset, uint64_t *size);

#endif /* XXFCLIB_7ZIP_INTERNAL_H */
