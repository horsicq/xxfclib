/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/elf/xx_elf_dwarf.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include <limits.h>
#include <string.h>

#define DWARF_SECTION_LIMIT (UINT64_C(512) * UINT64_C(1024) * UINT64_C(1024))
#define DWARF_UNIT_LIMIT UINT32_C(1048576)
#define DWARF_ABBREV_LIMIT UINT32_C(1048576)
#define DWARF_SPEC_LIMIT UINT32_C(4194304)
#define DWARF_DIE_LIMIT UINT32_C(4194304)
#define DWARF_ATTRIBUTE_LIMIT UINT32_C(16777216)
#define DWARF_LINE_FILE_LIMIT UINT32_C(1048576)
#define DWARF_LINE_ROW_LIMIT UINT32_C(16777216)
#define DWARF_SOURCE_FILE_LIMIT UINT32_C(2097152)
#define DWARF_TABLE_FORMAT_LIMIT UINT32_C(64)
#define DWARF_DIRECTORY_LIMIT UINT32_C(1048576)
#define DWARF_RELOCATION_LIMIT UINT32_C(16777216)

#define ELF_SECTION_RELA UINT32_C(4)
#define ELF_SECTION_REL UINT32_C(9)

#define DW_FORM_ADDR UINT64_C(0x01)
#define DW_FORM_BLOCK2 UINT64_C(0x03)
#define DW_FORM_BLOCK4 UINT64_C(0x04)
#define DW_FORM_DATA2 UINT64_C(0x05)
#define DW_FORM_DATA4 UINT64_C(0x06)
#define DW_FORM_DATA8 UINT64_C(0x07)
#define DW_FORM_STRING UINT64_C(0x08)
#define DW_FORM_BLOCK UINT64_C(0x09)
#define DW_FORM_BLOCK1 UINT64_C(0x0a)
#define DW_FORM_DATA1 UINT64_C(0x0b)
#define DW_FORM_FLAG UINT64_C(0x0c)
#define DW_FORM_SDATA UINT64_C(0x0d)
#define DW_FORM_STRP UINT64_C(0x0e)
#define DW_FORM_UDATA UINT64_C(0x0f)
#define DW_FORM_REF_ADDR UINT64_C(0x10)
#define DW_FORM_REF1 UINT64_C(0x11)
#define DW_FORM_REF2 UINT64_C(0x12)
#define DW_FORM_REF4 UINT64_C(0x13)
#define DW_FORM_REF8 UINT64_C(0x14)
#define DW_FORM_REF_UDATA UINT64_C(0x15)
#define DW_FORM_INDIRECT UINT64_C(0x16)
#define DW_FORM_SEC_OFFSET UINT64_C(0x17)
#define DW_FORM_EXPRLOC UINT64_C(0x18)
#define DW_FORM_FLAG_PRESENT UINT64_C(0x19)
#define DW_FORM_STRX UINT64_C(0x1a)
#define DW_FORM_ADDRX UINT64_C(0x1b)
#define DW_FORM_REF_SUP4 UINT64_C(0x1c)
#define DW_FORM_STRP_SUP UINT64_C(0x1d)
#define DW_FORM_DATA16 UINT64_C(0x1e)
#define DW_FORM_LINE_STRP UINT64_C(0x1f)
#define DW_FORM_REF_SIG8 UINT64_C(0x20)
#define DW_FORM_IMPLICIT_CONST UINT64_C(0x21)
#define DW_FORM_LOCLISTX UINT64_C(0x22)
#define DW_FORM_RNGLISTX UINT64_C(0x23)
#define DW_FORM_REF_SUP8 UINT64_C(0x24)
#define DW_FORM_STRX1 UINT64_C(0x25)
#define DW_FORM_STRX2 UINT64_C(0x26)
#define DW_FORM_STRX3 UINT64_C(0x27)
#define DW_FORM_STRX4 UINT64_C(0x28)
#define DW_FORM_ADDRX1 UINT64_C(0x29)
#define DW_FORM_ADDRX2 UINT64_C(0x2a)
#define DW_FORM_ADDRX3 UINT64_C(0x2b)
#define DW_FORM_ADDRX4 UINT64_C(0x2c)
#define DW_FORM_GNU_ADDR_INDEX UINT64_C(0x1f01)
#define DW_FORM_GNU_STR_INDEX UINT64_C(0x1f02)
#define DW_FORM_GNU_REF_ALT UINT64_C(0x1f20)
#define DW_FORM_GNU_STRP_ALT UINT64_C(0x1f21)

#define DW_UT_COMPILE UINT8_C(0x01)
#define DW_UT_TYPE UINT8_C(0x02)
#define DW_UT_PARTIAL UINT8_C(0x03)
#define DW_UT_SKELETON UINT8_C(0x04)
#define DW_UT_SPLIT_COMPILE UINT8_C(0x05)
#define DW_UT_SPLIT_TYPE UINT8_C(0x06)

#define DW_LNCT_PATH UINT64_C(0x01)
#define DW_LNCT_DIRECTORY_INDEX UINT64_C(0x02)
#define DW_LNCT_TIMESTAMP UINT64_C(0x03)
#define DW_LNCT_SIZE UINT64_C(0x04)
#define DW_LNCT_MD5 UINT64_C(0x05)

#define DW_LNS_COPY UINT8_C(0x01)
#define DW_LNS_ADVANCE_PC UINT8_C(0x02)
#define DW_LNS_ADVANCE_LINE UINT8_C(0x03)
#define DW_LNS_SET_FILE UINT8_C(0x04)
#define DW_LNS_SET_COLUMN UINT8_C(0x05)
#define DW_LNS_NEGATE_STMT UINT8_C(0x06)
#define DW_LNS_SET_BASIC_BLOCK UINT8_C(0x07)
#define DW_LNS_CONST_ADD_PC UINT8_C(0x08)
#define DW_LNS_FIXED_ADVANCE_PC UINT8_C(0x09)
#define DW_LNS_SET_PROLOGUE_END UINT8_C(0x0a)
#define DW_LNS_SET_EPILOGUE_BEGIN UINT8_C(0x0b)
#define DW_LNS_SET_ISA UINT8_C(0x0c)

#define DW_LNE_END_SEQUENCE UINT8_C(0x01)
#define DW_LNE_SET_ADDRESS UINT8_C(0x02)
#define DW_LNE_DEFINE_FILE UINT8_C(0x03)
#define DW_LNE_SET_DISCRIMINATOR UINT8_C(0x04)

typedef struct dwarf_section {
    uint8_t *data;
    uint64_t size;
} dwarf_section;

typedef struct dwarf_cursor {
    const uint8_t *data;
    uint64_t size;
    uint64_t position;
    bool big_endian;
} dwarf_cursor;

struct xx_elf_dwarf {
    const xx_elf *elf;
    bool big_endian;
    dwarf_section info;
    dwarf_section abbrev;
    dwarf_section strings;
    dwarf_section line;
    dwarf_section line_strings;
    dwarf_section string_offsets;
    dwarf_section addresses;
    xx_elf_dwarf_unit *units;
    uint32_t unit_count;
    uint32_t unit_capacity;
    xx_elf_dwarf_abbreviation *abbreviations;
    uint32_t abbreviation_count;
    uint32_t abbreviation_capacity;
    xx_elf_dwarf_attribute_spec *specifications;
    uint32_t specification_count;
    uint32_t specification_capacity;
    xx_elf_dwarf_die *dies;
    uint32_t die_count;
    uint32_t die_capacity;
    xx_elf_dwarf_attribute *attributes;
    uint32_t attribute_count;
    uint32_t attribute_capacity;
    xx_elf_dwarf_line_file *line_files;
    uint32_t line_file_count;
    uint32_t line_file_capacity;
    xx_elf_dwarf_line_row *line_rows;
    uint32_t line_row_count;
    uint32_t line_row_capacity;
    uint32_t *address_line_row_indices;
    uint32_t address_line_row_count;
    char **source_files;
    uint32_t source_file_count;
    uint32_t source_file_capacity;
    bool line_table_valid;
};

static uint16_t dwarf_u16(const uint8_t *data, bool big_endian)
{
    return big_endian ? (uint16_t)(((uint16_t)data[0] << 8) | data[1]) : (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t dwarf_u32(const uint8_t *data, bool big_endian)
{
    if (big_endian) {
        return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
    }
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint64_t dwarf_u64(const uint8_t *data, bool big_endian)
{
    uint64_t value = 0U;
    unsigned index;
    if (big_endian) {
        for (index = 0U; index < 8U; ++index) value = (value << 8) | data[index];
    } else {
        for (index = 0U; index < 8U; ++index) value |= (uint64_t)data[index] << (index * 8U);
    }
    return value;
}

static bool dwarf_add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (!result || left > UINT64_MAX - right) return false;
    *result = left + right;
    return true;
}

static bool dwarf_grow(void **pointer, uint32_t *capacity, uint32_t needed, size_t element_size, uint32_t limit)
{
    uint32_t next;
    void *allocation;
    if (!pointer || !capacity || !element_size || needed > limit) return false;
    if (needed <= *capacity) return true;
    next = *capacity ? *capacity : 16U;
    while (next < needed) {
        if (next > limit / 2U) {
            next = limit;
            break;
        }
        next *= 2U;
    }
    if ((size_t)next > SIZE_MAX / element_size) return false;
    allocation = xx_mem_realloc(*pointer, (size_t)next * element_size);
    if (!allocation) return false;
    *pointer = allocation;
    *capacity = next;
    return true;
}

static bool dwarf_cursor_bytes(dwarf_cursor *cursor, uint64_t count, const uint8_t **output)
{
    if (!cursor || cursor->position > cursor->size || count > cursor->size - cursor->position) return false;
    if (output) *output = cursor->data + cursor->position;
    cursor->position += count;
    return true;
}

static bool dwarf_cursor_uint(dwarf_cursor *cursor, unsigned width, uint64_t *output)
{
    const uint8_t *bytes;
    uint64_t value = 0U;
    unsigned index;
    if (!output || width == 0U || width > 8U || !dwarf_cursor_bytes(cursor, width, &bytes)) return false;
    if (cursor->big_endian) {
        for (index = 0U; index < width; ++index) value = (value << 8) | bytes[index];
    } else {
        for (index = 0U; index < width; ++index) value |= (uint64_t)bytes[index] << (index * 8U);
    }
    *output = value;
    return true;
}

static bool dwarf_cursor_uleb(dwarf_cursor *cursor, uint64_t *output)
{
    uint64_t value = 0U;
    unsigned shift = 0U;
    unsigned count;
    if (!cursor || !output) return false;
    for (count = 0U; count < 10U; ++count) {
        uint8_t byte;
        if (cursor->position >= cursor->size) return false;
        byte = cursor->data[cursor->position++];
        if (shift == 63U && (byte & UINT8_C(0x7e)) != 0U) return false;
        value |= (uint64_t)(byte & UINT8_C(0x7f)) << shift;
        if ((byte & UINT8_C(0x80)) == 0U) {
            *output = value;
            return true;
        }
        shift += 7U;
    }
    return false;
}

static bool dwarf_cursor_sleb(dwarf_cursor *cursor, int64_t *output)
{
    uint64_t value = 0U;
    unsigned shift = 0U;
    unsigned count;
    uint8_t byte = 0U;
    if (!cursor || !output) return false;
    for (count = 0U; count < 10U; ++count) {
        if (cursor->position >= cursor->size) return false;
        byte = cursor->data[cursor->position++];
        if (shift == 63U && (byte & UINT8_C(0x7e)) != 0U && (byte & UINT8_C(0x7f)) != UINT8_C(0x7f)) return false;
        value |= (uint64_t)(byte & UINT8_C(0x7f)) << shift;
        shift += 7U;
        if ((byte & UINT8_C(0x80)) == 0U) break;
    }
    if ((byte & UINT8_C(0x80)) != 0U) return false;
    if (shift < 64U && (byte & UINT8_C(0x40)) != 0U) value |= UINT64_MAX << shift;
    *output = (int64_t)value;
    return true;
}

static const char *dwarf_cursor_string(dwarf_cursor *cursor)
{
    uint64_t start;
    if (!cursor) return NULL;
    start = cursor->position;
    while (cursor->position < cursor->size && cursor->data[cursor->position] != 0U) ++cursor->position;
    if (cursor->position >= cursor->size) return NULL;
    ++cursor->position;
    return (const char *)(cursor->data + start);
}

static const char *dwarf_section_string(const dwarf_section *section, uint64_t offset)
{
    uint64_t index;
    if (!section || !section->data || offset >= section->size) return NULL;
    for (index = offset; index < section->size; ++index) {
        if (section->data[index] == 0U) return (const char *)(section->data + offset);
    }
    return NULL;
}

static char *dwarf_strdup(const char *value)
{
    size_t length;
    char *copy;
    if (!value) return NULL;
    length = strlen(value);
    if (length == SIZE_MAX) return NULL;
    copy = (char *)xx_mem_alloc(length + 1U);
    if (!copy) return NULL;
    memcpy(copy, value, length + 1U);
    return copy;
}

static bool dwarf_read_device(const xx_elf *elf, uint64_t relative, uint8_t *output, uint64_t size)
{
    uint64_t index;
    int64_t total;
    uint64_t absolute;
    if (!elf || !elf->format.device || (!output && size != 0U) || elf->format.base_address < 0 || size > SIZE_MAX) return false;
    total = xx_io_total_size(elf->format.device);
    if (total < 0 || (uint64_t)elf->format.base_address > (uint64_t)total) return false;
    absolute = (uint64_t)elf->format.base_address + relative;
    if (absolute < relative || absolute > (uint64_t)total || size > (uint64_t)total - absolute) return false;
    for (index = 0U; index < size; ++index) output[index] = xx_io_get_u8(elf->format.device, (int64_t)(absolute + index));
    return true;
}

static bool dwarf_section_name(const xx_elf *elf, const xx_elf_section_header *section, char *output, size_t output_size)
{
    const xx_elf_section_header *names;
    uint64_t index;
    if (!elf || !section || !output || output_size == 0U || !elf->section_headers || elf->section_name_index >= elf->section_header_count) return false;
    names = &elf->section_headers[elf->section_name_index];
    if (names->type == XX_ELF_SECTION_NOBITS || section->name_offset >= names->size) return false;
    for (index = 0U; index + 1U < output_size && (uint64_t)section->name_offset + index < names->size; ++index) {
        uint64_t relative = names->offset + section->name_offset + index;
        int64_t absolute;
        uint8_t value;
        if (relative < names->offset || elf->format.base_address < 0 || relative > (uint64_t)INT64_MAX - (uint64_t)elf->format.base_address) return false;
        absolute = elf->format.base_address + (int64_t)relative;
        value = xx_io_get_u8(elf->format.device, absolute);
        output[index] = (char)value;
        if (value == 0U) return true;
    }
    output[output_size - 1U] = '\0';
    return false;
}

static void dwarf_store_uint(uint8_t *bytes, unsigned width, bool big_endian, uint64_t value)
{
    unsigned index;
    if (big_endian) {
        for (index = 0U; index < width; ++index) bytes[index] = (uint8_t)(value >> ((width - 1U - index) * 8U));
    } else {
        for (index = 0U; index < width; ++index) bytes[index] = (uint8_t)(value >> (index * 8U));
    }
}

static int64_t dwarf_signed_uint(uint64_t value, unsigned width)
{
    uint64_t sign;
    int64_t result;
    if (width < 8U) {
        sign = UINT64_C(1) << (width * 8U - 1U);
        if ((value & sign) != 0U) value |= UINT64_MAX << (width * 8U);
    }
    memcpy(&result, &value, sizeof(result));
    return result;
}

static bool dwarf_relocation_shape(uint16_t machine, uint64_t type, unsigned *width, bool *pc_relative)
{
    if (!width || !pc_relative) return false;
    *pc_relative = false;
    switch (machine) {
        case 3U: /* EM_386 */
            if (type == 1U) {
                *width = 4U;
                return true;
            }
            if (type == 2U) {
                *width = 4U;
                *pc_relative = true;
                return true;
            }
            break;
        case 20U: /* EM_PPC */
            if (type == 1U) {
                *width = 4U;
                return true;
            }
            if (type == 26U) {
                *width = 4U;
                *pc_relative = true;
                return true;
            }
            break;
        case 21U: /* EM_PPC64 */
            if (type == 38U) {
                *width = 8U;
                return true;
            }
            if (type == 1U) {
                *width = 4U;
                return true;
            }
            if (type == 44U) {
                *width = 8U;
                *pc_relative = true;
                return true;
            }
            if (type == 26U) {
                *width = 4U;
                *pc_relative = true;
                return true;
            }
            break;
        case 40U: /* EM_ARM */
            if (type == 2U) {
                *width = 4U;
                return true;
            }
            if (type == 3U) {
                *width = 4U;
                *pc_relative = true;
                return true;
            }
            break;
        case 62U: /* EM_X86_64 */
            if (type == 1U) {
                *width = 8U;
                return true;
            }
            if (type == 10U || type == 11U) {
                *width = 4U;
                return true;
            }
            if (type == 2U) {
                *width = 4U;
                *pc_relative = true;
                return true;
            }
            break;
        case 183U: /* EM_AARCH64 */
            if (type == 257U) {
                *width = 8U;
                return true;
            }
            if (type == 258U) {
                *width = 4U;
                return true;
            }
            if (type == 259U) {
                *width = 2U;
                return true;
            }
            if (type == 260U) {
                *width = 8U;
                *pc_relative = true;
                return true;
            }
            if (type == 261U) {
                *width = 4U;
                *pc_relative = true;
                return true;
            }
            if (type == 262U) {
                *width = 2U;
                *pc_relative = true;
                return true;
            }
            break;
        case 243U: /* EM_RISCV */
            if (type == 1U) {
                *width = 4U;
                return true;
            }
            if (type == 2U) {
                *width = 8U;
                return true;
            }
            break;
        default: break;
    }
    return false;
}

static bool dwarf_add_signed(uint64_t value, int64_t addend, uint64_t *result)
{
    uint64_t magnitude;
    if (!result) return false;
    if (addend >= 0) {
        if (value > UINT64_MAX - (uint64_t)addend) return false;
        *result = value + (uint64_t)addend;
        return true;
    }
    magnitude = (uint64_t)(-(addend + 1)) + 1U;
    if (value < magnitude) return false;
    *result = value - magnitude;
    return true;
}

static bool dwarf_apply_relocations(const xx_elf *elf, uint64_t target_index, dwarf_section *target)
{
    uint64_t section_index;
    bool big_endian;
    bool wide;
    if (!elf || !target || !target->data) return false;
    big_endian = elf->data_encoding == XX_ELF_DATA_MSB;
    wide = elf->elf_class == XX_ELF_CLASS_64;
    for (section_index = 0U; section_index < elf->section_header_count; ++section_index) {
        const xx_elf_section_header *relocations = &elf->section_headers[section_index];
        const xx_elf_section_header *symbols;
        uint64_t canonical_relocation_size;
        uint64_t canonical_symbol_size;
        uint64_t relocation_count;
        uint64_t relocation_index;
        if ((relocations->type != ELF_SECTION_RELA && relocations->type != ELF_SECTION_REL) || relocations->info != target_index) continue;
        if (relocations->link >= elf->section_header_count) return false;
        symbols = &elf->section_headers[relocations->link];
        canonical_relocation_size = wide ? (relocations->type == ELF_SECTION_RELA ? 24U : 16U) : (relocations->type == ELF_SECTION_RELA ? 12U : 8U);
        canonical_symbol_size = wide ? 24U : 16U;
        if (relocations->entry_size < canonical_relocation_size || symbols->entry_size < canonical_symbol_size || relocations->size % relocations->entry_size != 0U ||
            symbols->size % symbols->entry_size != 0U)
            return false;
        relocation_count = relocations->size / relocations->entry_size;
        if (relocation_count > DWARF_RELOCATION_LIMIT) return false;
        for (relocation_index = 0U; relocation_index < relocation_count; ++relocation_index) {
            uint8_t relocation_bytes[24];
            uint8_t symbol_bytes[24];
            uint64_t relocation_offset;
            uint64_t relocation_info;
            uint64_t symbol_index;
            uint64_t relocation_type;
            uint64_t symbol_value;
            uint64_t relocated_value;
            int64_t addend;
            unsigned width;
            bool pc_relative;
            uint64_t entry_offset = relocations->offset + relocation_index * relocations->entry_size;
            if (entry_offset < relocations->offset || !dwarf_read_device(elf, entry_offset, relocation_bytes, canonical_relocation_size)) return false;
            if (wide) {
                relocation_offset = dwarf_u64(relocation_bytes, big_endian);
                relocation_info = dwarf_u64(relocation_bytes + 8U, big_endian);
                symbol_index = relocation_info >> 32;
                relocation_type = relocation_info & UINT32_MAX;
                addend = relocations->type == ELF_SECTION_RELA ? dwarf_signed_uint(dwarf_u64(relocation_bytes + 16U, big_endian), 8U) : 0;
            } else {
                relocation_offset = dwarf_u32(relocation_bytes, big_endian);
                relocation_info = dwarf_u32(relocation_bytes + 4U, big_endian);
                symbol_index = relocation_info >> 8;
                relocation_type = relocation_info & UINT8_MAX;
                addend = relocations->type == ELF_SECTION_RELA ? dwarf_signed_uint(dwarf_u32(relocation_bytes + 8U, big_endian), 4U) : 0;
            }
            if (!dwarf_relocation_shape(elf->machine, relocation_type, &width, &pc_relative) || relocation_offset > target->size ||
                width > target->size - relocation_offset || symbol_index >= symbols->size / symbols->entry_size)
                return false;
            entry_offset = symbols->offset + symbol_index * symbols->entry_size;
            if (entry_offset < symbols->offset || !dwarf_read_device(elf, entry_offset, symbol_bytes, canonical_symbol_size)) return false;
            symbol_value = wide ? dwarf_u64(symbol_bytes + 8U, big_endian) : dwarf_u32(symbol_bytes + 4U, big_endian);
            if (relocations->type == ELF_SECTION_REL) {
                uint64_t raw = width == 8U   ? dwarf_u64(target->data + relocation_offset, big_endian)
                               : width == 4U ? dwarf_u32(target->data + relocation_offset, big_endian)
                                             : dwarf_u16(target->data + relocation_offset, big_endian);
                addend = dwarf_signed_uint(raw, width);
            }
            if (!dwarf_add_signed(symbol_value, addend, &relocated_value)) return false;
            if (pc_relative) {
                if (relocated_value < relocation_offset) return false;
                relocated_value -= relocation_offset;
            }
            if ((width == 2U && relocated_value > UINT16_MAX) || (width == 4U && relocated_value > UINT32_MAX)) return false;
            dwarf_store_uint(target->data + relocation_offset, width, big_endian, relocated_value);
        }
    }
    return true;
}

static bool dwarf_name_is(const char *actual, const char *canonical)
{
    if (!actual || !canonical) return false;
    if (strcmp(actual, canonical) == 0) return true;
    if (strncmp(actual, ".zdebug_", 8U) == 0 && strncmp(canonical, ".debug_", 7U) == 0) return strcmp(actual + 8U, canonical + 7U) == 0;
    return false;
}

static bool dwarf_load_section(const xx_elf *elf, const char *wanted, dwarf_section *output)
{
    uint64_t index;
    if (!elf || !wanted || !output) return false;
    output->data = NULL;
    output->size = 0U;
    for (index = 0U; index < elf->section_header_count; ++index) {
        const xx_elf_section_header *header = &elf->section_headers[index];
        char name[128];
        uint8_t *raw;
        uint64_t raw_size;
        if (!dwarf_section_name(elf, header, name, sizeof(name)) || !dwarf_name_is(name, wanted)) continue;
        if (header->type == XX_ELF_SECTION_NOBITS || header->size == 0U || header->size > DWARF_SECTION_LIMIT || header->size > SIZE_MAX) return false;
        raw_size = header->size;
        raw = (uint8_t *)xx_mem_alloc((size_t)raw_size);
        if (!raw || !dwarf_read_device(elf, header->offset, raw, raw_size)) {
            xx_mem_free(raw);
            return false;
        }
        if ((header->flags & XX_ELF_SECTION_FLAG_COMPRESSED) != 0U) {
            uint64_t decoded_size;
            uint64_t header_size;
            uint64_t type;
            uint8_t *decoded;
            size_t written = 0U;
            if (elf->elf_class == XX_ELF_CLASS_64) {
                if (raw_size < 24U) {
                    xx_mem_free(raw);
                    return false;
                }
                type = dwarf_u32(raw, elf->data_encoding == XX_ELF_DATA_MSB);
                decoded_size = dwarf_u64(raw + 8U, elf->data_encoding == XX_ELF_DATA_MSB);
                header_size = 24U;
            } else {
                if (raw_size < 12U) {
                    xx_mem_free(raw);
                    return false;
                }
                type = dwarf_u32(raw, elf->data_encoding == XX_ELF_DATA_MSB);
                decoded_size = dwarf_u32(raw + 4U, elf->data_encoding == XX_ELF_DATA_MSB);
                header_size = 12U;
            }
            if (type != 1U || decoded_size == 0U || decoded_size > DWARF_SECTION_LIMIT || decoded_size > SIZE_MAX) {
                xx_mem_free(raw);
                return false;
            }
            decoded = (uint8_t *)xx_mem_alloc((size_t)decoded_size);
            if (!decoded || !xx_zlib_stream_decode_memory(raw + header_size, (size_t)(raw_size - header_size), decoded, (size_t)decoded_size, &written) ||
                written != decoded_size) {
                xx_mem_free(decoded);
                xx_mem_free(raw);
                return false;
            }
            xx_mem_free(raw);
            output->data = decoded;
            output->size = decoded_size;
            return dwarf_apply_relocations(elf, index, output);
        }
        if (strncmp(name, ".zdebug_", 8U) == 0) {
            uint64_t decoded_size;
            uint8_t *decoded;
            size_t written = 0U;
            if (raw_size < 12U || memcmp(raw, "ZLIB", 4U) != 0) {
                xx_mem_free(raw);
                return false;
            }
            decoded_size = dwarf_u64(raw + 4U, true);
            if (decoded_size == 0U || decoded_size > DWARF_SECTION_LIMIT || decoded_size > SIZE_MAX) {
                xx_mem_free(raw);
                return false;
            }
            decoded = (uint8_t *)xx_mem_alloc((size_t)decoded_size);
            if (!decoded || !xx_zlib_stream_decode_memory(raw + 12U, (size_t)(raw_size - 12U), decoded, (size_t)decoded_size, &written) || written != decoded_size) {
                xx_mem_free(decoded);
                xx_mem_free(raw);
                return false;
            }
            xx_mem_free(raw);
            output->data = decoded;
            output->size = decoded_size;
            return dwarf_apply_relocations(elf, index, output);
        }
        output->data = raw;
        output->size = raw_size;
        return dwarf_apply_relocations(elf, index, output);
    }
    return true;
}

static bool dwarf_read_initial_length(dwarf_cursor *cursor, uint64_t *unit_end, uint8_t *offset_size, uint64_t *length)
{
    uint64_t initial;
    uint64_t value;
    if (!cursor || !unit_end || !offset_size || !length || !dwarf_cursor_uint(cursor, 4U, &initial)) return false;
    if (initial == UINT32_C(0xffffffff)) {
        if (!dwarf_cursor_uint(cursor, 8U, &value)) return false;
        *offset_size = 8U;
    } else if (initial >= UINT32_C(0xfffffff0)) {
        return false;
    } else {
        value = initial;
        *offset_size = 4U;
    }
    if (!dwarf_add_u64(cursor->position, value, unit_end) || *unit_end > cursor->size) return false;
    *length = value;
    return true;
}

static bool dwarf_parse_abbreviations(xx_elf_dwarf *dwarf)
{
    dwarf_cursor cursor;
    uint64_t table_offset = 0U;
    if (!dwarf) return false;
    if (!dwarf->abbrev.data) return dwarf->info.data == NULL;
    cursor.data = dwarf->abbrev.data;
    cursor.size = dwarf->abbrev.size;
    cursor.position = 0U;
    cursor.big_endian = dwarf->big_endian;
    while (cursor.position < cursor.size) {
        uint64_t entry_offset = cursor.position;
        uint64_t code;
        uint64_t tag;
        uint64_t children;
        xx_elf_dwarf_abbreviation *abbreviation;
        if (!dwarf_cursor_uleb(&cursor, &code)) return false;
        if (code == 0U) {
            table_offset = cursor.position;
            continue;
        }
        if (!dwarf_cursor_uleb(&cursor, &tag) || !dwarf_cursor_uint(&cursor, 1U, &children) || children > 1U ||
            !dwarf_grow((void **)&dwarf->abbreviations, &dwarf->abbreviation_capacity, dwarf->abbreviation_count + 1U, sizeof(*dwarf->abbreviations), DWARF_ABBREV_LIMIT))
            return false;
        abbreviation = &dwarf->abbreviations[dwarf->abbreviation_count++];
        abbreviation->table_offset = table_offset;
        abbreviation->offset = entry_offset;
        abbreviation->code = code;
        abbreviation->tag = tag;
        abbreviation->has_children = children != 0U;
        abbreviation->attribute_spec_index = dwarf->specification_count;
        abbreviation->attribute_spec_count = 0U;
        for (;;) {
            uint64_t name;
            uint64_t form;
            xx_elf_dwarf_attribute_spec *specification;
            if (!dwarf_cursor_uleb(&cursor, &name) || !dwarf_cursor_uleb(&cursor, &form)) return false;
            if (name == 0U && form == 0U) break;
            if (name == 0U || form == 0U ||
                !dwarf_grow((void **)&dwarf->specifications, &dwarf->specification_capacity, dwarf->specification_count + 1U, sizeof(*dwarf->specifications),
                            DWARF_SPEC_LIMIT))
                return false;
            specification = &dwarf->specifications[dwarf->specification_count++];
            specification->name = name;
            specification->form = form;
            specification->implicit_constant = 0;
            if (form == DW_FORM_IMPLICIT_CONST && !dwarf_cursor_sleb(&cursor, &specification->implicit_constant)) return false;
            ++abbreviation->attribute_spec_count;
        }
    }
    return true;
}

static const xx_elf_dwarf_abbreviation *dwarf_find_abbreviation(const xx_elf_dwarf *dwarf, uint64_t table_offset, uint64_t code)
{
    uint32_t index;
    if (!dwarf) return NULL;
    for (index = 0U; index < dwarf->abbreviation_count; ++index) {
        const xx_elf_dwarf_abbreviation *value = &dwarf->abbreviations[index];
        if (value->table_offset == table_offset && value->code == code) return value;
    }
    return NULL;
}

static bool dwarf_parse_form(xx_elf_dwarf *dwarf, dwarf_cursor *cursor, const xx_elf_dwarf_unit *unit, uint64_t form, int64_t implicit_constant, unsigned recursion,
                             xx_elf_dwarf_attribute *attribute)
{
    uint64_t value = 0U;
    uint64_t count = 0U;
    int64_t signed_value = 0;
    const char *string_value;
    unsigned width = 0U;
    if (!dwarf || !cursor || !unit || !attribute || recursion > 4U) return false;
    attribute->form = form;
    attribute->kind = XX_ELF_DWARF_VALUE_NONE;
    attribute->data_offset = cursor->position;
    switch (form) {
        case DW_FORM_ADDR:
            width = unit->address_size;
            attribute->kind = XX_ELF_DWARF_VALUE_UNSIGNED;
            break;
        case DW_FORM_DATA1:
        case DW_FORM_FLAG:
        case DW_FORM_REF1:
        case DW_FORM_STRX1:
        case DW_FORM_ADDRX1: width = 1U; break;
        case DW_FORM_DATA2:
        case DW_FORM_REF2:
        case DW_FORM_STRX2:
        case DW_FORM_ADDRX2: width = 2U; break;
        case DW_FORM_DATA4:
        case DW_FORM_REF4:
        case DW_FORM_REF_SUP4:
        case DW_FORM_STRX4:
        case DW_FORM_ADDRX4: width = 4U; break;
        case DW_FORM_DATA8:
        case DW_FORM_REF8:
        case DW_FORM_REF_SIG8:
        case DW_FORM_REF_SUP8: width = 8U; break;
        case DW_FORM_STRX3:
        case DW_FORM_ADDRX3: width = 3U; break;
        case DW_FORM_STRP:
        case DW_FORM_STRP_SUP:
        case DW_FORM_LINE_STRP:
        case DW_FORM_SEC_OFFSET:
        case DW_FORM_GNU_REF_ALT:
        case DW_FORM_GNU_STRP_ALT: width = unit->offset_size; break;
        case DW_FORM_REF_ADDR: width = unit->version == 2U ? unit->address_size : unit->offset_size; break;
        default: break;
    }
    if (width != 0U) {
        if (!dwarf_cursor_uint(cursor, width, &value)) return false;
        attribute->unsigned_value = value;
        attribute->data_size = width;
        if (form == DW_FORM_STRP) {
            attribute->kind = XX_ELF_DWARF_VALUE_STRING;
            attribute->string_value = dwarf_section_string(&dwarf->strings, value);
            return attribute->string_value != NULL;
        }
        if (form == DW_FORM_LINE_STRP) {
            attribute->kind = XX_ELF_DWARF_VALUE_STRING;
            attribute->string_value = dwarf_section_string(&dwarf->line_strings, value);
            return attribute->string_value != NULL;
        }
        if (form == DW_FORM_STRP_SUP || form == DW_FORM_GNU_STRP_ALT) {
            attribute->kind = XX_ELF_DWARF_VALUE_UNSIGNED;
            return true;
        }
        if (form >= DW_FORM_STRX1 && form <= DW_FORM_STRX4) {
            attribute->kind = XX_ELF_DWARF_VALUE_STRING_INDEX;
            return true;
        }
        if (form >= DW_FORM_ADDRX1 && form <= DW_FORM_ADDRX4) {
            attribute->kind = XX_ELF_DWARF_VALUE_ADDRESS_INDEX;
            return true;
        }
        if ((form >= DW_FORM_REF1 && form <= DW_FORM_REF8) || form == DW_FORM_REF_UDATA) {
            if (value > UINT64_MAX - unit->offset) return false;
            attribute->kind = XX_ELF_DWARF_VALUE_REFERENCE;
            attribute->unsigned_value = unit->offset + value;
            return true;
        }
        if (form == DW_FORM_REF_ADDR || form == DW_FORM_REF_SUP4 || form == DW_FORM_REF_SUP8 || form == DW_FORM_REF_SIG8 || form == DW_FORM_GNU_REF_ALT) {
            attribute->kind = XX_ELF_DWARF_VALUE_REFERENCE;
            return true;
        }
        if (attribute->kind == XX_ELF_DWARF_VALUE_NONE) attribute->kind = XX_ELF_DWARF_VALUE_UNSIGNED;
        return true;
    }
    switch (form) {
        case DW_FORM_STRING:
            attribute->data_offset = cursor->position;
            string_value = dwarf_cursor_string(cursor);
            if (!string_value) return false;
            attribute->kind = XX_ELF_DWARF_VALUE_STRING;
            attribute->string_value = string_value;
            attribute->data_size = strlen(string_value) + 1U;
            return true;
        case DW_FORM_UDATA:
        case DW_FORM_REF_UDATA:
        case DW_FORM_STRX:
        case DW_FORM_ADDRX:
        case DW_FORM_LOCLISTX:
        case DW_FORM_RNGLISTX:
        case DW_FORM_GNU_ADDR_INDEX:
        case DW_FORM_GNU_STR_INDEX:
            if (!dwarf_cursor_uleb(cursor, &value)) return false;
            attribute->unsigned_value = value;
            if (form == DW_FORM_REF_UDATA) {
                if (value > UINT64_MAX - unit->offset) return false;
                attribute->unsigned_value = unit->offset + value;
                attribute->kind = XX_ELF_DWARF_VALUE_REFERENCE;
            } else if (form == DW_FORM_STRX || form == DW_FORM_GNU_STR_INDEX) {
                attribute->kind = XX_ELF_DWARF_VALUE_STRING_INDEX;
            } else if (form == DW_FORM_ADDRX || form == DW_FORM_GNU_ADDR_INDEX) {
                attribute->kind = XX_ELF_DWARF_VALUE_ADDRESS_INDEX;
            } else if (form == DW_FORM_LOCLISTX) {
                attribute->kind = XX_ELF_DWARF_VALUE_LOCLIST_INDEX;
            } else if (form == DW_FORM_RNGLISTX) {
                attribute->kind = XX_ELF_DWARF_VALUE_RNGLIST_INDEX;
            } else {
                attribute->kind = XX_ELF_DWARF_VALUE_UNSIGNED;
            }
            attribute->data_size = cursor->position - attribute->data_offset;
            return true;
        case DW_FORM_SDATA:
            if (!dwarf_cursor_sleb(cursor, &signed_value)) return false;
            attribute->kind = XX_ELF_DWARF_VALUE_SIGNED;
            attribute->signed_value = signed_value;
            attribute->data_size = cursor->position - attribute->data_offset;
            return true;
        case DW_FORM_FLAG_PRESENT:
            attribute->kind = XX_ELF_DWARF_VALUE_UNSIGNED;
            attribute->unsigned_value = 1U;
            return true;
        case DW_FORM_IMPLICIT_CONST:
            attribute->kind = XX_ELF_DWARF_VALUE_SIGNED;
            attribute->signed_value = implicit_constant;
            return true;
        case DW_FORM_BLOCK1:
            if (!dwarf_cursor_uint(cursor, 1U, &count)) return false;
            break;
        case DW_FORM_BLOCK2:
            if (!dwarf_cursor_uint(cursor, 2U, &count)) return false;
            break;
        case DW_FORM_BLOCK4:
            if (!dwarf_cursor_uint(cursor, 4U, &count)) return false;
            break;
        case DW_FORM_BLOCK:
        case DW_FORM_EXPRLOC:
            if (!dwarf_cursor_uleb(cursor, &count)) return false;
            break;
        case DW_FORM_DATA16: count = 16U; break;
        case DW_FORM_INDIRECT:
            if (!dwarf_cursor_uleb(cursor, &value)) return false;
            return dwarf_parse_form(dwarf, cursor, unit, value, 0, recursion + 1U, attribute);
        default: return false;
    }
    attribute->data_offset = cursor->position;
    attribute->data_size = count;
    attribute->kind = XX_ELF_DWARF_VALUE_BLOCK;
    return dwarf_cursor_bytes(cursor, count, NULL);
}

static bool dwarf_parse_unit_header(dwarf_cursor *cursor, xx_elf_dwarf_unit *unit, uint64_t *unit_end)
{
    uint64_t value;
    uint64_t length;
    uint8_t offset_size;
    if (!cursor || !unit || !unit_end) return false;
    memset(unit, 0, sizeof(*unit));
    unit->offset = cursor->position;
    unit->statement_list = UINT64_MAX;
    if (!dwarf_read_initial_length(cursor, unit_end, &offset_size, &length) || !dwarf_cursor_uint(cursor, 2U, &value) || value < 2U || value > 5U) return false;
    unit->length = length;
    unit->offset_size = offset_size;
    unit->version = (uint16_t)value;
    if (unit->version >= 5U) {
        if (!dwarf_cursor_uint(cursor, 1U, &value)) return false;
        unit->unit_type = (uint8_t)value;
        if (unit->unit_type < DW_UT_COMPILE || unit->unit_type > DW_UT_SPLIT_TYPE || !dwarf_cursor_uint(cursor, 1U, &value) ||
            (value != 1U && value != 2U && value != 4U && value != 8U))
            return false;
        unit->address_size = (uint8_t)value;
        if (!dwarf_cursor_uint(cursor, unit->offset_size, &unit->abbreviation_offset)) return false;
        if (unit->unit_type == DW_UT_TYPE || unit->unit_type == DW_UT_SPLIT_TYPE) {
            if (!dwarf_cursor_uint(cursor, 8U, &unit->type_signature) || !dwarf_cursor_uint(cursor, unit->offset_size, &unit->type_offset)) return false;
        } else if (unit->unit_type == DW_UT_SKELETON || unit->unit_type == DW_UT_SPLIT_COMPILE) {
            if (!dwarf_cursor_uint(cursor, 8U, &unit->dwo_id)) return false;
        }
    } else {
        unit->unit_type = DW_UT_COMPILE;
        if (!dwarf_cursor_uint(cursor, unit->offset_size, &unit->abbreviation_offset) || !dwarf_cursor_uint(cursor, 1U, &value) ||
            (value != 1U && value != 2U && value != 4U && value != 8U))
            return false;
        unit->address_size = (uint8_t)value;
    }
    return cursor->position <= *unit_end;
}

static void dwarf_update_unit_from_attribute(xx_elf_dwarf_unit *unit, const xx_elf_dwarf_attribute *attribute)
{
    if (!unit || !attribute) return;
    if (attribute->name == XX_DW_AT_STMT_LIST && attribute->kind == XX_ELF_DWARF_VALUE_UNSIGNED) unit->statement_list = attribute->unsigned_value;
    else if (attribute->name == XX_DW_AT_STR_OFFSETS_BASE && attribute->kind == XX_ELF_DWARF_VALUE_UNSIGNED) unit->string_offsets_base = attribute->unsigned_value;
    else if (attribute->name == XX_DW_AT_ADDR_BASE && attribute->kind == XX_ELF_DWARF_VALUE_UNSIGNED) unit->address_base = attribute->unsigned_value;
    else if (attribute->name == XX_DW_AT_RNGLISTS_BASE && attribute->kind == XX_ELF_DWARF_VALUE_UNSIGNED) unit->range_lists_base = attribute->unsigned_value;
    else if (attribute->name == XX_DW_AT_LOCLISTS_BASE && attribute->kind == XX_ELF_DWARF_VALUE_UNSIGNED) unit->location_lists_base = attribute->unsigned_value;
    else if (attribute->name == XX_DW_AT_NAME && attribute->string_value) unit->name = attribute->string_value;
    else if (attribute->name == XX_DW_AT_COMP_DIR && attribute->string_value) unit->compilation_directory = attribute->string_value;
    else if (attribute->name == XX_DW_AT_PRODUCER && attribute->string_value) unit->producer = attribute->string_value;
}

static bool dwarf_parse_information(xx_elf_dwarf *dwarf)
{
    dwarf_cursor cursor;
    if (!dwarf) return false;
    if (!dwarf->info.data) return true;
    cursor.data = dwarf->info.data;
    cursor.size = dwarf->info.size;
    cursor.position = 0U;
    cursor.big_endian = dwarf->big_endian;
    while (cursor.position < cursor.size) {
        xx_elf_dwarf_unit parsed;
        xx_elf_dwarf_unit *unit;
        uint64_t unit_end;
        uint32_t depth = 0U;
        uint32_t unit_index;
        if (!dwarf_parse_unit_header(&cursor, &parsed, &unit_end) || parsed.abbreviation_offset >= dwarf->abbrev.size ||
            !dwarf_grow((void **)&dwarf->units, &dwarf->unit_capacity, dwarf->unit_count + 1U, sizeof(*dwarf->units), DWARF_UNIT_LIMIT))
            return false;
        parsed.die_index = dwarf->die_count;
        unit_index = dwarf->unit_count;
        dwarf->units[dwarf->unit_count++] = parsed;
        unit = &dwarf->units[unit_index];
        while (cursor.position < unit_end) {
            uint64_t code;
            const xx_elf_dwarf_abbreviation *abbreviation;
            xx_elf_dwarf_die *die;
            uint32_t spec_index;
            {
                uint64_t code_offset = cursor.position;
                if (!dwarf_cursor_uleb(&cursor, &code)) return false;
                if (code == 0U) {
                    if (depth > 0U) --depth;
                    continue;
                }
                abbreviation = dwarf_find_abbreviation(dwarf, unit->abbreviation_offset, code);
                if (!abbreviation || !dwarf_grow((void **)&dwarf->dies, &dwarf->die_capacity, dwarf->die_count + 1U, sizeof(*dwarf->dies), DWARF_DIE_LIMIT)) return false;
                die = &dwarf->dies[dwarf->die_count++];
                memset(die, 0, sizeof(*die));
                die->offset = code_offset;
                die->abbreviation_code = code;
                die->tag = abbreviation->tag;
                die->unit_index = unit_index;
                die->depth = depth;
                die->has_children = abbreviation->has_children;
                die->attribute_index = dwarf->attribute_count;
                for (spec_index = 0U; spec_index < abbreviation->attribute_spec_count; ++spec_index) {
                    const xx_elf_dwarf_attribute_spec *specification = &dwarf->specifications[abbreviation->attribute_spec_index + spec_index];
                    xx_elf_dwarf_attribute *attribute;
                    if (!dwarf_grow((void **)&dwarf->attributes, &dwarf->attribute_capacity, dwarf->attribute_count + 1U, sizeof(*dwarf->attributes),
                                    DWARF_ATTRIBUTE_LIMIT))
                        return false;
                    attribute = &dwarf->attributes[dwarf->attribute_count];
                    memset(attribute, 0, sizeof(*attribute));
                    attribute->name = specification->name;
                    if (!dwarf_parse_form(dwarf, &cursor, unit, specification->form, specification->implicit_constant, 0U, attribute) || cursor.position > unit_end)
                        return false;
                    ++dwarf->attribute_count;
                    ++die->attribute_count;
                    if (die->depth == 0U) dwarf_update_unit_from_attribute(unit, attribute);
                }
                if (abbreviation->has_children) {
                    if (depth == UINT32_MAX) return false;
                    ++depth;
                }
            }
        }
        if (cursor.position != unit_end) return false;
        unit->die_count = dwarf->die_count - unit->die_index;
    }
    return true;
}

static bool dwarf_read_section_uint(const dwarf_section *section, uint64_t offset, unsigned width, bool big_endian, uint64_t *output)
{
    dwarf_cursor cursor;
    if (!section || !output || offset > section->size) return false;
    cursor.data = section->data;
    cursor.size = section->size;
    cursor.position = offset;
    cursor.big_endian = big_endian;
    return dwarf_cursor_uint(&cursor, width, output);
}

static bool dwarf_resolve_indexed_values(xx_elf_dwarf *dwarf)
{
    uint32_t die_index;
    if (!dwarf) return false;
    for (die_index = 0U; die_index < dwarf->die_count; ++die_index) {
        xx_elf_dwarf_die *die = &dwarf->dies[die_index];
        xx_elf_dwarf_unit *unit;
        uint32_t attribute_index;
        if (die->unit_index >= dwarf->unit_count) return false;
        unit = &dwarf->units[die->unit_index];
        for (attribute_index = 0U; attribute_index < die->attribute_count; ++attribute_index) {
            xx_elf_dwarf_attribute *attribute = &dwarf->attributes[die->attribute_index + attribute_index];
            if (attribute->kind == XX_ELF_DWARF_VALUE_STRING_INDEX) {
                uint64_t entry_offset;
                uint64_t string_offset;
                uint64_t index = attribute->unsigned_value;
                if (index > (UINT64_MAX - unit->string_offsets_base) / unit->offset_size) return false;
                entry_offset = unit->string_offsets_base + index * unit->offset_size;
                if (!dwarf_read_section_uint(&dwarf->string_offsets, entry_offset, unit->offset_size, dwarf->big_endian, &string_offset)) return false;
                attribute->string_value = dwarf_section_string(&dwarf->strings, string_offset);
                if (!attribute->string_value) return false;
            } else if (attribute->kind == XX_ELF_DWARF_VALUE_ADDRESS_INDEX) {
                uint64_t entry_offset;
                uint64_t address;
                uint64_t index = attribute->unsigned_value;
                if (index > (UINT64_MAX - unit->address_base) / unit->address_size) return false;
                entry_offset = unit->address_base + index * unit->address_size;
                if (!dwarf_read_section_uint(&dwarf->addresses, entry_offset, unit->address_size, dwarf->big_endian, &address)) return false;
                attribute->data_offset = index;
                attribute->unsigned_value = address;
            }
            if (die->depth == 0U) dwarf_update_unit_from_attribute(unit, attribute);
        }
    }
    return true;
}

typedef struct dwarf_line_format {
    uint64_t content;
    uint64_t form;
} dwarf_line_format;

typedef struct dwarf_line_value {
    uint64_t number;
    const char *string;
    const uint8_t *bytes;
    uint64_t size;
} dwarf_line_value;

typedef struct dwarf_line_state {
    uint64_t address;
    uint64_t operation_index;
    uint64_t file;
    int64_t line;
    uint64_t column;
    uint64_t isa;
    uint64_t discriminator;
    bool is_statement;
    bool basic_block;
    bool end_sequence;
    bool prologue_end;
    bool epilogue_begin;
} dwarf_line_state;

static const xx_elf_dwarf_unit *dwarf_unit_for_line(const xx_elf_dwarf *dwarf, uint64_t line_offset)
{
    uint32_t index;
    if (!dwarf) return NULL;
    for (index = 0U; index < dwarf->unit_count; ++index) {
        if (dwarf->units[index].statement_list == line_offset) return &dwarf->units[index];
    }
    return NULL;
}

static bool dwarf_path_is_absolute(const char *path)
{
    if (!path || !path[0]) return false;
    return path[0] == '/' || path[0] == '\\' || (path[0] != '\0' && path[1] == ':');
}

static bool dwarf_path_has_directory_prefix(const char *path, const char *directory)
{
    size_t length;
    if (!path || !directory || !directory[0]) return false;
    length = strlen(directory);
    while (length > 1U && (directory[length - 1U] == '/' || directory[length - 1U] == '\\')) --length;
    return strncmp(path, directory, length) == 0 && (path[length] == '\0' || path[length] == '/' || path[length] == '\\');
}

static char *dwarf_join_path(const char *directory, const char *name)
{
    size_t directory_length;
    size_t name_length;
    size_t total;
    bool separator;
    char *result;
    if (!name) return NULL;
    if (!directory || !directory[0] || dwarf_path_is_absolute(name)) return dwarf_strdup(name);
    directory_length = strlen(directory);
    name_length = strlen(name);
    separator = directory[directory_length - 1U] != '/' && directory[directory_length - 1U] != '\\';
    if (directory_length > SIZE_MAX - name_length - (separator ? 2U : 1U)) return NULL;
    total = directory_length + name_length + (separator ? 2U : 1U);
    result = (char *)xx_mem_alloc(total);
    if (!result) return NULL;
    memcpy(result, directory, directory_length);
    if (separator) result[directory_length++] = '/';
    memcpy(result + directory_length, name, name_length + 1U);
    return result;
}

static bool dwarf_line_form_value(xx_elf_dwarf *dwarf, dwarf_cursor *cursor, uint64_t form, uint8_t offset_size, uint8_t address_size, dwarf_line_value *output)
{
    uint64_t width = 0U;
    uint64_t value;
    const char *string;
    if (!dwarf || !cursor || !output) return false;
    memset(output, 0, sizeof(*output));
    switch (form) {
        case DW_FORM_DATA1: width = 1U; break;
        case DW_FORM_DATA2: width = 2U; break;
        case DW_FORM_DATA4: width = 4U; break;
        case DW_FORM_DATA8: width = 8U; break;
        case DW_FORM_ADDR: width = address_size; break;
        case DW_FORM_STRP:
        case DW_FORM_LINE_STRP:
        case DW_FORM_SEC_OFFSET: width = offset_size; break;
        default: break;
    }
    if (width != 0U) {
        if (!dwarf_cursor_uint(cursor, (unsigned)width, &value)) return false;
        output->number = value;
        if (form == DW_FORM_STRP) {
            output->string = dwarf_section_string(&dwarf->strings, value);
            return output->string != NULL;
        }
        if (form == DW_FORM_LINE_STRP) {
            output->string = dwarf_section_string(&dwarf->line_strings, value);
            return output->string != NULL;
        }
        return true;
    }
    switch (form) {
        case DW_FORM_STRING:
            string = dwarf_cursor_string(cursor);
            if (!string) return false;
            output->string = string;
            return true;
        case DW_FORM_UDATA: return dwarf_cursor_uleb(cursor, &output->number);
        case DW_FORM_SDATA: {
            int64_t signed_value;
            if (!dwarf_cursor_sleb(cursor, &signed_value)) return false;
            output->number = (uint64_t)signed_value;
            return true;
        }
        case DW_FORM_FLAG_PRESENT: output->number = 1U; return true;
        case DW_FORM_DATA16: output->size = 16U; return dwarf_cursor_bytes(cursor, 16U, &output->bytes);
        case DW_FORM_BLOCK1:
            if (!dwarf_cursor_uint(cursor, 1U, &output->size)) return false;
            return dwarf_cursor_bytes(cursor, output->size, &output->bytes);
        case DW_FORM_BLOCK2:
            if (!dwarf_cursor_uint(cursor, 2U, &output->size)) return false;
            return dwarf_cursor_bytes(cursor, output->size, &output->bytes);
        case DW_FORM_BLOCK4:
            if (!dwarf_cursor_uint(cursor, 4U, &output->size)) return false;
            return dwarf_cursor_bytes(cursor, output->size, &output->bytes);
        case DW_FORM_BLOCK:
            if (!dwarf_cursor_uleb(cursor, &output->size)) return false;
            return dwarf_cursor_bytes(cursor, output->size, &output->bytes);
        default: return false;
    }
}

static bool dwarf_add_line_file(xx_elf_dwarf *dwarf, uint64_t program_offset, uint16_t version, const xx_elf_dwarf_unit *unit, char *const *directories,
                                uint32_t directory_count, const char *name, uint64_t directory_index, uint64_t timestamp, uint64_t size, const uint8_t *md5)
{
    xx_elf_dwarf_line_file *file;
    const char *directory = NULL;
    char *name_copy;
    char *path;
    if (!dwarf || !name ||
        !dwarf_grow((void **)&dwarf->line_files, &dwarf->line_file_capacity, dwarf->line_file_count + 1U, sizeof(*dwarf->line_files), DWARF_LINE_FILE_LIMIT))
        return false;
    if (version >= 5U) {
        if (directory_index < directory_count) directory = directories[directory_index];
    } else if (directory_index == 0U) {
        if (unit) directory = unit->compilation_directory;
    } else if (directory_index - 1U < directory_count) {
        directory = directories[directory_index - 1U];
    }
    name_copy = dwarf_strdup(name);
    path = dwarf_join_path(directory, name);
    if (!name_copy || !path) {
        xx_mem_free(name_copy);
        xx_mem_free(path);
        return false;
    }
    file = &dwarf->line_files[dwarf->line_file_count++];
    memset(file, 0, sizeof(*file));
    file->line_program_offset = program_offset;
    file->directory_index = directory_index;
    file->timestamp = timestamp;
    file->size = size;
    file->name = name_copy;
    file->path = path;
    if (md5) {
        memcpy(file->md5, md5, sizeof(file->md5));
        file->has_md5 = true;
    }
    return true;
}

static bool dwarf_add_directory(char ***directories, uint32_t *count, uint32_t *capacity, const char *base, const char *path)
{
    char *copy;
    if (!directories || !count || !capacity || !path || !dwarf_grow((void **)directories, capacity, *count + 1U, sizeof(**directories), DWARF_DIRECTORY_LIMIT))
        return false;
    copy = dwarf_path_has_directory_prefix(path, base) ? dwarf_strdup(path) : dwarf_join_path(base, path);
    if (!copy) return false;
    (*directories)[(*count)++] = copy;
    return true;
}

static void dwarf_free_directories(char **directories, uint32_t count)
{
    uint32_t index;
    for (index = 0U; index < count; ++index) xx_mem_free(directories[index]);
    xx_mem_free(directories);
}

static void dwarf_line_state_reset(dwarf_line_state *state, bool default_is_statement)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->file = 1U;
    state->line = 1;
    state->is_statement = default_is_statement;
}

static bool dwarf_line_advance_address(dwarf_line_state *state, uint64_t operation_advance, uint8_t minimum_instruction_length, uint8_t maximum_operations)
{
    uint64_t total;
    uint64_t instruction_advance;
    uint64_t byte_advance;
    if (!state || maximum_operations == 0U || state->operation_index > UINT64_MAX - operation_advance) return false;
    total = state->operation_index + operation_advance;
    instruction_advance = total / maximum_operations;
    if (instruction_advance > UINT64_MAX / minimum_instruction_length) return false;
    byte_advance = instruction_advance * minimum_instruction_length;
    if (state->address > UINT64_MAX - byte_advance) return false;
    state->address += byte_advance;
    state->operation_index = total % maximum_operations;
    return true;
}

static bool dwarf_append_line_row(xx_elf_dwarf *dwarf, uint64_t program_offset, uint16_t version, uint32_t file_base, uint32_t local_file_count,
                                  const dwarf_line_state *state)
{
    xx_elf_dwarf_line_row *row;
    uint64_t local_index;
    if (!dwarf || !state || state->line < 0 || (uint64_t)state->line > UINT32_MAX || state->column > UINT32_MAX || state->isa > UINT32_MAX ||
        !dwarf_grow((void **)&dwarf->line_rows, &dwarf->line_row_capacity, dwarf->line_row_count + 1U, sizeof(*dwarf->line_rows), DWARF_LINE_ROW_LIMIT))
        return false;
    if (version >= 5U) local_index = state->file;
    else if (state->file != 0U) local_index = state->file - 1U;
    else local_index = UINT64_MAX;
    row = &dwarf->line_rows[dwarf->line_row_count++];
    memset(row, 0, sizeof(*row));
    row->line_program_offset = program_offset;
    row->address = state->address;
    row->discriminator = state->discriminator;
    row->file_index = local_index < local_file_count ? file_base + (uint32_t)local_index : UINT32_MAX;
    row->line = (uint32_t)state->line;
    row->column = (uint32_t)state->column;
    row->isa = (uint32_t)state->isa;
    row->is_statement = state->is_statement;
    row->basic_block = state->basic_block;
    row->end_sequence = state->end_sequence;
    row->prologue_end = state->prologue_end;
    row->epilogue_begin = state->epilogue_begin;
    return true;
}

static void dwarf_line_after_row(dwarf_line_state *state)
{
    if (!state) return;
    state->basic_block = false;
    state->prologue_end = false;
    state->epilogue_begin = false;
    state->discriminator = 0U;
}

static bool dwarf_parse_v5_table(xx_elf_dwarf *dwarf, dwarf_cursor *cursor, uint64_t program_offset, const xx_elf_dwarf_unit *unit, uint16_t version, uint8_t offset_size,
                                 uint8_t address_size, bool is_directory, char ***directories, uint32_t *directory_count, uint32_t *directory_capacity)
{
    dwarf_line_format formats[DWARF_TABLE_FORMAT_LIMIT];
    uint64_t format_count;
    uint64_t row_count;
    uint64_t row_index;
    uint64_t format_index;
    if (!dwarf_cursor_uint(cursor, 1U, &format_count) || format_count > DWARF_TABLE_FORMAT_LIMIT) return false;
    for (format_index = 0U; format_index < format_count; ++format_index) {
        if (!dwarf_cursor_uleb(cursor, &formats[format_index].content) || !dwarf_cursor_uleb(cursor, &formats[format_index].form)) return false;
    }
    if (!dwarf_cursor_uleb(cursor, &row_count) || row_count > (is_directory ? DWARF_DIRECTORY_LIMIT : DWARF_LINE_FILE_LIMIT)) return false;
    for (row_index = 0U; row_index < row_count; ++row_index) {
        const char *path = NULL;
        uint64_t directory_index = 0U;
        uint64_t timestamp = 0U;
        uint64_t size = 0U;
        uint8_t md5[16];
        bool has_md5 = false;
        memset(md5, 0, sizeof(md5));
        for (format_index = 0U; format_index < format_count; ++format_index) {
            dwarf_line_value value;
            if (!dwarf_line_form_value(dwarf, cursor, formats[format_index].form, offset_size, address_size, &value)) return false;
            switch (formats[format_index].content) {
                case DW_LNCT_PATH: path = value.string; break;
                case DW_LNCT_DIRECTORY_INDEX: directory_index = value.number; break;
                case DW_LNCT_TIMESTAMP: timestamp = value.number; break;
                case DW_LNCT_SIZE: size = value.number; break;
                case DW_LNCT_MD5:
                    if (value.size != 16U || !value.bytes) return false;
                    memcpy(md5, value.bytes, sizeof(md5));
                    has_md5 = true;
                    break;
                default: break;
            }
        }
        if (!path) return false;
        if (is_directory) {
            if (!dwarf_add_directory(directories, directory_count, directory_capacity, unit ? unit->compilation_directory : NULL, path)) return false;
        } else if (!dwarf_add_line_file(dwarf, program_offset, version, unit, *directories, *directory_count, path, directory_index, timestamp, size,
                                        has_md5 ? md5 : NULL))
            return false;
    }
    return true;
}

static bool dwarf_parse_line_program(xx_elf_dwarf *dwarf, dwarf_cursor *cursor)
{
    uint64_t program_offset = cursor->position;
    uint64_t program_end;
    uint64_t declared_length;
    uint64_t value;
    uint64_t header_length;
    uint64_t header_end;
    uint8_t offset_size;
    uint16_t version;
    uint8_t address_size;
    uint8_t segment_selector_size = 0U;
    uint8_t minimum_instruction_length;
    uint8_t maximum_operations = 1U;
    bool default_is_statement;
    int8_t line_base;
    uint8_t line_range;
    uint8_t opcode_base;
    uint8_t standard_lengths[256];
    char **directories = NULL;
    uint32_t directory_count = 0U;
    uint32_t directory_capacity = 0U;
    uint32_t file_base = dwarf->line_file_count;
    uint32_t local_file_count;
    const xx_elf_dwarf_unit *unit = dwarf_unit_for_line(dwarf, program_offset);
    dwarf_line_state state;
    bool result = false;
    uint64_t index;
    if (!dwarf_read_initial_length(cursor, &program_end, &offset_size, &declared_length) || declared_length == 0U || !dwarf_cursor_uint(cursor, 2U, &value) ||
        value < 2U || value > 5U)
        goto cleanup;
    version = (uint16_t)value;
    address_size = unit ? unit->address_size : (dwarf->elf->elf_class == XX_ELF_CLASS_64 ? 8U : 4U);
    if (version >= 5U) {
        if (!dwarf_cursor_uint(cursor, 1U, &value) || (value != 1U && value != 2U && value != 4U && value != 8U)) goto cleanup;
        address_size = (uint8_t)value;
        if (!dwarf_cursor_uint(cursor, 1U, &value) || value > 8U) goto cleanup;
        segment_selector_size = (uint8_t)value;
    }
    if (!dwarf_cursor_uint(cursor, offset_size, &header_length) || !dwarf_add_u64(cursor->position, header_length, &header_end) || header_end > program_end ||
        !dwarf_cursor_uint(cursor, 1U, &value) || value == 0U)
        goto cleanup;
    minimum_instruction_length = (uint8_t)value;
    if (version >= 4U) {
        if (!dwarf_cursor_uint(cursor, 1U, &value) || value == 0U) goto cleanup;
        maximum_operations = (uint8_t)value;
    }
    if (!dwarf_cursor_uint(cursor, 1U, &value) || value > 1U) goto cleanup;
    default_is_statement = value != 0U;
    if (!dwarf_cursor_uint(cursor, 1U, &value)) goto cleanup;
    line_base = (int8_t)(uint8_t)value;
    if (!dwarf_cursor_uint(cursor, 1U, &value) || value == 0U) goto cleanup;
    line_range = (uint8_t)value;
    if (!dwarf_cursor_uint(cursor, 1U, &value) || value == 0U || value > 255U) goto cleanup;
    opcode_base = (uint8_t)value;
    memset(standard_lengths, 0, sizeof(standard_lengths));
    for (index = 1U; index < opcode_base; ++index) {
        if (!dwarf_cursor_uint(cursor, 1U, &value) || value > UINT8_MAX) goto cleanup;
        standard_lengths[index] = (uint8_t)value;
    }
    if (version >= 5U) {
        if (!dwarf_parse_v5_table(dwarf, cursor, program_offset, unit, version, offset_size, address_size, true, &directories, &directory_count, &directory_capacity) ||
            !dwarf_parse_v5_table(dwarf, cursor, program_offset, unit, version, offset_size, address_size, false, &directories, &directory_count, &directory_capacity))
            goto cleanup;
    } else {
        while (cursor->position < header_end && cursor->data[cursor->position] != 0U) {
            const char *directory = dwarf_cursor_string(cursor);
            if (!directory || !dwarf_add_directory(&directories, &directory_count, &directory_capacity, unit ? unit->compilation_directory : NULL, directory))
                goto cleanup;
        }
        if (cursor->position >= header_end) goto cleanup;
        ++cursor->position;
        while (cursor->position < header_end && cursor->data[cursor->position] != 0U) {
            const char *name = dwarf_cursor_string(cursor);
            uint64_t directory_index;
            uint64_t timestamp;
            uint64_t size;
            if (!name || !dwarf_cursor_uleb(cursor, &directory_index) || !dwarf_cursor_uleb(cursor, &timestamp) || !dwarf_cursor_uleb(cursor, &size) ||
                !dwarf_add_line_file(dwarf, program_offset, version, unit, directories, directory_count, name, directory_index, timestamp, size, NULL))
                goto cleanup;
        }
        if (cursor->position >= header_end) goto cleanup;
        ++cursor->position;
    }
    if (cursor->position > header_end) goto cleanup;
    cursor->position = header_end;
    local_file_count = dwarf->line_file_count - file_base;
    dwarf_line_state_reset(&state, default_is_statement);
    while (cursor->position < program_end) {
        uint8_t opcode = cursor->data[cursor->position++];
        if (opcode == 0U) {
            uint64_t extended_length;
            uint64_t extended_end;
            uint8_t extended_opcode;
            if (!dwarf_cursor_uleb(cursor, &extended_length) || extended_length == 0U || !dwarf_add_u64(cursor->position, extended_length, &extended_end) ||
                extended_end > program_end || !dwarf_cursor_uint(cursor, 1U, &value))
                goto cleanup;
            extended_opcode = (uint8_t)value;
            if (extended_opcode == DW_LNE_END_SEQUENCE) {
                state.end_sequence = true;
                if (!dwarf_append_line_row(dwarf, program_offset, version, file_base, local_file_count, &state)) goto cleanup;
                dwarf_line_state_reset(&state, default_is_statement);
            } else if (extended_opcode == DW_LNE_SET_ADDRESS) {
                if (segment_selector_size != 0U && !dwarf_cursor_bytes(cursor, segment_selector_size, NULL)) goto cleanup;
                if (!dwarf_cursor_uint(cursor, address_size, &state.address)) goto cleanup;
                state.operation_index = 0U;
            } else if (extended_opcode == DW_LNE_DEFINE_FILE && version < 5U) {
                const char *name = dwarf_cursor_string(cursor);
                uint64_t directory_index;
                uint64_t timestamp;
                uint64_t size;
                if (!name || !dwarf_cursor_uleb(cursor, &directory_index) || !dwarf_cursor_uleb(cursor, &timestamp) || !dwarf_cursor_uleb(cursor, &size) ||
                    !dwarf_add_line_file(dwarf, program_offset, version, unit, directories, directory_count, name, directory_index, timestamp, size, NULL))
                    goto cleanup;
                local_file_count = dwarf->line_file_count - file_base;
            } else if (extended_opcode == DW_LNE_SET_DISCRIMINATOR) {
                if (!dwarf_cursor_uleb(cursor, &state.discriminator)) goto cleanup;
            }
            if (cursor->position > extended_end) goto cleanup;
            cursor->position = extended_end;
        } else if (opcode < opcode_base) {
            switch (opcode) {
                case DW_LNS_COPY:
                    if (!dwarf_append_line_row(dwarf, program_offset, version, file_base, local_file_count, &state)) goto cleanup;
                    dwarf_line_after_row(&state);
                    break;
                case DW_LNS_ADVANCE_PC:
                    if (!dwarf_cursor_uleb(cursor, &value) || !dwarf_line_advance_address(&state, value, minimum_instruction_length, maximum_operations)) goto cleanup;
                    break;
                case DW_LNS_ADVANCE_LINE: {
                    int64_t delta;
                    if (!dwarf_cursor_sleb(cursor, &delta) || (delta > 0 && state.line > INT64_MAX - delta) || (delta < 0 && state.line < INT64_MIN - delta))
                        goto cleanup;
                    state.line += delta;
                    break;
                }
                case DW_LNS_SET_FILE:
                    if (!dwarf_cursor_uleb(cursor, &state.file)) goto cleanup;
                    break;
                case DW_LNS_SET_COLUMN:
                    if (!dwarf_cursor_uleb(cursor, &state.column)) goto cleanup;
                    break;
                case DW_LNS_NEGATE_STMT: state.is_statement = !state.is_statement; break;
                case DW_LNS_SET_BASIC_BLOCK: state.basic_block = true; break;
                case DW_LNS_CONST_ADD_PC:
                    value = (UINT8_MAX - opcode_base) / line_range;
                    if (!dwarf_line_advance_address(&state, value, minimum_instruction_length, maximum_operations)) goto cleanup;
                    break;
                case DW_LNS_FIXED_ADVANCE_PC:
                    if (!dwarf_cursor_uint(cursor, 2U, &value) || state.address > UINT64_MAX - value) goto cleanup;
                    state.address += value;
                    state.operation_index = 0U;
                    break;
                case DW_LNS_SET_PROLOGUE_END: state.prologue_end = true; break;
                case DW_LNS_SET_EPILOGUE_BEGIN: state.epilogue_begin = true; break;
                case DW_LNS_SET_ISA:
                    if (!dwarf_cursor_uleb(cursor, &state.isa)) goto cleanup;
                    break;
                default:
                    for (index = 0U; index < standard_lengths[opcode]; ++index) {
                        if (!dwarf_cursor_uleb(cursor, &value)) goto cleanup;
                    }
                    break;
            }
        } else {
            uint8_t adjusted = (uint8_t)(opcode - opcode_base);
            uint64_t operation_advance = adjusted / line_range;
            int64_t line_increment = (int64_t)line_base + (adjusted % line_range);
            if (!dwarf_line_advance_address(&state, operation_advance, minimum_instruction_length, maximum_operations) ||
                (line_increment > 0 && state.line > INT64_MAX - line_increment) || (line_increment < 0 && state.line < INT64_MIN - line_increment))
                goto cleanup;
            state.line += line_increment;
            if (!dwarf_append_line_row(dwarf, program_offset, version, file_base, local_file_count, &state)) goto cleanup;
            dwarf_line_after_row(&state);
        }
    }
    if (cursor->position != program_end) goto cleanup;
    result = true;
cleanup:
    dwarf_free_directories(directories, directory_count);
    return result;
}

static bool dwarf_parse_lines(xx_elf_dwarf *dwarf)
{
    dwarf_cursor cursor;
    if (!dwarf) return false;
    if (!dwarf->line.data) return true;
    cursor.data = dwarf->line.data;
    cursor.size = dwarf->line.size;
    cursor.position = 0U;
    cursor.big_endian = dwarf->big_endian;
    while (cursor.position < cursor.size) {
        if (!dwarf_parse_line_program(dwarf, &cursor)) return false;
    }
    return true;
}

static uint64_t dwarf_hash_u64(uint64_t value)
{
    value ^= value >> 30U;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27U;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
}

static uint64_t dwarf_hash_string(const char *string)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    const unsigned char *cursor = (const unsigned char *)string;
    while (cursor && *cursor) {
        hash ^= *cursor++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool dwarf_hash_capacity(uint64_t candidates, uint32_t *capacity)
{
    uint64_t needed;
    uint32_t result = 16U;
    if (!capacity || candidates > UINT32_MAX / 2U) return false;
    needed = candidates ? candidates * 2U : 1U;
    while ((uint64_t)result < needed) {
        if (result > UINT32_MAX / 2U) return false;
        result *= 2U;
    }
    *capacity = result;
    return true;
}

static bool dwarf_build_address_line_rows(xx_elf_dwarf *dwarf)
{
    uint32_t *slots;
    uint32_t capacity;
    uint32_t mask;
    uint32_t index;
    if (!dwarf) return false;
    if (dwarf->line_row_count == 0U) return true;
    if (!dwarf_hash_capacity(dwarf->line_row_count, &capacity)) return false;
    dwarf->address_line_row_indices = (uint32_t *)xx_mem_alloc((size_t)dwarf->line_row_count * sizeof(*dwarf->address_line_row_indices));
    slots = (uint32_t *)xx_mem_alloc((size_t)capacity * sizeof(*slots));
    if (!dwarf->address_line_row_indices || !slots) {
        xx_mem_free(slots);
        return false;
    }
    memset(slots, 0, (size_t)capacity * sizeof(*slots));
    mask = capacity - 1U;
    for (index = 0U; index < dwarf->line_row_count; ++index) {
        const xx_elf_dwarf_line_row *row = &dwarf->line_rows[index];
        uint32_t slot = (uint32_t)dwarf_hash_u64(row->address) & mask;
        while (slots[slot] != 0U && dwarf->line_rows[slots[slot] - 1U].address != row->address) {
            slot = (slot + 1U) & mask;
        }
        if (slots[slot] != 0U && dwarf->line_rows[slots[slot] - 1U].line == row->line) continue;
        if (slots[slot] == 0U) slots[slot] = index + 1U;
        dwarf->address_line_row_indices[dwarf->address_line_row_count++] = index;
    }
    xx_mem_free(slots);
    return true;
}

static bool dwarf_add_source_file(xx_elf_dwarf *dwarf, uint32_t *slots, uint32_t mask, const char *path)
{
    uint32_t slot;
    char *copy;
    if (!dwarf || !slots || !path || !path[0]) return true;
    slot = (uint32_t)dwarf_hash_string(path) & mask;
    while (slots[slot] != 0U) {
        if (strcmp(dwarf->source_files[slots[slot] - 1U], path) == 0) return true;
        slot = (slot + 1U) & mask;
    }
    if (!dwarf_grow((void **)&dwarf->source_files, &dwarf->source_file_capacity, dwarf->source_file_count + 1U, sizeof(*dwarf->source_files), DWARF_SOURCE_FILE_LIMIT))
        return false;
    copy = dwarf_strdup(path);
    if (!copy) return false;
    dwarf->source_files[dwarf->source_file_count] = copy;
    slots[slot] = ++dwarf->source_file_count;
    return true;
}

static bool dwarf_build_source_files(xx_elf_dwarf *dwarf)
{
    uint64_t candidates;
    uint32_t capacity;
    uint32_t *slots;
    uint32_t index;
    if (!dwarf) return false;
    candidates = (uint64_t)dwarf->unit_count + dwarf->line_file_count;
    if (candidates == 0U) return true;
    if (!dwarf_hash_capacity(candidates, &capacity)) return false;
    slots = (uint32_t *)xx_mem_alloc((size_t)capacity * sizeof(*slots));
    if (!slots) return false;
    memset(slots, 0, (size_t)capacity * sizeof(*slots));
    for (index = 0U; index < dwarf->unit_count; ++index) {
        const xx_elf_dwarf_unit *unit = &dwarf->units[index];
        char *path;
        bool added;
        if (!unit->name) continue;
        path = dwarf_join_path(unit->compilation_directory, unit->name);
        if (!path) {
            xx_mem_free(slots);
            return false;
        }
        added = dwarf_add_source_file(dwarf, slots, capacity - 1U, path);
        xx_mem_free(path);
        if (!added) {
            xx_mem_free(slots);
            return false;
        }
    }
    for (index = 0U; index < dwarf->address_line_row_count; ++index) {
        const xx_elf_dwarf_line_row *row = &dwarf->line_rows[dwarf->address_line_row_indices[index]];
        if (row->file_index != UINT32_MAX && row->file_index < dwarf->line_file_count &&
            !dwarf_add_source_file(dwarf, slots, capacity - 1U, dwarf->line_files[row->file_index].path)) {
            xx_mem_free(slots);
            return false;
        }
    }
    xx_mem_free(slots);
    return true;
}

xx_elf_dwarf *xx_elf_dwarf_create(const xx_elf *elf, xx_pd_struct *pd)
{
    xx_elf_dwarf *dwarf;
    if (!elf || !elf->format.device || !elf->format.is_valid || !elf->format.base_info_handled || !elf->section_headers || xx_pd_is_stopped(pd)) return NULL;
    dwarf = (xx_elf_dwarf *)xx_mem_alloc(sizeof(*dwarf));
    if (!dwarf) return NULL;
    memset(dwarf, 0, sizeof(*dwarf));
    dwarf->elf = elf;
    dwarf->big_endian = elf->data_encoding == XX_ELF_DATA_MSB;
    if (!dwarf_load_section(elf, ".debug_info", &dwarf->info) || !dwarf_load_section(elf, ".debug_abbrev", &dwarf->abbrev) ||
        !dwarf_load_section(elf, ".debug_str", &dwarf->strings) || !dwarf_load_section(elf, ".debug_line", &dwarf->line) ||
        !dwarf_load_section(elf, ".debug_line_str", &dwarf->line_strings) || !dwarf_load_section(elf, ".debug_str_offsets", &dwarf->string_offsets) ||
        !dwarf_load_section(elf, ".debug_addr", &dwarf->addresses) || (dwarf->info.data != NULL && dwarf->abbrev.data == NULL) || !dwarf_parse_abbreviations(dwarf) ||
        !dwarf_parse_information(dwarf) || !dwarf_resolve_indexed_values(dwarf) || xx_pd_is_stopped(pd)) {
        xx_elf_dwarf_free(dwarf);
        return NULL;
    }
    dwarf->line_table_valid = dwarf_parse_lines(dwarf);
    if (!dwarf->line_table_valid) {
        uint32_t index;
        for (index = 0U; index < dwarf->line_file_count; ++index) {
            xx_mem_free((void *)dwarf->line_files[index].name);
            xx_mem_free((void *)dwarf->line_files[index].path);
        }
        xx_mem_free(dwarf->line_rows);
        xx_mem_free(dwarf->line_files);
        dwarf->line_rows = NULL;
        dwarf->line_files = NULL;
        dwarf->line_row_count = 0U;
        dwarf->line_file_count = 0U;
        dwarf->line_row_capacity = 0U;
        dwarf->line_file_capacity = 0U;
    }
    if (!dwarf_build_address_line_rows(dwarf) || !dwarf_build_source_files(dwarf)) {
        xx_elf_dwarf_free(dwarf);
        return NULL;
    }
    return dwarf;
}

void xx_elf_dwarf_free(xx_elf_dwarf *dwarf)
{
    uint32_t index;
    if (!dwarf) return;
    for (index = 0U; index < dwarf->source_file_count; ++index) xx_mem_free(dwarf->source_files[index]);
    for (index = 0U; index < dwarf->line_file_count; ++index) {
        xx_mem_free((void *)dwarf->line_files[index].name);
        xx_mem_free((void *)dwarf->line_files[index].path);
    }
    xx_mem_free(dwarf->source_files);
    xx_mem_free(dwarf->address_line_row_indices);
    xx_mem_free(dwarf->line_rows);
    xx_mem_free(dwarf->line_files);
    xx_mem_free(dwarf->attributes);
    xx_mem_free(dwarf->dies);
    xx_mem_free(dwarf->specifications);
    xx_mem_free(dwarf->abbreviations);
    xx_mem_free(dwarf->units);
    xx_mem_free(dwarf->addresses.data);
    xx_mem_free(dwarf->string_offsets.data);
    xx_mem_free(dwarf->line_strings.data);
    xx_mem_free(dwarf->line.data);
    xx_mem_free(dwarf->strings.data);
    xx_mem_free(dwarf->abbrev.data);
    xx_mem_free(dwarf->info.data);
    xx_mem_free(dwarf);
}

bool xx_elf_dwarf_has_valid_line_table(const xx_elf_dwarf *dwarf)
{
    return dwarf && dwarf->line_table_valid;
}

uint64_t xx_elf_dwarf_get_number_of_units(const xx_elf_dwarf *dwarf)
{
    return dwarf ? dwarf->unit_count : 0U;
}

const xx_elf_dwarf_unit *xx_elf_dwarf_get_unit(const xx_elf_dwarf *dwarf, uint64_t index)
{
    return dwarf && index < dwarf->unit_count ? &dwarf->units[index] : NULL;
}

uint64_t xx_elf_dwarf_get_number_of_abbreviations(const xx_elf_dwarf *dwarf)
{
    return dwarf ? dwarf->abbreviation_count : 0U;
}

const xx_elf_dwarf_abbreviation *xx_elf_dwarf_get_abbreviation(const xx_elf_dwarf *dwarf, uint64_t index)
{
    return dwarf && index < dwarf->abbreviation_count ? &dwarf->abbreviations[index] : NULL;
}

const xx_elf_dwarf_attribute_spec *xx_elf_dwarf_get_abbreviation_attribute(const xx_elf_dwarf *dwarf, const xx_elf_dwarf_abbreviation *abbreviation, uint64_t index)
{
    if (!dwarf || !abbreviation || index >= abbreviation->attribute_spec_count || abbreviation->attribute_spec_index > dwarf->specification_count ||
        index >= dwarf->specification_count - abbreviation->attribute_spec_index)
        return NULL;
    return &dwarf->specifications[abbreviation->attribute_spec_index + (uint32_t)index];
}

uint64_t xx_elf_dwarf_get_number_of_dies(const xx_elf_dwarf *dwarf)
{
    return dwarf ? dwarf->die_count : 0U;
}

const xx_elf_dwarf_die *xx_elf_dwarf_get_die(const xx_elf_dwarf *dwarf, uint64_t index)
{
    return dwarf && index < dwarf->die_count ? &dwarf->dies[index] : NULL;
}

const xx_elf_dwarf_attribute *xx_elf_dwarf_get_die_attribute(const xx_elf_dwarf *dwarf, const xx_elf_dwarf_die *die, uint64_t index)
{
    if (!dwarf || !die || index >= die->attribute_count || die->attribute_index > dwarf->attribute_count || index >= dwarf->attribute_count - die->attribute_index)
        return NULL;
    return &dwarf->attributes[die->attribute_index + (uint32_t)index];
}

const xx_elf_dwarf_attribute *xx_elf_dwarf_find_die_attribute(const xx_elf_dwarf *dwarf, const xx_elf_dwarf_die *die, uint64_t name)
{
    uint32_t index;
    if (!dwarf || !die) return NULL;
    for (index = 0U; index < die->attribute_count; ++index) {
        const xx_elf_dwarf_attribute *attribute = xx_elf_dwarf_get_die_attribute(dwarf, die, index);
        if (attribute && attribute->name == name) return attribute;
    }
    return NULL;
}

const uint8_t *xx_elf_dwarf_get_attribute_block(const xx_elf_dwarf *dwarf, const xx_elf_dwarf_attribute *attribute)
{
    if (!dwarf || !attribute || attribute->kind != XX_ELF_DWARF_VALUE_BLOCK || attribute->data_offset > dwarf->info.size ||
        attribute->data_size > dwarf->info.size - attribute->data_offset)
        return NULL;
    return dwarf->info.data + attribute->data_offset;
}

const xx_elf_dwarf_die *xx_elf_dwarf_find_die(const xx_elf_dwarf *dwarf, uint64_t tag, const char *name, uint64_t start_index)
{
    uint64_t index;
    if (!dwarf) return NULL;
    for (index = start_index; index < dwarf->die_count; ++index) {
        const xx_elf_dwarf_die *die = &dwarf->dies[index];
        const xx_elf_dwarf_attribute *attribute;
        if (tag != 0U && die->tag != tag) continue;
        if (!name) return die;
        attribute = xx_elf_dwarf_find_die_attribute(dwarf, die, XX_DW_AT_NAME);
        if (attribute && attribute->string_value && strcmp(attribute->string_value, name) == 0) return die;
    }
    return NULL;
}

uint64_t xx_elf_dwarf_get_number_of_line_files(const xx_elf_dwarf *dwarf)
{
    return dwarf ? dwarf->line_file_count : 0U;
}

const xx_elf_dwarf_line_file *xx_elf_dwarf_get_line_file(const xx_elf_dwarf *dwarf, uint64_t index)
{
    return dwarf && index < dwarf->line_file_count ? &dwarf->line_files[index] : NULL;
}

uint64_t xx_elf_dwarf_get_number_of_line_rows(const xx_elf_dwarf *dwarf)
{
    return dwarf ? dwarf->line_row_count : 0U;
}

const xx_elf_dwarf_line_row *xx_elf_dwarf_get_line_row(const xx_elf_dwarf *dwarf, uint64_t index)
{
    return dwarf && index < dwarf->line_row_count ? &dwarf->line_rows[index] : NULL;
}

uint64_t xx_elf_dwarf_get_number_of_address_line_rows(const xx_elf_dwarf *dwarf)
{
    return dwarf ? dwarf->address_line_row_count : 0U;
}

const xx_elf_dwarf_line_row *xx_elf_dwarf_get_address_line_row(const xx_elf_dwarf *dwarf, uint64_t index)
{
    if (!dwarf || index >= dwarf->address_line_row_count) return NULL;
    return &dwarf->line_rows[dwarf->address_line_row_indices[index]];
}

uint64_t xx_elf_dwarf_get_number_of_source_files(const xx_elf_dwarf *dwarf)
{
    return dwarf ? dwarf->source_file_count : 0U;
}

const char *xx_elf_dwarf_get_source_file(const xx_elf_dwarf *dwarf, uint64_t index)
{
    return dwarf && index < dwarf->source_file_count ? dwarf->source_files[index] : NULL;
}

const xx_elf_dwarf_line_row *xx_elf_dwarf_find_line_row(const xx_elf_dwarf *dwarf, uint64_t address, uint64_t address_bias)
{
    const xx_elf_dwarf_line_row *best = NULL;
    uint64_t best_address = 0U;
    uint32_t index;
    if (!dwarf) return NULL;
    for (index = 0U; index < dwarf->line_row_count; ++index) {
        const xx_elf_dwarf_line_row *row = &dwarf->line_rows[index];
        uint64_t relocated;
        if (row->address > UINT64_MAX - address_bias) continue;
        relocated = row->address + address_bias;
        if (!row->end_sequence && relocated <= address && (!best || relocated >= best_address)) {
            best = row;
            best_address = relocated;
        }
    }
    return best;
}
