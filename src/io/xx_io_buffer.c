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

/** @file xx_io_buffer.c @brief Aligned read cache with adaptive whole-file loading. */
#include "xxfclib/io/xx_io.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include "xx_io_buffer.h"
#include "../global/xx_tls.h"

static xx_tls_key xx_io_alloc_failure_key;

void xx_io_note_alloc_failure(void)
{
    uintptr_t count = (uintptr_t)xx_tls_get(&xx_io_alloc_failure_key);
    (void)xx_tls_set(&xx_io_alloc_failure_key, (void *)(count + 1U));
}

uintptr_t xx_io_alloc_failures(void)
{
    return (uintptr_t)xx_tls_get(&xx_io_alloc_failure_key);
}

size_t xx_io_buffer_size_for(size_t capacity, uint64_t need)
{
    if (!capacity) capacity = xx_get_file_buffer_size();
    if (!capacity) capacity = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (need < (uint64_t)capacity) capacity = (size_t)need;
    return capacity ? capacity : 1U;
}

struct xx_io_buffer_state {
    xx_io_device device;
    xx_io_device *source;
    xx_io_device *host;
    unsigned char *buffer;
    const unsigned char *resident_data;
    size_t capacity, valid;
    int64_t size, position, base;
    unsigned char previous_tail[8];
    size_t previous_valid;
    int64_t previous_base;
    bool own_source, failed, resident;
    uint64_t streamed, memory_limit;
    /* Keep the window allocation alive after promotion for borrowed views. */
    unsigned char *window_buffer;
    unsigned char *scratch;
    size_t scratch_capacity;
    bool loaded, load_failed;
};

static int xx_io_buffer_close_cb(xx_io_device *device);

static xx_io_buffer_state *xx_io_buffer_state_of(const xx_io_device *device)
{
    if (!device || !device->buffer_state) return NULL;
    if (device->close != xx_io_buffer_close_cb && device->buffer_state->host != device) return NULL;
    return device->buffer_state;
}

/* Attached caches preserve the original device and callbacks. Call its
 * backend directly here to avoid routing source access through this cache. */
static ssize_t xx_io_buffer_source_read(xx_io_buffer_state *state, void *data, size_t count)
{
    if (state->host) return state->source->read(state->source, data, count);
    return xx_io_read(state->source, data, count);
}

static int xx_io_buffer_source_seek(xx_io_buffer_state *state, int64_t offset)
{
    xx_io_device *source = state->source;
    if (!state->host) return xx_io_seek64(source, offset, SEEK_SET);
    if (source->seek64) return source->seek64(source, offset, SEEK_SET);
    if (offset < LONG_MIN || offset > LONG_MAX) return -1;
    return source->seek ? source->seek(source, (long)offset, SEEK_SET) : -1;
}

static bool xx_io_buffer_enabled(const xx_io_buffer_state *state)
{
    return state->host ? state->host->buffer_enabled : state->device.buffer_enabled;
}

static void xx_io_buffer_clear(xx_io_buffer_state *state)
{
    xx_mem_free(state->buffer);
    xx_mem_free(state->window_buffer);
    xx_mem_free(state->scratch);
    state->buffer = NULL;
    state->window_buffer = NULL;
    state->scratch = NULL;
    state->scratch_capacity = 0;
    state->loaded = false;
    state->valid = state->resident ? (size_t)state->size : 0;
    state->base = state->resident ? 0 : -1;
    state->previous_valid = 0;
    state->streamed = 0;
    state->load_failed = false;
}

static bool xx_io_buffer_load_whole(xx_io_buffer_state *state, bool required)
{
    unsigned char *whole;
    size_t size, done = 0;
    if (state->loaded) return true;
    if ((uint64_t)state->size >= (uint64_t)SIZE_MAX) return false;
    size = (size_t)state->size;
    whole = (unsigned char *)xx_mem_alloc(size + 1);
    if (!whole && required) xx_io_note_alloc_failure();
    if (!whole || xx_io_buffer_source_seek(state, 0) != 0) {
        xx_mem_free(whole);
        return false;
    }
    while (done < size) {
        size_t request = size - done;
        ssize_t got;
        if (request > state->capacity) request = state->capacity;
        if (request > (size_t)PTRDIFF_MAX) request = (size_t)PTRDIFF_MAX;
        got = xx_io_buffer_source_read(state, whole + done, request);
        if (got <= 0 || (size_t)got > request) {
            xx_mem_free(whole);
            return false;
        }
        done += (size_t)got;
    }
    whole[size] = 0;
    state->window_buffer = state->buffer;
    state->buffer = whole;
    state->base = 0;
    state->valid = size;
    state->previous_valid = 0;
    state->loaded = true;
    return true;
}

/* Loading the whole file becomes worthwhile when repeated source transfers
 * plus an upcoming transfer reach its size. This allocation/read is optional;
 * a refusal leaves the existing cache intact and does not poison reads. */
static bool xx_io_buffer_load(xx_io_buffer_state *state, uint64_t upcoming)
{
    if (state->loaded) return true;
    if (!xx_io_buffer_enabled(state) || state->resident || state->load_failed || state->failed || !state->memory_limit) return false;
    if ((uint64_t)state->size > state->memory_limit) return false;
    if (state->streamed < (uint64_t)state->size && upcoming < (uint64_t)state->size - state->streamed) return false;
    if (xx_io_buffer_load_whole(state, false)) return true;
    state->load_failed = true;
    return false;
}

static bool xx_io_buffer_fill(xx_io_buffer_state *state, int64_t offset)
{
    int64_t base;
    size_t wanted, done = 0;
    if (state->failed || offset < 0 || offset >= state->size) return false;
    if (state->base >= 0 && offset >= state->base && (uint64_t)(offset - state->base) < state->valid) return true;
    if (xx_io_buffer_load(state, (uint64_t)state->capacity)) return true;
    base = offset - (int64_t)((uint64_t)offset % state->capacity);
    wanted = state->capacity;
    if ((uint64_t)(state->size - base) < wanted) wanted = (size_t)(state->size - base);
    /* A window never holds more than the source: [base, base + wanted) lies
     * within it, and a resize frees the window before the size changes. */
    if (!state->buffer) {
        state->buffer = (unsigned char *)xx_mem_alloc(xx_io_buffer_size_for(state->capacity, (uint64_t)state->size));
        if (!state->buffer) xx_io_note_alloc_failure();
    }
    /* Keep a small prior tail for repeated searches across block boundaries. */
    if (state->valid) {
        size_t tail_capacity = state->capacity < sizeof(state->previous_tail) ? state->capacity : sizeof(state->previous_tail);
        state->previous_valid = state->valid < tail_capacity ? state->valid : tail_capacity;
        state->previous_base = state->base + (int64_t)(state->valid - state->previous_valid);
        xx_mem_copy(state->previous_tail, state->buffer + state->valid - state->previous_valid, state->previous_valid);
    }
    state->valid = 0;
    if (!state->buffer || xx_io_buffer_source_seek(state, base) != 0) {
        state->failed = true;
        return false;
    }
    while (done < wanted) {
        size_t request = wanted - done;
        ssize_t got;
        if (request > state->capacity) request = state->capacity;
        if (request > (size_t)PTRDIFF_MAX) request = (size_t)PTRDIFF_MAX;
        got = xx_io_buffer_source_read(state, state->buffer + done, request);
        if (got <= 0 || (size_t)got > request) {
            state->failed = true;
            return false;
        }
        done += (size_t)got;
    }
    state->base = base;
    state->valid = done;
    /* Saturate so repeated scans cannot wrap the promotion accounting. */
    state->streamed = UINT64_MAX - state->streamed < done ? UINT64_MAX : state->streamed + done;
    return true;
}

static const unsigned char *xx_io_buffer_fresh_view(xx_io_buffer_state *state, int64_t offset, size_t *count)
{
    size_t wanted = *count, done = 0;
    unsigned char *scratch;
    *count = 0;
    if (wanted > state->capacity) wanted = state->capacity;
    if ((uint64_t)wanted > (uint64_t)(state->size - offset)) wanted = (size_t)(state->size - offset);
    if (wanted > state->scratch_capacity) {
        scratch = (unsigned char *)xx_mem_alloc(wanted);
        if (!scratch) {
            xx_io_note_alloc_failure();
            state->failed = true;
            return NULL;
        }
        xx_mem_free(state->scratch);
        state->scratch = scratch;
        state->scratch_capacity = wanted;
    }
    if (xx_io_buffer_source_seek(state, offset) != 0) {
        state->failed = true;
        return NULL;
    }
    while (done < wanted) {
        size_t request = wanted - done;
        ssize_t got;
        if (request > (size_t)PTRDIFF_MAX) request = (size_t)PTRDIFF_MAX;
        got = xx_io_buffer_source_read(state, state->scratch + done, request);
        if (got <= 0 || (size_t)got > request) {
            state->failed = true;
            return NULL;
        }
        done += (size_t)got;
    }
    *count = done;
    return state->scratch;
}

const unsigned char *xx_io_buffer_view(xx_io_device *device, int64_t offset, size_t *count)
{
    xx_io_buffer_state *state = xx_io_buffer_state_of(device);
    const unsigned char *view;
    size_t wanted, within, available;
    if (!count) return NULL;
    wanted = *count;
    *count = 0;
    if (!state || !wanted || state->failed || offset < 0 || offset >= state->size) return NULL;
    if (!device->buffer_enabled) {
        *count = wanted;
        return xx_io_buffer_fresh_view(state, offset, count);
    }
    if (state->base >= 0 && offset >= state->base && (uint64_t)(offset - state->base) < state->valid) {
        within = (size_t)(offset - state->base);
        view = (state->resident ? state->resident_data : state->buffer) + within;
        available = state->valid - within;
    } else if (offset >= state->previous_base && (uint64_t)(offset - state->previous_base) < state->previous_valid) {
        within = (size_t)(offset - state->previous_base);
        view = state->previous_tail + within;
        available = state->previous_valid - within;
    } else {
        if (!xx_io_buffer_fill(state, offset)) return NULL;
        within = (size_t)(offset - state->base);
        view = state->buffer + within;
        available = state->valid - within;
    }
    *count = wanted < available ? wanted : available;
    return view;
}

static ssize_t xx_io_buffer_read_cb(xx_io_device *device, void *data, size_t count)
{
    xx_io_buffer_state *state = xx_io_buffer_state_of(device);
    size_t done = 0;
    if (!state) return -1;
    if (!count) return 0;
    if (!data || state->failed) return -1;
    /* Shrinking an attached device preserves its logical cursor, which can
     * consequently remain beyond EOF. Avoid subtracting it from the size. */
    if (state->position >= state->size) return 0;
    if (count > (size_t)PTRDIFF_MAX) count = (size_t)PTRDIFF_MAX;
    if ((uint64_t)(state->size - state->position) < count) count = (size_t)(state->size - state->position);
    if (!device->buffer_enabled && count) {
        if (xx_io_buffer_source_seek(state, state->position) != 0) {
            state->failed = true;
            return -1;
        }
        while (done < count) {
            ssize_t got = xx_io_buffer_source_read(state, (unsigned char *)data + done, count - done);
            if (got <= 0 || (size_t)got > count - done) {
                state->failed = true;
                return done ? (ssize_t)done : -1;
            }
            state->position += (int64_t)got;
            done += (size_t)got;
        }
        return (ssize_t)done;
    }
    while (done < count) {
        size_t chunk = count - done;
        const unsigned char *view;
        if (chunk > state->capacity) chunk = state->capacity;
        view = xx_io_buffer_view(device, state->position, &chunk);
        if (!view) return done ? (ssize_t)done : -1;
        xx_mem_copy((unsigned char *)data + done, view, chunk);
        state->position += (int64_t)chunk;
        done += chunk;
    }
    return (ssize_t)done;
}

static int xx_io_buffer_seek64_cb(xx_io_device *device, int64_t offset, int whence)
{
    xx_io_buffer_state *state = xx_io_buffer_state_of(device);
    int64_t base;
    if (!state) return -1;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = state->position;
    else if (whence == SEEK_END) base = state->size;
    else return -1;
    if (offset < -base || offset > INT64_MAX - base) return -1;
    /* Read-only wrappers retain their bounded cursor. Resizable attached
     * devices have the underlying file's ability to seek beyond EOF. */
    if ((!state->host || !state->host->resize) && base + offset > state->size) return -1;
    state->position = base + offset;
    return 0;
}

static int xx_io_buffer_seek_cb(xx_io_device *device, long offset, int whence)
{
    return xx_io_buffer_seek64_cb(device, (int64_t)offset, whence);
}

static int64_t xx_io_buffer_size_cb(xx_io_device *device)
{
    xx_io_buffer_state *state = xx_io_buffer_state_of(device);
    return state ? state->size : -1;
}

static int64_t xx_io_buffer_tell_cb(xx_io_device *device)
{
    xx_io_buffer_state *state = xx_io_buffer_state_of(device);
    return state ? state->position : -1;
}

static int xx_io_buffer_close_cb(xx_io_device *device)
{
    xx_io_buffer_state *state = xx_io_buffer_state_of(device);
    int result;
    if (!state) return -1;
    if (state->host) {
        device->buffer_state = NULL;
        device->buffer_enabled = false;
        xx_io_buffer_clear(state);
        xx_mem_free(state);
        return device->close ? device->close(device) : -1;
    }
    result = state->own_source ? xx_io_close(state->source) : 0;
    xx_io_buffer_clear(state);
    xx_mem_free(state);
    return result;
}

xx_io_device *xx_io_buffer_open(xx_io_device *source, size_t capacity, uint64_t memory_limit, bool take_ownership)
{
    xx_io_buffer_state *state;
    int64_t size;
    if (!source || !source->read || (!source->seek64 && !source->seek) || (size = xx_io_total_size(source)) < 0) return NULL;
    if (!capacity) capacity = xx_get_file_buffer_size();
    if (!capacity) capacity = XX_DEFAULT_FILE_BUFFER_SIZE;
    state = (xx_io_buffer_state *)xx_mem_calloc(1, sizeof(*state));
    if (!state) {
        xx_io_note_alloc_failure();
        return NULL;
    }
    state->source = source;
    state->size = size;
    state->capacity = capacity;
    state->base = -1;
    state->own_source = take_ownership;
    state->memory_limit = xx_io_is_memory(source) ? 0 : memory_limit;
    state->device.read = xx_io_buffer_read_cb;
    state->device.seek = xx_io_buffer_seek_cb;
    state->device.seek64 = xx_io_buffer_seek64_cb;
    state->device.tell = xx_io_buffer_tell_cb;
    state->device.total_size = xx_io_buffer_size_cb;
    state->device.get_total_size = xx_io_buffer_size_cb;
    state->device.size = xx_io_buffer_size_cb;
    state->device.close = xx_io_buffer_close_cb;
    state->device.buffer_state = state;
    state->device.buffer_enabled = true;
    return &state->device;
}

xx_io_device *xx_io_buffer_open_auto(xx_io_device *source, size_t capacity, uint64_t memory_limit, bool take_ownership)
{
    xx_io_device *device = xx_io_buffer_open(source, capacity, memory_limit, false);
    xx_io_buffer_state *state;
    if (!device) return NULL;
    state = xx_io_buffer_state_of(device);
    if ((uint64_t)state->size < (uint64_t)state->capacity && !xx_io_buffer_load_whole(state, true)) {
        xx_io_close(device);
        return NULL;
    }
    state->own_source = take_ownership;
    return device;
}

xx_io_device *xx_io_buffer_open_memory(const void *data, size_t size, size_t capacity)
{
    xx_io_device *source, *device;
    xx_io_buffer_state *state;
    if ((!data && size) || (uint64_t)size > INT64_MAX) return NULL;
    source = xx_io_mem_open_ro(data, size);
    if (!source) return NULL;
    device = xx_io_buffer_open(source, capacity, 0, true);
    if (!device) {
        xx_io_close(source);
        return NULL;
    }
    state = xx_io_buffer_state_of(device);
    state->resident_data = (const unsigned char *)data;
    state->resident = true;
    state->valid = size;
    state->base = 0;
    return device;
}

size_t xx_io_buffer_capacity(const xx_io_device *device)
{
    xx_io_buffer_state *state = xx_io_buffer_state_of(device);
    return state ? state->capacity : 0;
}

uint64_t xx_io_buffer_search_access(const xx_io_device *device, int64_t range)
{
    xx_io_buffer_state *state = xx_io_buffer_state_of(device);
    size_t capacity = state ? state->capacity : xx_get_file_buffer_size();
    uint64_t available;
    if (range <= 0) return 0;
    if (!capacity) capacity = XX_DEFAULT_FILE_BUFFER_SIZE;
    available = (uint64_t)range;
    return available < (uint64_t)capacity ? available : (uint64_t)capacity;
}

bool xx_io_buffer_failed(const xx_io_device *device)
{
    xx_io_buffer_state *state = xx_io_buffer_state_of(device);
    return state ? state->failed : false;
}

xx_io_device *xx_io_buffer_source(const xx_io_device *device)
{
    xx_io_buffer_state *state = xx_io_buffer_state_of(device);
    return state && !state->host ? state->source : NULL;
}

const unsigned char *xx_io_buffer_whole(xx_io_device *device, uint64_t upcoming)
{
    xx_io_buffer_state *state = xx_io_buffer_state_of(device);
    if (!state || state->failed || !device->buffer_enabled) return NULL;
    if (state->resident) return state->resident_data;
    if (!state->loaded && upcoming) xx_io_buffer_load(state, upcoming);
    return state->loaded ? state->buffer : NULL;
}

bool xx_io_set_buffer_enabled(xx_io_device *device, bool enabled)
{
    xx_io_buffer_state *state;
    xx_io_device *wrapper;
    int64_t position, size;
    if (!device) return false;
    state = xx_io_buffer_state_of(device);
    if (state) {
        if (device->buffer_enabled == enabled) return true;
        if (state->host && !enabled) {
            if (xx_io_buffer_source_seek(state, state->position) != 0) return false;
            device->buffer_state = NULL;
            device->buffer_enabled = false;
            xx_io_buffer_clear(state);
            xx_mem_free(state);
            return true;
        }
        xx_io_buffer_clear(state);
        device->buffer_enabled = enabled;
        return true;
    }
    if (!enabled) {
        device->buffer_enabled = false;
        return true;
    }
    if (!device->read || (!device->seek64 && !device->seek) || !device->tell) return false;
    size = xx_io_total_size(device);
    position = device->tell(device);
    if (size < 0 || position < 0 || (position > size && !device->resize)) return false;
    wrapper = xx_io_buffer_open(device, 0, xx_get_file_memory_limit(), false);
    if (!wrapper) return false;
    state = wrapper->buffer_state;
    state->host = device;
    state->position = position;
    device->buffer_state = state;
    device->buffer_enabled = true;
    return true;
}

ssize_t xx_io_buffer_read_dispatch(xx_io_device *device, void *buffer, size_t size)
{
    return xx_io_buffer_read_cb(device, buffer, size);
}

ssize_t xx_io_buffer_write_dispatch(xx_io_device *device, const void *buffer, size_t size)
{
    xx_io_buffer_state *state = xx_io_buffer_state_of(device);
    if (!state || !state->host || !device->write) return -1;
    if (size && !buffer) return -1;
    if (!xx_io_set_buffer_enabled(device, false)) return -1;
    return device->write(device, buffer, size);
}

int xx_io_buffer_seek_dispatch(xx_io_device *device, int64_t offset, int whence)
{
    return xx_io_buffer_seek64_cb(device, offset, whence);
}

int64_t xx_io_buffer_tell_dispatch(xx_io_device *device)
{
    return xx_io_buffer_tell_cb(device);
}

int xx_io_buffer_resize_dispatch(xx_io_device *device, int64_t new_size)
{
    xx_io_buffer_state *state = xx_io_buffer_state_of(device);
    int64_t physical_position;
    int result;
    bool enabled;
    if (!state || !state->host || !device->resize || new_size < 0) return -1;

    /* Temporarily expose the original device to its callbacks. In particular,
     * file callbacks may route direct resize calls here while a cache exists;
     * removing the attachment prevents that forwarding from recursing. */
    enabled = device->buffer_enabled;
    device->buffer_state = NULL;
    device->buffer_enabled = false;
    physical_position = device->tell ? device->tell(device) : -1;
    if (physical_position < 0) {
        device->buffer_state = state;
        device->buffer_enabled = enabled;
        return -1;
    }
    if (physical_position != state->position && xx_io_buffer_source_seek(state, state->position) != 0) {
        (void)xx_io_buffer_source_seek(state, physical_position);
        device->buffer_state = state;
        device->buffer_enabled = enabled;
        return -1;
    }
    result = device->resize(device, new_size);
    if (result != 0) {
        /* A refused resize leaves the logical cursor and every cached byte
         * intact, including an already promoted whole-file cache. */
        (void)xx_io_buffer_source_seek(state, physical_position);
    } else {
        state->size = new_size;
        xx_io_buffer_clear(state);
        state->failed = false;
    }
    device->buffer_state = state;
    device->buffer_enabled = enabled;
    return result;
}

int xx_io_buffer_close_dispatch(xx_io_device *device)
{
    return xx_io_buffer_close_cb(device);
}
