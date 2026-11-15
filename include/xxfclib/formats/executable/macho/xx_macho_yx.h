/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* YARA-X-exact Mach-O helpers.
 *
 * The regular readers in xx_macho.h / xx_machofat.h follow the archive-member
 * semantics that DIE and the script API rely on. The functions in this header
 * instead reproduce, bit for bit, the parsing behavior of YARA-X 1.21.0's
 * macho module (and of the xxyara port of it): thin/fat magic handling with
 * the single-successfully-parsed-child flattening rule, LC_SEGMENT /
 * LC_SEGMENT_64 walks, LC_MAIN and LC_UNIXTHREAD entry-point resolution,
 * LC_UUID / LC_SOURCE_VERSION / LC_BUILD_VERSION formatting, dylib/rpath
 * ASCII-case-insensitive predicates, the symtab and export-trie views, the
 * code-signature SuperBlob with entitlements XML and X.509 certificate
 * extraction, and the symhash input collector.
 *
 * These functions never modify their input and only read inside the supplied
 * buffer. Byte strings returned by the accessors borrow the input and remain
 * valid for its lifetime. The extras result owns its strings and arrays and
 * must be released with xx_macho_yx_extras_free.
 */

#ifndef XXFCLIB_MACHO_YX_H
#define XXFCLIB_MACHO_YX_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Public numeric fields from YARA-X 1.21's macho.Macho schema. A recognized
 * field is undefined (returns false) when the input is not a Mach-O file or
 * when the corresponding optional value was not present. */
XXFC_API bool xx_macho_yx_field_supported(const char *member);
XXFC_API bool xx_macho_yx_field(const uint8_t *data, size_t size,
                                const char *member, uint64_t *value);

/* Byte range of the top-level Mach-O file. A fat archive exposes one child
 * at the top level when exactly one architecture parses successfully. The
 * returned range borrows the scanned buffer and is undefined otherwise. */
XXFC_API bool xx_macho_yx_top_file_span(const uint8_t *data, size_t size,
                                        const uint8_t **bytes,
                                        size_t *length);
/* Exposed file[index] child span, for recursively applying ordinary accessors.
 * Undefined for thin files or a fat binary flattened to a single child. */
XXFC_API bool xx_macho_yx_file_span(const uint8_t *data, size_t size,
                                    uint64_t index, const uint8_t **bytes,
                                    size_t *length);

/* Byte strings refer to the scanned input and remain valid for its lifetime. */
XXFC_API bool xx_macho_yx_string_field_supported(const char *member);
XXFC_API bool xx_macho_yx_string_field(const uint8_t *data, size_t size,
                                       const char *member,
                                       const uint8_t **bytes, size_t *length);

/* LC_SOURCE_VERSION is decoded to A.B.C.D.E in the caller's buffer. The
 * buffer must hold the terminating NUL; 32 bytes suffice for every value.
 * A missing command, hidden top-level file, or undersized buffer returns
 * false without modifying the buffer or length. */
XXFC_API bool xx_macho_yx_source_version(const uint8_t *data, size_t size,
                                         char *buffer, size_t capacity,
                                         size_t *length);
XXFC_API bool xx_macho_yx_file_source_version(const uint8_t *data, size_t size,
                                              uint64_t index, char *buffer,
                                              size_t capacity,
                                              size_t *length);

/* LC_UUID is formatted as uppercase hex with dashes. Its source bytes are
 * trimmed of trailing NULs by YARA-X; 37 bytes suffice for every value. */
XXFC_API bool xx_macho_yx_uuid(const uint8_t *data, size_t size, char *buffer,
                               size_t capacity, size_t *length);
XXFC_API bool xx_macho_yx_file_uuid(const uint8_t *data, size_t size,
                                    uint64_t index, char *buffer,
                                    size_t capacity, size_t *length);

/* Generated text fields, including build/minimum OS versions and dylib
 * versions. The caller owns the NUL-terminated output; 37 bytes suffice for
 * all fields. On an undefined field or undersized buffer, outputs are
 * unchanged. Top-level names use a dotted path such as
 * "build_version.minos". Indexed names use collection "file" with a
 * top-level name, or "dylibs" with "current_version" or
 * "compatibility_version". */
XXFC_API bool xx_macho_yx_text_field_supported(const char *member);
XXFC_API bool xx_macho_yx_text_field(const uint8_t *data, size_t size,
                                     const char *member, char *buffer,
                                     size_t capacity, size_t *length);
XXFC_API bool xx_macho_yx_indexed_text_field_supported(const char *collection,
                                                       const char *member);
XXFC_API bool xx_macho_yx_indexed_text_field(const uint8_t *data, size_t size,
                                             const char *collection,
                                             uint64_t index,
                                             const char *member,
                                             char *buffer, size_t capacity,
                                             size_t *length);
/* Nested dylib version strings under an indexed fat Mach-O file. */
XXFC_API bool xx_macho_yx_file_dylib_version(const uint8_t *data, size_t size,
                                             uint64_t file_index,
                                             uint64_t dylib_index,
                                             const char *member,
                                             char *buffer, size_t capacity,
                                             size_t *length);
XXFC_API bool xx_macho_yx_file_dylib_name(const uint8_t *data, size_t size,
                                          uint64_t file_index,
                                          uint64_t dylib_index,
                                          const uint8_t **bytes,
                                          size_t *length);

/* Build-tool fields: build_version.tools.len(), tools[i].tool, and
 * tools[i].version, including those under file[i] in fat binaries.
 * The indexed generic APIs use collection "build_version" and member
 * "tools.tool" or "tools.version". The nested file APIs take both indices.
 * Version output is caller-owned and NUL terminated; 16 bytes suffice. */
XXFC_API bool xx_macho_yx_file_build_tool_field(const uint8_t *data,
                                                size_t size,
                                                uint64_t file_index,
                                                uint64_t tool_index,
                                                const char *member,
                                                uint64_t *value);
XXFC_API bool xx_macho_yx_file_build_tool_version(const uint8_t *data,
                                                  size_t size,
                                                  uint64_t file_index,
                                                  uint64_t tool_index,
                                                  char *buffer,
                                                  size_t capacity,
                                                  size_t *length);

/* Inline enum values from the public macho.proto. These are defined as soon
 * as the module is imported, independently of the scanned input. */
XXFC_API bool xx_macho_yx_constant(const char *member, uint64_t *value);

/* Top-level repeated fields have a defined length, including zero for
 * non-Mach-O data. The nested build_version.tools.len() and symtab.nlists.len()
 * evaluate to one when their containing object is absent in YARA-X 1.21;
 * symtab.entries.len() is zero in that case. Fat archives with exactly
 * one successfully parsed child expose that child's collections at the top
 * level. */
XXFC_API bool xx_macho_yx_collection_supported(const char *member);
XXFC_API bool xx_macho_yx_collection_count(const uint8_t *data, size_t size,
                                           const char *member,
                                           uint64_t *count);

/* Public fat_arch descriptors are present only for fat archives whose
 * successfully parsed child count differs from one. The index is zero based. */
XXFC_API bool xx_macho_yx_fat_arch_field_supported(const char *member);
XXFC_API bool xx_macho_yx_fat_arch_field(const uint8_t *data, size_t size,
                                         uint64_t index, const char *member,
                                         uint64_t *value);

/* One-index fields of segments, dylibs, build_version.tools, symtab, fat_arch, and
 * file. The latter is exposed only for fat archives with zero or multiple
 * parsed children. For symtab use numeric members "nlists.n_strx", "nlists.n_type",
 * "nlists.n_sect", "nlists.n_desc", "nlists.n_value" and string member
 * "entries.value"; both arrays enumerate retained nonempty symbol names. */
XXFC_API bool xx_macho_yx_indexed_field_supported(const char *collection,
                                                  const char *member);
XXFC_API bool xx_macho_yx_indexed_field(const uint8_t *data, size_t size,
                                        const char *collection, uint64_t index,
                                        const char *member, uint64_t *value);
XXFC_API bool xx_macho_yx_indexed_string_field_supported(const char *collection,
                                                         const char *member);
XXFC_API bool xx_macho_yx_indexed_string_field(const uint8_t *data,
                                               size_t size,
                                               const char *collection,
                                               uint64_t index,
                                               const char *member,
                                               const uint8_t **bytes,
                                               size_t *length);

/* Nested collection lengths and section fields under a top-level segment. */
XXFC_API bool xx_macho_yx_nested_collection_count(const uint8_t *data,
                                                  size_t size,
                                                  const char *collection,
                                                  uint64_t index,
                                                  const char *member,
                                                  uint64_t *count);
XXFC_API bool xx_macho_yx_section_field_supported(const char *member);
XXFC_API bool xx_macho_yx_section_field(const uint8_t *data, size_t size,
                                        uint64_t segment_index,
                                        uint64_t section_index,
                                        const char *member, uint64_t *value);
XXFC_API bool xx_macho_yx_section_string_field_supported(const char *member);
XXFC_API bool xx_macho_yx_section_string_field(const uint8_t *data,
                                               size_t size,
                                               uint64_t segment_index,
                                               uint64_t section_index,
                                               const char *member,
                                               const uint8_t **bytes,
                                               size_t *length);

/* Return the fat_arch index for a CPU type, optionally constrained by CPU
 * subtype. Undefined if the input has no exposed fat_arch array or no match. */
XXFC_API bool xx_macho_yx_file_index_for_arch(const uint8_t *data, size_t size,
                                              uint64_t cputype,
                                              bool has_subtype,
                                              uint64_t cpusubtype,
                                              uint64_t *index);

/* These predicates are defined on all inputs and compare ASCII without
 * case, matching YARA-X's BStr comparison. */
XXFC_API bool xx_macho_yx_has_dylib(const uint8_t *data, size_t size,
                                    const uint8_t *name, size_t name_length,
                                    bool *found);
XXFC_API bool xx_macho_yx_has_rpath(const uint8_t *data, size_t size,
                                    const uint8_t *name, size_t name_length,
                                    bool *found);

/* Fat-archive entry offset plus a matching child's entry point. */
XXFC_API bool xx_macho_yx_entry_point_for_arch(const uint8_t *data,
                                               size_t size, uint64_t cputype,
                                               bool has_subtype,
                                               uint64_t cpusubtype,
                                               uint64_t *offset);

/* Parsed code-signature, import, and export strings. All strings and arrays are
 * owned by the result, which must be released with xx_macho_yx_extras_free.
 * A valid Mach-O with no such data returns an empty result. For a fat file
 * with multiple children, the top-level result is empty and the indexed
 * function accesses file[index]. An invalid input returns false and leaves
 * the result empty and free-safe. */
typedef struct {
    char *issuer;
    size_t issuer_length;
    char *subject;
    size_t subject_length;
    bool is_self_signed;
} xx_macho_yx_certificate;

typedef struct {
    char **entitlements;
    size_t entitlement_count;
    xx_macho_yx_certificate *certificates;
    size_t certificate_count;
    char **imports;
    size_t import_count;
    char **exports;
    size_t export_count;
} xx_macho_yx_extras;

XXFC_API bool xx_macho_yx_extras_parse(const uint8_t *data, size_t size,
                                       xx_macho_yx_extras *result);
XXFC_API bool xx_macho_yx_file_extras_parse(const uint8_t *data, size_t size,
                                            uint64_t index,
                                            xx_macho_yx_extras *result);
XXFC_API void xx_macho_yx_extras_free(xx_macho_yx_extras *result);

/* The sorted, deduplicated, display-decoded symbol bytes used by macho.symhash().
 * Returns false (undefined) if no symbol-table entry was parsed. A table
 * with entries but no eligible undefined external symbol yields an empty
 * allocated buffer. The caller frees *bytes. */
XXFC_API bool xx_macho_yx_symhash_input(const uint8_t *data, size_t size,
                                        uint8_t **bytes, size_t *length);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_MACHO_YX_H */
