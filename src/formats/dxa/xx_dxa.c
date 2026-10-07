/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DxLib DXA resource archive (*.dxa, Wolf RPG Editor *.wolf, ...).
 * xx_dxa.h carries the field tables.
 *
 * The key recovery, the index layout and the LZ decoder follow GARbro's
 * ArcFormats/DxLib/ArcDX.cs and DxKey.cs (MIT, (C) 2015-2018 morkt): the
 * guessing of the 12-byte XOR key from the known header plaintext
 * (GuessKeyV6 and the version 4..1 loop of GuessKey), the version 2..4 and
 * version 6 index readers and DxOpener.Unpack.  The code here is a C
 * re-implementation with the structural checks a magic-less probe needs, an
 * explicit directory stack instead of recursion, and a streaming decoder.
 * The member-name safety and duplicate handling follow this library's
 * rpg_maker_rgssad reader (src/formats/rpg_maker_rgssad, MIT).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dxa/xx_dxa.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef DXA
#define XX_DXA_FILE_TYPE XX_FILE_TYPE_DXA
#else
#define XX_DXA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define DXA_KEY 12U
#define DXA_SIGNATURE 0x5844U
#define DXA_V6_HEADER 0x30U
#define DXA_ATTR_DIRECTORY 0x10U
/* Versions 1..4 are guessed with the index running to EOF; GARbro caps that
 * at 16 MiB and so does this reader.  Version 6 states the index size. */
#define DXA_MAX_INDEX_V4 UINT32_C(0xFFFFFF)
#define DXA_MAX_INDEX_V6 UINT32_C(0x4000000)
#define DXA_MAX_MEMBERS 1000000U
#define DXA_MAX_DEPTH 64U
#define DXA_MAX_NAME 1024U
#define DXA_PATH_BUFFER ((DXA_MAX_DEPTH + 1U) * (3U * DXA_MAX_NAME + 1U) + 16U)
#define DXA_POLL_MASK 0x3ffU
/* LZ output ring used when a member is larger than it; twice the longest
 * match distance, so flushed history is still there for the next match. */
#define DXA_RING UINT32_C(0x2000000)
#define DXA_MAX_DISTANCE UINT32_C(0x1000000)
#define DXA_LZ_HEADER 9U
#define DXA_IO_CHUNK 0x10000U

typedef struct dxa_layout_s {
    uint8_t key[DXA_KEY];
    uint32_t version;
    int64_t total;      /**< Bytes from the archive start to EOF. */
    int64_t data_start; /**< Relative to the archive start. */
    int64_t index_offset;
    uint32_t index_size;
    uint32_t file_table; /**< Index relative. */
    uint32_t dir_table;
    uint32_t head_size;  /**< File head size. */
    uint32_t dir_size;   /**< Directory entry size. */
    uint32_t count;      /**< Members found by the walk. */
    uint32_t dirs;       /**< Directories found by the walk, root included. */
    int64_t end;         /**< Format size. */
} dxa_layout;

typedef struct dxa_member_s {
    int64_t head;   /**< Index relative offset of the file head. */
    int64_t offset; /**< Data, relative to the archive start. */
    int64_t stored;
    int64_t size;
    uint32_t name;  /**< Index relative offset of the name entry. */
    uint32_t dir;   /**< Owning directory number. */
    bool packed;
    bool renamed;
} dxa_member;

typedef struct dxa_dir_s {
    uint32_t name;   /**< Name entry offset; unused for the root. */
    uint32_t parent; /**< Directory number; UINT32_MAX for the root. */
} dxa_dir;

typedef struct dxa_frame_s {
    uint32_t dir_offset;
    uint32_t dir_number;
    uint64_t next;
    uint64_t count;
    uint64_t first;
} dxa_frame;

typedef struct dxa_key_s {
    uint64_t hash;
    uint32_t index;
} dxa_key;

typedef struct dxa_stream_s {
    dxa_layout layout;
    uint8_t *index;
    dxa_member *items;
    dxa_dir *dirs;
    size_t count;
    size_t at;
    char *name;
} dxa_stream;

/* ---- helpers ----------------------------------------------------------- */

static bool dxa_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool dxa_write_all(xx_io_device *destination, const uint8_t *data,
                          size_t size) {
    size_t done = 0U;
    if (!destination) return true;
    while (done < size) {
        ssize_t amount = xx_io_write(destination, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* XOR `size` bytes with the key, `position` being the key position of the
 * first byte. */
static void dxa_xor(uint8_t *data, size_t size, const uint8_t *key,
                    uint64_t position) {
    size_t at;
    uint32_t k = (uint32_t)(position % DXA_KEY);
    for (at = 0U; at < size; ++at) {
        data[at] ^= key[k];
        if (++k == DXA_KEY) k = 0U;
    }
}

/* ---- header / key recovery --------------------------------------------- */

/* Version 6: the high dword of the data start (0x0C) and of the file table
 * (0x1C) are zero, and the data start is 0x30. */
static bool dxa_guess_v6(const uint8_t *raw, int64_t total, dxa_layout *l) {
    uint8_t h[DXA_V6_HEADER];
    uint32_t key0, key1, key2;
    uint64_t index_offset, file_table, dir_table;
    if (total < (int64_t)DXA_V6_HEADER) return false;
    key0 = xx_data_get_u32(raw, 4, 0, false) ^ (DXA_SIGNATURE | (UINT32_C(6) << 16U));
    if ((xx_data_get_u32(raw + 12, 4, 0, false) ^ key0) != 0U) return false;
    key1 = xx_data_get_u32(raw + 0x1C, 4, 0, false);
    key2 = xx_data_get_u32(raw + 8, 4, 0, false) ^ DXA_V6_HEADER;
    l->key[0] = (uint8_t)key0;
    l->key[1] = (uint8_t)(key0 >> 8U);
    l->key[2] = (uint8_t)(key0 >> 16U);
    l->key[3] = (uint8_t)(key0 >> 24U);
    l->key[4] = (uint8_t)key1;
    l->key[5] = (uint8_t)(key1 >> 8U);
    l->key[6] = (uint8_t)(key1 >> 16U);
    l->key[7] = (uint8_t)(key1 >> 24U);
    l->key[8] = (uint8_t)key2;
    l->key[9] = (uint8_t)(key2 >> 8U);
    l->key[10] = (uint8_t)(key2 >> 16U);
    l->key[11] = (uint8_t)(key2 >> 24U);
    xx_rt_memcpy(h, raw, sizeof(h));
    dxa_xor(h, sizeof(h), l->key, 0U);
    l->version = 6U;
    l->index_size = xx_data_get_u32(h + 4, 4, 0, false);
    l->data_start = (int64_t)xx_data_get_u64(h + 8, 8, 0, false); /* 0x30 by construction */
    index_offset = xx_data_get_u64(h + 0x10, 8, 0, false);
    file_table = xx_data_get_u64(h + 0x18, 8, 0, false);
    dir_table = xx_data_get_u64(h + 0x20, 8, 0, false);
    if (l->index_size == 0U || l->index_size > DXA_MAX_INDEX_V6 ||
        index_offset < DXA_V6_HEADER || index_offset > (uint64_t)total ||
        (uint64_t)l->index_size > (uint64_t)total - index_offset ||
        file_table >= dir_table || dir_table >= l->index_size)
        return false;
    l->index_offset = (int64_t)index_offset;
    l->file_table = (uint32_t)file_table;
    l->dir_table = (uint32_t)dir_table;
    l->head_size = 0x40U;
    l->dir_size = 0x20U;
    return true;
}

/* Versions 1..4: the index is assumed to run from its offset to EOF, which
 * gives key bytes 4..6; key byte 7 is then the plain top byte (zero). */
static bool dxa_guess_v4(const uint8_t *raw, int64_t total, uint32_t version,
                         dxa_layout *l) {
    uint8_t h[0x1C];
    const uint32_t header = version >= 4U ? 0x1CU : 0x18U;
    uint32_t key0, index_offset, index_size;
    size_t at;
    if (total < (int64_t)header) return false;
    xx_rt_memcpy(h, raw, header);
    for (at = 0U; at < DXA_KEY; ++at) l->key[at] = h[at];
    l->key[0] ^= (uint8_t)'D';
    l->key[1] ^= (uint8_t)'X';
    l->key[2] ^= (uint8_t)version;
    l->key[8] ^= (uint8_t)header;
    key0 = xx_data_get_u32(l->key, 4, 0, false);
    index_offset = xx_data_get_u32(h + 12, 4, 0, false) ^ key0;
    if (index_offset <= header || (int64_t)index_offset >= total) return false;
    if (total - (int64_t)index_offset > (int64_t)DXA_MAX_INDEX_V4) return false;
    index_size = (uint32_t)(total - (int64_t)index_offset);
    l->key[4] ^= (uint8_t)index_size;
    l->key[5] ^= (uint8_t)(index_size >> 8U);
    l->key[6] ^= (uint8_t)(index_size >> 16U);
    dxa_xor(h, header, l->key, 0U);
    l->version = version;
    l->index_size = xx_data_get_u32(h + 4, 4, 0, false);
    l->data_start = (int64_t)xx_data_get_u32(h + 8, 4, 0, false);
    l->index_offset = (int64_t)xx_data_get_u32(h + 12, 4, 0, false);
    l->file_table = xx_data_get_u32(h + 16, 4, 0, false);
    l->dir_table = xx_data_get_u32(h + 20, 4, 0, false);
    if (l->index_size != index_size || l->data_start != (int64_t)header ||
        l->index_offset != (int64_t)index_offset ||
        l->file_table >= l->dir_table || l->dir_table >= l->index_size)
        return false;
    l->head_size = version >= 2U ? 0x2CU : 0x28U;
    l->dir_size = 0x10U;
    return true;
}

static uint64_t dxa_field(const dxa_layout *l, const uint8_t *p,
                          uint32_t slot) {
    return l->version >= 6U ? xx_data_get_u64(p + 8U * slot, 8, 0, false) : xx_data_get_u32(p + 4U * slot, 4, 0, false);
}

static bool dxa_is_minus_one(const dxa_layout *l, uint64_t value) {
    return l->version >= 6U ? value == UINT64_MAX : value == UINT32_MAX;
}

/* Key position of the first index byte. */
static uint64_t dxa_index_key_position(const dxa_layout *l) {
    return l->version >= 6U ? 0U : (uint64_t)l->index_offset;
}

/* Cheap check before the whole index is loaded: the root directory entry. */
static bool dxa_root_plausible(Abstractformat *format, const dxa_layout *l) {
    uint8_t entry[0x20];
    uint64_t parent, count, first;
    if ((uint64_t)l->dir_table + l->dir_size > l->index_size ||
        !dxa_read_at(format->device,
                     format->base_address + l->index_offset + l->dir_table,
                     entry, l->dir_size))
        return false;
    dxa_xor(entry, l->dir_size, l->key,
            dxa_index_key_position(l) + l->dir_table);
    parent = dxa_field(l, entry, 1U);
    count = dxa_field(l, entry, 2U);
    first = dxa_field(l, entry, 3U);
    return dxa_is_minus_one(l, parent) &&
           count <= (uint64_t)(l->dir_table - l->file_table) / l->head_size &&
           first <= (uint64_t)(l->dir_table - l->file_table);
}

/* ---- index walk -------------------------------------------------------- */

/* The original-case name of the entry at `offset`: a NUL-terminated string
 * inside the name table.  Fills *length. */
static const uint8_t *dxa_name_at(const dxa_layout *l, const uint8_t *index,
                                  uint64_t offset, size_t *length) {
    uint32_t units, span, n;
    const uint8_t *name;
    if (offset > l->file_table || l->file_table - offset < 4U) return NULL;
    units = (uint32_t)index[offset] | ((uint32_t)index[offset + 1U] << 8U);
    span = units * 4U;
    if (units == 0U ||
        (uint64_t)l->file_table - offset - 4U < 2ULL * (uint64_t)span)
        return NULL;
    name = index + offset + 4U + span;
    for (n = 0U; n < span && name[n] != 0U; ++n) {}
    if (n == 0U || n == span || n > DXA_MAX_NAME) return NULL;
    *length = n;
    return name;
}

/* Walk the directory tree from the root.  With `items` NULL only counts. */
static bool dxa_walk(const dxa_layout *layout_in, dxa_layout *out,
                     const uint8_t *index, dxa_member *items, dxa_dir *dirs,
                     xx_pd_struct *pd) {
    dxa_layout l = *layout_in;
    dxa_frame stack[DXA_MAX_DEPTH + 1U];
    uint8_t *visited;
    const uint64_t slots = (uint64_t)(l.index_size - l.dir_table) / l.dir_size;
    const uint64_t heads_end = l.dir_table;
    uint32_t depth = 0U, count = 0U, dir_count = 1U;
    uint64_t steps = 0U;
    int64_t end;
    bool ok = false;
    const uint8_t *entry;
    if (slots == 0U) return false;
    visited = (uint8_t *)xx_mem_calloc((size_t)((slots + 7U) / 8U), 1U);
    if (!visited) return false;
    end = l.index_offset + (int64_t)l.index_size;
    if (end < l.data_start) end = l.data_start;
    visited[0] = 1U;
    entry = index + l.dir_table;
    if (!dxa_is_minus_one(&l, dxa_field(&l, entry, 1U))) goto done;
    stack[0].dir_offset = 0U;
    stack[0].dir_number = 0U;
    stack[0].next = 0U;
    stack[0].count = dxa_field(&l, entry, 2U);
    stack[0].first = dxa_field(&l, entry, 3U);
    if (dirs) {
        dirs[0].name = 0U;
        dirs[0].parent = UINT32_MAX;
    }
    for (;;) {
        dxa_frame *frame = &stack[depth];
        uint64_t head_offset;
        const uint8_t *head;
        uint64_t name, attr, data;
        size_t name_length;
        if (frame->next == 0U &&
            (frame->count > (heads_end - l.file_table) / l.head_size ||
             frame->first > heads_end - l.file_table ||
             frame->count * l.head_size >
                 heads_end - l.file_table - frame->first))
            goto done;
        if (frame->next >= frame->count) {
            if (depth == 0U) break;
            --depth;
            continue;
        }
        if ((++steps & DXA_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd))
            goto done;
        head_offset = l.file_table + frame->first + frame->next * l.head_size;
        ++frame->next;
        head = index + head_offset;
        name = dxa_field(&l, head, 0U);
        attr = dxa_field(&l, head, 1U);
        if (!dxa_name_at(&l, index, name, &name_length)) goto done;
        /* three FILETIMEs follow the attributes */
        if (l.version >= 6U)
            data = xx_data_get_u64(head + 0x28, 8, 0, false);
        else
            data = xx_data_get_u32(head + 0x20, 4, 0, false);
        if (attr & DXA_ATTR_DIRECTORY) {
            const uint8_t *child;
            uint64_t slot;
            if (data == 0U || data % l.dir_size != 0U ||
                data > (uint64_t)(l.index_size - l.dir_table) - l.dir_size)
                goto done;
            slot = data / l.dir_size;
            if (slot >= slots || (visited[slot / 8U] & (1U << (slot % 8U))) ||
                depth + 1U > DXA_MAX_DEPTH)
                goto done;
            visited[slot / 8U] |= (uint8_t)(1U << (slot % 8U));
            child = index + l.dir_table + data;
            if (dxa_field(&l, child, 1U) != frame->dir_offset) goto done;
            if (dirs) {
                dirs[dir_count].name = (uint32_t)name;
                dirs[dir_count].parent = frame->dir_number;
            }
            ++depth;
            stack[depth].dir_offset = (uint32_t)data;
            stack[depth].dir_number = dir_count;
            stack[depth].next = 0U;
            stack[depth].count = dxa_field(&l, child, 2U);
            stack[depth].first = dxa_field(&l, child, 3U);
            ++dir_count;
        } else {
            uint64_t size, stored;
            bool packed = false;
            if (l.version >= 6U) {
                size = xx_data_get_u64(head + 0x30, 8, 0, false);
                stored = xx_data_get_u64(head + 0x38, 8, 0, false);
                if (stored != UINT64_MAX) packed = true;
            } else {
                size = xx_data_get_u32(head + 0x24, 4, 0, false);
                stored = l.version >= 2U ? xx_data_get_u32(head + 0x28, 4, 0, false) : UINT32_MAX;
                if (stored != UINT32_MAX) packed = true;
            }
            if (!packed) stored = size;
            if (count >= DXA_MAX_MEMBERS ||
                data > (uint64_t)l.total ||
                (uint64_t)l.data_start > (uint64_t)l.total - data ||
                stored > (uint64_t)l.total - data - (uint64_t)l.data_start ||
                (packed && (stored < DXA_LZ_HEADER || size > UINT32_MAX)))
                goto done;
            if ((int64_t)(l.data_start + data + stored) > end)
                end = (int64_t)(l.data_start + data + stored);
            if (items) {
                dxa_member *m = &items[count];
                m->head = (int64_t)head_offset;
                m->offset = l.data_start + (int64_t)data;
                m->stored = (int64_t)stored;
                m->size = (int64_t)size;
                m->packed = packed;
                m->renamed = false;
                m->name = (uint32_t)name;
                m->dir = frame->dir_number;
            }
            ++count;
        }
    }
    out->count = count;
    out->dirs = dir_count;
    out->end = end;
    ok = true;
done:
    xx_mem_free(visited);
    return ok;
}

static bool dxa_load_index(Abstractformat *format, const dxa_layout *l,
                           uint8_t **result) {
    uint8_t *index = (uint8_t *)xx_mem_alloc(l->index_size);
    if (!index) return false;
    if (!dxa_read_at(format->device, format->base_address + l->index_offset,
                     index, l->index_size)) {
        xx_mem_free(index);
        return false;
    }
    dxa_xor(index, l->index_size, l->key, dxa_index_key_position(l));
    *result = index;
    return true;
}

/* Recover the key and walk the index.  On success *index (if requested)
 * holds the decrypted index. */
static bool dxa_parse(Abstractformat *format, dxa_layout *layout,
                      uint8_t **index_out, xx_pd_struct *pd) {
    uint8_t raw[DXA_V6_HEADER];
    int64_t total;
    size_t have;
    int attempt;
    if (index_out) *index_out = NULL;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    total -= format->base_address;
    if (total < 0x18) return false;
    have = total < (int64_t)sizeof(raw) ? (size_t)total : sizeof(raw);
    xx_mem_zero(raw, sizeof(raw));
    if (!dxa_read_at(format->device, format->base_address, raw, have))
        return false;
    /* Version 6 first, then 4..1, as GARbro guesses. */
    for (attempt = 0; attempt < 5; ++attempt) {
        dxa_layout l;
        uint8_t *index = NULL;
        bool ok;
        xx_mem_zero(&l, sizeof(l));
        l.total = total;
        if (attempt == 0 ? !dxa_guess_v6(raw, total, &l)
                         : !dxa_guess_v4(raw, total, (uint32_t)(5 - attempt),
                                         &l))
            continue;
        if (!dxa_root_plausible(format, &l) ||
            !dxa_load_index(format, &l, &index))
            continue;
        ok = dxa_walk(&l, &l, index, NULL, NULL, pd);
        if (ok) {
            *layout = l;
            if (index_out)
                *index_out = index;
            else
                xx_mem_free(index);
            return true;
        }
        xx_mem_free(index);
        if (pd && xx_pd_is_stopped(pd)) return false;
    }
    return false;
}

/* ---- member names ------------------------------------------------------ */

static bool dxa_valid_utf8(const uint8_t *raw, size_t length) {
    size_t index = 0U;
    while (index < length) {
        uint8_t c = raw[index];
        size_t extra, k;
        uint32_t cp;
        if (c < 0x80U) {
            ++index;
            continue;
        }
        if (c >= 0xc2U && c <= 0xdfU) {
            extra = 1U;
            cp = c & 0x1fU;
        } else if (c >= 0xe0U && c <= 0xefU) {
            extra = 2U;
            cp = c & 0x0fU;
        } else if (c >= 0xf0U && c <= 0xf4U) {
            extra = 3U;
            cp = c & 0x07U;
        } else {
            return false;
        }
        if (length - index - 1U < extra) return false;
        for (k = 1U; k <= extra; ++k) {
            uint8_t t = raw[index + k];
            if ((t & 0xc0U) != 0x80U) return false;
            cp = (cp << 6U) | (t & 0x3fU);
        }
        if ((extra == 2U && (cp < 0x800U || (cp >= 0xd800U && cp <= 0xdfffU))) ||
            (extra == 3U && (cp < 0x10000U || cp > 0x10ffffU)))
            return false;
        index += extra + 1U;
    }
    return true;
}

static size_t dxa_put_escape(char *out, uint8_t c) {
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '%';
    out[1] = digits[(c >> 4U) & 0x0fU];
    out[2] = digits[c & 0x0fU];
    return 3U;
}

/* Append one converted component; `out` has room for 3 * length bytes. */
static size_t dxa_convert_component(const uint8_t *raw, size_t length,
                                    char *out) {
    size_t at = 0U, index;
    bool utf8 = dxa_valid_utf8(raw, length);
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c == (uint8_t)'%' || c == (uint8_t)'/' || c == (uint8_t)'\\' ||
            (!utf8 && c >= 0x80U))
            at += dxa_put_escape(out + at, c);
        else
            out[at++] = (char)c;
    }
    return at;
}

/* Build the '/' joined path of member `index` into stream->name. */
static size_t dxa_build_path(const dxa_stream *s, size_t index) {
    const dxa_member *m = &s->items[index];
    uint32_t chain[DXA_MAX_DEPTH + 1U];
    uint32_t depth = 0U, d = m->dir;
    size_t at = 0U, length;
    const uint8_t *raw;
    while (d != 0U && d < s->layout.dirs && depth < DXA_MAX_DEPTH) {
        chain[depth++] = d;
        d = s->dirs[d].parent;
    }
    while (depth != 0U) {
        --depth;
        raw = dxa_name_at(&s->layout, s->index, s->dirs[chain[depth]].name,
                          &length);
        if (!raw) break;
        at += dxa_convert_component(raw, length, s->name + at);
        s->name[at++] = '/';
    }
    raw = dxa_name_at(&s->layout, s->index, m->name, &length);
    if (raw) at += dxa_convert_component(raw, length, s->name + at);
    s->name[at] = 0;
    return at;
}

static uint64_t dxa_name_hash(const char *name, size_t length) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    size_t index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)name[index];
        if (c >= (uint8_t)'A' && c <= (uint8_t)'Z')
            c = (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a');
        hash ^= (uint64_t)c;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

static void dxa_insert_suffix(char *name, size_t length, uint32_t index) {
    char suffix[2 + 10];
    char digits[10];
    size_t suffix_length = 0U, digit_count = 0U, component = 0U, at, tail;
    size_t dot = length;
    for (at = 0U; at < length; ++at)
        if (name[at] == '/') component = at + 1U;
    for (at = length; at > component + 1U; --at)
        if (name[at - 1U] == '.') {
            dot = at - 1U;
            break;
        }
    do {
        digits[digit_count++] = (char)('0' + (char)(index % 10U));
        index /= 10U;
    } while (index != 0U && digit_count < sizeof(digits));
    suffix[suffix_length++] = '%';
    suffix[suffix_length++] = '_';
    while (digit_count != 0U) suffix[suffix_length++] = digits[--digit_count];
    if (length + suffix_length >= DXA_PATH_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at)
        name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

static bool dxa_reserved_component(const char *segment, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",    "AUX",
                                          "NUL",    "CLOCK$", "CONIN$",
                                          "CONOUT$"};
    char stem[8];
    size_t stem_length = 0U, index;
    while (stem_length < length && segment[stem_length] != '.') ++stem_length;
    while (stem_length != 0U && segment[stem_length - 1U] == ' ')
        --stem_length;
    if (stem_length < 3U || stem_length > sizeof(stem) - 1U) return false;
    for (index = 0U; index < stem_length; ++index) {
        char c = segment[index];
        stem[index] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    stem[stem_length] = 0;
    if (stem_length == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') ||
         (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T')))
        return true;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (xx_str_len(devices[index]) == stem_length &&
            xx_rt_memcmp(stem, devices[index], stem_length) == 0)
            return true;
    return false;
}

static bool dxa_safe_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || c == 0x7fU ||
            (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || segment[length - 1U] == '.' ||
                segment[length - 1U] == ' ' ||
                dxa_reserved_component(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static int dxa_compare_keys(const void *left, const void *right) {
    const dxa_key *a = (const dxa_key *)left;
    const dxa_key *b = (const dxa_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

static bool dxa_mark_duplicates(dxa_stream *s) {
    dxa_key *keys;
    size_t index;
    if (s->count < 2U) return true;
    keys = (dxa_key *)xx_mem_alloc(s->count * sizeof(*keys));
    if (!keys) return false;
    for (index = 0U; index < s->count; ++index) {
        size_t length = dxa_build_path(s, index);
        keys[index].hash = dxa_name_hash(s->name, length);
        keys[index].index = (uint32_t)index;
    }
    xx_rt_qsort(keys, s->count, sizeof(*keys), dxa_compare_keys);
    for (index = 1U; index < s->count; ++index)
        if (keys[index].hash == keys[index - 1U].hash)
            s->items[keys[index].index].renamed = true;
    xx_mem_free(keys);
    return true;
}

/* ---- streams ----------------------------------------------------------- */

static void dxa_stream_free(void *opaque) {
    dxa_stream *s = (dxa_stream *)opaque;
    if (!s) return;
    if (s->index) xx_mem_free(s->index);
    if (s->items) xx_mem_free(s->items);
    if (s->dirs) xx_mem_free(s->dirs);
    if (s->name) xx_mem_free(s->name);
    xx_mem_free(s);
}

static bool dxa_open_stream(Abstractformat *format, dxa_stream **result,
                            xx_pd_struct *pd) {
    dxa_stream *s = (dxa_stream *)xx_mem_calloc(1U, sizeof(*s));
    dxa_layout second;
    if (!s) return false;
    if (!dxa_parse(format, &s->layout, &s->index, pd)) goto fail;
    s->items = (dxa_member *)xx_mem_calloc(
        s->layout.count ? s->layout.count : 1U, sizeof(*s->items));
    s->dirs = (dxa_dir *)xx_mem_calloc(s->layout.dirs, sizeof(*s->dirs));
    s->name = (char *)xx_mem_alloc(DXA_PATH_BUFFER);
    if (!s->items || !s->dirs || !s->name) goto fail;
    second = s->layout;
    if (!dxa_walk(&s->layout, &second, s->index, s->items, s->dirs, pd) ||
        second.count != s->layout.count || second.dirs != s->layout.dirs)
        goto fail;
    s->count = s->layout.count;
    if (!dxa_mark_duplicates(s)) goto fail;
    *result = s;
    return true;
fail:
    dxa_stream_free(s);
    return false;
}

/* ---- data -------------------------------------------------------------- */

typedef struct dxa_input_s {
    xx_io_device *device;
    int64_t position; /**< Absolute device offset of the next refill. */
    int64_t left;     /**< Bytes not yet read from the device. */
    uint64_t key_position;
    const uint8_t *key;
    uint8_t *buffer;
    size_t at, length;
} dxa_input;

static bool dxa_input_refill(dxa_input *in) {
    size_t chunk;
    if (in->left <= 0) return false;
    chunk = in->left > (int64_t)DXA_IO_CHUNK ? DXA_IO_CHUNK : (size_t)in->left;
    if (!dxa_read_at(in->device, in->position, in->buffer, chunk)) return false;
    dxa_xor(in->buffer, chunk, in->key, in->key_position);
    in->key_position += chunk;
    in->position += (int64_t)chunk;
    in->left -= (int64_t)chunk;
    in->at = 0U;
    in->length = chunk;
    return true;
}

static bool dxa_input_byte(dxa_input *in, uint8_t *value) {
    if (in->at == in->length && !dxa_input_refill(in)) return false;
    *value = in->buffer[in->at++];
    return true;
}

typedef struct dxa_output_s {
    xx_io_device *destination;
    uint8_t *ring;
    uint32_t ring_size;
    uint32_t write;   /**< Ring position of the next byte. */
    uint32_t flushed; /**< Ring position of the first unwritten byte. */
    uint64_t pending; /**< Bytes produced but not yet written. */
    uint64_t produced;
} dxa_output;

static bool dxa_output_flush(dxa_output *out) {
    uint64_t left = out->pending;
    while (left != 0U) {
        uint32_t piece = out->ring_size - out->flushed;
        if ((uint64_t)piece > left) piece = (uint32_t)left;
        if (!dxa_write_all(out->destination, out->ring + out->flushed, piece))
            return false;
        out->flushed += piece;
        if (out->flushed == out->ring_size) out->flushed = 0U;
        left -= piece;
    }
    out->pending = 0U;
    return true;
}

static bool dxa_output_byte(dxa_output *out, uint8_t value) {
    if (out->pending == out->ring_size && !dxa_output_flush(out)) return false;
    out->ring[out->write] = value;
    if (++out->write == out->ring_size) out->write = 0U;
    ++out->pending;
    ++out->produced;
    return true;
}

static bool dxa_decode(dxa_input *in, const dxa_member *m,
                       xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t head[DXA_LZ_HEADER];
    uint32_t unpacked, packed, at;
    uint64_t remaining, steps = 0U;
    uint8_t code;
    dxa_output out;
    bool ok = false;
    for (at = 0U; at < DXA_LZ_HEADER; ++at)
        if (!dxa_input_byte(in, &head[at])) return false;
    unpacked = xx_data_get_u32(head, 4, 0, false);
    packed = xx_data_get_u32(head + 4, 4, 0, false);
    code = head[8];
    if ((int64_t)unpacked != m->size || packed < DXA_LZ_HEADER ||
        (int64_t)packed > m->stored)
        return false;
    remaining = packed - DXA_LZ_HEADER;
    xx_mem_zero(&out, sizeof(out));
    out.destination = destination;
    out.ring_size = unpacked > DXA_RING ? DXA_RING : unpacked;
    if (out.ring_size == 0U) return remaining == 0U;
    out.ring = (uint8_t *)xx_mem_alloc(out.ring_size);
    if (!out.ring) return false;
    while (remaining != 0U) {
        uint8_t b, extra;
        uint32_t count, distance, source;
        if ((++steps & 0xfffffU) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
        if (!dxa_input_byte(in, &b)) goto done;
        --remaining;
        if (b != code) {
            if (out.produced >= unpacked || !dxa_output_byte(&out, b)) goto done;
            continue;
        }
        if (remaining == 0U || !dxa_input_byte(in, &b)) goto done;
        --remaining;
        if (b == code) {
            if (out.produced >= unpacked || !dxa_output_byte(&out, b)) goto done;
            continue;
        }
        if (b > code) --b;
        count = (uint32_t)b >> 3U;
        if (b & 4U) {
            if (remaining == 0U || !dxa_input_byte(in, &extra)) goto done;
            --remaining;
            count |= (uint32_t)extra << 5U;
        }
        count += 4U;
        switch (b & 3U) {
        case 0U:
            if (remaining < 1U || !dxa_input_byte(in, &extra)) goto done;
            remaining -= 1U;
            distance = extra;
            break;
        case 1U:
            if (remaining < 2U || !dxa_input_byte(in, &extra)) goto done;
            distance = extra;
            if (!dxa_input_byte(in, &extra)) goto done;
            distance |= (uint32_t)extra << 8U;
            remaining -= 2U;
            break;
        case 2U:
            if (remaining < 3U || !dxa_input_byte(in, &extra)) goto done;
            distance = extra;
            if (!dxa_input_byte(in, &extra)) goto done;
            distance |= (uint32_t)extra << 8U;
            if (!dxa_input_byte(in, &extra)) goto done;
            distance |= (uint32_t)extra << 16U;
            remaining -= 3U;
            break;
        default:
            goto done;
        }
        ++distance;
        if ((uint64_t)distance > out.produced || distance > DXA_MAX_DISTANCE ||
            distance > out.ring_size ||
            (uint64_t)count > (uint64_t)unpacked - out.produced)
            goto done;
        source = out.write >= distance ? out.write - distance
                                       : out.write + out.ring_size - distance;
        while (count-- != 0U) {
            uint8_t value = out.ring[source];
            if (++source == out.ring_size) source = 0U;
            if (!dxa_output_byte(&out, value)) goto done;
        }
    }
    ok = out.produced == unpacked && dxa_output_flush(&out);
done:
    xx_mem_free(out.ring);
    return ok;
}

static bool dxa_unpack_member(xx_io_device *source, int64_t base,
                              const dxa_layout *l, const dxa_member *m,
                              xx_io_device *destination, xx_pd_struct *pd) {
    dxa_input in;
    bool ok = true;
    xx_mem_zero(&in, sizeof(in));
    in.device = source;
    in.position = base + m->offset;
    in.left = m->stored;
    in.key = l->key;
    in.key_position = l->version >= 6U ? (uint64_t)m->size
                                       : (uint64_t)m->offset;
    if (m->stored == 0) return !m->packed && m->size == 0;
    in.buffer = (uint8_t *)xx_mem_alloc(DXA_IO_CHUNK);
    if (!in.buffer) return false;
    if (m->packed) {
        ok = dxa_decode(&in, m, destination, pd);
    } else {
        while (in.left > 0) {
            if ((pd && xx_pd_is_stopped(pd)) || !dxa_input_refill(&in) ||
                !dxa_write_all(destination, in.buffer, in.length)) {
                ok = false;
                break;
            }
        }
    }
    xx_mem_free(in.buffer);
    return ok;
}

/* ---- records ----------------------------------------------------------- */

static bool dxa_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *dxa_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool dxa_set_record(Abstractformat *format, xx_archive_record *record,
                           dxa_stream *s, size_t index) {
    const dxa_member *m = &s->items[index];
    size_t length;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    length = dxa_build_path(s, index);
    if (m->renamed) dxa_insert_suffix(s->name, length, (uint32_t)index);
    record->header_offset =
        format->base_address + s->layout.index_offset + m->head;
    record->header_size = s->layout.head_size;
    record->data_offset = format->base_address + m->offset;
    record->compressed_size = m->stored;
    return xx_archive_record_set_original_name(record, s->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)m->stored) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)m->size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSION_METHOD,
               m->packed ? XX_DXA_METHOD_LZ : XX_DXA_METHOD_STORE) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API -------------------------------------------------------- */

void xx_dxa_init(xx_dxa *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_DXA_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "dxa");
    archive->format.check_is_valid = xx_dxa_check_is_valid;
    archive->format.handle_base_info = xx_dxa_handle_base_info;
    archive->format.get_format_size = xx_dxa_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_dxa_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_dxa_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_dxa_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_dxa_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_dxa_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_dxa_free_archive_records_reading;
}

xx_dxa *xx_dxa_create(xx_io_device *device, int64_t base_address) {
    xx_dxa *archive = (xx_dxa *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_dxa_init(archive, device, base_address);
    return archive;
}

void xx_dxa_destroy(xx_dxa *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_dxa_free(xx_dxa *archive) {
    if (!archive) return;
    xx_dxa_destroy(archive);
    xx_mem_free(archive);
}

bool xx_dxa_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    dxa_layout layout;
    return dxa_parse(format, &layout, NULL, pd);
}

bool xx_dxa_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    dxa_layout layout;
    xx_dxa *archive;
    if (!dxa_parse(format, &layout, NULL, pd)) return false;
    archive = (xx_dxa *)format;
    archive->number_of_records = layout.count;
    archive->dxa_version = layout.version;
    archive->index_size = layout.index_size;
    archive->index_offset = layout.index_offset;
    archive->data_start = layout.data_start;
    xx_rt_memcpy(archive->key, layout.key, DXA_KEY);
    format->version[0] = (char)('0' + (char)layout.version);
    format->version[1] = 0;
    format->number_of_archive_records = layout.count;
    format->format_size = layout.end;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_dxa_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_dxa_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_dxa_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_dxa_handle_base_info(format, pd))
               ? ((xx_dxa *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_dxa_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    dxa_stream *s;
    xx_archive_record_state *state;
    if (!format || (!format->base_info_handled &&
                    !xx_dxa_handle_base_info(format, pd)))
        return NULL;
    if (!dxa_open_stream(format, &s, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        dxa_stream_free(s);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = s;
    state->free_internal = dxa_stream_free;
    state->total_records = s->count;
    if (!dxa_copy_options(&state->options, options) ||
        (s->count != 0U &&
         !dxa_set_record(format, &state->current_record, s, 0U))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    /* An archive with no members yields a state without a record. */
    state->has_record = s->count != 0U;
    return state;
}

const xx_archive_record *xx_dxa_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_dxa_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    dxa_stream *s;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(s = (dxa_stream *)state->internal_state) || ++s->at >= s->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        dxa_set_record(format, &state->current_record, s, s->at);
    return state->has_record;
}

bool xx_dxa_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    dxa_stream *s;
    const dxa_member *m;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(s = (dxa_stream *)state->internal_state) || s->at >= s->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    m = &s->items[s->at];
    path_option = dxa_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: decode into nothing, which verifies the member. */
        return dxa_unpack_member(format->device, format->base_address,
                                 &s->layout, m, NULL, pd);
    if (!dxa_safe_name(s->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", s->name)
               : xx_str_concat(base, s->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = dxa_unpack_member(format->device, format->base_address,
                                   &s->layout, m, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_dxa_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
