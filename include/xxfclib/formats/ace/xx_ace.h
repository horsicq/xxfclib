/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ace.h @brief ACE 1.x and ACE 2.x archive reader. */
#ifndef XXFCLIB_FORMAT_ACE_H
#define XXFCLIB_FORMAT_ACE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ace xx_ace;
typedef struct xx_ace xx_ace_t;
typedef struct xx_ace XAce;

struct xx_ace {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint8_t extract_version;
    uint8_t create_version;
    void *internal;
};

XXFC_API void xx_ace_init(xx_ace *ace, xx_io_device *dev, int64_t base_address);
XXFC_API xx_ace *xx_ace_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_ace_destroy(xx_ace *ace);
XXFC_API void xx_ace_free(xx_ace *ace);

XXFC_API bool xx_ace_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ace_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ace_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_ace_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_ace_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ace_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ace_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ace_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ace_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_ace_to_format(xx_ace *ace) { return ace ? &ace->format : NULL; }
static inline void XAce_init(xx_ace *ace, xx_io_device *dev, int64_t base) { xx_ace_init(ace, dev, base); }
static inline bool XAce_is_valid(xx_ace *ace, xx_pd_struct *pd) { return ace && xx_format_is_valid(&ace->format, pd); }
#ifdef __cplusplus
}
#endif
#endif
