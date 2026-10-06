/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Active Delivery AD01: the PE actdlvry section holds an ordinary ZIP with
 * coherent ZIP-relative or absolute local/directory offsets. Its stub supplies a ZipCrypto password
 * from .data. Mode-2 packages have one or three leading stored payloads;
 * their separate password is in the decoded ADX configuration. Remaining
 * Deflate support members use the stub password. Every member is CRC-checked.
 */
#include "xxfclib/formats/sfx_ad01/xx_sfx_ad01.h"
#include "../makeself/xx_fourth_wrapper_table.h"
#include "xxfclib/algo/zipcrypto/xx_zipcrypto.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/crc/xx_crc.h"

#define AD_INPUT_MAX (256U * 1024U * 1024U)
#define AD_SECTION_MAX (64U * 1024U)
#define AD_MEMBER_MAX (64U * 1024U * 1024U)
#define AD_TOTAL_MAX (64U * 1024U * 1024U)
#define AD_MAX_COUNT 64U
#define AD_ENCRYPTED_COUNT 6U
#define AD_ADX_MAX (1024U * 1024U)
#define AD_PASSWORD_MAX 64U

typedef struct ad_member {
    char name[96];
    int64_t data;
    uint32_t packed, raw, crc;
    uint16_t flags, method, time;
} ad_member;

/* Require a real PE section, not a coincidental AD01 string in an overlay. */
static bool ad_sections(Abstractformat *f, int64_t *data_at,
                        uint32_t *data_size, int64_t *ad_at,
                        uint32_t *ad_size, xx_pd_struct *pd) {
    uint8_t header[64], section[40];
    int64_t overlay, cabinet, cabinet_end, limit = pm_available(f);
    uint32_t pe;
    uint16_t count, optional, i;
    if (limit < 512 || limit > AD_INPUT_MAX ||
        !wg_pe(f, &overlay, &cabinet, &cabinet_end, pd) ||
        !pm_read(f, 0, header, sizeof(header))) return false;
    pe = pm_le32(header + 60);
    if (!pm_read(f, pe, header, 24)) return false;
    count = pm_le16(header + 6);
    optional = pm_le16(header + 20);
    *data_at = *ad_at = -1;
    for (i = 0U; i < count; ++i) {
        uint32_t size, raw;
        if (wg_stop(pd) ||
            !pm_read(f, (int64_t)pe + 24 + optional + (int64_t)i * 40,
                     section, sizeof(section))) return false;
        size = pm_le32(section + 16);
        raw = pm_le32(section + 20);
        if (!xx_rt_memcmp(section, ".data\0\0\0", 8)) {
            if (*data_at >= 0 || !size || size > AD_SECTION_MAX) return false;
            *data_at = raw; *data_size = size;
        } else if (!xx_rt_memcmp(section, "actdlvry", 8)) {
            if (*ad_at >= 0 || size < 34U) return false;
            *ad_at = raw; *ad_size = size;
        }
    }
    return *data_at >= 0 && *ad_at >= 0;
}

static bool ad_safe_name(const char *name, size_t n) {
    size_t i;
    if (!n || n >= 96U || name[0] == '.') return false;
    for (i = 0U; i < n; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32U || c > 126U || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*') return false;
    }
    return name[n - 1U] != '.' && name[n - 1U] != ' ';
}

static bool ad_directory(Abstractformat *f, int64_t start, int64_t end,
                         ad_member members[AD_MAX_COUNT], unsigned *count_out,
                         unsigned *stored_out, bool *encrypted_out,
                         xx_pd_struct *pd) {
    uint8_t eocd[22], central[46], local[30];
    int64_t directory, bias, at, previous_end = start;
    uint32_t bytes;
    uint64_t raw_total = 0U;
    unsigned count, i, j, stored = 0U;
    bool encrypted = false, control = false;
    if (end - start < 22 || !pm_read(f, end - 22, eocd, sizeof(eocd)) ||
        xx_rt_memcmp(eocd, "PK\5\6", 4) ||
        pm_le16(eocd + 4) || pm_le16(eocd + 6) || pm_le16(eocd + 20))
        return false;
    count = pm_le16(eocd + 10);
    if (!count || count > AD_MAX_COUNT || pm_le16(eocd + 8) != count)
        return false;
    bytes = pm_le32(eocd + 12);
    if (bytes > (uint64_t)(end - start - 22)) return false;
    directory = end - 22 - bytes;
    bias = directory - pm_le32(eocd + 16);
    /* Some stubs retain ordinary ZIP-relative offsets, while others rewrite
     * the whole graph to absolute file offsets. Require a single origin for
     * the directory and every local member; never repair individual offsets. */
    if ((bias != 0 && bias != start) ||
        !wg_zip(f, start, end, pd)) return false;
    at = directory;
    for (i = 0U; i < count; ++i) {
        uint16_t names, extra, comment, local_names, local_extra;
        int64_t local_at, record_bytes, data;
        ad_member *m = &members[i];
        if (wg_stop(pd) || at > end - 22 - 46 ||
            !pm_read(f, at, central, sizeof(central)) ||
            xx_rt_memcmp(central, "PK\1\2", 4)) return false;
        names = pm_le16(central + 28);
        extra = pm_le16(central + 30);
        comment = pm_le16(central + 32);
        record_bytes = 46 + (int64_t)names + extra + comment;
        if (!names || names >= sizeof(m->name) ||
            record_bytes > end - 22 - at ||
            !pm_read(f, at + 46, m->name, names) ||
            !ad_safe_name(m->name, names)) return false;
        m->name[names] = '\0';
        for (j = 0U; j < i; ++j)
            if (!xx_rt_strcmp(m->name, members[j].name)) return false;
        m->flags = pm_le16(central + 8);
        m->method = pm_le16(central + 10);
        m->time = pm_le16(central + 12);
        m->crc = pm_le32(central + 16);
        m->packed = pm_le32(central + 20);
        m->raw = pm_le32(central + 24);
        local_at = (int64_t)pm_le32(central + 42) + bias;
        if (!m->raw || m->raw > AD_MEMBER_MAX ||
            m->raw > AD_TOTAL_MAX - raw_total ||
            !m->packed || m->packed > AD_MEMBER_MAX ||
            local_at < previous_end || local_at > directory - 30 ||
            !pm_read(f, local_at, local, sizeof(local)) ||
            xx_rt_memcmp(local, "PK\3\4", 4)) return false;
        local_names = pm_le16(local + 26);
        local_extra = pm_le16(local + 28);
        data = (int64_t)local_at + 30 + local_names + local_extra;
        if (local_names != names || data > directory ||
            m->packed > (uint64_t)(directory - data)) return false;
        m->data = data;
        raw_total += m->raw;
        previous_end = data + m->packed;
        if (i == 0U) encrypted = (m->flags & 1U) != 0U;
        if (!xx_rt_strcmp(m->name, "_Active Delivery_")) control = true;
        if (encrypted) {
            if (count != AD_ENCRYPTED_COUNT) return false;
            if (i == 0U) {
                if (names < 5U ||
                    xx_rt_strcmp(m->name + names - 4U, ".cab") ||
                    m->flags != 1U || m->method != 0U) return false;
            }
            if (m->flags == 1U && m->method == 0U) {
                /* The delivery DLL's mode-2 password applies to the entire
                 * leading stored payload group, not just its first CAB. */
                if (i != stored || stored >= 3U ||
                    m->packed != (uint64_t)m->raw + XX_ZIPCRYPTO_HEADER_SIZE)
                    return false;
                ++stored;
            } else if (m->flags != 3U || m->method != 8U ||
                       m->packed <= XX_ZIPCRYPTO_HEADER_SIZE) return false;
        } else if ((m->flags != 0U || m->method != 0U ||
                    m->packed != m->raw) &&
                   (m->flags != 2U || m->method != 8U)) return false;
        at += record_bytes;
    }
    if (at != end - 22 || !control ||
        (encrypted && stored != 1U && stored != 3U)) return false;
    *count_out = count;
    *stored_out = stored;
    *encrypted_out = encrypted;
    return true;
}

static bool ad_decode(Abstractformat *f, const ad_member *m,
                      bool encrypted, const uint8_t *password,
                      size_t password_size,
                      uint8_t **output, xx_pd_struct *pd) {
    uint8_t *envelope = NULL, *compressed = NULL, *plain = NULL;
    const uint8_t *payload;
    xx_io_device *sink = NULL;
    size_t size, consumed = 0U;
    bool okay = false;
    *output = NULL;
    if (wg_stop(pd)) return false;
    envelope = (uint8_t *)xx_mem_alloc(m->packed);
    plain = (uint8_t *)xx_mem_alloc(m->raw);
    if (!envelope || !plain ||
        !pm_read(f, m->data, envelope, m->packed)) goto done;
    size = m->packed;
    payload = envelope;
    if (encrypted) {
        compressed = (uint8_t *)xx_mem_alloc(m->packed -
                                              XX_ZIPCRYPTO_HEADER_SIZE);
        if (!compressed ||
            !xx_zipcrypto_decrypt_envelope_progress(
                envelope, m->packed, password, password_size, m->crc, m->time,
                false, compressed, m->packed - XX_ZIPCRYPTO_HEADER_SIZE,
                &size, pd) ||
            size != m->packed - XX_ZIPCRYPTO_HEADER_SIZE) goto done;
        payload = compressed;
    }
    if (m->method == 0U) {
        if (size != m->raw) goto done;
        xx_mem_copy(plain, payload, size);
    } else if (m->method == 8U) {
        sink = xx_io_mem_open(plain, m->raw);
        if (!sink ||
            !xx_deflate_unpack_memory_to_device_ex(payload, size, sink,
                                                    &consumed, false, pd) ||
            consumed != size || xx_io_tell(sink) != m->raw) goto done;
    } else goto done;
    if (xx_crc32_calc(0U, plain, m->raw) != m->crc || wg_stop(pd)) goto done;
    *output = plain;
    plain = NULL;
    okay = true;
done:
    if (sink) xx_io_close(sink);
    if (envelope) { xx_mem_zero(envelope, m->packed); xx_mem_free(envelope); }
    if (compressed) { xx_mem_zero(compressed, m->packed - XX_ZIPCRYPTO_HEADER_SIZE); xx_mem_free(compressed); }
    xx_mem_free(plain);
    return okay;
}

/* The stub stores its own password as a NUL-terminated ASCII string in
 * .data. Discover it there and prove it by full member CRC, rather than
 * baking the observed password into the library. */
static bool ad_password(Abstractformat *f, int64_t data_at,
                        uint32_t data_size, const ad_member *probe,
                        uint8_t out[65], size_t *out_size,
                        xx_pd_struct *pd) {
    uint8_t *section = NULL, *plain = NULL, encrypted_header[12];
    size_t i, candidates = 0U, ignored = 0U;
    bool found = false;
    if (!pm_read(f, probe->data, encrypted_header,
                 sizeof(encrypted_header))) return false;
    section = (uint8_t *)xx_mem_alloc(data_size);
    if (!section || !pm_read(f, data_at, section, data_size)) goto done;
    for (i = 0U; i < data_size;) {
        size_t end = i;
        if (wg_stop(pd)) goto done;
        if (i && section[i - 1U] != 0U) { ++i; continue; }
        while (end < data_size && end - i <= 64U &&
               section[end] >= 32U && section[end] <= 126U) ++end;
        if (end - i >= 4U && end - i <= 64U && end < data_size &&
            section[end] == 0U) {
            if (++candidates > 4096U) goto done;
            if (xx_zipcrypto_decrypt_envelope_progress(
                    encrypted_header, sizeof(encrypted_header), section + i,
                    end - i, probe->crc, probe->time, false, NULL, 0U,
                    &ignored, pd) &&
                ad_decode(f, probe, true, section + i, end - i, &plain, pd)) {
                xx_mem_copy(out, section + i, end - i);
                *out_size = end - i;
                found = true;
                goto done;
            }
        }
        i = end > i ? end + 1U : i + 1U;
    }
done:
    xx_mem_free(plain);
    if (section) { xx_mem_zero(section, data_size); xx_mem_free(section); }
    return found;
}

/* ADX obfuscates configuration keys and values with a seed-derived 16-byte
 * mask. The installed ADX DLL builds four zero-padded decimal numbers and
 * combines their first sixteen characters with this fixed mask source. */
static bool ad_adx_mask(const uint8_t *line, size_t size, uint8_t mask[16]) {
    static const char source[] = "[cvyxtrWZiho}~l]";
    char numbers[64];
    uint32_t seed = 0U;
    size_t i;
    int written;
    if (size < 4U || size > 9U ||
        xx_rt_memcmp(line, "<=>", 3)) return false;
    for (i = 3U; i < size; ++i) {
        if (line[i] < '0' || line[i] > '9') return false;
        seed = seed * 10U + (uint32_t)(line[i] - '0');
    }
    if (!seed || seed > 1000000U) return false;
    written = xx_rt_snprintf(numbers, sizeof(numbers),
                             "%04u%04u%04u%04u", 42U * seed, seed,
                             24U * seed, 33U * seed);
    if (written < 16 || (size_t)written >= sizeof(numbers)) return false;
    for (i = 0U; i < 16U; ++i)
        mask[i] = (uint8_t)((source[i] ^ numbers[i]) & 0x0f);
    return true;
}

static bool ad_adx_line(const uint8_t *adx, size_t size, size_t *cursor,
                        const uint8_t **line, size_t *line_size) {
    size_t start = *cursor, end = start;
    if (start >= size) return false;
    while (end < size && adx[end] != '\n') ++end;
    *cursor = end < size ? end + 1U : end;
    if (end > start && adx[end - 1U] == '\r') --end;
    *line = adx + start;
    *line_size = end - start;
    return true;
}

static uint8_t ad_adx_decode_byte(uint8_t encoded, uint8_t mask) {
    if (encoded == 0x10U) return '\r';
    if (encoded == 0x11U) return '\n';
    if (encoded == 0x12U) return '=';
    if (encoded == 0x13U) encoded = '=';
    return (uint8_t)(encoded ^ mask);
}

/* The ADX DLL uses numeric item 401 to select a stored CAB password (mode 2),
 * and item 402 to supply it. Match exact encoded keys rather than decoding
 * unrelated configuration lines. */
static bool ad_adx_cab_password(const uint8_t *adx, size_t size,
                                uint8_t out[65], size_t *out_size,
                                xx_pd_struct *pd) {
    static const char *keys[2] = {"ADK_00000401", "ADK_00000402"};
    uint8_t mask[16], encoded_keys[2][12];
    const uint8_t *line;
    size_t cursor = 0U, length, i, k;
    bool seen_mode = false, seen_password = false;
    if (!adx || size < 40U || size > AD_ADX_MAX ||
        !ad_adx_line(adx, size, &cursor, &line, &length) ||
        length != 5U || xx_rt_memcmp(line, "[ADX]", 5) ||
        !ad_adx_line(adx, size, &cursor, &line, &length) ||
        length != 18U ||
        xx_rt_memcmp(line, "ADXVersion=1.00.00", 18) ||
        !ad_adx_line(adx, size, &cursor, &line, &length) ||
        !ad_adx_mask(line, length, mask)) return false;
    for (k = 0U; k < 2U; ++k) {
        for (i = 0U; i < 12U; ++i) {
            uint8_t encoded = (uint8_t)(keys[k][i] ^ mask[i]);
            encoded_keys[k][i] = encoded == '=' ? 0x13U : encoded;
        }
    }
    while (ad_adx_line(adx, size, &cursor, &line, &length)) {
        if (wg_stop(pd)) return false;
        if (length < 13U || line[12] != '=') continue;
        if (!xx_rt_memcmp(line, encoded_keys[0], 12)) {
            if (seen_mode || length != 14U ||
                ad_adx_decode_byte(line[13], mask[0]) != '2') return false;
            seen_mode = true;
        } else if (!xx_rt_memcmp(line, encoded_keys[1], 12)) {
            if (seen_password || length <= 13U ||
                length > 13U + AD_PASSWORD_MAX) return false;
            *out_size = length - 13U;
            for (i = 0U; i < *out_size; ++i) {
                out[i] = ad_adx_decode_byte(line[13U + i], mask[i & 15U]);
                if (!out[i]) return false;
            }
            seen_password = true;
        }
    }
    return seen_mode && seen_password;
}

static bool pm_parse(Abstractformat *f, pm_stream *stream, xx_pd_struct *pd) {
    ad_member members[AD_MAX_COUNT];
    uint8_t *decoded[AD_MAX_COUNT];
    uint8_t header[12], password[65], cab_password[65];
    int64_t data_at, ad_at, zip_at, zip_end, limit = pm_available(f);
    uint32_t data_size = 0U, ad_size, zip_size;
    size_t password_size = 0U, cab_password_size = 0U;
    unsigned i, count = 0U, stored = 0U, adx_index = AD_MAX_COUNT;
    unsigned control_index = AD_MAX_COUNT;
    bool okay = false, encrypted = false;
    xx_mem_zero(members, sizeof(members));
    xx_mem_zero(decoded, sizeof(decoded));
    xx_mem_zero(password, sizeof(password));
    xx_mem_zero(cab_password, sizeof(cab_password));
    if (!ad_sections(f, &data_at, &data_size, &ad_at, &ad_size, pd) ||
        !pm_read(f, ad_at, header, sizeof(header)) ||
        xx_rt_memcmp(header, "AD01", 4)) goto done;
    zip_size = pm_le32(header + 8);
    zip_at = ad_at + 12;
    zip_end = zip_at + zip_size;
    if (zip_size < 22U || zip_size > ad_size - 12U || zip_end > limit ||
        !ad_directory(f, zip_at, zip_end, members, &count, &stored,
                      &encrypted, pd))
        goto done;
    if (encrypted) {
        for (i = stored; i < count; ++i)
            if (!xx_rt_strcmp(members[i].name, "_Active Delivery_"))
                control_index = i;
        /* Prove the stub password on its small control member. A stored
         * application payload can require a different ADX password. */
        if (control_index == AD_MAX_COUNT ||
            members[control_index].raw > AD_ADX_MAX ||
            !ad_password(f, data_at, data_size, &members[control_index],
                         password, &password_size, pd)) goto done;
    }
    for (i = encrypted ? stored : 0U; i < count; ++i) {
        size_t name_size = xx_rt_strlen(members[i].name);
        if (encrypted && name_size >= 4U &&
            !xx_rt_strcmp(members[i].name + name_size - 4U, ".adx")) {
            if (adx_index != AD_MAX_COUNT || members[i].raw > AD_ADX_MAX)
                goto done;
            adx_index = i;
        }
        if (wg_stop(pd) || !ad_decode(f, &members[i], encrypted, password,
                                     password_size, &decoded[i], pd)) goto done;
    }
    if (encrypted) {
        if (adx_index == AD_MAX_COUNT ||
            !ad_adx_cab_password(decoded[adx_index], members[adx_index].raw,
                                 cab_password, &cab_password_size, pd)) goto done;
        for (i = 0U; i < stored; ++i)
            if (!ad_decode(f, &members[i], true, cab_password,
                           cab_password_size, &decoded[i], pd)) goto done;
        /* The .cab payload is opaque to this wrapper (for example, IC60
         * InstallShield data). Its name does not require Microsoft CAB
         * syntax; the complete ZIP size and CRC were verified above. */
    }
    for (i = 0U; i < count; ++i) {
        pm_member *added;
        if (wg_stop(pd)) goto done;
        if (!pm_add(f, stream, members[i].name, members[i].data,
                    members[i].packed)) goto done;
        added = &stream->items[stream->count - 1U];
        xx_rt_snprintf(added->name, sizeof(added->name), "%s",
                       members[i].name);
        added->source_encrypted = encrypted;
        added->compression_method = members[i].method;
        if (encrypted) {
            const uint8_t *recovered = i < stored ? cab_password : password;
            size_t recovered_size = i < stored ? cab_password_size : password_size;
            /* Publication follows validation of every member. The stream
             * owns this copy and clears it independently of decoded data. */
            added->password = (char *)xx_mem_alloc(recovered_size + 1U);
            if (!added->password) goto done;
            xx_mem_copy(added->password, recovered, recovered_size);
            added->password[recovered_size] = '\0';
        }
        added->memory = decoded[i];
        added->size = members[i].raw;
        decoded[i] = NULL;
    }
    stream->size = limit;
    okay = true;
done:
    for (i = 0U; i < count; ++i) xx_mem_free(decoded[i]);
    xx_mem_zero(password, sizeof(password));
    xx_mem_zero(cab_password, sizeof(cab_password));
    return okay;
}

void xx_sfx_ad01_init(xx_sfx_ad01 *r, xx_io_device *d, int64_t b) {
    if (r) { xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SFX_AD01, "exe"); }
}
xx_sfx_ad01 *xx_sfx_ad01_create(xx_io_device *d, int64_t b) {
    xx_sfx_ad01 *r = (xx_sfx_ad01 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfx_ad01_init(r, d, b);
    return r;
}
void xx_sfx_ad01_destroy(xx_sfx_ad01 *r) {
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sfx_ad01_free(xx_sfx_ad01 *r) {
    if (r) { xx_sfx_ad01_destroy(r); xx_mem_free(r); }
}
bool xx_sfx_ad01_check_is_valid(Abstractformat *f, xx_pd_struct *pd) {
    return pm_valid(f, pd);
}
bool xx_sfx_ad01_handle_base_info(Abstractformat *f, xx_pd_struct *pd) {
    return pm_handle(f, pd);
}
