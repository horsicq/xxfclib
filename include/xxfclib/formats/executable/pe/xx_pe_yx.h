/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* YARA-X-exact PE helpers.
 *
 * The regular readers in xx_pe.h / xx_pe_inspect.h follow the Windows loader
 * or flat-inspection semantics that DIE and the script API rely on. The
 * functions in this header instead reproduce, bit for bit, the parsing
 * behavior of YARA-X 1.21.0's PE module (and of the xxyara port of it):
 * RVA<->file-offset conversion, the pe.calculate_checksum quirk, the Rich
 * signature view (including the decrypted clear data), standard and delay
 * import walking with the published ordinal-name tables, and export
 * enumeration with forwarder detection.
 *
 * These functions never modify their input and only read inside the supplied
 * buffer. Name and DLL bytes returned by the visitors borrow the input and
 * remain valid for its lifetime. */

#ifndef XXFCLIB_PE_YX_H
#define XXFCLIB_PE_YX_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Strict UTF-8 validation over a byte range (no NUL requirement), shared by
 * the YARA-X-exact PE parsers and Authenticode name formatting. */
XXFC_API bool xx_pe_yx_utf8_valid(const uint8_t *text, size_t length);

/** Maps an RVA to a file offset with the YARA-X PE parser semantics: the
 * section with the highest start RVA containing the RVA wins, the raw offset
 * is aligned down by min(file_alignment, 0x200) (and to 0x200 when
 * section_alignment >= 0x1000), and RVAs below the lowest section RVA pass
 * through. Returns false when the RVA is not backed by raw data or the input
 * is not a PE. */
XXFC_API bool xx_pe_yx_rva_to_offset(const uint8_t *data, size_t size,
                                     uint64_t rva, uint64_t *value);

/** Inverse of xx_pe_yx_rva_to_offset with the YARA-X bounds checks. Sections
 * without file data do not participate; an offset below the lowest mapped
 * raw offset passes through. Returns false when the offset is not inside a
 * section's raw data. */
XXFC_API bool xx_pe_yx_offset_to_rva(const uint8_t *data, size_t size,
                                     uint64_t offset, uint64_t *value);

/** The pe.calculate_checksum value: the YARA-X 1.21.0 checksum iterator
 * consumes 32-bit words (a trailing 16-bit word when only two bytes remain)
 * with carry propagation; an odd trailing ASCII digit contributes its numeric
 * value while other trailing bytes are left unread. The stored CheckSum
 * header field is then subtracted (with the reference's borrow quirk), the
 * sum is folded to 16 bits and the file length added. Unlike xx_pe_checksum,
 * the CheckSum field itself is included in the iterated data. Returns false
 * when the CheckSum field is undefined for this input. */
XXFC_API bool xx_pe_yx_calculate_checksum(const uint8_t *data, size_t size,
                                          uint64_t *value);

/** Located Rich signature. `offset`/`length` describe the encrypted blob in
 * the file (starting at the xored "DanS" marker); the clear payload has the
 * same length once decrypted with `key`. */
typedef struct {
    bool present;
    size_t offset;
    size_t length;
    uint32_t key;
} xx_pe_yx_rich_info;

/** Locates the Rich signature. Returns false when the input is not a PE or
 * no well-formed DanS..Rich pair exists; `info` is unchanged then. */
XXFC_API bool xx_pe_yx_rich_read(const uint8_t *data, size_t size,
                                 xx_pe_yx_rich_info *info);

/** Number of tool records, matching the reference module: 1 when the Rich
 * signature is absent, otherwise (length - 16) / 8, or 0 for a truncated
 * signature shorter than 16 bytes. Always returns true. */
XXFC_API bool xx_pe_yx_rich_tool_count(const uint8_t *data, size_t size,
                                       uint64_t *count);

/** One tool record field; `member` is "toolid", "version" or "times".
 * Returns false when the index is out of range or the member is unknown. */
XXFC_API bool xx_pe_yx_rich_tool_field(const uint8_t *data, size_t size,
                                       uint64_t index, const char *member,
                                       uint64_t *value);

/** Allocates and decrypts the DanS..Rich payload (allocated with malloc; the
 * caller frees with free). Returns false when no Rich signature exists. */
XXFC_API bool xx_pe_yx_rich_clear_data(const uint8_t *data, size_t size,
                                       uint8_t **bytes, size_t *length);

/** Import walk totals; undefined (false) on non-PE data. */
typedef struct {
    uint64_t standard_libraries;
    uint64_t standard_functions;
    uint64_t delayed_libraries;
    uint64_t delayed_functions;
} xx_pe_yx_import_stats;

/** Called for every successfully parsed import. `function` names an ordinal
 * fallback ("ordN" or a published ordinal name) when `by_ordinal` is true.
 * `rva` is the IAT slot address. All name bytes borrow the input. */
typedef void (*xx_pe_yx_import_visitor)(void *opaque,
                                        const uint8_t *dll,
                                        size_t dll_length,
                                        const uint8_t *function,
                                        size_t function_length,
                                        bool by_ordinal, uint16_t ordinal,
                                        uint32_t rva);

/** Like xx_pe_yx_import_visitor, but also receives the descriptor index,
 * preserving separate library records for adjacent identical DLL names. */
typedef void (*xx_pe_yx_import_descriptor_visitor)(
    void *opaque, size_t descriptor_index, const uint8_t *dll,
    size_t dll_length, const uint8_t *function, size_t function_length,
    bool by_ordinal, uint16_t ordinal, uint32_t rva);

/** Fills `out` by walking the standard (directory 1) and delay-load
 * (directory 13) import descriptors with the reference budgets. Returns
 * false on non-PE data. */
XXFC_API bool xx_pe_yx_get_import_stats(const uint8_t *data, size_t size,
                                        xx_pe_yx_import_stats *out);

/** Walks parsed imports in file order; `flags` bit 0 selects standard
 * imports, bit 1 delay imports. A NULL visitor walks nothing. */
XXFC_API void xx_pe_yx_visit_imports(const uint8_t *data, size_t size,
                                     unsigned flags,
                                     xx_pe_yx_import_visitor visitor,
                                     void *opaque);

/** Descriptor-indexed variant of xx_pe_yx_visit_imports. */
XXFC_API void xx_pe_yx_visit_import_descriptors(
    const uint8_t *data, size_t size, unsigned flags,
    xx_pe_yx_import_descriptor_visitor visitor, void *opaque);

/** Resolves an import ordinal for oleaut32.dll, wsock32.dll and ws2_32.dll
 * using YARA-X's published tables; writes "ordN" into `fallback` otherwise
 * and returns it. `fallback` must hold 16 bytes. */
XXFC_API const char *xx_pe_yx_lookup_ordinal_name(const uint8_t *dll,
                                                  size_t dll_length,
                                                  uint16_t ordinal,
                                                  char fallback[16]);

/** One export address table slot. `name` is NULL for unnamed slots; a slot
 * inside the export directory range exposes `forward_name` instead of an
 * offset. All bytes borrow the input. */
typedef struct {
    const uint8_t *name;
    size_t name_length;
    uint32_t ordinal;
    uint32_t rva;
    bool has_offset;
    uint32_t offset;
    const uint8_t *forward_name;
    size_t forward_name_length;
} xx_pe_yx_export;

typedef void (*xx_pe_yx_export_visitor)(void *opaque,
                                        const xx_pe_yx_export *entry);

/** The reference module's scalar export fields. Returns false only when the
 * input is not a PE. `*present` reports whether the export directory was
 * located; when absent, `*function_count` is 0 and `*timestamp` is
 * meaningless. */
XXFC_API bool xx_pe_yx_export_info(const uint8_t *data, size_t size,
                                   bool *present, uint64_t *function_count,
                                   uint64_t *timestamp);

/** The export directory's DLL name: a validated UTF-8 string borrowing the
 * input. Returns false when the directory or its name cannot be read. */
XXFC_API bool xx_pe_yx_export_dll_name(const uint8_t *data, size_t size,
                                       const uint8_t **bytes, size_t *length);

/** Visits every export address table slot in order, applying the reference
 * name aliasing (first ordinal-table occurrence wins) and forwarder
 * detection. `poll`/`poll_opaque` optionally abort the walk: returning true
 * from `poll` stops visiting and makes this function return false. Without a
 * poll callback the walk always returns true. */
XXFC_API bool xx_pe_yx_visit_exports(const uint8_t *data, size_t size,
                                     xx_pe_yx_export_visitor visitor,
                                     void *opaque,
                                     bool (*poll)(void *poll_opaque),
                                     void *poll_opaque);

/** Finds an export by name (case-insensitive ASCII) or slot ordinal.
 * `*found`/`*index` are always written; the index is the EAT slot. */
XXFC_API bool xx_pe_yx_exports(const uint8_t *data, size_t size,
                               const char *name, size_t name_length,
                               bool by_ordinal, uint64_t ordinal,
                               bool *found, uint64_t *index);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_PE_YX_H */
