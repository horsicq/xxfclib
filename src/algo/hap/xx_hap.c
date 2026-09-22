/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * HAP (Harri Hirvola) method 0x16 decoder.  Ported one-for-one from the
 * XArchive reference decoder (XArchive/Algos/xhapdecoder.cpp).
 *
 * The model is ONE FLAT BLOCK addressed by byte offset, exactly as the
 * original does it.  Keeping the offsets rather than inventing named arrays is
 * deliberate and load-bearing: several regions are aliased (the count table is
 * the symbol table plus 0x8000; the tree's child arrays share memory with the
 * position map; the cumulative-frequency stack can, at a wrapped stack
 * pointer, reach the input-byte latch), and every index is computed with
 * 16-bit wrap-around.  Splitting the block into separate arrays would change
 * the output on any stream that relies on that aliasing, so do NOT "clean this
 * up" into typed arrays.
 *
 * Every index into the block is masked to 16 bits before it is added to a
 * region base, and the largest reachable address is
 * HAP_OFF_FREELIST + 0x211 + 1 = 0x504f2, inside HAP_STATE_SIZE = 0x50600.
 * That is why the accessors below need no per-access bound test, which is the
 * reference's behaviour too.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/hap/xx_hap.h"

#define HAP_STATE_SIZE 0x50600

#define HAP_OFF_LISTCOUNT 0x2c   /* symbols in the context being scanned     */
#define HAP_OFF_LISTSTART 0x2e   /* arena index of that context              */
#define HAP_OFF_STACKPTR 0x30    /* cumulative-frequency stack pointer       */
#define HAP_OFF_SAVE32 0x32
#define HAP_OFF_SAVE34 0x34
#define HAP_OFF_SAVE36 0x36
#define HAP_OFF_SAVE38 0x38
#define HAP_OFF_MARKER 0x3b      /* per level: 0 coded, -1 new, -4 esc, -6 grow */
#define HAP_OFF_SYMBOL 0x45      /* the byte just decoded                    */
#define HAP_OFF_LEVELSTART 0x46  /* per level: arena index                   */
#define HAP_OFF_LEVEL 0x50       /* 8, 6, 4, 2, 0                            */
#define HAP_OFF_LEVELCOUNT 0x53  /* per level: symbol count                  */
#define HAP_OFF_BITCOUNT 0x5d
#define HAP_OFF_CUMLOW 0x5e
#define HAP_OFF_TOTAL 0x60
#define HAP_OFF_CUMHIGH 0x62
#define HAP_OFF_EXCLTOP 0x64     /* next free slot in the exclusion log      */
#define HAP_OFF_NEXTNODE 0x68    /* next context id, stepped by 2            */
#define HAP_OFF_SAVEA3 0x75      /* per level: previous node                 */
#define HAP_OFF_NODE 0x7f        /* per level: current context node          */
#define HAP_OFF_SAVEAD 0x6b      /* per level: previous symbol slot          */
#define HAP_OFF_MATCHPOS 0x22    /* per level: symbol slot of the decoded byte */
#define HAP_OFF_STACK 0x89       /* cumulative-frequency stack base          */
#define HAP_OFF_INBYTE 0x10089
#define HAP_OFF_VALUE 0x1008a
#define HAP_OFF_LOW 0x1008c
#define HAP_OFF_HIGH 0x1008e
#define HAP_OFF_EXCL 0x10090     /* 0xff allowed, 0 excluded                 */
#define HAP_OFF_CHILD 0x102e0    /* per symbol slot: child context, 16 bit   */
#define HAP_OFF_SYM 0x202e0      /* arena: symbol bytes                      */
#define HAP_OFF_CNT 0x282e0      /* arena: count bytes (SYM + 0x8000)        */
#define HAP_OFF_NODESTART 0x302e0 /* per node: arena index                   */
#define HAP_OFF_NODECOUNT 0x402e0 /* per node: symbol count minus one        */
#define HAP_OFF_FREELIST 0x502e0  /* free-list head per size class           */

#define HAP_MAX_OUTPUT ((size_t)((uint32_t)512U * 1024U * 1024U))

typedef struct hap_model_s {
    uint8_t *s;
    const uint8_t *input;
    size_t input_size;
    size_t input_pos;
} hap_model;

static uint32_t hap_g8(const hap_model *m, int32_t offset)
{
    return (uint32_t)m->s[offset];
}

static void hap_p8(hap_model *m, int32_t offset, uint32_t value)
{
    m->s[offset] = (uint8_t)(value & 0xffU);
}

static uint32_t hap_g16(const hap_model *m, int32_t offset)
{
    return (uint32_t)m->s[offset] | ((uint32_t)m->s[offset + 1] << 8);
}

static int32_t hap_gi16(const hap_model *m, int32_t offset)
{
    uint32_t value = hap_g16(m, offset);
    return (value >= 0x8000U) ? (int32_t)value - 0x10000 : (int32_t)value;
}

static void hap_p16(hap_model *m, int32_t offset, uint32_t value)
{
    value &= 0xffffU;
    m->s[offset] = (uint8_t)(value & 0xffU);
    m->s[offset + 1] = (uint8_t)(value >> 8);
}

static uint32_t hap_g32(const hap_model *m, int32_t offset)
{
    return (uint32_t)m->s[offset] | ((uint32_t)m->s[offset + 1] << 8) |
           ((uint32_t)m->s[offset + 2] << 16) |
           ((uint32_t)m->s[offset + 3] << 24);
}

static void hap_p32(hap_model *m, int32_t offset, uint32_t value)
{
    m->s[offset] = (uint8_t)(value & 0xffU);
    m->s[offset + 1] = (uint8_t)((value >> 8) & 0xffU);
    m->s[offset + 2] = (uint8_t)((value >> 16) & 0xffU);
    m->s[offset + 3] = (uint8_t)((value >> 24) & 0xffU);
}

static void hap_push(hap_model *m, uint32_t value)
{
    uint32_t stack_pointer = (hap_g16(m, HAP_OFF_STACKPTR) - 2U) & 0xffffU;
    hap_p16(m, HAP_OFF_STACKPTR, stack_pointer);
    hap_p16(m, HAP_OFF_STACK + (int32_t)stack_pointer, value);
}

static void hap_init(hap_model *m)
{
    int32_t u = 0;
    int32_t i;
    int32_t previous;
    uint32_t c;
    uint32_t v;

    /* NOTE: init() deliberately does NOT clear the block.  The reference zeroes
     * it once when the model is constructed and then calls init() again, mid
     * stream, whenever the arena is exhausted - at which point the arithmetic
     * registers (VALUE/LOW/HIGH/BITCOUNT) and the exclusion table must survive.
     * Adding a memset here would silently corrupt every restarted stream. */

    for (i = 0; i < 0x100; i++) {
        hap_p16(m, HAP_OFF_FREELIST + u, 0xffffU);
        u += 2;
    }
    hap_p16(m, HAP_OFF_FREELIST + u, 0x300U); /* u == 0x200 */
    hap_p16(m, HAP_OFF_FREELIST + 2, 0x203U);

    hap_p16(m, HAP_OFF_NODECOUNT + 0, 0xffU);
    hap_p16(m, HAP_OFF_NODECOUNT + 2, 0xffU);
    hap_p16(m, HAP_OFF_NODECOUNT + 4, 0U);
    hap_p16(m, HAP_OFF_NODECOUNT + 6, 0U);
    hap_p16(m, HAP_OFF_NODECOUNT + 8, 0U);

    hap_p16(m, HAP_OFF_NODESTART + 0, 0U);
    hap_p16(m, HAP_OFF_NODESTART + 2, 0x100U);
    hap_p16(m, HAP_OFF_NODESTART + 4, 0x200U);
    hap_p16(m, HAP_OFF_NODESTART + 6, 0x201U);
    hap_p16(m, HAP_OFF_NODESTART + 8, 0x202U);

    c = 0U;
    for (i = 0; i < 0x200; i++) {
        hap_p8(m, HAP_OFF_SYM + i, c);
        c = (c + 1U) & 0xffU;
    }
    hap_p8(m, HAP_OFF_SYM + 0x200, 0x68U);
    hap_p8(m, HAP_OFF_SYM + 0x201, 0x65U);
    hap_p8(m, HAP_OFF_SYM + 0x202, 0x65U);

    u = 0;
    for (i = 0; i < 0x80; i++) {
        hap_p16(m, HAP_OFF_CNT + u, 0x101U);
        u = (int32_t)((uint32_t)(u + 2) & 0xffffU);
    }
    previous = u;
    for (i = 0; i < 0x80; i++) {
        previous = u;
        hap_p16(m, HAP_OFF_CNT + u, 0U);
        u = (int32_t)((uint32_t)(u + 2) & 0xffffU);
    }
    hap_p16(m, HAP_OFF_CNT + u, 0U);
    hap_p8(m, HAP_OFF_CNT + (int32_t)((uint32_t)(previous + 4) & 0xffffU), 0U);

    u = 0x200;
    for (i = 0; i < 0x100; i++) {
        hap_p16(m, HAP_OFF_CHILD + u, 0xffffU);
        u += 2;
    }
    hap_p16(m, HAP_OFF_CHILD + 0x2d0, 4U);
    hap_p16(m, HAP_OFF_CHILD + 0x2e8, 2U);
    hap_p16(m, HAP_OFF_CHILD + 0x400, 3U);
    hap_p16(m, HAP_OFF_CHILD + 0x402, 0xffffU);
    hap_p16(m, HAP_OFF_CHILD + 0x404, 0xffffU);

    u = 0x406;
    v = 0x204U;
    for (i = 0; i < 0xfc; i++) {
        hap_p16(m, HAP_OFF_CHILD + u, v);
        v++;
        u += 2;
    }
    hap_p16(m, HAP_OFF_CHILD + u, 0xffffU); /* u == 0x600 */

    u = 0x600;
    v = 0x400U;
    for (i = 0; i < 0x7c; i++) {
        hap_p16(m, HAP_OFF_CHILD + u, v);
        v = (v + 0x100U) & 0xffffU;
        u = (int32_t)((uint32_t)(u + 0x200) & 0xffffU);
    }
    hap_p16(m, HAP_OFF_CHILD + u, 0xffffU);

    u = 0;
    for (i = 0; i < 0x80; i++) {
        hap_p16(m, HAP_OFF_EXCL + u, 0xffffU);
        u += 2;
    }

    hap_p16(m, HAP_OFF_NODE + 0, 0U);
    hap_p16(m, HAP_OFF_NODE + 2, 1U);
    hap_p16(m, HAP_OFF_NODE + 4, 0xffffU);
    hap_p16(m, HAP_OFF_NODE + 6, 0xffffU);
    hap_p16(m, HAP_OFF_NODE + 8, 0xffffU);
    hap_p16(m, HAP_OFF_SAVEA3 + 2, 1U);
    hap_p16(m, HAP_OFF_SAVEA3 + 4, 3U);
    hap_p16(m, HAP_OFF_SAVEA3 + 6, 4U);
    hap_p16(m, HAP_OFF_SAVEAD + 2, 0x65U);
    hap_p16(m, HAP_OFF_SAVEAD + 4, 0U);
    hap_p16(m, HAP_OFF_SAVEAD + 6, 0U);
    hap_p16(m, HAP_OFF_NEXTNODE, 10U);
    hap_p32(m, HAP_OFF_EXCLTOP, 0x100U);
}

/* Witten-Neal-Cleary interval narrowing plus renormalisation, including the
 * underflow (E3) case.  Returns false only when the input runs out. */
static bool hap_arith_update(hap_model *m)
{
    uint32_t low = hap_g16(m, HAP_OFF_LOW);
    uint32_t high = hap_g16(m, HAP_OFF_HIGH);
    uint32_t total = hap_g16(m, HAP_OFF_TOTAL);
    uint32_t cum_high = hap_g16(m, HAP_OFF_CUMHIGH);
    uint32_t cum_low = hap_g16(m, HAP_OFF_CUMLOW);
    uint32_t range;
    uint32_t new_low;
    uint32_t new_high;

    if (total == 0U) return false;

    range = (high - low) & 0xffffU;
    new_low = low;
    new_high = high;
    if (cum_high != total) {
        new_high = (((range * cum_high + cum_high) / total) + low - 1U) & 0xffffU;
    }
    if (cum_low != 0U) {
        new_low = (low + ((range * cum_low + cum_low) / total)) & 0xffffU;
    }
    hap_p16(m, HAP_OFF_LOW, new_low);
    hap_p16(m, HAP_OFF_HIGH, new_high);

    for (;;) {
        if ((hap_g8(m, HAP_OFF_HIGH + 1) ^ hap_g8(m, HAP_OFF_LOW + 1)) & 0x80U) {
            uint32_t mask;
            if (hap_g16(m, HAP_OFF_HIGH) & 0x4000U) return true;
            mask = hap_g8(m, HAP_OFF_LOW + 1) & 0x40U;
            if (mask == 0U) return true;
            hap_p8(m, HAP_OFF_VALUE + 1, hap_g8(m, HAP_OFF_VALUE + 1) ^ mask);
            hap_p8(m, HAP_OFF_HIGH + 1, hap_g8(m, HAP_OFF_HIGH + 1) | mask);
            hap_p8(m, HAP_OFF_LOW + 1, hap_g8(m, HAP_OFF_LOW + 1) & 0x3fU);
        }
        hap_p16(m, HAP_OFF_LOW, hap_g16(m, HAP_OFF_LOW) * 2U);
        hap_p16(m, HAP_OFF_HIGH, hap_g16(m, HAP_OFF_HIGH) * 2U + 1U);
        if (hap_g8(m, HAP_OFF_BITCOUNT) == 0U) {
            hap_p8(m, HAP_OFF_BITCOUNT, 8U);
            if (m->input_pos >= m->input_size) return false;
            hap_p8(m, HAP_OFF_INBYTE, (uint32_t)m->input[m->input_pos++]);
        }
        hap_p8(m, HAP_OFF_BITCOUNT, hap_g8(m, HAP_OFF_BITCOUNT) - 1U);
        hap_p16(m, HAP_OFF_VALUE,
                hap_g16(m, HAP_OFF_VALUE) * 2U + (hap_g8(m, HAP_OFF_INBYTE) >> 7));
        hap_p8(m, HAP_OFF_INBYTE, hap_g8(m, HAP_OFF_INBYTE) * 2U);
    }
}

/* After a symbol was coded in a shorter context, bump its count in the level we
 * escaped from - or mark that level as "needs to grow" when the symbol is not
 * in it at all. */
static void hap_fixup_escape(hap_model *m)
{
    int32_t level = (int32_t)hap_g16(m, HAP_OFF_LEVEL);
    int32_t count = hap_gi16(m, HAP_OFF_LEVELCOUNT + level);
    uint32_t index = hap_g16(m, HAP_OFF_LEVELSTART + level);

    for (;;) {
        if (count == 0) {
            hap_p16(m, HAP_OFF_MARKER + level, 0xfffaU);
            return;
        }
        if (hap_g8(m, HAP_OFF_SYMBOL) == hap_g8(m, HAP_OFF_SYM + (int32_t)index))
            break;
        index = (index + 1U) & 0xffffU;
        count--;
    }
    hap_p8(m, HAP_OFF_CNT + (int32_t)index,
           hap_g8(m, HAP_OFF_CNT + (int32_t)index) + 1U);
    hap_p16(m, HAP_OFF_MARKER + level, 0U);
}

/* Allocate a brand new one-symbol context.  Returns the node id, or -1 when the
 * arena is exhausted (the caller then resets the whole model). */
static int32_t hap_new_context(hap_model *m)
{
    uint32_t node = hap_g16(m, HAP_OFF_NEXTNODE);
    uint32_t head;
    uint32_t next;
    int32_t head2;

    hap_p16(m, HAP_OFF_NEXTNODE, node + 2U);
    if (node == 0xffffU) return -1;

    head = hap_g16(m, HAP_OFF_FREELIST + 2);
    if (head == 0xffffU) {
        int32_t u = 4;
        int32_t k;
        for (k = 0xff; k != 0; k--) {
            if (hap_gi16(m, HAP_OFF_FREELIST + u) != -1) {
                uint32_t block = hap_g16(m, HAP_OFF_FREELIST + u);
                int32_t block2;
                uint32_t block_next;
                hap_p16(m, HAP_OFF_FREELIST + u - 2, block + 1U);
                hap_p8(m, HAP_OFF_SYM + (int32_t)block, hap_g8(m, HAP_OFF_SYMBOL));
                hap_p8(m, HAP_OFF_CNT + (int32_t)block, 1U);
                hap_p16(m, HAP_OFF_NODESTART + (int32_t)node, block);
                hap_p16(m, HAP_OFF_NODECOUNT + (int32_t)node, 0U);
                block2 = (int32_t)((block * 2U) & 0xffffU);
                block_next = hap_g16(m, HAP_OFF_CHILD + block2);
                hap_p16(m, HAP_OFF_CHILD + block2, 0xffffU);
                hap_p16(m, HAP_OFF_CHILD + block2 + 2, 0xffffU);
                hap_p16(m, HAP_OFF_FREELIST + u, block_next);
                return (int32_t)(node >> 1);
            }
            u = (int32_t)((uint32_t)(u + 2) & 0xffffU);
        }
        return -1;
    }

    hap_p8(m, HAP_OFF_SYM + (int32_t)head, hap_g8(m, HAP_OFF_SYMBOL));
    hap_p8(m, HAP_OFF_CNT + (int32_t)head, 1U);
    hap_p16(m, HAP_OFF_NODESTART + (int32_t)node, head);
    hap_p16(m, HAP_OFF_NODECOUNT + (int32_t)node, 0U);
    head2 = (int32_t)((head * 2U) & 0xffffU);
    next = hap_g16(m, HAP_OFF_CHILD + head2);
    hap_p16(m, HAP_OFF_CHILD + head2, 0xffffU);
    hap_p16(m, HAP_OFF_FREELIST + 2, next);
    return (int32_t)(node >> 1);
}

/* Move the current context to a one-slot-larger arena block and append the new
 * symbol.  Returns the old symbol count, or -1 when no block is available. */
static int32_t hap_grow_context(hap_model *m)
{
    int32_t level = (int32_t)hap_g16(m, HAP_OFF_LEVEL);
    int32_t node2 = (int32_t)((uint32_t)(hap_gi16(m, HAP_OFF_NODE + level) * 2) & 0xffffU);
    int32_t count;
    int32_t k;
    uint32_t size_class;
    bool empty = false;
    int32_t u;
    uint32_t block;
    int32_t remainder;
    int32_t n;
    uint32_t old_start;
    uint32_t spare;
    uint32_t a;
    uint32_t b;
    int32_t j;
    uint32_t source;
    uint32_t dest;
    uint32_t result;

    hap_p16(m, HAP_OFF_SAVE38, (uint32_t)node2);
    count = hap_gi16(m, HAP_OFF_NODECOUNT + node2);
    hap_p16(m, HAP_OFF_NODECOUNT + node2, (uint32_t)(count + 1));
    k = 0xff - count;
    count = count + 2;
    size_class = (uint32_t)(count * 2) & 0xffffU;

    u = (int32_t)size_class;
    for (;;) {
        if (k == 0) break;
        if (u > 0x210) return -1;
        empty = (hap_gi16(m, HAP_OFF_FREELIST + u) == -1);
        u = (int32_t)((uint32_t)(u + 2) & 0xffffU);
        k--;
        if (!empty) break;
    }
    if (empty) return -1;

    u = (int32_t)((uint32_t)(u - 2) & 0xffffU);
    if (u >= 0x211) return -1;

    block = hap_g16(m, HAP_OFF_FREELIST + u);
    hap_p16(m, HAP_OFF_FREELIST + u,
            hap_g16(m, HAP_OFF_CHILD + (int32_t)((block * 2U) & 0xffffU)));
    remainder = (int32_t)((uint32_t)(u - (int32_t)size_class) & 0xffffU);
    if (remainder != 0) {
        int32_t tail_offset = (int32_t)((block * 2U + size_class) & 0xffffU);
        uint32_t temp = (uint32_t)tail_offset >> 1;
        uint32_t other;
        if (remainder > 0x210) return -1;
        other = hap_g16(m, HAP_OFF_FREELIST + remainder);
        hap_p16(m, HAP_OFF_FREELIST + remainder, temp);
        temp = other;
        hap_p16(m, HAP_OFF_CHILD + tail_offset, temp);
    }

    n = (int32_t)(size_class >> 1) - 1;
    old_start = hap_g16(m, HAP_OFF_NODESTART + (int32_t)hap_g16(m, HAP_OFF_SAVE38));
    hap_p16(m, HAP_OFF_NODESTART + (int32_t)hap_g16(m, HAP_OFF_SAVE38), block);

    if (((uint32_t)(n * 2) & 0xffffU) >= 0x211U) return -1;
    spare = hap_g16(m, HAP_OFF_FREELIST + (int32_t)((uint32_t)(n * 2) & 0xffffU));
    hap_p16(m, HAP_OFF_FREELIST + (int32_t)((uint32_t)(n * 2) & 0xffffU), old_start);

    hap_p16(m, HAP_OFF_SAVE36, block);
    hap_p16(m, HAP_OFF_SAVE34, old_start);
    hap_p16(m, HAP_OFF_SAVE32, (uint32_t)n);

    a = block;
    b = old_start;
    for (j = n; j != 0; j--) {
        hap_p8(m, HAP_OFF_SYM + (int32_t)a, hap_g8(m, HAP_OFF_SYM + (int32_t)b));
        a = (a + 1U) & 0xffffU;
        b = (b + 1U) & 0xffffU;
    }
    hap_p8(m, HAP_OFF_SYM + (int32_t)a, hap_g8(m, HAP_OFF_SYMBOL));

    /* The count table is the symbol table plus 0x8000; the reference walks it
     * through HAP_OFF_SYM with a +0x8000 index on purpose, so do the same. */
    a = (uint32_t)((hap_gi16(m, HAP_OFF_SAVE36) + 0x8000) & 0xffff);
    b = (uint32_t)((hap_gi16(m, HAP_OFF_SAVE34) + 0x8000) & 0xffff);
    for (j = hap_gi16(m, HAP_OFF_SAVE32); j != 0; j--) {
        hap_p8(m, HAP_OFF_SYM + (int32_t)a, hap_g8(m, HAP_OFF_SYM + (int32_t)b));
        a = (a + 1U) & 0xffffU;
        b = (b + 1U) & 0xffffU;
    }
    hap_p8(m, HAP_OFF_SYM + (int32_t)a, 1U);

    source = (uint32_t)((hap_gi16(m, HAP_OFF_SAVE34) * 2) & 0xffff);
    dest = (uint32_t)((hap_gi16(m, HAP_OFF_SAVE36) * 2) & 0xffff);
    result = hap_g16(m, HAP_OFF_SAVE32);
    {
        uint32_t step;
        for (step = result; step != 0U; step--) {
            hap_p16(m, HAP_OFF_CHILD + (int32_t)dest,
                    hap_g16(m, HAP_OFF_CHILD + (int32_t)source));
            dest = (dest + 2U) & 0xffffU;
            source = (source + 2U) & 0xffffU;
        }
    }
    hap_p16(m, HAP_OFF_CHILD + (int32_t)dest, 0xffffU);
    hap_p16(m, HAP_OFF_CHILD + (int32_t)((hap_gi16(m, HAP_OFF_SAVE34) * 2) & 0xffff),
            spare);
    return (int32_t)result;
}

/* Unwinds the frequency stack.  The original also uses the restored pointer as
 * a sanity check and downgrades the result to "symbol coded" when it does not
 * come back to 0x8000; that behaviour is reproduced verbatim. */
static int32_t hap_tail(hap_model *m, int32_t result, uint32_t extra)
{
    uint32_t v = (extra + (uint32_t)hap_gi16(m, HAP_OFF_LISTCOUNT) + 1U) & 0xffffU;
    v = (v * 2U) & 0xffffU;
    hap_p16(m, HAP_OFF_STACKPTR, hap_g16(m, HAP_OFF_STACKPTR) + v);
    if (hap_g16(m, HAP_OFF_STACKPTR) != 0x8000U) result = 0;
    return result;
}

/* Returns 0 when a symbol was decoded into HAP_OFF_SYMBOL, -4 on an escape and
 * -9 when the context has no codable symbol left. */
static int32_t hap_decode_symbol(hap_model *m)
{
    int32_t level;
    int32_t node2;
    int32_t count;
    int32_t start;
    uint32_t index;
    uint32_t list_count;
    uint32_t total = 0U;
    uint32_t symbols = 0U;
    bool zero = false;
    uint32_t span;
    uint32_t hi;
    uint32_t lo;
    uint32_t denominator;
    uint32_t target;
    uint32_t cursor;
    uint32_t steps = 0U;
    uint32_t position;

    hap_p16(m, HAP_OFF_STACKPTR, 0x8000U);
    level = (int32_t)hap_g16(m, HAP_OFF_LEVEL);
    node2 = (int32_t)((uint32_t)(hap_gi16(m, HAP_OFF_NODE + level) * 2) & 0xffffU);
    count = hap_gi16(m, HAP_OFF_NODECOUNT + node2);
    start = hap_gi16(m, HAP_OFF_NODESTART + node2);
    hap_p16(m, HAP_OFF_LEVELSTART + level, (uint32_t)start);
    hap_p16(m, HAP_OFF_LISTSTART, (uint32_t)start);
    index = (uint32_t)((start + count) & 0xffff);
    list_count = (uint32_t)((count + 1) & 0xffff);
    hap_p16(m, HAP_OFF_LISTCOUNT, list_count);
    hap_p16(m, HAP_OFF_LEVELCOUNT + level, list_count);

    for (;;) {
        uint32_t n = list_count;
        uint32_t with_escape;
        uint32_t adjusted;
        symbols = 0U;
        total = 0U;
        for (;;) {
            uint32_t symbol;
            uint32_t weight;
            hap_push(m, total);
            symbol = hap_g8(m, HAP_OFF_SYM + (int32_t)index);
            weight = hap_g8(m, HAP_OFF_EXCL + (int32_t)symbol) &
                     hap_g8(m, HAP_OFF_SYM + (int32_t)((index + 0x8000U) & 0xffffU));
            index = (index - 1U) & 0xffffU;
            if (weight != 0U) {
                symbols = (symbols + 1U) & 0xffffU;
                total = (total + weight) & 0xffffU;
            }
            n = (n - 1U) & 0xffffU;
            if (n == 0U) break;
        }
        hap_push(m, total);
        if (total == 0U) {
            zero = true;
            break;
        }
        if (hap_g16(m, HAP_OFF_LEVEL) == 0U) break;
        with_escape = (total + symbols) & 0xffffU;
        adjusted = with_escape;
        if ((with_escape & 1U) && (symbols != 1U))
            adjusted = (with_escape - 1U) & 0xffffU;
        if (adjusted < 0x3fffU) {
            total = adjusted;
            break;
        }
        /* Halve every count in this context and rebuild the frequency stack. */
        {
            int32_t j = hap_gi16(m, HAP_OFF_LISTCOUNT);
            uint32_t p;
            hap_p16(m, HAP_OFF_STACKPTR,
                    hap_g16(m, HAP_OFF_STACKPTR) + (uint32_t)(j * 2));
            p = hap_g16(m, HAP_OFF_LISTSTART);
            while (j != 0) {
                hap_p8(m, HAP_OFF_CNT + (int32_t)p,
                       hap_g8(m, HAP_OFF_CNT + (int32_t)p) >> 1);
                p = (p + 1U) & 0xffffU;
                j--;
            }
            index = (p - 1U) & 0xffffU;
            list_count = hap_g16(m, HAP_OFF_LISTCOUNT);
        }
    }

    if (zero) return hap_tail(m, -9, 0U);

    hap_push(m, total);
    hap_p16(m, HAP_OFF_TOTAL, total);

    span = (hap_g16(m, HAP_OFF_VALUE) - hap_g16(m, HAP_OFF_LOW) + 1U) & 0xffffU;
    hi = 0U;
    lo = 0U;
    if (span == 0U) {
        hi = (total - 1U) & 0xffffU;
        lo = 0xffffU;
    } else {
        uint32_t product = span * total - 1U; /* wraps at 32 bits, as in the reference */
        lo = product & 0xffffU;
        hi = (product >> 16) & 0xffffU;
    }
    denominator = (hap_g16(m, HAP_OFF_HIGH) - hap_g16(m, HAP_OFF_LOW) + 1U) & 0xffffU;
    if (denominator == 0U) {
        target = hi & 0xffffU;
    } else {
        target = (((hi << 16) + lo) / denominator) & 0xffffU;
    }

    cursor = hap_g16(m, HAP_OFF_STACKPTR);
    for (;;) {
        steps = (steps + 1U) & 0xffffU;
        cursor = (cursor + 2U) & 0xffffU;
        if (!(target < hap_g16(m, HAP_OFF_STACK - 2 + (int32_t)cursor))) break;
    }
    cursor = (cursor - 4U) & 0xffffU;
    hap_p16(m, HAP_OFF_CUMHIGH, hap_g16(m, HAP_OFF_STACK + (int32_t)cursor));
    hap_p16(m, HAP_OFF_CUMLOW, hap_g16(m, HAP_OFF_STACK + 2 + (int32_t)cursor));

    if (cursor == hap_g16(m, HAP_OFF_STACKPTR)) {
        /* Escape: exclude everything this context could have coded, logging the
         * symbols so the exclusions can be undone before the next byte. */
        uint32_t r = hap_g16(m, HAP_OFF_EXCLTOP);
        int32_t j = (int32_t)hap_g16(m, HAP_OFF_LISTCOUNT);
        uint32_t p = hap_g16(m, HAP_OFF_LISTSTART);
        while (j != 0) {
            if (hap_g8(m, HAP_OFF_CNT + (int32_t)p) != 0U) {
                uint32_t symbol = hap_g8(m, HAP_OFF_SYM + (int32_t)p);
                if (hap_g8(m, HAP_OFF_EXCL + (int32_t)symbol) != 0U) {
                    hap_p8(m, HAP_OFF_EXCL + (int32_t)r, symbol);
                    hap_p8(m, HAP_OFF_EXCL + (int32_t)symbol, 0U);
                    r = (r + 1U) & 0xffffU;
                }
            }
            p = (p + 1U) & 0xffffU;
            j--;
        }
        hap_p32(m, HAP_OFF_EXCLTOP, r);
        return hap_tail(m, -4, 1U);
    }

    position = ((steps - 3U) + hap_g16(m, HAP_OFF_LISTSTART)) & 0xffffU;
    hap_p8(m, HAP_OFF_SYMBOL, hap_g8(m, HAP_OFF_SYM + (int32_t)position));
    if (hap_g8(m, HAP_OFF_CNT + (int32_t)position) == 0xffU) {
        int32_t j = hap_gi16(m, HAP_OFF_LISTCOUNT);
        uint32_t q = hap_g16(m, HAP_OFF_LISTSTART);
        while (j != 0) {
            hap_p8(m, HAP_OFF_CNT + (int32_t)q,
                   hap_g8(m, HAP_OFF_CNT + (int32_t)q) >> 1);
            q = (q + 1U) & 0xffffU;
            j--;
        }
    }
    hap_p8(m, HAP_OFF_CNT + (int32_t)position,
           hap_g8(m, HAP_OFF_CNT + (int32_t)position) + 1U);
    return hap_tail(m, 0, 1U);
}

static bool hap_decode_stream(hap_model *m, uint8_t *output, size_t output_size,
                              size_t *produced)
{
    size_t remaining = output_size;
    uint32_t primed;

    if (output_size == 0U) return true;
    if (m->input_pos + 2U > m->input_size) return false;
    primed = (uint32_t)m->input[m->input_pos] |
             ((uint32_t)m->input[m->input_pos + 1] << 8);
    m->input_pos += 2U;
    hap_p16(m, HAP_OFF_VALUE, ((primed & 0xffU) << 8) | (primed >> 8));
    hap_p8(m, HAP_OFF_BITCOUNT, 0U);
    hap_p16(m, HAP_OFF_LOW, 0U);
    hap_p16(m, HAP_OFF_HIGH, 0xffffU);
    hap_init(m);

    while (remaining > 0U) {
        uint32_t u;
        int32_t n;
        int32_t guard;
        bool restart = false;
        uint32_t symbol;
        int32_t c;

        hap_p16(m, HAP_OFF_LEVEL, 8U);

        u = 0x100U;
        n = (int32_t)hap_g32(m, HAP_OFF_EXCLTOP) - 0x100;
        if (n != 0) {
            while (n > 0) {
                /* The escape path stored 0 in EXCL[symbol]; the restore
                 * DECREMENTS, which wraps 0 back to 0xff.  Deliberate. */
                uint32_t t = hap_g8(m, HAP_OFF_EXCL + (int32_t)u);
                hap_p8(m, HAP_OFF_EXCL + (int32_t)t,
                       hap_g8(m, HAP_OFF_EXCL + (int32_t)t) - 1U);
                u = (u + 1U) & 0xffffU;
                n--;
            }
            hap_p32(m, HAP_OFF_EXCLTOP, 0x100U);
        }
        /* The order-0 context always exists, so this walk always stops; the
         * counter only keeps a corrupt state from indexing off the block. */
        for (guard = 0; guard < 5; guard++) {
            if (hap_gi16(m, HAP_OFF_NODE + (int32_t)hap_g16(m, HAP_OFF_LEVEL)) != -1)
                break;
            hap_p16(m, HAP_OFF_MARKER + (int32_t)hap_g16(m, HAP_OFF_LEVEL), 0xffffU);
            if (hap_g16(m, HAP_OFF_LEVEL) == 0U) return false;
            hap_p16(m, HAP_OFF_LEVEL, hap_g16(m, HAP_OFF_LEVEL) - 2U);
        }

        for (;;) {
            int32_t r = hap_decode_symbol(m);
            if (r == 0) break;
            if (r != -9) {
                if (!hap_arith_update(m)) return false;
            }
            hap_p16(m, HAP_OFF_MARKER + (int32_t)hap_g16(m, HAP_OFF_LEVEL), 0xfffcU);
            if (hap_g16(m, HAP_OFF_LEVEL) == 0U) return false;
            hap_p16(m, HAP_OFF_LEVEL, hap_g16(m, HAP_OFF_LEVEL) - 2U);
        }

        if (!hap_arith_update(m)) return false;
        hap_p16(m, HAP_OFF_MARKER + (int32_t)hap_g16(m, HAP_OFF_LEVEL), 0U);
        output[*produced] = (uint8_t)hap_g8(m, HAP_OFF_SYMBOL);
        (*produced)++;
        remaining--;

        if (hap_g16(m, HAP_OFF_LEVEL) != 8U) {
            for (;;) {
                int32_t level = (int32_t)hap_g16(m, HAP_OFF_LEVEL);
                if (hap_gi16(m, HAP_OFF_MARKER + level) == -4) hap_fixup_escape(m);
                if (hap_gi16(m, HAP_OFF_MARKER + level) == -1) {
                    int32_t r = hap_new_context(m);
                    int32_t previous;
                    int32_t a;
                    int32_t b;
                    int32_t offset;
                    if (r < 0) {
                        hap_init(m);
                        restart = true;
                        break;
                    }
                    hap_p16(m, HAP_OFF_NODE + level, (uint32_t)r);
                    previous = (int32_t)((uint32_t)(level - 2) & 0xffffU);
                    a = hap_gi16(m, HAP_OFF_SAVEA3 + previous);
                    b = hap_gi16(m, HAP_OFF_SAVEAD + previous);
                    offset = hap_gi16(m,
                        HAP_OFF_NODESTART + (int32_t)((uint32_t)(a * 2) & 0xffffU));
                    hap_p16(m,
                        HAP_OFF_CHILD + (int32_t)((uint32_t)((offset + b) * 2) & 0xffffU),
                        (uint32_t)r);
                } else if (hap_gi16(m, HAP_OFF_MARKER + level) == -6) {
                    if (hap_grow_context(m) < 0) {
                        hap_init(m);
                        restart = true;
                        break;
                    }
                }
                hap_p16(m, HAP_OFF_LEVEL, hap_g16(m, HAP_OFF_LEVEL) + 2U);
                if (!(hap_g16(m, HAP_OFF_LEVEL) < 9U)) break;
            }
            if (restart) continue;
        }

        symbol = hap_g8(m, HAP_OFF_SYMBOL);
        hap_p16(m, HAP_OFF_MATCHPOS + 2, symbol);
        u = 4U;
        for (;;) {
            uint32_t base = hap_g16(m, HAP_OFF_NODESTART +
                (int32_t)((uint32_t)(hap_gi16(m, HAP_OFF_NODE + (int32_t)u) * 2) & 0xffffU));
            uint32_t j = base;
            for (c = 0x100; c > 0; c--) {
                if (symbol == hap_g8(m, HAP_OFF_SYM + (int32_t)j)) break;
                j = (j + 1U) & 0xffffU;
            }
            hap_p16(m, HAP_OFF_MATCHPOS + (int32_t)u, (j - base) & 0xffffU);
            u = (u + 2U) & 0xffffU;
            if (!(u < 8U)) break;
        }
        u = 6U;
        for (c = 0; c < 3; c++) {
            int32_t offset;
            hap_p16(m, HAP_OFF_SAVEA3 + (int32_t)u,
                    hap_g16(m, HAP_OFF_NODE + (int32_t)u));
            offset = hap_gi16(m, HAP_OFF_NODESTART +
                (int32_t)((uint32_t)(hap_gi16(m, HAP_OFF_NODE + (int32_t)u) * 2) & 0xffffU));
            hap_p16(m, HAP_OFF_SAVEAD + (int32_t)u,
                    hap_g16(m, HAP_OFF_MATCHPOS + (int32_t)u));
            hap_p16(m, HAP_OFF_NODE + (int32_t)u + 2,
                    hap_g16(m, HAP_OFF_CHILD +
                        (int32_t)((uint32_t)((offset +
                            hap_gi16(m, HAP_OFF_MATCHPOS + (int32_t)u)) * 2) & 0xffffU)));
            u = (u - 2U) & 0xffffU;
        }
    }
    return true;
}

bool xx_hap_decode_memory(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size, size_t *written)
{
    hap_model model;
    size_t produced = 0U;
    bool result;

    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U))
        return false;
    if (output_size > HAP_MAX_OUTPUT) return false;
    if (output_size == 0U) return true;
    if (input_size == 0U) return false;

    /* ~330 KiB of model state; heap, never static, so the decoder is
     * re-entrant and thread-safe. */
    model.s = (uint8_t *)xx_mem_alloc(HAP_STATE_SIZE);
    if (!model.s) return false;
    xx_rt_memset(model.s, 0, HAP_STATE_SIZE);
    model.input = input;
    model.input_size = input_size;
    model.input_pos = 0U;

    result = hap_decode_stream(&model, output, output_size, &produced);
    xx_mem_free(model.s);

    if (!result || (produced != output_size)) return false;
    if (written) *written = produced;
    return true;
}
