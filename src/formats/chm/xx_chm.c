/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft Compiled HTML Help (ITSF storage).  xx_chm.h carries the layout.
 *
 * Written from the format's structure.  7-Zip's Chm handler (LGPL) and
 * libmspack's chmd (LGPL) were read for understanding only; no code comes
 * from them.  The member-name safety and duplicate-name helpers follow the
 * ones of this library's own JGsoft DeployMaster reader.  LZX frames are
 * decoded by this library's xx_lzx_cab_decode: a CHM reset group has exactly
 * the shape of one CAB folder (a fresh LZX stream whose 32 KiB frames each
 * end on a 16-bit boundary), so each group is handed over as one call with
 * one block per frame.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/chm/xx_chm.h"

#include "xxfclib/algo/lzx/xx_lzx.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * the real file type is picked up as soon as the format is registered. */
#ifdef CHM

#define XX_CHM_FILE_TYPE XX_FILE_TYPE_CHM
#else
#define XX_CHM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define CHM_HEADER_V2 0x58U
#define CHM_HEADER_V3 0x60U
#define CHM_SECTION0_MIN 0x18U
#define CHM_SECTION0_MAGIC 0x1FEU
#define CHM_ITSP_SIZE 0x54U
#define CHM_PMGL_HEADER 20U
#define CHM_MIN_CHUNK 32U
#define CHM_MAX_CHUNK 0x100000U
#define CHM_MAX_CHUNKS 0x100000U
#define CHM_MAX_DIRECTORY (INT64_C(64) << 20)
#define CHM_MAX_ENTRIES 0x100000U
#define CHM_MAX_NAME 8192U
#define CHM_MAX_SECTIONS 32U
#define CHM_MAX_SECTION_NAME 64U
#define CHM_FRAME 32768U
#define CHM_MAX_FRAMES 0x100000U
/* Reset table header, entries behind it. */
#define CHM_RT_HEADER 40U
#define CHM_MAX_META 0x10000U
/* One decoded reset group (or its needed prefix) and its packed bytes; the
 * packed cap leaves room for the 16-byte header of every stored LZX frame. */
#define CHM_MAX_GROUP (32U << 20)
#define CHM_MAX_PACKED (CHM_MAX_GROUP + (CHM_MAX_GROUP >> 4U))
#define CHM_WINDOW_MIN 15U
#define CHM_WINDOW_MAX 21U
#define CHM_COPY_BUFFER 0x10000U
#define CHM_NAME_CHARS 240U
#define CHM_DEDUP_TRIES 32U
#define CHM_SSIZE_LIMIT (((size_t)-1) >> 1U)

#define CHM_METHOD_STORE 0U
#define CHM_METHOD_LZX 3U

enum { CHM_KIND_NONE = 0, CHM_KIND_STORED = 1, CHM_KIND_LZX = 2 };

static const char chm_rt_suffix[] =
    "Transform/{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/InstanceData/ResetTable";

typedef struct chm_entry_s {
    size_t name_offset;
    uint32_t name_length;
    uint64_t section;
    uint64_t offset;
    uint64_t size;
} chm_entry;

typedef struct chm_section_s {
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
} chm_section;

typedef struct chm_record_s {
    size_t entry;
    char *name;
    bool folder;
    bool extractable;
} chm_record;

typedef struct chm_context_s {
    int64_t total;          /* bytes from base to the device end */
    uint32_t version;
    uint32_t header_size;
    int64_t directory_offset;
    int64_t directory_size;
    int64_t content_offset;
    int64_t format_size;
    uint64_t declared_size;
    uint32_t chunk_size;
    uint32_t chunk_count;
    uint32_t listing_chunks;
    chm_entry *entries;
    size_t entry_count;
    size_t entry_capacity;
    uint8_t *pool;
    size_t pool_size;
    size_t pool_capacity;
    chm_section sections[CHM_MAX_SECTIONS];
    uint32_t section_count;
    uint32_t lzx_sections;
    /* Reset-table entries held by all sections together: sections may all
     * point at the same table, so the cap is shared, not per section. */
    uint64_t reset_entries;
    chm_record *records;
    size_t record_count;
    uint64_t folders;
    uint64_t unsupported;
    /* Case-insensitive set of published names: slot = record index + 1. */
    uint32_t *slots;
    size_t mask;
} chm_context;

#ifndef CHM_CACHE_SLOTS
#define CHM_CACHE_SLOTS 2U
#endif

typedef struct chm_cache_slot_s {
    uint8_t *data;
    size_t capacity;
    uint32_t section;
    uint64_t group;
    uint64_t frames;
    uint64_t used;          /* cache_clock at the last use */
    bool valid;
} chm_cache_slot;

typedef struct chm_stream_s {
    chm_context context;
    size_t index;
    uint64_t max_member;
    /* The two most recently used decoded reset groups (or their first
     * frames): a member straddling a group boundary needs two groups, and
     * the next member usually needs the same two. */
    chm_cache_slot cache[CHM_CACHE_SLOTS];
    uint64_t cache_clock;
} chm_stream;

typedef struct chm_sink_s {
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
} chm_sink;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */

#include "xxfclib/global/xx_global.h"
static size_t gb_chm_capacity(void) {
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}
static ssize_t gb_chm_read(xx_io_device *device, void *buffer, size_t size, size_t capacity) {
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
static ssize_t gb_chm_write(xx_io_device *device, const void *buffer, size_t size, size_t capacity) {
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


static uint32_t chm_le16(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U);
}

static uint32_t chm_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static uint64_t chm_le64(const uint8_t *p) {
    return (uint64_t)chm_le32(p) | ((uint64_t)chm_le32(p + 4U) << 32U);
}

static bool chm_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    const size_t file_io_capacity = gb_chm_capacity();
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = gb_chm_read(device, (uint8_t *)buffer + done,
                                    size - done, file_io_capacity);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* True when [offset, offset + size) lies inside [0, limit). */
static bool chm_within(uint64_t limit, uint64_t offset, uint64_t size) {
    return offset <= limit && size <= limit - offset;
}

/* Big-endian base-128, at most 9 bytes (63 bits). */
static bool chm_encint(const uint8_t *data, size_t end, size_t *position,
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

static int chm_log2(uint32_t value) {
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

static void chm_context_free(chm_context *context) {
    size_t index;
    if (!context) return;
    if (context->entries) xx_mem_free(context->entries);
    if (context->pool) xx_mem_free(context->pool);
    for (index = 0U; index < CHM_MAX_SECTIONS; ++index)
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

static bool chm_add_entry(chm_context *context, const uint8_t *name,
                          uint32_t name_length, uint64_t section,
                          uint64_t offset, uint64_t size) {
    chm_entry *entry;
    if (context->entry_count >= CHM_MAX_ENTRIES) return false;
    if (context->entry_count == context->entry_capacity) {
        size_t capacity = context->entry_capacity ? context->entry_capacity * 2U
                                                  : 256U;
        chm_entry *grown = (chm_entry *)xx_mem_realloc(
            context->entries, capacity * sizeof(*grown));
        if (!grown) return false;
        context->entries = grown;
        context->entry_capacity = capacity;
    }
    if (name_length > context->pool_capacity - context->pool_size) {
        size_t capacity = context->pool_capacity ? context->pool_capacity : 4096U;
        uint8_t *grown;
        while (capacity - context->pool_size < name_length) {
            if (capacity > ((size_t)CHM_MAX_DIRECTORY) * 2U) return false;
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

/* One PMGL chunk: entries from byte 20 up to (chunk size - free space), and
 * a closing entry count that is exact or zero. */
static bool chm_parse_listing(chm_context *context, const uint8_t *chunk,
                              uint32_t chunk_size) {
    uint32_t free_space = chm_le32(chunk + 4U), count = 0U, stored;
    size_t position = CHM_PMGL_HEADER, end;
    if (free_space < 2U || free_space > chunk_size - CHM_PMGL_HEADER)
        return false;
    end = chunk_size - free_space;
    while (position < end) {
        uint64_t length, section, offset, size;
        if (!chm_encint(chunk, end, &position, &length) || length == 0U ||
            length > CHM_MAX_NAME || length > end - position)
            return false;
        {
            const uint8_t *name = chunk + position;
            position += (size_t)length;
            if (!chm_encint(chunk, end, &position, &section) ||
                !chm_encint(chunk, end, &position, &offset) ||
                !chm_encint(chunk, end, &position, &size) ||
                !chm_add_entry(context, name, (uint32_t)length, section, offset,
                               size))
                return false;
        }
        ++count;
    }
    stored = chm_le16(chunk + chunk_size - 2U);
    return stored == 0U || stored == (count & 0xFFFFU);
}

/* Header, header section 0 and the whole directory. */
static bool chm_parse_directory(Abstractformat *format, chm_context *context,
                                xx_pd_struct *pd) {
    uint8_t header[CHM_HEADER_V3];
    uint8_t section0[CHM_SECTION0_MIN];
    uint8_t itsp[CHM_ITSP_SIZE];
    uint8_t *chunk = NULL;
    int64_t total, section0_offset, section0_size, end;
    uint64_t value;
    uint32_t index;
    bool result = false;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    total -= format->base_address;
    if (total < (int64_t)CHM_HEADER_V2 ||
        !chm_read_at(format->device, format->base_address, header,
                     CHM_HEADER_V2))
        return false;
    if (xx_rt_memcmp(header, "ITSF", 4U) != 0) return false;
    context->total = total;
    context->version = chm_le32(header + 4U);
    context->header_size = chm_le32(header + 8U);
    if (!((context->version == 3U && context->header_size == CHM_HEADER_V3) ||
          (context->version == 2U && context->header_size == CHM_HEADER_V2)) ||
        chm_le32(header + 12U) > 1U || total < (int64_t)context->header_size)
        return false;
    if (context->version == 3U &&
        !chm_read_at(format->device, format->base_address + CHM_HEADER_V2,
                     header + CHM_HEADER_V2, CHM_HEADER_V3 - CHM_HEADER_V2))
        return false;
    /* Header section 0: magic 0x1FE and the file size. */
    value = chm_le64(header + 0x38U);
    if (value > (uint64_t)total) return false;
    section0_offset = (int64_t)value;
    value = chm_le64(header + 0x40U);
    if (value < CHM_SECTION0_MIN ||
        !chm_within((uint64_t)total, (uint64_t)section0_offset, value))
        return false;
    section0_size = (int64_t)value;
    if (!chm_read_at(format->device, format->base_address + section0_offset,
                     section0, sizeof(section0)) ||
        chm_le32(section0) != CHM_SECTION0_MAGIC)
        return false;
    context->declared_size = chm_le64(section0 + 8U);
    /* Directory. */
    value = chm_le64(header + 0x48U);
    if (value > (uint64_t)total) return false;
    context->directory_offset = (int64_t)value;
    value = chm_le64(header + 0x50U);
    if (value < CHM_ITSP_SIZE ||
        !chm_within((uint64_t)total, (uint64_t)context->directory_offset,
                    value) ||
        value > (uint64_t)CHM_MAX_DIRECTORY)
        return false;
    context->directory_size = (int64_t)value;
    if (context->version == 3U) {
        value = chm_le64(header + 0x58U);
        if (value > (uint64_t)total) return false;
        context->content_offset = (int64_t)value;
    } else {
        context->content_offset =
            context->directory_offset + context->directory_size;
    }
    if (!chm_read_at(format->device,
                     format->base_address + context->directory_offset, itsp,
                     sizeof(itsp)) ||
        xx_rt_memcmp(itsp, "ITSP", 4U) != 0 || chm_le32(itsp + 4U) != 1U ||
        chm_le32(itsp + 8U) != CHM_ITSP_SIZE)
        return false;
    context->chunk_size = chm_le32(itsp + 0x10U);
    context->chunk_count = chm_le32(itsp + 0x2CU);
    if (context->chunk_size < CHM_MIN_CHUNK ||
        context->chunk_size > CHM_MAX_CHUNK || context->chunk_count == 0U ||
        context->chunk_count > CHM_MAX_CHUNKS ||
        (uint64_t)context->chunk_count * context->chunk_size >
            (uint64_t)context->directory_size - CHM_ITSP_SIZE)
        return false;
    chunk = (uint8_t *)xx_mem_alloc(context->chunk_size);
    if (!chunk) return false;
    for (index = 0U; index < context->chunk_count; ++index) {
        int64_t at = format->base_address + context->directory_offset +
                     (int64_t)CHM_ITSP_SIZE +
                     (int64_t)index * (int64_t)context->chunk_size;
        if ((index & 0xFFU) == 0xFFU && pd && xx_pd_is_stopped(pd)) goto done;
        if (!chm_read_at(format->device, at, chunk, context->chunk_size))
            goto done;
        if (xx_rt_memcmp(chunk, "PMGL", 4U) != 0) continue;
        if (!chm_parse_listing(context, chunk, context->chunk_size)) goto done;
        ++context->listing_chunks;
    }
    if (context->listing_chunks == 0U) goto done;
    /* The format ends at the furthest of the header, its sections and the
     * file size header section 0 records, but never past the device. */
    end = (int64_t)context->header_size;
    if (section0_offset + section0_size > end)
        end = section0_offset + section0_size;
    if (context->directory_offset + context->directory_size > end)
        end = context->directory_offset + context->directory_size;
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
static const chm_entry *chm_find(const chm_context *context, const char *name,
                                 size_t length) {
    size_t index;
    for (index = 0U; index < context->entry_count; ++index) {
        const chm_entry *entry = &context->entries[index];
        if (entry->name_length == length &&
            xx_rt_memcmp(context->pool + entry->name_offset, name, length) == 0)
            return entry;
    }
    return NULL;
}

/* A stored (section 0) metadata file, read whole. */
static uint8_t *chm_read_stored(Abstractformat *format,
                                const chm_context *context,
                                const chm_entry *entry, uint64_t limit,
                                size_t *size) {
    uint8_t *data;
    if (!entry || entry->section != 0U || entry->size > limit ||
        context->content_offset > context->total ||
        !chm_within((uint64_t)(context->total - context->content_offset),
                    entry->offset, entry->size))
        return NULL;
    data = (uint8_t *)xx_mem_alloc(entry->size ? (size_t)entry->size : 1U);
    if (!data) return NULL;
    if (!chm_read_at(format->device,
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
static const chm_entry *chm_find_storage(const chm_context *context,
                                         const char *section,
                                         const char *leaf) {
    char name[CHM_MAX_SECTION_NAME + sizeof(chm_rt_suffix) + 32U];
    int length = xx_rt_snprintf(name, sizeof(name),
                                "::DataSpace/Storage/%s/%s", section, leaf);
    if (length <= 0 || (size_t)length >= sizeof(name)) return NULL;
    return chm_find(context, name, (size_t)length);
}

/* ControlData, reset table and Content of one LZX section; leaves the
 * section unusable (kind NONE) on any inconsistency. */
static void chm_load_lzx(Abstractformat *format, chm_context *context,
                         chm_section *section, const char *name) {
    const chm_entry *content = chm_find_storage(context, name, "Content");
    const chm_entry *control = chm_find_storage(context, name, "ControlData");
    const chm_entry *table = chm_find_storage(context, name, chm_rt_suffix);
    uint8_t *data = NULL;
    size_t size = 0U, index;
    int reset_bits, window_bits;
    uint64_t count, packed, span, frames;
    if (!content || !control || !table || content->section != 0U ||
        content->offset > (uint64_t)INT64_MAX ||
        content->size > (uint64_t)INT64_MAX ||
        (uint64_t)context->content_offset >
            (uint64_t)INT64_MAX - content->offset)
        return;
    data = chm_read_stored(format, context, control, CHM_MAX_META, &size);
    if (!data) return;
    if (size < 24U || chm_le32(data) < 5U ||
        xx_rt_memcmp(data + 4U, "LZXC", 4U) != 0 ||
        (chm_le32(data + 8U) != 2U && chm_le32(data + 8U) != 3U)) {
        xx_mem_free(data);
        return;
    }
    reset_bits = chm_log2(chm_le32(data + 12U));
    window_bits = chm_log2(chm_le32(data + 16U));
    xx_mem_free(data);
    if (reset_bits < 0 || reset_bits > 16 || window_bits < 0 ||
        window_bits > (int)(CHM_WINDOW_MAX - CHM_WINDOW_MIN))
        return;
    /* Read no more reset table than the shared budget can still take. */
    data = chm_read_stored(
        format, context, table,
        CHM_RT_HEADER + 8U * ((uint64_t)(CHM_MAX_FRAMES + 2U) -
                              context->reset_entries),
        &size);
    if (!data) return;
    if (size == 0U) {
        /* An empty reset table (.chw files): nothing to decode. */
        count = packed = span = frames = 0U;
    } else {
        if (size < CHM_RT_HEADER ||
            (chm_le32(data) != 2U && chm_le32(data) != 3U) ||
            chm_le32(data + 8U) != 8U || chm_le32(data + 12U) != CHM_RT_HEADER ||
            chm_le64(data + 32U) != CHM_FRAME) {
            xx_mem_free(data);
            return;
        }
        count = chm_le32(data + 4U);
        span = chm_le64(data + 16U);
        packed = chm_le64(data + 24U);
        frames = span / CHM_FRAME + (span % CHM_FRAME ? 1U : 0U);
        if ((uint64_t)size != CHM_RT_HEADER + 8U * count || count < frames ||
            count > frames + 2U || frames > CHM_MAX_FRAMES ||
            packed > content->size ||
            count > (uint64_t)(CHM_MAX_FRAMES + 2U) - context->reset_entries) {
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
            uint64_t at = chm_le64(data + CHM_RT_HEADER + index * 8U);
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
    section->window_bits = CHM_WINDOW_MIN + (uint32_t)window_bits;
    section->data_offset = context->content_offset + (int64_t)content->offset;
    section->data_size = content->size;
    section->kind = CHM_KIND_LZX;
    ++context->lzx_sections;
}

/* NameList: u16 length, u16 count, then per section u16 length, UTF-16LE
 * name and a u16 terminator.  Section 0 is the stored one. */
static void chm_load_sections(Abstractformat *format, chm_context *context) {
    const char list_name[] = "::DataSpace/NameList";
    const chm_entry *list = chm_find(context, list_name, sizeof(list_name) - 1U);
    uint8_t *data;
    size_t size = 0U, position = 4U;
    uint32_t count, index;
    context->sections[0].kind = CHM_KIND_STORED;
    context->section_count = 1U;
    data = chm_read_stored(format, context, list, CHM_MAX_META, &size);
    if (!data) return;
    if (size < 4U) goto done;
    count = chm_le16(data + 2U);
    if (count > CHM_MAX_SECTIONS) count = CHM_MAX_SECTIONS;
    for (index = 0U; index < count; ++index) {
        char name[CHM_MAX_SECTION_NAME + 1U];
        uint32_t length, character;
        bool usable = true;
        if (size - position < 2U) break;
        length = chm_le16(data + position);
        position += 2U;
        if (length > (size - position) / 2U ||
            (size - position) - (size_t)length * 2U < 2U)
            break;
        if (length == 0U || length > CHM_MAX_SECTION_NAME) usable = false;
        for (character = 0U; character < length; ++character) {
            uint32_t c = chm_le16(data + position + character * 2U);
            if (c < 0x20U || c > 0x7EU || c == '/' || c == '\\') usable = false;
            if (usable) name[character] = (char)c;
        }
        position += (size_t)length * 2U;
        if (chm_le16(data + position) != 0U) usable = false;
        position += 2U;
        if (index == 0U) continue;
        if (usable) {
            name[length] = 0;
            chm_load_lzx(format, context, &context->sections[index], name);
        }
        context->section_count = index + 1U;
    }
done:
    xx_mem_free(data);
}

/* ---------------------------------------------------------------------- */
/* Member names                                                            */

static char chm_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* One path component that is safe to create: no separators, drive colons
 * or wildcard/reserved punctuation, no control characters, no trailing dot
 * or space (so never "." or ".."), and not a Windows device name in any
 * case, with or without an extension.  @p name is UTF-8. */
static bool chm_safe_component(const char *name, size_t length) {
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
    if (characters > CHM_NAME_CHARS || name[length - 1U] == '.' ||
        name[length - 1U] == ' ')
        return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (device = 0U; device < sizeof(devices) / sizeof(devices[0]); ++device) {
        const char *word = devices[device];
        size_t k = 0U;
        while (k < stem && word[k] && chm_upper(name[k]) == word[k]) ++k;
        if (k == stem && word[k] == 0) return false;
    }
    if (stem >= 4U &&
        ((chm_upper(name[0]) == 'C' && chm_upper(name[1]) == 'O' &&
          chm_upper(name[2]) == 'M') ||
         (chm_upper(name[0]) == 'L' && chm_upper(name[1]) == 'P' &&
          chm_upper(name[2]) == 'T'))) {
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
static bool chm_safe_path(const char *path) {
    size_t start = 0U, index = 0U;
    if (!path || !path[0]) return false;
    for (;;) {
        if (path[index] == '/' || path[index] == 0) {
            if (!chm_safe_component(path + start, index - start)) return false;
            if (path[index] == 0) return true;
            start = index + 1U;
        }
        ++index;
    }
}

/* Length of the UTF-8 sequence at @p data (well formed, shortest form, no
 * surrogates, at most U+10FFFF), 0 if there is none. */
static size_t chm_utf8_sequence(const uint8_t *data, size_t available,
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
static char *chm_decode_name(const uint8_t *raw, size_t length, bool *safe) {
    char *out;
    size_t used = 0U, index;
    bool utf8 = true, clean = true;
    uint32_t code;
    out = (char *)xx_mem_alloc(length * 2U + 1U);
    if (!out) return NULL;
    for (index = 0U; index < length;) {
        size_t step = chm_utf8_sequence(raw + index, length - index, &code);
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
    *safe = clean && chm_safe_path(out);
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
typedef struct chm_fold_range_s {
    uint32_t lo;
    uint32_t hi;
    int32_t step;
    int32_t delta;
} chm_fold_range;

static const chm_fold_range chm_fold_ranges[] = {
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

static uint32_t chm_fold(uint32_t c) {
    size_t low = 0U,
           high = sizeof(chm_fold_ranges) / sizeof(chm_fold_ranges[0]);
    if (c < 0x80U) return (c >= 'a' && c <= 'z') ? c - 0x20U : c;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        const chm_fold_range *range = &chm_fold_ranges[middle];
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
static uint32_t chm_next_folded(const char *name, size_t *position) {
    uint32_t code = 0U;
    size_t available = 0U, step;
    const uint8_t *p = (const uint8_t *)name + *position;
    if (!*p) return 0U;
    while (available < 4U && p[available]) ++available;
    step = chm_utf8_sequence(p, available, &code);
    if (step == 0U) {
        code = *p;
        step = 1U;
    }
    *position += step;
    return chm_fold(code);
}

static uint32_t chm_name_hash(const char *name) {
    uint32_t hash = UINT32_C(2166136261), code;
    size_t position = 0U;
    while ((code = chm_next_folded(name, &position)) != 0U) {
        hash ^= code;
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static bool chm_same_name(const char *left, const char *right) {
    size_t a = 0U, b = 0U;
    for (;;) {
        uint32_t x = chm_next_folded(left, &a), y = chm_next_folded(right, &b);
        if (x != y) return false;
        if (x == 0U) return true;
    }
}

static bool chm_name_taken(const chm_context *context, const char *name) {
    size_t slot = chm_name_hash(name) & context->mask, probes;
    for (probes = 0U; probes <= context->mask; ++probes) {
        uint32_t value = context->slots[slot];
        if (value == 0U) return false;
        if (context->records[value - 1U].extractable &&
            chm_same_name(context->records[value - 1U].name, name))
            return true;
        slot = (slot + 1U) & context->mask;
    }
    return true;
}

static void chm_name_insert(chm_context *context, size_t index) {
    size_t slot = chm_name_hash(context->records[index].name) & context->mask,
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
static void chm_suffixed(char *out, size_t out_size, const char *base,
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
static bool chm_publish_name(chm_context *context, size_t index) {
    chm_record *record = &context->records[index];
    char *candidate;
    size_t length, attempt;
    if (!record->extractable) return true;
    if (!chm_name_taken(context, record->name)) {
        chm_name_insert(context, index);
        return true;
    }
    record->extractable = false;
    length = xx_str_len(record->name);
    candidate = (char *)xx_mem_alloc(length + 32U);
    if (!candidate) return false;
    for (attempt = 0U; attempt < CHM_DEDUP_TRIES; ++attempt) {
        chm_suffixed(candidate, length + 32U, record->name, index + 1U,
                     attempt);
        if (chm_safe_path(candidate) && !chm_name_taken(context, candidate)) {
            xx_mem_free(record->name);
            record->name = candidate;
            record->extractable = true;
            chm_name_insert(context, index);
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
static bool chm_record_before(const chm_context *context, const chm_record *a,
                              const chm_record *b) {
    const chm_entry *x = &context->entries[a->entry];
    const chm_entry *y = &context->entries[b->entry];
    if (a->folder != b->folder) return a->folder;
    if (!a->folder) {
        if (x->section != y->section) return x->section < y->section;
        if (x->offset != y->offset) return x->offset < y->offset;
        if (x->size != y->size) return x->size < y->size;
    }
    return a->entry < b->entry;
}

static bool chm_sort_records(chm_context *context) {
    size_t count = context->record_count, width, left;
    chm_record *temp, *source = context->records, *target;
    if (count < 2U) return true;
    temp = (chm_record *)xx_mem_alloc(count * sizeof(*temp));
    if (!temp) return false;
    target = temp;
    for (width = 1U; width < count; width *= 2U) {
        for (left = 0U; left < count; left += 2U * width) {
            size_t middle = left + width < count ? left + width : count;
            size_t right = middle + width < count ? middle + width : count;
            size_t i = left, j = middle, k = left;
            while (i < middle && j < right)
                target[k++] = chm_record_before(context, &source[j], &source[i])
                                  ? source[j++]
                                  : source[i++];
            while (i < middle) target[k++] = source[i++];
            while (j < right) target[k++] = source[j++];
        }
        {
            chm_record *swap = source;
            source = target;
            target = swap;
        }
    }
    if (source != context->records)
        xx_rt_memcpy(context->records, source, count * sizeof(*source));
    xx_mem_free(temp);
    return true;
}

static bool chm_member_decodable(const chm_context *context,
                                 const chm_entry *entry) {
    const chm_section *section;
    if (entry->section >= context->section_count) return false;
    section = &context->sections[entry->section];
    if (section->kind == CHM_KIND_STORED)
        return context->content_offset <= context->total &&
               chm_within((uint64_t)(context->total - context->content_offset),
                          entry->offset, entry->size);
    if (section->kind == CHM_KIND_LZX)
        return chm_within(section->span, entry->offset, entry->size);
    return false;
}

static bool chm_build_records(chm_context *context) {
    size_t index, count = 0U, slots;
    for (index = 0U; index < context->entry_count; ++index) {
        const chm_entry *entry = &context->entries[index];
        if (entry->name_length > 1U && context->pool[entry->name_offset] == '/')
            ++count;
    }
    if (count == 0U) return true;
    context->records = (chm_record *)xx_mem_calloc(count, sizeof(chm_record));
    if (!context->records) return false;
    for (index = 0U; index < context->entry_count; ++index) {
        const chm_entry *entry = &context->entries[index];
        const uint8_t *name = context->pool + entry->name_offset;
        chm_record *record;
        size_t length = entry->name_length;
        bool safe = false;
        if (length < 2U || name[0] != '/') continue;
        record = &context->records[context->record_count++];
        record->entry = index;
        record->folder = name[length - 1U] == '/';
        ++name;
        --length;
        if (record->folder) --length;
        record->name = chm_decode_name(name, length, &safe);
        if (!record->name) return false;
        record->extractable = safe;
        if (record->folder)
            ++context->folders;
        else if (!chm_member_decodable(context, entry))
            ++context->unsupported;
    }
    if (!chm_sort_records(context)) return false;
    for (slots = 16U; slots < context->record_count * 2U; slots *= 2U) {}
    context->slots = (uint32_t *)xx_mem_calloc(slots, sizeof(uint32_t));
    if (!context->slots) return false;
    context->mask = slots - 1U;
    for (index = 0U; index < context->record_count; ++index)
        if (!chm_publish_name(context, index)) return false;
    return true;
}

static bool chm_parse(Abstractformat *format, chm_context *context,
                      bool records, xx_pd_struct *pd) {
    xx_mem_zero(context, sizeof(*context));
    if (!chm_parse_directory(format, context, pd)) goto fail;
    if (records) {
        chm_load_sections(format, context);
        if (!chm_build_records(context)) goto fail;
    }
    return true;
fail:
    chm_context_free(context);
    return false;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                              */

static bool chm_sink_write(chm_sink *sink, const uint8_t *data, size_t size) {
    const size_t file_io_capacity = gb_chm_capacity();
    size_t done = 0U;
    if ((uint64_t)size > sink->limit - sink->written) return false;
    while (sink->target && done < size) {
        size_t chunk = size - done;
        ssize_t wrote;
        if (chunk > CHM_SSIZE_LIMIT) chunk = CHM_SSIZE_LIMIT;
        wrote = gb_chm_write(sink->target, data + done, chunk, file_io_capacity);
        if (wrote <= 0 || (size_t)wrote > chunk) return false;
        done += (size_t)wrote;
    }
    sink->written += size;
    return true;
}

static bool chm_copy(xx_io_device *source, int64_t offset, uint64_t size,
                     chm_sink *sink, xx_pd_struct *pd) {
    const size_t file_io_capacity = gb_chm_capacity();
    uint8_t *buffer;
    bool result = true;
    if (size > sink->limit) return false;
    buffer = (uint8_t *)xx_mem_alloc(file_io_capacity);
    if (!buffer) return false;
    while (size > 0U) {
        size_t chunk = size < file_io_capacity ? (size_t)size : file_io_capacity;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !chm_read_at(source, offset, buffer, chunk) ||
            !chm_sink_write(sink, buffer, chunk)) {
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
static uint64_t chm_frame_end(const chm_section *section, uint64_t frame) {
    return frame + 1U < section->reset_count ? section->resets[frame + 1U]
                                             : section->packed;
}

/* Make a cache slot hold at least the first @p need frames of reset group
 * @p group of section @p index and return its bytes in @p *out: the whole
 * group when it fits the cap, else a prefix that at least doubles the one
 * already cached, so members walking through an oversized group cost O(cap)
 * decoding in total, not O(cap) each.  A group not cached replaces the
 * least recently used slot. */
static bool chm_cache_group(Abstractformat *format, chm_stream *stream,
                            const chm_section *section, uint32_t index,
                            uint64_t group, uint64_t need,
                            const uint8_t **out) {
    const uint64_t cap = CHM_MAX_GROUP / CHM_FRAME;
    uint64_t first = group * section->reset_frames, count, frame, start, stop;
    uint64_t cached = 0U;
    const uint8_t **blocks = NULL;
    size_t *block_sizes = NULL, *plain_sizes = NULL, written = 0U, output_size;
    size_t slot_index, victim = 0U;
    chm_cache_slot *slot = NULL;
    uint8_t *packed = NULL;
    bool result = false;
    for (slot_index = 0U; slot_index < CHM_CACHE_SLOTS; ++slot_index) {
        chm_cache_slot *candidate = &stream->cache[slot_index];
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
           chm_frame_end(section, first + count - 1U) - start > CHM_MAX_PACKED)
        --count;
    stop = chm_frame_end(section, first + count - 1U);
    /* The Content extent is checked against the device here rather than at
     * parse time, so a truncated file still yields its leading members. */
    if (stop <= start || stop - start > CHM_MAX_PACKED ||
        stop > section->data_size ||
        section->data_offset > stream->context.total ||
        stop > (uint64_t)(stream->context.total - section->data_offset))
        return false;
    output_size = (size_t)count * CHM_FRAME;
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
        !chm_read_at(format->device,
                     format->base_address + section->data_offset +
                         (int64_t)start,
                     packed, (size_t)(stop - start)))
        goto done;
    for (frame = 0U; frame < count; ++frame) {
        uint64_t from = section->resets[first + frame];
        uint64_t to = chm_frame_end(section, first + frame);
        if (to <= from) goto done;
        blocks[frame] = packed + (from - start);
        block_sizes[frame] = (size_t)(to - from);
        plain_sizes[frame] = CHM_FRAME;
    }
    if (!xx_lzx_cab_decode(blocks, block_sizes, plain_sizes, (size_t)count,
                           section->window_bits, slot->data, output_size,
                           &written) ||
        written != output_size) {
        /* HTML Help Workshop and chmcmd pad the section's last frame to a
         * full 32 KiB; a compiler that ends the stream on the span instead
         * leaves a shorter last frame, so retry that shape once. */
        uint64_t tail = section->span % CHM_FRAME;
        if (first + count != section->frames || tail == 0U) goto done;
        plain_sizes[count - 1U] = (size_t)tail;
        output_size -= CHM_FRAME - (size_t)tail;
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

static bool chm_unpack_lzx(Abstractformat *format, chm_stream *stream,
                           uint32_t index, const chm_entry *entry,
                           chm_sink *sink, xx_pd_struct *pd) {
    const chm_section *section = &stream->context.sections[index];
    uint64_t first_frame, last_frame, group, last_group, end;
    if (entry->size == 0U) return true;
    if (!chm_within(section->span, entry->offset, entry->size) ||
        entry->size > sink->limit)
        return false;
    end = entry->offset + entry->size;
    first_frame = entry->offset / CHM_FRAME;
    last_frame = (end - 1U) / CHM_FRAME;
    if (last_frame >= section->frames) return false;
    last_group = last_frame / section->reset_frames;
    for (group = first_frame / section->reset_frames; group <= last_group;
         ++group) {
        uint64_t base = group * section->reset_frames * (uint64_t)CHM_FRAME;
        uint64_t need, from, to;
        const uint8_t *data = NULL;
        if (pd && xx_pd_is_stopped(pd)) return false;
        need = (group == last_group ? last_frame + 1U
                                    : (group + 1U) * section->reset_frames) -
               group * section->reset_frames;
        if (!chm_cache_group(format, stream, section, index, group, need,
                             &data))
            return false;
        from = entry->offset > base ? entry->offset : base;
        to = base + need * CHM_FRAME;
        if (to > end) to = end;
        if (to <= from ||
            !chm_sink_write(sink, data + (size_t)(from - base),
                            (size_t)(to - from)))
            return false;
    }
    return sink->written == entry->size;
}

static bool chm_unpack_record(Abstractformat *format, chm_stream *stream,
                              const chm_record *record, xx_io_device *target,
                              xx_pd_struct *pd) {
    const chm_context *context = &stream->context;
    const chm_entry *entry = &context->entries[record->entry];
    chm_sink sink;
    if (record->folder) return true;
    if (!chm_member_decodable(context, entry) || entry->size > stream->max_member)
        return false;
    sink.target = target;
    sink.written = 0U;
    sink.limit = entry->size;
    if (context->sections[entry->section].kind == CHM_KIND_STORED)
        return chm_copy(format->device,
                        format->base_address + context->content_offset +
                            (int64_t)entry->offset,
                        entry->size, &sink, pd) &&
               sink.written == entry->size;
    return chm_unpack_lzx(format, stream, (uint32_t)entry->section, entry,
                          &sink, pd);
}

/* ---------------------------------------------------------------------- */
/* Options and records                                                     */

static bool chm_copy_options(xx_list_s *destination, const xx_list_s *source) {
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
static uint64_t chm_max_member(const Abstractformat *format,
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

static bool chm_set_record(xx_archive_record *out, const chm_context *context,
                           const chm_record *record, int64_t base) {
    const chm_entry *entry = &context->entries[record->entry];
    bool stored = entry->section == 0U;
    xx_archive_record_cleanup(out);
    xx_archive_record_init(out);
    out->header_offset = -1;
    out->header_size = 0;
    out->data_offset = -1;
    out->compressed_size = -1;
    if (!record->folder && stored && chm_member_decodable(context, entry)) {
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
               stored ? CHM_METHOD_STORE : CHM_METHOD_LZX) &&
           (!stored || xx_archive_record_set_meta_u64(
                           out, XX_META_ID_COMPRESSED_SIZE, entry->size));
}

static void chm_stream_free(void *opaque) {
    chm_stream *stream = (chm_stream *)opaque;
    size_t slot;
    if (!stream) return;
    chm_context_free(&stream->context);
    for (slot = 0U; slot < CHM_CACHE_SLOTS; ++slot)
        if (stream->cache[slot].data) xx_mem_free(stream->cache[slot].data);
    xx_mem_free(stream);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_chm_init(xx_chm *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CHM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.os = XX_OS_WINDOWS;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/vnd.ms-htmlhelp");
    xx_format_set_extension(&archive->format, "chm");
    archive->format.check_is_valid = xx_chm_check_is_valid;
    archive->format.handle_base_info = xx_chm_handle_base_info;
    archive->format.get_format_size = xx_chm_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_chm_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_chm_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_chm_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_chm_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_chm_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_chm_free_archive_records_reading;
    archive->content_offset = -1;
    archive->declared_size = -1;
}

xx_chm *xx_chm_create(xx_io_device *device, int64_t base_address) {
    xx_chm *archive = (xx_chm *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_chm_init(archive, device, base_address);
    return archive;
}

void xx_chm_destroy(xx_chm *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_chm_free(xx_chm *archive) {
    if (!archive) return;
    xx_chm_destroy(archive);
    xx_mem_free(archive);
}

bool xx_chm_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    chm_context context;
    if (!chm_parse(format, &context, false, pd)) return false;
    chm_context_free(&context);
    return true;
}

bool xx_chm_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    chm_context context;
    xx_chm *archive;
    if (!format || !chm_parse(format, &context, true, pd)) return false;
    archive = (xx_chm *)format;
    archive->number_of_records = (uint64_t)context.record_count;
    archive->version = context.version;
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
    chm_context_free(&context);
    return true;
}

int64_t xx_chm_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_chm_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_chm_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_chm_handle_base_info(format, pd))
               ? ((xx_chm *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_chm_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    chm_stream *stream;
    xx_archive_record_state *state;
    if (!format) return NULL;
    stream = (chm_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!chm_parse(format, &stream->context, true, pd)) {
        xx_mem_free(stream);
        return NULL;
    }
    if (stream->context.record_count == 0U) {
        chm_stream_free(stream);
        return NULL;
    }
    stream->max_member = chm_max_member(format, options);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        chm_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = chm_stream_free;
    state->total_records = (int64_t)stream->context.record_count;
    if (!chm_copy_options(&state->options, options) ||
        !chm_set_record(&state->current_record, &stream->context,
                        &stream->context.records[0], format->base_address)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->current_index = 0;
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_chm_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_chm_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    chm_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (chm_stream *)state->internal_state) ||
        stream->index + 1U >= stream->context.record_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!chm_set_record(&state->current_record, &stream->context,
                        &stream->context.records[stream->index],
                        format->base_address)) {
        state->has_record = false;
        return false;
    }
    state->current_index = (int64_t)stream->index;
    state->has_record = true;
    return true;
}

bool xx_chm_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    chm_stream *stream;
    const chm_record *record;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (chm_stream *)state->internal_state) ||
        stream->index >= stream->context.record_count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    record = &stream->context.records[stream->index];
    path_option = xx_format_resolve_extra_parameter(
        format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return chm_unpack_record(format, stream, record, NULL, pd);
    if (!record->extractable || !chm_safe_path(record->name)) return false;
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
        result = chm_unpack_record(format, stream, record, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_chm_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
