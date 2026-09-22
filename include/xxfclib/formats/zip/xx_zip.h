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
 * @file xx_zip.h
 * @brief Concrete ZIP and ZIP64 archive format implementation.
 */

#ifndef XXFCLIB_FORMAT_ZIP_H
#define XXFCLIB_FORMAT_ZIP_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations and types */
typedef struct xx_zip xx_zip;
typedef struct xx_zip xx_zip_t;
typedef struct xx_zip XZip;
typedef struct xx_data_struct_field_desc xx_zip_field_desc;
typedef struct xx_data_struct_field_desc XZipFieldDesc;

/**
 * @brief Encryption method used when writing a ZIP entry.
 *
 * Set this value through XX_META_ID_ENCRYPTION_METHOD in the writer options or
 * on an individual archive record. A record value overrides the writer option.
 */
typedef enum xx_zip_encryption_method_e {
    XX_ZIP_ENCRYPTION_NONE = 0,
    XX_ZIP_ENCRYPTION_ZIPCRYPTO = 1,
    XX_ZIP_ENCRYPTION_AES_128 = 2,
    XX_ZIP_ENCRYPTION_AES_192 = 3,
    XX_ZIP_ENCRYPTION_AES_256 = 4
} xx_zip_encryption_method_t;
typedef xx_zip_encryption_method_t xx_zip_encryption_method;

/**
 * @brief Identifiers for ZIP-specific binary data structures (used with xx_data_struct.id).
 */
typedef enum xx_zip_data_struct_id_e {
    XX_ZIP_DS_UNKNOWN = 0,                            /**< Unknown / unassigned */
    XX_ZIP_DS_LOCAL_FILE_HEADER,                       /**< Local file header (per-entry) */
    XX_ZIP_DS_DATA,                                    /**< Compressed/stored record payload data (per-entry, raw data) */
    XX_ZIP_DS_DATA_DESCRIPTOR,                         /**< Optional post-data descriptor (per-entry) */
    XX_ZIP_DS_CENTRAL_DIRECTORY_HEADER,                /**< Central directory file header (per-entry) */
    XX_ZIP_DS_END_OF_CENTRAL_DIRECTORY,                /**< End of Central Directory record (EOCD) */
    XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY,          /**< ZIP64 End of Central Directory record */
    XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR   /**< ZIP64 End of Central Directory locator */
} xx_zip_data_struct_id_t;

/**
 * @brief Concrete ZIP archive format structure.
 * Inherits from Abstractformat by placing it as the first member.
 */
struct xx_zip {
    Abstractformat format;             /**< Base format structure (first member) */
    bool           is_zip64;           /**< True if ZIP64 format detected */
    uint64_t       number_of_records;  /**< Number of central directory records */
    int64_t        cd_offset;          /**< Offset of central directory in device */
    int64_t        cd_size;            /**< Size of central directory in bytes */
    int64_t        eocd_offset;        /**< Offset of EOCD record in device */
    char           comment[256];       /**< Archive comment string */
    bool           is_split;           /**< Native disk-relative split ZIP, not raw .001 chunks. */
    uint32_t       split_disk_count;   /**< Validated count of supplied native ZIP disks. */
    uint32_t       split_marker_size;  /**< Optional initial PK0708/PK00 marker (0 or 4). */
    int64_t        zip64_eocd_offset;   /**< Joined ZIP64 EOCD offset, or -1. */
    int64_t        zip64_eocd_size;     /**< Validated ZIP64 EOCD record size. */
};

/* --- Constructors & Lifecycle --- */
XXFC_API void xx_zip_init(xx_zip *zip, xx_io_device *dev, int64_t base_address);
XXFC_API xx_zip *xx_zip_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_zip_free(xx_zip *zip);
XXFC_API void xx_zip_destroy(xx_zip *zip);

/* --- Format Implementation Callbacks --- */
/** Prepare native split ZIP offsets on a borrowed multi-volume device.
 * ZIP/ZIP64 disk-relative offsets are mapped with checked 64-bit arithmetic.
 * Native split archives must start at device offset zero. Ordinary ZIP/ZIP64
 * and raw .zip.001 input retain their existing behavior. No archive bytes or
 * device ownership are changed.
 */
XXFC_API bool xx_zip_handle_split_format(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_zip_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_zip_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_zip_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_zip_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

/* --- Stream Archive Records Reading --- */
XXFC_API xx_archive_record_state *xx_zip_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zip_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zip_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zip_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zip_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Check that a named ZIP entry exists and its payload can be decoded.
 *
 * This helper is intended for ZIP-derived formats whose identity is defined by
 * a mandatory entry. The entry must be non-empty, unencrypted, no larger than
 * max_size in either packed or unpacked form, and its decoded CRC must match.
 */
XXFC_API bool xx_zip_has_valid_file(Abstractformat *self,
                                    const char *record_name,
                                    uint64_t max_size,
                                    xx_pd_struct *pd);

/**
 * @brief Check for a decodable entry whose normalized path matches a pattern.
 *
 * Both slash styles in an entry name are treated as '/'. A matching name must
 * start with @p prefix, end with @p suffix, and contain at least one byte
 * between them. When @p single_component is true, that middle portion cannot
 * contain a path separator. Payload validation is identical to
 * xx_zip_has_valid_file().
 */
XXFC_API bool xx_zip_has_valid_file_pattern(Abstractformat *self,
                                            const char *prefix,
                                            const char *suffix,
                                            bool single_component,
                                            uint64_t max_size,
                                            xx_pd_struct *pd);

/* --- Stream Archive Records Writing / Packing --- */
XXFC_API xx_archive_write_state *xx_zip_create_archive_records_writing(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API bool xx_zip_pack_archive_record(Abstractformat *self, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev, xx_pd_struct *pd);
XXFC_API bool xx_zip_finalize_archive_records_writing(Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd);
XXFC_API void xx_zip_free_archive_records_writing(Abstractformat *self, xx_archive_write_state *state);

/* --- Data Struct Id <-> String Conversion --- */
XXFC_API const char *xx_zip_data_struct_id_to_string(Abstractformat *self, uint32_t id);
XXFC_API uint32_t xx_zip_data_struct_string_to_id(Abstractformat *self, const char *name);

/* --- Stream Data Structs Reading (headers/tables of the ZIP format) --- */
XXFC_API xx_data_struct_state *xx_zip_create_data_structs_reading(Abstractformat *self, xx_pd_struct *pd);
XXFC_API const xx_data_struct *xx_zip_get_current_data_struct(Abstractformat *self, xx_data_struct_state *state);
XXFC_API bool xx_zip_data_struct_move_to_next(Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
XXFC_API void xx_zip_free_data_structs_reading(Abstractformat *self, xx_data_struct_state *state);

/* --- Stream Data Struct Records Reading (named fields of a single STRUCT-type data struct) --- */
XXFC_API xx_data_struct_record_state *xx_zip_create_data_struct_records_reading(Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd);
XXFC_API const xx_data_struct_record *xx_zip_get_current_data_struct_record(Abstractformat *self, xx_data_struct_record_state *state);
XXFC_API bool xx_zip_data_struct_record_move_to_next(Abstractformat *self, xx_data_struct_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zip_free_data_struct_records_reading(Abstractformat *self, xx_data_struct_record_state *state);

/* --- ZIP Getters & Properties --- */
XXFC_API bool xx_zip_is_zip64(const xx_zip *zip);
XXFC_API uint64_t xx_zip_get_number_of_records(const xx_zip *zip);
XXFC_API int64_t xx_zip_get_cd_offset(const xx_zip *zip);
XXFC_API int64_t xx_zip_get_cd_size(const xx_zip *zip);
XXFC_API int64_t xx_zip_get_eocd_offset(const xx_zip *zip);
XXFC_API const char *xx_zip_get_comment(const xx_zip *zip);

/* Cast helpers */
static inline Abstractformat *xx_zip_to_format(xx_zip *zip) {
    return zip ? &zip->format : NULL;
}

static inline const Abstractformat *xx_zip_to_format_const(const xx_zip *zip) {
    return zip ? &zip->format : NULL;
}

/* User-facing aliases without xx_ prefix */
static inline void XZip_init(xx_zip *zip, xx_io_device *dev, int64_t base_address) {
    xx_zip_init(zip, dev, base_address);
}

static inline xx_zip *XZip_create(xx_io_device *dev, int64_t base_address) {
    return xx_zip_create(dev, base_address);
}

static inline void XZip_free(xx_zip *zip) {
    xx_zip_free(zip);
}

static inline bool XZip_check_is_valid(xx_zip *zip, xx_pd_struct *pd) {
    return zip ? xx_zip_check_is_valid(&zip->format, pd) : false;
}

static inline bool XZip_handle_base_info(xx_zip *zip, xx_pd_struct *pd) {
    return zip ? xx_zip_handle_base_info(&zip->format, pd) : false;
}

static inline bool XZip_is_valid(xx_zip *zip, xx_pd_struct *pd) {
    return zip ? xx_format_is_valid(&zip->format, pd) : false;
}

static inline bool XZip_is_zip64(const xx_zip *zip) {
    return xx_zip_is_zip64(zip);
}

static inline uint64_t XZip_get_number_of_records(const xx_zip *zip) {
    return xx_zip_get_number_of_records(zip);
}

static inline int64_t XZip_get_cd_offset(const xx_zip *zip) {
    return xx_zip_get_cd_offset(zip);
}

static inline int64_t XZip_get_cd_size(const xx_zip *zip) {
    return xx_zip_get_cd_size(zip);
}

static inline int64_t XZip_get_eocd_offset(const xx_zip *zip) {
    return xx_zip_get_eocd_offset(zip);
}

static inline const char *XZip_get_comment(const xx_zip *zip) {
    return xx_zip_get_comment(zip);
}

static inline xx_archive_record_state *XZip_create_archive_records_reading(xx_zip *zip, const xx_list_s *options, xx_pd_struct *pd) {
    return zip ? xx_zip_create_archive_records_reading(&zip->format, options, pd) : NULL;
}

static inline const xx_archive_record *XZip_get_current_archive_record(xx_zip *zip, xx_archive_record_state *state) {
    return zip ? xx_zip_get_current_archive_record(&zip->format, state) : NULL;
}

static inline bool XZip_unpack_current_archive_record(xx_zip *zip, xx_archive_record_state *state, xx_pd_struct *pd) {
    return zip ? xx_zip_unpack_current_archive_record(&zip->format, state, pd) : false;
}

static inline bool XZip_archive_record_move_to_next(xx_zip *zip, xx_archive_record_state *state, xx_pd_struct *pd) {
    return zip ? xx_zip_archive_record_move_to_next(&zip->format, state, pd) : false;
}

static inline void XZip_free_archive_records_reading(xx_zip *zip, xx_archive_record_state *state) {
    if (zip) xx_zip_free_archive_records_reading(&zip->format, state);
}

static inline xx_archive_write_state *XZip_create_archive_records_writing(xx_zip *zip, const xx_list_s *options, xx_pd_struct *pd) {
    return zip ? xx_zip_create_archive_records_writing(&zip->format, options, pd) : NULL;
}

static inline bool XZip_pack_archive_record(xx_zip *zip, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev, xx_pd_struct *pd) {
    return zip ? xx_zip_pack_archive_record(&zip->format, state, record, source_dev, pd) : false;
}

static inline bool XZip_finalize_archive_records_writing(xx_zip *zip, xx_archive_write_state *state, xx_pd_struct *pd) {
    return zip ? xx_zip_finalize_archive_records_writing(&zip->format, state, pd) : false;
}

static inline void XZip_free_archive_records_writing(xx_zip *zip, xx_archive_write_state *state) {
    if (zip) xx_zip_free_archive_records_writing(&zip->format, state);
}

static inline const char *XZip_data_struct_id_to_string(xx_zip *zip, uint32_t id) {
    return zip ? xx_zip_data_struct_id_to_string(&zip->format, id) : "UNKNOWN";
}

static inline uint32_t XZip_data_struct_string_to_id(xx_zip *zip, const char *name) {
    return zip ? xx_zip_data_struct_string_to_id(&zip->format, name) : (uint32_t)XX_ZIP_DS_UNKNOWN;
}

static inline xx_data_struct_state *XZip_create_data_structs_reading(xx_zip *zip, xx_pd_struct *pd) {
    return zip ? xx_zip_create_data_structs_reading(&zip->format, pd) : NULL;
}

static inline const xx_data_struct *XZip_get_current_data_struct(xx_zip *zip, xx_data_struct_state *state) {
    return zip ? xx_zip_get_current_data_struct(&zip->format, state) : NULL;
}

static inline bool XZip_data_struct_move_to_next(xx_zip *zip, xx_data_struct_state *state, xx_pd_struct *pd) {
    return zip ? xx_zip_data_struct_move_to_next(&zip->format, state, pd) : false;
}

static inline void XZip_free_data_structs_reading(xx_zip *zip, xx_data_struct_state *state) {
    if (zip) xx_zip_free_data_structs_reading(&zip->format, state);
}

static inline xx_data_struct_record_state *XZip_create_data_struct_records_reading(xx_zip *zip, const xx_data_struct *ds, xx_pd_struct *pd) {
    return zip ? xx_zip_create_data_struct_records_reading(&zip->format, ds, pd) : NULL;
}

static inline const xx_data_struct_record *XZip_get_current_data_struct_record(xx_zip *zip, xx_data_struct_record_state *state) {
    return zip ? xx_zip_get_current_data_struct_record(&zip->format, state) : NULL;
}

static inline bool XZip_data_struct_record_move_to_next(xx_zip *zip, xx_data_struct_record_state *state, xx_pd_struct *pd) {
    return zip ? xx_zip_data_struct_record_move_to_next(&zip->format, state, pd) : false;
}

static inline void XZip_free_data_struct_records_reading(xx_zip *zip, xx_data_struct_record_state *state) {
    if (zip) xx_zip_free_data_struct_records_reading(&zip->format, state);
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZIP_H */
