/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft program database reader: MSF 7.00 ("DS"), the old
 * "program database 2.00" layout ("JG") and portable PDB (ECMA-335 "BSJB").
 * C port of _mylibs/XPDB (XPDB); field and method names follow it so the
 * two can be compared line by line.
 *
 * JG files (XX_MS_PDB_TYPE_PDB2) keep their types and symbols in the record
 * kinds of their time: LF_*_16t / S_*_16t (16 bit type indices) or LF_*_ST /
 * S_*_ST (length prefixed names). For such a file every record is reported
 * in the shape of its current counterpart: the kind is the current LF_ / S_
 * value, the type indices are 32 bit, the names are C strings. The record
 * offsets and sizes of a type then describe the normalised record, not the
 * bytes of the stream. MSF 7.00 files are read as they are.
 *
 * Bounded and lazy: stream pages are read on demand through the block map,
 * the whole file is never loaded. Payloads are never executed.
 *
 * Strings: every `const char *` returned by this API is UTF-8, NUL
 * terminated, owned by the xx_ms_pdb object and valid until
 * xx_ms_pdb_close(). Arrays returned through an `xx_ms_pdb_*_list` are owned
 * by the caller and released with the matching *_list_free().
 *
 * Strict mode (xx_ms_pdb_set_strict, off by default). By default the reader
 * decodes exactly as XPDB does, its mistakes included, so that a program
 * built on it gives the output of its Qt counterpart. Strict mode decodes
 * these places the way the format defines them:
 *  1. Names. XPDB steps over a name by its UTF-16 length, not its byte
 *     length, and cuts a name at 4096 (symbols, modules, type list) or 8192
 *     bytes (decoded types, field lists). After a name with a non-ASCII
 *     character, or a longer one, the unique name of a type, the next entry
 *     of a field list and the object name / next record of the module list
 *     are read from the wrong place. Strict: a name is stepped over by its
 *     bytes and ends at its zero byte or at the end of its record.
 *     The text itself stays what it is in both modes: UTF-8, with U+FFFD for
 *     every byte that is not valid UTF-8 (the names of an old ANSI file are
 *     in a code page the file does not record; MSDIA converts them with the
 *     code page of the machine). Strict only keeps a leading U+FEFF, which
 *     XPDB drops.
 *  2. LF_ENUM size. XPDB takes it from the low byte of the underlying type
 *     index even when that is an index of the type stream. Strict follows
 *     LF_MODIFIER / LF_ENUM records to the primitive type (0 if there is none).
 *  3. xx_ms_pdb_get_method_list. XPDB reads the vtable offset of an
 *     introducing method wherever it falls, also behind the record. Strict:
 *     an entry cut off by the end of its record ends the list.
 *  4. Numeric leaves. XPDB knows LF_CHAR .. LF_UQUADWORD and takes every
 *     other leaf for 2 bytes. Strict steps over LF_REAL48, LF_COMPLEX32/64/
 *     80/128, LF_VARSTRING, LF_OCTWORD, LF_UOCTWORD, LF_DECIMAL, LF_DATE,
 *     LF_UTF8STRING and LF_REAL16 by their size; the value of a 128 bit
 *     integer is its low 64 bits, that of the others 0.
 *  5. Field lists. XPDB ends a list at LF_NESTTYPEEX, LF_FRIENDFCN,
 *     LF_FRIENDCLS, LF_VFUNCOFF and LF_MEMBERMODIFY. Strict reports
 *     LF_NESTTYPEEX as LF_NESTTYPE with its attributes and walks over the
 *     other four without listing them (as for a JG file in both modes).
 *  6. xx_ms_pdb_get_pdb_info. XPDB lists the word behind the named stream map
 *     (the largest name index, usually 0) as the first feature code. Strict
 *     leaves it out.
 *  7. xx_ms_pdb_get_section_contribs. With 32 byte (V2) entries XPDB accepts
 *     a last entry of 28 bytes. Strict wants all 32.
 * Everything else is the same in both modes. In particular the member count
 * of LF_CLASS2 / LF_STRUCTURE2 / LF_INTERFACE2 / LF_UNION2 is 2 plain bytes
 * in both (MSDIA reads it so; a numeric leaf there breaks it). In a JG file
 * the numeric leaves of point 4 are stepped over correctly in both modes.
 */
#ifndef XX_MICROSOFT_PDB_H
#define XX_MICROSOFT_PDB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_ms_pdb_type {
    XX_MS_PDB_TYPE_UNKNOWN = 0,
    XX_MS_PDB_TYPE_PDB2,    /* "Microsoft C/C++ program database 2.00", JG */
    XX_MS_PDB_TYPE_PDB7,    /* "Microsoft C/C++ MSF 7.00", DS */
    XX_MS_PDB_TYPE_PORTABLE /* ECMA-335 metadata, "BSJB" */
} xx_ms_pdb_type;

typedef struct xx_ms_pdb xx_ms_pdb; /* opaque */

/* ---- MSF container ---------------------------------------------------- */

typedef struct xx_ms_pdb_msf_header {
    bool is_valid;
    uint32_t block_size;
    uint32_t free_block_map_block;
    uint32_t num_blocks;
    uint32_t num_directory_bytes;
    uint32_t block_map_addr;
} xx_ms_pdb_msf_header;

typedef struct xx_ms_pdb_stream {
    int32_t index;
    int64_t size;      /* -1 / 0xFFFFFFFF "nil" streams report -1 */
    const char *name;  /* well known / named streams, else "" */
} xx_ms_pdb_stream;

/* ---- streams ------------------------------------------------------------ */

typedef struct xx_ms_pdb_info {
    bool is_valid;
    uint32_t version;
    const char *version_string;
    uint32_t signature;
    uint32_t age;
    const char *guid; /* "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" as XPDB::_guidToString */
    size_t feature_count;
    const uint32_t *features;
    bool minimal_debug_info;
    bool no_type_merge;
    bool has_id_stream;
} xx_ms_pdb_info;

typedef struct xx_ms_pdb_tpi_info {
    bool is_valid;
    uint32_t version;
    const char *version_string;
    uint32_t header_size;
    uint32_t type_index_begin;
    uint32_t type_index_end;
    uint32_t type_record_bytes;
    uint32_t number_of_types;
    uint16_t hash_stream_index;
    uint16_t hash_aux_stream_index;
    uint32_t hash_key_size;
    uint32_t num_hash_buckets;
    int32_t hash_value_buffer_offset;
    uint32_t hash_value_buffer_length;
    int32_t index_offset_buffer_offset;
    uint32_t index_offset_buffer_length;
    int32_t hash_adj_buffer_offset;
    uint32_t hash_adj_buffer_length;
} xx_ms_pdb_tpi_info;

typedef struct xx_ms_pdb_dbi_info {
    bool is_valid;
    int32_t version_signature;
    uint32_t version_header;
    const char *version_string;
    uint32_t age;
    uint16_t global_stream_index;
    uint16_t build_number;
    uint16_t public_stream_index;
    uint16_t pdb_dll_version;
    uint16_t sym_record_stream;
    uint16_t pdb_dll_rbld;
    int32_t mod_info_size;
    int32_t section_contribution_size;
    int32_t section_map_size;
    int32_t source_info_size;
    int32_t type_server_map_size;
    uint32_t mfc_type_server_index;
    int32_t optional_dbg_header_size;
    int32_t ec_substream_size;
    uint16_t flags;
    uint16_t machine;
    const char *machine_string;
    bool is_incremental;
    bool is_stripped;
    bool is_ctypes;
    uint16_t toolchain_major;
    uint16_t toolchain_minor;
    bool new_version_format;
    size_t dbg_stream_count;
    const uint16_t *dbg_streams;
} xx_ms_pdb_dbi_info;

typedef struct xx_ms_pdb_section_contrib {
    uint16_t section;
    int32_t offset;
    int32_t size;
    uint32_t characteristics;
    uint16_t module_index;
    uint32_t data_crc;
    uint32_t reloc_crc;
} xx_ms_pdb_section_contrib;

typedef struct xx_ms_pdb_module {
    int32_t index;
    const char *module_name;
    const char *object_name;
    uint16_t flags;
    uint16_t stream_index;
    uint32_t sym_byte_size;
    uint32_t c11_byte_size;
    uint32_t c13_byte_size;
    uint16_t source_file_count;
    xx_ms_pdb_section_contrib section_contrib;
} xx_ms_pdb_module;

typedef struct xx_ms_pdb_section_header {
    const char *name;
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t size_of_raw_data;
    uint32_t pointer_to_raw_data;
    uint32_t characteristics;
} xx_ms_pdb_section_header;

typedef struct xx_ms_pdb_symbol {
    uint16_t kind;
    const char *kind_string;
    const char *name;
    uint32_t type_index;
    uint32_t offset;      /* inside the segment */
    uint16_t segment;     /* 1 based section number */
    uint32_t flags;
    uint64_t address;     /* RVA, (uint64_t)-1 if unknown */
    int32_t module_index;
    int64_t value;        /* S_CONSTANT */
    bool is_value_valid;  /* S_CONSTANT */
    uint16_t value_leaf;  /* S_CONSTANT: numeric leaf the value was stored in */
    int64_t record_offset;
    int32_t record_size;
    /* S_GPROC32 / S_LPROC32 / S_GPROC32_ID / S_LPROC32_ID (and their JG 16t / ST
     * forms): the PROCSYM32 fields; 0 for every other kind. parent / end /
     * next are offsets in the same symbol stream (end = the S_END record),
     * debug_start / debug_end are offsets from the start of the code. */
    uint32_t parent;
    uint32_t end;
    uint32_t next;
    uint32_t code_size;
    uint32_t debug_start;
    uint32_t debug_end;
} xx_ms_pdb_symbol;

typedef struct xx_ms_pdb_type_record {
    uint32_t type_index;
    uint16_t kind;
    const char *kind_string;
    const char *name;
    int64_t record_offset;
    int32_t record_size;
} xx_ms_pdb_type_record;

/* A decoded CodeView type record (XPDB::CVTYPE). Only fields that apply to
 * the record kind are filled in; the rest are zero / "". */
typedef struct xx_ms_pdb_cvtype {
    bool is_valid;
    uint32_t type_index;
    uint16_t kind;
    const char *name;
    const char *unique_name;
    uint64_t size;
    uint32_t field_list_index;
    uint32_t element_type_index;
    uint32_t index_type_index;
    uint32_t arg_list_index;
    uint32_t class_type_index;
    uint32_t this_type_index;
    uint32_t count;
    uint16_t property;
    uint8_t udt_kind;  /* XX_MS_PDB_UDT_KIND_* */
    uint8_t calling_convention;
    uint8_t bit_position;
    uint8_t bit_size;
    bool is_forward_ref;
    bool is_nested;
    bool is_scoped;
    bool is_const;
    bool is_volatile;
    bool is_unaligned;
    bool is_pointer;
    bool is_reference;
    uint8_t pointer_mode;
    int64_t record_offset;
    int32_t record_size;
} xx_ms_pdb_cvtype;

/* One entry of a field list (XPDB::CVFIELD). */
typedef struct xx_ms_pdb_cvfield {
    uint16_t kind;
    const char *name;
    uint32_t type_index;
    int64_t offset;
    uint16_t attr;
    uint8_t access; /* XX_MS_PDB_FLD_ACCESS_* */
    uint8_t method_property;
    bool is_static;
    int64_t value;
    bool is_value_valid;
    uint16_t value_leaf;
    int32_t method_count;
    uint32_t method_list_index;
    uint32_t vbptr_type_index;
    int64_t vbptr_offset;
    int64_t vbtable_index;
    uint32_t vtable_offset;
    bool is_vtable_offset_valid;
} xx_ms_pdb_cvfield;

/* XX_MS_PDB_UDT_KIND_STRUCT/CLASS/UNION/INTERFACE (0..3) and
 * XX_MS_PDB_FLD_ACCESS_NONE/PRIVATE/PROTECTED/PUBLIC (0..3) are #defined in
 * xx_microsoft_pdb_def.h; an enum with the same names here does not compile. */

/* Generic growable result arrays. */
typedef struct xx_ms_pdb_symbol_list { xx_ms_pdb_symbol *items; size_t count; } xx_ms_pdb_symbol_list;
typedef struct xx_ms_pdb_module_list { xx_ms_pdb_module *items; size_t count; } xx_ms_pdb_module_list;
typedef struct xx_ms_pdb_cvfield_list { xx_ms_pdb_cvfield *items; size_t count; } xx_ms_pdb_cvfield_list;
typedef struct xx_ms_pdb_u32_list { uint32_t *items; size_t count; } xx_ms_pdb_u32_list;
typedef struct xx_ms_pdb_stream_list { xx_ms_pdb_stream *items; size_t count; } xx_ms_pdb_stream_list;
typedef struct xx_ms_pdb_type_record_list { xx_ms_pdb_type_record *items; size_t count; } xx_ms_pdb_type_record_list;
typedef struct xx_ms_pdb_section_header_list { xx_ms_pdb_section_header *items; size_t count; } xx_ms_pdb_section_header_list;
typedef struct xx_ms_pdb_section_contrib_list { xx_ms_pdb_section_contrib *items; size_t count; } xx_ms_pdb_section_contrib_list;

XXFC_API void xx_ms_pdb_symbol_list_free(xx_ms_pdb_symbol_list *list);
XXFC_API void xx_ms_pdb_module_list_free(xx_ms_pdb_module_list *list);
XXFC_API void xx_ms_pdb_cvfield_list_free(xx_ms_pdb_cvfield_list *list);
XXFC_API void xx_ms_pdb_u32_list_free(xx_ms_pdb_u32_list *list);
XXFC_API void xx_ms_pdb_stream_list_free(xx_ms_pdb_stream_list *list);
XXFC_API void xx_ms_pdb_type_record_list_free(xx_ms_pdb_type_record_list *list);
XXFC_API void xx_ms_pdb_section_header_list_free(xx_ms_pdb_section_header_list *list);
XXFC_API void xx_ms_pdb_section_contrib_list_free(xx_ms_pdb_section_contrib_list *list);

/* ---- lifetime ----------------------------------------------------------- */

/* Opens over a device (not taken over; must outlive the object).
 * Returns NULL if the device is not a recognised PDB. pd may be NULL. */
XXFC_API xx_ms_pdb *xx_ms_pdb_open(xx_io_device *device, xx_pd_struct *pd);
XXFC_API void xx_ms_pdb_close(xx_ms_pdb *pdb);
XXFC_API bool xx_ms_pdb_is_valid_device(xx_io_device *device);
XXFC_API xx_ms_pdb_type xx_ms_pdb_get_type(const xx_ms_pdb *pdb);

/* ---- MSF ---------------------------------------------------------------- */

XXFC_API xx_ms_pdb_msf_header xx_ms_pdb_get_msf_header(xx_ms_pdb *pdb);
XXFC_API int32_t xx_ms_pdb_get_number_of_streams(xx_ms_pdb *pdb);
XXFC_API bool xx_ms_pdb_get_streams(xx_ms_pdb *pdb, xx_ms_pdb_stream_list *out);
XXFC_API int64_t xx_ms_pdb_get_stream_size(xx_ms_pdb *pdb, int32_t index);
/* Reads [offset, offset+size) of a stream into buffer; returns bytes read
 * (short at the stream end), -1 on error. */
XXFC_API int64_t xx_ms_pdb_read_stream(xx_ms_pdb *pdb, int32_t index, int64_t offset, void *buffer, int64_t size);

/* ---- streams ------------------------------------------------------------ */

XXFC_API xx_ms_pdb_info xx_ms_pdb_get_pdb_info(xx_ms_pdb *pdb);
XXFC_API xx_ms_pdb_tpi_info xx_ms_pdb_get_tpi_info(xx_ms_pdb *pdb);
XXFC_API xx_ms_pdb_tpi_info xx_ms_pdb_get_ipi_info(xx_ms_pdb *pdb);
XXFC_API xx_ms_pdb_dbi_info xx_ms_pdb_get_dbi_info(xx_ms_pdb *pdb);
XXFC_API bool xx_ms_pdb_get_modules(xx_ms_pdb *pdb, xx_ms_pdb_module_list *out);
XXFC_API bool xx_ms_pdb_get_section_contribs(xx_ms_pdb *pdb, int32_t limit, xx_ms_pdb_section_contrib_list *out);
XXFC_API bool xx_ms_pdb_get_section_headers(xx_ms_pdb *pdb, xx_ms_pdb_section_header_list *out);
/* Global symbol record stream (XPDB::getSymbols). limit -1 = all. */
XXFC_API bool xx_ms_pdb_get_symbols(xx_ms_pdb *pdb, int32_t limit, xx_ms_pdb_symbol_list *out);
XXFC_API bool xx_ms_pdb_get_module_symbols(xx_ms_pdb *pdb, int32_t module_index, int32_t limit, xx_ms_pdb_symbol_list *out);
XXFC_API bool xx_ms_pdb_get_public_symbols(xx_ms_pdb *pdb, int32_t limit, xx_ms_pdb_symbol_list *out);
XXFC_API bool xx_ms_pdb_get_types(xx_ms_pdb *pdb, int32_t limit, xx_ms_pdb_type_record_list *out);
XXFC_API bool xx_ms_pdb_get_ids(xx_ms_pdb *pdb, int32_t limit, xx_ms_pdb_type_record_list *out);
/* Named stream index (e.g. "/names"), -1 if absent. */
XXFC_API int32_t xx_ms_pdb_get_named_stream_index(xx_ms_pdb *pdb, const char *name);

/* ---- CodeView types (XPDB::loadTypes and friends) ------------------------ */

XXFC_API bool xx_ms_pdb_load_types(xx_ms_pdb *pdb);
XXFC_API void xx_ms_pdb_unload_types(xx_ms_pdb *pdb);
XXFC_API bool xx_ms_pdb_is_types_loaded(const xx_ms_pdb *pdb);
XXFC_API uint32_t xx_ms_pdb_get_type_index_begin(xx_ms_pdb *pdb);
XXFC_API uint32_t xx_ms_pdb_get_type_index_end(xx_ms_pdb *pdb);
XXFC_API xx_ms_pdb_cvtype xx_ms_pdb_get_type_by_index(xx_ms_pdb *pdb, uint32_t type_index);
XXFC_API bool xx_ms_pdb_get_field_list(xx_ms_pdb *pdb, uint32_t type_index, xx_ms_pdb_cvfield_list *out);
XXFC_API bool xx_ms_pdb_get_method_list(xx_ms_pdb *pdb, uint32_t type_index, int32_t count, xx_ms_pdb_cvfield_list *out);
XXFC_API bool xx_ms_pdb_get_arg_list(xx_ms_pdb *pdb, uint32_t type_index, xx_ms_pdb_u32_list *out);
/* Forward reference -> index of its definition (or the input if none). */
XXFC_API uint32_t xx_ms_pdb_resolve_forward_ref(xx_ms_pdb *pdb, uint32_t type_index);
XXFC_API uint32_t xx_ms_pdb_resolve_forward_ref_type(xx_ms_pdb *pdb, const xx_ms_pdb_cvtype *cvtype);

/* ---- static helpers (XPDB static methods) -------------------------------- */

XXFC_API bool xx_ms_pdb_is_udt_leaf(uint16_t kind);
XXFC_API bool xx_ms_pdb_is_primitive_type(uint32_t type_index);
XXFC_API bool xx_ms_pdb_is_primitive_pointer(uint32_t type_index);
XXFC_API uint32_t xx_ms_pdb_primitive_type_strip_pointer(uint32_t type_index);
XXFC_API int32_t xx_ms_pdb_primitive_pointer_size(uint32_t type_index);
XXFC_API int32_t xx_ms_pdb_primitive_type_to_basic_type(uint32_t type_index); /* DIA BasicType */
XXFC_API int32_t xx_ms_pdb_primitive_type_size(uint32_t type_index);
XXFC_API const char *xx_ms_pdb_primitive_type_to_string(uint32_t type_index);
XXFC_API uint8_t xx_ms_pdb_field_attr_to_access(uint16_t attr);
XXFC_API const char *xx_ms_pdb_symbol_kind_to_string(uint32_t kind);
XXFC_API const char *xx_ms_pdb_leaf_kind_to_string(uint32_t kind);
XXFC_API const char *xx_ms_pdb_machine_to_string(uint32_t machine);

/* ---- additions of the parser lane ------------------------------------------ */

/* One stream of a portable PDB (metadata heap or table stream), XPDB::MDSTREAM. */
typedef struct xx_ms_pdb_md_stream {
    const char *name;
    uint32_t offset;       /* from the start of the metadata root */
    uint32_t size;
    int64_t header_offset; /* offset of the stream header itself */
} xx_ms_pdb_md_stream;

/* Portable PDB basics (XPDB::PORTABLEINFO without the table row counts). */
typedef struct xx_ms_pdb_portable_info {
    bool is_valid;
    uint16_t major_version;
    uint16_t minor_version;
    const char *version; /* "PDB v1.0" */
    uint16_t flags;
    uint16_t number_of_streams;
    size_t stream_count;
    const xx_ms_pdb_md_stream *streams; /* owned by the xx_ms_pdb object */
    bool pdb_stream_valid;
    const char *pdb_id; /* the GUID part of the 20 byte id */
    uint32_t pdb_stamp;
    uint32_t entry_point;
    uint64_t referenced_type_system_tables;
    bool tables_valid;
    uint8_t tables_major_version;
    uint8_t tables_minor_version;
    uint8_t heap_sizes;
    uint64_t valid_tables;
    uint64_t sorted_tables;
} xx_ms_pdb_portable_info;

/* xx_ms_pdb_open only borrows its pd for the open itself. This sets the
 * progress object the later calls poll for a stop request (NULL = none); it
 * has to stay alive while it is set. A stopped call returns what it has. */
XXFC_API void xx_ms_pdb_set_pd(xx_ms_pdb *pdb, xx_pd_struct *pd);
/* Strict decoding on / off (off after xx_ms_pdb_open), see the list at the
 * top of this file. Call it right after xx_ms_pdb_open, before
 * xx_ms_pdb_load_types and the getters. A change of the mode later unloads
 * the types and drops the cached PDB info and module list, so they are read
 * again in the new mode; the `features` array of an xx_ms_pdb_info taken
 * before the change is then no longer valid (strings stay valid). Lists the
 * caller already holds keep the mode they were made in. */
XXFC_API void xx_ms_pdb_set_strict(xx_ms_pdb *pdb, bool strict);
XXFC_API bool xx_ms_pdb_is_strict(const xx_ms_pdb *pdb);
XXFC_API xx_ms_pdb_portable_info xx_ms_pdb_get_portable_info(xx_ms_pdb *pdb);
/* The named stream map of the PDB info stream (XPDB::getNamedStreams), in
 * QMap key order: name, index = stream index, size = that stream's size. */
XXFC_API bool xx_ms_pdb_get_named_streams(xx_ms_pdb *pdb, xx_ms_pdb_stream_list *out);
/* TPI hash adjusters (XPDB::getHashAdjusters); they need the types loaded
 * and load them on demand. */
XXFC_API size_t xx_ms_pdb_get_hash_adjuster_count(xx_ms_pdb *pdb);
XXFC_API bool xx_ms_pdb_get_hash_adjuster(xx_ms_pdb *pdb, const char *name, uint32_t *type_index);

/* ---- IPI (id) records by index ----------------------------------------------- */

/* A decoded record of the IPI stream (stream 4). Only the fields of the kind
 * are filled in, the rest are 0 / "":
 *   LF_FUNC_ID        scope_index (an LF_STRING_ID / namespace id, 0 = global),
 *                     type_index (the LF_PROCEDURE), name
 *   LF_MFUNC_ID       parent_type_index (the class), type_index (the
 *                     LF_MFUNCTION), name
 *   LF_STRING_ID      list_index (an LF_SUBSTR_LIST id or 0), name (the string)
 *   LF_UDT_SRC_LINE   type_index (the UDT), source_index (an LF_STRING_ID id
 *                     holding the file name), line
 *   LF_UDT_MOD_SRC_LINE type_index, source_index (an offset into /names; name is
 *                     that string when the /names stream has it), line, module
 *                     (1 based)
 *   LF_BUILDINFO, LF_SUBSTR_LIST  count (the ids: xx_ms_pdb_get_id_list)
 * Every other kind comes back with is_valid, kind and the record location. */
typedef struct xx_ms_pdb_id_record {
    bool is_valid;          /* the index names a record of the IPI stream */
    uint32_t id_index;
    uint16_t kind;
    const char *name;       /* never NULL */
    uint32_t scope_index;
    uint32_t parent_type_index;
    uint32_t type_index;
    uint32_t list_index;
    uint32_t source_index;
    uint32_t line;
    uint16_t module;
    uint32_t count;
    int64_t record_offset;  /* in the IPI stream (header included) */
    int32_t record_size;
} xx_ms_pdb_id_record;

/* The record of an id index (>= the IPI type_index_begin). The offsets of the
 * IPI records are collected on the first call (one walk over the stream, as
 * xx_ms_pdb_get_ids does; the records themselves are read on demand and not
 * kept). False (out->is_valid false) when the PDB has no IPI stream or the
 * index is not in it. JG files have no IPI stream. */
XXFC_API bool xx_ms_pdb_get_id_by_index(xx_ms_pdb *pdb, uint32_t id_index, xx_ms_pdb_id_record *out);
/* The ids of an LF_BUILDINFO or LF_SUBSTR_LIST record (count entries, cut at
 * the end of the record). True with an empty list for every other kind. */
XXFC_API bool xx_ms_pdb_get_id_list(xx_ms_pdb *pdb, uint32_t id_index, xx_ms_pdb_u32_list *out);

/* Notes on the functions above:
 * - a list getter returns false only for a bad argument or a failed
 *   allocation, and then leaves the list empty (items == NULL, count == 0,
 *   nothing to free); "nothing found" is true with count == 0 and items == NULL;
 * - the *_to_string helpers return a static string; a kind without a name is
 *   "0x%04x" as in XPDB. */

#ifdef __cplusplus
}
#endif

#endif
