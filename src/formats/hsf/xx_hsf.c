/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * High Sierra CD-ROM image.  xx_hsf.h carries the field table.
 *
 * Written from the High Sierra Group's 1986 volume and file structure
 * layout.  Deark's iso9660 module (MIT, modules/iso9660.c, is_hsf paths)
 * was read to confirm the shifted field positions (descriptor block number
 * in the first eight bytes, 6-byte dates, flags at +24) and the identifying
 * test; libmirage's image-iso parser (GPL) was read only to confirm the
 * "CDROM" signature position.  No code was taken from either.
 *
 * Nothing from the image is trusted: every count, length and extent is
 * checked against the device, both halves of each both-endian field must
 * agree, directories are read block by block under a global byte budget,
 * each directory extent is walked once, and depth, member count and name
 * length are capped.  The detector runs this as a late probe, so garbage is
 * rejected after one 16-byte read.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/hsf/xx_hsf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as HSF is registered there. */
#ifdef HSF
#define XX_HSF_FILE_TYPE XX_FILE_TYPE_HSF
#else
#define XX_HSF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define HSF_SECTOR 2048U
#define HSF_FIRST_DESCRIPTOR 16U
#define HSF_MAX_DESCRIPTORS 64U
#define HSF_MAX_ENTRIES 100000U
#define HSF_MAX_DIRECTORIES 16384U
#define HSF_MAX_DEPTH 64U
#define HSF_MAX_NAME 4096U
#define HSF_MAX_DIR_SIZE UINT32_C(0x01000000)            /* 16 MiB */
#define HSF_DIR_BUDGET INT64_C(0x10000000)               /* 256 MiB */
#define HSF_MIN_RECORD 33U
#define HSF_COPY_CHUNK 65536U

typedef struct hsf_entry_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    uint32_t data_size;
    uint8_t flags;
    uint8_t unit;
    uint8_t gap;
} hsf_entry;

typedef struct hsf_parsed_s {
    hsf_entry *entries;
    size_t count;
    size_t capacity;
    uint32_t *hash;        /* entry index + 1, 0 = empty */
    size_t hash_capacity;  /* power of two */
    uint32_t *dirs;        /* directory extents already walked */
    size_t dir_count;
    size_t dir_capacity;
    int64_t dir_bytes;
    int64_t volume_end;
    int64_t total_size;
    uint32_t block_size;
    uint32_t volume_space;
    char volume_id[33];
} hsf_parsed;

typedef struct hsf_stream_s {
    hsf_parsed parsed;
    size_t index;
} hsf_stream;

static void xx_hsf_vtable_destroy(Abstractformat *self);

static uint16_t hsf_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8U));
}
static uint16_t hsf_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8U) | p[1]);
}
static uint32_t hsf_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}
static uint32_t hsf_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
           ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
}
/* A both-endian u32 whose halves agree. */
static bool hsf_both32(const uint8_t *p, uint32_t *out) {
    uint32_t v = hsf_le32(p);
    if (v != hsf_be32(p + 4U)) return false;
    *out = v;
    return true;
}

static bool hsf_read_at(xx_io_device *device, int64_t offset, void *data,
                        size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* base + blocks * block_size without overflow. */
static bool hsf_block_offset(int64_t base, uint64_t blocks, uint32_t block_size,
                             int64_t *out) {
    uint64_t rel;
    if (base < 0 || block_size == 0U ||
        blocks > (uint64_t)INT64_MAX / block_size) return false;
    rel = blocks * block_size;
    if (rel > (uint64_t)(INT64_MAX - base)) return false;
    *out = base + (int64_t)rel;
    return true;
}

/* Blocks a file occupies on disc, counting interleave gaps. */
static uint64_t hsf_span_blocks(uint32_t size, uint32_t block_size,
                                uint8_t unit, uint8_t gap) {
    uint64_t blocks = ((uint64_t)size + block_size - 1U) / block_size;
    uint64_t units;
    if (blocks == 0U) return 0U;
    if (unit == 0U || gap == 0U) return blocks;
    units = (blocks + unit - 1U) / unit;
    return (units - 1U) * ((uint64_t)unit + gap) +
           (blocks - (units - 1U) * unit);
}

static char hsf_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static uint32_t hsf_hash_name(const char *name) {
    uint32_t h = UINT32_C(2166136261);
    for (; *name; ++name) {
        h ^= (uint8_t)hsf_lower(*name);
        h *= UINT32_C(16777619);
    }
    return h;
}

static bool hsf_same_name(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b)
        if (hsf_lower(*a) != hsf_lower(*b)) return false;
    return *a == *b;
}

static void hsf_parsed_cleanup(hsf_parsed *parsed) {
    size_t i;
    if (!parsed) return;
    for (i = 0U; i < parsed->count; ++i)
        if (parsed->entries[i].name) xx_mem_free(parsed->entries[i].name);
    if (parsed->entries) xx_mem_free(parsed->entries);
    if (parsed->hash) xx_mem_free(parsed->hash);
    if (parsed->dirs) xx_mem_free(parsed->dirs);
    xx_rt_memset(parsed, 0, sizeof(*parsed));
    parsed->volume_end = -1;
}

static bool hsf_hash_contains(const hsf_parsed *parsed, const char *name) {
    size_t mask, slot;
    if (!parsed->hash) return false;
    mask = parsed->hash_capacity - 1U;
    slot = hsf_hash_name(name) & mask;
    while (parsed->hash[slot] != 0U) {
        if (hsf_same_name(parsed->entries[parsed->hash[slot] - 1U].name, name))
            return true;
        slot = (slot + 1U) & mask;
    }
    return false;
}

static void hsf_hash_insert(hsf_parsed *parsed, size_t index) {
    size_t mask = parsed->hash_capacity - 1U;
    size_t slot = hsf_hash_name(parsed->entries[index].name) & mask;
    while (parsed->hash[slot] != 0U) slot = (slot + 1U) & mask;
    parsed->hash[slot] = (uint32_t)(index + 1U);
}

/* Room for one more entry in the array and the hash (kept at most half
 * full). */
static bool hsf_reserve(hsf_parsed *parsed) {
    if (parsed->count >= HSF_MAX_ENTRIES) return false;
    if (parsed->count == parsed->capacity) {
        size_t cap = parsed->capacity ? parsed->capacity * 2U : 32U;
        hsf_entry *grown = (hsf_entry *)xx_mem_realloc(
            parsed->entries, cap * sizeof(*grown));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = cap;
    }
    if ((parsed->count + 1U) * 2U > parsed->hash_capacity) {
        size_t cap = parsed->hash_capacity ? parsed->hash_capacity * 2U : 64U;
        size_t i;
        uint32_t *table = (uint32_t *)xx_mem_alloc(cap * sizeof(*table));
        if (!table) return false;
        xx_rt_memset(table, 0, cap * sizeof(*table));
        if (parsed->hash) xx_mem_free(parsed->hash);
        parsed->hash = table;
        parsed->hash_capacity = cap;
        for (i = 0U; i < parsed->count; ++i) hsf_hash_insert(parsed, i);
    }
    return true;
}

/* Walk each directory extent once: 1 = first visit, 0 = already walked,
 * -1 = too many directories or out of memory. */
static int hsf_first_visit(hsf_parsed *parsed, uint32_t extent) {
    size_t i;
    for (i = 0U; i < parsed->dir_count; ++i)
        if (parsed->dirs[i] == extent) return 0;
    if (parsed->dir_count >= HSF_MAX_DIRECTORIES) return -1;
    if (parsed->dir_count == parsed->dir_capacity) {
        size_t cap = parsed->dir_capacity ? parsed->dir_capacity * 2U : 16U;
        uint32_t *grown =
            (uint32_t *)xx_mem_realloc(parsed->dirs, cap * sizeof(*grown));
        if (!grown) return -1;
        parsed->dirs = grown;
        parsed->dir_capacity = cap;
    }
    parsed->dirs[parsed->dir_count++] = extent;
    return 1;
}

/* The member name for one identifier.  A final ";1" (the usual version)
 * is dropped together with a '.' left in front of it, so "README.;1" names
 * "README"; any other version stays in the name, which keeps "A;1" and "A;2"
 * apart the way Deark and The Unarchiver name them.  Path separators and NUL
 * inside the identifier become '_'; anything else is kept as recorded and
 * extraction refuses unsafe names. */
static size_t hsf_component(const uint8_t *id, size_t length, char *out) {
    size_t n = 0U, i;
    for (i = 0U; i < length; ++i) {
        char c = (char)id[i];
        if (c == '/' || c == 0x5C || c == 0) c = '_';
        out[n++] = c;
    }
    if (n >= 3U && out[n - 2U] == ';' && out[n - 1U] == '1') {
        n -= 2U;
        if (n > 1U && out[n - 1U] == '.') --n;
    }
    if (n == 0U) out[n++] = '_';
    out[n] = 0;
    return n;
}

/* prefix "/" component, made unique against every earlier member. */
static char *hsf_unique_name(const hsf_parsed *parsed, const char *prefix,
                             const char *component) {
    size_t plen = xx_str_len(prefix), clen = xx_str_len(component);
    size_t base_len = plen + (plen ? 1U : 0U) + clen;
    char *name;
    unsigned suffix;
    if (base_len + 8U > HSF_MAX_NAME) return NULL;
    name = (char *)xx_mem_alloc(base_len + 8U);
    if (!name) return NULL;
    if (plen) {
        xx_rt_memcpy(name, prefix, plen);
        name[plen] = '/';
    }
    xx_rt_memcpy(name + base_len - clen, component, clen);
    name[base_len] = 0;
    for (suffix = 2U; hsf_hash_contains(parsed, name); ++suffix) {
        char digits[8];
        size_t nd = 0U, k;
        unsigned v = suffix;
        if (suffix > 99999U) {
            xx_mem_free(name);
            return NULL;
        }
        do {
            digits[nd++] = (char)('0' + v % 10U);
            v /= 10U;
        } while (v);
        name[base_len] = '~';
        for (k = 0U; k < nd; ++k) name[base_len + 1U + k] = digits[nd - 1U - k];
        name[base_len + 1U + nd] = 0;
    }
    return name;
}

static bool hsf_parse_directory(Abstractformat *self, hsf_parsed *parsed,
                                uint32_t extent, uint32_t size,
                                const char *prefix, unsigned depth,
                                xx_pd_struct *pd);

static bool hsf_parse_record(Abstractformat *self, hsf_parsed *parsed,
                             const uint8_t *rec, size_t reclen,
                             int64_t rec_offset, const char *prefix,
                             unsigned depth, xx_pd_struct *pd) {
    uint32_t extent, size;
    uint8_t xar = rec[1], flags = rec[24], unit = rec[26], gap = rec[27];
    uint8_t idlen = rec[32];
    int64_t data_offset, data_end;
    uint64_t span;
    char component[256];
    hsf_entry *entry;
    char *name;
    if ((size_t)HSF_MIN_RECORD + idlen > reclen || idlen == 0U ||
        !hsf_both32(rec + 2U, &extent) || !hsf_both32(rec + 10U, &size))
        return false;
    if (idlen == 1U && (rec[33] == 0U || rec[33] == 1U)) return true;
    /* Files split over several extents are not modelled. */
    if ((flags & 0x80U) != 0U) return false;
    if ((flags & 0x02U) != 0U) unit = gap = 0U;
    span = (uint64_t)xar + hsf_span_blocks(size, parsed->block_size, unit, gap);
    if (!hsf_block_offset(self->base_address, (uint64_t)extent + xar,
                          parsed->block_size, &data_offset) ||
        !hsf_block_offset(self->base_address, (uint64_t)extent + span,
                          parsed->block_size, &data_end) ||
        data_end > parsed->volume_end)
        return false;
    (void)hsf_component(rec + 33U, idlen, component);
    if (!hsf_reserve(parsed)) return false;
    name = hsf_unique_name(parsed, prefix, component);
    if (!name) return false;
    entry = &parsed->entries[parsed->count];
    entry->name = name;
    entry->header_offset = rec_offset;
    entry->data_offset = data_offset;
    entry->data_size = size;
    entry->flags = flags;
    entry->unit = unit;
    entry->gap = gap;
    hsf_hash_insert(parsed, parsed->count);
    ++parsed->count;
    if ((flags & 0x02U) != 0U)
        return hsf_parse_directory(self, parsed, extent + xar, size, name,
                                   depth + 1U, pd);
    return true;
}

static bool hsf_parse_directory(Abstractformat *self, hsf_parsed *parsed,
                                uint32_t extent, uint32_t size,
                                const char *prefix, unsigned depth,
                                xx_pd_struct *pd) {
    int64_t dir_offset;
    uint32_t bs = parsed->block_size;
    uint32_t done;
    uint8_t *block;
    bool ok = true;
    int visit;
    if (depth > HSF_MAX_DEPTH || (pd && xx_pd_is_stopped(pd))) return false;
    if (size == 0U) return true;
    if (size > HSF_MAX_DIR_SIZE ||
        (int64_t)size > HSF_DIR_BUDGET - parsed->dir_bytes ||
        !hsf_block_offset(self->base_address, extent, bs, &dir_offset) ||
        dir_offset > parsed->total_size ||
        (int64_t)size > parsed->total_size - dir_offset)
        return false;
    /* A directory reached twice (a loop, or two names for one extent) is
     * listed but walked only the first time. */
    visit = hsf_first_visit(parsed, extent);
    if (visit <= 0) return visit == 0;
    parsed->dir_bytes += size;
    block = (uint8_t *)xx_mem_alloc(bs);
    if (!block) return false;
    for (done = 0U; ok && done < size; done += bs) {
        uint32_t blen = size - done < bs ? size - done : bs;
        uint32_t pos = 0U;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !hsf_read_at(self->device, dir_offset + done, block, blen)) {
            ok = false;
            break;
        }
        while (pos < blen) {
            uint8_t reclen = block[pos];
            if (reclen == 0U) break; /* padding to the next block */
            if (reclen < HSF_MIN_RECORD || reclen > blen - pos ||
                !hsf_parse_record(self, parsed, block + pos, reclen,
                                  dir_offset + done + pos, prefix, depth,
                                  pd)) {
                ok = false;
                break;
            }
            pos += reclen;
        }
    }
    xx_mem_free(block);
    return ok;
}

static bool hsf_parse(Abstractformat *self, hsf_parsed *parsed,
                      xx_pd_struct *pd) {
    uint8_t head[16];
    uint8_t vd[HSF_SECTOR];
    uint8_t sfsvd[HSF_SECTOR];
    bool have_sfsvd = false;
    uint32_t last_sector = HSF_FIRST_DESCRIPTOR;
    uint32_t index, vs, root_extent, root_size;
    uint16_t bs;
    int64_t offset, min_end;
    size_t n;
    if (parsed) {
        xx_rt_memset(parsed, 0, sizeof(*parsed));
        parsed->volume_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    parsed->total_size = xx_io_total_size(self->device);
    if (parsed->total_size < self->base_address ||
        parsed->total_size - self->base_address <
            (int64_t)(HSF_FIRST_DESCRIPTOR + 1U) * HSF_SECTOR)
        return false;
    /* Cheap rejection first: block number 16 in both byte orders, then
     * "CDROM" and version 1. */
    offset = self->base_address + (int64_t)HSF_FIRST_DESCRIPTOR * HSF_SECTOR;
    if (!hsf_read_at(self->device, offset, head, sizeof(head)) ||
        hsf_le32(head) != HSF_FIRST_DESCRIPTOR ||
        hsf_be32(head + 4U) != HSF_FIRST_DESCRIPTOR ||
        xx_rt_memcmp(head + 9U, "CDROM", 5U) != 0 || head[14] != 1U)
        return false;
    for (index = 0U; index < HSF_MAX_DESCRIPTORS; ++index) {
        uint32_t sector = HSF_FIRST_DESCRIPTOR + index;
        offset = self->base_address + (int64_t)sector * HSF_SECTOR;
        if (offset > parsed->total_size - (int64_t)HSF_SECTOR ||
            !hsf_read_at(self->device, offset, vd, sizeof(vd)) ||
            hsf_le32(vd) != sector || hsf_be32(vd + 4U) != sector ||
            xx_rt_memcmp(vd + 9U, "CDROM", 5U) != 0 || vd[14] != 1U)
            break;
        last_sector = sector;
        if (vd[8] == 1U && !have_sfsvd) {
            xx_rt_memcpy(sfsvd, vd, sizeof(sfsvd));
            have_sfsvd = true;
        }
        if (vd[8] == 255U) break;
    }
    if (!have_sfsvd || !hsf_both32(sfsvd + 88U, &vs) || vs == 0U) goto fail;
    bs = hsf_le16(sfsvd + 136U);
    if (bs != hsf_be16(sfsvd + 138U) ||
        (bs != 512U && bs != 1024U && bs != 2048U))
        goto fail;
    parsed->block_size = bs;
    parsed->volume_space = vs;
    if (!hsf_block_offset(self->base_address, vs, bs, &parsed->volume_end) ||
        !hsf_block_offset(self->base_address, (uint64_t)last_sector + 1U,
                          HSF_SECTOR, &min_end) ||
        parsed->volume_end < min_end)
        goto fail;
    /* Root directory record. */
    if (sfsvd[180] < 34U || !hsf_both32(sfsvd + 182U, &root_extent) ||
        !hsf_both32(sfsvd + 190U, &root_size) ||
        (sfsvd[180 + 24] & 0x02U) == 0U || root_size == 0U)
        goto fail;
    xx_rt_memcpy(parsed->volume_id, sfsvd + 48U, 32U);
    n = 32U;
    while (n > 0U && (parsed->volume_id[n - 1U] == ' ' ||
                      parsed->volume_id[n - 1U] == 0))
        --n;
    parsed->volume_id[n] = 0;
    {
        int64_t root_end;
        if (!hsf_block_offset(self->base_address,
                              (uint64_t)root_extent + sfsvd[181],
                              bs, &root_end) ||
            root_end > parsed->volume_end)
            goto fail;
    }
    if (!hsf_parse_directory(self, parsed, root_extent + sfsvd[181], root_size,
                             "", 0U, pd))
        goto fail;
    return true;
fail:
    hsf_parsed_cleanup(parsed);
    return false;
}

/* Extraction writes <base>/<name>.  Refuses empty components, "." / ".."
 * (and names of only dots and spaces), control bytes, characters Windows
 * reserves, trailing dots or spaces, and device names such as CON, LPT1.EXT
 * or CONIN$, in any case. */
static bool hsf_component_is_safe(const char *s, size_t len) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t i, stem = 0U, d;
    bool meaningful = false;
    if (len == 0U || s[len - 1U] == '.' || s[len - 1U] == ' ') return false;
    for (i = 0U; i < len; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20U || c > 0x7EU || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*' || c == '\\')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < len && s[stem] != '.') ++stem;
    while (stem > 0U && s[stem - 1U] == ' ') --stem;
    for (d = 0U; d < sizeof(devices) / sizeof(devices[0]); ++d) {
        const char *w = devices[d];
        size_t k;
        for (k = 0U; k < stem && w[k]; ++k) {
            char c = s[k];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (c != w[k]) break;
        }
        if (k == stem && w[k] == 0) return false;
    }
    if (stem == 4U && s[3] >= '0' && s[3] <= '9') {
        char a = s[0], b = s[1], c = s[2];
        if (a >= 'a') a = (char)(a - 32);
        if (b >= 'a') b = (char)(b - 32);
        if (c >= 'a') c = (char)(c - 32);
        if ((a == 'C' && b == 'O' && c == 'M') ||
            (a == 'L' && b == 'P' && c == 'T'))
            return false;
    }
    return true;
}

static bool hsf_safe_name(const char *name) {
    const char *start = name;
    const char *p;
    if (!name || !name[0] || name[0] == '/') return false;
    for (p = name;; ++p) {
        if (*p == '/' || *p == 0) {
            if (!hsf_component_is_safe(start, (size_t)(p - start)))
                return false;
            if (*p == 0) return true;
            start = p + 1;
        }
    }
}

static bool hsf_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
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

static const xx_var *hsf_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool hsf_set_record(xx_archive_record *record, const hsf_entry *entry) {
    bool folder = (entry->flags & 0x02U) != 0U;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = -1;
    record->data_offset = entry->data_offset;
    record->compressed_size = folder ? 0 : (int64_t)entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          folder ? 0U : entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          folder ? 0U : entry->data_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           folder);
}

static void hsf_stream_free(void *pointer) {
    hsf_stream *stream = (hsf_stream *)pointer;
    if (!stream) return;
    hsf_parsed_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* Every byte of the member is on the device. */
static bool hsf_entry_present(const hsf_parsed *parsed,
                              const hsf_entry *entry) {
    uint64_t span = hsf_span_blocks(entry->data_size, parsed->block_size,
                                    entry->unit, entry->gap);
    uint64_t bytes;
    if ((entry->flags & 0x02U) != 0U) return true;
    if (entry->data_size == 0U) return entry->data_offset <= parsed->total_size;
    /* The last block may be partial: the member ends at data_size within
     * its last unit. */
    bytes = (span - 1U) * parsed->block_size +
            (entry->data_size - 1U) % parsed->block_size + 1U;
    return entry->data_offset <= parsed->total_size &&
           bytes <= (uint64_t)(parsed->total_size - entry->data_offset);
}

/* Interleaved member: `unit` blocks of data, `gap` blocks skipped. */
static bool hsf_copy_interleaved(xx_io_device *src, const hsf_parsed *parsed,
                                 const hsf_entry *entry, xx_io_device *dst,
                                 xx_pd_struct *pd) {
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(HSF_COPY_CHUNK);
    uint64_t remaining = entry->data_size;
    uint64_t unit_bytes = (uint64_t)entry->unit * parsed->block_size;
    uint64_t stride = ((uint64_t)entry->unit + entry->gap) * parsed->block_size;
    int64_t cursor = entry->data_offset;
    bool ok = buffer != NULL;
    while (ok && remaining) {
        uint64_t chunk = remaining < unit_bytes ? remaining : unit_bytes;
        uint64_t done = 0U;
        while (ok && done < chunk) {
            size_t piece = chunk - done < HSF_COPY_CHUNK
                               ? (size_t)(chunk - done) : HSF_COPY_CHUNK;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !hsf_read_at(src, cursor + (int64_t)done, buffer, piece) ||
                xx_io_write(dst, buffer, piece) != (ssize_t)piece)
                ok = false;
            done += piece;
        }
        remaining -= chunk;
        cursor += (int64_t)stride;
    }
    if (buffer) xx_mem_free(buffer);
    return ok;
}

void xx_hsf_init(xx_hsf *hsf, xx_io_device *dev, int64_t base_address) {
    if (!hsf) return;
    xx_rt_memset(hsf, 0, sizeof(*hsf));
    xx_format_init(&hsf->format, dev, base_address);
    hsf->format.endian = XX_ENDIAN_LITTLE;
    hsf->format.file_type = XX_HSF_FILE_TYPE;
    hsf->format.format_type = XX_TYPE_ARCHIVE;
    hsf->format.is_archive = true;
    xx_format_set_mime_type(&hsf->format, "application/x-iso9660-image");
    xx_format_set_extension(&hsf->format, "iso");
    hsf->format.check_is_valid = xx_hsf_check_is_valid;
    hsf->format.handle_base_info = xx_hsf_handle_base_info;
    hsf->format.get_format_size = xx_hsf_get_format_size;
    hsf->format.get_number_of_archive_records =
        xx_hsf_get_number_of_archive_records;
    hsf->format.create_archive_records_reading =
        xx_hsf_create_archive_records_reading;
    hsf->format.get_current_archive_record = xx_hsf_get_current_archive_record;
    hsf->format.unpack_current_archive_record =
        xx_hsf_unpack_current_archive_record;
    hsf->format.archive_record_move_to_next =
        xx_hsf_archive_record_move_to_next;
    hsf->format.free_archive_records_reading =
        xx_hsf_free_archive_records_reading;
    hsf->format.destroy = xx_hsf_vtable_destroy;
    hsf->volume_end = -1;
}

xx_hsf *xx_hsf_create(xx_io_device *dev, int64_t base_address) {
    xx_hsf *hsf = (xx_hsf *)xx_mem_alloc(sizeof(*hsf));
    if (hsf) xx_hsf_init(hsf, dev, base_address);
    return hsf;
}

void xx_hsf_destroy(xx_hsf *hsf) {
    if (!hsf) return;
    if (hsf->internal) {
        hsf_parsed_cleanup((hsf_parsed *)hsf->internal);
        xx_mem_free(hsf->internal);
        hsf->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&hsf->format);
}

static void xx_hsf_vtable_destroy(Abstractformat *self) {
    xx_hsf_destroy((xx_hsf *)self);
}

void xx_hsf_free(xx_hsf *hsf) {
    if (!hsf) return;
    xx_hsf_destroy(hsf);
    xx_mem_free(hsf);
}

bool xx_hsf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    hsf_parsed parsed;
    bool result = hsf_parse(self, &parsed, pd);
    hsf_parsed_cleanup(&parsed);
    return result;
}

bool xx_hsf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_hsf *hsf = (xx_hsf *)self;
    hsf_parsed *parsed;
    int64_t end;
    if (!self) return false;
    parsed = (hsf_parsed *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !hsf_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (hsf->internal) {
        hsf_parsed_cleanup((hsf_parsed *)hsf->internal);
        xx_mem_free(hsf->internal);
    }
    hsf->internal = parsed;
    hsf->number_of_records = parsed->count;
    hsf->logical_block_size = parsed->block_size;
    hsf->volume_space_size = parsed->volume_space;
    hsf->volume_end = parsed->volume_end;
    hsf->truncated = parsed->volume_end > parsed->total_size;
    xx_rt_memcpy(hsf->volume_id, parsed->volume_id, sizeof(hsf->volume_id));
    end = hsf->truncated ? parsed->total_size : parsed->volume_end;
    self->format_size = end - self->base_address;
    if (parsed->total_size > end) {
        self->overlay_offset = end;
        self->overlay_size = parsed->total_size - end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_hsf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd)))
        return -1;
    return self->format_size;
}

uint64_t xx_hsf_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd)))
        return 0U;
    return ((xx_hsf *)self)->number_of_records;
}

xx_archive_record_state *xx_hsf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    hsf_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (hsf_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!hsf_copy_options(&state->options, options) ||
        !hsf_parse(self, &stream->parsed, pd)) {
        hsf_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->internal_state = stream;
    state->free_internal = hsf_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        hsf_set_record(&state->current_record, &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_hsf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_hsf_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    hsf_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (hsf_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count ||
        !hsf_set_record(&state->current_record,
                        &stream->parsed.entries[stream->index])) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_hsf_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    hsf_stream *stream;
    const hsf_entry *entry;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    size_t blen;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (hsf_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    entry = &stream->parsed.entries[stream->index];
    if (!hsf_safe_name(entry->name) ||
        !hsf_entry_present(&stream->parsed, entry))
        return false;
    option = hsf_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return true;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    blen = xx_str_len(base);
    path = (blen && base[blen - 1U] != '/' && base[blen - 1U] != '\\')
               ? xx_str_concat3(base, "/", entry->name)
               : xx_str_concat(base, entry->name);
    if (!path) goto done;
    if ((entry->flags & 0x02U) != 0U) {
        result = xx_store_create_dirs_a(path, true);
    } else if (!xx_store_create_dirs_a(path, false)) {
        result = false;
    } else if (entry->unit == 0U || entry->gap == 0U) {
        /* Contiguous: the store helper deletes its own output on failure. */
        result = xx_store_unpack_device_to_file(
            self->device, entry->data_offset, (int64_t)entry->data_size, path,
            pd);
    } else {
        xx_io_device *out = xx_io_file_open(path, "wb");
        if (out) {
            created = true;
            result = hsf_copy_interleaved(self->device, &stream->parsed, entry,
                                          out, pd);
            if (xx_io_close(out) != 0) result = false;
            if (!result && created) xx_rt_remove(path);
        }
    }
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_hsf_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
