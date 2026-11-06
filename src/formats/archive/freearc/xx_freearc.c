/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * FreeArc archive reader.
 *
 * An archive is a run of blocks. Control blocks (header, directory, footer,
 * recovery) are each followed by a local descriptor; solid data blocks are
 * not, the directory block names them instead.
 *
 *   0..7   header block data: "ArC\x01", uint16 LE flags, uint16 LE version
 *   8..    the header block's local descriptor, which repeats "ArC\x01"
 *   ...    solid data blocks
 *   ...    directory block (packed) + its local descriptor
 *   ...    footer block (packed) + its local descriptor, near the end
 *
 * Local descriptor:
 *   "ArC\x01", num type, method string, num unpacked, num packed,
 *   u32 CRC-32 of the unpacked data, u32 CRC-32 of the descriptor so far.
 *   The block's data ends where its descriptor starts.
 *
 * Numbers are little-endian variable-length: the count of low one-bits in
 * the first byte is the count of extra bytes (0..7), and the value is the
 * whole little-endian word shifted right by (extra + 1); eight one-bits mean
 * an 8-byte value follows. Strings are NUL-terminated UTF-8.
 *
 * Footer data: num count, then per control block {num type, method,
 * num (footer offset - block offset), num unpacked, num packed, u32 CRC},
 * then the lock flag and comments (ignored here).
 * Directory data: num nblocks; nblocks x num files-in-block; nblocks x
 * method; nblocks x num (directory offset - block offset); nblocks x num
 * packed size; num ndirs; ndirs x directory name; nfiles x file name;
 * nfiles x num directory index; nfiles x num size; nfiles x u32 time;
 * nfiles x byte is-folder; nfiles x u32 CRC; optional trailing fields.
 * Members fill their solid block in order.
 *
 * Method chains ("rep:1mb+exe+delta+lzma:32kb") are undone right to left.
 * The chain decoder follows XArchive's Algos/xfreearcdecoder.cpp and the
 * container walk follows archives/xfreearcnative.cpp (both MIT, same
 * author). The x86 converter follows Igor Pavlov's public-domain Bra86.c
 * (LZMA SDK). DELTA: the decompression part of Bulat Ziganshin's binary
 * tables preprocessor, whose notice reads: "Delta: binary tables
 * preprocessor v1.51 (c) Bulat.Ziganshin@gmail.com 2013-09-18. All rights
 * reserved. You can for free use decompression part of the algorithm for
 * decompression of FreeArc archives." Only that decompression part is here
 * and it is used only to decompress FreeArc archives.
 *
 * Tornado: a bounded coder-3/minimum-match-2 Huffman profile without table
 * transforms is implemented here. The six-byte stream header is authoritative.
 * Other methods (ppmd, grzip, dict, mm, other encryption...) are not
 * implemented: such blocks are listed but their members do not unpack.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/archive/freearc/xx_freearc.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/lz4/xx_lz4.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/sha/xx_sha.h"
#include "../../../algo/aes/xx_aes_internal.h"
/* SHA-512 already lives in this private interoperability module. Reuse its
 * standard FIPS 180-4 digest rather than duplicate a cryptographic primitive. */
#include "../dgca/dgca_cipher.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef FREEARC
#define XX_FREEARC_FILE_TYPE XX_FILE_TYPE_FREEARC
#else
#define XX_FREEARC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_FREEARC_SIGNATURE_SIZE 4U
#define XX_FREEARC_HEADER_SIZE 8
#define XX_FREEARC_MIN_SIZE (XX_FREEARC_HEADER_SIZE + (int)XX_FREEARC_SIGNATURE_SIZE)

#define FA_TAIL_SIZE 4096
#define FA_MAX_CONTROL ((int64_t)64 * 1024 * 1024)
#define FA_MAX_SOLID ((int64_t)256 * 1024 * 1024)
#define FA_MAX_STAGE ((int64_t)336 * 1024 * 1024)
#define FA_MAX_ENTRIES 1000000U
#define FA_MAX_CONTROL_BLOCKS 100000U
#define FA_MAX_NAME 32768U
#define FA_MAX_NAME_BYTES ((uint64_t)256 * 1024 * 1024)
#define FA_MAX_STAGES 16
#define FA_MAX_METHOD 1024U
#define FA_LZP_MAX_TABLE_INIT ((uint64_t)64 * 1024 * 1024)
#define FA_MAX_KDF_ROUNDS 1000000U
#define FA_MAX_PASSWORD 1048576U

#define FA_BLOCK_HEADER 1U
#define FA_BLOCK_DIR 3U
#define FA_BLOCK_FOOTER 4U

static const uint8_t XX_FREEARC_MAGIC[XX_FREEARC_SIGNATURE_SIZE] = {'A', 'r', 'C', 0x01U};

static void xx_freearc_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------ io */

static bool fa_read_dev(xx_io_device *device, int64_t offset, uint8_t *buffer, size_t size)
{
    size_t completed = 0U;

    if (!device || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (completed < size) {
        size_t want = size - completed;
        ssize_t received;
        if (want > 0x100000U) want = 0x100000U;
        received = xx_io_read(device, buffer + completed, want);
        if (received <= 0 || (size_t)received > want) return false;
        completed += (size_t)received;
    }
    return true;
}

static bool xx_freearc_read_at(Abstractformat *self, int64_t offset, uint8_t *buffer, size_t size)
{
    if (!self) return false;
    return fa_read_dev(self->device, offset, buffer, size);
}

static uint32_t fa_crc32(const uint8_t *data, size_t size)
{
    return xx_crc32(XX_CRC_TYPE_CRC32, data, size);
}

static bool fa_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static void fa_secure_clear(void *data, size_t size)
{
    volatile uint8_t *p = (volatile uint8_t *)data;
    while (size--) *p++ = 0U;
}

/* Keep no plaintext password in a reader cache. Presence distinguishes a
 * deliberately empty password from a password that has not been supplied. */
static void fa_password_identity(const char *password, uint8_t fingerprint[32])
{
    xx_sha256_memory(password, password ? xx_str_len(password) : 0U, fingerprint);
}

/* -------------------------------------------------------------- cursor */

typedef struct fa_cursor {
    const uint8_t *p;
    size_t size;
    size_t pos;
} fa_cursor;

static bool fa_num(fa_cursor *c, uint64_t *value)
{
    uint8_t first;
    unsigned extra = 0U, i;
    uint64_t result = 0U;

    if (c->pos >= c->size) return false;
    first = c->p[c->pos];
    while (extra < 8U && (first & (1U << extra))) ++extra;
    if (extra == 8U) {
        if (c->size - c->pos < 9U) return false;
        for (i = 0U; i < 8U; ++i) result |= (uint64_t)c->p[c->pos + 1U + i] << (8U * i);
        c->pos += 9U;
    } else {
        if (c->size - c->pos < (size_t)extra + 1U) return false;
        for (i = 0U; i <= extra; ++i) result |= (uint64_t)c->p[c->pos + i] << (8U * i);
        result >>= extra + 1U;
        c->pos += (size_t)extra + 1U;
    }
    if (result > (uint64_t)INT64_MAX) return false;
    *value = result;
    return true;
}

static bool fa_u32(fa_cursor *c, uint32_t *value)
{
    if (c->size - c->pos < 4U || c->pos > c->size) return false;
    *value = xx_data_get_u32(c->p + c->pos, 4, 0, false);
    c->pos += 4U;
    return true;
}

static bool fa_byte(fa_cursor *c, uint8_t *value)
{
    if (c->pos >= c->size) return false;
    *value = c->p[c->pos++];
    return true;
}

/* A NUL-terminated string of at most @p limit bytes; *s points into the
 * buffer. */
static bool fa_str(fa_cursor *c, const char **s, size_t *length, size_t limit)
{
    size_t i;
    for (i = c->pos; i < c->size; ++i) {
        if (c->p[i] == 0U) {
            if (i - c->pos > limit) return false;
            *s = (const char *)(c->p + c->pos);
            *length = i - c->pos;
            c->pos = i + 1U;
            return true;
        }
        if (i - c->pos > limit) return false;
    }
    return false;
}

static char *fa_strndup(const char *s, size_t length)
{
    char *copy = (char *)xx_mem_alloc(length + 1U);
    if (!copy) return NULL;
    if (length) xx_rt_memcpy(copy, s, length);
    copy[length] = 0;
    return copy;
}

/* ------------------------------------------------------ method parsing */

typedef enum fa_kind {
    FA_STORE,
    FA_LZMA,
    FA_REP,
    FA_EXE,
    FA_DELTA,
    FA_LZP,
    FA_AES256_CTR,
    FA_4X4_LZ4,
    FA_TOR_HUF3
} fa_kind;

typedef struct fa_stage {
    fa_kind kind;
    uint32_t lc, lp, pb;
    uint64_t dictionary; /* 0 = not stated */
    uint32_t lzp_min_match, lzp_hash_bits, lzp_smallest;
    uint64_t lzp_barrier;
    uint32_t iterations;
    uint8_t salt[64], check[32], iv[16];
    size_t salt_size, check_size;
    bool fixed_hex;
} fa_stage;

static bool fa_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool fa_decimal(const char *s, size_t n, uint64_t *value)
{
    uint64_t result = 0U;
    size_t i;
    if (n && s[0] == '=') {
        ++s;
        --n;
    }
    if (!n || n > 19U) return false;
    for (i = 0U; i < n; ++i) {
        if (!fa_is_digit(s[i])) return false;
        result = result * 10U + (uint64_t)(s[i] - '0');
    }
    *value = result;
    return true;
}

static bool fa_ends(const char *s, size_t n, const char *suffix, size_t k)
{
    return n >= k && xx_rt_memcmp(s + n - k, suffix, k) == 0;
}

/* FreeArc memory sizes: "96mb", "1m", "32kb", "4k", "123b", "24" (a power
 * of two), "24^". */
static bool fa_memory(const char *s, size_t n, uint64_t *value)
{
    uint64_t multiplier = 1U, number = 0U;
    bool power = false;
    if (fa_ends(s, n, "gb", 2U)) {
        multiplier = (uint64_t)1 << 30;
        n -= 2U;
    } else if (fa_ends(s, n, "g", 1U)) {
        multiplier = (uint64_t)1 << 30;
        n -= 1U;
    } else if (fa_ends(s, n, "mb", 2U)) {
        multiplier = (uint64_t)1 << 20;
        n -= 2U;
    } else if (fa_ends(s, n, "m", 1U)) {
        multiplier = (uint64_t)1 << 20;
        n -= 1U;
    } else if (fa_ends(s, n, "kb", 2U)) {
        multiplier = 1024U;
        n -= 2U;
    } else if (fa_ends(s, n, "k", 1U)) {
        multiplier = 1024U;
        n -= 1U;
    } else if (fa_ends(s, n, "b", 1U)) {
        n -= 1U;
    } else {
        power = true;
        if (fa_ends(s, n, "^", 1U)) n -= 1U;
    }
    if (!fa_decimal(s, n, &number)) return false;
    if (power) {
        if (number >= 63U) return false;
        *value = (uint64_t)1 << number;
    } else {
        if (number > UINT64_MAX / multiplier) return false;
        *value = number * multiplier;
    }
    return *value != 0U;
}

static bool fa_word_is(const char *s, size_t n, const char *word)
{
    size_t k = xx_str_len(word);
    return n == k && xx_rt_memcmp(s, word, k) == 0;
}

static int fa_hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool fa_hex(const char *s, size_t n, uint8_t *out, size_t capacity, size_t *written)
{
    size_t i;
    if ((n & 1U) || n / 2U > capacity) return false;
    for (i = 0U; i < n; i += 2U) {
        int a = fa_hex_digit(s[i]), b = fa_hex_digit(s[i + 1U]);
        if (a < 0 || b < 0) return false;
        out[i / 2U] = (uint8_t)((unsigned)a * 16U + (unsigned)b);
    }
    *written = n / 2U;
    return true;
}

static bool fa_parse_aes(const char *s, size_t n, size_t colon, fa_stage *stage)
{
    unsigned seen = 0U;
    stage->kind = FA_AES256_CTR;
    stage->iterations = 1000U;
    while (colon < n) {
        size_t start = ++colon, len, bytes = 0U;
        uint64_t value;
        unsigned flag;
        while (colon < n && s[colon] != ':') ++colon;
        len = colon - start;
        if (!len) return false;
        switch (s[start]) {
            case 'f': flag = 1U; if (len != 1U) return false; stage->fixed_hex = true; break;
            case 'n':
                flag = 2U;
                if (!fa_decimal(s + start + 1U, len - 1U, &value) || !value || value > FA_MAX_KDF_ROUNDS) return false;
                stage->iterations = (uint32_t)value;
                break;
            case 'r':
                flag = 4U;
                if (!fa_decimal(s + start + 1U, len - 1U, &value) || (value != 0U && value != 14U)) return false;
                break;
            case 's':
                flag = 8U;
                if (!fa_hex(s + start + 1U, len - 1U, stage->salt, sizeof(stage->salt), &stage->salt_size)) return false;
                break;
            case 'c':
                flag = 16U;
                if (!fa_hex(s + start + 1U, len - 1U, stage->check, sizeof(stage->check), &stage->check_size)) return false;
                break;
            case 'i':
                flag = 32U;
                if (!fa_hex(s + start + 1U, len - 1U, stage->iv, sizeof(stage->iv), &bytes) || bytes != sizeof(stage->iv)) return false;
                break;
            default: return false;
        }
        if (seen & flag) return false;
        seen |= flag;
    }
    /* Require the archive's complete IV. Empty/missing salt or verifier
     * retain upstream semantics; raw-key :k methods are unsupported. */
    return (seen & 32U) != 0U;
}

static bool fa_parse_stage(const char *s, size_t n, fa_stage *stage)
{
    size_t colon = 0U;
    const char *name = s;
    size_t name_len;
    while (colon < n && s[colon] != ':') ++colon;
    name_len = colon;
    xx_mem_zero(stage, sizeof(*stage));
    stage->lc = 3U;
    stage->lp = 0U;
    stage->pb = 2U;
    if (fa_word_is(name, name_len, "aes-256/ctr")) return fa_parse_aes(s, n, colon, stage);
    if (fa_word_is(name, name_len, "storing") || fa_word_is(name, name_len, "exe")) {
        stage->kind = name[0] == 's' ? FA_STORE : FA_EXE;
        return colon == n;
    }
    if (fa_word_is(name, name_len, "rep") || fa_word_is(name, name_len, "delta")) {
        stage->kind = name[0] == 'r' ? FA_REP : FA_DELTA;
        /* Encoder buffer options do not change the stream framing. */
        while (colon < n) {
            size_t start = ++colon;
            uint64_t ignored;
            while (colon < n && s[colon] != ':') ++colon;
            /* REP h/a select encoder searches, not decoder memory or framing.
             * The decoder obtains its actual window from the stream header. */
            if (stage->kind == FA_REP && colon - start > 1U && (s[start] == 'h' || s[start] == 'a')) {
                if (!fa_decimal(s + start + 1U, colon - start - 1U, &ignored)) return false;
                continue;
            }
            if (colon - start > 0U && s[start] == 'b') ++start;
            if (!fa_memory(s + start, colon - start, &ignored)) return false;
        }
        return true;
    }
    if (fa_word_is(name, name_len, "tor")) {
        stage->kind = FA_TOR_HUF3;
        /* Encoder buffer/hash/coder options are not decoder allocations.
         * Only the admitted in-stream header chooses this bounded decoder. */
        while (colon < n) {
            size_t start = ++colon, length;
            uint64_t ignored;
            while (colon < n && s[colon] != ':') ++colon;
            length = colon - start;
            if (!length) return false;
            if (s[start] == 'c') {
                if (!fa_decimal(s + start + 1U, length - 1U, &ignored) || ignored > 4U) return false;
            } else {
                if (s[start] == 'h' || s[start] == 'b') { ++start; --length; }
                if (!fa_memory(s + start, length, &ignored)) return false;
            }
        }
        return true;
    }
    if (fa_word_is(name, name_len, "4x4")) {
        stage->kind = FA_4X4_LZ4;
        return colon < n && fa_word_is(s + colon + 1U, n - colon - 1U, "lz4");
    }
    if (fa_word_is(name, name_len, "lzp")) {
        stage->kind = FA_LZP;
        stage->dictionary = 8U * 1024U * 1024U;
        stage->lzp_min_match = 64U;
        stage->lzp_hash_bits = 18U;
        stage->lzp_barrier = INT32_MAX;
        stage->lzp_smallest = 32U;
        while (colon < n) {
            size_t start = ++colon, len;
            uint64_t v;
            while (colon < n && s[colon] != ':') ++colon;
            len = colon - start;
            if (!len) return false;
            if (s[colon - 1U] == '%') {
                if (!fa_decimal(s + start, len - 1U, &v) || v > 100U) return false;
            } else if (s[start] == 'b') {
                if (!fa_memory(s + start + 1U, len - 1U, &stage->dictionary)) return false;
            } else if (s[start] == 'd') {
                if (!fa_memory(s + start + 1U, len - 1U, &stage->lzp_barrier)) {
                    const char *value = s + start + 1U;
                    size_t length = len - 1U;
                    if (length < 2U || value[length - 1U] != 'b' || !fa_decimal(value, length - 1U, &v) || v) return false;
                    stage->lzp_barrier = 0U;
                }
            } else if (s[start] == 'l' || s[start] == 'h' || s[start] == 's') {
                if (!fa_decimal(s + start + 1U, len - 1U, &v) || v > UINT32_MAX) return false;
                if (s[start] == 'l') stage->lzp_min_match = (uint32_t)v;
                else if (s[start] == 'h') stage->lzp_hash_bits = (uint32_t)v;
                else stage->lzp_smallest = (uint32_t)v;
            } else if (fa_decimal(s + start, len, &v)) {
                if (v > UINT32_MAX) return false;
                stage->lzp_min_match = (uint32_t)v;
            } else if (!fa_memory(s + start, len, &stage->dictionary)) return false;
        }
        return stage->dictionary >= 1U && stage->dictionary <= FA_MAX_SOLID && stage->lzp_min_match >= 4U && stage->lzp_min_match <= FA_MAX_SOLID &&
               stage->lzp_smallest >= 4U && stage->lzp_smallest <= FA_MAX_SOLID && stage->lzp_hash_bits <= 20U && stage->lzp_barrier <= INT32_MAX;
    }
    if (!fa_word_is(name, name_len, "lzma")) return false;
    stage->kind = FA_LZMA;
    while (colon < n) {
        size_t start = ++colon, len;
        const char *o;
        uint64_t v = 0U;
        while (colon < n && s[colon] != ':') ++colon;
        o = s + start;
        len = colon - start;
        if (len && o[0] == '*') {
            ++o;
            --len;
        }
        if (!len) return false;
        if (fa_word_is(o, len, "fastest") || fa_word_is(o, len, "fast") || fa_word_is(o, len, "normal") || fa_word_is(o, len, "max") || fa_word_is(o, len, "ultra") ||
            fa_word_is(o, len, "ht4") || fa_word_is(o, len, "hc4") || fa_word_is(o, len, "bt2") || fa_word_is(o, len, "bt3") || fa_word_is(o, len, "bt4"))
            continue;
        if (len > 2U && (xx_rt_memcmp(o, "lc", 2U) == 0 || xx_rt_memcmp(o, "lp", 2U) == 0 || xx_rt_memcmp(o, "pb", 2U) == 0)) {
            if (!fa_decimal(o + 2, len - 2U, &v) || v > 8U) return false;
            if (o[0] == 'l' && o[1] == 'c') stage->lc = (uint32_t)v;
            else if (o[0] == 'l') stage->lp = (uint32_t)v;
            else stage->pb = (uint32_t)v;
        } else if (len > 2U && (xx_rt_memcmp(o, "fb", 2U) == 0 || xx_rt_memcmp(o, "mc", 2U) == 0)) {
            if (!fa_decimal(o + 2, len - 2U, &v)) return false;
        } else if (len > 2U && xx_rt_memcmp(o, "mf", 2U) == 0) {
            continue;
        } else if (o[0] == 'd' && len > 1U) {
            if (!fa_memory(o + 1, len - 1U, &stage->dictionary)) return false;
        } else if (o[0] == 'a' && len > 1U && fa_is_digit(o[1])) {
            if (!fa_decimal(o + 1, len - 1U, &v) || v > 2U) return false;
        } else if (o[0] == 'h' && len > 1U && fa_is_digit(o[1])) {
            if (!fa_memory(o + 1, len - 1U, &v)) return false;
        } else if (fa_decimal(o, len, &v)) {
            if (v < 5U || v > 273U) return false; /* encoder fast bytes */
        } else if (!fa_memory(o, len, &stage->dictionary)) {
            return false;
        }
    }
    return stage->lc <= 8U && stage->lp <= 4U && stage->pb <= 4U && stage->lc + stage->lp <= 4U;
}

static bool fa_parse_chain(const char *method, fa_stage *stages, size_t *count)
{
    size_t n, start = 0U, i;
    uint32_t kdf_rounds = 0U;
    if (!method) return false;
    n = xx_str_len(method);
    if (!n || n > FA_MAX_METHOD) return false;
    *count = 0U;
    for (i = 0U; i <= n; ++i) {
        if (i == n || method[i] == '+') {
            if (i == start || *count >= FA_MAX_STAGES || !fa_parse_stage(method + start, i - start, &stages[*count])) return false;
            if (stages[*count].kind == FA_AES256_CTR) {
                if (stages[*count].iterations > FA_MAX_KDF_ROUNDS - kdf_rounds) return false;
                kdf_rounds += stages[*count].iterations;
            }
            ++*count;
            start = i + 1U;
        }
    }
    return *count > 0U;
}

static bool fa_method_encrypted(const char *method)
{
    size_t at = 0U;
    if (!method) return false;
    do {
        size_t start = at;
        while (method[at] && method[at] != '+') ++at;
        if (at - start >= 11U && xx_rt_memcmp(method + start, "aes-256/ctr", 11U) == 0 &&
            (at - start == 11U || method[start + 11U] == ':')) return true;
        if (!method[at]) break;
        ++at;
    } while (method[at]);
    return false;
}

bool xx_freearc_method_supported(const char *method)
{
    fa_stage stages[FA_MAX_STAGES];
    size_t count;
    return fa_parse_chain(method, stages, &count);
}

/* -------------------------------------------------------------- codecs */

static uint8_t *fa_alloc(size_t size)
{
    return (uint8_t *)xx_mem_alloc(size ? size : 1U);
}

static bool fa_lzma(const fa_stage *st, const uint8_t *in, size_t in_size, int64_t expected, size_t cap, uint8_t **out, size_t *out_size)
{
    uint8_t props[5];
    uint64_t dict = (uint64_t)cap;
    size_t written = 0U;
    uint8_t *buffer;
    /* Every match distance is bounded by the bytes produced so far, so a
     * window the size of the output cap decodes any valid stream. */
    if (st->dictionary && st->dictionary < dict) dict = st->dictionary;
    if (dict < 4096U) dict = 4096U;
    if (dict > (uint64_t)FA_MAX_SOLID) dict = (uint64_t)FA_MAX_SOLID;
    props[0] = (uint8_t)((st->pb * 5U + st->lp) * 9U + st->lc);
    props[1] = (uint8_t)dict;
    props[2] = (uint8_t)(dict >> 8);
    props[3] = (uint8_t)(dict >> 16);
    props[4] = (uint8_t)(dict >> 24);
    buffer = fa_alloc(cap);
    if (!buffer) return false;
    if (in_size == 0U) {
        if (expected > 0) {
            xx_mem_free(buffer);
            return false;
        }
    } else if (!xx_lzma_decompress_memory(in, in_size, props, sizeof(props), expected, buffer, cap, &written) || written > cap) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = written;
    return true;
}

/* REP: u32 window, then frames {u32 length, u32 count, count x u32 match
 * length, count x u32 distance, (count+1) x u32 literal length, literals},
 * ended by a zero frame length. */
static bool fa_rep(const uint8_t *in, size_t in_size, size_t cap, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    uint8_t *buffer;
    size_t cursor = 4U, produced = 0U;
    uint32_t window;
    if (in_size < 8U) return false;
    window = xx_data_get_u32(in, 4, 0, false);
    if (!window || window > 0x7fffffffU) return false;
    buffer = fa_alloc(cap);
    if (!buffer) return false;
    while (in_size - cursor >= 4U) {
        uint32_t length = xx_data_get_u32(in + cursor, 4, 0, false), count, i;
        size_t end, lengths, distances, literal_lengths, literals;
        cursor += 4U;
        if (!length) {
            if (cursor != in_size) break;
            *out = buffer;
            *out_size = produced;
            return true;
        }
        if (fa_stopped(pd) || length < 8U || length > in_size - cursor) break;
        end = cursor + length;
        count = xx_data_get_u32(in + cursor, 4, 0, false);
        if (count > (length - 8U) / 12U) break;
        lengths = cursor + 4U;
        distances = lengths + (size_t)count * 4U;
        literal_lengths = distances + (size_t)count * 4U;
        literals = literal_lengths + ((size_t)count + 1U) * 4U;
        for (i = 0U; i <= count; ++i) {
            uint32_t literal_size = xx_data_get_u32(in + literal_lengths + (size_t)i * 4U, 4, 0, false);
            uint32_t match_size, distance, j;
            if (literal_size > end - literals || literal_size > cap - produced) goto fail;
            xx_rt_memcpy(buffer + produced, in + literals, literal_size);
            produced += literal_size;
            literals += literal_size;
            if (i == count) break;
            match_size = xx_data_get_u32(in + lengths + (size_t)i * 4U, 4, 0, false);
            distance = xx_data_get_u32(in + distances + (size_t)i * 4U, 4, 0, false);
            if (match_size > cap - produced) goto fail;
            if (match_size && (!distance || distance > window || distance > produced)) goto fail;
            for (j = 0U; j < match_size; ++j) buffer[produced + j] = buffer[produced + j - distance];
            produced += match_size;
        }
        if (literals != end) break;
        cursor = end;
    }
fail:
    xx_mem_free(buffer);
    return false;
}

/* Bounded Tornado Huffman profile. Independently written from the historical
 * stream description: coder 3, minimum match 2, no table transforms. Tree
 * ties and rescaling are wire semantics; logical bits never read padding. */
#define FA_TOR_CODES 842U
#define FA_TOR_EOB 832U
#define FA_TOR_REPCHAR 833U
#define FA_TOR_NODES (2U * FA_TOR_CODES + 4U)
#define FA_TOR_MAX_BYTES (8U * 1024U * 1024U)
#define FA_TOR_MAX_WORK (64U * 1024U * 1024U)

typedef struct fa_tor_node {
    uint64_t weight;
    uint16_t left, right, symbol;
    uint8_t depth;
} fa_tor_node;

typedef struct fa_tor_state {
    const uint8_t *input;
    size_t size, bit, work;
    xx_pd_struct *pd;
    uint32_t counts[FA_TOR_CODES];
    uint64_t keys[FA_TOR_CODES];
    fa_tor_node nodes[FA_TOR_NODES];
    uint16_t root;
} fa_tor_state;

static bool fa_tor_charge(fa_tor_state *s)
{
    if (s->work >= FA_TOR_MAX_WORK) return false;
    ++s->work;
    return (s->work & 1023U) != 0U || !fa_stopped(s->pd);
}

static bool fa_tor_bits(fa_tor_state *s, unsigned count, uint32_t *value)
{
    uint32_t result = 0U;
    unsigned i;
    if (count > 30U || s->bit > s->size * 8U || count > s->size * 8U - s->bit) return false;
    for (i = 0U; i < count; ++i) {
        if (!fa_tor_charge(s)) return false;
        result |= (uint32_t)((s->input[s->bit >> 3U] >> (s->bit & 7U)) & 1U) << i;
        ++s->bit;
    }
    *value = result;
    return true;
}

static bool fa_tor_sift(fa_tor_state *s, size_t count, size_t parent)
{
    while (parent < count / 2U) {
        size_t child = parent * 2U + 1U;
        uint64_t swap;
        if (!fa_tor_charge(s)) return false;
        if (child + 1U < count && s->keys[child] < s->keys[child + 1U]) ++child;
        if (s->keys[parent] >= s->keys[child]) break;
        swap = s->keys[parent]; s->keys[parent] = s->keys[child]; s->keys[child] = swap;
        parent = child;
    }
    return true;
}

static bool fa_tor_tree(fa_tor_state *s, unsigned mode)
{
    static const unsigned factors[8] = {2U, 3U, 4U, 6U, 8U, 10U, 12U, 16U};
    size_t i, a = 0U, b = FA_TOR_CODES + 2U, end = b;
    if (mode >= 8U) return false;
    for (i = 0U; i < FA_TOR_CODES; ++i) {
        if (!fa_tor_charge(s) || !s->counts[i]) return false;
        s->keys[i] = ((uint64_t)s->counts[i] << 10U) | i;
    }
    /* Heap-sort the fixed alphabet by frequency, then symbol number. */
    for (i = FA_TOR_CODES / 2U; i > 0U; --i) if (!fa_tor_sift(s, FA_TOR_CODES, i - 1U)) return false;
    for (i = FA_TOR_CODES; i > 1U; --i) {
        uint64_t swap = s->keys[0]; s->keys[0] = s->keys[i - 1U]; s->keys[i - 1U] = swap;
        if (!fa_tor_sift(s, i - 1U, 0U)) return false;
    }
    for (i = 0U; i < FA_TOR_NODES; ++i) {
        if (!fa_tor_charge(s)) return false;
        s->nodes[i].weight = UINT64_MAX;
        s->nodes[i].depth = 0U;
    }
    for (i = 0U; i < FA_TOR_CODES; ++i) {
        s->nodes[i].weight = s->keys[i] >> 10U;
        s->nodes[i].symbol = (uint16_t)(s->keys[i] & 1023U);
    }
    /* The two FIFO queues have a prescribed tie order. A mixed pair puts
     * the original leaf on its left, including equal-frequency ties. */
    while (!(a == FA_TOR_CODES && end - b == 1U)) {
        size_t left, right;
        if (!fa_tor_charge(s) || a > FA_TOR_CODES || b > end || end >= FA_TOR_NODES) return false;
        if (s->nodes[a + 1U].weight < s->nodes[b].weight) { left = a; right = a + 1U; a += 2U; }
        else if (s->nodes[a].weight > s->nodes[b + 1U].weight) { left = b; right = b + 1U; b += 2U; }
        else { left = a++; right = b++; }
        if (s->nodes[left].weight == UINT64_MAX || s->nodes[right].weight == UINT64_MAX ||
            s->nodes[left].weight > UINT64_MAX - s->nodes[right].weight) return false;
        s->nodes[end].weight = s->nodes[left].weight + s->nodes[right].weight;
        s->nodes[end].left = (uint16_t)left; s->nodes[end].right = (uint16_t)right;
        ++end;
    }
    s->root = (uint16_t)b;
    for (i = b + 1U; i-- > FA_TOR_CODES + 2U;) {
        unsigned depth = (unsigned)s->nodes[i].depth + 1U;
        if (!fa_tor_charge(s) || depth > 31U) return false;
        s->nodes[s->nodes[i].left].depth = (uint8_t)depth;
        s->nodes[s->nodes[i].right].depth = (uint8_t)depth;
    }
    for (i = 0U; i < FA_TOR_CODES; ++i) {
        uint32_t v = s->counts[i];
        if (!fa_tor_charge(s)) return false;
        s->counts[i] -= v > 1U && v < factors[mode] ? 1U : v / factors[mode];
    }
    return true;
}

static bool fa_tor_symbol(fa_tor_state *s, uint32_t *symbol)
{
    for (;;) {
        uint16_t node = s->root;
        uint32_t bit, mode;
        while (node >= FA_TOR_CODES + 2U) {
            if (!fa_tor_bits(s, 1U, &bit)) return false;
            node = bit ? s->nodes[node].right : s->nodes[node].left;
        }
        if (node >= FA_TOR_CODES) return false;
        *symbol = s->nodes[node].symbol;
        if (*symbol != FA_TOR_EOB) {
            if (!fa_tor_charge(s) || s->counts[*symbol] == UINT32_MAX) return false;
            ++s->counts[*symbol];
            return true;
        }
        if (!fa_tor_bits(s, 3U, &mode) || !fa_tor_tree(s, mode)) return false;
    }
}

static bool fa_tor(const uint8_t *input, size_t size, size_t limit, uint8_t **out, size_t *written, xx_pd_struct *pd)
{
    static const unsigned length_bits[16] = {0U,0U,0U,0U,0U,0U,0U,1U,1U,2U,2U,3U,3U,4U,8U,30U};
    static const unsigned distance_bits[32] = {4U,4U,5U,5U,5U,6U,6U,7U,7U,8U,8U,9U,9U,10U,10U,11U,
                                              11U,12U,12U,13U,13U,14U,14U,15U,15U,16U,17U,18U,19U,21U,23U,30U};
    const uint32_t impossible = INT32_MAX / 2U;
    fa_tor_state *s = NULL;
    uint8_t *plain = NULL;
    uint32_t lengths[16], distances[32], previous[4] = {0U,0U,0U,0U};
    uint32_t window, ring, base = 0U;
    size_t produced = 0U, i;
    bool ok = false;
    if (!input || size < 6U || size > FA_TOR_MAX_BYTES || input[0] != 3U || input[1] != 2U || fa_stopped(pd)) return false;
    window = xx_data_get_u32(input + 2U, 4, 0, false);
    if (!window || window > FA_TOR_MAX_BYTES) return false;
    ring = window < 262144U ? 262144U : window;
    if (limit > FA_TOR_MAX_BYTES) limit = FA_TOR_MAX_BYTES;
    s = (fa_tor_state *)fa_alloc(sizeof(*s));
    plain = (uint8_t *)fa_alloc(limit);
    if (!s || !plain) goto done;
    xx_mem_zero(s, sizeof(*s));
    s->input = input + 6U; s->size = size - 6U; s->pd = pd;
    for (i = 0U; i < FA_TOR_CODES; ++i) s->counts[i] = 1U;
    for (i = 0U; i < 16U; ++i) { lengths[i] = base; base += (uint32_t)1U << length_bits[i]; }
    base = 0U;
    for (i = 0U; i < 32U; ++i) { distances[i] = base; base += (uint32_t)1U << distance_bits[i]; }
    if (!fa_tor_tree(s, 0U)) goto done;
    for (;;) {
        uint32_t symbol, extra, length, distance, slot;
        if (!fa_tor_symbol(s, &symbol)) goto done;
        if (symbol < 256U) {
            if (produced >= limit || !fa_tor_charge(s)) goto done;
            plain[produced++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == FA_TOR_REPCHAR) { length = 1U; distance = previous[0]; }
        else {
            if (symbol >= FA_TOR_EOB) goto done; /* Reserved codes and unsupported future forms. */
            slot = symbol & 15U;
            if (!fa_tor_bits(s, length_bits[slot], &extra)) goto done;
            length = lengths[slot] + extra;
            /* Values 101..104 carry table-transform metadata. */
            if (length > 100U && length <= 104U) goto done;
            length = length > 100U ? length - 4U + 2U : length + 2U;
            slot = (symbol - 256U) / 16U;
            if (slot < 4U) {
                distance = previous[slot];
                while (slot > 0U) { previous[slot] = previous[slot - 1U]; --slot; }
                previous[0] = distance;
            } else {
                slot -= 4U;
                if (slot >= 32U || !fa_tor_bits(s, distance_bits[slot], &extra)) goto done;
                distance = distances[slot] + extra + 1U;
                previous[3] = previous[2]; previous[2] = previous[1]; previous[1] = previous[0]; previous[0] = distance;
            }
        }
        if (length == impossible && distance == impossible) {
            if ((s->bit + 7U) / 8U != s->size || fa_stopped(pd)) goto done;
            *out = plain; *written = produced; plain = NULL; ok = true;
            goto done;
        }
        if (!length || length >= impossible || !distance || distance > ring || distance > produced ||
            (uint64_t)length > 2U * (uint64_t)window || length > limit - produced) goto done;
        for (i = 0U; i < length; ++i) {
            if (!fa_tor_charge(s)) goto done;
            plain[produced] = plain[produced - distance]; ++produced;
        }
    }
done:
    xx_mem_free(plain);
    xx_mem_free(s);
    return ok;
}

/* FreeArc 4x4 version 0, restricted to its LZ4 DeCompressMem adapter.
 * Each record carries signed LE32 {decoded size, packed size}; decoded -1
 * denotes stored bytes. Compressed payloads begin with nested version 1,
 * followed by one raw LZ4 block, without standard LZ4 frame/length headers.
 * Framing policy: mirror/freearc 71f3ab36df26401fff4301b4c8600a31a90d8da9,
 * Compression/4x4/C_4x4.cpp and Compression/LZ4/C_LZ4.cpp. */
static bool fa_4x4_record(const uint8_t *in, size_t size, size_t *cursor, const uint8_t **payload,
                          size_t *packed, size_t *decoded, bool *stored)
{
    uint32_t raw, compressed;
    size_t at = *cursor;
    if (at > size || size - at < 8U) return false;
    raw = xx_data_get_u32(in + at, 4, 0, false);
    compressed = xx_data_get_u32(in + at + 4U, 4, 0, false);
    at += 8U;
    if ((raw != UINT32_MAX && raw > INT32_MAX) || compressed > INT32_MAX || (size_t)compressed > size - at) return false;
    *stored = raw == UINT32_MAX;
    if (!*stored && (compressed < 1U || in[at] != 1U)) return false;
    *packed = (size_t)compressed;
    *decoded = *stored ? (size_t)compressed : (size_t)raw;
    *payload = in + at;
    *cursor = at + (size_t)compressed;
    return true;
}

static bool fa_4x4_lz4(const uint8_t *in, size_t in_size, size_t cap, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    size_t cursor = 4U, total = 0U, ceiling = 0U, produced = 0U;
    uint8_t *buffer;
    bool first = true;
    if (in_size < 4U || in_size > (size_t)FA_MAX_STAGE || cap > (size_t)FA_MAX_STAGE ||
        xx_data_get_u32(in, 4, 0, false) != 0U || fa_stopped(pd)) return false;
    /* Check every record before allocation. Retain the historical first-buffer
     * ceiling as a compatibility policy, then allocate only the exact total. */
    while (cursor < in_size) {
        const uint8_t *payload;
        size_t packed, decoded;
        bool stored;
        if (fa_stopped(pd) || !fa_4x4_record(in, in_size, &cursor, &payload, &packed, &decoded, &stored) || decoded > cap - total) return false;
        if (first) {
            size_t seed = decoded ? decoded : packed;
            ceiling = seed + seed / 4U + 16384U; /* seed <= existing stage cap */
            first = false;
        }
        if (packed > ceiling || decoded > ceiling) return false;
        total += decoded;
    }
    buffer = fa_alloc(total);
    if (!buffer) return false;
    cursor = 4U;
    while (cursor < in_size) {
        const uint8_t *payload;
        size_t packed, decoded, written = 0U;
        bool stored;
        if (fa_stopped(pd) || !fa_4x4_record(in, in_size, &cursor, &payload, &packed, &decoded, &stored) || decoded > total - produced) goto fail;
        if (stored) {
            while (written < packed) {
                size_t part = packed - written;
                if (part > 65536U) part = 65536U;
                if (fa_stopped(pd)) goto fail;
                xx_rt_memcpy(buffer + produced + written, payload + written, part);
                written += part;
            }
        } else if (!xx_lz4_decompress_block(payload + 1U, packed - 1U, buffer + produced, decoded, &written) || written != decoded) goto fail;
        if (fa_stopped(pd) || written != decoded) goto fail;
        produced += written;
    }
    if (produced != total || fa_stopped(pd)) goto fail;
    *out = buffer;
    *out_size = total;
    return true;
fail:
    xx_mem_free(buffer);
    return false;
}

/* Original FreeArc LZP (not a generic LZP stream): frames carry signed LE32
 * lengths. Positive frames have a 12-byte raw prefix, a forward byte stream
 * and a reverse match-length/escape stream sharing one input range. Hash
 * table entries are output offsets, so no stream pointer can escape a block.
 * Derived independently from mirror/freearc Compression/LZP/C_LZP.cpp,
 * commit 71f3ab36df26401fff4301b4c8600a31a90d8da9. */
static uint32_t fa_lzp_hash(const uint8_t *out, size_t pos, uint32_t mask)
{
    uint32_t c = xx_data_get_u32(out + pos - 4U, 4, 0, false);
    uint32_t prior = xx_data_get_u32(out + pos - 5U, 4, 0, false);
    uint32_t rotate = (c >> 17U) | (c << 15U);
    return (c + 5U * rotate + 3U * prior) & mask;
}

static bool fa_lzp_block(const uint8_t *input, size_t size, uint8_t *output, size_t cap, const fa_stage *stage, uint32_t *table, xx_pd_struct *pd, size_t *written)
{
    size_t front = 12U, back = size, pos = 12U, hash_size, i;
    uint32_t mask, key, context, n = 1U, n1 = 1U;
    if (size < 13U || cap < 12U || stage->lzp_hash_bits > 20U) return false;
    hash_size = (size_t)1U << stage->lzp_hash_bits;
    mask = (uint32_t)(hash_size - 1U);
    for (i = 0U; i < hash_size; ++i) {
        if ((i & 4095U) == 0U && fa_stopped(pd)) return false;
        table[i] = 5U;
    }
    xx_rt_memcpy(output, input, 12U);
    context = xx_data_get_u32(output + pos - 4U, 4, 0, false);
    key = fa_lzp_hash(output, pos, mask);
    while (front < back) {
        uint8_t symbol = input[front++];
        uint32_t predictor = table[key];
        if (fa_stopped(pd)) return false;
        if (--n == 0U) {
            table[key] = (uint32_t)pos;
            n = n1;
        }
        if (symbol != 0xB5U || context != xx_data_get_u32(output + predictor - 4U, 4, 0, false)) {
            if (pos == cap) return false;
            output[pos++] = symbol;
        } else {
            uint8_t end_token;
            if (front >= back) return false;
            end_token = input[--back];
            if (end_token == 255U) {
                if (pos == cap) return false;
                output[pos++] = symbol;
            } else {
                size_t distance = pos - predictor, length, copied;
                uint64_t wide = (distance > stage->lzp_barrier ? stage->lzp_smallest : stage->lzp_min_match) - 1U;
                table[key] = (uint32_t)pos;
                if (distance > (size_t)(n1 + 1U) * hash_size && n1 < 7U) ++n1;
                while (end_token == 0U) {
                    wide += 254U;
                    if (wide > cap || front >= back) return false;
                    end_token = input[--back];
                }
                wide += end_token;
                if (!distance || predictor >= pos || wide > cap - pos || !wide) return false;
                length = (size_t)wide;
                copied = 0U;
                {
                    unsigned update = 2U * n1 + 2U;
                    while (copied < length) {
                        if ((copied & 1023U) == 0U && fa_stopped(pd)) return false;
                        if (--update == 0U) {
                            update = 2U * n1 + 1U;
                            table[fa_lzp_hash(output, pos, mask)] = (uint32_t)pos;
                        }
                        output[pos++] = output[predictor++];
                        ++copied;
                    }
                }
            }
        }
        if (pos < 12U || pos > cap) return false;
        context = xx_data_get_u32(output + pos - 4U, 4, 0, false);
        key = fa_lzp_hash(output, pos, mask);
    }
    if (front != back || fa_stopped(pd)) return false;
    *written = pos;
    return true;
}

static bool fa_lzp(const fa_stage *stage, const uint8_t *in, size_t in_size, size_t cap, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    uint8_t *buffer = NULL;
    uint32_t *table = NULL;
    size_t cursor = 0U, produced = 0U, hash_size;
    uint64_t table_init = 0U;
    if (in_size > (size_t)FA_MAX_STAGE || stage->dictionary > FA_MAX_SOLID || stage->lzp_hash_bits > 20U) return false;
    buffer = fa_alloc(cap);
    if (!buffer) return false;
    hash_size = (size_t)1U << stage->lzp_hash_bits;
    while (cursor < in_size) {
        int32_t signed_length;
        size_t frame_length;
        if (fa_stopped(pd) || in_size - cursor < 4U) goto fail;
        signed_length = (int32_t)xx_data_get_u32(in + cursor, 4, 0, false);
        cursor += 4U;
        if (!signed_length || signed_length == INT32_MIN) goto fail;
        frame_length = signed_length < 0 ? (size_t)(-(int64_t)signed_length) : (size_t)signed_length;
        if (frame_length > in_size - cursor || frame_length > stage->dictionary || frame_length > cap - produced) goto fail;
        if (signed_length < 0) {
            size_t copied = 0U;
            while (copied < frame_length) {
                size_t take = frame_length - copied;
                if (fa_stopped(pd)) goto fail;
                if (take > 65536U) take = 65536U;
                xx_rt_memcpy(buffer + produced + copied, in + cursor + copied, take);
                copied += take;
            }
            produced += frame_length;
        } else {
            size_t decoded = 0U, available = cap - produced;
            if (hash_size > FA_LZP_MAX_TABLE_INIT - table_init) goto fail;
            table_init += hash_size;
            if (!table) {
                table = (uint32_t *)xx_mem_alloc(hash_size * sizeof(*table));
                if (!table) goto fail;
            }
            if (available > stage->dictionary) available = (size_t)stage->dictionary;
            if (!fa_lzp_block(in + cursor, frame_length, buffer + produced, available, stage, table, pd, &decoded)) goto fail;
            produced += decoded;
        }
        cursor += frame_length;
    }
    if (fa_stopped(pd)) goto fail;
    if (table) xx_mem_free(table);
    *out = buffer;
    *out_size = produced;
    return true;
fail:
    if (table) xx_mem_free(table);
    xx_mem_free(buffer);
    return false;
}

/* DELTA frames: u32 size, u32 table bytes (4 x tables), then skip[],
 * type[], rows[] (u32 each), then the frame's size data bytes. A table
 * type's low bits, below its leading one, flag each column immutable. */
static bool fa_delta(const uint8_t *in, size_t in_size, size_t cap, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    uint8_t *buffer, *shuffled = NULL;
    size_t cursor = 0U, produced = 0U;
    /* Every frame costs at least its own data bytes of input, so the output
     * never outgrows the input; the cap is enforced per frame. */
    buffer = fa_alloc(in_size);
    if (!buffer) return false;
    while (cursor < in_size) {
        uint32_t size, table_bytes, t;
        size_t skips, types, rows, position = 0U;
        uint8_t *block;
        if (fa_stopped(pd) || in_size - cursor < 8U) goto fail;
        size = xx_data_get_u32(in + cursor, 4, 0, false);
        table_bytes = xx_data_get_u32(in + cursor + 4U, 4, 0, false);
        cursor += 8U;
        if ((table_bytes & 3U) || table_bytes > 0x7fffffffU || (uint64_t)table_bytes * 3U + size > (uint64_t)(in_size - cursor) || size > cap - produced) goto fail;
        skips = cursor;
        types = skips + table_bytes;
        rows = types + table_bytes;
        cursor += (size_t)table_bytes * 3U;
        block = buffer + produced;
        xx_rt_memcpy(block, in + cursor, size);
        cursor += size;
        for (t = 0U; t < table_bytes / 4U; ++t) {
            uint32_t type = xx_data_get_u32(in + types + (size_t)t * 4U, 4, 0, false);
            uint32_t skip = xx_data_get_u32(in + skips + (size_t)t * 4U, 4, 0, false);
            uint32_t row_count = xx_data_get_u32(in + rows + (size_t)t * 4U, 4, 0, false);
            bool immutable[31];
            unsigned width = 0U, immutable_count = 0U, column;
            uint64_t bytes;
            uint32_t row;
            if (fa_stopped(pd)) goto fail;
            while (type > 1U && width < 31U) {
                immutable[width] = (type & 1U) != 0U;
                immutable_count += type & 1U;
                ++width;
                type >>= 1;
            }
            bytes = (uint64_t)width * row_count;
            if (!width || type != 1U || !row_count || skip > size - position || bytes > size - position - skip) goto fail;
            position += skip;
            if (immutable_count && immutable_count != width) {
                size_t a = 0U, b = (size_t)immutable_count * row_count;
                shuffled = fa_alloc((size_t)bytes);
                if (!shuffled) goto fail;
                xx_rt_memcpy(shuffled, block + position, (size_t)bytes);
                for (row = 0U; row < row_count; ++row)
                    for (column = 0U; column < width; ++column) block[position + (size_t)row * width + column] = shuffled[immutable[column] ? a++ : b++];
                xx_mem_free(shuffled);
                shuffled = NULL;
            }
            for (row = 1U; row < row_count; ++row) {
                uint32_t carry = 0U;
                for (column = 0U; column < width; ++column) {
                    size_t at = position + (size_t)row * width + column;
                    uint32_t sum;
                    if (immutable[column]) {
                        carry = 0U;
                        continue;
                    }
                    sum = (uint32_t)block[at] + block[at - width] + carry;
                    block[at] = (uint8_t)sum;
                    carry = sum >> 8;
                }
            }
            position += (size_t)bytes;
        }
        produced += size;
    }
    *out = buffer;
    *out_size = produced;
    return true;
fail:
    if (shuffled) xx_mem_free(shuffled);
    xx_mem_free(buffer);
    return false;
}

/* x86 CALL/JMP converter, decoding direction, after Igor Pavlov's
 * public-domain Bra86.c. */
static bool fa_ms_byte(uint8_t b)
{
    return b == 0U || b == 0xFFU;
}

static void fa_exe(uint8_t *data, size_t size)
{
    static const uint8_t allowed[8] = {1, 1, 1, 0, 1, 0, 0, 0};
    static const uint8_t bit_number[8] = {0, 1, 2, 2, 3, 3, 3, 3};
    size_t position = 0U, previous = (size_t)0 - 1U;
    uint32_t mask = 0U, ip = 5U;
    if (size < 5U) return;
    for (;;) {
        size_t distance;
        while (position < size - 4U && (data[position] & 0xFEU) != 0xE8U) ++position;
        if (position >= size - 4U) break;
        distance = position - previous;
        if (distance > 3U) {
            mask = 0U;
        } else {
            mask = (mask << (distance - 1U)) & 7U;
            if (mask && (!allowed[mask] || fa_ms_byte(data[position + 4U - bit_number[mask]]))) {
                previous = position;
                mask = ((mask << 1) & 7U) | 1U;
                ++position;
                continue;
            }
        }
        previous = position;
        if (fa_ms_byte(data[position + 4U])) {
            uint32_t source = xx_data_get_u32(data + position + 1U, 4, 0, false), target;
            for (;;) {
                unsigned shift;
                target = source - (ip + (uint32_t)position);
                if (!mask) break;
                shift = bit_number[mask] * 8U;
                if (!fa_ms_byte((uint8_t)(target >> (24U - shift)))) break;
                source = target ^ ((1U << (32U - shift)) - 1U);
            }
            data[position + 4U] = (uint8_t)(~(((target >> 24) & 1U) - 1U));
            data[position + 3U] = (uint8_t)(target >> 16);
            data[position + 2U] = (uint8_t)(target >> 8);
            data[position + 1U] = (uint8_t)target;
            position += 5U;
        } else {
            mask = ((mask << 1) & 7U) | 1U;
            ++position;
        }
    }
}

/* FreeArc protocol reference, including its historical hex-decoding bug:
 * https://github.com/mirror/freearc/tree/71f3ab36df26401fff4301b4c8600a31a90d8da9
 * Encryption.hs (65..85,114..117), C_Encryption.cpp (125..160,221..227,
 * 300..305), Common.h (787..790). PBKDF2 uses HMAC-SHA512; c contains the
 * derived bytes immediately after the AES key, not a ciphertext MAC. */
typedef struct fa_hmac512 {
    uint8_t inner[128], outer[128];
} fa_hmac512;

static void fa_hmac512_init(fa_hmac512 *h, const uint8_t *password, size_t size)
{
    uint8_t key[128] = {0};
    size_t i;
    if (size > sizeof(key)) dg_sha512_digest(password, size, key);
    else if (size) xx_rt_memcpy(key, password, size);
    for (i = 0U; i < sizeof(key); ++i) {
        h->inner[i] = (uint8_t)(key[i] ^ 0x36U);
        h->outer[i] = (uint8_t)(key[i] ^ 0x5cU);
    }
    fa_secure_clear(key, sizeof(key));
}

static void fa_hmac512_digest(const fa_hmac512 *h, const uint8_t *data, size_t size, uint8_t out[64])
{
    uint8_t inner[196], outer[192];
    /* Only salt || BE32(1) (at most 68 bytes), or a 64-byte digest. */
    xx_rt_memcpy(inner, h->inner, 128U);
    xx_rt_memcpy(inner + 128U, data, size);
    xx_rt_memcpy(outer, h->outer, 128U);
    dg_sha512_digest(inner, 128U + size, outer + 128U);
    dg_sha512_digest(outer, sizeof(outer), out);
    fa_secure_clear(inner, sizeof(inner));
    fa_secure_clear(outer, sizeof(outer));
}

/* Foreign.C.String.withCStringLen in legacy FreeArc truncates each Haskell
 * character to its low byte. :f first encodes UTF-8, and also fixes hex. */
static bool fa_legacy_password(const char *utf8, uint8_t *out, size_t *size)
{
    const uint8_t *p = (const uint8_t *)utf8;
    size_t i = 0U, n = 0U, length = *size;
    while (i < length) {
        uint32_t cp = p[i++];
        unsigned more = 0U, j;
        if (cp >= 0xc2U && cp <= 0xdfU) { cp &= 31U; more = 1U; }
        else if (cp >= 0xe0U && cp <= 0xefU) { cp &= 15U; more = 2U; }
        else if (cp >= 0xf0U && cp <= 0xf4U) { cp &= 7U; more = 3U; }
        else if (cp >= 0x80U) return false;
        if (more > length - i) return false;
        for (j = 0U; j < more; ++j) {
            if ((p[i] & 0xc0U) != 0x80U) return false;
            cp = (cp << 6U) | (p[i++] & 63U);
        }
        if ((more == 1U && cp < 0x80U) || (more == 2U && cp < 0x800U) || (more == 3U && cp < 0x10000U) ||
            cp > 0x10ffffU || (cp >= 0xd800U && cp <= 0xdfffU)) return false;
        out[n++] = (uint8_t)cp;
    }
    *size = n;
    return true;
}

static uint8_t fa_legacy_hex_byte(uint8_t b)
{
    unsigned hi = b >> 4U, lo = b & 15U;
    return (uint8_t)(((hi < 10U ? hi : hi - 10U) << 4U) | (lo < 10U ? lo : lo - 10U));
}

static bool fa_aes_decrypt(const fa_stage *stage, uint8_t *data, size_t size, const char *password, xx_pd_struct *pd)
{
    uint8_t derived[64] = {0}, link[64], seed[68], counter[16], mask[16];
    uint8_t *legacy = NULL;
    const uint8_t *password_bytes = (const uint8_t *)password;
    fa_hmac512 h;
    xx_aes_context aes;
    size_t password_size, i, at;
    uint32_t round;
    unsigned mismatch = 0U;
    bool result = false;
    if (!password) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "FreeArc archive requires a password");
        return false;
    }
    password_size = xx_str_len(password);
    if (password_size > FA_MAX_PASSWORD || size > FA_MAX_STAGE || fa_stopped(pd)) return false;
    if (!stage->fixed_hex) {
        legacy = (uint8_t *)fa_alloc(password_size);
        if (!legacy || !fa_legacy_password(password, legacy, &password_size)) goto done;
        password_bytes = legacy;
    }
    fa_hmac512_init(&h, password_bytes, password_size);
    xx_rt_memcpy(seed, stage->salt, stage->salt_size);
    seed[stage->salt_size] = seed[stage->salt_size + 1U] = seed[stage->salt_size + 2U] = 0U;
    seed[stage->salt_size + 3U] = 1U;
    fa_hmac512_digest(&h, seed, stage->salt_size + 4U, link);
    xx_rt_memcpy(derived, link, sizeof(derived));
    for (round = 1U; round < stage->iterations; ++round) {
        if ((round & 63U) == 0U && fa_stopped(pd)) goto done;
        fa_hmac512_digest(&h, link, sizeof(link), link);
        for (i = 0U; i < sizeof(derived); ++i) derived[i] ^= link[i];
    }
    for (i = 0U; i < stage->check_size; ++i) mismatch |= (unsigned)(derived[32U + i] ^ stage->check[i]);
    if (mismatch) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "FreeArc password is incorrect");
        goto done;
    }
    xx_rt_memcpy(counter, stage->iv, sizeof(counter));
    if (!stage->fixed_hex) {
        for (i = 0U; i < 32U; ++i) derived[i] = fa_legacy_hex_byte(derived[i]);
        for (i = 0U; i < sizeof(counter); ++i) counter[i] = fa_legacy_hex_byte(counter[i]);
    }
    if (!xx_aes_internal_set_key(&aes, derived, 32U)) goto done;
    for (at = 0U; at < size;) {
        size_t amount = size - at > sizeof(mask) ? sizeof(mask) : size - at;
        if ((at & 0xffffU) == 0U && fa_stopped(pd)) goto done;
        xx_aes_internal_encrypt_block(&aes, counter, mask);
        for (i = 0U; i < amount; ++i) data[at + i] ^= mask[i];
        at += amount;
        for (i = 0U; i < sizeof(counter); ++i) if (++counter[i]) break;
    }
    result = !fa_stopped(pd);
done:
    if (legacy) { fa_secure_clear(legacy, xx_str_len(password)); xx_mem_free(legacy); }
    fa_secure_clear(&h, sizeof(h));
    fa_secure_clear(&aes, sizeof(aes));
    fa_secure_clear(derived, sizeof(derived));
    fa_secure_clear(link, sizeof(link));
    fa_secure_clear(seed, sizeof(seed));
    fa_secure_clear(counter, sizeof(counter));
    fa_secure_clear(mask, sizeof(mask));
    return result;
}

bool xx_freearc_decode_chain_password(const char *method, uint8_t *in, size_t in_size, int64_t expected, const char *password,
                                      uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    fa_stage stages[FA_MAX_STAGES];
    size_t count = 0U, i, cur_size = in_size;
    uint8_t *cur = in;
    size_t bound;

    if (out) *out = NULL;
    if (out_size) *out_size = 0U;
    if (!in || !out || !out_size || expected < -1 || expected > FA_MAX_SOLID || !fa_parse_chain(method, stages, &count)) {
        if (in) xx_mem_free(in);
        return false;
    }
    /* Intermediate streams carry framing on top of the final bytes; allow
     * a quarter plus 1 MiB of it. With no final size, use the solid cap. */
    bound = expected >= 0 ? (size_t)(expected + expected / 4 + 0x100000) : (size_t)FA_MAX_SOLID;
    for (i = count; i-- > 0U;) {
        uint8_t *next = NULL;
        size_t next_size = 0U;
        size_t cap = (i == 0U && expected >= 0) ? (size_t)expected : bound;
        bool ok;
        if (fa_stopped(pd)) {
            xx_mem_free(cur);
            return false;
        }
        switch (stages[i].kind) {
            case FA_AES256_CTR:
                next = cur;
                next_size = cur_size;
                ok = cur_size <= cap && fa_aes_decrypt(&stages[i], cur, cur_size, password, pd);
                break;
            case FA_STORE:
                next = cur;
                next_size = cur_size;
                ok = cur_size <= cap;
                break;
            case FA_EXE:
                fa_exe(cur, cur_size);
                next = cur;
                next_size = cur_size;
                ok = cur_size <= cap;
                break;
            case FA_LZMA: ok = fa_lzma(&stages[i], cur, cur_size, (i == 0U) ? expected : -1, cap, &next, &next_size); break;
            case FA_REP: ok = fa_rep(cur, cur_size, cap, &next, &next_size, pd); break;
            case FA_4X4_LZ4: ok = fa_4x4_lz4(cur, cur_size, cap, &next, &next_size, pd); break;
            case FA_TOR_HUF3: ok = fa_tor(cur, cur_size, cap, &next, &next_size, pd); break;
            case FA_LZP: ok = fa_lzp(&stages[i], cur, cur_size, cap, &next, &next_size, pd); break;
            default: ok = fa_delta(cur, cur_size, cap, &next, &next_size, pd); break;
        }
        if (!ok) {
            if (next && next != cur) xx_mem_free(next);
            xx_mem_free(cur);
            return false;
        }
        if (next != cur) xx_mem_free(cur);
        cur = next;
        cur_size = next_size;
    }
    if (expected >= 0 && cur_size != (size_t)expected) {
        xx_mem_free(cur);
        return false;
    }
    *out = cur;
    *out_size = cur_size;
    return true;
}

bool xx_freearc_decode_chain(const char *method, uint8_t *in, size_t in_size, int64_t expected, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    return xx_freearc_decode_chain_password(method, in, in_size, expected, NULL, out, out_size, pd);
}

/* ------------------------------------------------------ probe (header) */

static bool xx_freearc_probe(Abstractformat *self, xx_freearc *out)
{
    uint8_t header[XX_FREEARC_HEADER_SIZE];
    uint8_t block_magic[XX_FREEARC_SIGNATURE_SIZE];
    int64_t total;
    int64_t span;

    if (!self || !self->device || self->base_address < 0) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;
    if (span < XX_FREEARC_MIN_SIZE) {
        return false;
    }
    if (!xx_freearc_read_at(self, self->base_address, header, sizeof(header)) || xx_rt_memcmp(header, XX_FREEARC_MAGIC, XX_FREEARC_SIGNATURE_SIZE) != 0) {
        return false;
    }
    /* The first block repeats the magic at offset 8. Without this second
     * check a four-byte prefix would be the entire evidence. */
    if (!xx_freearc_read_at(self, self->base_address + XX_FREEARC_HEADER_SIZE, block_magic, sizeof(block_magic)) ||
        xx_rt_memcmp(block_magic, XX_FREEARC_MAGIC, XX_FREEARC_SIGNATURE_SIZE) != 0) {
        return false;
    }

    if (out) {
        out->flags = (uint16_t)((uint16_t)header[4] | ((uint16_t)header[5] << 8));
        out->version = (uint16_t)((uint16_t)header[6] | ((uint16_t)header[7] << 8));
    }
    return true;
}

/* ------------------------------------------------------- control blocks */

typedef struct fa_desc {
    uint64_t type;
    char method[FA_MAX_METHOD + 1U];
    int64_t offset; /* relative to the archive start */
    int64_t unpacked;
    int64_t packed;
    uint32_t crc;
} fa_desc;

/* Parse a local descriptor starting at tail[start]; @p physical is the
 * archive-relative offset of tail[start]. */
static bool fa_local_descriptor(const uint8_t *tail, size_t tail_size, size_t start, int64_t physical, fa_desc *d, size_t *end)
{
    fa_cursor c;
    const char *method;
    size_t method_len, checked;
    uint64_t unpacked, packed;
    uint32_t own;
    c.p = tail;
    c.size = tail_size;
    c.pos = start + 4U;
    if (!fa_num(&c, &d->type) || !fa_str(&c, &method, &method_len, FA_MAX_METHOD) || !fa_num(&c, &unpacked) || !fa_num(&c, &packed) || !fa_u32(&c, &d->crc)) return false;
    checked = c.pos;
    if (!fa_u32(&c, &own) || own != fa_crc32(tail + start, checked - start)) return false;
    if ((int64_t)packed > physical) return false;
    xx_rt_memcpy(d->method, method, method_len);
    d->method[method_len] = 0;
    d->unpacked = (int64_t)unpacked;
    d->packed = (int64_t)packed;
    d->offset = physical - (int64_t)packed;
    *end = c.pos;
    return true;
}

/* Find the footer descriptor in the last 4 KiB of the device. */
static bool fa_find_footer(Abstractformat *self, fa_desc *footer, int64_t *archive_end)
{
    uint8_t tail[FA_TAIL_SIZE];
    int64_t total, span, tail_rel;
    size_t tail_size, s;
    if (!self || !self->device || self->base_address < 0) return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;
    if (span < XX_FREEARC_MIN_SIZE) return false;
    tail_size = span < FA_TAIL_SIZE ? (size_t)span : FA_TAIL_SIZE;
    tail_rel = span - (int64_t)tail_size;
    if (!xx_freearc_read_at(self, self->base_address + tail_rel, tail, tail_size)) return false;
    for (s = tail_size - 4U + 1U; s-- > 0U;) {
        size_t end;
        if (xx_rt_memcmp(tail + s, XX_FREEARC_MAGIC, 4U) != 0) continue;
        if (fa_local_descriptor(tail, tail_size, s, tail_rel + (int64_t)s, footer, &end) && footer->type == FA_BLOCK_FOOTER) {
            if (archive_end) *archive_end = tail_rel + (int64_t)end;
            return true;
        }
    }
    return false;
}

/* Read and decode a control block, checking its CRC. */
static bool fa_read_control(Abstractformat *self, const fa_desc *d, uint8_t **data, size_t *size, xx_pd_struct *pd)
{
    uint8_t *packed;
    if (d->packed > FA_MAX_CONTROL || d->unpacked > FA_MAX_CONTROL || !xx_freearc_method_supported(d->method)) return false;
    packed = fa_alloc((size_t)d->packed);
    if (!packed) return false;
    if (!xx_freearc_read_at(self, self->base_address + d->offset, packed, (size_t)d->packed)) {
        xx_mem_free(packed);
        return false;
    }
    if (fa_method_encrypted(d->method)) self->is_crypted = true;
    if (!xx_freearc_decode_chain_password(d->method, packed, (size_t)d->packed, d->unpacked, xx_format_get_password(self), data, size, pd)) return false;
    if (fa_crc32(*data, *size) != d->crc) {
        xx_mem_free(*data);
        *data = NULL;
        return false;
    }
    return true;
}

/* --------------------------------------------------------------- index */

typedef struct fa_block {
    char *method;
    int64_t offset; /* relative to the archive start */
    int64_t packed;
    int64_t unpacked;
    bool supported;
} fa_block;

typedef struct fa_entry {
    char *name;
    int64_t size;
    int64_t offset; /* within the block's unpacked bytes */
    size_t block;
    uint32_t crc;
    uint32_t time;
    bool folder;
} fa_entry;

typedef struct fa_index {
    size_t references;
    fa_block *blocks;
    size_t block_count, block_cap;
    fa_entry *entries;
    size_t entry_count, entry_cap;
    uint64_t name_bytes;
} fa_index;

static void fa_index_free(fa_index *ix)
{
    size_t i;
    if (!ix) return;
    if (--ix->references) return;
    for (i = 0U; i < ix->block_count; ++i) xx_mem_free(ix->blocks[i].method);
    for (i = 0U; i < ix->entry_count; ++i) xx_mem_free(ix->entries[i].name);
    xx_mem_free(ix->blocks);
    xx_mem_free(ix->entries);
    xx_mem_free(ix);
}

static bool fa_grow(void **array, size_t *cap, size_t need, size_t elem)
{
    size_t n;
    void *grown;
    if (need <= *cap) return true;
    n = *cap ? *cap : 16U;
    while (n < need) n *= 2U;
    if (n > (size_t)-1 / elem) return false;
    grown = xx_mem_realloc(*array, n * elem);
    if (!grown) return false;
    *array = grown;
    *cap = n;
    return true;
}

/* Parse one directory block's data (dir_offset archive-relative). */
static bool fa_parse_dir(fa_index *ix, const uint8_t *data, size_t size, int64_t dir_offset, xx_pd_struct *pd)
{
    fa_cursor c;
    uint64_t nblocks, ndirs, nfiles = 0U, v, i;
    size_t first_block = ix->block_count, first_entry = ix->entry_count;
    uint64_t *block_files = NULL;
    const char **dirs = NULL;
    size_t *dir_lens = NULL;
    bool ok = false;

    c.p = data;
    c.size = size;
    c.pos = 0U;
    if (!fa_num(&c, &nblocks) || nblocks > FA_MAX_ENTRIES || nblocks > size) return false;
    block_files = (uint64_t *)xx_mem_alloc((size_t)(nblocks ? nblocks : 1U) * sizeof(uint64_t));
    if (!block_files) return false;
    for (i = 0U; i < nblocks; ++i) {
        if (!fa_num(&c, &block_files[i]) || block_files[i] > FA_MAX_ENTRIES - nfiles) goto done;
        nfiles += block_files[i];
    }
    if (nfiles > FA_MAX_ENTRIES - ix->entry_count || nfiles > size) goto done;
    if (!fa_grow((void **)&ix->blocks, &ix->block_cap, ix->block_count + (size_t)nblocks, sizeof(fa_block))) goto done;
    for (i = 0U; i < nblocks; ++i) {
        const char *m;
        size_t ml;
        fa_block *b = &ix->blocks[ix->block_count];
        if (!fa_str(&c, &m, &ml, FA_MAX_METHOD)) goto done;
        xx_mem_zero(b, sizeof(*b));
        b->method = fa_strndup(m, ml);
        if (!b->method) goto done;
        b->supported = xx_freearc_method_supported(b->method);
        ++ix->block_count;
    }
    for (i = 0U; i < nblocks; ++i) {
        fa_block *b = &ix->blocks[first_block + (size_t)i];
        if (!fa_num(&c, &v) || (int64_t)v > dir_offset) goto done;
        b->offset = dir_offset - (int64_t)v;
    }
    for (i = 0U; i < nblocks; ++i) {
        fa_block *b = &ix->blocks[first_block + (size_t)i];
        if (!fa_num(&c, &v) || (int64_t)v > dir_offset - b->offset) goto done;
        b->packed = (int64_t)v;
    }
    if (!fa_num(&c, &ndirs) || ndirs > FA_MAX_ENTRIES || ndirs > size) goto done;
    dirs = (const char **)xx_mem_alloc((size_t)(ndirs ? ndirs : 1U) * sizeof(char *));
    dir_lens = (size_t *)xx_mem_alloc((size_t)(ndirs ? ndirs : 1U) * sizeof(size_t));
    if (!dirs || !dir_lens) goto done;
    for (i = 0U; i < ndirs; ++i)
        if (!fa_str(&c, &dirs[i], &dir_lens[i], FA_MAX_NAME)) goto done;
    if (!fa_grow((void **)&ix->entries, &ix->entry_cap, ix->entry_count + (size_t)nfiles, sizeof(fa_entry))) goto done;
    for (i = 0U; i < nfiles; ++i) {
        fa_entry *e = &ix->entries[ix->entry_count];
        const char *n;
        size_t nl;
        if (fa_stopped(pd) || !fa_str(&c, &n, &nl, FA_MAX_NAME) || !nl) goto done;
        xx_mem_zero(e, sizeof(*e));
        e->name = fa_strndup(n, nl);
        if (!e->name) goto done;
        ++ix->entry_count;
    }
    for (i = 0U; i < nfiles; ++i) {
        fa_entry *e = &ix->entries[first_entry + (size_t)i];
        size_t nl, dl, k;
        char *joined;
        if (!fa_num(&c, &v) || (ndirs ? v >= ndirs : v != 0U)) goto done;
        nl = xx_str_len(e->name);
        dl = ndirs ? dir_lens[v] : 0U;
        if (dl + 1U + nl > FA_MAX_NAME || ix->name_bytes + dl + nl + 2U > FA_MAX_NAME_BYTES) goto done;
        ix->name_bytes += dl + nl + 2U;
        if (dl) {
            joined = (char *)xx_mem_alloc(dl + 1U + nl + 1U);
            if (!joined) goto done;
            xx_rt_memcpy(joined, dirs[v], dl);
            joined[dl] = '/';
            xx_rt_memcpy(joined + dl + 1U, e->name, nl + 1U);
            xx_mem_free(e->name);
            e->name = joined;
        }
        for (k = 0U; e->name[k]; ++k)
            if (e->name[k] == '\\') e->name[k] = '/';
    }
    for (i = 0U; i < nfiles; ++i) {
        fa_entry *e = &ix->entries[first_entry + (size_t)i];
        if (!fa_num(&c, &v) || v > (uint64_t)FA_MAX_SOLID) goto done;
        e->size = (int64_t)v;
    }
    for (i = 0U; i < nfiles; ++i)
        if (!fa_u32(&c, &ix->entries[first_entry + (size_t)i].time)) goto done;
    for (i = 0U; i < nfiles; ++i) {
        uint8_t flag;
        fa_entry *e = &ix->entries[first_entry + (size_t)i];
        if (!fa_byte(&c, &flag) || flag > 1U || (flag && e->size)) goto done;
        e->folder = flag != 0U;
    }
    for (i = 0U; i < nfiles; ++i)
        if (!fa_u32(&c, &ix->entries[first_entry + (size_t)i].crc)) goto done;
    /* Members fill their block in order. */
    {
        size_t index = first_entry, b;
        for (b = 0U; b < (size_t)nblocks; ++b) {
            fa_block *blk = &ix->blocks[first_block + b];
            for (v = 0U; v < block_files[b]; ++v) {
                fa_entry *e = &ix->entries[index++];
                e->block = first_block + b;
                e->offset = blk->unpacked;
                if (e->size > FA_MAX_SOLID - blk->unpacked) {
                    /* Listed, but the block is too large to decode. */
                    blk->supported = false;
                    blk->unpacked = FA_MAX_SOLID + 1;
                } else {
                    blk->unpacked += e->size;
                }
            }
        }
    }
    ok = true;
done:
    xx_mem_free(block_files);
    if (dirs) xx_mem_free((void *)dirs);
    if (dir_lens) xx_mem_free(dir_lens);
    return ok;
}

static fa_index *fa_build_index(Abstractformat *self, xx_pd_struct *pd)
{
    fa_desc footer;
    uint8_t *data = NULL;
    size_t size = 0U;
    fa_cursor c;
    uint64_t count, i;
    fa_index *ix;
    if (!fa_find_footer(self, &footer, NULL) || !fa_read_control(self, &footer, &data, &size, pd)) return NULL;
    ix = (fa_index *)xx_mem_calloc(1U, sizeof(*ix));
    if (!ix) {
        xx_mem_free(data);
        return NULL;
    }
    ix->references = 1U;
    c.p = data;
    c.size = size;
    c.pos = 0U;
    if (!fa_num(&c, &count) || count < 1U || count > FA_MAX_CONTROL_BLOCKS) goto fail;
    for (i = 0U; i < count; ++i) {
        fa_desc d;
        const char *m;
        size_t ml;
        uint64_t rel, unpacked, packed;
        if (fa_stopped(pd) || !fa_num(&c, &d.type) || !fa_str(&c, &m, &ml, FA_MAX_METHOD) || !fa_num(&c, &rel) || !fa_num(&c, &unpacked) || !fa_num(&c, &packed) ||
            !fa_u32(&c, &d.crc) || (int64_t)rel > footer.offset)
            goto fail;
        xx_rt_memcpy(d.method, m, ml);
        d.method[ml] = 0;
        d.offset = footer.offset - (int64_t)rel;
        d.unpacked = (int64_t)unpacked;
        d.packed = (int64_t)packed;
        if (d.packed > footer.offset - d.offset) goto fail;
        if (d.type == FA_BLOCK_DIR) {
            uint8_t *dir = NULL;
            size_t dir_size = 0U;
            bool ok;
            if (!fa_read_control(self, &d, &dir, &dir_size, pd)) goto fail;
            ok = fa_parse_dir(ix, dir, dir_size, d.offset, pd);
            xx_mem_free(dir);
            if (!ok) goto fail;
        }
    }
    xx_mem_free(data);
    return ix;
fail:
    xx_mem_free(data);
    fa_index_free(ix);
    return NULL;
}

static fa_index *fa_index_get(Abstractformat *self, xx_pd_struct *pd)
{
    xx_freearc *archive = (xx_freearc *)self;
    const char *password;
    uint8_t fingerprint[32];
    if (!self) return NULL;
    password = xx_format_get_password(self);
    fa_password_identity(password, fingerprint);
    if (!archive->password_fingerprint_set || archive->password_present != (password != NULL) ||
        xx_rt_memcmp(fingerprint, archive->password_fingerprint, sizeof(fingerprint)) != 0) {
        fa_index_free((fa_index *)archive->index);
        archive->index = NULL;
        archive->index_tried = false;
        xx_rt_memcpy(archive->password_fingerprint, fingerprint, sizeof(fingerprint));
        archive->password_fingerprint_set = true;
        archive->password_present = password != NULL;
    }
    fa_secure_clear(fingerprint, sizeof(fingerprint));
    if (!archive->index_tried) {
        if (!xx_freearc_probe(self, NULL)) return NULL;
        archive->index = fa_build_index(self, pd);
        /* A stopped parse is not a verdict on the archive. */
        if (archive->index) archive->index_tried = true;
    }
    if (archive->index) {
        const fa_index *ix = (const fa_index *)archive->index;
        size_t i;
        for (i = 0U; i < ix->block_count; ++i) {
            if (fa_method_encrypted(ix->blocks[i].method)) { self->is_crypted = true; break; }
        }
    }
    return (fa_index *)archive->index;
}

/* --------------------------------------------------------- reader api */

void xx_freearc_init(xx_freearc *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FREEARC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-freearc");
    xx_format_set_extension(&archive->format, "arc");
    archive->format.check_is_valid = xx_freearc_check_is_valid;
    archive->format.handle_base_info = xx_freearc_handle_base_info;
    archive->format.get_format_size = xx_freearc_get_format_size;
    archive->format.get_number_of_archive_records = xx_freearc_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_freearc_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_freearc_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_freearc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_freearc_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_freearc_free_archive_records_reading;
    archive->format.destroy = xx_freearc_vtable_destroy;
    archive->archive_size = -1;
}

xx_freearc *xx_freearc_create(xx_io_device *device, int64_t base_address)
{
    xx_freearc *archive = (xx_freearc *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_freearc_init(archive, device, base_address);
    return archive;
}

void xx_freearc_destroy(xx_freearc *archive)
{
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches back through format.destroy. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    fa_index_free((fa_index *)archive->index);
    archive->index = NULL;
    archive->index_tried = false;
    archive->flags = 0U;
    archive->version = 0U;
    fa_secure_clear(archive->password_fingerprint, sizeof(archive->password_fingerprint));
    archive->password_fingerprint_set = false;
    archive->password_present = false;
}

void xx_freearc_free(xx_freearc *archive)
{
    if (!archive) return;
    xx_freearc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_freearc_vtable_destroy(Abstractformat *self)
{
    xx_freearc_destroy((xx_freearc *)self);
}

bool xx_freearc_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    return xx_freearc_probe(self, NULL);
}

bool xx_freearc_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_freearc *archive = (xx_freearc *)self;
    fa_desc footer;
    int64_t end = -1;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    self->is_valid = xx_freearc_probe(self, archive);
    if (!self->is_valid) {
        self->format_size = 0;
        return false;
    }
    /* The footer descriptor closes the archive. Without one (a damaged or
     * truncated archive) it is taken to run to the end of the device. */
    if (fa_find_footer(self, &footer, &end) && end > XX_FREEARC_HEADER_SIZE) {
        if (fa_method_encrypted(footer.method)) self->is_crypted = true;
        archive->archive_size = end;
        self->format_size = end;
    } else {
        archive->archive_size = -1;
        self->format_size = xx_io_total_size(self->device) - self->base_address;
    }
    return true;
}

int64_t xx_freearc_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_freearc_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    fa_index *ix = fa_index_get(self, pd);
    return ix ? (uint64_t)ix->entry_count : 0U;
}

/* ------------------------------------------------------ record reading */

typedef struct fa_stream {
    fa_index *ix; /* borrowed from the archive */
    size_t index;
    size_t cached_block; /* (size_t)-1 when nothing is cached */
    bool cached_ok;
    uint8_t *cache;
    size_t cache_size;
    uint64_t *seen; /* hashes of output names already used */
    size_t seen_cap;
    uint8_t password_fingerprint[32];
    bool password_fingerprint_set;
    bool password_present;
} fa_stream;

static void fa_stream_free(void *opaque)
{
    fa_stream *s = (fa_stream *)opaque;
    if (!s) return;
    if (s->cache) xx_mem_free(s->cache);
    if (s->seen) xx_mem_free(s->seen);
    fa_index_free(s->ix);
    fa_secure_clear(s->password_fingerprint, sizeof(s->password_fingerprint));
    xx_mem_free(s);
}

static bool fa_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *fa_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool fa_set_record(xx_archive_record *record, const fa_index *ix, size_t i)
{
    const fa_entry *e = &ix->entries[i];
    const fa_block *b = &ix->blocks[e->block];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = -1;
    record->header_size = 0;
    record->data_offset = b->offset;
    record->compressed_size = b->packed;
    return xx_archive_record_set_original_name(record, e->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)e->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32, e->crc) && xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, e->time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, fa_method_encrypted(b->method)) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, e->folder);
}

xx_archive_record_state *xx_freearc_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    fa_index *ix = fa_index_get(self, pd);
    fa_stream *stream;
    xx_archive_record_state *state;
    if (!ix) return NULL;
    stream = (fa_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->ix = ix;
    ++ix->references;
    stream->cached_block = (size_t)-1;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        fa_index_free(ix);
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = fa_stream_free;
    state->total_records = (int64_t)ix->entry_count;
    if (!fa_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (ix->entry_count) {
        if (!fa_set_record(&state->current_record, ix, 0U)) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_freearc_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_freearc_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    fa_stream *stream;
    (void)pd;
    if (!self || !state || state->format != self || !(stream = (fa_stream *)state->internal_state) || stream->index + 1U >= stream->ix->entry_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    state->current_index = (int64_t)stream->index;
    if (!fa_set_record(&state->current_record, stream->ix, stream->index)) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

/* Decode (or reuse) the solid block holding entry @p e. */
static bool fa_load_block(Abstractformat *self, fa_stream *s, size_t block, xx_pd_struct *pd)
{
    const fa_block *b = &s->ix->blocks[block];
    uint8_t *packed;
    int64_t total;
    const char *password = xx_format_get_password(self);
    uint8_t fingerprint[32];
    fa_password_identity(password, fingerprint);
    if (!s->password_fingerprint_set || s->password_present != (password != NULL) ||
        xx_rt_memcmp(fingerprint, s->password_fingerprint, sizeof(fingerprint)) != 0) {
        s->cached_block = (size_t)-1;
        xx_rt_memcpy(s->password_fingerprint, fingerprint, sizeof(fingerprint));
        s->password_fingerprint_set = true;
        s->password_present = password != NULL;
    }
    fa_secure_clear(fingerprint, sizeof(fingerprint));
    if (s->cached_block == block) return s->cached_ok;
    if (s->cache) xx_mem_free(s->cache);
    s->cache = NULL;
    s->cache_size = 0U;
    s->cached_block = block;
    s->cached_ok = false;
    total = xx_io_total_size(self->device) - self->base_address;
    if (!b->supported || b->unpacked > FA_MAX_SOLID || b->packed > FA_MAX_STAGE || b->offset < 0 || b->packed < 0 || b->offset > total || b->packed > total - b->offset)
        return false;
    packed = fa_alloc((size_t)b->packed);
    if (!packed) return false;
    if (!xx_freearc_read_at(self, self->base_address + b->offset, packed, (size_t)b->packed)) {
        xx_mem_free(packed);
        return false;
    }
    if (fa_method_encrypted(b->method)) self->is_crypted = true;
    if (!xx_freearc_decode_chain_password(b->method, packed, (size_t)b->packed, b->unpacked, password, &s->cache, &s->cache_size, pd)) {
        /* A stopped decode may be retried later. */
        if (fa_stopped(pd)) s->cached_block = (size_t)-1;
        return false;
    }
    s->cached_ok = true;
    return true;
}

static char fa_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool fa_stem_is(const char *name, size_t stem, const char *word)
{
    size_t i;
    for (i = 0U; i < stem; ++i)
        if (!word[i] || fa_upper(name[i]) != word[i]) return false;
    return word[stem] == 0;
}

/* One path component [s, s+n): no control or reserved characters, not only
 * dots and spaces, not a Windows device name. */
static bool fa_safe_component(const char *s, size_t n)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t i, stem = 0U;
    bool meaningful = false;
    if (!n) return false;
    for (i = 0U; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20U || c == 0x7FU || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' || c == '\\') return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < n && s[stem] != '.') ++stem;
    while (stem > 0U && s[stem - 1U] == ' ') --stem;
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i)
        if (fa_stem_is(s, stem, devices[i])) return false;
    if (stem == 4U && s[3] >= '0' && s[3] <= '9' &&
        ((fa_upper(s[0]) == 'C' && fa_upper(s[1]) == 'O' && fa_upper(s[2]) == 'M') || (fa_upper(s[0]) == 'L' && fa_upper(s[1]) == 'P' && fa_upper(s[2]) == 'T')))
        return false;
    return true;
}

static bool fa_safe_path(const char *path)
{
    size_t start = 0U, i;
    if (!path || !path[0] || path[0] == '/') return false;
    for (i = 0U;; ++i) {
        if (path[i] == '/' || path[i] == 0) {
            if (!fa_safe_component(path + start, i - start)) return false;
            if (!path[i]) break;
            start = i + 1U;
        }
    }
    return true;
}

static uint64_t fa_name_hash(const char *s)
{
    uint64_t h = 1469598103934665603ULL;
    for (; *s; ++s) {
        h ^= (uint8_t)fa_upper(*s);
        h *= 1099511628211ULL;
    }
    return h ? h : 1U;
}

/* Returns true if @p name was new (and records it). */
static bool fa_seen_insert(fa_stream *s, const char *name)
{
    uint64_t h = fa_name_hash(name);
    size_t i;
    if (!s->seen) {
        size_t cap = 64U;
        while (cap < s->ix->entry_count * 2U + 2U) cap *= 2U;
        s->seen = (uint64_t *)xx_mem_calloc(cap, sizeof(uint64_t));
        if (!s->seen) return true;
        s->seen_cap = cap;
    }
    i = (size_t)h & (s->seen_cap - 1U);
    while (s->seen[i]) {
        if (s->seen[i] == h) return false;
        i = (i + 1U) & (s->seen_cap - 1U);
    }
    s->seen[i] = h;
    return true;
}

static bool fa_write_file(const char *path, const uint8_t *data, size_t size)
{
    xx_io_device *destination;
    bool created, result = true;
    size_t done = 0U;
    if (!xx_store_create_dirs_a(path, false)) return false;
    destination = xx_io_file_open(path, "wb");
    if (!destination) return false;
    created = true;
    while (done < size) {
        size_t part = size - done > 0x100000U ? 0x100000U : size - done;
        ssize_t w = xx_io_write(destination, data + done, part);
        if (w <= 0 || (size_t)w > part) {
            result = false;
            break;
        }
        done += (size_t)w;
    }
    if (xx_io_close(destination) != 0) result = false;
    if (!result && created) xx_rt_remove(path);
    return result;
}

bool xx_freearc_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    fa_stream *stream;
    const fa_entry *e;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL, *path = NULL, *name = NULL;
    const uint8_t *slice = NULL;
    bool result = false;
    if (!self || !state || state->format != self || !state->has_record || !(stream = (fa_stream *)state->internal_state) || stream->index >= stream->ix->entry_count ||
        fa_stopped(pd))
        return false;
    e = &stream->ix->entries[stream->index];
    if (!e->folder) {
        if (!fa_load_block(self, stream, e->block, pd) || e->offset > (int64_t)stream->cache_size || e->size > (int64_t)stream->cache_size - e->offset) return false;
        slice = stream->cache + e->offset;
        if (fa_crc32(slice, (size_t)e->size) != e->crc) return false;
    }
    path_option = fa_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
    if (!fa_safe_path(e->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* A second member with the same name (case-insensitively) is written
     * as "<name>~<index>" instead of overwriting the first. */
    if (fa_seen_insert(stream, e->name)) {
        name = xx_str_concat(e->name, "");
    } else {
        char suffix[32];
        (void)xx_rt_snprintf(suffix, sizeof(suffix), "~%llu", (unsigned long long)stream->index);
        name = xx_str_concat(e->name, suffix);
        if (name) (void)fa_seen_insert(stream, name);
    }
    if (!name) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", name) : xx_str_concat(base, name);
    if (!path) goto done;
    if (e->folder) result = xx_store_create_dirs_a(path, true);
    else result = fa_write_file(path, slice, (size_t)e->size);
done:
    if (path) xx_str_free(path);
    if (name) xx_str_free(name);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_freearc_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}

uint16_t xx_freearc_get_flags(const xx_freearc *archive)
{
    return archive ? archive->flags : 0U;
}

uint16_t xx_freearc_get_version(const xx_freearc *archive)
{
    return archive ? archive->version : 0U;
}
