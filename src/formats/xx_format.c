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

/* Generic format properties, detection orchestration and callback dispatch.
 * Concrete reader dependencies belong to detection/xx_format_detect_device.c.
 */
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/formats/pe/xx_pe.h"
#include "xxfclib/rt/xx_rt.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

xx_format_type_t xx_format_get_type(Abstractformat *f)
{
    if (!f) {
        return XX_TYPE_UNKNOWN;
    }
    if (f->get_type) {
        return f->get_type(f);
    }
    return f->format_type;
}

void xx_format_set_type(Abstractformat *f, xx_format_type_t type)
{
    if (f) {
        if (f->format_type != type) xx_format_invalidate_memory_map(f);
        f->format_type = type;
    }
}

xx_endian_t xx_format_get_endian(Abstractformat *f)
{
    if (!f) {
        return XX_ENDIAN_UNKNOWN;
    }
    if (f->get_endian) {
        return f->get_endian(f);
    }
    return f->endian;
}

void xx_format_set_endian(Abstractformat *f, xx_endian_t endian)
{
    if (f) {
        if (f->endian != endian) xx_format_invalidate_memory_map(f);
        f->endian = endian;
    }
}

xx_os_t xx_format_get_os(Abstractformat *f)
{
    if (!f) {
        return XX_OS_UNKNOWN;
    }
    if (f->get_os) {
        return f->get_os(f);
    }
    return f->os;
}

void xx_format_set_os(Abstractformat *f, xx_os_t os)
{
    if (f) {
        f->os = os;
    }
}

xx_arch_t xx_format_get_arch(Abstractformat *f)
{
    if (!f) {
        return XX_ARCH_UNKNOWN;
    }
    if (f->get_arch) {
        return f->get_arch(f);
    }
    return f->arch;
}

void xx_format_set_arch(Abstractformat *f, xx_arch_t arch)
{
    if (f) {
        if (f->arch != arch) xx_format_invalidate_memory_map(f);
        f->arch = arch;
    }
}

xx_file_type_t xx_format_get_parent_file_type(xx_file_type_t type)
{
    switch (type) {
        case XX_FILE_TYPE_SFX_INFTOOL: return XX_FILE_TYPE_PE32;
        /* ZIP containers. */
        case XX_FILE_TYPE_ZIP64:
        case XX_FILE_TYPE_JAR:
        case XX_FILE_TYPE_APK:
        case XX_FILE_TYPE_IPA:
        case XX_FILE_TYPE_APKS: return XX_FILE_TYPE_ZIP;
        /* An npm package is a tar.gz with a package/ root, and a tar.gz is a
         * gzip stream, so this walks two levels. */
        case XX_FILE_TYPE_NPM: return XX_FILE_TYPE_TAR_GZ;
        case XX_FILE_TYPE_TAR_GZ: return XX_FILE_TYPE_GZ;
        case XX_FILE_TYPE_TAR_BZ2: return XX_FILE_TYPE_BZ2;
        case XX_FILE_TYPE_TAR_XZ: return XX_FILE_TYPE_XZ;
        /* The remaining compressed-tar variants have no standalone type for
         * their outer stream, so they hang directly off BINARY. */
        case XX_FILE_TYPE_PE32:
        case XX_FILE_TYPE_PE64:
        case XX_FILE_TYPE_DOTNET:
        case XX_FILE_TYPE_NE:
        case XX_FILE_TYPE_LE:
        case XX_FILE_TYPE_LX: return XX_FILE_TYPE_MSDOS;
        /* Nothing is more generic than a binary, so this ends the chain. */
        case XX_FILE_TYPE_UNKNOWN:
        case XX_FILE_TYPE_BINARY: return XX_FILE_TYPE_UNKNOWN;
        default: return XX_FILE_TYPE_BINARY;
    }
}

size_t xx_format_get_file_type_chain(xx_file_type_t type, xx_file_type_t *types, size_t capacity)
{
    xx_file_type_t stack[XX_FILE_TYPE_CHAIN_MAX];
    size_t count = 0U;
    xx_file_type_t current = type;

    if (type == XX_FILE_TYPE_UNKNOWN) {
        return 0U;
    }
    /* Walk from the most specific type towards BINARY, then reverse, so the
     * caller sees the chain outermost-container first. The bound also stops a
     * malformed parent table from looping forever. */
    while (current != XX_FILE_TYPE_UNKNOWN && count < XX_FILE_TYPE_CHAIN_MAX) {
        stack[count++] = current;
        current = xx_format_get_parent_file_type(current);
    }
    if (!types || count > capacity) {
        return count;
    }
    {
        size_t index;
        for (index = 0U; index < count; ++index) {
            types[index] = stack[count - 1U - index];
        }
    }
    return count;
}

xx_file_type_t xx_format_get_pref_type(const xx_list_t *types)
{
    xx_file_type_t preferred = XX_FILE_TYPE_UNKNOWN;
    size_t preferred_depth = 0U;
    size_t index;

    if (!types || types->elem_size != sizeof(xx_file_type_t) || types->count > types->capacity || types->count > SIZE_MAX / sizeof(xx_file_type_t) ||
        (types->count && !types->data))
        return preferred;

    for (index = 0U; index < types->count; ++index) {
        xx_file_type_t type;
        size_t depth;
        if (!xx_list_get(types, index, &type)) return XX_FILE_TYPE_UNKNOWN;
        if (type == XX_FILE_TYPE_UNKNOWN || strcmp(xx_format_file_type_to_string(type), "UNKNOWN") == 0) continue;
        depth = xx_format_get_file_type_chain(type, NULL, 0U);
        /* A managed assembly adds DOTNET after its PE32/PE64 carrier even
         * though the width-independent parent table points to MSDOS. */
        if (type == XX_FILE_TYPE_DOTNET) ++depth;
        if (depth >= preferred_depth) {
            preferred = type;
            preferred_depth = depth;
        }
    }
    return preferred;
}

xx_list_t *xx_format_get_file_types_device(xx_io_device *dev)
{
    xx_file_type_t chain[XX_FILE_TYPE_CHAIN_MAX];
    xx_list_t *list = xx_list_create(sizeof(xx_file_type_t), NULL);
    size_t count;
    size_t index;
    xx_file_type_t detected;

    if (!list) {
        return NULL;
    }
    detected = xx_format_get_file_type_device(dev);
    if (detected == XX_FILE_TYPE_DOTNET) {
        int64_t saved = xx_io_tell(dev);
        uint32_t nt_offset = xx_io_get_u32(dev, 0x3c, false);
        uint16_t magic = xx_io_get_u16(dev, (int64_t)nt_offset + 24, false);
        chain[0] = XX_FILE_TYPE_BINARY;
        chain[1] = XX_FILE_TYPE_MSDOS;
        chain[2] = magic == XX_PE_MAGIC_64 ? XX_FILE_TYPE_PE64 : XX_FILE_TYPE_PE32;
        chain[3] = XX_FILE_TYPE_DOTNET;
        count = 4U;
        if (saved >= 0) (void)xx_io_seek64(dev, saved, SEEK_SET);
    } else {
        count = xx_format_get_file_type_chain(detected, chain, XX_FILE_TYPE_CHAIN_MAX);
    }
    for (index = 0U; index < count; ++index) {
        if (!xx_list_append(list, &chain[index])) {
            xx_list_destroy(list);
            return NULL;
        }
    }
    return list;
}

xx_list_t *xx_format_get_file_types_detectors(xx_io_device *device, int64_t base_address, bool is_mapped, const xx_list_t *detectors)
{
    xx_list_t *types;
    int64_t saved_position;
    int64_t total;
    size_t index;

    if (detectors && (detectors->elem_size != sizeof(Abstractdetector *) || detectors->count > detectors->capacity ||
                      detectors->count > SIZE_MAX / sizeof(Abstractdetector *) || (detectors->count && !detectors->data)))
        return NULL;
    types = xx_list_create(sizeof(xx_file_type_t), NULL);
    if (!types) return NULL;
    if (!device || !detectors || !detectors->count || base_address < 0) return types;

    saved_position = xx_io_tell(device);
    if (saved_position < 0) {
        xx_list_destroy(types);
        return NULL;
    }
    total = xx_io_total_size(device);
    if (total < 0) goto failed;
    if (base_address >= total) goto done;

    for (index = 0U; index < detectors->count; ++index) {
        Abstractdetector *detector;
        xx_file_type_t type;
        if (!xx_list_get(detectors, index, &detector)) goto failed;
        if (!detector || !detector->fast_detect || !detector->file_type) continue;
        if (xx_io_seek64(device, saved_position, SEEK_SET) != 0) goto failed;
        if (!detector->fast_detect(device, base_address, is_mapped)) continue;
        if (xx_io_seek64(device, saved_position, SEEK_SET) != 0) goto failed;
        type = detector->file_type(device, base_address, is_mapped);
        if (type != XX_FILE_TYPE_UNKNOWN && !xx_list_contains(types, &type, NULL) && !xx_list_append(types, &type)) goto failed;
    }
done:
    if (xx_io_seek64(device, saved_position, SEEK_SET) != 0) goto failed;
    return types;
failed:
    (void)xx_io_seek64(device, saved_position, SEEK_SET);
    xx_list_destroy(types);
    return NULL;
}

xx_file_type_t xx_format_get_file_type(Abstractformat *fmt)
{
    if (!fmt) {
        return XX_FILE_TYPE_UNKNOWN;
    }
    if (fmt->get_file_type) {
        return fmt->get_file_type(fmt);
    }
    if (fmt->file_type != XX_FILE_TYPE_UNKNOWN) {
        return fmt->file_type;
    }
    if (fmt->device) {
        xx_format_set_file_type(fmt, xx_format_get_file_type_device(fmt->device));
        return fmt->file_type;
    }
    return XX_FILE_TYPE_UNKNOWN;
}

const char *xx_format_data_struct_id_to_string(Abstractformat *f, uint32_t id)
{
    if (id == XX_DATA_STRUCT_ID_RAW_DATA) {
        return "RAW_DATA";
    }
    if (f && f->data_struct_id_to_string) {
        return (f->data_struct_id_to_string)(f, id);
    }
    return "UNKNOWN";
}

uint32_t xx_format_data_struct_string_to_id(Abstractformat *f, const char *name)
{
    if (f && f->data_struct_string_to_id) {
        return (f->data_struct_string_to_id)(f, name);
    }
    return 0;
}

static const char *xx_format_get_short_name(Abstractformat *f)
{
    return f ? xx_format_file_type_to_string(f->file_type) : "UNKNOWN";
}

wchar_t *xx_format_data_struct_to_string(Abstractformat *f, const xx_data_struct *ds)
{
    if (!ds) {
        return NULL;
    }

    const char *id_name = xx_format_data_struct_id_to_string(f, ds->id);
    const char *type_name = xx_data_struct_type_to_string(ds->type);

    char buf[256];
    /* Global ids (e.g. RAW_DATA) are format-independent, so no "<FORMAT>::" prefix is added */
    if (ds->id == XX_DATA_STRUCT_ID_RAW_DATA) {
        xx_rt_snprintf(buf, sizeof(buf), "%s?offset=%lld&entry_size=%lld&total_size=%lld&count=%llu&type=%s", id_name, (long long)ds->offset, (long long)ds->entry_size,
                       (long long)ds->total_size, (unsigned long long)ds->count, type_name);
    } else {
        const char *format_name = xx_format_get_short_name(f);
        xx_rt_snprintf(buf, sizeof(buf), "%s::%s?offset=%lld&entry_size=%lld&total_size=%lld&count=%llu&type=%s", format_name, id_name, (long long)ds->offset,
                       (long long)ds->entry_size, (long long)ds->total_size, (unsigned long long)ds->count, type_name);
    }

    return xx_str_ansi_to_unicode(buf);
}
