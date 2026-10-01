/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Private helper for disk containers that expose a seekable guest disk.
 * The guest device is borrowed by bounded MBR/FAT partition views; no
 * materialized disk.img is needed to list or extract filesystem members.
 */

#ifndef XX_NESTED_MBR_FAT_H
#define XX_NESTED_MBR_FAT_H

#include "xxfclib/formats/mbr/xx_mbr.h"
#include "xxfclib/formats/fat/xx_fat.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#define XX_NESTED_FAT_MAX_PARTITIONS 32U

typedef struct xx_nested_fat_child_s {
    xx_io_device *partition;
    xx_fat *fat;
    xx_archive_record_state *records;
    char prefix[32];
} xx_nested_fat_child;

typedef struct xx_nested_fat_s {
    xx_io_device *guest;
    xx_mbr *mbr;
    xx_nested_fat_child *children;
    size_t count;
    size_t index;
    uint64_t total_records;
} xx_nested_fat;

static void xx_nested_fat_free(xx_nested_fat *nested) {
    size_t index;
    if (!nested) return;
    for (index = 0U; index < nested->count; ++index) {
        xx_nested_fat_child *child = &nested->children[index];
        if (child->records)
            xx_format_free_archive_records_reading(&child->fat->format,
                                                   child->records);
        if (child->fat) xx_fat_free(child->fat);
        if (child->partition) xx_io_close(child->partition);
    }
    xx_mem_free(nested->children);
    if (nested->mbr) xx_mbr_free(nested->mbr);
    if (nested->guest) xx_io_close(nested->guest);
    xx_mem_free(nested);
}

/* Build a FAT output root under the container's destination.  The reader
 * copies these options into its own state, so this temporary list is freed
 * immediately after xx_format_create_archive_records_reading returns. */
static bool xx_nested_fat_options(xx_list_t *target,
                                  const xx_list_s *source,
                                  const char *prefix) {
    size_t index;
    xx_list_init(target, sizeof(xx_meta), xx_meta_free_elem);
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        bool ok;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (original->meta_id == XX_META_ID_OPT_UNPACK_PATH) {
            const char *base = NULL;
            char *owned = NULL;
            char *path;
            if (original->var.type == XX_VAR_TYPE_STRING ||
                original->var.type == XX_VAR_TYPE_STRING_VIEW)
                base = xx_var_get_str(&original->var);
            else if (original->var.type == XX_VAR_TYPE_WSTRING ||
                     original->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
                owned = xx_str_unicode_to_utf8(xx_var_get_wstr(&original->var));
                base = owned;
            }
            if (!base) { xx_str_free(owned); return false; }
            path = base[0] && base[xx_str_len(base) - 1U] != '/' &&
                   base[xx_str_len(base) - 1U] != '\\'
                       ? xx_str_concat3(base, "/", prefix)
                       : xx_str_concat(base, prefix);
            xx_str_free(owned);
            if (!path) return false;
            ok = xx_var_set_str(&copy.var, path);
            xx_str_free(path);
        } else {
            ok = xx_var_copy(&copy.var, &original->var);
        }
        if (!ok || !xx_list_append(target, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

/* Takes ownership of guest on success and failure.  A non-FAT disk returns
 * NULL so the outer reader can retain its original raw disk.img fallback. */
static xx_nested_fat *xx_nested_fat_open(xx_io_device *guest,
                                         const xx_list_s *options,
                                         xx_pd_struct *pd) {
    xx_nested_fat *nested;
    uint64_t partition_count, ordinal;
    if (!guest) return NULL;
    nested = (xx_nested_fat *)xx_mem_calloc(1U, sizeof(*nested));
    if (!nested) { xx_io_close(guest); return NULL; }
    nested->guest = guest;
    nested->mbr = xx_mbr_create(guest, 0);
    if (!nested->mbr ||
        !xx_format_is_valid(&nested->mbr->format, pd) ||
        !xx_format_handle_base_info(&nested->mbr->format, pd) ||
        xx_mbr_is_protective(nested->mbr))
        goto fail;
    partition_count = xx_mbr_get_number_of_archive_records(
        &nested->mbr->format, pd);
    if (!partition_count || partition_count > XX_NESTED_FAT_MAX_PARTITIONS)
        goto fail;
    nested->children = (xx_nested_fat_child *)xx_mem_calloc(
        (size_t)partition_count, sizeof(*nested->children));
    if (!nested->children) goto fail;
    for (ordinal = 0U; ordinal < partition_count; ++ordinal) {
        xx_mbr_partition_info info;
        xx_io_volume volume;
        xx_nested_fat_child candidate = {0};
        xx_list_t child_options;
        uint64_t records;
        int prefix_length;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_mbr_get_partition_info(nested->mbr, ordinal, &info) ||
            info.offset < 0 || info.size <= 0)
            continue;
        prefix_length = snprintf(candidate.prefix, sizeof(candidate.prefix),
                                 "FAT.Partition.%llu",
                                 (unsigned long long)(ordinal + 1U));
        if (prefix_length <= 0 ||
            (size_t)prefix_length >= sizeof(candidate.prefix)) goto fail;
        volume.device = guest;
        volume.offset = info.offset;
        volume.size = info.size;
        candidate.partition = xx_io_multivolume_open(&volume, 1U, false);
        if (!candidate.partition) goto fail;
        candidate.fat = xx_fat_create(candidate.partition, 0);
        if (!candidate.fat ||
            !xx_format_is_valid(&candidate.fat->format, pd) ||
            !xx_format_handle_base_info(&candidate.fat->format, pd)) {
            if (candidate.fat) xx_fat_free(candidate.fat);
            xx_io_close(candidate.partition);
            continue;
        }
        records = xx_fat_get_number_of_archive_records(
            &candidate.fat->format, pd);
        if (!records) {
            xx_fat_free(candidate.fat);
            xx_io_close(candidate.partition);
            continue;
        }
        if (records > (uint64_t)INT64_MAX - nested->total_records) {
            xx_fat_free(candidate.fat);
            xx_io_close(candidate.partition);
            goto fail;
        }
        if (!xx_nested_fat_options(&child_options, options,
                                   candidate.prefix)) {
            xx_list_cleanup(&child_options);
            xx_fat_free(candidate.fat);
            xx_io_close(candidate.partition);
            goto fail;
        }
        candidate.records = xx_format_create_archive_records_reading(
            &candidate.fat->format, (const xx_list_s *)&child_options, pd);
        xx_list_cleanup(&child_options);
        if (!candidate.records || !candidate.records->has_record) {
            if (candidate.records)
                xx_format_free_archive_records_reading(&candidate.fat->format,
                                                       candidate.records);
            xx_fat_free(candidate.fat);
            xx_io_close(candidate.partition);
            goto fail;
        }
        nested->children[nested->count++] = candidate;
        nested->total_records += records;
    }
    if (!nested->count) goto fail;
    return nested;
fail:
    xx_nested_fat_free(nested);
    return NULL;
}

static bool xx_nested_fat_set_record(xx_archive_record *record,
                                     const xx_nested_fat *nested) {
    const xx_nested_fat_child *child;
    const xx_archive_record *source;
    const char *name;
    char *prefixed;
    size_t index;
    if (!record || !nested || nested->index >= nested->count) return false;
    child = &nested->children[nested->index];
    source = xx_format_get_current_archive_record(&child->fat->format,
                                                  child->records);
    if (!source || !(name = xx_archive_record_get_original_name(source)))
        return false;
    prefixed = xx_str_concat3(child->prefix, "/", name);
    if (!prefixed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    /* Inner offsets are relative to a virtual partition, not contiguous
     * offsets in the outer VHD/VDI file. */
    record->compressed_size = source->compressed_size;
    for (index = 0U; index < source->list_meta.count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at(
            (const xx_list_t *)&source->list_meta, index);
        if (meta && meta->meta_id != XX_META_ID_ORIGINAL_NAME &&
            !xx_archive_record_add_meta(record, meta->meta_id, &meta->var)) {
            xx_str_free(prefixed);
            return false;
        }
    }
    if (!xx_archive_record_set_original_name(record, prefixed)) {
        xx_str_free(prefixed);
        return false;
    }
    xx_str_free(prefixed);
    return true;
}

static bool xx_nested_fat_advance(xx_nested_fat *nested, xx_pd_struct *pd) {
    xx_nested_fat_child *child;
    if (!nested || nested->index >= nested->count) return false;
    child = &nested->children[nested->index];
    if (xx_format_archive_record_move_to_next(&child->fat->format,
                                               child->records, pd))
        return true;
    ++nested->index;
    return nested->index < nested->count;
}

static bool xx_nested_fat_unpack(xx_nested_fat *nested, xx_pd_struct *pd) {
    xx_nested_fat_child *child;
    if (!nested || nested->index >= nested->count) return false;
    child = &nested->children[nested->index];
    return xx_format_unpack_current_archive_record(&child->fat->format,
                                                   child->records, pd);
}

#endif /* XX_NESTED_MBR_FAT_H */
