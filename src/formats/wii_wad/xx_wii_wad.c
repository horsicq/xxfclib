/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded Wii WAD reader adapted from XArchive XWiiWAD. */
#include "xxfclib/formats/wii_wad/xx_wii_wad.h"
#include "xxfclib/algo/aes/xx_aes.h"
#include "xxfclib/algo/sha/xx_sha.h"
#include "../xx_payload_members.h"

typedef struct wad_content {
    uint8_t encrypted_key[16], title_id[8], sha1[20];
    uint16_t index;
} wad_content;

static uint16_t wad_be16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static uint32_t wad_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t wad_be64(const uint8_t *p) { return ((uint64_t)wad_be32(p) << 32) | wad_be32(p + 4); }
static uint64_t wad_align(uint64_t n, uint64_t alignment) { return (n + alignment - 1U) & ~(alignment - 1U); }
static void wad_wipe(void *p, size_t n) {
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--) *v++ = 0;
}
static void wad_free_content(void *p) { if (p) { wad_wipe(p, sizeof(wad_content)); xx_mem_free(p); } }

static bool wad_read(Abstractformat *f, int64_t at, void *buffer, size_t amount, xx_pd_struct *pd) {
    size_t done = 0;
    int64_t available = pm_available(f);
    if (at < 0 || at > available || amount > (uint64_t)(available - at) ||
        (pd && xx_pd_is_stopped(pd)) ||
        xx_io_seek64(f->device, f->base_address + at, SEEK_SET) != 0) return false;
    while (done < amount) {
        ssize_t got;
        if (pd && xx_pd_is_stopped(pd)) return false;
        got = xx_io_read(f->device, (uint8_t *)buffer + done, amount - done);
        if (got <= 0 || (size_t)got > amount - done) return false;
        done += (size_t)got;
    }
    return !(pd && xx_pd_is_stopped(pd));
}

static int wad_hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool wad_key(Abstractformat *f, uint8_t key[16]) {
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_PASSWORD);
    const char *text = NULL;
    char *owned = NULL;
    size_t i, size = 0;
    bool result = false;
    if (!v) return false;
    if (v->type == XX_VAR_TYPE_BYTES || v->type == XX_VAR_TYPE_BYTES_VIEW) {
        const void *bytes = xx_var_get_bytes(v, &size);
        if (bytes && size == 16U) { xx_mem_copy(key, bytes, 16U); return true; }
        return false;
    }
    if (v->type == XX_VAR_TYPE_STRING || v->type == XX_VAR_TYPE_STRING_VIEW) text = xx_var_get_str(v);
    else if (v->type == XX_VAR_TYPE_WSTRING || v->type == XX_VAR_TYPE_WSTRING_VIEW)
        text = owned = xx_str_unicode_to_utf8(xx_var_get_wstr(v));
    if (text && xx_rt_strlen(text) == 32U) {
        result = true;
        for (i = 0; i < 16U; ++i) {
            int high = wad_hex((unsigned char)text[i * 2U]);
            int low = wad_hex((unsigned char)text[i * 2U + 1U]);
            if (high < 0 || low < 0) { result = false; break; }
            key[i] = (uint8_t)((high << 4) | low);
        }
    }
    if (owned) { wad_wipe(owned, xx_rt_strlen(owned)); xx_str_free(owned); }
    return result;
}

static bool wad_decode(Abstractformat *f, pm_member *m, xx_io_device *output, xx_pd_struct *pd) {
    wad_content *content = (wad_content *)m->context;
    uint8_t common[16] = {0}, key[16] = {0}, iv[16] = {0}, digest[20] = {0};
    uint8_t *cipher = NULL, *plain;
    xx_sha1_context hash;
    const xx_var *limit;
    uint64_t done = 0, written = 0;
    size_t capacity = 65536U;
    bool result = false;
    if (!content || (pd && xx_pd_is_stopped(pd)) || !wad_key(f, common)) goto cleanup;
    xx_mem_copy(iv, content->title_id, 8U);
    if (!xx_aes_cbc_decrypt(content->encrypted_key, 16U, common, 16U, iv, key) ||
        !xx_sha1_init(&hash)) goto cleanup;
    xx_mem_zero(iv, sizeof(iv)); iv[0] = (uint8_t)(content->index >> 8); iv[1] = (uint8_t)content->index;
    limit = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (limit && xx_var_get_u64(limit) < capacity * 2U) capacity = (size_t)(xx_var_get_u64(limit) / 2U);
    capacity &= ~(size_t)15U;
    if (!capacity || !(cipher = (uint8_t *)xx_mem_alloc(capacity * 2U))) goto cleanup;
    plain = cipher + capacity;
    while (done < (uint64_t)m->packed_size) {
        uint64_t left = (uint64_t)m->packed_size - done;
        size_t n = left > capacity ? capacity : (size_t)left;
        size_t meaningful = (uint64_t)m->size - written > n ? n : (size_t)((uint64_t)m->size - written);
        size_t sent = 0;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !wad_read(f, m->offset - f->base_address + (int64_t)done, cipher, n, pd) ||
            !xx_aes_cbc_decrypt(cipher, n, key, 16U, iv, plain)) goto cleanup;
        xx_mem_copy(iv, cipher + n - 16U, 16U);
        xx_sha1_update(&hash, plain, meaningful);
        while (output && sent < meaningful) {
            ssize_t amount;
            if (pd && xx_pd_is_stopped(pd)) goto cleanup;
            amount = xx_io_write(output, plain + sent, meaningful - sent);
            if (amount <= 0 || (size_t)amount > meaningful - sent) goto cleanup;
            sent += (size_t)amount;
        }
        written += meaningful; done += n;
    }
    if (written != (uint64_t)m->size || !xx_sha1_final(&hash, digest, sizeof(digest)) ||
        xx_mem_compare(digest, content->sha1, sizeof(digest)) != 0 || (pd && xx_pd_is_stopped(pd))) goto cleanup;
    result = true;
cleanup:
    if (cipher) { wad_wipe(cipher, capacity * 2U); xx_mem_free(cipher); }
    wad_wipe(common, sizeof(common)); wad_wipe(key, sizeof(key)); wad_wipe(iv, sizeof(iv));
    wad_wipe(digest, sizeof(digest)); wad_wipe(&hash, sizeof(hash));
    return result;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    uint8_t header[32], ticket[0x2a4], tmd[0x1e4], record[0x24];
    uint64_t offsets[6], lengths[6], cursor = 64U, available, end, required = 0;
    uint16_t count;
    unsigned i;
    char label[48];
    int64_t size = pm_available(f);
    if (size < 0x4e4 || !wad_read(f, 0, header, sizeof(header), pd) ||
        wad_be32(header) != 32U || header[6] || header[7] ||
        !((header[4] == 'I' && header[5] == 's') || (header[4] == 'i' && header[5] == 'b'))) return false;
    available = (uint64_t)size;
    for (i = 0; i < 6U; ++i) {
        lengths[i] = wad_be32(header + 8U + i * 4U);
        if (i != 4U && lengths[i] > 1048576U) return false;
        offsets[i] = cursor;
        if (lengths[i]) {
            if (cursor > available || lengths[i] > available - cursor) return false;
            cursor = wad_align(cursor + lengths[i], 64U);
        }
    }
    end = cursor < available ? cursor : available;
    if (lengths[2] < sizeof(ticket) || lengths[3] < sizeof(tmd) ||
        !wad_read(f, (int64_t)offsets[2], ticket, sizeof(ticket), pd) || wad_be32(ticket) != 0x10001U ||
        !wad_read(f, (int64_t)offsets[3], tmd, sizeof(tmd), pd) || wad_be32(tmd) != 0x10001U) return false;
    count = wad_be16(tmd + 0x1de);
    if (count > 1024U || sizeof(tmd) + (uint64_t)count * sizeof(record) > lengths[3]) return false;
    for (i = 0; i < 4U; ++i) {
        static const char *extensions[] = {"cert", "crl", "tik", "tmd"};
        if (!lengths[i]) continue;
        (void)xx_rt_snprintf(label, sizeof(label), "%08x%08x.%s", wad_be32(ticket + 0x1dc),
                             wad_be32(ticket + 0x1e0), extensions[i]);
        if (!pm_add(f, s, label, (int64_t)offsets[i], (int64_t)lengths[i])) return false;
    }
    for (i = 0; i < count; ++i) {
        uint64_t plain_size, cipher_size, at = offsets[4] + required;
        wad_content *content;
        pm_member *member;
        if (!wad_read(f, (int64_t)(offsets[3] + sizeof(tmd) + i * sizeof(record)), record, sizeof(record), pd)) return false;
        plain_size = wad_be64(record + 8);
        if (!plain_size || plain_size > 536870912U) return false;
        cipher_size = wad_align(plain_size, 16U);
        if (at > available || cipher_size > available - at ||
            required + cipher_size > wad_align(lengths[4], 16U)) return false;
        (void)xx_rt_snprintf(label, sizeof(label), "%08x.app", (unsigned)wad_be16(record + 4));
        if (!pm_add(f, s, label, (int64_t)at, (int64_t)cipher_size)) return false;
        member = &s->items[s->count - 1U];
        member->size = (int64_t)plain_size;
        member->source_encrypted = true;
        member->compression_method = XX_WII_WAD_METHOD_AES128_CBC;
        member->read_all = wad_decode;
        content = (wad_content *)xx_mem_alloc(sizeof(*content));
        if (!content) return false;
        xx_mem_copy(content->encrypted_key, ticket + 0x1bf, 16U);
        xx_mem_copy(content->title_id, ticket + 0x1dc, 8U);
        xx_mem_copy(content->sha1, record + 16, 20U);
        content->index = wad_be16(record + 4);
        member->context = content; member->free_context = wad_free_content;
        required += i + 1U < count ? wad_align(plain_size, 64U) : cipher_size;
    }
    if (lengths[5] && !pm_add(f, s, "footer.bin", (int64_t)offsets[5], (int64_t)lengths[5])) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    s->size = (int64_t)end;
    return true;
}

void xx_wii_wad_init(xx_wii_wad *r, xx_io_device *d, int64_t base) {
    if (!r) return;
    xx_mem_zero(r, sizeof(*r)); pm_init(&r->format, d, base, XX_FILE_TYPE_WII_WAD, "wad");
    xx_format_set_mime_type(&r->format, "application/octet-stream");
}
xx_wii_wad *xx_wii_wad_create(xx_io_device *d, int64_t base) {
    xx_wii_wad *r = (xx_wii_wad *)xx_mem_alloc(sizeof(*r));
    if (r) xx_wii_wad_init(r, d, base);
    return r;
}
void xx_wii_wad_destroy(xx_wii_wad *r) {
    if (r) { xx_format_cleanup_extra_parameters(&r->format); xx_format_invalidate_memory_map(&r->format); }
}
void xx_wii_wad_free(xx_wii_wad *r) { if (r) { xx_wii_wad_destroy(r); xx_mem_free(r); } }

xx_file_type_t xx_wii_wad_detect(xx_io_device *d, int64_t base) {
    uint8_t header[32];
    int64_t size, saved;
    bool valid;
    xx_wii_wad r;
    if (!d || base < 0 || (size = xx_io_size(d)) < base || size - base < 0x4e4 ||
        (saved = xx_io_tell(d)) < 0 || !xx_io_read_at(d, base, header, sizeof(header)) ||
        wad_be32(header) != 32U || header[6] || header[7] ||
        !((header[4] == 'I' && header[5] == 's') || (header[4] == 'i' && header[5] == 'b')) ||
        wad_be32(header + 16) < 0x2a4U || wad_be32(header + 20) < 0x1e4U) return XX_FILE_TYPE_UNKNOWN;
    xx_wii_wad_init(&r, d, base);
    valid = pm_valid(&r.format, NULL);
    xx_wii_wad_destroy(&r);
    if (xx_io_seek64(d, saved, SEEK_SET) != 0) valid = false;
    return valid ? XX_FILE_TYPE_WII_WAD : XX_FILE_TYPE_UNKNOWN;
}
