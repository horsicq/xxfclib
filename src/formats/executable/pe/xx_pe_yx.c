/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* Implements include/xxfclib/formats/executable/pe/xx_pe_yx.h. The parsing
 * behavior is a bit-exact port of the YARA-X 1.21.0 PE module semantics as
 * used by xxyara; see the header for the contract. */

#include "xxfclib/formats/executable/pe/xx_pe_yx.h"
#include "xxfclib/algo/md5/xx_md5.h"
#include "xxfclib/strings/xx_string_casefold.h"

#include "xx_pe_yx_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool xx_pe_yx_rva_to_offset(const uint8_t *data, size_t size, uint64_t rva,
                            uint64_t *value)
{
    size_t pe_offset, sections_offset, count;
    uint32_t lowest_rva = UINT32_MAX;
    uint32_t section_rva = 0, section_offset = 0, section_raw_size = 0;
    uint64_t file_alignment, section_alignment;
    if (!value || rva > UINT32_MAX || !xx_pe_yx_is_pe(data, size)) return false;
    pe_offset = (size_t)xx_pe_yx_read(data, 60, 4);
    count = xx_pe_yx_sections(data, size, pe_offset, &sections_offset);
    if (!count) return false;
    for (size_t i = 0; i < count; ++i) {
        size_t offset = sections_offset + i * 40;
        uint32_t virtual_address = (uint32_t)xx_pe_yx_read(data, offset + 12, 4);
        if (virtual_address < lowest_rva) lowest_rva = virtual_address;
    }
    if (rva < lowest_rva) { *value = rva; return true; }
    if (!xx_pe_yx_optional_field32(data, size, 36, 36, &file_alignment) ||
        !xx_pe_yx_optional_field32(data, size, 32, 32, &section_alignment))
        return false;
    for (size_t i = 0; i < count; ++i) {
        size_t offset = sections_offset + i * 40;
        uint32_t virtual_size = (uint32_t)xx_pe_yx_read(data, offset + 8, 4);
        uint32_t virtual_address = (uint32_t)xx_pe_yx_read(data, offset + 12, 4);
        uint32_t raw_size = (uint32_t)xx_pe_yx_read(data, offset + 16, 4);
        uint32_t raw_offset = (uint32_t)xx_pe_yx_read(data, offset + 20, 4);
        uint32_t length = virtual_size > raw_size ? virtual_size : raw_size;
        uint32_t end = UINT32_MAX - virtual_address < length ?
                       UINT32_MAX : virtual_address + length;
        if (section_rva <= virtual_address &&
            rva >= virtual_address && rva < end) {
            uint32_t alignment = file_alignment < 0x200 ?
                                 (uint32_t)file_alignment : 0x200;
            section_rva = virtual_address;
            section_offset = raw_offset;
            section_raw_size = raw_size;
            if (alignment) section_offset -= section_offset % alignment;
            if (section_alignment >= 0x1000)
                section_offset -= section_offset % 0x200;
        }
    }
    if (rva - section_rva >= section_raw_size) return false;
    *value = UINT32_MAX - section_offset < rva - section_rva ?
             UINT32_MAX : section_offset + rva - section_rva;
    return true;
}

bool xx_pe_yx_offset_to_rva(const uint8_t *data, size_t size, uint64_t offset,
                            uint64_t *value)
{
    size_t pe_offset, sections_offset, count;
    uint64_t lowest_offset = UINT64_MAX;
    uint32_t section_rva = 0, section_offset = 0, section_raw_size = 0;
    uint64_t file_alignment, section_alignment;
    if (!value || offset > UINT32_MAX || !xx_pe_yx_is_pe(data, size) ||
        !xx_pe_yx_optional_field32(data, size, 36, 36, &file_alignment) ||
        !xx_pe_yx_optional_field32(data, size, 32, 32, &section_alignment))
        return false;
    pe_offset = (size_t)xx_pe_yx_read(data, 60, 4);
    count = xx_pe_yx_sections(data, size, pe_offset, &sections_offset);
    for (size_t i = 0; i < count; ++i) {
        size_t section = sections_offset + i * 40;
        uint32_t raw_offset = (uint32_t)xx_pe_yx_read(data, section + 20, 4);
        uint32_t raw_size = (uint32_t)xx_pe_yx_read(data, section + 16, 4);
        uint32_t alignment = file_alignment < 0x200 ?
                             (uint32_t)file_alignment : 0x200;
        uint32_t end;
        /* Sections with no file data do not participate in this mapping. */
        if (!raw_offset) continue;
        if (alignment) raw_offset -= raw_offset % alignment;
        if (section_alignment >= 0x1000)
            raw_offset -= raw_offset % 0x200;
        if (raw_offset < lowest_offset) lowest_offset = raw_offset;
        end = UINT32_MAX - raw_offset < raw_size ?
              UINT32_MAX : raw_offset + raw_size;
        if (section_offset <= raw_offset &&
            offset >= raw_offset && offset < end) {
            section_rva = (uint32_t)xx_pe_yx_read(data, section + 12, 4);
            section_offset = raw_offset;
            section_raw_size = raw_size;
        }
    }
    if (lowest_offset != UINT64_MAX && offset < lowest_offset) {
        *value = offset;
        return true;
    }
    if (offset < section_offset ||
        offset - section_offset >= section_raw_size) return false;
    *value = UINT32_MAX - section_rva < offset - section_offset ?
             UINT32_MAX : section_rva + offset - section_offset;
    return true;
}

bool xx_pe_yx_calculate_checksum(const uint8_t *data, size_t size,
                                 uint64_t *value)
{
    uint32_t sum = 0, header_checksum;
    bool carry = false;
    uint64_t checksum;
    if (!value ||
        !xx_pe_yx_optional_field32(data, size, 64, 64, &checksum))
        return false;
    header_checksum = (uint32_t)checksum;
    /* In YARA-X 1.21.0, the checksum iterator consumes 32-bit words and
     * then a final 16-bit word. Its single-byte fallback is a decimal parser:
     * an odd trailing ASCII digit contributes its numeric value; other bytes
     * are left unread. */
    for (size_t offset = 0; size - offset >= 2;) {
        uint32_t word = 0;
        uint64_t wide;
        unsigned width = size - offset < 4 ? 2u : 4u;
        for (unsigned i = 0; i < width; ++i)
            word |= (uint32_t)data[offset + i] << (8 * i);
        wide = (uint64_t)sum + word + carry;
        sum = (uint32_t)wide;
        carry = wide > UINT32_MAX;
        offset += width;
    }
    if ((size & 1) && data[size - 1] >= '0' && data[size - 1] <= '9') {
        uint64_t wide = (uint64_t)sum + (data[size - 1] - '0') + carry;
        sum = (uint32_t)wide;
    }
    if (sum < header_checksum) {
        sum -= header_checksum;
        --sum;
    } else {
        sum -= header_checksum;
    }
    sum = (sum & 0xffffu) + (sum >> 16);
    sum += sum >> 16;
    sum &= 0xffffu;
    sum += (uint32_t)size;
    *value = sum;
    return true;
}

static inline bool xx_pe_yx_offset_for_rva(const uint8_t *data, size_t size,
                                           uint32_t rva, size_t *offset)
{
    uint64_t candidate;
    if (!xx_pe_yx_rva_to_offset(data, size, rva, &candidate) || candidate > size)
        return false;
    *offset = (size_t)candidate;
    return true;
}

/* ---------------------------------------------------------------- Rich -- */

bool xx_pe_yx_rich_read(const uint8_t *data, size_t size,
                        xx_pe_yx_rich_info *info)
{
    size_t end, rich = SIZE_MAX, dans = SIZE_MAX;
    uint32_t key;
    if (!info || !xx_pe_yx_is_pe(data, size)) return false;
    end = (size_t)xx_pe_yx_read(data, 60, 4);
    if (end < 4) return false;
    for (size_t i = end - 4;; --i) {
        if (memcmp(data + i, "Rich", 4) == 0) { rich = i; break; }
        if (!i) break;
    }
    if (rich == SIZE_MAX || end - rich < 8 || rich < 4) return false;
    key = (uint32_t)xx_pe_yx_read(data, rich + 4, 4);
    for (size_t i = rich - 4;; --i) {
        if (xx_pe_yx_read(data, i, 4) == (UINT32_C(0x536e6144) ^ key)) {
            dans = i;
            break;
        }
        if (!i) break;
    }
    if (dans == SIZE_MAX) return false;
    info->offset = dans;
    info->length = rich - dans;
    info->key = key;
    info->present = true;
    return true;
}

bool xx_pe_yx_rich_tool_count(const uint8_t *data, size_t size, uint64_t *count)
{
    xx_pe_yx_rich_info info;
    if (!count) return false;
    *count = 1;
    memset(&info, 0, sizeof(info));
    if (xx_pe_yx_rich_read(data, size, &info))
        *count = info.length >= 16 ? (info.length - 16) / 8 : 0;
    return true;
}

bool xx_pe_yx_rich_tool_field(const uint8_t *data, size_t size, uint64_t index,
                              const char *member, uint64_t *value)
{
    xx_pe_yx_rich_info info;
    size_t at;
    uint32_t version_tool;
    if (!member || !value || !xx_pe_yx_rich_read(data, size, &info) ||
        info.length < 16 || index >= (info.length - 16) / 8) return false;
    at = info.offset + 16 + (size_t)index * 8;
    version_tool = (uint32_t)xx_pe_yx_read(data, at, 4) ^ info.key;
    if (strcmp(member, "version") == 0) *value = version_tool & 0xffffu;
    else if (strcmp(member, "toolid") == 0) *value = version_tool >> 16;
    else if (strcmp(member, "times") == 0)
        *value = (uint32_t)xx_pe_yx_read(data, at + 4, 4) ^ info.key;
    else return false;
    return true;
}

bool xx_pe_yx_rich_clear_data(const uint8_t *data, size_t size,
                              uint8_t **bytes, size_t *length)
{
    xx_pe_yx_rich_info info;
    uint8_t *copy;
    if (!bytes || !length || !xx_pe_yx_rich_read(data, size, &info))
        return false;
    copy = malloc(info.length ? info.length : 1);
    if (!copy) return false;
    for (size_t i = 0; i < info.length; ++i)
        copy[i] = data[info.offset + i] ^ (uint8_t)(info.key >> (8u * (i % 4)));
    *bytes = copy;
    *length = info.length;
    return true;
}

/* ------------------------------------------------------------- Imports -- */

static uint8_t xx_pe_yx_ascii_lower(uint8_t ch)
{
    return ch >= 'A' && ch <= 'Z' ? (uint8_t)(ch + 'a' - 'A') : ch;
}

static bool xx_pe_yx_ascii_equal(const uint8_t *a, size_t a_length,
                                 const char *b, size_t b_length)
{
    if (!b || a_length != b_length) return false;
    for (size_t i = 0; i < a_length; ++i)
        if (xx_pe_yx_ascii_lower(a[i]) !=
            xx_pe_yx_ascii_lower((uint8_t)b[i])) return false;
    return true;
}

bool xx_pe_yx_utf8_valid(const uint8_t *text, size_t length)
{
    for (size_t i = 0; i < length;) {
        uint32_t codepoint;
        unsigned trailing;
        uint8_t ch = text[i++];
        if (ch < 0x80) continue;
        if (ch >= 0xc2 && ch <= 0xdf) {
            codepoint = ch & 0x1f;
            trailing = 1;
        } else if (ch >= 0xe0 && ch <= 0xef) {
            codepoint = ch & 0x0f;
            trailing = 2;
        } else if (ch >= 0xf0 && ch <= 0xf4) {
            codepoint = ch & 0x07;
            trailing = 3;
        } else return false;
        if (trailing > length - i) return false;
        for (unsigned j = 0; j < trailing; ++j) {
            uint8_t next = text[i++];
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if ((trailing == 1 && codepoint < 0x80) ||
            (trailing == 2 && codepoint < 0x800) ||
            (trailing == 3 && codepoint < 0x10000) ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
            codepoint > 0x10ffff) return false;
    }
    return true;
}

static bool xx_pe_yx_import_dll_name(const uint8_t *data, size_t size,
                                     uint32_t rva, const uint8_t **name,
                                     size_t *length)
{
    size_t offset, n = 0;
    if (!xx_pe_yx_offset_for_rva(data, size, rva, &offset)) return false;
    while (n < 512 && n < size - offset && data[offset + n]) {
        uint8_t ch = data[offset + n];
        if (ch < 0x20 || ch == 0x7f || ch == ' ' || ch == '"' ||
            ch == '*' || ch == '<' || ch == '>' || ch == '?' || ch == '|')
            return false;
        ++n;
    }
    if (!xx_pe_yx_utf8_valid(data + offset, n)) return false;
    *name = data + offset;
    *length = n;
    return true;
}

static bool xx_pe_yx_import_function_name(const uint8_t *data, size_t size,
                                          uint32_t rva, const uint8_t **name,
                                          size_t *length)
{
    size_t offset, n = 0;
    if (!xx_pe_yx_offset_for_rva(data, size, rva, &offset) ||
        size - offset < 2) return false;
    offset += 2; /* Hint. */
    while (n < 1024 && n < size - offset && data[offset + n]) {
        if (data[offset + n] < 0x21 || data[offset + n] > 0x7e)
            return false;
        ++n;
    }
    if (!n) return false;
    *name = data + offset;
    *length = n;
    return true;
}

/* Ordinal names for wsock32/ws2_32 and oleaut32 are populated below from
 * YARA-X's published PE ordinal tables. Unknown ordinals use "ordN". */
typedef struct { uint16_t ordinal; const char *name; } xx_pe_yx_ordinal_entry;

#include "xx_pe_yx_ordinals.inc"

const char *xx_pe_yx_lookup_ordinal_name(const uint8_t *dll, size_t dll_length,
                                         uint16_t ordinal, char fallback[16])
{
    const xx_pe_yx_ordinal_entry *entries = NULL;
    size_t count = 0;
    if (!fallback) return NULL;
    if (xx_pe_yx_ascii_equal(dll, dll_length, "oleaut32.dll", 12)) {
        entries = xx_pe_yx_oleaut32_ordinals;
        count = sizeof(xx_pe_yx_oleaut32_ordinals) /
                sizeof(xx_pe_yx_oleaut32_ordinals[0]);
    } else if (xx_pe_yx_ascii_equal(dll, dll_length, "wsock32.dll", 11) ||
               xx_pe_yx_ascii_equal(dll, dll_length, "ws2_32.dll", 10)) {
        entries = xx_pe_yx_wsock32_ordinals;
        count = sizeof(xx_pe_yx_wsock32_ordinals) /
                sizeof(xx_pe_yx_wsock32_ordinals[0]);
    }
    /* Both tables are sorted by ordinal. */
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (entries[mid].ordinal < ordinal) lo = mid + 1;
        else hi = mid;
    }
    if (lo < count && entries[lo].ordinal == ordinal)
        return entries[lo].name;
    snprintf(fallback, 16, "ord%u", ordinal);
    return fallback;
}

static void xx_pe_yx_walk_import_directory(const uint8_t *data, size_t size,
                                           bool delayed,
                                           xx_pe_yx_import_stats *stats,
                                           xx_pe_yx_import_visitor visitor,
                                           xx_pe_yx_import_descriptor_visitor
                                               descriptor_visitor,
                                           void *opaque)
{
    size_t pe_offset, optional_offset, directory_offset, start, end;
    uint16_t magic;
    uint32_t address, directory_size;
    uint64_t image_base;
    unsigned width;
    uint64_t parsed_functions = 0;

    pe_offset = (size_t)xx_pe_yx_read(data, 60, 4);
    optional_offset = pe_offset + 24;
    if (size - optional_offset < 2) return;
    magic = (uint16_t)xx_pe_yx_read(data, optional_offset, 2);
    width = magic == 0x20b ? 8u : 4u;
    if (size - optional_offset < (width == 8 ? 112u : 96u)) return;
    image_base = xx_pe_yx_read(data, optional_offset +
                               (width == 8 ? 24u : 28u), width);
    directory_offset = optional_offset + (width == 8 ? 112u : 96u) +
                       (delayed ? 13u : 1u) * 8u;
    if (directory_offset > size || size - directory_offset < 8) return;
    address = (uint32_t)xx_pe_yx_read(data, directory_offset, 4);
    directory_size = (uint32_t)xx_pe_yx_read(data, directory_offset + 4, 4);
    if (!address || !xx_pe_yx_offset_for_rva(data, size, address, &start)) return;
    end = size;
    if (delayed) {
        uint64_t candidate = (uint64_t)start + directory_size;
        if (candidate > UINT32_MAX) candidate = UINT32_MAX;
        if (candidate < end) end = (size_t)candidate;
    }
    for (size_t descriptor = 0, at = start;
         descriptor < 16384 && at <= end &&
         end - at >= (delayed ? 32u : 20u);
         ++descriptor, at += delayed ? 32u : 20u) {
        uint32_t attributes = delayed ?
            (uint32_t)xx_pe_yx_read(data, at, 4) : 1u;
        uint32_t dll_rva = (uint32_t)xx_pe_yx_read(data, at +
                                                   (delayed ? 4u : 12u), 4);
        uint32_t int_rva = (uint32_t)xx_pe_yx_read(data, at +
                                                   (delayed ? 16u : 0u), 4);
        uint32_t iat_rva = (uint32_t)xx_pe_yx_read(data, at +
                                                   (delayed ? 12u : 16u), 4);
        const uint8_t *dll;
        size_t dll_length, thunks;
        uint64_t functions_here = 0;
        if (!dll_rva || (!int_rva && !iat_rva)) break;
        if (delayed && !(attributes & 1u)) {
            uint32_t base;
            if (image_base > UINT32_MAX) continue;
            base = (uint32_t)image_base;
            dll_rva = dll_rva > base ? dll_rva - base : 0;
            int_rva = int_rva > base ? int_rva - base : 0;
            iat_rva = iat_rva > base ? iat_rva - base : 0;
        }
        if (!xx_pe_yx_import_dll_name(data, size, dll_rva, &dll,
                                      &dll_length)) continue;
        if (!int_rva ||
            !xx_pe_yx_offset_for_rva(data, size, int_rva, &thunks)) {
            if (!xx_pe_yx_offset_for_rva(data, size, iat_rva, &thunks))
                continue;
        }
        for (size_t i = 0; i < 16384 && i <= (size - thunks) / width &&
                           width <= size - thunks - i * width; ++i) {
            uint64_t thunk = xx_pe_yx_read(data, thunks + i * width, width);
            uint64_t max_thunk = width == 8 ?
                UINT64_C(0x800000000000ffff) : UINT64_C(0x8000ffff);
            uint64_t ordinal_bit = width == 8 ?
                UINT64_C(0x8000000000000000) : UINT64_C(0x80000000);
            const uint8_t *name;
            size_t name_length;
            uint16_t ordinal = 0;
            bool by_ordinal;
            uint32_t function_rva;
            char fallback[16];
            if (!thunk) break;
            if (thunk > max_thunk) continue;
            by_ordinal = (thunk & ordinal_bit) != 0;
            if (by_ordinal) {
                const char *mapped;
                ordinal = (uint16_t)thunk;
                mapped = xx_pe_yx_lookup_ordinal_name(dll, dll_length, ordinal,
                                                      fallback);
                name = (const uint8_t *)mapped;
                name_length = strlen(mapped);
            } else {
                uint64_t name_rva = thunk;
                if (delayed && !(attributes & 1u))
                    name_rva = name_rva > image_base ?
                               name_rva - image_base : 0;
                if (name_rva > UINT32_MAX ||
                    !xx_pe_yx_import_function_name(data, size,
                                                   (uint32_t)name_rva,
                                                   &name, &name_length))
                    continue;
            }
            function_rva = UINT32_MAX - i * width < iat_rva ?
                           UINT32_MAX : iat_rva + (uint32_t)(i * width);
            if (visitor)
                visitor(opaque, dll, dll_length, name, name_length,
                        by_ordinal, ordinal, function_rva);
            if (descriptor_visitor)
                descriptor_visitor(opaque, descriptor, dll, dll_length,
                                   name, name_length, by_ordinal, ordinal,
                                   function_rva);
            ++functions_here;
        }
        if (functions_here) {
            if (delayed) {
                ++stats->delayed_libraries;
                stats->delayed_functions += functions_here;
            } else {
                ++stats->standard_libraries;
                stats->standard_functions += functions_here;
            }
            parsed_functions += functions_here;
            if (parsed_functions >= 16384) break;
        }
    }
}

static bool xx_pe_yx_walk_imports(const uint8_t *data, size_t size,
                                  unsigned flags, xx_pe_yx_import_stats *stats,
                                  xx_pe_yx_import_visitor visitor,
                                  xx_pe_yx_import_descriptor_visitor
                                      descriptor_visitor,
                                  void *opaque)
{
    memset(stats, 0, sizeof(*stats));
    if (!xx_pe_yx_is_pe(data, size)) return false;
    if (flags & 1u)
        xx_pe_yx_walk_import_directory(data, size, false, stats,
                                       visitor, descriptor_visitor, opaque);
    if (flags & 2u)
        xx_pe_yx_walk_import_directory(data, size, true, stats,
                                       visitor, descriptor_visitor, opaque);
    return true;
}

bool xx_pe_yx_get_import_stats(const uint8_t *data, size_t size,
                               xx_pe_yx_import_stats *out)
{
    return out && xx_pe_yx_walk_imports(data, size, 3u, out, NULL, NULL, NULL);
}

void xx_pe_yx_visit_imports(const uint8_t *data, size_t size, unsigned flags,
                            xx_pe_yx_import_visitor visitor, void *opaque)
{
    xx_pe_yx_import_stats stats;
    if (visitor)
        xx_pe_yx_walk_imports(data, size, flags, &stats, visitor, NULL, opaque);
}

void xx_pe_yx_visit_import_descriptors(const uint8_t *data, size_t size,
                                       unsigned flags,
                                       xx_pe_yx_import_descriptor_visitor visitor,
                                       void *opaque)
{
    xx_pe_yx_import_stats stats;
    if (visitor)
        xx_pe_yx_walk_imports(data, size, flags, &stats, NULL, visitor, opaque);
}

/* ------------------------------------------------------------- Exports -- */

typedef struct {
    uint32_t export_rva;
    uint32_t export_size;
    uint32_t name_rva;
    uint32_t timestamp;
    uint32_t ordinal_base;
    size_t function_count;
    size_t name_count;
    size_t functions_offset;
    size_t names_offset;
    size_t ordinals_offset;
} xx_pe_yx_export_directory;

static bool xx_pe_yx_export_directory_read(const uint8_t *data, size_t size,
                                           xx_pe_yx_export_directory *result)
{
    size_t pe_offset, optional_offset, directory_offset, export_offset;
    size_t export_end, array_offset;
    uint16_t magic;
    uint32_t export_rva, export_size, function_rva, names_rva, ordinals_rva;
    uint32_t declared_functions, declared_names;
    memset(result, 0, sizeof(*result));
    if (!xx_pe_yx_is_pe(data, size)) return false;
    pe_offset = (size_t)xx_pe_yx_read(data, 60, 4);
    optional_offset = pe_offset + 24;
    if (size - optional_offset < 2) return false;
    magic = (uint16_t)xx_pe_yx_read(data, optional_offset, 2);
    directory_offset = optional_offset + (magic == 0x20b ? 112u : 96u);
    if (directory_offset > size || size - directory_offset < 8) return false;
    export_rva = (uint32_t)xx_pe_yx_read(data, directory_offset, 4);
    export_size = (uint32_t)xx_pe_yx_read(data, directory_offset + 4, 4);
    if (!export_rva || !xx_pe_yx_offset_for_rva(data, size, export_rva,
                                                &export_offset)) return false;
    export_end = UINT32_MAX - export_offset < export_size ?
                 UINT32_MAX : (size_t)export_offset + export_size;
    if (export_end > size) export_end = size;
    if (export_end < export_offset || export_end - export_offset < 40)
        return false;
    result->export_rva = export_rva;
    result->export_size = export_size;
    result->timestamp = (uint32_t)xx_pe_yx_read(data, export_offset + 4, 4);
    result->name_rva = (uint32_t)xx_pe_yx_read(data, export_offset + 12, 4);
    result->ordinal_base = (uint32_t)xx_pe_yx_read(data, export_offset + 16, 4);
    declared_functions = (uint32_t)xx_pe_yx_read(data, export_offset + 20, 4);
    declared_names = (uint32_t)xx_pe_yx_read(data, export_offset + 24, 4);
    function_rva = (uint32_t)xx_pe_yx_read(data, export_offset + 28, 4);
    names_rva = (uint32_t)xx_pe_yx_read(data, export_offset + 32, 4);
    ordinals_rva = (uint32_t)xx_pe_yx_read(data, export_offset + 36, 4);

    /* The upstream parser returns the export metadata with an empty EAT
     * when address_of_functions is zero or the EAT cannot be mapped. */
    if (!function_rva || !xx_pe_yx_offset_for_rva(data, size, function_rva,
                                                  &array_offset)) return true;
    result->functions_offset = array_offset;
    result->function_count = declared_functions < 16384 ?
                             declared_functions : 16384;
    if (result->function_count > (size - array_offset) / 4)
        result->function_count = (size - array_offset) / 4;
    if (result->function_count >
        (uint64_t)UINT32_MAX - result->ordinal_base + 1)
        result->function_count = (size_t)((uint64_t)UINT32_MAX -
                                          result->ordinal_base + 1);
    if (!result->function_count) return true;

    result->name_count = declared_names < 16384 ? declared_names : 16384;
    if (!result->name_count ||
        !xx_pe_yx_offset_for_rva(data, size, names_rva, &array_offset) ||
        result->name_count > (size - array_offset) / 4) {
        result->name_count = 0;
        return true;
    }
    result->names_offset = array_offset;
    if (xx_pe_yx_offset_for_rva(data, size, ordinals_rva, &array_offset))
        result->ordinals_offset = array_offset;
    else result->name_count = 0;
    return true;
}

bool xx_pe_yx_export_info(const uint8_t *data, size_t size, bool *present,
                          uint64_t *function_count, uint64_t *timestamp)
{
    xx_pe_yx_export_directory directory;
    if (!present || !function_count || !timestamp ||
        !xx_pe_yx_is_pe(data, size)) return false;
    *present = xx_pe_yx_export_directory_read(data, size, &directory);
    *function_count = *present ? directory.function_count : 0;
    *timestamp = directory.timestamp;
    return true;
}

bool xx_pe_yx_export_dll_name(const uint8_t *data, size_t size,
                              const uint8_t **bytes, size_t *length)
{
    xx_pe_yx_export_directory directory;
    size_t offset, count = 0;
    if (!bytes || !length ||
        !xx_pe_yx_export_directory_read(data, size, &directory) ||
        !xx_pe_yx_offset_for_rva(data, size, directory.name_rva, &offset))
        return false;
    while (count < 512 && count < size - offset && data[offset + count]) {
        uint8_t ch = data[offset + count];
        if (ch < 0x20 || ch == 0x7f || ch == ' ' || ch == '"' ||
            ch == '*' || ch == '<' || ch == '>' || ch == '?' || ch == '|')
            return false;
        ++count;
    }
    if (!xx_pe_yx_utf8_valid(data + offset, count)) return false;
    *bytes = data + offset;
    *length = count;
    return true;
}

static bool xx_pe_yx_export_name_at(const uint8_t *data, size_t size,
                                    const xx_pe_yx_export_directory *directory,
                                    size_t index, const uint8_t **name,
                                    size_t *name_length)
{
    uint32_t name_rva = (uint32_t)xx_pe_yx_read(data,
        directory->names_offset + 4 * index, 4);
    size_t name_offset, length = 0;
    if (!xx_pe_yx_offset_for_rva(data, size, name_rva, &name_offset)) return false;
    while (length < 1024 && length < size - name_offset &&
           data[name_offset + length] != 0) ++length;
    if (!xx_pe_yx_utf8_valid(data + name_offset, length)) return false;
    *name = data + name_offset;
    *name_length = length;
    return true;
}

static bool xx_pe_yx_export_name(const uint8_t *data, size_t size,
                                 const xx_pe_yx_export_directory *directory,
                                 size_t function_index, const uint8_t **name,
                                 size_t *name_length)
{
    size_t ordinal_count = directory->name_count;
    if (ordinal_count > (size - directory->ordinals_offset) / 2)
        ordinal_count = (size - directory->ordinals_offset) / 2;
    for (size_t i = 0; i < ordinal_count; ++i) {
        if (xx_pe_yx_read(data, directory->ordinals_offset + 2 * i, 2) !=
            function_index) continue;
        return xx_pe_yx_export_name_at(data, size, directory, i, name,
                                       name_length);
    }
    return false;
}

bool xx_pe_yx_visit_exports(const uint8_t *data, size_t size,
                            xx_pe_yx_export_visitor visitor, void *opaque,
                            bool (*poll)(void *poll_opaque), void *poll_opaque)
{
    xx_pe_yx_export_directory directory;
    uint16_t *names;
    size_t ordinal_count;
    if (!xx_pe_yx_is_pe(data, size)) return true;
    if (!xx_pe_yx_export_directory_read(data, size, &directory)) return true;
    if (!visitor) return true;
    if (!directory.function_count) return true;
    names = calloc(directory.function_count, sizeof(*names));
    if (!names) return false;
    ordinal_count = directory.name_count;
    if (ordinal_count > (size - directory.ordinals_offset) / 2)
        ordinal_count = (size - directory.ordinals_offset) / 2;
    /* Keep the first ordinal-table occurrence, including an invalid name:
     * the reference does not fall through to later aliases in that case. */
    for (size_t i = 0; i < ordinal_count; ++i) {
        size_t function_index;
        if (poll && poll(poll_opaque)) { free(names); return false; }
        function_index = (size_t)xx_pe_yx_read(data,
            directory.ordinals_offset + 2 * i, 2);
        if (function_index < directory.function_count && !names[function_index])
            names[function_index] = (uint16_t)(i + 1);
    }
    for (size_t i = 0; i < directory.function_count; ++i) {
        xx_pe_yx_export entry;
        uint64_t mapped;
        uint64_t export_end = (uint64_t)directory.export_rva +
                              directory.export_size;
        memset(&entry, 0, sizeof(entry));
        if (poll && poll(poll_opaque)) { free(names); return false; }
        entry.ordinal = directory.ordinal_base + (uint32_t)i;
        entry.rva = (uint32_t)xx_pe_yx_read(data,
            directory.functions_offset + i * 4, 4);
        if (names[i])
            (void)xx_pe_yx_export_name_at(data, size, &directory,
                                          names[i] - 1,
                                          &entry.name, &entry.name_length);
        if (entry.rva >= directory.export_rva && entry.rva < export_end) {
            size_t offset;
            if (xx_pe_yx_offset_for_rva(data, size, entry.rva, &offset)) {
                size_t length = 0;
                while (length < 1024 && length < size - offset &&
                       data[offset + length]) ++length;
                if (xx_pe_yx_utf8_valid(data + offset, length)) {
                    entry.forward_name = data + offset;
                    entry.forward_name_length = length;
                }
            }
        } else if (xx_pe_yx_rva_to_offset(data, size, entry.rva, &mapped) &&
                   mapped <= UINT32_MAX) {
            entry.has_offset = true;
            entry.offset = (uint32_t)mapped;
        }
        visitor(opaque, &entry);
    }
    free(names);
    return true;
}

bool xx_pe_yx_exports(const uint8_t *data, size_t size,
                      const char *name, size_t name_length,
                      bool by_ordinal, uint64_t ordinal,
                      bool *found, uint64_t *index)
{
    xx_pe_yx_export_directory directory;
    if (!found || !index) return false;
    *found = false;
    *index = 0;
    if (!xx_pe_yx_is_pe(data, size)) return true;
    if (!xx_pe_yx_export_directory_read(data, size, &directory)) return true;
    for (size_t i = 0; i < directory.function_count; ++i) {
        const uint8_t *export_name;
        size_t export_length;
        if (by_ordinal) {
            if (ordinal != (uint64_t)directory.ordinal_base + i) continue;
        } else {
            if (!name || !xx_pe_yx_export_name(data, size, &directory, i,
                                               &export_name, &export_length) ||
                name_length != export_length) continue;
            bool equal = true;
            for (size_t j = 0; j < name_length; ++j) {
                unsigned char a = (unsigned char)name[j];
                unsigned char b = export_name[j];
                if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
                if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
                if (a != b) { equal = false; break; }
            }
            if (!equal) continue;
        }
        *found = true;
        *index = i;
        return true;
    }
    return true;
}

/* ------------------------------------------------------------ Imphash -- */

typedef struct {
    xx_md5_context hash;
    bool ok;
    bool first;
} xx_pe_yx_imphash_context;

static void xx_pe_yx_hash_update(xx_pe_yx_imphash_context *ctx,
                                 const void *bytes, size_t size)
{
    if (ctx->ok) xx_md5_update(&ctx->hash, bytes, size);
}

static void xx_pe_yx_hash_sink(void *opaque, const char *bytes, size_t size)
{
    xx_pe_yx_hash_update((xx_pe_yx_imphash_context *)opaque, bytes, size);
}

/* YARA-X's update_lowercase: an ASCII name is hashed as it is, or
 * lowercased byte-wise in 256-byte chunks; any other name takes Rust's
 * str::to_lowercase, streamed by xx_string_to_lower_stream. Import names
 * are valid UTF-8 here. */
static void xx_pe_yx_hash_lowercase(xx_pe_yx_imphash_context *ctx,
                                    const uint8_t *s, size_t length)
{
    uint8_t buf[256];
    bool upper = false;
    size_t ascii = 0;
    for (; ascii < length && s[ascii] < 0x80; ++ascii)
        if (s[ascii] >= 'A' && s[ascii] <= 'Z') upper = true;
    if (ascii < length) {
        xx_string_to_lower_stream(s, length, xx_pe_yx_hash_sink, ctx);
        return;
    }
    if (!upper) {
        if (length) xx_pe_yx_hash_update(ctx, s, length);
        return;
    }
    for (size_t offset = 0; offset < length;) {
        size_t n = length - offset < sizeof(buf) ?
                   length - offset : sizeof(buf);
        for (size_t i = 0; i < n; ++i)
            buf[i] = xx_pe_yx_ascii_lower(s[offset + i]);
        xx_pe_yx_hash_update(ctx, buf, n);
        offset += n;
    }
}

static void xx_pe_yx_imphash_visit(void *opaque,
                                   const uint8_t *dll, size_t dll_length,
                                   const uint8_t *function,
                                   size_t function_length,
                                   bool by_ordinal, uint16_t ordinal,
                                   uint32_t rva)
{
    xx_pe_yx_imphash_context *ctx = opaque;
    (void)by_ordinal;
    (void)ordinal;
    (void)rva;
    if (dll_length >= 4 &&
        (xx_pe_yx_ascii_equal(dll + dll_length - 4, 4, ".dll", 4) ||
         xx_pe_yx_ascii_equal(dll + dll_length - 4, 4, ".sys", 4) ||
         xx_pe_yx_ascii_equal(dll + dll_length - 4, 4, ".ocx", 4)))
        dll_length -= 4;
    if (!ctx->first) xx_pe_yx_hash_update(ctx, ",", 1);
    xx_pe_yx_hash_lowercase(ctx, dll, dll_length);
    xx_pe_yx_hash_update(ctx, ".", 1);
    xx_pe_yx_hash_lowercase(ctx, function, function_length);
    ctx->first = false;
}

bool xx_pe_yx_imphash(const uint8_t *data, size_t size, char out[33])
{
    xx_pe_yx_imphash_context ctx;
    uint8_t digest[16];
    static const char hexadecimal[] = "0123456789abcdef";
    bool ok;
    if (!out || !xx_pe_yx_is_pe(data, size)) return false;
    if (!xx_md5_init(&ctx.hash)) return false;
    ctx.ok = true;
    ctx.first = true;
    xx_pe_yx_visit_imports(data, size, 1u, xx_pe_yx_imphash_visit, &ctx);
    ok = ctx.ok && xx_md5_final(&ctx.hash, digest, sizeof(digest));
    if (!ok) return false;
    for (size_t i = 0; i < sizeof(digest); ++i) {
        out[2 * i] = hexadecimal[digest[i] >> 4];
        out[2 * i + 1] = hexadecimal[digest[i] & 15];
    }
    out[32] = '\0';
    return true;
}

/* ---------------------------------------------------------- Resources -- */

typedef struct {
    size_t offset; /* Relative to the first byte of the resource directory. */
    xx_pe_yx_resource_id ids[3];
    unsigned level;
} xx_pe_yx_resource_node;

static xx_pe_yx_resource_id xx_pe_yx_resource_parse_id(const uint8_t *data,
                                                       size_t section_offset,
                                                       size_t section_size,
                                                       uint32_t value)
{
    xx_pe_yx_resource_id id = {0};
    if (!(value & 0x80000000u)) {
        id.kind = XX_PE_YX_RESOURCE_NUMERIC;
        id.value = value;
    } else {
        size_t relative = (size_t)(value & 0x7fffffffu);
        if (relative <= section_size && section_size - relative >= 2) {
            size_t length = (size_t)xx_pe_yx_read(data, section_offset + relative,
                                                  2);
            if (length < 1000 && length <=
                (section_size - relative - 2) / 2) {
                id.kind = XX_PE_YX_RESOURCE_NAMED;
                id.name = data + section_offset + relative + 2;
                id.name_length = length * 2;
            }
        }
    }
    return id;
}

bool xx_pe_yx_visit_resources(const uint8_t *data, size_t size,
                              xx_pe_yx_resource_directory *result,
                              xx_pe_yx_resource_visitor visitor, void *opaque,
                              bool (*poll)(void *poll_opaque),
                              void *poll_opaque)
{
    size_t pe_offset, optional_offset, directory_offset, section_offset;
    size_t section_size, used = 0, head = 0, capacity = 16;
    uint16_t magic;
    uint32_t resource_rva;
    xx_pe_yx_resource_node *queue;
    if (!result || !xx_pe_yx_is_pe(data, size)) return false;
    memset(result, 0, sizeof(*result));
    pe_offset = (size_t)xx_pe_yx_read(data, 60, 4);
    optional_offset = pe_offset + 24;
    if (size - optional_offset < 2) return true;
    magic = (uint16_t)xx_pe_yx_read(data, optional_offset, 2);
    directory_offset = optional_offset + (magic == 0x20b ? 112u : 96u);
    if (directory_offset > size || size - directory_offset < 3u * 8u)
        return true;
    directory_offset += 2u * 8u;
    resource_rva = (uint32_t)xx_pe_yx_read(data, directory_offset, 4);
    if (!xx_pe_yx_offset_for_rva(data, size, resource_rva, &section_offset))
        return true;
    section_size = size - section_offset;
    queue = (xx_pe_yx_resource_node *)malloc(capacity * sizeof(*queue));
    if (!queue) return false;
    memset(&queue[used], 0, sizeof(*queue));
    ++used;
    while (head < used && head < 65536) {
        if (poll && poll(poll_opaque)) { free(queue); return false; }
        xx_pe_yx_resource_node node = queue[head++];
        size_t directory = node.offset, entries;
        if (directory > section_size || section_size - directory < 16)
            continue;
        directory += section_offset;
        if (xx_pe_yx_read(data, directory, 4) != 0) continue;
        if (xx_pe_yx_read(data, directory + 12, 2) > 32768 ||
            xx_pe_yx_read(data, directory + 14, 2) > 32768) continue;
        if (node.level == 0) {
            result->timestamp = (uint32_t)xx_pe_yx_read(data, directory + 4, 4);
            result->major_version = (uint16_t)xx_pe_yx_read(data, directory + 8, 2);
            result->minor_version = (uint16_t)xx_pe_yx_read(data, directory + 10, 2);
        }
        entries = (size_t)xx_pe_yx_read(data, directory + 12, 2) +
                  (size_t)xx_pe_yx_read(data, directory + 14, 2);
        if (entries > (size - directory - 16) / 8)
            entries = (size - directory - 16) / 8;
        for (size_t i = 0; i < entries; ++i) {
            if (poll && poll(poll_opaque)) { free(queue); return false; }
            size_t entry = directory + 16 + i * 8;
            uint32_t name_id = (uint32_t)xx_pe_yx_read(data, entry, 4);
            uint32_t target = (uint32_t)xx_pe_yx_read(data, entry + 4, 4);
            size_t relative = (size_t)(target & 0x7fffffffu);
            xx_pe_yx_resource_node child = node;
            size_t leaf;
            uint32_t rva, leaf_size;
            if (!relative || relative >= section_size) continue;
            if (node.level >= 3) continue;
            child.ids[node.level] = xx_pe_yx_resource_parse_id(
                data, section_offset, section_size, name_id);
            if (node.level == 2 &&
                child.ids[2].kind == XX_PE_YX_RESOURCE_NUMERIC &&
                child.ids[2].value > 0xfffffu) continue;
            if (target & 0x80000000u && node.level < 3 && used < 65536) {
                if (used == capacity) {
                    size_t new_capacity = capacity * 2;
                    xx_pe_yx_resource_node *grown = (xx_pe_yx_resource_node *)
                        realloc(queue, new_capacity * sizeof(*grown));
                    if (!grown) { free(queue); return false; }
                    queue = grown;
                    capacity = new_capacity;
                }
                child.offset = relative;
                child.level = node.level + 1;
                queue[used++] = child;
            }
            if (section_size - relative < 16) continue;
            leaf = section_offset + relative;
            rva = (uint32_t)xx_pe_yx_read(data, leaf, 4);
            leaf_size = (uint32_t)xx_pe_yx_read(data, leaf + 4, 4);
            if (!rva || !leaf_size || leaf_size >= 0x3fffffffu) continue;
            {
                xx_pe_yx_resource resource = {0};
                uint64_t mapped;
                resource.length = leaf_size;
                resource.rva = rva;
                resource.type = child.ids[0];
                resource.id = child.ids[1];
                resource.language = child.ids[2];
                if (xx_pe_yx_rva_to_offset(data, size, rva, &mapped) &&
                    mapped <= UINT32_MAX) {
                    resource.has_offset = true;
                    resource.offset = (uint32_t)mapped;
                }
                result->present = true;
                ++result->count;
                if (visitor) visitor(opaque, &resource);
                if (result->count == 65536) { free(queue); return true; }
            }
        }
    }
    free(queue);
    return true;
}

/* ------------------------------------------------------- Version info -- */

typedef struct {
    size_t next;
    size_t children;
    size_t end;
    uint16_t value_length;
} xx_pe_yx_version_node;

/* The version-information format stores nested, 4-byte-aligned UTF-16
 * records. The reference parser treats a missing terminator as the end of
 * the input, but requires a complete 2-byte code unit at each step. */
static bool xx_pe_yx_utf16_skip(const uint8_t *data, size_t end, size_t *position)
{
    size_t at = *position;
    while (end - at >= 2) {
        uint16_t ch = (uint16_t)xx_pe_yx_read(data, at, 2);
        at += 2;
        if (!ch) { *position = at; return true; }
    }
    if (at != end) return false;
    *position = at;
    return true;
}

static bool xx_pe_yx_utf16_key(const uint8_t *data, size_t start, size_t end,
                               const char *key)
{
    size_t i = 0, at = start;
    while (at + 1 < end && key[i] &&
           data[at] == (uint8_t)key[i] && data[at + 1] == 0) {
        at += 2;
        ++i;
    }
    return key[i] == 0 && at + 1 < end &&
           data[at] == 0 && data[at + 1] == 0;
}

static bool xx_pe_yx_version_parse_node(const uint8_t *data, size_t size,
                                        size_t at, const char *expected_key,
                                        xx_pe_yx_version_node *node)
{
    size_t length, end, key_start, key_end, aligned;
    if (at > size || size - at < 6) return false;
    length = ((size_t)xx_pe_yx_read(data, at, 2) + 3u) & ~(size_t)3u;
    if (!length || length > size - at || length < 6) return false;
    end = at + length;
    key_start = at + 6;
    key_end = key_start;
    if (!xx_pe_yx_utf16_skip(data, end, &key_end)) return false;
    if (expected_key && !xx_pe_yx_utf16_key(data, key_start, key_end,
                                            expected_key)) return false;
    aligned = (key_end - at + 3u) & ~(size_t)3u;
    if (aligned > length) return false;
    node->next = end;
    node->children = at + aligned;
    node->end = end;
    node->value_length = (uint16_t)xx_pe_yx_read(data, at + 2, 2);
    return true;
}

static char *xx_pe_yx_utf16_to_utf8(const uint8_t *data, size_t start,
                                    size_t end, size_t *length)
{
    size_t units, used = 0;
    char *text;
    if (end < start || (end - start) / 2 > (SIZE_MAX - 1) / 3)
        return NULL;
    units = (end - start) / 2;
    text = (char *)malloc(units * 3 + 1);
    if (!text) return NULL;
    for (size_t i = 0; i < units; ++i) {
        uint32_t ch = (uint32_t)xx_pe_yx_read(data, start + i * 2, 2);
        if (!ch) break;
        if (ch >= 0xd800 && ch <= 0xdbff && i + 1 < units) {
            uint32_t lo = (uint32_t)xx_pe_yx_read(data, start + (i + 1) * 2, 2);
            if (lo >= 0xdc00 && lo <= 0xdfff) {
                ch = 0x10000u + ((ch - 0xd800u) << 10) + lo - 0xdc00u;
                ++i;
            } else ch = 0xfffdu;
        } else if (ch >= 0xd800 && ch <= 0xdfff) ch = 0xfffdu;
        if (ch < 0x80) text[used++] = (char)ch;
        else if (ch < 0x800) {
            text[used++] = (char)(0xc0u | (ch >> 6));
            text[used++] = (char)(0x80u | (ch & 0x3fu));
        } else if (ch < 0x10000) {
            text[used++] = (char)(0xe0u | (ch >> 12));
            text[used++] = (char)(0x80u | ((ch >> 6) & 0x3fu));
            text[used++] = (char)(0x80u | (ch & 0x3fu));
        } else {
            text[used++] = (char)(0xf0u | (ch >> 18));
            text[used++] = (char)(0x80u | ((ch >> 12) & 0x3fu));
            text[used++] = (char)(0x80u | ((ch >> 6) & 0x3fu));
            text[used++] = (char)(0x80u | (ch & 0x3fu));
        }
    }
    text[used] = 0;
    *length = used;
    return text;
}

static bool xx_pe_yx_version_parse_table(const uint8_t *data, size_t size,
                                         size_t at, size_t *next,
                                         uint64_t *strings,
                                         xx_pe_yx_version_visitor visitor,
                                         void *opaque,
                                         bool (*poll)(void *poll_opaque),
                                         void *poll_opaque)
{
    xx_pe_yx_version_node table, item;
    size_t cursor;
    uint64_t count = 0;
    if (!xx_pe_yx_version_parse_node(data, size, at, NULL, &table)) return false;
    cursor = table.children;
    while (cursor < table.end &&
           xx_pe_yx_version_parse_node(data, table.end, cursor, NULL, &item)) {
        if (poll && poll(poll_opaque)) return false;
        /* The value parser is optional in upstream. A malformed string value
         * becomes an empty value without discarding the key/value record. */
        if (visitor) {
            size_t key_length, value_length = 0;
            size_t key_end = cursor + 6, value_end = item.children;
            char *key, *value;
            if (!xx_pe_yx_utf16_skip(data, item.children, &key_end)) return false;
            key = xx_pe_yx_utf16_to_utf8(data, cursor + 6, key_end,
                                         &key_length);
            if (!key) return false;
            if (item.value_length &&
                xx_pe_yx_utf16_skip(data, item.end, &value_end))
                value = xx_pe_yx_utf16_to_utf8(data, item.children, value_end,
                                               &value_length);
            else value = xx_pe_yx_utf16_to_utf8(data, item.children,
                                                item.children, &value_length);
            if (!value) { free(key); return false; }
            visitor(opaque, key, key_length, value, value_length);
            free(key);
            free(value);
        }
        if (item.value_length) {
            size_t value_end = item.children;
            (void)xx_pe_yx_utf16_skip(data, item.end, &value_end);
        }
        ++count;
        cursor = item.next;
    }
    if (!count) return false;
    *next = table.next;
    *strings = count;
    return true;
}

static bool xx_pe_yx_version_parse_strings(const uint8_t *data, size_t size,
                                           size_t at, size_t *next,
                                           uint64_t *strings,
                                           xx_pe_yx_version_visitor visitor,
                                           void *opaque,
                                           bool (*poll)(void *poll_opaque),
                                           void *poll_opaque)
{
    xx_pe_yx_version_node info;
    size_t cursor;
    uint64_t count = 0;
    if (!xx_pe_yx_version_parse_node(data, size, at, "StringFileInfo", &info))
        return false;
    cursor = info.children;
    while (cursor < info.end) {
        size_t table_next;
        uint64_t table_count;
        if (!xx_pe_yx_version_parse_table(data, info.end, cursor, &table_next,
                                          &table_count, visitor, opaque,
                                          poll, poll_opaque)) break;
        count += table_count;
        cursor = table_next;
    }
    if (!count) return false;
    *next = info.next;
    *strings = count;
    return true;
}

uint64_t xx_pe_yx_version_walk_at(const uint8_t *data, size_t size,
                                  size_t offset,
                                  xx_pe_yx_version_visitor visitor,
                                  void *opaque,
                                  bool (*poll)(void *poll_opaque),
                                  void *poll_opaque)
{
    xx_pe_yx_version_node root, other;
    size_t cursor;
    uint64_t result = 0;
    bool seen_strings = false, seen_var = false;
    if (offset >= size || !xx_pe_yx_version_parse_node(data, size, offset,
                                                       "VS_VERSION_INFO", &root))
        return 0;
    cursor = root.children;
    if (root.value_length && root.end - cursor >= 52) cursor += 52;
    /* `permutation((opt(VarFileInfo), opt(StringFileInfo)))` accepts either
     * child order, with each optional child appearing at most once. */
    for (unsigned i = 0; i < 2 && cursor < root.end; ++i) {
        size_t next;
        uint64_t strings;
        if (!seen_var && xx_pe_yx_version_parse_node(data, root.end, cursor,
                                                     "VarFileInfo", &other)) {
            seen_var = true;
            cursor = other.next;
        } else if (!seen_strings &&
                   xx_pe_yx_version_parse_strings(data, root.end, cursor,
                                                  &next, &strings, visitor,
                                                  opaque, poll, poll_opaque)) {
            seen_strings = true;
            result += strings;
            cursor = next;
        } else break;
    }
    return result;
}

typedef struct {
    const uint8_t *data;
    size_t size;
    xx_pe_yx_version_visitor visitor;
    void *opaque;
} xx_pe_yx_version_context;

static void xx_pe_yx_visit_version_resource(void *opaque,
                                            const xx_pe_yx_resource *resource)
{
    xx_pe_yx_version_context *ctx = opaque;
    if (resource->type.kind == XX_PE_YX_RESOURCE_NUMERIC &&
        resource->type.value == 16 && resource->has_offset &&
        resource->offset < ctx->size)
        (void)xx_pe_yx_version_walk_at(ctx->data, ctx->size, resource->offset,
                                       ctx->visitor, ctx->opaque, NULL, NULL);
}

bool xx_pe_yx_visit_version_info(const uint8_t *data, size_t size,
                                 xx_pe_yx_version_visitor visitor,
                                 void *opaque,
                                 bool (*poll)(void *poll_opaque),
                                 void *poll_opaque)
{
    xx_pe_yx_resource_directory directory;
    xx_pe_yx_version_context ctx = {data, size, visitor, opaque};
    return xx_pe_yx_visit_resources(data, size, &directory,
                                    xx_pe_yx_visit_version_resource, &ctx,
                                    poll, poll_opaque);
}
