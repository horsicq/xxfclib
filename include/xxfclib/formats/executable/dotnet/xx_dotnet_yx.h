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

#ifndef XXFCLIB_DOTNET_YX_H
#define XXFCLIB_DOTNET_YX_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* YARA-X-exact .NET helpers.
 *
 * The regular readers in xx_dotnet.h / xx_dotnet_inspect.h follow the flat
 * inspection semantics that DIE and the script API rely on, with different
 * limits (128 streams, row counts up to 0xFFFFFF, >= 72-byte CLI header,
 * duplicate-name and stream-overlap rejection, UTF-8 #US conversion). The
 * functions in this header instead reproduce, bit for bit, the parsing
 * behavior of YARA-X 1.23.0's dotnet module (and of the xxyara port of it):
 * the 72-byte CLI header requirement, up to 256 streams resolved by name
 * (last one wins, overlaps tolerated when in bounds), every metadata table
 * capped at 15,000 rows before coded-index widths are computed, raw UTF-16LE
 * #US user strings with the truncated-payload invalidation quirk, at most 16
 * GUIDs in the byte-swapped lowercase format, lazy class/method/base-type
 * names, the ECMA-335 signature formatter, and the GuidAttribute typelib
 * extraction. PE RVA translation uses xx_pe_yx_rva_to_offset.
 *
 * The result borrows the scanned buffer (all text pointers refer to `data`),
 * so data must outlive the result. xx_dotnet_yx_parse fills a caller-provided
 * result structure; the accessors are read-only over it. */

typedef struct {
    const uint8_t *name; /* NULL if the header name is not valid UTF-8. */
    size_t name_length;
    uint32_t offset; /* Absolute file offset. */
    uint32_t size;
} xx_dotnet_yx_stream;

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t build_number;
    uint16_t revision_number;
} xx_dotnet_yx_version;

typedef struct {
    bool present;
    const uint8_t *name;
    size_t name_length;
    const uint8_t *culture;
    size_t culture_length;
    xx_dotnet_yx_version version;
} xx_dotnet_yx_assembly;

typedef struct {
    const uint8_t *name;
    size_t name_length;
    const uint8_t *public_key_or_token;
    size_t public_key_or_token_length;
    xx_dotnet_yx_version version;
} xx_dotnet_yx_assembly_ref;

typedef struct {
    const uint8_t *name;
    size_t name_length;
    bool has_offset;
    uint32_t offset;
    bool has_length;
    uint32_t length;
} xx_dotnet_yx_resource;

typedef struct {
    const char *fullname; /* Points into caller-provided buffer if present. */
    size_t fullname_length;
    size_t required_fullname_capacity; /* Includes trailing NUL. */
    const char *name;
    size_t name_length;
    const char *namespace_name;
    size_t namespace_length;
    const char *visibility; /* Static string. */
    const char *type; /* "class" or "interface". */
    bool is_abstract;
    bool is_sealed;
    size_t number_of_base_types;
    size_t number_of_generic_parameters;
} xx_dotnet_yx_class;

typedef struct {
    const uint8_t *name; /* Borrowed from the #Strings heap. */
    size_t name_length;
    const char *visibility;
    bool is_abstract;
    bool is_static;
    bool is_virtual;
    bool is_final;
    bool has_return_type; /* Constructors have an undefined return type. */
    size_t return_type_length; /* Excludes the trailing NUL. */
    size_t number_of_generic_parameters;
    size_t number_of_parameters;
    size_t raw_method_index;
    size_t class_index;
    size_t raw_class_index;
    size_t raw_param_list_index;
} xx_dotnet_yx_method;

typedef struct {
    const uint8_t *name; /* NULL means generated name P_<index>. */
    size_t name_length;
    size_t generated_name_index;
    size_t type_length; /* Excludes the trailing NUL. */
} xx_dotnet_yx_method_parameter;

typedef struct {
    bool is_dotnet;
    const uint8_t *module_name; /* NULL when the Module table has no name. */
    size_t module_name_length;
    const uint8_t *version;
    size_t version_length;
    const uint8_t *typelib;
    size_t typelib_length;
    size_t stream_count;
    size_t guid_count;
    size_t class_count;
    size_t assembly_ref_count;
    size_t module_ref_count;
    size_t resource_count;
    size_t field_offset_count;
    size_t user_string_count;
    size_t constant_count;
    xx_dotnet_yx_assembly assembly;
    /* Private borrowed slices used by the indexed assembly reference API. */
    const uint8_t *assembly_ref_rows;
    size_t assembly_ref_row_size;
    const uint8_t *strings_heap;
    size_t strings_heap_size;
    const uint8_t *blob_heap;
    size_t blob_heap_size;
    uint8_t string_index_size;
    uint8_t blob_index_size;
    const uint8_t *module_ref_rows;
    size_t module_ref_row_count;
    const uint8_t *constant_rows;
    size_t constant_row_count;
    size_t constant_row_size;
    const uint8_t *field_rva_rows;
    size_t field_rva_row_count;
    const uint8_t *resource_rows;
    size_t resource_row_size;
    const uint8_t *resource_bytes;
    size_t resource_bytes_length;
    uint32_t resource_file_offset;
    const uint8_t *type_ref_rows;
    size_t type_ref_row_count;
    size_t type_ref_row_size;
    const uint8_t *type_def_rows;
    size_t type_def_row_count;
    size_t type_def_row_size;
    const uint8_t *interface_impl_rows;
    size_t interface_impl_row_count;
    const uint8_t *type_spec_rows;
    size_t type_spec_row_count;
    const uint8_t *method_def_rows;
    size_t method_def_row_count;
    size_t method_def_row_size;
    const uint8_t *param_rows;
    size_t param_row_count;
    size_t param_row_size;
    const uint8_t *nested_class_rows;
    size_t nested_class_row_count;
    const uint8_t *generic_param_rows;
    size_t generic_param_row_count;
    size_t generic_param_row_size;
    xx_dotnet_yx_stream streams[256];
} xx_dotnet_yx_result;

/* Parse a PE/CLR metadata root. All text pointers refer to `data`, whose
 * lifetime must cover use of `result`. Malformed or ordinary files produce
 * is_dotnet=false; a false return indicates an invalid output pointer. */
XXFC_API bool xx_dotnet_yx_parse(const uint8_t *data, size_t size,
                                 xx_dotnet_yx_result *result);

/* Accessors for repeated values. The user string bytes are UTF-16LE and
 * exclude the final ECMA-335 terminal flag byte. */
XXFC_API bool xx_dotnet_yx_guid_at(const uint8_t *data, size_t size,
                                   const xx_dotnet_yx_result *result,
                                   size_t index, char text[37]);
XXFC_API bool xx_dotnet_yx_user_string_at(const uint8_t *data, size_t size,
                                          const xx_dotnet_yx_result *result,
                                          size_t index, const uint8_t **bytes,
                                          size_t *length);
XXFC_API bool xx_dotnet_yx_assembly_ref_at(const xx_dotnet_yx_result *result,
                                           size_t index,
                                           xx_dotnet_yx_assembly_ref *out);
XXFC_API bool xx_dotnet_yx_module_ref_at(const xx_dotnet_yx_result *result,
                                         size_t index, const uint8_t **name,
                                         size_t *length);
XXFC_API bool xx_dotnet_yx_constant_at(const xx_dotnet_yx_result *result,
                                       size_t index, const uint8_t **bytes,
                                       size_t *length);
XXFC_API bool xx_dotnet_yx_field_offset_at(const uint8_t *data, size_t size,
                                           const xx_dotnet_yx_result *result,
                                           size_t index, uint32_t *offset);
XXFC_API bool xx_dotnet_yx_resource_at(const xx_dotnet_yx_result *result,
                                       size_t index,
                                       xx_dotnet_yx_resource *out);
/* Pass NULL and 0 for the buffer to query required_fullname_capacity, then
 * call again with a buffer of that size. A zero capacity means the official
 * full name is undefined for this class. */
XXFC_API bool xx_dotnet_yx_class_at(const xx_dotnet_yx_result *result,
                                    size_t index, char *fullname_buffer,
                                    size_t buffer_capacity,
                                    xx_dotnet_yx_class *out);
XXFC_API bool xx_dotnet_yx_class_raw_index(const xx_dotnet_yx_result *result,
                                           size_t class_index,
                                           size_t *raw_index);
XXFC_API bool xx_dotnet_yx_type_def_or_ref_fullname(
    const xx_dotnet_yx_result *result, uint16_t coded, char *buffer,
    size_t capacity, size_t *required, size_t *length);
XXFC_API bool xx_dotnet_yx_class_generic_parameter_at(
    const xx_dotnet_yx_result *result, size_t class_index,
    size_t parameter_index, const uint8_t **name, size_t *length);
/* Query a base type name with NULL,0, then call with required_capacity.
 * Only resolvable base types are counted. */
XXFC_API bool xx_dotnet_yx_class_base_type_at(
    const xx_dotnet_yx_result *result, size_t class_index, size_t base_index,
    char *buffer, size_t buffer_capacity, size_t *required_capacity,
    size_t *length);

/* Methods with missing names or malformed signatures are omitted, exactly as
 * in the reference parser. All indices are zero-based and refer to the
 * filtered `classes`/`methods` arrays visible to rules and CLI dumps. */
XXFC_API size_t xx_dotnet_yx_class_method_count(
    const xx_dotnet_yx_result *result, size_t class_index);

/* Pass NULL/0 for return_type_buffer to query return_type_length; then call
 * with at least return_type_length+1 bytes. The buffer is NUL-terminated when
 * large enough. A constructor reports has_return_type=false. */
XXFC_API bool xx_dotnet_yx_class_method_at(const xx_dotnet_yx_result *result,
                                           size_t class_index,
                                           size_t method_index,
                                           char *return_type_buffer,
                                           size_t buffer_capacity,
                                           xx_dotnet_yx_method *out);

XXFC_API bool xx_dotnet_yx_method_parameter_at(
    const xx_dotnet_yx_result *result, const xx_dotnet_yx_method *method,
    size_t parameter_index, char *type_buffer, size_t buffer_capacity,
    xx_dotnet_yx_method_parameter *out);

XXFC_API bool xx_dotnet_yx_method_generic_parameter_at(
    const xx_dotnet_yx_result *result, const xx_dotnet_yx_method *method,
    size_t parameter_index, const uint8_t **name, size_t *length);

/* Resolve a TypeSpec row for base types as well as method signatures. */
XXFC_API bool xx_dotnet_yx_type_spec_fullname(
    const xx_dotnet_yx_result *result, size_t raw_type_spec_index,
    size_t class_index, char *buffer, size_t buffer_capacity,
    size_t *required_capacity, size_t *length);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_DOTNET_YX_H */
