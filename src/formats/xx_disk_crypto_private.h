/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original disk-mode/KDF/AF glue from the public LUKS1 and QEMU format facts.
 * AES round primitives below are reused from this library's MIT xx_aes.c.
 * No QEMU/cryptsetup implementation is incorporated.
 */
#ifndef XX_DISK_CRYPTO_PRIVATE_H
#define XX_DISK_CRYPTO_PRIVATE_H
#include "xxfclib/algo/sha/xx_sha.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>

static void dc_clear(void *p, size_t n)
{
    volatile uint8_t *q = (volatile uint8_t *)p;
    while (n--) *q++ = 0;
}
static void dc_copy(uint8_t *d, const uint8_t *s, size_t n)
{
    if (n) xx_rt_memcpy(d, s, n);
}
#define DC_AES_BLOCK_SIZE 16U
typedef struct dc_aes_context {
    uint8_t round_keys[240], sbox[256], inverse_sbox[256];
    unsigned rounds;
} dc_aes_context;
static uint8_t dc_aes_xtime(uint8_t value)
{
    return (uint8_t)((uint8_t)(value << 1U) ^ (uint8_t)(0x1BU & (uint8_t)(0U - (uint8_t)(value >> 7U))));
}

static uint8_t dc_aes_gf_multiply(uint8_t left, uint8_t right)
{
    uint8_t result = 0U;
    unsigned int bit;
    for (bit = 0U; bit < 8U; ++bit) {
        uint8_t mask = (uint8_t)(0U - (uint8_t)(right & 1U));
        result ^= (uint8_t)(left & mask);
        left = dc_aes_xtime(left);
        right >>= 1U;
    }
    return result;
}

static uint8_t dc_aes_gf_inverse(uint8_t value)
{
    uint8_t result = 1U;
    uint8_t base = value;
    unsigned int exponent = 254U;

    if (value == 0U) {
        return 0U;
    }
    while (exponent > 0U) {
        if ((exponent & 1U) != 0U) {
            result = dc_aes_gf_multiply(result, base);
        }
        base = dc_aes_gf_multiply(base, base);
        exponent >>= 1U;
    }
    return result;
}

static uint8_t dc_rotate_left8(uint8_t value, unsigned int count)
{
    return (uint8_t)(((uint32_t)value << count) | ((uint32_t)value >> (8U - count)));
}

static uint8_t dc_aes_calculate_sbox(uint8_t value)
{
    uint8_t inverse = dc_aes_gf_inverse(value);
    return (uint8_t)(inverse ^ dc_rotate_left8(inverse, 1U) ^ dc_rotate_left8(inverse, 2U) ^ dc_rotate_left8(inverse, 3U) ^ dc_rotate_left8(inverse, 4U) ^ 0x63U);
}

static bool dc_aes_set_key(dc_aes_context *context, const uint8_t *key, size_t key_size)
{
    size_t generated;
    size_t round_key_size;
    uint8_t rcon = 1U;
    unsigned int index;

    if (!context || !key || (key_size != 16U && key_size != 24U && key_size != 32U)) {
        return false;
    }

    for (index = 0U; index < 256U; ++index) {
        context->sbox[index] = dc_aes_calculate_sbox((uint8_t)index);
    }
    for (index = 0U; index < 256U; ++index) {
        context->inverse_sbox[context->sbox[index]] = (uint8_t)index;
    }
    context->rounds = (unsigned int)(key_size / 4U) + 6U;
    round_key_size = ((size_t)context->rounds + 1U) * DC_AES_BLOCK_SIZE;
    dc_copy(context->round_keys, key, key_size);
    generated = key_size;

    while (generated < round_key_size) {
        uint8_t temporary[4];
        temporary[0] = context->round_keys[generated - 4U];
        temporary[1] = context->round_keys[generated - 3U];
        temporary[2] = context->round_keys[generated - 2U];
        temporary[3] = context->round_keys[generated - 1U];

        if ((generated % key_size) == 0U) {
            uint8_t rotated = temporary[0];
            temporary[0] = context->sbox[temporary[1]];
            temporary[1] = context->sbox[temporary[2]];
            temporary[2] = context->sbox[temporary[3]];
            temporary[3] = context->sbox[rotated];
            temporary[0] ^= rcon;
            rcon = dc_aes_xtime(rcon);
        } else if (key_size == 32U && (generated % key_size) == 16U) {
            temporary[0] = context->sbox[temporary[0]];
            temporary[1] = context->sbox[temporary[1]];
            temporary[2] = context->sbox[temporary[2]];
            temporary[3] = context->sbox[temporary[3]];
        }

        for (index = 0U; index < 4U && generated < round_key_size; ++index) {
            context->round_keys[generated] = (uint8_t)(context->round_keys[generated - key_size] ^ temporary[index]);
            ++generated;
        }
        dc_clear(temporary, sizeof(temporary));
    }
    return true;
}

static void dc_aes_add_round_key(uint8_t state[DC_AES_BLOCK_SIZE], const uint8_t *round_key)
{
    unsigned int index;
    for (index = 0U; index < DC_AES_BLOCK_SIZE; ++index) {
        state[index] ^= round_key[index];
    }
}

static void dc_aes_sub_bytes(uint8_t state[DC_AES_BLOCK_SIZE], const uint8_t sbox[256])
{
    unsigned int index;
    for (index = 0U; index < DC_AES_BLOCK_SIZE; ++index) {
        state[index] = sbox[state[index]];
    }
}

static void dc_aes_shift_rows(uint8_t state[DC_AES_BLOCK_SIZE])
{
    uint8_t temporary;

    temporary = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = temporary;

    temporary = state[2];
    state[2] = state[10];
    state[10] = temporary;
    temporary = state[6];
    state[6] = state[14];
    state[14] = temporary;

    temporary = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = temporary;
}

static void dc_aes_mix_columns(uint8_t state[DC_AES_BLOCK_SIZE])
{
    unsigned int column;
    for (column = 0U; column < 4U; ++column) {
        uint8_t *values = state + ((size_t)column * 4U);
        uint8_t first = values[0];
        uint8_t combined = (uint8_t)(values[0] ^ values[1] ^ values[2] ^ values[3]);
        values[0] ^= (uint8_t)(combined ^ dc_aes_xtime((uint8_t)(values[0] ^ values[1])));
        values[1] ^= (uint8_t)(combined ^ dc_aes_xtime((uint8_t)(values[1] ^ values[2])));
        values[2] ^= (uint8_t)(combined ^ dc_aes_xtime((uint8_t)(values[2] ^ values[3])));
        values[3] ^= (uint8_t)(combined ^ dc_aes_xtime((uint8_t)(values[3] ^ first)));
    }
}

static void dc_aes_encrypt_block(const dc_aes_context *context, const uint8_t input[DC_AES_BLOCK_SIZE], uint8_t output[DC_AES_BLOCK_SIZE])
{
    uint8_t state[DC_AES_BLOCK_SIZE];
    unsigned int round;

    dc_copy(state, input, sizeof(state));
    dc_aes_add_round_key(state, context->round_keys);

    for (round = 1U; round < context->rounds; ++round) {
        dc_aes_sub_bytes(state, context->sbox);
        dc_aes_shift_rows(state);
        dc_aes_mix_columns(state);
        dc_aes_add_round_key(state, context->round_keys + ((size_t)round * DC_AES_BLOCK_SIZE));
    }

    dc_aes_sub_bytes(state, context->sbox);
    dc_aes_shift_rows(state);
    dc_aes_add_round_key(state, context->round_keys + ((size_t)context->rounds * DC_AES_BLOCK_SIZE));
    dc_copy(output, state, sizeof(state));
    dc_clear(state, sizeof(state));
}

static void dc_aes_inverse_shift_rows(uint8_t state[DC_AES_BLOCK_SIZE])
{
    uint8_t temporary;

    temporary = state[13];
    state[13] = state[9];
    state[9] = state[5];
    state[5] = state[1];
    state[1] = temporary;

    temporary = state[2];
    state[2] = state[10];
    state[10] = temporary;
    temporary = state[6];
    state[6] = state[14];
    state[14] = temporary;

    temporary = state[3];
    state[3] = state[7];
    state[7] = state[11];
    state[11] = state[15];
    state[15] = temporary;
}

static void dc_aes_inverse_sub_bytes(uint8_t state[DC_AES_BLOCK_SIZE], const uint8_t inverse_sbox[256])
{
    unsigned int index;
    for (index = 0U; index < DC_AES_BLOCK_SIZE; ++index) {
        state[index] = inverse_sbox[state[index]];
    }
}

static void dc_aes_inverse_mix_columns(uint8_t state[DC_AES_BLOCK_SIZE])
{
    unsigned int column;
    for (column = 0U; column < 4U; ++column) {
        uint8_t *values = state + ((size_t)column * 4U);
        uint8_t a = values[0];
        uint8_t b = values[1];
        uint8_t c = values[2];
        uint8_t d = values[3];
        values[0] = dc_aes_gf_multiply(a, 14U) ^ dc_aes_gf_multiply(b, 11U) ^ dc_aes_gf_multiply(c, 13U) ^ dc_aes_gf_multiply(d, 9U);
        values[1] = dc_aes_gf_multiply(a, 9U) ^ dc_aes_gf_multiply(b, 14U) ^ dc_aes_gf_multiply(c, 11U) ^ dc_aes_gf_multiply(d, 13U);
        values[2] = dc_aes_gf_multiply(a, 13U) ^ dc_aes_gf_multiply(b, 9U) ^ dc_aes_gf_multiply(c, 14U) ^ dc_aes_gf_multiply(d, 11U);
        values[3] = dc_aes_gf_multiply(a, 11U) ^ dc_aes_gf_multiply(b, 13U) ^ dc_aes_gf_multiply(c, 9U) ^ dc_aes_gf_multiply(d, 14U);
    }
}

static void dc_aes_decrypt_block(const dc_aes_context *context, const uint8_t input[DC_AES_BLOCK_SIZE], uint8_t output[DC_AES_BLOCK_SIZE])
{
    uint8_t state[DC_AES_BLOCK_SIZE];
    unsigned int round;

    dc_copy(state, input, sizeof(state));
    dc_aes_add_round_key(state, context->round_keys + ((size_t)context->rounds * DC_AES_BLOCK_SIZE));
    for (round = context->rounds - 1U; round > 0U; --round) {
        dc_aes_inverse_shift_rows(state);
        dc_aes_inverse_sub_bytes(state, context->inverse_sbox);
        dc_aes_add_round_key(state, context->round_keys + ((size_t)round * DC_AES_BLOCK_SIZE));
        dc_aes_inverse_mix_columns(state);
    }
    dc_aes_inverse_shift_rows(state);
    dc_aes_inverse_sub_bytes(state, context->inverse_sbox);
    dc_aes_add_round_key(state, context->round_keys);
    dc_copy(output, state, sizeof(state));
    dc_clear(state, sizeof(state));
}

typedef struct dc_hash {
    union {
        xx_sha1_context a;
        xx_sha256_context b;
    } u;
    bool sha256;
} dc_hash;
static void dc_hash_init(dc_hash *h, bool s)
{
    h->sha256 = s;
    if (s) xx_sha256_init(&h->u.b);
    else xx_sha1_init(&h->u.a);
}
static void dc_hash_update(dc_hash *h, const void *p, size_t n)
{
    if (h->sha256) xx_sha256_update(&h->u.b, p, n);
    else xx_sha1_update(&h->u.a, p, n);
}
static void dc_hash_final(dc_hash *h, uint8_t out[32])
{
    if (h->sha256) xx_sha256_final(&h->u.b, out, 32);
    else xx_sha1_final(&h->u.a, out, 20);
}
typedef struct dc_hmac {
    dc_hash inner, outer;
} dc_hmac;
static void dc_hmac_init(dc_hmac *h, bool sha256, const uint8_t *key, size_t n)
{
    uint8_t block[64] = {0}, digest[32] = {0};
    dc_hash t;
    size_t i;
    if (n > 64) {
        dc_hash_init(&t, sha256);
        dc_hash_update(&t, key, n);
        dc_hash_final(&t, digest);
        dc_copy(block, digest, sha256 ? 32 : 20);
    } else dc_copy(block, key, n);
    for (i = 0; i < 64; i++) block[i] ^= 0x36;
    dc_hash_init(&h->inner, sha256);
    dc_hash_update(&h->inner, block, 64);
    for (i = 0; i < 64; i++) block[i] ^= 0x36 ^ 0x5c;
    dc_hash_init(&h->outer, sha256);
    dc_hash_update(&h->outer, block, 64);
    dc_clear(block, sizeof(block));
    dc_clear(digest, sizeof(digest));
    dc_clear(&t, sizeof(t));
}
static void dc_hmac_parts(const dc_hmac *h, const uint8_t *a, size_t an, const uint8_t *b, size_t bn, uint8_t out[32])
{
    dc_hash i = h->inner, o = h->outer;
    uint8_t mid[32] = {0};
    dc_hash_update(&i, a, an);
    dc_hash_update(&i, b, bn);
    dc_hash_final(&i, mid);
    dc_hash_update(&o, mid, h->inner.sha256 ? 32 : 20);
    dc_hash_final(&o, out);
    dc_clear(mid, sizeof(mid));
}
/* Bound hostile iteration counts as well as the total work across eight slots. */
static bool dc_pbkdf(bool s, const uint8_t *pw, size_t pwn, const uint8_t *salt, size_t sn, uint32_t rounds, uint8_t *out, size_t n, uint64_t *work, xx_pd_struct *pd)
{
    dc_hmac h;
    uint8_t u[32] = {0}, v[32] = {0}, idx[4];
    size_t done = 0, hlen = s ? 32 : 20;
    uint32_t block = 1, j;
    bool ok = false;
    if (!rounds || rounds > 5000000U || n > 64 || !work || (uint64_t)rounds * ((n + hlen - 1) / hlen) > *work) return false;
    *work -= (uint64_t)rounds * ((n + hlen - 1) / hlen);
    dc_hmac_init(&h, s, pw, pwn);
    while (done < n) {
        size_t k, amount = n - done;
        if (amount > hlen) amount = hlen;
        idx[0] = (uint8_t)(block >> 24);
        idx[1] = (uint8_t)(block >> 16);
        idx[2] = (uint8_t)(block >> 8);
        idx[3] = (uint8_t)block;
        dc_hmac_parts(&h, salt, sn, idx, 4, u);
        dc_copy(v, u, hlen);
        for (j = 1; j < rounds; j++) {
            if ((j & 63U) == 0 && xx_pd_is_stopped(pd)) goto end;
            dc_hmac_parts(&h, u, hlen, NULL, 0, u);
            for (k = 0; k < hlen; k++) v[k] ^= u[k];
        }
        if (xx_pd_is_stopped(pd)) {
            goto end;
        }
        dc_copy(out + done, v, amount);
        done += amount;
        block++;
    }
    ok = true;
end:
    if (!ok) dc_clear(out, n);
    dc_clear(&h, sizeof(h));
    dc_clear(u, sizeof(u));
    dc_clear(v, sizeof(v));
    return ok;
}

enum {
    DC_CBC_PLAIN = 1,
    DC_CBC_PLAIN64,
    DC_CBC_ESSIV,
    DC_XTS_PLAIN64
};
typedef struct dc_crypto {
    dc_aes_context data, tweak;
    unsigned mode;
} dc_crypto;
static bool dc_crypto_init(dc_crypto *c, unsigned mode, const uint8_t *key, size_t n)
{
    uint8_t digest[32];
    bool ok = false;
    xx_mem_zero(c, sizeof(*c));
    c->mode = mode;
    if (mode == DC_XTS_PLAIN64) {
        if (n != 32 && n != 64) goto end;
        ok = dc_aes_set_key(&c->data, key, n / 2) && dc_aes_set_key(&c->tweak, key + n / 2, n / 2);
    } else {
        if (!dc_aes_set_key(&c->data, key, n)) goto end;
        if (mode == DC_CBC_ESSIV) {
            xx_sha256_memory(key, n, digest);
            if (!dc_aes_set_key(&c->tweak, digest, 32)) goto end;
        }
        ok = mode >= DC_CBC_PLAIN && mode <= DC_CBC_ESSIV;
    }
end:
    dc_clear(digest, sizeof(digest));
    if (!ok) dc_clear(c, sizeof(*c));
    return ok;
}
static bool dc_decrypt(dc_crypto *c, uint64_t sector, uint8_t *data, size_t n, xx_pd_struct *pd)
{
    uint8_t iv[16], a[16], b[16];
    size_t off, k, j;
    bool ok = false;
    if (!c || !c->mode || (n & 511U)) return false;
    for (off = 0; off < n; off += 512, sector++) {
        if (xx_pd_is_stopped(pd)) goto end;
        xx_mem_zero(iv, 16);
        for (k = 0; k < (c->mode == DC_CBC_PLAIN ? 4U : 8U); k++) iv[k] = (uint8_t)(sector >> (8 * k));
        if (c->mode == DC_CBC_ESSIV || c->mode == DC_XTS_PLAIN64) dc_aes_encrypt_block(&c->tweak, iv, iv);
        for (j = 0; j < 512; j += 16) {
            dc_copy(a, data + off + j, 16);
            if (c->mode == DC_XTS_PLAIN64) {
                unsigned carry = 0;
                for (k = 0; k < 16; k++) a[k] ^= iv[k];
                dc_aes_decrypt_block(&c->data, a, b);
                for (k = 0; k < 16; k++) data[off + j + k] = b[k] ^ iv[k];
                for (k = 0; k < 16; k++) {
                    unsigned next = iv[k] >> 7;
                    iv[k] = (uint8_t)((iv[k] << 1) | carry);
                    carry = next;
                }
                if (carry) iv[0] ^= 0x87;
            } else {
                dc_aes_decrypt_block(&c->data, a, b);
                for (k = 0; k < 16; k++) data[off + j + k] = b[k] ^ iv[k];
                dc_copy(iv, a, 16);
            }
        }
    }
    ok = true;
end:
    dc_clear(iv, sizeof(iv));
    dc_clear(a, sizeof(a));
    dc_clear(b, sizeof(b));
    return ok;
}
static bool dc_limit(Abstractformat *f, const xx_list_s *opts, xx_meta_id_t id, uint64_t def, uint64_t *out)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, opts, id);
    *out = def;
    if (!v) return true;
    if (v->type == XX_VAR_TYPE_UINT64) {
        *out = xx_var_get_u64(v);
        return true;
    }
    if (v->type == XX_VAR_TYPE_INT64 && xx_var_get_i64(v) >= 0) {
        *out = (uint64_t)xx_var_get_i64(v);
        return true;
    }
    return false;
}
static bool dc_password(Abstractformat *f, const xx_list_s *opts, const uint8_t **out, size_t *n, char **owned, uint64_t available)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, opts, XX_META_ID_OPT_PASSWORD);
    *out = NULL;
    *n = 0;
    *owned = NULL;
    if (!v) return false;
    if (v->type == XX_VAR_TYPE_STRING || v->type == XX_VAR_TYPE_STRING_VIEW) {
        *out = (const uint8_t *)xx_var_get_str(v);
        *n = v->val.str.len;
    } else if (v->type == XX_VAR_TYPE_BYTES || v->type == XX_VAR_TYPE_BYTES_VIEW) *out = xx_var_get_bytes(v, n);
    else if (v->type == XX_VAR_TYPE_WSTRING || v->type == XX_VAR_TYPE_WSTRING_VIEW) {
        if (available == 0 || v->val.wstr.len > 1048576U || v->val.wstr.len > (available - 1U) / 4U) return false;
        *owned = xx_str_unicode_to_utf8(xx_var_get_wstr(v));
        if (!*owned) return false;
        *out = (const uint8_t *)*owned;
        *n = xx_str_len(*owned);
    } else {
        return false;
    }
    return *n <= 1048576 && (*out || !*n);
}
static bool dc_read_at(xx_io_device *d, uint64_t off, uint8_t *p, size_t n)
{
    size_t done = 0;
    if (off > INT64_MAX || xx_io_seek64(d, (int64_t)off, SEEK_SET) != 0) return false;
    while (done < n) {
        ssize_t got = xx_io_read(d, p + done, n - done);
        if (got <= 0 || (size_t)got > n - done) return false;
        done += (size_t)got;
    }
    return true;
}
static bool dc_text(const uint8_t *p, size_t n, const char *s)
{
    size_t k = xx_str_len(s);
    return k < n && xx_rt_memcmp(p, s, k) == 0 && p[k] == 0;
}
static bool dc_same_path(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x == '\\') x = '/';
        if (y == '\\') y = '/';
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return *a == *b;
}
/* A path decode is staged beside its destination and published only after
 * complete success. A NULL-destination test never enters this helper. */
static xx_io_device *dc_stage(const char *destination, char **stage)
{
    char *parent = xx_str_dup(destination);
    size_t i, cut = 0;
    unsigned attempt;
    *stage = NULL;
    if (!parent) return NULL;
    for (i = 0; parent[i]; i++) {
        if (parent[i] == '/' || parent[i] == '\\') cut = i + 1;
    }
    parent[cut] = 0;
    for (attempt = 0; attempt < 128; attempt++) {
        char tail[40], *candidate;
        xx_io_device *d;
        xx_rt_snprintf(tail, sizeof(tail), ".xx_crypto.tmp.%u", attempt);
        candidate = xx_str_concat(parent, tail);
        if (!candidate) break;
        if (dc_same_path(candidate, destination)) {
            xx_str_free(candidate);
            continue;
        }
        d = xx_io_file_open(candidate, "wbx");
        if (d) {
            *stage = candidate;
            xx_str_free(parent);
            return d;
        }
        xx_str_free(candidate);
    }
    xx_str_free(parent);
    return NULL;
}
static unsigned dc_luks_mode(const uint8_t h[592])
{
    if (!dc_text(h + 8, 32, "aes")) return 0;
    if (dc_text(h + 40, 32, "cbc-plain")) return DC_CBC_PLAIN;
    if (dc_text(h + 40, 32, "cbc-plain64")) return DC_CBC_PLAIN64;
    if (dc_text(h + 40, 32, "cbc-essiv:sha256")) return DC_CBC_ESSIV;
    if (dc_text(h + 40, 32, "xts-plain64")) {
        return DC_XTS_PLAIN64;
    }
    return 0;
}
/* Header/keyslot extent bounds are also checked for detached QCOW2 headers.
 * AF material is read one sector at a time; its full declared striped extent
 * is authenticated indirectly by the recovered master-key digest. */
static XXFC_MAYBE_UNUSED bool dc_luks_unlock(xx_io_device *d, uint64_t base, uint64_t extent, bool detached, const uint8_t *pw, size_t pwn, dc_crypto *result,
                                             xx_pd_struct *pd)
{
    uint8_t h[592], key[64] = {0}, derived[64] = {0}, digest[32] = {0}, sector[512] = {0};
    dc_crypto trial;
    uint32_t n, iter, mode;
    bool sha256, ok = false;
    unsigned slot;
    uint64_t work = 16000000;
    uint64_t starts[8] = {0}, lengths[8] = {0}, payload;
    xx_mem_zero(&trial, sizeof(trial));
    xx_mem_zero(result, sizeof(*result));
    if (extent < 592 || extent > INT64_MAX || base > (uint64_t)INT64_MAX - extent || !dc_read_at(d, base, h, sizeof(h)) || xx_rt_memcmp(h, "LUKS\xba\xbe\0\1", 8))
        goto end;
    n = xx_data_get_u32(h, sizeof(h), 108, true);
    iter = xx_data_get_u32(h, sizeof(h), 164, true);
    mode = dc_luks_mode(h);
    payload = (uint64_t)xx_data_get_u32(h, sizeof(h), 104, true) * 512;
    if (!mode || !iter || iter > 5000000 || (mode == DC_XTS_PLAIN64 ? (n != 32 && n != 64) : (n != 16 && n != 24 && n != 32))) goto end;
    sha256 = dc_text(h + 72, 32, "sha256");
    if (!sha256 && !dc_text(h + 72, 32, "sha1")) goto end;
    if (!detached && (payload < 1024 || payload > extent)) goto end;
    for (slot = 0; slot < 8; slot++) {
        size_t a = 208 + 48 * slot;
        uint32_t active = xx_data_get_u32(h, sizeof(h), a, true), stripes = xx_data_get_u32(h, sizeof(h), a + 44, true);
        unsigned j;
        if (active == 0xdead) {
            continue;
        }
        if (active != 0xac71f3 || stripes == 0 || stripes > 1000000) goto end;
        starts[slot] = (uint64_t)xx_data_get_u32(h, sizeof(h), a + 40, true) * 512;
        lengths[slot] = ((uint64_t)n * stripes + 511) & ~UINT64_C(511);
        if (starts[slot] < 1024 || starts[slot] > extent || lengths[slot] > extent - starts[slot] || (!detached && starts[slot] + lengths[slot] > payload)) goto end;
        for (j = 0; j < slot; j++)
            if (lengths[j] && starts[slot] < starts[j] + lengths[j] && starts[j] < starts[slot] + lengths[slot]) goto end;
    }
    for (slot = 0; slot < 8; slot++) {
        size_t a = 208 + 48 * slot;
        uint32_t rounds, stripes, s;
        uint64_t pos = 0;
        unsigned k, difference = 0;
        if (!lengths[slot]) {
            continue;
        }
        rounds = xx_data_get_u32(h, sizeof(h), a + 4, true);
        stripes = xx_data_get_u32(h, sizeof(h), a + 44, true);
        if (!dc_pbkdf(sha256, pw, pwn, h + a + 8, 32, rounds, derived, n, &work, pd) || !dc_crypto_init(&trial, mode, derived, n)) goto end;
        xx_mem_zero(key, sizeof(key));
        for (s = 0; s < stripes; s++) {
            for (k = 0; k < n; k++, pos++) {
                if ((pos & 511) == 0) {
                    if (!dc_read_at(d, base + starts[slot] + pos, sector, 512) || !dc_decrypt(&trial, pos / 512, sector, 512, pd)) goto end;
                }
                key[k] ^= sector[pos & 511];
            }
            if (s + 1 < stripes) {
                size_t p, hl = sha256 ? 32 : 20;
                for (p = 0; p < n; p += hl) {
                    dc_hash hash;
                    uint8_t idx[4] = {0, 0, 0, (uint8_t)(p / hl)};
                    size_t z = n - p;
                    if (z > hl) z = hl;
                    dc_hash_init(&hash, sha256);
                    dc_hash_update(&hash, idx, 4);
                    dc_hash_update(&hash, key + p, z);
                    dc_hash_final(&hash, digest);
                    dc_copy(key + p, digest, z);
                }
            }
        }
        if (!dc_pbkdf(sha256, key, n, h + 132, 32, iter, digest, 20, &work, pd)) goto end;
        for (k = 0; k < 20; k++) difference |= (unsigned)(digest[k] ^ h[112 + k]);
        if (!difference) {
            ok = dc_crypto_init(result, mode, key, n);
            goto end;
        }
        dc_clear(&trial, sizeof(trial));
        dc_clear(derived, sizeof(derived));
        dc_clear(key, sizeof(key));
    }
end:
    dc_clear(h, sizeof(h));
    dc_clear(key, sizeof(key));
    dc_clear(derived, sizeof(derived));
    dc_clear(digest, sizeof(digest));
    dc_clear(sector, sizeof(sector));
    dc_clear(&trial, sizeof(trial));
    if (!ok) dc_clear(result, sizeof(*result));
    return ok;
}
#endif
