/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_HISTORY_XX_HISTORY_H
#define XXFCLIB_HISTORY_XX_HISTORY_H

#include "xxfclib/xxfc_defs.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_history xx_history;
typedef struct xx_history_entry xx_history_entry;

typedef enum xx_history_status {
    XX_HISTORY_OK = 0,
    XX_HISTORY_INVALID_ARGUMENT,
    XX_HISTORY_OUT_OF_MEMORY,
    XX_HISTORY_EMPTY,
    XX_HISTORY_BUSY,
    XX_HISTORY_APPLY_FAILED
} xx_history_status;

/** A reversible command whose initial change is applied by the caller.
 * undo/redo return zero on success. A failed callback MUST leave its external
 * state unchanged: the history does not attempt to reverse a failed callback.
 * context is supplied to xx_history_undo/redo, and is never retained.
 * dispose is optional and owns cleanup of data, including external resources.
 * label is optional, copied by prepare, and may be freed by the caller afterward.
 */
typedef struct xx_history_command {
    int (*undo)(void *data, void *context);
    int (*redo)(void *data, void *context);
    void (*dispose)(void *data);
    void *data;
    const char *label;
} xx_history_command;

/** Create a serialized command history. max_entries == 0 means unlimited.
 * This module has no thread dependency; the caller serializes ALL access.
 */
XXFC_API xx_history *xx_history_create(size_t max_entries);

/** Dispose committed commands and an outstanding prepared command, then free
 * the history. NULL is harmless. Calling destroy from a callback/disposer on
 * this same history does nothing; the active operation keeps ownership.
 */
XXFC_API void xx_history_destroy(xx_history *history);

/** Reserve an entry BEFORE mutating external state. Both replay callbacks are
 * required. On success the history owns command.data; on failure it remains
 * the caller's responsibility. *entry is NULL on failure when entry != NULL.
 * Only one entry may be pending. Pending preparation blocks prepare, clear,
 * undo, redo and push_applied until commit or abort. It preserves the current
 * redo branch, labels and counts, and performs no replay callback.
 */
XXFC_API xx_history_status xx_history_prepare(xx_history *history, const xx_history_command *command, xx_history_entry **entry);

/** Publish the prepared entry after the initial external change succeeds.
 * This operation allocates nothing and never invokes undo/redo. It discards
 * the redo branch and, if bounded, the oldest entries beyond max_entries.
 * Only the exact pending entry from this history is accepted. On success the
 * entry handle is consumed; do not reuse it. A valid commit cannot fail unless
 * called reentrantly during a callback/disposer.
 */
XXFC_API xx_history_status xx_history_commit(xx_history *history, xx_history_entry *entry);

/** Discard a prepared command when the initial change was not applied or was
 * rolled back. Its disposer is called once. The redo branch is preserved.
 * The entry handle is consumed on success.
 */
XXFC_API xx_history_status xx_history_abort(xx_history *history, xx_history_entry *entry);

/** Convenience for an already-applied command: prepare followed by commit.
 * On failure before preparation the caller still owns command.data. For
 * changes needing guaranteed history storage, use prepare BEFORE applying.
 */
XXFC_API xx_history_status xx_history_push_applied(xx_history *history, const xx_history_command *command);

/** Replay exactly one command. A nonzero callback result returns APPLY_FAILED
 * and leaves the history cursor/counts unchanged. Reentrant mutation of the
 * same history from replay callbacks or disposers returns BUSY. Callbacks must
 * not destroy the active history; destroy is a no-op while it is busy.
 */
XXFC_API xx_history_status xx_history_undo(xx_history *history, void *context);
XXFC_API xx_history_status xx_history_redo(xx_history *history, void *context);
XXFC_API xx_history_status xx_history_clear(xx_history *history);

/** can_undo/can_redo are false while busy or while an entry is pending.
 * Counts always describe committed entries and exclude a prepared entry.
 * Labels are borrowed until the next successful mutation or destruction;
 * NULL means no respective entry, or an entry without a label.
 * NULL histories have zero counts, false capabilities and NULL labels.
 */
XXFC_API int xx_history_can_undo(const xx_history *history);
XXFC_API int xx_history_can_redo(const xx_history *history);
XXFC_API size_t xx_history_undo_count(const xx_history *history);
XXFC_API size_t xx_history_redo_count(const xx_history *history);
XXFC_API const char *xx_history_undo_label(const xx_history *history);
XXFC_API const char *xx_history_redo_label(const xx_history *history);
/** Borrow the next undo/redo command for inspection, for example to check
 * command-specific capabilities. NULL for an empty, busy or pending history.
 * The command and its payload remain owned by the history and are valid only
 * until its next successful mutation/destruction. Invoke replay through the
 * history API so its cursor continues to match the external state. */
XXFC_API const xx_history_command *xx_history_undo_command(const xx_history *history);
XXFC_API const xx_history_command *xx_history_redo_command(const xx_history *history);

#ifdef __cplusplus
}
#endif

#endif
