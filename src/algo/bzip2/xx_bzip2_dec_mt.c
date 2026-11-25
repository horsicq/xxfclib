/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Multi-threaded bzip2 block decoding.
 *
 * A bzip2 stream has no block index, but every block starts with the 48-bit
 * magic 0x314159265359 at an arbitrary bit position. The calling thread
 * reads the input in overlapping segments, lists every bit position where
 * the magic occurs (candidates) and hands them to worker threads, which
 * decode each candidate on its own into memory. The calling thread then
 * emits the blocks in stream order: a decoded candidate is used only when
 * it starts exactly where the previous block ended, so a magic that occurs
 * by chance inside block data is never used. Whenever the next block is not
 * available this way (end of stream, damaged data, a block larger than the
 * segment overlap) the reader is positioned there and the sequential decoder
 * continues, so results and errors are those of the sequential decoder.
 *
 * Only the calling thread touches the source and destination devices.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_bzip2_internal.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/threads/xx_threads.h"
#include "../../io/xx_io_buffer.h"

#if XX_BZIP2_MT

extern uint32_t xx_crc32_mpeg2_raw(uint32_t reg, const void *data, size_t size);

#define BZ2_MT_MAX_THREADS 16
/* New input bytes per segment, and the bytes of the next segment repeated
 * at its end: a block that starts in a segment is decoded from that segment
 * alone, so it has to end within the overlap (blocks of bzip2 compressors
 * stay below 1.2 MiB). */
#define BZ2_MT_SEG_PRIMARY ((size_t)4 << 20)
#define BZ2_MT_SEG_OVERLAP ((size_t)2 << 20)
#define BZ2_MT_SEG_PAD 16
#define BZ2_MT_MAGIC 0x314159265359ULL

typedef struct bz2_mt_seg {
    struct bz2_mt_seg *next;
    uint8_t *data;
    size_t len;       /* valid bytes in data */
    size_t prim;      /* blocks starting below this offset belong to this segment */
    uint64_t base;    /* stream offset of data[0] */
    uint32_t *cand;   /* bit offsets of the magics in data, ascending */
    size_t ncand;
    size_t cand_cap;
    size_t next_cand; /* first candidate not yet made a task */
    int running;      /* workers decoding from this segment */
    bool owned;
} bz2_mt_seg;

enum { BZ2_MT_QUEUED = 0, BZ2_MT_RUNNING = 1, BZ2_MT_DONE = 2 };

typedef struct bz2_mt_task {
    struct bz2_mt_task *next;
    bz2_mt_seg *seg;
    uint64_t start_bit; /* stream bit position of the block magic */
    uint64_t end_bit;   /* the bit after the block */
    int state;
    bool abandoned; /* no longer wanted: the worker frees it */
    bool ok;        /* the block decoded */
    bool big;       /* output larger than a pooled buffer: mid holds the block before the final run-length stage */
    uint32_t stored_crc;
    uint32_t calc_crc;
    uint8_t *out;
    size_t out_len;
    uint8_t *mid;
    uint32_t n;
} bz2_mt_task;

typedef struct bz2_mt bz2_mt;

typedef struct {
    bz2_mt *mt;
    bz2_block_work work;
    xx_thread_t *thread;
} bz2_mt_worker;

struct bz2_mt {
    xx_mutex_t *lock;
    xx_cond_t *work_cond;
    xx_cond_t *done_cond;
    bz2_mt_task *head; /* tasks in stream order, not yet consumed */
    bz2_mt_task *tail;
    bz2_mt_task *next_run; /* first queued task */
    int live;              /* tasks in the list */
    bool quit;
    int block_bytes;
    size_t out_cap;
    uint8_t *pool[2 * BZ2_MT_MAX_THREADS + 4];
    int pool_count;
    int threads;
    bz2_mt_worker *workers;
    /* input, calling thread only */
    bz2_bit_reader *br;
    bz2_mt_seg *first; /* oldest segment kept */
    bz2_mt_seg *cur;   /* newest segment: the one candidates are taken from */
    bool input_done;   /* the source has no more bytes */
    bool stalled;      /* an allocation failed: no further segments or tasks */
    bool seeded;       /* the reader's buffered bytes were moved into a segment */
    uint8_t *big_buf;  /* output of a block larger than a pooled buffer */
    size_t big_cap;
};

/* ---- thread count ----------------------------------------------------- */

static xx_once_t bz2_mt_env_once = XX_ONCE_INIT;
static int bz2_mt_env_limit = -1; /* written once, under bz2_mt_env_once */

static void bz2_mt_read_env(void *arg)
{
    const char *value = xx_rt_getenv("XX_BZIP2_MT");
    (void)arg;
    if (value && value[0] >= '0' && value[0] <= '9') {
        int limit = 0;
        for (int i = 0; i < 4 && value[i] >= '0' && value[i] <= '9'; i++) limit = limit * 10 + (value[i] - '0');
        bz2_mt_env_limit = limit;
    }
}

int bz2_mt_thread_count(void)
{
    uint32_t count = xx_thread_cpu_count();
    xx_once(&bz2_mt_env_once, bz2_mt_read_env, NULL);
    if (count > BZ2_MT_MAX_THREADS) count = BZ2_MT_MAX_THREADS;
#if SIZE_MAX <= 0xFFFFFFFFu
    /* A 32-bit address space: fewer blocks in memory at once. */
    if (count > 4) count = 4;
#endif
    if (bz2_mt_env_limit >= 0 && (uint32_t)bz2_mt_env_limit < count) count = (uint32_t)bz2_mt_env_limit;
    return count < 2 ? 0 : (int)count;
}

/* ---- output buffers --------------------------------------------------- */

/* lock held */
static void bz2_mt_pool_put(bz2_mt *mt, uint8_t *buf)
{
    if (!buf) return;
    if (mt->pool_count < (int)(sizeof(mt->pool) / sizeof(mt->pool[0]))) mt->pool[mt->pool_count++] = buf;
    else xx_mem_free(buf);
}

/* lock held */
static void bz2_mt_task_free(bz2_mt *mt, bz2_mt_task *t)
{
    bz2_mt_pool_put(mt, t->out);
    if (t->mid) xx_mem_free(t->mid);
    xx_mem_free(t);
}

/* ---- workers ---------------------------------------------------------- */

static void bz2_mt_decode(bz2_mt *mt, bz2_block_work *w, bz2_mt_task *t)
{
    bz2_block_info info;
    bz2_rle1 rle;
    uint64_t end_bit = 0;
    uint32_t crc = 0xFFFFFFFFU;
    uint8_t *out;
    size_t len;
    const bz2_mt_seg *seg = t->seg;

    t->ok = false;
    if (!bz2_block_read_mem(seg->data, seg->len, t->start_bit - seg->base * 8u, w, mt->block_bytes, &info, &end_bit)) return;
    bz2_block_ibwt(w, &info);

    xx_mutex_lock(mt->lock);
    out = mt->pool_count > 0 ? mt->pool[--mt->pool_count] : NULL;
    xx_mutex_unlock(mt->lock);
    if (!out) out = (uint8_t *)xx_mem_alloc(mt->out_cap);
    if (!out) return;

    bz2_rle1_init(&rle);
    len = bz2_rle1_expand(&rle, w->mid, info.n, out, mt->out_cap);
    crc = xx_crc32_mpeg2_raw(crc, out, len);
    t->out = out;
    t->out_len = len;
    if (!bz2_rle1_done(&rle, info.n)) {
        /* Long runs: keep the block before the run-length stage and only
         * measure and check the output here; the writer expands it again. */
        uint8_t *copy = (uint8_t *)xx_mem_alloc((size_t)info.n + BZ2_WORK_PAD);
        if (!copy) return;
        xx_rt_memcpy(copy, w->mid, (size_t)info.n);
        xx_rt_memset(copy + info.n, 0, BZ2_WORK_PAD);
        do {
            len = bz2_rle1_expand(&rle, w->mid, info.n, out, mt->out_cap);
            crc = xx_crc32_mpeg2_raw(crc, out, len);
        } while (!bz2_rle1_done(&rle, info.n));
        t->mid = copy;
        t->n = info.n;
        t->out_len = 0;
        t->big = true;
    }
    t->stored_crc = info.stored_crc;
    t->calc_crc = crc ^ 0xFFFFFFFFU;
    t->end_bit = seg->base * 8u + end_bit;
    t->ok = true;
}

static int bz2_mt_worker_main(void *arg)
{
    bz2_mt_worker *self = (bz2_mt_worker *)arg;
    bz2_mt *mt = self->mt;
    xx_mutex_lock(mt->lock);
    for (;;) {
        bz2_mt_task *t;
        while (!mt->quit && !mt->next_run) (void)xx_cond_wait(mt->work_cond, mt->lock, XX_WAIT_INFINITE);
        if (mt->quit) break;
        t = mt->next_run;
        mt->next_run = t->next;
        t->state = BZ2_MT_RUNNING;
        t->seg->running++;
        xx_mutex_unlock(mt->lock);

        bz2_mt_decode(mt, &self->work, t);

        xx_mutex_lock(mt->lock);
        t->seg->running--;
        t->state = BZ2_MT_DONE;
        if (t->abandoned) bz2_mt_task_free(mt, t);
        xx_cond_broadcast(mt->done_cond);
    }
    xx_mutex_unlock(mt->lock);
    return 0;
}

/* ---- input segments (calling thread) ---------------------------------- */

static bool bz2_mt_add_candidate(bz2_mt_seg *seg, uint32_t bit)
{
    if (seg->ncand == seg->cand_cap) {
        size_t cap = seg->cand_cap ? seg->cand_cap * 2 : 64;
        uint32_t *grown = (uint32_t *)xx_mem_alloc(cap * sizeof(uint32_t));
        if (!grown) return false;
        if (seg->ncand) xx_rt_memcpy(grown, seg->cand, seg->ncand * sizeof(uint32_t));
        if (seg->cand) xx_mem_free(seg->cand);
        seg->cand = grown;
        seg->cand_cap = cap;
    }
    seg->cand[seg->ncand++] = bit;
    return true;
}

/* Lists the magics that start in the first seg->prim bytes, at or after
 * min_bit. data is readable for BZ2_MT_SEG_PAD bytes past len. */
static bool bz2_mt_scan(bz2_mt_seg *seg, uint64_t min_bit)
{
    const uint8_t *data = seg->data;
    const uint64_t end_bit = (uint64_t)seg->len * 8u;
    bool filter[256];
    for (int i = 0; i < 256; i++) filter[i] = false;
    /* The third byte of a magic that starts k bits into a byte. */
    for (int k = 0; k < 8; k++) filter[(BZ2_MT_MAGIC >> (24 + k)) & 0xFFu] = true;
    for (size_t i = 0; i < seg->prim; i++) {
        uint64_t window;
        if (!filter[data[i + 2]]) continue;
        window = xx_data_get_be64(data + i);
        for (unsigned k = 0; k < 8; k++) {
            if (((window >> (16u - k)) & 0xFFFFFFFFFFFFULL) == BZ2_MT_MAGIC) {
                uint64_t bit = (uint64_t)i * 8u + k;
                if (bit >= min_bit && bit + 48u <= end_bit && !bz2_mt_add_candidate(seg, (uint32_t)bit)) return false;
            }
        }
    }
    return true;
}

static void bz2_mt_seg_free(bz2_mt_seg *seg)
{
    if (seg->owned && seg->data) xx_mem_free(seg->data);
    if (seg->cand) xx_mem_free(seg->cand);
    xx_mem_free(seg);
}

/* Reads up to size bytes from the source into dst. */
static size_t bz2_mt_read(bz2_mt *mt, uint8_t *dst, size_t size)
{
    bz2_bit_reader *br = mt->br;
    size_t done = 0;
    while (done < size && !mt->input_done) {
        size_t want = size - done;
        ssize_t got;
        if (br->remaining >= 0) {
            if (br->remaining == 0) {
                mt->input_done = true;
                break;
            }
            if ((int64_t)want > br->remaining) want = (size_t)br->remaining;
        }
        got = xx_io_read(br->dev, dst + done, want);
        if (got <= 0) {
            mt->input_done = true;
            break;
        }
        if (br->remaining >= 0) br->remaining -= got;
        done += (size_t)got;
    }
    if (br->remaining == 0) mt->input_done = true;
    return done;
}

/* Appends the next segment. *start_bit is set for the first one: the
 * stream position of the reader. False: nothing appended. */
static bool bz2_mt_next_segment(bz2_mt *mt, uint64_t *start_bit)
{
    bz2_bit_reader *br = mt->br;
    bz2_mt_seg *prev = mt->cur;
    bz2_mt_seg *seg = (bz2_mt_seg *)xx_mem_alloc(sizeof(*seg));
    uint64_t min_bit = 0;
    if (!seg) return false;
    xx_rt_memset(seg, 0, sizeof(*seg));

    if (!br->dev) {
        /* Memory source: the segments are views of it. */
        size_t offset = prev ? (size_t)(prev->base + prev->prim) : 0;
        size_t left = br->ibuf_len - offset;
        seg->data = br->ibuf + offset;
        seg->base = offset;
        seg->len = left < BZ2_MT_SEG_PRIMARY + BZ2_MT_SEG_OVERLAP ? left : BZ2_MT_SEG_PRIMARY + BZ2_MT_SEG_OVERLAP;
        seg->prim = left <= BZ2_MT_SEG_PRIMARY + BZ2_MT_SEG_OVERLAP ? left : BZ2_MT_SEG_PRIMARY;
        if (seg->prim == left) mt->input_done = true;
        if (!prev) {
            min_bit = (uint64_t)br->ibuf_pos * 8u - (uint64_t)br->n_bits;
            *start_bit = min_bit;
        }
        /* The scan reads a few bytes past its last position: stop it short
         * of the end of the caller's memory. */
        if (seg->len < BZ2_MT_SEG_PAD) {
            seg->prim = 0;
        } else if (seg->prim > seg->len - BZ2_MT_SEG_PAD) {
            seg->prim = seg->len - BZ2_MT_SEG_PAD;
            mt->input_done = true;
        }
    } else {
        size_t carry = prev ? prev->len - prev->prim : 0;
        size_t seed_bits = prev ? 0 : (size_t)(br->n_bits > 0 ? br->n_bits : 0);
        size_t seed_bytes = (seed_bits + 7u) / 8u;
        size_t seed = prev ? 0 : seed_bytes + (br->ibuf_len - br->ibuf_pos);
        size_t cap = prev ? BZ2_MT_SEG_PRIMARY : BZ2_MT_SEG_PRIMARY + BZ2_MT_SEG_OVERLAP;
        size_t fill = 0;
        if (br->remaining >= 0 && (uint64_t)br->remaining < (uint64_t)cap) cap = (size_t)br->remaining;
        cap += carry + seed;
        seg->data = (uint8_t *)xx_mem_alloc(cap + BZ2_MT_SEG_PAD);
        if (!seg->data) {
            xx_mem_free(seg);
            return false;
        }
        seg->owned = true;
        if (prev) {
            xx_rt_memcpy(seg->data, prev->data + prev->prim, carry);
            fill = carry;
            seg->base = prev->base + prev->prim;
        } else {
            /* The bits and bytes the reader has buffered come first. */
            for (size_t i = 0; i < seed_bytes; i++) {
                size_t shift = (seed_bytes - 1u - i) * 8u;
                uint64_t mask = shift + 8u <= seed_bits ? 0xFFu : ((uint64_t)1 << (seed_bits - shift)) - 1u;
                seg->data[i] = (uint8_t)((br->bits >> shift) & mask);
            }
            xx_rt_memcpy(seg->data + seed_bytes, br->ibuf + br->ibuf_pos, br->ibuf_len - br->ibuf_pos);
            fill = seed;
            min_bit = (uint64_t)seed_bytes * 8u - seed_bits;
            *start_bit = min_bit;
            br->ibuf_pos = br->ibuf_len;
            br->bits = 0;
            br->n_bits = 0;
            mt->seeded = true;
        }
        fill += bz2_mt_read(mt, seg->data + fill, cap - fill);
        xx_rt_memset(seg->data + fill, 0, BZ2_MT_SEG_PAD);
        seg->len = fill;
        seg->prim = mt->input_done ? fill : (fill > BZ2_MT_SEG_OVERLAP ? fill - BZ2_MT_SEG_OVERLAP : 0);
    }

    if (!bz2_mt_scan(seg, min_bit)) {
        /* Keep the data (the reader needs it back), drop the candidates. */
        seg->ncand = 0;
        mt->stalled = true;
    }
    if (prev) prev->next = seg;
    else mt->first = seg;
    mt->cur = seg;
    return true;
}

/* The next candidate at or after bit position from, or false. */
static bool bz2_mt_next_candidate(bz2_mt *mt, uint64_t from, bz2_mt_seg **seg, uint64_t *bit)
{
    for (;;) {
        bz2_mt_seg *cur = mt->cur;
        uint64_t unused = 0;
        if (mt->stalled) return false;
        while (cur->next_cand < cur->ncand) {
            uint64_t pos = cur->base * 8u + cur->cand[cur->next_cand++];
            if (pos >= from) {
                *seg = cur;
                *bit = pos;
                return true;
            }
        }
        if (mt->input_done) return false;
        if (!bz2_mt_next_segment(mt, &unused)) {
            mt->stalled = true;
            return false;
        }
    }
}

/* Lock held. Drops the tasks that start before the stream bit position
 * pos (magics inside block data), then frees the segments wholly before
 * it: no task left in the list refers to those. */
static void bz2_mt_advance(bz2_mt *mt, uint64_t pos)
{
    while (mt->head && mt->head->start_bit < pos) {
        bz2_mt_task *t = mt->head;
        mt->head = t->next;
        if (!mt->head) mt->tail = NULL;
        if (mt->next_run == t) mt->next_run = t->next;
        mt->live--;
        if (t->state == BZ2_MT_RUNNING) t->abandoned = true;
        else bz2_mt_task_free(mt, t);
    }
    while (mt->first && mt->first != mt->cur && mt->first->running == 0 && (mt->first->base + mt->first->prim) * 8u <= pos) {
        bz2_mt_seg *seg = mt->first;
        mt->first = seg->next;
        bz2_mt_seg_free(seg);
    }
}

/* Positions the reader at stream bit position pos: the bytes read ahead
 * go back into its buffer. Workers have stopped. */
static bool bz2_mt_hand_back(bz2_mt *mt, uint64_t pos)
{
    bz2_bit_reader *br = mt->br;
    uint64_t byte = pos >> 3;
    if (!br->dev) {
        br->ibuf_pos = (size_t)byte;
        br->bits = 0;
        br->n_bits = 0;
    } else {
        bz2_mt_seg *seg;
        size_t total = 0;
        size_t done = 0;
        if (!mt->seeded) return true; /* the reader was never touched */
        for (seg = mt->first; seg; seg = seg->next) {
            uint64_t end = seg->base + (seg->next ? seg->prim : seg->len);
            if (end > byte) total += (size_t)(end - (seg->base > byte ? seg->base : byte));
        }
        if (total > br->ibuf_capacity || (!br->ibuf && total != 0)) {
            uint8_t *grown = (uint8_t *)xx_mem_alloc(total);
            if (!grown) {
                xx_io_note_alloc_failure();
                br->ibuf_pos = 0;
                br->ibuf_len = 0;
                br->error = true;
                return false;
            }
            if (br->ibuf) xx_mem_free(br->ibuf);
            br->ibuf = grown;
            br->ibuf_capacity = total;
        }
        for (seg = mt->first; seg; seg = seg->next) {
            uint64_t end = seg->base + (seg->next ? seg->prim : seg->len);
            if (end > byte) {
                uint64_t from = seg->base > byte ? seg->base : byte;
                xx_rt_memcpy(br->ibuf + done, seg->data + (size_t)(from - seg->base), (size_t)(end - from));
                done += (size_t)(end - from);
            }
        }
        br->ibuf_pos = 0;
        br->ibuf_len = total;
        br->bits = 0;
        br->n_bits = 0;
    }
    return bz2_br_skip_bits(br, (int)(pos & 7u));
}

/* ---- the ordered writer ------------------------------------------------ */

static bool bz2_mt_write_all(xx_io_device *dev, const uint8_t *data, size_t size)
{
    size_t done = 0;
    while (done < size) {
        ssize_t w = xx_io_write(dev, data + done, size - done);
        if (w <= 0) return false;
        done += (size_t)w;
    }
    return true;
}

int bz2_mt_run(bz2_bit_reader *br, xx_io_device *dst, int block_bytes, uint32_t *combined_crc, xx_pd_struct *pd, int threads, uint64_t *blocks)
{
    bz2_mt *mt;
    uint64_t pos = 0;
    bool failed = false;
    int started = 0;
    int limit;

    if (blocks) *blocks = 0;
    if (!br || !dst || !combined_crc || !blocks || br->error || threads < 2 || br->n_bits < 0 || br->n_bits > 64 || br->ibuf_pos > br->ibuf_len) return BZ2_MT_CONTINUE;
    if (threads > BZ2_MT_MAX_THREADS) threads = BZ2_MT_MAX_THREADS;
    /* A memory source is a segment of its own: it must hold the bit buffer's bytes. */
    if (!br->dev && (!br->ibuf || (size_t)((br->n_bits + 7) / 8) > br->ibuf_pos)) return BZ2_MT_CONTINUE;

    mt = (bz2_mt *)xx_mem_alloc(sizeof(*mt));
    if (!mt) return BZ2_MT_CONTINUE;
    xx_rt_memset(mt, 0, sizeof(*mt));
    mt->br = br;
    mt->block_bytes = block_bytes;
    mt->out_cap = (size_t)block_bytes * 4;
    mt->lock = xx_mutex_create();
    mt->work_cond = xx_cond_create();
    mt->done_cond = xx_cond_create();
    mt->workers = (bz2_mt_worker *)xx_mem_alloc((size_t)threads * sizeof(bz2_mt_worker));
    if (!mt->lock || !mt->work_cond || !mt->done_cond || !mt->workers) goto cleanup;
    xx_rt_memset(mt->workers, 0, (size_t)threads * sizeof(bz2_mt_worker));

    /* The first segment tells whether there is anything to do in parallel:
     * at least two candidates. */
    if (!bz2_mt_next_segment(mt, &pos)) goto cleanup;
    if (mt->cur->ncand < 2 && mt->input_done) goto finish;

    for (int i = 0; i < threads; i++) {
        bz2_mt_worker *wk = &mt->workers[started];
        wk->mt = mt;
        if (!bz2_work_init(&wk->work, block_bytes)) break;
        wk->thread = xx_thread_create(bz2_mt_worker_main, wk);
        if (!wk->thread) {
            bz2_work_free(&wk->work);
            break;
        }
        started++;
    }
    mt->threads = started;
    if (started == 0) goto finish;
    limit = 2 * started;

    for (;;) {
        bz2_mt_task *t;
        bool stop = false;

        if (pd && xx_pd_is_stopped(pd)) {
            failed = true;
            break;
        }

        /* Keep the workers supplied: at most `limit` blocks in memory. */
        while (mt->live < limit) {
            bz2_mt_seg *seg = NULL;
            uint64_t bit = 0;
            if (!bz2_mt_next_candidate(mt, pos, &seg, &bit)) break;
            t = (bz2_mt_task *)xx_mem_alloc(sizeof(*t));
            if (!t) {
                mt->stalled = true;
                break;
            }
            xx_rt_memset(t, 0, sizeof(*t));
            t->seg = seg;
            t->start_bit = bit;
            xx_mutex_lock(mt->lock);
            if (mt->tail) mt->tail->next = t;
            else mt->head = t;
            mt->tail = t;
            if (!mt->next_run) mt->next_run = t;
            mt->live++;
            xx_cond_signal(mt->work_cond);
            xx_mutex_unlock(mt->lock);
        }

        xx_mutex_lock(mt->lock);
        bz2_mt_advance(mt, pos);
        t = mt->head;
        if (!t) {
            xx_mutex_unlock(mt->lock);
            /* More candidates may follow in input not yet read. */
            if (!mt->stalled && !(mt->input_done && mt->cur->next_cand >= mt->cur->ncand)) continue;
            break;
        }
        if (t->start_bit != pos) {
            /* No block starts here: end of stream, or damage. */
            xx_mutex_unlock(mt->lock);
            break;
        }
        while (t->state != BZ2_MT_DONE) {
            (void)xx_cond_wait(mt->done_cond, mt->lock, 100);
            if (t->state != BZ2_MT_DONE && pd && xx_pd_is_stopped(pd)) {
                stop = true;
                break;
            }
        }
        if (stop) {
            xx_mutex_unlock(mt->lock);
            failed = true;
            break;
        }
        mt->head = t->next;
        if (!mt->head) mt->tail = NULL;
        mt->live--;
        xx_mutex_unlock(mt->lock);

        if (!t->ok) {
            /* The sequential decoder decides what this block is. */
            xx_mutex_lock(mt->lock);
            bz2_mt_task_free(mt, t);
            xx_mutex_unlock(mt->lock);
            break;
        }

        /* The whole block is written before its CRC is compared. */
        if (!t->big) {
            if (!bz2_mt_write_all(dst, t->out, t->out_len)) failed = true;
        } else {
            /* One write for the whole block, as the sequential decoder does. */
            uint32_t crc = 0;
            if (!mt->big_buf) {
                mt->big_cap = mt->out_cap * 2;
                mt->big_buf = (uint8_t *)xx_mem_alloc(mt->big_cap);
            }
            if (!mt->big_buf) {
                /* The sequential decoder takes it from here. */
                mt->big_cap = 0;
                xx_mutex_lock(mt->lock);
                bz2_mt_task_free(mt, t);
                xx_mutex_unlock(mt->lock);
                break;
            }
            if (!bz2_block_emit(dst, t->mid, t->n, &mt->big_buf, &mt->big_cap, &crc)) failed = true;
        }
        if (!failed && t->calc_crc != t->stored_crc) failed = true;
        if (!failed) {
            *combined_crc = ((*combined_crc << 1) | (*combined_crc >> 31)) ^ t->calc_crc;
            pos = t->end_bit;
            (*blocks)++;
        }
        xx_mutex_lock(mt->lock);
        bz2_mt_task_free(mt, t);
        xx_mutex_unlock(mt->lock);
        if (failed) break;
    }

finish:
    /* Stop the workers; a block being decoded is finished first. */
    if (mt->threads > 0) {
        xx_mutex_lock(mt->lock);
        mt->quit = true;
        xx_cond_broadcast(mt->work_cond);
        xx_mutex_unlock(mt->lock);
        for (int i = 0; i < mt->threads; i++) {
            (void)xx_thread_join(mt->workers[i].thread, NULL);
            bz2_work_free(&mt->workers[i].work);
        }
    }
    while (mt->head) {
        bz2_mt_task *t = mt->head;
        mt->head = t->next;
        /* Workers have exited: an abandoned task was freed by its worker, the rest are queued or done. */
        bz2_mt_task_free(mt, t);
    }
    if (!bz2_mt_hand_back(mt, pos)) failed = true;

cleanup:
    if (mt->big_buf) xx_mem_free(mt->big_buf);
    while (mt->pool_count > 0) xx_mem_free(mt->pool[--mt->pool_count]);
    while (mt->first) {
        bz2_mt_seg *seg = mt->first;
        mt->first = seg->next;
        bz2_mt_seg_free(seg);
    }
    if (mt->workers) xx_mem_free(mt->workers);
    if (mt->done_cond) xx_cond_destroy(mt->done_cond);
    if (mt->work_cond) xx_cond_destroy(mt->work_cond);
    if (mt->lock) xx_mutex_destroy(mt->lock);
    xx_mem_free(mt);
    return failed ? BZ2_MT_FAILED : BZ2_MT_CONTINUE;
}

#else

typedef int xx_bzip2_dec_mt_unused;

#endif /* XX_BZIP2_MT */
