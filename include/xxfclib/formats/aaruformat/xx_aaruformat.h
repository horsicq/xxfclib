/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_aaruformat.h @brief Native bounded AaruFormat V2 block-media reader.
 * Supports little-endian V2.0 UnknownMO and GENERIC_HDD block media with one
 * final flat IDX3, one
 * single-level uncompressed UserData DDT2, and uncompressed UserData DBLKs.
 * Every logical sector must have Dumped status; deduplicated pointers are
 * resolved to indexed blocks. CRC64 validates index, DDT and data payloads.
 * One media.img member reconstructs the full sector stream. Missing, errored,
 * encrypted and generable sectors, compressed blocks, hierarchy, parent or
 * snapshot data, optical/tape/flux tracks and extra indexed media metadata
 * are refused. Primary format evidence: aaru-dps/libaaruformat spec and
 * include/aaruformat/structs/{header,index,ddt,data}.h. No upstream source
 * is incorporated and no Aaru runtime is used. Initial parsing caps DDT at
 * 64MiB, data blocks at 32MiB, index entries at 100000 and image at 1PiB.
 * Extraction MAX_MEMBER_SIZE includes all sector bytes; MEMORY_LIMIT covers
 * retained DDT/view, iterator and 64KiB transfer. Input cursor is preserved.
 * Files use exclusive sibling staging and overwrite defaults to false.
 * Borrowed input must stay open and unchanged during the volume lifetime.
 */
#ifndef XXFCLIB_FORMAT_AARUFORMAT_H
#define XXFCLIB_FORMAT_AARUFORMAT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_aaruformat {
    Abstractformat format;
    uint64_t sectors;
    uint64_t media_size;
    uint32_t sector_size;
    void *internal;
} xx_aaruformat;
typedef xx_aaruformat xx_aaruformat_t;
typedef xx_aaruformat XAaruformat;
XXFC_API void xx_aaruformat_init(xx_aaruformat *, xx_io_device *, int64_t);
XXFC_API xx_aaruformat *xx_aaruformat_create(xx_io_device *, int64_t);
XXFC_API void xx_aaruformat_destroy(xx_aaruformat *);
XXFC_API void xx_aaruformat_free(xx_aaruformat *);
XXFC_API bool xx_aaruformat_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_aaruformat_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_aaruformat_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_aaruformat_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_aaruformat_create_archive_records_reading(
    Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_aaruformat_get_current_archive_record(
    Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_aaruformat_archive_record_move_to_next(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_aaruformat_unpack_current_archive_record(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_aaruformat_extract_record_to_device(
    Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_aaruformat_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_aaruformat_to_format(xx_aaruformat *v){return v?&v->format:NULL;}
#ifdef __cplusplus
}
#endif
#endif
