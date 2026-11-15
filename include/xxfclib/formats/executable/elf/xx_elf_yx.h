/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* YARA-X-exact ELF helpers.
 *
 * The regular readers in xx_elf.h / xx_elf_inspect.h follow the flat
 * inspection semantics that DIE and the script API rely on (for example,
 * xx_elf_get_entry_point returns the raw virtual e_entry value). The
 * functions in this header instead reproduce, bit for bit, the parsing
 * behavior of YARA-X 1.21.0's ELF module (and of the xxyara port of it):
 * entry-point resolution to a file offset through PT_LOAD segments (or
 * non-NOBITS sections for ET_REL), the sh_count >= 0xff00 truncation rule,
 * PT_DYNAMIC walking up to the first DT_NULL, the symtab/dynsym views with
 * lossy UTF-8 names, the inline enum constants from elf.proto, and the
 * import_md5 / telfhash digest helpers.
 *
 * These functions never modify their input and only read inside the supplied
 * buffer. String bytes returned by xx_elf_yx_indexed_string_field borrow the
 * input and remain valid for its lifetime.
 */

#ifndef XXFCLIB_ELF_YX_H
#define XXFCLIB_ELF_YX_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True when `member` is a scalar ELF module field exposed by YARA-X
 * (type, machine, osabi, entry_point, ...). */
XXFC_API bool xx_elf_yx_field_supported(const char *member);

/** Reads a scalar ELF module field with the YARA-X semantics. `entry_point`
 * is resolved to a file offset and is undefined when e_entry is zero,
 * sh_count >= 0xff00, or no segment/section contains it; `symtab_entries` /
 * `dynsym_entries` are undefined when the section table is truncated.
 * Returns false when the field is undefined for this input. */
XXFC_API bool xx_elf_yx_field(const uint8_t *data, size_t size,
                              const char *member, uint64_t *value);

/** Number of entries in a repeated ELF module view. `member` is one of
 * "sections", "segments", "symtab", "dynsym" or "dynamic". Sections are
 * counted as zero when sh_count >= 0xff00. Always returns true for a known
 * member (zero count when the input is not an ELF). */
XXFC_API bool xx_elf_yx_collection_count(const uint8_t *data, size_t size,
                                         const char *member, uint64_t *count);

/** True when (collection, member) is a supported indexed field pair. */
XXFC_API bool xx_elf_yx_indexed_field_supported(const char *collection,
                                                const char *member);

/** Reads one field of a section, segment, symtab/dynsym symbol or dynamic
 * entry. Enum-typed values are narrowed to signed i32 exactly as the
 * reference protobuf stores them. Returns false when the entry or field is
 * undefined for this input. */
XXFC_API bool xx_elf_yx_indexed_field(const uint8_t *data, size_t size,
                                      const char *collection, uint64_t index,
                                      const char *member, uint64_t *value);

/** True when (collection, member) is a supported indexed string field pair
 * ("name" of sections, symtab and dynsym entries). */
XXFC_API bool xx_elf_yx_indexed_string_field_supported(const char *collection,
                                                       const char *member);

/** Reads one NUL-terminated name of a section or symtab/dynsym symbol. The
 * returned bytes borrow the input and `length` excludes the terminator.
 * Returns false when the entry or its name is undefined. */
XXFC_API bool xx_elf_yx_indexed_string_field(const uint8_t *data, size_t size,
                                             const char *collection,
                                             uint64_t index,
                                             const char *member,
                                             const uint8_t **bytes,
                                             size_t *length);

/** Resolves an inline enum constant from elf.proto (ET_*, EM_*, OSABI_*,
 * SHT_*, PT_*, PF_*, STT_*, STB_*, STV_*, DT_*). Returns false when the name
 * is not one of the published constants. */
XXFC_API bool xx_elf_yx_constant(const char *member, uint64_t *value);

/** The elf.import_md5 value: undefined symbols (shndx == 0) of SHT_DYNSYM
 * (or SHT_SYMTAB when there is no dynsym), lossy-decoded, Unicode-lowercased,
 * strcmp-sorted and comma-joined, then MD5 as lowercase hex. Returns false
 * when the input is not an ELF with a symbol table; `out` is empty then. */
XXFC_API bool xx_elf_yx_import_md5(const uint8_t *data, size_t size,
                                   char out[33]);

/** The elf.telfhash value: global default-visibility STT_FUNC symbols of
 * SHT_DYNSYM (or SHT_SYMTAB), filtered by the telfhash exclusion rules,
 * lossy-decoded, Unicode-lowercased, strcmp-sorted and comma-joined, then
 * the 128-bit TLSH digest as 72 hex chars (out has room for the NUL).
 * Returns false when the input is not an ELF with a symbol table. */
XXFC_API bool xx_elf_yx_telfhash(const uint8_t *data, size_t size,
                                 char out[73]);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ELF_YX_H */
