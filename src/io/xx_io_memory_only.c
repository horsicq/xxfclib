/* SPDX-License-Identifier: MIT. Thread-scoped RAM-only temporary streams.
 * There is deliberately no filesystem fallback or virtual path namespace. */
#include "xx_io_policy.h"
#include "xxfclib/memory/xx_memory.h"
#include "../global/xx_tls.h"

typedef struct xx_io_policy_context {
    struct xx_io_policy_context *parent;
    uintptr_t thread_id;
    uint64_t limit, used, streams;
    size_t refs;
    uint32_t error;
    bool active;
} xx_io_policy_context;
typedef struct xx_io_ram_temp {
    xx_io_policy_context *owner;
    uint8_t *data;
    size_t capacity, size, position;
} xx_io_ram_temp;
/* The calling thread's innermost scope, in a thread slot rather than a
 * thread-local variable (see xx_tls.h). */
static xx_tls_key xx_io_policy_key;
static xx_io_policy_context *xx_io_policy_get(void)
{
    return (xx_io_policy_context *)xx_tls_get(&xx_io_policy_key);
}

static void xx_io_policy_fail(xx_io_policy_context *context, uint32_t error)
{
    for (; context; context = context->parent)
        if (!context->error) context->error = error;
}
static void xx_io_policy_release(xx_io_policy_context *context)
{
    while (context && --context->refs == 0U) {
        xx_io_policy_context *parent = context->parent;
        xx_mem_free(context);
        context = parent;
    }
}
bool xx_io_memory_only_begin(xx_io_memory_only_scope *scope, uint64_t limit)
{
    xx_io_policy_context *context, *current = xx_io_policy_get();
    if (!scope || scope->internal) return false;
    scope->error = XX_IO_MEMORY_ONLY_OK;
    context = (xx_io_policy_context *)xx_mem_calloc(1U, sizeof(*context));
    if (!context || !xx_tls_set(&xx_io_policy_key, context)) {
        xx_mem_free(context);
        scope->error = XX_IO_MEMORY_ONLY_ALLOCATION;
        xx_io_policy_fail(current, scope->error);
        return false;
    }
    context->parent = current;
    context->thread_id = xx_tls_thread_id();
    context->limit = limit;
    context->refs = 1U;
    context->active = true;
    if (context->parent) ++context->parent->refs;
    scope->internal = context;
    return true;
}
bool xx_io_memory_only_end(xx_io_memory_only_scope *scope)
{
    xx_io_policy_context *context, *current = xx_io_policy_get();
    if (!scope || !scope->internal) return false;
    context = (xx_io_policy_context *)scope->internal;
    if (context != current) {
        xx_io_policy_fail(current, XX_IO_MEMORY_ONLY_SCOPE_ORDER);
        scope->error = XX_IO_MEMORY_ONLY_SCOPE_ORDER;
        return false;
    }
    if (context->streams) xx_io_policy_fail(context, XX_IO_MEMORY_ONLY_LIVE_TEMP);
    scope->error = context->error;
    context->active = false;
    xx_tls_set(&xx_io_policy_key, context->parent);
    scope->internal = NULL;
    xx_io_policy_release(context);
    return scope->error == XX_IO_MEMORY_ONLY_OK;
}
bool xx_io_memory_only_active(void)
{
    return xx_io_policy_get() != NULL;
}
uint64_t xx_io_memory_only_used(void)
{
    xx_io_policy_context *current = xx_io_policy_get();
    return current ? current->used : 0U;
}
xx_io_memory_only_error_t xx_io_memory_only_error(const xx_io_memory_only_scope *scope)
{
    if (!scope) return XX_IO_MEMORY_ONLY_SCOPE_ORDER;
    return (xx_io_memory_only_error_t)(scope->internal ? ((xx_io_policy_context *)scope->internal)->error : scope->error);
}
bool xx_io_policy_mutation_allowed(void)
{
    xx_io_policy_context *current = xx_io_policy_get();
    if (!current) return true;
    xx_io_policy_fail(current, XX_IO_MEMORY_ONLY_DISK_WRITE);
    return false;
}
bool xx_io_policy_file_open_allowed(const char *mode)
{
    const char *p;
    if (!xx_io_policy_get()) return true;
    if (mode && mode[0] == 'r') {
        for (p = mode + 1; *p; ++p)
            if (*p != 'b' && *p != 't' && *p != 'e') break;
        if (!*p) return true;
    }
    return xx_io_policy_mutation_allowed();
}

static int xx_io_ram_close(xx_io_device *device);
static xx_io_ram_temp *xx_io_ram_state(xx_io_device *device)
{
    return device && device->close == xx_io_ram_close ? (xx_io_ram_temp *)device->priv : NULL;
}
static bool xx_io_ram_live(xx_io_ram_temp *temp)
{
    xx_io_policy_context *context;
    if (!temp || !temp->owner || temp->owner->thread_id != xx_tls_thread_id() || !temp->owner->active) return false;
    for (context = xx_io_policy_get(); context; context = context->parent)
        if (context == temp->owner) return true;
    return false;
}
static bool xx_io_ram_grow(xx_io_ram_temp *temp, size_t needed)
{
    xx_io_policy_context *context;
    uint64_t available = UINT64_MAX, addition;
    size_t target;
    uint8_t *data;
    if (needed <= temp->capacity) return true;
    for (context = temp->owner; context; context = context->parent) {
        uint64_t remaining = context->limit - context->used;
        if (remaining < available) available = remaining;
    }
    addition = (uint64_t)(needed - temp->capacity);
    if (addition > available) {
        xx_io_policy_fail(temp->owner, XX_IO_MEMORY_ONLY_LIMIT);
        return false;
    }
    target = temp->capacity < 65536U ? 65536U : temp->capacity;
    while (target < needed) {
        if (target > SIZE_MAX / 2U) {
            target = needed;
            break;
        }
        target *= 2U;
    }
    if ((uint64_t)(target - temp->capacity) > available || (uint64_t)target > INT64_MAX) target = needed;
    data = (uint8_t *)xx_mem_realloc(temp->data, target);
    if (!data) {
        xx_io_policy_fail(temp->owner, XX_IO_MEMORY_ONLY_ALLOCATION);
        return false;
    }
    addition = (uint64_t)(target - temp->capacity);
    for (context = temp->owner; context; context = context->parent) context->used += addition;
    temp->data = data;
    temp->capacity = target;
    return true;
}
static ssize_t xx_io_ram_read(xx_io_device *device, void *buffer, size_t n)
{
    xx_io_ram_temp *temp = xx_io_ram_state(device);
    if (!xx_io_ram_live(temp) || (n && !buffer)) return -1;
    if (!n || temp->position >= temp->size) return 0;
    if (n > temp->size - temp->position) n = temp->size - temp->position;
    if (n > (size_t)PTRDIFF_MAX) n = (size_t)PTRDIFF_MAX;
    xx_mem_copy(buffer, temp->data + temp->position, n);
    temp->position += n;
    return (ssize_t)n;
}
static ssize_t xx_io_ram_write(xx_io_device *device, const void *buffer, size_t n)
{
    xx_io_ram_temp *temp = xx_io_ram_state(device);
    size_t end;
    if (!xx_io_ram_live(temp) || (n && !buffer)) return -1;
    if (!n) return 0;
    /* An inner policy must never allocate or mutate an outer-owned stream
     * without charging the inner ceiling. Keep reads/seeks usable for source
     * streams, but require writes to belong to the innermost active scope. */
    if (temp->owner != xx_io_policy_get()) {
        xx_io_policy_fail(xx_io_policy_get(), XX_IO_MEMORY_ONLY_SCOPE_ORDER);
        return -1;
    }
    if (n > (size_t)PTRDIFF_MAX || n > SIZE_MAX - temp->position || (uint64_t)n > INT64_MAX - (uint64_t)temp->position) {
        xx_io_policy_fail(temp->owner, XX_IO_MEMORY_ONLY_LIMIT);
        return -1;
    }
    end = temp->position + n;
    if (!xx_io_ram_grow(temp, end)) return -1;
    if (temp->position > temp->size) xx_mem_zero(temp->data + temp->size, temp->position - temp->size);
    xx_mem_copy(temp->data + temp->position, buffer, n);
    temp->position = end;
    if (end > temp->size) {
        temp->size = end;
    }
    return (ssize_t)n;
}
static int xx_io_ram_seek64(xx_io_device *device, int64_t offset, int whence)
{
    xx_io_ram_temp *temp = xx_io_ram_state(device);
    int64_t base, target;
    if (!xx_io_ram_live(temp)) return -1;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = (int64_t)temp->position;
    else if (whence == SEEK_END) base = (int64_t)temp->size;
    else return -1;
    if (offset < -base || offset > INT64_MAX - base) return -1;
    target = base + offset;
    if ((uint64_t)target > SIZE_MAX) return -1;
    temp->position = (size_t)target;
    return 0;
}
static int xx_io_ram_seek(xx_io_device *device, long offset, int whence)
{
    return xx_io_ram_seek64(device, (int64_t)offset, whence);
}
static int64_t xx_io_ram_tell(xx_io_device *device)
{
    xx_io_ram_temp *temp = xx_io_ram_state(device);
    return xx_io_ram_live(temp) ? (int64_t)temp->position : -1;
}
static int64_t xx_io_ram_size(xx_io_device *device)
{
    xx_io_ram_temp *temp = xx_io_ram_state(device);
    return xx_io_ram_live(temp) ? (int64_t)temp->size : -1;
}
static int xx_io_ram_close(xx_io_device *device)
{
    xx_io_ram_temp *temp = xx_io_ram_state(device);
    xx_io_policy_context *context;
    if (!temp || !temp->owner || temp->owner->thread_id != xx_tls_thread_id()) return -1;
    for (context = temp->owner; context; context = context->parent) {
        context->used -= temp->capacity;
        --context->streams;
    }
    if (temp->data) {
        xx_mem_zero(temp->data, temp->capacity);
        xx_mem_free(temp->data);
    }
    xx_io_policy_release(temp->owner);
    xx_mem_free(temp);
    xx_mem_free(device);
    return 0;
}
xx_io_device *xx_io_memory_temp_open(void)
{
    xx_io_device *device;
    xx_io_ram_temp *temp;
    xx_io_policy_context *context = xx_io_policy_get();
    if (!context) return NULL;
    device = (xx_io_device *)xx_mem_calloc(1U, sizeof(*device));
    temp = (xx_io_ram_temp *)xx_mem_calloc(1U, sizeof(*temp));
    if (!device || !temp) {
        xx_mem_free(device);
        xx_mem_free(temp);
        xx_io_policy_fail(context, XX_IO_MEMORY_ONLY_ALLOCATION);
        return NULL;
    }
    temp->owner = context;
    ++context->refs;
    for (; context; context = context->parent) ++context->streams;
    device->priv = temp;
    device->read = xx_io_ram_read;
    device->write = xx_io_ram_write;
    device->seek = xx_io_ram_seek;
    device->seek64 = xx_io_ram_seek64;
    device->tell = xx_io_ram_tell;
    device->close = xx_io_ram_close;
    device->size = device->get_total_size = device->total_size = xx_io_ram_size;
    return device;
}
bool xx_io_memory_temp_is_device(const xx_io_device *device)
{
    return device && device->priv && device->close == xx_io_ram_close;
}
