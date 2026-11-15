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

#include "xxfclib/algo/md2/xx_md2.h"
#include <string.h>

/* Permutation table S derived from the digits of pi (RFC 1319 Section 3.1) */
static const uint8_t md2_pi_subst[256] = {
    41,  46,  67, 201, 162, 216, 124,   1,  61,  54,  84, 161, 236, 240,   6,  19,
    98, 167,   5, 243, 192, 199, 115, 140, 152, 147,  43, 217, 188,  76, 130, 202,
    30, 155,  87,  60, 253, 212, 224,  22, 103,  66, 111,  24, 138,  23, 229,  18,
   190,  78, 196, 214, 218, 158, 222,  73, 160, 251, 245, 142, 187,  47, 238, 122,
   169, 104, 121, 145,  21, 178,   7,  63, 148, 194,  16, 137,  11,  34,  95,  33,
   128, 127,  93, 154,  90, 144,  50,  39,  53,  62, 204, 231, 191, 247, 151,   3,
   255,  25,  48, 179,  72, 165, 181, 209, 215,  94, 146,  42, 172,  86, 170, 198,
    79, 184,  56, 210, 150, 164, 125, 182, 118, 252, 107, 226, 156, 116,   4, 241,
    69, 157, 112,  89, 100, 113, 135,  32, 134,  91, 207, 101, 230,  45, 168,   2,
    27,  96,  37, 173, 174, 176, 185, 246,  28,  70,  97, 105,  52,  64, 126,  15,
    85,  71, 163,  35, 221,  81, 175,  58, 195,  92, 249, 206, 186, 197, 234,  38,
    44,  83,  13, 110, 133,  40, 132,   9, 211, 223, 205, 244,  65, 129,  77,  82,
   106, 220,  55, 200, 108, 193, 171, 250,  36, 225, 123,   8,  12, 189, 177,  74,
   120, 136, 149, 139, 227,  99, 232, 109, 233, 203, 213, 254,  59,   0,  29,  57,
   242, 239, 183,  14, 102,  88, 208, 228, 166, 119, 114, 248, 235, 117,  75,  10,
    49,  68,  80, 180, 143, 237,  31,  26, 219, 153, 141,  51, 159,  17, 131,  20
};

static void md2_transform_block(uint8_t state[48], const uint8_t block[16])
{
    for (size_t i = 0; i < 16; ++i) {
        state[16 + i] = block[i];
        state[32 + i] = block[i] ^ state[i];
    }
    uint8_t t = 0;
    for (size_t round = 0; round < 18; ++round) {
        for (size_t k = 0; k < 48; ++k) {
            t = state[k] ^ md2_pi_subst[t];
            state[k] = t;
        }
        t = (uint8_t)((t + (uint8_t)round) & 0xFF);
    }
}

static void md2_update_checksum(uint8_t checksum[16], const uint8_t block[16])
{
    uint8_t l = checksum[15];
    for (size_t i = 0; i < 16; ++i) {
        checksum[i] ^= md2_pi_subst[block[i] ^ l];
        l = checksum[i];
    }
}

static void md2_process_block(xx_md2_context *ctx, const uint8_t block[16])
{
    md2_update_checksum(ctx->checksum, block);
    md2_transform_block(ctx->state, block);
}

static void md2_clear(void *data, size_t size)
{
    volatile uint8_t *p = (volatile uint8_t *)data;
    while (size-- != 0U) *p++ = 0U;
}

bool xx_md2_init(xx_md2_context *context)
{
    if (!context) return false;
    memset(context, 0, sizeof(*context));
    context->initialized = true;
    return true;
}

bool xx_md2_update(xx_md2_context *context, const void *data, size_t size)
{
    const uint8_t *input = (const uint8_t *)data;
    if (!context || !context->initialized || context->buffer_size >= 16U || (!data && size)) return false;
    if (!size) return true;
    if (context->buffer_size) {
        size_t need = 16U - context->buffer_size;
        size_t take = size < need ? size : need;
        memcpy(context->buffer + context->buffer_size, input, take);
        context->buffer_size += take;
        input += take;
        size -= take;
        if (context->buffer_size == 16U) {
            md2_process_block(context, context->buffer);
            context->buffer_size = 0U;
        }
    }
    while (size >= 16U) {
        md2_process_block(context, input);
        input += 16U;
        size -= 16U;
    }
    if (size) {
        memcpy(context->buffer, input, size);
        context->buffer_size = size;
    }
    return true;
}

bool xx_md2_final(xx_md2_context *context, void *digest, size_t digest_size)
{
    uint8_t padding[16];
    size_t pad;
    if (!context || !context->initialized || context->buffer_size >= 16U || !digest || digest_size < 16U) return false;
    pad = 16U - context->buffer_size;
    memset(padding, (int)pad, pad);
    (void)xx_md2_update(context, padding, pad);
    md2_transform_block(context->state, context->checksum);
    memcpy(digest, context->state, 16U);
    md2_clear(context, sizeof(*context));
    md2_clear(padding, sizeof(padding));
    return true;
}

bool xx_md2_memory(const void *data, size_t size, void *digest)
{
    xx_md2_context context;
    if ((!data && size) || !digest) return false;
    (void)xx_md2_init(&context);
    (void)xx_md2_update(&context, data, size);
    return xx_md2_final(&context, digest, 16U);
}
