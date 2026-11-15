/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <xxfclib/formats/executable/elf/xx_elf_yx.h>

#include <xxfclib/algo/md5/xx_md5.h>
#include <xxfclib/algo/tlsh/xx_tlsh.h>
#include <xxfclib/strings/xx_string_casefold.h>

#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    bool little;
    bool is64;
    uint64_t type;
    uint64_t machine;
    uint64_t osabi;
    uint64_t entry_raw;
    uint64_t ph_offset;
    uint64_t sh_offset;
    uint64_t ph_entry_size;
    uint64_t sh_entry_size;
    uint64_t ph_count;
    uint64_t sh_count;
    bool ph_valid;
    bool sh_valid;
} xx_elf_yx_header;

typedef struct {
    uint64_t type;
    uint64_t offset;
    uint64_t address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t link;
} xx_elf_yx_table_entry;

static bool xx_elf_yx_span(const xx_elf_yx_header *elf, uint64_t offset,
                         size_t length, const uint8_t **out)
{
    if (offset > elf->size || length > elf->size - (size_t) offset)
        return false;
    if (out) *out = elf->data + (size_t) offset;
    return true;
}

static bool xx_elf_yx_read(const xx_elf_yx_header *elf, uint64_t offset,
                         unsigned width, uint64_t *value)
{
    const uint8_t *at;
    uint64_t number = 0;
    if (!xx_elf_yx_span(elf, offset, width, &at)) return false;
    if (elf->little) {
        for (unsigned i = 0; i < width; ++i)
            number |= (uint64_t) at[i] << (8 * i);
    } else {
        for (unsigned i = 0; i < width; ++i)
            number = (number << 8) | at[i];
    }
    *value = number;
    return true;
}

static bool xx_elf_yx_table_valid(const xx_elf_yx_header *elf, uint64_t offset,
                                uint64_t count, size_t width)
{
    return offset <= elf->size &&
           count <= (elf->size - (size_t) offset) / width;
}

static bool xx_elf_yx_parse(const uint8_t *data, size_t size, xx_elf_yx_header *elf)
{
    unsigned width;
    uint64_t fields[13];
    static const unsigned offsets32[] = {
        16, 18, 20, 24, 28, 32, 36, 40, 42, 44, 46, 48, 50
    };
    static const unsigned offsets64[] = {
        16, 18, 20, 24, 32, 40, 48, 52, 54, 56, 58, 60, 62
    };
    const unsigned *offsets;
    if (!elf || !data || size < 16 || memcmp(data, "\177ELF", 4) != 0 ||
        (data[4] != 1 && data[4] != 2) ||
        (data[5] != 1 && data[5] != 2))
        return false;
    memset(elf, 0, sizeof(*elf));
    elf->data = data;
    elf->size = size;
    elf->little = data[5] == 1;
    elf->is64 = data[4] == 2;
    elf->osabi = data[7];
    offsets = elf->is64 ? offsets64 : offsets32;
    for (size_t i = 0; i < 13; ++i) {
        if (i == 0 || i == 1 || i >= 7) width = 2;
        else if (i == 2 || i == 6) width = 4;
        else width = elf->is64 ? 8 : 4;
        if (!xx_elf_yx_read(elf, offsets[i], width, &fields[i])) return false;
    }
    elf->type = fields[0];
    elf->machine = fields[1];
    elf->entry_raw = fields[3];
    elf->ph_offset = fields[4];
    elf->sh_offset = fields[5];
    elf->ph_entry_size = fields[8];
    elf->ph_count = fields[9];
    elf->sh_entry_size = fields[10];
    elf->sh_count = fields[11];
    elf->ph_valid = xx_elf_yx_table_valid(elf, elf->ph_offset, elf->ph_count,
                                     elf->is64 ? 56 : 32);
    elf->sh_valid = xx_elf_yx_table_valid(elf, elf->sh_offset, elf->sh_count,
                                     elf->is64 ? 64 : 40);
    return true;
}

static bool xx_elf_yx_program(const xx_elf_yx_header *elf, uint64_t index,
                            xx_elf_yx_table_entry *entry)
{
    uint64_t at;
    size_t width = elf->is64 ? 56 : 32;
    if (!elf->ph_valid || index >= elf->ph_count ||
        index > (UINT64_MAX - elf->ph_offset) / width)
        return false;
    at = elf->ph_offset + index * width;
    memset(entry, 0, sizeof(*entry));
    return xx_elf_yx_read(elf, at, 4, &entry->type) &&
           xx_elf_yx_read(elf, at + (elf->is64 ? 8 : 4), elf->is64 ? 8 : 4,
                       &entry->offset) &&
           xx_elf_yx_read(elf, at + (elf->is64 ? 16 : 8), elf->is64 ? 8 : 4,
                       &entry->address) &&
           xx_elf_yx_read(elf, at + (elf->is64 ? 32 : 16), elf->is64 ? 8 : 4,
                       &entry->file_size) &&
           xx_elf_yx_read(elf, at + (elf->is64 ? 40 : 20), elf->is64 ? 8 : 4,
                       &entry->memory_size);
}

static bool xx_elf_yx_section(const xx_elf_yx_header *elf, uint64_t index,
                            xx_elf_yx_table_entry *entry)
{
    uint64_t at;
    size_t width = elf->is64 ? 64 : 40;
    if (!elf->sh_valid || index >= elf->sh_count ||
        index > (UINT64_MAX - elf->sh_offset) / width)
        return false;
    at = elf->sh_offset + index * width;
    memset(entry, 0, sizeof(*entry));
    return xx_elf_yx_read(elf, at + 4, 4, &entry->type) &&
           xx_elf_yx_read(elf, at + (elf->is64 ? 16 : 12), elf->is64 ? 8 : 4,
                       &entry->address) &&
           xx_elf_yx_read(elf, at + (elf->is64 ? 24 : 16), elf->is64 ? 8 : 4,
                       &entry->offset) &&
           xx_elf_yx_read(elf, at + (elf->is64 ? 32 : 20), elf->is64 ? 8 : 4,
                       &entry->file_size) &&
           xx_elf_yx_read(elf, at + (elf->is64 ? 40 : 24), 4, &entry->link);
}

static bool xx_elf_yx_entry_point(const xx_elf_yx_header *elf, uint64_t *value)
{
    xx_elf_yx_table_entry entry;
    if (!elf->entry_raw || elf->sh_count >= 0xff00) return false;
    if (elf->type == 2 || elf->type == 3) {
        if (!elf->ph_valid) return false;
        for (uint64_t i = 0; i < elf->ph_count; ++i) {
            if (!xx_elf_yx_program(elf, i, &entry) ||
                entry.address > UINT64_MAX - entry.memory_size)
                return false;
            if (elf->entry_raw >= entry.address &&
                elf->entry_raw - entry.address < entry.memory_size) {
                uint64_t delta = elf->entry_raw - entry.address;
                if (entry.offset > UINT64_MAX - delta) return false;
                *value = entry.offset + delta;
                return true;
            }
        }
    } else {
        if (!elf->sh_valid) return false;
        for (uint64_t i = 0; i < elf->sh_count; ++i) {
            if (!xx_elf_yx_section(elf, i, &entry)) return false;
            if (entry.type == 0 || entry.type == 8) continue;
            if (entry.address > UINT64_MAX - entry.file_size) return false;
            if (elf->entry_raw >= entry.address &&
                elf->entry_raw - entry.address < entry.file_size) {
                uint64_t delta = elf->entry_raw - entry.address;
                if (entry.offset > UINT64_MAX - delta) return false;
                *value = entry.offset + delta;
                return true;
            }
        }
    }
    return false;
}

static uint64_t xx_elf_yx_dynamic_count(const xx_elf_yx_header *elf)
{
    xx_elf_yx_table_entry entry;
    uint64_t count = 0;
    unsigned word = elf->is64 ? 8 : 4;
    if (!elf->ph_valid) return 0;
    for (uint64_t i = 0; i < elf->ph_count; ++i) {
        uint64_t position, end;
        if (!xx_elf_yx_program(elf, i, &entry) || entry.type != 2 ||
            entry.offset > elf->size ||
            entry.file_size > elf->size - (size_t) entry.offset)
            continue;
        position = entry.offset;
        end = position + entry.file_size;
        while (end - position >= word * 2) {
            uint64_t tag;
            if (!xx_elf_yx_read(elf, position, word, &tag) || tag == 0) break;
            ++count;
            position += word * 2;
        }
    }
    return count;
}

static uint64_t xx_elf_yx_symbol_count(const xx_elf_yx_header *elf, uint32_t type,
                                     xx_elf_yx_table_entry *table)
{
    xx_elf_yx_table_entry entry;
    size_t width = elf->is64 ? 24 : 16;
    if (!elf->sh_valid || elf->sh_count >= 0xff00) return 0;
    for (uint64_t i = 0; i < elf->sh_count; ++i) {
        if (!xx_elf_yx_section(elf, i, &entry)) return 0;
        if (entry.type != type) continue;
        if (table) *table = entry;
        if (entry.offset > elf->size ||
            entry.file_size > elf->size - (size_t) entry.offset)
            return 0;
        return entry.file_size / width;
    }
    return 0;
}

XXFC_API bool xx_elf_yx_field_supported(const char *member)
{
    static const char *const fields[] = {
        "type", "machine", "osabi", "entry_point", "sh_offset",
        "sh_entry_size", "ph_offset", "ph_entry_size",
        "number_of_sections", "number_of_segments", "symtab_entries",
        "dynsym_entries", "dynamic_section_entries"
    };
    if (!member) return false;
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i)
        if (strcmp(member, fields[i]) == 0) return true;
    return false;
}

XXFC_API bool xx_elf_yx_field(const uint8_t *data, size_t size, const char *member,
                  uint64_t *value)
{
    xx_elf_yx_header elf;
    if (!member || !value || !xx_elf_yx_parse(data, size, &elf)) return false;
    if (strcmp(member, "type") == 0) *value = elf.type;
    else if (strcmp(member, "machine") == 0) *value = elf.machine;
    else if (strcmp(member, "osabi") == 0) *value = elf.osabi;
    else if (strcmp(member, "entry_point") == 0)
        return xx_elf_yx_entry_point(&elf, value);
    else if (strcmp(member, "sh_offset") == 0) *value = elf.sh_offset;
    else if (strcmp(member, "sh_entry_size") == 0) *value = elf.sh_entry_size;
    else if (strcmp(member, "ph_offset") == 0) *value = elf.ph_offset;
    else if (strcmp(member, "ph_entry_size") == 0) *value = elf.ph_entry_size;
    else if (strcmp(member, "number_of_sections") == 0) *value = elf.sh_count;
    else if (strcmp(member, "number_of_segments") == 0) *value = elf.ph_count;
    else if (strcmp(member, "dynamic_section_entries") == 0)
        *value = xx_elf_yx_dynamic_count(&elf);
    else if (strcmp(member, "symtab_entries") == 0) {
        if (!elf.sh_valid || elf.sh_count >= 0xff00) return false;
        *value = xx_elf_yx_symbol_count(&elf, 2, NULL);
    } else if (strcmp(member, "dynsym_entries") == 0) {
        if (!elf.sh_valid || elf.sh_count >= 0xff00) return false;
        *value = xx_elf_yx_symbol_count(&elf, 11, NULL);
    } else return false;
    return true;
}

XXFC_API bool xx_elf_yx_collection_count(const uint8_t *data, size_t size,
                              const char *member, uint64_t *count)
{
    xx_elf_yx_header elf;
    if (!member || !count || (!data && size)) return false;
    *count = 0;
    if (strcmp(member, "sections") != 0 &&
        strcmp(member, "segments") != 0 &&
        strcmp(member, "symtab") != 0 &&
        strcmp(member, "dynsym") != 0 &&
        strcmp(member, "dynamic") != 0) return false;
    if (!xx_elf_yx_parse(data, size, &elf)) return true;
    if (strcmp(member, "sections") == 0)
        *count = elf.sh_valid && elf.sh_count < 0xff00 ? elf.sh_count : 0;
    else if (strcmp(member, "segments") == 0)
        *count = elf.ph_valid ? elf.ph_count : 0;
    else if (strcmp(member, "symtab") == 0)
        *count = xx_elf_yx_symbol_count(&elf, 2, NULL);
    else if (strcmp(member, "dynsym") == 0)
        *count = xx_elf_yx_symbol_count(&elf, 11, NULL);
    else *count = xx_elf_yx_dynamic_count(&elf);
    return true;
}

XXFC_API bool xx_elf_yx_indexed_field_supported(const char *collection,
                                     const char *member)
{
    if (!collection || !member) return false;
    if (strcmp(collection, "sections") == 0)
        return strcmp(member, "type") == 0 ||
               strcmp(member, "flags") == 0 ||
               strcmp(member, "address") == 0 ||
               strcmp(member, "size") == 0 ||
               strcmp(member, "offset") == 0;
    if (strcmp(collection, "segments") == 0)
        return strcmp(member, "type") == 0 ||
               strcmp(member, "flags") == 0 ||
               strcmp(member, "offset") == 0 ||
               strcmp(member, "virtual_address") == 0 ||
               strcmp(member, "physical_address") == 0 ||
               strcmp(member, "file_size") == 0 ||
               strcmp(member, "memory_size") == 0 ||
               strcmp(member, "alignment") == 0;
    if (strcmp(collection, "symtab") == 0 ||
        strcmp(collection, "dynsym") == 0)
        return strcmp(member, "value") == 0 ||
               strcmp(member, "size") == 0 ||
               strcmp(member, "type") == 0 ||
               strcmp(member, "bind") == 0 ||
               strcmp(member, "shndx") == 0 ||
               strcmp(member, "visibility") == 0;
    if (strcmp(collection, "dynamic") == 0)
        return strcmp(member, "type") == 0 ||
               strcmp(member, "val") == 0;
    return false;
}

static bool xx_elf_yx_symbol_at(const xx_elf_yx_header *elf,
                              const char *collection, uint64_t index,
                              uint64_t *at, xx_elf_yx_table_entry *table)
{
    xx_elf_yx_table_entry selected;
    uint64_t count = xx_elf_yx_symbol_count(
        elf, strcmp(collection, "dynsym") == 0 ? 11 : 2, &selected);
    size_t width = elf->is64 ? 24 : 16;
    if (index >= count) return false;
    *at = selected.offset + index * width;
    if (table) *table = selected;
    return true;
}

static bool xx_elf_yx_dynamic_at(const xx_elf_yx_header *elf, uint64_t index,
                               uint64_t *at)
{
    xx_elf_yx_table_entry entry;
    unsigned word = elf->is64 ? 8 : 4;
    if (!elf->ph_valid) return false;
    for (uint64_t i = 0; i < elf->ph_count; ++i) {
        uint64_t position, end;
        if (!xx_elf_yx_program(elf, i, &entry) || entry.type != 2 ||
            entry.offset > elf->size ||
            entry.file_size > elf->size - (size_t)entry.offset)
            continue;
        position = entry.offset;
        end = position + entry.file_size;
        while (end - position >= word * 2) {
            uint64_t tag;
            if (!xx_elf_yx_read(elf, position, word, &tag) || tag == 0) break;
            if (index == 0) { *at = position; return true; }
            --index;
            position += word * 2;
        }
    }
    return false;
}

/* Protobuf stores ELF enum values as signed i32. The on-disk section and
 * segment types are u32, while dynamic tags can be 64-bit; both are narrowed
 * to i32 by the reference parser before rules observe them. */
static uint64_t xx_elf_yx_enum_i32(uint64_t raw)
{
    uint32_t bits = (uint32_t)raw;
    int64_t signed_value = bits <= INT32_MAX ? (int64_t)bits :
                           (int64_t)bits - INT64_C(4294967296);
    return (uint64_t)signed_value;
}

XXFC_API bool xx_elf_yx_indexed_field(const uint8_t *data, size_t size,
                           const char *collection, uint64_t index,
                           const char *member, uint64_t *value)
{
    xx_elf_yx_header elf;
    uint64_t at;
    unsigned field_offset, field_width;
    bool section;
    if (!value || !xx_elf_yx_indexed_field_supported(collection, member) ||
        !xx_elf_yx_parse(data, size, &elf)) return false;
    if (strcmp(collection, "symtab") == 0 ||
        strcmp(collection, "dynsym") == 0) {
        uint64_t info, other;
        if (!xx_elf_yx_symbol_at(&elf, collection, index, &at, NULL))
            return false;
        if (strcmp(member, "value") == 0)
            return xx_elf_yx_read(&elf, at + (elf.is64 ? 8 : 4),
                               elf.is64 ? 8 : 4, value);
        if (strcmp(member, "size") == 0)
            return xx_elf_yx_read(&elf, at + (elf.is64 ? 16 : 8),
                               elf.is64 ? 8 : 4, value);
        if (strcmp(member, "shndx") == 0)
            return xx_elf_yx_read(&elf, at + (elf.is64 ? 6 : 14), 2,
                               value);
        if (!xx_elf_yx_read(&elf, at + (elf.is64 ? 4 : 12), 1, &info) ||
            !xx_elf_yx_read(&elf, at + (elf.is64 ? 5 : 13), 1, &other))
            return false;
        if (strcmp(member, "type") == 0) *value = info & 0x0f;
        else if (strcmp(member, "bind") == 0) *value = info >> 4;
        else *value = other & 3;
        return true;
    }
    if (strcmp(collection, "dynamic") == 0) {
        unsigned word = elf.is64 ? 8 : 4;
        if (!xx_elf_yx_dynamic_at(&elf, index, &at)) return false;
        if (!xx_elf_yx_read(&elf, at +
                         (strcmp(member, "val") == 0 ? word : 0),
                         word, value)) return false;
        if (strcmp(member, "type") == 0)
            *value = xx_elf_yx_enum_i32(*value);
        return true;
    }
    section = strcmp(collection, "sections") == 0;
    if (section) {
        if (!elf.sh_valid || elf.sh_count >= 0xff00 || index >= elf.sh_count)
            return false;
        at = elf.sh_offset + index * (elf.is64 ? 64u : 40u);
        if (strcmp(member, "type") == 0) field_offset = 4;
        else if (strcmp(member, "flags") == 0) field_offset = 8;
        else if (strcmp(member, "address") == 0)
            field_offset = elf.is64 ? 16 : 12;
        else if (strcmp(member, "offset") == 0)
            field_offset = elf.is64 ? 24 : 16;
        else field_offset = elf.is64 ? 32 : 20;
        field_width = strcmp(member, "type") == 0 ? 4 : (elf.is64 ? 8 : 4);
    } else {
        if (!elf.ph_valid || index >= elf.ph_count) return false;
        at = elf.ph_offset + index * (elf.is64 ? 56u : 32u);
        if (strcmp(member, "type") == 0) field_offset = 0;
        else if (strcmp(member, "flags") == 0)
            field_offset = elf.is64 ? 4 : 24;
        else if (strcmp(member, "offset") == 0)
            field_offset = elf.is64 ? 8 : 4;
        else if (strcmp(member, "virtual_address") == 0)
            field_offset = elf.is64 ? 16 : 8;
        else if (strcmp(member, "physical_address") == 0)
            field_offset = elf.is64 ? 24 : 12;
        else if (strcmp(member, "file_size") == 0)
            field_offset = elf.is64 ? 32 : 16;
        else if (strcmp(member, "memory_size") == 0)
            field_offset = elf.is64 ? 40 : 20;
        else field_offset = elf.is64 ? 48 : 28;
        field_width = strcmp(member, "type") == 0 ||
                      strcmp(member, "flags") == 0 ? 4 : (elf.is64 ? 8 : 4);
    }
    if (!xx_elf_yx_read(&elf, at + field_offset, field_width, value))
        return false;
    if (strcmp(member, "type") == 0)
        *value = xx_elf_yx_enum_i32(*value);
    return true;
}

XXFC_API bool xx_elf_yx_indexed_string_field_supported(const char *collection,
                                            const char *member)
{
    return collection && member && strcmp(member, "name") == 0 &&
           (strcmp(collection, "sections") == 0 ||
            strcmp(collection, "symtab") == 0 ||
            strcmp(collection, "dynsym") == 0);
}

XXFC_API bool xx_elf_yx_indexed_string_field(const uint8_t *data, size_t size,
                                  const char *collection, uint64_t index,
                                  const char *member,
                                  const uint8_t **bytes, size_t *length)
{
    xx_elf_yx_header elf;
    xx_elf_yx_table_entry string_table;
    uint64_t table_index, name_index, name_offset;
    size_t available;
    const uint8_t *start, *end;
    if (!bytes || !length ||
        !xx_elf_yx_indexed_string_field_supported(collection, member) ||
        !xx_elf_yx_parse(data, size, &elf) || !elf.sh_valid ||
        elf.sh_count >= 0xff00)
        return false;
    if (strcmp(collection, "sections") == 0) {
        if (index >= elf.sh_count ||
            !xx_elf_yx_read(&elf, elf.is64 ? 62 : 50, 2, &table_index) ||
            !xx_elf_yx_section(&elf, table_index, &string_table) ||
            !xx_elf_yx_read(&elf, elf.sh_offset +
                               index * (elf.is64 ? 64u : 40u), 4,
                               &name_index))
            return false;
    } else {
        xx_elf_yx_table_entry symbols;
        uint64_t at;
        if (!xx_elf_yx_symbol_at(&elf, collection, index, &at, &symbols) ||
            !xx_elf_yx_section(&elf, symbols.link, &string_table) ||
            !xx_elf_yx_read(&elf, at, 4, &name_index))
            return false;
    }
    if (string_table.offset > UINT64_MAX - name_index) return false;
    name_offset = string_table.offset + name_index;
    if (!xx_elf_yx_span(&elf, name_offset, 0, &start)) return false;
    available = size - (size_t)name_offset;
    end = memchr(start, 0, available);
    *bytes = start;
    *length = end ? (size_t)(end - start) : available;
    return true;
}

/* All inline enum constants from elf.proto in YARA-X v1.21.0. */
XXFC_API bool xx_elf_yx_constant(const char *member, uint64_t *value)
{
    static const struct { const char *name; uint64_t value; } constants[] = {
    {"ET_NONE", UINT64_C(0)},
    {"ET_REL", UINT64_C(1)},
    {"ET_EXEC", UINT64_C(2)},
    {"ET_DYN", UINT64_C(3)},
    {"ET_CORE", UINT64_C(4)},
    {"ET_LOPROC", UINT64_C(65280)},
    {"ET_HIPROC", UINT64_C(255)},
    {"EM_NONE", UINT64_C(0)},
    {"EM_M32", UINT64_C(1)},
    {"EM_SPARC", UINT64_C(2)},
    {"EM_386", UINT64_C(3)},
    {"EM_68K", UINT64_C(4)},
    {"EM_88K", UINT64_C(5)},
    {"EM_IAMCU", UINT64_C(6)},
    {"EM_860", UINT64_C(7)},
    {"EM_MIPS", UINT64_C(8)},
    {"EM_S370", UINT64_C(9)},
    {"EM_MIPS_RS3_LE", UINT64_C(10)},
    {"EM_PARISC", UINT64_C(15)},
    {"EM_SPARC32PLUS", UINT64_C(18)},
    {"EM_PPC", UINT64_C(20)},
    {"EM_PPC64", UINT64_C(21)},
    {"EM_S390", UINT64_C(22)},
    {"EM_MCORE", UINT64_C(39)},
    {"EM_RCE", UINT64_C(39)},
    {"EM_ARM", UINT64_C(40)},
    {"EM_SH", UINT64_C(42)},
    {"EM_SPARCV9", UINT64_C(43)},
    {"EM_X86_64", UINT64_C(62)},
    {"EM_ARC_COMPACT", UINT64_C(93)},
    {"EM_AARCH64", UINT64_C(183)},
    {"EM_BPF", UINT64_C(247)},
    {"EM_LOONGARCH", UINT64_C(258)},
    {"OSABI_NONE", UINT64_C(0)},
    {"OSABI_HPUX", UINT64_C(1)},
    {"OSABI_NETBSD", UINT64_C(2)},
    {"OSABI_LINUX", UINT64_C(3)},
    {"OSABI_GNU_HURD", UINT64_C(4)},
    {"OSABI_SOLARIS", UINT64_C(6)},
    {"OSABI_AIX", UINT64_C(7)},
    {"OSABI_IRIX", UINT64_C(8)},
    {"OSABI_FREE_BSD", UINT64_C(9)},
    {"OSABI_TRU64", UINT64_C(10)},
    {"OSABI_MODESTO", UINT64_C(11)},
    {"OSABI_OPENBSD", UINT64_C(12)},
    {"OSABI_OPENVMS", UINT64_C(13)},
    {"OSABI_NSK", UINT64_C(14)},
    {"OSABI_AROS", UINT64_C(15)},
    {"OSABI_FENIXOS", UINT64_C(16)},
    {"OSABI_CLOUDABI", UINT64_C(17)},
    {"OSABI_OPENVOS", UINT64_C(18)},
    {"OSABI_ARM_AEABI", UINT64_C(64)},
    {"OSABI_ARM", UINT64_C(97)},
    {"OSABI_STANDALONE", UINT64_C(255)},
    {"SHT_NULL", UINT64_C(0)},
    {"SHT_PROGBITS", UINT64_C(1)},
    {"SHT_SYMTAB", UINT64_C(2)},
    {"SHT_STRTAB", UINT64_C(3)},
    {"SHT_RELA", UINT64_C(4)},
    {"SHT_HASH", UINT64_C(5)},
    {"SHT_DYNAMIC", UINT64_C(6)},
    {"SHT_NOTE", UINT64_C(7)},
    {"SHT_NOBITS", UINT64_C(8)},
    {"SHT_REL", UINT64_C(9)},
    {"SHT_SHLIB", UINT64_C(10)},
    {"SHT_DYNSYM", UINT64_C(11)},
    {"SHT_INIT_ARRAY", UINT64_C(14)},
    {"SHT_FINI_ARRAY", UINT64_C(15)},
    {"PT_NULL", UINT64_C(0)},
    {"PT_LOAD", UINT64_C(1)},
    {"PT_DYNAMIC", UINT64_C(2)},
    {"PT_INTERP", UINT64_C(3)},
    {"PT_NOTE", UINT64_C(4)},
    {"PT_SHLIB", UINT64_C(5)},
    {"PT_PHDR", UINT64_C(6)},
    {"PT_TLS", UINT64_C(7)},
    {"PT_GNU_EH_FRAME", UINT64_C(1685382480)},
    {"PT_GNU_STACK", UINT64_C(1685382481)},
    {"PT_GNU_RELRO", UINT64_C(1685382482)},
    {"PT_GNU_PROPERTY", UINT64_C(1685382483)},
    {"PF_X", UINT64_C(1)},
    {"PF_W", UINT64_C(2)},
    {"PF_R", UINT64_C(4)},
    {"STT_NOTYPE", UINT64_C(0)},
    {"STT_OBJECT", UINT64_C(1)},
    {"STT_FUNC", UINT64_C(2)},
    {"STT_SECTION", UINT64_C(3)},
    {"STT_FILE", UINT64_C(4)},
    {"STT_COMMON", UINT64_C(5)},
    {"STT_TLS", UINT64_C(6)},
    {"STB_LOCAL", UINT64_C(0)},
    {"STB_GLOBAL", UINT64_C(1)},
    {"STB_WEAK", UINT64_C(2)},
    {"STV_DEFAULT", UINT64_C(0)},
    {"STV_INTERNAL", UINT64_C(1)},
    {"STV_HIDDEN", UINT64_C(2)},
    {"STV_PROTECTED", UINT64_C(3)},
    {"DT_NULL", UINT64_C(0)},
    {"DT_NEEDED", UINT64_C(1)},
    {"DT_PLTRELSZ", UINT64_C(2)},
    {"DT_PLTGOT", UINT64_C(3)},
    {"DT_HASH", UINT64_C(4)},
    {"DT_STRTAB", UINT64_C(5)},
    {"DT_SYMTAB", UINT64_C(6)},
    {"DT_RELA", UINT64_C(7)},
    {"DT_RELASZ", UINT64_C(8)},
    {"DT_RELAENT", UINT64_C(9)},
    {"DT_STRSZ", UINT64_C(10)},
    {"DT_SYMENT", UINT64_C(11)},
    {"DT_INIT", UINT64_C(12)},
    {"DT_FINI", UINT64_C(13)},
    {"DT_SONAME", UINT64_C(14)},
    {"DT_RPATH", UINT64_C(15)},
    {"DT_SYMBOLIC", UINT64_C(16)},
    {"DT_REL", UINT64_C(17)},
    {"DT_RELSZ", UINT64_C(18)},
    {"DT_RELENT", UINT64_C(19)},
    {"DT_PLTREL", UINT64_C(20)},
    {"DT_DEBUG", UINT64_C(21)},
    {"DT_TEXTREL", UINT64_C(22)},
    {"DT_JMPREL", UINT64_C(23)},
    {"DT_BIND_NOW", UINT64_C(24)},
    {"DT_INIT_ARRAY", UINT64_C(25)},
    {"DT_FINI_ARRAY", UINT64_C(26)},
    {"DT_INIT_ARRAYSZ", UINT64_C(27)},
    {"DT_FINI_ARRAYSZ", UINT64_C(28)},
    {"DT_RUNPATH", UINT64_C(29)},
    {"DT_FLAGS", UINT64_C(30)},
    {"DT_ENCODING", UINT64_C(32)},
    {"DT_LOOS", UINT64_C(1610612749)},
    {"DT_HIOS", UINT64_C(1879044096)},
    {"DT_VALRNGLO", UINT64_C(1879047424)},
    {"DT_VALRNGHI", UINT64_C(1879047679)},
    {"DT_ADDRRNGLO", UINT64_C(1879047680)},
    {"DT_ADDRRNGHI", UINT64_C(1879047935)},
    {"DT_VERSYM", UINT64_C(1879048176)},
    {"DT_RELACOUNT", UINT64_C(1879048185)},
    {"DT_RELCOUNT", UINT64_C(1879048186)},
    {"DT_FLAGS_1", UINT64_C(1879048187)},
    {"DT_VERDEF", UINT64_C(1879048188)},
    {"DT_VERDEFNUM", UINT64_C(1879048189)},
    {"DT_VERNEED", UINT64_C(1879048190)},
    {"DT_VERNEEDNUM", UINT64_C(1879048191)},
    {"DT_LOPROC", UINT64_C(1879048192)},
    {"DT_HIPROC", UINT64_C(2147483647)},
    };
    /* Every constant name starts with an upper-case letter; field names
     * do not, so they skip the table scan. */
    if (!member || !value || member[0] < 'A' || member[0] > 'Z') return false;
    for (size_t i = 0; i < sizeof(constants) / sizeof(*constants); ++i) {
        if (strcmp(member, constants[i].name) == 0) {
            *value = constants[i].value;
            return true;
        }
    }
    return false;
}
static int xx_elf_yx_compare_names(const void *left, const void *right)
{
    const char *const *a = (const char *const *) left;
    const char *const *b = (const char *const *) right;
    return strcmp(*a, *b);
}

/* Lowercase hexadecimal MD5 of a buffer; `out` receives 32 digits plus
 * NUL. A NULL buffer with size 0 denotes the empty string. */
static bool xx_elf_yx_md5_hex(const uint8_t *data, size_t size, char out[33])
{
    static const char digits[] = "0123456789abcdef";
    uint8_t digest[16];
    size_t i;
    if (!out || (!data && size)) return false;
    if (!xx_md5_memory(data, size, digest)) return false;
    for (i = 0; i < 16; ++i) {
        out[i * 2] = digits[digest[i] >> 4];
        out[i * 2 + 1] = digits[digest[i] & 15];
    }
    out[32] = '\0';
    return true;
}

XXFC_API bool xx_elf_yx_import_md5(const uint8_t *data, size_t size, char out[33])
{
    xx_elf_yx_header elf;
    xx_elf_yx_table_entry symtab = {0}, strtab;
    uint64_t count = 0;
    char **names = NULL, *joined = NULL;
    size_t name_count = 0, joined_size = 0;
    bool success = false;
    if (!out || (!data && size)) return false;
    out[0] = '\0';

    if (xx_elf_yx_parse(data, size, &elf) && elf.sh_valid &&
        elf.sh_count < 0xff00) {
        count = xx_elf_yx_symbol_count(&elf, 11, &symtab);
        if (!count) count = xx_elf_yx_symbol_count(&elf, 2, &symtab);
        for (uint64_t i = 0; i < count; ++i) {
            uint64_t symbol_at, name_index, section_index;
            const uint8_t *name_start, *nul;
            size_t name_length;
            char *copy, **grown;
            size_t copy_length;
            unsigned width = elf.is64 ? 24 : 16;
            if (i > (UINT64_MAX - symtab.offset) / width) goto done;
            symbol_at = symtab.offset + i * width;
            if (!xx_elf_yx_read(&elf, symbol_at, 4, &name_index) ||
                !xx_elf_yx_read(&elf, symbol_at + (elf.is64 ? 6 : 14), 2,
                              &section_index)) goto done;
            if (section_index != 0 || symtab.link >= elf.sh_count ||
                !xx_elf_yx_section(&elf, symtab.link, &strtab) ||
                strtab.offset > UINT64_MAX - name_index ||
                !xx_elf_yx_span(&elf, strtab.offset + name_index, 0,
                              &name_start))
                continue;
            nul = (const uint8_t *) memchr(
                name_start, 0, elf.size - (size_t)(strtab.offset + name_index));
            name_length = nul ? (size_t)(nul - name_start) :
                                elf.size - (size_t)(strtab.offset + name_index);
            if (!name_length) continue;
            if (name_count == SIZE_MAX / sizeof(*names) ||
                !xx_string_to_lower_alloc(name_start, name_length,
                                    &copy, &copy_length)) goto done;
            grown = (char **) realloc(names,
                                      (name_count + 1) * sizeof(*names));
            if (!grown) { free(copy); goto done; }
            names = grown;
            names[name_count++] = copy;
            if (joined_size > SIZE_MAX - copy_length - (name_count > 1))
                goto done;
            joined_size += copy_length + (name_count > 1);
        }
    }

    if (name_count) qsort(names, name_count, sizeof(*names),
                          xx_elf_yx_compare_names);
    joined = (char *) calloc(joined_size ? joined_size : 1, 1);
    if (!joined) goto done;
    {
        size_t used = 0;
        for (size_t i = 0; i < name_count; ++i) {
            size_t length = strlen(names[i]);
            if (i) joined[used++] = ',';
            memcpy(joined + used, names[i], length);
            used += length;
        }
    }
    if (!xx_elf_yx_md5_hex((const uint8_t *)joined, joined_size, out))
        goto done;
    success = true;
done:
    for (size_t i = 0; i < name_count; ++i) free(names[i]);
    free(names);
    free(joined);
    return success;
}

static bool xx_elf_yx_telfhash_name_allowed(const char *name, size_t length)
{
    static const char *const excluded[] = {
        "__libc_start_main", "main", "abort", "cachectl", "cacheflush",
        "puts", "atol", "malloc_trim"
    };
    for (size_t i = 0; i < sizeof(excluded) / sizeof(excluded[0]); ++i) {
        if (strcmp(name, excluded[i]) == 0) return false;
    }
    if (length && (name[0] == '.' || name[0] == '_')) return false;
    if (length >= 3 &&
        (memcmp(name, "mem", 3) == 0 || memcmp(name, "str", 3) == 0))
        return false;
    if (length >= 2 && memcmp(name + length - 2, "64", 2) == 0)
        return false;
    return true;
}

XXFC_API bool xx_elf_yx_telfhash(const uint8_t *data, size_t size, char out[73])
{
    xx_elf_yx_header elf;
    xx_elf_yx_table_entry symtab = {0}, strtab;
    uint64_t count;
    char **names = NULL, *joined = NULL;
    size_t name_count = 0, joined_size = 0;
    bool success = false;
    if (!out || (!data && size)) return false;
    out[0] = '\0';
    if (!xx_elf_yx_parse(data, size, &elf) || !elf.sh_valid ||
        elf.sh_count >= 0xff00) return false;
    count = xx_elf_yx_symbol_count(&elf, 11, &symtab);
    if (!count) count = xx_elf_yx_symbol_count(&elf, 2, &symtab);
    for (uint64_t i = 0; i < count; ++i) {
        unsigned width = elf.is64 ? 24 : 16;
        uint64_t symbol_at, name_index, info, other;
        const uint8_t *name_start, *nul;
        size_t name_length, display_length, copy_length;
        char *display = NULL, *copy = NULL, **grown;
        if (i > (UINT64_MAX - symtab.offset) / width) goto done;
        symbol_at = symtab.offset + i * width;
        if (!xx_elf_yx_read(&elf, symbol_at, 4, &name_index) ||
            !xx_elf_yx_read(&elf, symbol_at + (elf.is64 ? 4 : 12), 1, &info) ||
            !xx_elf_yx_read(&elf, symbol_at + (elf.is64 ? 5 : 13), 1, &other))
            goto done;
        if ((info & 15) != 2 || (info >> 4) != 1 || (other & 3) != 0 ||
            symtab.link >= elf.sh_count ||
            !xx_elf_yx_section(&elf, symtab.link, &strtab) ||
            strtab.offset > UINT64_MAX - name_index ||
            !xx_elf_yx_span(&elf, strtab.offset + name_index, 0, &name_start))
            continue;
        nul = (const uint8_t *)memchr(
            name_start, 0, elf.size - (size_t)(strtab.offset + name_index));
        name_length = nul ? (size_t)(nul - name_start) :
                            elf.size - (size_t)(strtab.offset + name_index);
        if (!xx_string_utf8_lossy(name_start, name_length, NULL, 0,
                                &display_length) || display_length == SIZE_MAX)
            goto done;
        display = (char *)malloc(display_length + 1);
        if (!display ||
            !xx_string_utf8_lossy(name_start, name_length, display,
                                display_length + 1, &display_length)) {
            free(display);
            goto done;
        }
        if (!xx_elf_yx_telfhash_name_allowed(display, display_length)) {
            free(display);
            continue;
        }
        if (!xx_string_to_lower_alloc((const uint8_t *)display, display_length,
                               &copy, &copy_length)) {
            free(display);
            goto done;
        }
        free(display);
        if (name_count >= SIZE_MAX / sizeof(*names) ||
            copy_length > SIZE_MAX - joined_size ||
            (name_count != 0 && copy_length == SIZE_MAX - joined_size)) {
            free(copy);
            goto done;
        }
        grown = (char **)realloc(names, (name_count + 1) * sizeof(*names));
        if (!grown) { free(copy); goto done; }
        names = grown;
        names[name_count++] = copy;
        joined_size += copy_length + (name_count > 1);
    }
    if (name_count) qsort(names, name_count, sizeof(*names),
                          xx_elf_yx_compare_names);
    joined = (char *)malloc(joined_size ? joined_size : 1);
    if (!joined) goto done;
    {
        size_t used = 0;
        for (size_t i = 0; i < name_count; ++i) {
            size_t length = strlen(names[i]);
            if (i) joined[used++] = ',';
            memcpy(joined + used, names[i], length);
            used += length;
        }
    }
    success = xx_tlsh_hash128((const uint8_t *)joined, joined_size, out);
done:
    for (size_t i = 0; i < name_count; ++i) free(names[i]);
    free(names);
    free(joined);
    return success;
}
