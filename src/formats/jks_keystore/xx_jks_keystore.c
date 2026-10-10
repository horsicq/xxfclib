/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://github.com/openjdk/jdk/blob/master/src/java.base/share/classes/sun/security/provider/JavaKeyStore.java */
#include "xxfclib/formats/jks_keystore/xx_jks_keystore.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_protocol_framing.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 12, end, alias, n, start;
    uint8_t *hashed = NULL, digest[20];
    bool ok = false;
    unsigned work = 0;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 32 && xx_data_get_u32(b.p, 4, 0, true) == 0xfeedfeedU && xx_data_get_u32(b.p + 4, 4, 0, true) == 2);
    uint32_t count = xx_data_get_u32(b.p + 8, 4, 0, true);
    BLOB_NEED(count && count <= 256);
    end = b.n - 20;
    hashed = (uint8_t *)xx_mem_alloc((size_t)end + 16);
    BLOB_NEED(hashed);
    xx_rt_memcpy(hashed, "Mighty Aphrodite", 16);
    xx_rt_memcpy(hashed + 16, b.p, (size_t)end);
    BLOB_NEED(!binary_stop(pd) && xx_sha1_memory(hashed, (size_t)end + 16, digest) && !binary_stop(pd) && !xx_rt_memcmp(digest, b.p + (size_t)end, 20) &&
              blob_add(f, s, &b, "jks-header", 0, 12));
    uint64_t aliases[256], lengths[256];
    for (uint32_t i = 0; i < count; ++i) {
        start = at;
        BLOB_NEED(protocol_take(&b, &at, end, 4));
        uint32_t tag = xx_data_get_u32(b.p + (size_t)at - 4, 4, 0, true);
        BLOB_NEED((tag == 1 || tag == 2) && protocol_utf16string(&b, &at, end, &alias, &n) && n);
        for (uint32_t j = 0; j < i; ++j) BLOB_NEED(lengths[j] != n || xx_rt_memcmp(b.p + (size_t)aliases[j], b.p + (size_t)alias, (size_t)n));
        aliases[i] = alias;
        lengths[i] = n;
        BLOB_NEED(protocol_take(&b, &at, end, 8) && blob_add(f, s, &b, "entry-alias-time", start, at - start));
        uint32_t certs = 1;
        if (tag == 1) {
            BLOB_NEED(protocol_take(&b, &at, end, 4));
            n = xx_data_get_u32(b.p + (size_t)at - 4, 4, 0, true);
            uint64_t key = at;
            BLOB_NEED(n && protocol_take(&b, &at, end, n));
            uint64_t k = key;
            der_tlv root, alg, data;
            BLOB_NEED(protocol_tree(&b, key, key + n, 0, &work) && der_take(&b, &k, key + n, 48, &root) && k == key + n);
            k = root.value;
            BLOB_NEED(der_take(&b, &k, root.end, 48, &alg) && der_alg(&b, &alg) && der_take(&b, &k, root.end, 4, &data) && data.end > data.value && k == root.end &&
                      blob_add(f, s, &b, "encoded-protected-private-key", key, n) && protocol_take(&b, &at, end, 4));
            certs = xx_data_get_u32(b.p + (size_t)at - 4, 4, 0, true);
            BLOB_NEED(certs >= 1 && certs <= 16);
        }
        for (uint32_t j = 0; j < certs; ++j) {
            uint64_t typelen, type;
            BLOB_NEED(protocol_utf16string(&b, &at, end, &type, &typelen) && protocol_eq(&b, type, typelen, "X.509") && protocol_take(&b, &at, end, 4));
            n = xx_data_get_u32(b.p + (size_t)at - 4, 4, 0, true);
            uint64_t cert = at;
            BLOB_NEED(n && protocol_take(&b, &at, end, n));
            uint64_t k = cert;
            der_tlv root;
            BLOB_NEED(protocol_tree(&b, cert, cert + n, 0, &work) && der_take(&b, &k, cert + n, 48, &root) && k == cert + n && der_cert(&b, &root) &&
                      blob_add(f, s, &b, "certificate", cert, n));
        }
    }
    BLOB_NEED(at == end && blob_add(f, s, &b, "verified-empty-password-sha1", end, 20));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(hashed);
    xx_mem_free(b.p);
    return ok;
}

void xx_jks_keystore_init(xx_jks_keystore *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_JKS_KEYSTORE, "bin");
    }
}
xx_jks_keystore *xx_jks_keystore_create(xx_io_device *d, int64_t b)
{
    xx_jks_keystore *r = (xx_jks_keystore *)xx_mem_alloc(sizeof(*r));
    if (r) xx_jks_keystore_init(r, d, b);
    return r;
}
void xx_jks_keystore_destroy(xx_jks_keystore *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_jks_keystore_free(xx_jks_keystore *r)
{
    if (r) {
        xx_jks_keystore_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_jks_keystore_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_jks_keystore_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
