/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original short-header Squeeze decoder shared by native Apple archives. */
#ifndef XX_APPLE_SQUEEZE_PRIVATE_H
#define XX_APPLE_SQUEEZE_PRIVATE_H
static int32_t as_signed(const uint8_t *p)
{
    uint16_t v = xx_data_get_u16(p, 2, 0, false);
    return v & 0x8000U ? (int32_t)v - 65536 : (int32_t)v;
}
static bool as_decode(af_work *w, const uint8_t *p, size_t n, uint8_t *out, uint32_t wanted)
{
    int32_t tree[256][2];
    uint32_t count, i, used = 0;
    size_t at;
    unsigned bit = 0;
    uint8_t previous = 0;
    bool have = false, escape = false;
    if (n < 2U || (count = xx_data_get_u16(p, 2, 0, false)) > 256U || n < 2U + count * 4U) return false;
    if (!count) return wanted == 0U && n == 2U;
    for (i = 0; i < count; ++i) {
        tree[i][0] = as_signed(p + 2U + i * 4U);
        tree[i][1] = as_signed(p + 4U + i * 4U);
        if (tree[i][0] >= (int32_t)count || tree[i][0] < -257 || tree[i][1] >= (int32_t)count || tree[i][1] < -257) return false;
    }
    at = 2U + count * 4U;
    for (;;) {
        int32_t node = 0;
        uint32_t depth = 0, symbol;
        if (!af_poll(w)) return false;
        while (node >= 0) {
            unsigned direction;
            if (++depth > count || at >= n) return false;
            direction = (p[at] >> bit) & 1U;
            if (++bit == 8U) {
                bit = 0;
                ++at;
            }
            node = tree[node][direction];
        }
        /* ACU producers can leave one arbitrary flush byte after the encoded
         * EOF (including FF). It is inside the authoritative packed extent.
         * EOF, complete RLE state and exact expanded size are still mandatory. */
        symbol = (uint32_t)(-node - 1);
        if (symbol == 256U) {
            size_t consumed = at + (bit ? 1U : 0U);
            return !escape && used == wanted && (consumed == n || consumed + 1U == n);
        }
        if (escape) {
            escape = false;
            if (symbol == 0U) {
                if (used >= wanted) return false;
                out[used++] = 0x90U;
                previous = 0x90U;
                have = true;
            } else {
                uint32_t repeats = symbol - 1U;
                if (!have || repeats > wanted - used) return false;
                while (repeats--) out[used++] = previous;
            }
        } else if (symbol == 0x90U) escape = true;
        else {
            if (used >= wanted) return false;
            out[used++] = (uint8_t)symbol;
            previous = (uint8_t)symbol;
            have = true;
        }
    }
}
#endif
