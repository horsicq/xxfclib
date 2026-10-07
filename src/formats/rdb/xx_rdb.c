/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rdb/xx_rdb.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved locally until the enumerator
 * lands. The RDB macro is the alias xxfc_defs.h defines next to every
 * XX_FILE_TYPE_* value, so this block heals itself the moment the enum grows
 * an RDB member; delete it then. */
#ifdef RDB
#define XX_RDB_FILE_TYPE XX_FILE_TYPE_RDB
#else
#define XX_RDB_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_RDB_SCAN_STEP 512U
#define XX_RDB_END_OF_CHAIN 0xFFFFFFFFU
#define XX_RDB_ID_RDSK 0x5244534BU /* 'RDSK' */
#define XX_RDB_ID_PART 0x50415254U /* 'PART' */
#define XX_RDB_ID_FSHD 0x46534844U /* 'FSHD' */
#define XX_RDB_ID_LSEG 0x4C534547U /* 'LSEG' */
#define XX_RDB_MIN_BLOCK 256U
#define XX_RDB_MAX_BLOCK 32768U
/* Longs each block type must checksum to hold the fields read from it. */
#define XX_RDB_RDSK_MIN_LONGS 40U  /* through RDBBlocksHi (long 33) */
#define XX_RDB_PART_MIN_LONGS 43U  /* through de_HighCyl (long 42) */
#define XX_RDB_FSHD_MIN_LONGS 19U  /* through SegListBlocks (long 18) */
#define XX_RDB_LSEG_HEADER_LONGS 5U
#define XX_RDB_NAME_FIELD 32U
#define XX_RDB_COMMENT_SIZE 64U

typedef struct xx_rdb_entry_s {
    char *name;                       /**< "partition3" / "filesystem1". */
    char drive_name[XX_RDB_NAME_FIELD];
    bool is_filesystem;
    uint32_t index;                   /**< 1-based position in its chain. */
    int64_t header_offset;
    int64_t data_offset;              /**< Partition payload, or -1. */
    int64_t data_size;                /**< Bytes the record extracts. */
    uint64_t declared_size;
    uint32_t dos_type;
    uint32_t flags;                   /**< PART Flags / FSHD Version. */
    uint32_t low_cyl;
    uint32_t high_cyl;
    uint32_t *lseg_blocks;            /**< Filesystem only. */
    uint32_t lseg_count;
} xx_rdb_entry;

typedef struct xx_rdb_private_s {
    xx_rdb_entry *entries;
    size_t count;
    size_t capacity;
    int64_t input_size;
    int64_t archive_end;
    uint32_t block_bytes;
    uint32_t rdsk_block;
    uint32_t partitions;
    uint32_t filesystems;
    uint32_t lseg_total;              /**< LSEG blocks accepted so far. */
    uint8_t *block;                   /**< Scratch of block_bytes bytes. */
} xx_rdb_private;

typedef struct xx_rdb_archive_stream_s {
    xx_rdb_private parsed;
    size_t index;
} xx_rdb_archive_stream;

static void xx_rdb_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* Read up to size bytes at an absolute offset; returns the count read. */
static size_t xx_rdb_read_at(xx_io_device *device, int64_t offset, void *data,
                             size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || !data || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return 0U;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) break;
        done += (size_t)got;
    }
    return done;
}

/* The first longs of a block add up to zero. */
static bool xx_rdb_checksum_ok(const uint8_t *data, uint32_t longs) {
    uint32_t sum = 0U;
    uint32_t index;
    for (index = 0U; index < longs; ++index) {
        sum += xx_data_get_u32(data + (size_t)index * 4U, 4, 0, true);
    }
    return sum == 0U;
}

/* a * b, refusing a result above INT64_MAX. */
static bool xx_rdb_mul(uint64_t a, uint64_t b, uint64_t *result) {
    if (a != 0U && b > (uint64_t)INT64_MAX / a) return false;
    *result = a * b;
    return true;
}

/* base + block * block_bytes, refusing anything that does not fit. */
static bool xx_rdb_block_offset(int64_t base, uint32_t block,
                                uint32_t block_bytes, int64_t *result) {
    uint64_t bytes;
    if (base < 0 || !xx_rdb_mul(block, block_bytes, &bytes) ||
        bytes > (uint64_t)(INT64_MAX - base)) {
        return false;
    }
    *result = base + (int64_t)bytes;
    return true;
}

static bool xx_rdb_is_power_of_two_block(uint32_t value) {
    return value >= XX_RDB_MIN_BLOCK && value <= XX_RDB_MAX_BLOCK &&
           (value & (value - 1U)) == 0U;
}

/* "<prefix><index>", CRT free. */
static char *xx_rdb_make_name(const char *prefix, unsigned index) {
    char digits[16];
    char buffer[40];
    size_t used = xx_str_len(prefix);
    size_t count = 0U;
    if (used > 20U) return NULL;
    xx_rt_memcpy(buffer, prefix, used);
    do {
        digits[count++] = (char)('0' + (index % 10U));
        index /= 10U;
    } while (index != 0U && count < sizeof(digits));
    while (count != 0U) buffer[used++] = digits[--count];
    buffer[used] = '\0';
    return xx_str_create(buffer);
}

/* A BCPL string of at most 31 characters; only printable ASCII is kept. */
static void xx_rdb_copy_bstr(const uint8_t *raw,
                             char out[XX_RDB_NAME_FIELD]) {
    size_t length = raw[0];
    size_t index;
    if (length > XX_RDB_NAME_FIELD - 1U) length = XX_RDB_NAME_FIELD - 1U;
    for (index = 0U; index < length; ++index) {
        uint8_t ch = raw[1U + index];
        out[index] = (ch >= 0x20U && ch < 0x7FU) ? (char)ch : '?';
    }
    out[length] = '\0';
}

/* DosType as its usual tag: 0x444F5303 -> "DOS3", 0x50465303 -> "PFS3". */
static void xx_rdb_dos_type_tag(uint32_t dos_type, char out[20]) {
    size_t used = 0U;
    int shift;
    for (shift = 24; shift >= 0; shift -= 8) {
        uint8_t ch = (uint8_t)(dos_type >> shift);
        if (ch >= 0x20U && ch < 0x7FU) {
            out[used++] = (char)ch;
        } else if (ch < 10U) {
            out[used++] = (char)('0' + ch);
        } else {
            static const char hex[] = "0123456789ABCDEF";
            out[used++] = '\\';
            out[used++] = 'x';
            out[used++] = hex[ch >> 4];
            out[used++] = hex[ch & 15U];
        }
    }
    out[used] = '\0';
}

/* Read the block with the given number and check its ID, SummedLongs
 * (min_longs .. block size) and checksum. *next receives the Next field when
 * the ID matched, so a chain can go on past a damaged block; the result says
 * whether the block itself is usable. *type_ok is false when the block is not
 * of the wanted type or cannot be read at all. */
static bool xx_rdb_read_block(Abstractformat *self, xx_rdb_private *parsed,
                              uint32_t block, uint32_t id, uint32_t min_longs,
                              int64_t *offset_out, uint32_t *longs_out,
                              uint32_t *next, bool *type_ok) {
    int64_t offset;
    size_t got;
    uint32_t longs;
    *type_ok = false;
    if (!xx_rdb_block_offset(self->base_address, block, parsed->block_bytes,
                             &offset) ||
        offset >= parsed->input_size) {
        return false;
    }
    got = xx_rdb_read_at(self->device, offset, parsed->block,
                         parsed->block_bytes);
    if (got < 20U || xx_data_get_u32(parsed->block + 0U, 4, 0, true) != id) return false;
    *type_ok = true;
    *next = xx_data_get_u32(parsed->block + 16U, 4, 0, true);
    longs = xx_data_get_u32(parsed->block + 4U, 4, 0, true);
    if (offset + (int64_t)got > parsed->archive_end) {
        parsed->archive_end = offset + (int64_t)got;
    }
    if (longs < min_longs || longs > got / 4U ||
        !xx_rdb_checksum_ok(parsed->block, longs)) {
        return false;
    }
    *offset_out = offset;
    *longs_out = longs;
    return true;
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

static void xx_rdb_entry_cleanup(xx_rdb_entry *entry) {
    if (entry->name) xx_str_free(entry->name);
    if (entry->lseg_blocks) xx_mem_free(entry->lseg_blocks);
    entry->name = NULL;
    entry->lseg_blocks = NULL;
}

static void xx_rdb_private_reset(xx_rdb_private *parsed) {
    xx_rt_memset(parsed, 0, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static void xx_rdb_private_cleanup(xx_rdb_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        xx_rdb_entry_cleanup(&parsed->entries[index]);
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    if (parsed->block) xx_mem_free(parsed->block);
    xx_rdb_private_reset(parsed);
}

static bool xx_rdb_append_entry(xx_rdb_private *parsed, xx_rdb_entry *entry) {
    xx_rdb_entry *grown;
    size_t capacity;
    if (!entry->name ||
        parsed->count >= XX_RDB_MAX_PARTITIONS + XX_RDB_MAX_FILESYSTEMS) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 8U;
        grown = (xx_rdb_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_rt_memset(entry, 0, sizeof(*entry));
    return true;
}

/* Find the RDSK block in the first XX_RDB_SCAN_BLOCKS 512-byte blocks. The
 * block must checksum, declare a power-of-two BlockBytes that its own
 * position is a multiple of, and hold its SummedLongs. */
static bool xx_rdb_find_rdsk(Abstractformat *self, xx_rdb_private *parsed,
                             uint8_t *rdsk_copy) {
    uint8_t window[XX_RDB_SCAN_BLOCKS * XX_RDB_SCAN_STEP];
    size_t available;
    size_t got;
    uint32_t index;
    if (parsed->input_size - self->base_address < (int64_t)XX_RDB_MIN_BLOCK) {
        return false;
    }
    available = sizeof(window);
    if ((int64_t)available > parsed->input_size - self->base_address) {
        available = (size_t)(parsed->input_size - self->base_address);
    }
    got = xx_rdb_read_at(self->device, self->base_address, window, available);
    for (index = 0U; index < XX_RDB_SCAN_BLOCKS; ++index) {
        size_t position = (size_t)index * XX_RDB_SCAN_STEP;
        const uint8_t *block = window + position;
        uint32_t longs;
        uint32_t block_bytes;
        if (position + XX_RDB_MIN_BLOCK > got) break;
        if (xx_data_get_u32(block + 0U, 4, 0, true) != XX_RDB_ID_RDSK) continue;
        longs = xx_data_get_u32(block + 4U, 4, 0, true);
        block_bytes = xx_data_get_u32(block + 16U, 4, 0, true);
        if (longs < XX_RDB_RDSK_MIN_LONGS ||
            longs > XX_RDB_SCAN_STEP / 4U ||
            (size_t)longs * 4U > got - position ||
            !xx_rdb_is_power_of_two_block(block_bytes) ||
            (size_t)longs * 4U > block_bytes ||
            position % block_bytes != 0U ||
            !xx_rdb_checksum_ok(block, longs)) {
            continue;
        }
        parsed->block_bytes = block_bytes;
        parsed->rdsk_block = (uint32_t)(position / block_bytes);
        xx_rt_memset(rdsk_copy, 0, XX_RDB_SCAN_STEP);
        xx_rt_memcpy(rdsk_copy, block, (size_t)longs * 4U);
        parsed->archive_end = self->base_address + (int64_t)position +
                              (int64_t)longs * 4;
        return true;
    }
    return false;
}

/* Decode the PART block now in parsed->block. A partition with nonsense
 * geometry or one that starts past the device is not published. */
static bool xx_rdb_collect_partition(Abstractformat *self,
                                     xx_rdb_private *parsed, uint32_t longs,
                                     int64_t header_offset, uint32_t position) {
    const uint8_t *block = parsed->block;
    const uint8_t *env = block + 128;
    xx_rdb_entry entry;
    uint32_t table_size = xx_data_get_u32(env + 0U, 4, 0, true);
    uint32_t surfaces = xx_data_get_u32(env + 12U, 4, 0, true);
    uint32_t blocks_per_track = xx_data_get_u32(env + 20U, 4, 0, true);
    uint64_t cylinder_bytes;
    uint64_t start;
    uint64_t declared;
    int64_t available;
    xx_rt_memset(&entry, 0, sizeof(entry));
    entry.low_cyl = xx_data_get_u32(env + 36U, 4, 0, true);
    entry.high_cyl = xx_data_get_u32(env + 40U, 4, 0, true);
    entry.flags = xx_data_get_u32(block + 20U, 4, 0, true);
    /* DosType is environment long 16, present when the table reaches it. */
    if (table_size >= 16U && longs >= 32U + 17U) {
        entry.dos_type = xx_data_get_u32(env + 64U, 4, 0, true);
    }
    if (table_size < 10U || surfaces == 0U || blocks_per_track == 0U ||
        entry.high_cyl < entry.low_cyl) {
        return true;
    }
    /* Cylinders are Surfaces * BlocksPerTrack RDB blocks (BlockBytes each),
     * as the Linux amiga partition parser and amitools count them. */
    if (!xx_rdb_mul((uint64_t)surfaces * blocks_per_track, parsed->block_bytes,
                    &cylinder_bytes) ||
        !xx_rdb_mul(cylinder_bytes, entry.low_cyl, &start) ||
        !xx_rdb_mul(cylinder_bytes,
                    (uint64_t)entry.high_cyl - entry.low_cyl + 1U,
                    &declared) ||
        start > (uint64_t)(INT64_MAX - self->base_address)) {
        return true;
    }
    entry.data_offset = self->base_address + (int64_t)start;
    if (entry.data_offset >= parsed->input_size) return true;
    available = parsed->input_size - entry.data_offset;
    if (declared < (uint64_t)available) available = (int64_t)declared;
    entry.declared_size = declared;
    entry.data_size = available;
    entry.header_offset = header_offset;
    entry.index = position;
    xx_rdb_copy_bstr(block + 36, entry.drive_name);
    entry.name = xx_rdb_make_name("partition", position);
    if (!xx_rdb_append_entry(parsed, &entry)) {
        xx_rdb_entry_cleanup(&entry);
        return false;
    }
    if (entry.data_offset + available > parsed->archive_end) {
        parsed->archive_end = entry.data_offset + available;
    }
    ++parsed->partitions;
    return true;
}

/* Is block in the first count items of list? */
static bool xx_rdb_seen(const uint32_t *list, uint32_t count, uint32_t block) {
    uint32_t index;
    for (index = 0U; index < count; ++index) {
        if (list[index] == block) return true;
    }
    return false;
}

static bool xx_rdb_walk_partitions(Abstractformat *self,
                                   xx_rdb_private *parsed, uint32_t first,
                                   xx_pd_struct *pd) {
    uint32_t visited[XX_RDB_MAX_PARTITIONS];
    uint32_t count = 0U;
    uint32_t block = first;
    while (block != XX_RDB_END_OF_CHAIN && block != 0U &&
           count < XX_RDB_MAX_PARTITIONS &&
           !xx_rdb_seen(visited, count, block)) {
        int64_t offset = 0;
        uint32_t longs = 0U;
        uint32_t next = XX_RDB_END_OF_CHAIN;
        bool type_ok;
        bool usable;
        if (pd && xx_pd_is_stopped(pd)) return false;
        visited[count++] = block;
        usable = xx_rdb_read_block(self, parsed, block, XX_RDB_ID_PART,
                                   XX_RDB_PART_MIN_LONGS, &offset, &longs,
                                   &next, &type_ok);
        if (!type_ok) break;
        if (usable &&
            !xx_rdb_collect_partition(self, parsed, longs, offset, count)) {
            return false;
        }
        block = next;
    }
    return true;
}

/* Follow the LSEG chain of one filesystem; false when it is broken, loops
 * or exceeds the budget, in which case the filesystem is dropped. */
static bool xx_rdb_walk_lseg(Abstractformat *self, xx_rdb_private *parsed,
                             uint32_t first, xx_rdb_entry *entry,
                             xx_pd_struct *pd) {
    uint32_t capacity = 0U;
    uint32_t block = first;
    uint64_t total = 0U;
    /* Brent's cycle check: a chain that comes back to a block would run
     * forever, and is caught within about twice its length. */
    uint32_t marker = XX_RDB_END_OF_CHAIN;
    uint32_t power = 1U;
    uint32_t steps = 0U;
    while (block != XX_RDB_END_OF_CHAIN) {
        int64_t offset = 0;
        uint32_t longs = 0U;
        uint32_t next = XX_RDB_END_OF_CHAIN;
        bool type_ok;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (block == marker) return false;
        if (++steps == power) {
            marker = block;
            power *= 2U;
            steps = 0U;
        }
        if (parsed->lseg_total >= XX_RDB_MAX_LSEG_BLOCKS) return false;
        if (!xx_rdb_read_block(self, parsed, block, XX_RDB_ID_LSEG,
                               XX_RDB_LSEG_HEADER_LONGS, &offset, &longs,
                               &next, &type_ok)) {
            return false;
        }
        if (entry->lseg_count == capacity) {
            uint32_t *grown;
            capacity = capacity ? capacity * 2U : 16U;
            grown = (uint32_t *)xx_mem_realloc(entry->lseg_blocks,
                                               capacity * sizeof(uint32_t));
            if (!grown) return false;
            entry->lseg_blocks = grown;
        }
        entry->lseg_blocks[entry->lseg_count++] = block;
        ++parsed->lseg_total;
        total += (uint64_t)(longs - XX_RDB_LSEG_HEADER_LONGS) * 4U;
        block = next;
    }
    entry->data_size = (int64_t)total;
    entry->declared_size = total;
    return true;
}

static bool xx_rdb_walk_filesystems(Abstractformat *self,
                                    xx_rdb_private *parsed, uint32_t first,
                                    xx_pd_struct *pd) {
    uint32_t visited[XX_RDB_MAX_FILESYSTEMS];
    uint32_t count = 0U;
    uint32_t block = first;
    while (block != XX_RDB_END_OF_CHAIN && block != 0U &&
           count < XX_RDB_MAX_FILESYSTEMS &&
           !xx_rdb_seen(visited, count, block)) {
        xx_rdb_entry entry;
        int64_t offset = 0;
        uint32_t longs = 0U;
        uint32_t next = XX_RDB_END_OF_CHAIN;
        uint32_t seg_list;
        bool type_ok;
        bool usable;
        if (pd && xx_pd_is_stopped(pd)) return false;
        visited[count++] = block;
        usable = xx_rdb_read_block(self, parsed, block, XX_RDB_ID_FSHD,
                                   XX_RDB_FSHD_MIN_LONGS, &offset, &longs,
                                   &next, &type_ok);
        if (!type_ok) break;
        block = next;
        if (!usable) continue;
        xx_rt_memset(&entry, 0, sizeof(entry));
        entry.is_filesystem = true;
        entry.dos_type = xx_data_get_u32(parsed->block + 32U, 4, 0, true);
        entry.flags = xx_data_get_u32(parsed->block + 36U, 4, 0, true);
        entry.header_offset = offset;
        entry.data_offset = -1;
        entry.index = count;
        seg_list = xx_data_get_u32(parsed->block + 72U, 4, 0, true);
        if (seg_list == XX_RDB_END_OF_CHAIN || seg_list == 0U) continue;
        if (!xx_rdb_walk_lseg(self, parsed, seg_list, &entry, pd)) {
            xx_rdb_entry_cleanup(&entry);
            if (pd && xx_pd_is_stopped(pd)) return false;
            continue;
        }
        entry.name = xx_rdb_make_name("filesystem", count);
        if (!xx_rdb_append_entry(parsed, &entry)) {
            xx_rdb_entry_cleanup(&entry);
            return false;
        }
        ++parsed->filesystems;
    }
    return true;
}

/* Grow archive_end to base + blocks * unit, clamped to the device. */
static void xx_rdb_extend_end(Abstractformat *self, xx_rdb_private *parsed,
                              uint64_t blocks, uint64_t unit) {
    uint64_t bytes;
    int64_t end;
    if (blocks == 0U || !xx_rdb_mul(blocks, unit, &bytes) ||
        bytes > (uint64_t)(INT64_MAX - self->base_address)) {
        return;
    }
    end = self->base_address + (int64_t)bytes;
    if (end > parsed->input_size) end = parsed->input_size;
    if (end > parsed->archive_end) parsed->archive_end = end;
}

static bool xx_rdb_parse(Abstractformat *self, xx_rdb_private *parsed,
                         xx_pd_struct *pd) {
    uint8_t rdsk[XX_RDB_SCAN_STEP];
    uint64_t cylinder_blocks;
    uint32_t rdb_blocks_hi;
    /* Initialise before the guard clause: callers run the cleanup on their
     * stack copy whatever this returns. */
    if (parsed) xx_rdb_private_reset(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (parsed->input_size <= self->base_address) return false;
    if (!xx_rdb_find_rdsk(self, parsed, rdsk)) goto fail;
    parsed->block = (uint8_t *)xx_mem_alloc(parsed->block_bytes);
    if (!parsed->block) goto fail;
    if (!xx_rdb_walk_partitions(self, parsed, xx_data_get_u32(rdsk + 28U, 4, 0, true), pd) ||
        !xx_rdb_walk_filesystems(self, parsed, xx_data_get_u32(rdsk + 32U, 4, 0, true), pd)) {
        goto fail;
    }
    /* Physical geometry, then the blocks reserved for the RDB. */
    if (xx_rdb_mul((uint64_t)xx_data_get_u32(rdsk + 68U, 4, 0, true), xx_data_get_u32(rdsk + 72U, 4, 0, true),
                   &cylinder_blocks)) {
        uint64_t blocks;
        if (xx_rdb_mul(cylinder_blocks, xx_data_get_u32(rdsk + 64U, 4, 0, true), &blocks)) {
            xx_rdb_extend_end(self, parsed, blocks, parsed->block_bytes);
        }
    }
    rdb_blocks_hi = xx_data_get_u32(rdsk + 132U, 4, 0, true);
    if (rdb_blocks_hi != XX_RDB_END_OF_CHAIN) {
        xx_rdb_extend_end(self, parsed, (uint64_t)rdb_blocks_hi + 1U,
                          parsed->block_bytes);
    }
    return true;
fail:
    xx_rdb_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------ */
/* Archive record plumbing                                             */
/* ------------------------------------------------------------------ */

static bool xx_rdb_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
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

static const xx_var *xx_rdb_find_option(const xx_list_s *options,
                                        uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

/* "<drive name> <DosType tag>" for a partition, "<DosType tag>" for a
 * filesystem. */
static void xx_rdb_make_comment(const xx_rdb_entry *entry,
                                char out[XX_RDB_COMMENT_SIZE]) {
    char tag[20];
    size_t used = 0U;
    size_t length;
    xx_rdb_dos_type_tag(entry->dos_type, tag);
    if (!entry->is_filesystem) {
        length = xx_str_len(entry->drive_name);
        xx_rt_memcpy(out, entry->drive_name, length);
        used = length;
        if (used != 0U) out[used++] = ' ';
    }
    length = xx_str_len(tag);
    xx_rt_memcpy(out + used, tag, length);
    used += length;
    out[used] = '\0';
}

static bool xx_rdb_populate_record(xx_archive_record *record,
                                   const xx_rdb_entry *entry,
                                   uint32_t block_bytes) {
    char comment[XX_RDB_COMMENT_SIZE];
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    xx_rdb_make_comment(entry, comment);
    record->header_offset = entry->header_offset;
    record->header_size = (int64_t)block_bytes;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          entry->flags) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          comment) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_rdb_archive_stream_free(void *pointer) {
    xx_rdb_archive_stream *stream = (xx_rdb_archive_stream *)pointer;
    if (!stream) return;
    xx_rdb_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* Record names are generated here, never taken from the disk, so this only
 * has to refuse the impossible. */
static bool xx_rdb_safe_name(const char *name) {
    size_t index;
    if (!name || !name[0] || name[0] == '.') return false;
    for (index = 0U; name[index] != '\0'; ++index) {
        char ch = name[index];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) {
            return false;
        }
    }
    return true;
}

/* Copy the LoadData of every LSEG block of a filesystem to destination (or
 * only verify the chain when destination is NULL). Each block is checked
 * again, so a device that changed since the parse fails cleanly. */
static bool xx_rdb_copy_filesystem(Abstractformat *self,
                                   xx_rdb_private *parsed,
                                   const xx_rdb_entry *entry,
                                   xx_io_device *destination,
                                   xx_pd_struct *pd) {
    uint32_t index;
    uint64_t written = 0U;
    for (index = 0U; index < entry->lseg_count; ++index) {
        int64_t offset = 0;
        uint32_t longs = 0U;
        uint32_t next = XX_RDB_END_OF_CHAIN;
        bool type_ok;
        size_t size;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_rdb_read_block(self, parsed, entry->lseg_blocks[index],
                               XX_RDB_ID_LSEG, XX_RDB_LSEG_HEADER_LONGS,
                               &offset, &longs, &next, &type_ok)) {
            return false;
        }
        size = (size_t)(longs - XX_RDB_LSEG_HEADER_LONGS) * 4U;
        if (destination && size != 0U &&
            xx_io_write(destination, parsed->block + 20, size) !=
                (ssize_t)size) {
            return false;
        }
        written += size;
    }
    return written == (uint64_t)entry->data_size;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void xx_rdb_init(xx_rdb *rdb, xx_io_device *dev, int64_t base_address) {
    if (!rdb) return;
    xx_rt_memset(rdb, 0, sizeof(*rdb));
    xx_format_init(&rdb->format, dev, base_address);
    rdb->format.endian = XX_ENDIAN_BIG;
    rdb->format.file_type = XX_RDB_FILE_TYPE;
    rdb->format.format_type = XX_TYPE_ARCHIVE;
    rdb->format.is_archive = true;
    xx_format_set_mime_type(&rdb->format, "application/x-amiga-rdb");
    xx_format_set_extension(&rdb->format, "hdf");
    rdb->format.check_is_valid = xx_rdb_check_is_valid;
    rdb->format.handle_base_info = xx_rdb_handle_base_info;
    rdb->format.get_format_size = xx_rdb_get_format_size;
    rdb->format.get_number_of_archive_records =
        xx_rdb_get_number_of_archive_records;
    rdb->format.create_archive_records_reading =
        xx_rdb_create_archive_records_reading;
    rdb->format.get_current_archive_record = xx_rdb_get_current_archive_record;
    rdb->format.unpack_current_archive_record =
        xx_rdb_unpack_current_archive_record;
    rdb->format.archive_record_move_to_next = xx_rdb_archive_record_move_to_next;
    rdb->format.free_archive_records_reading =
        xx_rdb_free_archive_records_reading;
    rdb->format.destroy = xx_rdb_vtable_destroy;
    rdb->archive_end = -1;
}

xx_rdb *xx_rdb_create(xx_io_device *dev, int64_t base_address) {
    xx_rdb *rdb = (xx_rdb *)xx_mem_alloc(sizeof(*rdb));
    if (rdb) xx_rdb_init(rdb, dev, base_address);
    return rdb;
}

void xx_rdb_destroy(xx_rdb *rdb) {
    if (!rdb) return;
    if (rdb->internal) {
        xx_rdb_private_cleanup((xx_rdb_private *)rdb->internal);
        xx_mem_free(rdb->internal);
        rdb->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&rdb->format);
}

static void xx_rdb_vtable_destroy(Abstractformat *self) {
    xx_rdb_destroy((xx_rdb *)self);
}

void xx_rdb_free(xx_rdb *rdb) {
    if (!rdb) return;
    xx_rdb_destroy(rdb);
    xx_mem_free(rdb);
}

/* The probe only needs the RDSK block: a 8 KiB read and 16 comparisons. */
bool xx_rdb_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_rdb_private parsed;
    uint8_t rdsk[XX_RDB_SCAN_STEP];
    bool result;
    xx_rdb_private_reset(&parsed);
    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed.input_size = xx_io_total_size(self->device);
    if (parsed.input_size <= self->base_address) return false;
    result = xx_rdb_find_rdsk(self, &parsed, rdsk);
    xx_rdb_private_cleanup(&parsed);
    return result;
}

bool xx_rdb_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_rdb_private *parsed;
    xx_rdb *rdb = (xx_rdb *)self;
    int64_t total_size;
    if (!self) return false;
    parsed = (xx_rdb_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_rdb_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (rdb->internal) {
        xx_rdb_private_cleanup((xx_rdb_private *)rdb->internal);
        xx_mem_free(rdb->internal);
    }
    rdb->internal = parsed;
    rdb->number_of_records = parsed->count;
    rdb->number_of_members = parsed->count;
    rdb->block_bytes = parsed->block_bytes;
    rdb->rdsk_block = parsed->rdsk_block;
    rdb->partitions = parsed->partitions;
    rdb->filesystems = parsed->filesystems;
    rdb->archive_end = parsed->archive_end;
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
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_rdb_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_rdb_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_rdb *)self)->number_of_records;
}

xx_archive_record_state *xx_rdb_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_rdb_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_rdb_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_rdb_copy_options(&state->options, options) ||
        !xx_rdb_parse(self, &stream->parsed, pd)) {
        xx_rdb_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_rdb_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_rdb_populate_record(&state->current_record,
                               &stream->parsed.entries[0],
                               stream->parsed.block_bytes)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_rdb_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_rdb_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_rdb_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_rdb_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_rdb_populate_record(&state->current_record,
                                &stream->parsed.entries[stream->index],
                                stream->parsed.block_bytes)) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_rdb_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_rdb_archive_stream *stream;
    const xx_rdb_entry *entry;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    size_t base_length;
    bool result = false;
    bool created = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_rdb_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    entry = &stream->parsed.entries[stream->index];
    if (!xx_rdb_safe_name(entry->name)) return false;
    option = xx_rdb_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        int64_t total = xx_io_total_size(self->device);
        if (entry->is_filesystem) {
            return xx_rdb_copy_filesystem(self, &stream->parsed, entry, NULL,
                                          pd);
        }
        return entry->data_offset >= 0 && entry->data_size >= 0 &&
               entry->data_offset <= total &&
               entry->data_size <= total - entry->data_offset;
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
    base_length = xx_str_len(base);
    if (base_length != 0U && base[base_length - 1U] != '/' &&
        base[base_length - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", entry->name);
    } else {
        destination = xx_str_concat(base, entry->name);
    }
    if (!destination || !xx_store_create_dirs_a(destination, false)) {
        goto cleanup;
    }
    if (!entry->is_filesystem) {
        /* The store helper removes its own output on failure. */
        result = xx_store_unpack_device_to_file(self->device,
                                                entry->data_offset,
                                                entry->data_size,
                                                destination, pd);
    } else {
        xx_io_device *output = xx_io_file_open(destination, "wb");
        if (!output) goto cleanup;
        /* Only a file this call created may be removed on failure. */
        created = true;
        result = xx_rdb_copy_filesystem(self, &stream->parsed, entry, output,
                                        pd);
        if (xx_io_close(output) != 0) result = false;
        if (!result && created) xx_rt_remove(destination);
    }
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_rdb_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_rdb_get_number_of_records(const xx_rdb *rdb) {
    return rdb ? rdb->number_of_records : 0U;
}
uint64_t xx_rdb_get_number_of_members(const xx_rdb *rdb) {
    return rdb ? rdb->number_of_members : 0U;
}
uint32_t xx_rdb_get_block_bytes(const xx_rdb *rdb) {
    return rdb ? rdb->block_bytes : 0U;
}
int64_t xx_rdb_get_archive_end(const xx_rdb *rdb) {
    return rdb ? rdb->archive_end : -1;
}

bool xx_rdb_get_member_info(const xx_rdb *rdb, uint64_t index,
                            xx_rdb_member_info *info) {
    const xx_rdb_private *parsed;
    const xx_rdb_entry *entry;
    if (!rdb || !info || !rdb->internal) return false;
    parsed = (const xx_rdb_private *)rdb->internal;
    if (index >= parsed->count) return false;
    entry = &parsed->entries[index];
    info->is_filesystem = entry->is_filesystem;
    info->index = entry->index;
    info->offset = entry->data_offset;
    info->size = entry->data_size;
    info->declared_size = entry->declared_size;
    info->dos_type = entry->dos_type;
    info->flags = entry->flags;
    info->low_cyl = entry->low_cyl;
    info->high_cyl = entry->high_cyl;
    info->drive_name = entry->drive_name;
    info->name = entry->name;
    return true;
}
