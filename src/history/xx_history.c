/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/history/xx_history.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

struct xx_history_entry {
    struct xx_history_entry *previous;
    struct xx_history_entry *next;
    xx_history_command command;
    char *label;
};

struct xx_history {
    xx_history_entry *first;
    xx_history_entry *last;
    xx_history_entry *current;
    xx_history_entry *pending;
    size_t max_entries;
    size_t count;
    size_t applied;
    int busy;
};

/* All callers hold busy while invoking application-owned cleanup. Unlink
 * nodes and publish the new cursor before releasing their external resources.
 */
static void xx_history_release_entry(xx_history_entry *entry)
{
    if (entry->command.dispose) entry->command.dispose(entry->command.data);
    xx_mem_free(entry->label);
    xx_mem_free(entry);
}

static void xx_history_release_chain(xx_history_entry *entry)
{
    while (entry) {
        xx_history_entry *next = entry->next;
        xx_history_release_entry(entry);
        entry = next;
    }
}

xx_history *xx_history_create(size_t max_entries)
{
    xx_history *history = (xx_history *)xx_mem_calloc(1, sizeof(*history));
    if (history) history->max_entries = max_entries;
    return history;
}

void xx_history_destroy(xx_history *history)
{
    xx_history_entry *entries;
    xx_history_entry *pending;
    if (!history || history->busy) return;
    history->busy = 1;
    entries = history->first;
    pending = history->pending;
    history->first = NULL;
    history->last = NULL;
    history->current = NULL;
    history->pending = NULL;
    history->count = 0;
    history->applied = 0;
    if (pending) xx_history_release_entry(pending);
    xx_history_release_chain(entries);
    xx_mem_free(history);
}

xx_history_status xx_history_prepare(xx_history *history, const xx_history_command *command, xx_history_entry **entry)
{
    xx_history_entry *prepared;
    if (entry) *entry = NULL;
    if (!history || !command || !entry || !command->undo || !command->redo) return XX_HISTORY_INVALID_ARGUMENT;
    if (history->busy || history->pending) return XX_HISTORY_BUSY;
    if (history->applied == (size_t)-1) return XX_HISTORY_OUT_OF_MEMORY;
    prepared = (xx_history_entry *)xx_mem_calloc(1, sizeof(*prepared));
    if (!prepared) return XX_HISTORY_OUT_OF_MEMORY;
    if (command->label) {
        size_t length = xx_rt_strlen(command->label);
        if (length == (size_t)-1) {
            xx_mem_free(prepared);
            return XX_HISTORY_OUT_OF_MEMORY;
        }
        prepared->label = (char *)xx_mem_alloc(length + 1);
        if (!prepared->label) {
            xx_mem_free(prepared);
            return XX_HISTORY_OUT_OF_MEMORY;
        }
        xx_rt_memcpy(prepared->label, command->label, length + 1);
    }
    prepared->command = *command;
    prepared->command.label = prepared->label;
    history->pending = prepared;
    *entry = prepared;
    return XX_HISTORY_OK;
}

xx_history_status xx_history_commit(xx_history *history, xx_history_entry *entry)
{
    xx_history_entry *discarded;
    if (!history || !entry) return XX_HISTORY_INVALID_ARGUMENT;
    if (history->busy) return XX_HISTORY_BUSY;
    if (history->pending != entry) return XX_HISTORY_INVALID_ARGUMENT;
    history->busy = 1;
    discarded = history->current ? history->current->next : history->first;
    entry->previous = history->current;
    if (history->current) history->current->next = entry;
    else history->first = entry;
    history->last = entry;
    history->current = entry;
    history->pending = NULL;
    history->count = history->applied + 1;
    history->applied = history->count;
    xx_history_release_chain(discarded);
    if (history->max_entries && history->count > history->max_entries) {
        xx_history_entry *oldest = history->first;
        history->first = oldest->next;
        history->first->previous = NULL;
        --history->count;
        --history->applied;
        xx_history_release_entry(oldest);
    }
    history->busy = 0;
    return XX_HISTORY_OK;
}

xx_history_status xx_history_abort(xx_history *history, xx_history_entry *entry)
{
    if (!history || !entry) return XX_HISTORY_INVALID_ARGUMENT;
    if (history->busy) return XX_HISTORY_BUSY;
    if (history->pending != entry) return XX_HISTORY_INVALID_ARGUMENT;
    history->busy = 1;
    history->pending = NULL;
    xx_history_release_entry(entry);
    history->busy = 0;
    return XX_HISTORY_OK;
}

xx_history_status xx_history_push_applied(xx_history *history, const xx_history_command *command)
{
    xx_history_entry *entry;
    xx_history_status status = xx_history_prepare(history, command, &entry);
    if (status != XX_HISTORY_OK) return status;
    return xx_history_commit(history, entry);
}

xx_history_status xx_history_undo(xx_history *history, void *context)
{
    int result;
    xx_history_entry *entry;
    if (!history) return XX_HISTORY_INVALID_ARGUMENT;
    if (history->busy || history->pending) return XX_HISTORY_BUSY;
    if (!history->current) return XX_HISTORY_EMPTY;
    history->busy = 1;
    entry = history->current;
    result = entry->command.undo(entry->command.data, context);
    if (!result) {
        history->current = entry->previous;
        --history->applied;
    }
    history->busy = 0;
    return result ? XX_HISTORY_APPLY_FAILED : XX_HISTORY_OK;
}

xx_history_status xx_history_redo(xx_history *history, void *context)
{
    int result;
    xx_history_entry *entry;
    if (!history) return XX_HISTORY_INVALID_ARGUMENT;
    if (history->busy || history->pending) return XX_HISTORY_BUSY;
    entry = history->current ? history->current->next : history->first;
    if (!entry) return XX_HISTORY_EMPTY;
    history->busy = 1;
    result = entry->command.redo(entry->command.data, context);
    if (!result) {
        history->current = entry;
        ++history->applied;
    }
    history->busy = 0;
    return result ? XX_HISTORY_APPLY_FAILED : XX_HISTORY_OK;
}

xx_history_status xx_history_clear(xx_history *history)
{
    xx_history_entry *entries;
    if (!history) return XX_HISTORY_INVALID_ARGUMENT;
    if (history->busy || history->pending) return XX_HISTORY_BUSY;
    history->busy = 1;
    entries = history->first;
    history->first = NULL;
    history->last = NULL;
    history->current = NULL;
    history->count = 0;
    history->applied = 0;
    xx_history_release_chain(entries);
    history->busy = 0;
    return XX_HISTORY_OK;
}

int xx_history_can_undo(const xx_history *history)
{
    return history && !history->busy && !history->pending && history->current;
}

int xx_history_can_redo(const xx_history *history)
{
    return history && !history->busy && !history->pending && history->count > history->applied;
}

size_t xx_history_undo_count(const xx_history *history)
{
    return history ? history->applied : 0;
}

size_t xx_history_redo_count(const xx_history *history)
{
    return history ? history->count - history->applied : 0;
}

const char *xx_history_undo_label(const xx_history *history)
{
    return history && history->current ? history->current->label : NULL;
}

const char *xx_history_redo_label(const xx_history *history)
{
    const xx_history_entry *entry;
    if (!history) return NULL;
    entry = history->current ? history->current->next : history->first;
    return entry ? entry->label : NULL;
}

const xx_history_command *xx_history_undo_command(const xx_history *history)
{
    return xx_history_can_undo(history) ? &history->current->command : NULL;
}

const xx_history_command *xx_history_redo_command(const xx_history *history)
{
    const xx_history_entry *entry;
    if (!xx_history_can_redo(history)) return NULL;
    entry = history->current ? history->current->next : history->first;
    return &entry->command;
}
