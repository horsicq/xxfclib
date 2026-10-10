/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native read-only TI-99/4A sector-based DSK filesystem reader.
 * Standard 360/720/1440 x 256-byte images, VIB/FDR/cluster directory.
 * PROGRAM outputs exact EOF-trimmed bytes; FIXED outputs concatenated logical
 * records; DISPLAY VARIABLE outputs records with LF; INTERNAL VARIABLE
 * outputs length-prefixed records. No TIFILES header is prepended. Metadata,
 * fragmented clusters, allocation and record boundaries are validated.
 * No track dumps, HFE, nonstandard CF geometry, deleted recovery or TI
 * proprietary protection/record codecs. Source cursor, short I/O, limits,
 * cancellation and staged overwrite are supported.
 * MEMORY_LIMIT includes retained view and copy buffer, excluding parser
 * temporaries and generic record metadata.
 * Primary: TI disk peripheral software specification:
 * https://ftp.whtech.com/datasheets%20and%20manuals/Datasheets%20-%20TI/TI99%20TI%20Software%20Specs%20for%20Disk%20Peripheral.pdf
 * Layout cross-check: https://www.unige.ch/medecine/nouspikel/ti99/disks.htm
 */
#ifndef XXFCLIB_FORMAT_TI99_DSK_H
#define XXFCLIB_FORMAT_TI99_DSK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_ti99_dsk_variant_e {
    XX_TI99_DSK_AUTO = 0,
    XX_TI99_DSK_SS_SD = 1,
    XX_TI99_DSK_DS_SD = 2,
    XX_TI99_DSK_DS_DD = 3
} xx_ti99_dsk_variant;
typedef struct xx_ti99_dsk_s {
    Abstractformat format;
    xx_ti99_dsk_variant variant;
    uint32_t sector_count;
    uint64_t number_of_records;
} xx_ti99_dsk;
typedef xx_ti99_dsk xx_ti99_dsk_t;
typedef xx_ti99_dsk XTI99_DSK;
XXFC_API void xx_ti99_dsk_init(xx_ti99_dsk *, xx_io_device *, int64_t);
XXFC_API void xx_ti99_dsk_init_ex(xx_ti99_dsk *, xx_io_device *, int64_t, xx_ti99_dsk_variant);
XXFC_API xx_ti99_dsk *xx_ti99_dsk_create(xx_io_device *, int64_t);
XXFC_API void xx_ti99_dsk_destroy(xx_ti99_dsk *);
XXFC_API void xx_ti99_dsk_free(xx_ti99_dsk *);
XXFC_API bool xx_ti99_dsk_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_ti99_dsk_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_ti99_dsk_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_ti99_dsk_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_ti99_dsk_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_ti99_dsk_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_ti99_dsk_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_ti99_dsk_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_ti99_dsk_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_ti99_dsk_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_ti99_dsk_to_format(xx_ti99_dsk *v)
{
    return v ? &v->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
