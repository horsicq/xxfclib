/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://the.earth.li/~sgtatham/putty/0.85/htmldoc/AppendixC.html */
#include "xxfclib/formats/putty_ppk/xx_putty_ppk.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_security_framing.h"

static bool field(memory_blob *b, uint64_t *at, const char *prefix, uint64_t *value, uint64_t *n)
{
    uint64_t p, z, k = xx_rt_strlen(prefix);
    if (!protocol_line(b, at, &p, &z, false)) return false;
    if (z && b->p[(size_t)(p + z - 1)] == 13) --z;
    if (z < k || !protocol_eq(b, p, k, prefix)) return false;
    *value = p + k;
    *n = z - k;
    return true;
}
static bool lines(memory_blob *b, uint64_t *at, unsigned count, uint8_t **out, uint64_t *n)
{
    uint64_t p, z, used = 0;
    uint8_t *text = (uint8_t *)xx_mem_alloc((size_t)count * 64);
    if (!text) return false;
    bool ok = false;
    for (unsigned i = 0; i < count; ++i) {
        if (!protocol_line(b, at, &p, &z, false)) goto done;
        if (z && b->p[(size_t)(p + z - 1)] == 13) --z;
        if (!z || z > 64 || (i + 1 < count && z != 64)) goto done;
        xx_rt_memcpy(text + (size_t)used, b->p + (size_t)p, (size_t)z);
        used += z;
    }
    memory_blob t = {text, used, b->pd};
    ok = security_b64(&t, 0, used, out, n);
done:
    xx_mem_free(text);
    return ok;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    bool ok = false;
    uint8_t *pub = NULL, *priv = NULL, *macdata = NULL;
    uint64_t at = 0, p, n, pubn = 0, privn = 0, comment, commentn, u;
    unsigned version = 0;
    uint8_t want[32], digest[32], key[20];
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(field(&b, &at, "PuTTY-User-Key-File-", &p, &n) && n == 14 && (b.p[(size_t)p] == '2' || b.p[(size_t)p] == '3') &&
              protocol_eq(&b, p + 1, 13, ": ssh-ed25519"));
    version = b.p[(size_t)p] - '0';
    BLOB_NEED(field(&b, &at, "Encryption: ", &p, &n) && protocol_eq(&b, p, n, "none") && field(&b, &at, "Comment: ", &comment, &commentn) &&
              serialized_utf(&b, comment, commentn));
    BLOB_NEED(field(&b, &at, "Public-Lines: ", &p, &n) && protocol_dec(&b, p, n, &u) && u > 0 && u <= 1024 && lines(&b, &at, (unsigned)u, &pub, &pubn));
    BLOB_NEED(field(&b, &at, "Private-Lines: ", &p, &n) && protocol_dec(&b, p, n, &u) && u > 0 && u <= 1024 && lines(&b, &at, (unsigned)u, &priv, &privn));
    BLOB_NEED(field(&b, &at, "Private-MAC: ", &p, &n) && n == (version == 3 ? 64U : 40U) && security_hex(&b, p, n, want) && at == b.n);
    memory_blob pb = {pub, pubn, pd}, kb = {priv, privn, pd};
    uint64_t q = 0, v, z;
    BLOB_NEED(security_string(&pb, &q, pubn, &v, &z, false) && protocol_eq(&pb, v, z, "ssh-ed25519") && security_string(&pb, &q, pubn, &v, &z, false) && z == 32 &&
              q == pubn);
    q = 0;
    BLOB_NEED(security_string(&kb, &q, privn, &v, &z, false) && z > 0 && z <= 33 && q == privn && !(priv[v] & 128) && (z == 1 || priv[v] || (priv[v + 1] & 128)));
    uint64_t size = 20 + 11 + 4 + commentn + pubn + privn;
    macdata = (uint8_t *)xx_mem_alloc((size_t)size);
    BLOB_NEED(macdata);
    q = 0;
    xx_data_set_u32(macdata + q, 4, 0, (uint32_t)11, true);
    q += 4;
    xx_rt_memcpy(macdata + q, "ssh-ed25519", 11);
    q += 11;
    xx_data_set_u32(macdata + q, 4, 0, (uint32_t)4, true);
    q += 4;
    xx_rt_memcpy(macdata + q, "none", 4);
    q += 4;
    xx_data_set_u32(macdata + q, 4, 0, (uint32_t)commentn, true);
    q += 4;
    xx_rt_memcpy(macdata + q, b.p + (size_t)comment, (size_t)commentn);
    q += commentn;
    xx_data_set_u32(macdata + q, 4, 0, (uint32_t)pubn, true);
    q += 4;
    xx_rt_memcpy(macdata + q, pub, (size_t)pubn);
    q += pubn;
    xx_data_set_u32(macdata + q, 4, 0, (uint32_t)privn, true);
    q += 4;
    xx_rt_memcpy(macdata + q, priv, (size_t)privn);
    q += privn;
    BLOB_NEED(q == size);
    size_t keyn = 0;
    if (version == 2) {
        BLOB_NEED(xx_sha1_memory("putty-private-key-file-mac-key", 30, key));
        keyn = 20;
    }
    BLOB_NEED(security_hmac(&b, version == 3 ? XX_HASH_SHA256 : XX_HASH_SHA1, key, keyn, macdata, (size_t)size, digest) &&
              !xx_rt_memcmp(want, digest, version == 3 ? 32 : 20));
    BLOB_NEED(blob_add(f, s, &b, "comment", comment, commentn) && protocol_mem(f, s, "public-key-blob", &pub, pubn) &&
              protocol_mem(f, s, "private-key-blob", &priv, privn) && blob_add(f, s, &b, "verified-private-mac", p, n));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(macdata);
    xx_mem_free(pub);
    xx_mem_free(priv);
    xx_mem_free(b.p);
    return ok;
}

void xx_putty_ppk_init(xx_putty_ppk *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_PUTTY_PPK, "bin");
    }
}
xx_putty_ppk *xx_putty_ppk_create(xx_io_device *d, int64_t b)
{
    xx_putty_ppk *r = (xx_putty_ppk *)xx_mem_alloc(sizeof(*r));
    if (r) xx_putty_ppk_init(r, d, b);
    return r;
}
void xx_putty_ppk_destroy(xx_putty_ppk *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_putty_ppk_free(xx_putty_ppk *r)
{
    if (r) {
        xx_putty_ppk_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_putty_ppk_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_putty_ppk_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
