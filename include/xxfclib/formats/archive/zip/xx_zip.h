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
    XX_ZIP_DS_UNKNOWN = 0,                           /**< Unknown / unassigned */
    XX_ZIP_DS_LOCAL_FILE_HEADER,                     /**< Local file header (per-entry) */
    XX_ZIP_DS_DATA,                                  /**< Compressed/stored record payload data (per-entry, raw data) */
    XX_ZIP_DS_DATA_DESCRIPTOR,                       /**< Optional post-data descriptor (per-entry) */
    XX_ZIP_DS_CENTRAL_DIRECTORY_HEADER,              /**< Central directory file header (per-entry) */
    XX_ZIP_DS_END_OF_CENTRAL_DIRECTORY,              /**< End of Central Directory record (EOCD) */
    XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY,        /**< ZIP64 End of Central Directory record */
    XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR, /**< ZIP64 End of Central Directory locator */
    XX_ZIP_DS_EXTRA_FIELD                            /**< Generic extra-field ID/size/data record */
} xx_zip_data_struct_id_t;

/**
 * @brief Concrete ZIP archive format structure.
 * Inherits from Abstractformat by placing it as the first member.
 */
struct xx_zip {
    Abstractformat format;      /**< Base format structure (first member) */
    bool is_zip64;              /**< True if ZIP64 format detected */
    uint64_t number_of_records; /**< Number of central directory records */
    int64_t cd_offset;          /**< Offset of central directory in device */
    int64_t cd_size;            /**< Size of central directory in bytes */
    int64_t eocd_offset;        /**< Offset of EOCD record in device */
    char comment[256];          /**< Archive comment string */
    bool is_split;              /**< Native disk-relative split ZIP, not raw .001 chunks. */
    uint32_t split_disk_count;  /**< Validated count of supplied native ZIP disks. */
    uint32_t split_marker_size; /**< Optional initial PK0708/PK00 marker (0 or 4). */
    int64_t zip64_eocd_offset;  /**< Joined ZIP64 EOCD offset, or -1. */
    int64_t zip64_eocd_size;    /**< Validated ZIP64 EOCD record size. */
    void *analysis;             /**< Owned native signature-query cache. */
    /** A central directory entry before the EOCD's count did not hold up
     * while the records were read: the members after it are missing. */
    bool directory_damaged;
    /** Set by the archive opener before validation: the ZIP may follow other
     * data (a self-extractor stub, a script), found from its end records. */
    bool allow_leading_data;
    int64_t lhdr_cache_offset;  /**< Cached local file header block start offset */
    size_t lhdr_cache_size;     /**< Cached local file header block byte count */
    uint8_t *lhdr_cache;        /**< Owned dynamic buffer for read-ahead local header caching */
    uint8_t *cd_cache;          /**< Cached entire central directory block buffer */
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
/**
 * @brief Decode the selected record to a caller-owned writable device.
 *
 * Uses the reading state's password and extraction limits (with format-wide
 * parameters as fallback), authenticates encrypted entries, and verifies
 * unencrypted entries' CRC and exact size. No archive-derived path is opened.
 * Neither device is closed. A valid empty directory record writes no bytes.
 * A failed operation can have written a prefix; callers must stage output
 * when they require publication only after successful verification.
 */
XXFC_API bool xx_zip_unpack_current_archive_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd);
XXFC_API bool xx_zip_resolve_record_data_offset(xx_zip *zip, xx_archive_record *rec);
XXFC_API bool xx_zip_resolve_record_data_offset_ex(xx_zip *zip, xx_archive_record *rec, uint8_t *magic, bool *has_magic);
XXFC_API bool xx_zip_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zip_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Check that a named ZIP entry exists and its payload can be decoded.
 *
 * This helper is intended for ZIP-derived formats whose identity is defined by
 * a mandatory entry. The entry must be non-empty, unencrypted, no larger than
 * max_size in either packed or unpacked form, and its decoded CRC must match.
 */
XXFC_API bool xx_zip_has_valid_file(Abstractformat *self, const char *record_name, uint64_t max_size, xx_pd_struct *pd);

/* Native metadata inspection shared by ZIP-derived formats and DIE.
 * The input is borrowed. Returned strings use xx_str_free; other getter
 * results are borrowed until destruction. Selected member reads validate
 * their decoded size and CRC and never open an archive-derived output path. */
XXFC_API bool xx_zip_read_file(xx_zip *zip, const char *name, size_t limit, uint8_t **data, size_t *size, xx_pd_struct *pd);
XXFC_API bool xx_zip_analyze(xx_zip *zip, xx_pd_struct *pd);
XXFC_API void xx_zip_cleanup_analysis(xx_zip *zip);
XXFC_API const xx_list_s *xx_zip_get_record_names(const xx_zip *zip);
XXFC_API bool xx_zip_record_present(const xx_zip *zip, const char *name);
XXFC_API char *xx_zip_manifest_record(const xx_zip *zip, const char *key);
XXFC_API char *xx_zip_packagejson_record(const xx_zip *zip, const char *key);
XXFC_API const char *xx_zip_get_jvm_version(const xx_zip *zip);

/**
 * @brief Check for a decodable entry whose normalized path matches a pattern.
 *
 * Both slash styles in an entry name are treated as '/'. A matching name must
 * start with @p prefix, end with @p suffix, and contain at least one byte
 * between them. When @p single_component is true, that middle portion cannot
 * contain a path separator. Payload validation is identical to
 * xx_zip_has_valid_file().
 */
XXFC_API bool xx_zip_has_valid_file_pattern(Abstractformat *self, const char *prefix, const char *suffix, bool single_component, uint64_t max_size, xx_pd_struct *pd);

/* --- Stream Archive Records Writing / Packing --- */
/* DEFLATE/Deflate64 accept XX_META_ID_COMPRESSION_CANDIDATES (nonnegative
 * integer): positive overrides level, zero/absent uses level. Resolution is
 * per record > writer options > format extra parameters.
 * XX_META_ID_COMPRESSION_LAZY_MATCH uses the same resolution: zero/absent
 * is greedy; a positive match-length threshold enables one-byte lookahead
 * below that length. It does not override the compression level.
 * XX_META_ID_COMPRESSION_SHORT_MATCH_FILTER is boolean (default false),
 * rejecting length-3 matches beyond distance 4096 with the same precedence.
 * XX_META_ID_COMPRESSION_NICE_MATCH and XX_META_ID_COMPRESSION_GOOD_MATCH
 * are nonnegative match-length thresholds (zero disables) with the same precedence.
 * This is an in-memory
 * packing control; explicit STORE and other codec methods are unchanged. */
XXFC_API xx_archive_write_state *xx_zip_create_archive_records_writing(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API bool xx_zip_pack_archive_record(Abstractformat *self, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev,
                                         xx_pd_struct *pd);
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
static inline Abstractformat *xx_zip_to_format(xx_zip *zip)
{
    return zip ? &zip->format : NULL;
}

static inline const Abstractformat *xx_zip_to_format_const(const xx_zip *zip)
{
    return zip ? &zip->format : NULL;
}

/* XZip user-facing compatibility aliases */
static inline void XZip_init(xx_zip *zip, xx_io_device *dev, int64_t base) { xx_zip_init(zip, dev, base); }
static inline xx_zip *XZip_create(xx_io_device *dev, int64_t base) { return xx_zip_create(dev, base); }
static inline void XZip_free(xx_zip *zip) { xx_zip_free(zip); }
static inline void XZip_destroy(xx_zip *zip) { xx_zip_destroy(zip); }
static inline bool XZip_check_is_valid(xx_zip *zip, xx_pd_struct *pd) { return xx_zip_check_is_valid(xx_zip_to_format(zip), pd); }
static inline bool XZip_is_valid(xx_zip *zip, xx_pd_struct *pd) { return xx_zip_check_is_valid(xx_zip_to_format(zip), pd); }
static inline bool XZip_handle_base_info(xx_zip *zip, xx_pd_struct *pd) { return xx_zip_handle_base_info(xx_zip_to_format(zip), pd); }
static inline bool XZip_is_zip64(const xx_zip *zip) { return xx_zip_is_zip64(zip); }
static inline uint64_t XZip_get_number_of_records(const xx_zip *zip) { return xx_zip_get_number_of_records(zip); }
static inline int64_t XZip_get_cd_offset(const xx_zip *zip) { return xx_zip_get_cd_offset(zip); }
static inline int64_t XZip_get_cd_size(const xx_zip *zip) { return xx_zip_get_cd_size(zip); }
static inline int64_t XZip_get_eocd_offset(const xx_zip *zip) { return xx_zip_get_eocd_offset(zip); }
static inline const char *XZip_get_comment(const xx_zip *zip) { return xx_zip_get_comment(zip); }
static inline xx_archive_record_state *XZip_create_archive_records_reading(xx_zip *zip, const xx_list_s *options, xx_pd_struct *pd) { return xx_zip_create_archive_records_reading(xx_zip_to_format(zip), options, pd); }
static inline const xx_archive_record *XZip_get_current_archive_record(xx_zip *zip, xx_archive_record_state *state) { return xx_zip_get_current_archive_record(xx_zip_to_format(zip), state); }
static inline bool XZip_unpack_current_archive_record(xx_zip *zip, xx_archive_record_state *state, xx_pd_struct *pd) { return xx_zip_unpack_current_archive_record(xx_zip_to_format(zip), state, pd); }
static inline bool XZip_archive_record_move_to_next(xx_zip *zip, xx_archive_record_state *state, xx_pd_struct *pd) { return xx_zip_archive_record_move_to_next(xx_zip_to_format(zip), state, pd); }
static inline void XZip_free_archive_records_reading(xx_zip *zip, xx_archive_record_state *state) { xx_zip_free_archive_records_reading(xx_zip_to_format(zip), state); }
static inline const char *XZip_data_struct_id_to_string(xx_zip *zip, uint32_t id) { return xx_zip_data_struct_id_to_string(xx_zip_to_format(zip), id); }
static inline uint32_t XZip_data_struct_string_to_id(xx_zip *zip, const char *name) { return xx_zip_data_struct_string_to_id(xx_zip_to_format(zip), name); }
static inline xx_data_struct_state *XZip_create_data_structs_reading(xx_zip *zip, xx_pd_struct *pd) { return xx_zip_create_data_structs_reading(xx_zip_to_format(zip), pd); }
static inline const xx_data_struct *XZip_get_current_data_struct(xx_zip *zip, xx_data_struct_state *state) { return xx_zip_get_current_data_struct(xx_zip_to_format(zip), state); }
static inline bool XZip_data_struct_move_to_next(xx_zip *zip, xx_data_struct_state *state, xx_pd_struct *pd) { return xx_zip_data_struct_move_to_next(xx_zip_to_format(zip), state, pd); }
static inline void XZip_free_data_structs_reading(xx_zip *zip, xx_data_struct_state *state) { xx_zip_free_data_structs_reading(xx_zip_to_format(zip), state); }
static inline xx_data_struct_record_state *XZip_create_data_struct_records_reading(xx_zip *zip, const xx_data_struct *ds, xx_pd_struct *pd) { return xx_zip_create_data_struct_records_reading(xx_zip_to_format(zip), ds, pd); }
static inline const xx_data_struct_record *XZip_get_current_data_struct_record(xx_zip *zip, xx_data_struct_record_state *state) { return xx_zip_get_current_data_struct_record(xx_zip_to_format(zip), state); }
static inline bool XZip_data_struct_record_move_to_next(xx_zip *zip, xx_data_struct_record_state *state, xx_pd_struct *pd) { return xx_zip_data_struct_record_move_to_next(xx_zip_to_format(zip), state, pd); }
static inline void XZip_free_data_struct_records_reading(xx_zip *zip, xx_data_struct_record_state *state) { xx_zip_free_data_struct_records_reading(xx_zip_to_format(zip), state); }



#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_zip_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_zip_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_zip_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_zip_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_zip_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_ZIP_H */
