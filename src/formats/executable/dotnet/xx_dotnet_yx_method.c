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

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *at;
    const uint8_t *end;
} xx_dotnet_yx_signature_reader;

typedef struct {
    char *bytes;
    size_t capacity;
    size_t length;
} xx_dotnet_yx_type_text;

typedef struct {
    const xx_dotnet_yx_result *result;
    size_t class_index;
    size_t raw_method_index;
    unsigned depth;
} xx_dotnet_yx_type_context;

static bool xx_dotnet_yx_u16(const uint8_t *bytes, size_t length, size_t at,
                   uint16_t *value)
{
    if (!bytes || at > length || length - at < 2) return false;
    *value = (uint16_t)bytes[at] | (uint16_t)bytes[at + 1] << 8;
    return true;
}

static bool xx_dotnet_yx_u32(const uint8_t *bytes, size_t length, size_t at,
                   uint32_t *value)
{
    if (!bytes || at > length || length - at < 4) return false;
    *value = (uint32_t)bytes[at] | (uint32_t)bytes[at + 1] << 8 |
             (uint32_t)bytes[at + 2] << 16 |
             (uint32_t)bytes[at + 3] << 24;
    return true;
}

static bool xx_dotnet_yx_index(const uint8_t *bytes, size_t length, size_t at,
                     size_t width, uint32_t *value)
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

static bool xx_dotnet_yx_utf8(const uint8_t *bytes, size_t length)
{
    for (size_t i = 0; i < length;) {
        unsigned char first = bytes[i];
        size_t width;
        uint32_t codepoint;
        if (first < 0x80) { ++i; continue; }
        if (first >= 0xc2 && first <= 0xdf) {
            width = 2; codepoint = first & 0x1fu;
        } else if (first >= 0xe0 && first <= 0xef) {
            width = 3; codepoint = first & 0x0fu;
        } else if (first >= 0xf0 && first <= 0xf4) {
            width = 4; codepoint = first & 0x07u;
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

static const uint8_t *xx_dotnet_yx_heap_string(const xx_dotnet_yx_result *result,
                                      uint32_t index, size_t *length)
{
    const uint8_t *start, *end;
    if (!result->strings_heap || index >= result->strings_heap_size)
        return NULL;
    start = result->strings_heap + index;
    end = memchr(start, 0, result->strings_heap_size - index);
    if (!end || !xx_dotnet_yx_utf8(start, (size_t)(end - start))) return NULL;
    *length = (size_t)(end - start);
    return start;
}

static bool xx_dotnet_yx_read_uint(xx_dotnet_yx_signature_reader *reader, uint32_t *value)
{
    uint8_t first;
    if (reader->at >= reader->end) return false;
    first = *reader->at++;
    if ((first & 0x80u) == 0) {
        *value = first;
    } else if ((first & 0xc0u) == 0x80u) {
        if ((size_t)(reader->end - reader->at) < 1) return false;
        *value = ((uint32_t)(first & 0x3fu) << 8) | *reader->at++;
    } else if ((first & 0xe0u) == 0xc0u) {
        if ((size_t)(reader->end - reader->at) < 3) return false;
        *value = ((uint32_t)(first & 0x1fu) << 24) |
                 ((uint32_t)reader->at[0] << 16) |
                 ((uint32_t)reader->at[1] << 8) | reader->at[2];
        reader->at += 3;
    } else return false;
    return true;
}

static bool xx_dotnet_yx_read_sint(xx_dotnet_yx_signature_reader *reader, int32_t *value)
{
    const uint8_t *start = reader->at;
    uint32_t raw;
    uint32_t width;
    if (start >= reader->end || !xx_dotnet_yx_read_uint(reader, &raw)) return false;
    width = ((start[0] & 0x80u) == 0) ? 6u :
            ((start[0] & 0xc0u) == 0x80u) ? 13u : 28u;
    *value = (int32_t)(raw >> 1);
    if (raw & 1u) *value -= (int32_t)(UINT32_C(1) << width);
    return true;
}

static bool xx_dotnet_yx_blob(const xx_dotnet_yx_result *result, uint32_t index,
                    xx_dotnet_yx_signature_reader *reader)
{
    xx_dotnet_yx_signature_reader heap;
    uint32_t length;
    if (!result->blob_heap || index >= result->blob_heap_size) return false;
    heap.at = result->blob_heap + index;
    heap.end = result->blob_heap + result->blob_heap_size;
    if (!xx_dotnet_yx_read_uint(&heap, &length) ||
        length > (size_t)(heap.end - heap.at)) return false;
    reader->at = heap.at;
    reader->end = heap.at + length;
    return true;
}

static bool xx_dotnet_yx_text_append(xx_dotnet_yx_type_text *text, const char *bytes,
                           size_t length)
{
    if (text->length > SIZE_MAX - length) return false;
    if (text->bytes && text->length < text->capacity) {
        size_t n = text->capacity - text->length;
        if (n > length) n = length;
        memcpy(text->bytes + text->length, bytes, n);
    }
    text->length += length;
    return true;
}

static bool xx_dotnet_yx_text_literal(xx_dotnet_yx_type_text *text, const char *literal)
{
    return xx_dotnet_yx_text_append(text, literal, strlen(literal));
}

static bool xx_dotnet_yx_text_number(xx_dotnet_yx_type_text *text, int64_t number)
{
    char number_buffer[32];
    int length = snprintf(number_buffer, sizeof(number_buffer), "%" PRId64,
                          number);
    return length >= 0 && (size_t)length < sizeof(number_buffer) &&
           xx_dotnet_yx_text_append(text, number_buffer, (size_t)length);
}

static void xx_dotnet_yx_text_finish(xx_dotnet_yx_type_text *text)
{
    if (text->bytes && text->capacity > text->length)
        text->bytes[text->length] = '\0';
}

static bool xx_dotnet_yx_generic_name(const xx_dotnet_yx_result *result,
                            size_t raw_owner_index, bool method_owner,
                            size_t ordinal, const uint8_t **name,
                            size_t *length)
{
    size_t matched = 0;
    if (!result->generic_param_rows) return false;
    for (size_t i = 0; i < result->generic_param_row_count; ++i) {
        const uint8_t *row = result->generic_param_rows +
                             i * result->generic_param_row_size;
        uint16_t owner;
        uint32_t name_index;
        const uint8_t *current;
        size_t current_length;
        if (!xx_dotnet_yx_u16(row, result->generic_param_row_size, 4, &owner) ||
            (bool)(owner & 1u) != method_owner ||
            (size_t)((owner >> 1) ? (owner >> 1) - 1u : 0u) !=
                raw_owner_index ||
            !xx_dotnet_yx_index(row, result->generic_param_row_size, 6,
                      result->string_index_size, &name_index)) continue;
        current = xx_dotnet_yx_heap_string(result, name_index, &current_length);
        if (!current) continue;
        if (matched++ == ordinal) {
            if (name) *name = current;
            if (length) *length = current_length;
            return true;
        }
    }
    return false;
}

static size_t xx_dotnet_yx_generic_count(const xx_dotnet_yx_result *result,
                               size_t raw_owner_index, bool method_owner)
{
    size_t count = 0;
    while (xx_dotnet_yx_generic_name(result, raw_owner_index, method_owner,
                           count, NULL, NULL)) ++count;
    return count;
}

static bool xx_dotnet_yx_parse_type(xx_dotnet_yx_type_context *context,
                          xx_dotnet_yx_signature_reader *reader,
                          xx_dotnet_yx_type_text *text);

static bool xx_dotnet_yx_parse_type_spec_index(xx_dotnet_yx_type_context *context,
                                     size_t raw_type_spec_index,
                                     xx_dotnet_yx_type_text *text)
{
    const uint8_t *row;
    uint32_t blob_index;
    xx_dotnet_yx_signature_reader signature;
    if (!context->result->type_spec_rows ||
        raw_type_spec_index >= context->result->type_spec_row_count)
        return false;
    row = context->result->type_spec_rows +
          raw_type_spec_index * context->result->blob_index_size;
    if (!xx_dotnet_yx_index(row, context->result->blob_index_size, 0,
                  context->result->blob_index_size, &blob_index) ||
        !xx_dotnet_yx_blob(context->result, blob_index, &signature)) return false;
    return xx_dotnet_yx_parse_type(context, &signature, text);
}

static bool xx_dotnet_yx_type_name(xx_dotnet_yx_type_context *context, uint32_t coded,
                         xx_dotnet_yx_type_text *text)
{
    size_t index = (coded >> 2) ? (size_t)(coded >> 2) - 1 : 0;
    size_t required = 0, length = 0;
    char *name;
    bool ok;
    if ((coded & 3u) == 2u)
        return xx_dotnet_yx_parse_type_spec_index(context, index, text);
    if ((coded & 3u) == 3u || coded > UINT16_MAX ||
        !xx_dotnet_yx_type_def_or_ref_fullname(context->result, (uint16_t)coded,
                                             NULL, 0, &required, &length) ||
        !required || required <= length) return false;
    name = malloc(required);
    if (!name) return false;
    ok = xx_dotnet_yx_type_def_or_ref_fullname(context->result,
                                             (uint16_t)coded, name, required,
                                             NULL, NULL) &&
         xx_dotnet_yx_text_append(text, name, length);
    free(name);
    return ok;
}

static bool xx_dotnet_yx_parse_array(xx_dotnet_yx_type_context *context,
                           xx_dotnet_yx_signature_reader *reader,
                           xx_dotnet_yx_type_text *text)
{
    uint32_t dimensions, size_count, lower_count;
    int32_t sizes[50], lower_bounds[50];
    if (!xx_dotnet_yx_parse_type(context, reader, text) ||
        !xx_dotnet_yx_read_uint(reader, &dimensions) || dimensions > 50 ||
        !xx_dotnet_yx_read_uint(reader, &size_count) || size_count > 50)
        return false;
    for (uint32_t i = 0; i < size_count; ++i) {
        uint32_t value;
        if (!xx_dotnet_yx_read_uint(reader, &value)) return false;
        sizes[i] = (int32_t)value;
    }
    if (!xx_dotnet_yx_read_uint(reader, &lower_count) || lower_count > 50)
        return false;
    for (uint32_t i = 0; i < lower_count; ++i)
        if (!xx_dotnet_yx_read_sint(reader, &lower_bounds[i])) return false;
    if (!xx_dotnet_yx_text_literal(text, "[")) return false;
    for (uint32_t i = 0; i < dimensions; ++i) {
        if (i < lower_count && i < size_count) {
            if (lower_bounds[i] == 0) {
                if (!xx_dotnet_yx_text_number(text, sizes[i])) return false;
            } else if (sizes[i] >= 1) {
                int32_t upper = (int32_t)((uint32_t)lower_bounds[i] +
                                           (uint32_t)sizes[i] - 1u);
                if (!xx_dotnet_yx_text_number(text, lower_bounds[i]) ||
                    !xx_dotnet_yx_text_literal(text, "...") ||
                    !xx_dotnet_yx_text_number(text, upper)) return false;
            }
        } else if (i < lower_count) {
            if (!xx_dotnet_yx_text_number(text, lower_bounds[i]) ||
                !xx_dotnet_yx_text_literal(text, "...")) return false;
        } else if (i < size_count) {
            if (!xx_dotnet_yx_text_number(text, sizes[i])) return false;
        }
        if (i + 1 < dimensions && !xx_dotnet_yx_text_literal(text, ",")) return false;
    }
    return xx_dotnet_yx_text_literal(text, "]");
}

static bool xx_dotnet_yx_parse_type(xx_dotnet_yx_type_context *context,
                          xx_dotnet_yx_signature_reader *reader,
                          xx_dotnet_yx_type_text *text)
{
    static const char *const primitive[] = {
        NULL, "void", "bool", "char", "sbyte", "byte", "short", "ushort",
        "int", "uint", "long", "ulong", "float", "double", "string"
    };
    uint8_t type;
    uint32_t value;
    if (context->depth == 16 || reader->at >= reader->end) return false;
    type = *reader->at++;
    ++context->depth;
    if (type < sizeof(primitive) / sizeof(*primitive) && primitive[type]) {
        if (!xx_dotnet_yx_text_literal(text, primitive[type])) return false;
    } else switch (type) {
    case 0x0f: /* Ptr */
        if (!xx_dotnet_yx_text_literal(text, "Ptr<") ||
            !xx_dotnet_yx_parse_type(context, reader, text) ||
            !xx_dotnet_yx_text_literal(text, ">")) return false;
        break;
    case 0x10: /* ByRef */
        if (!xx_dotnet_yx_text_literal(text, "ref ") ||
            !xx_dotnet_yx_parse_type(context, reader, text)) return false;
        break;
    case 0x11: case 0x12: /* ValueType, Class */
        if (!xx_dotnet_yx_read_uint(reader, &value) ||
            !xx_dotnet_yx_type_name(context, value, text)) return false;
        break;
    case 0x13: case 0x1e: { /* Var, MVar */
        const uint8_t *name;
        size_t length;
        bool method_owner = type == 0x1e;
        size_t owner = method_owner ? context->raw_method_index :
                                        context->class_index;
        if (!xx_dotnet_yx_read_uint(reader, &value)) return false;
        if (method_owner) {
            if (owner == SIZE_MAX ||
                !xx_dotnet_yx_generic_name(context->result, owner, true, value,
                                 &name, &length)) return false;
        } else {
            if (owner == SIZE_MAX ||
                !xx_dotnet_yx_class_generic_parameter_at(context->result, owner,
                                                       value, &name, &length))
                return false;
        }
        if (!xx_dotnet_yx_text_append(text, (const char *)name, length)) return false;
        break;
    }
    case 0x14: /* Array */
        if (!xx_dotnet_yx_parse_array(context, reader, text)) return false;
        break;
    case 0x15: /* GenericInst */
        if (!xx_dotnet_yx_parse_type(context, reader, text) ||
            !xx_dotnet_yx_read_uint(reader, &value) || value >= 1000 ||
            !xx_dotnet_yx_text_literal(text, "<")) return false;
        for (uint32_t i = 0; i < value; ++i) {
            if (!xx_dotnet_yx_parse_type(context, reader, text) ||
                (i + 1 < value && !xx_dotnet_yx_text_literal(text, ",")))
                return false;
        }
        if (!xx_dotnet_yx_text_literal(text, ">")) return false;
        break;
    case 0x16:
        if (!xx_dotnet_yx_text_literal(text, "TypedReference")) return false;
        break;
    case 0x18:
        if (!xx_dotnet_yx_text_literal(text, "IntPtr")) return false;
        break;
    case 0x19:
        if (!xx_dotnet_yx_text_literal(text, "UintPtr")) return false;
        break;
    case 0x1b: /* FnPtr */
        if (reader->at >= reader->end) return false;
        ++reader->at; /* Calling-convention flags, unvalidated upstream. */
        if (!xx_dotnet_yx_read_uint(reader, &value) || value >= 1000 ||
            !xx_dotnet_yx_text_literal(text, "FnPtr<") ||
            !xx_dotnet_yx_parse_type(context, reader, text) ||
            !xx_dotnet_yx_text_literal(text, "(")) return false;
        for (uint32_t i = 0; i < value; ++i) {
            if (!xx_dotnet_yx_parse_type(context, reader, text) ||
                (i + 1 < value && !xx_dotnet_yx_text_literal(text, ", ")))
                return false;
        }
        if (!xx_dotnet_yx_text_literal(text, ")>")) return false;
        break;
    case 0x1c:
        if (!xx_dotnet_yx_text_literal(text, "object")) return false;
        break;
    case 0x1d:
        if (!xx_dotnet_yx_parse_type(context, reader, text) ||
            !xx_dotnet_yx_text_literal(text, "[]")) return false;
        break;
    case 0x1f: case 0x20: /* CModReqd, CModOpt */
        if (!xx_dotnet_yx_read_uint(reader, &value) ||
            !xx_dotnet_yx_parse_type(context, reader, text)) return false;
        break;
    default: return false;
    }
    --context->depth;
    return true;
}

static bool xx_dotnet_yx_type_def_method_range(const xx_dotnet_yx_result *result,
                                      size_t class_index, size_t *raw_class,
                                      size_t *first, size_t *last)
{
    const uint8_t *row;
    uint16_t start, end;
    if (!result || !result->is_dotnet || !result->type_def_rows ||
        result->type_def_row_size < 2 ||
        !xx_dotnet_yx_class_raw_index(result, class_index, raw_class))
        return false;
    row = result->type_def_rows + *raw_class * result->type_def_row_size;
    if (!xx_dotnet_yx_u16(row, result->type_def_row_size,
                 result->type_def_row_size - 2, &start)) return false;
    *first = start ? (size_t)start - 1 : 0;
    if (*raw_class + 1 < result->type_def_row_count) {
        row += result->type_def_row_size;
        if (!xx_dotnet_yx_u16(row, result->type_def_row_size,
                     result->type_def_row_size - 2, &end)) return false;
        *last = end ? (size_t)end - 1 : 0;
    } else *last = result->method_def_row_count;
    return *first <= *last && *last <= result->method_def_row_count;
}

static const char *xx_dotnet_yx_visibility(uint16_t flags)
{
    switch (flags & 7u) {
    case 1: return "private";
    case 2: return "private protected";
    case 3: return "internal";
    case 4: return "protected";
    case 5: return "protected internal";
    case 6: return "public";
    default: return "private";
    }
}

static bool xx_dotnet_yx_method_info(const xx_dotnet_yx_result *result,
                           size_t class_index, size_t raw_class_index,
                           size_t raw_method_index, char *return_buffer,
                           size_t return_capacity,
                           size_t selected_parameter,
                           char *parameter_buffer,
                           size_t parameter_capacity,
                           xx_dotnet_yx_method *method,
                           xx_dotnet_yx_method_parameter *parameter)
{
    const uint8_t *row, *name;
    uint16_t flags;
    uint32_t name_index, blob_index, param_list;
    size_t name_length, param_list_width;
    xx_dotnet_yx_signature_reader signature;
    xx_dotnet_yx_type_context context;
    xx_dotnet_yx_type_text return_text = {return_buffer, return_capacity, 0};
    xx_dotnet_yx_type_text param_text = {parameter_buffer, parameter_capacity, 0};
    uint32_t generic_count_in_signature, param_count;
    uint8_t call_flags;
    if (!result || !result->method_def_rows ||
        raw_method_index >= result->method_def_row_count ||
        result->method_def_row_size <
            8u + result->string_index_size + result->blob_index_size + 2u ||
        (result->blob_index_size != 2 && result->blob_index_size != 4))
        return false;
    row = result->method_def_rows +
          raw_method_index * result->method_def_row_size;
    param_list_width = result->method_def_row_size - 8 -
                       result->string_index_size - result->blob_index_size;
    if (!xx_dotnet_yx_u16(row, result->method_def_row_size, 6, &flags) ||
        !xx_dotnet_yx_index(row, result->method_def_row_size, 8,
                  result->string_index_size, &name_index) ||
        !xx_dotnet_yx_index(row, result->method_def_row_size,
                  8 + result->string_index_size,
                  result->blob_index_size, &blob_index) ||
        !xx_dotnet_yx_index(row, result->method_def_row_size,
                  8 + result->string_index_size + result->blob_index_size,
                  param_list_width, &param_list)) return false;
    name = xx_dotnet_yx_heap_string(result, name_index, &name_length);
    if (!name || !xx_dotnet_yx_blob(result, blob_index, &signature) ||
        signature.at >= signature.end) return false;
    call_flags = *signature.at++;
    generic_count_in_signature = 0;
    if ((call_flags & 0x10u) &&
        !xx_dotnet_yx_read_uint(&signature, &generic_count_in_signature)) return false;
    if (!xx_dotnet_yx_read_uint(&signature, &param_count) ||
        param_count > (size_t)(signature.end - signature.at)) return false;
    (void)generic_count_in_signature;
    context.result = result;
    context.class_index = class_index;
    context.raw_method_index = raw_method_index;
    context.depth = 0;
    if (!xx_dotnet_yx_parse_type(&context, &signature, &return_text)) return false;
    xx_dotnet_yx_text_finish(&return_text);
    for (uint32_t i = 0; i < param_count; ++i) {
        xx_dotnet_yx_type_text scratch = {NULL, 0, 0};
        xx_dotnet_yx_type_text *current = i == selected_parameter ? &param_text :
                                                      &scratch;
        if (!xx_dotnet_yx_parse_type(&context, &signature, current)) return false;
        if (i == selected_parameter) xx_dotnet_yx_text_finish(&param_text);
    }
    if (method) {
        memset(method, 0, sizeof(*method));
        method->name = name;
        method->name_length = name_length;
        method->visibility = xx_dotnet_yx_visibility(flags);
        method->is_abstract = (flags & 0x400u) != 0;
        method->is_static = (flags & 0x10u) != 0;
        method->is_virtual = (flags & 0x40u) != 0;
        method->is_final = (flags & 0x20u) != 0;
        method->has_return_type = !((name_length == 5 &&
                                    memcmp(name, ".ctor", 5) == 0) ||
                                   (name_length == 6 &&
                                    memcmp(name, ".cctor", 6) == 0));
        method->return_type_length = return_text.length;
        method->number_of_generic_parameters = xx_dotnet_yx_generic_count(
            result, raw_method_index, true);
        method->number_of_parameters = param_count;
        method->raw_method_index = raw_method_index;
        method->class_index = class_index;
        method->raw_class_index = raw_class_index;
        method->raw_param_list_index = param_list ? (size_t)param_list - 1 : 0;
    }
    if (parameter) {
        size_t raw_param_index = (param_list ? (size_t)param_list - 1 : 0) +
                                 selected_parameter;
        uint32_t parameter_name_index;
        const uint8_t *param_row;
        memset(parameter, 0, sizeof(*parameter));
        parameter->generated_name_index = selected_parameter;
        parameter->type_length = param_text.length;
        if (raw_param_index < result->param_row_count && result->param_rows) {
            param_row = result->param_rows +
                        raw_param_index * result->param_row_size;
            if (xx_dotnet_yx_index(param_row, result->param_row_size, 4,
                         result->string_index_size, &parameter_name_index))
                parameter->name = xx_dotnet_yx_heap_string(result,
                                                  parameter_name_index,
                                                  &parameter->name_length);
        }
    }
    return true;
}

size_t xx_dotnet_yx_class_method_count(const xx_dotnet_yx_result *result,
                                     size_t class_index)
{
    size_t raw_class, first, last, count = 0;
    if (!xx_dotnet_yx_type_def_method_range(result, class_index,
                                   &raw_class, &first, &last)) return 0;
    for (size_t i = first; i < last; ++i)
        if (xx_dotnet_yx_method_info(result, class_index, raw_class, i, NULL, 0,
                           SIZE_MAX, NULL, 0, NULL, NULL)) ++count;
    return count;
}

bool xx_dotnet_yx_class_method_at(const xx_dotnet_yx_result *result,
                                size_t class_index, size_t method_index,
                                char *return_type_buffer,
                                size_t buffer_capacity,
                                xx_dotnet_yx_method *out)
{
    size_t raw_class, first, last, ordinal = 0;
    if (!out || !xx_dotnet_yx_type_def_method_range(result, class_index,
                                          &raw_class, &first, &last))
        return false;
    for (size_t i = first; i < last; ++i) {
        xx_dotnet_yx_method current;
        if (!xx_dotnet_yx_method_info(result, class_index, raw_class, i, NULL, 0,
                            SIZE_MAX, NULL, 0, &current, NULL)) continue;
        if (ordinal++ != method_index) continue;
        if (return_type_buffer && buffer_capacity > current.return_type_length)
            return xx_dotnet_yx_method_info(result, class_index, raw_class, i,
                                   return_type_buffer, buffer_capacity,
                                   SIZE_MAX, NULL, 0, out, NULL);
        *out = current;
        return true;
    }
    return false;
}

bool xx_dotnet_yx_method_parameter_at(const xx_dotnet_yx_result *result,
                                    const xx_dotnet_yx_method *method,
                                    size_t parameter_index,
                                    char *type_buffer, size_t buffer_capacity,
                                    xx_dotnet_yx_method_parameter *out)
{
    xx_dotnet_yx_method_parameter parameter;
    if (!method || !out || parameter_index >= method->number_of_parameters ||
        !xx_dotnet_yx_method_info(result, method->class_index,
                         method->raw_class_index, method->raw_method_index,
                         NULL, 0, parameter_index, type_buffer,
                         buffer_capacity, NULL, &parameter)) return false;
    *out = parameter;
    return true;
}

bool xx_dotnet_yx_method_generic_parameter_at(const xx_dotnet_yx_result *result,
                                            const xx_dotnet_yx_method *method,
                                            size_t parameter_index,
                                            const uint8_t **name,
                                            size_t *length)
{
    return result && method && name && length &&
           parameter_index < method->number_of_generic_parameters &&
           xx_dotnet_yx_generic_name(result, method->raw_method_index, true,
                            parameter_index, name, length);
}

bool xx_dotnet_yx_type_spec_fullname(const xx_dotnet_yx_result *result,
                                   size_t raw_type_spec_index,
                                   size_t class_index,
                                   char *buffer, size_t buffer_capacity,
                                   size_t *required_capacity,
                                   size_t *length)
{
    xx_dotnet_yx_type_context context;
    xx_dotnet_yx_type_text text = {buffer, buffer_capacity, 0};
    bool ok;
    if (required_capacity) *required_capacity = 0;
    if (length) *length = 0;
    if (!result || !result->is_dotnet) return false;
    context.result = result;
    context.class_index = class_index;
    context.raw_method_index = SIZE_MAX;
    context.depth = 0;
    ok = xx_dotnet_yx_parse_type_spec_index(&context, raw_type_spec_index, &text);
    if (!ok || text.length == SIZE_MAX) return false;
    xx_dotnet_yx_text_finish(&text);
    if (required_capacity) *required_capacity = text.length + 1;
    if (length) *length = text.length;
    return true;
}
