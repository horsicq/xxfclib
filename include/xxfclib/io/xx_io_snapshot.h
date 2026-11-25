/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_IO_SNAPSHOT_H
#define XX_IO_SNAPSHOT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xxfclib/io/xx_io.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_io_snapshot xx_io_snapshot;
typedef bool (*xx_io_snapshot_reader)(void *user, uint64_t offset, void *bytes, size_t length);
typedef bool (*xx_io_snapshot_progress)(uint64_t current, uint64_t total, void *user);
/* Streams a consistent caller-owned read provider into a private temporary
 * file. The caller keeps its provider immutable for this call and validates
 * external sources before/after. Cancellation returns NULL and sets cancelled.
 * No input-sized memory allocation. Each opened device has an independent
 * cursor and must close before destroy; the snapshot owns/removes its path. */
XXFC_API xx_io_snapshot *xx_io_snapshot_create(uint64_t size, xx_io_snapshot_reader reader, void *reader_user, xx_io_snapshot_progress progress, void *progress_user,
                                               bool *cancelled, char *error, size_t capacity);
/* Retain the same immutable revision without copying bytes. */
XXFC_API xx_io_snapshot *xx_io_snapshot_retain(xx_io_snapshot *snapshot);
typedef struct xx_io_snapshot_range {
    uint64_t offset, length;
} xx_io_snapshot_range;
/* Clone source into a private immutable revision, replacing only listed ranges.
 * APFS uses clonefile (copy on write); portable fallback streams bounded blocks.
 * Bytes outside ranges must equal source, and source size must equal size. */
XXFC_API xx_io_snapshot *xx_io_snapshot_create_ranges(const char *source, uint64_t size, const xx_io_snapshot_range *ranges, size_t range_count,
                                                      xx_io_snapshot_reader reader, void *reader_user, xx_io_snapshot_progress progress, void *progress_user,
                                                      bool *cancelled, char *error, size_t capacity);
XXFC_API const char *xx_io_snapshot_path(const xx_io_snapshot *snapshot);
XXFC_API uint64_t xx_io_snapshot_size(const xx_io_snapshot *snapshot);
/* True after publication to a caller-owned durable cache path. */
XXFC_API bool xx_io_snapshot_is_persistent(const xx_io_snapshot *snapshot);
XXFC_API xx_io_device *xx_io_snapshot_open(const xx_io_snapshot *snapshot);
/* Publish into a caller-owned private cache path (which may be an empty
 * reservation), then retain that path after destroy. Across filesystems the
 * implementation stages a bounded copy before replacing the target. */
XXFC_API bool xx_io_snapshot_persist(xx_io_snapshot *snapshot, const char *path, char *error, size_t capacity);
/* Progress is called before publication and at most 64 KiB apart during a
 * cross-volume copy. Return false to cancel. Failure/cancellation preserves
 * the original snapshot and the target; no incomplete target is published.
 * On cancellation returns false, sets *cancelled and leaves error empty. */
XXFC_API bool xx_io_snapshot_persist_progress(xx_io_snapshot *snapshot, const char *path, xx_io_snapshot_progress progress, void *user, bool *cancelled, char *error,
                                              size_t capacity);
XXFC_API void xx_io_snapshot_destroy(xx_io_snapshot *snapshot);
#ifdef __cplusplus
}
#endif
#endif
