/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native read-only Apple DOS3.3 filesystem for raw16-sector logical images.
 * Supports18–50 tracks, DOS sector order and ProDOS block order. AUTO parses
 * both orders and refuses ambiguity; explicit selection handles empty disks.
 * VTOC, catalog allocation, catalog sector counts and all T/S lists are
 * validated, including sparse maps, loops, crosslinks and source bounds.
 * T/S list offsets must follow the standard consecutive0,122,244,... order.
 *
 * LOGICAL is the default extraction mode: Integer/Applesoft BASIC omit their
 * two-byte length headers; binary files omit the four-byte load-address/length
 * header. Sequential text ends at its first zero byte. Text with sparse holes
 * before its final mapped sector uses the full mapped span with zero-filled
 * holes. Other types use the full sector span. Text high-ASCII bytes are not
 * converted. Empty files are retained. DOS cannot distinguish a fully allocated
 * random-access text file from sequential text, and some custom binary loaders
 * understate their length: RAW_SECTORS mode preserves every mapped file sector,
 * headers and holes, without interpreting payload lengths. T/S metadata is
 * excluded from both modes. Filenames are safe unique ASCII host components.
 *
 * All source reads preserve its cursor; short I/O and cancellation supported.
 * MAX_MEMBER_SIZE bounds extracted bytes; MEMORY_LIMIT bounds the reader's
 * fixed sector/member views, iterator and transfer buffer (at most64KiB).
 * Operation options override format-wide options. Generic metadata/options
 * bookkeeping and the caller's device storage are outside this memory budget.
 * No13/32-sector,80-half-track, nibble/flux, deleted recovery or disk writing.
 * The DDD image decoder is a separate outer format, not this filesystem.
 * References: Beneath Apple DOS (5th printing); The DOS Manual;
 * https://ciderpress2.com/formatdoc/DOS-notes.html
 * https://ciderpress2.com/formatdoc/Unadorned-notes.html
 */
#ifndef XXFCLIB_FORMAT_APPLE_DOS33_H
#define XXFCLIB_FORMAT_APPLE_DOS33_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_apple_dos33_order_e {
    XX_APPLE_DOS33_ORDER_AUTO = 0,
    XX_APPLE_DOS33_ORDER_DOS = 1,
    XX_APPLE_DOS33_ORDER_PRODOS = 2
} xx_apple_dos33_order;
typedef enum xx_apple_dos33_mode_e {
    XX_APPLE_DOS33_MODE_LOGICAL = 0,
    XX_APPLE_DOS33_MODE_RAW_SECTORS = 1
} xx_apple_dos33_mode;
typedef struct xx_apple_dos33 {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t track_count;
    uint8_t volume_number;
    xx_apple_dos33_order requested_order, detected_order;
    xx_apple_dos33_mode mode;
} xx_apple_dos33;
typedef xx_apple_dos33 xx_apple_dos33_t;
typedef xx_apple_dos33 XAppleDOS33;
XXFC_API void xx_apple_dos33_init(xx_apple_dos33 *volume, xx_io_device *device, int64_t base_address);
XXFC_API void xx_apple_dos33_init_ex(xx_apple_dos33 *volume, xx_io_device *device,
    int64_t base_address, xx_apple_dos33_order order, xx_apple_dos33_mode mode);
XXFC_API xx_apple_dos33 *xx_apple_dos33_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_apple_dos33_destroy(xx_apple_dos33 *volume);
XXFC_API void xx_apple_dos33_free(xx_apple_dos33 *volume);
XXFC_API bool xx_apple_dos33_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_apple_dos33_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_apple_dos33_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_apple_dos33_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_apple_dos33_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_apple_dos33_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_apple_dos33_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_apple_dos33_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_apple_dos33_extract_record_to_device(Abstractformat *self,
    xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd);
XXFC_API void xx_apple_dos33_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);
static inline Abstractformat *xx_apple_dos33_to_format(xx_apple_dos33 *volume) { return volume ? &volume->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif
