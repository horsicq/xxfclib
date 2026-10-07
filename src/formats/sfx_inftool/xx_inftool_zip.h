/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Legacy MRI ends at a ZIP local-record chain followed by exact RSFX EOF.
 * The installer omits the standard central directory. Preserve local bytes
 * and append a directory reconstructed solely from the bounded local fields.
 * ZIP record/encryption reference: PKWARE APPNOTE, sections 4.3 and 6.0.
 * Included after the private bounded helpers in xx_sfx_inftool.c.
 */
typedef struct inf_zip_entry_s {
    uint32_t at, size, packed, crc;
    uint16_t version, flags, method, time, date, name_size;
} inf_zip_entry;

static bool inf_legacy_zip(uint8_t **image, size_t *size, const uint8_t *inf,
                            size_t inf_size, bool *encrypted, xx_pd_struct *pd) {
    inf_zip_entry *entries = NULL;
    size_t at = 0, count = 0, capacity = 0, cd_size = 0, cd_at, i;
    uint64_t raw_total = 0;
    uint8_t *zip = *image, *grown;
    bool found_inf = false, ok = false;
    *encrypted = false;
    if (*size < 34 || xx_rt_memcmp(zip + *size - 4, "RSFX", 4)) return false;
    cd_at = *size - 4;
    while (at < cd_at) {
        inf_zip_entry *entry;
        size_t extra_size, payload, descriptor;
        uint16_t flags, method;
        if (inf_stop(pd) || count == 65535 || !inf_range(cd_at, at, 30) ||
            xx_rt_memcmp(zip + at, "PK\3\4", 4) ||
            (flags = xx_data_get_u16(zip + at + 6, 2, 0, false)) & ~11U || !(flags & 8) ||
            ((method = xx_data_get_u16(zip + at + 8, 2, 0, false)) != 0 && method != 8) ||
            xx_data_get_u16(zip + at + 4, 2, 0, false) < 10 || xx_data_get_u16(zip + at + 4, 2, 0, false) > 20) goto done;
        if (count == capacity) {
            size_t next = capacity ? capacity * 2 : 64;
            inf_zip_entry *more;
            if (next > 65535) next = 65535;
            more = (inf_zip_entry *)xx_mem_realloc(entries, next * sizeof(*entries));
            if (!more) goto done;
            entries = more; capacity = next;
        }
        entry = &entries[count]; xx_mem_zero(entry, sizeof(*entry));
        entry->at = (uint32_t)at;
        entry->version = xx_data_get_u16(zip + at + 4, 2, 0, false);
        entry->flags = flags; entry->method = method;
        entry->time = xx_data_get_u16(zip + at + 10, 2, 0, false); entry->date = xx_data_get_u16(zip + at + 12, 2, 0, false);
        entry->crc = xx_data_get_u32(zip + at + 14, 4, 0, false); entry->packed = xx_data_get_u32(zip + at + 18, 4, 0, false);
        entry->size = xx_data_get_u32(zip + at + 22, 4, 0, false); entry->name_size = xx_data_get_u16(zip + at + 26, 2, 0, false);
        extra_size = xx_data_get_u16(zip + at + 28, 2, 0, false);
        payload = at + 30U + entry->name_size + extra_size;
        if (!inf_range(cd_at, at + 30, (uint64_t)entry->name_size + extra_size) ||
            !entry->name_size || entry->name_size > 4096 ||
            !inf_range(cd_at, payload, entry->packed) ||
            ((flags & 1) && entry->packed < 12) ||
            (method == 0 && entry->packed != (uint64_t)entry->size + ((flags & 1) ? 12 : 0)) ||
            (raw_total += entry->size) > INFTOOL_MAX_CABINET) goto done;
        if (!inf_name(zip + at + 30, entry->name_size)) goto done;
        if (inf_equal_name(zip + at + 30, entry->name_size, inf, inf_size)) found_inf = true;
        /* Keep unsupported AES/ZIP64 extras out of this legacy 32-bit layout. */
        {
            size_t p = at + 30 + entry->name_size, end = payload;
            while (p < end) {
                unsigned id, n;
                if (!inf_range(end, p, 4)) goto done;
                id = xx_data_get_u16(zip + p, 2, 0, false); n = xx_data_get_u16(zip + p + 2, 2, 0, false); p += 4;
                if (id == 1 || id == 0x9901 || !inf_range(end, p, n)) goto done;
                p += n;
            }
        }
        descriptor = payload + entry->packed;
        if (!inf_range(cd_at, descriptor, 16) ||
            xx_rt_memcmp(zip + descriptor, "PK\7\10", 4) ||
            xx_data_get_u32(zip + descriptor + 4, 4, 0, false) != entry->crc ||
            xx_data_get_u32(zip + descriptor + 8, 4, 0, false) != entry->packed ||
            xx_data_get_u32(zip + descriptor + 12, 4, 0, false) != entry->size) goto done;
        if (flags & 1) *encrypted = true;
        cd_size += 46U + entry->name_size;
        at = descriptor + 16;
        ++count;
    }
    if (!count || !found_inf || at != cd_at ||
        cd_size > INFTOOL_MAX_CABINET - cd_at ||
        cd_at + cd_size > INFTOOL_MAX_CABINET - 22U) goto done;
    grown = (uint8_t *)xx_mem_realloc(zip, cd_at + cd_size + 22);
    if (!grown) goto done;
    zip = grown; *image = grown;
    at = cd_at;
    for (i = 0; i < count; ++i) {
        const inf_zip_entry *e = &entries[i];
        uint8_t *h = zip + at;
        if (inf_stop(pd)) goto done;
        xx_mem_zero(h, 46); xx_mem_copy(h, "PK\1\2", 4);
        xx_data_set_u16(h + 4, 2, 0, 20, false); xx_data_set_u16(h + 6, 2, 0, e->version, false);
        xx_data_set_u16(h + 8, 2, 0, e->flags, false); xx_data_set_u16(h + 10, 2, 0, e->method, false);
        xx_data_set_u16(h + 12, 2, 0, e->time, false); xx_data_set_u16(h + 14, 2, 0, e->date, false);
        xx_data_set_u32(h + 16, 4, 0, e->crc, false); xx_data_set_u32(h + 20, 4, 0, e->packed, false); xx_data_set_u32(h + 24, 4, 0, e->size, false);
        xx_data_set_u16(h + 28, 2, 0, e->name_size, false); xx_data_set_u32(h + 38, 4, 0, 32, false);
        xx_data_set_u32(h + 42, 4, 0, e->at, false);
        /* Reallocation may move the image; use the stored local offset. */
        xx_mem_copy(h + 46, zip + e->at + 30, e->name_size);
        at += 46U + e->name_size;
    }
    xx_mem_zero(zip + at, 22); xx_mem_copy(zip + at, "PK\5\6", 4);
    xx_data_set_u16(zip + at + 8, 2, 0, (uint16_t)count, false); xx_data_set_u16(zip + at + 10, 2, 0, (uint16_t)count, false);
    xx_data_set_u32(zip + at + 12, 4, 0, (uint32_t)cd_size, false); xx_data_set_u32(zip + at + 16, 4, 0, (uint32_t)cd_at, false);
    *size = at + 22;
    ok = !inf_stop(pd);
done:
    xx_mem_free(entries);
    return ok;
}
