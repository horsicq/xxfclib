/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/executable/py2exe/xx_py2exe.h"
#include "xxfclib/formats/archive/zip/xx_zip.h"
#include "../../common/xx_pe_resource_walk.h"

typedef struct py2exe_script {
    int64_t offset;
    uint32_t size;
    bool embedded_library;
} py2exe_script;

static bool py2exe_resource(Abstractformat *format, pm_stream *stream, const rw_leaf_info *leaf, void *context, xx_pd_struct *pd)
{
    py2exe_script *script = (py2exe_script *)context;
    uint8_t header[16], name[4096], prefix[5];
    uint32_t size, count;
    size_t name_size, separator;
    int64_t offset;
    (void)stream;
    (void)pd;

    if (script->size || !leaf->type_name || xx_rt_strcmp(leaf->type_name, "PYTHONSCRIPT") ||
        leaf->name || leaf->name_id != 1 || leaf->bytes < sizeof(header) + 7 ||
        !pm_read(format, leaf->at, header, sizeof(header)) || xx_rt_memcmp(header, "\x12\x34\x56\x78", 4))
        return true;

    /* Four little-endian uint32 values, a NUL-terminated library name and
     * a marshal list of code objects, followed by a NUL terminator. */
    size = xx_data_get_u32(header + 12, 4, 0, false);
    if (size < sizeof(prefix) || size > leaf->bytes - sizeof(header) - 2) return true;
    name_size = leaf->bytes - sizeof(header) - size - 1;
    if (name_size > sizeof(name) || !pm_read(format, leaf->at + sizeof(header), name, name_size)) return true;
    for (separator = 0; separator < name_size && name[separator]; ++separator) {
    }
    if (separator + 1 != name_size) return true;

    offset = leaf->at + (int64_t)sizeof(header) + (int64_t)name_size;
    if (!pm_read(format, offset, prefix, sizeof(prefix)) || (prefix[0] & 0x7f) != '[') return true;
    count = xx_data_get_u32(prefix + 1, 4, 0, false);
    if (!count || count > size - sizeof(prefix)) return true;
    if (!pm_read(format, offset + size, prefix, 1) || prefix[0]) return true;

    script->offset = offset;
    script->size = size;
    script->embedded_library = separator == 0;
    return true;
}

typedef struct py2exe_zip_patch {
    uint64_t offset;
    uint8_t bytes[4];
} py2exe_zip_patch;

typedef struct py2exe_zip_rebase {
    size_t count;
    py2exe_zip_patch patches[];
} py2exe_zip_rebase;

static void py2exe_add_patch(py2exe_zip_rebase *rebase, uint64_t offset, uint32_t value)
{
    py2exe_zip_patch *patch = &rebase->patches[rebase->count++];
    patch->offset = offset;
    for (unsigned i = 0; i < 4; ++i) patch->bytes[i] = (uint8_t)(value >> (i * 8));
}

static bool py2exe_read_zip_range(Abstractformat *format, pm_member *member, uint64_t offset, void *buffer, size_t size, xx_pd_struct *pd)
{
    const py2exe_zip_rebase *rebase = (const py2exe_zip_rebase *)member->context;
    size_t low = 0, high = rebase->count;
    uint64_t end = offset + size;

    if (carrier_stop(pd) || !pm_read(format, member->offset - format->base_address + (int64_t)offset, buffer, size)) return false;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (rebase->patches[mid].offset + 4 <= offset) low = mid + 1;
        else high = mid;
    }
    for (; low < rebase->count && rebase->patches[low].offset < end; ++low) {
        const py2exe_zip_patch *patch = &rebase->patches[low];
        for (unsigned i = 0; i < 4; ++i) {
            uint64_t at = patch->offset + i;
            if (at >= offset && at < end) ((uint8_t *)buffer)[at - offset] = patch->bytes[i];
        }
    }
    return true;
}

/* The ZIP reader already validates the directory. Rebase classic ZIP
 * offsets so that the recovered library opens as a ZIP on its own. */
static bool py2exe_add_zip(Abstractformat *format, pm_stream *stream, const xx_zip *zip, int64_t image_end, xx_pd_struct *pd)
{
    int64_t end = zip->eocd_offset - format->base_address;
    int64_t central = zip->cd_offset - format->base_address;
    uint8_t header[46];
    uint32_t stored_central;
    uint64_t count = zip->format.number_of_archive_records;
    int64_t bias, at;
    py2exe_zip_rebase *rebase;

    if (zip->zip64_eocd_offset >= 0) {
        /* Keep ZIP64's original coordinates; open this payload explicitly
         * as a ZIP. Its 64-bit extra fields are not rewritten here. */
        return pm_add(format, stream, "payload.zip", 0, pm_available(format));
    }
    if (central < image_end || central > end || !pm_read(format, end, header, 22) ||
        xx_rt_memcmp(header, "PK\x05\x06", 4) || count > UINT16_MAX ||
        count != xx_data_get_u16(header + 10, 2, 0, false))
        return false;

    stored_central = xx_data_get_u32(header + 16, 4, 0, false);
    if (central - image_end > UINT32_MAX) return false;
    if (stored_central == (uint64_t)(central - image_end))
        return pm_add(format, stream, "payload.zip", image_end, pm_available(format) - image_end);

    rebase = (py2exe_zip_rebase *)xx_mem_alloc(sizeof(*rebase) + ((size_t)count + 1) * sizeof(rebase->patches[0]));
    if (!rebase) return false;
    rebase->count = 0;
    bias = central - stored_central;
    at = central;
    for (uint64_t i = 0; i < count; ++i) {
        int64_t local, length;
        if (carrier_stop(pd) || end - at < (int64_t)sizeof(header) ||
            !pm_read(format, at, header, sizeof(header)) || xx_rt_memcmp(header, "PK\x01\x02", 4))
            goto fail;
        local = (int64_t)xx_data_get_u32(header + 42, 4, 0, false) + bias;
        if (local < image_end || local >= central) goto fail;
        length = (int64_t)sizeof(header) + xx_data_get_u16(header + 28, 2, 0, false) +
                 xx_data_get_u16(header + 30, 2, 0, false) + xx_data_get_u16(header + 32, 2, 0, false);
        if (length > end - at) goto fail;
        py2exe_add_patch(rebase, (uint64_t)(at + 42 - image_end), (uint32_t)(local - image_end));
        at += length;
    }
    if (at != end || !pm_add(format, stream, "payload.zip", image_end, pm_available(format) - image_end)) goto fail;
    py2exe_add_patch(rebase, (uint64_t)(end + 16 - image_end), (uint32_t)(central - image_end));
    pm_member *member = &stream->items[stream->count - 1];
    member->read_range = py2exe_read_zip_range;
    member->context = rebase;
    member->free_context = xx_mem_free;
    return true;

fail:
    xx_mem_free(rebase);
    return false;
}

static bool py2exe_append_library(Abstractformat *format, pm_stream *stream, int64_t image_end, xx_pd_struct *pd)
{
    xx_zip zip;
    xx_archive_record_state *state = NULL;
    uint8_t magic[4];
    bool valid = false;
    int64_t available = pm_available(format);

    if (image_end >= available || !pm_read(format, image_end, magic, sizeof(magic)) || xx_rt_memcmp(magic, "PK\x03\x04", 4)) return true;

    xx_zip_init(&zip, format->device, format->base_address);
    zip.allow_leading_data = true;
    zip.zc_dictionary_enabled = false;
    if (xx_zip_check_is_valid(&zip.format, pd) && xx_zip_handle_base_info(&zip.format, pd) &&
        !zip.directory_damaged && zip.format.format_size == available) {
        state = xx_zip_create_archive_records_reading(&zip.format, NULL, pd);
        if (state && state->has_record && state->current_record.header_offset >= format->base_address + image_end) valid = true;
    }
    if (state) xx_zip_free_archive_records_reading(&zip.format, state);
    if (valid) valid = py2exe_add_zip(format, stream, &zip, image_end, pd);
    xx_zip_destroy(&zip);
    return valid;
}

static bool py2exe_find_script(Abstractformat *format, rw_walk *walk, py2exe_script *script, xx_pd_struct *pd)
{
    uint8_t magic[2];
    int64_t cursor = xx_io_tell(format->device);
    bool found = pm_read(format, 0, magic, sizeof(magic)) && !xx_rt_memcmp(magic, "MZ", 2) &&
                 rw_open(format, walk, pd) && rw_walk_leaves(format, NULL, walk, py2exe_resource, script, pd) && script->size;
    if (cursor >= 0) (void)xx_io_seek64(format->device, cursor, SEEK_SET);
    return found;
}

static bool pm_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    rw_walk walk;
    py2exe_script script = {0};

    if (!py2exe_find_script(format, &walk, &script, pd)) return false;
    if (!pm_add(format, stream, "entry-scripts.pyc.marshal", script.offset, script.size)) return false;
    if (script.embedded_library && !py2exe_append_library(format, stream, walk.image_end, pd)) return false;

    stream->size = pm_available(format);
    return true;
}

void xx_py2exe_init(xx_py2exe *reader, xx_io_device *device, int64_t base)
{
    pm_init(&reader->format, device, base, XX_FILE_TYPE_PY2EXE, "exe");
    reader->format.check_is_valid = xx_py2exe_check_is_valid;
}

xx_py2exe *xx_py2exe_create(xx_io_device *device, int64_t base)
{
    xx_py2exe *reader = (xx_py2exe *)xx_mem_calloc(1, sizeof(*reader));
    if (reader) xx_py2exe_init(reader, device, base);
    return reader;
}

void xx_py2exe_destroy(xx_py2exe *reader)
{
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_py2exe_free(xx_py2exe *reader)
{
    xx_py2exe_destroy(reader);
    xx_mem_free(reader);
}

bool xx_py2exe_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    rw_walk walk;
    py2exe_script script = {0};
    return format && format->device && py2exe_find_script(format, &walk, &script, pd);
}
