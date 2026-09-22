/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
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
 * @file xx_rar.h
 * @brief RAR 4.x and RAR 5.x archive reader and extractor.
 *
 * The reader handles stored and supported compressed entries, including RAR3/4
 * AES-128 and RAR5 AES-256 encrypted members and headers.
 * Ordered native volumes are prepared as a read-only normalized archive view;
 * headers are synthesized and packed payload ranges remain borrowed. Split
 * files require CRC32 or RAR5 BLAKE2sp, known sizes, and consistent metadata.
 * Every declared checksum is verified, including encrypted HASHMAC digests.
 * Unknown/malformed hash metadata is rejected. Unnumbered RAR4
 * volumes and pre-RAR3 encryption are unsupported. Native source/view offsets
 * are 64-bit; extracting a member still requires its packed and unpacked data
 * to fit the process address space.
 * Record/data-structure offsets refer to the normalized view, not source disks.
 * Preparation requires an explicit xx_io_multivolume device (one-element sets
 * are checked too). A plain device preserves inspection of an individual
 * volume; incomplete split members on that legacy path are not extractable.
 * Set the format-wide password before preparation to decrypt headers. Member
 * extraction also accepts an operation password overriding that default.
 * Changing a format password rebuilds its prepared view, so release active
 * iterators first. Missing passwords permit legacy encrypted-header detection
 * without exposing members; wrong passwords do not cache a prepared view.
 */

#ifndef XXFCLIB_FORMAT_RAR_H
#define XXFCLIB_FORMAT_RAR_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_rar xx_rar;
typedef struct xx_rar xx_rar_t;
typedef struct xx_rar XRar;

typedef enum xx_rar_version_e {
    XX_RAR_VERSION_UNKNOWN = 0,
    XX_RAR_VERSION_4 = 4,
    XX_RAR_VERSION_5 = 5
} xx_rar_version_t;

typedef enum xx_rar_data_struct_id_e {
    XX_RAR_DS_UNKNOWN = 0,
    XX_RAR_DS_SIGNATURE,
    XX_RAR_DS_MAIN_HEADER,
    XX_RAR_DS_FILE_HEADER,
    XX_RAR_DS_SERVICE_HEADER,
    XX_RAR_DS_ENCRYPTION_HEADER,
    XX_RAR_DS_END_HEADER,
    XX_RAR_DS_OTHER_HEADER,
    XX_RAR_DS_DATA
} xx_rar_data_struct_id_t;

enum {
    XX_RAR4_METHOD_STORE = 0x30,
    XX_RAR4_METHOD_FASTEST = 0x31,
    XX_RAR4_METHOD_FAST = 0x32,
    XX_RAR4_METHOD_NORMAL = 0x33,
    XX_RAR4_METHOD_GOOD = 0x34,
    XX_RAR4_METHOD_BEST = 0x35,
    XX_RAR5_METHOD_STORE = 0,
    XX_RAR5_METHOD_FASTEST = 1,
    XX_RAR5_METHOD_FAST = 2,
    XX_RAR5_METHOD_NORMAL = 3,
    XX_RAR5_METHOD_GOOD = 4,
    XX_RAR5_METHOD_BEST = 5
};

struct xx_rar {
    Abstractformat    format;              /**< Base structure; must be first. */
    xx_rar_version_t version;             /**< Detected archive generation. */
    int64_t          signature_offset;    /**< Absolute signature offset. */
    int64_t          first_header_offset; /**< Absolute first block offset. */
    uint64_t         number_of_records;   /**< Number of file headers. */
    void            *split_view;          /**< Private normalized multi-volume view. */
};

XXFC_API void xx_rar_init(xx_rar *rar, xx_io_device *dev, int64_t base_address);
XXFC_API xx_rar *xx_rar_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_rar_free(xx_rar *rar);
XXFC_API void xx_rar_destroy(xx_rar *rar);

XXFC_API bool xx_rar_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_rar_handle_split_format(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_rar_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_rar_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_rar_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rar_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rar_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rar_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rar_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rar_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

XXFC_API const char *xx_rar_data_struct_id_to_string(Abstractformat *self, uint32_t id);
XXFC_API uint32_t xx_rar_data_struct_string_to_id(Abstractformat *self, const char *name);
XXFC_API xx_data_struct_state *xx_rar_create_data_structs_reading(Abstractformat *self, xx_pd_struct *pd);
XXFC_API const xx_data_struct *xx_rar_get_current_data_struct(Abstractformat *self, xx_data_struct_state *state);
XXFC_API bool xx_rar_data_struct_move_to_next(Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
XXFC_API void xx_rar_free_data_structs_reading(Abstractformat *self, xx_data_struct_state *state);

XXFC_API xx_data_struct_record_state *xx_rar_create_data_struct_records_reading(Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd);
XXFC_API const xx_data_struct_record *xx_rar_get_current_data_struct_record(Abstractformat *self, xx_data_struct_record_state *state);
XXFC_API bool xx_rar_data_struct_record_move_to_next(Abstractformat *self, xx_data_struct_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rar_free_data_struct_records_reading(Abstractformat *self, xx_data_struct_record_state *state);

XXFC_API xx_rar_version_t xx_rar_get_version(const xx_rar *rar);
XXFC_API int64_t xx_rar_get_signature_offset(const xx_rar *rar);
XXFC_API uint64_t xx_rar_get_number_of_records(const xx_rar *rar);

static inline Abstractformat *xx_rar_to_format(xx_rar *rar) {
    return rar ? &rar->format : NULL;
}

static inline const Abstractformat *xx_rar_to_format_const(const xx_rar *rar) {
    return rar ? &rar->format : NULL;
}

static inline void XRar_init(xx_rar *rar, xx_io_device *dev, int64_t base_address) {
    xx_rar_init(rar, dev, base_address);
}

static inline xx_rar *XRar_create(xx_io_device *dev, int64_t base_address) {
    return xx_rar_create(dev, base_address);
}

static inline void XRar_free(xx_rar *rar) {
    xx_rar_free(rar);
}

static inline bool XRar_is_valid(xx_rar *rar, xx_pd_struct *pd) {
    return rar ? xx_format_is_valid(&rar->format, pd) : false;
}

static inline bool XRar_handle_base_info(xx_rar *rar, xx_pd_struct *pd) {
    return rar ? xx_rar_handle_base_info(&rar->format, pd) : false;
}

static inline xx_rar_version_t XRar_get_version(const xx_rar *rar) {
    return xx_rar_get_version(rar);
}

static inline int64_t XRar_get_signature_offset(const xx_rar *rar) {
    return xx_rar_get_signature_offset(rar);
}

static inline uint64_t XRar_get_number_of_records(const xx_rar *rar) {
    return xx_rar_get_number_of_records(rar);
}

static inline xx_archive_record_state *XRar_create_archive_records_reading(xx_rar *rar, const xx_list_s *options, xx_pd_struct *pd) {
    return rar ? xx_rar_create_archive_records_reading(&rar->format, options, pd) : NULL;
}

static inline const xx_archive_record *XRar_get_current_archive_record(xx_rar *rar, xx_archive_record_state *state) {
    return rar ? xx_rar_get_current_archive_record(&rar->format, state) : NULL;
}

static inline bool XRar_unpack_current_archive_record(xx_rar *rar, xx_archive_record_state *state, xx_pd_struct *pd) {
    return rar ? xx_rar_unpack_current_archive_record(&rar->format, state, pd) : false;
}

static inline bool XRar_archive_record_move_to_next(xx_rar *rar, xx_archive_record_state *state, xx_pd_struct *pd) {
    return rar ? xx_rar_archive_record_move_to_next(&rar->format, state, pd) : false;
}

static inline void XRar_free_archive_records_reading(xx_rar *rar, xx_archive_record_state *state) {
    if (rar) xx_rar_free_archive_records_reading(&rar->format, state);
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RAR_H */
