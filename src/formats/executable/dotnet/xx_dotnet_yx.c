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

#include <xxfclib/formats/executable/dotnet/xx_dotnet_yx.h>

#include "xxfclib/formats/executable/pe/xx_pe_yx.h"
#include "../pe/xx_pe_yx_internal.h"

#include <limits.h>
#include <string.h>

static bool xx_dotnet_yx_u16(const uint8_t *data, size_t size, size_t offset,
                           uint16_t *value)
{
    if (offset > size || size - offset < 2) return false;
    *value = (uint16_t)data[offset] | (uint16_t)data[offset + 1] << 8;
    return true;
}

static bool xx_dotnet_yx_u32(const uint8_t *data, size_t size, size_t offset,
                           uint32_t *value)
{
    if (offset > size || size - offset < 4) return false;
    *value = (uint32_t)data[offset] | (uint32_t)data[offset + 1] << 8 |
             (uint32_t)data[offset + 2] << 16 |
             (uint32_t)data[offset + 3] << 24;
    return true;
}

static bool xx_dotnet_yx_u64(const uint8_t *data, size_t size, size_t offset,
                           uint64_t *value)
{
    uint32_t lo, hi;
    if (!xx_dotnet_yx_u32(data, size, offset, &lo) ||
        !xx_dotnet_yx_u32(data, size, offset + 4, &hi)) return false;
    *value = (uint64_t)lo | (uint64_t)hi << 32;
    return true;
}

static bool xx_dotnet_yx_utf8(const uint8_t *bytes, size_t length)
{
    for (size_t i = 0; i < length;) {
        uint8_t first = bytes[i];
        size_t width;
        uint32_t codepoint;
        if (first < 0x80) { ++i; continue; }
        if (first >= 0xc2 && first <= 0xdf) {
            width = 2;
            codepoint = first & 0x1fu;
        } else if (first >= 0xe0 && first <= 0xef) {
            width = 3;
            codepoint = first & 0x0fu;
        } else if (first >= 0xf0 && first <= 0xf4) {
            width = 4;
            codepoint = first & 0x07u;
        } else return false;
        if (width > length - i) return false;
        for (size_t j = 1; j < width; ++j) {
            if ((bytes[i + j] & 0xc0u) != 0x80u) return false;
            codepoint = (codepoint << 6) | (bytes[i + j] & 0x3fu);
        }
        if ((width == 2 && codepoint < 0x80) ||
            (width == 3 && codepoint < 0x800) ||
            (width == 4 && codepoint < 0x10000) ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
            codepoint > 0x10ffff) return false;
        i += width;
    }
    return true;
}

static bool xx_dotnet_yx_rva_offset(const uint8_t *data, size_t size,
                                  uint32_t rva, size_t *offset)
{
    uint64_t mapped;
    if (!xx_pe_yx_rva_to_offset(data, size, rva, &mapped) || mapped >= size)
        return false;
    *offset = (size_t)mapped;
    return true;
}

static bool xx_dotnet_yx_get_stream(const uint8_t *data, size_t size,
                                  const xx_dotnet_yx_stream *stream,
                                  const uint8_t **bytes, size_t *length)
{
    if ((size_t)stream->offset > size ||
        (size_t)stream->size > size - (size_t)stream->offset) return false;
    *bytes = data + stream->offset;
    *length = stream->size;
    return true;
}

static const uint8_t *xx_dotnet_yx_heap_string(const uint8_t *strings,
                                            size_t strings_size,
                                            uint32_t index,
                                            size_t *length)
{
    const uint8_t *start, *end;
    if (index >= strings_size) return NULL;
    start = strings + index;
    end = memchr(start, 0, strings_size - index);
    if (!end || !xx_dotnet_yx_utf8(start, (size_t)(end - start))) return NULL;
    *length = (size_t)(end - start);
    return start;
}

static bool xx_dotnet_yx_next_user_string(const uint8_t *bytes, size_t length,
                                        size_t *at, const uint8_t **value,
                                        size_t *value_length,
                                        bool *truncated_payload)
{
    uint32_t n;
    uint8_t first;
    if (truncated_payload) *truncated_payload = false;
    if (*at >= length) return false;
    first = bytes[(*at)++];
    if ((first & 0x80u) == 0) n = first;
    else if ((first & 0xc0u) == 0x80u) {
        if (length - *at < 1) return false;
        n = ((uint32_t)(first & 0x3fu) << 8) | bytes[(*at)++];
    } else if ((first & 0xe0u) == 0xc0u) {
        if (length - *at < 3) return false;
        n = ((uint32_t)(first & 0x1fu) << 24) |
            ((uint32_t)bytes[*at] << 16) |
            ((uint32_t)bytes[*at + 1] << 8) | bytes[*at + 2];
        *at += 3;
    } else return false;
    if (n > length - *at) {
        if (truncated_payload) *truncated_payload = true;
        return false;
    }
    *value = bytes + *at;
    *value_length = n;
    *at += n;
    return true;
}

static size_t xx_dotnet_yx_user_string_count(const uint8_t *data, size_t size,
                                           const xx_dotnet_yx_stream *stream)
{
    const uint8_t *bytes;
    size_t length, at = 0, count = 0;
    if (!stream || !xx_dotnet_yx_get_stream(data, size, stream,
                                         &bytes, &length)) return 0;
    while (at < length) {
        const uint8_t *value;
        size_t value_length;
        bool truncated_payload;
        if (!xx_dotnet_yx_next_user_string(bytes, length, &at,
                                         &value, &value_length,
                                         &truncated_payload)) {
            /* A complete length prefix with too few bytes invalidates the
             * entire #US parse, including earlier strings. */
            if (truncated_payload) return 0;
            break;
        }
        (void)value;
        if (value_length >= 3) ++count;
    }
    return count;
}

static size_t xx_dotnet_yx_coded_width(const uint32_t rows[64],
                                    const uint8_t *tables, size_t count,
                                    uint32_t limit)
{
    for (size_t i = 0; i < count; ++i)
        if (rows[tables[i]] > limit) return 4;
    return 2;
}

static size_t xx_dotnet_yx_row_size(unsigned table, const uint32_t rows[64],
                                  size_t s, size_t b, size_t g)
{
    static const uint8_t member_ref_parent[] = {1, 2, 26, 6, 27};
    static const uint8_t custom_attribute_type[] = {6, 10};
    static const uint8_t has_custom_attribute[] = {
        6, 4, 1, 2, 8, 9, 10, 0, 23, 20, 17, 26, 27,
        32, 35, 38, 39, 40, 42, 44, 43
    };
    switch (table) {
    case 0: return 2 + s + 3 * g;
    case 1: return 2 + 2 * s;
    case 2: return 4 + 2 * s + 6;
    case 3: case 5: case 7: case 19: case 22: return 2;
    case 4: case 23: return 2 + s + b;
    case 6: return 10 + s + b;
    case 8: return 4 + s;
    case 9: case 18: case 21: return 4;
    case 10: return xx_dotnet_yx_coded_width(rows, member_ref_parent,
                   sizeof(member_ref_parent), 8192) + s + b;
    case 11: return 4 + b;
    case 12: return xx_dotnet_yx_coded_width(rows, has_custom_attribute,
                   sizeof(has_custom_attribute), 2048) +
                   xx_dotnet_yx_coded_width(rows, custom_attribute_type,
                   sizeof(custom_attribute_type), 8192) + b;
    case 13: return 2 + b;
    case 14: return 4 + b;
    case 15: return 8;
    case 16: return 6;
    case 17: case 27: return b;
    case 20: return 4 + s;
    case 24: return 6;
    case 25: return 6;
    case 26: return s;
    case 28: return 6 + s;
    case 29: return 6;
    case 30: return 8;
    case 31: case 33: return 4;
    case 32: return 16 + b + 2 * s;
    case 34: return 12;
    case 35: return 12 + 2 * b + 2 * s;
    case 36: return 6;
    case 37: return 14;
    case 38: return 4 + s + b;
    case 39: return 10 + 2 * s;
    case 40: return 10 + s;
    case 41: return 4;
    case 42: return 6 + s;
    default: return 0;
    }
}

static bool xx_dotnet_yx_constant_type_valid(uint8_t type)
{
    if (type <= 0x15u) return true;
    switch (type) {
    case 0x18: case 0x19:
    case 0x1b: case 0x1c: case 0x1d: case 0x1e:
    case 0x1f: case 0x20: case 0x21:
    case 0x40: case 0x41: case 0x45:
        return true;
    default:
        return false;
    }
}

static bool xx_dotnet_yx_blob_value(const uint8_t *bytes, size_t length,
                                  uint32_t index, const uint8_t **value,
                                  size_t *value_length)
{
    size_t at = index;
    uint32_t n;
    uint8_t first;
    if (at >= length) return false;
    first = bytes[at++];
    if ((first & 0x80u) == 0) n = first;
    else if ((first & 0xc0u) == 0x80u) {
        if (length - at < 1) return false;
        n = ((uint32_t)(first & 0x3fu) << 8) | bytes[at++];
    } else if ((first & 0xe0u) == 0xc0u) {
        if (length - at < 3) return false;
        n = ((uint32_t)(first & 0x1fu) << 24) |
            ((uint32_t)bytes[at] << 16) |
            ((uint32_t)bytes[at + 1] << 8) | bytes[at + 2];
        at += 3;
    } else return false;
    if (n > length - at) return false;
    if (value) *value = bytes + at;
    if (value_length) *value_length = n;
    return true;
}

static bool xx_dotnet_yx_index(const uint8_t *bytes, size_t length,
                             size_t at, size_t width, uint32_t *value)
{
    if (width == 4) return xx_dotnet_yx_u32(bytes, length, at, value);
    if (width == 2) {
        uint16_t small;
        if (!xx_dotnet_yx_u16(bytes, length, at, &small)) return false;
        *value = small;
        return true;
    }
    return false;
}

static bool xx_dotnet_yx_version_at(const uint8_t *bytes, size_t length,
                                  size_t at, xx_dotnet_yx_version *version)
{
    return xx_dotnet_yx_u16(bytes, length, at, &version->major) &&
           xx_dotnet_yx_u16(bytes, length, at + 2, &version->minor) &&
           xx_dotnet_yx_u16(bytes, length, at + 4,
                          &version->build_number) &&
           xx_dotnet_yx_u16(bytes, length, at + 6,
                          &version->revision_number);
}

static void xx_dotnet_yx_typelib(const uint8_t *type_refs,
                               size_t type_ref_count, size_t type_ref_width,
                               const uint8_t *member_refs,
                               size_t member_ref_count,
                               size_t member_ref_width,
                               const uint8_t *attributes,
                               size_t attribute_count,
                               size_t attribute_width,
                               size_t parent_width, size_t type_width,
                               xx_dotnet_yx_result *result)
{
    size_t member_class_width = member_ref_width -
                                result->string_index_size -
                                result->blob_index_size;
    if (!type_refs || !member_refs || !attributes ||
        type_ref_width < (size_t)2 + result->string_index_size ||
        member_class_width < 2 ||
        attribute_width < parent_width + type_width +
                          result->blob_index_size) return;
    for (size_t i = 0; i < attribute_count; ++i) {
        const uint8_t *attribute = attributes + i * attribute_width;
        const uint8_t *member, *type_ref, *name, *blob;
        uint32_t parent, type, class_index, name_index, blob_index;
        size_t member_index, type_ref_index, name_length, blob_length;
        size_t text_length;
        if (!xx_dotnet_yx_index(attribute, attribute_width, 0,
                              parent_width, &parent) ||
            !xx_dotnet_yx_index(attribute, attribute_width, parent_width,
                              type_width, &type) ||
            (parent & 31u) != 14u || (type & 7u) != 3u)
            continue;
        member_index = type >> 3;
        if (member_index) --member_index;
        if (member_index >= member_ref_count) continue;
        member = member_refs + member_index * member_ref_width;
        if (!xx_dotnet_yx_index(member, member_ref_width, 0,
                              member_class_width, &class_index) ||
            (class_index & 7u) != 1u) continue;
        type_ref_index = class_index >> 3;
        if (type_ref_index) --type_ref_index;
        if (type_ref_index >= type_ref_count) continue;
        type_ref = type_refs + type_ref_index * type_ref_width;
        if (!xx_dotnet_yx_index(type_ref, type_ref_width, 2,
                              result->string_index_size,
                              &name_index)) continue;
        name = xx_dotnet_yx_heap_string(result->strings_heap,
                                      result->strings_heap_size,
                                      name_index, &name_length);
        if (!name || name_length != 13 ||
            memcmp(name, "GuidAttribute", 13) != 0) continue;
        if (!result->blob_heap ||
            !xx_dotnet_yx_index(attribute, attribute_width,
                              parent_width + type_width,
                              result->blob_index_size, &blob_index) ||
            !xx_dotnet_yx_blob_value(result->blob_heap,
                                   result->blob_heap_size, blob_index,
                                   &blob, &blob_length) || blob_length < 3)
            return;
        text_length = blob[2];
        if (text_length > blob_length - 3 ||
            !xx_dotnet_yx_utf8(blob + 3, text_length)) return;
        result->typelib = blob + 3;
        result->typelib_length = text_length;
        return;
    }
}

static void xx_dotnet_yx_module_name(const uint8_t *data, size_t size,
                                   const xx_dotnet_yx_stream *table_stream,
                                   const xx_dotnet_yx_stream *strings_stream,
                                   const xx_dotnet_yx_stream *blob_stream,
                                   xx_dotnet_yx_result *result)
{
    const uint8_t *tables, *strings = NULL, *blobs = NULL;
    size_t tables_size, strings_size = 0, blobs_size = 0, at;
    uint64_t valid;
    uint32_t rows[64] = {0};
    size_t string_index_size, guid_index_size, row_length, name_length;
    const uint8_t *type_refs = NULL, *member_refs = NULL,
                  *attributes = NULL;
    size_t type_ref_width = 0, member_ref_width = 0,
           attribute_width = 0;
    uint32_t name_index;
    if (!table_stream ||
        !xx_dotnet_yx_get_stream(data, size, table_stream,
                               &tables, &tables_size) ||
        tables_size < 24 ||
        !xx_dotnet_yx_u64(tables, tables_size, 8, &valid)) return;
    if (strings_stream)
        (void)xx_dotnet_yx_get_stream(data, size, strings_stream,
                                    &strings, &strings_size);
    if (blob_stream)
        (void)xx_dotnet_yx_get_stream(data, size, blob_stream,
                                    &blobs, &blobs_size);
    result->strings_heap = strings;
    result->strings_heap_size = strings_size;
    result->blob_heap = blobs;
    result->blob_heap_size = blobs_size;
    at = 24;
    for (unsigned table = 0; table < 64; ++table) {
        uint32_t count;
        if (!(valid & (UINT64_C(1) << table))) continue;
        if (!xx_dotnet_yx_u32(tables, tables_size, at, &count)) return;
        /* Upstream caps counts before computing table widths and offsets. */
        rows[table] = count > 15000u ? 15000u : count;
        at += 4;
    }
    string_index_size = (tables[6] & 1u) ? 4u : 2u;
    guid_index_size = (tables[6] & 2u) ? 4u : 2u;
    row_length = 2 + string_index_size + guid_index_size * 3;
    if (at > tables_size || rows[0] > (tables_size - at) / row_length)
        return;
    if (rows[0]) {
        if (string_index_size == 4) {
            if (!xx_dotnet_yx_u32(tables, tables_size, at + 2, &name_index))
                return;
        } else {
            uint16_t small;
            if (!xx_dotnet_yx_u16(tables, tables_size, at + 2, &small)) return;
            name_index = small;
        }
        result->module_name = xx_dotnet_yx_heap_string(
            strings, strings_size, name_index, &name_length);
        if (result->module_name) result->module_name_length = name_length;
    }
    at += rows[0] * row_length;
    row_length = 2 + string_index_size * 2; /* TypeRef */
    if (rows[1] > (tables_size - at) / row_length) return;
    type_refs = tables + at;
    type_ref_width = row_length;
    result->type_ref_rows = type_refs;
    result->type_ref_row_count = rows[1];
    result->type_ref_row_size = row_length;
    at += rows[1] * row_length;
    row_length = 4 + string_index_size * 2 + 2 + 2 + 2; /* TypeDef */
    if (rows[2] > (tables_size - at) / row_length) return;
    /* A TypeDefOrRef coded index has three possible targets. A tag of 3
     * invalidates the whole TypeDef table, including earlier rows. */
    for (uint32_t i = 0; i < rows[2]; ++i) {
        uint16_t extends;
        if (!xx_dotnet_yx_u16(tables, tables_size,
                            at + (size_t)i * row_length + 4 +
                            string_index_size * 2, &extends) ||
            (extends & 3u) == 3u) return;
    }
    result->type_def_rows = tables + at;
    result->type_def_row_count = rows[2];
    result->type_def_row_size = row_length;
    for (uint32_t i = 0; i < rows[2]; ++i) {
        const uint8_t *name;
        if (string_index_size == 4) {
            if (!xx_dotnet_yx_u32(tables, tables_size, at + 4, &name_index))
                return;
        } else {
            uint16_t small;
            if (!xx_dotnet_yx_u16(tables, tables_size, at + 4, &small)) return;
            name_index = small;
        }
        name = xx_dotnet_yx_heap_string(strings, strings_size,
                                      name_index, &name_length);
        if (name && name_length &&
            (name_length != 8 || memcmp(name, "<Module>", 8) != 0))
            ++result->class_count;
        at += row_length;
    }
    /* Later tables can still be valid when all names in #Strings are
     * unavailable. Continue to parse Assembly, AssemblyRef, and others. */
    {
        size_t blob_index_size = (tables[6] & 4u) ? 4u : 2u;
        result->string_index_size = (uint8_t)string_index_size;
        result->blob_index_size = (uint8_t)blob_index_size;
        for (unsigned table = 3; table <= 42; ++table) {
            size_t width = xx_dotnet_yx_row_size(table, rows,
                                               string_index_size,
                                               blob_index_size,
                                               guid_index_size);
            if (!width || at > tables_size ||
                rows[table] > (tables_size - at) / width) return;
            if (table == 9 || table == 14 || table == 20 ||
                table == 39 || table == 40) {
                size_t coded_column = 2;
                bool invalid_coded_index = false;
                /* The row cap keeps these coded indexes 16-bit. Validate
                 * the entire table before retaining it, and keep earlier
                 * metadata when a reserved tag rejects this table. */
                if (table == 20) coded_column = 2 + string_index_size;
                else if (table == 39)
                    coded_column = 8 + 2 * string_index_size;
                else if (table == 40)
                    coded_column = 8 + string_index_size;
                for (uint32_t i = 0; i < rows[table]; ++i) {
                    uint16_t coded;
                    if (!xx_dotnet_yx_u16(tables, tables_size,
                                        at + (size_t)i * width +
                                        coded_column, &coded) ||
                        (coded & 3u) == 3u) {
                        invalid_coded_index = true;
                        break;
                    }
                }
                if (invalid_coded_index) break;
            }
            if (table == 10) {
                size_t coded_width = width - string_index_size -
                                     blob_index_size;
                for (uint32_t i = 0; i < rows[table]; ++i) {
                    uint32_t parent;
                    if (!xx_dotnet_yx_index(tables, tables_size,
                                          at + (size_t)i * width,
                                          coded_width, &parent) ||
                        (parent & 7u) >= 5u) return;
                }
                member_refs = tables + at;
                member_ref_width = width;
            }
            if (table == 9) {
                result->interface_impl_rows = tables + at;
                result->interface_impl_row_count = rows[table];
            }
            if (table == 6) {
                result->method_def_rows = tables + at;
                result->method_def_row_count = rows[table];
                result->method_def_row_size = width;
            }
            if (table == 8) {
                result->param_rows = tables + at;
                result->param_row_count = rows[table];
                result->param_row_size = width;
            }
            if (table == 12) {
                size_t type_width = rows[6] > 8192u || rows[10] > 8192u
                                  ? 4u : 2u;
                size_t parent_width = width - type_width - blob_index_size;
                for (uint32_t i = 0; i < rows[table]; ++i) {
                    size_t row = at + (size_t)i * width;
                    uint32_t parent, type;
                    if (!xx_dotnet_yx_index(tables, tables_size, row,
                                          parent_width, &parent) ||
                        (parent & 31u) >= 22u ||
                        !xx_dotnet_yx_index(tables, tables_size,
                                          row + parent_width, type_width,
                                          &type) ||
                        (type & 7u) >= 5u) return;
                }
                attributes = tables + at;
                attribute_width = width;
                /* MO-10: upstream computes dotnet.typelib lazily
                 * (get_typelib/parse_typelib, parser.rs:255,772) from
                 * TypeRef(1), MemberRef(10) and CustomAttribute(12) alone,
                 * so it survives a later table (13..42) overflowing the
                 * stream. Do it here, as soon as all three are available,
                 * instead of after the loop over every table, which
                 * returns before reaching there whenever a later table
                 * fails validation. */
                {
                    static const uint8_t has_custom_attribute[] = {
                        6, 4, 1, 2, 8, 9, 10, 0, 23, 20, 17,
                        26, 27, 32, 35, 38, 39, 40, 42, 44, 43
                    };
                    static const uint8_t custom_attribute_type[] = {6, 10};
                    size_t parent_width = xx_dotnet_yx_coded_width(
                        rows, has_custom_attribute,
                        sizeof(has_custom_attribute), 2048);
                    size_t type_width = xx_dotnet_yx_coded_width(
                        rows, custom_attribute_type,
                        sizeof(custom_attribute_type), 8192);
                    xx_dotnet_yx_typelib(type_refs, rows[1], type_ref_width,
                                       member_refs, rows[10],
                                       member_ref_width, attributes,
                                       rows[12], attribute_width,
                                       parent_width, type_width, result);
                }
            }
            if (table == 11) {
                /* A malformed Constant row prevents later tables from
                 * being parsed. Validate the complete table first. */
                for (uint32_t i = 0; i < rows[table]; ++i) {
                    size_t row = at + (size_t)i * width;
                    uint16_t parent;
                    if (!xx_dotnet_yx_constant_type_valid(tables[row]) ||
                        !xx_dotnet_yx_u16(tables, tables_size, row + 2,
                                         &parent) ||
                        (parent & 3u) == 3u) return;
                }
                if (blobs) {
                    result->constant_rows = tables + at;
                    result->constant_row_count = rows[table];
                    result->constant_row_size = width;
                    for (uint32_t i = 0; i < rows[table]; ++i) {
                        uint32_t blob_index;
                        size_t row = at + (size_t)i * width;
                        if (tables[row] != 0x0e) continue;
                        if (blob_index_size == 4) {
                            if (!xx_dotnet_yx_u32(tables, tables_size, row + 4,
                                                &blob_index)) continue;
                        } else {
                            uint16_t small;
                            if (!xx_dotnet_yx_u16(tables, tables_size, row + 4,
                                                &small)) continue;
                            blob_index = small;
                        }
                        if (xx_dotnet_yx_blob_value(blobs, blobs_size,
                                                  blob_index, NULL, NULL))
                            ++result->constant_count;
                    }
                }
            }
            if (table == 26) {
                result->module_ref_rows = tables + at;
                result->module_ref_row_count = rows[table];
                for (uint32_t i = 0; i < rows[table]; ++i) {
                    uint32_t string_index;
                    size_t string_length;
                    if (xx_dotnet_yx_index(tables, tables_size,
                                         at + i * width, width,
                                         &string_index) &&
                        xx_dotnet_yx_heap_string(strings, strings_size,
                                                string_index,
                                                &string_length))
                        ++result->module_ref_count;
                }
            }
            if (table == 27) {
                result->type_spec_rows = tables + at;
                result->type_spec_row_count = rows[table];
            }
            if (table == 29) {
                result->field_rva_rows = tables + at;
                result->field_rva_row_count = rows[table];
                for (uint32_t i = 0; i < rows[table]; ++i) {
                    uint32_t rva;
                    uint64_t offset;
                    if (xx_dotnet_yx_u32(tables, tables_size,
                                       at + i * width, &rva) &&
                        xx_pe_yx_rva_to_offset(data, size, rva, &offset) &&
                        offset <= UINT32_MAX)
                        ++result->field_offset_count;
                }
            }
            if (table == 32 && rows[table]) {
                uint32_t culture_index;
                size_t culture_length;
                xx_dotnet_yx_assembly *assembly = &result->assembly;
                assembly->present = xx_dotnet_yx_version_at(
                    tables, tables_size, at + 4, &assembly->version);
                if (!assembly->present) return;
                if (xx_dotnet_yx_index(tables, tables_size,
                                     at + 16 + blob_index_size,
                                     string_index_size, &name_index)) {
                    assembly->name = xx_dotnet_yx_heap_string(
                        strings, strings_size, name_index, &name_length);
                    if (assembly->name) assembly->name_length = name_length;
                }
                if (xx_dotnet_yx_index(tables, tables_size,
                                     at + 16 + blob_index_size +
                                     string_index_size, string_index_size,
                                     &culture_index)) {
                    assembly->culture = xx_dotnet_yx_heap_string(
                        strings, strings_size, culture_index,
                        &culture_length);
                    if (assembly->culture && culture_length)
                        assembly->culture_length = culture_length;
                    else assembly->culture = NULL;
                }
            }
            if (table == 35) {
                result->assembly_ref_rows = tables + at;
                result->assembly_ref_row_size = width;
                result->assembly_ref_count = rows[table];
            }
            if (table == 40) {
                result->resource_rows = tables + at;
                result->resource_row_size = width;
                result->resource_count = rows[table];
            }
            if (table == 41) {
                result->nested_class_rows = tables + at;
                result->nested_class_row_count = rows[table];
            }
            if (table == 42) {
                result->generic_param_rows = tables + at;
                result->generic_param_row_count = rows[table];
                result->generic_param_row_size = width;
            }
            at += rows[table] * width;
        }
    }
}

bool xx_dotnet_yx_parse(const uint8_t *data, size_t size,
                      xx_dotnet_yx_result *result)
{
    uint32_t pe_offset, metadata_rva, metadata_size, cli_rva;
    uint32_t resources_rva, resources_size;
    uint16_t magic, stream_count;
    size_t opt, directory, cli_offset, metadata_offset, metadata_available;
    size_t at;
    uint64_t sum;
    const xx_dotnet_yx_stream *table_stream = NULL, *strings_stream = NULL,
                           *guid_stream = NULL, *us_stream = NULL,
                           *blob_stream = NULL;
    if (!result) return false;
    memset(result, 0, sizeof(*result));
    if (!xx_pe_yx_is_pe(data, size) ||
        !xx_dotnet_yx_u32(data, size, 60, &pe_offset) ||
        (size_t)pe_offset > size || size - (size_t)pe_offset < 24)
        return true;
    opt = (size_t)pe_offset + 24;
    if (!xx_dotnet_yx_u16(data, size, opt, &magic)) return true;
    /* As with the PE parser, any magic other than PE32+ uses the PE32
     * layout. Corrupt optional-header magic does not hide CLR metadata. */
    directory = opt + (magic == 0x20bu ? 112u : 96u) + 14u * 8u;
    if (!xx_dotnet_yx_u32(data, size, directory, &cli_rva) ||
        !xx_dotnet_yx_rva_offset(data, size, cli_rva, &cli_offset) ||
        !xx_dotnet_yx_u32(data, size, cli_offset, &metadata_size) ||
        metadata_size != 72 ||
        !xx_dotnet_yx_u32(data, size, cli_offset + 8, &metadata_rva) ||
        !xx_dotnet_yx_u32(data, size, cli_offset + 12, &metadata_size) ||
        !xx_dotnet_yx_rva_offset(data, size, metadata_rva,
                               &metadata_offset)) return true;
    metadata_available = size - metadata_offset;
    if (metadata_available > metadata_size)
        metadata_available = metadata_size;
    if (!xx_dotnet_yx_u32(data + metadata_offset, metadata_available, 0,
                        &metadata_rva) || metadata_rva != 0x424a5342u ||
        !xx_dotnet_yx_u32(data + metadata_offset, metadata_available, 12,
                        &metadata_rva) || metadata_rva > 255 ||
        metadata_rva > metadata_available - 16) return true;
    at = 16;
    const uint8_t *version = data + metadata_offset + at;
    const uint8_t *nul = memchr(version, 0, metadata_rva);
    size_t version_bytes = nul ? (size_t)(nul - version) : metadata_rva;
    if (!xx_dotnet_yx_utf8(version, version_bytes)) return true;
    at += metadata_rva;
    if (!xx_dotnet_yx_u16(data + metadata_offset, metadata_available,
                        at + 2, &stream_count) || stream_count > 256u)
        return true;
    at += 4;
    for (unsigned i = 0; i < stream_count; ++i) {
        uint32_t relative_offset, stream_size;
        size_t name_start, name_length, padded;
        const uint8_t *name_end;
        xx_dotnet_yx_stream *stream = &result->streams[i];
        if (!xx_dotnet_yx_u32(data + metadata_offset, metadata_available,
                            at, &relative_offset) ||
            !xx_dotnet_yx_u32(data + metadata_offset, metadata_available,
                            at + 4, &stream_size)) return true;
        at += 8;
        name_start = at;
        if (at > metadata_available) return true;
        name_end = memchr(data + metadata_offset + at, 0,
                           metadata_available - at);
        if (!name_end) return true;
        name_length = (size_t)(name_end -
                       (data + metadata_offset + name_start));
        padded = (name_length + 4u) & ~(size_t)3u;
        if (padded > metadata_available - at) return true;
        sum = (uint64_t)metadata_offset + relative_offset;
        stream->offset = sum > UINT32_MAX ? UINT32_MAX : (uint32_t)sum;
        stream->size = stream_size;
        if (xx_dotnet_yx_utf8(data + metadata_offset + name_start,
                            name_length)) {
            stream->name = data + metadata_offset + name_start;
            stream->name_length = name_length;
        }
        at += padded;
        if (stream->name && stream->name_length == 2 &&
            (memcmp(stream->name, "#~", 2) == 0 ||
             memcmp(stream->name, "#-", 2) == 0)) table_stream = stream;
        if (stream->name && stream->name_length == 8 &&
            memcmp(stream->name, "#Strings", 8) == 0)
            strings_stream = stream;
        if (stream->name && stream->name_length == 5 &&
            memcmp(stream->name, "#GUID", 5) == 0) guid_stream = stream;
        if (stream->name && stream->name_length == 3 &&
            memcmp(stream->name, "#US", 3) == 0) us_stream = stream;
        if (stream->name && stream->name_length == 5 &&
            memcmp(stream->name, "#Blob", 5) == 0) blob_stream = stream;
    }
    result->is_dotnet = true;
    result->version = version;
    result->version_length = version_bytes;
    result->stream_count = stream_count;
    if (xx_dotnet_yx_u32(data, size, cli_offset + 24, &resources_rva) &&
        xx_dotnet_yx_u32(data, size, cli_offset + 28, &resources_size)) {
        size_t resources_offset;
        if (xx_dotnet_yx_rva_offset(data, size, resources_rva,
                                  &resources_offset) &&
            resources_size <= size - resources_offset &&
            resources_offset <= UINT32_MAX) {
            result->resource_bytes = data + resources_offset;
            result->resource_bytes_length = resources_size;
            result->resource_file_offset = (uint32_t)resources_offset;
        }
    }
    if (guid_stream) {
        const uint8_t *bytes;
        size_t length;
        if (xx_dotnet_yx_get_stream(data, size, guid_stream, &bytes, &length)) {
            (void)bytes;
            result->guid_count = length / 16 > 16 ? 16 : length / 16;
        }
    }
    xx_dotnet_yx_module_name(data, size, table_stream, strings_stream,
                           blob_stream, result);
    result->user_string_count = xx_dotnet_yx_user_string_count(data, size,
                                                             us_stream);
    return true;
}

bool xx_dotnet_yx_guid_at(const uint8_t *data, size_t size,
                        const xx_dotnet_yx_result *result, size_t index,
                        char text[37])
{
    static const uint8_t order[16] = {
        3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15
    };
    static const char hex[] = "0123456789abcdef";
    const uint8_t *bytes;
    size_t length, at = 0;
    if (!data || !result || !text || !result->is_dotnet ||
        index >= result->guid_count) return false;
    for (size_t i = result->stream_count; i-- > 0;) {
        const xx_dotnet_yx_stream *stream = &result->streams[i];
        if (!stream->name || stream->name_length != 5 ||
            memcmp(stream->name, "#GUID", 5) != 0) continue;
        if (!xx_dotnet_yx_get_stream(data, size, stream, &bytes, &length) ||
            index >= length / 16) return false;
        bytes += index * 16;
        for (size_t j = 0; j < 16; ++j) {
            uint8_t byte = bytes[order[j]];
            if (j == 4 || j == 6 || j == 8 || j == 10) text[at++] = '-';
            text[at++] = hex[byte >> 4];
            text[at++] = hex[byte & 15u];
        }
        text[at] = '\0';
        return true;
    }
    return false;
}

bool xx_dotnet_yx_user_string_at(const uint8_t *data, size_t size,
                               const xx_dotnet_yx_result *result, size_t index,
                               const uint8_t **bytes, size_t *length)
{
    if (!data || !result || !bytes || !length || !result->is_dotnet ||
        index >= result->user_string_count) return false;
    for (size_t i = result->stream_count; i-- > 0;) {
        const xx_dotnet_yx_stream *stream = &result->streams[i];
        const uint8_t *stream_bytes;
        size_t stream_length, at = 0, ordinal = 0;
        if (!stream->name || stream->name_length != 3 ||
            memcmp(stream->name, "#US", 3) != 0 ||
            !xx_dotnet_yx_get_stream(data, size, stream,
                                   &stream_bytes, &stream_length)) continue;
        while (at < stream_length) {
            const uint8_t *value;
            size_t value_length;
            if (!xx_dotnet_yx_next_user_string(stream_bytes, stream_length,
                                             &at, &value, &value_length,
                                             NULL))
                return false;
            if (value_length < 3) continue;
            if (ordinal++ != index) continue;
            *bytes = value;
            *length = value_length - 1;
            return true;
        }
    }
    return false;
}

bool xx_dotnet_yx_assembly_ref_at(const xx_dotnet_yx_result *result, size_t index,
                                xx_dotnet_yx_assembly_ref *out)
{
    const uint8_t *row;
    uint32_t token_index, name_index;
    size_t token_length, name_length;
    if (!result || !out || !result->is_dotnet ||
        !result->assembly_ref_rows ||
        index >= result->assembly_ref_count) return false;
    memset(out, 0, sizeof(*out));
    row = result->assembly_ref_rows +
          index * result->assembly_ref_row_size;
    if (!xx_dotnet_yx_version_at(row, result->assembly_ref_row_size,
                               0, &out->version)) return false;
    if (result->blob_heap &&
        xx_dotnet_yx_index(row, result->assembly_ref_row_size, 12,
                         result->blob_index_size, &token_index) &&
        xx_dotnet_yx_blob_value(result->blob_heap,
                              result->blob_heap_size, token_index,
                              &out->public_key_or_token,
                              &token_length)) {
        if (token_length) out->public_key_or_token_length = token_length;
        else out->public_key_or_token = NULL;
    }
    if (xx_dotnet_yx_index(row, result->assembly_ref_row_size,
                         12 + result->blob_index_size,
                         result->string_index_size, &name_index)) {
        out->name = xx_dotnet_yx_heap_string(result->strings_heap,
                                           result->strings_heap_size,
                                           name_index, &name_length);
        if (out->name) out->name_length = name_length;
    }
    return true;
}

bool xx_dotnet_yx_module_ref_at(const xx_dotnet_yx_result *result, size_t index,
                              const uint8_t **name, size_t *length)
{
    size_t ordinal = 0;
    if (!result || !name || !length || !result->is_dotnet ||
        !result->module_ref_rows || index >= result->module_ref_count)
        return false;
    for (size_t i = 0; i < result->module_ref_row_count; ++i) {
        uint32_t string_index;
        const uint8_t *value;
        size_t value_length;
        const uint8_t *row = result->module_ref_rows +
                             i * result->string_index_size;
        if (!xx_dotnet_yx_index(row, result->string_index_size, 0,
                              result->string_index_size,
                              &string_index)) continue;
        value = xx_dotnet_yx_heap_string(result->strings_heap,
                                       result->strings_heap_size,
                                       string_index, &value_length);
        if (!value) continue;
        if (ordinal++ != index) continue;
        *name = value;
        *length = value_length;
        return true;
    }
    return false;
}

bool xx_dotnet_yx_constant_at(const xx_dotnet_yx_result *result, size_t index,
                            const uint8_t **bytes, size_t *length)
{
    size_t ordinal = 0;
    if (!result || !bytes || !length || !result->is_dotnet ||
        !result->constant_rows || !result->blob_heap ||
        index >= result->constant_count) return false;
    for (size_t i = 0; i < result->constant_row_count; ++i) {
        const uint8_t *row = result->constant_rows +
                             i * result->constant_row_size;
        const uint8_t *value;
        size_t value_length;
        uint32_t blob_index;
        if (row[0] != 0x0e ||
            !xx_dotnet_yx_index(row, result->constant_row_size, 4,
                              result->blob_index_size,
                              &blob_index) ||
            !xx_dotnet_yx_blob_value(result->blob_heap,
                                   result->blob_heap_size, blob_index,
                                   &value, &value_length)) continue;
        if (ordinal++ != index) continue;
        *bytes = value;
        *length = value_length;
        return true;
    }
    return false;
}

bool xx_dotnet_yx_field_offset_at(const uint8_t *data, size_t size,
                                const xx_dotnet_yx_result *result, size_t index,
                                uint32_t *offset)
{
    size_t ordinal = 0;
    if (!data || !result || !offset || !result->is_dotnet ||
        !result->field_rva_rows || index >= result->field_offset_count)
        return false;
    for (size_t i = 0; i < result->field_rva_row_count; ++i) {
        uint32_t rva;
        uint64_t mapped;
        if (!xx_dotnet_yx_u32(result->field_rva_rows,
                            result->field_rva_row_count * 6,
                            i * 6, &rva) ||
            !xx_pe_yx_rva_to_offset(data, size, rva, &mapped) ||
            mapped > UINT32_MAX) continue;
        if (ordinal++ != index) continue;
        *offset = (uint32_t)mapped;
        return true;
    }
    return false;
}

bool xx_dotnet_yx_resource_at(const xx_dotnet_yx_result *result, size_t index,
                            xx_dotnet_yx_resource *out)
{
    const uint8_t *row;
    uint32_t name_index, relative, body_length;
    uint16_t encoded;
    size_t name_length;
    if (!result || !out || !result->is_dotnet ||
        !result->resource_rows || index >= result->resource_count)
        return false;
    memset(out, 0, sizeof(*out));
    row = result->resource_rows + index * result->resource_row_size;
    if (xx_dotnet_yx_index(row, result->resource_row_size, 8,
                         result->string_index_size, &name_index)) {
        out->name = xx_dotnet_yx_heap_string(result->strings_heap,
                                           result->strings_heap_size,
                                           name_index, &name_length);
        if (out->name) out->name_length = name_length;
    }
    if (!result->resource_bytes ||
        !xx_dotnet_yx_u32(row, result->resource_row_size, 0, &relative) ||
        !xx_dotnet_yx_u16(row, result->resource_row_size,
                        8 + result->string_index_size,
                        &encoded)) return true;
    if ((encoded & 3u) != 0 || (encoded >> 2) > 1 ||
        relative > result->resource_bytes_length ||
        result->resource_bytes_length - relative < 4 ||
        !xx_dotnet_yx_u32(result->resource_bytes,
                        result->resource_bytes_length,
                        relative, &body_length)) return true;
    {
        uint32_t start = relative > UINT32_MAX - 4 ?
                         UINT32_MAX : relative + 4;
        out->has_offset = true;
        out->offset = result->resource_file_offset > UINT32_MAX - start ?
                      UINT32_MAX : result->resource_file_offset + start;
        if ((uint64_t)start <= result->resource_bytes_length &&
            body_length <= result->resource_bytes_length - start) {
            out->has_length = true;
            out->length = body_length;
        }
    }
    return true;
}

static bool xx_dotnet_yx_type_def(const xx_dotnet_yx_result *result, size_t index,
                                uint32_t *flags, const uint8_t **name,
                                size_t *name_length,
                                const uint8_t **namespace_name,
                                size_t *namespace_length)
{
    const uint8_t *row;
    uint32_t name_index, namespace_index;
    if (!result->type_def_rows || index >= result->type_def_row_count)
        return false;
    row = result->type_def_rows + index * result->type_def_row_size;
    if (!xx_dotnet_yx_u32(row, result->type_def_row_size, 0, flags) ||
        !xx_dotnet_yx_index(row, result->type_def_row_size, 4,
                          result->string_index_size, &name_index) ||
        !xx_dotnet_yx_index(row, result->type_def_row_size,
                          4 + result->string_index_size,
                          result->string_index_size,
                          &namespace_index)) return false;
    *name = xx_dotnet_yx_heap_string(result->strings_heap,
                                   result->strings_heap_size,
                                   name_index, name_length);
    *namespace_name = xx_dotnet_yx_heap_string(result->strings_heap,
                                             result->strings_heap_size,
                                             namespace_index,
                                             namespace_length);
    if (*namespace_name && !*namespace_length) *namespace_name = NULL;
    return true;
}

static size_t xx_dotnet_yx_plain_length(const uint8_t *name, size_t length)
{
    size_t plain = length;
    for (size_t i = 0; i < length; ++i)
        if (name[i] == '`') plain = i;
    return plain;
}

static size_t xx_dotnet_yx_class_raw_index_private(
    const xx_dotnet_yx_result *result, size_t class_index)
{
    size_t ordinal = 0;
    for (size_t i = 0; i < result->type_def_row_count; ++i) {
        const uint8_t *name, *namespace_name;
        size_t name_length = 0, namespace_length = 0;
        uint32_t flags;
        if (!xx_dotnet_yx_type_def(result, i, &flags,
                                 &name, &name_length,
                                 &namespace_name,
                                 &namespace_length)) return SIZE_MAX;
        if (!name || !name_length ||
            (name_length == 8 && memcmp(name, "<Module>", 8) == 0))
            continue;
        if (ordinal++ == class_index) return i;
    }
    return SIZE_MAX;
}

bool xx_dotnet_yx_class_raw_index(const xx_dotnet_yx_result *result,
                                size_t class_index, size_t *raw_index)
{
    size_t index;
    if (!result || !raw_index || !result->is_dotnet ||
        class_index >= result->class_count) return false;
    index = xx_dotnet_yx_class_raw_index_private(result, class_index);
    if (index == SIZE_MAX) return false;
    *raw_index = index;
    return true;
}

static bool xx_dotnet_yx_generic_param(const xx_dotnet_yx_result *result,
                                     size_t raw_class_index, size_t index,
                                     const uint8_t **name, size_t *length)
{
    const uint8_t *row;
    uint16_t owner;
    uint32_t name_index;
    if (!result->generic_param_rows ||
        index >= result->generic_param_row_count) return false;
    row = result->generic_param_rows +
          index * result->generic_param_row_size;
    if (!xx_dotnet_yx_u16(row, result->generic_param_row_size,
                        4, &owner) || (owner & 1u) != 0 ||
        ((owner >> 1) ? (owner >> 1) - 1u : 0u) != raw_class_index ||
        !xx_dotnet_yx_index(row, result->generic_param_row_size, 6,
                          result->string_index_size,
                          &name_index)) return false;
    *name = xx_dotnet_yx_heap_string(result->strings_heap,
                                   result->strings_heap_size,
                                   name_index, length);
    return *name != NULL;
}

static bool xx_dotnet_yx_type_def_fullname(const xx_dotnet_yx_result *result,
                                        size_t raw_index, char *buffer,
                                        size_t capacity, size_t *required,
                                        size_t *length)
{
    size_t chain[16], depth = 0, needed = 0, parts_count = 0, used = 0;
    const uint8_t *name, *namespace_name;
    size_t name_length, namespace_length;
    uint32_t flags;
    if (required) *required = 0;
    if (length) *length = 0;
    for (;;) {
        size_t parent = SIZE_MAX;
        if (depth >= 16) return false;
        chain[depth++] = raw_index;
        if (!xx_dotnet_yx_type_def(result, raw_index, &flags,
                                 &name, &name_length,
                                 &namespace_name,
                                 &namespace_length) || !name ||
            !name_length) return false;
        if ((flags & 7u) < 2) break;
        for (size_t i = 0; i < result->nested_class_row_count; ++i) {
            const uint8_t *row = result->nested_class_rows + i * 4;
            uint16_t child_index, parent_index;
            if (!xx_dotnet_yx_u16(row, 4, 0, &child_index) ||
                !xx_dotnet_yx_u16(row, 4, 2, &parent_index)) continue;
            size_t child = child_index ? child_index - 1 : 0;
            size_t enclosing = parent_index ? parent_index - 1 : 0;
            if (child == raw_index && enclosing != raw_index) {
                parent = enclosing;
                break;
            }
        }
        if (parent == SIZE_MAX) break;
        raw_index = parent;
    }
    for (size_t j = depth; j-- > 0;) {
        if (!xx_dotnet_yx_type_def(result, chain[j], &flags,
                                 &name, &name_length,
                                 &namespace_name,
                                 &namespace_length) || !name ||
            !name_length) return false;
        name_length = xx_dotnet_yx_plain_length(name, name_length);
        if (namespace_name && namespace_length) {
            if (needed > SIZE_MAX - namespace_length -
                         (parts_count ? 1u : 0u)) return false;
            needed += namespace_length + (parts_count ? 1u : 0u);
            ++parts_count;
        }
        /* Upstream always joins the plain name, even when it is empty:
         * TypeDef::plain_name() is `name.rsplit_once('`').0`, which can be
         * Some("") for a name that starts with the generic-arity backtick
         * (e.g. "`BCodeProvider"). Only a wholly absent part is skipped,
         * never a merely zero-length one, so this must not be `if
         * (name_length)`. */
        if (needed > SIZE_MAX - name_length -
                     (parts_count ? 1u : 0u)) return false;
        needed += name_length + (parts_count ? 1u : 0u);
        ++parts_count;
    }
    if (needed == SIZE_MAX) return false;
    if (required) *required = needed + 1;
    if (length) *length = needed;
    if (!buffer || capacity < needed + 1) return true;
    for (size_t j = depth; j-- > 0;) {
        if (!xx_dotnet_yx_type_def(result, chain[j], &flags,
                                 &name, &name_length,
                                 &namespace_name,
                                 &namespace_length) || !name ||
            !name_length) return false;
        name_length = xx_dotnet_yx_plain_length(name, name_length);
        const uint8_t *parts[2] = {namespace_name, name};
        size_t lengths[2] = {namespace_length, name_length};
        for (unsigned part = 0; part < 2; ++part) {
            /* See the matching comment in the sizing pass above: skip a
             * part only when wholly absent (NULL), not merely empty. */
            if (!parts[part]) continue;
            if (used) buffer[used++] = '.';
            if (lengths[part])
                memcpy(buffer + used, parts[part], lengths[part]);
            used += lengths[part];
        }
    }
    buffer[used] = '\0';
    return true;
}

static bool xx_dotnet_yx_type_ref_fullname(const xx_dotnet_yx_result *result,
                                        size_t raw_index, char *buffer,
                                        size_t capacity, size_t *required,
                                        size_t *length)
{
    const uint8_t *row, *name, *namespace_name;
    uint32_t name_index, namespace_index;
    size_t name_length, namespace_length, needed;
    if (required) *required = 0;
    if (length) *length = 0;
    if (!result->type_ref_rows || raw_index >= result->type_ref_row_count)
        return false;
    row = result->type_ref_rows + raw_index * result->type_ref_row_size;
    if (!xx_dotnet_yx_index(row, result->type_ref_row_size, 2,
                          result->string_index_size, &name_index) ||
        !xx_dotnet_yx_index(row, result->type_ref_row_size,
                          2 + result->string_index_size,
                          result->string_index_size,
                          &namespace_index)) return false;
    name = xx_dotnet_yx_heap_string(result->strings_heap,
                                  result->strings_heap_size,
                                  name_index, &name_length);
    namespace_name = xx_dotnet_yx_heap_string(result->strings_heap,
                                            result->strings_heap_size,
                                            namespace_index,
                                            &namespace_length);
    if (!name) return false;
    name_length = xx_dotnet_yx_plain_length(name, name_length);
    if (namespace_name && namespace_length > SIZE_MAX - name_length - 1)
        return false;
    needed = name_length + (namespace_name ? namespace_length + 1 : 0);
    if (needed == SIZE_MAX) return false;
    if (required) *required = needed + 1;
    if (length) *length = needed;
    if (!buffer || capacity < needed + 1) return true;
    size_t used = 0;
    if (namespace_name) {
        memcpy(buffer, namespace_name, namespace_length);
        used = namespace_length;
        buffer[used++] = '.';
    }
    memcpy(buffer + used, name, name_length);
    buffer[needed] = '\0';
    return true;
}

bool xx_dotnet_yx_type_def_or_ref_fullname(
    const xx_dotnet_yx_result *result, uint16_t coded, char *buffer,
    size_t capacity, size_t *required, size_t *length)
{
    size_t raw_index = (coded >> 2) ? (size_t)(coded >> 2) - 1 : 0;
    switch (coded & 3u) {
    case 0: return xx_dotnet_yx_type_def_fullname(result, raw_index,
                                                 buffer, capacity,
                                                 required, length);
    case 1: return xx_dotnet_yx_type_ref_fullname(result, raw_index,
                                                 buffer, capacity,
                                                 required, length);
    default: return false; /* TypeSpec requires a decoded signature. */
    }
}

static bool xx_dotnet_yx_base_type_fullname(
    const xx_dotnet_yx_result *result, uint16_t coded, size_t class_index,
    char *buffer, size_t capacity, size_t *required, size_t *length)
{
    if ((coded & 3u) == 2u) {
        size_t raw_index = (coded >> 2) ? (size_t)(coded >> 2) - 1 : 0;
        return xx_dotnet_yx_type_spec_fullname(result, raw_index, class_index,
                                              buffer, capacity,
                                              required, length);
    }
    return xx_dotnet_yx_type_def_or_ref_fullname(result, coded, buffer,
                                                capacity, required, length);
}

static bool xx_dotnet_yx_base_type_coded_at(const xx_dotnet_yx_result *result,
                                         size_t raw_class_index,
                                         size_t candidate_index,
                                         uint16_t *coded)
{
    if (candidate_index == 0) {
        const uint8_t *row = result->type_def_rows +
                             raw_class_index * result->type_def_row_size;
        return xx_dotnet_yx_u16(row, result->type_def_row_size,
                              4 + 2 * result->string_index_size, coded);
    }
    size_t ordinal = 1;
    for (size_t i = 0; i < result->interface_impl_row_count; ++i) {
        const uint8_t *row = result->interface_impl_rows + i * 4;
        uint16_t class_index, interface_index;
        if (!xx_dotnet_yx_u16(row, 4, 0, &class_index) ||
            !xx_dotnet_yx_u16(row, 4, 2, &interface_index)) continue;
        size_t indexed_class = class_index ? class_index - 1u : 0u;
        if (indexed_class != raw_class_index) continue;
        if (ordinal++ == candidate_index) {
            *coded = interface_index;
            return true;
        }
    }
    return false;
}

bool xx_dotnet_yx_class_at(const xx_dotnet_yx_result *result, size_t index,
                         char *fullname_buffer, size_t buffer_capacity,
                         xx_dotnet_yx_class *out)
{
    static const char *const visibility[8] = {
        "internal", "public", "public", "private", "protected",
        "internal", "private protected", "protected internal"
    };
    size_t raw_index, chain[16], depth = 0;
    const uint8_t *name, *namespace_name;
    size_t name_length = 0, namespace_length = 0;
    uint32_t flags = 0;
    if (!result || !out || !result->is_dotnet ||
        index >= result->class_count) return false;
    memset(out, 0, sizeof(*out));
    raw_index = xx_dotnet_yx_class_raw_index_private(result, index);
    if (raw_index == SIZE_MAX ||
        !xx_dotnet_yx_type_def(result, raw_index, &flags,
                             &name, &name_length,
                             &namespace_name,
                             &namespace_length)) return false;
    out->visibility = visibility[flags & 7u];
    out->type = (flags & 0x20u) ? "interface" : "class";
    out->is_abstract = (flags & 0x80u) != 0;
    out->is_sealed = (flags & 0x100u) != 0;
    for (size_t i = 0; i < result->generic_param_row_count; ++i) {
        const uint8_t *generic_name;
        size_t generic_length;
        if (xx_dotnet_yx_generic_param(result, raw_index, i,
                                     &generic_name, &generic_length))
            ++out->number_of_generic_parameters;
    }
    for (size_t i = 0; i <= result->interface_impl_row_count; ++i) {
        uint16_t coded;
        if (!xx_dotnet_yx_base_type_coded_at(result, raw_index, i, &coded))
            break;
        if (xx_dotnet_yx_base_type_fullname(result, coded, index, NULL, 0,
                                          NULL, NULL))
            ++out->number_of_base_types;
    }
    for (;;) {
        size_t parent = SIZE_MAX;
        if (depth >= 16) return true;
        chain[depth++] = raw_index;
        if (!xx_dotnet_yx_type_def(result, raw_index, &flags,
                                 &name, &name_length,
                                 &namespace_name,
                                 &namespace_length) ||
            !name) return true;
        if ((flags & 7u) < 2) break;
        for (size_t i = 0; i < result->nested_class_row_count; ++i) {
            const uint8_t *row = result->nested_class_rows + i * 4;
            uint16_t child_index, parent_index;
            if (!xx_dotnet_yx_u16(row, 4, 0, &child_index) ||
                !xx_dotnet_yx_u16(row, 4, 2, &parent_index)) continue;
            size_t child = child_index ? child_index - 1 : 0;
            size_t enclosing = parent_index ? parent_index - 1 : 0;
            if (child == raw_index && enclosing != raw_index) {
                parent = enclosing;
                break;
            }
        }
        if (parent == SIZE_MAX) break;
        raw_index = parent;
    }
    {
        size_t required = 0, parts_count = 0, used = 0;
        for (size_t j = depth; j-- > 0;) {
            if (!xx_dotnet_yx_type_def(result, chain[j], &flags,
                                     &name, &name_length,
                                     &namespace_name,
                                     &namespace_length) || !name)
                return true;
            name_length = xx_dotnet_yx_plain_length(name, name_length);
            if (namespace_name && namespace_length) {
                if (required > SIZE_MAX - namespace_length -
                               (parts_count ? 1u : 0u)) return true;
                required += namespace_length + (parts_count ? 1u : 0u);
                ++parts_count;
            }
            /* Same upstream rule as xx_dotnet_yx_type_def_fullname: the plain
             * name is always joined in, even when it is empty after
             * stripping the generic-arity backtick, so this must not be
             * `if (name_length)`. */
            if (required > SIZE_MAX - name_length -
                           (parts_count ? 1u : 0u)) return true;
            required += name_length + (parts_count ? 1u : 0u);
            ++parts_count;
        }
        if (required == SIZE_MAX) return true;
        out->required_fullname_capacity = required + 1;
        if (!fullname_buffer || buffer_capacity < required + 1)
            return true;
        for (size_t j = depth; j-- > 0;) {
            if (!xx_dotnet_yx_type_def(result, chain[j], &flags,
                                     &name, &name_length,
                                     &namespace_name,
                                     &namespace_length) ||
                !name) return true;
            name_length = xx_dotnet_yx_plain_length(name, name_length);
            const uint8_t *parts[2] = {namespace_name, name};
            size_t lengths[2] = {namespace_length, name_length};
            for (unsigned part = 0; part < 2; ++part) {
                /* Skip only a wholly absent part (NULL), never a merely
                 * empty one - see the comment above. */
                if (!parts[part]) continue;
                if (used && used < buffer_capacity)
                    fullname_buffer[used++] = '.';
                if (lengths[part] >= buffer_capacity - used)
                    return true;
                if (lengths[part])
                    memcpy(fullname_buffer + used, parts[part],
                           lengths[part]);
                used += lengths[part];
            }
        }
        fullname_buffer[used] = '\0';
        out->fullname = fullname_buffer;
        out->fullname_length = used;
        out->name = fullname_buffer;
        out->name_length = used;
        for (size_t i = 0; i < used; ++i) {
            if (fullname_buffer[i] == '.') {
                out->namespace_name = fullname_buffer;
                out->namespace_length = i;
                out->name = fullname_buffer + i + 1;
                out->name_length = used - i - 1;
            }
        }
    }
    return true;
}

bool xx_dotnet_yx_class_generic_parameter_at(
    const xx_dotnet_yx_result *result, size_t class_index,
    size_t parameter_index, const uint8_t **name, size_t *length)
{
    size_t raw_index, ordinal = 0;
    if (!result || !name || !length || !result->is_dotnet ||
        class_index >= result->class_count) return false;
    raw_index = xx_dotnet_yx_class_raw_index_private(result, class_index);
    if (raw_index == SIZE_MAX) return false;
    for (size_t i = 0; i < result->generic_param_row_count; ++i) {
        const uint8_t *candidate;
        size_t candidate_length;
        if (!xx_dotnet_yx_generic_param(result, raw_index, i,
                                      &candidate, &candidate_length))
            continue;
        if (ordinal++ != parameter_index) continue;
        *name = candidate;
        *length = candidate_length;
        return true;
    }
    return false;
}

bool xx_dotnet_yx_class_base_type_at(const xx_dotnet_yx_result *result,
                                   size_t class_index, size_t base_index,
                                   char *buffer, size_t buffer_capacity,
                                   size_t *required_capacity,
                                   size_t *length)
{
    size_t raw_index, ordinal = 0;
    if (required_capacity) *required_capacity = 0;
    if (length) *length = 0;
    if (!result || !result->is_dotnet || class_index >= result->class_count)
        return false;
    raw_index = xx_dotnet_yx_class_raw_index_private(result, class_index);
    if (raw_index == SIZE_MAX) return false;
    for (size_t i = 0; i <= result->interface_impl_row_count; ++i) {
        uint16_t coded;
        if (!xx_dotnet_yx_base_type_coded_at(result, raw_index, i, &coded))
            break;
        if (!xx_dotnet_yx_base_type_fullname(result, coded, class_index,
                                           NULL, 0, NULL, NULL)) continue;
        if (ordinal++ != base_index) continue;
        return xx_dotnet_yx_base_type_fullname(
            result, coded, class_index, buffer, buffer_capacity,
            required_capacity, length);
    }
    return false;
}
