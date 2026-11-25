/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Multi-threaded LZMA2 / xz decoding: shared pieces.  Not part of the public
 * API.  The implementation lives in xx_lzma_mt.inc, compiled as part of
 * xx_lzma_dec.c.
 *
 * Only data that the format itself makes independent is decoded in
 * parallel: runs of LZMA2 chunks that start with a dictionary reset, and the
 * blocks of an xz stream.  The calling thread does all the reading and
 * writing, in stream order; workers only turn one buffer into another.  A
 * unit that a worker cannot decode is decoded again by the sequential code,
 * so that damaged input fails exactly as it does without threads.
 */

#ifndef XXFCLIB_LZMA_MT_INTERNAL_H
#define XXFCLIB_LZMA_MT_INTERNAL_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

/* Compile-time switch: XX_LZMA_MT=1 enables multi-threaded LZMA2 / xz block
 * decoding; such a build must also compile src/threads/xx_threads.c and its
 * platform file.  The full and formats profiles define it; component builds
 * (per-format consoles, the ZIP library, archive families) leave it off and
 * use nothing of xxfclib/threads. */
#ifndef XX_LZMA_MT
#define XX_LZMA_MT 0
#endif

/* Most worker threads a decode may use. */
#ifndef XX_LZMA_MT_MAX_THREADS
#define XX_LZMA_MT_MAX_THREADS 32U
#endif

/* Memory the buffers of units in flight may take together (each holds its
 * compressed and its decoded bytes until it is written).  A unit that needs
 * more than half of it is left to the sequential decoder. */
#ifndef XX_LZMA_MT_MEMORY
#define XX_LZMA_MT_MEMORY (sizeof(void *) > 4U ? (uint64_t)1536U * 1024U * 1024U : (uint64_t)256U * 1024U * 1024U)
#endif

/* Threads to decode with: the CPU count, at most XX_LZMA_MT_MAX_THREADS.
 * The environment variable XXFC_LZMA_THREADS overrides it; 0 or 1 turns
 * multi-threaded decoding off.  1 means: decode sequentially. */
unsigned xx_lzma_mt_threads(void);

/* XX_LZMA_MT_MEMORY, or XXFC_LZMA_MT_MEMORY_MB mebibytes from the environment. */
uint64_t xx_lzma_mt_memory(void);

/* ---- an ordered queue of jobs run by worker threads (XX_LZMA_MT only) ---- */

typedef struct xx_lzma_mt_pipe_s xx_lzma_mt_pipe;

/* Runs on a worker thread.  *cancel turns nonzero when the result is no
 * longer wanted; the job should then return (false) soon. */
typedef bool (*xx_lzma_mt_work_fn)(void *job, volatile long *cancel);

xx_lzma_mt_pipe *xx_lzma_mt_pipe_create(unsigned threads);
/* Queue a job behind the ones already queued.  False: it was not queued. */
bool xx_lzma_mt_pipe_push(xx_lzma_mt_pipe *pipe, xx_lzma_mt_work_fn work, void *job);
/* Jobs pushed and not yet popped. */
size_t xx_lzma_mt_pipe_count(const xx_lzma_mt_pipe *pipe);
/* Wait up to timeout_ms for the oldest job: 1 it succeeded, 0 it failed,
 * -1 it is not finished (or there is none). */
int xx_lzma_mt_pipe_wait(xx_lzma_mt_pipe *pipe, uint32_t timeout_ms);
/* Take the oldest job off the queue; it must have finished, or the pipe
 * must have been stopped.  NULL when the queue is empty. */
void *xx_lzma_mt_pipe_pop(xx_lzma_mt_pipe *pipe);
/* Cancel the running jobs, drop the waiting ones and join the workers.  The
 * jobs stay on the queue for the caller to pop and release. */
void xx_lzma_mt_pipe_stop(xx_lzma_mt_pipe *pipe);
/* Stops the pipe if needed.  The queue must have been emptied. */
void xx_lzma_mt_pipe_free(xx_lzma_mt_pipe *pipe);

/* ---- LZMA2 ---- */

typedef struct xx_lzma2_mt_source_s {
    xx_io_device *device; /* NULL: the stream is in memory */
    int64_t offset;       /* device offset of the stream's first byte */
    const uint8_t *memory;
    uint64_t size;        /* bytes the stream may occupy */
} xx_lzma2_mt_source;

typedef bool (*xx_lzma2_mt_write_fn)(void *context, const uint8_t *data, size_t size);

typedef enum xx_lzma2_mt_result_e {
    XX_LZMA2_MT_RESUME = 0,   /* decode on sequentially, with a fresh decoder, at *resume */
    XX_LZMA2_MT_FINISHED = 1, /* the end marker was reached and everything written */
    XX_LZMA2_MT_FAILED = 2    /* the output failed or the caller stopped the work */
} xx_lzma2_mt_result;

/* Decode the leading dictionary-reset units of an LZMA2 stream in parallel
 * and write their output in order.  It stops at the first thing it does not
 * take (the end of the data, a chunk it cannot parse, a unit too large for
 * the memory budget, a unit a worker could not decode) and reports in
 * *resume the offset of the unit to continue with: always a chunk that
 * resets the dictionary, where a fresh sequential decoder behaves exactly
 * as one that decoded everything before it.  A stream of a single unit is
 * not touched at all (RESUME at 0). */
xx_lzma2_mt_result xx_lzma2_mt_decode(const xx_lzma2_mt_source *source, uint8_t property, unsigned threads, uint64_t memory, xx_lzma2_mt_write_fn write, void *context,
                                      xx_pd_struct *pd, uint64_t *resume);

#endif /* XXFCLIB_LZMA_MT_INTERNAL_H */
