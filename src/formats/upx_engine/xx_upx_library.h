/* SPDX-License-Identifier: MIT
 * Optional in-process UPX bridge. Included by sevenzip_backend after its
 * monotonic clock helper; all existing iterator and extraction policy stays
 * in sevenzip_engine. */
#include "upx_bridge.h"

typedef struct upxl_context {
    xx_io_device *source, *destination;
    int64_t base, length;
    const xx_sevenzip_backend_options *options;
    xx_sevenzip_backend_status status;
    uint64_t expected, maximum, received, start;
    unsigned timeout;
    unsigned char prefix[4];
    size_t prefix_size;
    int level;
} upxl_context;

static bool upxl_cancelled(void *opaque) {
    upxl_context *context = (upxl_context *)opaque;
    if (context->options && context->options->pd && xx_pd_is_stopped(context->options->pd)) {
        context->status = XX_SEVENZIP_BACKEND_CANCELLED;
        return true;
    }
    if (sb_clock() - context->start >= context->timeout) {
        context->status = XX_SEVENZIP_BACKEND_TIMEOUT;
        return true;
    }
    return false;
}

static bool upxl_read_at(void *opaque, uint64_t offset, unsigned char *data, size_t count) {
    upxl_context *context = (upxl_context *)opaque;
    size_t done = 0;
    if (upxl_cancelled(context)) return false;
    if (offset > (uint64_t)context->length || count > (uint64_t)context->length - offset ||
        xx_io_seek64(context->source, context->base + (int64_t)offset, SEEK_SET) != 0) {
        context->status = XX_SEVENZIP_BACKEND_IO;
        return false;
    }
    while (done < count) {
        ssize_t got;
        if (upxl_cancelled(context)) return false;
        got = xx_io_read(context->source, data + done, count - done);
        if (got <= 0 || (size_t)got > count - done) {
            context->status = XX_SEVENZIP_BACKEND_IO;
            return false;
        }
        done += (size_t)got;
    }
    return true;
}

static bool upxl_write(void *opaque, const unsigned char *data, size_t count) {
    upxl_context *context = (upxl_context *)opaque;
    size_t done = 0, prefix;
    if (upxl_cancelled(context)) return false;
    if (context->received > context->maximum || count > context->maximum - context->received ||
        (context->expected != UINT64_MAX && (context->received > context->expected ||
            count > context->expected - context->received))) {
        context->status = XX_SEVENZIP_BACKEND_LIMIT;
        return false;
    }
    prefix = sizeof(context->prefix) - context->prefix_size;
    if (prefix > count) prefix = count;
    if (prefix) {
        xx_rt_memcpy(context->prefix + context->prefix_size, data, prefix);
        context->prefix_size += prefix;
    }
    while (context->destination && done < count) {
        ssize_t wrote;
        if (upxl_cancelled(context)) return false;
        wrote = xx_io_write(context->destination, data + done, count - done);
        if (wrote <= 0 || (size_t)wrote > count - done) {
            context->status = XX_SEVENZIP_BACKEND_IO;
            return false;
        }
        done += (size_t)wrote;
    }
    context->received += count;
    if (context->options)
        xx_pd_set_current(context->options->pd, context->level, context->received);
    return !upxl_cancelled(context);
}

static bool upxl_run(xx_io_device *source, int64_t base, int64_t length,
    uint32_t index, uint64_t expected, xx_io_device *destination,
    const xx_sevenzip_backend_options *options,
    xx_sevenzip_backend_entry_fn callback, void *user) {
    upxl_context context;
    int64_t total, saved = -1;
    uint64_t memory = options ? options->memory_limit : 0;
    char error[2048] = {0};
    bool success = false;
    int result;
    xx_rt_memset(&context, 0, sizeof(context));
    context.source = source; context.destination = destination;
    context.base = base; context.options = options; context.expected = expected;
    context.maximum = options ? options->max_member_size : UINT64_MAX;
    context.status = XX_SEVENZIP_BACKEND_FORMAT; context.level = -1;
    context.start = sb_clock(); context.timeout = options && options->timeout_ms ? options->timeout_ms : 60000U;
    if (!source || destination == source || index ||
        (options && options->password && options->password[0])) {
        context.status = XX_SEVENZIP_BACKEND_UNSUPPORTED;
        goto done;
    }
    total = xx_io_size(source);
    if (base < 0 || total < 0 || base > total) goto done;
    if (length < 0) length = total - base;
    if (length < 4 || length > total - base || length > XFU_UPX_MAX_INPUT ||
        (memory && memory < 262144U) || (expected != UINT64_MAX && expected > context.maximum)) {
        context.status = XX_SEVENZIP_BACKEND_LIMIT;
        goto done;
    }
    context.length = length;
    saved = xx_io_tell(source);
    if (saved < 0 || upxl_cancelled(&context)) goto done;
    if (options) context.level = xx_pd_enter_level(options->pd,
        expected == UINT64_MAX ? 0 : expected, "UPX decode");
    result = xfu_upx_transform(&context, (uint64_t)length, true, 1,
        memory, context.maximum, upxl_read_at, upxl_write, upxl_cancelled, error, sizeof(error));
    if (result || (expected != UINT64_MAX && context.received != expected)) {
        if (context.status == XX_SEVENZIP_BACKEND_FORMAT &&
            (strstr(error, "limit") || strstr(error, "workspace")))
            context.status = XX_SEVENZIP_BACKEND_LIMIT;
        goto done;
    }
    if (callback) {
        xx_sevenzip_backend_entry entry;
        xx_rt_memset(&entry, 0, sizeof(entry));
        entry.path = context.prefix_size >= 2 && context.prefix[0] == 'M' && context.prefix[1] == 'Z' ?
            "unpacked.exe" : context.prefix_size == 4 && !xx_rt_memcmp(context.prefix, "\177ELF", 4) ?
                "unpacked.elf" : "unpacked.bin";
        entry.size = context.received; entry.packed_size = (uint64_t)length;
        if (!callback(user, &entry)) { context.status = XX_SEVENZIP_BACKEND_LIMIT; goto done; }
    }
    if (options && options->detected_handler && options->detected_handler_capacity >= 4)
        xx_rt_memcpy(options->detected_handler, "UPX", 4);
    if (upxl_cancelled(&context)) goto done;
    context.status = XX_SEVENZIP_BACKEND_OK;
    success = true;
done:
    if (saved >= 0 && xx_io_seek64(source, saved, SEEK_SET) != 0) {
        context.status = XX_SEVENZIP_BACKEND_IO; success = false;
    }
    if (options) xx_pd_leave_level(options->pd, context.level);
    if (upxl_cancelled(&context)) success = false;
    if (!success && options && options->pd && !options->pd->last_error && error[0])
        xx_pd_set_error(options->pd, XXFC_ERR_GENERIC, error);
    if (options && options->status) *options->status = context.status;
    return success;
}
