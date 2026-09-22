/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_tar_common.h @brief Shared compressed-TAR adapter support. */

#ifndef XXFCLIB_FORMAT_TAR_COMMON_H
#define XXFCLIB_FORMAT_TAR_COMMON_H

#include "xxfclib/formats/tar/xx_tar.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decode the compressed stream represented by @p outer into @p destination.
 * On success, @p compressed_size receives the exact number of source bytes
 * occupied by the compressed stream (excluding an optional outer overlay).
 */
typedef bool (*xx_tar_common_decode_fn)(Abstractformat *outer,
                                        xx_io_device *destination,
                                        int64_t *compressed_size,
                                        xx_pd_struct *pd);

/**
 * Encode a finalized logical TAR stream into the transport owned by @p outer.
 * Implementations must start writing at outer->base_address and return the
 * exact encoded transport size through @p compressed_size.
 */
typedef bool (*xx_tar_common_encode_fn)(Abstractformat *outer,
                                        const xx_list_s *options,
                                        xx_io_device *tar_source,
                                        int64_t tar_size,
                                        int64_t *compressed_size,
                                        xx_pd_struct *pd);

/** Private state embedded by the tar.gz, tar.bz2, and tar.xz readers. */
typedef struct xx_tar_common_s {
    uint8_t *decoded_data;
    size_t decoded_size;
    size_t decoded_capacity;
    xx_io_device *decoded_device;
    xx_tar *tar;
    int64_t compressed_size;
    bool attempted;
    bool valid;
} xx_tar_common;

void xx_tar_common_init(xx_tar_common *common);
void xx_tar_common_cleanup(xx_tar_common *common);

bool xx_tar_common_load(xx_tar_common *common, Abstractformat *outer,
                        xx_tar_common_decode_fn decode, xx_pd_struct *pd);
bool xx_tar_common_handle_base_info(xx_tar_common *common,
                                    Abstractformat *outer,
                                    xx_tar_common_decode_fn decode,
                                    xx_pd_struct *pd);
int64_t xx_tar_common_get_format_size(xx_tar_common *common,
                                      Abstractformat *outer,
                                      xx_tar_common_decode_fn decode,
                                      xx_pd_struct *pd);
uint64_t xx_tar_common_get_number_of_archive_records(
    xx_tar_common *common, Abstractformat *outer,
    xx_tar_common_decode_fn decode, xx_pd_struct *pd);

xx_archive_record_state *xx_tar_common_create_archive_records_reading(
    xx_tar_common *common, Abstractformat *outer,
    xx_tar_common_decode_fn decode, const xx_list_s *options,
    xx_pd_struct *pd);
const xx_archive_record *xx_tar_common_get_current_archive_record(
    xx_tar_common *common, xx_archive_record_state *state);
bool xx_tar_common_unpack_current_archive_record(
    xx_tar_common *common, xx_archive_record_state *state,
    xx_pd_struct *pd);
bool xx_tar_common_archive_record_move_to_next(
    xx_tar_common *common, xx_archive_record_state *state,
    xx_pd_struct *pd);
void xx_tar_common_free_archive_records_reading(
    xx_tar_common *common, xx_archive_record_state *state);

const char *xx_tar_common_data_struct_id_to_string(
    xx_tar_common *common, uint32_t id);
uint32_t xx_tar_common_data_struct_string_to_id(
    xx_tar_common *common, const char *name);
xx_data_struct_state *xx_tar_common_create_data_structs_reading(
    xx_tar_common *common, Abstractformat *outer,
    xx_tar_common_decode_fn decode, xx_pd_struct *pd);
const xx_data_struct *xx_tar_common_get_current_data_struct(
    xx_tar_common *common, xx_data_struct_state *state);
bool xx_tar_common_data_struct_move_to_next(
    xx_tar_common *common, xx_data_struct_state *state,
    xx_pd_struct *pd);
void xx_tar_common_free_data_structs_reading(
    xx_tar_common *common, xx_data_struct_state *state);

xx_data_struct_record_state *
xx_tar_common_create_data_struct_records_reading(
    xx_tar_common *common, const xx_data_struct *data_struct,
    xx_pd_struct *pd);
const xx_data_struct_record *xx_tar_common_get_current_data_struct_record(
    xx_tar_common *common, xx_data_struct_record_state *state);
bool xx_tar_common_data_struct_record_move_to_next(
    xx_tar_common *common, xx_data_struct_record_state *state,
    xx_pd_struct *pd);
void xx_tar_common_free_data_struct_records_reading(
    xx_tar_common *common, xx_data_struct_record_state *state);

xx_archive_write_state *xx_tar_common_create_archive_records_writing(
    Abstractformat *outer, const xx_list_s *options,
    xx_tar_common_encode_fn encode, xx_pd_struct *pd);
bool xx_tar_common_pack_archive_record(
    Abstractformat *outer, xx_archive_write_state *state,
    const xx_archive_record *record, xx_io_device *source_dev,
    xx_pd_struct *pd);
bool xx_tar_common_finalize_archive_records_writing(
    Abstractformat *outer, xx_archive_write_state *state,
    xx_pd_struct *pd);
void xx_tar_common_free_archive_records_writing(
    Abstractformat *outer, xx_archive_write_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TAR_COMMON_H */
