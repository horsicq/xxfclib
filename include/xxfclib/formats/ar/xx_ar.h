/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_ar.h
 * @brief Unix ar archive reader (System V, GNU, and BSD name variants),
 *        including Debian packages and Microsoft .lib static and import
 *        libraries (linker members, "/<NAME>/" members, NUL-terminated long
 *        names, absolute and duplicated member names).
 */

#ifndef XXFCLIB_FORMAT_AR_H
#define XXFCLIB_FORMAT_AR_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ar xx_ar;
typedef struct xx_ar xx_ar_t;
typedef struct xx_ar XAr;

typedef enum xx_ar_data_struct_id_e {
    XX_AR_DS_UNKNOWN = 0,
    XX_AR_DS_GLOBAL_HEADER,
    XX_AR_DS_MEMBER_HEADER,
    XX_AR_DS_BSD_EXTENDED_NAME,
    XX_AR_DS_DATA,
    XX_AR_DS_PADDING
} xx_ar_data_struct_id_t;

struct xx_ar {
    Abstractformat format;             /**< Base format structure (first member). */
    uint64_t number_of_records;        /**< User-visible archive members. */
    uint64_t number_of_members;        /**< All members, including index/name tables. */
    int64_t archive_end;               /**< Absolute end offset in the device. */
    bool is_thin;                      /**< Thin archives are detected but unsupported. */
};

XXFC_API void xx_ar_init(xx_ar *ar, xx_io_device *dev, int64_t base_address);
XXFC_API xx_ar *xx_ar_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_ar_free(xx_ar *ar);
XXFC_API void xx_ar_destroy(xx_ar *ar);

XXFC_API bool xx_ar_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ar_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ar_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_ar_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ar_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ar_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ar_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ar_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ar_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

XXFC_API const char *xx_ar_data_struct_id_to_string(Abstractformat *self, uint32_t id);
XXFC_API uint32_t xx_ar_data_struct_string_to_id(Abstractformat *self, const char *name);
XXFC_API xx_data_struct_state *xx_ar_create_data_structs_reading(Abstractformat *self, xx_pd_struct *pd);
XXFC_API const xx_data_struct *xx_ar_get_current_data_struct(Abstractformat *self, xx_data_struct_state *state);
XXFC_API bool xx_ar_data_struct_move_to_next(Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
XXFC_API void xx_ar_free_data_structs_reading(Abstractformat *self, xx_data_struct_state *state);

XXFC_API xx_data_struct_record_state *xx_ar_create_data_struct_records_reading(Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd);
XXFC_API const xx_data_struct_record *xx_ar_get_current_data_struct_record(Abstractformat *self, xx_data_struct_record_state *state);
XXFC_API bool xx_ar_data_struct_record_move_to_next(Abstractformat *self, xx_data_struct_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ar_free_data_struct_records_reading(Abstractformat *self, xx_data_struct_record_state *state);

XXFC_API uint64_t xx_ar_get_number_of_records(const xx_ar *ar);
XXFC_API uint64_t xx_ar_get_number_of_members(const xx_ar *ar);
XXFC_API int64_t xx_ar_get_archive_end(const xx_ar *ar);
XXFC_API bool xx_ar_is_thin(const xx_ar *ar);

static inline Abstractformat *xx_ar_to_format(xx_ar *ar) {
    return ar ? &ar->format : NULL;
}

static inline const Abstractformat *xx_ar_to_format_const(const xx_ar *ar) {
    return ar ? &ar->format : NULL;
}

static inline void XAr_init(xx_ar *ar, xx_io_device *dev, int64_t base_address) {
    xx_ar_init(ar, dev, base_address);
}

static inline xx_ar *XAr_create(xx_io_device *dev, int64_t base_address) {
    return xx_ar_create(dev, base_address);
}

static inline void XAr_free(xx_ar *ar) {
    xx_ar_free(ar);
}

static inline bool XAr_is_valid(xx_ar *ar, xx_pd_struct *pd) {
    return ar ? xx_format_is_valid(&ar->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_AR_H */
