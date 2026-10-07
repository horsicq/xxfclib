/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "ITOLITLS" storage: Microsoft Help 2 (.HxS and its .HxI/.HxR/.HxQ/.HxW
 * companions) and Microsoft Reader e-books (.lit).  xx_hxs.h carries the
 * layout.
 *
 * Written from the format's structure.  7-Zip's Chm handler (LGPL, its Hxs
 * path) was read for understanding only; no code comes from it.  Everything
 * behind the directory (entries, the NameList, LZXC control data, reset
 * tables, member names, duplicate handling and the reset-group cache) has
 * the same shape as in CHM, so that part follows this library's own CHM
 * reader (src/formats/chm/xx_chm.c, MIT).  LZX frames are decoded by this
 * library's xx_lzx_cab_decode, one reset group per call.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/hxs/xx_hxs.h"

#include "xxfclib/algo/lzx/xx_lzx.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder: xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * the real file type is picked up as soon as the format is registered. */
#ifdef HXS
#define XX_HXS_FILE_TYPE XX_FILE_TYPE_HXS
#else
#define XX_HXS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define HXS_HEADER 0x28U
#define HXS_HEADER_SECTIONS 5U
#define HXS_SECTION_TABLE (HXS_HEADER_SECTIONS * 16U)
/* Post-header: directory fields up to the CAOL block at +0x98, then the
 * 0x50-byte CAOL block whose last 0x20 bytes are an "ITSF" block. */
#define HXS_CAOL_AT 0x98U
#define HXS_CAOL_SIZE 0x50U
#define HXS_ITSF_AT 0x30U
#define HXS_POST_MIN (HXS_CAOL_AT + HXS_CAOL_SIZE)
#define HXS_POST_MAX 0x1000U
#define HXS_SECTION0_MIN 0x18U
#define HXS_SECTION0_MAGIC 0x1FEU
#define HXS_IFCM_SIZE 0x20U
#define HXS_AOLL_HEADER 48U
#define HXS_MIN_CHUNK 64U
#define HXS_MAX_CHUNK 0x100000U
#define HXS_MAX_CHUNKS 0x100000U
#define HXS_MAX_DIRECTORY (INT64_C(64) << 20)
#define HXS_MAX_ENTRIES 0x100000U
#define HXS_MAX_NAME 8192U
#define HXS_MAX_SECTIONS 32U
#define HXS_MAX_SECTION_NAME 64U
#define HXS_FRAME 32768U
#define HXS_MAX_FRAMES 0x100000U
/* Reset table header, entries behind it. */
#define HXS_RT_HEADER 40U
#define HXS_MAX_META 0x10000U
/* One decoded reset group (or its needed prefix) and its packed bytes; the
 * packed cap leaves room for the 16-byte header of every stored LZX frame. */
#define HXS_MAX_GROUP (32U << 20)
#define HXS_MAX_PACKED (HXS_MAX_GROUP + (HXS_MAX_GROUP >> 4U))
#define HXS_WINDOW_MIN 15U
#define HXS_WINDOW_MAX 21U
#define HXS_NAME_CHARS 240U
#define HXS_DEDUP_TRIES 32U
#define HXS_SSIZE_LIMIT (((size_t)-1) >> 1U)

#define HXS_METHOD_STORE 0U
#define HXS_METHOD_LZX 3U

enum { HXS_KIND_NONE = 0, HXS_KIND_STORED = 1, HXS_KIND_LZX = 2 };

/* {0A9007C1-4076-11D3-8789-0000F8105754}, right behind the header fields. */
static const uint8_t hxs_header_guid[16] = {
    0xC1, 0x07, 0x90, 0x0A, 0x76, 0x40, 0xD3, 0x11,
    0x87, 0x89, 0x00, 0x00, 0xF8, 0x10, 0x57, 0x54};

/* The LZX transform: Help 2 / Reader {0A9007C6-...}, HTML Help's
 * {7FC28940-...} (accepted too).  A section whose Transform/List holds
 * anything else (the DES transform of DRM-protected .lit books, or a chain
 * of several transforms) is listed but not decoded. */
typedef struct hxs_transform_s {
    uint8_t guid[16];
    const char *text;
} hxs_transform;

static const hxs_transform hxs_lzx_transforms[] = {
    {{0xC6, 0x07, 0x90, 0x0A, 0x76, 0x40, 0xD3, 0x11, 0x87, 0x89, 0x00, 0x00,
      0xF8, 0x10, 0x57, 0x54},
     "{0A9007C6-4076-11D3-8789-0000F8105754}"},
    {{0x40, 0x89, 0xC2, 0x7F, 0x31, 0x9D, 0xD0, 0x11, 0x9B, 0x27, 0x00, 0xA0,
      0xC9, 0x1E, 0x9C, 0x7C},
     "{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}"},
};

typedef struct hxs_entry_s {
    size_t name_offset;
    uint32_t name_length;
    uint64_t section;
    uint64_t offset;
    uint64_t size;
} hxs_entry;

typedef struct hxs_section_s {
    uint8_t kind;
    int64_t data_offset;   /* Content, from base */
    uint64_t data_size;
    uint64_t span;         /* uncompressed bytes */
    uint64_t packed;       /* compressed bytes the frames use */
    uint64_t frames;
    uint32_t reset_frames;
    uint32_t window_bits;
    uint64_t *resets;
    uint64_t reset_count;
} hxs_section;

typedef struct hxs_record_s {
    size_t entry;
    char *name;
    bool folder;
    bool extractable;
} hxs_record;

typedef struct hxs_context_s {
    int64_t total;          /* bytes from base to the device end */
    uint32_t post_size;     /* post-header length */
    uint64_t expected_entries;
    int64_t directory_offset;
    int64_t directory_size;
    int64_t content_offset;
    int64_t format_size;
    uint64_t declared_size;
    uint32_t chunk_size;
    uint32_t chunk_count;
    uint32_t listing_chunks;
    hxs_entry *entries;
    size_t entry_count;
    size_t entry_capacity;
    uint8_t *pool;
    size_t pool_size;
    size_t pool_capacity;
    hxs_section sections[HXS_MAX_SECTIONS];
    uint32_t section_count;
    uint32_t lzx_sections;
    /* Reset-table entries held by all sections together: sections may all
     * point at the same table, so the cap is shared, not per section. */
    uint64_t reset_entries;
    hxs_record *records;
    size_t record_count;
    uint64_t folders;
    uint64_t unsupported;
    /* Case-insensitive set of published names: slot = record index + 1. */
    uint32_t *slots;
    size_t mask;
} hxs_context;

#ifndef HXS_CACHE_SLOTS
#define HXS_CACHE_SLOTS 2U
#endif

typedef struct hxs_cache_slot_s {
    uint8_t *data;
    size_t capacity;
    uint32_t section;
    uint64_t group;
    uint64_t frames;
    uint64_t used;          /* cache_clock at the last use */
    bool valid;
} hxs_cache_slot;

typedef struct hxs_stream_s {
    hxs_context context;
    size_t index;
    uint64_t max_member;
    /* The two most recently used decoded reset groups (or their first
     * frames): a member straddling a group boundary needs two groups, and
     * the next member usually needs the same two. */
    hxs_cache_slot cache[HXS_CACHE_SLOTS];
    uint64_t cache_clock;
} hxs_stream;

typedef struct hxs_sink_s {
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
} hxs_sink;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */

#include "xxfclib/global/xx_global.h"
static size_t gb_hxs_capacity(void) {
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}
static ssize_t gb_hxs_read(xx_io_device *device, void *buffer, size_t size, size_t capacity) {
    size_t done = 0;
    if (size > (SIZE_MAX >> 1)) return -1;
    while (done < size) {
        size_t take = size - done;
        ssize_t n;
        if (take > capacity) take = capacity;
        n = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (n < 0 || (size_t)n > take) return -1;
        if (!n) break;
        done += (size_t)n;
    }
    return (ssize_t)done;
}
static ssize_t gb_hxs_write(xx_io_device *device, const void *buffer, size_t size, size_t capacity) {
    size_t done = 0;
    if (size > (SIZE_MAX >> 1)) return -1;
    while (done < size) {
        size_t take = size - done;
        ssize_t n;
        if (take > capacity) take = capacity;
        n = xx_io_write(device, (const uint8_t *)buffer + done, take);
        if (n < 0 || (size_t)n > take) return -1;
        if (!n) break;
        done += (size_t)n;
    }
    return (ssize_t)done;
}


static uint32_t hxs_le16(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U);
}

static bool hxs_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    const size_t file_io_capacity = gb_hxs_capacity();
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = gb_hxs_read(device, (uint8_t *)buffer + done,
                                    size - done, file_io_capacity);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* True when [offset, offset + size) lies inside [0, limit). */
static bool hxs_within(uint64_t limit, uint64_t offset, uint64_t size) {
    return offset <= limit && size <= limit - offset;
}

/* Big-endian base-128, at most 9 bytes (63 bits). */
static bool hxs_encint(const uint8_t *data, size_t end, size_t *position,
                       uint64_t *value) {
    uint64_t result = 0U;
    unsigned index;
    for (index = 0U; index < 9U; ++index) {
        uint8_t byte;
        if (*position >= end) return false;
        byte = data[(*position)++];
        result = (result << 7U) | (uint64_t)(byte & 0x7FU);
        if ((byte & 0x80U) == 0U) {
            *value = result;
            return true;
        }
    }
    return false;
}

static int hxs_log2(uint32_t value) {
    int bits = 0;
    if (value == 0U || (value & (value - 1U)) != 0U) return -1;
    while (value > 1U) {
        value >>= 1U;
        ++bits;
    }
    return bits;
}

/* ---------------------------------------------------------------------- */
/* Context                                                                 */

static void hxs_context_free(hxs_context *context) {
    size_t index;
    if (!context) return;
    if (context->entries) xx_mem_free(context->entries);
    if (context->pool) xx_mem_free(context->pool);
    for (index = 0U; index < HXS_MAX_SECTIONS; ++index)
        if (context->sections[index].resets)
            xx_mem_free(context->sections[index].resets);
    if (context->records) {
        for (index = 0U; index < context->record_count; ++index)
            if (context->records[index].name)
                xx_mem_free(context->records[index].name);
        xx_mem_free(context->records);
    }
    if (context->slots) xx_mem_free(context->slots);
    xx_mem_zero(context, sizeof(*context));
}

static bool hxs_add_entry(hxs_context *context, const uint8_t *name,
                          uint32_t name_length, uint64_t section,
                          uint64_t offset, uint64_t size) {
    hxs_entry *entry;
    if (context->entry_count >= HXS_MAX_ENTRIES) return false;
    if (context->entry_count == context->entry_capacity) {
        size_t capacity = context->entry_capacity ? context->entry_capacity * 2U
                                                  : 256U;
        hxs_entry *grown = (hxs_entry *)xx_mem_realloc(
            context->entries, capacity * sizeof(*grown));
        if (!grown) return false;
        context->entries = grown;
        context->entry_capacity = capacity;
    }
    if (name_length > context->pool_capacity - context->pool_size) {
        size_t capacity = context->pool_capacity ? context->pool_capacity : 4096U;
        uint8_t *grown;
        while (capacity - context->pool_size < name_length) {
            if (capacity > ((size_t)HXS_MAX_DIRECTORY) * 2U) return false;
            capacity *= 2U;
        }
        grown = (uint8_t *)xx_mem_realloc(context->pool, capacity);
        if (!grown) return false;
        context->pool = grown;
        context->pool_capacity = capacity;
    }
    xx_rt_memcpy(context->pool + context->pool_size, name, name_length);
    entry = &context->entries[context->entry_count++];
    entry->name_offset = context->pool_size;
    entry->name_length = name_length;
    entry->section = section;
    entry->offset = offset;
    entry->size = size;
    context->pool_size += name_length;
    return true;
}

/* One AOLL chunk: entries from byte 48 up to (chunk size - quickref
 * length), and a closing entry count that must match. */
static bool hxs_parse_listing(hxs_context *context, const uint8_t *chunk,
                              uint32_t chunk_size) {
    uint32_t quickref = xx_data_get_u32(chunk + 4U, 4, 0, false), count = 0U;
    size_t position = HXS_AOLL_HEADER, end;
    if (quickref < 2U || quickref > chunk_size - HXS_AOLL_HEADER) return false;
    end = chunk_size - quickref;
    while (position < end) {
        uint64_t length, section, offset, size;
        if (!hxs_encint(chunk, end, &position, &length) || length == 0U ||
            length > HXS_MAX_NAME || length > end - position)
            return false;
        {
            const uint8_t *name = chunk + position;
            position += (size_t)length;
            if (!hxs_encint(chunk, end, &position, &section) ||
                !hxs_encint(chunk, end, &position, &offset) ||
                !hxs_encint(chunk, end, &position, &size) ||
                !hxs_add_entry(context, name, (uint32_t)length, section, offset,
                               size))
                return false;
        }
        ++count;
    }
    return hxs_le16(chunk + chunk_size - 2U) == (count & 0xFFFFU);
}

/* Header, post-header, header section 0 and the whole directory. */
static bool hxs_parse_directory(Abstractformat *format, hxs_context *context,
                                xx_pd_struct *pd) {
    uint8_t header[HXS_HEADER + HXS_SECTION_TABLE];
    uint8_t post[HXS_POST_MIN];
    uint8_t section0[HXS_SECTION0_MIN];
    uint8_t ifcm[HXS_IFCM_SIZE];
    uint8_t *chunk = NULL;
    const uint8_t *caol, *itsf;
    int64_t total, end;
    uint64_t offsets[HXS_HEADER_SECTIONS], sizes[HXS_HEADER_SECTIONS], value;
    uint32_t index;
    bool result = false;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    total -= format->base_address;
    if (total < (int64_t)(sizeof(header) + HXS_POST_MIN) ||
        !hxs_read_at(format->device, format->base_address, header,
                     sizeof(header)))
        return false;
    if (xx_rt_memcmp(header, "ITOLITLS", 8U) != 0 ||
        xx_data_get_u32(header + 8U, 4, 0, false) != 1U || xx_data_get_u32(header + 12U, 4, 0, false) != HXS_HEADER ||
        xx_data_get_u32(header + 16U, 4, 0, false) != HXS_HEADER_SECTIONS ||
        xx_rt_memcmp(header + 24U, hxs_header_guid, 16U) != 0)
        return false;
    context->total = total;
    context->post_size = xx_data_get_u32(header + 20U, 4, 0, false);
    if (context->post_size < HXS_POST_MIN || context->post_size > HXS_POST_MAX)
        return false;
    for (index = 0U; index < HXS_HEADER_SECTIONS; ++index) {
        offsets[index] = xx_data_get_u64(header + HXS_HEADER + index * 16U, 8, 0, false);
        sizes[index] = xx_data_get_u64(header + HXS_HEADER + index * 16U + 8U, 8, 0, false);
        if (!hxs_within((uint64_t)total, offsets[index], sizes[index]))
            return false;
    }
    /* Post-header: version 2, CAOL at +0x98, the directory entry count at
     * +0x40; CAOL version 2, length 0x50, then ITSF version 4, length 0x20,
     * with the offset of content section 0. */
    if (!hxs_read_at(format->device,
                     format->base_address + (int64_t)sizeof(header), post,
                     sizeof(post)) ||
        xx_data_get_u32(post, 4, 0, false) != 2U || xx_data_get_u32(post + 4U, 4, 0, false) != HXS_CAOL_AT)
        return false;
    context->expected_entries = xx_data_get_u64(post + 0x40U, 8, 0, false);
    caol = post + HXS_CAOL_AT;
    itsf = caol + HXS_ITSF_AT;
    if (xx_rt_memcmp(caol, "CAOL", 4U) != 0 || xx_data_get_u32(caol + 4U, 4, 0, false) != 2U ||
        xx_data_get_u32(caol + 8U, 4, 0, false) != HXS_CAOL_SIZE ||
        xx_rt_memcmp(itsf, "ITSF", 4U) != 0 || xx_data_get_u32(itsf + 4U, 4, 0, false) != 4U ||
        xx_data_get_u32(itsf + 8U, 4, 0, false) != 0x20U || xx_data_get_u32(itsf + 12U, 4, 0, false) > 1U ||
        context->expected_entries == 0U ||
        context->expected_entries > HXS_MAX_ENTRIES)
        return false;
    value = xx_data_get_u64(itsf + 16U, 8, 0, false);
    if (value > (uint64_t)total) return false;
    context->content_offset = (int64_t)value;
    /* Header section 0: magic 0x1FE and the file size. */
    if (sizes[0] < HXS_SECTION0_MIN ||
        !hxs_read_at(format->device,
                     format->base_address + (int64_t)offsets[0], section0,
                     sizeof(section0)) ||
        xx_data_get_u32(section0, 4, 0, false) != HXS_SECTION0_MAGIC)
        return false;
    context->declared_size = xx_data_get_u64(section0 + 8U, 8, 0, false);
    /* Header section 1: the directory, an "IFCM" header (version 1, chunk
     * size, chunk count) and the chunks. */
    context->directory_offset = (int64_t)offsets[1];
    context->directory_size = (int64_t)sizes[1];
    if (sizes[1] < HXS_IFCM_SIZE || sizes[1] > (uint64_t)HXS_MAX_DIRECTORY ||
        !hxs_read_at(format->device,
                     format->base_address + context->directory_offset, ifcm,
                     sizeof(ifcm)) ||
        xx_rt_memcmp(ifcm, "IFCM", 4U) != 0 || xx_data_get_u32(ifcm + 4U, 4, 0, false) != 1U ||
        xx_data_get_u32(ifcm + 28U, 4, 0, false) != 0U)
        return false;
    context->chunk_size = xx_data_get_u32(ifcm + 8U, 4, 0, false);
    context->chunk_count = xx_data_get_u32(ifcm + 24U, 4, 0, false);
    if (context->chunk_size < HXS_MIN_CHUNK ||
        context->chunk_size > HXS_MAX_CHUNK || context->chunk_count == 0U ||
        context->chunk_count > HXS_MAX_CHUNKS ||
        (uint64_t)context->chunk_count * context->chunk_size >
            sizes[1] - HXS_IFCM_SIZE)
        return false;
    chunk = (uint8_t *)xx_mem_alloc(context->chunk_size);
    if (!chunk) return false;
    for (index = 0U; index < context->chunk_count; ++index) {
        int64_t at = format->base_address + context->directory_offset +
                     (int64_t)HXS_IFCM_SIZE +
                     (int64_t)index * (int64_t)context->chunk_size;
        if ((index & 0xFFU) == 0xFFU && pd && xx_pd_is_stopped(pd)) goto done;
        if (!hxs_read_at(format->device, at, chunk, context->chunk_size))
            goto done;
        if (xx_rt_memcmp(chunk, "AOLL", 4U) != 0) continue;
        if (!hxs_parse_listing(context, chunk, context->chunk_size)) goto done;
        ++context->listing_chunks;
    }
    if (context->listing_chunks == 0U ||
        (uint64_t)context->entry_count != context->expected_entries)
        goto done;
    /* The format ends at the furthest of the headers, the header sections
     * and the file size header section 0 records, but never past the
     * device. */
    end = (int64_t)(sizeof(header) + context->post_size) > total
              ? total
              : (int64_t)(sizeof(header) + context->post_size);
    for (index = 0U; index < HXS_HEADER_SECTIONS; ++index)
        if ((int64_t)(offsets[index] + sizes[index]) > end)
            end = (int64_t)(offsets[index] + sizes[index]);
    if (context->declared_size > (uint64_t)end)
        end = context->declared_size > (uint64_t)total
                  ? total
                  : (int64_t)context->declared_size;
    context->format_size = end;
    result = true;
done:
    xx_mem_free(chunk);
    return result;
}

/* First entry named exactly @p name (length @p length). */
static const hxs_entry *hxs_find(const hxs_context *context, const char *name,
                                 size_t length) {
    size_t index;
    for (index = 0U; index < context->entry_count; ++index) {
        const hxs_entry *entry = &context->entries[index];
        if (entry->name_length == length &&
            xx_rt_memcmp(context->pool + entry->name_offset, name, length) == 0)
            return entry;
    }
    return NULL;
}

/* A stored (section 0) metadata file, read whole. */
static uint8_t *hxs_read_stored(Abstractformat *format,
                                const hxs_context *context,
                                const hxs_entry *entry, uint64_t limit,
                                size_t *size) {
    uint8_t *data;
    if (!entry || entry->section != 0U || entry->size > limit ||
        context->content_offset > context->total ||
        !hxs_within((uint64_t)(context->total - context->content_offset),
                    entry->offset, entry->size))
        return NULL;
    data = (uint8_t *)xx_mem_alloc(entry->size ? (size_t)entry->size : 1U);
    if (!data) return NULL;
    if (!hxs_read_at(format->device,
                     format->base_address + context->content_offset +
                         (int64_t)entry->offset,
                     data, (size_t)entry->size)) {
        xx_mem_free(data);
        return NULL;
    }
    *size = (size_t)entry->size;
    return data;
}

/* "::DataSpace/Storage/<section>/<leaf>" */
static const hxs_entry *hxs_find_storage(const hxs_context *context,
                                         const char *section,
                                         const char *leaf) {
    char name[HXS_MAX_SECTION_NAME + 128U];
    int length = xx_rt_snprintf(name, sizeof(name),
                                "::DataSpace/Storage/%s/%s", section, leaf);
    if (length <= 0 || (size_t)length >= sizeof(name)) return NULL;
    return hxs_find(context, name, (size_t)length);
}

/* The section's Transform/List: exactly one GUID, and an LZX one. */
static const hxs_transform *hxs_section_transform(Abstractformat *format,
                                                  const hxs_context *context,
                                                  const char *name) {
    const hxs_entry *list = hxs_find_storage(context, name, "Transform/List");
    const hxs_transform *found = NULL;
    uint8_t *data;
    size_t size = 0U, index;
    data = hxs_read_stored(format, context, list, 16U, &size);
    if (!data) return NULL;
    if (size == 16U)
        for (index = 0U;
             index < sizeof(hxs_lzx_transforms) / sizeof(hxs_lzx_transforms[0]);
             ++index)
            if (xx_rt_memcmp(data, hxs_lzx_transforms[index].guid, 16U) == 0)
                found = &hxs_lzx_transforms[index];
    xx_mem_free(data);
    return found;
}

/* ControlData, reset table and Content of one LZX section; leaves the
 * section unusable (kind NONE) on any inconsistency. */
static void hxs_load_lzx(Abstractformat *format, hxs_context *context,
                         hxs_section *section, const char *name) {
    const hxs_transform *transform =
        hxs_section_transform(format, context, name);
    const hxs_entry *content = hxs_find_storage(context, name, "Content");
    const hxs_entry *control = hxs_find_storage(context, name, "ControlData");
    const hxs_entry *table = NULL;
    uint8_t *data = NULL;
    size_t size = 0U, index;
    int reset_bits, window_bits;
    uint64_t count, packed, span, frames;
    if (transform) {
        char leaf[96];
        int length = xx_rt_snprintf(leaf, sizeof(leaf),
                                    "Transform/%s/InstanceData/ResetTable",
                                    transform->text);
        if (length > 0 && (size_t)length < sizeof(leaf))
            table = hxs_find_storage(context, name, leaf);
    }
    if (!content || !control || !table || content->section != 0U ||
        content->offset > (uint64_t)INT64_MAX ||
        content->size > (uint64_t)INT64_MAX ||
        (uint64_t)context->content_offset >
            (uint64_t)INT64_MAX - content->offset)
        return;
    data = hxs_read_stored(format, context, control, HXS_MAX_META, &size);
    if (!data) return;
    if (size < 24U || xx_data_get_u32(data, 4, 0, false) < 5U ||
        xx_rt_memcmp(data + 4U, "LZXC", 4U) != 0 ||
        (xx_data_get_u32(data + 8U, 4, 0, false) != 2U && xx_data_get_u32(data + 8U, 4, 0, false) != 3U)) {
        xx_mem_free(data);
        return;
    }
    reset_bits = hxs_log2(xx_data_get_u32(data + 12U, 4, 0, false));
    window_bits = hxs_log2(xx_data_get_u32(data + 16U, 4, 0, false));
    xx_mem_free(data);
    if (reset_bits < 0 || reset_bits > 16 || window_bits < 0 ||
        window_bits > (int)(HXS_WINDOW_MAX - HXS_WINDOW_MIN))
        return;
    /* Read no more reset table than the shared budget can still take. */
    data = hxs_read_stored(
        format, context, table,
        HXS_RT_HEADER + 8U * ((uint64_t)(HXS_MAX_FRAMES + 2U) -
                              context->reset_entries),
        &size);
    if (!data) return;
    if (size == 0U) {
        /* An empty reset table (.chw files): nothing to decode. */
        count = packed = span = frames = 0U;
    } else {
        if (size < HXS_RT_HEADER ||
            (xx_data_get_u32(data, 4, 0, false) != 2U && xx_data_get_u32(data, 4, 0, false) != 3U) ||
            xx_data_get_u32(data + 8U, 4, 0, false) != 8U || xx_data_get_u32(data + 12U, 4, 0, false) != HXS_RT_HEADER ||
            xx_data_get_u64(data + 32U, 8, 0, false) != HXS_FRAME) {
            xx_mem_free(data);
            return;
        }
        count = xx_data_get_u32(data + 4U, 4, 0, false);
        span = xx_data_get_u64(data + 16U, 8, 0, false);
        packed = xx_data_get_u64(data + 24U, 8, 0, false);
        frames = span / HXS_FRAME + (span % HXS_FRAME ? 1U : 0U);
        if ((uint64_t)size != HXS_RT_HEADER + 8U * count || count < frames ||
            count > frames + 2U || frames > HXS_MAX_FRAMES ||
            packed > content->size ||
            count > (uint64_t)(HXS_MAX_FRAMES + 2U) - context->reset_entries) {
            xx_mem_free(data);
            return;
        }
    }
    if (count) {
        section->resets = (uint64_t *)xx_mem_alloc((size_t)count * 8U);
        if (!section->resets) {
            xx_mem_free(data);
            return;
        }
        for (index = 0U; index < (size_t)count; ++index) {
            uint64_t at = xx_data_get_u64(data + HXS_RT_HEADER + index * 8U, 8, 0, false);
            if ((index == 0U && at != 0U) || at > packed ||
                (index > 0U && at < section->resets[index - 1U])) {
                xx_mem_free(data);
                xx_mem_free(section->resets);
                section->resets = NULL;
                return;
            }
            section->resets[index] = at;
        }
    }
    xx_mem_free(data);
    section->reset_count = count;
    context->reset_entries += count;
    section->span = span;
    section->packed = packed;
    section->frames = frames;
    section->reset_frames = (uint32_t)1U << (unsigned)reset_bits;
    section->window_bits = HXS_WINDOW_MIN + (uint32_t)window_bits;
    section->data_offset = context->content_offset + (int64_t)content->offset;
    section->data_size = content->size;
    section->kind = HXS_KIND_LZX;
    ++context->lzx_sections;
}

/* NameList: u16 length, u16 count, then per section u16 length, UTF-16LE
 * name and a u16 terminator.  Section 0 is the stored one. */
static void hxs_load_sections(Abstractformat *format, hxs_context *context) {
    const char list_name[] = "::DataSpace/NameList";
    const hxs_entry *list = hxs_find(context, list_name, sizeof(list_name) - 1U);
    uint8_t *data;
    size_t size = 0U, position = 4U;
    uint32_t count, index;
    context->sections[0].kind = HXS_KIND_STORED;
    context->section_count = 1U;
    data = hxs_read_stored(format, context, list, HXS_MAX_META, &size);
    if (!data) return;
    if (size < 4U) goto done;
    count = hxs_le16(data + 2U);
    if (count > HXS_MAX_SECTIONS) count = HXS_MAX_SECTIONS;
    for (index = 0U; index < count; ++index) {
        char name[HXS_MAX_SECTION_NAME + 1U];
        uint32_t length, character;
        bool usable = true;
        if (size - position < 2U) break;
        length = hxs_le16(data + position);
        position += 2U;
        if (length > (size - position) / 2U ||
            (size - position) - (size_t)length * 2U < 2U)
            break;
        if (length == 0U || length > HXS_MAX_SECTION_NAME) usable = false;
        for (character = 0U; character < length; ++character) {
            uint32_t c = hxs_le16(data + position + character * 2U);
            if (c < 0x20U || c > 0x7EU || c == '/' || c == '\\') usable = false;
            if (usable) name[character] = (char)c;
        }
        position += (size_t)length * 2U;
        if (hxs_le16(data + position) != 0U) usable = false;
        position += 2U;
        if (index == 0U) continue;
        if (usable) {
            name[length] = 0;
            hxs_load_lzx(format, context, &context->sections[index], name);
        }
        context->section_count = index + 1U;
    }
done:
    xx_mem_free(data);
}

/* ---------------------------------------------------------------------- */
/* Member names                                                            */

static char hxs_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* One path component that is safe to create: no separators, drive colons
 * or wildcard/reserved punctuation, no control characters, no trailing dot
 * or space (so never "." or ".."), and not a Windows device name in any
 * case, with or without an extension.  @p name is UTF-8. */
static bool hxs_safe_component(const char *name, size_t length) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t index, stem = 0U, characters = 0U, device;
    if (!name || length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char c = (unsigned char)name[index];
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*')
            return false;
        /* C1 controls, U+0080..U+009F, are C2 80..C2 9F in UTF-8. */
        if (c == 0xC2U && index + 1U < length &&
            (unsigned char)name[index + 1U] >= 0x80U &&
            (unsigned char)name[index + 1U] <= 0x9FU)
            return false;
        if ((c & 0xC0U) != 0x80U) ++characters;
    }
    if (characters > HXS_NAME_CHARS || name[length - 1U] == '.' ||
        name[length - 1U] == ' ')
        return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (device = 0U; device < sizeof(devices) / sizeof(devices[0]); ++device) {
        const char *word = devices[device];
        size_t k = 0U;
        while (k < stem && word[k] && hxs_upper(name[k]) == word[k]) ++k;
        if (k == stem && word[k] == 0) return false;
    }
    if (stem >= 4U &&
        ((hxs_upper(name[0]) == 'C' && hxs_upper(name[1]) == 'O' &&
          hxs_upper(name[2]) == 'M') ||
         (hxs_upper(name[0]) == 'L' && hxs_upper(name[1]) == 'P' &&
          hxs_upper(name[2]) == 'T'))) {
        /* COM0..COM9, LPT0..LPT9 and the superscript-digit forms
         * (U+00B9, U+00B2, U+00B3, UTF-8 C2 B9 / C2 B2 / C2 B3). */
        if (stem == 4U && name[3] >= '0' && name[3] <= '9') return false;
        if (stem == 5U && (unsigned char)name[3] == 0xC2U &&
            ((unsigned char)name[4] == 0xB9U ||
             (unsigned char)name[4] == 0xB2U ||
             (unsigned char)name[4] == 0xB3U))
            return false;
    }
    return true;
}

/* A relative path of safe components separated by '/'. */
static bool hxs_safe_path(const char *path) {
    size_t start = 0U, index = 0U;
    if (!path || !path[0]) return false;
    for (;;) {
        if (path[index] == '/' || path[index] == 0) {
            if (!hxs_safe_component(path + start, index - start)) return false;
            if (path[index] == 0) return true;
            start = index + 1U;
        }
        ++index;
    }
}

/* Length of the UTF-8 sequence at @p data (well formed, shortest form, no
 * surrogates, at most U+10FFFF), 0 if there is none. */
static size_t hxs_utf8_sequence(const uint8_t *data, size_t available,
                                uint32_t *code) {
    uint8_t c = data[0];
    uint32_t value;
    size_t length, index;
    if (c < 0x80U) {
        *code = c;
        return 1U;
    }
    if (c >= 0xC2U && c <= 0xDFU) {
        length = 2U;
        value = c & 0x1FU;
    } else if (c >= 0xE0U && c <= 0xEFU) {
        length = 3U;
        value = c & 0x0FU;
    } else if (c >= 0xF0U && c <= 0xF4U) {
        length = 4U;
        value = c & 0x07U;
    } else {
        return 0U;
    }
    if (available < length) return 0U;
    for (index = 1U; index < length; ++index) {
        if ((data[index] & 0xC0U) != 0x80U) return 0U;
        value = (value << 6U) | (data[index] & 0x3FU);
    }
    if ((length == 3U && value < 0x800U) || (length == 4U && value < 0x10000U) ||
        value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU))
        return 0U;
    *code = value;
    return length;
}

/* The record name: the entry name without its leading '/' (and a folder's
 * trailing '/'), as UTF-8.  Names that are not UTF-8 are read as Latin-1;
 * NUL and other C0 controls become '_' so the text stays printable (such a
 * name is never extracted: @p safe is false). */
static char *hxs_decode_name(const uint8_t *raw, size_t length, bool *safe) {
    char *out;
    size_t used = 0U, index;
    bool utf8 = true, clean = true;
    uint32_t code;
    out = (char *)xx_mem_alloc(length * 2U + 1U);
    if (!out) return NULL;
    for (index = 0U; index < length;) {
        size_t step = hxs_utf8_sequence(raw + index, length - index, &code);
        if (step == 0U) {
            utf8 = false;
            break;
        }
        index += step;
    }
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c < 0x20U) {
            out[used++] = '_';
            clean = false;
        } else if (c < 0x80U || utf8) {
            out[used++] = (char)c;
        } else {
            out[used++] = (char)(0xC0U | (c >> 6U));
            out[used++] = (char)(0x80U | (c & 0x3FU));
        }
    }
    out[used] = 0;
    /* "~" plus a digit is how NTFS spells 8.3 short-name aliases
     * ("LONGFI~1.TXT"); such a member could open the file an earlier
     * member created under its long name, so the tilde becomes '_'. */
    for (index = 0U; index + 1U < used; ++index)
        if (out[index] == '~' && out[index + 1U] >= '0' &&
            out[index + 1U] <= '9')
            out[index] = '_';
    *safe = clean && hxs_safe_path(out);
    return out;
}

/* Windows compares file names case-insensitively through the volume's
 * $UpCase table.  Fold every code point to the smallest member of its case
 * class, where the classes join the Unicode simple upper- and lower-case
 * mappings (all planes) with the Windows NLS upper-case table: a superset
 * of the pairs an NTFS volume merges.  Folding too much only renames a member;
 * folding too little could let one overwrite another.  Each row maps
 * lo..hi, every step-th code point, to code point + delta; rows are sorted
 * and disjoint; they were derived from Unicode 15.1 and the Windows 11
 * RtlUpcaseUnicodeChar table. */
typedef struct hxs_fold_range_s {
    uint32_t lo;
    uint32_t hi;
    int32_t step;
    int32_t delta;
} hxs_fold_range;

static const hxs_fold_range hxs_fold_ranges[] = {
    {0x00061U, 0x0007AU, 1, -32},
    {0x000E0U, 0x000F6U, 1, -32},
    {0x000F8U, 0x000FEU, 1, -32},
    {0x00101U, 0x0012FU, 2, -1},
    {0x00131U, 0x00131U, 1, -232},
    {0x00133U, 0x00137U, 2, -1},
    {0x0013AU, 0x00148U, 2, -1},
    {0x0014BU, 0x00177U, 2, -1},
    {0x00178U, 0x00178U, 1, -121},
    {0x0017AU, 0x0017EU, 2, -1},
    {0x0017FU, 0x0017FU, 1, -300},
    {0x00183U, 0x00185U, 2, -1},
    {0x00188U, 0x00188U, 1, -1},
    {0x0018CU, 0x0018CU, 1, -1},
    {0x00192U, 0x00192U, 1, -1},
    {0x00199U, 0x00199U, 1, -1},
    {0x001A1U, 0x001A5U, 2, -1},
    {0x001A8U, 0x001A8U, 1, -1},
    {0x001ADU, 0x001ADU, 1, -1},
    {0x001B0U, 0x001B0U, 1, -1},
    {0x001B4U, 0x001B6U, 2, -1},
    {0x001B9U, 0x001B9U, 1, -1},
    {0x001BDU, 0x001BDU, 1, -1},
    {0x001C5U, 0x001C5U, 1, -1},
    {0x001C6U, 0x001C6U, 1, -2},
    {0x001C8U, 0x001C8U, 1, -1},
    {0x001C9U, 0x001C9U, 1, -2},
    {0x001CBU, 0x001CBU, 1, -1},
    {0x001CCU, 0x001CCU, 1, -2},
    {0x001CEU, 0x001DCU, 2, -1},
    {0x001DDU, 0x001DDU, 1, -79},
    {0x001DFU, 0x001EFU, 2, -1},
    {0x001F2U, 0x001F2U, 1, -1},
    {0x001F3U, 0x001F3U, 1, -2},
    {0x001F5U, 0x001F5U, 1, -1},
    {0x001F6U, 0x001F6U, 1, -97},
    {0x001F7U, 0x001F7U, 1, -56},
    {0x001F9U, 0x0021FU, 2, -1},
    {0x00220U, 0x00220U, 1, -130},
    {0x00223U, 0x00233U, 2, -1},
    {0x0023CU, 0x0023CU, 1, -1},
    {0x0023DU, 0x0023DU, 1, -163},
    {0x00242U, 0x00242U, 1, -1},
    {0x00243U, 0x00243U, 1, -195},
    {0x00247U, 0x0024FU, 2, -1},
    {0x00253U, 0x00253U, 1, -210},
    {0x00254U, 0x00254U, 1, -206},
    {0x00256U, 0x00257U, 1, -205},
    {0x00259U, 0x00259U, 1, -202},
    {0x0025BU, 0x0025BU, 1, -203},
    {0x00260U, 0x00260U, 1, -205},
    {0x00263U, 0x00263U, 1, -207},
    {0x00268U, 0x00268U, 1, -209},
    {0x00269U, 0x00269U, 1, -211},
    {0x0026FU, 0x0026FU, 1, -211},
    {0x00272U, 0x00272U, 1, -213},
    {0x00275U, 0x00275U, 1, -214},
    {0x00280U, 0x00280U, 1, -218},
    {0x00283U, 0x00283U, 1, -218},
    {0x00288U, 0x00288U, 1, -218},
    {0x00289U, 0x00289U, 1, -69},
    {0x0028AU, 0x0028BU, 1, -217},
    {0x0028CU, 0x0028CU, 1, -71},
    {0x00292U, 0x00292U, 1, -219},
    {0x00371U, 0x00373U, 2, -1},
    {0x00377U, 0x00377U, 1, -1},
    {0x00399U, 0x00399U, 1, -84},
    {0x0039CU, 0x0039CU, 1, -743},
    {0x003ACU, 0x003ACU, 1, -38},
    {0x003ADU, 0x003AFU, 1, -37},
    {0x003B1U, 0x003B8U, 1, -32},
    {0x003B9U, 0x003B9U, 1, -116},
    {0x003BAU, 0x003BBU, 1, -32},
    {0x003BCU, 0x003BCU, 1, -775},
    {0x003BDU, 0x003C1U, 1, -32},
    {0x003C2U, 0x003C2U, 1, -31},
    {0x003C3U, 0x003CBU, 1, -32},
    {0x003CCU, 0x003CCU, 1, -64},
    {0x003CDU, 0x003CEU, 1, -63},
    {0x003D0U, 0x003D0U, 1, -62},
    {0x003D1U, 0x003D1U, 1, -57},
    {0x003D5U, 0x003D5U, 1, -47},
    {0x003D6U, 0x003D6U, 1, -54},
    {0x003D7U, 0x003D7U, 1, -8},
    {0x003D9U, 0x003EFU, 2, -1},
    {0x003F0U, 0x003F0U, 1, -86},
    {0x003F1U, 0x003F1U, 1, -80},
    {0x003F3U, 0x003F3U, 1, -116},
    {0x003F4U, 0x003F4U, 1, -92},
    {0x003F5U, 0x003F5U, 1, -96},
    {0x003F8U, 0x003F8U, 1, -1},
    {0x003F9U, 0x003F9U, 1, -7},
    {0x003FBU, 0x003FBU, 1, -1},
    {0x003FDU, 0x003FFU, 1, -130},
    {0x00430U, 0x0044FU, 1, -32},
    {0x00450U, 0x0045FU, 1, -80},
    {0x00461U, 0x00481U, 2, -1},
    {0x0048BU, 0x004BFU, 2, -1},
    {0x004C2U, 0x004CEU, 2, -1},
    {0x004CFU, 0x004CFU, 1, -15},
    {0x004D1U, 0x0052FU, 2, -1},
    {0x00561U, 0x00586U, 1, -48},
    {0x013F8U, 0x013FDU, 1, -8},
    {0x01C80U, 0x01C80U, 1, -6254},
    {0x01C81U, 0x01C81U, 1, -6253},
    {0x01C82U, 0x01C82U, 1, -6244},
    {0x01C83U, 0x01C84U, 1, -6242},
    {0x01C85U, 0x01C85U, 1, -6243},
    {0x01C86U, 0x01C86U, 1, -6236},
    {0x01C87U, 0x01C87U, 1, -6181},
    {0x01C90U, 0x01CBAU, 1, -3008},
    {0x01CBDU, 0x01CBFU, 1, -3008},
    {0x01E01U, 0x01E95U, 2, -1},
    {0x01E9BU, 0x01E9BU, 1, -59},
    {0x01E9EU, 0x01E9EU, 1, -7615},
    {0x01EA1U, 0x01EFFU, 2, -1},
    {0x01F08U, 0x01F0FU, 1, -8},
    {0x01F18U, 0x01F1DU, 1, -8},
    {0x01F28U, 0x01F2FU, 1, -8},
    {0x01F38U, 0x01F3FU, 1, -8},
    {0x01F48U, 0x01F4DU, 1, -8},
    {0x01F59U, 0x01F5FU, 2, -8},
    {0x01F68U, 0x01F6FU, 1, -8},
    {0x01F88U, 0x01F8FU, 1, -8},
    {0x01F98U, 0x01F9FU, 1, -8},
    {0x01FA8U, 0x01FAFU, 1, -8},
    {0x01FB8U, 0x01FB9U, 1, -8},
    {0x01FBAU, 0x01FBBU, 1, -74},
    {0x01FBCU, 0x01FBCU, 1, -9},
    {0x01FBEU, 0x01FBEU, 1, -7289},
    {0x01FC8U, 0x01FCBU, 1, -86},
    {0x01FCCU, 0x01FCCU, 1, -9},
    {0x01FD8U, 0x01FD9U, 1, -8},
    {0x01FDAU, 0x01FDBU, 1, -100},
    {0x01FE8U, 0x01FE9U, 1, -8},
    {0x01FEAU, 0x01FEBU, 1, -112},
    {0x01FECU, 0x01FECU, 1, -7},
    {0x01FF8U, 0x01FF9U, 1, -128},
    {0x01FFAU, 0x01FFBU, 1, -126},
    {0x01FFCU, 0x01FFCU, 1, -9},
    {0x02126U, 0x02126U, 1, -7549},
    {0x0212AU, 0x0212AU, 1, -8415},
    {0x0212BU, 0x0212BU, 1, -8294},
    {0x0214EU, 0x0214EU, 1, -28},
    {0x02170U, 0x0217FU, 1, -16},
    {0x02184U, 0x02184U, 1, -1},
    {0x024D0U, 0x024E9U, 1, -26},
    {0x02C30U, 0x02C5FU, 1, -48},
    {0x02C61U, 0x02C61U, 1, -1},
    {0x02C62U, 0x02C62U, 1, -10743},
    {0x02C63U, 0x02C63U, 1, -3814},
    {0x02C64U, 0x02C64U, 1, -10727},
    {0x02C65U, 0x02C65U, 1, -10795},
    {0x02C66U, 0x02C66U, 1, -10792},
    {0x02C68U, 0x02C6CU, 2, -1},
    {0x02C6DU, 0x02C6DU, 1, -10780},
    {0x02C6EU, 0x02C6EU, 1, -10749},
    {0x02C6FU, 0x02C6FU, 1, -10783},
    {0x02C70U, 0x02C70U, 1, -10782},
    {0x02C73U, 0x02C73U, 1, -1},
    {0x02C76U, 0x02C76U, 1, -1},
    {0x02C7EU, 0x02C7FU, 1, -10815},
    {0x02C81U, 0x02CE3U, 2, -1},
    {0x02CECU, 0x02CEEU, 2, -1},
    {0x02CF3U, 0x02CF3U, 1, -1},
    {0x02D00U, 0x02D25U, 1, -7264},
    {0x02D27U, 0x02D27U, 1, -7264},
    {0x02D2DU, 0x02D2DU, 1, -7264},
    {0x0A641U, 0x0A649U, 2, -1},
    {0x0A64AU, 0x0A64AU, 1, -35266},
    {0x0A64BU, 0x0A64BU, 1, -35267},
    {0x0A64DU, 0x0A66DU, 2, -1},
    {0x0A681U, 0x0A69BU, 2, -1},
    {0x0A723U, 0x0A72FU, 2, -1},
    {0x0A733U, 0x0A76FU, 2, -1},
    {0x0A77AU, 0x0A77CU, 2, -1},
    {0x0A77DU, 0x0A77DU, 1, -35332},
    {0x0A77FU, 0x0A787U, 2, -1},
    {0x0A78CU, 0x0A78CU, 1, -1},
    {0x0A78DU, 0x0A78DU, 1, -42280},
    {0x0A791U, 0x0A793U, 2, -1},
    {0x0A797U, 0x0A7A9U, 2, -1},
    {0x0A7AAU, 0x0A7AAU, 1, -42308},
    {0x0A7ABU, 0x0A7ABU, 1, -42319},
    {0x0A7ACU, 0x0A7ACU, 1, -42315},
    {0x0A7ADU, 0x0A7ADU, 1, -42305},
    {0x0A7AEU, 0x0A7AEU, 1, -42308},
    {0x0A7B0U, 0x0A7B0U, 1, -42258},
    {0x0A7B1U, 0x0A7B1U, 1, -42282},
    {0x0A7B2U, 0x0A7B2U, 1, -42261},
    {0x0A7B5U, 0x0A7C3U, 2, -1},
    {0x0A7C4U, 0x0A7C4U, 1, -48},
    {0x0A7C5U, 0x0A7C5U, 1, -42307},
    {0x0A7C6U, 0x0A7C6U, 1, -35384},
    {0x0A7C8U, 0x0A7CAU, 2, -1},
    {0x0A7D1U, 0x0A7D1U, 1, -1},
    {0x0A7D7U, 0x0A7D9U, 2, -1},
    {0x0A7F6U, 0x0A7F6U, 1, -1},
    {0x0AB53U, 0x0AB53U, 1, -928},
    {0x0AB70U, 0x0ABBFU, 1, -38864},
    {0x0FF41U, 0x0FF5AU, 1, -32},
    {0x10428U, 0x1044FU, 1, -40},
    {0x104D8U, 0x104FBU, 1, -40},
    {0x10597U, 0x105A1U, 1, -39},
    {0x105A3U, 0x105B1U, 1, -39},
    {0x105B3U, 0x105B9U, 1, -39},
    {0x105BBU, 0x105BCU, 1, -39},
    {0x10CC0U, 0x10CF2U, 1, -64},
    {0x118C0U, 0x118DFU, 1, -32},
    {0x16E60U, 0x16E7FU, 1, -32},
    {0x1E922U, 0x1E943U, 1, -34},
};

static uint32_t hxs_fold(uint32_t c) {
    size_t low = 0U,
           high = sizeof(hxs_fold_ranges) / sizeof(hxs_fold_ranges[0]);
    if (c < 0x80U) return (c >= 'a' && c <= 'z') ? c - 0x20U : c;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        const hxs_fold_range *range = &hxs_fold_ranges[middle];
        if (c < range->lo) {
            high = middle;
        } else if (c > range->hi) {
            low = middle + 1U;
        } else {
            if ((c - range->lo) % (uint32_t)range->step != 0U) return c;
            return (uint32_t)((int64_t)c + range->delta);
        }
    }
    return c;
}

/* Next folded code point of a published (UTF-8) name, 0 at the end. */
static uint32_t hxs_next_folded(const char *name, size_t *position) {
    uint32_t code = 0U;
    size_t available = 0U, step;
    const uint8_t *p = (const uint8_t *)name + *position;
    if (!*p) return 0U;
    while (available < 4U && p[available]) ++available;
    step = hxs_utf8_sequence(p, available, &code);
    if (step == 0U) {
        code = *p;
        step = 1U;
    }
    *position += step;
    return hxs_fold(code);
}

static uint32_t hxs_name_hash(const char *name) {
    uint32_t hash = UINT32_C(2166136261), code;
    size_t position = 0U;
    while ((code = hxs_next_folded(name, &position)) != 0U) {
        hash ^= code;
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static bool hxs_same_name(const char *left, const char *right) {
    size_t a = 0U, b = 0U;
    for (;;) {
        uint32_t x = hxs_next_folded(left, &a), y = hxs_next_folded(right, &b);
        if (x != y) return false;
        if (x == 0U) return true;
    }
}

static bool hxs_name_taken(const hxs_context *context, const char *name) {
    size_t slot = hxs_name_hash(name) & context->mask, probes;
    for (probes = 0U; probes <= context->mask; ++probes) {
        uint32_t value = context->slots[slot];
        if (value == 0U) return false;
        if (context->records[value - 1U].extractable &&
            hxs_same_name(context->records[value - 1U].name, name))
            return true;
        slot = (slot + 1U) & context->mask;
    }
    return true;
}

static void hxs_name_insert(hxs_context *context, size_t index) {
    size_t slot = hxs_name_hash(context->records[index].name) & context->mask,
           probes;
    for (probes = 0U; probes <= context->mask; ++probes) {
        if (context->slots[slot] == 0U) {
            context->slots[slot] = (uint32_t)(index + 1U);
            return;
        }
        slot = (slot + 1U) & context->mask;
    }
}

/* "stem_NNNN.ext" (or "name_NNNN" without an extension in the last path
 * component), with a further counter after the first attempt. */
static void hxs_suffixed(char *out, size_t out_size, const char *base,
                         size_t ordinal, size_t attempt) {
    size_t length = xx_str_len(base), dot = length, index;
    for (index = length; index > 0U; --index) {
        if (base[index - 1U] == '/') break;
        if (base[index - 1U] == '.') {
            dot = index - 1U;
            break;
        }
    }
    if (dot == 0U || base[dot - 1U] == '/') dot = length;
    if (attempt == 0U)
        (void)xx_rt_snprintf(out, out_size, "%.*s_%04u%s", (int)dot, base,
                             (unsigned)ordinal, base + dot);
    else
        (void)xx_rt_snprintf(out, out_size, "%.*s_%04u_%u%s", (int)dot, base,
                             (unsigned)ordinal, (unsigned)attempt, base + dot);
}

/* Keep a safe name unique among the extractable ones: a name already taken
 * gets "_NNNN" (and a further counter if needed) before its extension; if
 * no free variant is found the record stays listed but is never written. */
static bool hxs_publish_name(hxs_context *context, size_t index) {
    hxs_record *record = &context->records[index];
    char *candidate;
    size_t length, attempt;
    if (!record->extractable) return true;
    if (!hxs_name_taken(context, record->name)) {
        hxs_name_insert(context, index);
        return true;
    }
    record->extractable = false;
    length = xx_str_len(record->name);
    candidate = (char *)xx_mem_alloc(length + 32U);
    if (!candidate) return false;
    for (attempt = 0U; attempt < HXS_DEDUP_TRIES; ++attempt) {
        hxs_suffixed(candidate, length + 32U, record->name, index + 1U,
                     attempt);
        if (hxs_safe_path(candidate) && !hxs_name_taken(context, candidate)) {
            xx_mem_free(record->name);
            record->name = candidate;
            record->extractable = true;
            hxs_name_insert(context, index);
            return true;
        }
    }
    xx_mem_free(candidate);
    return true;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

/* Folders first in directory order, then files by section and offset, so
 * that consecutive members share decoded LZX groups. */
static bool hxs_record_before(const hxs_context *context, const hxs_record *a,
                              const hxs_record *b) {
    const hxs_entry *x = &context->entries[a->entry];
    const hxs_entry *y = &context->entries[b->entry];
    if (a->folder != b->folder) return a->folder;
    if (!a->folder) {
        if (x->section != y->section) return x->section < y->section;
        if (x->offset != y->offset) return x->offset < y->offset;
        if (x->size != y->size) return x->size < y->size;
    }
    return a->entry < b->entry;
}

static bool hxs_sort_records(hxs_context *context) {
    size_t count = context->record_count, width, left;
    hxs_record *temp, *source = context->records, *target;
    if (count < 2U) return true;
    temp = (hxs_record *)xx_mem_alloc(count * sizeof(*temp));
    if (!temp) return false;
    target = temp;
    for (width = 1U; width < count; width *= 2U) {
        for (left = 0U; left < count; left += 2U * width) {
            size_t middle = left + width < count ? left + width : count;
            size_t right = middle + width < count ? middle + width : count;
            size_t i = left, j = middle, k = left;
            while (i < middle && j < right)
                target[k++] = hxs_record_before(context, &source[j], &source[i])
                                  ? source[j++]
                                  : source[i++];
            while (i < middle) target[k++] = source[i++];
            while (j < right) target[k++] = source[j++];
        }
        {
            hxs_record *swap = source;
            source = target;
            target = swap;
        }
    }
    if (source != context->records)
        xx_rt_memcpy(context->records, source, count * sizeof(*source));
    xx_mem_free(temp);
    return true;
}

static bool hxs_member_decodable(const hxs_context *context,
                                 const hxs_entry *entry) {
    const hxs_section *section;
    if (entry->section >= context->section_count) return false;
    section = &context->sections[entry->section];
    if (section->kind == HXS_KIND_STORED)
        return context->content_offset <= context->total &&
               hxs_within((uint64_t)(context->total - context->content_offset),
                          entry->offset, entry->size);
    if (section->kind == HXS_KIND_LZX)
        return hxs_within(section->span, entry->offset, entry->size);
    return false;
}

static bool hxs_build_records(hxs_context *context) {
    size_t index, count = 0U, slots;
    for (index = 0U; index < context->entry_count; ++index) {
        const hxs_entry *entry = &context->entries[index];
        if (entry->name_length > 1U && context->pool[entry->name_offset] == '/')
            ++count;
    }
    if (count == 0U) return true;
    context->records = (hxs_record *)xx_mem_calloc(count, sizeof(hxs_record));
    if (!context->records) return false;
    for (index = 0U; index < context->entry_count; ++index) {
        const hxs_entry *entry = &context->entries[index];
        const uint8_t *name = context->pool + entry->name_offset;
        hxs_record *record;
        size_t length = entry->name_length;
        bool safe = false;
        if (length < 2U || name[0] != '/') continue;
        record = &context->records[context->record_count++];
        record->entry = index;
        record->folder = name[length - 1U] == '/';
        ++name;
        --length;
        if (record->folder) --length;
        record->name = hxs_decode_name(name, length, &safe);
        if (!record->name) return false;
        record->extractable = safe;
        if (record->folder)
            ++context->folders;
        else if (!hxs_member_decodable(context, entry))
            ++context->unsupported;
    }
    if (!hxs_sort_records(context)) return false;
    for (slots = 16U; slots < context->record_count * 2U; slots *= 2U) {}
    context->slots = (uint32_t *)xx_mem_calloc(slots, sizeof(uint32_t));
    if (!context->slots) return false;
    context->mask = slots - 1U;
    for (index = 0U; index < context->record_count; ++index)
        if (!hxs_publish_name(context, index)) return false;
    return true;
}

static bool hxs_parse(Abstractformat *format, hxs_context *context,
                      bool records, xx_pd_struct *pd) {
    xx_mem_zero(context, sizeof(*context));
    if (!hxs_parse_directory(format, context, pd)) goto fail;
    if (records) {
        hxs_load_sections(format, context);
        if (!hxs_build_records(context)) goto fail;
    }
    return true;
fail:
    hxs_context_free(context);
    return false;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                              */

static bool hxs_sink_write(hxs_sink *sink, const uint8_t *data, size_t size) {
    const size_t file_io_capacity = gb_hxs_capacity();
    size_t done = 0U;
    if ((uint64_t)size > sink->limit - sink->written) return false;
    while (sink->target && done < size) {
        size_t chunk = size - done;
        ssize_t wrote;
        if (chunk > HXS_SSIZE_LIMIT) chunk = HXS_SSIZE_LIMIT;
        wrote = gb_hxs_write(sink->target, data + done, chunk, file_io_capacity);
        if (wrote <= 0 || (size_t)wrote > chunk) return false;
        done += (size_t)wrote;
    }
    sink->written += size;
    return true;
}

static bool hxs_copy(xx_io_device *source, int64_t offset, uint64_t size,
                     hxs_sink *sink, xx_pd_struct *pd) {
    const size_t file_io_capacity = gb_hxs_capacity();
    uint8_t *buffer;
    bool result = true;
    if (size > sink->limit) return false;
    buffer = (uint8_t *)xx_mem_alloc(file_io_capacity);
    if (!buffer) return false;
    while (size > 0U) {
        size_t chunk = size < file_io_capacity ? (size_t)size : file_io_capacity;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !hxs_read_at(source, offset, buffer, chunk) ||
            !hxs_sink_write(sink, buffer, chunk)) {
            result = false;
            break;
        }
        offset += (int64_t)chunk;
        size -= chunk;
    }
    xx_mem_free(buffer);
    return result;
}

/* Compressed end of frame @p frame. */
static uint64_t hxs_frame_end(const hxs_section *section, uint64_t frame) {
    return frame + 1U < section->reset_count ? section->resets[frame + 1U]
                                             : section->packed;
}

/* Make a cache slot hold at least the first @p need frames of reset group
 * @p group of section @p index and return its bytes in @p *out: the whole
 * group when it fits the cap, else a prefix that at least doubles the one
 * already cached, so members walking through an oversized group cost O(cap)
 * decoding in total, not O(cap) each.  A group not cached replaces the
 * least recently used slot. */
static bool hxs_cache_group(Abstractformat *format, hxs_stream *stream,
                            const hxs_section *section, uint32_t index,
                            uint64_t group, uint64_t need,
                            const uint8_t **out) {
    const uint64_t cap = HXS_MAX_GROUP / HXS_FRAME;
    uint64_t first = group * section->reset_frames, count, frame, start, stop;
    uint64_t cached = 0U;
    const uint8_t **blocks = NULL;
    size_t *block_sizes = NULL, *plain_sizes = NULL, written = 0U, output_size;
    size_t slot_index, victim = 0U;
    hxs_cache_slot *slot = NULL;
    uint8_t *packed = NULL;
    bool result = false;
    for (slot_index = 0U; slot_index < HXS_CACHE_SLOTS; ++slot_index) {
        hxs_cache_slot *candidate = &stream->cache[slot_index];
        if (candidate->valid && candidate->section == index &&
            candidate->group == group) {
            slot = candidate;
            break;
        }
        if (!candidate->valid || (stream->cache[victim].valid &&
                                  candidate->used < stream->cache[victim].used))
            victim = slot_index;
    }
    if (slot) {
        slot->used = ++stream->cache_clock;
        if (slot->frames >= need) {
            *out = slot->data;
            return true;
        }
        cached = slot->frames;
    } else {
        slot = &stream->cache[victim];
    }
    slot->valid = false;
    count = section->frames - first;
    if (count > section->reset_frames) count = section->reset_frames;
    if (count > cap) {
        uint64_t want = cached > need / 2U ? cached * 2U : need;
        count = want < cap ? want : cap;
    }
    if (need == 0U || need > count || count > cap) return false;
    start = section->resets[first];
    /* Reset offsets only grow, so trimming frames shrinks the packed span. */
    while (count > need &&
           hxs_frame_end(section, first + count - 1U) - start > HXS_MAX_PACKED)
        --count;
    stop = hxs_frame_end(section, first + count - 1U);
    /* The Content extent is checked against the device here rather than at
     * parse time, so a truncated file still yields its leading members. */
    if (stop <= start || stop - start > HXS_MAX_PACKED ||
        stop > section->data_size ||
        section->data_offset > stream->context.total ||
        stop > (uint64_t)(stream->context.total - section->data_offset))
        return false;
    output_size = (size_t)count * HXS_FRAME;
    if (slot->capacity < output_size) {
        uint8_t *grown = (uint8_t *)xx_mem_alloc(output_size);
        if (!grown) return false;
        if (slot->data) xx_mem_free(slot->data);
        slot->data = grown;
        slot->capacity = output_size;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)(stop - start));
    blocks = (const uint8_t **)xx_mem_alloc((size_t)count * sizeof(*blocks));
    block_sizes = (size_t *)xx_mem_alloc((size_t)count * sizeof(size_t));
    plain_sizes = (size_t *)xx_mem_alloc((size_t)count * sizeof(size_t));
    if (!packed || !blocks || !block_sizes || !plain_sizes ||
        !hxs_read_at(format->device,
                     format->base_address + section->data_offset +
                         (int64_t)start,
                     packed, (size_t)(stop - start)))
        goto done;
    for (frame = 0U; frame < count; ++frame) {
        uint64_t from = section->resets[first + frame];
        uint64_t to = hxs_frame_end(section, first + frame);
        if (to <= from) goto done;
        blocks[frame] = packed + (from - start);
        block_sizes[frame] = (size_t)(to - from);
        plain_sizes[frame] = HXS_FRAME;
    }
    if (!xx_lzx_cab_decode(blocks, block_sizes, plain_sizes, (size_t)count,
                           section->window_bits, slot->data, output_size,
                           &written) ||
        written != output_size) {
        /* A compiler may pad the section's last frame to a full 32 KiB or
         * end the stream on the span; retry the shorter shape once. */
        uint64_t tail = section->span % HXS_FRAME;
        if (first + count != section->frames || tail == 0U) goto done;
        plain_sizes[count - 1U] = (size_t)tail;
        output_size -= HXS_FRAME - (size_t)tail;
        if (!xx_lzx_cab_decode(blocks, block_sizes, plain_sizes, (size_t)count,
                               section->window_bits, slot->data,
                               output_size, &written) ||
            written != output_size)
            goto done;
    }
    slot->valid = true;
    slot->section = index;
    slot->group = group;
    slot->frames = count;
    slot->used = ++stream->cache_clock;
    *out = slot->data;
    result = true;
done:
    if (packed) xx_mem_free(packed);
    if (blocks) xx_mem_free((void *)blocks);
    if (block_sizes) xx_mem_free(block_sizes);
    if (plain_sizes) xx_mem_free(plain_sizes);
    return result;
}

static bool hxs_unpack_lzx(Abstractformat *format, hxs_stream *stream,
                           uint32_t index, const hxs_entry *entry,
                           hxs_sink *sink, xx_pd_struct *pd) {
    const hxs_section *section = &stream->context.sections[index];
    uint64_t first_frame, last_frame, group, last_group, end;
    if (entry->size == 0U) return true;
    if (!hxs_within(section->span, entry->offset, entry->size) ||
        entry->size > sink->limit)
        return false;
    end = entry->offset + entry->size;
    first_frame = entry->offset / HXS_FRAME;
    last_frame = (end - 1U) / HXS_FRAME;
    if (last_frame >= section->frames) return false;
    last_group = last_frame / section->reset_frames;
    for (group = first_frame / section->reset_frames; group <= last_group;
         ++group) {
        uint64_t base = group * section->reset_frames * (uint64_t)HXS_FRAME;
        uint64_t need, from, to;
        const uint8_t *data = NULL;
        if (pd && xx_pd_is_stopped(pd)) return false;
        need = (group == last_group ? last_frame + 1U
                                    : (group + 1U) * section->reset_frames) -
               group * section->reset_frames;
        if (!hxs_cache_group(format, stream, section, index, group, need,
                             &data))
            return false;
        from = entry->offset > base ? entry->offset : base;
        to = base + need * HXS_FRAME;
        if (to > end) to = end;
        if (to <= from ||
            !hxs_sink_write(sink, data + (size_t)(from - base),
                            (size_t)(to - from)))
            return false;
    }
    return sink->written == entry->size;
}

static bool hxs_unpack_record(Abstractformat *format, hxs_stream *stream,
                              const hxs_record *record, xx_io_device *target,
                              xx_pd_struct *pd) {
    const hxs_context *context = &stream->context;
    const hxs_entry *entry = &context->entries[record->entry];
    hxs_sink sink;
    if (record->folder) return true;
    if (!hxs_member_decodable(context, entry) || entry->size > stream->max_member)
        return false;
    sink.target = target;
    sink.written = 0U;
    sink.limit = entry->size;
    if (context->sections[entry->section].kind == HXS_KIND_STORED)
        return hxs_copy(format->device,
                        format->base_address + context->content_offset +
                            (int64_t)entry->offset,
                        entry->size, &sink, pd) &&
               sink.written == entry->size;
    return hxs_unpack_lzx(format, stream, (uint32_t)entry->section, entry,
                          &sink, pd);
}

/* ---------------------------------------------------------------------- */
/* Options and records                                                     */

static bool hxs_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

/* XX_META_ID_OPT_MAX_MEMBER_SIZE, when set to a non-negative integer. */
static uint64_t hxs_max_member(const Abstractformat *format,
                               const xx_list_s *options) {
    const xx_var *limit = xx_format_resolve_extra_parameter(
        format, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (!limit) return UINT64_MAX;
    switch (limit->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return xx_var_get_u64(limit);
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(limit);
            return value >= 0 ? (uint64_t)value : UINT64_MAX;
        }
        default: return UINT64_MAX;
    }
}

static bool hxs_set_record(xx_archive_record *out, const hxs_context *context,
                           const hxs_record *record, int64_t base) {
    const hxs_entry *entry = &context->entries[record->entry];
    bool stored = entry->section == 0U;
    xx_archive_record_cleanup(out);
    xx_archive_record_init(out);
    out->header_offset = -1;
    out->header_size = 0;
    out->data_offset = -1;
    out->compressed_size = -1;
    if (!record->folder && stored && hxs_member_decodable(context, entry)) {
        out->data_offset = base + context->content_offset + (int64_t)entry->offset;
        out->compressed_size = (int64_t)entry->size;
    }
    if (!xx_archive_record_set_original_name(out, record->name) ||
        !xx_archive_record_set_meta_bool(out, XX_META_ID_IS_FOLDER,
                                         record->folder) ||
        !xx_archive_record_set_meta_bool(out, XX_META_ID_IS_ENCRYPTED, false))
        return false;
    if (record->folder) return true;
    return xx_archive_record_set_meta_u64(out, XX_META_ID_UNCOMPRESSED_SIZE,
                                          entry->size) &&
           xx_archive_record_set_meta_u64(
               out, XX_META_ID_COMPRESSION_METHOD,
               stored ? HXS_METHOD_STORE : HXS_METHOD_LZX) &&
           (!stored || xx_archive_record_set_meta_u64(
                           out, XX_META_ID_COMPRESSED_SIZE, entry->size));
}

static void hxs_stream_free(void *opaque) {
    hxs_stream *stream = (hxs_stream *)opaque;
    size_t slot;
    if (!stream) return;
    hxs_context_free(&stream->context);
    for (slot = 0U; slot < HXS_CACHE_SLOTS; ++slot)
        if (stream->cache[slot].data) xx_mem_free(stream->cache[slot].data);
    xx_mem_free(stream);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_hxs_init(xx_hxs *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_HXS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.os = XX_OS_WINDOWS;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "hxs");
    archive->format.check_is_valid = xx_hxs_check_is_valid;
    archive->format.handle_base_info = xx_hxs_handle_base_info;
    archive->format.get_format_size = xx_hxs_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_hxs_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_hxs_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_hxs_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_hxs_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_hxs_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_hxs_free_archive_records_reading;
    archive->content_offset = -1;
    archive->declared_size = -1;
}

xx_hxs *xx_hxs_create(xx_io_device *device, int64_t base_address) {
    xx_hxs *archive = (xx_hxs *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_hxs_init(archive, device, base_address);
    return archive;
}

void xx_hxs_destroy(xx_hxs *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_hxs_free(xx_hxs *archive) {
    if (!archive) return;
    xx_hxs_destroy(archive);
    xx_mem_free(archive);
}

bool xx_hxs_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    hxs_context context;
    if (!hxs_parse(format, &context, false, pd)) return false;
    hxs_context_free(&context);
    return true;
}

bool xx_hxs_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    hxs_context context;
    xx_hxs *archive;
    if (!format || !hxs_parse(format, &context, true, pd)) return false;
    archive = (xx_hxs *)format;
    archive->number_of_records = (uint64_t)context.record_count;
    archive->sections = context.section_count;
    archive->lzx_sections = context.lzx_sections;
    archive->chunk_size = context.chunk_size;
    archive->listing_chunks = context.listing_chunks;
    archive->entries = (uint64_t)context.entry_count;
    archive->folders = context.folders;
    archive->unsupported = context.unsupported;
    archive->content_offset = context.content_offset;
    archive->declared_size = context.declared_size > (uint64_t)INT64_MAX
                                 ? INT64_MAX
                                 : (int64_t)context.declared_size;
    format->number_of_archive_records = archive->number_of_records;
    format->format_size = context.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    hxs_context_free(&context);
    return true;
}

int64_t xx_hxs_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_hxs_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_hxs_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_hxs_handle_base_info(format, pd))
               ? ((xx_hxs *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_hxs_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    hxs_stream *stream;
    xx_archive_record_state *state;
    if (!format) return NULL;
    stream = (hxs_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!hxs_parse(format, &stream->context, true, pd)) {
        xx_mem_free(stream);
        return NULL;
    }
    if (stream->context.record_count == 0U) {
        hxs_stream_free(stream);
        return NULL;
    }
    stream->max_member = hxs_max_member(format, options);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        hxs_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = hxs_stream_free;
    state->total_records = (int64_t)stream->context.record_count;
    if (!hxs_copy_options(&state->options, options) ||
        !hxs_set_record(&state->current_record, &stream->context,
                        &stream->context.records[0], format->base_address)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->current_index = 0;
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_hxs_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_hxs_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    hxs_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (hxs_stream *)state->internal_state) ||
        stream->index + 1U >= stream->context.record_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!hxs_set_record(&state->current_record, &stream->context,
                        &stream->context.records[stream->index],
                        format->base_address)) {
        state->has_record = false;
        return false;
    }
    state->current_index = (int64_t)stream->index;
    state->has_record = true;
    return true;
}

bool xx_hxs_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    hxs_stream *stream;
    const hxs_record *record;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (hxs_stream *)state->internal_state) ||
        stream->index >= stream->context.record_count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    record = &stream->context.records[stream->index];
    path_option = xx_format_resolve_extra_parameter(
        format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return hxs_unpack_record(format, stream, record, NULL, pd);
    if (!record->extractable || !hxs_safe_path(record->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", record->name)
               : xx_str_concat(base, record->name);
    if (!path) goto done;
    if (record->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = hxs_unpack_record(format, stream, record, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_hxs_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
