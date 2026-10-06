/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PDF stream filters, written from Adobe PDF Reference 1.7, section 3.3,
 * and ISO 32000-2 section 7.4. Each stage is published only after successful
 * decoding; a failed later stage never yields intermediate encoded data.
 */
#include "xxpdf_decode.h"
#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

typedef struct pdf_decode_buffer {
    uint8_t *data;
    size_t size, capacity, output_limit, memory_limit, reserved;
    xx_pd_struct *pd;
} pdf_decode_buffer;

static bool pdf_decode_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}

static bool pdf_decode_space(uint8_t c) {
    return c == 0U || c == 9U || c == 10U || c == 12U || c == 13U || c == 32U;
}

static bool pdf_decode_reserve(pdf_decode_buffer *b, size_t extra) {
    size_t limit, need, cap;
    uint8_t *next;
    if (pdf_decode_stopped(b->pd) || b->reserved > b->memory_limit ||
        extra > b->output_limit || b->size > b->output_limit - extra)
        return false;
    need = b->size + extra;
    limit = b->memory_limit - b->reserved;
    if (limit > b->output_limit) limit = b->output_limit;
    if (need > limit) return false;
    if (need <= b->capacity) return true;
    cap = b->capacity ? b->capacity : 256U;
    if (cap > limit) cap = limit;
    while (cap < need) {
        if (cap > limit / 2U) { cap = limit; break; }
        cap *= 2U;
    }
    next = (uint8_t *)xx_mem_realloc(b->data, cap);
    if (!next) return false;
    b->data = next;
    b->capacity = cap;
    return true;
}

static bool pdf_decode_append(pdf_decode_buffer *b, const uint8_t *data,
                              size_t size) {
    if (!pdf_decode_reserve(b, size)) return false;
    if (size) xx_mem_copy(b->data + b->size, data, size);
    b->size += size;
    return true;
}

static bool pdf_decode_byte(pdf_decode_buffer *b, uint8_t c) {
    return pdf_decode_append(b, &c, 1U);
}

static ssize_t pdf_decode_write(xx_io_device *device, const void *data,
                                size_t size) {
    pdf_decode_buffer *buffer = (pdf_decode_buffer *)device->priv;
    if (size > (SIZE_MAX >> 1U) ||
        !pdf_decode_append(buffer, (const uint8_t *)data, size)) return -1;
    return (ssize_t)size;
}

static bool pdf_decode_flate(const uint8_t *input, size_t size,
                              pdf_decode_buffer *output) {
    xx_io_device destination;
    size_t consumed = 0U, workspace, file_buffer, original_reserved;
    uint32_t adler, actual = XX_ADLER32_INIT;
    size_t offset;
    bool success;
    if (size < 8U || size > INT64_MAX ||
        !xx_zlib_stream_header_is_valid(input, size)) return false;
    /* The native memory-to-device inflater allocates a 32 KiB history and
     * its configured output buffer. Include both in our live heap budget. */
    file_buffer = xx_get_file_buffer_size();
    if (!file_buffer) file_buffer = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (file_buffer > SIZE_MAX - 32768U) return false;
    workspace = file_buffer + 32768U;
    original_reserved = output->reserved;
    if (original_reserved > output->memory_limit ||
        workspace > output->memory_limit - original_reserved) return false;
    output->reserved += workspace;
    xx_mem_zero(&destination, sizeof(destination));
    destination.write = pdf_decode_write;
    destination.priv = output;
    success = xx_deflate_unpack_memory_to_device_ex(input + 2U, size - 6U,
        &destination, &consumed, false, output->pd);
    output->reserved = original_reserved;
    if (!success || consumed != size - 6U || pdf_decode_stopped(output->pd))
        return false;
    adler = ((uint32_t)input[size - 4U] << 24U) |
            ((uint32_t)input[size - 3U] << 16U) |
            ((uint32_t)input[size - 2U] << 8U) | input[size - 1U];
    for (offset = 0U; offset < output->size;) {
        size_t amount = output->size - offset;
        if (amount > 65536U) amount = 65536U;
        if (pdf_decode_stopped(output->pd)) return false;
        actual = xx_adler32_update(actual, output->data + offset, amount);
        offset += amount;
    }
    return adler == actual;
}

static int pdf_decode_hex_digit(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool pdf_decode_asciihex(const uint8_t *input, size_t size,
                                pdf_decode_buffer *output) {
    size_t i;
    int high = -1;
    for (i = 0U; i < size; ++i) {
        int value;
        uint8_t c = input[i];
        if ((i & 4095U) == 0U && pdf_decode_stopped(output->pd)) return false;
        if (pdf_decode_space(c)) continue;
        if (c == '>') {
            if (high >= 0 && !pdf_decode_byte(output, (uint8_t)(high << 4U)))
                return false;
            return true;
        }
        value = pdf_decode_hex_digit(c);
        if (value < 0) return false;
        if (high < 0) high = value;
        else {
            if (!pdf_decode_byte(output, (uint8_t)((high << 4U) | value)))
                return false;
            high = -1;
        }
    }
    return false; /* A PDF filter has an explicit EOD marker. */
}

static bool pdf_decode_ascii85(const uint8_t *input, size_t size,
                               pdf_decode_buffer *output) {
    size_t i;
    uint64_t value = 0U;
    unsigned count = 0U;
    for (i = 0U; i < size; ++i) {
        uint8_t c = input[i];
        if ((i & 4095U) == 0U && pdf_decode_stopped(output->pd)) return false;
        if (pdf_decode_space(c)) continue;
        if (c == '~') {
            uint8_t bytes[4];
            unsigned j, produced = count ? count - 1U : 0U;
            if (i + 1U >= size || input[i + 1U] != '>' || count == 1U)
                return false;
            while (count && count < 5U) { value = value * 85U + 84U; ++count; }
            if (value > UINT32_MAX) return false;
            for (j = 0U; j < produced; ++j)
                bytes[j] = (uint8_t)(value >> (24U - 8U * j));
            return pdf_decode_append(output, bytes, produced);
        }
        if (c == 'z') {
            static const uint8_t zero[4] = {0U, 0U, 0U, 0U};
            if (count || !pdf_decode_append(output, zero, 4U)) return false;
            continue;
        }
        if (c < '!' || c > 'u') return false;
        value = value * 85U + (uint64_t)(c - '!');
        if (++count == 5U) {
            uint8_t bytes[4];
            unsigned j;
            if (value > UINT32_MAX) return false;
            for (j = 0U; j < 4U; ++j)
                bytes[j] = (uint8_t)(value >> (24U - 8U * j));
            if (!pdf_decode_append(output, bytes, 4U)) return false;
            count = 0U;
            value = 0U;
        }
    }
    return false;
}

static bool pdf_decode_runlength(const uint8_t *input, size_t size,
                                  pdf_decode_buffer *output) {
    size_t i = 0U;
    while (i < size) {
        unsigned c = input[i++];
        size_t count;
        if (pdf_decode_stopped(output->pd)) return false;
        if (c == 128U) return true;
        if (c < 128U) {
            count = (size_t)c + 1U;
            if (count > size - i || !pdf_decode_append(output, input + i, count))
                return false;
            i += count;
        } else {
            uint8_t byte;
            if (i == size) return false;
            byte = input[i++];
            count = 257U - c;
            if (!pdf_decode_reserve(output, count)) return false;
            while (count--) output->data[output->size++] = byte;
        }
    }
    return false;
}

typedef struct pdf_lzw_workspace {
    uint16_t prefix[4096];
    uint8_t suffix[4096], stack[4096];
} pdf_lzw_workspace;

static bool pdf_lzw_read_code(const uint8_t *input, size_t size,
                              size_t *byte, unsigned *bit, unsigned width,
                              unsigned *code) {
    unsigned i, value = 0U;
    for (i = 0U; i < width; ++i) {
        if (*byte >= size) return false;
        value = (value << 1U) | ((input[*byte] >> (7U - *bit)) & 1U);
        if (++*bit == 8U) { *bit = 0U; ++*byte; }
    }
    *code = value;
    return true;
}

static bool pdf_decode_lzw(const uint8_t *input, size_t size,
                           unsigned early_change, pdf_decode_buffer *output) {
    pdf_lzw_workspace *w;
    size_t byte = 0U, original_reserved = output->reserved;
    unsigned bit = 0U, width = 9U, next = 258U, old = 0U;
    bool have_old = false, first_code = true, success = false;
    if (early_change > 1U || original_reserved > output->memory_limit ||
        sizeof(*w) > output->memory_limit - original_reserved) return false;
    w = (pdf_lzw_workspace *)xx_mem_alloc(sizeof(*w));
    if (!w) return false;
    output->reserved += sizeof(*w);
    for (;;) {
        unsigned code, current, count = 0U, first;
        bool special;
        if (pdf_decode_stopped(output->pd) ||
            !pdf_lzw_read_code(input, size, &byte, &bit, width, &code)) break;
        if (first_code && code != 256U) break;
        first_code = false;
        if (code == 256U) {
            next = 258U; width = 9U; have_old = false;
            continue;
        }
        if (code == 257U) { success = true; break; }
        if (code > next || (!have_old && code >= 256U)) break;
        special = code == next;
        if (special && !have_old) break;
        current = special ? old : code;
        while (current >= 258U) {
            if (current >= next || count >= 4095U) goto done;
            w->stack[count++] = w->suffix[current];
            current = w->prefix[current];
        }
        if (current > 255U || count >= 4096U) break;
        first = current;
        w->stack[count++] = (uint8_t)first;
        if (!pdf_decode_reserve(output, (size_t)count + (special ? 1U : 0U)))
            break;
        while (count) output->data[output->size++] = w->stack[--count];
        if (special) output->data[output->size++] = (uint8_t)first;
        if (have_old && next < 4096U) {
            w->prefix[next] = (uint16_t)old;
            w->suffix[next] = (uint8_t)first;
            ++next;
            if (width < 12U && next + early_change == (1U << width)) ++width;
        }
        old = code;
        have_old = true;
    }
done:
    output->reserved = original_reserved;
    xx_mem_free(w);
    return success;
}

static unsigned pdf_sample_get(const uint8_t *row, size_t bit,
                               unsigned width) {
    unsigned i, sample = 0U;
    for (i = 0U; i < width; ++i) {
        size_t current = bit + i;
        sample = (sample << 1U) | ((row[current >> 3U] >>
            (7U - (unsigned)(current & 7U))) & 1U);
    }
    return sample;
}

static void pdf_sample_set(uint8_t *row, size_t bit, unsigned width,
                           unsigned sample) {
    unsigned i;
    for (i = 0U; i < width; ++i) {
        size_t current = bit + i;
        uint8_t mask = (uint8_t)(1U << (7U - (unsigned)(current & 7U)));
        if ((sample >> (width - 1U - i)) & 1U) row[current >> 3U] |= mask;
        else row[current >> 3U] &= (uint8_t)~mask;
    }
}

static unsigned pdf_decode_paeth(unsigned left, unsigned up, unsigned diagonal) {
    int p = (int)left + (int)up - (int)diagonal;
    int a = p - (int)left, b = p - (int)up, c = p - (int)diagonal;
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    if (c < 0) c = -c;
    return a <= b && a <= c ? left : b <= c ? up : diagonal;
}

static bool pdf_decode_predictor_geometry(const xx_pdf_decode_params *params,
    size_t *out_samples, size_t *out_row, size_t *out_bpp) {
    size_t samples, bits;
    unsigned width = params->bits_per_component;
    if (!params->colors || !params->columns ||
        (width != 1U && width != 2U && width != 4U && width != 8U && width != 16U) ||
        (size_t)params->colors > SIZE_MAX / params->columns) return false;
    samples = (size_t)params->colors * params->columns;
    if (samples > (SIZE_MAX - 7U) / width) return false;
    bits = samples * width;
    *out_row = (bits + 7U) / 8U;
    if ((size_t)params->colors > (SIZE_MAX - 7U) / width) return false;
    *out_bpp = ((size_t)params->colors * width + 7U) / 8U;
    *out_samples = samples;
    return *out_row != 0U;
}

static bool pdf_decode_predictor_limit(const xx_pdf_decode_params *params,
                                       size_t final_limit, size_t *stage_limit) {
    size_t samples, row, bpp, rows;
    *stage_limit = final_limit;
    if (params->predictor == 1U) return true;
    if (params->predictor != 2U &&
        (params->predictor < 10U || params->predictor > 15U)) return false;
    if (!pdf_decode_predictor_geometry(params, &samples, &row, &bpp)) return false;
    if (params->predictor == 2U || final_limit == SIZE_MAX) return true;
    /* Permit exactly one PNG marker per complete output row allowed by the
     * final byte limit. No partial row can form a valid predictor result. */
    rows = final_limit / row;
    if (row == SIZE_MAX || rows > SIZE_MAX / (row + 1U))
        *stage_limit = SIZE_MAX;
    else *stage_limit = rows * (row + 1U);
    return true;
}

static bool pdf_decode_predictor(pdf_decode_buffer *buffer,
                                  const xx_pdf_decode_params *params) {
    size_t samples, row, bpp, rows, y;
    unsigned width = params->bits_per_component;
    if (params->predictor == 1U) return true;
    if (params->predictor != 2U &&
        (params->predictor < 10U || params->predictor > 15U)) return false;
    if (!pdf_decode_predictor_geometry(params, &samples, &row, &bpp)) return false;
    if (params->predictor == 2U) {
        if (buffer->size % row) return false;
        rows = buffer->size / row;
        for (y = 0U; y < rows; ++y) {
            size_t x;
            uint8_t *data = buffer->data + y * row;
            unsigned mask = (1U << width) - 1U;
            if (pdf_decode_stopped(buffer->pd)) return false;
            for (x = params->colors; x < samples; ++x) {
                unsigned sample;
                if ((x & 4095U) == 0U && pdf_decode_stopped(buffer->pd))
                    return false;
                sample = pdf_sample_get(data, x * width, width) +
                    pdf_sample_get(data, (x - params->colors) * width, width);
                pdf_sample_set(data, x * width, width, sample & mask);
            }
        }
        return true;
    }
    if (row == SIZE_MAX || buffer->size % (row + 1U)) return false;
    rows = buffer->size / (row + 1U);
    for (y = 0U; y < rows; ++y) {
        size_t x;
        const uint8_t *source = buffer->data + y * (row + 1U) + 1U;
        uint8_t *destination = buffer->data + y * row;
        const uint8_t *previous = y ? buffer->data + (y - 1U) * row : NULL;
        unsigned filter = source[-1];
        if (filter > 4U || pdf_decode_stopped(buffer->pd)) return false;
        /* Every PNG row carries its filter byte, including predictors10..14.
         * Moving rows towards the buffer start keeps source bytes readable. */
        for (x = 0U; x < row; ++x) {
            unsigned left = x >= bpp ? destination[x - bpp] : 0U;
            unsigned up = previous ? previous[x] : 0U;
            unsigned diagonal = previous && x >= bpp ? previous[x - bpp] : 0U;
            unsigned value = source[x];
            if ((x & 4095U) == 0U && pdf_decode_stopped(buffer->pd)) return false;
            if (filter == 1U) value += left;
            else if (filter == 2U) value += up;
            else if (filter == 3U) value += (left + up) / 2U;
            else if (filter == 4U) value += pdf_decode_paeth(left, up, diagonal);
            destination[x] = (uint8_t)value;
        }
    }
    buffer->size = rows * row;
    return true;
}

bool xx_pdf_decode_stream_ex(const uint8_t *input, size_t input_size,
    const xx_pdf_filter_spec *filters, size_t filter_count, size_t output_limit,
    size_t memory_limit, xx_pd_struct *pd, uint8_t **output,
    size_t *output_size, size_t *output_capacity) {
    const uint8_t *current = input;
    size_t current_size = input_size, current_capacity = 0U, i;
    uint8_t *owned = NULL;
    if (output) *output = NULL;
    if (output_size) *output_size = 0U;
    if (output_capacity) *output_capacity = 0U;
    if (!output || !output_size || !output_capacity || (!input && input_size) ||
        (!filters && filter_count) || filter_count > 64U ||
        pdf_decode_stopped(pd)) return false;
    /* There is still an owned result when no decoding filter was requested. */
    for (i = 0U; i < (filter_count ? filter_count : 1U); ++i) {
        pdf_decode_buffer next;
        bool success = false;
        bool terminal = !filter_count || i + 1U == filter_count;
        xx_pdf_filter filter = filter_count ? filters[i].filter : XX_PDF_FILTER_DCT;
        xx_mem_zero(&next, sizeof(next));
        next.output_limit = terminal ? output_limit : memory_limit;
        next.memory_limit = memory_limit;
        next.reserved = current_capacity;
        next.pd = pd;
        if ((filter == XX_PDF_FILTER_FLATE || filter == XX_PDF_FILTER_LZW) &&
            !pdf_decode_predictor_limit(&filters[i].params, next.output_limit,
                &next.output_limit)) goto failure;
        if (filter == XX_PDF_FILTER_FLATE)
            success = pdf_decode_flate(current, current_size, &next);
        else if (filter == XX_PDF_FILTER_LZW)
            success = pdf_decode_lzw(current, current_size,
                filters[i].params.early_change, &next);
        else if (filter == XX_PDF_FILTER_ASCII85)
            success = pdf_decode_ascii85(current, current_size, &next);
        else if (filter == XX_PDF_FILTER_ASCIIHEX)
            success = pdf_decode_asciihex(current, current_size, &next);
        else if (filter == XX_PDF_FILTER_RUNLENGTH)
            success = pdf_decode_runlength(current, current_size, &next);
        else if ((filter == XX_PDF_FILTER_DCT || filter == XX_PDF_FILTER_JPX) &&
                  (!filter_count || i + 1U == filter_count)) {
            /* An earlier filter already produced an owned encoded image. */
            if (owned) {
                if (current_size > output_limit || current_capacity > memory_limit ||
                    pdf_decode_stopped(pd)) goto failure;
                *output = owned;
                *output_size = current_size;
                *output_capacity = current_capacity;
                return true;
            }
            success = pdf_decode_append(&next, current, current_size);
        }
        if (success && (filter == XX_PDF_FILTER_FLATE || filter == XX_PDF_FILTER_LZW))
            success = pdf_decode_predictor(&next, &filters[i].params);
        if (!success || (terminal && next.size > output_limit) || pdf_decode_stopped(pd)) {
            xx_mem_free(next.data);
            goto failure;
        }
        xx_mem_free(owned);
        owned = next.data;
        current = owned;
        current_size = next.size;
        current_capacity = next.capacity;
    }
    *output = owned;
    *output_size = current_size;
    *output_capacity = current_capacity;
    return true;
failure:
    xx_mem_free(owned);
    return false;
}

bool xx_pdf_decode_stream(const uint8_t *input, size_t input_size,
    const xx_pdf_filter_spec *filters, size_t filter_count, size_t output_limit,
    size_t memory_limit, xx_pd_struct *pd, uint8_t **output,
    size_t *output_size) {
    size_t capacity;
    return xx_pdf_decode_stream_ex(input, input_size, filters, filter_count,
        output_limit, memory_limit, pd, output, output_size, &capacity);
}
