/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DAEMON Tools MDX / MDS v2 disc images.  xx_daemon_tools_mdx.h carries the
 * file header table.  Written from the structure of the format as it is
 * documented by the libMirage MDX parser (GPL, read for understanding only;
 * no code taken) and the public reverse-engineering notes it cites
 * (github.com/Marisa-Chan/mdsx).  The primitives - RIPEMD-160 (Dobbertin,
 * Bosselaers, Preneel), HMAC, PBKDF2 (RFC 8018), LRW tweaks in GF(2^128) and
 * the CD-ROM EDC CRC - are implemented here from their published
 * definitions; AES-256 and Deflate come from the library.
 *
 * Layout of the parsed descriptor (offsets are into the plain descriptor,
 * whose first 18 bytes are the file's signature and version):
 *
 *   header   96 bytes: +0x12 u16 medium type, +0x14 u16 sessions,
 *            +0x50 u32 session blocks, +0x58 u32 data key header
 *   session  32 bytes: +0x08 u16 number, +0x0A u8 blocks, +0x14 u32 track
 *            blocks
 *   track    80 bytes: +0 u8 mode/flags, +1 u8 subchannel<<3, +4 u8 point,
 *            +0x0C u32 extra block, +0x10 u16 sector size, +0x28 u64 data
 *            offset, +0x30 u32 footer count, +0x34 u32 footers
 *   footer   32 bytes: +0 u32 file name (UTF-16LE), +4 u8 flags (bit 0:
 *            grouped compression), +0x0C u32 sectors per group, +0x10 u64
 *            sectors, +0x18 u64 compression table (relative to the data)
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/daemon_tools_mdx/xx_daemon_tools_mdx.h"

#include "xxfclib/algo/aes/xx_aes.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef DAEMON_TOOLS_MDX
#define XX_DAEMON_TOOLS_MDX_FILE_TYPE XX_FILE_TYPE_DAEMON_TOOLS_MDX
#else
#define XX_DAEMON_TOOLS_MDX_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define MDX_FILE_HEADER 48U
#define MDX_KEY_HEADER 512U
#define MDX_SALT 64U
#define MDX_KEY_DATA 256U
#define MDX_PREFIX 18U
#define MDX_DESC_HEADER 96U
#define MDX_SESSION_SIZE 32U
#define MDX_TRACK_SIZE 80U
#define MDX_FOOTER_SIZE 32U
#define MDX_PBKDF2_ROUNDS 2000U
#define MDX_DERIVED 152U /* 120-byte largest key + 32-byte legacy IV area */

/* Caps on untrusted fields.  A double-layer DVD holds about 4.2 million
 * sectors, so eight million per footer leaves room for anything real. */
#define MDX_MAX_DESCRIPTOR (16U * 1024U * 1024U)
#define MDX_MAX_SESSIONS 99U
#define MDX_MAX_TRACKS 256U
#define MDX_MAX_FOOTERS 1024U
#define MDX_MAX_FOOTERS_PER_TRACK 64U
#define MDX_MAX_SECTORS UINT64_C(8388608)
#define MDX_MAX_GROUP 1024U
#define MDX_MAX_SECTOR_SIZE 4096U
#define MDX_NAME_SIZE 32U
#define MDX_FILE_NAME_MAX 260U
#define MDX_COPY_CHUNK 65536U

/* ---------------------------------------------------------------------- */
/* Little-endian helpers                                                   */

static bool mdx_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || offset < 0 || (!buffer && size) ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        size_t request = size - done;
        ssize_t amount;
        if (request > MDX_COPY_CHUNK) request = MDX_COPY_CHUNK;
        amount = xx_io_read(device, (uint8_t *)buffer + done, request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool mdx_write_all(xx_io_device *device, const uint8_t *data,
                          size_t size) {
    size_t done = 0U;
    if (!device) return true;
    while (done < size) {
        ssize_t n = xx_io_write(device, data + done, size - done);
        if (n <= 0 || (size_t)n > size - done) return false;
        done += (size_t)n;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* RIPEMD-160                                                              */

typedef struct mdx_rmd_s {
    uint32_t h[5];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} mdx_rmd;

static const uint8_t mdx_rmd_rl[80] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    7, 4, 13, 1, 10, 6, 15, 3, 12, 0, 9, 5, 2, 14, 11, 8,
    3, 10, 14, 4, 9, 15, 8, 1, 2, 7, 0, 6, 13, 11, 5, 12,
    1, 9, 11, 10, 0, 8, 12, 4, 13, 3, 7, 15, 14, 5, 6, 2,
    4, 0, 5, 9, 7, 12, 2, 10, 14, 1, 3, 8, 11, 6, 15, 13};
static const uint8_t mdx_rmd_rr[80] = {
    5, 14, 7, 0, 9, 2, 11, 4, 13, 6, 15, 8, 1, 10, 3, 12,
    6, 11, 3, 7, 0, 13, 5, 10, 14, 15, 8, 12, 4, 9, 1, 2,
    15, 5, 1, 3, 7, 14, 6, 9, 11, 8, 12, 2, 10, 0, 4, 13,
    8, 6, 4, 1, 3, 11, 15, 0, 5, 12, 2, 13, 9, 7, 10, 14,
    12, 15, 10, 4, 1, 5, 8, 7, 6, 2, 13, 14, 0, 3, 9, 11};
static const uint8_t mdx_rmd_sl[80] = {
    11, 14, 15, 12, 5, 8, 7, 9, 11, 13, 14, 15, 6, 7, 9, 8,
    7, 6, 8, 13, 11, 9, 7, 15, 7, 12, 15, 9, 11, 7, 13, 12,
    11, 13, 6, 7, 14, 9, 13, 15, 14, 8, 13, 6, 5, 12, 7, 5,
    11, 12, 14, 15, 14, 15, 9, 8, 9, 14, 5, 6, 8, 6, 5, 12,
    9, 15, 5, 11, 6, 8, 13, 12, 5, 12, 13, 14, 11, 8, 5, 6};
static const uint8_t mdx_rmd_sr[80] = {
    8, 9, 9, 11, 13, 15, 15, 5, 7, 7, 8, 11, 14, 14, 12, 6,
    9, 13, 15, 7, 12, 8, 9, 11, 7, 7, 12, 7, 6, 15, 13, 11,
    9, 7, 15, 11, 8, 6, 6, 14, 12, 13, 5, 14, 13, 13, 7, 5,
    15, 5, 8, 11, 14, 14, 6, 14, 6, 9, 12, 9, 12, 5, 15, 8,
    8, 5, 12, 9, 12, 5, 14, 6, 8, 13, 6, 5, 15, 13, 11, 11};
static const uint32_t mdx_rmd_kl[5] = {0x00000000U, 0x5A827999U, 0x6ED9EBA1U,
                                       0x8F1BBCDCU, 0xA953FD4EU};
static const uint32_t mdx_rmd_kr[5] = {0x50A28BE6U, 0x5C4DD124U, 0x6D703EF3U,
                                       0x7A6D76E9U, 0x00000000U};

static uint32_t mdx_rol(uint32_t x, unsigned n) {
    return (x << n) | (x >> (32U - n));
}

static uint32_t mdx_rmd_f(unsigned round, uint32_t x, uint32_t y, uint32_t z) {
    switch (round) {
    case 0: return x ^ y ^ z;
    case 1: return (x & y) | (~x & z);
    case 2: return (x | ~y) ^ z;
    case 3: return (x & z) | (y & ~z);
    default: return x ^ (y | ~z);
    }
}

static void mdx_rmd_compress(uint32_t h[5], const uint8_t block[64]) {
    uint32_t x[16], al, bl, cl, dl, el, ar, br, cr, dr, er, t;
    unsigned j;
    for (j = 0; j < 16U; ++j) x[j] = xx_data_get_u32(block + 4U * j, 4, 0, false);
    al = ar = h[0];
    bl = br = h[1];
    cl = cr = h[2];
    dl = dr = h[3];
    el = er = h[4];
    for (j = 0; j < 80U; ++j) {
        unsigned round = j / 16U;
        t = mdx_rol(al + mdx_rmd_f(round, bl, cl, dl) + x[mdx_rmd_rl[j]] +
                        mdx_rmd_kl[round],
                    mdx_rmd_sl[j]) + el;
        al = el;
        el = dl;
        dl = mdx_rol(cl, 10U);
        cl = bl;
        bl = t;
        t = mdx_rol(ar + mdx_rmd_f(4U - round, br, cr, dr) + x[mdx_rmd_rr[j]] +
                        mdx_rmd_kr[round],
                    mdx_rmd_sr[j]) + er;
        ar = er;
        er = dr;
        dr = mdx_rol(cr, 10U);
        cr = br;
        br = t;
    }
    t = h[1] + cl + dr;
    h[1] = h[2] + dl + er;
    h[2] = h[3] + el + ar;
    h[3] = h[4] + al + br;
    h[4] = h[0] + bl + cr;
    h[0] = t;
}

static void mdx_rmd_init(mdx_rmd *c) {
    c->h[0] = 0x67452301U;
    c->h[1] = 0xEFCDAB89U;
    c->h[2] = 0x98BADCFEU;
    c->h[3] = 0x10325476U;
    c->h[4] = 0xC3D2E1F0U;
    c->length = 0U;
    c->used = 0U;
}

static void mdx_rmd_update(mdx_rmd *c, const uint8_t *data, size_t size) {
    c->length += size;
    while (size) {
        size_t take = 64U - c->used;
        if (take > size) take = size;
        xx_rt_memcpy(c->block + c->used, data, take);
        c->used += take;
        data += take;
        size -= take;
        if (c->used == 64U) {
            mdx_rmd_compress(c->h, c->block);
            c->used = 0U;
        }
    }
}

static void mdx_rmd_final(mdx_rmd *c, uint8_t out[20]) {
    uint64_t bits = c->length * 8U;
    uint8_t pad = 0x80U, zero = 0U, len[8];
    unsigned i;
    mdx_rmd_update(c, &pad, 1U);
    while (c->used != 56U) mdx_rmd_update(c, &zero, 1U);
    for (i = 0; i < 8U; ++i) len[i] = (uint8_t)(bits >> (8U * i));
    mdx_rmd_update(c, len, 8U);
    for (i = 0; i < 5U; ++i) xx_data_set_u32(out + 4U * i, 4, 0, c->h[i], false);
}

void xx_daemon_tools_mdx_rmd160(const void *data, size_t size,
                                uint8_t digest[20]) {
    mdx_rmd c;
    if (!digest) return;
    mdx_rmd_init(&c);
    if (data && size) mdx_rmd_update(&c, (const uint8_t *)data, size);
    mdx_rmd_final(&c, digest);
}

/* PBKDF2 with HMAC-RIPEMD-160.  The inner and outer pad states are hashed
 * once and copied for every round. */
static void mdx_pbkdf2_rmd160(const uint8_t *password, size_t password_size,
                              const uint8_t *salt, size_t salt_size,
                              uint32_t rounds, uint8_t *out, size_t out_size) {
    uint8_t key[64], pad[64], u[20], t[20], counter[4];
    mdx_rmd inner, outer, c;
    uint32_t block = 1U, r;
    unsigned i;
    xx_rt_memset(key, 0, sizeof(key));
    if (password_size > 64U) {
        xx_daemon_tools_mdx_rmd160(password, password_size, key);
    } else if (password_size) {
        xx_rt_memcpy(key, password, password_size);
    }
    for (i = 0; i < 64U; ++i) pad[i] = (uint8_t)(key[i] ^ 0x36U);
    mdx_rmd_init(&inner);
    mdx_rmd_update(&inner, pad, 64U);
    for (i = 0; i < 64U; ++i) pad[i] = (uint8_t)(key[i] ^ 0x5CU);
    mdx_rmd_init(&outer);
    mdx_rmd_update(&outer, pad, 64U);
    while (out_size) {
        size_t take = out_size < 20U ? out_size : 20U;
        counter[0] = (uint8_t)(block >> 24);
        counter[1] = (uint8_t)(block >> 16);
        counter[2] = (uint8_t)(block >> 8);
        counter[3] = (uint8_t)block;
        c = inner;
        mdx_rmd_update(&c, salt, salt_size);
        mdx_rmd_update(&c, counter, 4U);
        mdx_rmd_final(&c, u);
        c = outer;
        mdx_rmd_update(&c, u, 20U);
        mdx_rmd_final(&c, u);
        xx_rt_memcpy(t, u, 20U);
        for (r = 1U; r < rounds; ++r) {
            c = inner;
            mdx_rmd_update(&c, u, 20U);
            mdx_rmd_final(&c, u);
            c = outer;
            mdx_rmd_update(&c, u, 20U);
            mdx_rmd_final(&c, u);
            for (i = 0; i < 20U; ++i) t[i] ^= u[i];
        }
        xx_rt_memcpy(out, t, take);
        out += take;
        out_size -= take;
        ++block;
    }
    xx_rt_memset(key, 0, sizeof(key));
    xx_rt_memset(pad, 0, sizeof(pad));
}

/* ---------------------------------------------------------------------- */
/* Checksums                                                               */

/* CRC-32 as zlib computes it (reflected 0xEDB88320, inverted). */
static uint32_t mdx_crc32(const uint8_t *data, size_t size) {
    return xx_crc32_calc(0U, data, size);
}

/* The CD-ROM EDC: reflected polynomial 0xD8018001, zero start, no final
 * inversion. */
static uint32_t mdx_edc(const uint8_t *data, size_t size) {
    static const xx_crc_model model = {32U, 0x8001801BU, 0U, true, true, 0U, "CD-ROM EDC"};
    return (uint32_t)xx_crc_calculate(&model, data, size);
}

/* The password that protects the descriptor (and password-less track data)
 * is computed from the key header's salt: a linear congruential stream,
 * seeded from the salt's EDC, is XORed into each little-endian word of the
 * salt and zero bytes are replaced by 0x5F. */
static void mdx_salt_password(const uint8_t salt[MDX_SALT],
                              uint8_t password[MDX_SALT]) {
    uint32_t modifier = mdx_edc(salt, MDX_SALT) ^ 0x567372FFU;
    unsigned i, b;
    for (i = 0; i < MDX_SALT / 4U; ++i) {
        uint32_t v = xx_data_get_u32(salt + 4U * i, 4, 0, false);
        modifier = modifier * 0x35E85A6DU + 0x1548DCE9U;
        v ^= modifier ^ 0xEC564717U;
        for (b = 0; b < 4U; ++b)
            if (((v >> (8U * b)) & 0xFFU) == 0U) v |= 0x5FU << (8U * b);
        xx_data_set_u32(password + 4U * i, 4, 0, v, false);
    }
}

/* ---------------------------------------------------------------------- */
/* AES modes                                                               */

/* Whitened CBC: every 16-byte ciphertext block has the second IV half
 * XORed into both of its 8-byte halves; after removing that it is plain
 * CBC with the full IV.  @p size must be a multiple of 16. */
static bool mdx_cbc_whitened(uint8_t *data, size_t size, const uint8_t key[32],
                             const uint8_t iv[16]) {
    size_t at;
    unsigned i;
    if (size & 15U) return false;
    for (at = 0; at < size; at += 16U)
        for (i = 0; i < 16U; ++i) data[at + i] ^= iv[8U + (i & 7U)];
    return xx_aes_cbc_decrypt(data, size, key, 32U, iv, data);
}

typedef struct mdx_lrw_s {
    uint8_t aes_key[32];
    uint8_t times_power[64][16];  /**< K * x^j */
    uint8_t times_ones[64][16];   /**< K * (x^(j+1) - 1): 2^(j+1)-1 */
} mdx_lrw;

static void mdx_gf_double(uint8_t v[16]) {
    uint8_t carry = (uint8_t)(v[0] >> 7);
    int i;
    for (i = 0; i < 15; ++i) v[i] = (uint8_t)((v[i] << 1) | (v[i + 1] >> 7));
    v[15] = (uint8_t)(v[15] << 1);
    if (carry) v[15] ^= 0x87U;
}

/* LRW (IEEE P1619 draft) with the tweak key in the big-endian bit-string
 * convention: byte 15 bit 0 is x^0, reduction by x^128+x^7+x^2+x+1. */
static void mdx_lrw_init(mdx_lrw *l, const uint8_t tweak_key[16],
                         const uint8_t aes_key[32]) {
    uint8_t v[16];
    unsigned j, i;
    xx_rt_memcpy(l->aes_key, aes_key, 32U);
    xx_rt_memcpy(v, tweak_key, 16U);
    for (j = 0; j < 64U; ++j) {
        xx_rt_memcpy(l->times_power[j], v, 16U);
        mdx_gf_double(v);
    }
    xx_rt_memcpy(l->times_ones[0], l->times_power[0], 16U);
    for (j = 1; j < 64U; ++j)
        for (i = 0; i < 16U; ++i)
            l->times_ones[j][i] =
                (uint8_t)(l->times_ones[j - 1][i] ^ l->times_power[j][i]);
}

static void mdx_lrw_tweak(const mdx_lrw *l, uint64_t index, uint8_t t[16]) {
    unsigned j, i;
    xx_rt_memset(t, 0, 16U);
    for (j = 0; j < 64U; ++j)
        if ((index >> j) & 1U)
            for (i = 0; i < 16U; ++i) t[i] ^= l->times_power[j][i];
}

/* Decipher whole 16-byte blocks of @p data; block k uses tweak index
 * @p first + k.  ECB is obtained from the library's CBC by undoing the
 * chaining XOR with the previous ciphertext block. */
static bool mdx_lrw_decrypt(const mdx_lrw *l, uint8_t *data, size_t size,
                            uint64_t first) {
    uint8_t cipher[1024], plain[1024], tweak[16], zero[16];
    uint64_t index = first;
    size_t at = 0U;
    unsigned i;
    size &= ~(size_t)15U;
    xx_rt_memset(zero, 0, sizeof(zero));
    mdx_lrw_tweak(l, index, tweak);
    while (at < size) {
        size_t chunk = size - at, k;
        uint8_t tw[64][16];
        if (chunk > sizeof(cipher)) chunk = sizeof(cipher);
        for (k = 0; k < chunk; k += 16U) {
            xx_rt_memcpy(tw[k / 16U], tweak, 16U);
            for (i = 0; i < 16U; ++i)
                cipher[k + i] = (uint8_t)(data[at + k + i] ^ tweak[i]);
            {
                /* T(n+1) = T(n) ^ K*((n+1) ^ n); (n+1)^n = 2^(t+1)-1 with t
                 * the number of trailing one bits of n. */
                unsigned ones = 0U;
                uint64_t n = index;
                while ((n & 1U) && ones < 63U) {
                    n >>= 1;
                    ++ones;
                }
                for (i = 0; i < 16U; ++i) tweak[i] ^= l->times_ones[ones][i];
                ++index;
            }
        }
        if (!xx_aes_cbc_decrypt(cipher, chunk, l->aes_key, 32U, zero, plain))
            return false;
        for (k = 0; k < chunk; k += 16U)
            for (i = 0; i < 16U; ++i) {
                uint8_t v = plain[k + i];
                if (k) v ^= cipher[k - 16U + i];
                data[at + k + i] = (uint8_t)(v ^ tw[k / 16U][i]);
            }
        at += chunk;
    }
    xx_rt_memset(cipher, 0, sizeof(cipher));
    xx_rt_memset(plain, 0, sizeof(plain));
    return true;
}

/* Decipher a 512-byte key header in place.  @p cbc selects the file's
 * primary header mode (whitened CBC); otherwise LRW with index 1.  Success
 * requires "TRUE", a 256-byte key and the key's CRC-32. */
static bool mdx_open_key_header(uint8_t header[MDX_KEY_HEADER],
                                const uint8_t *password, size_t password_size,
                                bool cbc) {
    uint8_t master[MDX_DERIVED];
    uint8_t work[MDX_KEY_HEADER];
    bool ok;
    xx_rt_memcpy(work, header, sizeof(work));
    mdx_pbkdf2_rmd160(password, password_size, work, MDX_SALT,
                      MDX_PBKDF2_ROUNDS, master, sizeof(master));
    if (cbc) {
        ok = mdx_cbc_whitened(work + MDX_SALT, MDX_KEY_HEADER - MDX_SALT,
                              master + 32U, master);
    } else {
        mdx_lrw *l = (mdx_lrw *)xx_mem_alloc(sizeof(*l));
        ok = l != NULL;
        if (ok) {
            mdx_lrw_init(l, master, master + 32U);
            ok = mdx_lrw_decrypt(l, work + MDX_SALT, MDX_KEY_HEADER - MDX_SALT,
                                 1U);
            xx_rt_memset(l, 0, sizeof(*l));
            xx_mem_free(l);
        }
    }
    xx_rt_memset(master, 0, sizeof(master));
    if (!ok || xx_rt_memcmp(work + 68, "EURT", 4U) != 0 ||
        xx_data_get_u16(work + 74, 2, 0, false) != MDX_KEY_DATA ||
        xx_data_get_u32(work + 64, 4, 0, false) != mdx_crc32(work + 80, MDX_KEY_DATA)) {
        xx_rt_memset(work, 0, sizeof(work));
        return false;
    }
    xx_rt_memcpy(header, work, sizeof(work));
    xx_rt_memset(work, 0, sizeof(work));
    return true;
}

/* ---------------------------------------------------------------------- */
/* Parsed image                                                            */

typedef struct mdx_footer_s {
    uint64_t sectors;
    uint64_t data_offset;   /**< Absolute offset in the data file. */
    uint64_t table_offset;  /**< Relative to data_offset. */
    uint32_t group;
    uint32_t file;          /**< Data file index (0 for MDX). */
    bool compressed;
} mdx_footer;

typedef struct mdx_track_s {
    char name[MDX_NAME_SIZE];
    uint32_t point;
    uint32_t session;
    uint32_t stored_size;   /**< Bytes per stored sector. */
    uint32_t footer_first;
    uint32_t footer_count;
    uint64_t sectors;
    uint8_t mode;
    bool compressed;
} mdx_track;

typedef struct mdx_image_s {
    mdx_track tracks[MDX_MAX_TRACKS];
    uint32_t track_count;
    mdx_footer *footers;
    uint32_t footer_count;
    char *files[XX_DAEMON_TOOLS_MDX_MAX_FILES];
    uint32_t file_count;
    bool is_mdx;
    bool data_encrypted;
    bool data_key;
    mdx_lrw *lrw;
    int64_t format_size;
    uint8_t version_minor;
    uint16_t medium_type;
    uint16_t sessions;
} mdx_image;

static void mdx_image_free(mdx_image *image) {
    uint32_t i;
    if (!image) return;
    if (image->footers) xx_mem_free(image->footers);
    for (i = 0; i < image->file_count; ++i)
        if (image->files[i]) xx_str_free(image->files[i]);
    if (image->lrw) {
        xx_rt_memset(image->lrw, 0, sizeof(*image->lrw));
        xx_mem_free(image->lrw);
    }
    xx_mem_free(image);
}

/* Header checks that need no deciphering; fills the key header location and
 * the descriptor's position and stored size. */
typedef struct mdx_layout_s {
    uint8_t header[MDX_FILE_HEADER];
    int64_t key_offset;
    int64_t descriptor_offset;
    uint64_t descriptor_size;
    int64_t format_size;
    bool is_mdx;
} mdx_layout;

static bool mdx_layout_read(Abstractformat *format, mdx_layout *out) {
    int64_t total, size, base;
    uint32_t key_field;
    if (!format || !format->device || format->base_address < 0) return false;
    base = format->base_address;
    total = xx_io_total_size(format->device);
    if (total < base) return false;
    size = total - base;
    if (size < (int64_t)(MDX_FILE_HEADER + MDX_KEY_HEADER + 16U) ||
        !mdx_read_at(format->device, base, out->header, MDX_FILE_HEADER))
        return false;
    if (xx_rt_memcmp(out->header, "MEDIA DESCRIPTOR", 16U) != 0 ||
        out->header[16] != 2U)
        return false;
    key_field = xx_data_get_u32(out->header + 44, 4, 0, false);
    if (key_field == 0xFFFFFFFFU) {
        uint8_t loc[16];
        uint64_t offset, length;
        if (!mdx_read_at(format->device, base + MDX_FILE_HEADER, loc, 16U))
            return false;
        offset = xx_data_get_u64(loc, 8, 0, false);
        length = xx_data_get_u64(loc + 8, 8, 0, false);
        if (offset < MDX_FILE_HEADER + 16U || length <= MDX_SALT ||
            length > (uint64_t)MDX_MAX_DESCRIPTOR + MDX_SALT ||
            offset > (uint64_t)size || length > (uint64_t)size - offset ||
            (uint64_t)size - offset - length < MDX_KEY_HEADER - MDX_SALT)
            return false;
        out->is_mdx = true;
        out->descriptor_offset = base + (int64_t)offset;
        out->descriptor_size = length - MDX_SALT;
        out->key_offset = out->descriptor_offset + (int64_t)out->descriptor_size;
        out->format_size = (int64_t)(offset + length + MDX_KEY_HEADER - MDX_SALT);
    } else {
        if (key_field < MDX_FILE_HEADER + 16U ||
            (uint64_t)key_field + MDX_KEY_HEADER > (uint64_t)size ||
            key_field - MDX_FILE_HEADER > MDX_MAX_DESCRIPTOR)
            return false;
        out->is_mdx = false;
        out->descriptor_offset = base + MDX_FILE_HEADER;
        out->descriptor_size = key_field - MDX_FILE_HEADER;
        out->key_offset = base + key_field;
        out->format_size = (int64_t)key_field + MDX_KEY_HEADER;
    }
    return (out->descriptor_size & 15U) == 0U;
}

/* Deciphers the primary key header and returns it with the descriptor's
 * compressed and plain sizes checked against the layout. */
static bool mdx_primary_key(Abstractformat *format, const mdx_layout *layout,
                            uint8_t key[MDX_KEY_HEADER]) {
    uint8_t password[MDX_SALT];
    uint32_t packed, plain;
    bool ok;
    if (!mdx_read_at(format->device, layout->key_offset, key, MDX_KEY_HEADER))
        return false;
    mdx_salt_password(key, password);
    ok = mdx_open_key_header(key, password, sizeof(password), true);
    xx_rt_memset(password, 0, sizeof(password));
    if (!ok) return false;
    packed = xx_data_get_u32(key + 336, 4, 0, false);
    plain = xx_data_get_u32(key + 340, 4, 0, false);
    if (packed == 0U || plain < MDX_DESC_HEADER - MDX_PREFIX ||
        plain > MDX_MAX_DESCRIPTOR ||
        (((uint64_t)packed + 15U) & ~(uint64_t)15U) != layout->descriptor_size)
        return false;
    return true;
}

static bool mdx_in(uint64_t offset, uint64_t size, uint64_t total) {
    return offset <= total && size <= total - offset;
}

static uint32_t mdx_main_size(uint8_t flags, uint32_t *audio) {
    uint32_t mode = flags & 7U, size;
    bool sync = (flags & 0x80U) != 0, sub = (flags & 0x40U) != 0,
         hdr = (flags & 0x20U) != 0, edc = (flags & 0x08U) != 0;
    *audio = 0U;
    switch (mode) {
    case 1: *audio = 1U; return 2352U;
    case 2:
        size = 2048U + (sync ? 12U : 0U) + (hdr ? 4U : 0U) + (edc ? 288U : 0U);
        return size;
    case 3: return 2336U + (sync ? 12U : 0U) + (hdr ? 4U : 0U);
    case 4:
        return 2048U + (sync ? 12U : 0U) + (hdr ? 4U : 0U) + (sub ? 8U : 0U) +
               (edc ? 280U : 0U);
    case 5:
        return 2324U + (sync ? 12U : 0U) + (hdr ? 4U : 0U) + (sub ? 8U : 0U) +
               (edc ? 4U : 0U);
    default: return 0U;
    }
}

static bool mdx_name_is_safe_basename(const char *name) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t length, stem = 0U, i, d;
    bool meaningful = false;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < length && name[stem] != '.') ++stem;
    for (d = 0; d < sizeof(devices) / sizeof(devices[0]); ++d) {
        const char *w = devices[d];
        size_t k = 0;
        while (k < stem && w[k]) {
            char c = name[k];
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            if (c != w[k]) break;
            ++k;
        }
        if (k == stem && !w[k]) return false;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9') {
        char a = name[0], b = name[1], c = name[2];
        if (a >= 'a') a = (char)(a - 32);
        if (b >= 'a') b = (char)(b - 32);
        if (c >= 'a') c = (char)(c - 32);
        if ((a == 'C' && b == 'O' && c == 'M') || (a == 'L' && b == 'P' && c == 'T'))
            return false;
    }
    return true;
}

/* MDS v2 data file name: UTF-16LE, NUL-terminated, inside the descriptor.
 * Non-ASCII code units become '_'; the result is only used to find the
 * file next to the descriptor, never as an output name. */
static bool mdx_read_file_name(const uint8_t *desc, uint64_t desc_size,
                               uint32_t offset, char *out) {
    uint32_t n = 0U;
    if (offset < MDX_DESC_HEADER || offset >= desc_size) return false;
    for (;;) {
        uint16_t u;
        if ((uint64_t)offset + 2U * n + 2U > desc_size || n >= MDX_FILE_NAME_MAX - 1U)
            return false;
        u = xx_data_get_u16(desc + offset + 2U * n, 2, 0, false);
        if (u == 0U) break;
        out[n++] = (u >= 0x20U && u < 0x7FU) ? (char)u : '_';
    }
    out[n] = 0;
    return n > 0U;
}

static void mdx_make_name(char *name, uint32_t point, const char *extension) {
    size_t at = 0U, i;
    const char prefix[] = "track";
    for (i = 0; prefix[i]; ++i) name[at++] = prefix[i];
    name[at++] = (char)('0' + (point / 10U) % 10U);
    name[at++] = (char)('0' + point % 10U);
    name[at++] = '.';
    for (i = 0; extension[i]; ++i) name[at++] = extension[i];
    name[at] = 0;
}

static bool mdx_parse_descriptor(mdx_image *image, const uint8_t *desc,
                                 uint64_t desc_size, int64_t data_base,
                                 int64_t data_total) {
    uint32_t sessions, sessions_offset, s, key_offset;
    if (desc_size < MDX_DESC_HEADER) return false;
    image->medium_type = xx_data_get_u16(desc + 18, 2, 0, false);
    sessions = xx_data_get_u16(desc + 20, 2, 0, false);
    image->sessions = (uint16_t)sessions;
    sessions_offset = xx_data_get_u32(desc + 80, 4, 0, false);
    key_offset = xx_data_get_u32(desc + 88, 4, 0, false);
    if (image->medium_type != 0U && image->medium_type != 3U) return false;
    if (sessions == 0U || sessions > MDX_MAX_SESSIONS ||
        sessions_offset < MDX_DESC_HEADER ||
        !mdx_in(sessions_offset, (uint64_t)sessions * MDX_SESSION_SIZE, desc_size))
        return false;
    if (key_offset) {
        if (key_offset < MDX_DESC_HEADER || key_offset > desc_size ||
            desc_size - key_offset != MDX_KEY_HEADER)
            return false;
        image->data_encrypted = true;
    }
    image->footers = (mdx_footer *)xx_mem_calloc(MDX_MAX_FOOTERS, sizeof(mdx_footer));
    if (!image->footers) return false;
    for (s = 0; s < sessions; ++s) {
        const uint8_t *sb = desc + sessions_offset + (uint64_t)s * MDX_SESSION_SIZE;
        uint32_t blocks = sb[10], tracks_offset = xx_data_get_u32(sb + 20, 4, 0, false), b;
        if (tracks_offset < MDX_DESC_HEADER ||
            !mdx_in(tracks_offset, (uint64_t)blocks * MDX_TRACK_SIZE, desc_size))
            return false;
        for (b = 0; b < blocks; ++b) {
            const uint8_t *tb = desc + tracks_offset + (uint64_t)b * MDX_TRACK_SIZE;
            uint32_t point = tb[4], audio = 0U, main_size, sub_size = 0U,
                     declared = xx_data_get_u16(tb + 16, 2, 0, false), footers, footers_offset, f;
            uint64_t start = xx_data_get_u64(tb + 40, 8, 0, false), total_sectors = 0U;
            mdx_track *track;
            uint32_t k;
            if (point == 0U || point > 99U) continue; /* lead-in entries */
            if (image->track_count >= MDX_MAX_TRACKS) return false;
            for (k = 0; k < image->track_count; ++k)
                if (image->tracks[k].point == point) return false;
            footers = xx_data_get_u32(tb + 48, 4, 0, false);
            footers_offset = xx_data_get_u32(tb + 52, 4, 0, false);
            if (footers == 0U || footers > MDX_MAX_FOOTERS_PER_TRACK ||
                footers_offset < MDX_DESC_HEADER ||
                image->footer_count + footers > MDX_MAX_FOOTERS ||
                !mdx_in(footers_offset, (uint64_t)footers * MDX_FOOTER_SIZE, desc_size))
                return false;
            main_size = mdx_main_size(tb[0], &audio);
            if (main_size == 0U) {
                /* Unknown sector type: keep the declared stored size. */
                main_size = declared;
            } else if (image->version_minor == 0U) {
                if (declared == main_size + 16U) sub_size = 16U;
                else if (declared == main_size + 96U) sub_size = 96U;
            } else {
                switch ((tb[1] >> 3) & 7U) {
                case 1: case 4: sub_size = 96U; break;
                case 2: sub_size = 16U; break;
                default: break;
                }
            }
            if (main_size == 0U || main_size + sub_size > MDX_MAX_SECTOR_SIZE)
                return false;
            track = &image->tracks[image->track_count];
            xx_mem_zero(track, sizeof(*track));
            track->point = point;
            track->session = xx_data_get_u16(sb + 8, 2, 0, false);
            track->mode = (uint8_t)(tb[0] & 7U);
            track->stored_size = main_size + sub_size;
            track->footer_first = image->footer_count;
            track->footer_count = footers;
            mdx_make_name(track->name, point,
                          audio ? "cdda"
                                : (track->stored_size == 2048U ? "iso" : "bin"));
            for (f = 0; f < footers; ++f) {
                const uint8_t *fb = desc + footers_offset + (uint64_t)f * MDX_FOOTER_SIZE;
                mdx_footer *footer = &image->footers[image->footer_count + f];
                uint64_t sectors = xx_data_get_u64(fb + 16, 8, 0, false);
                if (sectors > MDX_MAX_SECTORS) return false;
                footer->sectors = sectors;
                footer->compressed = (fb[4] & 1U) != 0;
                footer->group = xx_data_get_u32(fb + 12, 4, 0, false);
                footer->table_offset = xx_data_get_u64(fb + 24, 8, 0, false);
                if (footer->compressed &&
                    (footer->group == 0U || footer->group > MDX_MAX_GROUP))
                    return false;
                if (image->is_mdx) {
                    footer->file = 0U;
                    footer->data_offset = f == 0U ? start : 0U;
                    if (footer->data_offset > (uint64_t)INT64_MAX - (uint64_t)data_base)
                        return false;
                    footer->data_offset += (uint64_t)data_base;
                    if (f != 0U) return false; /* MDX is never split */
                    if (!footer->compressed &&
                        !mdx_in(footer->data_offset,
                                sectors * track->stored_size, (uint64_t)data_total))
                        return false;
                } else {
                    char name[MDX_FILE_NAME_MAX];
                    uint32_t n;
                    if (!mdx_read_file_name(desc, desc_size, xx_data_get_u32(fb, 4, 0, false), name))
                        return false;
                    for (n = 0; n < image->file_count; ++n)
                        if (xx_str_cmp(image->files[n], name) == 0) break;
                    if (n == image->file_count) {
                        if (n >= XX_DAEMON_TOOLS_MDX_MAX_FILES) return false;
                        image->files[n] = xx_str_dup(name);
                        if (!image->files[n]) return false;
                        ++image->file_count;
                    }
                    footer->file = n;
                    footer->data_offset = f == 0U ? start : 0U;
                    if (footer->data_offset > (uint64_t)INT64_MAX / 2U) return false;
                }
                if (footer->compressed) track->compressed = true;
                total_sectors += sectors;
            }
            track->sectors = total_sectors;
            image->footer_count += footers;
            ++image->track_count;
        }
    }
    return image->track_count > 0U;
}

static bool mdx_data_key(mdx_image *image, uint8_t *key_header) {
    uint8_t password[MDX_SALT];
    bool ok;
    mdx_salt_password(key_header, password);
    ok = mdx_open_key_header(key_header, password, sizeof(password), false);
    xx_rt_memset(password, 0, sizeof(password));
    if (!ok) return false;
    image->lrw = (mdx_lrw *)xx_mem_alloc(sizeof(*image->lrw));
    if (!image->lrw) return false;
    /* Tweak key: the first 16 key bytes; AES key: bytes 32..63. */
    mdx_lrw_init(image->lrw, key_header + 80, key_header + 80 + 32);
    return true;
}

static mdx_image *mdx_load(Abstractformat *format) {
    mdx_layout layout;
    uint8_t key[MDX_KEY_HEADER];
    uint8_t *raw = NULL, *desc = NULL;
    uint32_t packed, plain;
    size_t written = 0U;
    mdx_image *image = NULL;
    bool ok = false;
    xx_mem_zero(&layout, sizeof(layout));
    if (!mdx_layout_read(format, &layout) || !mdx_primary_key(format, &layout, key))
        return NULL;
    packed = xx_data_get_u32(key + 336, 4, 0, false);
    plain = xx_data_get_u32(key + 340, 4, 0, false);
    raw = (uint8_t *)xx_mem_alloc((size_t)layout.descriptor_size);
    desc = (uint8_t *)xx_mem_calloc(1U, (size_t)plain + MDX_PREFIX);
    image = (mdx_image *)xx_mem_calloc(1U, sizeof(*image));
    if (!raw || !desc || !image) goto done;
    if (!mdx_read_at(format->device, layout.descriptor_offset, raw,
                     (size_t)layout.descriptor_size))
        goto done;
    {
        /* The descriptor is enciphered in 512-byte units, each restarting
         * the chain from the same IV. */
        size_t at;
        for (at = 0; at < layout.descriptor_size; at += 512U) {
            size_t unit = (size_t)layout.descriptor_size - at;
            if (unit > 512U) unit = 512U;
            if (!mdx_cbc_whitened(raw + at, unit, key + 80 + 32, key + 80))
                goto done;
        }
    }
    if (!xx_zlib_stream_decode_memory(raw, packed, desc + MDX_PREFIX, plain,
                                      &written) ||
        written != plain ||
        !xx_zlib_stream_trailer_matches(raw, packed, desc + MDX_PREFIX, plain))
        goto done;
    xx_rt_memcpy(desc, layout.header, MDX_PREFIX);
    image->is_mdx = layout.is_mdx;
    image->format_size = layout.format_size;
    image->version_minor = layout.header[17];
    if (!mdx_parse_descriptor(image, desc, (uint64_t)plain + MDX_PREFIX,
                              format->base_address,
                              xx_io_total_size(format->device)))
        goto done;
    if (image->data_encrypted) {
        uint32_t key_offset = xx_data_get_u32(desc + 88, 4, 0, false);
        image->data_key = mdx_data_key(image, desc + key_offset);
    }
    ok = true;
done:
    xx_rt_memset(key, 0, sizeof(key));
    if (raw) xx_mem_free(raw);
    if (desc) xx_mem_free(desc);
    if (!ok) {
        mdx_image_free(image);
        image = NULL;
    }
    return image;
}

/* ---------------------------------------------------------------------- */
/* Track extraction                                                        */

typedef struct mdx_writer_s {
    xx_io_device *out;
    uint64_t written;
    xx_pd_struct *pd;
} mdx_writer;

static bool mdx_emit(mdx_writer *w, const uint8_t *data, size_t size) {
    if (w->pd && xx_pd_is_stopped(w->pd)) return false;
    if (!mdx_write_all(w->out, data, size)) return false;
    w->written += size;
    return true;
}

/* Uncompressed footer: sectors back to back.  Enciphered data is one LRW
 * run per sector whose first index is 1 + sector * (size & ~15) / 16. */
static bool mdx_copy_plain(const mdx_image *image, xx_io_device *device,
                           const mdx_footer *footer, uint32_t stored,
                           mdx_writer *w, uint8_t *buffer, size_t capacity) {
    uint64_t sector = 0U;
    uint32_t per_chunk = (uint32_t)(capacity / stored);
    int64_t total = xx_io_total_size(device);
    if (per_chunk == 0U || total < 0 ||
        !mdx_in(footer->data_offset, footer->sectors * stored, (uint64_t)total))
        return false;
    while (sector < footer->sectors) {
        uint64_t left = footer->sectors - sector;
        uint32_t n = left < per_chunk ? (uint32_t)left : per_chunk, k;
        if (!mdx_read_at(device, (int64_t)(footer->data_offset + sector * stored),
                         buffer, (size_t)n * stored))
            return false;
        if (image->data_encrypted)
            for (k = 0; k < n; ++k)
                if (!mdx_lrw_decrypt(image->lrw, buffer + (size_t)k * stored, stored,
                                     1U + (sector + k) * (stored & ~15U) / 16U))
                    return false;
        if (!mdx_emit(w, buffer, (size_t)n * stored)) return false;
        sector += n;
    }
    return true;
}

/* Compressed footer: a zlib-packed table of one u16 per group (0 stored,
 * 0x8000|v a run of byte v, else the raw-Deflate size of the group). */
static bool mdx_copy_grouped(const mdx_image *image, xx_io_device *device,
                             const mdx_footer *footer, uint32_t stored,
                             mdx_writer *w) {
    uint64_t groups, g, cursor;
    uint16_t *table = NULL;
    uint8_t *packed = NULL, *group_buffer = NULL, *raw_table = NULL;
    size_t group_bytes = (size_t)footer->group * stored, table_read, written = 0U;
    int64_t total = xx_io_total_size(device);
    bool ok = false;
    if (total < 0 || footer->sectors == 0U) return footer->sectors == 0U;
    groups = (footer->sectors + footer->group - 1U) / footer->group;
    if (footer->table_offset > (uint64_t)total ||
        footer->data_offset > (uint64_t)total - footer->table_offset)
        return false;
    table_read = (size_t)((groups + 0x800U) * 2U);
    if ((uint64_t)table_read > (uint64_t)total - footer->data_offset - footer->table_offset)
        table_read = (size_t)((uint64_t)total - footer->data_offset - footer->table_offset);
    if (table_read < 2U) return false;
    raw_table = (uint8_t *)xx_mem_alloc(table_read);
    table = (uint16_t *)xx_mem_alloc((size_t)groups * 2U);
    group_buffer = (uint8_t *)xx_mem_alloc(group_bytes);
    packed = (uint8_t *)xx_mem_alloc(0x8000U);
    if (!raw_table || !table || !group_buffer || !packed) goto done;
    if (!mdx_read_at(device, (int64_t)(footer->data_offset + footer->table_offset),
                     raw_table, table_read) ||
        !xx_zlib_stream_decode_memory(raw_table, table_read, (uint8_t *)table,
                                      (size_t)groups * 2U, &written) ||
        written != (size_t)groups * 2U)
        goto done;
    cursor = footer->data_offset;
    for (g = 0; g < groups; ++g) {
        uint16_t value = xx_data_get_u16((const uint8_t *)&table[g], 2, 0, false);
        uint64_t first = g * footer->group;
        uint32_t count = footer->group;
        size_t bytes;
        if (footer->sectors - first < count) count = (uint32_t)(footer->sectors - first);
        bytes = (size_t)count * stored;
        if (value & 0x8000U) {
            xx_rt_memset(group_buffer, value & 0xFFU, bytes);
        } else {
            size_t read = value ? value : bytes;
            size_t got = 0U;
            if (!mdx_in(cursor, read, (uint64_t)total)) goto done;
            if (value) {
                if (!mdx_read_at(device, (int64_t)cursor, packed, read)) goto done;
                if (image->data_encrypted &&
                    !mdx_lrw_decrypt(image->lrw, packed, read,
                                     1U + first * (stored & ~15U) / 16U))
                    goto done;
                xx_rt_memset(group_buffer, 0, bytes);
                if (!xx_deflate_decompress_memory(packed, read, group_buffer, bytes,
                                                  &got, false) ||
                    got > bytes)
                    goto done;
            } else {
                /* A stored group occupies a full group in the file even
                 * when it is the short last one; only its sectors are read. */
                if (!mdx_in(cursor, bytes, (uint64_t)total)) goto done;
                if (!mdx_read_at(device, (int64_t)cursor, group_buffer, bytes))
                    goto done;
                if (image->data_encrypted &&
                    !mdx_lrw_decrypt(image->lrw, group_buffer, bytes,
                                     1U + first * (stored & ~15U) / 16U))
                    goto done;
                read = group_bytes;
            }
            cursor += read;
        }
        if (!mdx_emit(w, group_buffer, bytes)) goto done;
    }
    ok = true;
done:
    if (raw_table) xx_mem_free(raw_table);
    if (table) xx_mem_free(table);
    if (group_buffer) xx_mem_free(group_buffer);
    if (packed) xx_mem_free(packed);
    return ok;
}

static bool mdx_extract_track(xx_daemon_tools_mdx *archive, const mdx_image *image,
                              const mdx_track *track, xx_io_device *out,
                              xx_pd_struct *pd) {
    mdx_writer w;
    uint8_t *buffer;
    uint32_t f;
    bool ok = true;
    if (image->data_encrypted && !image->data_key) return false;
    buffer = (uint8_t *)xx_mem_alloc(MDX_COPY_CHUNK > track->stored_size
                                         ? MDX_COPY_CHUNK : track->stored_size);
    if (!buffer) return false;
    w.out = out;
    w.written = 0U;
    w.pd = pd;
    for (f = 0; f < track->footer_count && ok; ++f) {
        const mdx_footer *footer = &image->footers[track->footer_first + f];
        xx_io_device *device = image->is_mdx ? archive->format.device
                                             : (footer->file < XX_DAEMON_TOOLS_MDX_MAX_FILES
                                                    ? archive->data[footer->file] : NULL);
        if (!device) {
            ok = false;
            break;
        }
        if (footer->compressed)
            ok = mdx_copy_grouped(image, device, footer, track->stored_size, &w);
        else
            ok = mdx_copy_plain(image, device, footer, track->stored_size, &w,
                                buffer, MDX_COPY_CHUNK > track->stored_size
                                            ? MDX_COPY_CHUNK : track->stored_size);
    }
    xx_mem_free(buffer);
    return ok && w.written == track->sectors * track->stored_size;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

typedef struct mdx_stream_s {
    uint32_t index;
    uint32_t count;
} mdx_stream;

static mdx_image *mdx_image_of(Abstractformat *format) {
    xx_daemon_tools_mdx *archive = (xx_daemon_tools_mdx *)format;
    if (!archive) return NULL;
    if (!archive->image && !xx_daemon_tools_mdx_handle_base_info(format, NULL))
        return NULL;
    return (mdx_image *)archive->image;
}

void xx_daemon_tools_mdx_init(xx_daemon_tools_mdx *archive, xx_io_device *device,
                              int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_DAEMON_TOOLS_MDX_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-mdx");
    xx_format_set_extension(&archive->format, "mdx");
    archive->format.check_is_valid = xx_daemon_tools_mdx_check_is_valid;
    archive->format.handle_base_info = xx_daemon_tools_mdx_handle_base_info;
    archive->format.get_format_size = xx_daemon_tools_mdx_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_daemon_tools_mdx_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_daemon_tools_mdx_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_daemon_tools_mdx_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_daemon_tools_mdx_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_daemon_tools_mdx_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_daemon_tools_mdx_free_archive_records_reading;
}

xx_daemon_tools_mdx *xx_daemon_tools_mdx_create(xx_io_device *device,
                                                int64_t base_address) {
    xx_daemon_tools_mdx *archive =
        (xx_daemon_tools_mdx *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_daemon_tools_mdx_init(archive, device, base_address);
    return archive;
}

void xx_daemon_tools_mdx_destroy(xx_daemon_tools_mdx *archive) {
    uint32_t i;
    if (!archive) return;
    for (i = 0; i < XX_DAEMON_TOOLS_MDX_MAX_FILES; ++i) {
        if (archive->data[i] && archive->data_owned[i]) xx_io_close(archive->data[i]);
        archive->data[i] = NULL;
        archive->data_owned[i] = false;
    }
    mdx_image_free((mdx_image *)archive->image);
    archive->image = NULL;
    xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_daemon_tools_mdx_free(xx_daemon_tools_mdx *archive) {
    if (!archive) return;
    xx_daemon_tools_mdx_destroy(archive);
    xx_mem_free(archive);
}

bool xx_daemon_tools_mdx_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    mdx_layout layout;
    uint8_t key[MDX_KEY_HEADER];
    bool ok;
    (void)pd;
    xx_mem_zero(&layout, sizeof(layout));
    if (!mdx_layout_read(format, &layout)) return false;
    ok = mdx_primary_key(format, &layout, key);
    xx_rt_memset(key, 0, sizeof(key));
    return ok;
}

bool xx_daemon_tools_mdx_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xx_daemon_tools_mdx *archive = (xx_daemon_tools_mdx *)format;
    mdx_image *image;
    (void)pd;
    if (!archive) return false;
    if (archive->image) return true;
    image = mdx_load(format);
    if (!image) return false;
    archive->image = image;
    archive->number_of_records = image->track_count;
    archive->is_mdx = image->is_mdx;
    archive->version_minor = image->version_minor;
    archive->medium_type = image->medium_type;
    archive->number_of_sessions = image->sessions;
    archive->data_encrypted = image->data_encrypted;
    archive->data_key_available = image->data_key;
    archive->number_of_files = image->file_count;
    format->number_of_archive_records = image->track_count;
    format->format_size = image->format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    if (!image->is_mdx) xx_format_set_extension(format, "mds");
    return true;
}

int64_t xx_daemon_tools_mdx_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_daemon_tools_mdx_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_daemon_tools_mdx_get_number_of_archive_records(Abstractformat *format,
                                                           xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_daemon_tools_mdx_handle_base_info(format, pd))
               ? ((xx_daemon_tools_mdx *)format)->number_of_records : 0U;
}

const char *xx_daemon_tools_mdx_get_file_name(xx_daemon_tools_mdx *archive,
                                              uint32_t index) {
    mdx_image *image = archive ? mdx_image_of(&archive->format) : NULL;
    return image && index < image->file_count ? image->files[index] : NULL;
}

bool xx_daemon_tools_mdx_set_data_device(xx_daemon_tools_mdx *archive,
                                         uint32_t index, xx_io_device *device) {
    if (!archive || index >= XX_DAEMON_TOOLS_MDX_MAX_FILES) return false;
    if (archive->data[index] && archive->data_owned[index])
        xx_io_close(archive->data[index]);
    archive->data[index] = device;
    archive->data_owned[index] = false;
    return true;
}

uint32_t xx_daemon_tools_mdx_open_data_files(xx_daemon_tools_mdx *archive,
                                             const char *mds_path) {
    mdx_image *image;
    size_t directory = 0U, dot = 0U, i, length;
    uint32_t file, opened = 0U;
    if (!archive || !mds_path || !(image = mdx_image_of(&archive->format)) ||
        image->is_mdx)
        return 0U;
    length = xx_str_len(mds_path);
    for (i = 0; i < length; ++i) {
        if (mds_path[i] == '/' || mds_path[i] == '\\') {
            directory = i + 1U;
            dot = 0U;
        } else if (mds_path[i] == '.') {
            dot = i;
        }
    }
    for (file = 0; file < image->file_count; ++file) {
        const char *declared = image->files[file];
        char *path = NULL;
        xx_io_device *device;
        if (archive->data[file]) {
            ++opened;
            continue;
        }
        if (declared[0] == '*' && declared[1] == '.') {
            /* "*.ext": the descriptor's own name with that extension. */
            size_t stem = dot > directory ? dot : length;
            const char *ext = declared + 1;
            size_t ext_len = xx_str_len(ext);
            if (!mdx_name_is_safe_basename(ext + 1)) continue;
            path = (char *)xx_mem_alloc(stem + ext_len + 1U);
            if (!path) continue;
            xx_rt_memcpy(path, mds_path, stem);
            xx_rt_memcpy(path + stem, ext, ext_len + 1U);
        } else {
            size_t base_len;
            if (!mdx_name_is_safe_basename(declared)) continue;
            base_len = xx_str_len(declared);
            path = (char *)xx_mem_alloc(directory + base_len + 1U);
            if (!path) continue;
            xx_rt_memcpy(path, mds_path, directory);
            xx_rt_memcpy(path + directory, declared, base_len + 1U);
        }
        device = xx_io_file_open(path, "rb");
        xx_mem_free(path);
        if (!device) continue;
        archive->data[file] = device;
        archive->data_owned[file] = true;
        ++opened;
    }
    return opened;
}

static bool mdx_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *mdx_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool mdx_set_record(xx_archive_record *record, const mdx_image *image,
                           const mdx_track *track) {
    const mdx_footer *first = &image->footers[track->footer_first];
    uint64_t size = track->sectors * track->stored_size;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->data_offset = (int64_t)first->data_offset;
    record->compressed_size = (int64_t)size;
    return xx_archive_record_set_original_name(record, track->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          track->compressed ? 8U : 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           image->data_encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

xx_archive_record_state *xx_daemon_tools_mdx_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    mdx_image *image;
    mdx_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!(image = mdx_image_of(format)) || image->track_count == 0U) return NULL;
    stream = (mdx_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->count = image->track_count;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = xx_mem_free;
    state->total_records = stream->count;
    if (!mdx_copy_options(&state->options, options) ||
        !mdx_set_record(&state->current_record, image, &image->tracks[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_daemon_tools_mdx_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_daemon_tools_mdx_archive_record_move_to_next(Abstractformat *format,
                                                     xx_archive_record_state *state,
                                                     xx_pd_struct *pd) {
    mdx_stream *stream;
    mdx_image *image;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (mdx_stream *)state->internal_state) ||
        !(image = mdx_image_of(format)) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    if (!mdx_set_record(&state->current_record, image, &image->tracks[stream->index])) {
        state->has_record = false;
        return false;
    }
    return true;
}

bool xx_daemon_tools_mdx_unpack_current_archive_record(Abstractformat *format,
                                                       xx_archive_record_state *state,
                                                       xx_pd_struct *pd) {
    mdx_stream *stream;
    mdx_image *image;
    const mdx_track *track;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL, *path = NULL;
    bool result = false, created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (mdx_stream *)state->internal_state) ||
        !(image = mdx_image_of(format)) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    track = &image->tracks[stream->index];
    path_option = mdx_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return mdx_extract_track((xx_daemon_tools_mdx *)format, image, track, NULL, pd);
    if (!mdx_name_is_safe_basename(track->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", track->name)
               : xx_str_concat(base, track->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = mdx_extract_track((xx_daemon_tools_mdx *)format, image, track,
                                   destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_daemon_tools_mdx_free_archive_records_reading(Abstractformat *format,
                                                      xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
