/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_RTPATCH_RAW_PROGRAM_PRIVATE_H
#define XX_RTPATCH_RAW_PROGRAM_PRIVATE_H
typedef struct xx_rtp_raw_cursor_s {
    const uint8_t *data;
    size_t size, at;
} xx_rtp_raw_cursor;
typedef struct xx_rtp_raw_template_s {
    int64_t offset, size;
} xx_rtp_raw_template;

static bool xx_rtp_raw_byte(xx_rtp_raw_cursor *c, uint8_t *value)
{
    if (c->at >= c->size) return false;
    *value = c->data[c->at++];
    return true;
}

static bool xx_rtp_raw_vli(xx_rtp_raw_cursor *c, int64_t *value)
{
    uint8_t lead, tail;
    unsigned mask = 64U, count = 0U;
    uint64_t result;
    if (!xx_rtp_raw_byte(c, &lead)) return false;
    while ((lead & mask) != 0U) {
        ++count;
        mask >>= 1U;
        if (count > 4U) return false;
    }
    result = count == 0U ? lead & 63U : (uint64_t)(lead & (mask - 1U)) << (8U * count);
    for (unsigned i = 0U; i < count; ++i) {
        if (!xx_rtp_raw_byte(c, &tail)) return false;
        result |= (uint64_t)tail << (8U * i);
    }
    *value = (lead & 128U) != 0U ? -(int64_t)result : (int64_t)result;
    return true;
}

/* Validate the instruction stream without materializing or applying a target.
 * Unsupported instruction families reject instead of being length-guessed. */
static bool xx_rtpatch_raw_delta_valid(const uint8_t *data, size_t size, int64_t source_size, int64_t destination_size, xx_pd_struct *pd)
{
    xx_rtp_raw_cursor c = {data, size, 0U};
    xx_rtp_raw_template *templates = NULL;
    size_t template_count = 0U;
    int64_t write = 0, poke = 0, pending = 0;
    bool valid = false;
    if (!data || size == 0U || size > 1048576U || source_size <= 0 || destination_size <= 0) return false;
    while (c.at < c.size) {
        uint8_t op, ignored;
        int64_t a, b, n;
        unsigned width;
        if ((pd && xx_pd_is_stopped(pd)) || !xx_rtp_raw_byte(&c, &op)) goto done;
        if (op == 1U) {
            valid = c.at == c.size && pending == 0 && write == destination_size;
            goto done;
        }
        if (op == 2U) {
            if (!xx_rtp_raw_vli(&c, &a) || a != 0 || pending != 0) goto done;
            write = poke = 0;
        } else if (op == 3U || op == 4U) {
            if (op == 4U) {
                if (!xx_rtp_raw_vli(&c, &a) || a < 0 || a > destination_size - write) goto done;
                pending += a;
                write += a;
            }
            if (!xx_rtp_raw_vli(&c, &a) || !xx_rtp_raw_vli(&c, &b) || a < 0 || a > source_size || b < 0 || b > source_size - a || b > destination_size - write) goto done;
            write += b;
        } else if (op == 5U) {
            pending += destination_size - write;
            if (pending < 0 || (uint64_t)pending > c.size - c.at) goto done;
            c.at += (size_t)pending;
            pending = 0;
            write = destination_size;
        } else if (op == 8U) {
            if (!xx_rtp_raw_vli(&c, &a) || !xx_rtp_raw_vli(&c, &b) || a < 0 || a > source_size || b < 0 || b > source_size - a || template_count >= 4096U) goto done;
            if (!templates) {
                templates = (xx_rtp_raw_template *)xx_mem_alloc(4096U * sizeof(*templates));
                if (!templates) goto done;
            }
            templates[template_count].offset = a;
            templates[template_count++].size = b;
        } else if (op == 9U || op == 10U) {
            if (op == 10U) {
                if (!xx_rtp_raw_vli(&c, &a) || a < 0 || a > destination_size - write) goto done;
                pending += a;
                write += a;
            }
            if (!xx_rtp_raw_vli(&c, &a) || a < 0 || (uint64_t)a >= template_count || templates[a].size > destination_size - write) goto done;
            write += templates[a].size;
        } else if (op == 11U || op == 12U) {
            if (op == 12U) {
                if (!xx_rtp_raw_vli(&c, &a) || a < 0 || a > destination_size - write) goto done;
                pending += a;
                write += a;
            }
            if (!xx_rtp_raw_vli(&c, &a) || a < 0 || a > destination_size - write) goto done;
            write += a;
        } else if (op == 6U) {
            if (!xx_rtp_raw_vli(&c, &a) || !xx_rtp_raw_byte(&c, &ignored)) goto done;
            poke += a;
            if (poke < 0 || poke >= destination_size) goto done;
        } else if (op == 13U) {
            if (!xx_rtp_raw_vli(&c, &n) || n < 0 || n > 100000) goto done;
            poke = 0;
            for (int64_t i = 0; i < n; ++i) {
                if (!xx_rtp_raw_vli(&c, &a) || !xx_rtp_raw_byte(&c, &ignored)) goto done;
                poke += a;
                if (poke < 0 || poke >= destination_size) goto done;
            }
        } else if (op == 7U || op == 14U || op == 15U || op == 16U) {
            width = op == 15U ? 2U : op == 16U ? 4U : 1U;
            for (unsigned i = 0U; i < width; ++i)
                if (!xx_rtp_raw_byte(&c, &ignored)) goto done;
            if (!xx_rtp_raw_vli(&c, &n) || n < 0 || n > 100000) goto done;
            poke = 0;
            for (int64_t i = 0; i < n; ++i) {
                if (!xx_rtp_raw_vli(&c, &a)) goto done;
                poke += a;
                if (poke < 0 || poke > destination_size - (int64_t)width) goto done;
            }
        } else goto done;
    }
done:
    if (templates) xx_mem_free(templates);
    return valid;
}
#endif
