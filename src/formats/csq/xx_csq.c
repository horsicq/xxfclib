/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/csq/xx_csq.h"
#include "../xx_bounded_deflate_members.h"
#include "../xx_format_abstract_extractor_adapter.h"
static bool csq_csq_lzss(const uint8_t *src, size_t packed, uint8_t *dst, size_t expected, xx_pd_struct *pd)
{
    uint8_t history[4096];
    unsigned flags = 0, position = 0;
    size_t at = 0, out = 0;
    xx_mem_zero(history, sizeof(history));
    while (at < packed && out < expected) {
        unsigned token;
        if (pd && xx_pd_is_stopped(pd)) return false;
        flags >>= 1;
        if (!(flags & 256U)) {
            flags = src[at++] | 0xff00U;
            if (at == packed) return false;
        }
        token = src[at++];
        if (flags & 1U) {
            dst[out++] = history[position] = (uint8_t)token;
            position = (position + 1U) & 4095U;
        } else {
            unsigned high, offset, length, j;
            if (at == packed) return false;
            high = src[at++];
            offset = (((high & 240U) << 4) | token) + 18U;
            length = (high & 15U) + 3U;
            if (length > expected - out) return false;
            for (j = 0; j < length; ++j) {
                uint8_t v = history[(offset + j) & 4095U];
                dst[out++] = history[position] = v;
                position = (position + 1U) & 4095U;
            }
        }
    }
    /* An exhausted eight-token group may be followed by an unused zero
     * control byte. TiGGER's writer emits it for exact group boundaries. */
    return out == expected && (at == packed || (at + 1U == packed && src[at] == 0 && !((flags >> 1) & 256U)));
}

static bool csq_csq_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[9], e[26];
    unsigned i, count;
    int64_t size = pm_available(f);
    uint64_t retained = 0;
    if (size < 9 || !pm_read(f, 0, h, 9) || xx_mem_compare(h, "TiGGER\1\7", 8) || !(count = h[8]) || 9U + count * 26U > (uint64_t)size) return false;
    for (i = 0; i < count; ++i) {
        uint32_t at, packed, plain;
        char name[13];
        unsigned n;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, 9 + (int64_t)i * 26, e, 26)) return false;
        at = bdm_u32(e);
        packed = bdm_u32(e + 4);
        plain = bdm_u32(e + 8);
        n = e[13];
        if (!n || n > 12U || e[12] > 1U || at < 9U + count * 26U || (uint64_t)at + packed > (uint64_t)size || plain > BDM_MEMORY_LIMIT || (e[12] == 0 && packed != plain))
            return false;
        xx_mem_copy(name, e + 14, n);
        name[n] = 0;
        if (!bdm_add(f, s, name, at, packed)) return false;
        s->items[s->count - 1U].size = plain;
        {
            uint8_t *input, *output;
            bool okay;
            uint32_t j;
            retained += plain;
            if (retained + packed + (uint64_t)s->capacity * sizeof(pm_member) + 4096U > bdm_budget(f)) return false;
            input = (uint8_t *)xx_mem_alloc(packed ? packed : 1U);
            output = (uint8_t *)xx_mem_alloc(plain ? plain : 1U);
            okay = input && output && pm_read(f, at, input, packed);
            /* TiGGER advances an 8-bit XOR counter from zero for each file,
             * for stored data as well as the optional LZSS stream. */
            if (okay) {
                for (j = 0; j < packed; ++j) input[j] ^= (uint8_t)(j + 1U);
                if (e[12]) okay = csq_csq_lzss(input, packed, output, plain, pd);
                else if (plain) xx_mem_copy(output, input, plain);
            }
            xx_mem_free(input);
            if (!okay) {
                xx_mem_free(output);
                return false;
            }
            s->items[s->count - 1U].memory = output;
            s->items[s->count - 1U].compression_method = e[12];
        }
    }
    s->size = size;
    return true;
}

static bool pm_parse(Abstractformat *format, pm_stream *members, xx_pd_struct *pd)
{
    if ((pd && xx_pd_is_stopped(pd)) || format->file_type != XX_FILE_TYPE_CSQ) return false;
    return csq_csq_parse(format, members, pd);
}

Abstractformat *xx_csq_create(xx_io_device *device, int64_t base)
{
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) pm_init(format, device, base, XX_FILE_TYPE_CSQ, "csq");
    return format;
}
void xx_csq_free(Abstractformat *format)
{
    if (format) {
        xx_format_destroy(format);
        xx_mem_free(format);
    }
}

/* Detection keeps the inexpensive signature rule; the reader validates the grammar. */
xx_file_type_t xx_csq_detect(xx_io_device *device, int64_t base)
{
    uint8_t signature[8];
    int64_t saved, total;
    xx_file_type_t result = XX_FILE_TYPE_UNKNOWN;
    if (!device || base < 0 || (total = xx_io_size(device)) < base || total - base < 8) return result;
    saved = xx_io_tell(device);
    if (xx_io_read_at(device, base, signature, sizeof(signature)) && !xx_mem_compare(signature, "TiGGER\1\7", sizeof(signature))) result = XX_FILE_TYPE_CSQ;
    if (saved >= 0 && xx_io_seek64(device, saved, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *csq_open(xx_io_device *device)
{
    return xx_csq_create(device, 0);
}
static const xx_file_type_t csq_types[] = {XX_FILE_TYPE_CSQ};
static const xx_format_search_desc csq_descriptor = {csq_types, 1, NULL, 0, csq_open, xx_csq_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(csq, csq_descriptor)
