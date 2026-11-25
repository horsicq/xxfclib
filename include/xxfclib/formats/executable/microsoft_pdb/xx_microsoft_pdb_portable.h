/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft Portable PDB reader: ECMA-335 ("BSJB") metadata root,
 * stream directory, #Pdb stream, #~ / #- metadata tables, and
 * Document table reader.
 *
 * C port of _mylibs/XPDB (xpdb.cpp / xpdb.h); field and method names
 * follow it so the two can be compared line by line.
 *
 * Bounded and lazy: streams and blobs are read on demand through the
 * abstract xx_io_device interface. The whole file is never loaded.
 *
 * Strings: every `char *` and `char **` returned by this API is UTF-8,
 * NUL-terminated, owned by the caller, and must be released with the
 * matching free function (xx_ms_pdb_portable_details_free or
 * xx_ms_pdb_portable_documents_free).
 */
#ifndef XX_MICROSOFT_PDB_PORTABLE_H
#define XX_MICROSOFT_PDB_PORTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Portable PDB Constants --------------------------------------------- */

#ifndef XX_MS_PDB_PORTABLE_SIGNATURE
#define XX_MS_PDB_PORTABLE_SIGNATURE 0x424A5342 /* "BSJB" */
#endif

#ifndef XX_MS_PDB_PORTABLE_VERSION_OFFSET
#define XX_MS_PDB_PORTABLE_VERSION_OFFSET 16
#endif

/* Heap size flags of the table stream header */
#ifndef XX_MS_PDB_PORTABLE_HEAPSIZE_STRINGS
#define XX_MS_PDB_PORTABLE_HEAPSIZE_STRINGS 0x01 /* #Strings uses 4-byte indexes */
#endif
#ifndef XX_MS_PDB_PORTABLE_HEAPSIZE_GUID
#define XX_MS_PDB_PORTABLE_HEAPSIZE_GUID 0x02 /* #GUID uses 4-byte indexes */
#endif
#ifndef XX_MS_PDB_PORTABLE_HEAPSIZE_BLOB
#define XX_MS_PDB_PORTABLE_HEAPSIZE_BLOB 0x04 /* #Blob uses 4-byte indexes */
#endif

/* Metadata table indexes */
#ifndef XX_MS_PDB_TABLE_MODULE
#define XX_MS_PDB_TABLE_MODULE 0x00
#define XX_MS_PDB_TABLE_TYPEREF 0x01
#define XX_MS_PDB_TABLE_TYPEDEF 0x02
#define XX_MS_PDB_TABLE_FIELD 0x04
#define XX_MS_PDB_TABLE_METHODDEF 0x06
#define XX_MS_PDB_TABLE_PARAM 0x08
#define XX_MS_PDB_TABLE_DOCUMENT 0x30
#define XX_MS_PDB_TABLE_METHODDEBUGINFORMATION 0x31
#define XX_MS_PDB_TABLE_LOCALSCOPE 0x32
#define XX_MS_PDB_TABLE_LOCALVARIABLE 0x33
#define XX_MS_PDB_TABLE_LOCALCONSTANT 0x34
#define XX_MS_PDB_TABLE_IMPORTSCOPE 0x35
#define XX_MS_PDB_TABLE_STATEMACHINEMETHOD 0x36
#define XX_MS_PDB_TABLE_CUSTOMDEBUGINFORMATION 0x37
#define XX_MS_PDB_TABLE_MAX 0x40
#endif

/* Well-known stream names of a portable PDB */
#define XX_MS_PDB_PORTABLE_STREAM_PDB "#Pdb"
#define XX_MS_PDB_PORTABLE_STREAM_TABLES "#~"
#define XX_MS_PDB_PORTABLE_STREAM_TABLES_UNCOMPRESSED "#-"
#define XX_MS_PDB_PORTABLE_STREAM_BLOB "#Blob"
#define XX_MS_PDB_PORTABLE_STREAM_STRINGS "#Strings"
#define XX_MS_PDB_PORTABLE_STREAM_GUID "#GUID"
#define XX_MS_PDB_PORTABLE_STREAM_USERSTRINGS "#US"

/* ---- Structures --------------------------------------------------------- */

/* One stream of a portable PDB (metadata heap or table stream), XPDB::MDSTREAM. */
typedef struct xx_ms_pdb_portable_stream {
    char *name;            /* NUL-terminated UTF-8 stream name (owned by caller / freed by details_free) */
    uint32_t offset;       /* Byte offset from the start of the metadata root */
    uint32_t size;         /* Byte size of the stream */
    int64_t header_offset; /* Absolute device offset of the stream header itself */
} xx_ms_pdb_portable_stream;

typedef xx_ms_pdb_portable_stream xx_ms_pdb_portable_md_stream;

/* Portable PDB details (XPDB::PORTABLEINFO). */
typedef struct xx_ms_pdb_portable_details {
    bool is_valid;                      /* True if a valid ECMA-335 "BSJB" metadata root was parsed */
    uint16_t major_version;             /* Metadata root major version */
    uint16_t minor_version;             /* Metadata root minor version */
    char *version;                      /* Version string, e.g. "PDB v1.0", caller-owned */
    uint16_t flags;                     /* Flags following the version string */
    uint16_t number_of_streams;         /* Declared number of streams in metadata root */
    xx_ms_pdb_portable_stream *streams; /* Array of valid streams, caller-owned */
    size_t stream_count;                /* Number of valid streams in streams array */

    /* #Pdb stream information */
    bool pdb_stream_valid;                  /* True if the #Pdb stream was found and valid */
    char *pdb_id;                           /* GUID string "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}", caller-owned */
    uint8_t pdb_id_raw[20];                 /* Raw 20-byte id (16-byte GUID + 4-byte timestamp) */
    uint32_t pdb_stamp;                     /* Time stamp */
    uint32_t stamp;                         /* Alias for pdb_stamp */
    uint32_t entry_point;                   /* Entry point token or 0 */
    uint64_t referenced_type_system_tables; /* Mask of type-system tables referenced */
    uint32_t type_system_table_rows[64];    /* Row counts for referenced type-system tables */

    /* #~ or #- metadata table stream header */
    bool tables_valid;            /* True if the table stream header was parsed */
    uint8_t tables_major_version; /* Table schema major version */
    uint8_t tables_minor_version; /* Table schema minor version */
    uint8_t heap_sizes;           /* Heap size bitmask (0x01 strings, 0x02 GUID, 0x04 blob) */
    uint64_t valid_tables;        /* 64-bit mask of present tables in table stream */
    uint64_t valid_mask;          /* Alias for valid_tables */
    uint64_t sorted_tables;       /* 64-bit mask of sorted tables */
    uint64_t sorted_mask;         /* Alias for sorted_tables */
    uint32_t table_rows[64];      /* Row counts per table (0..63), including type-system rows */
} xx_ms_pdb_portable_details;

/* ---- Public API --------------------------------------------------------- */

/**
 * @brief Read portable PDB information from an xx_io_device.
 *
 * Parses the ECMA-335 metadata root at base offset 0, enumerates stream headers,
 * inspects the #Pdb stream (20-byte id, entry point, referenced type-system tables
 * and row counts), and inspects the #~ / #- table stream (version, heap sizes,
 * valid/sorted masks, table row counts).
 *
 * All string and array members in @p out are allocated and owned by the caller.
 * Release with xx_ms_pdb_portable_details_free().
 *
 * @param device Open, seekable I/O device.
 * @param out Pointer to details struct to populate.
 * @return true on success, false on I/O error, invalid format, or allocation failure.
 */
XXFC_API bool xx_ms_pdb_portable_read_info(xx_io_device *device, xx_ms_pdb_portable_details *out);

/**
 * @brief Free all resources allocated in an xx_ms_pdb_portable_details structure.
 *
 * Frees version string, pdb_id string, stream names, and streams array.
 * Clears the structure to zero. Safe to call on NULL or already-freed structs.
 *
 * @param d Pointer to details struct to free.
 */
XXFC_API void xx_ms_pdb_portable_details_free(xx_ms_pdb_portable_details *d);

/**
 * @brief Read source document paths from the Document table of a portable PDB.
 *
 * Follows XPDB's document-name decoding rule: the first byte of each Document
 * row's Name blob is the separator character; subsequent compressed unsigned
 * integers are Blob heap indexes of individual path parts. Parts are fetched
 * from the #Blob stream and joined using the separator.
 *
 * Reading is lazy and bounded by stream size; the whole file is never loaded.
 *
 * Returned strings and array are owned by the caller, released with
 * xx_ms_pdb_portable_documents_free().
 *
 * @param device Open, seekable I/O device.
 * @param limit Maximum number of documents to return, or negative for all.
 * @param names Output pointer to array of UTF-8 document path strings.
 * @param count Output pointer to number of returned document strings.
 * @return true on success (including 0 documents found), false on bad argument or allocation failure.
 */
XXFC_API bool xx_ms_pdb_portable_get_documents(xx_io_device *device, int32_t limit, char ***names, size_t *count);

/**
 * @brief Free document paths array and individual strings returned by xx_ms_pdb_portable_get_documents.
 *
 * @param names Array of string pointers to free.
 * @param count Number of elements in the array.
 */
XXFC_API void xx_ms_pdb_portable_documents_free(char **names, size_t count);

/**
 * @brief Convert metadata table ID (0..63) to human-readable name.
 *
 * Matches XPDB::tableIdToString. For recognized tables, returns static string
 * (e.g. "Document", "MethodDebugInformation", "Module", etc.).
 * For unrecognized table IDs, returns formatted hex string (e.g. "0x03").
 *
 * @param table Table ID (0..63).
 * @return Static string pointer (valid for the lifetime of the process).
 */
XXFC_API const char *xx_ms_pdb_portable_table_name(uint32_t table);

/* ---- Helper Utilities --------------------------------------------------- */

/**
 * @brief Find stream by name in parsed details.
 *
 * @param d Pointer to parsed details.
 * @param name Stream name (e.g. "#Pdb", "#~", "#Blob").
 * @return Pointer to stream entry, or NULL if not found.
 */
XXFC_API const xx_ms_pdb_portable_stream *xx_ms_pdb_portable_get_stream(const xx_ms_pdb_portable_details *d, const char *name);

/**
 * @brief Decode an ECMA-335 compressed unsigned integer (II.23.2).
 *
 * 1-byte, 2-byte, or 4-byte big-endian encoding determined by top bits.
 *
 * @param data Buffer containing encoded data.
 * @param data_size Size of buffer in bytes.
 * @param offset Byte offset within buffer.
 * @param out_value Output pointer for decoded value.
 * @return Number of bytes read (1, 2, or 4), or 0 on error/out-of-bounds.
 */
XXFC_API int32_t xx_ms_pdb_portable_read_compressed_uint(const void *data, int32_t data_size, int32_t offset, uint32_t *out_value);

/**
 * @brief Count number of set bits in a 64-bit integer (popcount).
 *
 * Matches XPDB::_countBits.
 *
 * @param value 64-bit unsigned integer.
 * @return Number of bits set (0..64).
 */
XXFC_API int32_t xx_ms_pdb_portable_count_bits(uint64_t value);

#ifdef __cplusplus
}
#endif

#endif /* XX_MICROSOFT_PDB_PORTABLE_H */
