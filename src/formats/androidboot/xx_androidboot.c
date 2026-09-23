/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/androidboot/xx_androidboot.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved through the alias macro that
 * the enumerator will define. Once XX_FILE_TYPE_ANDROIDBOOT lands the alias
 * is defined and this picks it up with no further change. */
#ifdef ANDROIDBOOT
#define XX_ANDROIDBOOT_FILE_TYPE XX_FILE_TYPE_ANDROIDBOOT
#else
#define XX_ANDROIDBOOT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* --- on-disk sizes ------------------------------------------------------ */

#define XX_ANDROIDBOOT_MAGIC_SIZE 8
#define XX_ANDROIDBOOT_HDR_V0_SIZE 1632
#define XX_ANDROIDBOOT_HDR_V1_SIZE 1648
#define XX_ANDROIDBOOT_HDR_V2_SIZE 1660
#define XX_ANDROIDBOOT_HDR_V3_SIZE 1580
#define XX_ANDROIDBOOT_HDR_V4_SIZE 1584
/* The largest of the five, so one read covers every layout. */
#define XX_ANDROIDBOOT_HDR_READ_SIZE XX_ANDROIDBOOT_HDR_V2_SIZE

/* The last header_version AOSP defines. */
#define XX_ANDROIDBOOT_MAX_VERSION 4U

#define XX_ANDROIDBOOT_V3_PAGE_SIZE 4096
/* AOSP mkbootimg accepts --pagesize 2^11 .. 2^17. */
#define XX_ANDROIDBOOT_MIN_PAGE_SIZE 2048
#define XX_ANDROIDBOOT_MAX_PAGE_SIZE 131072

/* Field offsets. header_version is the one offset shared by all versions. */
#define XX_ANDROIDBOOT_OFF_HEADER_VERSION 40U
/* v0 - v2 */
#define XX_ANDROIDBOOT_OFF_V0_KERNEL_SIZE 8U
#define XX_ANDROIDBOOT_OFF_V0_RAMDISK_SIZE 16U
#define XX_ANDROIDBOOT_OFF_V0_SECOND_SIZE 24U
#define XX_ANDROIDBOOT_OFF_V0_PAGE_SIZE 36U
#define XX_ANDROIDBOOT_OFF_V0_NAME 48U
#define XX_ANDROIDBOOT_V0_NAME_SIZE 16U
#define XX_ANDROIDBOOT_OFF_V0_CMDLINE 64U
#define XX_ANDROIDBOOT_V0_CMDLINE_SIZE 512U
#define XX_ANDROIDBOOT_OFF_V0_EXTRA_CMDLINE 608U
#define XX_ANDROIDBOOT_V0_EXTRA_CMDLINE_SIZE 1024U
#define XX_ANDROIDBOOT_OFF_V1_RECOVERY_DTBO_SIZE 1632U
#define XX_ANDROIDBOOT_OFF_V1_RECOVERY_DTBO_OFFSET 1636U
#define XX_ANDROIDBOOT_OFF_V1_HEADER_SIZE 1644U
#define XX_ANDROIDBOOT_OFF_V2_DTB_SIZE 1648U
/* v3 - v4 */
#define XX_ANDROIDBOOT_OFF_V3_KERNEL_SIZE 8U
#define XX_ANDROIDBOOT_OFF_V3_RAMDISK_SIZE 12U
#define XX_ANDROIDBOOT_OFF_V3_HEADER_SIZE 20U
#define XX_ANDROIDBOOT_OFF_V3_CMDLINE 44U
#define XX_ANDROIDBOOT_V3_CMDLINE_SIZE 1536U
#define XX_ANDROIDBOOT_OFF_V4_SIGNATURE_SIZE 1580U

/* Bombing defence. Every blob size is an attacker-controlled u32, so the
 * widest a single record can legally be is 4 GiB - 1; anything at or beyond
 * this ceiling is refused at parse time rather than at extraction, and the
 * device's own size bounds it a second time. A boot partition this large
 * does not exist in practice. */
#define XX_ANDROIDBOOT_MAX_MEMBER_SIZE INT64_C(0x40000000) /* 1 GiB */
/* The cmdline is at most 1536 bytes, so this only ever fails a corrupt
 * accounting mistake. */
#define XX_ANDROIDBOOT_MAX_CMDLINE_SIZE 2048U
#define XX_ANDROIDBOOT_MAX_ENTRIES 8U

/* One published record: a device range, or a short synthesised blob that has
 * no single contiguous range of its own (the v0-v2 command line is spliced
 * from two disjoint header fields). */
typedef struct xx_androidboot_entry_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t data_size;
    uint8_t *synthesized; /**< NULL for a plain device range. */
} xx_androidboot_entry;

typedef struct xx_androidboot_private_s {
    xx_androidboot_entry entries[XX_ANDROIDBOOT_MAX_ENTRIES];
    size_t count;
    int64_t input_size;
    int64_t archive_end;
    uint32_t header_version;
    uint32_t page_size;
    uint32_t header_size;
} xx_androidboot_private;

typedef struct xx_androidboot_archive_stream_s {
    xx_androidboot_private parsed;
    size_t index;
} xx_androidboot_archive_stream;

static void xx_androidboot_vtable_destroy(Abstractformat *self);

static bool xx_androidboot_read_at(xx_io_device *device, int64_t offset,
                                   void *data, size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_androidboot_range_within(int64_t total_size, int64_t offset,
                                        int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* Round up to the next page boundary, refusing to overflow. page must be a
 * positive power of two, which the caller has already established. */
static bool xx_androidboot_align_up(int64_t value, int64_t page,
                                    int64_t *result) {
    if (!result || value < 0 || page <= 0 || value > INT64_MAX - (page - 1)) {
        return false;
    }
    *result = ((value + page - 1) / page) * page;
    return true;
}

static void xx_androidboot_private_cleanup(xx_androidboot_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) {
            xx_str_free(parsed->entries[index].name);
        }
        if (parsed->entries[index].synthesized) {
            xx_mem_free(parsed->entries[index].synthesized);
        }
    }
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

/* Append a record naming a byte range in the device. The name is copied. */
static bool xx_androidboot_append_range(xx_androidboot_private *parsed,
                                        const char *name, int64_t offset,
                                        int64_t size) {
    xx_androidboot_entry *entry;
    if (!parsed || !name || parsed->count >= XX_ANDROIDBOOT_MAX_ENTRIES) {
        return false;
    }
    entry = &parsed->entries[parsed->count];
    xx_mem_zero(entry, sizeof(*entry));
    entry->name = xx_str_create(name);
    if (!entry->name) return false;
    entry->header_offset = -1;
    entry->header_size = 0;
    entry->data_offset = offset;
    entry->data_size = size;
    ++parsed->count;
    return true;
}

/* Append a record whose bytes are held in memory rather than named by a
 * device range. Ownership of blob transfers on success. */
static bool xx_androidboot_append_synth(xx_androidboot_private *parsed,
                                        const char *name, int64_t origin,
                                        uint8_t *blob, size_t size) {
    xx_androidboot_entry *entry;
    if (!parsed || !name || !blob ||
        parsed->count >= XX_ANDROIDBOOT_MAX_ENTRIES) {
        return false;
    }
    entry = &parsed->entries[parsed->count];
    xx_mem_zero(entry, sizeof(*entry));
    entry->name = xx_str_create(name);
    if (!entry->name) return false;
    entry->header_offset = origin;
    entry->header_size = 0;
    entry->data_offset = origin;
    entry->data_size = (int64_t)size;
    entry->synthesized = blob;
    ++parsed->count;
    return true;
}

/* A NUL-padded ASCII field: printable bytes up to the first NUL, then NUL to
 * the end of the field. Anything else means this is not really a boot image,
 * which is the only structural check available for v3 where the page size is
 * implicit. *out_length receives the text length, never the field size. */
static bool xx_androidboot_read_text(const uint8_t *header, size_t header_size,
                                     size_t offset, size_t size,
                                     size_t *out_length) {
    size_t index;
    size_t length = size;
    bool terminated = false;
    if (!header || !out_length || size == 0U || offset > header_size ||
        size > header_size - offset) {
        return false;
    }
    for (index = 0U; index < size; ++index) {
        uint8_t value = header[offset + index];
        if (!terminated) {
            if (value == 0U) {
                length = index;
                terminated = true;
            } else if (value < 0x20U || value > 0x7eU) {
                return false;
            }
        } else if (value != 0U) {
            return false;
        }
    }
    *out_length = length;
    return true;
}

/* Build the "cmdline.txt" blob. For v0-v2 the command line is the 512-byte
 * field at +64 followed by the 1024-byte extra field at +608; the two are
 * not adjacent, which is why this record cannot be a device range. */
static bool xx_androidboot_build_cmdline(const uint8_t *header,
                                         size_t header_size,
                                         uint32_t header_version,
                                         uint8_t **out_blob,
                                         size_t *out_size) {
    size_t first_offset;
    size_t first_length = 0U;
    size_t extra_length = 0U;
    size_t total;
    uint8_t *blob;
    if (!header || !out_blob || !out_size) return false;
    *out_blob = NULL;
    *out_size = 0U;
    if (header_version <= 2U) {
        first_offset = XX_ANDROIDBOOT_OFF_V0_CMDLINE;
        if (!xx_androidboot_read_text(header, header_size, first_offset,
                                      XX_ANDROIDBOOT_V0_CMDLINE_SIZE,
                                      &first_length) ||
            !xx_androidboot_read_text(header, header_size,
                                      XX_ANDROIDBOOT_OFF_V0_EXTRA_CMDLINE,
                                      XX_ANDROIDBOOT_V0_EXTRA_CMDLINE_SIZE,
                                      &extra_length)) {
            return false;
        }
    } else {
        first_offset = XX_ANDROIDBOOT_OFF_V3_CMDLINE;
        if (!xx_androidboot_read_text(header, header_size, first_offset,
                                      XX_ANDROIDBOOT_V3_CMDLINE_SIZE,
                                      &first_length)) {
            return false;
        }
    }
    total = first_length + extra_length;
    if (total > XX_ANDROIDBOOT_MAX_CMDLINE_SIZE) return false;
    /* A zero-length command line is legal; the allocation is padded by one
     * so xx_mem_alloc() is never asked for nothing. */
    blob = (uint8_t *)xx_mem_alloc(total + 1U);
    if (!blob) return false;
    if (first_length != 0U) {
        xx_mem_copy(blob, header + first_offset, first_length);
    }
    if (extra_length != 0U) {
        xx_mem_copy(blob + first_length,
                    header + XX_ANDROIDBOOT_OFF_V0_EXTRA_CMDLINE,
                    extra_length);
    }
    blob[total] = 0U;
    *out_blob = blob;
    *out_size = total;
    return true;
}

/* Lay out the page-aligned payload blobs named by sizes[] and add a record
 * for each non-empty one. position advances by the page-rounded size, which
 * is how mkbootimg writes them. */
static bool xx_androidboot_place_blobs(xx_androidboot_private *parsed,
                                       int64_t base_address,
                                       const int64_t *sizes,
                                       const char *const *names, size_t count,
                                       int64_t page_size, int64_t *position) {
    size_t index;
    if (!parsed || !sizes || !names || !position) return false;
    for (index = 0U; index < count; ++index) {
        int64_t size = sizes[index];
        int64_t aligned;
        if (size < 0 || size > XX_ANDROIDBOOT_MAX_MEMBER_SIZE) return false;
        if (size == 0) continue;
        /* Bound the declared size against the device before anything else
         * walks it, so a header cannot describe a blob the file cannot
         * hold. */
        if (!xx_androidboot_range_within(parsed->input_size,
                                         base_address + *position, size) ||
            !xx_androidboot_append_range(parsed, names[index],
                                         base_address + *position, size)) {
            return false;
        }
        if (!xx_androidboot_align_up(size, page_size, &aligned) ||
            aligned > INT64_MAX - *position) {
            return false;
        }
        *position += aligned;
    }
    return true;
}

static bool xx_androidboot_parse(Abstractformat *self,
                                 xx_androidboot_private *parsed,
                                 xx_pd_struct *pd) {
    uint8_t header[XX_ANDROIDBOOT_HDR_READ_SIZE];
    int64_t total_size;
    int64_t available;
    size_t header_read;
    int64_t position;
    int64_t page_size;
    uint32_t header_version;
    int64_t qcom_dt_size = 0;
    uint8_t *cmdline = NULL;
    size_t cmdline_size = 0U;
    /* Initialise before the guard clause: callers such as
     * xx_androidboot_check_is_valid() run the cleanup on their stack copy
     * whatever this returns. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    /* v3 is the smallest header, so nothing shorter can be any version. */
    if (!xx_androidboot_range_within(total_size, self->base_address,
                                     XX_ANDROIDBOOT_HDR_V3_SIZE)) {
        goto fail;
    }
    parsed->input_size = total_size;
    available = total_size - self->base_address;
    header_read = (available < XX_ANDROIDBOOT_HDR_READ_SIZE)
                      ? (size_t)available
                      : (size_t)XX_ANDROIDBOOT_HDR_READ_SIZE;
    xx_mem_zero(header, sizeof(header));
    if (!xx_androidboot_read_at(self->device, self->base_address, header,
                                header_read) ||
        xx_rt_memcmp(header, "ANDROID!", XX_ANDROIDBOOT_MAGIC_SIZE) != 0) {
        goto fail;
    }
    header_version = xx_data_get_u32(header, header_read,
                                     XX_ANDROIDBOOT_OFF_HEADER_VERSION, false);
    /* Qualcomm's CAF mkbootimg predates header_version and reused the v0
     * "unused" word at +40 as dt_size: the byte count of a device-tree table
     * placed on its own pages after the second stage. A real table is far
     * larger than 4 bytes, so a value past the last defined version is that
     * size, and the image is otherwise laid out exactly as v0. */
    if (header_version > XX_ANDROIDBOOT_MAX_VERSION) {
        qcom_dt_size = (int64_t)header_version;
        header_version = 0U;
    }
    parsed->header_version = header_version;

    if (header_version <= 2U) {
        int64_t sizes[6];
        static const char *const names[6] = {
            "kernel", "ramdisk", "second", "recovery_dtbo", "dtb", "dt"};
        int64_t declared_header_size = XX_ANDROIDBOOT_HDR_V0_SIZE;
        int64_t recovery_dtbo_size = 0;
        uint64_t recovery_dtbo_offset = 0U;
        int64_t dtb_size = 0;
        size_t name_length = 0U;
        if (header_read < XX_ANDROIDBOOT_HDR_V0_SIZE) goto fail;
        page_size = (int64_t)xx_data_get_u32(header, header_read,
                                             XX_ANDROIDBOOT_OFF_V0_PAGE_SIZE,
                                             false);
        if (page_size < XX_ANDROIDBOOT_MIN_PAGE_SIZE ||
            page_size > XX_ANDROIDBOOT_MAX_PAGE_SIZE ||
            (page_size & (page_size - 1)) != 0) {
            goto fail;
        }
        /* The board name is the cheapest structural check available, and it
         * is what separates a real header from eight matching bytes. */
        if (!xx_androidboot_read_text(header, header_read,
                                      XX_ANDROIDBOOT_OFF_V0_NAME,
                                      XX_ANDROIDBOOT_V0_NAME_SIZE,
                                      &name_length)) {
            goto fail;
        }
        if (header_version >= 1U) {
            if (header_read < XX_ANDROIDBOOT_HDR_V1_SIZE) goto fail;
            declared_header_size = (int64_t)xx_data_get_u32(
                header, header_read, XX_ANDROIDBOOT_OFF_V1_HEADER_SIZE, false);
            if (declared_header_size != ((header_version == 1U)
                                             ? XX_ANDROIDBOOT_HDR_V1_SIZE
                                             : XX_ANDROIDBOOT_HDR_V2_SIZE)) {
                goto fail;
            }
            recovery_dtbo_size = (int64_t)xx_data_get_u32(
                header, header_read, XX_ANDROIDBOOT_OFF_V1_RECOVERY_DTBO_SIZE,
                false);
            recovery_dtbo_offset = xx_data_get_u64(
                header, header_read,
                XX_ANDROIDBOOT_OFF_V1_RECOVERY_DTBO_OFFSET, false);
        }
        if (header_version >= 2U) {
            if (header_read < XX_ANDROIDBOOT_HDR_V2_SIZE) goto fail;
            dtb_size = (int64_t)xx_data_get_u32(
                header, header_read, XX_ANDROIDBOOT_OFF_V2_DTB_SIZE, false);
        }
        /* The header has to fit in the first page; otherwise the blob at
         * page_size would overlap it. */
        if (declared_header_size > page_size) goto fail;
        parsed->header_size = (uint32_t)declared_header_size;
        sizes[0] = (int64_t)xx_data_get_u32(
            header, header_read, XX_ANDROIDBOOT_OFF_V0_KERNEL_SIZE, false);
        sizes[1] = (int64_t)xx_data_get_u32(
            header, header_read, XX_ANDROIDBOOT_OFF_V0_RAMDISK_SIZE, false);
        sizes[2] = (int64_t)xx_data_get_u32(
            header, header_read, XX_ANDROIDBOOT_OFF_V0_SECOND_SIZE, false);
        sizes[3] = recovery_dtbo_size;
        sizes[4] = dtb_size;
        /* Only a v0-layout header can carry the Qualcomm table, and only
         * v1/v2 carry sizes[3..4], so the dt never shares a layout with
         * them and its slot after them is also its place after second. */
        sizes[5] = qcom_dt_size;
        position = page_size;
        /* The recovery dtbo carries its own offset, measured from the start
         * of the boot image (not of the device); it has to agree with where
         * the page layout puts it. Placing the first three blobs separately
         * is what makes that comparison possible. */
        if (!xx_androidboot_place_blobs(parsed, self->base_address, sizes,
                                        names, 3U, page_size, &position)) {
            goto fail;
        }
        if (recovery_dtbo_size != 0 &&
            recovery_dtbo_offset != (uint64_t)position) {
            goto fail;
        }
        if (!xx_androidboot_place_blobs(parsed, self->base_address, sizes + 3,
                                        names + 3, 3U, page_size, &position)) {
            goto fail;
        }
    } else if (header_version == 3U || header_version == 4U) {
        int64_t sizes[3];
        static const char *const names[3] = {"kernel", "ramdisk", "signature"};
        int64_t expected = (header_version == 3U) ? XX_ANDROIDBOOT_HDR_V3_SIZE
                                                  : XX_ANDROIDBOOT_HDR_V4_SIZE;
        page_size = XX_ANDROIDBOOT_V3_PAGE_SIZE;
        if ((int64_t)header_read < expected ||
            (int64_t)xx_data_get_u32(header, header_read,
                                     XX_ANDROIDBOOT_OFF_V3_HEADER_SIZE,
                                     false) != expected) {
            goto fail;
        }
        parsed->header_size = (uint32_t)expected;
        sizes[0] = (int64_t)xx_data_get_u32(
            header, header_read, XX_ANDROIDBOOT_OFF_V3_KERNEL_SIZE, false);
        sizes[1] = (int64_t)xx_data_get_u32(
            header, header_read, XX_ANDROIDBOOT_OFF_V3_RAMDISK_SIZE, false);
        sizes[2] = (header_version == 4U)
                       ? (int64_t)xx_data_get_u32(
                             header, header_read,
                             XX_ANDROIDBOOT_OFF_V4_SIGNATURE_SIZE, false)
                       : 0;
        position = page_size;
        if (!xx_androidboot_place_blobs(parsed, self->base_address, sizes,
                                        names, 3U, page_size, &position)) {
            goto fail;
        }
    } else {
        /* Unreachable: anything past 4 was folded into the Qualcomm v0
         * layout above. Kept so a future edit cannot fall through. */
        goto fail;
    }

    parsed->page_size = (uint32_t)page_size;
    /* A header describing nothing at all is indistinguishable from a random
     * eight-byte match, so at least one payload blob is required. */
    if (parsed->count == 0U || (pd && xx_pd_is_stopped(pd))) goto fail;
    if (!xx_androidboot_build_cmdline(header, header_read, header_version,
                                      &cmdline, &cmdline_size)) {
        goto fail;
    }
    if (!xx_androidboot_append_synth(
            parsed, "cmdline.txt",
            self->base_address + ((header_version <= 2U)
                                      ? XX_ANDROIDBOOT_OFF_V0_CMDLINE
                                      : XX_ANDROIDBOOT_OFF_V3_CMDLINE),
            cmdline, cmdline_size)) {
        goto fail;
    }
    cmdline = NULL;
    if (position > INT64_MAX - self->base_address) goto fail;
    parsed->archive_end = self->base_address + position;
    if (parsed->archive_end > total_size) parsed->archive_end = total_size;
    return true;
fail:
    if (cmdline) xx_mem_free(cmdline);
    xx_androidboot_private_cleanup(parsed);
    return false;
}

static bool xx_androidboot_copy_options(xx_list_s *destination,
                                        const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_androidboot_find_option(const xx_list_s *options,
                                                uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_androidboot_populate_record(
    xx_archive_record *record, const xx_androidboot_entry *entry) {
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = entry->header_size;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static void xx_androidboot_archive_stream_free(void *pointer) {
    xx_androidboot_archive_stream *stream =
        (xx_androidboot_archive_stream *)pointer;
    if (!stream) return;
    xx_androidboot_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* Extraction-time check: the name must stay inside the destination tree on
 * every host this library builds for. The names are chosen by this file, not
 * by the image, so this only guards against a future edit here. */
static bool xx_androidboot_safe_name(const char *name) {
    size_t index;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    for (index = 0U; name[index] != '\0'; ++index) {
        unsigned char ch = (unsigned char)name[index];
        if (ch < 32U || ch == ':' || ch == '<' || ch == '>' || ch == '"' ||
            ch == '|' || ch == '?' || ch == '*' || ch == '/' || ch == '\\') {
            return false;
        }
    }
    return true;
}

static bool xx_androidboot_write_blob(const char *path, const uint8_t *data,
                                      size_t size) {
    xx_io_device *output;
    size_t done = 0U;
    bool result;
    if (!path || (!data && size != 0U)) return false;
    output = xx_io_file_open(path, "wb");
    result = output != NULL;
    while (result && done < size) {
        ssize_t sent = xx_io_write(output, data + done, size - done);
        if (sent <= 0 || (size_t)sent > size - done) {
            result = false;
            break;
        }
        done += (size_t)sent;
    }
    if (output && xx_io_close(output) != 0) result = false;
    return result;
}

void xx_androidboot_init(xx_androidboot *image, xx_io_device *dev,
                         int64_t base_address) {
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    xx_format_init(&image->format, dev, base_address);
    image->format.endian = XX_ENDIAN_LITTLE;
    image->format.file_type = XX_ANDROIDBOOT_FILE_TYPE;
    image->format.format_type = XX_TYPE_ARCHIVE;
    image->format.is_archive = true;
    xx_format_set_mime_type(&image->format, "application/x-android-boot-image");
    xx_format_set_extension(&image->format, "img");
    image->format.check_is_valid = xx_androidboot_check_is_valid;
    image->format.handle_base_info = xx_androidboot_handle_base_info;
    image->format.get_format_size = xx_androidboot_get_format_size;
    image->format.get_number_of_archive_records =
        xx_androidboot_get_number_of_archive_records;
    image->format.create_archive_records_reading =
        xx_androidboot_create_archive_records_reading;
    image->format.get_current_archive_record =
        xx_androidboot_get_current_archive_record;
    image->format.unpack_current_archive_record =
        xx_androidboot_unpack_current_archive_record;
    image->format.archive_record_move_to_next =
        xx_androidboot_archive_record_move_to_next;
    image->format.free_archive_records_reading =
        xx_androidboot_free_archive_records_reading;
    image->format.destroy = xx_androidboot_vtable_destroy;
    image->archive_end = -1;
}

xx_androidboot *xx_androidboot_create(xx_io_device *dev,
                                      int64_t base_address) {
    xx_androidboot *image = (xx_androidboot *)xx_mem_alloc(sizeof(*image));
    if (image) xx_androidboot_init(image, dev, base_address);
    return image;
}

void xx_androidboot_destroy(xx_androidboot *image) {
    if (!image) return;
    if (image->internal) {
        xx_androidboot_private_cleanup(
            (xx_androidboot_private *)image->internal);
        xx_mem_free(image->internal);
        image->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&image->format);
}

static void xx_androidboot_vtable_destroy(Abstractformat *self) {
    xx_androidboot_destroy((xx_androidboot *)self);
}

void xx_androidboot_free(xx_androidboot *image) {
    if (!image) return;
    xx_androidboot_destroy(image);
    xx_mem_free(image);
}

bool xx_androidboot_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_androidboot_private parsed;
    bool result = xx_androidboot_parse(self, &parsed, pd);
    xx_androidboot_private_cleanup(&parsed);
    return result;
}

bool xx_androidboot_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_androidboot_private *parsed;
    xx_androidboot *image = (xx_androidboot *)self;
    int64_t total_size;
    char version_text[32];
    if (!self || !image) return false;
    parsed = (xx_androidboot_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_androidboot_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (image->internal) {
        xx_androidboot_private_cleanup(
            (xx_androidboot_private *)image->internal);
        xx_mem_free(image->internal);
    }
    image->internal = parsed;
    image->number_of_records = parsed->count;
    image->number_of_members = parsed->count;
    image->header_version = parsed->header_version;
    image->page_size = parsed->page_size;
    image->header_size = parsed->header_size;
    image->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    /* header_version is 0..4 here, so a single digit always fits. */
    version_text[0] = (char)('0' + (int)(parsed->header_version % 10U));
    version_text[1] = '\0';
    xx_format_set_version(self, version_text);
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_androidboot_get_format_size(Abstractformat *self,
                                       xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_androidboot_get_number_of_archive_records(Abstractformat *self,
                                                      xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_androidboot *)self)->number_of_records;
}

xx_archive_record_state *xx_androidboot_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_androidboot_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_androidboot_archive_stream *)xx_mem_calloc(1U,
                                                            sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_androidboot_copy_options(&state->options, options) ||
        !xx_androidboot_parse(self, &stream->parsed, pd)) {
        xx_androidboot_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_androidboot_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_androidboot_populate_record(&state->current_record,
                                       &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_androidboot_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_androidboot_archive_record_move_to_next(Abstractformat *self,
                                                xx_archive_record_state *state,
                                                xx_pd_struct *pd) {
    xx_androidboot_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_androidboot_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_androidboot_populate_record(
            &state->current_record,
            &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_androidboot_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_androidboot_archive_stream *stream;
    const xx_androidboot_entry *entry;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_androidboot_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    entry = &stream->parsed.entries[stream->index];
    if (!xx_androidboot_safe_name(entry->name)) return false;
    option = xx_androidboot_find_option(&state->options,
                                        XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: confirm the range is still readable and stop. */
        int64_t total = xx_io_total_size(self->device);
        if (entry->synthesized) return true;
        return xx_androidboot_range_within(total, entry->data_offset,
                                           entry->data_size);
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", entry->name);
    } else {
        destination = xx_str_concat(base, entry->name);
    }
    if (!destination) goto cleanup;
    if (!xx_store_create_dirs_a(destination, false)) {
        result = false;
    } else if (entry->synthesized) {
        result = xx_androidboot_write_blob(destination, entry->synthesized,
                                           (size_t)entry->data_size);
    } else {
        result = xx_store_unpack_device_to_file(self->device,
                                                entry->data_offset,
                                                entry->data_size, destination,
                                                pd);
    }
    if (!result) xx_rt_remove(destination);
    if (owned_base) xx_str_free(owned_base);
    xx_str_free(destination);
    return result;
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return false;
}

void xx_androidboot_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_androidboot_get_number_of_records(const xx_androidboot *image) {
    return image ? image->number_of_records : 0U;
}
uint64_t xx_androidboot_get_number_of_members(const xx_androidboot *image) {
    return image ? image->number_of_members : 0U;
}
uint32_t xx_androidboot_get_header_version(const xx_androidboot *image) {
    return image ? image->header_version : 0U;
}
uint32_t xx_androidboot_get_page_size(const xx_androidboot *image) {
    return image ? image->page_size : 0U;
}
uint32_t xx_androidboot_get_header_size(const xx_androidboot *image) {
    return image ? image->header_size : 0U;
}
int64_t xx_androidboot_get_archive_end(const xx_androidboot *image) {
    return image ? image->archive_end : -1;
}
