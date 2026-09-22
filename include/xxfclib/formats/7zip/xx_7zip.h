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
 * @file xx_7zip.h
 * @brief Read-only 7-Zip archive format implementation.
 */

#ifndef XXFCLIB_FORMAT_7ZIP_H
#define XXFCLIB_FORMAT_7ZIP_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_7zip xx_7zip;
typedef struct xx_7zip xx_7zip_t;
typedef struct xx_7zip X7Zip;

typedef enum xx_7zip_data_struct_id_e {
    XX_7ZIP_DS_UNKNOWN = 0,
    XX_7ZIP_DS_SIGNATURE_HEADER,
    XX_7ZIP_DS_PACKED_DATA,
    XX_7ZIP_DS_NEXT_HEADER
} xx_7zip_data_struct_id_t;

/**
 * @brief Concrete 7-Zip archive format.
 *
 * The implementation is deliberately read-only.  It enumerates all ordinary
 * records and extracts Copy, LZMA, LZMA2, BZip2, PPMd7, Brotli, LZ4, LZ5,
 * Lizard, Zstandard, and the BCJ/x86, BCJ2, PPC, IA64, ARM, ARMT, SPARC,
 * ARM64, and RISC-V branch filters. A native 7z AES-256-CBC stage is supported for
 * simple AES-to-codec chains. Other coder graphs remain enumerable and fail
 * extraction.
 */
struct xx_7zip {
    Abstractformat format;             /**< Base format (must be first). */
    uint64_t       number_of_records;  /**< FilesInfo record count. */
    int64_t        next_header_offset; /**< Absolute device offset. */
    uint64_t       next_header_size;   /**< Stored next-header byte count. */
    uint32_t       next_header_crc;    /**< CRC-32 from the start header. */
    bool           is_header_encoded; /**< Stored next header is encoded. */
    void          *internal;           /**< Private parsed representation. */
};

XXFC_API void xx_7zip_init(xx_7zip *archive, xx_io_device *dev, int64_t base_address);
XXFC_API xx_7zip *xx_7zip_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_7zip_free(xx_7zip *archive);
XXFC_API void xx_7zip_destroy(xx_7zip *archive);

XXFC_API bool xx_7zip_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_7zip_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_7zip_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_7zip_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_7zip_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_7zip_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_7zip_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_7zip_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_7zip_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

XXFC_API const char *xx_7zip_data_struct_id_to_string(Abstractformat *self, uint32_t id);
XXFC_API uint32_t xx_7zip_data_struct_string_to_id(Abstractformat *self, const char *name);
XXFC_API xx_data_struct_state *xx_7zip_create_data_structs_reading(Abstractformat *self, xx_pd_struct *pd);
XXFC_API const xx_data_struct *xx_7zip_get_current_data_struct(Abstractformat *self, xx_data_struct_state *state);
XXFC_API bool xx_7zip_data_struct_move_to_next(Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
XXFC_API void xx_7zip_free_data_structs_reading(Abstractformat *self, xx_data_struct_state *state);

XXFC_API xx_data_struct_record_state *xx_7zip_create_data_struct_records_reading(Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd);
XXFC_API const xx_data_struct_record *xx_7zip_get_current_data_struct_record(Abstractformat *self, xx_data_struct_record_state *state);
XXFC_API bool xx_7zip_data_struct_record_move_to_next(Abstractformat *self, xx_data_struct_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_7zip_free_data_struct_records_reading(Abstractformat *self, xx_data_struct_record_state *state);

XXFC_API uint64_t xx_7zip_get_number_of_records(const xx_7zip *archive);
XXFC_API int64_t xx_7zip_get_next_header_offset(const xx_7zip *archive);
XXFC_API uint64_t xx_7zip_get_next_header_size(const xx_7zip *archive);
XXFC_API bool xx_7zip_is_header_encoded(const xx_7zip *archive);

static inline Abstractformat *xx_7zip_to_format(xx_7zip *archive) {
    return archive ? &archive->format : NULL;
}

static inline const Abstractformat *xx_7zip_to_format_const(const xx_7zip *archive) {
    return archive ? &archive->format : NULL;
}

static inline void X7Zip_init(xx_7zip *archive, xx_io_device *dev, int64_t base_address) {
    xx_7zip_init(archive, dev, base_address);
}

static inline xx_7zip *X7Zip_create(xx_io_device *dev, int64_t base_address) {
    return xx_7zip_create(dev, base_address);
}

static inline void X7Zip_free(xx_7zip *archive) {
    xx_7zip_free(archive);
}

static inline bool X7Zip_is_valid(xx_7zip *archive, xx_pd_struct *pd) {
    return archive ? xx_format_is_valid(&archive->format, pd) : false;
}

static inline bool X7Zip_check_is_valid(xx_7zip *archive, xx_pd_struct *pd) {
    return archive ? xx_7zip_check_is_valid(&archive->format, pd) : false;
}

static inline bool X7Zip_handle_base_info(xx_7zip *archive, xx_pd_struct *pd) {
    return archive ? xx_format_handle_base_info(&archive->format, pd) : false;
}

static inline uint64_t X7Zip_get_number_of_records(const xx_7zip *archive) {
    return xx_7zip_get_number_of_records(archive);
}

static inline int64_t X7Zip_get_next_header_offset(const xx_7zip *archive) {
    return xx_7zip_get_next_header_offset(archive);
}

static inline uint64_t X7Zip_get_next_header_size(const xx_7zip *archive) {
    return xx_7zip_get_next_header_size(archive);
}

static inline bool X7Zip_is_header_encoded(const xx_7zip *archive) {
    return xx_7zip_is_header_encoded(archive);
}

static inline xx_archive_record_state *X7Zip_create_archive_records_reading(xx_7zip *archive, const xx_list_s *options, xx_pd_struct *pd) {
    return archive ? xx_7zip_create_archive_records_reading(&archive->format, options, pd) : NULL;
}

static inline const xx_archive_record *X7Zip_get_current_archive_record(xx_7zip *archive, xx_archive_record_state *state) {
    return archive ? xx_7zip_get_current_archive_record(&archive->format, state) : NULL;
}

static inline bool X7Zip_unpack_current_archive_record(xx_7zip *archive, xx_archive_record_state *state, xx_pd_struct *pd) {
    return archive ? xx_7zip_unpack_current_archive_record(&archive->format, state, pd) : false;
}

static inline bool X7Zip_archive_record_move_to_next(xx_7zip *archive, xx_archive_record_state *state, xx_pd_struct *pd) {
    return archive ? xx_7zip_archive_record_move_to_next(&archive->format, state, pd) : false;
}

static inline void X7Zip_free_archive_records_reading(xx_7zip *archive, xx_archive_record_state *state) {
    if (archive) xx_7zip_free_archive_records_reading(&archive->format, state);
}

static inline const char *X7Zip_data_struct_id_to_string(xx_7zip *archive, uint32_t id) {
    return archive ? xx_7zip_data_struct_id_to_string(&archive->format, id) : "UNKNOWN";
}

static inline uint32_t X7Zip_data_struct_string_to_id(xx_7zip *archive, const char *name) {
    return archive ? xx_7zip_data_struct_string_to_id(&archive->format, name) : XX_7ZIP_DS_UNKNOWN;
}

static inline xx_data_struct_state *X7Zip_create_data_structs_reading(xx_7zip *archive, xx_pd_struct *pd) {
    return archive ? xx_7zip_create_data_structs_reading(&archive->format, pd) : NULL;
}

static inline const xx_data_struct *X7Zip_get_current_data_struct(xx_7zip *archive, xx_data_struct_state *state) {
    return archive ? xx_7zip_get_current_data_struct(&archive->format, state) : NULL;
}

static inline bool X7Zip_data_struct_move_to_next(xx_7zip *archive, xx_data_struct_state *state, xx_pd_struct *pd) {
    return archive ? xx_7zip_data_struct_move_to_next(&archive->format, state, pd) : false;
}

static inline void X7Zip_free_data_structs_reading(xx_7zip *archive, xx_data_struct_state *state) {
    if (archive) xx_7zip_free_data_structs_reading(&archive->format, state);
}

static inline xx_data_struct_record_state *X7Zip_create_data_struct_records_reading(xx_7zip *archive, const xx_data_struct *ds, xx_pd_struct *pd) {
    return archive ? xx_7zip_create_data_struct_records_reading(&archive->format, ds, pd) : NULL;
}

static inline const xx_data_struct_record *X7Zip_get_current_data_struct_record(xx_7zip *archive, xx_data_struct_record_state *state) {
    return archive ? xx_7zip_get_current_data_struct_record(&archive->format, state) : NULL;
}

static inline bool X7Zip_data_struct_record_move_to_next(xx_7zip *archive, xx_data_struct_record_state *state, xx_pd_struct *pd) {
    return archive ? xx_7zip_data_struct_record_move_to_next(&archive->format, state, pd) : false;
}

static inline void X7Zip_free_data_struct_records_reading(xx_7zip *archive, xx_data_struct_record_state *state) {
    if (archive) xx_7zip_free_data_struct_records_reading(&archive->format, state);
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_7ZIP_H */
