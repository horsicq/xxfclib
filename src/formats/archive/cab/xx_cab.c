/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/archive/cab/xx_cab.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzx/xx_lzx.h"
#include "xxfclib/algo/quantum/xx_quantum.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "../../../io/xx_io_policy.h"
#include "xxfclib/memory/xx_memory.h"
#include "../../xx_archive_output_name.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"
#include "xxfclib/threads/xx_threads.h"
#include "../../../algo/lzx/xx_lzx_gated.h"

#define CAB_FLAG_PREV 1U
#define CAB_FLAG_NEXT 2U
#define CAB_FLAG_RESERVE 4U
#define CAB_METHOD_STORE 0U
#define CAB_METHOD_MSZIP 1U
#define CAB_METHOD_QUANTUM 2U
#define CAB_METHOD_LZX 3U
#define CAB_FRAME 32768U /* output bytes of every CFDATA block of an LZX folder but the last */

typedef struct cab_folder_s {
    uint32_t data_offset;
    uint16_t blocks, type;
    /* Filled while cab_parse walks the CFDATA headers: the folder's data
     * area (headers and packed bytes of every block), the offset of its last
     * block's packed bytes (both relative to data_offset), and the sum of
     * the blocks' plain sizes. */
    uint32_t raw_size, last_payload, plain_total;
    bool lzx_valid;
} cab_folder;
typedef struct cab_file_s {
    uint32_t size, folder_offset;
    uint16_t folder, date, time, attrs;
    char *name;
} cab_file;
typedef struct cab_context_s {
    cab_folder *folders;
    size_t folder_count;
    cab_file *files;
    size_t file_count, index;
    uint8_t folder_reserve, data_reserve;
    int64_t cabinet_size;
    uint8_t *decoded_folder;
    size_t decoded_size, decoded_index;
    uint32_t stage_seq;
} cab_context;

#ifndef CAB_ASYNC_MIN_OUTPUT
#define CAB_ASYNC_MIN_OUTPUT ((size_t)4U << 20) /* smaller folders are decoded on the calling thread */
#endif

/* Documented fixed estimates for heap allocations made inside the codecs and
 * the threaded LZX path (charged up-front and before allocation when MEMORY_LIMIT
 * is active):
 * - LZX (out\xx_lzx.c:616): xx_lzx_cab_decode_gated allocates one lzx_state
 *   heap block via xx_mem_calloc(1, sizeof(lzx_state)) = 77,792 bytes on 64-bit
 *   (77,784 bytes on 32-bit: 12,544*4 + 8,960*2 + 2,816*2 + 128*2 + tables/state).
 * - Threaded LZX sync/thread objects (cab_decode_lzx_async): xx_mutex_create
 *   (40 B Win64 / <=64 B POSIX) + two xx_cond_create (16 B Win64 / <=96 B POSIX)
 *   + xx_thread_create control block (32 B) = 88 B Win64 (<=192 B POSIX);
 *   conservatively 256 bytes.
 * - Quantum (src\algo\quantum\xx_quantum.c:336-352): xx_quantum_cab_decode
 *   holds q_state and q_range on the stack and uses the caller's output buffer
 *   as its history window, making 0 heap allocations.
 * - MSZIP (src\algo\deflate\xx_deflate_dec.c:623,1237):
 *   xx_deflate_decompress_memory_with_dictionary runs with dev==NULL (0 heap
 *   bytes in xx_br_init), allocates xx_dec_state (48,980 B) and, when
 *   dictionary_size > 0, xx_out_lin.base (32 KiB + 256 KiB = 294,912 B),
 *   totaling 343,892 B; conservatively rounded up to 384 KiB (393,216 B). */
#define CAB_LZX_DECODER_STATE_BYTES 77792U
#define CAB_LZX_ASYNC_SYNC_BYTES 256U
#define CAB_QUANTUM_DECODER_STATE_BYTES 0U
#define CAB_MSZIP_DECODER_STATE_BYTES (384U * 1024U)

typedef struct cab_limits_s {
    bool limits_active;
    bool member_present;
    bool memory_present;
    uint64_t member_limit;
    uint64_t memory_limit;
} cab_limits;

typedef struct cab_budget_s {
    bool limits_active;
    uint64_t memory_limit;
    uint64_t live_bytes;
} cab_budget;

static bool cab_add_u64(uint64_t *total, uint64_t amount)
{
    if (!total || *total > UINT64_MAX - amount) return false;
    *total += amount;
    return true;
}

static bool cab_mul_u64(uint64_t count, uint64_t elem_size, uint64_t *out)
{
    if (!out) return false;
    if (elem_size && count > UINT64_MAX / elem_size) return false;
    *out = count * elem_size;
    return true;
}

static bool cab_get_u64_limit(const Abstractformat *f, const xx_list_s *options, uint32_t meta_id, bool *present, uint64_t *limit)
{
    const xx_var *value;
    int64_t signed_value;
    if (!present || !limit) return false;
    *present = false;
    *limit = 0;
    value = xx_format_resolve_extra_parameter(f, options, meta_id);
    if (!value) return true;
    *present = true;
    switch ((xx_var_type_t)value->type) {
    case XX_VAR_TYPE_UINT8:
    case XX_VAR_TYPE_UINT16:
    case XX_VAR_TYPE_UINT32:
    case XX_VAR_TYPE_UINT64:
        *limit = xx_var_get_u64(value);
        return true;
    case XX_VAR_TYPE_INT8:
    case XX_VAR_TYPE_INT16:
    case XX_VAR_TYPE_INT32:
    case XX_VAR_TYPE_INT64:
        signed_value = xx_var_get_i64(value);
        if (signed_value < 0) return false;
        *limit = (uint64_t)signed_value;
        return true;
    default:
        return false;
    }
}

static bool cab_resolve_limits(const Abstractformat *f, const xx_list_s *options, cab_limits *limits, xx_pd_struct *pd)
{
    xx_mem_zero(limits, sizeof(*limits));
    if ((!options || !options->count) && (!f || !f->list_extra_parameters.count)) return true;
    if (!cab_get_u64_limit(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE, &limits->member_present, &limits->member_limit) ||
        !cab_get_u64_limit(f, options, XX_META_ID_OPT_MEMORY_LIMIT, &limits->memory_present, &limits->memory_limit)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "CAB extraction limits must be nonnegative integers");
        return false;
    }
    limits->limits_active = limits->member_present || limits->memory_present;
    return true;
}

static bool cab_budget_reserve(cab_budget *budget, uint64_t bytes, xx_pd_struct *pd)
{
    if (!budget || !budget->limits_active) return true;
    if (budget->live_bytes > budget->memory_limit || bytes > budget->memory_limit - budget->live_bytes) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "CAB extraction exceeds the configured memory limit");
        return false;
    }
    budget->live_bytes += bytes;
    return true;
}

static void cab_budget_release(cab_budget *budget, uint64_t bytes)
{
    if (!budget || !budget->limits_active) return;
    budget->live_bytes = bytes <= budget->live_bytes ? budget->live_bytes - bytes : 0U;
}

static uint64_t cab_budget_available(const cab_budget *budget)
{
    if (!budget || !budget->limits_active || budget->live_bytes >= budget->memory_limit) return 0U;
    return budget->memory_limit - budget->live_bytes;
}

static bool cab_resource_limits_allow(const cab_limits *limits, const cab_context *c, const cab_file *x, cab_budget *budget,
                                      cab_budget **decode_budget, xx_pd_struct *pd)
{
    uint64_t required = 0, raw_alloc, out_alloc;
    const cab_folder *folder;
    uint16_t method;

    *decode_budget = NULL;
    if (!limits || !limits->limits_active) return true;
    if (limits->member_present && (uint64_t)x->size > limits->member_limit) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "CAB member exceeds the configured size limit");
        return false;
    }
    /* When the record's folder is already decoded and cached, check only
     * MAX_MEMBER_SIZE; charge MEMORY_LIMIT only when the folder must decode. */
    if (!limits->memory_present || (c->decoded_folder && c->decoded_index == x->folder)) return true;

    folder = &c->folders[x->folder];
    method = folder->type & 15U;
    raw_alloc = folder->raw_size ? (uint64_t)folder->raw_size : 1ULL;
    out_alloc = folder->plain_total ? (uint64_t)folder->plain_total : 1ULL;
    if (!cab_add_u64(&required, raw_alloc) || !cab_add_u64(&required, out_alloc)) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "CAB extraction exceeds the configured memory limit");
        return false;
    }
    if (method == CAB_METHOD_LZX || method == CAB_METHOD_QUANTUM) {
        uint64_t arr_ptrs = 0, arr_packed = 0, arr_plain = 0, codec_bytes = 0;
        if (!cab_mul_u64(folder->blocks, sizeof(const uint8_t *), &arr_ptrs) || !cab_mul_u64(folder->blocks, sizeof(size_t), &arr_packed) ||
            !cab_mul_u64(folder->blocks, sizeof(size_t), &arr_plain) || !cab_add_u64(&required, arr_ptrs) || !cab_add_u64(&required, arr_packed) ||
            !cab_add_u64(&required, arr_plain)) {
            xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "CAB extraction exceeds the configured memory limit");
            return false;
        }
        if (method == CAB_METHOD_LZX) {
            codec_bytes = (uint64_t)CAB_LZX_DECODER_STATE_BYTES;
            if (folder->lzx_valid && (uint64_t)folder->plain_total >= (uint64_t)CAB_ASYNC_MIN_OUTPUT && folder->raw_size > 0U) {
                codec_bytes += (uint64_t)CAB_LZX_ASYNC_SYNC_BYTES;
            }
        } else {
            codec_bytes = (uint64_t)CAB_QUANTUM_DECODER_STATE_BYTES;
        }
        if (!cab_add_u64(&required, codec_bytes)) {
            xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "CAB extraction exceeds the configured memory limit");
            return false;
        }
    } else if (method == CAB_METHOD_MSZIP) {
        if (!cab_add_u64(&required, (uint64_t)CAB_MSZIP_DECODER_STATE_BYTES)) {
            xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "CAB extraction exceeds the configured memory limit");
            return false;
        }
    }
    if (required > limits->memory_limit) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "CAB extraction exceeds the configured memory limit");
        return false;
    }
    budget->limits_active = true;
    budget->memory_limit = limits->memory_limit;
    budget->live_bytes = 0;
    *decode_budget = budget;
    return true;
}

static bool cab_read(xx_io_device *d, int64_t o, void *p, size_t n)
{
    size_t x = 0;
    if (!d || o < 0 || o > LONG_MAX || xx_io_seek(d, (long)o, SEEK_SET)) return false;
    while (x < n) {
        ssize_t r = xx_io_read(d, (uint8_t *)p + x, n - x);
        if (r <= 0 || (size_t)r > n - x) return false;
        x += (size_t)r;
    }
    return true;
}
/* Buffered reads for cab_parse's CFDATA header walk: one seek and one large
 * read per window instead of a seek and an 8-byte read per block (a large
 * folder has tens of thousands of blocks, and the walk runs on every open). */
#define CAB_WALK_WINDOW (1U << 20)
typedef struct cab_walk_s {
    xx_io_device *device;
    uint8_t *buffer;
    int64_t base;
    size_t length;
} cab_walk;

/* Reads n bytes at o; the caller has checked that n <= end - o. */
static bool cab_walk_read(cab_walk *w, int64_t o, int64_t end, uint8_t *dst, size_t n)
{
    if (o < 0 || o > LONG_MAX) return false; /* as cab_read */
    if (o > end || (uint64_t)(end - o) < n) return false;
    if (!w->buffer || n == 0 || n > CAB_WALK_WINDOW) return cab_read(w->device, o, dst, n);
    if (o < w->base || o - w->base > (int64_t)w->length || n > w->length - (size_t)(o - w->base)) {
        /* n <= end - o and n <= the window, so n <= want <= the window. */
        int64_t avail = end - o;
        size_t want = avail > (int64_t)CAB_WALK_WINDOW ? CAB_WALK_WINDOW : (size_t)avail;
        if (!cab_read(w->device, o, w->buffer, want)) {
            /* A device shorter than the window: read exactly as before. */
            w->length = 0;
            return cab_read(w->device, o, dst, n);
        }
        w->base = o;
        w->length = want;
    }
    xx_mem_copy(dst, w->buffer + (size_t)(o - w->base), n);
    return true;
}

static void cab_free(void *p)
{
    cab_context *c = (cab_context *)p;
    size_t i;
    if (!c) return;
    if (c->files) {
        for (i = 0; i < c->file_count; i++)
            if (c->files[i].name) xx_str_free(c->files[i].name);
        xx_mem_free(c->files);
    }
    if (c->folders) xx_mem_free(c->folders);
    if (c->decoded_folder) xx_mem_free(c->decoded_folder);
    xx_mem_free(c);
}
static bool cab_cstring(Abstractformat *f, int64_t end, int64_t *at, char **value)
{
    size_t n = 0;
    uint8_t b;
    if (!at || *at < 0) return false;
    while (*at + (int64_t)n < end) {
        if (!cab_read(f->device, *at + (int64_t)n, &b, 1)) return false;
        if (!b) break;
        if (b < 0x20U) return false;
        n++;
        if (n > 32767U) return false;
    }
    if (*at + (int64_t)n >= end) return false;
    if (value) {
        *value = (char *)xx_mem_alloc(n + 1U);
        if (!*value) return false;
        if (n && !cab_read(f->device, *at, *value, n)) {
            xx_mem_free(*value);
            *value = NULL;
            return false;
        }
        (*value)[n] = 0;
    }
    *at += (int64_t)n + 1;
    return true;
}
#include "xx_cab_names.h"

#define CAB_WALK_LZX_OK 0x10000U
#define CAB_WALK_LEADER_MASK 0xFFFFU

static bool cab_lzx_type_ok(uint16_t type)
{
    unsigned window_bits;
    if ((type & 15U) != CAB_METHOD_LZX) return false;
    window_bits = (type >> 8U) & 31U;
    return window_bits >= 15U && window_bits <= 21U;
}

static bool cab_folder_key_less(const cab_folder *folders, uint16_t a, uint16_t b)
{
    if (folders[a].data_offset != folders[b].data_offset) return folders[a].data_offset < folders[b].data_offset;
    if (folders[a].blocks != folders[b].blocks) return folders[a].blocks < folders[b].blocks;
    return a < b;
}

static void cab_folder_sift_down(const cab_folder *folders, uint16_t *order, size_t root, size_t count)
{
    uint16_t val = order[root];
    while (root < count / 2U) {
        size_t child = root * 2U + 1U;
        if (child + 1U < count && cab_folder_key_less(folders, order[child], order[child + 1U])) child++;
        if (!cab_folder_key_less(folders, val, order[child])) break;
        order[root] = order[child];
        root = child;
    }
    order[root] = val;
}

/* Maps each folder index i to the earliest folder index <= i with the same
 * (data_offset, blocks) in O(n log n) worst-case time.  Returns NULL when
 * nfolders < 2 or on allocation failure (the caller walks every folder). */
static uint32_t *cab_build_folder_leaders(const cab_folder *folders, uint16_t nfolders)
{
    uint16_t *order;
    uint32_t *leader;
    uint16_t canon;
    size_t i;
    if (nfolders < 2U) return NULL;
    order = (uint16_t *)xx_mem_alloc((size_t)nfolders * sizeof(*order));
    leader = (uint32_t *)xx_mem_alloc((size_t)nfolders * sizeof(*leader));
    if (!order || !leader) {
        if (order) xx_mem_free(order);
        if (leader) xx_mem_free(leader);
        return NULL;
    }
    for (i = 0; i < nfolders; i++) order[i] = (uint16_t)i;
    for (i = nfolders / 2U; i > 0U; i--) cab_folder_sift_down(folders, order, i - 1U, nfolders);
    for (i = nfolders; i > 1U; i--) {
        uint16_t tmp = order[0];
        order[0] = order[i - 1U];
        order[i - 1U] = tmp;
        cab_folder_sift_down(folders, order, 0, i - 1U);
    }
    canon = order[0];
    leader[canon] = canon;
    for (i = 1U; i < nfolders; i++) {
        uint16_t idx = order[i];
        if (folders[idx].data_offset != folders[canon].data_offset || folders[idx].blocks != folders[canon].blocks) canon = idx;
        leader[idx] = canon;
    }
    xx_mem_free(order);
    return leader;
}

static bool cab_parse(Abstractformat *f, cab_context **out)
{
    uint8_t h[36];
    uint32_t cabinet, files_at;
    uint16_t flags, nfolders, nfiles;
    int64_t at, end, total;
    cab_context *c;
    size_t i;
    cab_walk walk = {NULL, NULL, 0, 0};
    uint32_t *leader = NULL;
    unsigned codepage;
    if (!f || !f->device || !out || f->base_address < 0) {
        return false;
    }
    total = xx_io_total_size(f->device);
    if (total - f->base_address < 36 || !cab_read(f->device, f->base_address, h, 36) || xx_rt_memcmp(h, "MSCF", 4)) return false;
    cabinet = xx_data_get_u32(h + 8, 4, 0, false);
    files_at = xx_data_get_u32(h + 16, 4, 0, false);
    nfolders = xx_data_get_u16(h + 26, 2, 0, false);
    nfiles = xx_data_get_u16(h + 28, 2, 0, false);
    flags = xx_data_get_u16(h + 30, 2, 0, false);
    if (cabinet < 36U || cabinet > (uint64_t)(total - f->base_address) || !nfolders || !nfiles || files_at >= cabinet) return false;
    end = f->base_address + cabinet;
    at = f->base_address + 36;
    c = (cab_context *)xx_mem_calloc(1, sizeof(*c));
    if (!c) return false;
    c->cabinet_size = cabinet;
    c->folder_count = nfolders;
    c->file_count = nfiles;
    c->folders = (cab_folder *)xx_mem_calloc(nfolders, sizeof(*c->folders));
    c->files = (cab_file *)xx_mem_calloc(nfiles, sizeof(*c->files));
    if (!c->folders || !c->files) goto fail;
    if (flags & CAB_FLAG_RESERVE) {
        uint8_t r[4];
        uint16_t hr;
        if (!cab_read(f->device, at, r, 4)) goto fail;
        hr = xx_data_get_u16(r, 2, 0, false);
        c->folder_reserve = r[2];
        c->data_reserve = r[3];
        at += 4;
        if (at > end || hr > (uint64_t)(end - at)) goto fail;
        at += hr;
    }
    if (flags & CAB_FLAG_PREV) {
        if (!cab_cstring(f, end, &at, NULL) || !cab_cstring(f, end, &at, NULL)) goto fail;
    }
    if (flags & CAB_FLAG_NEXT) {
        if (!cab_cstring(f, end, &at, NULL) || !cab_cstring(f, end, &at, NULL)) goto fail;
    }
    for (i = 0; i < nfolders; i++) {
        uint8_t b[8];
        if (at > end || 8U + c->folder_reserve > (uint64_t)(end - at) || !cab_read(f->device, at, b, 8)) goto fail;
        c->folders[i].data_offset = xx_data_get_u32(b, 4, 0, false);
        c->folders[i].blocks = xx_data_get_u16(b + 4, 2, 0, false);
        c->folders[i].type = xx_data_get_u16(b + 6, 2, 0, false);
        if (c->folders[i].data_offset >= cabinet || !c->folders[i].blocks) goto fail;
        at += 8 + c->folder_reserve;
    }
    at = f->base_address + files_at;
    codepage = cab_name_pe_codepage(f);
    for (i = 0; i < nfiles; i++) {
        uint8_t b[16];
        char *name = NULL;
        if (at > end || end - at < 16 || !cab_read(f->device, at, b, 16)) goto fail;
        c->files[i].size = xx_data_get_u32(b, 4, 0, false);
        c->files[i].folder_offset = xx_data_get_u32(b + 4, 4, 0, false);
        c->files[i].folder = xx_data_get_u16(b + 8, 2, 0, false);
        c->files[i].date = xx_data_get_u16(b + 10, 2, 0, false);
        c->files[i].time = xx_data_get_u16(b + 12, 2, 0, false);
        c->files[i].attrs = xx_data_get_u16(b + 14, 2, 0, false);
        at += 16;
        if (c->files[i].folder >= nfolders || !cab_cstring(f, end, &at, &name)) goto fail;
        c->files[i].name = cab_name_decode(name, c->files[i].attrs, codepage);
        xx_str_free(name);
        if (!c->files[i].name) goto fail;
    }
    /* Assuming f->device returns the same bytes for the same offsets during
     * one cab_parse call (as cab_walk already assumes), folders with an
     * identical (data_offset, blocks) reuse the first matching folder's
     * walk results; only lzx_valid still depends on the folder's own type. */
    leader = cab_build_folder_leaders(c->folders, nfolders);
    walk.device = f->device;
    walk.buffer = (uint8_t *)xx_mem_alloc(CAB_WALK_WINDOW); /* NULL: unbuffered reads */
    for (i = 0; i < nfolders; i++) {
        const int64_t start = f->base_address + c->folders[i].data_offset;
        int64_t p = start;
        uint16_t j;
        uint64_t unpacked = 0;
        bool lzx_blocks_ok = true;
        if (leader && (leader[i] & CAB_WALK_LEADER_MASK) < i) {
            uint32_t canon = leader[i] & CAB_WALK_LEADER_MASK;
            uint32_t entry = leader[canon];
            const cab_folder *src = &c->folders[canon];
            c->folders[i].raw_size = src->raw_size;
            c->folders[i].last_payload = src->last_payload;
            c->folders[i].plain_total = src->plain_total;
            c->folders[i].lzx_valid = cab_lzx_type_ok(c->folders[i].type) && (entry & CAB_WALK_LZX_OK) != 0U;
            continue;
        }
        for (j = 0; j < c->folders[i].blocks; j++) {
            uint8_t b[8];
            uint16_t packed, plain;
            if (p > end || 8U + c->data_reserve > (uint64_t)(end - p) || !cab_walk_read(&walk, p, end, b, 8)) goto fail;
            packed = xx_data_get_u16(b + 4, 2, 0, false);
            plain = xx_data_get_u16(b + 6, 2, 0, false);
            p += 8 + c->data_reserve;
            if (p > end || packed > (uint64_t)(end - p) || !plain) goto fail;
            if (lzx_blocks_ok && (!packed || plain > CAB_FRAME || ((uint32_t)j + 1U < c->folders[i].blocks && plain != CAB_FRAME))) lzx_blocks_ok = false;
            c->folders[i].last_payload = (uint32_t)(p - start);
            p += packed;
            unpacked += plain;
            if (unpacked > UINT32_MAX) goto fail;
        }
        /* p - start <= cabinet size (a uint32_t), unpacked <= UINT32_MAX. */
        c->folders[i].raw_size = (uint32_t)(p - start);
        c->folders[i].plain_total = (uint32_t)unpacked;
        c->folders[i].lzx_valid = cab_lzx_type_ok(c->folders[i].type) && lzx_blocks_ok;
        if (leader && lzx_blocks_ok) leader[i] |= CAB_WALK_LZX_OK;
    }
    if (leader) xx_mem_free(leader);
    if (walk.buffer) xx_mem_free(walk.buffer);
    *out = c;
    return true;
fail:
    if (leader) xx_mem_free(leader);
    if (walk.buffer) xx_mem_free(walk.buffer);
    cab_free(c);
    return false;
}

static bool cab_opts(xx_list_s *d, const xx_list_s *s)
{
    size_t i;
    if (!s) return true;
    for (i = 0; i < s->count; i++) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)s, i);
        xx_meta c;
        if (!m) continue;
        xx_meta_init(&c, m->meta_id);
        if (!xx_var_copy(&c.var, &m->var) || !xx_list_append(d, &c)) {
            xx_meta_cleanup(&c);
            return false;
        }
    }
    return true;
}
static const xx_var *cab_opt(const xx_list_s *l, uint32_t id)
{
    size_t i;
    if (!l) return NULL;
    for (i = 0; i < l->count; i++) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)l, i);
        if (m && m->meta_id == id) return &m->var;
    }
    return NULL;
}
static bool cab_safe(const char *n)
{
    const char *s = n, *p;
    if (!n || !n[0] || n[0] == '/' || n[0] == '\\' || n[1] == ':') return false;
    for (p = n;; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' || ch == '*' || (ch && ch < 32)) return false;
        if (ch == '/' || ch == '\\' || !ch) {
            size_t z = (size_t)(p - s);
            if (!z || (z == 1 && s[0] == '.') || (z == 2 && s[0] == '.' && s[1] == '.')) return false;
            if (!ch) return true;
            s = p + 1;
        }
    }
}
static bool cab_record(xx_archive_record *r, const cab_context *c, size_t i)
{
    const cab_file *x = &c->files[i];
    uint16_t method = c->folders[x->folder].type & 15U;
    xx_archive_record_cleanup(r);
    xx_archive_record_init(r);
    r->data_offset = -1;
    r->compressed_size = -1;
    return xx_archive_record_set_original_name(r, x->name) && xx_archive_record_set_meta_u64(r, XX_META_ID_UNCOMPRESSED_SIZE, x->size) &&
           xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSION_METHOD, method) && xx_archive_record_set_meta_u64(r, XX_META_ID_ATTRIBUTES, x->attrs) &&
           xx_archive_record_set_meta_u64(r, XX_META_ID_TIMESTAMP, ((uint32_t)x->date << 16U) | x->time) &&
           /* Also the packed DOS words: the listing reads TIMESTAMP as Unix seconds. */
           xx_archive_record_set_meta_u64(r, XX_META_ID_LAST_MOD_DATE, x->date & 0xFFFFU) &&
           xx_archive_record_set_meta_u64(r, XX_META_ID_LAST_MOD_TIME, x->time & 0xFFFFU) && xx_archive_record_set_meta_bool(r, XX_META_ID_IS_FOLDER, false) &&
           xx_archive_record_set_meta_bool(r, XX_META_ID_IS_ENCRYPTED, false);
}

/* Reads a folder's whole data area (CFDATA headers and packed bytes, as
 * measured by cab_parse) with one read instead of two seeks and reads per
 * block. */
static bool cab_load_raw(Abstractformat *f, const cab_folder *folder, cab_budget *budget, xx_pd_struct *pd, uint8_t **out, size_t *out_cap)
{
    const int64_t start = f->base_address + (int64_t)folder->data_offset;
    size_t alloc_size = folder->raw_size ? (size_t)folder->raw_size : 1U;
    uint8_t *raw;
    const bool track_budget = budget && budget->limits_active;
    *out = NULL;
    if (out_cap) *out_cap = 0;
    /* The per-block reads this replaces required every header and payload
     * start to be seekable with a long offset; keep rejecting the rest. */
    if (start < 0 || start > LONG_MAX || (int64_t)folder->last_payload > LONG_MAX - start) return false;
    if (track_budget && !cab_budget_reserve(budget, alloc_size, pd)) return false;
    raw = (uint8_t *)xx_mem_alloc(alloc_size);
    if (!raw) {
        if (track_budget) cab_budget_release(budget, alloc_size);
        return false;
    }
    if (folder->raw_size && !cab_read(f->device, start, raw, folder->raw_size)) {
        xx_mem_free(raw);
        if (track_budget) cab_budget_release(budget, alloc_size);
        return false;
    }
    *out = raw;
    if (out_cap) *out_cap = alloc_size;
    return true;
}

/* Steps over one CFDATA header in a loaded data area.  The area was read
 * again from the device, so every size is checked against it. */
static bool cab_next_block(const cab_context *c, const uint8_t *raw, size_t raw_size, size_t *off, const uint8_t **data, uint16_t *packed, uint16_t *plain)
{
    size_t header = 8U + c->data_reserve;
    if (*off > raw_size || header > raw_size - *off) return false;
    *packed = xx_data_get_u16(raw + *off + 4, 2, 0, false);
    *plain = xx_data_get_u16(raw + *off + 6, 2, 0, false);
    *off += header;
    if (!*packed || !*plain || *packed > raw_size - *off) return false;
    *data = raw + *off;
    *off += *packed;
    return true;
}

/* ---- LZX folders: decode on a worker thread while the calling thread loads the input and writes the record ----
 *
 * The calling thread (the owner) keeps every access to the io devices: it reads the folder's data area in slices,
 * steps over the CFDATA headers of what it has read, touches the output pages those blocks will fill (a fresh
 * buffer of this size is otherwise page-faulted by the decoder) and then publishes the number of blocks the decoder
 * may use.  The decoder reports before each block how many output bytes it is done with (it trails its matches'
 * reach, one window); the owner runs the E8 call translation over those bytes, which the decoder would otherwise do
 * itself, and writes the record's translated bytes to the output file.
 *
 * Shared state is only read or written under the job's mutex.  The buffers are shared without it, split by the
 * published counters: the owner writes the block arrays and input bytes of blocks at and above ready_blocks and
 * output touch points at least a page above the end of those blocks; the decoder reads blocks below ready_blocks and
 * reads and writes output from one window below the start of its block to at most 32 bytes past the end of that
 * block; the owner translates and reads output only below settled_bytes, which the decoder no longer uses.
 *
 * The worker never outlives cab_decode_lzx_async: every path out of the owner's loop sets `cancel` (unless the
 * decoder has finished) and joins it.  A record is still reported as extracted only after its whole folder decoded,
 * as before. */

#define CAB_ASYNC_READ_SLICE ((size_t)2U << 20) /* input bytes loaded per step */
#define CAB_ASYNC_LOOKAHEAD 512U                /* blocks kept ready ahead of the decoder (32 KiB of output each) */
#define CAB_ASYNC_WRITE_MIN ((size_t)1U << 20)  /* settled bytes that are worth translating and writing */
#define CAB_ASYNC_WRITE_MAX ((size_t)4U << 20)  /* bytes per write, so that the input stays ahead of the decoder */
#define CAB_ASYNC_PAGE ((size_t)4096U)          /* distance of the output touch points */
#define CAB_ASYNC_POLL_MS 100U                  /* how often a waiting owner looks at the stop flag */

/* Builds a stage path in `destination`'s folder whose filename is `suffix` alone,
 * so long destination filenames near the 255-byte component limit stay valid. */
static char *cab_stage_candidate(const char *destination, const char *suffix)
{
    size_t folder = 0U, index, length = xx_str_len(suffix);
    char *candidate;
    for (index = 0U; destination[index]; ++index) {
        if (destination[index] == '/' || destination[index] == '\\') folder = index + 1U;
    }
    candidate = (char *)xx_mem_alloc(folder + length + 1U);
    if (!candidate) return NULL;
    if (folder) xx_mem_copy(candidate, destination, folder);
    xx_mem_copy(candidate + folder, suffix, length + 1U);
    return candidate;
}

static xx_io_device *cab_open_stage(const char *destination, uint32_t *stage_seq, char **path)
{
    static const char hex[] = "0123456789abcdef";
    unsigned int attempt;
    if (!destination || !stage_seq || !path) return NULL;
    *path = NULL;
    for (attempt = 0U; attempt < 8U; ++attempt) {
        char suffix[46] = ".xxfclib.tmp.";
        uint32_t seq = ++(*stage_seq);
        uint32_t tid = (uint32_t)xx_thread_current_id();
        uint64_t clk = (uint64_t)xx_rt_clock_ms();
        /* No shared random generator (xx_rt_rand keeps unsynchronised static
         * state): the per-context counter's address separates concurrent
         * extractions; exclusive creation and the retries handle the rest. */
        uint32_t rnd = (uint32_t)(uintptr_t)stage_seq ^ (uint32_t)(clk >> 16U);
        uint32_t clk32 = (uint32_t)clk;
        size_t i;
        char *candidate;
        xx_io_device *device;
        bool retry;
        for (i = 0U; i < 4U; ++i) {
            uint8_t byte = (uint8_t)(seq >> (i * 8U));
            suffix[13U + 2U * i] = hex[byte >> 4U];
            suffix[14U + 2U * i] = hex[byte & 15U];
        }
        for (i = 0U; i < 4U; ++i) {
            uint8_t byte = (uint8_t)(tid >> (i * 8U));
            suffix[21U + 2U * i] = hex[byte >> 4U];
            suffix[22U + 2U * i] = hex[byte & 15U];
        }
        for (i = 0U; i < 4U; ++i) {
            uint8_t byte = (uint8_t)(clk32 >> (i * 8U));
            suffix[29U + 2U * i] = hex[byte >> 4U];
            suffix[30U + 2U * i] = hex[byte & 15U];
        }
        for (i = 0U; i < 4U; ++i) {
            uint8_t byte = (uint8_t)(rnd >> (i * 8U));
            suffix[37U + 2U * i] = hex[byte >> 4U];
            suffix[38U + 2U * i] = hex[byte & 15U];
        }
        suffix[45] = '\0';
        candidate = cab_stage_candidate(destination, suffix);
        if (!candidate) return NULL;
        device = xx_io_stage_open(destination, candidate, &retry);
        if (device) {
            *path = candidate;
            return device;
        }
        xx_mem_free(candidate);
        if (!retry) return NULL;
    }
    return NULL;
}

/* Where the bytes of the current record go.  The temporary sibling file is created by the first write
 * and moved over `path` only after the whole folder succeeds and the record's bytes are complete. */
typedef struct cab_sink_s {
    const char *path;
    char *stage_path;
    uint32_t *stage_seq;
    xx_io_device *device;
    size_t offset, size, done; /* the record's place in its folder; `done` of its bytes are written */
    bool failed;
} cab_sink;

static void cab_sink_discard(cab_sink *k)
{
    if (k->device) {
        (void)xx_io_close(k->device);
        k->device = NULL;
    }
    if (k->stage_path) {
        (void)xx_io_file_remove_a(k->stage_path);
        xx_mem_free(k->stage_path);
        k->stage_path = NULL;
    }
}

/* Writes the next `count` bytes of the record from the decoded folder. */
static bool cab_sink_write(cab_sink *k, const uint8_t *folder, size_t count)
{
    if (k->failed) return false;
    if (!k->device) {
        if (!xx_store_create_dirs_a(k->path, false) || !(k->device = cab_open_stage(k->path, k->stage_seq, &k->stage_path))) {
            k->failed = true;
            return false;
        }
    }
    while (count) {
        ssize_t n = xx_io_write(k->device, folder + k->offset + k->done, count);
        if (n <= 0 || (size_t)n > count) {
            k->failed = true;
            cab_sink_discard(k);
            return false;
        }
        k->done += (size_t)n;
        count -= (size_t)n;
    }
    return true;
}

typedef struct cab_job_s {
    xx_mutex_t *mutex;
    xx_cond_t *owner_cond, *decoder_cond;
    /* Decoder arguments.  Array entries and input bytes of blocks below ready_blocks do not change any more. */
    const uint8_t **blocks;
    size_t *packed_sizes, *plain_sizes;
    size_t block_count, output_size;
    unsigned window_bits;
    uint8_t *output;
    int32_t decoder_translation; /* E8 translation size; the worker's until it has finished block 0 */
    /* Guarded by mutex. */
    size_t ready_blocks;            /* owner: blocks the decoder may use */
    size_t decoded_blocks;          /* decoder: blocks it has finished */
    size_t settled_bytes;           /* decoder: output bytes it will not read or write any more */
    int32_t translation;            /* decoder: E8 translation size, valid once decoded_blocks is not 0 */
    size_t wake_blocks, wake_settled; /* owner: wake it when decoded_blocks / settled_bytes reach these */
    bool owner_waiting, decoder_waiting, cancel, done, ok;
} cab_job;

/* xx_lzx_cab_gate_fn, on the worker thread. */
static bool cab_job_gate(void *context, size_t entry, size_t settled_bytes)
{
    cab_job *j = (cab_job *)context;
    bool go;
    xx_mutex_lock(j->mutex);
    j->decoded_blocks = entry;
    j->settled_bytes = settled_bytes;
    if (entry) j->translation = j->decoder_translation;
    if (j->owner_waiting && (entry >= j->wake_blocks || settled_bytes >= j->wake_settled || j->ready_blocks <= entry)) {
        j->owner_waiting = false;
        xx_cond_signal(j->owner_cond);
    }
    while (!j->cancel && j->ready_blocks <= entry) {
        j->decoder_waiting = true;
        xx_cond_wait(j->decoder_cond, j->mutex, XX_WAIT_INFINITE);
    }
    j->decoder_waiting = false;
    go = !j->cancel;
    xx_mutex_unlock(j->mutex);
    return go;
}

static int cab_job_thread(void *arg)
{
    cab_job *j = (cab_job *)arg;
    size_t wrote = 0;
    bool ok = xx_lzx_cab_decode_gated((const uint8_t *const *)j->blocks, j->packed_sizes, j->plain_sizes, j->block_count, j->window_bits, j->output,
                                      j->output_size, &wrote, cab_job_gate, j, &j->decoder_translation) &&
              wrote == j->output_size;
    xx_mutex_lock(j->mutex);
    j->ok = ok;
    j->translation = j->decoder_translation;
    j->done = true;
    j->owner_waiting = false;
    xx_cond_signal(j->owner_cond);
    xx_mutex_unlock(j->mutex);
    return ok ? 0 : 1;
}

/* The owner's progress through the folder's input; only the owner uses it. */
typedef struct cab_feed_s {
    uint8_t *raw;
    size_t raw_loaded, parse_off; /* input bytes read; offset of the first block header not stepped over */
    size_t ready, plain_ready;    /* blocks stepped over, and the sum of their plain sizes */
    size_t touched;               /* output touch points (one per page) below this offset are done */
} cab_feed;

/* Loads the next slice of the data area, steps over the blocks that are complete in what is loaded, and touches
 * their output pages.  False when the input cannot be read or is not the block chain cab_parse measured. */
static bool cab_feed_step(Abstractformat *f, const cab_context *c, const cab_folder *folder, cab_job *j, cab_feed *s)
{
    size_t limit;
    if (s->raw_loaded < folder->raw_size) {
        size_t n = folder->raw_size - s->raw_loaded, got = 0;
        if (n > CAB_ASYNC_READ_SLICE) n = CAB_ASYNC_READ_SLICE;
        if (!s->raw_loaded) {
            /* One seek, as the single read this replaces; the later slices follow on without seeking. */
            if (!cab_read(f->device, f->base_address + (int64_t)folder->data_offset, s->raw, n)) return false;
        } else {
            while (got < n) {
                ssize_t r = xx_io_read(f->device, s->raw + s->raw_loaded + got, n - got);
                if (r <= 0 || (size_t)r > n - got) return false;
                got += (size_t)r;
            }
        }
        s->raw_loaded += n;
    }
    while (s->ready < j->block_count) {
        size_t off = s->parse_off;
        const uint8_t *data;
        uint16_t packed, plain;
        if (!cab_next_block(c, s->raw, s->raw_loaded, &off, &data, &packed, &plain)) {
            if (s->raw_loaded < folder->raw_size) break; /* perhaps only not loaded yet */
            return false;
        }
        if (plain > CAB_FRAME || (s->ready + 1U < j->block_count && plain != CAB_FRAME) || plain > j->output_size - s->plain_ready) return false;
        j->blocks[s->ready] = data;
        j->packed_sizes[s->ready] = packed;
        j->plain_sizes[s->ready] = plain;
        s->plain_ready += plain;
        s->parse_off = off;
        s->ready++;
    }
    if (s->ready == j->block_count && s->plain_ready != j->output_size) return false;
    /* Touch points stay a page above the blocks published so far (the decoder's copies overrun a block by at most
     * 32 bytes), except that the last step finishes them all. */
    limit = j->output_size - s->plain_ready > CAB_ASYNC_PAGE ? s->plain_ready + CAB_ASYNC_PAGE : j->output_size;
    if (s->ready == j->block_count) limit = j->output_size;
    while (s->touched < limit) {
        j->output[s->touched] = 0;
        s->touched += CAB_ASYNC_PAGE;
    }
    return true;
}

/* Decodes an LZX folder on a worker thread.  Returns 1 with the folder in *out, 0 when the folder cannot be decoded
 * (or the caller asked to stop), -1 when nothing was started and the caller should decode on its own thread.  When
 * `sink` is not NULL, the bytes of its record are written as they become final; the caller writes the rest. */
static int cab_decode_lzx_async(Abstractformat *f, const cab_context *c, const cab_folder *folder, xx_pd_struct *p, cab_budget *budget, cab_sink *sink, uint8_t **out,
                                size_t *out_size)
{
    const int64_t start = f->base_address + (int64_t)folder->data_offset;
    const size_t total = folder->plain_total;
    cab_job j;
    cab_feed feed;
    xx_thread_t *thread = NULL;
    size_t sink_end = 0, translated = 0; /* translated: output below it has had its E8 translation, in whole frames */
    unsigned spins = 0;
    int result = 0;
    bool failed = false;
    const bool track_budget = budget && budget->limits_active;
    uint64_t sync_charge = 0, blocks_charge = 0, packed_charge = 0, plain_charge = 0, raw_charge = 0, output_charge = 0, codec_charge = 0;
    if (!folder->lzx_valid) return 0;
    if (total < CAB_ASYNC_MIN_OUTPUT || folder->raw_size == 0) return -1;
    xx_mem_zero(&j, sizeof(j));
    xx_mem_zero(&feed, sizeof(feed));
    if (track_budget) {
        uint64_t b_sz = 0, pk_sz = 0, pl_sz = 0;
        /* As cab_load_raw. */
        if (start < 0 || start > LONG_MAX || (int64_t)folder->last_payload > LONG_MAX - start) return 0;
        if (!cab_mul_u64(folder->blocks, sizeof(*j.blocks), &b_sz) || b_sz > SIZE_MAX ||
            !cab_mul_u64(folder->blocks, sizeof(*j.packed_sizes), &pk_sz) || pk_sz > SIZE_MAX ||
            !cab_mul_u64(folder->blocks, sizeof(*j.plain_sizes), &pl_sz) || pl_sz > SIZE_MAX) {
            return 0;
        }
        if (!cab_budget_reserve(budget, CAB_LZX_ASYNC_SYNC_BYTES, p)) return 0;
        sync_charge = CAB_LZX_ASYNC_SYNC_BYTES;
        j.mutex = xx_mutex_create();
        j.owner_cond = xx_cond_create();
        j.decoder_cond = xx_cond_create();
        if (!j.mutex || !j.owner_cond || !j.decoder_cond) {
            result = -1; /* e.g. no condition variables before Windows Vista */
            goto cleanup;
        }
        if (!cab_budget_reserve(budget, b_sz, p)) goto cleanup;
        blocks_charge = b_sz;
        if (!cab_budget_reserve(budget, pk_sz, p)) goto cleanup;
        packed_charge = pk_sz;
        if (!cab_budget_reserve(budget, pl_sz, p)) goto cleanup;
        plain_charge = pl_sz;
        if (!cab_budget_reserve(budget, folder->raw_size, p)) goto cleanup;
        raw_charge = folder->raw_size;
        if (!cab_budget_reserve(budget, total, p)) goto cleanup;
        output_charge = total;
        if (!cab_budget_reserve(budget, CAB_LZX_DECODER_STATE_BYTES, p)) goto cleanup;
        codec_charge = CAB_LZX_DECODER_STATE_BYTES;
    } else {
        j.mutex = xx_mutex_create();
        j.owner_cond = xx_cond_create();
        j.decoder_cond = xx_cond_create();
        if (!j.mutex || !j.owner_cond || !j.decoder_cond) {
            result = -1; /* e.g. no condition variables before Windows Vista */
            goto cleanup;
        }
        /* As cab_load_raw. */
        if (start < 0 || start > LONG_MAX || (int64_t)folder->last_payload > LONG_MAX - start) goto cleanup;
    }
    j.block_count = folder->blocks;
    j.window_bits = (folder->type >> 8U) & 31U;
    j.output_size = total;
    j.blocks = (const uint8_t **)xx_mem_calloc(folder->blocks, sizeof(*j.blocks));
    j.packed_sizes = (size_t *)xx_mem_calloc(folder->blocks, sizeof(*j.packed_sizes));
    j.plain_sizes = (size_t *)xx_mem_calloc(folder->blocks, sizeof(*j.plain_sizes));
    feed.raw = (uint8_t *)xx_mem_alloc(folder->raw_size);
    if (!j.blocks || !j.packed_sizes || !j.plain_sizes || !feed.raw) goto cleanup;
    j.output = (uint8_t *)xx_mem_alloc(total);
    if (!j.output) goto cleanup;
    /* The first slice before the thread exists: an unreadable or foreign data area costs no thread. */
    if (!cab_feed_step(f, c, folder, &j, &feed)) goto cleanup;
    j.ready_blocks = feed.ready;
    thread = xx_thread_create(cab_job_thread, &j);
    if (!thread) {
        /* Decode on this thread instead; the caller reads the data area again. */
        result = -1;
        goto cleanup;
    }
    if (sink && sink->path && sink->offset <= total && sink->size <= total - sink->offset) sink_end = sink->offset + sink->size;
    else sink = NULL;

    for (;;) {
        size_t settled, decoded, final_bytes, position = 0, target = 0;
        int32_t translation;
        bool done, need_feed, can_translate, can_write = false, timed_out = false;
        xx_mutex_lock(j.mutex);
        done = j.done;
        decoded = j.decoded_blocks;
        settled = j.settled_bytes & ~(size_t)(CAB_FRAME - 1U); /* whole frames */
        translation = j.translation;                            /* 0 while the decoder is in block 0, as is settled */
        need_feed = feed.ready < j.block_count && feed.ready - decoded < CAB_ASYNC_LOOKAHEAD;
        can_translate = translation && settled - translated >= CAB_ASYNC_WRITE_MIN;
        final_bytes = translation ? translated : settled;
        if (sink && !sink->failed) {
            position = sink->offset + sink->done;
            target = sink_end - position > CAB_ASYNC_WRITE_MIN ? position + CAB_ASYNC_WRITE_MIN : sink_end;
            can_write = position < sink_end && final_bytes >= target;
        }
        if (!done && !need_feed && !can_translate && !can_write) {
            /* need_feed is false: either every block is ready or at least CAB_ASYNC_LOOKAHEAD of them are */
            j.wake_blocks = feed.ready < j.block_count ? feed.ready - CAB_ASYNC_LOOKAHEAD + 1U : SIZE_MAX;
            if (translation) j.wake_settled = translated + CAB_ASYNC_WRITE_MIN;
            else j.wake_settled = sink && !sink->failed && position < sink_end ? target : SIZE_MAX;
            j.owner_waiting = true;
            timed_out = xx_cond_wait(j.owner_cond, j.mutex, CAB_ASYNC_POLL_MS) != XX_WAIT_OK;
            j.owner_waiting = false;
            done = j.done;
        }
        xx_mutex_unlock(j.mutex);
        if (done) break;
        if (p && (timed_out || (++spins & 15U) == 0) && xx_pd_is_stopped(p)) {
            failed = true;
            break;
        }
        if (need_feed) {
            if (!cab_feed_step(f, c, folder, &j, &feed)) {
                failed = true;
                break;
            }
            xx_mutex_lock(j.mutex);
            j.ready_blocks = feed.ready;
            if (j.decoder_waiting) xx_cond_signal(j.decoder_cond);
            xx_mutex_unlock(j.mutex);
        } else if (can_translate) {
            size_t to = settled - translated > CAB_ASYNC_WRITE_MAX ? translated + CAB_ASYNC_WRITE_MAX : settled;
            xx_lzx_cab_translate(j.output, translated, to, translation);
            translated = to;
        } else if (can_write) {
            size_t n = (final_bytes < sink_end ? final_bytes : sink_end) - position;
            if (n > CAB_ASYNC_WRITE_MAX) n = CAB_ASYNC_WRITE_MAX;
            (void)cab_sink_write(sink, j.output, n); /* a failed sink is not written again; the folder is still decoded */
        }
    }
    xx_mutex_lock(j.mutex);
    if (!j.done) {
        j.cancel = true;
        xx_cond_signal(j.decoder_cond);
    }
    xx_mutex_unlock(j.mutex);
    xx_thread_join(thread, NULL);
    if (j.ok && !failed) {
        /* The worker is gone: the rest of the output is settled. */
        xx_lzx_cab_translate(j.output, translated, total, j.translation);
        *out = j.output;
        *out_size = total;
        j.output = NULL;
        output_charge = 0;
        result = 1;
    }
cleanup:
    if (j.output) xx_mem_free(j.output);
    if (feed.raw) xx_mem_free(feed.raw);
    if (j.blocks) xx_mem_free((void *)j.blocks);
    if (j.packed_sizes) xx_mem_free(j.packed_sizes);
    if (j.plain_sizes) xx_mem_free(j.plain_sizes);
    if (j.decoder_cond) xx_cond_destroy(j.decoder_cond);
    if (j.owner_cond) xx_cond_destroy(j.owner_cond);
    if (j.mutex) xx_mutex_destroy(j.mutex);
    if (track_budget) {
        cab_budget_release(budget, codec_charge);
        cab_budget_release(budget, output_charge);
        cab_budget_release(budget, raw_charge);
        cab_budget_release(budget, blocks_charge);
        cab_budget_release(budget, packed_charge);
        cab_budget_release(budget, plain_charge);
        cab_budget_release(budget, sync_charge);
    }
    return result;
}

static bool cab_decode_folder_advanced(Abstractformat *f, const cab_context *c, const cab_folder *folder, uint16_t method, xx_pd_struct *p, cab_budget *budget,
                                       cab_sink *sink, uint8_t **out, size_t *out_size)
{
    const uint8_t **blocks = NULL;
    uint8_t *raw = NULL, *result = NULL;
    size_t *packed_sizes = NULL, *plain_sizes = NULL, total = 0, wrote = 0, off = 0, i, raw_charge = 0;
    uint64_t blocks_charge = 0, packed_charge = 0, plain_charge = 0, result_charge = 0, codec_charge = 0;
    bool ok = false;
    unsigned window_bits = (folder->type >> 8U) & 31U;
    const bool track_budget = budget && budget->limits_active;
    if (method == CAB_METHOD_LZX) {
        int r = cab_decode_lzx_async(f, c, folder, p, budget, sink, out, out_size);
        if (r >= 0) return r > 0;
    }
    if (track_budget) {
        uint64_t b_sz = 0, pk_sz = 0, pl_sz = 0;
        if (!cab_mul_u64(folder->blocks, sizeof(*blocks), &b_sz) || b_sz > SIZE_MAX ||
            !cab_mul_u64(folder->blocks, sizeof(*packed_sizes), &pk_sz) || pk_sz > SIZE_MAX ||
            !cab_mul_u64(folder->blocks, sizeof(*plain_sizes), &pl_sz) || pl_sz > SIZE_MAX) {
            goto done;
        }
        if (!cab_budget_reserve(budget, b_sz, p)) goto done;
        blocks_charge = b_sz;
        if (!cab_budget_reserve(budget, pk_sz, p)) goto done;
        packed_charge = pk_sz;
        if (!cab_budget_reserve(budget, pl_sz, p)) goto done;
        plain_charge = pl_sz;
    }
    blocks = (const uint8_t **)xx_mem_calloc(folder->blocks, sizeof(*blocks));
    packed_sizes = (size_t *)xx_mem_calloc(folder->blocks, sizeof(*packed_sizes));
    plain_sizes = (size_t *)xx_mem_calloc(folder->blocks, sizeof(*plain_sizes));
    if (!blocks || !packed_sizes || !plain_sizes || !cab_load_raw(f, folder, budget, p, &raw, &raw_charge)) goto done;
    for (i = 0; i < folder->blocks; i++) {
        uint16_t packed, plain;
        if (!cab_next_block(c, raw, folder->raw_size, &off, &blocks[i], &packed, &plain) || plain > SIZE_MAX - total) goto done;
        packed_sizes[i] = packed;
        plain_sizes[i] = plain;
        total += plain;
    }
    if (track_budget) {
        size_t result_alloc = total ? total : 1U;
        uint64_t codec_bytes = method == CAB_METHOD_LZX ? (uint64_t)CAB_LZX_DECODER_STATE_BYTES : (uint64_t)CAB_QUANTUM_DECODER_STATE_BYTES;
        if (!cab_budget_reserve(budget, result_alloc, p)) goto done;
        result_charge = result_alloc;
        if (codec_bytes && !cab_budget_reserve(budget, codec_bytes, p)) goto done;
        codec_charge = codec_bytes;
        result = (uint8_t *)xx_mem_alloc(result_alloc);
    } else {
        result = (uint8_t *)xx_mem_alloc(total);
    }
    if (!result) goto done;
    if (method == CAB_METHOD_LZX) ok = xx_lzx_cab_decode(blocks, packed_sizes, plain_sizes, folder->blocks, window_bits, result, total, &wrote);
    else ok = xx_quantum_cab_decode(blocks, packed_sizes, plain_sizes, folder->blocks, window_bits, result, total, &wrote);
    if (!ok || wrote != total) {
        ok = false;
        goto done;
    }
    *out = result;
    *out_size = total;
    result = NULL;
    result_charge = 0;
done:
    if (result) xx_mem_free(result);
    if (raw) xx_mem_free(raw);
    if (blocks) xx_mem_free((void *)blocks);
    if (packed_sizes) xx_mem_free(packed_sizes);
    if (plain_sizes) xx_mem_free(plain_sizes);
    if (track_budget) {
        cab_budget_release(budget, codec_charge);
        cab_budget_release(budget, result_charge);
        cab_budget_release(budget, raw_charge);
        cab_budget_release(budget, blocks_charge);
        cab_budget_release(budget, packed_charge);
        cab_budget_release(budget, plain_charge);
    }
    return ok;
}

/* Decodes folder fi.  `p`, `budget` and `sink` may be NULL; see cab_decode_lzx_async. */
static bool cab_decode_folder(Abstractformat *f, const cab_context *c, uint16_t fi, xx_pd_struct *p, cab_budget *budget, cab_sink *sink, uint8_t **out,
                              size_t *out_size)
{
    const cab_folder *folder = &c->folders[fi];
    uint16_t method = folder->type & 15U, j;
    size_t cap = 0, used = 0, off = 0, raw_charge = 0;
    uint8_t *raw = NULL, *result = NULL;
    const bool track_budget = budget && budget->limits_active;
    if (method == CAB_METHOD_LZX || method == CAB_METHOD_QUANTUM) return cab_decode_folder_advanced(f, c, folder, method, p, budget, sink, out, out_size);
    if (method != CAB_METHOD_STORE && method != CAB_METHOD_MSZIP) return false;
    if (!cab_load_raw(f, folder, budget, p, &raw, &raw_charge)) return false;
    for (j = 0; j < folder->blocks; j++) {
        const uint8_t *in;
        uint16_t packed, plain;
        size_t wrote = 0;
        if (!cab_next_block(c, raw, folder->raw_size, &off, &in, &packed, &plain) || used > SIZE_MAX - plain) goto fail;
        if (used + plain > cap) {
            /* Grow geometrically (the old code grew by one block, copying
             * the whole output each time), but only as far as blocks have
             * decoded: a small corrupt cabinet that declares gigabytes of
             * output fails before allocating them. */
            size_t want = cap > SIZE_MAX / 2U ? SIZE_MAX : cap * 2U;
            uint8_t *grown;
            if (want < 65536U) want = 65536U;
            if (want > folder->plain_total) want = folder->plain_total;
            if (want < used + plain) want = used + plain;
            if (track_budget) {
                uint64_t avail = cab_budget_available(budget);
                if ((uint64_t)want > avail) want = (size_t)avail;
                if (want < used + plain) {
                    xx_pd_set_error(p, XXFC_ERR_OUT_OF_BOUNDS, "CAB extraction exceeds the configured memory limit");
                    goto fail;
                }
                if (!cab_budget_reserve(budget, want, p)) goto fail;
            }
            grown = (uint8_t *)xx_mem_realloc(result, want);
            if (!grown) {
                if (track_budget) cab_budget_release(budget, want);
                goto fail;
            }
            if (track_budget) cab_budget_release(budget, cap);
            result = grown;
            cap = want;
        }
        if (method == CAB_METHOD_STORE) {
            if (packed != plain) goto fail;
            xx_mem_copy(result + used, in, plain);
            wrote = plain;
        } else {
            bool ok;
            if (packed < 2 || in[0] != 'C' || in[1] != 'K') goto fail;
            if (track_budget && !cab_budget_reserve(budget, CAB_MSZIP_DECODER_STATE_BYTES, p)) goto fail;
            ok = xx_deflate_decompress_memory_with_dictionary(in + 2, packed - 2, result + used, plain, &wrote,
                                                              result + used - (used > 32768U ? 32768U : used), used > 32768U ? 32768U : used, false);
            if (track_budget) cab_budget_release(budget, CAB_MSZIP_DECODER_STATE_BYTES);
            if (!ok || wrote != plain) goto fail;
        }
        used += plain;
    }
    xx_mem_free(raw);
    if (track_budget) cab_budget_release(budget, raw_charge);
    *out = result;
    *out_size = used;
    return true;
fail:
    if (result) {
        xx_mem_free(result);
        if (track_budget) cab_budget_release(budget, cap);
    }
    xx_mem_free(raw);
    if (track_budget) cab_budget_release(budget, raw_charge);
    return false;
}

void xx_cab_init(xx_cab *a, xx_io_device *d, int64_t b)
{
    if (!a) return;
    xx_mem_zero(a, sizeof(*a));
    xx_format_init(&a->format, d, b);
    a->format.file_type = XX_FILE_TYPE_CAB;
    a->format.format_type = XX_TYPE_ARCHIVE;
    a->format.is_archive = true;
    xx_format_set_extension(&a->format, "cab");
    a->format.check_is_valid = xx_cab_check_is_valid;
    a->format.handle_base_info = xx_cab_handle_base_info;
    a->format.get_format_size = xx_cab_get_format_size;
    a->format.get_number_of_archive_records = xx_cab_get_number_of_archive_records;
    a->format.create_archive_records_reading = xx_cab_create_archive_records_reading;
    a->format.get_current_archive_record = xx_cab_get_current_archive_record;
    a->format.unpack_current_archive_record = xx_cab_unpack_current_archive_record;
    a->format.archive_record_move_to_next = xx_cab_archive_record_move_to_next;
    a->format.free_archive_records_reading = xx_cab_free_archive_records_reading;
    a->archive_end = -1;
}
xx_cab *xx_cab_create(xx_io_device *d, int64_t b)
{
    xx_cab *a = (xx_cab *)xx_mem_alloc(sizeof(*a));
    if (a) xx_cab_init(a, d, b);
    return a;
}
void xx_cab_destroy(xx_cab *a)
{
    if (a) xx_format_cleanup_extra_parameters(&a->format);
}
void xx_cab_free(xx_cab *a)
{
    if (a) {
        xx_cab_destroy(a);
        xx_mem_free(a);
    }
}
bool xx_cab_check_is_valid(Abstractformat *f, xx_pd_struct *p)
{
    cab_context *c;
    (void)p;
    if (!cab_parse(f, &c)) return false;
    cab_free(c);
    return true;
}
bool xx_cab_handle_base_info(Abstractformat *f, xx_pd_struct *p)
{
    cab_context *c;
    xx_cab *a;
    (void)p;
    if (!f || !cab_parse(f, &c)) return false;
    a = (xx_cab *)f;
    a->number_of_records = c->file_count;
    a->archive_end = f->base_address + c->cabinet_size;
    f->number_of_archive_records = c->file_count;
    f->format_size = c->cabinet_size;
    f->is_valid = true;
    f->base_info_handled = true;
    cab_free(c);
    return true;
}
int64_t xx_cab_get_format_size(Abstractformat *f, xx_pd_struct *p)
{
    return f && (f->base_info_handled || xx_cab_handle_base_info(f, p)) ? f->format_size : -1;
}
uint64_t xx_cab_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *p)
{
    return f && (f->base_info_handled || xx_cab_handle_base_info(f, p)) ? ((xx_cab *)f)->number_of_records : 0;
}
xx_archive_record_state *xx_cab_create_archive_records_reading(Abstractformat *f, const xx_list_s *o, xx_pd_struct *p)
{
    cab_context *c;
    xx_archive_record_state *s;
    cab_limits limits;
    if (!f || !cab_resolve_limits(f, o, &limits, p)) return NULL;
    if (!cab_parse(f, &c)) return NULL;
    s = (xx_archive_record_state *)xx_mem_alloc(sizeof(*s));
    if (!s) {
        cab_free(c);
        return NULL;
    }
    xx_archive_record_state_init(s, f);
    s->internal_state = c;
    s->free_internal = cab_free;
    s->total_records = c->file_count;
    if (!cab_opts(&s->options, o) || (c->file_count && !cab_record(&s->current_record, c, 0))) {
        xx_archive_record_state_free(s);
        return NULL;
    }
    s->has_record = c->file_count != 0;
    return s;
}
const xx_archive_record *xx_cab_get_current_archive_record(Abstractformat *f, xx_archive_record_state *s)
{
    return f && s && s->format == f && s->has_record ? &s->current_record : NULL;
}
bool xx_cab_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *p)
{
    cab_context *c;
    (void)p;
    if (!f || !s || s->format != f || !(c = (cab_context *)s->internal_state) || ++c->index >= c->file_count) {
        if (s) s->has_record = false;
        return false;
    }
    s->current_index++;
    s->has_record = cab_record(&s->current_record, c, c->index);
    return s->has_record;
}
bool xx_cab_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *p)
{
    cab_context *c;
    cab_file *x;
    const xx_var *o;
    uint8_t *folder = NULL;
    size_t folder_size = 0;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *relative = NULL;
    cab_sink sink;
    cab_limits limits;
    cab_budget budget;
    cab_budget *decode_budget = NULL;
    bool ok = false;
    xx_mem_zero(&sink, sizeof(sink));
    if (!f || !s || s->format != f || !s->has_record || !(c = (cab_context *)s->internal_state) || c->index >= c->file_count || (p && xx_pd_is_stopped(p))) return false;
    x = &c->files[c->index];
    if (!cab_resolve_limits(f, &s->options, &limits, p)) return false;
    if (limits.limits_active && !cab_resource_limits_allow(&limits, c, x, &budget, &decode_budget, p)) return false;
    relative = xx_archive_output_name(x->name, 0, XX_ARCHIVE_NAME_ROOTED);
    XX_ARCHIVE_OUTPUT_RESOLVE(relative, c->files, c->file_count, c->index, name, 0, XX_ARCHIVE_NAME_ROOTED);
    if (!relative || !cab_safe(relative)) goto done;
    /* The output path is worked out before the folder is decoded, so that an LZX folder can be written while it
     * decodes.  Nothing is created here, and a missing path fails the record only after the decode, as before. */
    o = cab_opt(&s->options, XX_META_ID_OPT_UNPACK_PATH);
    if (o) {
        if (o->type == XX_VAR_TYPE_STRING || o->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(o);
        else if (o->type == XX_VAR_TYPE_WSTRING || o->type == XX_VAR_TYPE_WSTRING_VIEW) {
            owned = xx_str_unicode_to_utf8(xx_var_get_wstr(o));
            base = owned;
        }
        if (base)
            path = (base[0] && base[xx_str_len(base) - 1] != '/' && base[xx_str_len(base) - 1] != '\\') ? xx_str_concat3(base, "/", relative) : xx_str_concat(base, relative);
    }
    sink.path = path;
    sink.stage_seq = &c->stage_seq;
    sink.offset = x->folder_offset;
    sink.size = x->size;
    if (c->decoded_folder && c->decoded_index != x->folder) {
        xx_mem_free(c->decoded_folder);
        c->decoded_folder = NULL;
        c->decoded_size = 0;
    }
    if (!c->decoded_folder) {
        if (!cab_decode_folder(f, c, x->folder, p, decode_budget, path ? &sink : NULL, &c->decoded_folder, &c->decoded_size)) goto done;
        c->decoded_index = x->folder;
    }
    folder = c->decoded_folder;
    folder_size = c->decoded_size;
    if (x->folder_offset > folder_size || x->size > folder_size - x->folder_offset) goto done;
    if (!o) {
        ok = true;
        goto done;
    }
    if (!base || !path) goto done;
    /* Whatever was not written during the decode (everything, for a cached folder); creates the stage file if needed. */
    ok = cab_sink_write(&sink, folder, sink.size - sink.done);
    if (sink.device) {
        if (xx_io_close(sink.device) != 0) ok = false;
        sink.device = NULL;
    }
    if (ok && p && xx_pd_is_stopped(p)) ok = false;
    if (ok && sink.stage_path) ok = xx_io_file_replace_a(sink.stage_path, path, true);
done:
    if (!ok) cab_sink_discard(&sink);
    else if (sink.stage_path) {
        xx_mem_free(sink.stage_path);
        sink.stage_path = NULL;
    }
    if (path) xx_str_free(path);
    if (relative) xx_mem_free(relative);
    if (owned) xx_str_free(owned);
    return ok;
}
void xx_cab_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *s)
{
    (void)f;
    xx_archive_record_state_free(s);
}

bool xx_cab_unpack_file_to_device(xx_io_device *device, int64_t base_address, size_t index, xx_io_device *out,
                                  char *name, size_t name_size)
{
    xx_cab cab;
    cab_context *c = NULL;
    uint8_t *folder = NULL;
    size_t folder_size = 0;
    const cab_file *x;
    size_t at = 0;
    bool ok = false;

    if (name && name_size) name[0] = 0;
    if (!device || !out) return false;
    xx_cab_init(&cab, device, base_address);
    if (!cab_parse(&cab.format, &c)) goto done;
    if (index >= c->file_count) goto done;
    x = &c->files[index];
    if (name && name_size && x->name) {
        size_t n = xx_str_len(x->name);
        if (n >= name_size) n = name_size - 1;
        xx_mem_copy(name, x->name, n);
        name[n] = 0;
    }
    /* No sink: the caller's device gets no bytes unless the whole folder decodes, as before. */
    if (!cab_decode_folder(&cab.format, c, x->folder, NULL, NULL, NULL, &folder, &folder_size)) goto done;
    if (x->folder_offset > folder_size || x->size > folder_size - x->folder_offset) goto done;
    ok = true;
    while (at < x->size) {
        ssize_t n = xx_io_write(out, folder + x->folder_offset + at, x->size - at);
        if (n <= 0 || (size_t)n > x->size - at) {
            ok = false;
            break;
        }
        at += (size_t)n;
    }
done:
    if (folder) xx_mem_free(folder);
    if (c) cab_free(c);
    xx_cab_destroy(&cab);
    return ok;
}
