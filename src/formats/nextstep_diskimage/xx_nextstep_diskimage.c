/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/nextstep_diskimage/xx_nextstep_diskimage.h"
#include "xxfclib/formats/sufs/xx_sufs.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef NEXTSTEP_DISKIMAGE
#define NS_FILE_TYPE XX_FILE_TYPE_NEXTSTEP_DISKIMAGE
#else
#define NS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define NS_HEADER 46U
#define NS_SECTOR 512U
#define NS_SCAN_LIMIT (1024U * 1024U)
#define NS_MAGIC_OFFSET (8192U + 1372U)
#define NS_COPY 65536U

typedef struct ns_layout_s {
    int64_t disk_base;
    uint64_t disk_size;
    int64_t archive_size;
} ns_layout;

static bool ns_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}
static bool ns_read(xx_io_device *device, int64_t offset, void *buffer,
                    size_t size) {
    int64_t saved;
    size_t done = 0U;
    bool ok = false;
    if (!device || offset < 0 || (!buffer && size)) return false;
    saved = xx_io_tell(device);
    if (saved < 0) return false;
    if (xx_io_seek64(device, offset, SEEK_SET) == 0) {
        while (done < size) {
            ssize_t got = xx_io_read(device, (uint8_t *)buffer + done,
                                     size - done);
            if (got <= 0 || (size_t)got > size - done) break;
            done += (size_t)got;
        }
        ok = done == size;
    }
    if (xx_io_seek64(device, saved, SEEK_SET) != 0) ok = false;
    return ok;
}

/* The NeXT container has no signature; all redundant geometry fields must
 * agree before probing its labelled disk. The disk is at most 4 GiB. */
static bool ns_parse(Abstractformat *f, ns_layout *layout, xx_pd_struct *pd) {
    uint8_t h[NS_HEADER];
    uint64_t geometry, image_size, total_sectors;
    int64_t total;
    if (!f || !f->device || f->base_address < 0 || ns_stopped(pd))
        return false;
    total = xx_io_total_size(f->device);
    if (total < f->base_address ||
        total - f->base_address < (int64_t)(NS_HEADER + NS_SECTOR) ||
        !ns_read(f->device, f->base_address, h, sizeof(h))) return false;
    total_sectors = xx_data_get_u32(h + 0x2A, 4, 0, true);
    image_size = xx_data_get_u32(h + 0x16, 4, 0, true);
    geometry = (uint64_t)xx_data_get_u32(h + 0x06, 4, 0, true) * xx_data_get_u32(h + 0x0A, 4, 0, true) *
               xx_data_get_u32(h + 0x24, 4, 0, true);
    if (!xx_data_get_u32(h, 4, 0, true) || xx_data_get_u32(h, 4, 0, true) > 16U || xx_data_get_u16(h + 4, 2, 0, true) != NS_SECTOR ||
        xx_data_get_u16(h + 0x22, 2, 0, true) != NS_SECTOR ||
        xx_data_get_u32(h + 0x1E, 4, 0, true) != NS_SECTOR || xx_data_get_u32(h + 0x1A, 4, 0, true) != 1U ||
        !xx_data_get_u32(h + 0x06, 4, 0, true) || xx_data_get_u32(h + 0x06, 4, 0, true) > 65535U ||
        !xx_data_get_u32(h + 0x0A, 4, 0, true) || xx_data_get_u32(h + 0x0A, 4, 0, true) > 255U ||
        !xx_data_get_u32(h + 0x24, 4, 0, true) || xx_data_get_u32(h + 0x24, 4, 0, true) > 255U ||
        !total_sectors || geometry != total_sectors ||
        image_size != total_sectors * NS_SECTOR ||
        image_size > (uint64_t)(total - f->base_address - NS_HEADER))
        return false;
    layout->disk_base = f->base_address + NS_HEADER;
    layout->disk_size = image_size;
    layout->archive_size = (int64_t)(NS_HEADER + image_size);
    return !ns_stopped(pd);
}

/* Locate the first independently valid legacy FFS partition. The NeXT disk
 * label has varied across media; the FFS superblock and root directory are
 * stronger bounds than a label-only partition entry. Searching only the
 * first MiB avoids scanning a huge untrusted image. */
static xx_sufs *ns_find_ufs(Abstractformat *f, const ns_layout *layout,
                            int64_t *partition_offset, xx_pd_struct *pd) {
    uint64_t at, end;
    uint8_t magic[4];
    int64_t saved, found = -1;
    xx_sufs *result = NULL;
    if (layout->disk_size < NS_MAGIC_OFFSET + sizeof(magic)) return NULL;
    saved = xx_io_tell(f->device);
    if (saved < 0) return NULL;
    end = layout->disk_size - NS_MAGIC_OFFSET - sizeof(magic);
    if (end > NS_SCAN_LIMIT) end = NS_SCAN_LIMIT;
    for (at = 0U; at <= end; at += NS_SECTOR) {
        xx_sufs *volume;
        int64_t base;
        if (ns_stopped(pd)) break;
        base = layout->disk_base + (int64_t)at;
        if (!ns_read(f->device, base + NS_MAGIC_OFFSET,
                     magic, sizeof(magic)) || magic[0] != 0U ||
            magic[1] != 1U || magic[2] != 0x19U || magic[3] != 0x54U)
            continue;
        volume = xx_sufs_create(f->device, base);
        if (!volume) break;
        xx_sufs_enable_nextstep_legacy(volume);
        if (xx_sufs_check_is_valid(&volume->format, pd) &&
            xx_sufs_handle_base_info(&volume->format, pd) &&
            volume->format.format_size > 0 &&
            (uint64_t)volume->format.format_size <= layout->disk_size - at &&
            volume->number_of_records > 0U) {
            found = base;
            result = volume;
            break;
        }
        xx_sufs_free(volume);
    }
    if (xx_io_seek64(f->device, saved, SEEK_SET) != 0 || ns_stopped(pd)) {
        if (result) xx_sufs_free(result);
        return NULL;
    }
    if (result) *partition_offset = found;
    return result;
}

static bool ns_copy_options(xx_list_s *dest, const xx_list_s *source) {
    size_t i;
    if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, i);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(dest, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}
static bool ns_prefix_record(xx_archive_record_state *state) {
    const char *name =
        xx_archive_record_get_original_name(&state->current_record);
    char *prefixed;
    bool ok;
    if (!name) return false;
    prefixed = xx_str_concat3("UFS.Partition.1", "/", name);
    if (!prefixed) return false;
    ok = xx_archive_record_set_original_name(&state->current_record,
                                              prefixed);
    xx_str_free(prefixed);
    return ok;
}
static bool ns_prefix_output(xx_archive_record_state *state) {
    size_t i;
    for (i = 0U; i < state->options.count; ++i) {
        xx_meta *meta = (xx_meta *)xx_list_at(&state->options, i);
        const char *base = NULL;
        char *wide_base = NULL, *path;
        bool ok;
        if (!meta || meta->meta_id != XX_META_ID_OPT_UNPACK_PATH) continue;
        if (meta->var.type == XX_VAR_TYPE_STRING ||
            meta->var.type == XX_VAR_TYPE_STRING_VIEW)
            base = xx_var_get_str(&meta->var);
        else if (meta->var.type == XX_VAR_TYPE_WSTRING ||
                 meta->var.type == XX_VAR_TYPE_WSTRING_VIEW)
            base = wide_base = xx_str_unicode_to_utf8(
                xx_var_get_wstr(&meta->var));
        if (!base) { xx_str_free(wide_base); return false; }
        path = base[0] ? xx_str_concat3(base, "/", "UFS.Partition.1") :
                         xx_str_dup("UFS.Partition.1");
        xx_str_free(wide_base);
        if (!path) return false;
        ok = xx_var_set_str(&meta->var, path);
        xx_str_free(path);
        return ok;
    }
    return true;
}
static bool ns_set_raw_record(xx_archive_record *record,
                              const ns_layout *layout) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = layout->disk_base - NS_HEADER;
    record->header_size = NS_HEADER;
    record->data_offset = layout->disk_base;
    record->compressed_size = (int64_t)layout->disk_size;
    return xx_archive_record_set_original_name(record, "disk.img") &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          layout->disk_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSED_SIZE,
                                          layout->disk_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}
static bool ns_copy_raw(Abstractformat *f, const ns_layout *layout,
                        xx_io_device *out, xx_pd_struct *pd) {
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(NS_COPY);
    uint64_t offset = 0U;
    bool ok = buffer != NULL;
    if (out == f->device) ok = false;
    while (ok && offset < layout->disk_size) {
        size_t part = (size_t)(layout->disk_size - offset > NS_COPY ?
                               NS_COPY : layout->disk_size - offset);
        if (ns_stopped(pd) ||
            !ns_read(f->device, layout->disk_base + (int64_t)offset,
                     buffer, part) ||
            (out && xx_io_write(out, buffer, part) != (ssize_t)part))
            ok = false;
        else
            offset += part;
    }
    xx_mem_free(buffer);
    return ok && !ns_stopped(pd);
}

void xx_nextstep_diskimage_init(xx_nextstep_diskimage *image,
                                xx_io_device *device, int64_t base) {
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    xx_format_init(&image->format, device, base);
    image->format.endian = XX_ENDIAN_BIG;
    image->format.file_type = NS_FILE_TYPE;
    image->format.format_type = XX_TYPE_ARCHIVE;
    image->format.is_archive = true;
    xx_format_set_mime_type(&image->format,
                            "application/x-nextstep-diskimage");
    xx_format_set_extension(&image->format, "diskimage");
    image->format.check_is_valid = xx_nextstep_diskimage_check_is_valid;
    image->format.handle_base_info = xx_nextstep_diskimage_handle_base_info;
    image->format.get_format_size = xx_nextstep_diskimage_get_format_size;
    image->format.get_number_of_archive_records =
        xx_nextstep_diskimage_get_number_of_archive_records;
    image->format.create_archive_records_reading =
        xx_nextstep_diskimage_create_archive_records_reading;
    image->format.get_current_archive_record =
        xx_nextstep_diskimage_get_current_archive_record;
    image->format.archive_record_move_to_next =
        xx_nextstep_diskimage_archive_record_move_to_next;
    image->format.unpack_current_archive_record =
        xx_nextstep_diskimage_unpack_current_archive_record;
    image->format.free_archive_records_reading =
        xx_nextstep_diskimage_free_archive_records_reading;
    image->partition_offset = -1;
}
xx_nextstep_diskimage *xx_nextstep_diskimage_create(xx_io_device *device,
                                                    int64_t base) {
    xx_nextstep_diskimage *image =
        (xx_nextstep_diskimage *)xx_mem_alloc(sizeof(*image));
    if (image) xx_nextstep_diskimage_init(image, device, base);
    return image;
}
void xx_nextstep_diskimage_destroy(xx_nextstep_diskimage *image) {
    if (image) xx_format_cleanup_extra_parameters(&image->format);
}
void xx_nextstep_diskimage_free(xx_nextstep_diskimage *image) {
    if (!image) return;
    xx_nextstep_diskimage_destroy(image);
    xx_mem_free(image);
}
bool xx_nextstep_diskimage_check_is_valid(Abstractformat *f,
                                          xx_pd_struct *pd) {
    ns_layout layout;
    return ns_parse(f, &layout, pd);
}
bool xx_nextstep_diskimage_handle_base_info(Abstractformat *f,
                                            xx_pd_struct *pd) {
    ns_layout layout;
    xx_sufs *volume;
    int64_t partition = -1, total;
    if (!ns_parse(f, &layout, pd)) return false;
    volume = ns_find_ufs(f, &layout, &partition, pd);
    if (ns_stopped(pd)) { if (volume) xx_sufs_free(volume); return false; }
    ((xx_nextstep_diskimage *)f)->number_of_records =
        volume ? volume->number_of_records : 1U;
    ((xx_nextstep_diskimage *)f)->partition_offset = partition;
    ((xx_nextstep_diskimage *)f)->disk_size = layout.disk_size;
    f->number_of_archive_records =
        ((xx_nextstep_diskimage *)f)->number_of_records;
    f->format_size = layout.archive_size;
    total = xx_io_total_size(f->device);
    f->overlay_offset = total > f->base_address + layout.archive_size ?
        f->base_address + layout.archive_size : -1;
    f->overlay_size = f->overlay_offset >= 0 ?
        total - f->overlay_offset : 0;
    f->is_valid = true;
    f->base_info_handled = true;
    if (volume) xx_sufs_free(volume);
    return true;
}
int64_t xx_nextstep_diskimage_get_format_size(Abstractformat *f,
                                              xx_pd_struct *pd) {
    return f && (f->base_info_handled ||
                 xx_nextstep_diskimage_handle_base_info(f, pd)) ?
           f->format_size : -1;
}
uint64_t xx_nextstep_diskimage_get_number_of_archive_records(
    Abstractformat *f, xx_pd_struct *pd) {
    return f && (f->base_info_handled ||
                 xx_nextstep_diskimage_handle_base_info(f, pd)) ?
           f->number_of_archive_records : 0U;
}
xx_archive_record_state *xx_nextstep_diskimage_create_archive_records_reading(
    Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    ns_layout parsed, *layout;
    xx_archive_record_state *state;
    xx_sufs *volume;
    int64_t partition = -1;
    if (!ns_parse(f, &parsed, pd)) return NULL;
    volume = ns_find_ufs(f, &parsed, &partition, pd);
    if (ns_stopped(pd)) { if (volume) xx_sufs_free(volume); return NULL; }
    if (volume) {
        state = xx_sufs_create_archive_records_reading(&volume->format,
                                                        options, pd);
        if (!state || !ns_prefix_output(state) ||
            (state->has_record && !ns_prefix_record(state))) {
            if (state) xx_sufs_free_archive_records_reading(&volume->format,
                                                              state);
            xx_sufs_free(volume);
            return NULL;
        }
        return state;
    }
    layout = (ns_layout *)xx_mem_alloc(sizeof(*layout));
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!layout || !state) {
        xx_mem_free(layout);
        xx_mem_free(state);
        return NULL;
    }
    *layout = parsed;
    xx_archive_record_state_init(state, f);
    state->internal_state = layout;
    state->free_internal = xx_mem_free;
    state->total_records = 1;
    if (!ns_copy_options(&state->options, options) ||
        !ns_set_raw_record(&state->current_record, layout)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}
const xx_archive_record *xx_nextstep_diskimage_get_current_archive_record(
    Abstractformat *f, xx_archive_record_state *state) {
    if (f && state && state->format != f)
        return xx_sufs_get_current_archive_record(state->format, state);
    return f && state && state->format == f && state->has_record ?
           &state->current_record : NULL;
}
bool xx_nextstep_diskimage_archive_record_move_to_next(
    Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (f && state && state->format != f) {
        if (!xx_sufs_archive_record_move_to_next(state->format, state, pd))
            return false;
        if (!ns_prefix_record(state)) { state->has_record = false; return false; }
        return true;
    }
    if (!f || !state || state->format != f || ns_stopped(pd)) return false;
    state->has_record = false;
    state->current_index = 1;
    return false;
}
bool xx_nextstep_diskimage_unpack_current_archive_record(
    Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    const xx_var *option;
    const char *base;
    char *wide_base = NULL, *path = NULL, *stage = NULL;
    xx_io_device *output = NULL;
    ns_layout *layout;
    size_t capacity;
    unsigned attempt;
    bool ok = false, overwrite;
    if (f && state && state->format != f)
        return xx_sufs_unpack_current_archive_record(state->format, state,
                                                      pd);
    if (!f || !state || state->format != f || !state->has_record ||
        !(layout = (ns_layout *)state->internal_state) || ns_stopped(pd))
        return false;
    option = xx_format_resolve_extra_parameter(f, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return ns_copy_raw(f, layout, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING ||
             option->type == XX_VAR_TYPE_WSTRING_VIEW)
        base = wide_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
    else return false;
    if (!base) goto done;
    path = base[0] ? xx_str_concat3(base, "/", "disk.img") :
                     xx_str_dup("disk.img");
    option = xx_format_resolve_extra_parameter(f, &state->options,
                                               XX_META_ID_OPT_OVERWRITE);
    overwrite = option && xx_var_get_bool(option);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path, false) ||
        xx_str_len(base) > SIZE_MAX - 48U) goto done;
    capacity = xx_str_len(base) + 48U;
    stage = (char *)xx_mem_alloc(capacity);
    if (!stage) goto done;
    for (attempt = 0U; attempt < 128U && !ns_stopped(pd); ++attempt) {
        int n = xx_rt_snprintf(stage, capacity,
                               "%s%s.xx_nextstep.tmp.%u",
                               base, base[0] ? "/" : "", attempt);
        if (n < 0 || (size_t)n >= capacity) goto done;
        output = xx_io_file_open(stage, "wbx");
        if (output) break;
    }
    if (!output) goto done;
    ok = ns_copy_raw(f, layout, output, pd);
    if (xx_io_close(output) != 0) ok = false;
    output = NULL;
    if (ok && !ns_stopped(pd))
        ok = xx_io_file_replace_a(stage, path, overwrite);
    else
        ok = false;
    if (!ok) (void)xx_io_file_remove_a(stage);
done:
    if (output) {
        (void)xx_io_close(output);
        (void)xx_io_file_remove_a(stage);
    }
    xx_mem_free(stage);
    xx_str_free(path);
    xx_str_free(wide_base);
    return ok;
}
void xx_nextstep_diskimage_free_archive_records_reading(
    Abstractformat *f, xx_archive_record_state *state) {
    if (f && state && state->format != f) {
        xx_sufs *volume = (xx_sufs *)state->format;
        xx_sufs_free_archive_records_reading(&volume->format, state);
        xx_sufs_free(volume);
        return;
    }
    xx_archive_record_state_free(state);
}
