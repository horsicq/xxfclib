/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_FORMAT_STREAMS_H
#define XX_FORMAT_STREAMS_H

#include "xxfclib/formats/xx_format_stream_features.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/var/xx_var.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Abstractformat Abstractformat;

typedef enum xx_symbol_kind {
    XX_SYMBOL_KIND_UNKNOWN = 0,
    XX_SYMBOL_KIND_FUNCTION,
    XX_SYMBOL_KIND_DATA,
    XX_SYMBOL_KIND_SECTION,
    XX_SYMBOL_KIND_FILE,
    XX_SYMBOL_KIND_LABEL
} xx_symbol_kind_t;

typedef enum xx_symbol_binding {
    XX_SYMBOL_BINDING_UNKNOWN = 0,
    XX_SYMBOL_BINDING_LOCAL,
    XX_SYMBOL_BINDING_GLOBAL,
    XX_SYMBOL_BINDING_WEAK
} xx_symbol_binding_t;

/** One native symbol-table entry. PE skips auxiliary entries and preserves
 * their count; .file entries expose their auxiliary filename in file_name.
 * offset/address identify the referenced target; record_offset identifies the
 * symbol-table entry. size is zero when unknown. value, native_type,
 * native_storage_class and section_number preserve the native encoding.
 * PE section numbers are one based, 0 undefined, -1 absolute and -2 debug. */
typedef struct xx_symbol_record {
    char *name;
    char *file_name;
    xx_symbol_kind_t kind;
    xx_symbol_binding_t binding;
    uint64_t value;
    uint64_t address;
    uint64_t size;
    uint64_t table_index;
    int64_t offset;
    int64_t record_offset;
    int32_t section_number;
    uint32_t native_type;
    uint32_t native_storage_class;
    uint32_t auxiliary_count;
    bool is_undefined;
    bool is_absolute;
    bool is_debug;
} xx_symbol_record;

/** One imported symbol. name is NULL/empty for ordinal imports. offset and
 * address identify the IAT slot, not the import-by-name structure. Ordinal is
 * zero for named imports. is_delay distinguishes delay-load imports. */
typedef struct xx_import_record {
    char *library_name;
    char *name;
    uint64_t ordinal;
    uint32_t hint;
    bool by_ordinal;
    bool is_delay;
    int64_t offset;
    uint64_t address;
} xx_import_record;

/** One exported function slot. name is NULL/empty for ordinal-only exports;
 * forwarder holds a DLL.symbol/DLL.ordinal string for forwarded exports.
 * PE returns the first name when several aliases reference the same slot. */
typedef struct xx_export_record {
    char *name;
    char *forwarder;
    uint64_t ordinal;
    int64_t offset;
    uint64_t address;
} xx_export_record;

/** One resource leaf. Named IDs use UINT32_MAX and the corresponding UTF-8
 * name. Numeric IDs preserve their value; their name may be NULL. */
typedef struct xx_resource_record {
    uint32_t type_id;
    uint32_t name_id;
    uint32_t language_id;
    char *type_name;
    char *name;
    char *language_name;
    int64_t offset;
    int64_t size;
    uint32_t code_page;
} xx_resource_record;

/** One typed metadata value. key/value are owned by the cursor. */
typedef struct xx_metadata_record {
    char *key;
    xx_var value;
    int64_t offset;
    int64_t size;
} xx_metadata_record;

/* Cursors borrow the format/device, which must outlive them. create preloads
 * the first record; current is borrowed until next/free. A missing native table
 * in a supported reader yields a successful empty cursor. current_index starts
 * at -1 when empty; total_records
 * is -1 until known. Exhaustion returns false with failed=false; malformed
 * input/cancellation returns false with failed=true and an optional pd error.
 * Independent cursors preserve the device position, but concurrent access from
 * multiple threads still requires caller synchronization. Do not change the
 * format's device/base/mapping while any cursor is alive.
 * All offsets are absolute in the underlying device (-1 when unavailable),
 * addresses are virtual addresses (UINT64_MAX when unavailable).
 */
#define XX_FORMAT_DECLARE_STREAM_STATE(kind)                                                \
    typedef struct xx_##kind##_state {                                                      \
        Abstractformat *format;                                                             \
        xx_##kind##_record current_record;                                                  \
        bool has_record;                                                                    \
        bool failed;                                                                        \
        int64_t current_index;                                                              \
        int64_t total_records;                                                              \
        void *internal_state;                                                               \
        void (*free_internal)(void *ptr);                                                   \
    } xx_##kind##_state;                                                                    \
    XXFC_API void xx_##kind##_record_init(xx_##kind##_record *record);                      \
    XXFC_API void xx_##kind##_record_cleanup(xx_##kind##_record *record);                   \
    XXFC_API void xx_##kind##_state_init(xx_##kind##_state *state, Abstractformat *format); \
    XXFC_API void xx_##kind##_state_cleanup(xx_##kind##_state *state);                      \
    XXFC_API void xx_##kind##_state_free(xx_##kind##_state *state)

XX_FORMAT_DECLARE_STREAM_STATE(import);
XX_FORMAT_DECLARE_STREAM_STATE(export);
XX_FORMAT_DECLARE_STREAM_STATE(resource);
XX_FORMAT_DECLARE_STREAM_STATE(metadata);
XX_FORMAT_DECLARE_STREAM_STATE(symbol);
#undef XX_FORMAT_DECLARE_STREAM_STATE

/** Stateless capability interfaces. The returned descriptors are borrowed and
 * shared; every operation receives its borrowed format context. Cursors own
 * their iteration state and the format/device must outlive each cursor.
 * Callbacks can be NULL; count-only providers do not expose record iteration.
 * Use the checked operation helpers, or check the specific callback before use.
 * Abstractformat contains no callbacks or pointers for these capabilities. */
typedef struct xx_data_struct xx_data_struct;
typedef struct xx_data_struct_record xx_data_struct_record;
typedef struct xx_data_struct_state xx_data_struct_state;
typedef struct xx_data_struct_record_state xx_data_struct_record_state;

typedef struct StreamDataStructs {
    const char *(*data_struct_id_to_string)(Abstractformat *self, uint32_t id);
    uint32_t (*data_struct_string_to_id)(Abstractformat *self, const char *name);
    xx_data_struct_state *(*create_data_structs_reading)(Abstractformat *self, xx_pd_struct *pd);
    const xx_data_struct *(*get_current_data_struct)(Abstractformat *self, xx_data_struct_state *state);
    bool (*data_struct_move_to_next)(Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
    void (*free_data_structs_reading)(Abstractformat *self, xx_data_struct_state *state);
    xx_data_struct_record_state *(*create_data_struct_records_reading)(Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd);
    const xx_data_struct_record *(*get_current_data_struct_record)(Abstractformat *self, xx_data_struct_record_state *state);
    bool (*data_struct_record_move_to_next)(Abstractformat *self, xx_data_struct_record_state *state, xx_pd_struct *pd);
    void (*free_data_struct_records_reading)(Abstractformat *self, xx_data_struct_record_state *state);
    /* Optional: the device the structure offsets refer to when it is not the
     * format's own (a compressed TAR reports structures of its decoded TAR). */
    xx_io_device *(*data_struct_device)(Abstractformat *self, xx_pd_struct *pd);
    /* Optional: what a structure is, NULL when it has no description. */
    const char *(*data_struct_description)(Abstractformat *self, uint32_t id);
} StreamDataStructs;

typedef struct StreamImport {
    uint64_t (*get_number_of_imports)(Abstractformat *self, xx_pd_struct *pd);
    xx_import_state *(*create_imports_reading)(Abstractformat *self, xx_pd_struct *pd);
    const xx_import_record *(*get_current_import)(Abstractformat *self, xx_import_state *state);
    bool (*import_move_to_next)(Abstractformat *self, xx_import_state *state, xx_pd_struct *pd);
    void (*free_imports_reading)(Abstractformat *self, xx_import_state *state);
} StreamImport;

typedef struct StreamExport {
    uint64_t (*get_number_of_exports)(Abstractformat *self, xx_pd_struct *pd);
    xx_export_state *(*create_exports_reading)(Abstractformat *self, xx_pd_struct *pd);
    const xx_export_record *(*get_current_export)(Abstractformat *self, xx_export_state *state);
    bool (*export_move_to_next)(Abstractformat *self, xx_export_state *state, xx_pd_struct *pd);
    void (*free_exports_reading)(Abstractformat *self, xx_export_state *state);
} StreamExport;

typedef struct StreamResources {
    uint64_t (*get_number_of_resources)(Abstractformat *self, xx_pd_struct *pd);
    xx_resource_state *(*create_resources_reading)(Abstractformat *self, xx_pd_struct *pd);
    const xx_resource_record *(*get_current_resource)(Abstractformat *self, xx_resource_state *state);
    bool (*resource_move_to_next)(Abstractformat *self, xx_resource_state *state, xx_pd_struct *pd);
    void (*free_resources_reading)(Abstractformat *self, xx_resource_state *state);
} StreamResources;

typedef struct StreamMetadata {
    uint64_t (*get_number_of_metadata)(Abstractformat *self, xx_pd_struct *pd);
    xx_metadata_state *(*create_metadata_reading)(Abstractformat *self, xx_pd_struct *pd);
    const xx_metadata_record *(*get_current_metadata)(Abstractformat *self, xx_metadata_state *state);
    bool (*metadata_move_to_next)(Abstractformat *self, xx_metadata_state *state, xx_pd_struct *pd);
    void (*free_metadata_reading)(Abstractformat *self, xx_metadata_state *state);
} StreamMetadata;

typedef struct StreamSymbols {
    uint64_t (*get_number_of_symbols)(Abstractformat *self, xx_pd_struct *pd);
    xx_symbol_state *(*create_symbols_reading)(Abstractformat *self, xx_pd_struct *pd);
    const xx_symbol_record *(*get_current_symbol)(Abstractformat *self, xx_symbol_state *state);
    bool (*symbol_move_to_next)(Abstractformat *self, xx_symbol_state *state, xx_pd_struct *pd);
    void (*free_symbols_reading)(Abstractformat *self, xx_symbol_state *state);
} StreamSymbols;

/** Compatibility spelling requested by older callers. */
typedef StreamSymbols StreamSymols;

XXFC_API const StreamDataStructs *xx_format_get_stream_datastructs(Abstractformat *format);
XXFC_API const StreamImport *xx_format_get_stream_import(Abstractformat *format);
XXFC_API const StreamExport *xx_format_get_stream_export(Abstractformat *format);
XXFC_API const StreamResources *xx_format_get_stream_resources(Abstractformat *format);
XXFC_API const StreamMetadata *xx_format_get_stream_metadata(Abstractformat *format);
XXFC_API const StreamSymbols *xx_format_get_stream_symbols(Abstractformat *format);

XXFC_API xx_import_state *xx_format_create_imports_reading(Abstractformat *format, xx_pd_struct *pd);
XXFC_API const xx_import_record *xx_format_get_current_import(Abstractformat *format, xx_import_state *state);
XXFC_API bool xx_format_import_move_to_next(Abstractformat *format, xx_import_state *state, xx_pd_struct *pd);
XXFC_API void xx_format_free_imports_reading(Abstractformat *format, xx_import_state *state);

XXFC_API xx_export_state *xx_format_create_exports_reading(Abstractformat *format, xx_pd_struct *pd);
XXFC_API const xx_export_record *xx_format_get_current_export(Abstractformat *format, xx_export_state *state);
XXFC_API bool xx_format_export_move_to_next(Abstractformat *format, xx_export_state *state, xx_pd_struct *pd);
XXFC_API void xx_format_free_exports_reading(Abstractformat *format, xx_export_state *state);

XXFC_API xx_resource_state *xx_format_create_resources_reading(Abstractformat *format, xx_pd_struct *pd);
XXFC_API const xx_resource_record *xx_format_get_current_resource(Abstractformat *format, xx_resource_state *state);
XXFC_API bool xx_format_resource_move_to_next(Abstractformat *format, xx_resource_state *state, xx_pd_struct *pd);
XXFC_API void xx_format_free_resources_reading(Abstractformat *format, xx_resource_state *state);

XXFC_API xx_metadata_state *xx_format_create_metadata_reading(Abstractformat *format, xx_pd_struct *pd);
XXFC_API const xx_metadata_record *xx_format_get_current_metadata(Abstractformat *format, xx_metadata_state *state);
XXFC_API bool xx_format_metadata_move_to_next(Abstractformat *format, xx_metadata_state *state, xx_pd_struct *pd);
XXFC_API void xx_format_free_metadata_reading(Abstractformat *format, xx_metadata_state *state);

XXFC_API xx_symbol_state *xx_format_create_symbols_reading(Abstractformat *format, xx_pd_struct *pd);
XXFC_API const xx_symbol_record *xx_format_get_current_symbol(Abstractformat *format, xx_symbol_state *state);
XXFC_API bool xx_format_symbol_move_to_next(Abstractformat *format, xx_symbol_state *state, xx_pd_struct *pd);
XXFC_API void xx_format_free_symbols_reading(Abstractformat *format, xx_symbol_state *state);

#ifdef __cplusplus
}
#endif
#endif
