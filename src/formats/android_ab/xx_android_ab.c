/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://android.googlesource.com/platform/frameworks/base/+/13ac041/services/java/com/android/server/BackupManagerService.java
 * Independently implemented bounded parser; input ownership remains with caller.
 */
#include "xxfclib/formats/android_ab/xx_android_ab.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/adler32/xx_adler32.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    (void)pd;

    uint8_t h[128];
    size_t have = (size_t)(pm_available(f) < 128 ? pm_available(f) : 128), at = 15;
    uint32_t version = 0, compressed;
    int64_t bytes;
    if (pm_available(f) < 24 || !pm_read(f, 0, h, have) || xx_rt_memcmp(h, "ANDROID BACKUP\n", 15)) return false;
    if (at >= have || h[at] < '1' || h[at] > '9') return false;
    while (at < have && h[at] >= '0' && h[at] <= '9') {
        version = version * 10 + h[at++] - '0';
        if (version > 5) return false;
    }
    if (at + 8 > have || h[at++] != '\n' || (h[at] != '0' && h[at] != '1')) return false;
    compressed = h[at++] - '0';
    if (h[at++] != '\n' || xx_rt_memcmp(h + at, "none\n", 5)) {
        return false;
    }
    at += 5;
    bytes = pm_available(f) - (int64_t)at;
    if (bytes < 6) return false;
    if (!compressed) {
        if (bytes < 1024 || bytes % 512 || !pm_add(f, s, "backup.tar", (int64_t)at, bytes)) return false;
    } else {
        uint8_t *input = NULL, *out = NULL;
        size_t cap = 65536, written = 0, consumed = 0;
        bool decoded = false;
        const xx_var *budget = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
        size_t ceiling = 256U * 1024U * 1024U;
        if (budget) {
            uint64_t limit = xx_var_get_u64(budget);
            if (limit < (uint64_t)bytes + 1024) return false;
            if (limit - (uint64_t)bytes < ceiling) ceiling = (size_t)(limit - (uint64_t)bytes);
        }
        if (bytes > 64 * 1024 * 1024 || ceiling < 1024) return false;
        input = (uint8_t *)xx_mem_alloc((size_t)bytes);
        if (!input) return false;
        if (!pm_read(f, (int64_t)at, input, (size_t)bytes) || (input[0] & 15) != 8 || input[0] >> 4 > 7 || ((uint32_t)input[0] * 256 + input[1]) % 31 ||
            (input[1] & 32)) {
            xx_mem_free(input);
            return false;
        }
        if (cap > ceiling) cap = ceiling;
        for (;;) {
            xx_io_device *destination;
            if (pd && xx_pd_is_stopped(pd)) break;
            out = (uint8_t *)xx_mem_alloc(cap);
            if (!out) break;
            destination = xx_io_mem_open(out, cap);
            if (!destination) {
                xx_mem_free(out);
                out = NULL;
                break;
            }
            consumed = 0;
            decoded = xx_deflate_unpack_memory_to_device_ex(input + 2, (size_t)bytes - 6, destination, &consumed, false, pd);
            written = (size_t)xx_io_tell(destination);
            xx_io_close(destination);
            if (decoded) {
                decoded = consumed == (size_t)bytes - 6;
                break;
            }
            xx_mem_free(out);
            out = NULL;
            if (cap == ceiling) break;
            cap = cap > ceiling / 2 ? ceiling : cap * 2;
            if (pd && xx_pd_is_stopped(pd)) break;
        }
        if (decoded) decoded = written >= 1024 && written % 512 == 0 && xx_adler32_update(1, out, written) == xx_data_get_u32(input + (size_t)bytes - 4, 4, 0, true);
        xx_mem_free(input);
        if (!decoded || !pm_add(f, s, "backup.tar", (int64_t)at, 0)) {
            if (out) xx_mem_free(out);
            return false;
        }
        s->items[0].memory = out;
        s->items[0].size = (int64_t)written;
        s->items[0].packed_size = bytes;
    }
    /* TAR terminator required; nested members are handled by the TAR reader. */
    {
        uint8_t tail[1024];
        size_t i;
        if (s->items[0].memory) xx_rt_memcpy(tail, s->items[0].memory + s->items[0].size - 1024, 1024);
        else if (!pm_read(f, pm_available(f) - 1024, tail, 1024)) return false;
        for (i = 0; i < 1024; ++i)
            if (tail[i]) return false;
    }
    s->size = pm_available(f);
    return true;
}
void xx_android_ab_init(xx_android_ab *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ANDROID_AB, "ab");
    }
}
xx_android_ab *xx_android_ab_create(xx_io_device *d, int64_t b)
{
    xx_android_ab *r = (xx_android_ab *)xx_mem_alloc(sizeof(*r));
    if (r) xx_android_ab_init(r, d, b);
    return r;
}
void xx_android_ab_destroy(xx_android_ab *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_android_ab_free(xx_android_ab *r)
{
    if (r) {
        xx_android_ab_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_android_ab_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_android_ab_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
