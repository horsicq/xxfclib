/* Independent standard transforms. SPDX-License-Identifier: MIT. */
#include "dgca_transform.h"
#include <string.h>

static int dt_cancel(int (*cancel)(void *), void *opaque)
{
    return cancel && cancel(opaque);
}
static int dt_overlap(const unsigned char *a, const unsigned char *b, size_t n)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? (uint64_t)(y - x) < n : (uint64_t)(x - y) < n;
}
int dg_inverse_bwt(const unsigned char *last, unsigned char *out, size_t count, uint32_t primary, uint32_t *links, int (*cancel)(void *), void *opaque)
{
    uint32_t starts[256] = {0}, sum = 0, pos;
    size_t i;
    unsigned c;
    if (dt_cancel(cancel, opaque)) return -1;
    if (!count) return primary ? 0 : 1;
    if (!last || !out || !links || count > UINT32_MAX || primary >= count || dt_overlap(last, out, count)) return 0;
    for (i = 0; i < count; ++i) {
        if (!(i & 4095) && dt_cancel(cancel, opaque)) return -1;
        ++starts[last[i]];
    }
    for (c = 0; c < 256; ++c) {
        uint32_t n = starts[c];
        starts[c] = sum;
        sum += n;
    }
    for (i = 0; i < count; ++i) {
        if (!(i & 4095) && dt_cancel(cancel, opaque)) return -1;
        links[starts[last[i]]++] = (uint32_t)i;
    }
    pos = primary;
    for (i = 0; i < count; ++i) {
        if (!(i & 4095) && dt_cancel(cancel, opaque)) return -1;
        pos = links[pos];
        out[i] = last[pos];
    }
    return 1;
}
int dg_deinterleave(const unsigned char *planes, unsigned char *out, size_t count, uint32_t stride, int (*cancel)(void *), void *opaque)
{
    size_t plane, offset = 0, chunk = 0;
    if (dt_cancel(cancel, opaque)) return -1;
    if (!stride || count > UINT32_MAX) return 0;
    if (!count) return 1;
    if (!planes || !out || dt_overlap(planes, out, count)) return 0;
    for (plane = 0; plane < stride && plane < count; ++plane) {
        size_t pos;
        for (pos = plane; pos < count;) {
            if (!(chunk++ & 4095) && dt_cancel(cancel, opaque)) {
                return -1;
            }
            out[pos] = planes[offset++];
            if (count - 1 - pos < stride) {
                break;
            }
            pos += stride;
        }
    }
    return offset == count ? 1 : 0;
}
