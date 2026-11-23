/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_ELF_DWARF_H
#define XXFCLIB_ELF_DWARF_H

#include "xxfclib/formats/executable/elf/xx_elf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Common tags and attributes used by callers.  Unknown vendor extensions are
 * retained numerically by the reader, so this list does not limit parsing. */
#define XX_DW_TAG_ARRAY_TYPE UINT64_C(0x01)
#define XX_DW_TAG_CLASS_TYPE UINT64_C(0x02)
#define XX_DW_TAG_FORMAL_PARAMETER UINT64_C(0x05)
#define XX_DW_TAG_MEMBER UINT64_C(0x0d)
#define XX_DW_TAG_POINTER_TYPE UINT64_C(0x0f)
#define XX_DW_TAG_COMPILE_UNIT UINT64_C(0x11)
#define XX_DW_TAG_STRUCTURE_TYPE UINT64_C(0x13)
#define XX_DW_TAG_TYPEDEF UINT64_C(0x16)
#define XX_DW_TAG_UNION_TYPE UINT64_C(0x17)
#define XX_DW_TAG_SUBRANGE_TYPE UINT64_C(0x21)
#define XX_DW_TAG_BASE_TYPE UINT64_C(0x24)
#define XX_DW_TAG_CONST_TYPE UINT64_C(0x26)
#define XX_DW_TAG_SUBPROGRAM UINT64_C(0x2e)
#define XX_DW_TAG_VARIABLE UINT64_C(0x34)

#define XX_DW_AT_LOCATION UINT64_C(0x02)
#define XX_DW_AT_NAME UINT64_C(0x03)
#define XX_DW_AT_BYTE_SIZE UINT64_C(0x0b)
#define XX_DW_AT_STMT_LIST UINT64_C(0x10)
#define XX_DW_AT_LOW_PC UINT64_C(0x11)
#define XX_DW_AT_HIGH_PC UINT64_C(0x12)
#define XX_DW_AT_LANGUAGE UINT64_C(0x13)
#define XX_DW_AT_COMP_DIR UINT64_C(0x1b)
#define XX_DW_AT_PRODUCER UINT64_C(0x25)
#define XX_DW_AT_ABSTRACT_ORIGIN UINT64_C(0x31)
#define XX_DW_AT_DECLARATION UINT64_C(0x3c)
#define XX_DW_AT_EXTERNAL UINT64_C(0x3f)
#define XX_DW_AT_FRAME_BASE UINT64_C(0x40)
#define XX_DW_AT_SPECIFICATION UINT64_C(0x47)
#define XX_DW_AT_TYPE UINT64_C(0x49)
#define XX_DW_AT_LINKAGE_NAME UINT64_C(0x6e)
#define XX_DW_AT_STR_OFFSETS_BASE UINT64_C(0x72)
#define XX_DW_AT_ADDR_BASE UINT64_C(0x73)
#define XX_DW_AT_RNGLISTS_BASE UINT64_C(0x74)
#define XX_DW_AT_LOCLISTS_BASE UINT64_C(0x8c)
#define XX_DW_AT_MIPS_LINKAGE_NAME UINT64_C(0x2007)

typedef enum xx_elf_dwarf_value_kind_e {
    XX_ELF_DWARF_VALUE_NONE = 0,
    XX_ELF_DWARF_VALUE_UNSIGNED,
    XX_ELF_DWARF_VALUE_SIGNED,
    XX_ELF_DWARF_VALUE_STRING,
    XX_ELF_DWARF_VALUE_REFERENCE,
    XX_ELF_DWARF_VALUE_BLOCK,
    XX_ELF_DWARF_VALUE_ADDRESS_INDEX,
    XX_ELF_DWARF_VALUE_STRING_INDEX,
    XX_ELF_DWARF_VALUE_LOCLIST_INDEX,
    XX_ELF_DWARF_VALUE_RNGLIST_INDEX
} xx_elf_dwarf_value_kind_t;

typedef struct xx_elf_dwarf_attribute {
    uint64_t name;
    uint64_t form;
    xx_elf_dwarf_value_kind_t kind;
    uint64_t unsigned_value;
    int64_t signed_value;
    uint64_t data_offset;
    uint64_t data_size;
    const char *string_value;
} xx_elf_dwarf_attribute;

typedef struct xx_elf_dwarf_die {
    uint64_t offset;
    uint64_t abbreviation_code;
    uint64_t tag;
    uint32_t unit_index;
    uint32_t depth;
    uint32_t attribute_index;
    uint32_t attribute_count;
    bool has_children;
} xx_elf_dwarf_die;

typedef struct xx_elf_dwarf_unit {
    uint64_t offset;
    uint64_t length;
    uint64_t abbreviation_offset;
    uint64_t type_signature;
    uint64_t type_offset;
    uint64_t dwo_id;
    uint64_t statement_list;
    uint64_t string_offsets_base;
    uint64_t address_base;
    uint64_t range_lists_base;
    uint64_t location_lists_base;
    uint32_t die_index;
    uint32_t die_count;
    uint16_t version;
    uint8_t unit_type;
    uint8_t address_size;
    uint8_t offset_size;
    const char *name;
    const char *compilation_directory;
    const char *producer;
} xx_elf_dwarf_unit;

typedef struct xx_elf_dwarf_abbreviation {
    uint64_t table_offset;
    uint64_t offset;
    uint64_t code;
    uint64_t tag;
    uint32_t attribute_spec_index;
    uint32_t attribute_spec_count;
    bool has_children;
} xx_elf_dwarf_abbreviation;

typedef struct xx_elf_dwarf_attribute_spec {
    uint64_t name;
    uint64_t form;
    int64_t implicit_constant;
} xx_elf_dwarf_attribute_spec;

typedef struct xx_elf_dwarf_line_file {
    uint64_t line_program_offset;
    uint64_t directory_index;
    uint64_t timestamp;
    uint64_t size;
    uint8_t md5[16];
    bool has_md5;
    const char *name;
    const char *path;
} xx_elf_dwarf_line_file;

typedef struct xx_elf_dwarf_line_row {
    uint64_t line_program_offset;
    uint64_t address;
    uint64_t discriminator;
    uint32_t file_index;
    uint32_t line;
    uint32_t column;
    uint32_t isa;
    bool is_statement;
    bool basic_block;
    bool end_sequence;
    bool prologue_end;
    bool epilogue_begin;
} xx_elf_dwarf_line_row;

typedef struct xx_elf_dwarf xx_elf_dwarf;

/** Parse bounded DWARF sections nested in an already opened ELF image.
 * Supports DWARF 2 through 5 compilation units, abbreviations and forms, and
 * DWARF 2 through 5 line programs.  ELF SHF_COMPRESSED and GNU .zdebug_ zlib
 * sections are decoded.  Relocatable addresses remain section relative. */
XXFC_API xx_elf_dwarf *xx_elf_dwarf_create(const xx_elf *elf, xx_pd_struct *pd);
XXFC_API void xx_elf_dwarf_free(xx_elf_dwarf *dwarf);
/** True when .debug_line was absent or every present line program was valid.
 * A malformed optional line table is isolated from otherwise valid DIE data. */
XXFC_API bool xx_elf_dwarf_has_valid_line_table(const xx_elf_dwarf *dwarf);

XXFC_API uint64_t xx_elf_dwarf_get_number_of_units(const xx_elf_dwarf *dwarf);
XXFC_API const xx_elf_dwarf_unit *xx_elf_dwarf_get_unit(const xx_elf_dwarf *dwarf, uint64_t index);
XXFC_API uint64_t xx_elf_dwarf_get_number_of_abbreviations(const xx_elf_dwarf *dwarf);
XXFC_API const xx_elf_dwarf_abbreviation *xx_elf_dwarf_get_abbreviation(const xx_elf_dwarf *dwarf, uint64_t index);
XXFC_API const xx_elf_dwarf_attribute_spec *xx_elf_dwarf_get_abbreviation_attribute(const xx_elf_dwarf *dwarf,
                                                                                     const xx_elf_dwarf_abbreviation *abbreviation,
                                                                                     uint64_t index);
XXFC_API uint64_t xx_elf_dwarf_get_number_of_dies(const xx_elf_dwarf *dwarf);
XXFC_API const xx_elf_dwarf_die *xx_elf_dwarf_get_die(const xx_elf_dwarf *dwarf, uint64_t index);
XXFC_API const xx_elf_dwarf_attribute *xx_elf_dwarf_get_die_attribute(const xx_elf_dwarf *dwarf,
                                                                       const xx_elf_dwarf_die *die, uint64_t index);
XXFC_API const xx_elf_dwarf_attribute *xx_elf_dwarf_find_die_attribute(const xx_elf_dwarf *dwarf,
                                                                        const xx_elf_dwarf_die *die, uint64_t name);
/** Return a bounded view of an expression or block attribute.  The pointer is
 * owned by dwarf and remains valid until xx_elf_dwarf_free(). */
XXFC_API const uint8_t *xx_elf_dwarf_get_attribute_block(const xx_elf_dwarf *dwarf,
                                                          const xx_elf_dwarf_attribute *attribute);
XXFC_API const xx_elf_dwarf_die *xx_elf_dwarf_find_die(const xx_elf_dwarf *dwarf, uint64_t tag,
                                                        const char *name, uint64_t start_index);

XXFC_API uint64_t xx_elf_dwarf_get_number_of_line_files(const xx_elf_dwarf *dwarf);
XXFC_API const xx_elf_dwarf_line_file *xx_elf_dwarf_get_line_file(const xx_elf_dwarf *dwarf, uint64_t index);
XXFC_API uint64_t xx_elf_dwarf_get_number_of_line_rows(const xx_elf_dwarf *dwarf);
XXFC_API const xx_elf_dwarf_line_row *xx_elf_dwarf_get_line_row(const xx_elf_dwarf *dwarf, uint64_t index);
/** Return the address-line view used by debugger/source lookup consumers.
 * The first row for an address is retained, later rows with that address and
 * the same line are omitted, and rows for a different line are retained. */
XXFC_API uint64_t xx_elf_dwarf_get_number_of_address_line_rows(const xx_elf_dwarf *dwarf);
XXFC_API const xx_elf_dwarf_line_row *xx_elf_dwarf_get_address_line_row(const xx_elf_dwarf *dwarf,
                                                                         uint64_t index);
/** Unique source paths referenced by address-line rows plus compilation-unit
 * source paths.  Strings are owned by dwarf. */
XXFC_API uint64_t xx_elf_dwarf_get_number_of_source_files(const xx_elf_dwarf *dwarf);
XXFC_API const char *xx_elf_dwarf_get_source_file(const xx_elf_dwarf *dwarf, uint64_t index);
/** Locate the last source row whose address does not exceed address, stopping
 * at an end-of-sequence boundary.  address_bias is useful for ET_REL images. */
XXFC_API const xx_elf_dwarf_line_row *xx_elf_dwarf_find_line_row(const xx_elf_dwarf *dwarf,
                                                                  uint64_t address, uint64_t address_bias);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ELF_DWARF_H */
