/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/**
 * @file xx_data_struct_compare.h
 * @brief Compare the DataStructs of two files of the same file format.
 *
 * Both formats are enumerated with the normal DataStructs stream. Structures
 * are paired by canonical name and occurrence (the third IMAGE_SECTION_HEADER
 * of one file with the third of the other); decoded fields of a paired structure
 * are paired by field name and occurrence within the structure. A field is
 * equal when the bytes it covers are equal in both files (read from each
 * format's data-struct device), so long byte strings whose display text is
 * abbreviated still compare exactly; fields without a known location fall
 * back to their raw and display text. Structures without decoded fields (raw data,
 * no field decoder) compare the bytes of their whole region. A table of N equal
 * entries (count > 1, entry_size * count <= total_size) is split into N
 * single-entry structures first, so each entry is paired and decoded.
 *
 * Results arrive through a callback in a merged order: the first file's
 * order, with structures that exist only in the second file inserted where
 * they appear there. A structure row comes before the rows of its fields.
 */
#ifndef XXFCLIB_FORMATS_XX_DATA_STRUCT_COMPARE_H
#define XXFCLIB_FORMATS_XX_DATA_STRUCT_COMPARE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_data_struct_diff_kind {
    XX_DATA_STRUCT_DIFF_SAME = 0,   /**< Present in both and equal */
    XX_DATA_STRUCT_DIFF_CHANGED,    /**< Present in both, differs (see what) */
    XX_DATA_STRUCT_DIFF_ONLY_FIRST, /**< Present only in the first file */
    XX_DATA_STRUCT_DIFF_ONLY_SECOND /**< Present only in the second file */
} xx_data_struct_diff_kind;

/* What differs in a CHANGED row (bit set). */
#define XX_DATA_STRUCT_DIFF_VALUE 0x01u   /**< Bytes (field or whole non-STRUCT region) */
#define XX_DATA_STRUCT_DIFF_OFFSET 0x02u  /**< File offset (structure) or offset within the structure (field) */
#define XX_DATA_STRUCT_DIFF_ADDRESS 0x04u /**< Mapped address (structure) */
#define XX_DATA_STRUCT_DIFF_SIZE 0x08u    /**< Entry/total size (structure) or field size */
#define XX_DATA_STRUCT_DIFF_COUNT 0x10u   /**< Element count (structure) */
#define XX_DATA_STRUCT_DIFF_FIELDS 0x20u  /**< Structure: at least one of its fields differs */

/* Options. */
#define XX_DATA_STRUCT_COMPARE_SAME 0x01u            /**< Also report equal rows */
#define XX_DATA_STRUCT_COMPARE_IGNORE_LOCATION 0x02u /**< Offsets and addresses do not count as differences */
#define XX_DATA_STRUCT_COMPARE_NO_FIELDS 0x04u       /**< Compare structures only (no field rows, no FIELDS bit) */

typedef struct xx_data_struct_diff {
    xx_data_struct_diff_kind kind;
    uint32_t what;                /**< XX_DATA_STRUCT_DIFF_* bits (CHANGED rows) */
    const char *struct_name;      /**< Canonical structure name, e.g. "IMAGE_SECTION_HEADER" */
    uint64_t occurrence;          /**< 0-based index among structures of this name */
    const xx_data_struct *first;  /**< Structure in the first file, NULL when absent */
    const xx_data_struct *second; /**< Structure in the second file, NULL when absent */
    /* Field rows only (both NULL for a structure row). Strings are copies owned
     * by the compare and valid during the callback only. */
    const xx_data_struct_record *first_field;
    const xx_data_struct_record *second_field;
    uint64_t field_occurrence; /**< 0-based index among fields of this name in the structure */
} xx_data_struct_diff;

/** Return false to stop the comparison (xx_format_compare_data_structs then returns false). */
typedef bool (*xx_data_struct_diff_callback)(void *user, const xx_data_struct_diff *diff);

typedef struct xx_data_struct_compare_stats {
    uint64_t structs[4]; /**< Indexed by xx_data_struct_diff_kind */
    uint64_t fields[4];  /**< Indexed by xx_data_struct_diff_kind */
} xx_data_struct_compare_stats;

/**
 * @brief Compare the DataStructs of two formats (normally of the same file type).
 * @param callback May be NULL (statistics only).
 * @param stats    May be NULL; zeroed first, then counts every row (also SAME
 *                 rows when XX_DATA_STRUCT_COMPARE_SAME is not set).
 * @return false on enumeration/decoding/allocation failure, cancellation through
 *         pd, or a callback returning false.
 */
XXFC_API bool xx_format_compare_data_structs(Abstractformat *first, Abstractformat *second, uint32_t options, xx_data_struct_diff_callback callback, void *user,
                                             xx_data_struct_compare_stats *stats, xx_pd_struct *pd);

/** Short lower-case name of a diff kind: "same", "changed", "only-first", "only-second". */
XXFC_API const char *xx_data_struct_diff_kind_to_string(xx_data_struct_diff_kind kind);

#ifdef __cplusplus
}
#endif

#endif
