/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Multi-threaded LZMA2 decoding (see xx_lzma_mt_internal.h).  This file is
 * part of xx_lzma_dec.c, which includes it: it uses that file's allocator
 * adapter and the LZMA SDK decoder compiled there.
 *
 * An LZMA2 stream is a sequence of chunks.  A chunk whose control byte is
 * 0x01, or 0xE0 and above, resets the dictionary; the decoder then also
 * requires new properties and a state reset before the next LZMA chunk.
 * Nothing after such a chunk depends on anything before it, so the run of
 * chunks from one reset to the next (a "unit" here; 7-Zip's multi-threaded
 * encoder writes one per block) can be decoded on its own, into a buffer of
 * exactly its unpacked size.  The chunk headers give every size, so the
 * units are found by walking the headers without decoding anything.
 */

#include "xx_lzma_mt_internal.h"
#if XX_LZMA_MT
#include "xxfclib/threads/xx_threads.h"
#endif

/* Input handed to the SDK per call, between checks for cancellation. */
#define XX_LZMA2_MT_SLICE ((size_t)1U << 20)
/* A unit's output is written in pieces of this size: one write of a hundred
 * megabytes is several times slower than the same bytes in small writes
 * (Windows stops caching very large writes), and a stop request is seen
 * between pieces. */
#define XX_LZMA2_MT_WRITE ((size_t)1U << 20)
/* How long the calling thread waits for a unit before it looks at the
 * progress object again. */
#define XX_LZMA2_MT_POLL_MS 50U
/* Lzma2Dec.c's LZMA2_STATE_CONTROL: the decoder is between two chunks. */
#define XX_LZMA2_MT_STATE_CONTROL 0U
/* Includes the LZMA2 probability model (lc + lp <= 4) and queue/allocator
 * overhead.  Keep this charged until the job is popped;
 * workers release input asynchronously, so admission never reads it. */
#define XX_LZMA2_MT_JOB_RESERVE ((uint64_t)2U << 20)
#if SIZE_MAX <= 0xFFFFFFFFU
/* Charge each live worker separately, including idle workers after their
 * jobs have been popped.  Use an explicit 1 MiB stack so that this 2 MiB
 * reserve also covers thread bookkeeping regardless of the host default. */
#define XX_LZMA_MT_WORKER_STACK ((size_t)1U << 20)
#define XX_LZMA_MT_WORKER_RESERVE ((uint64_t)2U << 20)
#endif

#if XX_LZMA_MT
static bool xx_lzma_mt_env(const char *name, uint64_t *value)
{
    const char *text = xx_rt_getenv(name);
    uint64_t result = 0U;
    size_t index;
    if (!text || !text[0]) return false;
    for (index = 0U; text[index]; ++index) {
        if (text[index] < '0' || text[index] > '9' || index >= 9U) return false;
        result = result * 10U + (uint64_t)(text[index] - '0');
    }
    *value = result;
    return true;
}
#endif

unsigned xx_lzma_mt_threads(void)
{
#if XX_LZMA_MT
    uint64_t wanted;
    unsigned count;
    if (xx_lzma_mt_env("XXFC_LZMA_THREADS", &wanted)) count = wanted > XX_LZMA_MT_MAX_THREADS ? XX_LZMA_MT_MAX_THREADS : (unsigned)wanted;
    else {
        count = (unsigned)xx_thread_cpu_count();
        if (count > XX_LZMA_MT_MAX_THREADS) count = XX_LZMA_MT_MAX_THREADS;
    }
    return count < 2U ? 1U : count;
#else
    return 1U;
#endif
}

uint64_t xx_lzma_mt_memory(void)
{
#if XX_LZMA_MT
    uint64_t megabytes;
    if (xx_lzma_mt_env("XXFC_LZMA_MT_MEMORY_MB", &megabytes)) return megabytes << 20;
#endif
    return XX_LZMA_MT_MEMORY;
}

#if !XX_LZMA_MT

/* Built without threads: every stream is left to the sequential decoder. */
xx_lzma2_mt_result xx_lzma2_mt_decode(const xx_lzma2_mt_source *source, uint8_t property, unsigned threads, uint64_t memory, xx_lzma2_mt_write_fn write, void *context,
                                      xx_pd_struct *pd, uint64_t *resume)
{
    (void)source;
    (void)property;
    (void)threads;
    (void)memory;
    (void)write;
    (void)context;
    (void)pd;
    if (resume) *resume = 0U;
    return XX_LZMA2_MT_RESUME;
}

#else /* XX_LZMA_MT */

/* ------------------------------------------------------------- the pipe */

/* Worker threads are plain xx_thread_t's (the thread pool is not part of
 * every build profile), started one at a time as jobs arrive while no
 * worker is idle.  One mutex guards the queue and the job states. */

typedef struct xx_lzma_mt_node_s {
    struct xx_lzma_mt_node_s *next;
    xx_lzma_mt_work_fn work;
    void *job;
    bool done;
    bool ok;
} xx_lzma_mt_node;

struct xx_lzma_mt_pipe_s {
    xx_mutex_t *mutex;
    xx_cond_t *work_ready;
    xx_cond_t *work_done;
    xx_lzma_mt_node *head;
    xx_lzma_mt_node *tail;
    xx_lzma_mt_node *next; /* the first job no worker has taken */
    size_t count;
    xx_thread_t **workers;
    unsigned worker_limit;
    unsigned worker_count;
#if SIZE_MAX <= 0xFFFFFFFFU
    uint64_t worker_memory;       /* released only after the workers join */
    uint64_t worker_memory_limit; /* budget left after admitted job costs */
#endif
    unsigned idle;
    volatile long cancel;
    bool quit;
    bool stopped;
};

static int xx_lzma_mt_worker(void *argument)
{
    xx_lzma_mt_pipe *pipe = (xx_lzma_mt_pipe *)argument;
    xx_mutex_lock(pipe->mutex);
    for (;;) {
        xx_lzma_mt_node *node;
        bool ok = false;
        while (!pipe->quit && !pipe->next) {
            ++pipe->idle;
            (void)xx_cond_wait(pipe->work_ready, pipe->mutex, XX_WAIT_INFINITE);
            --pipe->idle;
        }
        if (pipe->quit) break;
        node = pipe->next;
        pipe->next = node->next;
        xx_mutex_unlock(pipe->mutex);
        if (xx_atomic_load(&pipe->cancel) == 0) ok = node->work(node->job, &pipe->cancel);
        xx_mutex_lock(pipe->mutex);
        node->ok = ok;
        node->done = true;
        xx_cond_broadcast(pipe->work_done);
    }
    xx_mutex_unlock(pipe->mutex);
    return 0;
}

static void xx_lzma_mt_pipe_release(xx_lzma_mt_pipe *pipe)
{
    if (pipe->work_done) xx_cond_destroy(pipe->work_done);
    if (pipe->work_ready) xx_cond_destroy(pipe->work_ready);
    if (pipe->mutex) xx_mutex_destroy(pipe->mutex);
    xx_mem_free(pipe->workers);
    xx_mem_free(pipe);
}

xx_lzma_mt_pipe *xx_lzma_mt_pipe_create(unsigned threads)
{
    xx_lzma_mt_pipe *pipe;
    if (threads < 1U) return NULL;
    pipe = (xx_lzma_mt_pipe *)xx_mem_alloc(sizeof(*pipe));
    if (!pipe) return NULL;
    xx_rt_memset(pipe, 0, sizeof(*pipe));
    pipe->worker_limit = threads;
#if SIZE_MAX <= 0xFFFFFFFFU
    pipe->worker_memory_limit = UINT64_MAX;
#endif
    pipe->workers = (xx_thread_t **)xx_mem_alloc((size_t)threads * sizeof(*pipe->workers));
    pipe->mutex = xx_mutex_create();
    pipe->work_ready = xx_cond_create();
    pipe->work_done = xx_cond_create();
    if (!pipe->workers || !pipe->mutex || !pipe->work_ready || !pipe->work_done) {
        xx_lzma_mt_pipe_release(pipe);
        return NULL;
    }
    return pipe;
}

bool xx_lzma_mt_pipe_push(xx_lzma_mt_pipe *pipe, xx_lzma_mt_work_fn work, void *job)
{
    xx_lzma_mt_node *node;
    bool queued = true;
    if (!pipe || pipe->stopped || !work) return false;
    node = (xx_lzma_mt_node *)xx_mem_alloc(sizeof(*node));
    if (!node) return false;
    xx_rt_memset(node, 0, sizeof(*node));
    node->work = work;
    node->job = job;
    xx_mutex_lock(pipe->mutex);
    /* A job needs a worker: one more is started while none is idle.  With
     * no worker at all the job cannot be taken. */
    if (pipe->idle == 0U && pipe->worker_count < pipe->worker_limit) {
#if SIZE_MAX <= 0xFFFFFFFFU
        if (pipe->worker_memory <= pipe->worker_memory_limit &&
            XX_LZMA_MT_WORKER_RESERVE <= pipe->worker_memory_limit - pipe->worker_memory) {
            xx_thread_t *worker = xx_thread_create_ex(xx_lzma_mt_worker, pipe, XX_LZMA_MT_WORKER_STACK);
            if (worker) {
                pipe->workers[pipe->worker_count++] = worker;
                pipe->worker_memory += XX_LZMA_MT_WORKER_RESERVE;
            }
        }
#else
        xx_thread_t *worker = xx_thread_create(xx_lzma_mt_worker, pipe);
        if (worker) pipe->workers[pipe->worker_count++] = worker;
#endif
    }
    if (pipe->worker_count == 0U) {
        queued = false;
    } else {
        if (pipe->tail) pipe->tail->next = node;
        else pipe->head = node;
        pipe->tail = node;
        if (!pipe->next) pipe->next = node;
        ++pipe->count;
        xx_cond_signal(pipe->work_ready);
    }
    xx_mutex_unlock(pipe->mutex);
    if (!queued) xx_mem_free(node);
    return queued;
}

size_t xx_lzma_mt_pipe_count(const xx_lzma_mt_pipe *pipe)
{
    /* Changed by the calling thread alone. */
    return pipe ? pipe->count : 0U;
}

int xx_lzma_mt_pipe_wait(xx_lzma_mt_pipe *pipe, uint32_t timeout_ms)
{
    int state = -1;
    if (!pipe) return -1;
    xx_mutex_lock(pipe->mutex);
    if (pipe->head) {
        if (!pipe->head->done && !pipe->stopped) (void)xx_cond_wait(pipe->work_done, pipe->mutex, timeout_ms);
        if (pipe->head->done) state = pipe->head->ok ? 1 : 0;
    }
    xx_mutex_unlock(pipe->mutex);
    return state;
}

void *xx_lzma_mt_pipe_pop(xx_lzma_mt_pipe *pipe)
{
    xx_lzma_mt_node *node;
    void *job = NULL;
    if (!pipe) return NULL;
    xx_mutex_lock(pipe->mutex);
    node = pipe->head;
    /* A job a worker holds stays on the queue until the worker is done. */
    if (node && (node->done || pipe->stopped)) {
        pipe->head = node->next;
        if (!pipe->head) pipe->tail = NULL;
        if (pipe->next == node) pipe->next = node->next;
        --pipe->count;
    } else {
        node = NULL;
    }
    xx_mutex_unlock(pipe->mutex);
    if (node) {
        job = node->job;
        xx_mem_free(node);
    }
    return job;
}

void xx_lzma_mt_pipe_stop(xx_lzma_mt_pipe *pipe)
{
    unsigned index;
    if (!pipe || pipe->stopped) return;
    xx_mutex_lock(pipe->mutex);
    xx_atomic_store(&pipe->cancel, 1);
    pipe->quit = true;
    xx_cond_broadcast(pipe->work_ready);
    xx_mutex_unlock(pipe->mutex);
    for (index = 0U; index < pipe->worker_count; ++index) (void)xx_thread_join(pipe->workers[index], NULL);
    pipe->worker_count = 0U;
#if SIZE_MAX <= 0xFFFFFFFFU
    pipe->worker_memory = 0U;
#endif
    pipe->stopped = true;
}

void xx_lzma_mt_pipe_free(xx_lzma_mt_pipe *pipe)
{
    if (!pipe) return;
    xx_lzma_mt_pipe_stop(pipe);
    while (xx_lzma_mt_pipe_pop(pipe) != NULL) {
    }
    while (pipe->head) {
        /* Jobs pushed as NULL: release their nodes all the same. */
        xx_lzma_mt_node *node = pipe->head;
        pipe->head = node->next;
        xx_mem_free(node);
    }
    xx_lzma_mt_pipe_release(pipe);
}

/* ---------------------------------------------------------------- LZMA2 */

typedef struct xx_lzma2_mt_unit_s {
    uint64_t offset;
    uint64_t pack_size;
    uint64_t unpack_size;
    bool last; /* the end marker follows it */
} xx_lzma2_mt_unit;

typedef struct xx_lzma2_mt_job_s {
    uint64_t offset;
    uint8_t *input; /* owned copy of the unit, NULL when it is read in place */
    const uint8_t *data;
    size_t in_size;
    uint8_t *output;
    size_t out_size;
    uint64_t memory;
    uint8_t property;
} xx_lzma2_mt_job;

static bool xx_lzma2_mt_fetch(const xx_lzma2_mt_source *source, uint64_t offset, uint8_t *data, size_t size, xx_pd_struct *pd)
{
    size_t done = 0U;
    if (offset > source->size || (uint64_t)size > source->size - offset) return false;
    if (!source->device) {
        xx_rt_memcpy(data, source->memory + offset, size);
        return true;
    }
    if ((uint64_t)source->offset > (uint64_t)INT64_MAX - offset || xx_io_seek64(source->device, source->offset + (int64_t)offset, SEEK_SET) != 0) return false;
    while (done < size) {
        size_t request = size - done;
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (request > ((size_t)4U << 20)) request = (size_t)4U << 20;
        amount = xx_io_read(source->device, data + done, request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Walk the chunk headers of the unit that starts at `start`.  False when
 * there is no whole unit there that the limit admits: the sequential
 * decoder then says what the data is. */
static bool xx_lzma2_mt_scan(const xx_lzma2_mt_source *source, uint64_t start, uint64_t limit, xx_lzma2_mt_unit *unit, xx_pd_struct *pd)
{
    uint64_t position = start;
    uint64_t unpacked = 0U;
    bool first = true;
    for (;;) {
        uint8_t header[6];
        size_t available;
        size_t header_size;
        unsigned control;
        uint64_t chunk_unpacked;
        uint64_t chunk_packed;
        if (position >= source->size) return false;
        available = source->size - position < sizeof(header) ? (size_t)(source->size - position) : sizeof(header);
        if (!xx_lzma2_mt_fetch(source, position, header, available, pd)) return false;
        control = header[0];
        if (first) {
            if (control != 1U && control < 0xE0U) return false;
        } else if (control == 0U || control == 1U || control >= 0xE0U) {
            unit->offset = start;
            unit->pack_size = position - start;
            unit->unpack_size = unpacked;
            unit->last = control == 0U;
            return true;
        }
        if (control >= 3U && control < 0x80U) return false;
        header_size = control < 0x80U ? 3U : ((control & 0x40U) != 0U ? 6U : 5U);
        if (available < header_size) return false;
        if (control < 0x80U) {
            chunk_unpacked = (((uint64_t)header[1] << 8) | header[2]) + 1U;
            chunk_packed = chunk_unpacked;
        } else {
            chunk_unpacked = ((((uint64_t)control & 0x1FU) << 16) | ((uint64_t)header[1] << 8) | header[2]) + 1U;
            chunk_packed = (((uint64_t)header[3] << 8) | header[4]) + 1U;
        }
        if (source->size - position - header_size < chunk_packed) return false;
        position += header_size + chunk_packed;
        unpacked += chunk_unpacked;
        if (unpacked > limit || position - start > limit) return false;
        first = false;
    }
}

static void xx_lzma2_mt_job_free(xx_lzma2_mt_job *job)
{
    if (!job) return;
    xx_mem_free(job->input);
    xx_mem_free(job->output);
    xx_mem_free(job);
}

/* The unit's buffers; its compressed bytes are read here, by the caller's
 * thread.  NULL when memory or the input fails. */
static xx_lzma2_mt_job *xx_lzma2_mt_job_create(const xx_lzma2_mt_source *source, const xx_lzma2_mt_unit *unit, uint8_t property, xx_pd_struct *pd)
{
    xx_lzma2_mt_job *job = (xx_lzma2_mt_job *)xx_mem_alloc(sizeof(*job));
    if (!job) return NULL;
    xx_rt_memset(job, 0, sizeof(*job));
    job->offset = unit->offset;
    job->in_size = (size_t)unit->pack_size;
    job->out_size = (size_t)unit->unpack_size;
#if SIZE_MAX <= 0xFFFFFFFFU
    job->memory = (source->device ? unit->pack_size : 0U) + unit->unpack_size + XX_LZMA2_MT_JOB_RESERVE;
#else
    job->memory = unit->pack_size + unit->unpack_size;
#endif
    job->property = property;
    job->output = (uint8_t *)xx_mem_alloc(job->out_size);
    if (!job->output) {
        xx_lzma2_mt_job_free(job);
        return NULL;
    }
    if (!source->device) {
        job->data = source->memory + unit->offset;
        return job;
    }
    job->input = (uint8_t *)xx_mem_alloc(job->in_size);
    if (!job->input || !xx_lzma2_mt_fetch(source, unit->offset, job->input, job->in_size, pd)) {
        xx_lzma2_mt_job_free(job);
        return NULL;
    }
    job->data = job->input;
    return job;
}

/* Decode one unit with a decoder of its own.  The output buffer is the
 * dictionary: it holds the whole unit, so it never wraps.  Every chunk ends
 * exactly where its header says (LZMA_FINISH_END), as in the sequential
 * decoder; the unit is good when all its input is used, all its output is
 * there and the decoder is between two chunks. */
static bool xx_lzma2_mt_work(void *argument, volatile long *cancel)
{
    xx_lzma2_mt_job *job = (xx_lzma2_mt_job *)argument;
    CLzma2Dec decoder;
    size_t in_pos = 0U;
    bool ok = false;
    Lzma2Dec_CONSTRUCT(&decoder)
    if (Lzma2Dec_AllocateProbs(&decoder, job->property, &g_xx_sz_alloc) != SZ_OK) return false;
    decoder.decoder.dic = job->output;
    decoder.decoder.dicBufSize = job->out_size;
    Lzma2Dec_Init(&decoder);
    for (;;) {
        SizeT in_size = job->in_size - in_pos;
        SizeT before = decoder.decoder.dicPos;
        ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
        if (in_size > XX_LZMA2_MT_SLICE) in_size = XX_LZMA2_MT_SLICE;
        if (xx_atomic_load(cancel) != 0) break;
        if (Lzma2Dec_DecodeToDic(&decoder, job->out_size, job->data + in_pos, &in_size, LZMA_FINISH_END, &status) != SZ_OK) break;
        in_pos += in_size;
        if (in_size == 0U && decoder.decoder.dicPos == before) {
            ok = in_pos == job->in_size && decoder.decoder.dicPos == job->out_size && decoder.state == XX_LZMA2_MT_STATE_CONTROL;
            break;
        }
    }
    Lzma2Dec_FreeProbs(&decoder, &g_xx_sz_alloc);
    /* The compressed copy is not needed any more. */
    if (job->input) {
        xx_mem_free(job->input);
        job->input = NULL;
        job->data = NULL;
    }
    return ok;
}

xx_lzma2_mt_result xx_lzma2_mt_decode(const xx_lzma2_mt_source *source, uint8_t property, unsigned threads, uint64_t memory, xx_lzma2_mt_write_fn write, void *context,
                                      xx_pd_struct *pd, uint64_t *resume)
{
    xx_lzma2_mt_result result = XX_LZMA2_MT_RESUME;
    xx_lzma_mt_pipe *pipe;
    xx_lzma2_mt_unit unit;
#if SIZE_MAX <= 0xFFFFFFFFU
    uint64_t limit;
#else
    uint64_t limit = memory / 4U;
#endif
    uint64_t position = 0U;
    uint64_t written_to = 0U;
    uint64_t used = 0U;
    bool have_unit;
    bool scanning = true;
    bool ended = false;

    if (resume) *resume = 0U;
    if (!source || !write || !resume || threads < 2U || (!source->device && !source->memory)) return XX_LZMA2_MT_RESUME;
    /* On 32-bit hosts leave most of even a 2 GiB address space to the
     * caller, its input/output and the rest of the library.  An environment
     * override cannot remove this ceiling.  Allocation failure below can
     * reduce concurrency further when the address space is fragmented. */
#if SIZE_MAX <= 0xFFFFFFFFU
    if (memory > (uint64_t)(SIZE_MAX / 4U)) memory = (uint64_t)(SIZE_MAX / 4U);
    if (memory <= XX_LZMA2_MT_JOB_RESERVE + XX_LZMA_MT_WORKER_RESERVE) return XX_LZMA2_MT_RESUME;
    limit = memory - XX_LZMA2_MT_JOB_RESERVE - XX_LZMA_MT_WORKER_RESERVE;
    /* The aggregate job cost, rather than either buffer alone, is checked
     * at admission.  Do not reject a 128 MiB unit just to reserve four
     * hypothetical units.  The SDK still needs a bounded position. */
#endif
    if (limit > (uint64_t)0x7FFFFFFFU) limit = (uint64_t)0x7FFFFFFFU;
    if (limit > (uint64_t)(SIZE_MAX / 2U)) limit = (uint64_t)(SIZE_MAX / 2U);
    /* Threads are worth it from two units on. */
    if (!xx_lzma2_mt_scan(source, 0U, limit, &unit, pd) || unit.last) return XX_LZMA2_MT_RESUME;
    have_unit = true;
    pipe = xx_lzma_mt_pipe_create(threads);
    if (!pipe) return XX_LZMA2_MT_RESUME;

    for (;;) {
        xx_lzma2_mt_job *job;
        int state;
        bool ok;
        if (pd && xx_pd_is_stopped(pd)) {
            result = XX_LZMA2_MT_FAILED;
            break;
        }
        while (scanning) {
            uint64_t need;
            if (!have_unit) {
                if (!xx_lzma2_mt_scan(source, position, limit, &unit, pd)) {
                    scanning = false;
                    break;
                }
                have_unit = true;
            }
#if SIZE_MAX <= 0xFFFFFFFFU
            uint64_t workers = pipe->worker_memory;
            need = (source->device ? unit.pack_size : 0U) + unit.unpack_size + XX_LZMA2_MT_JOB_RESERVE;
            /* The first job must be able to start a worker.  Later jobs
             * retain every existing worker's charge, even with an empty
             * queue; push may add another worker only from the remainder. */
            if (workers == 0U) workers = XX_LZMA_MT_WORKER_RESERVE;
            /* One job more than there are workers, so that none waits while
             * this thread writes. */
            if (workers > memory || need > memory - workers) {
                scanning = false;
                break;
            }
            if (xx_lzma_mt_pipe_count(pipe) > (size_t)threads || need > memory - workers - used) break;
            pipe->worker_memory_limit = memory - used - need;
#else
            need = unit.pack_size + unit.unpack_size;
            if (xx_lzma_mt_pipe_count(pipe) > (size_t)threads || (xx_lzma_mt_pipe_count(pipe) != 0U && need > memory - used)) break;
#endif
            job = xx_lzma2_mt_job_create(source, &unit, property, pd);
            if (!job || !xx_lzma_mt_pipe_push(pipe, xx_lzma2_mt_work, job)) {
                xx_lzma2_mt_job_free(job);
#if SIZE_MAX <= 0xFFFFFFFFU
                /* Completed and running jobs still occupy buffers.  Drain
                 * the oldest one and retry this unit before abandoning
                 * parallel decoding.  With an empty queue, a failure is
                 * real and the sequential decoder resumes here. */
                if (xx_lzma_mt_pipe_count(pipe) == 0U) scanning = false;
#else
                scanning = false;
#endif
                break;
            }
            used += need;
            position = unit.offset + unit.pack_size;
            have_unit = false;
            if (unit.last) {
                scanning = false;
                ended = true;
            }
        }
        if (xx_lzma_mt_pipe_count(pipe) == 0U) break;
        state = xx_lzma_mt_pipe_wait(pipe, XX_LZMA2_MT_POLL_MS);
        if (state < 0) continue;
        job = (xx_lzma2_mt_job *)xx_lzma_mt_pipe_pop(pipe);
        if (state == 0) {
            /* The sequential decoder takes it from this unit on. */
            xx_lzma2_mt_job_free(job);
            ended = false;
            break;
        }
        ok = true;
        for (size_t done = 0U; ok && done < job->out_size;) {
            size_t amount = job->out_size - done < XX_LZMA2_MT_WRITE ? job->out_size - done : XX_LZMA2_MT_WRITE;
            ok = write(context, job->output + done, amount);
            done += amount;
        }
        used -= job->memory;
        written_to = job->offset + job->in_size;
        xx_lzma2_mt_job_free(job);
        if (!ok) {
            result = XX_LZMA2_MT_FAILED;
            break;
        }
    }

    xx_lzma_mt_pipe_stop(pipe);
    while (xx_lzma_mt_pipe_count(pipe) != 0U) {
        xx_lzma2_mt_job_free((xx_lzma2_mt_job *)xx_lzma_mt_pipe_pop(pipe));
        ended = false;
    }
    xx_lzma_mt_pipe_free(pipe);
    if (result == XX_LZMA2_MT_FAILED) return result;
    /* Finished: the stream's end marker (one 0x00 control byte) follows the
     * last unit and is consumed too, as the sequential decoder does. */
    *resume = ended ? written_to + 1U : written_to;
    return ended ? XX_LZMA2_MT_FINISHED : XX_LZMA2_MT_RESUME;
}

#endif /* XX_LZMA_MT */
