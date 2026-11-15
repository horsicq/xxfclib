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


#include <xxfclib/formats/executable/macho/xx_macho_yx.h>

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xxfclib/xml/xx_xml.h"
#include "xxfclib/algo/x509/xx_x509.h"
#include "xxfclib/strings/xx_string_casefold.h"

#define V_ASN1_UNIVERSAL 0x00
#define V_ASN1_CONTEXT_SPECIFIC 0x80
#define V_ASN1_INTEGER 2
#define V_ASN1_OBJECT 6
#define V_ASN1_SEQUENCE 16
#define V_ASN1_SET 17

#define XX_MACHO_YX_MH_MAGIC UINT32_C(0xfeedface)
#define XX_MACHO_YX_MH_CIGAM UINT32_C(0xcefaedfe)
#define XX_MACHO_YX_MH_MAGIC_64 UINT32_C(0xfeedfacf)
#define XX_MACHO_YX_MH_CIGAM_64 UINT32_C(0xcffaedfe)
#define XX_MACHO_YX_FAT_MAGIC UINT32_C(0xcafebabe)
#define XX_MACHO_YX_FAT_CIGAM UINT32_C(0xbebafeca)
#define XX_MACHO_YX_FAT_MAGIC_64 UINT32_C(0xcafebabf)
#define XX_MACHO_YX_FAT_CIGAM_64 UINT32_C(0xbfbafeca)

typedef struct {
    const uint8_t *data;
    size_t size;
    bool little;
    bool is64;
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
    uint64_t entry_point;
    uint64_t stack_size;
    bool has_entry_point;
    bool has_stack_size;
    uint64_t entry_rva;
    bool has_entry_rva;
    const uint8_t *dynamic_linker;
    size_t dynamic_linker_length;
    bool has_dynamic_linker;
    uint64_t source_version;
    bool has_source_version;
    uint8_t uuid[16];
    size_t uuid_length;
    bool has_uuid;
    uint64_t segments;
    uint64_t dylibs;
    uint64_t rpaths;
    uint64_t linker_options;
} xx_macho_yx_file;

typedef struct {
    const uint8_t *data;
    size_t size;
    bool valid;
    bool fat;
    bool fat_little;
    bool fat64;
    uint32_t fat_magic;
    uint32_t arch_count;
    uint64_t file_count;
    xx_macho_yx_file top_file;
    bool has_top_file;
} xx_macho_yx_model;

static bool xx_macho_yx_get(const uint8_t *data, size_t size, size_t at,
                          size_t width, bool little, uint64_t *value)
{
    uint64_t result = 0;
    if (!data || !value || (width != 1 && width != 2 && width != 4 && width != 8) ||
        at > size || size - at < width)
        return false;
    if (little) {
        for (size_t i = width; i != 0; --i)
            result = (result << 8) | data[at + i - 1];
    } else {
        for (size_t i = 0; i < width; ++i)
            result = (result << 8) | data[at + i];
    }
    *value = result;
    return true;
}

static bool xx_macho_yx_get32(const uint8_t *data, size_t size, size_t at,
                            bool little, uint32_t *value)
{
    uint64_t result;
    if (!value || !xx_macho_yx_get(data, size, at, 4, little, &result))
        return false;
    *value = (uint32_t) result;
    return true;
}

static bool xx_macho_yx_thread_rva(const uint8_t *payload,
                                 size_t payload_size,
                                 const xx_macho_yx_file *file, uint64_t *rva)
{
    size_t need = 8, offset = 8, width = 4;
    switch (file->cputype) {
    case 0x7: need += 16u * 4u; offset += 10u * 4u; break;
    case 0x01000007: need += 21u * 8u; offset += 16u * 8u;
        width = 8; break;
    case 0x0c: need += 17u * 4u; offset += 15u * 4u; break;
    case 0x0100000c: need += 33u * 8u + 4u; offset += 32u * 8u;
        width = 8; break;
    case 0x12: need += 40u * 4u; break;
    case 0x01000012: need += 2u * 8u + 32u * 8u + 4u + 4u * 8u;
        width = 8; break;
    case 0x0e: need += 18u * 4u; offset += 4u; break;
    case 0x06: need += 16u * 4u + 2u + 2u + 4u;
        offset += 16u * 4u + 2u + 2u; break;
    case 0x0d: need += 34u * 4u; offset += 31u * 4u; break;
    default: if (payload_size < 8) return false; *rva = 0; return true;
    }
    if (payload_size < need) return false;
    return xx_macho_yx_get(payload, payload_size, offset, width,
                         file->little, rva);
}

static bool xx_macho_yx_rva_to_offset(const xx_macho_yx_file *file,
                                    uint64_t rva, uint64_t *offset)
{
    size_t position = file->is64 ? 32u : 28u;
    for (uint32_t i = 0; i < file->ncmds; ++i) {
        uint32_t command, command_size;
        const uint8_t *payload;
        size_t payload_size, prefix_size, section_size, integer_width;
        uint32_t sections;
        uint64_t vmaddr, vmsize, fileoff;
        if (position > file->size || file->size - position < 8 ||
            !xx_macho_yx_get32(file->data, file->size, position,
                             file->little, &command) ||
            !xx_macho_yx_get32(file->data, file->size, position + 4,
                             file->little, &command_size)) break;
        position += 8;
        payload_size = command_size >= 8 ? (size_t)command_size - 8 : 0;
        if (payload_size > file->size - position) break;
        payload = file->data + position;
        prefix_size = file->is64 ? 64u : 48u;
        section_size = file->is64 ? 80u : 68u;
        integer_width = file->is64 ? 8u : 4u;
        if ((command == 1 || command == 0x19) &&
            payload_size >= prefix_size &&
            xx_macho_yx_get32(payload, payload_size, prefix_size - 8,
                             file->little, &sections) &&
            sections <= (payload_size - prefix_size) / section_size &&
            xx_macho_yx_get(payload, payload_size, 16, integer_width,
                          file->little, &vmaddr) &&
            xx_macho_yx_get(payload, payload_size,
                          16 + integer_width, integer_width,
                          file->little, &vmsize) &&
            xx_macho_yx_get(payload, payload_size,
                          16 + integer_width * 2, integer_width,
                          file->little, &fileoff)) {
            if (vmsize > UINT64_MAX - vmaddr) return false;
            if (rva >= vmaddr && rva - vmaddr < vmsize) {
                uint64_t delta = rva - vmaddr;
                if (delta > UINT64_MAX - fileoff) return false;
                *offset = fileoff + delta;
                return true;
            }
        }
        position += payload_size;
    }
    return false;
}

static bool xx_macho_yx_parse_file(const uint8_t *data, size_t size,
                                 xx_macho_yx_file *file)
{
    uint32_t magic;
    size_t position;
    if (!file || !xx_macho_yx_get32(data, size, 0, false, &magic))
        return false;
    if (magic != XX_MACHO_YX_MH_MAGIC && magic != XX_MACHO_YX_MH_CIGAM &&
        magic != XX_MACHO_YX_MH_MAGIC_64 && magic != XX_MACHO_YX_MH_CIGAM_64)
        return false;
    memset(file, 0, sizeof(*file));
    file->data = data;
    file->size = size;
    file->magic = magic;
    file->little = magic == XX_MACHO_YX_MH_CIGAM || magic == XX_MACHO_YX_MH_CIGAM_64;
    file->is64 = magic == XX_MACHO_YX_MH_MAGIC_64 || magic == XX_MACHO_YX_MH_CIGAM_64;
    if (size < (file->is64 ? 32u : 28u) ||
        !xx_macho_yx_get32(data, size, 4, file->little, &file->cputype) ||
        !xx_macho_yx_get32(data, size, 8, file->little, &file->cpusubtype) ||
        !xx_macho_yx_get32(data, size, 12, file->little, &file->filetype) ||
        !xx_macho_yx_get32(data, size, 16, file->little, &file->ncmds) ||
        !xx_macho_yx_get32(data, size, 20, file->little, &file->sizeofcmds) ||
        !xx_macho_yx_get32(data, size, 24, file->little, &file->flags))
        return false;
    if (file->is64 &&
        !xx_macho_yx_get32(data, size, 28, file->little, &file->reserved))
        return false;

    position = file->is64 ? 32u : 28u;
    for (uint32_t i = 0; i < file->ncmds; ++i) {
        uint32_t command, command_size;
        const uint8_t *payload;
        size_t payload_size;
        uint64_t temporary;
        if (position > size || size - position < 8 ||
            !xx_macho_yx_get32(data, size, position, file->little, &command) ||
            !xx_macho_yx_get32(data, size, position + 4, file->little,
                             &command_size))
            break;
        position += 8;
        payload_size = command_size >= 8 ? (size_t)command_size - 8 : 0;
        if (position > size || payload_size > size - position) break;
        payload = data + position;

        if (command == UINT32_C(0x80000028) && payload_size >= 16) {
            xx_macho_yx_get(payload, payload_size, 0, 8, file->little,
                          &file->entry_point);
            xx_macho_yx_get(payload, payload_size, 8, 8, file->little,
                          &file->stack_size);
            file->has_entry_point = true;
            file->has_stack_size = true;
        } else if (command == 5 &&
                   xx_macho_yx_thread_rva(payload, payload_size, file,
                                        &file->entry_rva)) {
            file->has_entry_rva = true;
        } else if (command == 1 || command == 0x19) {
            size_t prefix_size = file->is64 ? 64u : 48u;
            size_t section_size = file->is64 ? 80u : 68u;
            uint32_t sections;
            if (payload_size >= prefix_size &&
                xx_macho_yx_get32(payload, payload_size, prefix_size - 8,
                                 file->little, &sections) &&
                sections <= (payload_size - prefix_size) / section_size)
                ++file->segments;
        } else if (command == 0x0c || command == 0x0d ||
                   command == UINT32_C(0x80000018) ||
                   command == UINT32_C(0x8000001f) || command == 0x20 ||
                   command == UINT32_C(0x80000023)) {
            if (payload_size >= 16) ++file->dylibs;
        } else if (command == UINT32_C(0x8000001c)) {
            if (payload_size >= 4) ++file->rpaths;
        } else if (command == 0x0e || command == 0x0f ||
                   command == 0x27) {
            if (payload_size >= 4) {
                const uint8_t *end = memchr(payload + 4, 0,
                                             payload_size - 4);
                file->dynamic_linker = payload + 4;
                file->dynamic_linker_length = end ?
                    (size_t)(end - (payload + 4)) : payload_size - 4;
                file->has_dynamic_linker = true;
            }
        } else if (command == 0x2a && payload_size >= 8 &&
                   xx_macho_yx_get(payload, payload_size, 0, 8,
                                 file->little, &temporary)) {
            file->source_version = temporary;
            file->has_source_version = true;
        } else if (command == 0x1b && payload_size >= sizeof(file->uuid)) {
            memcpy(file->uuid, payload, sizeof(file->uuid));
            file->uuid_length = sizeof(file->uuid);
            while (file->uuid_length != 0 &&
                   file->uuid[file->uuid_length - 1] == 0)
                --file->uuid_length;
            file->has_uuid = true;
        } else if (command == 0x2d && payload_size >= 4 &&
                   xx_macho_yx_get(payload, payload_size, 0, 4,
                                 file->little, &temporary)) {
            size_t cursor = 4;
            uint64_t names = temporary;
            bool valid = names <= payload_size - cursor;
            for (uint64_t j = 0; valid && j < names; ++j) {
                const uint8_t *end = memchr(payload + cursor, 0,
                                             payload_size - cursor);
                if (!end) valid = false;
                else cursor = (size_t)(end - payload) + 1;
            }
            if (valid) file->linker_options += names;
        }
        position += payload_size;
    }
    if (file->has_entry_rva)
        file->has_entry_point = xx_macho_yx_rva_to_offset(
            file, file->entry_rva, &file->entry_point);
    return true;
}

static bool xx_macho_yx_arch(const xx_macho_yx_model *model, uint64_t index,
                           uint32_t *cputype, uint32_t *cpusubtype,
                           uint64_t *offset, uint64_t *length,
                           uint32_t *align, uint32_t *reserved)
{
    size_t width, at;
    if (!model || !model->fat || index >= model->arch_count) return false;
    width = model->fat64 ? 32u : 20u;
    at = 8u + (size_t)index * width;
    if (!xx_macho_yx_get32(model->data, model->size, at, model->fat_little,
                         cputype) ||
        !xx_macho_yx_get32(model->data, model->size, at + 4,
                         model->fat_little, cpusubtype) ||
        !xx_macho_yx_get(model->data, model->size, at + 8,
                       model->fat64 ? 8u : 4u, model->fat_little,
                       offset) ||
        !xx_macho_yx_get(model->data, model->size,
                       at + (model->fat64 ? 16u : 12u),
                       model->fat64 ? 8u : 4u, model->fat_little,
                       length) ||
        !xx_macho_yx_get32(model->data, model->size,
                         at + (model->fat64 ? 24u : 16u),
                         model->fat_little, align))
        return false;
    if (model->fat64) {
        if (!xx_macho_yx_get32(model->data, model->size, at + 28,
                             model->fat_little, reserved)) return false;
    } else *reserved = 0;
    return true;
}

static void xx_macho_yx_parse(const uint8_t *data, size_t size,
                            xx_macho_yx_model *model)
{
    uint32_t magic;
    uint32_t count;
    size_t width;
    memset(model, 0, sizeof(*model));
    model->data = data;
    model->size = size;
    if (!xx_macho_yx_get32(data, size, 0, false, &magic)) return;
    if (magic != XX_MACHO_YX_FAT_MAGIC && magic != XX_MACHO_YX_FAT_CIGAM &&
        magic != XX_MACHO_YX_FAT_MAGIC_64 && magic != XX_MACHO_YX_FAT_CIGAM_64) {
        model->has_top_file = xx_macho_yx_parse_file(data, size,
                                                   &model->top_file);
        model->valid = model->has_top_file;
        return;
    }
    model->fat = true;
    model->fat_magic = magic;
    model->fat_little = magic == XX_MACHO_YX_FAT_CIGAM || magic == XX_MACHO_YX_FAT_CIGAM_64;
    model->fat64 = magic == XX_MACHO_YX_FAT_MAGIC_64 || magic == XX_MACHO_YX_FAT_CIGAM_64;
    width = model->fat64 ? 32u : 20u;
    if (!xx_macho_yx_get32(data, size, 4, model->fat_little, &count) ||
        count > (size - 8u) / width) return;
    model->valid = true;
    model->arch_count = count;
    for (uint64_t i = 0; i < count; ++i) {
        uint32_t cputype, cpusubtype, align, reserved;
        uint64_t offset, length;
        xx_macho_yx_file child;
        if (!xx_macho_yx_arch(model, i, &cputype, &cpusubtype,
                            &offset, &length, &align, &reserved)) {
            model->valid = false;
            return;
        }
        if (offset > size || length > size - (size_t)offset) continue;
        if (!xx_macho_yx_parse_file(data + (size_t)offset, (size_t)length,
                                  &child)) continue;
        ++model->file_count;
        model->top_file = child;
    }
    model->has_top_file = model->file_count == 1;
}

bool xx_macho_yx_top_file_span(const uint8_t *data, size_t size,
                             const uint8_t **bytes, size_t *length)
{
    xx_macho_yx_model model;
    if ((!data && size) || !bytes || !length) return false;
    xx_macho_yx_parse(data, size, &model);
    if (!model.valid || !model.has_top_file) return false;
    *bytes = model.top_file.data;
    *length = model.top_file.size;
    return true;
}

static bool xx_macho_yx_child(const xx_macho_yx_model *model, uint64_t index,
                            xx_macho_yx_file *child)
{
    uint64_t found = 0;
    if (!model || !model->valid || !model->fat || model->has_top_file ||
        index >= model->file_count || !child) return false;
    for (uint64_t i = 0; i < model->arch_count; ++i) {
        uint32_t cputype, cpusubtype, align, reserved;
        uint64_t offset, length;
        if (!xx_macho_yx_arch(model, i, &cputype, &cpusubtype,
                            &offset, &length, &align, &reserved)) return false;
        if (offset > model->size ||
            length > model->size - (size_t)offset ||
            !xx_macho_yx_parse_file(model->data + (size_t)offset,
                                  (size_t)length, child)) continue;
        if (found++ == index) return true;
    }
    return false;
}

bool xx_macho_yx_file_span(const uint8_t *data, size_t size, uint64_t index,
                          const uint8_t **bytes, size_t *length)
{
    xx_macho_yx_model model;
    xx_macho_yx_file child;
    if (!bytes || !length || (!data && size)) return false;
    xx_macho_yx_parse(data, size, &model);
    if (!xx_macho_yx_child(&model, index, &child)) return false;
    *bytes = child.data;
    *length = child.size;
    return true;
}

static bool xx_macho_yx_command_item(const xx_macho_yx_file *file,
                                    const char *collection, uint64_t index,
                                    const uint8_t **item, size_t *length)
{
    size_t position;
    uint64_t found = 0;
    if (!file || !collection || !item || !length) return false;
    position = file->is64 ? 32u : 28u;
    for (uint32_t i = 0; i < file->ncmds; ++i) {
        uint32_t command, command_size;
        const uint8_t *payload;
        size_t payload_size;
        if (position > file->size || file->size - position < 8 ||
            !xx_macho_yx_get32(file->data, file->size, position,
                             file->little, &command) ||
            !xx_macho_yx_get32(file->data, file->size, position + 4,
                             file->little, &command_size)) break;
        position += 8;
        payload_size = command_size >= 8 ? (size_t)command_size - 8 : 0;
        if (payload_size > file->size - position) break;
        payload = file->data + position;
        if (strcmp(collection, "segments") == 0 &&
            (command == 1 || command == 0x19)) {
            size_t prefix_size = file->is64 ? 64u : 48u;
            size_t section_size = file->is64 ? 80u : 68u;
            uint32_t sections;
            if (payload_size >= prefix_size &&
                xx_macho_yx_get32(payload, payload_size, prefix_size - 8,
                                 file->little, &sections) &&
                sections <= (payload_size - prefix_size) / section_size) {
                if (found++ == index) {
                    *item = payload;
                    *length = payload_size;
                    return true;
                }
            }
        } else if (strcmp(collection, "dylibs") == 0 &&
                   (command == 0x0c || command == 0x0d ||
                    command == UINT32_C(0x80000018) ||
                    command == UINT32_C(0x8000001f) || command == 0x20 ||
                    command == UINT32_C(0x80000023))) {
            if (payload_size >= 16 && found++ == index) {
                *item = payload;
                *length = payload_size;
                return true;
            }
        } else if (strcmp(collection, "rpaths") == 0 &&
                   command == UINT32_C(0x8000001c)) {
            if (payload_size >= 4 && found++ == index) {
                *item = payload;
                *length = payload_size;
                return true;
            }
        } else if (strcmp(collection, "linker_options") == 0 &&
                   command == 0x2d && payload_size >= 4) {
            uint32_t count;
            size_t cursor = 4;
            bool valid;
            if (!xx_macho_yx_get32(payload, payload_size, 0,
                                 file->little, &count)) continue;
            valid = count <= payload_size - cursor;
            for (uint32_t j = 0; valid && j < count; ++j) {
                const uint8_t *end = memchr(payload + cursor, 0,
                                             payload_size - cursor);
                if (!end) valid = false;
                else cursor = (size_t)(end - payload) + 1;
            }
            if (valid && index >= found && index - found < count) {
                cursor = 4;
                for (uint32_t j = 0; j < (uint32_t)(index - found); ++j) {
                    const uint8_t *end = memchr(payload + cursor, 0,
                                                 payload_size - cursor);
                    cursor = (size_t)(end - payload) + 1;
                }
                *item = payload + cursor;
                *length = (size_t)((const uint8_t *)memchr(
                    payload + cursor, 0, payload_size - cursor) -
                    (payload + cursor));
                return true;
            }
            if (valid) found += count;
        }
        position += payload_size;
    }
    return false;
}

static size_t xx_macho_yx_trimmed_length(const uint8_t *bytes, size_t length)
{
    while (length && bytes[length - 1] == 0) --length;
    return length;
}

typedef struct {
    const char *name;
    unsigned group;
    size_t offset;
} xx_macho_yx_object_spec;

enum {
    XX_MACHO_YX_OBJECT_SYMTAB,
    XX_MACHO_YX_OBJECT_DYSYMTAB,
    XX_MACHO_YX_OBJECT_DYLD_INFO,
    XX_MACHO_YX_OBJECT_EXPORT_TRIE,
    XX_MACHO_YX_OBJECT_CHAINED_FIXUPS,
    XX_MACHO_YX_OBJECT_CODE_SIGNATURE,
    XX_MACHO_YX_OBJECT_BUILD_VERSION,
    XX_MACHO_YX_OBJECT_MIN_VERSION
};

static const xx_macho_yx_object_spec xx_macho_yx_object_fields[] = {
    {"symtab.symoff", XX_MACHO_YX_OBJECT_SYMTAB, 0},
    {"symtab.nsyms", XX_MACHO_YX_OBJECT_SYMTAB, 4},
    {"symtab.stroff", XX_MACHO_YX_OBJECT_SYMTAB, 8},
    {"symtab.strsize", XX_MACHO_YX_OBJECT_SYMTAB, 12},
    {"dysymtab.ilocalsym", XX_MACHO_YX_OBJECT_DYSYMTAB, 0},
    {"dysymtab.nlocalsym", XX_MACHO_YX_OBJECT_DYSYMTAB, 4},
    {"dysymtab.iextdefsym", XX_MACHO_YX_OBJECT_DYSYMTAB, 8},
    {"dysymtab.nextdefsym", XX_MACHO_YX_OBJECT_DYSYMTAB, 12},
    {"dysymtab.tocoff", XX_MACHO_YX_OBJECT_DYSYMTAB, 16},
    {"dysymtab.ntoc", XX_MACHO_YX_OBJECT_DYSYMTAB, 20},
    {"dysymtab.modtaboff", XX_MACHO_YX_OBJECT_DYSYMTAB, 24},
    {"dysymtab.nmodtab", XX_MACHO_YX_OBJECT_DYSYMTAB, 28},
    {"dysymtab.extrefsymoff", XX_MACHO_YX_OBJECT_DYSYMTAB, 32},
    {"dysymtab.nextrefsyms", XX_MACHO_YX_OBJECT_DYSYMTAB, 36},
    {"dysymtab.indirectsymoff", XX_MACHO_YX_OBJECT_DYSYMTAB, 40},
    {"dysymtab.nindirectsyms", XX_MACHO_YX_OBJECT_DYSYMTAB, 44},
    {"dysymtab.extreloff", XX_MACHO_YX_OBJECT_DYSYMTAB, 48},
    {"dysymtab.nextrel", XX_MACHO_YX_OBJECT_DYSYMTAB, 52},
    {"dysymtab.locreloff", XX_MACHO_YX_OBJECT_DYSYMTAB, 56},
    {"dysymtab.nlocrel", XX_MACHO_YX_OBJECT_DYSYMTAB, 60},
    {"dyld_info.rebase_off", XX_MACHO_YX_OBJECT_DYLD_INFO, 0},
    {"dyld_info.rebase_size", XX_MACHO_YX_OBJECT_DYLD_INFO, 4},
    {"dyld_info.bind_off", XX_MACHO_YX_OBJECT_DYLD_INFO, 8},
    {"dyld_info.bind_size", XX_MACHO_YX_OBJECT_DYLD_INFO, 12},
    {"dyld_info.weak_bind_off", XX_MACHO_YX_OBJECT_DYLD_INFO, 16},
    {"dyld_info.weak_bind_size", XX_MACHO_YX_OBJECT_DYLD_INFO, 20},
    {"dyld_info.lazy_bind_off", XX_MACHO_YX_OBJECT_DYLD_INFO, 24},
    {"dyld_info.lazy_bind_size", XX_MACHO_YX_OBJECT_DYLD_INFO, 28},
    {"dyld_info.export_off", XX_MACHO_YX_OBJECT_DYLD_INFO, 32},
    {"dyld_info.export_size", XX_MACHO_YX_OBJECT_DYLD_INFO, 36},
    {"code_signature_data.dataoff", XX_MACHO_YX_OBJECT_CODE_SIGNATURE, 0},
    {"code_signature_data.datasize", XX_MACHO_YX_OBJECT_CODE_SIGNATURE, 4},
    {"build_version.platform", XX_MACHO_YX_OBJECT_BUILD_VERSION, 0},
    {"build_version.ntools", XX_MACHO_YX_OBJECT_BUILD_VERSION, 12},
    {"min_version.device", XX_MACHO_YX_OBJECT_MIN_VERSION, 0}
};

static const xx_macho_yx_object_spec *xx_macho_yx_object_spec_for(
    const char *member)
{
    if (!member) return NULL;
    for (size_t i = 0; i < sizeof(xx_macho_yx_object_fields) /
                             sizeof(*xx_macho_yx_object_fields); ++i)
        if (strcmp(member, xx_macho_yx_object_fields[i].name) == 0)
            return &xx_macho_yx_object_fields[i];
    return NULL;
}

static bool xx_macho_yx_object_command(unsigned group, uint32_t command)
{
    switch (group) {
    case XX_MACHO_YX_OBJECT_SYMTAB: return command == 2;
    case XX_MACHO_YX_OBJECT_DYSYMTAB: return command == 0x0b;
    case XX_MACHO_YX_OBJECT_DYLD_INFO:
        return command == 0x22 || command == UINT32_C(0x80000022);
    case XX_MACHO_YX_OBJECT_EXPORT_TRIE:
        return command == UINT32_C(0x80000033);
    case XX_MACHO_YX_OBJECT_CHAINED_FIXUPS:
        return command == UINT32_C(0x80000034);
    case XX_MACHO_YX_OBJECT_CODE_SIGNATURE: return command == 0x1d;
    case XX_MACHO_YX_OBJECT_BUILD_VERSION: return command == 0x32;
    case XX_MACHO_YX_OBJECT_MIN_VERSION:
        return command == 0x24 || command == 0x25 ||
               command == 0x2f || command == 0x30;
    default: return false;
    }
}

static bool xx_macho_yx_object_payload(const xx_macho_yx_file *file,
                                     unsigned group,
                                     const uint8_t **last,
                                     size_t *last_length,
                                     uint32_t *last_command)
{
    size_t position = file->is64 ? 32u : 28u;
    *last = NULL;
    *last_length = 0;
    *last_command = 0;
    for (uint32_t i = 0; i < file->ncmds; ++i) {
        uint32_t command, command_size;
        size_t payload_size, required;
        const uint8_t *payload;
        if (position > file->size || file->size - position < 8 ||
            !xx_macho_yx_get32(file->data, file->size, position,
                             file->little, &command) ||
            !xx_macho_yx_get32(file->data, file->size, position + 4,
                             file->little, &command_size)) break;
        position += 8;
        payload_size = command_size >= 8 ? (size_t)command_size - 8 : 0;
        if (payload_size > file->size - position) break;
        payload = file->data + position;
        required = group == XX_MACHO_YX_OBJECT_SYMTAB ? 16u :
                   group == XX_MACHO_YX_OBJECT_DYSYMTAB ? 64u :
                   group == XX_MACHO_YX_OBJECT_DYLD_INFO ? 40u :
                   group == XX_MACHO_YX_OBJECT_BUILD_VERSION ? 16u : 8u;
        if (xx_macho_yx_object_command(group, command) &&
            payload_size >= required) {
            bool valid = true;
            if (group == XX_MACHO_YX_OBJECT_BUILD_VERSION) {
                uint32_t count;
                valid = xx_macho_yx_get32(payload, payload_size, 12,
                                        file->little, &count) &&
                        count <= (payload_size - 16u) / 8u;
            }
            if (valid) {
                *last = payload;
                *last_length = payload_size;
                *last_command = command;
            }
        }
        position += payload_size;
    }
    return *last != NULL;
}

static bool xx_macho_yx_object_field(const xx_macho_yx_file *file,
                                   const xx_macho_yx_object_spec *spec,
                                   uint64_t *value)
{
    const uint8_t *last;
    size_t last_length;
    uint32_t last_command;
    if (!xx_macho_yx_object_payload(file, spec->group, &last, &last_length,
                                  &last_command)) return false;
    if (spec->group == XX_MACHO_YX_OBJECT_MIN_VERSION) {
        *value = last_command;
        return true;
    }
    return xx_macho_yx_get(last, last_length, spec->offset, 4,
                         file->little, value);
}

static bool xx_macho_yx_build_tool_item(const xx_macho_yx_file *file,
                                      uint64_t index, uint64_t *tool,
                                      uint64_t *version)
{
    const uint8_t *payload;
    size_t payload_size;
    uint32_t command, count;
    size_t offset;
    if (!xx_macho_yx_object_payload(file, XX_MACHO_YX_OBJECT_BUILD_VERSION,
                                  &payload, &payload_size, &command) ||
        !xx_macho_yx_get32(payload, payload_size, 12, file->little, &count) ||
        index >= count) return false;
    offset = 16u + (size_t)index * 8u;
    return xx_macho_yx_get(payload, payload_size, offset, 4,
                         file->little, tool) &&
           xx_macho_yx_get(payload, payload_size, offset + 4u, 4,
                         file->little, version);
}

bool xx_macho_yx_field_supported(const char *member)
{
    static const char *const fields[] = {
        "magic", "cputype", "cpusubtype", "filetype", "ncmds",
        "sizeofcmds", "flags", "reserved", "number_of_segments",
        "entry_point", "stack_size", "fat_magic", "nfat_arch",
        "dysymtab.iundefsym", "dysymtab.nundefsym"
    };
    if (!member) return false;
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i)
        if (strcmp(member, fields[i]) == 0) return true;
    return xx_macho_yx_object_spec_for(member) != NULL;
}

bool xx_macho_yx_field(const uint8_t *data, size_t size, const char *member,
                    uint64_t *value)
{
    xx_macho_yx_model model;
    const xx_macho_yx_file *file;
    if (!member || !value || (!data && size)) return false;
    xx_macho_yx_parse(data, size, &model);
    if (!model.valid) return false;
    if (strcmp(member, "fat_magic") == 0) {
        if (!model.fat || model.has_top_file) return false;
        *value = model.fat_magic;
        return true;
    }
    if (strcmp(member, "nfat_arch") == 0) {
        if (!model.fat || model.has_top_file) return false;
        *value = model.arch_count;
        return true;
    }
    if (!model.has_top_file) return false;
    file = &model.top_file;
    {
        const xx_macho_yx_object_spec *spec =
            xx_macho_yx_object_spec_for(member);
        if (spec) return xx_macho_yx_object_field(file, spec, value);
    }
    if (strcmp(member, "magic") == 0) *value = file->magic;
    else if (strcmp(member, "cputype") == 0) *value = file->cputype;
    else if (strcmp(member, "cpusubtype") == 0) *value = file->cpusubtype;
    else if (strcmp(member, "filetype") == 0) *value = file->filetype;
    else if (strcmp(member, "ncmds") == 0) *value = file->ncmds;
    else if (strcmp(member, "sizeofcmds") == 0) *value = file->sizeofcmds;
    else if (strcmp(member, "flags") == 0) *value = file->flags;
    else if (strcmp(member, "reserved") == 0) {
        if (!file->is64) return false;
        *value = file->reserved;
    } else if (strcmp(member, "number_of_segments") == 0)
        *value = file->segments;
    else if (strcmp(member, "entry_point") == 0) {
        if (!file->has_entry_point) return false;
        *value = file->entry_point;
    } else if (strcmp(member, "stack_size") == 0) {
        if (!file->has_stack_size) return false;
        *value = file->stack_size;
    } else return false;
    return true;
}

bool xx_macho_yx_collection_supported(const char *member)
{
    static const char *const fields[] = {
        "segments", "dylibs", "rpaths", "linker_options",
        "fat_arch", "file", "build_version.tools", "symtab.entries",
        "symtab.nlists", "entitlements", "certificates", "imports", "exports"
    };
    if (!member) return false;
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i)
        if (strcmp(member, fields[i]) == 0) return true;
    return false;
}

/* Retain only nonempty, terminated symbol names, just as the reference parser
 * does. A malformed later symbol leaves already parsed entries visible. */
static bool xx_macho_yx_symtab_entry(const xx_macho_yx_file *file, uint64_t wanted,
                                    const uint8_t **name, size_t *name_length,
                                    size_t *symbol_offset, uint64_t *count)
{
    const uint8_t *payload;
    size_t payload_size;
    uint32_t command, symoff, nsyms, stroff, strsize;
    size_t width = file->is64 ? 16u : 12u;
    *count = 0;
    if (!xx_macho_yx_object_payload(file, XX_MACHO_YX_OBJECT_SYMTAB, &payload,
                                  &payload_size, &command)) return false;
    if (!xx_macho_yx_get32(payload, payload_size, 0, file->little, &symoff) ||
        !xx_macho_yx_get32(payload, payload_size, 4, file->little, &nsyms) ||
        !xx_macho_yx_get32(payload, payload_size, 8, file->little, &stroff) ||
        !xx_macho_yx_get32(payload, payload_size, 12, file->little, &strsize) ||
        symoff > file->size || stroff > file->size ||
        strsize > file->size - stroff) return true;
    for (uint32_t i = 0; i < nsyms; ++i) {
        size_t at;
        uint32_t strx;
        const uint8_t *bytes, *end;
        if ((size_t)i >= (file->size - symoff) / width) break;
        at = (size_t)symoff + (size_t)i * width;
        if (!xx_macho_yx_get32(file->data, file->size, at, file->little, &strx)) break;
        if (strx > strsize) continue;
        if (strx == strsize) break;
        bytes = file->data + stroff + strx;
        end = memchr(bytes, 0, strsize - strx);
        if (!end) break;
        if (end == bytes) continue;
        if (*count == wanted) {
            if (name) *name = bytes;
            if (name_length) *name_length = (size_t)(end - bytes);
            if (symbol_offset) *symbol_offset = at;
        }
        ++*count;
    }
    return true;
}

bool xx_macho_yx_collection_count(const uint8_t *data, size_t size,
                                const char *member, uint64_t *count)
{
    xx_macho_yx_model model;
    if (!xx_macho_yx_collection_supported(member) || !count || (!data && size))
        return false;
    *count = 0;
    xx_macho_yx_parse(data, size, &model);
    if (strcmp(member, "symtab.entries") == 0 || strcmp(member, "symtab.nlists") == 0) {
        bool present = model.has_top_file &&
            xx_macho_yx_symtab_entry(&model.top_file, UINT64_MAX, NULL, NULL, NULL, count);
        if (!present && strcmp(member, "symtab.nlists") == 0) *count = 1;
        return true;
    }
    if (strcmp(member, "entitlements") == 0 || strcmp(member, "certificates") == 0 ||
        strcmp(member, "imports") == 0 || strcmp(member, "exports") == 0) {
        xx_macho_yx_extras extras;
        if (xx_macho_yx_extras_parse(data, size, &extras)) {
            if (strcmp(member, "entitlements") == 0) *count = extras.entitlement_count;
            else if (strcmp(member, "certificates") == 0) *count = extras.certificate_count;
            else if (strcmp(member, "imports") == 0) *count = extras.import_count;
            else *count = extras.export_count;
            xx_macho_yx_extras_free(&extras);
        }
        return true;
    }
    if (strcmp(member, "build_version.tools") == 0) {
        const uint8_t *payload;
        size_t payload_size;
        uint32_t command, tool_count;
        /* YARA-X 1.21 evaluates this nested len() as one when the
         * optional build_version object is absent, even for non-Mach-O
         * data. Indexed tools remain undefined in that case. */
        *count = 1;
        if (model.has_top_file &&
            xx_macho_yx_object_payload(&model.top_file,
                                      XX_MACHO_YX_OBJECT_BUILD_VERSION, &payload,
                                      &payload_size, &command) &&
            xx_macho_yx_get32(payload, payload_size, 12,
                            model.top_file.little, &tool_count))
            *count = tool_count;
        return true;
    }
    if (!model.valid) return true;
    if (strcmp(member, "fat_arch") == 0) {
        if (model.fat && !model.has_top_file) *count = model.arch_count;
        return true;
    }
    if (strcmp(member, "file") == 0) {
        if (model.fat && !model.has_top_file) *count = model.file_count;
        return true;
    }
    if (!model.has_top_file) return true;
    if (strcmp(member, "segments") == 0)
        *count = model.top_file.segments;
    else if (strcmp(member, "dylibs") == 0)
        *count = model.top_file.dylibs;
    else if (strcmp(member, "rpaths") == 0)
        *count = model.top_file.rpaths;
    else if (strcmp(member, "linker_options") == 0)
        *count = model.top_file.linker_options;
    return true;
}

bool xx_macho_yx_fat_arch_field_supported(const char *member)
{
    static const char *const fields[] = {
        "cputype", "cpusubtype", "offset", "size", "align", "reserved"
    };
    if (!member) return false;
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i)
        if (strcmp(member, fields[i]) == 0) return true;
    return false;
}

bool xx_macho_yx_fat_arch_field(const uint8_t *data, size_t size, uint64_t index,
                              const char *member, uint64_t *value)
{
    xx_macho_yx_model model;
    uint32_t cputype, cpusubtype, align, reserved;
    uint64_t offset, length;
    if (!xx_macho_yx_fat_arch_field_supported(member) || !value ||
        (!data && size)) return false;
    xx_macho_yx_parse(data, size, &model);
    if (!model.valid || !model.fat || model.has_top_file ||
        !xx_macho_yx_arch(&model, index, &cputype, &cpusubtype,
                        &offset, &length, &align, &reserved)) return false;
    if (strcmp(member, "cputype") == 0) *value = cputype;
    else if (strcmp(member, "cpusubtype") == 0) *value = cpusubtype;
    else if (strcmp(member, "offset") == 0) *value = offset;
    else if (strcmp(member, "size") == 0) *value = length;
    else if (strcmp(member, "align") == 0) *value = align;
    else *value = reserved;
    return true;
}

bool xx_macho_yx_file_index_for_arch(const uint8_t *data, size_t size,
                                   uint64_t cputype, bool has_subtype,
                                   uint64_t cpusubtype, uint64_t *index)
{
    xx_macho_yx_model model;
    if (!index || (!data && size)) return false;
    xx_macho_yx_parse(data, size, &model);
    if (!model.valid || !model.fat || model.has_top_file) return false;
    for (uint64_t i = 0; i < model.arch_count; ++i) {
        uint32_t type, subtype, align, reserved;
        uint64_t offset, length;
        if (!xx_macho_yx_arch(&model, i, &type, &subtype, &offset,
                            &length, &align, &reserved)) return false;
        if (cputype == type && (!has_subtype || cpusubtype == subtype)) {
            *index = i;
            return true;
        }
    }
    return false;
}

bool xx_macho_yx_string_field_supported(const char *member)
{
    return member && strcmp(member, "dynamic_linker") == 0;
}

bool xx_macho_yx_string_field(const uint8_t *data, size_t size,
                           const char *member, const uint8_t **bytes,
                           size_t *length)
{
    xx_macho_yx_model model;
    if (!xx_macho_yx_string_field_supported(member) || !bytes || !length ||
        (!data && size)) return false;
    xx_macho_yx_parse(data, size, &model);
    if (!model.valid || !model.has_top_file ||
        !model.top_file.has_dynamic_linker) return false;
    *bytes = model.top_file.dynamic_linker;
    *length = model.top_file.dynamic_linker_length;
    return true;
}

static bool xx_macho_yx_format_source_version(const xx_macho_yx_file *file,
                                             char *buffer, size_t capacity,
                                             size_t *length)
{
    char text[32];
    uint64_t version;
    int count;
    if (!file || !file->has_source_version || !buffer) return false;
    version = file->source_version;
    count = snprintf(text, sizeof(text), "%" PRIu64 ".%" PRIu64
                     ".%" PRIu64 ".%" PRIu64 ".%" PRIu64,
                     version >> 40, (version >> 30) & UINT64_C(0x3f),
                     (version >> 20) & UINT64_C(0x3f),
                     (version >> 10) & UINT64_C(0x3f),
                     version & UINT64_C(0x3f));
    if (count < 0 || (size_t)count >= sizeof(text) ||
        (size_t)count >= capacity) return false;
    memcpy(buffer, text, (size_t)count + 1);
    if (length) *length = (size_t)count;
    return true;
}

bool xx_macho_yx_source_version(const uint8_t *data, size_t size,
                              char *buffer, size_t capacity,
                              size_t *length)
{
    xx_macho_yx_model model;
    if ((!data && size) || !buffer) return false;
    xx_macho_yx_parse(data, size, &model);
    return model.valid && model.has_top_file &&
           xx_macho_yx_format_source_version(&model.top_file, buffer,
                                           capacity, length);
}

bool xx_macho_yx_file_source_version(const uint8_t *data, size_t size,
                                   uint64_t index, char *buffer,
                                   size_t capacity, size_t *length)
{
    xx_macho_yx_model model;
    xx_macho_yx_file child;
    if ((!data && size) || !buffer) return false;
    xx_macho_yx_parse(data, size, &model);
    return xx_macho_yx_child(&model, index, &child) &&
           xx_macho_yx_format_source_version(&child, buffer, capacity,
                                           length);
}

static bool xx_macho_yx_format_uuid(const xx_macho_yx_file *file, char *buffer,
                                  size_t capacity, size_t *length)
{
    static const char hex[] = "0123456789ABCDEF";
    char text[37];
    size_t cursor = 0;
    if (!file || !file->has_uuid || !buffer) return false;
    for (size_t i = 0; i < file->uuid_length; ++i) {
        text[cursor++] = hex[file->uuid[i] >> 4];
        text[cursor++] = hex[file->uuid[i] & 15];
        if (i == 3 || i == 5 || i == 7 || i == 9)
            text[cursor++] = '-';
    }
    if (cursor >= capacity) return false;
    text[cursor] = '\0';
    memcpy(buffer, text, cursor + 1);
    if (length) *length = cursor;
    return true;
}

bool xx_macho_yx_uuid(const uint8_t *data, size_t size, char *buffer,
                    size_t capacity, size_t *length)
{
    xx_macho_yx_model model;
    if ((!data && size) || !buffer) return false;
    xx_macho_yx_parse(data, size, &model);
    return model.valid && model.has_top_file &&
           xx_macho_yx_format_uuid(&model.top_file, buffer, capacity,
                                 length);
}

bool xx_macho_yx_file_uuid(const uint8_t *data, size_t size, uint64_t index,
                         char *buffer, size_t capacity, size_t *length)
{
    xx_macho_yx_model model;
    xx_macho_yx_file child;
    if ((!data && size) || !buffer) return false;
    xx_macho_yx_parse(data, size, &model);
    return xx_macho_yx_child(&model, index, &child) &&
           xx_macho_yx_format_uuid(&child, buffer, capacity, length);
}

static bool xx_macho_yx_format_version(uint64_t encoded, char *buffer,
                                     size_t capacity, size_t *length)
{
    char text[32];
    int count;
    if (!buffer) return false;
    count = snprintf(text, sizeof(text), "%" PRIu64 ".%" PRIu64
                     ".%" PRIu64, encoded >> 16,
                     (encoded >> 8) & UINT64_C(0xff),
                     encoded & UINT64_C(0xff));
    if (count < 0 || (size_t)count >= sizeof(text) ||
        (size_t)count >= capacity) return false;
    memcpy(buffer, text, (size_t)count + 1);
    if (length) *length = (size_t)count;
    return true;
}

static bool xx_macho_yx_format_build_tool_version(uint64_t encoded,
                                                 char *buffer,
                                                 size_t capacity,
                                                 size_t *length)
{
    char text[16];
    int count;
    if (!buffer) return false;
    count = snprintf(text, sizeof(text), "%" PRIu64 ".%" PRIu64,
                     encoded >> 16, (encoded >> 8) & UINT64_C(0xff));
    if (count < 0 || (size_t)count >= sizeof(text) ||
        (size_t)count >= capacity) return false;
    memcpy(buffer, text, (size_t)count + 1);
    if (length) *length = (size_t)count;
    return true;
}

bool xx_macho_yx_text_field_supported(const char *member)
{
    return member && (strcmp(member, "source_version") == 0 ||
                      strcmp(member, "uuid") == 0 ||
                      strcmp(member, "build_version.minos") == 0 ||
                      strcmp(member, "build_version.sdk") == 0 ||
                      strcmp(member, "min_version.version") == 0 ||
                      strcmp(member, "min_version.sdk") == 0);
}

bool xx_macho_yx_text_field(const uint8_t *data, size_t size,
                          const char *member, char *buffer,
                          size_t capacity, size_t *length)
{
    xx_macho_yx_model model;
    const uint8_t *payload;
    size_t payload_size;
    uint32_t command;
    unsigned group;
    size_t offset;
    uint64_t encoded;
    if (!xx_macho_yx_text_field_supported(member) || !buffer ||
        (!data && size)) return false;
    if (strcmp(member, "source_version") == 0)
        return xx_macho_yx_source_version(data, size, buffer, capacity,
                                        length);
    if (strcmp(member, "uuid") == 0)
        return xx_macho_yx_uuid(data, size, buffer, capacity, length);
    xx_macho_yx_parse(data, size, &model);
    if (!model.valid || !model.has_top_file) return false;
    group = strncmp(member, "build_version.", 14) == 0 ?
            XX_MACHO_YX_OBJECT_BUILD_VERSION : XX_MACHO_YX_OBJECT_MIN_VERSION;
    offset = strcmp(member, "build_version.minos") == 0 ? 4u :
             strcmp(member, "build_version.sdk") == 0 ? 8u :
             strcmp(member, "min_version.version") == 0 ? 0u : 4u;
    if (!xx_macho_yx_object_payload(&model.top_file, group, &payload,
                                  &payload_size, &command) ||
        !xx_macho_yx_get(payload, payload_size, offset, 4,
                       model.top_file.little, &encoded)) return false;
    return xx_macho_yx_format_version(encoded, buffer, capacity, length);
}

bool xx_macho_yx_indexed_text_field_supported(const char *collection,
                                             const char *member)
{
    if (!collection || !member) return false;
    if (strcmp(collection, "build_version") == 0)
        return strcmp(member, "tools.version") == 0;
    if (strcmp(collection, "file") == 0)
        return xx_macho_yx_text_field_supported(member);
    return strcmp(collection, "dylibs") == 0 &&
           (strcmp(member, "current_version") == 0 ||
            strcmp(member, "compatibility_version") == 0);
}

bool xx_macho_yx_indexed_text_field(const uint8_t *data, size_t size,
                                   const char *collection, uint64_t index,
                                   const char *member, char *buffer,
                                   size_t capacity, size_t *length)
{
    xx_macho_yx_model model;
    const uint8_t *item;
    size_t item_length;
    uint64_t encoded;
    if (!xx_macho_yx_indexed_text_field_supported(collection, member) ||
        !buffer || (!data && size)) return false;
    xx_macho_yx_parse(data, size, &model);
    if (!model.valid) return false;
    if (strcmp(collection, "file") == 0) {
        xx_macho_yx_file child;
        return xx_macho_yx_child(&model, index, &child) &&
               xx_macho_yx_text_field(child.data, child.size, member,
                                    buffer, capacity, length);
    }
    if (strcmp(collection, "build_version") == 0) {
        uint64_t tool;
        return model.has_top_file &&
               xx_macho_yx_build_tool_item(&model.top_file, index, &tool,
                                         &encoded) &&
               xx_macho_yx_format_build_tool_version(encoded, buffer,
                                                    capacity, length);
    }
    if (!model.has_top_file ||
        !xx_macho_yx_command_item(&model.top_file, "dylibs", index,
                                 &item, &item_length) ||
        !xx_macho_yx_get(item, item_length,
                       strcmp(member, "current_version") == 0 ? 8u : 12u,
                       4, model.top_file.little, &encoded)) return false;
    return xx_macho_yx_format_version(encoded, buffer, capacity, length);
}

bool xx_macho_yx_file_dylib_version(const uint8_t *data, size_t size,
                                  uint64_t file_index,
                                  uint64_t dylib_index, const char *member,
                                  char *buffer, size_t capacity,
                                  size_t *length)
{
    xx_macho_yx_model model;
    xx_macho_yx_file child;
    if (!xx_macho_yx_indexed_text_field_supported("dylibs", member) ||
        !buffer || (!data && size)) return false;
    xx_macho_yx_parse(data, size, &model);
    return xx_macho_yx_child(&model, file_index, &child) &&
           xx_macho_yx_indexed_text_field(child.data, child.size, "dylibs",
                                        dylib_index, member, buffer,
                                        capacity, length);
}

bool xx_macho_yx_file_dylib_name(const uint8_t *data, size_t size,
                               uint64_t file_index, uint64_t dylib_index,
                               const uint8_t **bytes, size_t *length)
{
    xx_macho_yx_model model;
    xx_macho_yx_file child;
    if (!bytes || !length || (!data && size)) return false;
    xx_macho_yx_parse(data, size, &model);
    return xx_macho_yx_child(&model, file_index, &child) &&
           xx_macho_yx_indexed_string_field(child.data, child.size, "dylibs",
                                          dylib_index, "name", bytes, length);
}

bool xx_macho_yx_file_build_tool_version(const uint8_t *data, size_t size,
                                       uint64_t file_index,
                                       uint64_t tool_index, char *buffer,
                                       size_t capacity, size_t *length)
{
    xx_macho_yx_model model;
    xx_macho_yx_file child;
    if (!buffer || (!data && size)) return false;
    xx_macho_yx_parse(data, size, &model);
    return xx_macho_yx_child(&model, file_index, &child) &&
           xx_macho_yx_indexed_text_field(child.data, child.size,
                                        "build_version", tool_index,
                                        "tools.version", buffer,
                                        capacity, length);
}

bool xx_macho_yx_indexed_field_supported(const char *collection,
                                       const char *member)
{
    static const char *const segment_fields[] = {
        "vmaddr", "vmsize", "fileoff", "filesize", "maxprot", "initprot",
        "nsects", "flags", "sections.len"
    };
    if (!collection || !member) return false;
    if (strcmp(collection, "symtab") == 0) {
        static const char *const fields[] = {"nlists.n_strx", "nlists.n_type",
            "nlists.n_sect", "nlists.n_desc", "nlists.n_value"};
        for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i)
            if (strcmp(member, fields[i]) == 0) return true;
        return false;
    }
    if (strcmp(collection, "build_version") == 0)
        return strcmp(member, "tools.tool") == 0;
    if (strcmp(collection, "fat_arch") == 0)
        return xx_macho_yx_fat_arch_field_supported(member);
    if (strcmp(collection, "file") == 0)
        return xx_macho_yx_field_supported(member) &&
               strcmp(member, "fat_magic") != 0 &&
               strcmp(member, "nfat_arch") != 0;
    if (strcmp(collection, "dylibs") == 0)
        return strcmp(member, "timestamp") == 0;
    if (strcmp(collection, "segments") == 0) {
        for (size_t i = 0; i < sizeof(segment_fields) /
                                 sizeof(*segment_fields); ++i)
            if (strcmp(member, segment_fields[i]) == 0) return true;
    }
    return false;
}

bool xx_macho_yx_indexed_field(const uint8_t *data, size_t size,
                             const char *collection, uint64_t index,
                             const char *member, uint64_t *value)
{
    xx_macho_yx_model model;
    const uint8_t *item;
    size_t item_length;
    if (!xx_macho_yx_indexed_field_supported(collection, member) || !value ||
        (!data && size)) return false;
    if (strcmp(collection, "fat_arch") == 0)
        return xx_macho_yx_fat_arch_field(data, size, index, member, value);
    xx_macho_yx_parse(data, size, &model);
    if (!model.valid) return false;
    if (strcmp(collection, "symtab") == 0) {
        uint64_t count;
        size_t at = 0, offset, width = 1;
        if (!model.has_top_file ||
            !xx_macho_yx_symtab_entry(&model.top_file, index, NULL, NULL, &at, &count) ||
            index >= count) return false;
        if (strcmp(member, "nlists.n_strx") == 0) { offset = 0; width = 4; }
        else if (strcmp(member, "nlists.n_type") == 0) offset = 4;
        else if (strcmp(member, "nlists.n_sect") == 0) offset = 5;
        else if (strcmp(member, "nlists.n_desc") == 0) { offset = 6; width = 2; }
        else { offset = 8; width = model.top_file.is64 ? 8u : 4u; }
        return xx_macho_yx_get(model.top_file.data, model.top_file.size, at + offset,
                             width, model.top_file.little, value);
    }
    if (strcmp(collection, "file") == 0) {
        xx_macho_yx_file child;
        return xx_macho_yx_child(&model, index, &child) &&
               xx_macho_yx_field(child.data, child.size, member, value);
    }
    if (strcmp(collection, "build_version") == 0) {
        uint64_t version;
        return model.has_top_file &&
               xx_macho_yx_build_tool_item(&model.top_file, index, value,
                                         &version);
    }
    if (!model.has_top_file ||
        !xx_macho_yx_command_item(&model.top_file, collection, index,
                                 &item, &item_length)) return false;
    if (strcmp(collection, "dylibs") == 0)
        return xx_macho_yx_get(item, item_length, 4, 4,
                             model.top_file.little, value);
    if (strcmp(collection, "segments") == 0) {
        const xx_macho_yx_file *file = &model.top_file;
        size_t width = file->is64 ? 8u : 4u;
        size_t offset;
        if (strcmp(member, "vmaddr") == 0) offset = 16;
        else if (strcmp(member, "vmsize") == 0) offset = 16 + width;
        else if (strcmp(member, "fileoff") == 0) offset = 16 + width * 2;
        else if (strcmp(member, "filesize") == 0) offset = 16 + width * 3;
        else {
            width = 4;
            if (strcmp(member, "maxprot") == 0) offset = file->is64 ? 48 : 32;
            else if (strcmp(member, "initprot") == 0)
                offset = file->is64 ? 52 : 36;
            else if (strcmp(member, "nsects") == 0 ||
                     strcmp(member, "sections.len") == 0)
                offset = file->is64 ? 56 : 40;
            else offset = file->is64 ? 60 : 44;
        }
        return xx_macho_yx_get(item, item_length, offset, width,
                             file->little, value);
    }
    return false;
}

bool xx_macho_yx_file_build_tool_field(const uint8_t *data, size_t size,
                                     uint64_t file_index,
                                     uint64_t tool_index, const char *member,
                                     uint64_t *value)
{
    xx_macho_yx_model model;
    xx_macho_yx_file child;
    if (!member || strcmp(member, "tool") != 0 || !value ||
        (!data && size)) return false;
    xx_macho_yx_parse(data, size, &model);
    return xx_macho_yx_child(&model, file_index, &child) &&
           xx_macho_yx_indexed_field(child.data, child.size,
                                   "build_version", tool_index,
                                   "tools.tool", value);
}

bool xx_macho_yx_indexed_string_field_supported(const char *collection,
                                              const char *member)
{
    if (!collection || !member) return false;
    return (strcmp(collection, "symtab") == 0 &&
            strcmp(member, "entries.value") == 0) ||
           (strcmp(collection, "segments") == 0 &&
            strcmp(member, "segname") == 0) ||
           (strcmp(collection, "dylibs") == 0 &&
            strcmp(member, "name") == 0) ||
           (strcmp(collection, "file") == 0 &&
            xx_macho_yx_string_field_supported(member)) ||
           ((strcmp(collection, "rpaths") == 0 ||
             strcmp(collection, "linker_options") == 0) &&
            strcmp(member, "value") == 0);
}

bool xx_macho_yx_indexed_string_field(const uint8_t *data, size_t size,
                                    const char *collection, uint64_t index,
                                    const char *member,
                                    const uint8_t **bytes, size_t *length)
{
    xx_macho_yx_model model;
    const uint8_t *item;
    size_t item_length;
    if (!xx_macho_yx_indexed_string_field_supported(collection, member) ||
        !bytes || !length || (!data && size)) return false;
    xx_macho_yx_parse(data, size, &model);
    if (!model.valid) return false;
    if (strcmp(collection, "symtab") == 0) {
        uint64_t count;
        return model.has_top_file &&
               xx_macho_yx_symtab_entry(&model.top_file, index, bytes, length, NULL, &count) &&
               index < count;
    }
    if (strcmp(collection, "file") == 0) {
        xx_macho_yx_file child;
        return xx_macho_yx_child(&model, index, &child) &&
               xx_macho_yx_string_field(child.data, child.size, member,
                                       bytes, length);
    }
    if (!model.has_top_file ||
        !xx_macho_yx_command_item(&model.top_file, collection, index,
                                 &item, &item_length)) return false;
    if (strcmp(collection, "segments") == 0) {
        *bytes = item;
        *length = xx_macho_yx_trimmed_length(item, 16);
    } else if (strcmp(collection, "dylibs") == 0 ||
               strcmp(collection, "rpaths") == 0) {
        size_t offset = strcmp(collection, "dylibs") == 0 ? 16u : 4u;
        const uint8_t *end = memchr(item + offset, 0,
                                     item_length - offset);
        *bytes = item + offset;
        *length = end ? (size_t)(end - *bytes) : item_length - offset;
    } else {
        *bytes = item;
        *length = item_length;
    }
    return true;
}

bool xx_macho_yx_nested_collection_count(const uint8_t *data, size_t size,
                                        const char *collection,
                                        uint64_t index, const char *member,
                                        uint64_t *count)
{
    xx_macho_yx_model model;
    if (!collection || !member || !count || (!data && size)) return false;
    if (strcmp(collection, "segments") == 0 &&
        strcmp(member, "sections") == 0)
        return xx_macho_yx_indexed_field(data, size, collection, index,
                                       "sections.len", count);
    if (strcmp(collection, "file") != 0 ||
        !xx_macho_yx_collection_supported(member)) return false;
    xx_macho_yx_parse(data, size, &model);
    if (!model.valid) return false;
    {
        xx_macho_yx_file child;
        return xx_macho_yx_child(&model, index, &child) &&
               xx_macho_yx_collection_count(child.data, child.size,
                                          member, count);
    }
}

bool xx_macho_yx_section_field_supported(const char *member)
{
    static const char *const fields[] = {
        "addr", "size", "offset", "align", "reloff", "nreloc", "flags",
        "reserved1", "reserved2", "reserved3"
    };
    if (!member) return false;
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i)
        if (strcmp(member, fields[i]) == 0) return true;
    return false;
}

static bool xx_macho_yx_section_item(const uint8_t *data, size_t size,
                                   uint64_t segment_index,
                                   uint64_t section_index,
                                   xx_macho_yx_file *file,
                                   const uint8_t **section,
                                   size_t *section_length)
{
    xx_macho_yx_model model;
    const uint8_t *segment;
    size_t segment_length, prefix, width;
    uint32_t count;
    xx_macho_yx_parse(data, size, &model);
    if (!model.valid || !model.has_top_file ||
        !xx_macho_yx_command_item(&model.top_file, "segments", segment_index,
                                 &segment, &segment_length)) return false;
    *file = model.top_file;
    prefix = file->is64 ? 64u : 48u;
    width = file->is64 ? 80u : 68u;
    if (!xx_macho_yx_get32(segment, segment_length, prefix - 8,
                         file->little, &count) || section_index >= count)
        return false;
    *section = segment + prefix + (size_t)section_index * width;
    *section_length = width;
    return true;
}

bool xx_macho_yx_section_field(const uint8_t *data, size_t size,
                             uint64_t segment_index, uint64_t section_index,
                             const char *member, uint64_t *value)
{
    xx_macho_yx_file file;
    const uint8_t *section;
    size_t length, offset, width;
    if (!xx_macho_yx_section_field_supported(member) || !value ||
        (!data && size) ||
        !xx_macho_yx_section_item(data, size, segment_index, section_index,
                                &file, &section, &length)) return false;
    if (strcmp(member, "reserved3") == 0 && !file.is64) return false;
    width = 4;
    if (strcmp(member, "addr") == 0) {
        offset = 32;
        width = file.is64 ? 8u : 4u;
    } else if (strcmp(member, "size") == 0) {
        offset = file.is64 ? 40u : 36u;
        width = file.is64 ? 8u : 4u;
    } else if (strcmp(member, "offset") == 0)
        offset = file.is64 ? 48u : 40u;
    else if (strcmp(member, "align") == 0)
        offset = file.is64 ? 52u : 44u;
    else if (strcmp(member, "reloff") == 0)
        offset = file.is64 ? 56u : 48u;
    else if (strcmp(member, "nreloc") == 0)
        offset = file.is64 ? 60u : 52u;
    else if (strcmp(member, "flags") == 0)
        offset = file.is64 ? 64u : 56u;
    else if (strcmp(member, "reserved1") == 0)
        offset = file.is64 ? 68u : 60u;
    else if (strcmp(member, "reserved2") == 0)
        offset = file.is64 ? 72u : 64u;
    else offset = 76u;
    return xx_macho_yx_get(section, length, offset, width,
                         file.little, value);
}

bool xx_macho_yx_section_string_field_supported(const char *member)
{
    return member && (strcmp(member, "segname") == 0 ||
                      strcmp(member, "sectname") == 0);
}

bool xx_macho_yx_section_string_field(const uint8_t *data, size_t size,
                                    uint64_t segment_index,
                                    uint64_t section_index,
                                    const char *member,
                                    const uint8_t **bytes, size_t *length)
{
    xx_macho_yx_file file;
    const uint8_t *section;
    size_t section_length;
    if (!xx_macho_yx_section_string_field_supported(member) || !bytes ||
        !length || (!data && size) ||
        !xx_macho_yx_section_item(data, size, segment_index, section_index,
                                &file, &section, &section_length))
        return false;
    (void)file;
    (void)section_length;
    *bytes = section + (strcmp(member, "segname") == 0 ? 16u : 0u);
    *length = xx_macho_yx_trimmed_length(*bytes, 16);
    return true;
}

static uint8_t xx_macho_yx_ascii_lower(uint8_t character)
{
    return character >= 'A' && character <= 'Z' ?
        (uint8_t)(character + ('a' - 'A')) : character;
}

static bool xx_macho_yx_equal_ascii_case(const uint8_t *left,
                                       size_t left_length,
                                       const uint8_t *right,
                                       size_t right_length)
{
    if (left_length != right_length) return false;
    for (size_t i = 0; i < left_length; ++i)
        if (xx_macho_yx_ascii_lower(left[i]) !=
            xx_macho_yx_ascii_lower(right[i])) return false;
    return true;
}

static bool xx_macho_yx_file_has_name(const xx_macho_yx_file *file,
                                    const char *collection,
                                    const uint8_t *name,
                                    size_t name_length)
{
    uint64_t count = strcmp(collection, "dylibs") == 0 ?
        file->dylibs : file->rpaths;
    for (uint64_t i = 0; i < count; ++i) {
        const uint8_t *item, *member, *end;
        size_t length, prefix = strcmp(collection, "dylibs") == 0 ? 16u : 4u;
        if (!xx_macho_yx_command_item(file, collection, i, &item, &length))
            return false;
        member = item + prefix;
        end = memchr(member, 0, length - prefix);
        if (xx_macho_yx_equal_ascii_case(name, name_length, member,
                                      end ? (size_t)(end - member) :
                                            length - prefix)) return true;
    }
    return false;
}

static bool xx_macho_yx_has_name(const uint8_t *data, size_t size,
                               const char *collection,
                               const uint8_t *name, size_t name_length,
                               bool *found)
{
    xx_macho_yx_model model;
    if (!found || (!data && size) || (!name && name_length)) return false;
    *found = false;
    xx_macho_yx_parse(data, size, &model);
    if (!model.valid) return true;
    if (model.has_top_file) {
        *found = xx_macho_yx_file_has_name(&model.top_file, collection,
                                        name, name_length);
        return true;
    }
    for (uint64_t i = 0; i < model.file_count; ++i) {
        xx_macho_yx_file child;
        if (xx_macho_yx_child(&model, i, &child) &&
            xx_macho_yx_file_has_name(&child, collection, name, name_length)) {
            *found = true;
            break;
        }
    }
    return true;
}

bool xx_macho_yx_has_dylib(const uint8_t *data, size_t size,
                         const uint8_t *name, size_t name_length,
                         bool *found)
{
    return xx_macho_yx_has_name(data, size, "dylibs", name,
                              name_length, found);
}

bool xx_macho_yx_has_rpath(const uint8_t *data, size_t size,
                         const uint8_t *name, size_t name_length,
                         bool *found)
{
    return xx_macho_yx_has_name(data, size, "rpaths", name,
                              name_length, found);
}

bool xx_macho_yx_entry_point_for_arch(const uint8_t *data, size_t size,
                                    uint64_t cputype, bool has_subtype,
                                    uint64_t cpusubtype, uint64_t *offset)
{
    xx_macho_yx_model model;
    uint64_t index;
    uint32_t type, subtype, align, reserved;
    uint64_t file_offset, file_size;
    xx_macho_yx_file child;
    if (!offset || (!data && size) ||
        !xx_macho_yx_file_index_for_arch(data, size, cputype, has_subtype,
                                       cpusubtype, &index)) return false;
    xx_macho_yx_parse(data, size, &model);
    if (!xx_macho_yx_arch(&model, index, &type, &subtype, &file_offset,
                        &file_size, &align, &reserved) ||
        !xx_macho_yx_child(&model, index, &child) || !child.has_entry_point ||
        child.entry_point > UINT64_MAX - file_offset) return false;
    *offset = file_offset + child.entry_point;
    return true;
}

bool xx_macho_yx_constant(const char *member, uint64_t *value)
{
    static const struct { const char *name; uint64_t value; } constants[] = {
        {"MH_MAGIC", UINT64_C(0xfeedface)},
        {"MH_CIGAM", UINT64_C(0xcefaedfe)},
        {"MH_MAGIC_64", UINT64_C(0xfeedfacf)},
        {"MH_CIGAM_64", UINT64_C(0xcffaedfe)},
        {"FAT_MAGIC", UINT64_C(0xcafebabe)},
        {"FAT_CIGAM", UINT64_C(0xbebafeca)},
        {"FAT_MAGIC_64", UINT64_C(0xcafebabf)},
        {"FAT_CIGAM_64", UINT64_C(0xbfbafeca)},
        {"CPU_ARCH_ABI64", UINT64_C(0x1000000)},
        {"CPU_SUBTYPE_LIB64", UINT64_C(0x80000000)},
        {"CPU_TYPE_MC680X0", UINT64_C(0x6)},
        {"CPU_TYPE_X86", UINT64_C(0x7)},
        {"CPU_TYPE_X86_64", UINT64_C(0x1000007)},
        {"CPU_TYPE_MIPS", UINT64_C(0x8)},
        {"CPU_TYPE_MC98000", UINT64_C(0xa)},
        {"CPU_TYPE_ARM", UINT64_C(0xc)},
        {"CPU_TYPE_ARM64", UINT64_C(0x100000c)},
        {"CPU_TYPE_MC88000", UINT64_C(0xd)},
        {"CPU_TYPE_SPARC", UINT64_C(0xe)},
        {"CPU_TYPE_POWERPC", UINT64_C(0x12)},
        {"CPU_TYPE_POWERPC64", UINT64_C(0x1000012)},
        {"CPU_TYPE_I386", UINT64_C(0x7)},
        {"CPU_SUBTYPE_INTEL_MODEL_ALL", UINT64_C(0x0)},
        {"CPU_SUBTYPE_386", UINT64_C(0x3)},
        {"CPU_SUBTYPE_486", UINT64_C(0x4)},
        {"CPU_SUBTYPE_486SX", UINT64_C(0x84)},
        {"CPU_SUBTYPE_586", UINT64_C(0x5)},
        {"CPU_SUBTYPE_CELERON", UINT64_C(0x67)},
        {"CPU_SUBTYPE_CELERON_MOBILE", UINT64_C(0x77)},
        {"CPU_SUBTYPE_ITANIUM", UINT64_C(0xb)},
        {"CPU_SUBTYPE_ITANIUM_2", UINT64_C(0x1b)},
        {"CPU_SUBTYPE_XEON", UINT64_C(0xc)},
        {"CPU_SUBTYPE_XEON_MP", UINT64_C(0x1c)},
        {"CPU_SUBTYPE_I386_ALL", UINT64_C(0x3)},
        {"CPU_SUBTYPE_X86_64_ALL", UINT64_C(0x3)},
        {"CPU_SUBTYPE_PENT", UINT64_C(0x5)},
        {"CPU_SUBTYPE_PENTPRO", UINT64_C(0x16)},
        {"CPU_SUBTYPE_PENTII_M3", UINT64_C(0x36)},
        {"CPU_SUBTYPE_PENTII_M5", UINT64_C(0x56)},
        {"CPU_SUBTYPE_PENTIUM_3", UINT64_C(0x8)},
        {"CPU_SUBTYPE_PENTIUM_3_M", UINT64_C(0x18)},
        {"CPU_SUBTYPE_PENTIUM_3_XEON", UINT64_C(0x28)},
        {"CPU_SUBTYPE_PENTIUM_M", UINT64_C(0x9)},
        {"CPU_SUBTYPE_PENTIUM_4", UINT64_C(0xa)},
        {"CPU_SUBTYPE_PENTIUM_4_M", UINT64_C(0x1a)},
        {"CPU_SUBTYPE_ARM_ALL", UINT64_C(0x0)},
        {"CPU_SUBTYPE_ARM_V4T", UINT64_C(0x5)},
        {"CPU_SUBTYPE_ARM_V6", UINT64_C(0x6)},
        {"CPU_SUBTYPE_ARM_V5", UINT64_C(0x7)},
        {"CPU_SUBTYPE_ARM_XSCALE", UINT64_C(0x8)},
        {"CPU_SUBTYPE_ARM_V7", UINT64_C(0x9)},
        {"CPU_SUBTYPE_ARM_V7F", UINT64_C(0xa)},
        {"CPU_SUBTYPE_ARM_V7S", UINT64_C(0xb)},
        {"CPU_SUBTYPE_ARM_V7K", UINT64_C(0xc)},
        {"CPU_SUBTYPE_ARM_V6M", UINT64_C(0xe)},
        {"CPU_SUBTYPE_ARM_V7M", UINT64_C(0xf)},
        {"CPU_SUBTYPE_ARM_V7EM", UINT64_C(0x10)},
        {"CPU_SUBTYPE_ARM_V5TEJ", UINT64_C(0x7)},
        {"CPU_SUBTYPE_ARM64_ALL", UINT64_C(0x0)},
        {"CPU_SUBTYPE_SPARC_ALL", UINT64_C(0x0)},
        {"CPU_SUBTYPE_POWERPC_ALL", UINT64_C(0x0)},
        {"CPU_SUBTYPE_POWERPC_601", UINT64_C(0x1)},
        {"CPU_SUBTYPE_POWERPC_602", UINT64_C(0x2)},
        {"CPU_SUBTYPE_POWERPC_603", UINT64_C(0x3)},
        {"CPU_SUBTYPE_POWERPC_603e", UINT64_C(0x4)},
        {"CPU_SUBTYPE_POWERPC_603ev", UINT64_C(0x5)},
        {"CPU_SUBTYPE_POWERPC_604", UINT64_C(0x6)},
        {"CPU_SUBTYPE_POWERPC_604e", UINT64_C(0x7)},
        {"CPU_SUBTYPE_POWERPC_620", UINT64_C(0x8)},
        {"CPU_SUBTYPE_POWERPC_750", UINT64_C(0x9)},
        {"CPU_SUBTYPE_POWERPC_7400", UINT64_C(0xa)},
        {"CPU_SUBTYPE_POWERPC_7450", UINT64_C(0xb)},
        {"CPU_SUBTYPE_POWERPC_970", UINT64_C(0x64)},
        {"CPU_SUBTYPE_MC980000_ALL", UINT64_C(0x0)},
        {"CPU_SUBTYPE_MC98601", UINT64_C(0x1)},
        {"MH_OBJECT", UINT64_C(0x1)},
        {"MH_EXECUTE", UINT64_C(0x2)},
        {"MH_FVMLIB", UINT64_C(0x3)},
        {"MH_CORE", UINT64_C(0x4)},
        {"MH_PRELOAD", UINT64_C(0x5)},
        {"MH_DYLIB", UINT64_C(0x6)},
        {"MH_DYLINKER", UINT64_C(0x7)},
        {"MH_BUNDLE", UINT64_C(0x8)},
        {"MH_DYLIB_STUB", UINT64_C(0x9)},
        {"MH_DSYM", UINT64_C(0xa)},
        {"MH_KEXT_BUNDLE", UINT64_C(0xb)},
        {"MH_NOUNDEFS", UINT64_C(0x1)},
        {"MH_INCRLINK", UINT64_C(0x2)},
        {"MH_DYLDLINK", UINT64_C(0x4)},
        {"MH_BINDATLOAD", UINT64_C(0x8)},
        {"MH_PREBOUND", UINT64_C(0x10)},
        {"MH_SPLIT_SEGS", UINT64_C(0x20)},
        {"MH_LAZY_INIT", UINT64_C(0x40)},
        {"MH_TWOLEVEL", UINT64_C(0x80)},
        {"MH_FORCE_FLAT", UINT64_C(0x100)},
        {"MH_NOMULTIDEFS", UINT64_C(0x200)},
        {"MH_NOFIXPREBINDING", UINT64_C(0x400)},
        {"MH_PREBINDABLE", UINT64_C(0x800)},
        {"MH_ALLMODSBOUND", UINT64_C(0x1000)},
        {"MH_SUBSECTIONS_VIA_SYMBOLS", UINT64_C(0x2000)},
        {"MH_CANONICAL", UINT64_C(0x4000)},
        {"MH_WEAK_DEFINES", UINT64_C(0x8000)},
        {"MH_BINDS_TO_WEAK", UINT64_C(0x10000)},
        {"MH_ALLOW_STACK_EXECUTION", UINT64_C(0x20000)},
        {"MH_ROOT_SAFE", UINT64_C(0x40000)},
        {"MH_SETUID_SAFE", UINT64_C(0x80000)},
        {"MH_NO_REEXPORTED_DYLIBS", UINT64_C(0x100000)},
        {"MH_PIE", UINT64_C(0x200000)},
        {"MH_DEAD_STRIPPABLE_DYLIB", UINT64_C(0x400000)},
        {"MH_HAS_TLV_DESCRIPTORS", UINT64_C(0x800000)},
        {"MH_NO_HEAP_EXECUTION", UINT64_C(0x1000000)},
        {"MH_APP_EXTENSION_SAFE", UINT64_C(0x2000000)},
        {"SG_HIGHVM", UINT64_C(0x1)},
        {"SG_FVMLIB", UINT64_C(0x2)},
        {"SG_NORELOC", UINT64_C(0x4)},
        {"SG_PROTECTED_VERSION_1", UINT64_C(0x8)},
        {"SECTION_TYPE", UINT64_C(0xff)},
        {"SECTION_ATTRIBUTES", UINT64_C(0xffffff00)},
        {"S_REGULAR", UINT64_C(0x0)},
        {"S_ZEROFILL", UINT64_C(0x1)},
        {"S_CSTRING_LITERALS", UINT64_C(0x2)},
        {"S_4BYTE_LITERALS", UINT64_C(0x3)},
        {"S_8BYTE_LITERALS", UINT64_C(0x4)},
        {"S_LITERAL_POINTERS", UINT64_C(0x5)},
        {"S_NON_LAZY_SYMBOL_POINTERS", UINT64_C(0x6)},
        {"S_LAZY_SYMBOL_POINTERS", UINT64_C(0x7)},
        {"S_SYMBOL_STUBS", UINT64_C(0x8)},
        {"S_MOD_INIT_FUNC_POINTERS", UINT64_C(0x9)},
        {"S_MOD_TERM_FUNC_POINTERS", UINT64_C(0xa)},
        {"S_COALESCED", UINT64_C(0xb)},
        {"S_GB_ZEROFILL", UINT64_C(0xc)},
        {"S_INTERPOSING", UINT64_C(0xd)},
        {"S_16BYTE_LITERALS", UINT64_C(0xe)},
        {"S_DTRACE_DOF", UINT64_C(0xf)},
        {"S_LAZY_DYLIB_SYMBOL_POINTERS", UINT64_C(0x10)},
        {"S_THREAD_LOCAL_REGULAR", UINT64_C(0x11)},
        {"S_THREAD_LOCAL_ZEROFILL", UINT64_C(0x12)},
        {"S_THREAD_LOCAL_VARIABLES", UINT64_C(0x13)},
        {"S_THREAD_LOCAL_VARIABLE_POINTERS", UINT64_C(0x14)},
        {"S_THREAD_LOCAL_INIT_FUNCTION_POINTERS", UINT64_C(0x15)},
        {"S_ATTR_PURE_INSTRUCTIONS", UINT64_C(0x80000000)},
        {"S_ATTR_NO_TOC", UINT64_C(0x40000000)},
        {"S_ATTR_STRIP_STATIC_SYMS", UINT64_C(0x20000000)},
        {"S_ATTR_NO_DEAD_STRIP", UINT64_C(0x10000000)},
        {"S_ATTR_LIVE_SUPPORT", UINT64_C(0x8000000)},
        {"S_ATTR_SELF_MODIFYING_CODE", UINT64_C(0x4000000)},
        {"S_ATTR_DEBUG", UINT64_C(0x2000000)},
        {"S_ATTR_SOME_INSTRUCTIONS", UINT64_C(0x400)},
        {"S_ATTR_EXT_RELOC", UINT64_C(0x200)},
        {"S_ATTR_LOC_RELOC", UINT64_C(0x100)},
        {"MACOSX", UINT64_C(0x24)},
        {"IPHONEOS", UINT64_C(0x25)},
        {"TVOS", UINT64_C(0x2f)},
        {"WATCHOS", UINT64_C(0x30)},
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

void xx_macho_yx_extras_free(xx_macho_yx_extras *result)
{
    if (!result) return;
    for (size_t i = 0; i < result->entitlement_count; ++i)
        free(result->entitlements[i]);
    free(result->entitlements);
    for (size_t i = 0; i < result->certificate_count; ++i) {
        free(result->certificates[i].issuer);
        free(result->certificates[i].subject);
    }
    free(result->certificates);
    for (size_t i = 0; i < result->import_count; ++i)
        free(result->imports[i]);
    free(result->imports);
    for (size_t i = 0; i < result->export_count; ++i)
        free(result->exports[i]);
    free(result->exports);
    memset(result, 0, sizeof(*result));
}

static bool xx_macho_yx_copy_string(const char *text, size_t length,
                                  char **copy)
{
    if (length == SIZE_MAX) return false;
    *copy = malloc(length + 1);
    if (!*copy) return false;
    memcpy(*copy, text, length);
    (*copy)[length] = 0;
    return true;
}

static bool xx_macho_yx_append_string(char ***items, size_t *count,
                                    const char *text, size_t length)
{
    char **grown, *copy;
    if (*count >= SIZE_MAX / sizeof(**items) ||
        !xx_macho_yx_copy_string(text, length, &copy)) return false;
    grown = realloc(*items, (*count + 1) * sizeof(**items));
    if (!grown) {
        free(copy);
        return false;
    }
    *items = grown;
    (*items)[(*count)++] = copy;
    return true;
}

static bool xx_macho_yx_append_entitlement(xx_macho_yx_extras *result,
                                         const char *text)
{
    return xx_macho_yx_append_string(&result->entitlements,
                                   &result->entitlement_count,
                                   text, strlen(text));
}

static bool xx_macho_yx_xml_nonblank(const char *text)
{
    /* roxmltree's trim() suppresses whitespace-only array descendants. */
    while (*text) {
        if (*text != ' ' && *text != '\t' && *text != '\r' &&
            *text != '\n' && *text != '\v' && *text != '\f') return true;
        ++text;
    }
    return false;
}

typedef struct xx_macho_yx_xml_node {
    xx_xml_type_t type;
    bool key;
    bool array;
    char *text;
    struct xx_macho_yx_xml_node *children;
    struct xx_macho_yx_xml_node *last;
    struct xx_macho_yx_xml_node *next;
} xx_macho_yx_xml_node;

static void xx_macho_yx_xml_free(xx_macho_yx_xml_node *node)
{
    while (node) {
        xx_macho_yx_xml_node *next = node->next;
        xx_macho_yx_xml_free(node->children);
        free(node->text);
        free(node);
        node = next;
    }
}

static const char *xx_macho_yx_xml_text(const xx_macho_yx_xml_node *node)
{
    if (node->type == XX_XML_TEXT) return node->text;
    if (node->children && node->children->type == XX_XML_TEXT)
        return node->children->text;
    return NULL;
}

static bool xx_macho_yx_xml_array_entries(const xx_macho_yx_xml_node *node,
                                        xx_macho_yx_extras *result,
                                        size_t first)
{
    const char *text = xx_macho_yx_xml_text(node);
    if (text && xx_macho_yx_xml_nonblank(text)) {
        bool seen = false;
        for (size_t i = first; i < result->entitlement_count; ++i)
            if (strcmp(result->entitlements[i], text) == 0) {
                seen = true;
                break;
            }
        if (!seen && !xx_macho_yx_append_entitlement(result, text)) return false;
    }
    for (const xx_macho_yx_xml_node *child = node->children;
         child; child = child->next)
        if (!xx_macho_yx_xml_array_entries(child, result, first)) return false;
    return true;
}

static bool xx_macho_yx_xml_entries(const xx_macho_yx_xml_node *node,
                                  xx_macho_yx_extras *result)
{
    for (; node; node = node->next) {
        if (node->type == XX_XML_START && node->key) {
            const char *text = xx_macho_yx_xml_text(node);
            if (text && !xx_macho_yx_append_entitlement(result, text))
                return false;
        } else if (node->type == XX_XML_START && node->array &&
                   xx_macho_yx_xml_text(node)) {
            if (!xx_macho_yx_xml_array_entries(
                    node, result, result->entitlement_count)) return false;
        }
        if (node->children && !xx_macho_yx_xml_entries(node->children, result))
            return false;
    }
    return true;
}

static bool xx_macho_yx_xml_utf8(const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size;) {
        uint32_t point;
        size_t extra;
        uint8_t first = data[i++];
        if (first < 0x80) { point = first; extra = 0; }
        else if (first >= 0xc2 && first <= 0xdf) { point = first & 31; extra = 1; }
        else if (first >= 0xe0 && first <= 0xef) { point = first & 15; extra = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { point = first & 7; extra = 3; }
        else return false;
        if (extra > size - i) return false;
        for (size_t j = 0; j < extra; ++j) {
            if ((data[i] & 0xc0) != 0x80) return false;
            point = (point << 6) | (data[i++] & 63);
        }
        if ((extra == 1 && point < 0x80) ||
            (extra == 2 && point < 0x800) ||
            (extra == 3 && point < 0x10000) || point > 0x10ffff ||
            (point >= 0xd800 && point <= 0xdfff) ||
            point == 0xfffe || point == 0xffff ||
            (point < 0x20 && point != '\t' && point != '\n' && point != '\r'))
            return false;
    }
    return true;
}

static bool xx_macho_yx_parse_entitlements(const uint8_t *data, size_t size,
                                         xx_macho_yx_extras *result)
{
    xx_macho_yx_xml_node document = {0};
    xx_macho_yx_xml_node *parents[XX_XML_MAX_DEPTH + 1];
    size_t depth = 0, roots = 0;
    uint8_t *normalized = NULL;
    xx_xml xml;
    bool ok = true, malformed = false;
    if (size > INT_MAX) return true;
    /* Keep a small tree so array traversal retains its established ordering
     * and per-array duplicate suppression. The XML cursor owns decoding and
     * nesting checks, and never fetches external entities. */
    if (size >= 3 && memcmp(data, "\xef\xbb\xbf", 3) == 0) {
        data += 3;
        size -= 3;
    }
    if (!xx_macho_yx_xml_utf8(data, size)) return true;
    /* Normalize source line endings before entity decoding, so &#13; keeps
     * its literal carriage return as required by XML 1.0. */
    if (memchr(data, '\r', size)) {
        size_t written = 0;
        normalized = malloc(size);
        if (!normalized) return false;
        for (size_t i = 0; i < size; ++i) {
            if (data[i] == '\r') {
                normalized[written++] = '\n';
                if (i + 1 < size && data[i + 1] == '\n') ++i;
            } else normalized[written++] = data[i];
        }
        data = normalized;
        size = written;
    }
    parents[0] = &document;
    xx_xml_init(&xml, data, size);
    while (xx_xml_next(&xml)) {
        xx_macho_yx_xml_node *node, *parent;
        xx_xml_type_t type = xx_xml_type(&xml);
        const char *text = xx_xml_text(&xml);
        if (type == XX_XML_END) {
            if (!depth) { malformed = true; break; }
            --depth;
            continue;
        }
        if (type == XX_XML_START && !depth && ++roots > 1) {
            malformed = true;
            break;
        }
        if (type == XX_XML_TEXT && !depth &&
            text && xx_macho_yx_xml_nonblank(text)) {
            malformed = true;
            break;
        }
        if (type == XX_XML_DOCTYPE && (depth || roots)) {
            malformed = true;
            break;
        }
        node = calloc(1, sizeof(*node));
        if (!node) { ok = false; break; }
        node->type = type;
        if (type == XX_XML_START) {
            const char *name = xx_xml_name(&xml);
            const char *colon = strchr(name, ':');
            /* XML tree names are local names, independent of the prefix. */
            if (colon) name = colon + 1;
            node->key = strcmp(name, "key") == 0;
            node->array = strcmp(name, "array") == 0;
        } else if (type == XX_XML_TEXT && text) {
            size_t length = strlen(text);
            if (!xx_macho_yx_xml_utf8((const uint8_t *)text, length)) {
                free(node);
                malformed = true;
                break;
            }
            if (!xx_macho_yx_copy_string(text, length, &node->text)) {
                free(node);
                ok = false;
                break;
            }
        }
        parent = parents[depth];
        if (parent->last) parent->last->next = node;
        else parent->children = node;
        parent->last = node;
        if (type == XX_XML_START && !xml.self_closing) {
            if (depth >= XX_XML_MAX_DEPTH) { malformed = true; break; }
            parents[++depth] = node;
        }
    }
    malformed = malformed || xx_xml_failed(&xml) || depth != 0;
    xx_xml_cleanup(&xml);
    free(normalized);
    if (ok && !malformed && roots)
        ok = xx_macho_yx_xml_entries(document.children, result);
    xx_macho_yx_xml_free(document.children);
    return ok;
}

typedef struct {
    size_t content;
    size_t end;
    size_t next;
    int tag;
    int xclass;
} xx_macho_yx_ber_item;

static bool xx_macho_yx_ber_read(const uint8_t *data, size_t limit,
                              size_t offset, unsigned depth,
                              xx_macho_yx_ber_item *item);

/* x509-parser decodes BMPString code units as big-endian UCS-2, including
 * a final unpaired byte as a complete low-byte code unit. Its UCS-2 decoder
 * also accepts paired surrogates and rejects unpaired surrogates. */
static bool xx_macho_yx_bmp_text(const unsigned char *data, size_t size,
                                char **text, size_t *length)
{
    size_t used = 0;
    char *result;
    if (size > (SIZE_MAX - 4) / 3) return false;
    result = malloc(size * 3 + 4);
    if (!result) return false;
    for (size_t i = 0; i < size;) {
        uint32_t cp = data[i++];
        if (i < size) cp = (cp << 8) | data[i++];
        if (cp >= 0xd800 && cp <= 0xdbff) {
            uint32_t low;
            if (size - i < 2) { free(result); return false; }
            low = ((uint32_t)data[i] << 8) | data[i + 1];
            if (low < 0xdc00 || low > 0xdfff) { free(result); return false; }
            cp = 0x10000u + ((cp - 0xd800u) << 10) + low - 0xdc00u;
            i += 2;
        } else if (cp >= 0xdc00 && cp <= 0xdfff) {
            free(result);
            return false;
        }
        if (cp < 0x80) result[used++] = (char)cp;
        else if (cp < 0x800) {
            result[used++] = (char)(0xc0u | (cp >> 6));
            result[used++] = (char)(0x80u | (cp & 0x3fu));
        } else if (cp < 0x10000) {
            result[used++] = (char)(0xe0u | (cp >> 12));
            result[used++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
            result[used++] = (char)(0x80u | (cp & 0x3fu));
        } else {
            result[used++] = (char)(0xf0u | (cp >> 18));
            result[used++] = (char)(0x80u | ((cp >> 12) & 0x3fu));
            result[used++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
            result[used++] = (char)(0x80u | (cp & 0x3fu));
        }
    }
    result[used] = 0;
    *text = result;
    *length = used;
    return true;
}

static bool xx_macho_yx_x509_text_tag(int tag)
{
    return tag == 12 || tag == 18 || tag == 19 || tag == 20 ||
           tag == 21 || tag == 22 || tag == 25 || tag == 26 || tag == 27;
}

static bool xx_macho_yx_x509_hex(const unsigned char *data, size_t size,
                                char **text, size_t *length)
{
    static const char hex[] = "0123456789ABCDEF";
    char *result;
    if (size > (SIZE_MAX - 1) / 2) return false;
    result = malloc(size * 2 + 1);
    if (!result) return false;
    for (size_t i = 0; i < size; ++i) {
        result[i * 2] = hex[data[i] >> 4];
        result[i * 2 + 1] = hex[data[i] & 15];
    }
    result[size * 2] = 0;
    *text = result;
    *length = size * 2;
    return true;
}

static char *xx_macho_yx_invalid_name(void)
{
    static const char error_name[] = "<X509Error: Invalid X.509 name>";
    char *result = malloc(sizeof(error_name));
    if (result) memcpy(result, error_name, sizeof(error_name));
    return result;
}

static const char *xx_macho_yx_x509_label(const char *oid)
{
    static const struct { const char *oid, *label; } labels[] = {
        {"2.5.4.3", "CN"}, {"2.5.4.4", "SN"},
        {"2.5.4.5", "serialNumber"}, {"2.5.4.6", "C"},
        {"2.5.4.7", "L"}, {"2.5.4.8", "ST"},
        {"2.5.4.9", "street"}, {"2.5.4.10", "O"},
        {"2.5.4.11", "OU"}, {"2.5.4.12", "title"},
        {"2.5.4.13", "description"}, {"2.5.4.15", "businessCategory"},
        {"2.5.4.17", "postalCode"}, {"2.5.4.41", "name"},
        {"2.5.4.42", "GN"}, {"2.5.4.43", "initials"},
        {"2.5.4.44", "generationQualifier"}, {"2.5.4.46", "dnQualifier"},
        {"2.5.4.65", "pseudonym"},
        {"0.9.2342.19200300.100.1.1", "uid"},
        {"0.9.2342.19200300.100.1.25", "DC"},
        {"1.2.840.113549.1.9.1", "emailAddress"},
        {"1.3.6.1.4.1.311.60.2.1.1", "jurisdictionL"},
        {"1.3.6.1.4.1.311.60.2.1.2", "jurisdictionST"},
        {"1.3.6.1.4.1.311.60.2.1.3", "jurisdictionC"}
    };
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i)
        if (strcmp(oid, labels[i].oid) == 0) return labels[i].label;
    return "?";
}

/* X509Name Display validates raw string bytes as UTF-8 even for legacy
 * ASN.1 string tags. BMPString is decoded separately as big-endian UCS-2. */
static bool xx_macho_yx_x509_utf8_valid(const unsigned char *data, int length)
{
    int i = 0;
    if (length < 0) return false;
    while (i < length) {
        unsigned char c = data[i];
        int extra;
        uint32_t min, cp;
        if (c < 0x80) { ++i; continue; }
        else if ((c & 0xe0) == 0xc0) { extra = 1; min = 0x80; cp = c & 0x1f; }
        else if ((c & 0xf0) == 0xe0) { extra = 2; min = 0x800; cp = c & 0x0f; }
        else if ((c & 0xf8) == 0xf0) { extra = 3; min = 0x10000; cp = c & 0x07; }
        else return false;
        if (i + extra >= length) return false;
        for (int j = 1; j <= extra; ++j) {
            unsigned char cont = data[i + j];
            if ((cont & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (cont & 0x3f);
        }
        if (cp < min || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff)) return false;
        i += extra + 1;
    }
    return true;
}


static bool xx_macho_yx_append_certificate(xx_macho_yx_extras *result,
                                         char *issuer, size_t issuer_length,
                                         char *subject, size_t subject_length,
                                         bool self_signed)
{
    xx_macho_yx_certificate *grown;
    /* A Name rendering failure affects that Name alone, never the CMS
     * certificate list or the other Mach-O extras. */
    if (!issuer) {
        issuer = xx_macho_yx_invalid_name();
        issuer_length = issuer ? strlen(issuer) : 0;
    }
    if (!subject) {
        subject = xx_macho_yx_invalid_name();
        subject_length = subject ? strlen(subject) : 0;
    }
    if (!issuer || !subject ||
        result->certificate_count >= SIZE_MAX / sizeof(*grown)) {
        free(issuer);
        free(subject);
        return false;
    }
    grown = realloc(result->certificates,
                    (result->certificate_count + 1) * sizeof(*grown));
    if (!grown) {
        free(issuer);
        free(subject);
        return false;
    }
    result->certificates = grown;
    grown[result->certificate_count].issuer = issuer;
    grown[result->certificate_count].issuer_length = issuer_length;
    grown[result->certificate_count].subject = subject;
    grown[result->certificate_count].subject_length = subject_length;
    grown[result->certificate_count].is_self_signed = self_signed;
    ++result->certificate_count;
    return true;
}

/* CMS permits indefinite BER lengths even though embedded certificates use
 * DER. Keep all offsets inside the containing item and bound nesting. */
static bool xx_macho_yx_ber_read(const uint8_t *data, size_t limit,
                              size_t offset, unsigned depth,
                              xx_macho_yx_ber_item *item)
{
    xx_asn1_tlv tlv;
    size_t used;
    if (offset >= limit || depth >= 32 ||
        !xx_asn1_parse_tlv_ex(data + offset, limit - offset, depth,
                               &tlv, &used, NULL) || tlv.tag > INT_MAX)
        return false;
    item->content = offset + tlv.header_size;
    item->end = item->content + tlv.content_size;
    item->next = offset + used;
    item->tag = (int)tlv.tag;
    item->xclass = (int)tlv.tag_class << 6;
    return true;
}

static bool xx_macho_yx_ber_is(const xx_macho_yx_ber_item *item,
                            int xclass, int tag)
{
    return item->xclass == xclass && item->tag == tag;
}

static bool xx_macho_yx_ber_certificate_set(const uint8_t *data, size_t size,
                                          xx_macho_yx_ber_item *set)
{
    xx_macho_yx_ber_item outer, oid, tagged, signed_data, field;
    size_t at;
    if (!xx_macho_yx_ber_read(data, size, 0, 0, &outer) ||
        !xx_macho_yx_ber_is(&outer, V_ASN1_UNIVERSAL, V_ASN1_SEQUENCE) ||
        !xx_macho_yx_ber_read(data, outer.end, outer.content, 0, &oid) ||
        !xx_macho_yx_ber_is(&oid, V_ASN1_UNIVERSAL, V_ASN1_OBJECT) ||
        !xx_macho_yx_ber_read(data, outer.end, oid.next, 0, &tagged) ||
        !xx_macho_yx_ber_is(&tagged, V_ASN1_CONTEXT_SPECIFIC, 0) ||
        !xx_macho_yx_ber_read(data, tagged.end, tagged.content, 0, &signed_data) ||
        !xx_macho_yx_ber_is(&signed_data, V_ASN1_UNIVERSAL, V_ASN1_SEQUENCE))
        return false;
    /* macho::parse_certificates parses, but does not interpret, the outer
     * content OID and version. It does validate every digest AlgorithmId. */
    at = signed_data.content;
    if (!xx_macho_yx_ber_read(data, signed_data.end, at, 0, &field) ||
        !xx_macho_yx_ber_is(&field, V_ASN1_UNIVERSAL, V_ASN1_INTEGER)) return false;
    at = field.next;
    if (!xx_macho_yx_ber_read(data, signed_data.end, at, 0, &field) ||
        !xx_macho_yx_ber_is(&field, V_ASN1_UNIVERSAL, V_ASN1_SET)) return false;
    {
        xx_asn1_cursor algorithms;
        xx_asn1_tlv algorithm;
        xx_x509_algorithm_id id;
        if (!xx_asn1_cursor_init_span(&algorithms, data + field.content,
                field.end - field.content, 0)) return false;
        while (!xx_asn1_cursor_at_end(&algorithms))
            if (!xx_asn1_cursor_next(&algorithms, &algorithm) ||
                !xx_x509_parse_algorithm_identifier(&algorithm, &id)) return false;
    }
    at = field.next;
    if (!xx_macho_yx_ber_read(data, signed_data.end, at, 0, &field) ||
        !xx_macho_yx_ber_is(&field, V_ASN1_UNIVERSAL, V_ASN1_SEQUENCE)) return false;
    at = field.next;
    return xx_macho_yx_ber_read(data, signed_data.end, at, 0, set) &&
           xx_macho_yx_ber_is(set, V_ASN1_CONTEXT_SPECIFIC, 0);
}

/* Render the original X.509 value bytes using the upstream tag policy. */
static char *xx_macho_yx_ber_name(const uint8_t *data, size_t size,
                                 size_t *output_length)
{
    xx_macho_yx_ber_item sequence, set, attribute, oid, value;
    size_t rdn_at, used = 0, entries = 0;
    char *result = NULL, *bmp = NULL;
    if (!xx_macho_yx_ber_read(data, size, 0, 0, &sequence) ||
        sequence.tag != V_ASN1_SEQUENCE)
        return NULL;
    result = malloc(1);
    if (!result) return NULL;
    result[0] = 0;
    for (rdn_at = sequence.content; rdn_at < sequence.end; rdn_at = set.next) {
        size_t at;
        bool first = true;
        if (!xx_macho_yx_ber_read(data, sequence.end, rdn_at, 0, &set) ||
            set.tag != V_ASN1_SET) goto done;
        for (at = set.content; at < set.end; at = attribute.next) {
            const unsigned char *bytes;
            char oid_text[96];
            const char *label;
            size_t length, label_length, separator = entries ? (first ? 2 : 3) : 0;
            char *grown;
            if (!xx_macho_yx_ber_read(data, set.end, at, 0, &attribute) ||
                attribute.tag != V_ASN1_SEQUENCE ||
                !xx_macho_yx_ber_read(data, attribute.end, attribute.content, 0, &oid) ||
                (data[attribute.content] & 0x20) ||
                !xx_macho_yx_ber_read(data, attribute.end, oid.next, 0, &value) ||
                value.next != attribute.end)
                goto done;
            /* Attribute OIDs are read from primitive contents upstream;
             * their original identifier class/tag is not constrained. */
            if (!xx_asn1_oid_to_text(data + oid.content, oid.end - oid.content,
                    oid_text, sizeof(oid_text), NULL)) goto done;
            label = xx_macho_yx_x509_label(oid_text);
            label_length = strlen(label);
            bytes = data + value.content;
            length = value.end - value.content;
            if (value.tag == 30) {
                if (!xx_macho_yx_bmp_text(bytes, length, &bmp, &length)) {
                    goto fail;
                }
                bytes = (const unsigned char *)bmp;
            } else if (xx_macho_yx_x509_text_tag(value.tag)) {
                if (length > INT_MAX || !xx_macho_yx_x509_utf8_valid(bytes, (int)length)) {
                    goto fail;
                }
            } else {
                if (!xx_macho_yx_x509_hex(bytes, length, &bmp, &length)) {
                    goto fail;
                }
                bytes = (const unsigned char *)bmp;
            }
            if (used > SIZE_MAX - label_length - length - separator - 2) {
                goto fail;
            }
            grown = realloc(result, used + separator + label_length + length + 2);
            if (!grown) goto fail;
            result = grown;
            if (separator) {
                memcpy(result + used, first ? ", " : " + ", separator);
                used += separator;
            }
            memcpy(result + used, label, label_length);
            used += label_length;
            result[used++] = '=';
            memcpy(result + used, bytes, length);
            used += length;
            result[used] = 0;
            free(bmp);
            bmp = NULL;
            ++entries;
            first = false;
        }
    }
done:
    *output_length = used;
    return result;
fail:
    free(bmp);
    free(result);
    return NULL;
}

static bool xx_macho_yx_parse_certificates_ber(const uint8_t *data, size_t size,
                                             xx_macho_yx_extras *result)
{
    xx_macho_yx_ber_item set, item;
    size_t at;
    if (!xx_macho_yx_ber_certificate_set(data, size, &set)) return true;
    at = set.content;
    while (at < set.end) {
        xx_x509_certificate cert;
        char *issuer, *subject;
        size_t issuer_length = 0, subject_length = 0;
        bool self_signed;
        if (!xx_macho_yx_ber_read(data, set.end, at, 0, &item)) break;
        if (!xx_macho_yx_ber_is(&item, V_ASN1_UNIVERSAL,
                              V_ASN1_SEQUENCE)) {
            break;
        }
        /* Like SignedData::parse_certificates upstream, retain only the
         * valid X.509 prefix of the wire-ordered CertificateSet. Names are
         * rendered independently; their invalid string values do not reject
         * an otherwise parseable certificate. No Mach-O signature is verified. */
        if (!xx_x509_parse_certificate(data + at, item.next - at, &cert)) break;
        issuer = xx_macho_yx_ber_name(cert.issuer.der, cert.issuer.der_size,
                                    &issuer_length);
        subject = xx_macho_yx_ber_name(cert.subject.der, cert.subject.der_size,
                                     &subject_length);
        self_signed = cert.issuer.der_size == cert.subject.der_size &&
            memcmp(cert.issuer.der, cert.subject.der,
                   cert.issuer.der_size) == 0;
        if (!xx_macho_yx_append_certificate(result, issuer, issuer_length,
                                          subject, subject_length,
                                          self_signed)) return false;
        at = item.next;
    }
    return true;
}

static bool xx_macho_yx_parse_certificates(const uint8_t *data, size_t size,
                                        xx_macho_yx_extras *result)
{
    /* Keep certificate wire order and accept the BER outer CMS wrappers. */
    return xx_macho_yx_parse_certificates_ber(data, size, result);
}

static bool xx_macho_yx_parse_superblob(const xx_macho_yx_file *file,
                                      xx_macho_yx_extras *result)
{
    const uint8_t *payload;
    uint32_t command, dataoff, datasize, count;
    size_t payload_size, size;
    const uint8_t *data;
    if (!xx_macho_yx_object_payload(file, XX_MACHO_YX_OBJECT_CODE_SIGNATURE,
                                  &payload, &payload_size, &command) ||
        !xx_macho_yx_get32(payload, payload_size, 0, file->little, &dataoff) ||
        !xx_macho_yx_get32(payload, payload_size, 4, file->little, &datasize) ||
        dataoff > file->size || datasize > file->size - (size_t)dataoff)
        return true;
    data = file->data + (size_t)dataoff;
    size = (size_t)datasize;
    if (!xx_macho_yx_get32(data, size, 8, false, &count) ||
        count > (size - 12u) / 8u) return true;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t offset, magic, length;
        if (!xx_macho_yx_get32(data, size, 12u + (size_t)i * 8u + 4u,
                             false, &offset) ||
            offset > size || size - (size_t)offset < 8u ||
            !xx_macho_yx_get32(data, size, (size_t)offset, false, &magic) ||
            !xx_macho_yx_get32(data, size, (size_t)offset + 4u,
                             false, &length) ||
            length < 8u || length > size - (size_t)offset)
            continue;
        if (magic == UINT32_C(0xfade7171)) {
            if (!xx_macho_yx_parse_entitlements(data + (size_t)offset + 8u,
                    (size_t)length - 8u, result)) return false;
        } else if (magic == UINT32_C(0xfade0b01)) {
            if (!xx_macho_yx_parse_certificates(data + (size_t)offset + 8u,
                    (size_t)length - 8u, result)) return false;
        }
    }
    return true;
}

static bool xx_macho_yx_valid_utf8(const uint8_t *data, size_t size)
{
    size_t i = 0;
    while (i < size) {
        uint8_t first = data[i];
        unsigned extra;
        uint32_t code;
        if (first < 0x80) { ++i; continue; }
        if (first >= 0xc2 && first <= 0xdf) {
            extra = 1; code = first & 0x1fu;
        } else if (first >= 0xe0 && first <= 0xef) {
            extra = 2; code = first & 0x0fu;
        } else if (first >= 0xf0 && first <= 0xf4) {
            extra = 3; code = first & 0x07u;
        } else return false;
        if (extra > size - i - 1) return false;
        for (unsigned j = 1; j <= extra; ++j) {
            uint8_t next = data[i + j];
            if ((next & 0xc0u) != 0x80u) return false;
            code = (code << 6) | (next & 0x3fu);
        }
        if ((extra == 2 && (code < 0x800u ||
                            (code >= 0xd800u && code <= 0xdfffu))) ||
            (extra == 3 && (code < 0x10000u || code > 0x10ffffu)))
            return false;
        i += extra + 1;
    }
    return true;
}

static bool xx_macho_yx_skip_leb(const uint8_t *data, size_t size,
                               size_t *cursor)
{
    for (unsigned i = 0; i < 10 && *cursor < size; ++i) {
        if ((data[(*cursor)++] & 0x80u) == 0) return true;
    }
    return false;
}

static bool xx_macho_yx_parse_bind_stream(const uint8_t *data, size_t size,
                                        xx_macho_yx_extras *result)
{
    size_t cursor = 0;
    while (cursor < size) {
        uint8_t opcode = data[cursor++] & 0xf0u;
        if (opcode == 0x40u) {
            const uint8_t *end = memchr(data + cursor, 0, size - cursor);
            size_t length;
            bool seen = false;
            if (!end) break;
            length = (size_t)(end - (data + cursor));
            if (xx_macho_yx_valid_utf8(data + cursor, length)) {
                for (size_t i = 0; i < result->import_count; ++i)
                    if (strlen(result->imports[i]) == length &&
                        memcmp(result->imports[i], data + cursor,
                               length) == 0) {
                        seen = true;
                        break;
                    }
                if (!seen && !xx_macho_yx_append_string(
                        &result->imports, &result->import_count,
                        (const char *)data + cursor, length)) return false;
            }
            cursor += length + 1;
        } else if (opcode == 0x20u || opcode == 0x60u ||
                   opcode == 0x70u || opcode == 0x80u || opcode == 0xa0u) {
            if (!xx_macho_yx_skip_leb(data, size, &cursor)) break;
        } else if (opcode == 0xc0u) {
            if (!xx_macho_yx_skip_leb(data, size, &cursor) ||
                !xx_macho_yx_skip_leb(data, size, &cursor)) break;
        }
    }
    return true;
}

static bool xx_macho_yx_parse_dyld_imports(const xx_macho_yx_file *file,
                                         xx_macho_yx_extras *result)
{
    const uint8_t *payload;
    size_t payload_size;
    uint32_t command;
    static const size_t fields[] = {8u, 24u, 16u};
    if (!xx_macho_yx_object_payload(file, XX_MACHO_YX_OBJECT_DYLD_INFO, &payload,
                                  &payload_size, &command)) return true;
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i) {
        uint32_t offset, size;
        if (!xx_macho_yx_get32(payload, payload_size, fields[i],
                             file->little, &offset) ||
            !xx_macho_yx_get32(payload, payload_size, fields[i] + 4u,
                             file->little, &size) ||
            offset > file->size || size > file->size - (size_t)offset)
            continue;
        if (!xx_macho_yx_parse_bind_stream(file->data + (size_t)offset,
                                        (size_t)size, result)) return false;
    }
    return true;
}

static bool xx_macho_yx_parse_chained_imports(const xx_macho_yx_file *file,
                                            xx_macho_yx_extras *result)
{
    const uint8_t *payload, *data;
    size_t payload_size, size, entry_size;
    uint32_t command, dataoff, datasize, imports_offset, symbols_offset;
    uint32_t imports_count, imports_format;
    if (!xx_macho_yx_object_payload(file, XX_MACHO_YX_OBJECT_CHAINED_FIXUPS,
                                  &payload, &payload_size, &command) ||
        !xx_macho_yx_get32(payload, payload_size, 0, file->little, &dataoff) ||
        !xx_macho_yx_get32(payload, payload_size, 4, file->little, &datasize) ||
        dataoff > file->size || datasize > file->size - (size_t)dataoff)
        return true;
    data = file->data + (size_t)dataoff;
    size = (size_t)datasize;
    if (!xx_macho_yx_get32(data, size, 8, file->little, &imports_offset) ||
        !xx_macho_yx_get32(data, size, 12, file->little, &symbols_offset) ||
        !xx_macho_yx_get32(data, size, 16, file->little, &imports_count) ||
        !xx_macho_yx_get32(data, size, 20, file->little, &imports_format))
        return true;
    entry_size = imports_format == 2 ? 8u : imports_format == 3 ? 16u : 4u;
    if (imports_offset > size ||
        imports_count > (size - (size_t)imports_offset) / entry_size)
        return true;
    for (uint32_t i = 0; i < imports_count; ++i) {
        uint64_t entry, name_offset, name_at64;
        size_t at = (size_t)imports_offset + (size_t)i * entry_size;
        size_t name_at, length;
        const uint8_t *end;
        if (!xx_macho_yx_get(data, size, at,
                           imports_format == 3 ? 8u : 4u,
                           file->little, &entry)) break;
        name_offset = imports_format == 3 ? entry >> 32 :
                      (entry >> 9) & UINT64_C(0x7fffff);
        name_at64 = (uint64_t)symbols_offset + name_offset;
        name_at = (size_t)(name_at64 > UINT32_MAX ? UINT32_MAX : name_at64);
        if (name_at > size) continue;
        end = memchr(data + name_at, 0, size - name_at);
        if (!end) break;
        length = (size_t)(end - (data + name_at));
        if (xx_macho_yx_valid_utf8(data + name_at, length) &&
            !xx_macho_yx_append_string(&result->imports, &result->import_count,
                                    (const char *)data + name_at, length))
            return false;
    }
    return true;
}

static size_t xx_macho_yx_utf8_span(const uint8_t *bytes, size_t available,
                                 bool *valid)
{
    uint8_t first = bytes[0];
    unsigned expected;
    uint32_t code;
    *valid = true;
    if (first < 0x80u) return 1;
    if (first >= 0xc2u && first <= 0xdfu) {
        expected = 2; code = first & 0x1fu;
    } else if (first >= 0xe0u && first <= 0xefu) {
        expected = 3; code = first & 0x0fu;
    } else if (first >= 0xf0u && first <= 0xf4u) {
        expected = 4; code = first & 0x07u;
    } else {
        *valid = false;
        return 1;
    }
    for (unsigned i = 1; i < expected; ++i) {
        uint8_t next;
        if (i >= available) {
            *valid = false;
            return available; /* one replacement for a truncated prefix */
        }
        next = bytes[i];
        if ((next & 0xc0u) != 0x80u ||
            (i == 1 && ((first == 0xe0u && next < 0xa0u) ||
                        (first == 0xedu && next > 0x9fu) ||
                        (first == 0xf0u && next < 0x90u) ||
                        (first == 0xf4u && next > 0x8fu)))) {
            *valid = false;
            return i;
        }
        code = (code << 6) | (next & 0x3fu);
    }
    (void)code;
    return expected;
}

static bool xx_macho_yx_append_lossy_string(char ***items, size_t *count,
                                          const uint8_t *bytes, size_t length)
{
    static const char replacement[] = "\xef\xbf\xbd";
    char *converted;
    size_t source = 0, output = 0;
    bool appended;
    if (length > (SIZE_MAX - 1u) / 3u) return false;
    converted = malloc(length * 3u + 1u);
    if (!converted) return false;
    while (source < length) {
        bool valid;
        size_t span = xx_macho_yx_utf8_span(bytes + source,
                                         length - source, &valid);
        if (valid) {
            memcpy(converted + output, bytes + source, span);
            output += span;
        } else {
            memcpy(converted + output, replacement, 3);
            output += 3;
        }
        source += span;
    }
    appended = xx_macho_yx_append_string(items, count, converted, output);
    free(converted);
    return appended;
}

typedef struct {
    size_t offset;
    char *prefix;
} xx_macho_yx_export_node;

static bool xx_macho_yx_export_uleb(const uint8_t *data, size_t size,
                                  size_t *at, uint64_t *value)
{
    uint64_t number = 0;
    for (unsigned i = 0; i < 10 && *at < size; ++i) {
        uint8_t byte = data[(*at)++];
        if (i == 9 && (byte & 0xfeu)) return false;
        number |= (uint64_t)(byte & 0x7fu) << (i * 7);
        if (!(byte & 0x80u)) {
            *value = number;
            return true;
        }
    }
    return false;
}

/* Export-trie children are visited in reverse edge order, as in the
 * reference parser. A malformed branch keeps exports from earlier nodes. */
static bool xx_macho_yx_parse_export_trie(const uint8_t *data, size_t size,
                                        xx_macho_yx_extras *result)
{
    xx_macho_yx_export_node *stack = NULL;
    uint8_t *visited = NULL;
    size_t count = 0, capacity = 0;
    bool ok = true;
    if (size == 0) return true;
    visited = calloc(size, 1);
    stack = malloc(sizeof(*stack));
    if (!visited || !stack) { ok = false; goto done; }
    stack[0].prefix = malloc(1);
    if (!stack[0].prefix) { ok = false; goto done; }
    stack[0].prefix[0] = 0;
    stack[0].offset = 0;
    count = capacity = 1;
    while (count) {
        xx_macho_yx_export_node node = stack[--count];
        size_t at = node.offset;
        uint64_t terminal_size, flags, ignored;
        bool malformed = false;
        if (at >= size || visited[at]) { free(node.prefix); continue; }
        visited[at] = 1;
        if (!xx_macho_yx_export_uleb(data, size, &at, &terminal_size)) {
            free(node.prefix); break;
        }
        if (terminal_size) {
            if (!xx_macho_yx_export_uleb(data, size, &at, &flags)) {
                free(node.prefix); break;
            }
            if (flags == 0x10u) {
                malformed = !xx_macho_yx_export_uleb(data, size, &at, &ignored) ||
                            !xx_macho_yx_export_uleb(data, size, &at, &ignored);
            } else if (flags == 0x08u) {
                const uint8_t *end;
                malformed = !xx_macho_yx_export_uleb(data, size, &at, &ignored);
                end = malformed || at >= size ? NULL :
                      memchr(data + at, 0, size - at);
                if (!end) malformed = true;
                else at = (size_t)(end - data) + 1u;
            } else {
                malformed = !xx_macho_yx_export_uleb(data, size, &at, &ignored);
            }
        }
        if (malformed || at >= size) { free(node.prefix); break; }
        unsigned edges = data[at++];
        for (unsigned i = 0; i < edges; ++i) {
            const uint8_t *end;
            uint64_t child;
            size_t edge_length, prefix_length;
            char *prefix;
            if (at >= size || !(end = memchr(data + at, 0, size - at))) {
                malformed = true; break;
            }
            edge_length = (size_t)(end - (data + at));
            prefix_length = strlen(node.prefix);
            if (edge_length > SIZE_MAX - prefix_length - 1u) {
                ok = false; malformed = true; break;
            }
            at = (size_t)(end - data) + 1u;
            if (!xx_macho_yx_export_uleb(data, size, &at, &child)) {
                malformed = true; break;
            }
            if (child >= size || !xx_macho_yx_valid_utf8(end - edge_length,
                                                         edge_length))
                continue;
            prefix = malloc(prefix_length + edge_length + 1u);
            if (!prefix) { ok = false; malformed = true; break; }
            memcpy(prefix, node.prefix, prefix_length);
            memcpy(prefix + prefix_length, end - edge_length, edge_length);
            prefix[prefix_length + edge_length] = 0;
            if (count == capacity) {
                size_t grown_capacity = capacity <= SIZE_MAX / 2u ?
                                        capacity * 2u : SIZE_MAX;
                xx_macho_yx_export_node *grown;
                if (grown_capacity <= capacity ||
                    grown_capacity > SIZE_MAX / sizeof(*stack)) {
                    free(prefix); ok = false; malformed = true; break;
                }
                grown = realloc(stack, grown_capacity * sizeof(*stack));
                if (!grown) {
                    free(prefix); ok = false; malformed = true; break;
                }
                stack = grown;
                capacity = grown_capacity;
            }
            stack[count++] = (xx_macho_yx_export_node){(size_t)child, prefix};
        }
        if (!malformed && terminal_size &&
            !xx_macho_yx_append_string(&result->exports,
                                     &result->export_count,
                                     node.prefix, strlen(node.prefix))) {
            ok = false;
            malformed = true;
        }
        free(node.prefix);
        if (malformed) break;
    }
done:
    for (size_t i = 0; i < count; ++i) free(stack[i].prefix);
    free(stack);
    free(visited);
    return ok;
}

static bool xx_macho_yx_parse_exports(const xx_macho_yx_file *file,
                                    xx_macho_yx_extras *result)
{
    const uint8_t *payload;
    size_t payload_size;
    uint32_t command, offset, length;
    bool trie = xx_macho_yx_object_payload(file, XX_MACHO_YX_OBJECT_EXPORT_TRIE,
                                         &payload, &payload_size, &command);
    if (trie &&
        xx_macho_yx_get32(payload, payload_size, 0, file->little, &offset) &&
        xx_macho_yx_get32(payload, payload_size, 4, file->little, &length) &&
        offset <= file->size && length <= file->size - (size_t)offset &&
        !xx_macho_yx_parse_export_trie(file->data + (size_t)offset,
                                     (size_t)length, result)) return false;
    bool dyld = xx_macho_yx_object_payload(file, XX_MACHO_YX_OBJECT_DYLD_INFO,
                                         &payload, &payload_size, &command);
    if (dyld &&
        xx_macho_yx_get32(payload, payload_size, 32, file->little, &offset) &&
        xx_macho_yx_get32(payload, payload_size, 36, file->little, &length) &&
        offset <= file->size && length <= file->size - (size_t)offset &&
        !xx_macho_yx_parse_export_trie(file->data + (size_t)offset,
                                     (size_t)length, result)) return false;
    if (trie || dyld) return true;

    if (!xx_macho_yx_object_payload(file, XX_MACHO_YX_OBJECT_SYMTAB,
                                  &payload, &payload_size, &command))
        return true;
    uint32_t symoff, nsyms, stroff, strsize;
    size_t item_size = file->is64 ? 16u : 12u;
    if (!xx_macho_yx_get32(payload, payload_size, 0, file->little, &symoff) ||
        !xx_macho_yx_get32(payload, payload_size, 4, file->little, &nsyms) ||
        !xx_macho_yx_get32(payload, payload_size, 8, file->little, &stroff) ||
        !xx_macho_yx_get32(payload, payload_size, 12, file->little, &strsize) ||
        symoff > file->size || stroff > file->size ||
        strsize > file->size - (size_t)stroff) return true;
    for (uint32_t i = 0; i < nsyms; ++i) {
        size_t at = (size_t)symoff + (size_t)i * item_size;
        uint32_t strx;
        uint8_t type;
        const uint8_t *name, *end;
        if ((size_t)i >= (file->size - (size_t)symoff) / item_size ||
            !xx_macho_yx_get32(file->data, file->size, at,
                             file->little, &strx)) break;
        type = file->data[at + 4u];
        if (strx >= strsize) continue;
        name = file->data + (size_t)stroff + (size_t)strx;
        end = memchr(name, 0, (size_t)strsize - (size_t)strx);
        if (!end) break;
        if (end == name || !(type & 1u) || (type & 0xe0u) ||
            ((type & 0x0eu) != 0x0eu && (type & 0x0eu) != 0x02u &&
             (type & 0x0eu) != 0x0au)) continue;
        if (!xx_macho_yx_append_lossy_string(&result->exports,
                                           &result->export_count, name,
                                           (size_t)(end - name))) return false;
    }
    return true;
}

static int xx_macho_yx_compare_symbol_names(const void *left,
                                           const void *right)
{
    return strcmp(*(char *const *)left, *(char *const *)right);
}

static bool xx_macho_yx_symhash_file_input(const xx_macho_yx_file *file,
                                         uint8_t **bytes, size_t *length)
{
    const uint8_t *payload;
    size_t payload_size, count = 0, total = 0, used = 0;
    uint32_t command, symoff, nsyms, stroff, strsize;
    size_t item_size = file->is64 ? 16u : 12u;
    char **names = NULL, **display = NULL, *joined = NULL;
    size_t display_count = 0;
    bool any_entry = false, ok = false;
    if (!xx_macho_yx_object_payload(file, XX_MACHO_YX_OBJECT_SYMTAB,
                                  &payload, &payload_size, &command) ||
        !xx_macho_yx_get32(payload, payload_size, 0, file->little, &symoff) ||
        !xx_macho_yx_get32(payload, payload_size, 4, file->little, &nsyms) ||
        !xx_macho_yx_get32(payload, payload_size, 8, file->little, &stroff) ||
        !xx_macho_yx_get32(payload, payload_size, 12, file->little, &strsize) ||
        symoff > file->size || stroff > file->size ||
        strsize > file->size - (size_t)stroff) return false;
    for (uint32_t i = 0; i < nsyms; ++i) {
        size_t at;
        uint32_t strx;
        uint8_t type;
        const uint8_t *name, *end;
        char *trimmed;
        size_t trimmed_length;
        if ((size_t)i >= (file->size - (size_t)symoff) / item_size) break;
        at = (size_t)symoff + (size_t)i * item_size;
        if (!xx_macho_yx_get32(file->data, file->size, at,
                             file->little, &strx)) break;
        type = file->data[at + 4u];
        if (strx > strsize) continue;
        if (strx == strsize) break;
        name = file->data + (size_t)stroff + (size_t)strx;
        end = memchr(name, 0, (size_t)strsize - (size_t)strx);
        if (!end) break;
        if (end == name) continue;
        any_entry = true;
        if ((type & 0xe0u) || !(type & 1u) || (type & 0x0eu)) continue;
        if (!xx_string_trim_whitespace_alloc(name, (size_t)(end - name),
                                          &trimmed, &trimmed_length)) goto done;
        if (!xx_macho_yx_append_string(&names, &count, trimmed,
                                    trimmed_length)) {
            free(trimmed);
            goto done;
        }
        free(trimmed);
    }
    if (!any_entry) goto done;
    if (count)
        qsort(names, count, sizeof(*names), xx_macho_yx_compare_symbol_names);
    for (size_t i = 0; i < count; ++i) {
        if (i && strcmp(names[i], names[i - 1]) == 0) continue;
        if (!xx_macho_yx_append_lossy_string(&display, &display_count,
                                          (const uint8_t *)names[i],
                                          strlen(names[i]))) goto done;
    }
    for (size_t i = 0; i < display_count; ++i) {
        size_t name_length = strlen(display[i]);
        if (name_length > SIZE_MAX - (i != 0) ||
            name_length + (i != 0) > SIZE_MAX - total) goto done;
        total += name_length + (i != 0);
    }
    joined = malloc(total ? total : 1u);
    if (!joined) goto done;
    used = 0;
    for (size_t i = 0; i < display_count; ++i) {
        size_t name_length;
        if (i) joined[used++] = ',';
        name_length = strlen(display[i]);
        memcpy(joined + used, display[i], name_length);
        used += name_length;
    }
    *bytes = (uint8_t *)joined;
    *length = total;
    joined = NULL;
    ok = true;
done:
    for (size_t i = 0; i < count; ++i) free(names[i]);
    free(names);
    for (size_t i = 0; i < display_count; ++i) free(display[i]);
    free(display);
    free(joined);
    return ok;
}

bool xx_macho_yx_symhash_input(const uint8_t *data, size_t size,
                            uint8_t **bytes, size_t *length)
{
    xx_macho_yx_model model;
    xx_macho_yx_file child;
    if (!bytes || !length || (!data && size)) return false;
    *bytes = NULL;
    *length = 0;
    xx_macho_yx_parse(data, size, &model);
    if (!model.valid) return false;
    if (model.has_top_file)
        return xx_macho_yx_symhash_file_input(&model.top_file, bytes, length);
    if (!xx_macho_yx_child(&model, 0, &child)) return false;
    return xx_macho_yx_symhash_file_input(&child, bytes, length);
}

static bool xx_macho_yx_parse_symtab_imports(const xx_macho_yx_file *file,
                                           xx_macho_yx_extras *result)
{
    const uint8_t *payload, *ignored;
    size_t payload_size, ignored_size;
    uint32_t command, symoff, nsyms, stroff, strsize;
    size_t item_size = file->is64 ? 16u : 12u;
    if (xx_macho_yx_object_payload(file, XX_MACHO_YX_OBJECT_DYLD_INFO, &ignored,
                                  &ignored_size, &command) ||
        xx_macho_yx_object_payload(file, XX_MACHO_YX_OBJECT_CHAINED_FIXUPS,
                                  &ignored, &ignored_size, &command) ||
        !xx_macho_yx_object_payload(file, XX_MACHO_YX_OBJECT_SYMTAB, &payload,
                                  &payload_size, &command) ||
        !xx_macho_yx_get32(payload, payload_size, 0, file->little, &symoff) ||
        !xx_macho_yx_get32(payload, payload_size, 4, file->little, &nsyms) ||
        !xx_macho_yx_get32(payload, payload_size, 8, file->little, &stroff) ||
        !xx_macho_yx_get32(payload, payload_size, 12, file->little, &strsize) ||
        stroff > file->size || strsize > file->size - (size_t)stroff ||
        symoff > file->size)
        return true;
    for (uint32_t i = 0; i < nsyms; ++i) {
        size_t at;
        uint32_t strx;
        uint8_t type;
        const uint8_t *name, *end;
        size_t length;
        if ((size_t)i >= (file->size - (size_t)symoff) / item_size) break;
        at = (size_t)symoff + (size_t)i * item_size;
        if (at > file->size || item_size > file->size - at) break;
        if (!xx_macho_yx_get32(file->data, file->size, at,
                             file->little, &strx)) break;
        type = file->data[at + 4u];
        if (strx > strsize) continue;
        if (strx == strsize) break;
        name = file->data + (size_t)stroff + (size_t)strx;
        end = memchr(name, 0, (size_t)strsize - (size_t)strx);
        if (!end) break;
        length = (size_t)(end - name);
        if (length == 0 || (type & 0x0eu) != 0 || (type & 0xe0u) != 0)
            continue;
        /* BStr's display conversion replaces each invalid UTF-8 span. */
        if (!xx_macho_yx_append_lossy_string(&result->imports,
                                           &result->import_count,
                                           name, length))
            return false;
    }
    return true;
}

static bool xx_macho_yx_parse_file_extras(const xx_macho_yx_file *file,
                                        xx_macho_yx_extras *result)
{
    return xx_macho_yx_parse_superblob(file, result) &&
           xx_macho_yx_parse_dyld_imports(file, result) &&
           xx_macho_yx_parse_chained_imports(file, result) &&
           xx_macho_yx_parse_symtab_imports(file, result) &&
           xx_macho_yx_parse_exports(file, result);
}

bool xx_macho_yx_extras_parse(const uint8_t *data, size_t size,
                           xx_macho_yx_extras *result)
{
    xx_macho_yx_model model;
    if (!result) return false;
    memset(result, 0, sizeof(*result));
    if (!data && size) return false;
    xx_macho_yx_parse(data, size, &model);
    if (!model.valid) return false;
    if (model.has_top_file &&
        !xx_macho_yx_parse_file_extras(&model.top_file, result)) {
        xx_macho_yx_extras_free(result);
        return false;
    }
    return true;
}

bool xx_macho_yx_file_extras_parse(const uint8_t *data, size_t size,
                                uint64_t index, xx_macho_yx_extras *result)
{
    xx_macho_yx_model model;
    xx_macho_yx_file child;
    if (!result) return false;
    memset(result, 0, sizeof(*result));
    if (!data && size) return false;
    xx_macho_yx_parse(data, size, &model);
    if (!xx_macho_yx_child(&model, index, &child)) return false;
    if (!xx_macho_yx_parse_file_extras(&child, result)) {
        xx_macho_yx_extras_free(result);
        return false;
    }
    return true;
}
