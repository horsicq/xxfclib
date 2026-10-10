/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/jffs2_old/xx_jffs2_old.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/lzo/xx_lzo.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is supplied locally until the enumerator
 * lands. Delete this block once XX_FILE_TYPE_JFFS2_OLD exists in the enum. */
#ifdef JFFS2_OLD
#define XX_JFFS2_OLD_FILE_TYPE XX_FILE_TYPE_JFFS2_OLD
#else
#define XX_JFFS2_OLD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Derived from src/formats/jffs2/xx_jffs2.c of this library (MIT). The only
 * format difference is the node magic: 0x1984, the pre-2001 value the kernel
 * calls JFFS2_OLD_MAGIC_BITMASK. Extraction additionally refuses Windows
 * device names and renames members whose paths differ only in case. */
#define XX_JFFS2_OLD_MAGIC 0x1984U

#define XX_JFFS2_OLD_UNKNOWN_NODE_SIZE 12U
#define XX_JFFS2_OLD_DIRENT_NODE_SIZE 40U
#define XX_JFFS2_OLD_INODE_NODE_SIZE 68U
#define XX_JFFS2_OLD_HDR_CRC_SIZE 8U
#define XX_JFFS2_OLD_DIRENT_CRC_SIZE 32U
#define XX_JFFS2_OLD_INODE_CRC_SIZE 60U

/* Nodes are padded to a 4-byte boundary, and the scan resynchronises on the
 * same step whenever a candidate header fails to validate. */
#define XX_JFFS2_OLD_ALIGNMENT 4

/* DT_* constants, as carried by the dirent's type byte. */
#define XX_JFFS2_OLD_DT_FIFO 1U
#define XX_JFFS2_OLD_DT_CHR 2U
#define XX_JFFS2_OLD_DT_DIR 4U
#define XX_JFFS2_OLD_DT_BLK 6U
#define XX_JFFS2_OLD_DT_REG 8U
#define XX_JFFS2_OLD_DT_LNK 10U
#define XX_JFFS2_OLD_DT_SOCK 12U

/* The inode number of the root directory is fixed by the format. */
#define XX_JFFS2_OLD_ROOT_INO 1U

/* Budgets. Every one of them bounds work that a crafted image would
 * otherwise be able to drive without limit. */
#define XX_JFFS2_OLD_MAX_NODES 2000000U
#define XX_JFFS2_OLD_MAX_DIRENTS 200000U
#define XX_JFFS2_OLD_MAX_FRAGMENTS 600000U
#define XX_JFFS2_OLD_MAX_ENTRIES 200000U
#define XX_JFFS2_OLD_MAX_DEPTH 64U
#define XX_JFFS2_OLD_MAX_NAME 255U
#define XX_JFFS2_OLD_MAX_PATH 4096U
#define XX_JFFS2_OLD_MAX_FILE_SIZE (256U * 1024U * 1024U)
#define XX_JFFS2_OLD_MAX_LINK_TARGET 4096U

/* Window used to read node headers without one syscall per candidate. */
#define XX_JFFS2_OLD_WINDOW_SIZE 65536U

/* ------------------------------------------------------------ records --- */

/** One validated DIRENT node. The name is owned. */
typedef struct xx_jffs2_old_dirent_s {
    uint32_t pino;
    uint32_t ino; /**< Zero records an unlink of this name. */
    uint32_t version;
    uint32_t mctime;
    uint8_t type;
    char *name;
    int64_t node_offset; /**< Where this dirent node sits in the image. */
} xx_jffs2_old_dirent;

/** One validated INODE node - a single write into one file. */
typedef struct xx_jffs2_old_fragment_s {
    uint32_t ino;
    uint32_t version;
    uint32_t offset;
    uint32_t csize;
    uint32_t dsize;
    uint32_t isize;
    uint32_t mode;
    uint32_t mtime;
    uint32_t data_crc;
    uint8_t compr;
    int64_t data_offset; /**< Absolute position of the stored payload. */
} xx_jffs2_old_fragment;

/** One resolved tree member. */
typedef struct xx_jffs2_old_entry_s {
    char *name;        /**< Full path from the root, '/' separated. */
    char *link_target; /**< Symlink target, or NULL. */
    uint32_t ino;
    uint32_t mode;
    uint32_t mtime;
    uint64_t size;
    int64_t header_offset;
    bool is_folder;
    bool is_link;
    bool is_special; /**< Device, fifo or socket: nothing to extract. */
    bool has_data;   /**< At least one inode node exists for this ino. */
} xx_jffs2_old_entry;

/* Open-addressing set of inode numbers, used to stop a dirent that names an
 * ancestor from sending the directory walk round in a circle. Slots hold
 * ino + 1 so that zero can mean "empty"; ino 0 never occurs in a live
 * dirent, but storing ino + 1 keeps the invariant independent of that. */
typedef struct xx_jffs2_old_inoset_s {
    uint64_t *slots;
    size_t capacity;
    size_t count;
} xx_jffs2_old_inoset;

typedef struct xx_jffs2_old_private_s {
    xx_jffs2_old_dirent *dirents;
    size_t dirent_count;
    size_t dirent_capacity;
    xx_jffs2_old_fragment *fragments;
    size_t fragment_count;
    size_t fragment_capacity;
    xx_jffs2_old_entry *entries;
    size_t count;
    size_t capacity;
    xx_jffs2_old_inoset visited;
    xx_jffs2_old_inoset claimed; /**< Case-folded hashes of member paths. */
    int64_t input_size;
    int64_t archive_end;
    uint64_t node_count;
    uint32_t compression_mask;
    bool big_endian;
} xx_jffs2_old_private;

typedef struct xx_jffs2_old_archive_stream_s {
    xx_jffs2_old_private parsed;
    size_t index;
} xx_jffs2_old_archive_stream;

/* A buffered view over the device. Node headers are small and are read in
 * ascending order, so one refill serves many of them. */
typedef struct xx_jffs2_old_window_s {
    xx_io_device *device;
    int64_t offset; /**< Device position of buffer[0]; -1 when empty. */
    size_t size;
    int64_t total;
    uint8_t buffer[XX_JFFS2_OLD_WINDOW_SIZE];
} xx_jffs2_old_window;

static void xx_jffs2_old_vtable_destroy(Abstractformat *self);

/* -------------------------------------------------------- arithmetic --- */

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_jffs2_old_range_within(int64_t total_size, int64_t offset, int64_t size)
{
    return (total_size >= 0) && (offset >= 0) && (size >= 0) && (offset <= total_size) && (size <= total_size - offset);
}

static bool xx_jffs2_old_add(int64_t left, uint64_t right, int64_t *result)
{
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* Round up to the next 4-byte boundary, refusing to overflow. */
static bool xx_jffs2_old_align(int64_t value, int64_t *result)
{
    if (!result || value < 0 || value > INT64_MAX - (XX_JFFS2_OLD_ALIGNMENT - 1)) {
        return false;
    }
    *result = (value + (XX_JFFS2_OLD_ALIGNMENT - 1)) & ~(int64_t)(XX_JFFS2_OLD_ALIGNMENT - 1);
    return true;
}

/* JFFS2's CRC is the ordinary reflected CRC-32 polynomial run with an
 * initial register of zero and no final inversion - the kernel's crc32(0,
 * ...), which is not the same convention as zlib's. xx_crc32_calc() follows
 * zlib (it inverts on the way in and on the way out), so seeding it with
 * 0xFFFFFFFF cancels the entry inversion and a final XOR cancels the exit
 * one. binwalk and jefferson spell the same identity differently. */
static uint32_t xx_jffs2_old_crc(const void *data, size_t size)
{
    return xx_crc32_calc(0xFFFFFFFFU, data, size) ^ 0xFFFFFFFFU;
}

/* ------------------------------------------------------------ window --- */

static void xx_jffs2_old_window_init(xx_jffs2_old_window *window, xx_io_device *device, int64_t total)
{
    if (!window) return;
    window->device = device;
    window->offset = -1;
    window->size = 0U;
    window->total = total;
}

/* Return a pointer to `need` bytes at `offset`, refilling if necessary.
 * NULL when the request runs off the end of the device or a read fails. */
static const uint8_t *xx_jffs2_old_window_get(xx_jffs2_old_window *window, int64_t offset, size_t need)
{
    size_t want;
    size_t done = 0U;
    if (!window || !window->device || need == 0U || need > XX_JFFS2_OLD_WINDOW_SIZE || !xx_jffs2_old_range_within(window->total, offset, (int64_t)need)) {
        return NULL;
    }
    if (window->offset >= 0 && offset >= window->offset && offset - window->offset <= (int64_t)window->size && (size_t)(offset - window->offset) <= window->size - need) {
        return window->buffer + (offset - window->offset);
    }
    want = XX_JFFS2_OLD_WINDOW_SIZE;
    if ((int64_t)want > window->total - offset) {
        want = (size_t)(window->total - offset);
    }
    window->offset = -1;
    window->size = 0U;
    if (xx_io_seek64(window->device, offset, SEEK_SET) != 0) return NULL;
    while (done < want) {
        ssize_t got = xx_io_read(window->device, window->buffer + done, want - done);
        if (got <= 0 || (size_t)got > want - done) break;
        done += (size_t)got;
    }
    if (done < need) return NULL;
    window->offset = offset;
    window->size = done;
    return window->buffer;
}

/* --------------------------------------------------------- inode set --- */

static void xx_jffs2_old_inoset_cleanup(xx_jffs2_old_inoset *set)
{
    if (!set) return;
    if (set->slots) xx_mem_free(set->slots);
    xx_mem_zero(set, sizeof(*set));
}

static size_t xx_jffs2_old_inoset_slot(const xx_jffs2_old_inoset *set, uint32_t ino)
{
    uint64_t key = (uint64_t)ino;
    key = (key ^ (key >> 29U)) * UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 32U;
    return (size_t)key & (set->capacity - 1U);
}

static bool xx_jffs2_old_inoset_grow(xx_jffs2_old_inoset *set)
{
    uint64_t *slots;
    size_t capacity = set->capacity ? set->capacity * 2U : 256U;
    size_t index;
    xx_jffs2_old_inoset grown;
    if (capacity < set->capacity || capacity > SIZE_MAX / sizeof(*slots)) {
        return false;
    }
    slots = (uint64_t *)xx_mem_calloc(capacity, sizeof(*slots));
    if (!slots) return false;
    grown.slots = slots;
    grown.capacity = capacity;
    grown.count = set->count;
    for (index = 0U; index < set->capacity; ++index) {
        uint64_t stored = set->slots[index];
        size_t slot;
        if (stored == 0U) continue;
        slot = xx_jffs2_old_inoset_slot(&grown, (uint32_t)(stored - 1U));
        while (slots[slot] != 0U) slot = (slot + 1U) & (capacity - 1U);
        slots[slot] = stored;
    }
    if (set->slots) xx_mem_free(set->slots);
    *set = grown;
    return true;
}

/* Record ino and report whether it had already been seen. Allocation
 * failure is reported as "seen" so the walk stops rather than continuing
 * with a set that can no longer remember anything. */
static bool xx_jffs2_old_inoset_mark(xx_jffs2_old_inoset *set, uint32_t ino)
{
    size_t slot;
    if (!set) return true;
    if ((set->count + 1U) * 4U >= set->capacity * 3U) {
        if (!xx_jffs2_old_inoset_grow(set)) return true;
    }
    slot = xx_jffs2_old_inoset_slot(set, ino);
    while (set->slots[slot] != 0U) {
        if (set->slots[slot] == (uint64_t)ino + 1U) return true;
        slot = (slot + 1U) & (set->capacity - 1U);
    }
    set->slots[slot] = (uint64_t)ino + 1U;
    ++set->count;
    return false;
}

static void xx_jffs2_old_inoset_unmark(xx_jffs2_old_inoset *set, uint32_t ino)
{
    size_t slot;
    size_t scan;
    if (!set || set->capacity == 0U) return;
    slot = xx_jffs2_old_inoset_slot(set, ino);
    while (set->slots[slot] != 0U) {
        if (set->slots[slot] == (uint64_t)ino + 1U) break;
        slot = (slot + 1U) & (set->capacity - 1U);
    }
    if (set->slots[slot] == 0U) return;
    set->slots[slot] = 0U;
    --set->count;
    /* Open addressing: reinsert the run that followed the hole, or a probe
     * that walked past this slot would no longer find its entry. */
    scan = (slot + 1U) & (set->capacity - 1U);
    while (set->slots[scan] != 0U) {
        uint64_t moved = set->slots[scan];
        size_t target;
        set->slots[scan] = 0U;
        --set->count;
        target = xx_jffs2_old_inoset_slot(set, (uint32_t)(moved - 1U));
        while (set->slots[target] != 0U) {
            target = (target + 1U) & (set->capacity - 1U);
        }
        set->slots[target] = moved;
        ++set->count;
        scan = (scan + 1U) & (set->capacity - 1U);
    }
}

/* ---------------------------------------------------------- ordering --- */

/* A byte-wise heapsort. The library is CRT-free, so qsort() is unavailable;
 * heapsort is chosen over an insertion sort because a large image can carry
 * hundreds of thousands of nodes and over a merge sort because it needs no
 * second array. The caller supplies scratch space for one element. */
static void xx_jffs2_old_sift(uint8_t *base, size_t count, size_t size, size_t root, int (*compare)(const void *, const void *), uint8_t *scratch)
{
    for (;;) {
        size_t child = root * 2U + 1U;
        size_t largest = root;
        if (child >= count) break;
        if (compare(base + child * size, base + largest * size) > 0) {
            largest = child;
        }
        if (child + 1U < count && compare(base + (child + 1U) * size, base + largest * size) > 0) {
            largest = child + 1U;
        }
        if (largest == root) break;
        xx_mem_copy(scratch, base + root * size, size);
        xx_mem_copy(base + root * size, base + largest * size, size);
        xx_mem_copy(base + largest * size, scratch, size);
        root = largest;
    }
}

static void xx_jffs2_old_sort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *), uint8_t *scratch)
{
    uint8_t *bytes = (uint8_t *)base;
    size_t index;
    if (!bytes || !compare || !scratch || count < 2U) return;
    for (index = count / 2U; index-- > 0U;) {
        xx_jffs2_old_sift(bytes, count, size, index, compare, scratch);
    }
    for (index = count; index-- > 1U;) {
        xx_mem_copy(scratch, bytes, size);
        xx_mem_copy(bytes, bytes + index * size, size);
        xx_mem_copy(bytes + index * size, scratch, size);
        xx_jffs2_old_sift(bytes, index, size, 0U, compare, scratch);
    }
}

/* Dirents are ordered by parent, then name, then version, so that the run
 * for one name within one directory is contiguous and its last element is
 * the winning revision. */
static int xx_jffs2_old_dirent_compare(const void *left, const void *right)
{
    const xx_jffs2_old_dirent *a = (const xx_jffs2_old_dirent *)left;
    const xx_jffs2_old_dirent *b = (const xx_jffs2_old_dirent *)right;
    int order;
    if (a->pino != b->pino) return a->pino < b->pino ? -1 : 1;
    order = xx_str_cmp(a->name, b->name);
    if (order != 0) return order;
    if (a->version != b->version) return a->version < b->version ? -1 : 1;
    return 0;
}

/* Fragments are ordered by inode, then version: replaying one inode's run
 * front to back is exactly the log replay the format specifies. */
static int xx_jffs2_old_fragment_compare(const void *left, const void *right)
{
    const xx_jffs2_old_fragment *a = (const xx_jffs2_old_fragment *)left;
    const xx_jffs2_old_fragment *b = (const xx_jffs2_old_fragment *)right;
    if (a->ino != b->ino) return a->ino < b->ino ? -1 : 1;
    if (a->version != b->version) return a->version < b->version ? -1 : 1;
    if (a->data_offset != b->data_offset) {
        return a->data_offset < b->data_offset ? -1 : 1;
    }
    return 0;
}

/* First index whose pino is >= key, over an array already sorted. */
static size_t xx_jffs2_old_dirent_lower_bound(const xx_jffs2_old_dirent *dirents, size_t count, uint32_t pino)
{
    size_t low = 0U;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (dirents[middle].pino < pino) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low;
}

static size_t xx_jffs2_old_fragment_lower_bound(const xx_jffs2_old_fragment *fragments, size_t count, uint32_t ino)
{
    size_t low = 0U;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (fragments[middle].ino < ino) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low;
}

/* ------------------------------------------------------------- codecs --- */

/* RTIME. A byte is emitted verbatim, then a repeat count says how many more
 * bytes to copy from the last place that byte value was written. The
 * back-reference and the destination can overlap, so the copy is
 * byte-at-a-time. The kernel's decompressor trusts its input; this one does
 * not, so both the input cursor and the output cursor are bounded.
 * There is no xxfclib equivalent, so it is implemented here. */
static bool xx_jffs2_old_rtime_decompress(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size)
{
    /* The kernel uses a short here, which wraps once the output passes
     * 32767; a real RTIME node never exceeds one page, so a wider counter
     * agrees with it everywhere it matters and stays in range elsewhere. */
    uint32_t positions[256];
    size_t in_position = 0U;
    size_t out_position = 0U;
    if (!input || !output) return false;
    xx_mem_zero(positions, sizeof(positions));
    while (out_position < output_size) {
        uint8_t value;
        size_t backoffs;
        size_t repeat;
        if (in_position + 2U > input_size) return false;
        value = input[in_position++];
        repeat = input[in_position++];
        output[out_position++] = value;
        backoffs = (size_t)positions[value];
        positions[value] = (uint32_t)out_position;
        if (repeat == 0U) continue;
        if (repeat > output_size - out_position) return false;
        if (backoffs >= out_position) return false;
        while (repeat-- != 0U) {
            output[out_position++] = output[backoffs++];
        }
    }
    return in_position <= input_size;
}

/* RUBIN. A binary arithmetic coder over eight fixed per-bit probabilities;
 * RUBINMIPS uses a built-in table, DYNRUBIN reads the table from the first
 * eight payload bytes. Both were dropped from practical use long ago and
 * are reproduced here only because no xxfclib codec covers them. */
typedef struct xx_jffs2_old_rubin_s {
    const uint8_t *input;
    size_t input_size;
    size_t bit_offset;
    uint32_t p;
    uint32_t q;
    uint32_t rec_q;
    uint32_t bit_divider;
    uint32_t bits[8];
} xx_jffs2_old_rubin;

#define XX_JFFS2_OLD_RUBIN_REG_SIZE 16
#define XX_JFFS2_OLD_RUBIN_UPPER (UINT32_C(1) << (XX_JFFS2_OLD_RUBIN_REG_SIZE - 1))
#define XX_JFFS2_OLD_RUBIN_LOWER (XX_JFFS2_OLD_RUBIN_UPPER - UINT32_C(1))

/* Past the end of the stored bits the coder reads zeroes. The kernel walks
 * off its buffer here; refusing to would break images whose final byte is
 * only partly used, so the padding is silent and bounded instead. */
static uint32_t xx_jffs2_old_rubin_pullbit(xx_jffs2_old_rubin *state)
{
    size_t index = state->bit_offset >> 3U;
    uint32_t bit = 0U;
    if (index < state->input_size) {
        bit = (uint32_t)((state->input[index] >> (7U - (state->bit_offset & 7U))) & 1U);
    }
    ++state->bit_offset;
    return bit;
}

static void xx_jffs2_old_rubin_renormalise(xx_jffs2_old_rubin *state)
{
    uint32_t p = state->p;
    uint32_t q = state->q;
    uint32_t rec_q = state->rec_q;
    unsigned bits = 0U;
    do {
        ++bits;
        q &= XX_JFFS2_OLD_RUBIN_LOWER;
        q <<= 1U;
        p <<= 1U;
        /* p doubles each round; the loop cannot run past the register
         * width, but the guard keeps a crafted state from spinning. */
    } while (bits < 64U && ((q >= XX_JFFS2_OLD_RUBIN_UPPER) || ((p + q) <= XX_JFFS2_OLD_RUBIN_UPPER)));
    state->p = p;
    state->q = q;
    while (bits-- != 0U) {
        rec_q &= XX_JFFS2_OLD_RUBIN_LOWER;
        rec_q <<= 1U;
        rec_q += xx_jffs2_old_rubin_pullbit(state);
    }
    state->rec_q = rec_q;
}

static uint32_t xx_jffs2_old_rubin_decode(xx_jffs2_old_rubin *state, uint32_t a, uint32_t b)
{
    uint32_t i0;
    uint32_t threshold;
    uint32_t symbol;
    if ((state->q >= XX_JFFS2_OLD_RUBIN_UPPER) || ((state->p + state->q) <= XX_JFFS2_OLD_RUBIN_UPPER)) {
        xx_jffs2_old_rubin_renormalise(state);
    }
    if (a + b == 0U) return 0U;
    i0 = (uint32_t)(((uint64_t)a * (uint64_t)state->p) / (uint64_t)(a + b));
    if (i0 == 0U) i0 = 1U;
    if (i0 >= state->p) i0 = state->p ? state->p - 1U : 1U;
    threshold = state->q + i0;
    symbol = state->rec_q >= threshold ? 1U : 0U;
    if (symbol) {
        state->q += i0;
        i0 = state->p - i0;
    }
    state->p = i0;
    return symbol;
}

static bool xx_jffs2_old_rubin_decompress(const uint8_t *input, size_t input_size, uint32_t divider, const uint32_t *bits, uint8_t *output, size_t output_size)
{
    xx_jffs2_old_rubin state;
    size_t index;
    if (!input || !output || !bits || divider == 0U) return false;
    xx_mem_zero(&state, sizeof(state));
    state.input = input;
    state.input_size = input_size;
    state.p = 2U * XX_JFFS2_OLD_RUBIN_UPPER;
    state.bit_divider = divider;
    for (index = 0U; index < 8U; ++index) {
        if (bits[index] >= divider) return false;
        state.bits[index] = bits[index];
    }
    /* Prime the decoder with one register's worth of bits. */
    for (index = 0U; index < XX_JFFS2_OLD_RUBIN_REG_SIZE; ++index) {
        state.rec_q = state.rec_q * 2U + xx_jffs2_old_rubin_pullbit(&state);
    }
    for (index = 0U; index < output_size; ++index) {
        unsigned bit;
        uint32_t value = 0U;
        for (bit = 0U; bit < 8U; ++bit) {
            value |= xx_jffs2_old_rubin_decode(&state, divider - state.bits[bit], state.bits[bit]) << bit;
        }
        output[index] = (uint8_t)value;
    }
    return true;
}

#define XX_JFFS2_OLD_RUBIN_MIPS_DIVIDER 1043U

static bool xx_jffs2_old_rubinmips_decompress(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size)
{
    static const uint32_t mips_bits[8] = {277U, 249U, 290U, 267U, 229U, 341U, 212U, 241U};
    return xx_jffs2_old_rubin_decompress(input, input_size, XX_JFFS2_OLD_RUBIN_MIPS_DIVIDER, mips_bits, output, output_size);
}

static bool xx_jffs2_old_dynrubin_decompress(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size)
{
    uint32_t bits[8];
    size_t index;
    if (!input || input_size < 8U) return false;
    for (index = 0U; index < 8U; ++index) bits[index] = input[index];
    return xx_jffs2_old_rubin_decompress(input + 8U, input_size - 8U, 256U, bits, output, output_size);
}

/* Expand one inode node's payload into exactly dsize bytes at output. */
static bool xx_jffs2_old_decompress(uint8_t compression, const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size)
{
    size_t written = 0U;
    if (!output) return false;
    if (output_size == 0U) return true;
    switch (compression) {
        case XX_JFFS2_OLD_COMPR_ZERO: xx_mem_zero(output, output_size); return true;
        case XX_JFFS2_OLD_COMPR_NONE:
        case XX_JFFS2_OLD_COMPR_COPY:
            /* COPY is "compression was not worth it": stored verbatim, the
             * same as NONE. jefferson treats the pair identically. */
            if (!input || input_size != output_size) return false;
            xx_mem_copy(output, input, output_size);
            return true;
        case XX_JFFS2_OLD_COMPR_RTIME: return xx_jffs2_old_rtime_decompress(input, input_size, output, output_size);
        case XX_JFFS2_OLD_COMPR_RUBINMIPS: return xx_jffs2_old_rubinmips_decompress(input, input_size, output, output_size);
        case XX_JFFS2_OLD_COMPR_DYNRUBIN: return xx_jffs2_old_dynrubin_decompress(input, input_size, output, output_size);
        case XX_JFFS2_OLD_COMPR_ZLIB:
            if (!input) return false;
            if (!xx_zlib_stream_decode_memory(input, input_size, output, output_size, &written)) {
                return false;
            }
            return written == output_size;
        case XX_JFFS2_OLD_COMPR_LZO:
            if (!input) return false;
            if (!xx_lzo1x_decompress(input, input_size, output, output_size, &written)) {
                return false;
            }
            return written == output_size;
        case XX_JFFS2_OLD_COMPR_LZMA: {
            /* Not an upstream compressor. OpenWrt's jffs2-lzma writes a raw
             * LZMA stream with no property byte and no size field, encoded
             * with lc=0, lp=0, pb=0; the properties are synthesised here.
             * Untested against a real image - no sample was available. */
            static const uint8_t props[5] = {0x00U, 0x00U, 0x20U, 0x00U, 0x00U};
            if (!input) return false;
            if (!xx_lzma_decompress_memory(input, input_size, props, sizeof(props), (int64_t)output_size, output, output_size, &written)) {
                return false;
            }
            return written == output_size;
        }
        default: return false;
    }
}

const char *xx_jffs2_old_compression_to_string(uint32_t compression)
{
    switch (compression) {
        case XX_JFFS2_OLD_COMPR_NONE: return "NONE";
        case XX_JFFS2_OLD_COMPR_ZERO: return "ZERO";
        case XX_JFFS2_OLD_COMPR_RTIME: return "RTIME";
        case XX_JFFS2_OLD_COMPR_RUBINMIPS: return "RUBINMIPS";
        case XX_JFFS2_OLD_COMPR_COPY: return "COPY";
        case XX_JFFS2_OLD_COMPR_DYNRUBIN: return "DYNRUBIN";
        case XX_JFFS2_OLD_COMPR_ZLIB: return "ZLIB";
        case XX_JFFS2_OLD_COMPR_LZO: return "LZO";
        case XX_JFFS2_OLD_COMPR_LZMA: return "LZMA";
        default: return "Unknown";
    }
}

/* ----------------------------------------------------------- storage --- */

static void xx_jffs2_old_private_cleanup(xx_jffs2_old_private *parsed)
{
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->dirent_count; ++index) {
        if (parsed->dirents[index].name) xx_str_free(parsed->dirents[index].name);
    }
    if (parsed->dirents) xx_mem_free(parsed->dirents);
    if (parsed->fragments) xx_mem_free(parsed->fragments);
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) xx_str_free(parsed->entries[index].name);
        if (parsed->entries[index].link_target) {
            xx_str_free(parsed->entries[index].link_target);
        }
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    xx_jffs2_old_inoset_cleanup(&parsed->visited);
    xx_jffs2_old_inoset_cleanup(&parsed->claimed);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static bool xx_jffs2_old_grow(void **array, size_t *capacity, size_t count, size_t element_size, size_t limit)
{
    void *grown;
    size_t wanted;
    if (count < *capacity) return true;
    if (count >= limit) return false;
    wanted = *capacity ? *capacity * 2U : 64U;
    if (wanted < *capacity || wanted > SIZE_MAX / element_size) return false;
    if (wanted > limit) wanted = limit;
    grown = xx_mem_realloc(*array, wanted * element_size);
    if (!grown) return false;
    *array = grown;
    *capacity = wanted;
    return true;
}

static bool xx_jffs2_old_append_dirent(xx_jffs2_old_private *parsed, xx_jffs2_old_dirent *dirent)
{
    if (!parsed || !dirent || !dirent->name) return false;
    if (!xx_jffs2_old_grow((void **)&parsed->dirents, &parsed->dirent_capacity, parsed->dirent_count, sizeof(*parsed->dirents), XX_JFFS2_OLD_MAX_DIRENTS)) {
        return false;
    }
    parsed->dirents[parsed->dirent_count++] = *dirent;
    xx_mem_zero(dirent, sizeof(*dirent));
    return true;
}

static bool xx_jffs2_old_append_fragment(xx_jffs2_old_private *parsed, const xx_jffs2_old_fragment *fragment)
{
    if (!parsed || !fragment) return false;
    if (!xx_jffs2_old_grow((void **)&parsed->fragments, &parsed->fragment_capacity, parsed->fragment_count, sizeof(*parsed->fragments), XX_JFFS2_OLD_MAX_FRAGMENTS)) {
        return false;
    }
    parsed->fragments[parsed->fragment_count++] = *fragment;
    return true;
}

static bool xx_jffs2_old_append_entry(xx_jffs2_old_private *parsed, xx_jffs2_old_entry *entry)
{
    if (!parsed || !entry || !entry->name) return false;
    if (!xx_jffs2_old_grow((void **)&parsed->entries, &parsed->capacity, parsed->count, sizeof(*parsed->entries), XX_JFFS2_OLD_MAX_ENTRIES)) {
        return false;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

/* -------------------------------------------------------------- names --- */

/* Parse-time check. A dirent name is a single path component, so only an
 * empty name, a control byte, an embedded separator and the two dot names
 * make it implausible - JFFS2 stores neither "." nor "..". */
static bool xx_jffs2_old_plausible_name(const char *name, size_t length)
{
    size_t index;
    if (!name || length == 0U || length > XX_JFFS2_OLD_MAX_NAME) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char ch = (unsigned char)name[index];
        if (ch < 32U || ch == '/' || ch == '\\') return false;
    }
    if (length == 1U && name[0] == '.') return false;
    if (length == 2U && name[0] == '.' && name[1] == '.') return false;
    return true;
}

static char xx_jffs2_old_upper(char ch)
{
    return (ch >= 'a' && ch <= 'z') ? (char)(ch - 'a' + 'A') : ch;
}

/* True when a path component (not NUL terminated, `length` bytes) is a
 * Windows device name - CON, PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$,
 * CLOCK$ - in any case, with or without an extension, trailing spaces on
 * the stem ignored. */
static bool xx_jffs2_old_is_device_name(const char *component, size_t length)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U;
    size_t index;
    while (stem < length && component[stem] != '.') ++stem;
    while (stem > 0U && component[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        const char *word = devices[index];
        size_t position = 0U;
        while (position < stem && word[position] && xx_jffs2_old_upper(component[position]) == word[position]) {
            ++position;
        }
        if (position == stem && word[position] == '\0') return true;
    }
    if (stem == 4U && component[3] >= '0' && component[3] <= '9') {
        char a = xx_jffs2_old_upper(component[0]);
        char b = xx_jffs2_old_upper(component[1]);
        char c = xx_jffs2_old_upper(component[2]);
        if ((a == 'C' && b == 'O' && c == 'M') || (a == 'L' && b == 'P' && c == 'T')) {
            return true;
        }
    }
    return false;
}

/* Extraction-time check, deliberately stricter: the path must stay inside
 * the destination tree on every host this library builds for, so reserved
 * Windows punctuation is rejected even though JFFS2 may legally carry it. */
static bool xx_jffs2_old_safe_name(const char *name)
{
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) {
            return false;
        }
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') || (length == 2U && component[0] == '.' && component[1] == '.') || component[length - 1U] == ' ' ||
                component[length - 1U] == '.' || xx_jffs2_old_is_device_name(component, length)) {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

static char *xx_jffs2_old_join_name(const char *prefix, const char *name)
{
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    size_t name_size = name ? xx_str_len(name) : 0U;
    char *combined;
    if (!name || name_size == 0U || prefix_size >= XX_JFFS2_OLD_MAX_PATH || name_size > XX_JFFS2_OLD_MAX_PATH - prefix_size - (prefix_size != 0U ? 1U : 0U)) {
        return NULL;
    }
    combined = (char *)xx_mem_alloc(prefix_size + name_size + (prefix_size != 0U ? 2U : 1U));
    if (!combined) return NULL;
    if (prefix_size != 0U) {
        xx_mem_copy(combined, prefix, prefix_size);
        combined[prefix_size] = '/';
        xx_mem_copy(combined + prefix_size + 1U, name, name_size);
        combined[prefix_size + 1U + name_size] = '\0';
    } else {
        xx_mem_copy(combined, name, name_size);
        combined[name_size] = '\0';
    }
    return combined;
}

/* -------------------------------------------------------- node scanner --- */

/* Read one DIRENT node that has already passed the header check. */
static bool xx_jffs2_old_read_dirent(xx_jffs2_old_private *parsed, xx_jffs2_old_window *window, int64_t offset, uint32_t totlen)
{
    /* The fixed part is copied out rather than used in place: reading the
     * name needs a second window fetch, and that may reposition the window
     * and invalidate the pointer this one returned. */
    uint8_t header[XX_JFFS2_OLD_DIRENT_NODE_SIZE];
    const uint8_t *fetched;
    const uint8_t *raw_name;
    xx_jffs2_old_dirent dirent;
    char *name;
    uint32_t nsize;
    int64_t name_offset;
    if (totlen < XX_JFFS2_OLD_DIRENT_NODE_SIZE) return false;
    fetched = xx_jffs2_old_window_get(window, offset, XX_JFFS2_OLD_DIRENT_NODE_SIZE);
    if (!fetched) return false;
    xx_mem_copy(header, fetched, XX_JFFS2_OLD_DIRENT_NODE_SIZE);
    if (xx_jffs2_old_crc(header, XX_JFFS2_OLD_DIRENT_CRC_SIZE) != xx_data_get_u32(header, XX_JFFS2_OLD_DIRENT_NODE_SIZE, 32U, parsed->big_endian)) {
        return false;
    }
    nsize = xx_data_get_u8(header, XX_JFFS2_OLD_DIRENT_NODE_SIZE, 28U);
    if (nsize == 0U || totlen - XX_JFFS2_OLD_DIRENT_NODE_SIZE < nsize) return false;
    xx_mem_zero(&dirent, sizeof(dirent));
    dirent.pino = xx_data_get_u32(header, XX_JFFS2_OLD_DIRENT_NODE_SIZE, 12U, parsed->big_endian);
    dirent.version = xx_data_get_u32(header, XX_JFFS2_OLD_DIRENT_NODE_SIZE, 16U, parsed->big_endian);
    dirent.ino = xx_data_get_u32(header, XX_JFFS2_OLD_DIRENT_NODE_SIZE, 20U, parsed->big_endian);
    dirent.mctime = xx_data_get_u32(header, XX_JFFS2_OLD_DIRENT_NODE_SIZE, 24U, parsed->big_endian);
    dirent.type = xx_data_get_u8(header, XX_JFFS2_OLD_DIRENT_NODE_SIZE, 29U);
    dirent.node_offset = offset;
    if (!xx_jffs2_old_add(offset, XX_JFFS2_OLD_DIRENT_NODE_SIZE, &name_offset)) {
        return false;
    }
    raw_name = xx_jffs2_old_window_get(window, name_offset, nsize);
    if (!raw_name) return false;
    if (xx_jffs2_old_crc(raw_name, nsize) != xx_data_get_u32(header, XX_JFFS2_OLD_DIRENT_NODE_SIZE, 36U, parsed->big_endian)) {
        return false;
    }
    name = (char *)xx_mem_alloc((size_t)nsize + 1U);
    if (!name) return false;
    xx_mem_copy(name, raw_name, nsize);
    name[nsize] = '\0';
    if (!xx_jffs2_old_plausible_name(name, nsize)) {
        xx_str_free(name);
        return false;
    }
    dirent.name = name;
    if (!xx_jffs2_old_append_dirent(parsed, &dirent)) {
        xx_str_free(name);
        return false;
    }
    return true;
}

/* Read one INODE node that has already passed the header check. */
static bool xx_jffs2_old_read_inode(xx_jffs2_old_private *parsed, xx_jffs2_old_window *window, int64_t offset, uint32_t totlen)
{
    const uint8_t *header;
    xx_jffs2_old_fragment fragment;
    if (totlen < XX_JFFS2_OLD_INODE_NODE_SIZE) return false;
    header = xx_jffs2_old_window_get(window, offset, XX_JFFS2_OLD_INODE_NODE_SIZE);
    if (!header) return false;
    if (xx_jffs2_old_crc(header, XX_JFFS2_OLD_INODE_CRC_SIZE) != xx_data_get_u32(header, XX_JFFS2_OLD_INODE_NODE_SIZE, 64U, parsed->big_endian)) {
        return false;
    }
    xx_mem_zero(&fragment, sizeof(fragment));
    fragment.ino = xx_data_get_u32(header, XX_JFFS2_OLD_INODE_NODE_SIZE, 12U, parsed->big_endian);
    fragment.version = xx_data_get_u32(header, XX_JFFS2_OLD_INODE_NODE_SIZE, 16U, parsed->big_endian);
    fragment.mode = xx_data_get_u32(header, XX_JFFS2_OLD_INODE_NODE_SIZE, 20U, parsed->big_endian);
    fragment.isize = xx_data_get_u32(header, XX_JFFS2_OLD_INODE_NODE_SIZE, 28U, parsed->big_endian);
    fragment.mtime = xx_data_get_u32(header, XX_JFFS2_OLD_INODE_NODE_SIZE, 36U, parsed->big_endian);
    fragment.offset = xx_data_get_u32(header, XX_JFFS2_OLD_INODE_NODE_SIZE, 44U, parsed->big_endian);
    fragment.csize = xx_data_get_u32(header, XX_JFFS2_OLD_INODE_NODE_SIZE, 48U, parsed->big_endian);
    fragment.dsize = xx_data_get_u32(header, XX_JFFS2_OLD_INODE_NODE_SIZE, 52U, parsed->big_endian);
    fragment.compr = xx_data_get_u8(header, XX_JFFS2_OLD_INODE_NODE_SIZE, 56U);
    fragment.data_crc = xx_data_get_u32(header, XX_JFFS2_OLD_INODE_NODE_SIZE, 60U, parsed->big_endian);
    /* The payload must fit inside the node the header declares. */
    if (totlen - XX_JFFS2_OLD_INODE_NODE_SIZE < fragment.csize) return false;
    if (!xx_jffs2_old_add(offset, XX_JFFS2_OLD_INODE_NODE_SIZE, &fragment.data_offset)) {
        return false;
    }
    if (!xx_jffs2_old_range_within(parsed->input_size, fragment.data_offset, fragment.csize)) {
        return false;
    }
    if (fragment.ino == 0U) return false;
    if (fragment.compr < 32U) {
        parsed->compression_mask |= (uint32_t)1U << fragment.compr;
    }
    return xx_jffs2_old_append_fragment(parsed, &fragment);
}

/* Walk the log from base_address to the end of the device.
 *
 * Every path through the loop advances `offset` by at least the 4-byte
 * alignment step, which is what keeps a hostile totlen - zero, tiny, or
 * pointing backwards - from turning the scan into a hang. Once one node has
 * validated, its endianness is locked for the rest of the image: accepting
 * either per node would let random data half-match and produce nonsense
 * records. */
static bool xx_jffs2_old_scan(Abstractformat *self, xx_jffs2_old_private *parsed, xx_pd_struct *pd)
{
    xx_jffs2_old_window window;
    int64_t offset = self->base_address;
    int64_t last_end = self->base_address;
    bool endian_known = false;
    xx_jffs2_old_window_init(&window, self->device, parsed->input_size);
    while (offset <= parsed->input_size - (int64_t)XX_JFFS2_OLD_UNKNOWN_NODE_SIZE) {
        const uint8_t *header;
        uint32_t totlen;
        uint16_t nodetype;
        int64_t node_end;
        int64_t next;
        bool big_endian;
        bool consumed = false;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (parsed->node_count >= XX_JFFS2_OLD_MAX_NODES) break;
        header = xx_jffs2_old_window_get(&window, offset, XX_JFFS2_OLD_UNKNOWN_NODE_SIZE);
        if (!header) break;

        /* Endianness is not recorded anywhere, so it is inferred from the
         * magic and then confirmed by the header CRC. */
        if (endian_known) {
            big_endian = parsed->big_endian;
            if (xx_data_get_u16(header, XX_JFFS2_OLD_UNKNOWN_NODE_SIZE, 0U, big_endian) != XX_JFFS2_OLD_MAGIC) {
                goto resync;
            }
        } else if (xx_data_get_u16(header, XX_JFFS2_OLD_UNKNOWN_NODE_SIZE, 0U, false) == XX_JFFS2_OLD_MAGIC) {
            big_endian = false;
        } else if (xx_data_get_u16(header, XX_JFFS2_OLD_UNKNOWN_NODE_SIZE, 0U, true) == XX_JFFS2_OLD_MAGIC) {
            big_endian = true;
        } else {
            goto resync;
        }
        if (xx_jffs2_old_crc(header, XX_JFFS2_OLD_HDR_CRC_SIZE) != xx_data_get_u32(header, XX_JFFS2_OLD_UNKNOWN_NODE_SIZE, 8U, big_endian)) {
            goto resync;
        }
        nodetype = xx_data_get_u16(header, XX_JFFS2_OLD_UNKNOWN_NODE_SIZE, 2U, big_endian);
        totlen = xx_data_get_u32(header, XX_JFFS2_OLD_UNKNOWN_NODE_SIZE, 4U, big_endian);
        if (totlen < XX_JFFS2_OLD_UNKNOWN_NODE_SIZE || !xx_jffs2_old_add(offset, totlen, &node_end) || node_end > parsed->input_size) {
            goto resync;
        }
        if (!endian_known) {
            parsed->big_endian = big_endian;
            endian_known = true;
        }
        ++parsed->node_count;
        if (nodetype == XX_JFFS2_OLD_NODETYPE_DIRENT) {
            consumed = xx_jffs2_old_read_dirent(parsed, &window, offset, totlen);
        } else if (nodetype == XX_JFFS2_OLD_NODETYPE_INODE) {
            consumed = xx_jffs2_old_read_inode(parsed, &window, offset, totlen);
        } else {
            /* Cleanmarker, padding, summary, xattr, xref: nothing to
             * extract, but the node is real and is stepped over whole. */
            consumed = true;
        }
        if (node_end > last_end) last_end = node_end;
        if (!consumed) goto resync;
        if (!xx_jffs2_old_align(node_end, &next) || next <= offset) goto resync;
        offset = next;
        continue;

    resync:
        /* A candidate that did not validate: step one alignment unit and
         * look again. The step is unconditional, so progress is too. */
        if (!xx_jffs2_old_align(offset + 1, &next) || next <= offset) break;
        offset = next;
    }
    if (!endian_known) return false;
    parsed->archive_end = last_end;
    return true;
}

/* ------------------------------------------------- content replay --- */

/* Grow a zero-filled reconstruction buffer to at least `wanted` bytes. */
static bool xx_jffs2_old_ensure(uint8_t **buffer, size_t *capacity, size_t wanted)
{
    uint8_t *grown;
    size_t target;
    if (wanted <= *capacity) return true;
    if (wanted > XX_JFFS2_OLD_MAX_FILE_SIZE) return false;
    target = *capacity ? *capacity : 4096U;
    while (target < wanted) {
        if (target > XX_JFFS2_OLD_MAX_FILE_SIZE / 2U) {
            target = wanted;
            break;
        }
        target *= 2U;
    }
    grown = (uint8_t *)xx_mem_realloc(*buffer, target);
    if (!grown) return false;
    xx_mem_zero(grown + *capacity, target - *capacity);
    *buffer = grown;
    *capacity = target;
    return true;
}

/* Rebuild one inode's contents by replaying its nodes in version order.
 *
 * This is the whole point of the format. Each node states the file's size
 * once it has been applied (isize) and writes dsize bytes at its own
 * offset, so a later node both overwrites bytes and can shorten the file.
 * Regions no node ever covered read as zero, which is how JFFS2 represents
 * a hole. The caller owns *out_data. */
static bool xx_jffs2_old_reconstruct(xx_io_device *device, const xx_jffs2_old_private *parsed, uint32_t ino, uint64_t size_limit, uint8_t **out_data, size_t *out_size,
                                     xx_pd_struct *pd)
{
    xx_jffs2_old_window window;
    uint8_t *buffer = NULL;
    uint8_t *payload = NULL;
    size_t payload_capacity = 0U;
    size_t capacity = 0U;
    size_t size = 0U;
    size_t index;
    bool found = false;
    if (!device || !parsed || !out_data || !out_size) return false;
    *out_data = NULL;
    *out_size = 0U;
    if (size_limit > XX_JFFS2_OLD_MAX_FILE_SIZE) size_limit = XX_JFFS2_OLD_MAX_FILE_SIZE;
    xx_jffs2_old_window_init(&window, device, parsed->input_size);
    index = xx_jffs2_old_fragment_lower_bound(parsed->fragments, parsed->fragment_count, ino);
    for (; index < parsed->fragment_count; ++index) {
        const xx_jffs2_old_fragment *fragment = &parsed->fragments[index];
        uint64_t end;
        uint64_t wanted;
        const uint8_t *stored;
        if (fragment->ino != ino) break;
        found = true;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        end = (uint64_t)fragment->offset + (uint64_t)fragment->dsize;
        wanted = end > (uint64_t)fragment->isize ? end : (uint64_t)fragment->isize;
        if (wanted > size_limit) goto fail;
        if (!xx_jffs2_old_ensure(&buffer, &capacity, (size_t)wanted)) goto fail;
        /* A node that shortens the file discards the tail; if a later node
         * extends past it again the gap must read as zero, not as the bytes
         * that used to live there. */
        if ((uint64_t)fragment->isize < (uint64_t)size) {
            xx_mem_zero(buffer + fragment->isize, size - fragment->isize);
        }
        size = (size_t)fragment->isize;
        if (fragment->dsize != 0U) {
            stored = NULL;
            if (fragment->csize != 0U) {
                if (fragment->csize <= XX_JFFS2_OLD_WINDOW_SIZE) {
                    stored = xx_jffs2_old_window_get(&window, fragment->data_offset, fragment->csize);
                } else {
                    /* Payloads larger than one window are read into their
                     * own buffer; a node this big is unusual but legal. */
                    if (fragment->csize > XX_JFFS2_OLD_MAX_FILE_SIZE) goto fail;
                    if (fragment->csize > payload_capacity) {
                        uint8_t *grown = (uint8_t *)xx_mem_realloc(payload, fragment->csize);
                        if (!grown) goto fail;
                        payload = grown;
                        payload_capacity = fragment->csize;
                    }
                    if (!xx_jffs2_old_range_within(parsed->input_size, fragment->data_offset, fragment->csize) ||
                        !xx_store_unpack_device_to_memory(device, fragment->data_offset, fragment->csize, payload, payload_capacity, NULL, pd)) {
                        goto fail;
                    }
                    stored = payload;
                }
                if (!stored) goto fail;
                /* data_crc covers the stored bytes, before decompression. */
                if (xx_jffs2_old_crc(stored, fragment->csize) != fragment->data_crc) {
                    goto fail;
                }
            } else if (fragment->compr != XX_JFFS2_OLD_COMPR_ZERO) {
                goto fail;
            }
            if (!xx_jffs2_old_decompress(fragment->compr, stored, fragment->csize, buffer + fragment->offset, fragment->dsize)) {
                goto fail;
            }
        }
        if (end > (uint64_t)size) size = (size_t)end;
    }
    if (!found) goto fail;
    if (size > capacity && !xx_jffs2_old_ensure(&buffer, &capacity, size)) goto fail;
    if (payload) xx_mem_free(payload);
    *out_data = buffer;
    *out_size = size;
    return true;
fail:
    if (payload) xx_mem_free(payload);
    if (buffer) xx_mem_free(buffer);
    return false;
}

/* The declared size of an inode is the isize of its highest-version node;
 * the fragment array is sorted, so that is the last of its run. */
static uint64_t xx_jffs2_old_inode_size(const xx_jffs2_old_private *parsed, uint32_t ino, uint32_t *out_mode, uint32_t *out_mtime, bool *out_present)
{
    size_t index = xx_jffs2_old_fragment_lower_bound(parsed->fragments, parsed->fragment_count, ino);
    uint64_t size = 0U;
    bool present = false;
    for (; index < parsed->fragment_count; ++index) {
        const xx_jffs2_old_fragment *fragment = &parsed->fragments[index];
        if (fragment->ino != ino) break;
        size = fragment->isize;
        if (out_mode) *out_mode = fragment->mode;
        if (out_mtime) *out_mtime = fragment->mtime;
        present = true;
    }
    if (out_present) *out_present = present;
    return present ? size : 0U;
}

/* ---------------------------------------------------- path claiming --- */

/* Member paths are claimed in a set of case-folded 64-bit hashes so that two
 * names differing only in case (legal in JFFS2) cannot overwrite each other
 * on a case-insensitive host. A hash collision between genuinely different
 * names only costs a harmless rename. */
static uint64_t xx_jffs2_old_fold_hash(const char *name)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (; *name; ++name) {
        hash ^= (uint8_t)xx_jffs2_old_upper(*name);
        hash *= UINT64_C(0x100000001b3);
    }
    return hash | 1U;
}

static size_t xx_jffs2_old_hash_slot(size_t capacity, uint64_t key)
{
    key ^= key >> 31U;
    key *= UINT64_C(0x94d049bb133111eb);
    key ^= key >> 29U;
    return (size_t)key & (capacity - 1U);
}

/* 1 = inserted, 0 = already present, -1 = allocation failure. */
static int xx_jffs2_old_hash_insert(xx_jffs2_old_inoset *set, uint64_t key)
{
    size_t slot;
    if ((set->count + 1U) * 4U >= set->capacity * 3U) {
        size_t capacity = set->capacity ? set->capacity * 2U : 256U;
        size_t index;
        uint64_t *slots;
        if (capacity < set->capacity || capacity > SIZE_MAX / sizeof(*slots)) {
            return -1;
        }
        slots = (uint64_t *)xx_mem_calloc(capacity, sizeof(*slots));
        if (!slots) return -1;
        for (index = 0U; index < set->capacity; ++index) {
            uint64_t stored = set->slots[index];
            if (stored == 0U) continue;
            slot = xx_jffs2_old_hash_slot(capacity, stored);
            while (slots[slot] != 0U) slot = (slot + 1U) & (capacity - 1U);
            slots[slot] = stored;
        }
        if (set->slots) xx_mem_free(set->slots);
        set->slots = slots;
        set->capacity = capacity;
    }
    slot = xx_jffs2_old_hash_slot(set->capacity, key);
    while (set->slots[slot] != 0U) {
        if (set->slots[slot] == key) return 0;
        slot = (slot + 1U) & (set->capacity - 1U);
    }
    set->slots[slot] = key;
    ++set->count;
    return 1;
}

#define XX_JFFS2_OLD_MAX_RENAMES 1000U

/* Claim *name, or a "<name>_<n>" variant when a case-insensitively equal
 * path was claimed before. On success *name may be replaced by a new
 * allocation (the old one is freed). False means the member is dropped. */
static bool xx_jffs2_old_claim_name(xx_jffs2_old_private *parsed, char **name)
{
    size_t length;
    unsigned attempt;
    int status = xx_jffs2_old_hash_insert(&parsed->claimed, xx_jffs2_old_fold_hash(*name));
    if (status != 0) return status > 0;
    length = xx_str_len(*name);
    if (length > XX_JFFS2_OLD_MAX_PATH) return false;
    for (attempt = 1U; attempt <= XX_JFFS2_OLD_MAX_RENAMES; ++attempt) {
        char digits[8];
        size_t count = 0U;
        unsigned value = attempt;
        char *candidate;
        size_t position;
        while (value != 0U && count < sizeof(digits)) {
            digits[count++] = (char)('0' + value % 10U);
            value /= 10U;
        }
        candidate = (char *)xx_mem_alloc(length + 2U + count);
        if (!candidate) return false;
        xx_mem_copy(candidate, *name, length);
        position = length;
        candidate[position++] = '_';
        while (count != 0U) candidate[position++] = digits[--count];
        candidate[position] = '\0';
        status = xx_jffs2_old_hash_insert(&parsed->claimed, xx_jffs2_old_fold_hash(candidate));
        if (status > 0) {
            xx_str_free(*name);
            *name = candidate;
            return true;
        }
        xx_mem_free(candidate);
        if (status < 0) return false;
    }
    return false;
}

/* ------------------------------------------------------- tree walker --- */

static bool xx_jffs2_old_walk(xx_io_device *device, xx_jffs2_old_private *parsed, uint32_t pino, const char *prefix, unsigned depth, xx_pd_struct *pd);

static bool xx_jffs2_old_add_member(xx_io_device *device, xx_jffs2_old_private *parsed, const xx_jffs2_old_dirent *dirent, const char *prefix, unsigned depth,
                                    xx_pd_struct *pd)
{
    xx_jffs2_old_entry entry;
    char *full_name = xx_jffs2_old_join_name(prefix, dirent->name);
    bool present = false;
    if (!full_name) return false;
    if (!xx_jffs2_old_claim_name(parsed, &full_name)) {
        /* No distinct spelling left for this path: drop the member (and,
         * for a directory, its subtree) rather than let it overwrite. */
        xx_str_free(full_name);
        return true;
    }
    xx_mem_zero(&entry, sizeof(entry));
    entry.name = full_name;
    entry.ino = dirent->ino;
    entry.mtime = dirent->mctime;
    entry.header_offset = dirent->node_offset;
    entry.size = xx_jffs2_old_inode_size(parsed, dirent->ino, &entry.mode, &entry.mtime, &present);
    entry.has_data = present;
    if (dirent->type == XX_JFFS2_OLD_DT_DIR) {
        entry.is_folder = true;
        entry.size = 0U;
    } else if (dirent->type == XX_JFFS2_OLD_DT_LNK) {
        uint8_t *target = NULL;
        size_t target_size = 0U;
        entry.is_link = true;
        /* A symlink's target is the inode's contents, so it is resolved
         * now: it is at most a page and the listing wants to show it. */
        if (xx_jffs2_old_reconstruct(device, parsed, dirent->ino, XX_JFFS2_OLD_MAX_LINK_TARGET, &target, &target_size, pd)) {
            char *text = (char *)xx_mem_alloc(target_size + 1U);
            if (text) {
                if (target_size != 0U) xx_mem_copy(text, target, target_size);
                text[target_size] = '\0';
                entry.link_target = text;
            }
            xx_mem_free(target);
        }
    } else if (dirent->type != XX_JFFS2_OLD_DT_REG) {
        /* Block, character, fifo and socket nodes carry a device number
         * rather than contents; there is nothing to extract. */
        entry.is_special = true;
        entry.size = 0U;
    } else if (!present) {
        /* A regular file whose data nodes never made it into the image.
         * It is still a real name, so it is listed with size zero. */
        entry.size = 0U;
    }
    if (entry.is_folder) {
        /* Append before recursing, so the directory is listed ahead of its
         * contents, and borrow the stored name for the child prefix. */
        const char *child_prefix;
        if (!xx_jffs2_old_append_entry(parsed, &entry)) {
            xx_str_free(full_name);
            if (entry.link_target) xx_str_free(entry.link_target);
            return false;
        }
        child_prefix = parsed->entries[parsed->count - 1U].name;
        return xx_jffs2_old_walk(device, parsed, dirent->ino, child_prefix, depth + 1U, pd);
    }
    if (!xx_jffs2_old_append_entry(parsed, &entry)) {
        xx_str_free(full_name);
        if (entry.link_target) xx_str_free(entry.link_target);
        return false;
    }
    return true;
}

/* List one directory. The dirent array is sorted by (pino, name, version),
 * so the entries of one directory form a contiguous run and, within it, the
 * revisions of one name form a contiguous sub-run whose last element is the
 * one that survives. An ino of zero there means the name was unlinked. */
static bool xx_jffs2_old_walk(xx_io_device *device, xx_jffs2_old_private *parsed, uint32_t pino, const char *prefix, unsigned depth, xx_pd_struct *pd)
{
    size_t index;
    if (depth > XX_JFFS2_OLD_MAX_DEPTH) return true;
    if (xx_jffs2_old_inoset_mark(&parsed->visited, pino)) return true;
    index = xx_jffs2_old_dirent_lower_bound(parsed->dirents, parsed->dirent_count, pino);
    while (index < parsed->dirent_count && parsed->dirents[index].pino == pino) {
        size_t last = index;
        if (pd && xx_pd_is_stopped(pd)) {
            xx_jffs2_old_inoset_unmark(&parsed->visited, pino);
            return false;
        }
        while (last + 1U < parsed->dirent_count && parsed->dirents[last + 1U].pino == pino &&
               xx_str_cmp(parsed->dirents[last + 1U].name, parsed->dirents[index].name) == 0) {
            ++last;
        }
        if (parsed->dirents[last].ino != 0U && parsed->count < XX_JFFS2_OLD_MAX_ENTRIES) {
            if (!xx_jffs2_old_add_member(device, parsed, &parsed->dirents[last], prefix, depth, pd)) {
                xx_jffs2_old_inoset_unmark(&parsed->visited, pino);
                return false;
            }
        }
        index = last + 1U;
    }
    /* A directory may legitimately be reachable by more than one path only
     * through a corrupt image; releasing the mark keeps the set a record of
     * the current chain rather than of everything ever seen. */
    xx_jffs2_old_inoset_unmark(&parsed->visited, pino);
    return true;
}

/* ------------------------------------------------------------- parse --- */

static bool xx_jffs2_old_parse(Abstractformat *self, xx_jffs2_old_private *parsed, xx_pd_struct *pd)
{
    int64_t total_size;
    uint8_t scratch[sizeof(xx_jffs2_old_dirent) > sizeof(xx_jffs2_old_fragment) ? sizeof(xx_jffs2_old_dirent) : sizeof(xx_jffs2_old_fragment)];
    /* Initialise before the guard clause: callers run the cleanup on their
     * stack copy whatever this returns, and cleaning an uninitialised one
     * would free indeterminate pointers. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (!xx_jffs2_old_range_within(total_size, self->base_address, (int64_t)XX_JFFS2_OLD_UNKNOWN_NODE_SIZE)) {
        goto fail;
    }
    parsed->input_size = total_size;
    if (!xx_jffs2_old_scan(self, parsed, pd)) goto fail;
    /* A cleanmarker on its own is a valid but empty image; require at least
     * one dirent so that random data carrying one lucky header does not
     * register as a filesystem. */
    if (parsed->dirent_count == 0U) goto fail;
    xx_jffs2_old_sort(parsed->dirents, parsed->dirent_count, sizeof(*parsed->dirents), xx_jffs2_old_dirent_compare, scratch);
    xx_jffs2_old_sort(parsed->fragments, parsed->fragment_count, sizeof(*parsed->fragments), xx_jffs2_old_fragment_compare, scratch);
    if (!xx_jffs2_old_walk(self->device, parsed, XX_JFFS2_OLD_ROOT_INO, "", 0U, pd)) {
        goto fail;
    }
    if (parsed->count == 0U) goto fail;
    return true;
fail:
    xx_jffs2_old_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------ records --- */

static bool xx_jffs2_old_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_jffs2_old_find_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_jffs2_old_populate_record(xx_archive_record *record, const xx_jffs2_old_entry *entry)
{
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = 0;
    record->data_offset = -1;
    record->compressed_size = 0;
    if (!xx_archive_record_set_original_name(record, entry->name) || !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, entry->size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, entry->size) || !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, entry->mode) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, entry->mtime) || !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, entry->is_folder)) {
        return false;
    }
    if (entry->link_target && !xx_archive_record_set_meta_str(record, XX_META_ID_LINK_TARGET, entry->link_target)) {
        return false;
    }
    return true;
}

static void xx_jffs2_old_archive_stream_free(void *pointer)
{
    xx_jffs2_old_archive_stream *stream = (xx_jffs2_old_archive_stream *)pointer;
    if (!stream) return;
    xx_jffs2_old_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ---------------------------------------------------------- lifetime --- */

void xx_jffs2_old_init(xx_jffs2_old *jffs2_old, xx_io_device *dev, int64_t base_address)
{
    if (!jffs2_old) return;
    xx_mem_zero(jffs2_old, sizeof(*jffs2_old));
    xx_format_init(&jffs2_old->format, dev, base_address);
    /* Overwritten by handle_base_info() once the image has been read. */
    jffs2_old->format.endian = XX_ENDIAN_LITTLE;
    jffs2_old->format.file_type = XX_JFFS2_OLD_FILE_TYPE;
    jffs2_old->format.format_type = XX_TYPE_ARCHIVE;
    jffs2_old->format.is_archive = true;
    xx_format_set_mime_type(&jffs2_old->format, "application/x-jffs2");
    xx_format_set_extension(&jffs2_old->format, "jffs2");
    jffs2_old->format.check_is_valid = xx_jffs2_old_check_is_valid;
    jffs2_old->format.handle_base_info = xx_jffs2_old_handle_base_info;
    jffs2_old->format.get_format_size = xx_jffs2_old_get_format_size;
    jffs2_old->format.get_number_of_archive_records = xx_jffs2_old_get_number_of_archive_records;
    jffs2_old->format.create_archive_records_reading = xx_jffs2_old_create_archive_records_reading;
    jffs2_old->format.get_current_archive_record = xx_jffs2_old_get_current_archive_record;
    jffs2_old->format.unpack_current_archive_record = xx_jffs2_old_unpack_current_archive_record;
    jffs2_old->format.archive_record_move_to_next = xx_jffs2_old_archive_record_move_to_next;
    jffs2_old->format.free_archive_records_reading = xx_jffs2_old_free_archive_records_reading;
    jffs2_old->format.destroy = xx_jffs2_old_vtable_destroy;
    jffs2_old->archive_end = -1;
}

xx_jffs2_old *xx_jffs2_old_create(xx_io_device *dev, int64_t base_address)
{
    xx_jffs2_old *jffs2_old = (xx_jffs2_old *)xx_mem_alloc(sizeof(*jffs2_old));
    if (jffs2_old) xx_jffs2_old_init(jffs2_old, dev, base_address);
    return jffs2_old;
}

void xx_jffs2_old_destroy(xx_jffs2_old *jffs2_old)
{
    if (!jffs2_old) return;
    if (jffs2_old->internal) {
        xx_jffs2_old_private_cleanup((xx_jffs2_old_private *)jffs2_old->internal);
        xx_mem_free(jffs2_old->internal);
        jffs2_old->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&jffs2_old->format);
}

static void xx_jffs2_old_vtable_destroy(Abstractformat *self)
{
    xx_jffs2_old_destroy((xx_jffs2_old *)self);
}

void xx_jffs2_old_free(xx_jffs2_old *jffs2_old)
{
    if (!jffs2_old) return;
    xx_jffs2_old_destroy(jffs2_old);
    xx_mem_free(jffs2_old);
}

/* -------------------------------------------------------------- vtable --- */

bool xx_jffs2_old_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_jffs2_old_private parsed;
    bool result = xx_jffs2_old_parse(self, &parsed, pd);
    xx_jffs2_old_private_cleanup(&parsed);
    return result;
}

bool xx_jffs2_old_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_jffs2_old_private *parsed;
    xx_jffs2_old *jffs2_old = (xx_jffs2_old *)self;
    int64_t total_size;
    if (!self || !jffs2_old) return false;
    parsed = (xx_jffs2_old_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_jffs2_old_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (jffs2_old->internal) {
        xx_jffs2_old_private_cleanup((xx_jffs2_old_private *)jffs2_old->internal);
        xx_mem_free(jffs2_old->internal);
    }
    jffs2_old->internal = parsed;
    jffs2_old->number_of_records = parsed->count;
    jffs2_old->number_of_members = parsed->count;
    jffs2_old->number_of_nodes = parsed->node_count;
    jffs2_old->number_of_dirents = parsed->dirent_count;
    jffs2_old->number_of_inodes = parsed->fragment_count;
    jffs2_old->archive_end = parsed->archive_end;
    jffs2_old->compression_mask = parsed->compression_mask;
    jffs2_old->is_big_endian = parsed->big_endian;
    self->endian = parsed->big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    /* Trailing erased flash is common and is not an overlay in any useful
     * sense, but reporting it keeps the accounting consistent with the
     * other filesystem readers. */
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

int64_t xx_jffs2_old_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_jffs2_old_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_jffs2_old *)self)->number_of_records;
}

xx_archive_record_state *xx_jffs2_old_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    xx_jffs2_old_archive_stream *stream;
    if (!self || !self->device || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_jffs2_old_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_jffs2_old_copy_options(&state->options, options) || !xx_jffs2_old_parse(self, &stream->parsed, pd)) {
        xx_jffs2_old_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_jffs2_old_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U && xx_jffs2_old_populate_record(&state->current_record, &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_jffs2_old_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_jffs2_old_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_jffs2_old_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_jffs2_old_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_jffs2_old_populate_record(&state->current_record, &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_jffs2_old_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_jffs2_old_archive_stream *stream;
    const xx_jffs2_old_entry *entry;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination_path = NULL;
    xx_io_device *destination = NULL;
    uint8_t *data = NULL;
    size_t data_size = 0U;
    bool result = false;
    bool created = false;
    if (!self || !self->device || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_jffs2_old_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    entry = &stream->parsed.entries[stream->index];
    if (!xx_jffs2_old_safe_name(entry->name)) return false;

    option = xx_jffs2_old_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the member could be produced. A
         * directory and a special node always can; a file is answered by
         * actually replaying it, because unlike a stored archive there is
         * no single span whose presence would settle the question. */
        if (entry->is_folder || entry->is_special) return true;
        if (entry->is_link) return entry->link_target != NULL;
        if (!entry->has_data) return true;
        if (!xx_jffs2_old_reconstruct(self->device, &stream->parsed, entry->ino, XX_JFFS2_OLD_MAX_FILE_SIZE, &data, &data_size, pd)) {
            return false;
        }
        xx_mem_free(data);
        return true;
    }
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') {
        destination_path = xx_str_concat3(base, "/", entry->name);
    } else {
        destination_path = xx_str_concat(base, entry->name);
    }
    if (!destination_path) goto cleanup;
    if (entry->is_folder) {
        result = xx_store_create_dirs_a(destination_path, true);
        goto cleanup;
    }
    if (entry->is_special) {
        /* Device, fifo and socket nodes have no contents to write and are
         * reported as handled rather than as a failure. */
        result = true;
        goto cleanup;
    }
    if (!xx_store_create_dirs_a(destination_path, false)) goto cleanup;
    if (entry->is_link) {
        /* A symlink is materialised as a plain file holding its target: the
         * library does not create links, and dropping the member outright
         * would lose information the image carries. */
        if (!entry->link_target) goto cleanup;
        data_size = xx_str_len(entry->link_target);
        data = (uint8_t *)xx_mem_alloc(data_size == 0U ? 1U : data_size);
        if (!data) goto cleanup;
        if (data_size != 0U) xx_mem_copy(data, entry->link_target, data_size);
    } else if (entry->has_data && !xx_jffs2_old_reconstruct(self->device, &stream->parsed, entry->ino, XX_JFFS2_OLD_MAX_FILE_SIZE, &data, &data_size, pd)) {
        /* A name whose inode nodes never reached the image is written as an
         * empty file rather than dropped; the name is real either way. */
        goto cleanup;
    }
    destination = xx_io_file_open(destination_path, "wb");
    created = destination != NULL;
    if (!destination) goto cleanup;
    result = (data_size == 0U) || xx_store_unpack_memory_to_device(data, data_size, destination, pd);
    xx_io_close(destination);
    destination = NULL;
    if (!result && created) xx_rt_remove(destination_path);

cleanup:
    if (destination) xx_io_close(destination);
    if (data) xx_mem_free(data);
    if (owned_base) xx_str_free(owned_base);
    if (destination_path) xx_str_free(destination_path);
    return result;
}

void xx_jffs2_old_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}

/* --------------------------------------------------------- accessors --- */

uint64_t xx_jffs2_old_get_number_of_records(const xx_jffs2_old *jffs2_old)
{
    return jffs2_old ? jffs2_old->number_of_records : 0U;
}
uint64_t xx_jffs2_old_get_number_of_members(const xx_jffs2_old *jffs2_old)
{
    return jffs2_old ? jffs2_old->number_of_members : 0U;
}
uint64_t xx_jffs2_old_get_number_of_nodes(const xx_jffs2_old *jffs2_old)
{
    return jffs2_old ? jffs2_old->number_of_nodes : 0U;
}
uint64_t xx_jffs2_old_get_number_of_dirents(const xx_jffs2_old *jffs2_old)
{
    return jffs2_old ? jffs2_old->number_of_dirents : 0U;
}
uint64_t xx_jffs2_old_get_number_of_inodes(const xx_jffs2_old *jffs2_old)
{
    return jffs2_old ? jffs2_old->number_of_inodes : 0U;
}
int64_t xx_jffs2_old_get_archive_end(const xx_jffs2_old *jffs2_old)
{
    return jffs2_old ? jffs2_old->archive_end : -1;
}
uint32_t xx_jffs2_old_get_compression_mask(const xx_jffs2_old *jffs2_old)
{
    return jffs2_old ? jffs2_old->compression_mask : 0U;
}
bool xx_jffs2_old_get_is_big_endian(const xx_jffs2_old *jffs2_old)
{
    return jffs2_old ? jffs2_old->is_big_endian : false;
}
