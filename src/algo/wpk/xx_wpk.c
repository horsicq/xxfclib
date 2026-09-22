/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * WPK (Watcom installer "pack") methods A and B.  Ported instruction for
 * instruction from the reference decoder; see xx_wpk.h for the format and for
 * the three things in it that must not be "cleaned up".
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/wpk/xx_wpk.h"

#define XX_WPK_WINDOW 0x1000
#define XX_WPK_MAX_SYMBOLS 0x13B
#define XX_WPK_SORT_STACK_A 0x400
#define XX_WPK_SORT_STACK_B 0x3E4
#define XX_WPK_LOOKUP 0x10000

/* LHA "-lh1-" position tables.  Kept local so this codec links on its own. */
static const uint8_t g_wpk_d_code[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x0a, 0x0a, 0x0a, 0x0a,
    0x0a, 0x0a, 0x0a, 0x0a, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0c, 0x0c, 0x0c, 0x0c, 0x0d, 0x0d, 0x0d, 0x0d, 0x0e, 0x0e, 0x0e, 0x0e,
    0x0f, 0x0f, 0x0f, 0x0f, 0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11,
    0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13, 0x14, 0x14, 0x14, 0x14,
    0x15, 0x15, 0x15, 0x15, 0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
    0x18, 0x18, 0x19, 0x19, 0x1a, 0x1a, 0x1b, 0x1b, 0x1c, 0x1c, 0x1d, 0x1d,
    0x1e, 0x1e, 0x1f, 0x1f, 0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23,
    0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27, 0x28, 0x28, 0x29, 0x29,
    0x2a, 0x2a, 0x2b, 0x2b, 0x2c, 0x2c, 0x2d, 0x2d, 0x2e, 0x2e, 0x2f, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
    0x3c, 0x3d, 0x3e, 0x3f};

static const uint8_t g_wpk_d_len[16] = {3, 3, 4, 4, 4, 5, 5, 5,
                                        5, 6, 6, 6, 7, 7, 7, 8};

typedef struct xx_wpk_entry_s {
    int32_t symbol;
    int32_t code;
    int32_t length;
} xx_wpk_entry;

/* MSB-first bit reader.  Past the end of the buffer it shifts in ZERO BYTES
 * but still advances the read pointer, because consumed() has to report the
 * byte count the original reader would have taken - that is what the
 * directory's CRC-32 is computed over, and the last real token of a member
 * routinely needs those pad bits to be peekable.  Deliberate: do not turn the
 * padding in xx_wpk_bits_fill() into an end-of-input failure. */
typedef struct xx_wpk_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t acc;
    int32_t count;
} xx_wpk_bits;

static void xx_wpk_bits_init(xx_wpk_bits *bits, const uint8_t *data,
                             size_t size) {
    bits->data = data;
    bits->size = size;
    bits->position = 0;
    bits->acc = 0;
    bits->count = 0;
}

static void xx_wpk_bits_fill(xx_wpk_bits *bits, int32_t need) {
    while (bits->count < need) {
        if (bits->position >= bits->size) {
            bits->acc = bits->acc << 8;
        } else {
            bits->acc = (bits->acc << 8) | (uint32_t)bits->data[bits->position];
        }
        bits->position++;
        bits->count += 8;
    }
}

/* Returns -1 when the request cannot be served from the real byte supply. */
static int32_t xx_wpk_bits_get(xx_wpk_bits *bits, int32_t need) {
    int32_t result;

    if (need <= 0) {
        return 0;
    }
    if (bits->count < need) {
        if (bits->position >= bits->size) {
            return -1;
        }
        xx_wpk_bits_fill(bits, need);
    }
    bits->count -= need;
    result = (int32_t)((bits->acc >> bits->count) &
                       (((uint32_t)1 << need) - 1U));
    bits->acc &= ((uint32_t)1 << bits->count) - 1U;

    return result;
}

static int32_t xx_wpk_bits_bit(xx_wpk_bits *bits) {
    int32_t result;

    if (bits->count < 1) {
        if (bits->position >= bits->size) {
            return -1;
        }
        xx_wpk_bits_fill(bits, 1);
    }
    bits->count--;
    result = (int32_t)((bits->acc >> bits->count) & 1U);
    bits->acc &= ((uint32_t)1 << bits->count) - 1U;

    return result;
}

static uint32_t xx_wpk_bits_peek16(xx_wpk_bits *bits) {
    xx_wpk_bits_fill(bits, 16);

    return (bits->acc >> (bits->count - 16)) & 0xFFFFU;
}

static bool xx_wpk_bits_drop(xx_wpk_bits *bits, int32_t need) {
    if ((need <= 0) || (need > bits->count)) {
        return false;
    }
    bits->count -= need;
    bits->acc &= ((uint32_t)1 << bits->count) - 1U;

    return true;
}

static size_t xx_wpk_bits_consumed(const xx_wpk_bits *bits) {
    return bits->position - (size_t)(bits->count >> 3);
}

static int32_t xx_wpk_compare(const xx_wpk_entry *table, int32_t left,
                              int32_t right) {
    return table[left].length - table[right].length;
}

static void xx_wpk_swap(xx_wpk_entry *table, int32_t left, int32_t right) {
    xx_wpk_entry entry;

    entry = table[left];
    table[left] = table[right];
    table[right] = entry;
}

/* Reference sorter A: a simple quicksort over an explicit stack.  The point of
 * this function is its tie-break order inside a run of equal lengths, so
 * nothing here may be rewritten, simplified or replaced by a library sort. */
static void xx_wpk_sort_a(xx_wpk_entry *table, int32_t count,
                          int32_t *stack_base, int32_t *stack_count) {
    int32_t sp = 0;
    int32_t base = 0;
    int32_t cnt = count;

    for (;;) {
        bool again = true;

        while (again) {
            again = false;

            if (cnt > 1) {
                if (cnt == 2) {
                    if (xx_wpk_compare(table, base, base + 1) > 0) {
                        xx_wpk_swap(table, base, base + 1);
                    }
                } else {
                    int32_t index = 1;
                    int32_t less = 0;
                    int32_t equal_edge = 0;

                    xx_wpk_swap(table, base, base + (cnt >> 1));

                    while (index < cnt) {
                        const int32_t result =
                            xx_wpk_compare(table, base + index, base);
                        if (result < 1) {
                            less++;
                            if (index != less) {
                                xx_wpk_swap(table, base + index, base + less);
                            }
                        }
                        if (result != 0) {
                            equal_edge = less;
                        }
                        index++;
                    }

                    if ((less != (cnt - 1)) || (equal_edge != 0)) {
                        if (less != 0) {
                            xx_wpk_swap(table, base + less, base);
                        }
                        cnt = cnt - less - 1;
                        index = (less == equal_edge) ? less : (equal_edge + 1);

                        if (sp == XX_WPK_SORT_STACK_A) {
                            return;
                        }

                        if (index < cnt) {
                            stack_base[sp] = base + less + 1;
                            stack_count[sp] = cnt;
                            cnt = index;
                        } else {
                            stack_base[sp] = base;
                            stack_count[sp] = index;
                            base = base + less + 1;
                        }

                        sp++;
                        again = true;
                    }
                }
            }
        }

        if (sp == 0) {
            return;
        }
        sp--;
        base = stack_base[sp];
        cnt = stack_count[sp];
    }
}

static int32_t xx_wpk_med3(xx_wpk_entry *table, int32_t a, int32_t b,
                           int32_t c) {
    int32_t result = xx_wpk_compare(table, a, b);

    if (result < 1) {
        result = xx_wpk_compare(table, a, c);
        if (result < 0) {
            a = b;
            if (xx_wpk_compare(table, b, c) > 0) {
                a = c;
            }
        }
    } else {
        result = xx_wpk_compare(table, a, c);
        if (result > 0) {
            a = c;
            if (xx_wpk_compare(table, b, c) > 0) {
                a = b;
            }
        }
    }

    return a;
}

/* Reference sorter B: the BSD qsort shape - shell sort below 16 elements,
 * median of three (or of nine) above it.  Same warning as sorter A: the
 * tie-break order IS the format. */
static void xx_wpk_sort_b(xx_wpk_entry *table, int32_t count,
                          int32_t *stack_base, int32_t *stack_count) {
    int32_t sp = 0;
    int32_t base = 0;
    int32_t cnt = count;

    for (;;) {
        int32_t mid;
        int32_t pa;
        int32_t pb;
        int32_t pc;
        int32_t pd;
        int32_t end;
        int32_t span;
        int32_t left;
        int32_t right;
        int32_t i;

        while (cnt < 2) {
            if (sp == 0) {
                return;
            }
            sp--;
            base = stack_base[sp];
            cnt = stack_count[sp];
        }

        if (cnt < 0x10) {
            int32_t gap = 3;

            while (gap > 0) {
                int32_t k = base;

                for (;;) {
                    int32_t j;
                    k += gap;
                    j = k;
                    if (k >= (base + cnt)) {
                        break;
                    }
                    while ((j > base) &&
                           (xx_wpk_compare(table, j - gap, j) > 0)) {
                        xx_wpk_swap(table, j, j - gap);
                        j -= gap;
                    }
                }

                gap -= 2;
            }

            if (sp == 0) {
                return;
            }
            sp--;
            base = stack_base[sp];
            cnt = stack_count[sp];
            continue;
        }

        mid = base + (cnt >> 1);

        if (cnt > 0x1D) {
            int32_t hi = base + cnt - 1;
            int32_t lo = base;

            if (cnt > 0x2A) {
                const int32_t step = cnt >> 3;
                lo = xx_wpk_med3(table, base, base + step, base + step * 2);
                mid = xx_wpk_med3(table, mid - step, mid, mid + step);
                hi = xx_wpk_med3(table, hi - step * 2, hi - step, hi);
            }

            mid = xx_wpk_med3(table, lo, mid, hi);
        }

        pc = base + cnt - 1;
        pa = base;
        pb = base;
        pd = pc;

        for (;;) {
            int32_t keep;

            while (pb <= pd) {
                const int32_t result = xx_wpk_compare(table, pb, mid);
                if (result > 0) {
                    break;
                }
                if (result == 0) {
                    keep = pb;
                    if (mid != pa) {
                        keep = mid;
                        if (mid == pb) {
                            keep = pa;
                        }
                    }
                    xx_wpk_swap(table, pa, pb);
                    pa++;
                    mid = keep;
                }
                pb++;
            }

            while (pb <= pd) {
                const int32_t result = xx_wpk_compare(table, pd, mid);
                if (result < 0) {
                    break;
                }
                if (result == 0) {
                    keep = pc;
                    if (mid != pd) {
                        keep = mid;
                        if (mid == pc) {
                            keep = pd;
                        }
                    }
                    xx_wpk_swap(table, pd, pc);
                    pc--;
                    mid = keep;
                }
                pd--;
            }

            if (pd < pb) {
                break;
            }

            keep = pd;
            if (mid != pb) {
                keep = mid;
                if (mid == pd) {
                    keep = pb;
                }
            }
            mid = keep;
            xx_wpk_swap(table, pb, pd);
            pb++;
            pd--;
        }

        end = base + cnt;
        span = (pa - base) < (pb - pa) ? (pa - base) : (pb - pa);
        for (i = 0; i < span; i++) {
            xx_wpk_swap(table, base + i, (pb - span) + i);
        }
        span = (pc - pd) < ((end - pc) - 1) ? (pc - pd) : ((end - pc) - 1);
        for (i = 0; i < span; i++) {
            xx_wpk_swap(table, pb + i, (end - span) + i);
        }

        left = pb - pa;
        right = pc - pd;

        if (sp == XX_WPK_SORT_STACK_B) {
            return;
        }

        if (right < left) {
            if (left < 2) {
                if (sp == 0) {
                    return;
                }
                sp--;
                base = stack_base[sp];
                cnt = stack_count[sp];
                continue;
            }
            stack_base[sp] = base;
            stack_count[sp] = left;
            base = end - right;
            cnt = right;
        } else {
            stack_base[sp] = end - right;
            stack_count[sp] = right;
            cnt = left;
        }

        sp++;
    }
}

/* Insertion sort - stable, i.e. it keeps a run of equal lengths in the order
 * it was transmitted.  Kept only so a caller can probe all three orders; no
 * archive of the reference corpus wants it. */
static void xx_wpk_sort_stable(xx_wpk_entry *table, int32_t count) {
    int32_t i;

    for (i = 1; i < count; i++) {
        const xx_wpk_entry entry = table[i];
        int32_t j = i - 1;
        while ((j >= 0) && (table[j].length > entry.length)) {
            table[j + 1] = table[j];
            j--;
        }
        table[j + 1] = entry;
    }
}

/* Shared by both methods: the distance is a byte through the -lh1- tables plus
 * d_len[i >> 4] - 2 raw bits, and the source index carries the classic LZSS
 * -1.  Returns -1 when the input runs dry mid-token. */
static int32_t xx_wpk_decode_distance(xx_wpk_bits *bits, int32_t position) {
    int32_t i = xx_wpk_bits_get(bits, 8);
    int32_t prefix;
    int32_t extra;

    if (i < 0) {
        return -1;
    }

    prefix = (int32_t)g_wpk_d_code[i & 0xFF];
    extra = (int32_t)g_wpk_d_len[(i & 0xFF) >> 4] - 2;

    if (extra > 0) {
        const int32_t tail = xx_wpk_bits_get(bits, extra);
        if (tail < 0) {
            return -1;
        }
        i = ((i << extra) + tail) & 0xFF;
    }

    return (position - (prefix * 0x40 + (i & 0x3F)) - 1) & (XX_WPK_WINDOW - 1);
}

/* The code-length table: one count byte, then count + 1 control bytes that
 * either assign a length to a run of symbols or skip a run of unused ones. */
static bool xx_wpk_build_table(xx_wpk_bits *bits, xx_wpk_entry *table,
                               int32_t *count) {
    const int32_t controls = xx_wpk_bits_get(bits, 8);
    int32_t symbol = 0;
    int32_t used = 0;
    int32_t i;

    *count = 0;
    if (controls < 0) {
        return false;
    }

    for (i = 0; i <= controls; i++) {
        const int32_t byte = xx_wpk_bits_get(bits, 8);

        if (byte < 0) {
            return false;
        }

        if ((byte & 0x80) == 0) {
            const int32_t length = (byte & 0x0F) + 1;
            const int32_t run = (byte >> 4) + 1;
            int32_t n;

            for (n = 0; n < run; n++) {
                if (used == XX_WPK_MAX_SYMBOLS) {
                    return false;
                }
                table[used].symbol = symbol;
                table[used].code = 0;
                table[used].length = length;
                used++;
                symbol++;
            }
        } else {
            symbol += (byte & 0x7F) + 1;
        }
    }

    *count = used;

    return symbol < XX_WPK_MAX_SYMBOLS;
}

/* Everything method A needs that is too large for a stack frame. */
typedef struct xx_wpk_scratch_s {
    xx_wpk_entry table[XX_WPK_MAX_SYMBOLS];
    int32_t stack_base[XX_WPK_SORT_STACK_A];
    int32_t stack_count[XX_WPK_SORT_STACK_A];
    int32_t symbol[XX_WPK_LOOKUP];
    uint8_t width[XX_WPK_LOOKUP];
    uint8_t window[XX_WPK_WINDOW];
} xx_wpk_scratch;

bool xx_wpk_decode_method_a_memory(const uint8_t *input, size_t input_size,
                                   int sorter, uint8_t *output,
                                   size_t output_size, size_t *written,
                                   size_t *consumed) {
    xx_wpk_bits bits;
    xx_wpk_scratch *scratch;
    int32_t count = 0;
    int32_t code;
    int32_t width;
    int32_t i;
    int32_t position = 0;
    size_t produced = 0;
    bool ok = true;

    if (written) {
        *written = 0;
    }
    if (consumed) {
        *consumed = 0;
    }
    if (!input && (input_size != 0)) {
        return false;
    }
    if (!output || (output_size == 0)) {
        return false;
    }

    scratch = (xx_wpk_scratch *)xx_mem_alloc(sizeof(xx_wpk_scratch));
    if (!scratch) {
        return false;
    }

    xx_wpk_bits_init(&bits, input, input_size);

    if (!xx_wpk_build_table(&bits, scratch->table, &count) || (count <= 0)) {
        xx_mem_free(scratch);
        return false;
    }

    if (sorter == (int)XX_WPK_SORTER_A) {
        xx_wpk_sort_a(scratch->table, count, scratch->stack_base,
                      scratch->stack_count);
    } else if (sorter == (int)XX_WPK_SORTER_B) {
        xx_wpk_sort_b(scratch->table, count, scratch->stack_base,
                      scratch->stack_count);
    } else if (sorter == (int)XX_WPK_SORTER_STABLE) {
        xx_wpk_sort_stable(scratch->table, count);
    } else {
        xx_mem_free(scratch);
        return false;
    }

    /* Canonical codes are assigned from the LAST entry backwards, in a 16-bit
     * left-justified space.  Handing them out forwards inverts the alphabet. */
    code = 0;
    for (i = count - 1; i >= 0; i--) {
        scratch->table[i].code = code & 0xFFFF;
        code = (code + (1 << (16 - scratch->table[i].length))) & 0xFFFF;
    }

    /* 16-bit direct lookup, filled shortest code first so a short code wins
     * over anything a longer one would have claimed - which is what the
     * reference's ascending linear scan does.  Output-equivalent to that scan,
     * not a different table layout. */
    for (i = 0; i < XX_WPK_LOOKUP; i++) {
        scratch->symbol[i] = -1;
    }
    xx_rt_memset(scratch->width, 0, sizeof(scratch->width));

    for (width = 1; width <= 16; width++) {
        const int32_t span = 1 << (16 - width);

        for (i = 0; i < count; i++) {
            int32_t start;
            int32_t n;

            if (scratch->table[i].length != width) {
                continue;
            }
            start = scratch->table[i].code;
            if ((start < 0) || (start >= XX_WPK_LOOKUP)) {
                xx_mem_free(scratch);
                return false;
            }
            if (scratch->symbol[start] >= 0) {
                continue;
            }
            if ((start + span) > XX_WPK_LOOKUP) {
                xx_mem_free(scratch);
                return false;
            }

            for (n = 0; n < span; n++) {
                scratch->symbol[start + n] = scratch->table[i].symbol;
                scratch->width[start + n] = (uint8_t)width;
            }
        }
    }

    xx_rt_memset(scratch->window, 0x20, sizeof(scratch->window));

    while (produced < output_size) {
        const uint32_t peek = xx_wpk_bits_peek16(&bits);
        const int32_t symbol = scratch->symbol[peek];

        if (symbol < 0) {
            ok = false;
            break;
        }
        if (!xx_wpk_bits_drop(&bits, (int32_t)scratch->width[peek])) {
            ok = false;
            break;
        }

        if (symbol < 0x100) {
            output[produced] = (uint8_t)symbol;
            produced++;
            scratch->window[position] = (uint8_t)symbol;
            position = (position + 1) & (XX_WPK_WINDOW - 1);
        } else {
            /* A match is symbol - 0xFD bytes, so 3 .. 0x3D.  Deliberate: it is
             * not symbol - 0x100 or - 0xFF, the first match symbol means three
             * bytes. */
            int32_t length = symbol - 0xFD;
            int32_t source = xx_wpk_decode_distance(&bits, position);
            int32_t n;

            if (source < 0) {
                ok = false;
                break;
            }

            /* The reference does not stop mid-match at the stored size: it
             * overshoots into slack and then fails its final size check.  Any
             * overshoot is therefore a failure, which is exactly what
             * refusing to write past output_size produces. */
            if ((size_t)length > (output_size - produced)) {
                ok = false;
                break;
            }

            for (n = 0; n < length; n++) {
                const uint8_t byte = scratch->window[source];
                output[produced] = byte;
                produced++;
                scratch->window[position] = byte;
                position = (position + 1) & (XX_WPK_WINDOW - 1);
                source = (source + 1) & (XX_WPK_WINDOW - 1);
            }
        }
    }

    if (ok && (produced == output_size)) {
        if (written) {
            *written = produced;
        }
        if (consumed) {
            *consumed = xx_wpk_bits_consumed(&bits);
        }
        xx_mem_free(scratch);

        return true;
    }

    xx_mem_free(scratch);

    return false;
}

bool xx_wpk_decode_method_b_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written, size_t *consumed) {
    xx_wpk_bits bits;
    uint8_t window[XX_WPK_WINDOW];
    int32_t position = 0;
    size_t produced = 0;
    bool ok = true;

    if (written) {
        *written = 0;
    }
    if (consumed) {
        *consumed = 0;
    }
    if (!input && (input_size != 0)) {
        return false;
    }
    if (!output || (output_size == 0)) {
        return false;
    }

    xx_wpk_bits_init(&bits, input, input_size);
    xx_rt_memset(window, 0x20, sizeof(window));

    while (produced < output_size) {
        const int32_t flag = xx_wpk_bits_bit(&bits);

        if (flag < 0) {
            ok = false;
            break;
        }

        if (flag == 0) {
            const int32_t byte = xx_wpk_bits_get(&bits, 8);
            if (byte < 0) {
                ok = false;
                break;
            }
            window[position] = (uint8_t)byte;
            output[produced] = (uint8_t)byte;
            produced++;
            position = (position + 1) & (XX_WPK_WINDOW - 1);
        } else {
            /* Six raw bits of length, then the shared distance encoding.  A
             * zero length copies nothing; that is deliberate - the bit reader
             * running dry is what ends such a stream. */
            const int32_t length = xx_wpk_bits_get(&bits, 6);
            int32_t source;
            int32_t n;

            if (length < 0) {
                ok = false;
                break;
            }

            source = xx_wpk_decode_distance(&bits, position);
            if (source < 0) {
                ok = false;
                break;
            }

            if ((size_t)length > (output_size - produced)) {
                ok = false;
                break;
            }

            for (n = 0; n < length; n++) {
                const uint8_t byte = window[source];
                window[position] = byte;
                output[produced] = byte;
                produced++;
                source = (source + 1) & (XX_WPK_WINDOW - 1);
                position = (position + 1) & (XX_WPK_WINDOW - 1);
            }
        }
    }

    if (ok && (produced == output_size)) {
        if (written) {
            *written = produced;
        }
        if (consumed) {
            *consumed = xx_wpk_bits_consumed(&bits);
        }

        return true;
    }

    return false;
}

bool xx_wpk_decode_memory(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size,
                          size_t *written) {
    return xx_wpk_decode_method_a_memory(input, input_size,
                                         (int)XX_WPK_SORTER_A, output,
                                         output_size, written, NULL);
}
