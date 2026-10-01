/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_fmod_sample_bank.h @brief FMOD Sample Bank (FSB1..FSB5) reader. */

#ifndef XXFCLIB_FORMAT_FMOD_SAMPLE_BANK_H
#define XXFCLIB_FORMAT_FMOD_SAMPLE_BANK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An FMOD Sample Bank: a table of sample headers followed by the raw
 * (codec-specific) data of every sample.  Every sample is one record; its
 * bytes are extracted exactly as stored (no WAV/MP3 wrapping), under the
 * sample's own name, or "%08u.dat" (the sample index) when it has none.
 * All fields are little-endian.
 *
 * FSB1 (FMOD 3):
 *   0x00 "FSB1", u32 samples, u32 data size, u32 unused
 *   0x10 samples x 0x40-byte headers: char name[32], u32 length in samples,
 *        u32 stored bytes, ...;  data follows back to back.
 *
 * FSB2 / FSB3 / FSB4:
 *   0x00 "FSBn", u32 samples, u32 header-table size, u32 data size,
 *   FSB3/4 add u32 version (0x30000 / 0x30001 / 0x40000) and u32 flags,
 *   FSB4 adds 24 more bytes (zero + hash); fixed header 0x10 / 0x18 / 0x30.
 *   Each sample header starts u16 own size, char name[30], u32 length in
 *   samples, u32 stored bytes; with flag 0x02 (basic headers) every header
 *   after the first is only { u32 length, u32 stored bytes }.  Data follows
 *   the header table back to back; FSB4 with flag 0x40 aligns every next
 *   sample to a 32-byte boundary of the bank.
 *
 * FSB5 (FMOD Studio):
 *   0x00 "FSB5", u32 version (0 or 1), u32 samples, u32 header-table size,
 *   u32 name-table size, u32 data size, u32 codec; fixed header 0x40 for
 *   version 0 and 0x3C for version 1.  Each sample header is a u64 whose bits
 *   7..33 hold the data offset / 32 and whose bit 0 announces a chain of
 *   u32-prefixed extra chunks.  The name table is one u32 offset per sample
 *   followed by NUL-terminated names.  A sample runs to the next sample's
 *   offset, the last one to the data size.
 */
typedef struct xx_fmod_sample_bank {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t version;   /**< 1..5, the digit of the signature. */
    uint32_t codec;     /**< FSB5 codec field, 0 otherwise. */
} xx_fmod_sample_bank;

typedef xx_fmod_sample_bank xx_fmod_sample_bank_t;

XXFC_API void xx_fmod_sample_bank_init(xx_fmod_sample_bank *archive,
                                       xx_io_device *device,
                                       int64_t base_address);
XXFC_API xx_fmod_sample_bank *xx_fmod_sample_bank_create(xx_io_device *device,
                                                         int64_t base_address);
XXFC_API void xx_fmod_sample_bank_destroy(xx_fmod_sample_bank *archive);
XXFC_API void xx_fmod_sample_bank_free(xx_fmod_sample_bank *archive);

XXFC_API bool xx_fmod_sample_bank_check_is_valid(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API bool xx_fmod_sample_bank_handle_base_info(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API int64_t xx_fmod_sample_bank_get_format_size(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API uint64_t xx_fmod_sample_bank_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_fmod_sample_bank_create_archive_records_reading(Abstractformat *self,
                                                   const xx_list_s *options,
                                                   xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_fmod_sample_bank_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_fmod_sample_bank_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_fmod_sample_bank_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_fmod_sample_bank_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_FMOD_SAMPLE_BANK_H */
