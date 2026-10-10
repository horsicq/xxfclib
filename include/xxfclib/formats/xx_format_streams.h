/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_FORMAT_STREAMS_H
#define XX_FORMAT_STREAMS_H

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
 * the first record; current is borrowed until next/free. An absent table is a
 * successful empty cursor. current_index starts at -1 when empty; total_records
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
