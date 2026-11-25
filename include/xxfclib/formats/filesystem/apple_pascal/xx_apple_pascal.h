/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_apple_pascal.h @brief Apple II UCSD Pascal filesystem reader.
 *
 * Lists and extracts the actual contiguous file extents, including raw Pascal
 * .TEXT and .CODE files; it does not export their text or executable contents
 * to another representation. The packed 26-byte directory is at blocks2–5.
 * Supports at most 77 regular files and 65535 512-byte blocks per volume.
 * .BAD block reservations and boot-discarded zero-byte/pending records are
 * excluded from the regular-file list. Unknown securedir records are refused.
 *
 * Linear block order is used for .po images and hard-disk partitions. DOS
 * sector order is supported for 140KiB 16-sector Apple II floppies. AUTO
 * validates both layouts and refuses ambiguity; size alone is insufficient.
 * Explicit selection is available for ambiguous images. All image reads
 * preserve the input position, and base_address is the volume's byte offset.
 * MAX_MEMBER_SIZE limits logical file length. MEMORY_LIMIT includes the
 * retained directory view/names, iterator and bounded copy buffer; initial
 * parser transient allocations and generic archive metadata are excluded.
 * No writing, nibble/flux decoding, or deleted-file recovery is implemented.
 *
 * Layout references:
 * https://ciderpress2.com/formatdoc/Pascal-notes.html
 * https://ciderpress2.com/formatdoc/Unadorned-notes.html
 * Apple II Pascal 1.3, directory specification on page IV-15.
 */
#ifndef XXFCLIB_FORMAT_APPLE_PASCAL_H
#define XXFCLIB_FORMAT_APPLE_PASCAL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_apple_pascal_order_e {
    XX_APPLE_PASCAL_ORDER_AUTO = 0,
    XX_APPLE_PASCAL_ORDER_BLOCK = 1,
    XX_APPLE_PASCAL_ORDER_DOS = 2
} xx_apple_pascal_order;
typedef struct xx_apple_pascal {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t block_count;
    uint16_t directory_file_count;
    char volume_name[8];
    xx_apple_pascal_order requested_order;
    xx_apple_pascal_order detected_order;
} xx_apple_pascal;
typedef xx_apple_pascal xx_apple_pascal_t;
typedef xx_apple_pascal XApplePascal;

XXFC_API void xx_apple_pascal_init(xx_apple_pascal *volume, xx_io_device *device, int64_t base_address);
XXFC_API void xx_apple_pascal_init_order(xx_apple_pascal *volume, xx_io_device *device, int64_t base_address, xx_apple_pascal_order order);
XXFC_API xx_apple_pascal *xx_apple_pascal_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_apple_pascal_destroy(xx_apple_pascal *volume);
XXFC_API void xx_apple_pascal_free(xx_apple_pascal *volume);
XXFC_API bool xx_apple_pascal_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_apple_pascal_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_apple_pascal_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_apple_pascal_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_apple_pascal_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_apple_pascal_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_apple_pascal_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_apple_pascal_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
/** Write the current member to a borrowed device, or verify its readable
 *  bytes when destination is NULL. The input cursor is preserved. */
XXFC_API bool xx_apple_pascal_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd);
XXFC_API void xx_apple_pascal_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);
static inline Abstractformat *xx_apple_pascal_to_format(xx_apple_pascal *volume)
{
    return volume ? &volume->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
