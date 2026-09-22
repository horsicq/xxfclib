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
 * @file xx_format.h
 * @brief Abstractformat structure with function pointer vtable and format
 * interfaces.
 */

#ifndef XX_FORMAT_H
#define XX_FORMAT_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/formats/xx_memory_map.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/list/xx_list.h"
#include "xxfclib/var/xx_var.h"
#include "xxfclib/xxfc_defs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard type helpers */
#ifndef _QINT64_DEFINED
#define _QINT64_DEFINED
typedef int64_t qint64;
typedef int64_t qint64_t;
typedef uint64_t quint64;
typedef uint64_t quint64_t;
#endif

/* Forward declaration and types */
typedef struct Abstractformat Abstractformat;
typedef struct Abstractformat xx_format;
typedef struct Abstractformat xx_format_t;
typedef struct Abstractformat xx_abstract_format;
typedef struct Abstractformat AbstractFormat;

/* Forward declaration and types for archive record & metadata */
typedef struct xx_meta xx_meta;
typedef struct xx_meta xx_meta_t;
typedef struct xx_archive_record xx_archive_record;
typedef struct xx_archive_record xx_archive_record_t;
typedef struct xx_archive_record ARCHIVE_RECORD;
typedef struct xx_archive_record ArchiveRecord;
typedef struct xx_archive_record xx_archive_entry;
typedef struct xx_archive_record xx_archive_entry_t;
typedef struct xx_archive_record_state xx_archive_record_state;
typedef struct xx_archive_record_state xx_archive_record_state_t;
typedef struct xx_archive_record_state ARCHIVE_RECORD_STATE;
typedef struct xx_archive_record_state ArchiveRecordState;
typedef struct xx_archive_write_state xx_archive_write_state;
typedef struct xx_archive_write_state xx_archive_write_state_t;
typedef struct xx_archive_write_state ARCHIVE_WRITE_STATE;
typedef struct xx_archive_write_state ArchiveWriteState;
typedef struct xx_archive_write_state xx_archive_pack_state;
typedef struct xx_archive_write_state xx_archive_pack_state_t;

/* Forward declaration and types for generic format data structs (headers/tables/records) */
typedef struct xx_data_struct xx_data_struct;
typedef struct xx_data_struct xx_data_struct_t;
typedef struct xx_data_struct XDataStruct;
typedef struct xx_data_struct_state xx_data_struct_state;
typedef struct xx_data_struct_state xx_data_struct_state_t;
typedef struct xx_data_struct_state XDataStructState;

typedef struct xx_data_struct_record xx_data_struct_record;
typedef struct xx_data_struct_record xx_data_struct_record_t;
typedef struct xx_data_struct_record XDataStructRecord;
typedef struct xx_data_struct_record_state xx_data_struct_record_state;
typedef struct xx_data_struct_record_state xx_data_struct_record_state_t;
typedef struct xx_data_struct_record_state XDataStructRecordState;

typedef struct xx_data_struct_field_desc xx_data_struct_field_desc;
typedef struct xx_data_struct_field_desc xx_data_struct_field_desc_t;
typedef struct xx_data_struct_field_desc XDataStructFieldDesc;
typedef struct xx_data_struct_field_desc xx_data_struct_record_desc;
typedef struct xx_data_struct_field_desc xx_data_struct_record_desc_t;
typedef struct xx_data_struct_field_desc XDataStructRecordDesc;
typedef struct xx_data_struct_field_desc xx_field_desc;
typedef struct xx_data_struct_field_desc xx_field_desc_t;
typedef struct xx_data_struct_field_desc XFieldDesc;

/**
 * @brief Metadata identifier tags for archive records (including ZIP records and options).
 */
typedef enum xx_meta_id_e {
  XX_META_ID_UNKNOWN = 0,                 /**< 0 -> Unknown / unassigned */
  XX_META_ID_ORIGINAL_NAME,               /**< Original name (Unicode dynamic string: wchar_t* or UTF-8) */
  XX_META_ID_UNCOMPRESSED_SIZE,           /**< Uncompressed file size in bytes (uint64_t) */
  XX_META_ID_COMPRESSED_SIZE,             /**< Compressed data size in bytes (uint64_t) */
  XX_META_ID_CRC32,                       /**< CRC32 checksum (uint32_t) */
  XX_META_ID_COMPRESSION_METHOD,          /**< Compression method ID (uint16_t/uint32_t) */
  XX_META_ID_ATTRIBUTES,                  /**< General / external file attributes (uint32_t) */
  XX_META_ID_TIMESTAMP,                   /**< Modification timestamp (uint64_t) */
  XX_META_ID_LAST_MOD_TIME,               /**< DOS last mod file time (uint16_t) */
  XX_META_ID_LAST_MOD_DATE,               /**< DOS last mod file date (uint16_t) */
  XX_META_ID_IS_FOLDER,                   /**< Folder / directory flag (bool) */
  XX_META_ID_IS_ENCRYPTED,                /**< Encryption flag (bool) */
  XX_META_ID_COMMENT,                     /**< Entry comment (Unicode/string dynamic) */
  XX_META_ID_EXTRA_FIELD,                 /**< Raw extra field bytes (dynamic bytes) */
  XX_META_ID_VERSION_NEEDED,              /**< ZIP version needed to extract (uint16_t) */
  XX_META_ID_VERSION_MADE_BY,             /**< ZIP version made by / host system (uint16_t) */
  XX_META_ID_FLAGS,                       /**< General purpose bit flags (uint16_t) */
  XX_META_ID_INTERNAL_ATTRS,              /**< Internal file attributes (uint16_t) */
  XX_META_ID_EXTERNAL_ATTRS,              /**< External file attributes (uint32_t) */
  XX_META_ID_DISK_NUMBER_START,           /**< Disk number where file begins (uint32_t) */
  XX_META_ID_RELATIVE_OFFSET_LOCAL_HEADER, /**< Relative offset of local header (int64_t) */
  /* Options for archive reading / unpacking / packing */
  XX_META_ID_OPT_UNPACK_PATH,             /**< Target directory path for unpacking (Unicode/string dynamic) */
  XX_META_ID_OPT_PASSWORD,                /**< Format-wide or operation password (Unicode/string/bytes dynamic) */
  XX_META_ID_OPT_OVERWRITE,               /**< Overwrite existing files on unpack (bool) */
  XX_META_ID_COMPRESSION_LEVEL,            /**< Compression level (0-9) */
  XX_META_ID_ENCRYPTION_METHOD,            /**< Encryption method selector (format-specific integer) */
  XX_META_ID_OPT_MAX_MEMBER_SIZE,          /**< Maximum declared unpacked member size in bytes; absent is unlimited (uint64_t) */
  XX_META_ID_OPT_MEMORY_LIMIT,             /**< Maximum format-owned extraction buffer budget; absent is unlimited (uint64_t) */
  /* Appended rather than filed with the record fields above: these values
   * cross the shared-library boundary, so inserting in the middle would
   * renumber every option for a consumer built against an older header. */
  XX_META_ID_LINK_TARGET                   /**< Symlink target path, relative to the archive root (string) */
} xx_meta_id_t;

typedef enum xx_meta_id_e xx_archive_meta_id_t;

/* Common metadata ID aliases */
#define XX_META_ID_NAME               XX_META_ID_ORIGINAL_NAME
#define XX_META_ID_FILENAME           XX_META_ID_ORIGINAL_NAME
#define XX_META_ID_FILE_NAME          XX_META_ID_ORIGINAL_NAME
#define XX_ARCHIVE_META_ID_UNKNOWN    XX_META_ID_UNKNOWN
#define XX_ARCHIVE_META_ID_ORIGINAL_NAME XX_META_ID_ORIGINAL_NAME
#define XX_ARCHIVE_META_ID_UNCOMPRESSED_SIZE XX_META_ID_UNCOMPRESSED_SIZE
#define XX_ARCHIVE_META_ID_COMPRESSED_SIZE XX_META_ID_COMPRESSED_SIZE
#define XX_ARCHIVE_META_ID_CRC32      XX_META_ID_CRC32
#define XX_ARCHIVE_META_ID_COMPRESSION_METHOD XX_META_ID_COMPRESSION_METHOD
#define XX_ARCHIVE_META_ID_COMPRESSION_LEVEL XX_META_ID_COMPRESSION_LEVEL
#define XX_META_ID_OPT_COMPRESSION_LEVEL XX_META_ID_COMPRESSION_LEVEL
#define XX_ARCHIVE_META_ID_ENCRYPTION_METHOD XX_META_ID_ENCRYPTION_METHOD
#define XX_META_ID_OPT_ENCRYPTION_METHOD XX_META_ID_ENCRYPTION_METHOD
#define XX_ARCHIVE_META_ID_ATTRIBUTES XX_META_ID_ATTRIBUTES
#define XX_ARCHIVE_META_ID_TIMESTAMP  XX_META_ID_TIMESTAMP
#define XX_ARCHIVE_META_ID_IS_FOLDER  XX_META_ID_IS_FOLDER
#define XX_ARCHIVE_META_ID_IS_ENCRYPTED XX_META_ID_IS_ENCRYPTED
#define XX_ARCHIVE_META_ID_COMMENT    XX_META_ID_COMMENT
#define XX_ARCHIVE_META_ID_EXTRA_FIELD XX_META_ID_EXTRA_FIELD
#define XX_META_ID_UNPACK_PATH        XX_META_ID_OPT_UNPACK_PATH
#define XX_META_ID_OUTPUT_PATH        XX_META_ID_OPT_UNPACK_PATH
#define XX_META_ID_TARGET_PATH        XX_META_ID_OPT_UNPACK_PATH
#define XX_META_ID_PASSWORD           XX_META_ID_OPT_PASSWORD
#define XX_ARCHIVE_META_ID_UNPACK_PATH XX_META_ID_OPT_UNPACK_PATH
#define XX_ARCHIVE_META_ID_PASSWORD   XX_META_ID_OPT_PASSWORD
#define XX_META_ID_MAX_MEMBER_SIZE    XX_META_ID_OPT_MAX_MEMBER_SIZE
#define XX_META_ID_MEMORY_LIMIT       XX_META_ID_OPT_MEMORY_LIMIT
#define XX_ARCHIVE_META_ID_MAX_MEMBER_SIZE XX_META_ID_OPT_MAX_MEMBER_SIZE
#define XX_ARCHIVE_META_ID_MEMORY_LIMIT XX_META_ID_OPT_MEMORY_LIMIT

/**
 * @brief Represents a metadata item with an identifier and a variant value.
 */
struct xx_meta {
  uint32_t meta_id;
  xx_var var;
};

/**
 * @brief Represents an individual file or directory record within an archive.
 */
struct xx_archive_record {
  int64_t header_offset;      /**< Offset of record header in I/O device (-1 if unknown) */
  int64_t header_size;        /**< Size of record header in bytes (0 if unknown) */
  int64_t data_offset;        /**< Offset of entry payload / compressed data in I/O device (-1 if unknown) */
  int64_t compressed_size;    /**< Compressed size of entry in bytes */
  xx_list_s list_meta;        /**< Dynamic list of metadata items (xx_meta) */
};

/**
 * @brief State representing an active archive stream reading session.
 */
struct xx_archive_record_state {
  Abstractformat    *format;         /**< Associated Abstractformat instance */
  xx_archive_record  current_record; /**< Currently active archive record */
  bool               has_record;     /**< True if current_record is valid and loaded */
  int64_t            current_index;  /**< 0-based index of the current record */
  int64_t            total_records;  /**< Total number of records (-1 if streaming or unknown) */
  xx_list_s          options;        /**< Copied list of options (xx_meta) */
  void              *internal_state; /**< Format-specific internal cursor/context */
  void (*free_internal)(void *ptr);  /**< Destructor for internal_state */
};

/**
 * @brief State representing an active archive stream writing / packing session.
 */
struct xx_archive_write_state {
  Abstractformat    *format;         /**< Target Abstractformat instance (owning the output device) */
  xx_archive_record  current_record; /**< Metadata for the record currently being packed */
  bool               has_record;     /**< True if a record is active / in-progress */
  int64_t            current_index;  /**< 0-based index of the current entry being written */
  int64_t            total_records;  /**< Total records written or planned (-1 if streaming) */
  xx_list_s          options;        /**< Copied compression/packing options */
  void              *internal_state; /**< Format-specific internal writer context */
  void (*free_internal)(void *ptr);  /**< Destructor for internal_state */
};

/**
 * @brief Classification of a format-specific binary data structure occurrence.
 */
typedef enum xx_data_struct_type_e {
  XX_DATA_STRUCT_TYPE_UNKNOWN = 0, /**< Unknown / unassigned */
  XX_DATA_STRUCT_TYPE_STRUCT,       /**< Structure / header / table record */
  XX_DATA_STRUCT_TYPE_ENTRY,        /**< Single entry belonging to a table */
  XX_DATA_STRUCT_TYPE_FOOTER,       /**< Trailing/footer record */
  XX_DATA_STRUCT_TYPE_LOCATOR,      /**< Locator record pointing to another structure */
  XX_DATA_STRUCT_TYPE_RAW_DATA      /**< Raw/unparsed data region (e.g. gap, overlay, padding) */
} xx_data_struct_type_t;

/**
 * @brief Global data struct id shared by all formats (not part of any single format's id space).
 * Used for regions that are not a parsed structure of the format (gaps, overlay, padding, ...).
 */
#define XX_DATA_STRUCT_ID_RAW_DATA 0xFFFF0001u

/**
 * @brief Describes a single format-specific binary data structure occurrence
 * (e.g. LOCAL_FILE_HEADER, CENTRAL_DIRECTORY_HEADER, EOCD, ...).
 */
struct xx_data_struct {
  uint32_t               id;         /**< Format-specific data struct id */
  int64_t                offset;     /**< File offset of the structure in the I/O device (-1 if unknown) */
  int64_t                address;    /**< Mapped/virtual address of the structure (-1 if not mapped) */
  int64_t                entry_size; /**< Size in bytes of a single occurrence (-1 if unknown/variable) */
  int64_t                total_size; /**< Total size in bytes covered by all occurrences (-1 if unknown) */
  uint64_t               count;      /**< Number of elements represented (1 for a header, N for a table) */
  xx_data_struct_type_t  type;       /**< Structure classification (header/table/entry/...) */
};

/**
 * @brief State representing an active data-struct stream reading session.
 */
struct xx_data_struct_state {
  Abstractformat  *format;         /**< Associated Abstractformat instance */
  xx_data_struct   current_struct; /**< Currently active data struct descriptor */
  bool             has_struct;     /**< True if current_struct is valid and loaded */
  int64_t          current_index;  /**< 0-based index of the current data struct */
  int64_t          total_structs;  /**< Total number of data structs (-1 if unknown) */
  void            *internal_state; /**< Format-specific internal cursor/context */
  void (*free_internal)(void *ptr); /**< Destructor for internal_state */
};

/**
 * @brief Bit flags describing the semantic role of a xx_data_struct_record's value
 * within its parent data struct (e.g. a field is a virtual address, a size, a pointer, ...).
 */
typedef uint32_t xx_data_struct_record_property_t;

#define XX_DATA_STRUCT_RECORD_PROPERTY_NONE            0x00000000u /**< No specific semantic role */
#define XX_DATA_STRUCT_RECORD_PROPERTY_ID              0x00000001u /**< Value is an identifier / signature / magic */
#define XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS 0x00000002u /**< Value is a virtual/mapped address */
#define XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET          0x00000004u /**< Value is a file offset */
#define XX_DATA_STRUCT_RECORD_PROPERTY_SIZE            0x00000008u /**< Value is a size/length in bytes */
#define XX_DATA_STRUCT_RECORD_PROPERTY_COUNT           0x00000010u /**< Value is a count of elements */
#define XX_DATA_STRUCT_RECORD_PROPERTY_POINTER         0x00000020u /**< Value is a pointer/offset to other data */
#define XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS           0x00000040u /**< Value is itself a bit-flags field */
#define XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP       0x00000080u /**< Value is a date/time field */
#define XX_DATA_STRUCT_RECORD_PROPERTY_STRING          0x00000100u /**< Value is a text string */
#define XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED        0x00000200u /**< Value is reserved/padding */

/**
 * @brief Describes one named field within a fixed-layout data struct
 * (offset and size relative to the parent data struct).
 */
struct xx_data_struct_field_desc {
  const wchar_t                   *name;       /**< Dynamic or static Unicode display name of the field */
  const wchar_t                   *type;       /**< Dynamic or static Unicode display type (e.g. L"uint16", L"uint32") */
  int64_t                          rel_offset; /**< Offset of the field relative to the start of the parent data struct */
  int64_t                          size;       /**< Size in bytes of the field (1, 2, 4, 8) */
  xx_data_struct_record_property_t property;   /**< Bit flags describing the record's semantic role */
};

/**
 * @brief Describes a single named field/record within a xx_data_struct occurrence
 * (e.g. the "signature" or "compressed_size" field of a LOCAL_FILE_HEADER).
 */
struct xx_data_struct_record {
  wchar_t                          *name;          /**< Dynamic Unicode display name of the record (owned, free with xx_str_wfree) */
  wchar_t                          *type;          /**< Dynamic Unicode display type of the record (owned, free with xx_str_wfree) */
  wchar_t                          *display_value; /**< Dynamic Unicode formatted display value of the record (owned, free with xx_str_wfree) */
  int64_t                           offset;        /**< Offset of the record within its parent data struct */
  int64_t                           size;          /**< Size in bytes of the record */
  xx_data_struct_record_property_t  property;      /**< Bit flags describing the record's semantic role */
  xx_var                            value;         /**< Record value */
};

/**
 * @brief State representing an active data-struct-record stream reading session
 * (iterates the named fields of a single xx_data_struct occurrence).
 */
struct xx_data_struct_record_state {
  Abstractformat        *format;         /**< Associated Abstractformat instance */
  xx_data_struct          parent_struct;  /**< The data struct whose records are being read */
  xx_data_struct_record   current_record; /**< Currently active record descriptor */
  bool                    has_record;     /**< True if current_record is valid and loaded */
  int64_t                 current_index;  /**< 0-based index of the current record */
  int64_t                 total_records;  /**< Total number of records (-1 if unknown) */
  void                   *internal_state; /**< Format-specific internal cursor/context */
  void (*free_internal)(void *ptr);       /**< Destructor for internal_state */
};

/**
 * @brief Abstractformat structure with function pointer vtable.
 */
struct Abstractformat {
  xx_io_device *device; /**< Associated I/O device */
  int64_t base_address; /**< Absolute device offset at which this format starts. */
  bool is_mapped; /**< True if memory-mapped dump (e.g. raw dump from memory) */
  bool base_info_handled; /**< True if base format information has been
                             parsed/handled */
  bool is_valid;          /**< Cached validity flag */
  int64_t format_size;    /**< Format size without overlays */
  int64_t overlay_offset; /**< Offset where overlay data begins in device (-1 if
                             none) */
  int64_t overlay_size;   /**< Size of overlay data in bytes (0 if none) */
  xx_endian_t endian;     /**< Format endianness (XX_ENDIAN_LITTLE/BIG/UNKNOWN) */
  xx_file_type_t file_type; /**< Detected or assigned file type (0: unknown, 1:
                               binary, 2: ZIP, 3: ZIP64) */
  xx_os_t os; /**< Operating system enum (XX_OS_UNKNOWN, XX_OS_WINDOWS, etc.) */
  xx_format_type_t format_type; /**< Target binary type enum (XX_TYPE_CONSOLE_APPLICATION, XX_TYPE_DRIVER, XX_TYPE_LIBRARY, etc.) */
  xx_arch_t arch; /**< Architecture enum (XX_ARCH_UNKNOWN, XX_ARCH_X86_64, etc.) */

  /* Buffers for format string metadata */
  char mime_type[64];  /**< MIME type string buffer */
  char extension[32];  /**< File extension string buffer */
  char os_version[32]; /**< OS version string buffer */
  char version[32];    /**< Format version string buffer */

  /* Format classification flags */
  bool is_executable; /**< True if format is an executable */
  bool is_archive;    /**< True if format is an archive container */
  bool is_signed;     /**< True if format carries digital signature */
  bool is_crypted;    /**< True if format content is encrypted/crypted */

  /* Format record / payload counts (0 if none or not applicable) */
  uint64_t number_of_imports;         /**< Number of imports / import table records (0 if none) */
  uint64_t number_of_exports;         /**< Number of exports / export table records (0 if none) */
  uint64_t number_of_resources;       /**< Number of embedded resources (0 if none) */
  uint64_t number_of_metadata;        /**< Number of metadata records (0 if none) */
  uint64_t number_of_archive_records; /**< Number of archive records (0 for non-archives) */

  /**
   * Format-wide extra parameters.  Elements are owned xx_meta values and are
   * available to every format implementation.  Operation-specific option
   * lists may override these values for one read/write session.
   */
  xx_list_s list_extra_parameters;

  void *priv; /**< Implementation-specific private state */

  /* Function pointer vtable */
  bool (*check_is_valid)(Abstractformat *self, xx_pd_struct *pd);
  bool (*handle_base_info)(Abstractformat *self, xx_pd_struct *pd);
  xx_format_type_t (*get_type)(Abstractformat *self);
  xx_file_type_t (*get_file_type)(Abstractformat *self);
  const char *(*get_mime_type)(Abstractformat *self);
  const char *(*get_extension)(Abstractformat *self);
  xx_arch_t (*get_arch)(Abstractformat *self);
  xx_os_t (*get_os)(Abstractformat *self);
  const char *(*get_os_version)(Abstractformat *self);
  const char *(*get_version)(Abstractformat *self);
  xx_endian_t (*get_endian)(Abstractformat *self);
  int64_t (*get_format_size)(Abstractformat *self, xx_pd_struct *pd);
  uint64_t (*get_number_of_archive_records)(Abstractformat *self, xx_pd_struct *pd);
  uint64_t (*get_number_of_imports)(Abstractformat *self, xx_pd_struct *pd);
  uint64_t (*get_number_of_exports)(Abstractformat *self, xx_pd_struct *pd);
  uint64_t (*get_number_of_resources)(Abstractformat *self, xx_pd_struct *pd);
  uint64_t (*get_number_of_metadata)(Abstractformat *self, xx_pd_struct *pd);
  bool (*check_is_executable)(Abstractformat *self);
  bool (*check_is_archive)(Abstractformat *self);
  bool (*check_is_signed)(Abstractformat *self);
  bool (*check_is_crypted)(Abstractformat *self);
  int (*close)(Abstractformat *self);
  void (*destroy)(Abstractformat *self);

  /* Stream archive records reading vtable callbacks */
  xx_archive_record_state *(*create_archive_records_reading)(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
  const xx_archive_record *(*get_current_archive_record)(Abstractformat *self, xx_archive_record_state *state);
  bool (*unpack_current_archive_record)(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
  bool (*archive_record_move_to_next)(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
  void (*free_archive_records_reading)(Abstractformat *self, xx_archive_record_state *state);

  /* Stream archive records writing / packing vtable callbacks */
  xx_archive_write_state *(*create_archive_records_writing)(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
  bool (*pack_archive_record)(Abstractformat *self, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev, xx_pd_struct *pd);
  bool (*finalize_archive_records_writing)(Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd);
  void (*free_archive_records_writing)(Abstractformat *self, xx_archive_write_state *state);

  /* Format-specific data struct id <-> string conversion */
  const char *(*data_struct_id_to_string)(Abstractformat *self, uint32_t id);
  uint32_t (*data_struct_string_to_id)(Abstractformat *self, const char *name);

  /* Stream data structs reading vtable callbacks (headers/tables/records of the format) */
  xx_data_struct_state *(*create_data_structs_reading)(Abstractformat *self, xx_pd_struct *pd);
  const xx_data_struct *(*get_current_data_struct)(Abstractformat *self, xx_data_struct_state *state);
  bool (*data_struct_move_to_next)(Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
  void (*free_data_structs_reading)(Abstractformat *self, xx_data_struct_state *state);

  /* Stream data struct records reading vtable callbacks (named fields of a single data struct) */
  xx_data_struct_record_state *(*create_data_struct_records_reading)(Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd);
  const xx_data_struct_record *(*get_current_data_struct_record)(Abstractformat *self, xx_data_struct_record_state *state);
  bool (*data_struct_record_move_to_next)(Abstractformat *self, xx_data_struct_record_state *state, xx_pd_struct *pd);
  void (*free_data_struct_records_reading)(Abstractformat *self, xx_data_struct_record_state *state);

  /**
   * Optional read-side preparation for split archives, before parsing.
   * Inspect self->device with xx_io_multivolume_count/get_volume as needed.
   * Return true when ready (including non-split input), false on failure.
   * Use raw I/O, not generic parsing wrappers on this same object. Bind before
   * the first read operation. No device ownership is transferred by dispatch.
   */
  bool (*handle_split_format)(Abstractformat *self, xx_pd_struct *pd);
  bool split_format_handled;  /**< Dispatcher cache: installed callback succeeded. */
  bool split_format_handling; /**< Dispatcher recursion guard; do not modify. */

  /**
   * Format-specific memory-map producer. It receives an initialized, empty
   * output map. Abstractformat owns the cached memory_map populated by the
   * common dispatcher; callers borrow the pointer returned by
   * xx_format_get_memory_map(). These fields are kept together at the end of
   * Abstractformat. Because Abstractformat is public and embedded by concrete
   * formats, adding them changes the ABI and requires a full library/client
   * rebuild.
   */
  bool (*get_memory_map)(Abstractformat *self, xx_memory_map_mode_t mode,
                         xx_memory_map *output, xx_pd_struct *pd);
  xx_memory_map memory_map;
  bool memory_map_handled;
  bool memory_map_handling;
  xx_memory_map_mode_t memory_map_requested_mode;
  /** Optional loaded module VA; UINT64_MAX asks the format for its default. */
  uint64_t module_address;
};

/**
 * Prepare split-format state once before reading. A NULL callback is a
 * successful no-op (it does not mark split_format_handled); a NULL format fails.
 * Installed callbacks are skipped after success. Failure, cancellation, and
 * recursive dispatch return false; failed attempts are retryable. An already
 * stopped pd rejects an installed handler even if its success was cached.
 *
 * Generic validity/base-info, size/count, and reader-construction wrappers call
 * this helper automatically. Direct concrete-format calls must invoke it
 * explicitly when implementing split-aware parsers. Writing and destruction
 * do not dispatch it. The callback owns cleanup of any format-private mapping
 * or view, must preserve borrowed input ownership, and must leave retryable
 * state even if cancellation is detected after it returns true. Configure
 * before parsing; reconstruct the format object
 * when changing its input or callback after parsing, with no live readers.
 */
XXFC_API bool xx_format_handle_split_format(Abstractformat *format, xx_pd_struct *pd);

/* --- Common Memory Map --- */

/** Discard the owned cached map. Safe before or after base-info parsing. */
XXFC_API void xx_format_invalidate_memory_map(Abstractformat *format);

/** Build/cache the requested format-specific map (UNKNOWN = default view). */
XXFC_API bool xx_format_handle_memory_map(Abstractformat *format,
                                          xx_memory_map_mode_t mode,
                                          xx_pd_struct *pd);

/** Return the borrowed cached map, or NULL when construction fails. */
XXFC_API const xx_memory_map *xx_format_get_memory_map(
    Abstractformat *format, xx_memory_map_mode_t mode, xx_pd_struct *pd);

XXFC_API uint64_t xx_format_offset_to_address(Abstractformat *format,
                                               int64_t offset,
                                               xx_pd_struct *pd);
XXFC_API int64_t xx_format_address_to_offset(Abstractformat *format,
                                             uint64_t address,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_format_offset_to_rel_address(Abstractformat *format,
                                                   int64_t offset,
                                                   xx_pd_struct *pd);
XXFC_API int64_t xx_format_rel_address_to_offset(Abstractformat *format,
                                                 int64_t relative_address,
                                                 xx_pd_struct *pd);
XXFC_API uint64_t xx_format_rel_address_to_address(Abstractformat *format,
                                                    int64_t relative_address,
                                                    xx_pd_struct *pd);
XXFC_API int64_t xx_format_address_to_rel_address(Abstractformat *format,
                                                   uint64_t address,
                                                   xx_pd_struct *pd);

/* --- Format-wide Extra Parameters --- */

/** Store an owned copy of a format-wide parameter, replacing the old value. */
XXFC_API bool xx_format_set_extra_parameter(Abstractformat *format,
                                             uint32_t meta_id,
                                             const xx_var *value);

/** Find a format-wide parameter value, or NULL when it has not been set. */
XXFC_API const xx_var *xx_format_find_extra_parameter(
    const Abstractformat *format, uint32_t meta_id);

/**
 * Resolve a parameter using operation-specific values first and the
 * format-wide list as a fallback.
 */
XXFC_API const xx_var *xx_format_resolve_extra_parameter(
    const Abstractformat *format, const xx_list_s *operation_parameters,
    uint32_t meta_id);

/** Remove a format-wide parameter. */
XXFC_API bool xx_format_remove_extra_parameter(Abstractformat *format,
                                                uint32_t meta_id);

/**
 * Clear/free format-wide parameters and invalidate owned common caches.
 * Safe to call twice.
 */
XXFC_API void xx_format_cleanup_extra_parameters(Abstractformat *format);

/** Set or clear (password_utf8 == NULL) the password shared by all formats. */
XXFC_API bool xx_format_set_password(Abstractformat *format,
                                     const char *password_utf8);

/** Return the UTF-8 password set by xx_format_set_password, or NULL. */
XXFC_API const char *xx_format_get_password(const Abstractformat *format);

/* Compatibility member aliases matching camelCase / user naming */
#define createArchiveRecordsReading create_archive_records_reading
#define getCurrentArchiveRecord get_current_archive_record
#define unpackCurrentArchiveRecord unpack_current_archive_record
#define archiveRecordMoveToNext archive_record_move_to_next
#define freeArchiveRecordsReading free_archive_records_reading
#define freeActiveReadingRecord free_archive_records_reading
#define createArchiveRecordsWriting create_archive_records_writing
#define packArchiveRecord pack_archive_record
#define finalizeArchiveRecordsWriting finalize_archive_records_writing
#define freeArchiveRecordsWriting free_archive_records_writing
#define formatType format_type
#define getType get_type
#define fileType file_type
#define getFileType get_file_type
#define baseAddress base_address
#define isMapped is_mapped
#define baseInfoHandled base_info_handled
#define isValid is_valid
#define formatSize format_size
#define overlayOffset overlay_offset
#define overlaySize overlay_size
#define baseOffset base_address
#define validate check_is_valid
#define checkIsValid check_is_valid
#define handleBaseInfo handle_base_info
#define handleSplitFormat handle_split_format
#define getMemoryMap get_memory_map
#define memoryMap memory_map
#define memoryMapHandled memory_map_handled
#define moduleAddress module_address
#define splitFormatHandled split_format_handled
#define archName arch_name
#define archType arch
#define arch_type arch
#define mimeType mime_type
#define osName os_name
#define osVersion os_version
#define isExecutable is_executable
#define isArchive is_archive
#define isSigned is_signed
#define isCrypted is_crypted
#define numberOfImports number_of_imports
#define numberOfExports number_of_exports
#define numberOfResources number_of_resources
#define numberOfMetadata number_of_metadata
#define numberOfArchiveRecords number_of_archive_records
#define listExtraParameters list_extra_parameters
#define fileName file_name
#define sFileName file_name
#define headerOffset header_offset
#define nHeaderOffset header_offset
#define headerSize header_size
#define nHeaderSize header_size
#define dataOffset data_offset
#define nDataOffset data_offset
#define compressedSize compressed_size
#define nCompressedSize compressed_size
#define uncompressedSize uncompressed_size
#define nUncompressedSize uncompressed_size
#define nMethod method
#define nCRC32 crc32
#define isFolder is_folder
#define bIsFolder is_folder
#define isDirectory is_folder
#define isEncrypted is_encrypted
#define bIsEncrypted is_encrypted
#define listMeta list_meta
#define list_records list_meta
#define getOs get_os
#define getArch get_arch
#define getArchName get_arch_name
#define getEndian get_endian
#ifndef getFormatSize
#define getFormatSize get_format_size
#endif
#ifndef getSize
#define getSize get_format_size
#endif
#ifndef get_size
#define get_size get_format_size
#endif
#ifndef getTotalSize
#define getTotalSize get_total_size
#endif

/* --- Inline Convenience Wrappers --- */

static inline bool xx_format_is_valid(Abstractformat *f, xx_pd_struct *pd) {
  if (!xx_format_handle_split_format(f, pd))
    return false;
  if (f->base_info_handled)
    return f->is_valid;
  if (f->check_is_valid) {
    f->is_valid = f->check_is_valid(f, pd);
    return f->is_valid;
  }
  return f->is_valid;
}

static inline bool xx_format_handle_base_info(Abstractformat *f,
                                              xx_pd_struct *pd) {
  if (!xx_format_handle_split_format(f, pd))
    return false;
  if (f->base_info_handled)
    return true;
  if (f->handle_base_info) {
    f->base_info_handled = f->handle_base_info(f, pd);
    return f->base_info_handled;
  }
  return false;
}

XXFC_API xx_format_type_t xx_format_get_type(Abstractformat *f);
XXFC_API void xx_format_set_type(Abstractformat *f, xx_format_type_t type);

XXFC_API xx_file_type_t xx_format_get_file_type(Abstractformat *f);
XXFC_API xx_file_type_t xx_format_get_file_type_device(xx_io_device *dev);

/**
 * @brief Longest chain xx_format_get_file_type_chain() can produce.
 */
#define XX_FILE_TYPE_CHAIN_MAX 8

/**
 * @brief Return the container a file type is a specialisation of.
 *
 * A type that is not a specialisation of anything reports
 * XX_FILE_TYPE_BINARY, and XX_FILE_TYPE_BINARY itself reports
 * XX_FILE_TYPE_UNKNOWN, which terminates the chain.
 *
 * @param type File type to look up.
 * @return The parent file type.
 */
XXFC_API xx_file_type_t xx_format_get_parent_file_type(xx_file_type_t type);

/**
 * @brief Expand a file type into its chain, most generic first.
 *
 * XX_FILE_TYPE_PE64 expands to { BINARY, MSDOS, PE64 }, XX_FILE_TYPE_APK to
 * { BINARY, ZIP, APK }, and XX_FILE_TYPE_NPM to { BINARY, GZ, TAR_GZ, NPM }.
 *
 * @param type     Most specific file type, typically from
 *                 xx_format_get_file_type_device().
 * @param types    Destination array; may be NULL to query the length only.
 * @param capacity Number of entries @p types can hold.
 * @return Number of entries the chain has. A value greater than @p capacity
 *         means @p types was too small and nothing was written.
 */
XXFC_API size_t xx_format_get_file_type_chain(xx_file_type_t type,
                                              xx_file_type_t *types,
                                              size_t capacity);

/**
 * @brief Detect a device's file type chain, most generic first.
 *
 * For a 64-bit Windows executable the list holds XX_FILE_TYPE_BINARY,
 * XX_FILE_TYPE_MSDOS and XX_FILE_TYPE_PE64: the file is a binary, it carries a
 * DOS header, and that header introduces a PE64 image. The last element is
 * always what xx_format_get_file_type_device() returns on its own.
 *
 * @param dev Device to inspect.
 * @return A list of xx_file_type_t owned by the caller, empty when the device
 *         cannot be identified, or NULL on allocation failure. Release it with
 *         xx_list_destroy().
 */
XXFC_API xx_list_t *xx_format_get_file_types_device(xx_io_device *dev);

/**
 * @brief Short display name for a file type, for example "PE64" or "TAR.GZ".
 *
 * @param type File type to name.
 * @return A static string; "UNKNOWN" for unrecognised values.
 */
XXFC_API const char *xx_format_file_type_to_string(xx_file_type_t type);

static inline void xx_format_set_file_type(Abstractformat *f,
                                           xx_file_type_t type) {
  if (f) {
    if (f->file_type != type)
      xx_format_invalidate_memory_map(f);
    f->file_type = type;
  }
}

static inline const char *xx_format_get_mime_type(Abstractformat *f) {
  if (!f)
    return "";
  if (f->get_mime_type)
    return f->get_mime_type(f);
  return f->mime_type;
}

static inline const char *xx_format_get_extension(Abstractformat *f) {
  if (!f)
    return "";
  if (f->get_extension)
    return f->get_extension(f);
  return f->extension;
}

XXFC_API xx_arch_t xx_format_get_arch(Abstractformat *f);
XXFC_API void xx_format_set_arch(Abstractformat *f, xx_arch_t arch);

static inline const char *xx_format_get_arch_name(Abstractformat *f) {
  if (!f)
    return "";
  return xx_arch_to_string(xx_format_get_arch(f));
}

static inline void xx_format_set_arch_name(Abstractformat *f, const char *s) {
  (void)f;
  (void)s;
}

XXFC_API xx_os_t xx_format_get_os(Abstractformat *f);
XXFC_API void xx_format_set_os(Abstractformat *f, xx_os_t os);

static inline const char *xx_format_get_os_name(Abstractformat *f) {
  if (!f)
    return "";
  return xx_os_to_string(xx_format_get_os(f));
}

static inline const char *xx_format_get_type_name(Abstractformat *f) {
  if (!f)
    return "";
  return xx_type_to_string(xx_format_get_type(f));
}

static inline const char *xx_format_get_os_version(Abstractformat *f) {
  if (!f)
    return "";
  if (f->get_os_version)
    return f->get_os_version(f);
  return f->os_version;
}

static inline const char *xx_format_get_version(Abstractformat *f) {
  if (!f)
    return "";
  if (f->get_version)
    return f->get_version(f);
  return f->version;
}

XXFC_API xx_endian_t xx_format_get_endian(Abstractformat *f);
XXFC_API void xx_format_set_endian(Abstractformat *f, xx_endian_t endian);

static inline int64_t xx_format_get_format_size(Abstractformat *f,
                                                xx_pd_struct *pd) {
  if (!xx_format_handle_split_format(f, pd))
    return -1;
  if (f && f->get_format_size)
    return f->get_format_size(f, pd);
  return f ? f->format_size : -1;
}

static inline int64_t xx_format_get_size(Abstractformat *f, xx_pd_struct *pd) {
  return xx_format_get_format_size(f, pd);
}

static inline int64_t xx_format_get_total_size(Abstractformat *f) {
  return (f && f->device) ? xx_io_total_size(f->device) : -1;
}

static inline int64_t xx_format_get_overlay_offset(Abstractformat *f) {
  return f ? f->overlay_offset : -1;
}

static inline int64_t xx_format_get_overlay_size(Abstractformat *f) {
  return f ? f->overlay_size : 0;
}

static inline bool xx_format_is_overlay_present(Abstractformat *f) {
  return (f && f->overlay_size > 0 && f->overlay_offset >= 0);
}

static inline bool xx_format_is_mapped(Abstractformat *f) {
  return f ? f->is_mapped : false;
}

static inline void xx_format_set_mapped(Abstractformat *f, bool is_mapped) {
  if (f) {
    if (f->is_mapped != is_mapped) {
      xx_format_invalidate_memory_map(f);
      f->base_info_handled = false;
      f->is_valid = false;
    }
    f->is_mapped = is_mapped;
  }
}

static inline bool xx_format_is_base_info_handled(Abstractformat *f) {
  return f ? f->base_info_handled : false;
}

static inline void xx_format_set_base_info_handled(Abstractformat *f,
                                                   bool handled) {
  if (f) {
    if (!handled)
      xx_format_invalidate_memory_map(f);
    f->base_info_handled = handled;
  }
}

static inline int64_t xx_format_get_base_address(Abstractformat *f) {
  return f ? f->base_address : 0;
}

static inline void xx_format_set_base_address(Abstractformat *f,
                                              int64_t base_address) {
  if (f) {
    if (f->base_address != base_address) {
      xx_format_invalidate_memory_map(f);
      f->base_info_handled = false;
      f->is_valid = false;
    }
    f->base_address = base_address;
  }
}

/** Return the configured loaded module VA, or UINT64_MAX for format default. */
static inline uint64_t xx_format_get_module_address(Abstractformat *f) {
  return f ? f->module_address : XX_INVALID_ADDRESS;
}

/** Override the loaded module VA used by memory maps (e.g. a relocated image). */
static inline void xx_format_set_module_address(Abstractformat *f,
                                                uint64_t module_address) {
  if (f) {
    if (f->module_address != module_address)
      xx_format_invalidate_memory_map(f);
    f->module_address = module_address;
  }
}

static inline bool xx_format_is_executable(Abstractformat *f) {
  if (!f)
    return false;
  if (f->check_is_executable)
    return f->check_is_executable(f);
  return f->is_executable;
}

static inline bool xx_format_is_archive(Abstractformat *f) {
  if (!f)
    return false;
  if (f->check_is_archive)
    return f->check_is_archive(f);
  return f->is_archive;
}

static inline bool xx_format_is_signed(Abstractformat *f) {
  if (!f)
    return false;
  if (f->check_is_signed)
    return f->check_is_signed(f);
  return f->is_signed;
}

static inline bool xx_format_is_crypted(Abstractformat *f) {
  if (!f)
    return false;
  if (f->check_is_crypted)
    return f->check_is_crypted(f);
  return f->is_crypted;
}

static inline uint64_t xx_format_get_number_of_imports_pd(Abstractformat *f, xx_pd_struct *pd) {
  if (!f)
    return 0;
  if (f->get_number_of_imports)
    return f->get_number_of_imports(f, pd);
  return f->number_of_imports;
}

static inline uint64_t xx_format_get_number_of_imports(Abstractformat *f, xx_pd_struct *pd) {
  return xx_format_get_number_of_imports_pd(f, pd);
}

static inline uint64_t xx_format_get_number_of_exports_pd(Abstractformat *f, xx_pd_struct *pd) {
  if (!f)
    return 0;
  if (f->get_number_of_exports)
    return f->get_number_of_exports(f, pd);
  return f->number_of_exports;
}

static inline uint64_t xx_format_get_number_of_exports(Abstractformat *f, xx_pd_struct *pd) {
  return xx_format_get_number_of_exports_pd(f, pd);
}

static inline uint64_t xx_format_get_number_of_resources_pd(Abstractformat *f, xx_pd_struct *pd) {
  if (!f)
    return 0;
  if (f->get_number_of_resources)
    return f->get_number_of_resources(f, pd);
  return f->number_of_resources;
}

static inline uint64_t xx_format_get_number_of_resources(Abstractformat *f, xx_pd_struct *pd) {
  return xx_format_get_number_of_resources_pd(f, pd);
}

static inline uint64_t xx_format_get_number_of_metadata_pd(Abstractformat *f, xx_pd_struct *pd) {
  if (!f)
    return 0;
  if (f->get_number_of_metadata)
    return f->get_number_of_metadata(f, pd);
  return f->number_of_metadata;
}

static inline uint64_t xx_format_get_number_of_metadata(Abstractformat *f, xx_pd_struct *pd) {
  return xx_format_get_number_of_metadata_pd(f, pd);
}

static inline bool xx_format_has_file_import(Abstractformat *f) {
  return xx_format_get_number_of_imports_pd(f, NULL) > 0;
}

static inline bool xx_format_has_file_export(Abstractformat *f) {
  return xx_format_get_number_of_exports_pd(f, NULL) > 0;
}

static inline bool xx_format_has_file_resources(Abstractformat *f) {
  return xx_format_get_number_of_resources_pd(f, NULL) > 0;
}

static inline bool xx_format_has_file_metadata(Abstractformat *f) {
  return xx_format_get_number_of_metadata_pd(f, NULL) > 0;
}

static inline uint64_t xx_format_get_number_of_archive_records_pd(Abstractformat *f, xx_pd_struct *pd) {
  if (!xx_format_handle_split_format(f, pd))
    return 0;
  if (f->get_number_of_archive_records)
    return f->get_number_of_archive_records(f, pd);
  return f->number_of_archive_records;
}

static inline uint64_t xx_format_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *pd) {
  return xx_format_get_number_of_archive_records_pd(f, pd);
}

/* Setters for metadata string buffers and flags */
static inline void xx_format_set_mime_type(Abstractformat *f, const char *s) {
  if (!f)
    return;
  size_t i = 0;
  while (s && s[i] && i < sizeof(f->mime_type) - 1) {
    f->mime_type[i] = s[i];
    i++;
  }
  f->mime_type[i] = '\0';
}

static inline void xx_format_set_extension(Abstractformat *f, const char *s) {
  if (!f)
    return;
  size_t i = 0;
  while (s && s[i] && i < sizeof(f->extension) - 1) {
    f->extension[i] = s[i];
    i++;
  }
  f->extension[i] = '\0';
}

static inline void xx_format_set_os_name(Abstractformat *f, const char *s) {
  (void)f;
  (void)s;
}

static inline void xx_format_set_os_version(Abstractformat *f, const char *s) {
  if (!f)
    return;
  size_t i = 0;
  while (s && s[i] && i < sizeof(f->os_version) - 1) {
    f->os_version[i] = s[i];
    i++;
  }
  f->os_version[i] = '\0';
}

static inline void xx_format_set_version(Abstractformat *f, const char *s) {
  if (!f)
    return;
  size_t i = 0;
  while (s && s[i] && i < sizeof(f->version) - 1) {
    f->version[i] = s[i];
    i++;
  }
  f->version[i] = '\0';
}

static inline void xx_format_set_executable(Abstractformat *f, bool val) {
  if (f)
    f->is_executable = val;
}
static inline void xx_format_set_archive(Abstractformat *f, bool val) {
  if (f)
    f->is_archive = val;
}
static inline void xx_format_set_signed(Abstractformat *f, bool val) {
  if (f)
    f->is_signed = val;
}
static inline void xx_format_set_crypted(Abstractformat *f, bool val) {
  if (f)
    f->is_crypted = val;
}
static inline void xx_format_set_number_of_imports(Abstractformat *f, uint64_t count) {
  if (f)
    f->number_of_imports = count;
}
static inline void xx_format_set_number_of_exports(Abstractformat *f, uint64_t count) {
  if (f)
    f->number_of_exports = count;
}
static inline void xx_format_set_number_of_resources(Abstractformat *f, uint64_t count) {
  if (f)
    f->number_of_resources = count;
}
static inline void xx_format_set_number_of_metadata(Abstractformat *f, uint64_t count) {
  if (f)
    f->number_of_metadata = count;
}
static inline void xx_format_set_number_of_archive_records(Abstractformat *f, uint64_t count) {
  if (f)
    f->number_of_archive_records = count;
}
static inline void xx_format_set_has_file_import(Abstractformat *f, bool val) {
  if (f && val && f->number_of_imports == 0)
    f->number_of_imports = 1;
  else if (f && !val)
    f->number_of_imports = 0;
}
static inline void xx_format_set_has_file_export(Abstractformat *f, bool val) {
  if (f && val && f->number_of_exports == 0)
    f->number_of_exports = 1;
  else if (f && !val)
    f->number_of_exports = 0;
}
static inline void xx_format_set_has_file_resources(Abstractformat *f, bool val) {
  if (f && val && f->number_of_resources == 0)
    f->number_of_resources = 1;
  else if (f && !val)
    f->number_of_resources = 0;
}
static inline void xx_format_set_has_file_metadata(Abstractformat *f, bool val) {
  if (f && val && f->number_of_metadata == 0)
    f->number_of_metadata = 1;
  else if (f && !val)
    f->number_of_metadata = 0;
}

static inline int xx_format_close(Abstractformat *f) {
  return (f && f->close) ? f->close(f) : 0;
}

static inline void xx_format_destroy(Abstractformat *f) {
  if (f) {
    if (f->destroy) {
      f->destroy(f);
    } else if (f->close) {
      f->close(f);
    }
    xx_format_cleanup_extra_parameters(f);
  }
}

/* Aliases matching user request without xx_ prefix */
static inline bool Abstractformat_handle_split_format(Abstractformat *f,
                                                      xx_pd_struct *pd) {
  return xx_format_handle_split_format(f, pd);
}

static inline const xx_memory_map *Abstractformat_get_memory_map(
    Abstractformat *f, xx_memory_map_mode_t mode, xx_pd_struct *pd) {
  return xx_format_get_memory_map(f, mode, pd);
}

static inline uint64_t Abstractformat_offset_to_address(
    Abstractformat *f, int64_t offset, xx_pd_struct *pd) {
  return xx_format_offset_to_address(f, offset, pd);
}

static inline int64_t Abstractformat_address_to_offset(
    Abstractformat *f, uint64_t address, xx_pd_struct *pd) {
  return xx_format_address_to_offset(f, address, pd);
}

static inline uint64_t Abstractformat_offset_to_rel_address(
    Abstractformat *f, int64_t offset, xx_pd_struct *pd) {
  return xx_format_offset_to_rel_address(f, offset, pd);
}

static inline int64_t Abstractformat_rel_address_to_offset(
    Abstractformat *f, int64_t relative_address, xx_pd_struct *pd) {
  return xx_format_rel_address_to_offset(f, relative_address, pd);
}

static inline uint64_t Abstractformat_rel_address_to_address(
    Abstractformat *f, int64_t relative_address, xx_pd_struct *pd) {
  return xx_format_rel_address_to_address(f, relative_address, pd);
}

static inline int64_t Abstractformat_address_to_rel_address(
    Abstractformat *f, uint64_t address, xx_pd_struct *pd) {
  return xx_format_address_to_rel_address(f, address, pd);
}

static inline bool Abstractformat_is_valid(Abstractformat *f,
                                           xx_pd_struct *pd) {
  return xx_format_is_valid(f, pd);
}
static inline bool Abstractformat_handle_base_info(Abstractformat *f,
                                                   xx_pd_struct *pd) {
  return xx_format_handle_base_info(f, pd);
}
static inline xx_format_type_t Abstractformat_get_type(Abstractformat *f) {
  return xx_format_get_type(f);
}
static inline void Abstractformat_set_type(Abstractformat *f,
                                           xx_format_type_t type) {
  xx_format_set_type(f, type);
}
static inline xx_format_type_t get_type(Abstractformat *f) {
  return xx_format_get_type(f);
}
static inline const char *Abstractformat_get_type_name(Abstractformat *f) {
  return xx_format_get_type_name(f);
}
static inline const char *get_type_name(Abstractformat *f) {
  return xx_format_get_type_name(f);
}
static inline xx_file_type_t Abstractformat_get_file_type(Abstractformat *f) {
  return xx_format_get_file_type(f);
}
static inline xx_file_type_t
Abstractformat_get_file_type_device(xx_io_device *dev) {
  return xx_format_get_file_type_device(dev);
}
static inline void Abstractformat_set_file_type(Abstractformat *f,
                                                xx_file_type_t type) {
  xx_format_set_file_type(f, type);
}
static inline xx_file_type_t get_file_type(Abstractformat *f) {
  return xx_format_get_file_type(f);
}
static inline xx_file_type_t xx_io_get_file_type(xx_io_device *dev) {
  return xx_format_get_file_type_device(dev);
}
static inline xx_file_type_t io_get_file_type(xx_io_device *dev) {
  return xx_format_get_file_type_device(dev);
}
static inline const char *Abstractformat_get_mime_type(Abstractformat *f) {
  return xx_format_get_mime_type(f);
}
static inline const char *Abstractformat_get_extension(Abstractformat *f) {
  return xx_format_get_extension(f);
}
static inline xx_arch_t Abstractformat_get_arch(Abstractformat *f) {
  return xx_format_get_arch(f);
}
static inline void Abstractformat_set_arch(Abstractformat *f, xx_arch_t arch) {
  xx_format_set_arch(f, arch);
}
static inline xx_arch_t get_arch(Abstractformat *f) {
  return xx_format_get_arch(f);
}
static inline const char *Abstractformat_get_arch_name(Abstractformat *f) {
  return xx_format_get_arch_name(f);
}
static inline void Abstractformat_set_arch_name(Abstractformat *f, const char *s) {
  xx_format_set_arch_name(f, s);
}
static inline const char *get_arch_name(Abstractformat *f) {
  return xx_format_get_arch_name(f);
}
static inline xx_os_t Abstractformat_get_os(Abstractformat *f) {
  return xx_format_get_os(f);
}
static inline void Abstractformat_set_os(Abstractformat *f, xx_os_t os) {
  xx_format_set_os(f, os);
}
static inline xx_os_t get_os(Abstractformat *f) { return xx_format_get_os(f); }
static inline const char *Abstractformat_get_os_name(Abstractformat *f) {
  return xx_format_get_os_name(f);
}
static inline const char *get_os_name(Abstractformat *f) {
  return xx_format_get_os_name(f);
}
static inline const char *Abstractformat_get_os_version(Abstractformat *f) {
  return xx_format_get_os_version(f);
}
static inline const char *Abstractformat_get_version(Abstractformat *f) {
  return xx_format_get_version(f);
}
static inline xx_endian_t Abstractformat_get_endian(Abstractformat *f) {
  return xx_format_get_endian(f);
}
static inline void Abstractformat_set_endian(Abstractformat *f, xx_endian_t endian) {
  xx_format_set_endian(f, endian);
}
static inline xx_endian_t get_endian(Abstractformat *f) {
  return xx_format_get_endian(f);
}
static inline int64_t Abstractformat_get_format_size(Abstractformat *f,
                                                     xx_pd_struct *pd) {
  return xx_format_get_format_size(f, pd);
}
static inline int64_t Abstractformat_get_size(Abstractformat *f,
                                              xx_pd_struct *pd) {
  return xx_format_get_size(f, pd);
}
static inline int64_t Abstractformat_get_total_size(Abstractformat *f) {
  return xx_format_get_total_size(f);
}
static inline int64_t Abstractformat_get_overlay_offset(Abstractformat *f) {
  return xx_format_get_overlay_offset(f);
}
static inline int64_t Abstractformat_get_overlay_size(Abstractformat *f) {
  return xx_format_get_overlay_size(f);
}
static inline bool Abstractformat_is_overlay_present(Abstractformat *f) {
  return xx_format_is_overlay_present(f);
}
static inline bool Abstractformat_is_mapped(Abstractformat *f) {
  return xx_format_is_mapped(f);
}
static inline void Abstractformat_set_mapped(Abstractformat *f,
                                             bool is_mapped) {
  xx_format_set_mapped(f, is_mapped);
}
static inline bool Abstractformat_is_base_info_handled(Abstractformat *f) {
  return xx_format_is_base_info_handled(f);
}
static inline void Abstractformat_set_base_info_handled(Abstractformat *f,
                                                        bool handled) {
  xx_format_set_base_info_handled(f, handled);
}
static inline int64_t Abstractformat_get_base_address(Abstractformat *f) {
  return xx_format_get_base_address(f);
}
static inline void Abstractformat_set_base_address(Abstractformat *f,
                                                   int64_t base_address) {
  xx_format_set_base_address(f, base_address);
}
static inline uint64_t Abstractformat_get_module_address(Abstractformat *f) {
  return xx_format_get_module_address(f);
}
static inline void Abstractformat_set_module_address(Abstractformat *f,
                                                      uint64_t address) {
  xx_format_set_module_address(f, address);
}
static inline bool Abstractformat_is_executable(Abstractformat *f) {
  return xx_format_is_executable(f);
}
static inline bool Abstractformat_is_archive(Abstractformat *f) {
  return xx_format_is_archive(f);
}
static inline bool Abstractformat_is_signed(Abstractformat *f) {
  return xx_format_is_signed(f);
}
static inline bool Abstractformat_is_crypted(Abstractformat *f) {
  return xx_format_is_crypted(f);
}
static inline void Abstractformat_set_mime_type(Abstractformat *f,
                                                const char *s) {
  xx_format_set_mime_type(f, s);
}
static inline void Abstractformat_set_extension(Abstractformat *f,
                                                const char *s) {
  xx_format_set_extension(f, s);
}
static inline void Abstractformat_set_arch_name_alias(Abstractformat *f, const char *s) {
  xx_format_set_arch_name(f, s);
}
static inline void Abstractformat_set_os_name(Abstractformat *f,
                                              const char *s) {
  xx_format_set_os_name(f, s);
}
static inline void Abstractformat_set_os_version(Abstractformat *f,
                                                 const char *s) {
  xx_format_set_os_version(f, s);
}
static inline void Abstractformat_set_version(Abstractformat *f,
                                              const char *s) {
  xx_format_set_version(f, s);
}
static inline void Abstractformat_set_executable(Abstractformat *f, bool val) {
  xx_format_set_executable(f, val);
}
static inline void Abstractformat_set_archive(Abstractformat *f, bool val) {
  xx_format_set_archive(f, val);
}
static inline void Abstractformat_set_signed(Abstractformat *f, bool val) {
  xx_format_set_signed(f, val);
}
static inline void Abstractformat_set_crypted(Abstractformat *f, bool val) {
  xx_format_set_crypted(f, val);
}
static inline uint64_t Abstractformat_get_number_of_imports_pd(Abstractformat *f, xx_pd_struct *pd) {
  return xx_format_get_number_of_imports_pd(f, pd);
}
static inline void Abstractformat_set_number_of_imports(Abstractformat *f, uint64_t count) {
  xx_format_set_number_of_imports(f, count);
}

static inline uint64_t Abstractformat_get_number_of_exports_pd(Abstractformat *f, xx_pd_struct *pd) {
  return xx_format_get_number_of_exports_pd(f, pd);
}
static inline void Abstractformat_set_number_of_exports(Abstractformat *f, uint64_t count) {
  xx_format_set_number_of_exports(f, count);
}

static inline uint64_t Abstractformat_get_number_of_resources_pd(Abstractformat *f, xx_pd_struct *pd) {
  return xx_format_get_number_of_resources_pd(f, pd);
}
static inline void Abstractformat_set_number_of_resources(Abstractformat *f, uint64_t count) {
  xx_format_set_number_of_resources(f, count);
}

static inline uint64_t Abstractformat_get_number_of_metadata_pd(Abstractformat *f, xx_pd_struct *pd) {
  return xx_format_get_number_of_metadata_pd(f, pd);
}
static inline void Abstractformat_set_number_of_metadata(Abstractformat *f, uint64_t count) {
  xx_format_set_number_of_metadata(f, count);
}

static inline uint64_t Abstractformat_get_number_of_archive_records_pd(Abstractformat *f, xx_pd_struct *pd) {
  return xx_format_get_number_of_archive_records_pd(f, pd);
}
static inline void Abstractformat_set_number_of_archive_records(Abstractformat *f, uint64_t count) {
  xx_format_set_number_of_archive_records(f, count);
}

#define _XX_GET_REC_1(f) xx_format_get_number_of_archive_records_pd((f), NULL)
#define _XX_GET_REC_2(f, pd) xx_format_get_number_of_archive_records_pd((f), (pd))
#define _XX_GET_REC_CHOOSER(_1, _2, NAME, ...) NAME
#define _XX_GET_REC_EXPAND(x) x

#define get_number_of_archive_records(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_REC_2, _XX_GET_REC_1)(__VA_ARGS__))
#define Abstractformat_get_number_of_archive_records(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_REC_2, _XX_GET_REC_1)(__VA_ARGS__))
#define getNumberOfArchiveRecords(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_REC_2, _XX_GET_REC_1)(__VA_ARGS__))
#define get_number_of_records(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_REC_2, _XX_GET_REC_1)(__VA_ARGS__))
#define getNumberOfRecords(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_REC_2, _XX_GET_REC_1)(__VA_ARGS__))

#define _XX_GET_IMP_1(f) xx_format_get_number_of_imports_pd((f), NULL)
#define _XX_GET_IMP_2(f, pd) xx_format_get_number_of_imports_pd((f), (pd))
#define get_number_of_imports(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_IMP_2, _XX_GET_IMP_1)(__VA_ARGS__))
#define Abstractformat_get_number_of_imports(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_IMP_2, _XX_GET_IMP_1)(__VA_ARGS__))
#define getNumberOfImports(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_IMP_2, _XX_GET_IMP_1)(__VA_ARGS__))

#define _XX_GET_EXP_1(f) xx_format_get_number_of_exports_pd((f), NULL)
#define _XX_GET_EXP_2(f, pd) xx_format_get_number_of_exports_pd((f), (pd))
#define get_number_of_exports(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_EXP_2, _XX_GET_EXP_1)(__VA_ARGS__))
#define Abstractformat_get_number_of_exports(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_EXP_2, _XX_GET_EXP_1)(__VA_ARGS__))
#define getNumberOfExports(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_EXP_2, _XX_GET_EXP_1)(__VA_ARGS__))

#define _XX_GET_RES_1(f) xx_format_get_number_of_resources_pd((f), NULL)
#define _XX_GET_RES_2(f, pd) xx_format_get_number_of_resources_pd((f), (pd))
#define get_number_of_resources(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_RES_2, _XX_GET_RES_1)(__VA_ARGS__))
#define Abstractformat_get_number_of_resources(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_RES_2, _XX_GET_RES_1)(__VA_ARGS__))
#define getNumberOfResources(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_RES_2, _XX_GET_RES_1)(__VA_ARGS__))

#define _XX_GET_META_1(f) xx_format_get_number_of_metadata_pd((f), NULL)
#define _XX_GET_META_2(f, pd) xx_format_get_number_of_metadata_pd((f), (pd))
#define get_number_of_metadata(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_META_2, _XX_GET_META_1)(__VA_ARGS__))
#define Abstractformat_get_number_of_metadata(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_META_2, _XX_GET_META_1)(__VA_ARGS__))
#define getNumberOfMetadata(...) \
    _XX_GET_REC_EXPAND(_XX_GET_REC_CHOOSER(__VA_ARGS__, _XX_GET_META_2, _XX_GET_META_1)(__VA_ARGS__))

static inline bool Abstractformat_has_file_import(Abstractformat *f) {
  return xx_format_has_file_import(f);
}
static inline bool has_file_import(Abstractformat *f) {
  return xx_format_has_file_import(f);
}
static inline void Abstractformat_set_has_file_import(Abstractformat *f, bool val) {
  xx_format_set_has_file_import(f, val);
}

static inline bool Abstractformat_has_file_export(Abstractformat *f) {
  return xx_format_has_file_export(f);
}
static inline bool has_file_export(Abstractformat *f) {
  return xx_format_has_file_export(f);
}
static inline void Abstractformat_set_has_file_export(Abstractformat *f, bool val) {
  xx_format_set_has_file_export(f, val);
}

static inline bool Abstractformat_has_file_resources(Abstractformat *f) {
  return xx_format_has_file_resources(f);
}
static inline bool has_file_resources(Abstractformat *f) {
  return xx_format_has_file_resources(f);
}
static inline void Abstractformat_set_has_file_resources(Abstractformat *f, bool val) {
  xx_format_set_has_file_resources(f, val);
}

static inline bool Abstractformat_has_file_metadata(Abstractformat *f) {
  return xx_format_has_file_metadata(f);
}
static inline bool has_file_metadata(Abstractformat *f) {
  return xx_format_has_file_metadata(f);
}
static inline void Abstractformat_set_has_file_metadata(Abstractformat *f, bool val) {
  xx_format_set_has_file_metadata(f, val);
}

#define hasImport has_file_import
#define hasExport has_file_export
#define hasResources has_file_resources
#define hasMetadata has_file_metadata
#define hasFileImport has_file_import
#define hasFileExport has_file_export
#define hasFileResources has_file_resources
#define hasFileMetadata has_file_metadata
#define hasFileImportt has_file_import
static inline int Abstractformat_close(Abstractformat *f) {
  return xx_format_close(f);
}
static inline void Abstractformat_destroy(Abstractformat *f) {
  xx_format_destroy(f);
}

static inline bool Abstractformat_set_password(Abstractformat *f,
                                                const char *password_utf8) {
  return xx_format_set_password(f, password_utf8);
}

static inline const char *Abstractformat_get_password(
    const Abstractformat *f) {
  return xx_format_get_password(f);
}

/* --- Metadata & Archive Record Lifecycle --- */

XXFC_API void xx_meta_init(xx_meta *meta, uint32_t meta_id);
XXFC_API void xx_meta_cleanup(xx_meta *meta);
XXFC_API void xx_meta_free_elem(void *element);

XXFC_API void xx_archive_record_init(xx_archive_record *rec);
XXFC_API void xx_archive_record_cleanup(xx_archive_record *rec);
XXFC_API void xx_archive_record_free_elem(void *element);
XXFC_API bool xx_archive_record_add_meta(xx_archive_record *rec, uint32_t meta_id, const xx_var *var);
XXFC_API bool xx_archive_record_set_meta(xx_archive_record *rec, uint32_t meta_id, const xx_var *var);
XXFC_API bool xx_archive_record_add_meta_str(xx_archive_record *rec, uint32_t meta_id, const char *str);
XXFC_API bool xx_archive_record_add_meta_wstr(xx_archive_record *rec, uint32_t meta_id, const wchar_t *wstr);
XXFC_API bool xx_archive_record_add_meta_i64(xx_archive_record *rec, uint32_t meta_id, int64_t val);
XXFC_API bool xx_archive_record_add_meta_u64(xx_archive_record *rec, uint32_t meta_id, uint64_t val);
XXFC_API bool xx_archive_record_set_meta_str(xx_archive_record *rec, uint32_t meta_id, const char *str);
XXFC_API bool xx_archive_record_set_meta_wstr(xx_archive_record *rec, uint32_t meta_id, const wchar_t *wstr);
XXFC_API bool xx_archive_record_set_meta_i64(xx_archive_record *rec, uint32_t meta_id, int64_t val);
XXFC_API bool xx_archive_record_set_meta_u64(xx_archive_record *rec, uint32_t meta_id, uint64_t val);
XXFC_API bool xx_archive_record_set_meta_bool(xx_archive_record *rec, uint32_t meta_id, bool val);

XXFC_API const xx_var* xx_archive_record_find_meta(const xx_archive_record *rec, uint32_t meta_id);
XXFC_API const char* xx_archive_record_get_meta_str(const xx_archive_record *rec, uint32_t meta_id);
XXFC_API const wchar_t* xx_archive_record_get_meta_wstr(const xx_archive_record *rec, uint32_t meta_id);
XXFC_API int64_t xx_archive_record_get_meta_i64(const xx_archive_record *rec, uint32_t meta_id, int64_t default_val);
XXFC_API uint64_t xx_archive_record_get_meta_u64(const xx_archive_record *rec, uint32_t meta_id, uint64_t default_val);
XXFC_API bool xx_archive_record_get_meta_bool(const xx_archive_record *rec, uint32_t meta_id, bool default_val);

/* Convenience original name helpers */
static inline bool xx_archive_record_add_meta_unicode(xx_archive_record *rec, uint32_t meta_id, const wchar_t *wstr) {
  return xx_archive_record_add_meta_wstr(rec, meta_id, wstr);
}
static inline bool xx_archive_record_set_meta_unicode(xx_archive_record *rec, uint32_t meta_id, const wchar_t *wstr) {
  return xx_archive_record_set_meta_wstr(rec, meta_id, wstr);
}
static inline const wchar_t* xx_archive_record_get_meta_unicode(const xx_archive_record *rec, uint32_t meta_id) {
  return xx_archive_record_get_meta_wstr(rec, meta_id);
}
static inline const wchar_t* xx_archive_record_get_original_name_w(const xx_archive_record *rec) {
  return xx_archive_record_get_meta_wstr(rec, XX_META_ID_ORIGINAL_NAME);
}
static inline const char* xx_archive_record_get_original_name(const xx_archive_record *rec) {
  return xx_archive_record_get_meta_str(rec, XX_META_ID_ORIGINAL_NAME);
}
static inline bool xx_archive_record_set_original_name_w(xx_archive_record *rec, const wchar_t *wstr) {
  return xx_archive_record_set_meta_wstr(rec, XX_META_ID_ORIGINAL_NAME, wstr);
}
static inline bool xx_archive_record_set_original_name(xx_archive_record *rec, const char *str) {
  return xx_archive_record_set_meta_str(rec, XX_META_ID_ORIGINAL_NAME, str);
}

static inline void ArchiveRecord_init(xx_archive_record *rec) {
  xx_archive_record_init(rec);
}

static inline void ArchiveRecord_cleanup(xx_archive_record *rec) {
  xx_archive_record_cleanup(rec);
}

/* --- Archive Record Stream Reading Lifecycle & Operations --- */

XXFC_API void xx_archive_record_state_init(xx_archive_record_state *state, Abstractformat *fmt);
XXFC_API void xx_archive_record_state_cleanup(xx_archive_record_state *state);
XXFC_API void xx_archive_record_state_free(xx_archive_record_state *state);

XXFC_API xx_archive_record_state *xx_format_create_archive_records_reading(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_format_get_current_archive_record(Abstractformat *f, xx_archive_record_state *state);
XXFC_API bool xx_format_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_format_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_format_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *state);

static inline xx_archive_record_state *Abstractformat_create_archive_records_reading(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
  return xx_format_create_archive_records_reading(f, options, pd);
}
static inline const xx_archive_record *Abstractformat_get_current_archive_record(Abstractformat *f, xx_archive_record_state *state) {
  return xx_format_get_current_archive_record(f, state);
}
static inline bool Abstractformat_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
  return xx_format_unpack_current_archive_record(f, state, pd);
}
static inline bool Abstractformat_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
  return xx_format_archive_record_move_to_next(f, state, pd);
}
static inline void Abstractformat_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *state) {
  xx_format_free_archive_records_reading(f, state);
}

/* User-facing helper macros */
#define _XX_CREATE_ARCREAD_1(f) xx_format_create_archive_records_reading((f), NULL, NULL)
#define _XX_CREATE_ARCREAD_2(f, opt) xx_format_create_archive_records_reading((f), (opt), NULL)
#define _XX_CREATE_ARCREAD_3(f, opt, pd) xx_format_create_archive_records_reading((f), (opt), (pd))
#define _XX_CREATE_ARCREAD_CHOOSER(_1, _2, _3, NAME, ...) NAME

#define create_archive_records_reading(...) \
    _XX_GET_REC_EXPAND(_XX_CREATE_ARCREAD_CHOOSER(__VA_ARGS__, _XX_CREATE_ARCREAD_3, _XX_CREATE_ARCREAD_2, _XX_CREATE_ARCREAD_1)(__VA_ARGS__))

static inline const xx_archive_record *get_current_archive_record(Abstractformat *f, xx_archive_record_state *state) {
  return xx_format_get_current_archive_record(f, state);
}
#define get_curent_archive_record get_current_archive_record

static inline bool unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
  return xx_format_unpack_current_archive_record(f, state, pd);
}

static inline bool archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
  return xx_format_archive_record_move_to_next(f, state, pd);
}
#define move_to_the_next archive_record_move_to_next
#define move_to_next archive_record_move_to_next
#define moveToNext archive_record_move_to_next

static inline void free_archive_records_reading(Abstractformat *f, xx_archive_record_state *state) {
  xx_format_free_archive_records_reading(f, state);
}
#define free_active_reading_record free_archive_records_reading

/* --- Archive Record Stream Writing / Packing Lifecycle & Operations --- */

XXFC_API void xx_archive_write_state_init(xx_archive_write_state *state, Abstractformat *fmt);
XXFC_API void xx_archive_write_state_cleanup(xx_archive_write_state *state);
XXFC_API void xx_archive_write_state_free(xx_archive_write_state *state);

XXFC_API xx_archive_write_state *xx_format_create_archive_records_writing(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API bool xx_format_pack_archive_record(Abstractformat *f, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev, xx_pd_struct *pd);
XXFC_API bool xx_format_finalize_archive_records_writing(Abstractformat *f, xx_archive_write_state *state, xx_pd_struct *pd);
XXFC_API void xx_format_free_archive_records_writing(Abstractformat *f, xx_archive_write_state *state);

static inline xx_archive_write_state *Abstractformat_create_archive_records_writing(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
  return xx_format_create_archive_records_writing(f, options, pd);
}
static inline bool Abstractformat_pack_archive_record(Abstractformat *f, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev, xx_pd_struct *pd) {
  return xx_format_pack_archive_record(f, state, record, source_dev, pd);
}
static inline bool Abstractformat_finalize_archive_records_writing(Abstractformat *f, xx_archive_write_state *state, xx_pd_struct *pd) {
  return xx_format_finalize_archive_records_writing(f, state, pd);
}
static inline void Abstractformat_free_archive_records_writing(Abstractformat *f, xx_archive_write_state *state) {
  xx_format_free_archive_records_writing(f, state);
}

/* User-facing helper macros for packing/writing */
#define _XX_CREATE_ARCWRITE_1(f) xx_format_create_archive_records_writing((f), NULL, NULL)
#define _XX_CREATE_ARCWRITE_2(f, opt) xx_format_create_archive_records_writing((f), (opt), NULL)
#define _XX_CREATE_ARCWRITE_3(f, opt, pd) xx_format_create_archive_records_writing((f), (opt), (pd))
#define _XX_CREATE_ARCWRITE_CHOOSER(_1, _2, _3, NAME, ...) NAME

#define create_archive_records_writing(...) \
    _XX_GET_REC_EXPAND(_XX_CREATE_ARCWRITE_CHOOSER(__VA_ARGS__, _XX_CREATE_ARCWRITE_3, _XX_CREATE_ARCWRITE_2, _XX_CREATE_ARCWRITE_1)(__VA_ARGS__))

static inline bool pack_archive_record(Abstractformat *f, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev, xx_pd_struct *pd) {
  return xx_format_pack_archive_record(f, state, record, source_dev, pd);
}

static inline bool finalize_archive_records_writing(Abstractformat *f, xx_archive_write_state *state, xx_pd_struct *pd) {
  return xx_format_finalize_archive_records_writing(f, state, pd);
}

static inline void free_archive_records_writing(Abstractformat *f, xx_archive_write_state *state) {
  xx_format_free_archive_records_writing(f, state);
}

/* --- Data Struct Id <-> String Conversion & Stream Reading Lifecycle --- */

XXFC_API const char *xx_format_data_struct_id_to_string(Abstractformat *f, uint32_t id);
XXFC_API uint32_t xx_format_data_struct_string_to_id(Abstractformat *f, const char *name);

/**
 * @brief Classification string for a xx_data_struct_type_t value (e.g. "header", "table").
 */
XXFC_API const char *xx_data_struct_type_to_string(xx_data_struct_type_t type);

/**
 * @brief Build a dynamic Unicode URI-style description of a data struct, valid for any format,
 * e.g. L"ZIP::LOCAL_FILE_HEADER?offset=32&entry_size=30&total_size=30&count=1&type=header".
 * @return Newly allocated wide string (free with xx_str_wfree), or NULL on error.
 */
XXFC_API wchar_t *xx_format_data_struct_to_string(Abstractformat *f, const xx_data_struct *ds);

XXFC_API void xx_data_struct_state_init(xx_data_struct_state *state, Abstractformat *fmt);
XXFC_API void xx_data_struct_state_cleanup(xx_data_struct_state *state);
XXFC_API void xx_data_struct_state_free(xx_data_struct_state *state);

XXFC_API xx_data_struct_state *xx_format_create_data_structs_reading(Abstractformat *f, xx_pd_struct *pd);
XXFC_API const xx_data_struct *xx_format_get_current_data_struct(Abstractformat *f, xx_data_struct_state *state);
XXFC_API bool xx_format_data_struct_move_to_next(Abstractformat *f, xx_data_struct_state *state, xx_pd_struct *pd);
XXFC_API void xx_format_free_data_structs_reading(Abstractformat *f, xx_data_struct_state *state);

static inline const char *Abstractformat_data_struct_id_to_string(Abstractformat *f, uint32_t id) {
  return xx_format_data_struct_id_to_string(f, id);
}
static inline uint32_t Abstractformat_data_struct_string_to_id(Abstractformat *f, const char *name) {
  return xx_format_data_struct_string_to_id(f, name);
}
static inline const char *data_struct_id_to_string(Abstractformat *f, uint32_t id) {
  return xx_format_data_struct_id_to_string(f, id);
}
static inline uint32_t data_struct_string_to_id(Abstractformat *f, const char *name) {
  return xx_format_data_struct_string_to_id(f, name);
}

static inline const char *Abstractformat_data_struct_type_to_string(xx_data_struct_type_t type) {
  return xx_data_struct_type_to_string(type);
}
static inline wchar_t *Abstractformat_data_struct_to_string(Abstractformat *f, const xx_data_struct *ds) {
  return xx_format_data_struct_to_string(f, ds);
}
static inline wchar_t *data_struct_to_string(Abstractformat *f, const xx_data_struct *ds) {
  return xx_format_data_struct_to_string(f, ds);
}

static inline xx_data_struct_state *Abstractformat_create_data_structs_reading(Abstractformat *f, xx_pd_struct *pd) {
  return xx_format_create_data_structs_reading(f, pd);
}
static inline const xx_data_struct *Abstractformat_get_current_data_struct(Abstractformat *f, xx_data_struct_state *state) {
  return xx_format_get_current_data_struct(f, state);
}
static inline bool Abstractformat_data_struct_move_to_next(Abstractformat *f, xx_data_struct_state *state, xx_pd_struct *pd) {
  return xx_format_data_struct_move_to_next(f, state, pd);
}
static inline void Abstractformat_free_data_structs_reading(Abstractformat *f, xx_data_struct_state *state) {
  xx_format_free_data_structs_reading(f, state);
}

/* User-facing helper macros */
#define _XX_CREATE_DSREAD_1(f) xx_format_create_data_structs_reading((f), NULL)
#define _XX_CREATE_DSREAD_2(f, pd) xx_format_create_data_structs_reading((f), (pd))
#define _XX_CREATE_DSREAD_CHOOSER(_1, _2, NAME, ...) NAME

#define create_data_structs_reading(...) \
    _XX_GET_REC_EXPAND(_XX_CREATE_DSREAD_CHOOSER(__VA_ARGS__, _XX_CREATE_DSREAD_2, _XX_CREATE_DSREAD_1)(__VA_ARGS__))

static inline const xx_data_struct *get_current_data_struct(Abstractformat *f, xx_data_struct_state *state) {
  return xx_format_get_current_data_struct(f, state);
}

static inline bool data_struct_move_to_next(Abstractformat *f, xx_data_struct_state *state, xx_pd_struct *pd) {
  return xx_format_data_struct_move_to_next(f, state, pd);
}

static inline void free_data_structs_reading(Abstractformat *f, xx_data_struct_state *state) {
  xx_format_free_data_structs_reading(f, state);
}

/* --- Data Struct Record Lifecycle --- */

XXFC_API void xx_data_struct_record_init(xx_data_struct_record *rec);
XXFC_API void xx_data_struct_record_cleanup(xx_data_struct_record *rec);
XXFC_API void xx_data_struct_record_free_elem(void *element);

XXFC_API bool xx_data_struct_record_set_name(xx_data_struct_record *rec, const wchar_t *name);
XXFC_API bool xx_data_struct_record_set_type(xx_data_struct_record *rec, const wchar_t *type);
XXFC_API bool xx_data_struct_record_set_display_value(xx_data_struct_record *rec, const wchar_t *display_value);
XXFC_API bool xx_data_struct_record_set_value(xx_data_struct_record *rec, const xx_var *value);

XXFC_API bool xx_data_struct_record_populate(xx_data_struct_record *rec, xx_io_device *device,
                                            int64_t parent_offset, const xx_data_struct_field_desc *field,
                                            bool is_big_endian);

static inline bool xx_data_struct_record_has_property(const xx_data_struct_record *rec, xx_data_struct_record_property_t property) {
  return rec ? ((rec->property & property) != 0) : false;
}

/* --- Data Struct Records Stream Reading Lifecycle & Operations --- */

XXFC_API void xx_data_struct_record_state_init(xx_data_struct_record_state *state, Abstractformat *fmt, const xx_data_struct *ds);
XXFC_API void xx_data_struct_record_state_cleanup(xx_data_struct_record_state *state);
XXFC_API void xx_data_struct_record_state_free(xx_data_struct_record_state *state);

XXFC_API xx_data_struct_record_state *xx_format_create_data_struct_records_reading(Abstractformat *f, const xx_data_struct *ds, xx_pd_struct *pd);
XXFC_API const xx_data_struct_record *xx_format_get_current_data_struct_record(Abstractformat *f, xx_data_struct_record_state *state);
XXFC_API bool xx_format_data_struct_record_move_to_next(Abstractformat *f, xx_data_struct_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_format_free_data_struct_records_reading(Abstractformat *f, xx_data_struct_record_state *state);

static inline xx_data_struct_record_state *Abstractformat_create_data_struct_records_reading(Abstractformat *f, const xx_data_struct *ds, xx_pd_struct *pd) {
  return xx_format_create_data_struct_records_reading(f, ds, pd);
}
static inline const xx_data_struct_record *Abstractformat_get_current_data_struct_record(Abstractformat *f, xx_data_struct_record_state *state) {
  return xx_format_get_current_data_struct_record(f, state);
}
static inline bool Abstractformat_data_struct_record_move_to_next(Abstractformat *f, xx_data_struct_record_state *state, xx_pd_struct *pd) {
  return xx_format_data_struct_record_move_to_next(f, state, pd);
}
static inline void Abstractformat_free_data_struct_records_reading(Abstractformat *f, xx_data_struct_record_state *state) {
  xx_format_free_data_struct_records_reading(f, state);
}

static inline xx_data_struct_record_state *create_data_struct_records_reading(Abstractformat *f, const xx_data_struct *ds, xx_pd_struct *pd) {
  return xx_format_create_data_struct_records_reading(f, ds, pd);
}
static inline const xx_data_struct_record *get_current_data_struct_record(Abstractformat *f, xx_data_struct_record_state *state) {
  return xx_format_get_current_data_struct_record(f, state);
}
static inline bool data_struct_record_move_to_next(Abstractformat *f, xx_data_struct_record_state *state, xx_pd_struct *pd) {
  return xx_format_data_struct_record_move_to_next(f, state, pd);
}
static inline void free_data_struct_records_reading(Abstractformat *f, xx_data_struct_record_state *state) {
  xx_format_free_data_struct_records_reading(f, state);
}

/* --- Format Constructors & Lifecycle --- */

XXFC_API void xx_format_init(Abstractformat *fmt, xx_io_device *dev,
                             int64_t base_address);
XXFC_API Abstractformat *xx_format_create(xx_io_device *dev,
                                          int64_t base_address);
XXFC_API void xx_format_free(Abstractformat *fmt);

static inline void Abstractformat_init(Abstractformat *fmt, xx_io_device *dev,
                                       int64_t base_address) {
  xx_format_init(fmt, dev, base_address);
}

static inline Abstractformat *Abstractformat_create(xx_io_device *dev,
                                                    int64_t base_address) {
  return xx_format_create(dev, base_address);
}

static inline void Abstractformat_free(Abstractformat *fmt) {
  xx_format_free(fmt);
}

#ifdef __cplusplus
}
#endif

#endif /* XX_FORMAT_H */
