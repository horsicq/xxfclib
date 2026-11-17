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


#include <xxfclib/formats/installer/crx/xx_crx_yx.h>

#include "xxfclib/json/xx_json.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/base64/xx_base64.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/sha/xx_sha.h"

#include <limits.h>
#include "xxfclib/algo/pki/xx_pki_verify.h"
#include "xxfclib/algo/x509/xx_x509.h"
#include "xxfclib/algo/protobuf/xx_protobuf_wire.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t central_offset;
    size_t central_size;
    size_t base_offset;
    size_t entries;
} crx_zip;

typedef struct {
    const uint8_t *key;
    size_t key_size;
    const uint8_t *signature;
    size_t signature_size;
} crx_proof;

typedef struct {
    crx_proof *items;
    size_t count;
} crx_proofs;

typedef struct {
    char *version;
    size_t version_length;
    char *name;
    size_t name_length;
    char *description;
    size_t description_length;
    char *minimum_chrome_version;
    size_t minimum_chrome_version_length;
    char *homepage_url;
    size_t homepage_url_length;
    char *default_locale;
    size_t default_locale_length;
    xx_crx_yx_string_list permissions;
    xx_crx_yx_string_list host_permissions;
    xx_crx_yx_string_list optional_permissions;
    xx_crx_yx_string_list optional_host_permissions;
} crx_manifest;

typedef struct {
    char *key;
    size_t key_length;
    char *message;
    size_t message_length;
} crx_locale_entry;

typedef struct {
    crx_locale_entry *items;
    size_t count;
} crx_locale;

static uint16_t crx_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t crx_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static char *crx_dup_length(const char *value, size_t n)
{
    if (n == SIZE_MAX) return NULL;
    char *out = (char *)malloc(n + 1);
    if (out) {
        if (n) memcpy(out, value, n);
        out[n] = 0;
    }
    return out;
}

static char *crx_dup(const char *value)
{
    return crx_dup_length(value, strlen(value));
}

static void crx_list_free(xx_crx_yx_string_list *list)
{
    for (size_t i = 0; i < list->count; ++i) free(list->items[i]);
    free(list->items);
    free(list->lengths);
    list->items = NULL;
    list->lengths = NULL;
    list->count = 0;
}

static bool crx_list_push(xx_crx_yx_string_list *list, char *value,
                          size_t length)
{
    if (list->count >= SIZE_MAX / sizeof(char *)) return false;
    char **items = (char **)realloc(list->items,
                                   (list->count + 1) * sizeof(char *));
    if (!items) return false;
    list->items = items;
    size_t *lengths = (size_t *)realloc(list->lengths,
                                       (list->count + 1) * sizeof(size_t));
    if (!lengths) return false;
    list->lengths = lengths;
    list->lengths[list->count] = length;
    list->items[list->count++] = value;
    return true;
}

static void crx_manifest_free(crx_manifest *manifest)
{
    free(manifest->version);
    free(manifest->name);
    free(manifest->description);
    free(manifest->minimum_chrome_version);
    free(manifest->homepage_url);
    free(manifest->default_locale);
    crx_list_free(&manifest->permissions);
    crx_list_free(&manifest->host_permissions);
    crx_list_free(&manifest->optional_permissions);
    crx_list_free(&manifest->optional_host_permissions);
    memset(manifest, 0, sizeof(*manifest));
}

static void crx_locale_free(crx_locale *locale)
{
    for (size_t i = 0; i < locale->count; ++i) {
        free(locale->items[i].key);
        free(locale->items[i].message);
    }
    free(locale->items);
    memset(locale, 0, sizeof(*locale));
}

void xx_crx_yx_result_free(xx_crx_yx_result *result)
{
    if (!result) return;
    free(result->id);
    free(result->version);
    free(result->name);
    free(result->description);
    free(result->raw_name);
    free(result->raw_description);
    free(result->minimum_chrome_version);
    free(result->homepage_url);
    crx_list_free(&result->permissions);
    crx_list_free(&result->host_permissions);
    crx_list_free(&result->optional_permissions);
    crx_list_free(&result->optional_host_permissions);
    for (size_t i = 0; i < result->signature_count; ++i)
        free(result->signatures[i].key);
    free(result->signatures);
    memset(result, 0, sizeof(*result));
}

/* The ZIP crate parses central-directory extras before exposing any entry.
 * Keep its metadata here too: ZIP64 and UnicodePath can alter a later lookup. */
typedef struct {
    const uint8_t *name;
    size_t name_size;
    uint64_t compressed;
    uint64_t uncompressed;
    uint64_t relative_offset;
} crx_zip_entry;

static bool crx_utf8_valid(const uint8_t *data, size_t size);

static uint64_t crx_u64(const uint8_t *p)
{
    return (uint64_t)crx_u32(p) | ((uint64_t)crx_u32(p + 4) << 32);
}

static bool crx_zip_entry_parse(const uint8_t *central,
                                 crx_zip_entry *entry)
{
    entry->name = central + 46;
    entry->name_size = crx_u16(central + 28);
    entry->compressed = crx_u32(central + 20);
    entry->uncompressed = crx_u32(central + 24);
    entry->relative_offset = crx_u32(central + 42);
    const uint8_t *extra = central + 46 + entry->name_size;
    size_t remaining = crx_u16(central + 30);
    while (remaining) {
        /* A partial unknown field is padding. A known two-byte ID still
         * requires its length, as observed with zip 9.0.5. */
        if (remaining < 2) return true;
        uint16_t kind = crx_u16(extra);
        bool known = kind == 0x0001 || kind == 0x000a ||
                     kind == 0x9901 || kind == 0x5455 ||
                     kind == 0x7075 || kind == 0x6375;
        if (remaining < 4) return !known;
        size_t length = crx_u16(extra + 2);
        extra += 4;
        remaining -= 4;
        if (length > remaining) return false;
        if (kind == 0x000a) {
            if (length != 32 || crx_u16(extra + 4) != 1 ||
                crx_u16(extra + 6) != 24) return false;
        } else if (kind == 0x0001) {
            size_t used = 0;
            uint64_t *values[] = {&entry->uncompressed,
                                  &entry->compressed,
                                  &entry->relative_offset};
            static const size_t offsets[] = {24, 20, 42};
            for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
                if (crx_u32(central + offsets[i]) != UINT32_MAX) continue;
                if (length - used < 8) return false;
                *values[i] = crx_u64(extra + used);
                used += 8;
            }
        } else if (kind == 0x5455) {
            /* 9.0.5 tolerates omitted times and surplus bytes regardless
             * of flags; only the flags byte itself is mandatory. */
            if (!length) return false;
        } else if (kind == 0x7075 || kind == 0x6375) {
            if (length < 5) return false;
            /* A bad checksum or invalid UTF-8 makes the Unicode override
             * inapplicable, rather than invalidating the archive. Repeated
             * fields check the original header name, not an earlier override. */
            if (kind == 0x7075 &&
                crx_u32(extra + 1) == xx_crc32(XX_CRC_TYPE_CRC32,
                                               central + 46,
                                               crx_u16(central + 28)) &&
                crx_utf8_valid(extra + 5, length - 5)) {
                entry->name = extra + 5;
                entry->name_size = length - 5;
            }
        }
        /* The official build leaves AES data uninterpreted, including
         * when the entry compression method is 99. */
        extra += length;
        remaining -= length;
    }
    return true;
}

static bool crx_zip_open(const uint8_t *data, size_t size, crx_zip *zip)
{
    if (size < 22) return false;
    size_t lower = size > UINT16_MAX + 22U ? size - UINT16_MAX - 22U : 0;
    for (size_t pos = size - 22;; --pos) {
        if (crx_u32(data + pos) == UINT32_C(0x06054b50)) {
            uint16_t comment_len = crx_u16(data + pos + 20);
            uint16_t disk = crx_u16(data + pos + 4);
            uint16_t central_disk = crx_u16(data + pos + 6);
            uint32_t central_size = crx_u32(data + pos + 12);
            uint32_t central_offset = crx_u32(data + pos + 16);
            if ((size_t)comment_len <= size - pos - 22 && disk == 0 &&
                central_disk == 0 &&
                central_offset <= size &&
                central_size <= size - central_offset &&
                central_offset + central_size <= pos) {
                /* zip::ZipArchive accepts bytes prepended before the local
                 * ZIP header. Central and local offsets in the directory are
                 * relative to that ZIP start. Prefer a valid raw central
                 * offset, then infer the prefix from the EOCD position. */
                size_t absolute = central_offset;
                if (size - absolute < 4 ||
                    crx_u32(data + absolute) != UINT32_C(0x02014b50))
                    absolute = pos - central_size;
                size_t base = absolute - central_offset;
                size_t at = absolute;
                size_t entries = 0;
                bool valid = true;
                while (at < absolute + central_size) {
                    if (at > size || size - at < 46 ||
                        crx_u32(data + at) != UINT32_C(0x02014b50)) {
                        valid = false;
                        break;
                    }
                    size_t record_size = 46U + crx_u16(data + at + 28) +
                                         crx_u16(data + at + 30) +
                                         crx_u16(data + at + 32);
                    if (record_size > size - at ||
                        record_size > absolute + central_size - at) {
                        valid = false;
                        break;
                    }
                    crx_zip_entry entry;
                    if (!crx_zip_entry_parse(data + at, &entry)) {
                        valid = false;
                        break;
                    }
                    at += record_size;
                    ++entries;
                }
                if (valid && at == absolute + central_size) {
                    zip->data = data;
                    zip->size = size;
                    zip->central_offset = absolute;
                    zip->central_size = central_size;
                    zip->base_offset = base;
                    zip->entries = entries;
                    return true;
                }
            }
        }
        if (pos == lower) break;
    }
    return false;
}

/* The archive was checked above; individual entry errors only omit that
 * manifest or locale, matching ZipArchive::by_name/read behavior. */
static bool crx_zip_read(const crx_zip *zip, const char *wanted,
                         uint8_t **output, size_t *output_size)
{
    size_t wanted_size = strlen(wanted);
    size_t at = zip->central_offset;
    const uint8_t *c = NULL;
    crx_zip_entry entry = {0};
    for (size_t i = 0; i < zip->entries; ++i) {
        const uint8_t *candidate = zip->data + at;
        size_t name_size = crx_u16(candidate + 28);
        size_t record_size = 46U + name_size + crx_u16(candidate + 30) +
                             crx_u16(candidate + 32);
        at += record_size;
        crx_zip_entry current;
        if (!crx_zip_entry_parse(candidate, &current)) return false;
        if (current.name_size == wanted_size &&
            memcmp(current.name, wanted, wanted_size) == 0) {
            c = candidate; /* ZipArchive::by_name uses the last duplicate. */
            entry = current;
        }
    }
    if (!c) return false;
    if ((crx_u16(c + 8) & 1) ||
        entry.relative_offset > SIZE_MAX - zip->base_offset ||
        entry.uncompressed >= UINT32_MAX) return false;
    size_t uncompressed = (size_t)entry.uncompressed;
    size_t local_offset = zip->base_offset + (size_t)entry.relative_offset;
    if (local_offset > zip->size ||
        zip->size - local_offset < 30) return false;
    const uint8_t *local = zip->data + local_offset;
    if (crx_u32(local) != UINT32_C(0x04034b50)) return false;
    size_t payload_offset = (size_t)local_offset + 30U +
                            crx_u16(local + 26) + crx_u16(local + 28);
    if (payload_offset > zip->size) return false;
    /* MO-12: zip 8.2.0 wraps the entry in reader.take(compressed_size)
     * without a bounds check (read.rs:342) and inflates to the natural
     * end of the stream when the central directory's compressed_size
     * overstates what is actually left; it does not reject the entry. */
    size_t compressed = entry.compressed > zip->size - payload_offset ?
                        zip->size - payload_offset :
                        (size_t)entry.compressed;
    const uint8_t *payload = zip->data + payload_offset;
    uint16_t method = crx_u16(c + 10);
    if ((method != 0 && method != 8) ||
        (method == 0 && compressed != uncompressed) ||
        (method == 8 &&
         (!compressed || compressed > UINT_MAX || uncompressed >= UINT_MAX))) return false;
    uint8_t *buf = (uint8_t *)malloc((size_t)uncompressed + 1);
    if (!buf) return false;
    if (method == 0) {
        memcpy(buf, payload, uncompressed);
    } else {
        size_t written = 0;
        bool ok = xx_deflate_decompress_memory(payload, compressed, buf,
                      (size_t)uncompressed + 1, &written, false) &&
                  written == uncompressed;
        if (!ok) { free(buf); return false; }
    }
    if (xx_crc32(XX_CRC_TYPE_CRC32, buf, uncompressed) != crx_u32(c + 16)) {
        free(buf);
        return false;
    }
    buf[uncompressed] = 0;
    *output = buf;
    *output_size = uncompressed;
    return true;
}

static bool crx_read_varint(const uint8_t *data, size_t size, size_t *pos,
                            uint64_t *value)
{
    return xx_protobuf_wire_varint(data, size, pos, value);
}

static bool crx_skip_group(const uint8_t *data, size_t size, size_t *pos,
                           unsigned depth)
{
    return xx_protobuf_wire_skip_field_crx(data, size, pos, depth);
}

static bool crx_read_field(const uint8_t *data, size_t size, size_t *pos,
                           uint32_t *number, unsigned *wire,
                           const uint8_t **bytes, size_t *length,
                           uint64_t *scalar)
{
    uint64_t tag;
    if (!crx_read_varint(data, size, pos, &tag) ||
        (tag >> 3) == 0 || (tag >> 3) > UINT32_MAX) return false;
    *number = (uint32_t)(tag >> 3);
    *wire = (unsigned)(tag & 7);
    *bytes = NULL;
    *length = 0;
    if (*wire == 0) {
        uint64_t decoded;
        if (!crx_read_varint(data, size, pos, &decoded)) return false;
        if (scalar) *scalar = decoded;
        return true;
    }
    if (*wire == 1 || *wire == 5) {
        size_t width = *wire == 1 ? 8 : 4;
        if (width > size - *pos) return false;
        *pos += width;
        return true;
    }
    if (*wire == 2) {
        uint64_t n;
        if (!crx_read_varint(data, size, pos, &n) || n > size - *pos)
            return false;
        *bytes = data + *pos;
        *length = (size_t)n;
        *pos += (size_t)n;
        return true;
    }
    if (*wire == 3)
        return crx_skip_group(data, size, pos, 1);
    return false;
}

static bool crx_parse_proof(const uint8_t *data, size_t size,
                            crx_proof *proof)
{
    memset(proof, 0, sizeof(*proof));
    for (size_t pos = 0; pos < size;) {
        uint32_t number;
        unsigned wire;
        const uint8_t *bytes;
        size_t length;
        if (!crx_read_field(data, size, &pos, &number, &wire,
                            &bytes, &length, NULL)) return false;
        if (number == 1 && wire == 2) {
            proof->key = bytes;
            proof->key_size = length;
        } else if (number == 2 && wire == 2) {
            proof->signature = bytes;
            proof->signature_size = length;
        }
    }
    return true;
}

static bool crx_proofs_push(crx_proofs *list, crx_proof proof)
{
    if (list->count >= SIZE_MAX / sizeof(crx_proof)) return false;
    crx_proof *items = (crx_proof *)realloc(
        list->items, (list->count + 1) * sizeof(crx_proof));
    if (!items) return false;
    list->items = items;
    list->items[list->count++] = proof;
    return true;
}

static bool crx_parse_header(const uint8_t *data, size_t size,
                             crx_proofs *rsa, crx_proofs *ecdsa,
                             const uint8_t **signed_data,
                             size_t *signed_size)
{
    *signed_data = NULL;
    *signed_size = 0;
    for (size_t pos = 0; pos < size;) {
        uint32_t number;
        unsigned wire;
        const uint8_t *bytes;
        size_t length;
        if (!crx_read_field(data, size, &pos, &number, &wire,
                            &bytes, &length, NULL)) return false;
        if (number == 10000 && wire == 2) {
            *signed_data = bytes;
            *signed_size = length;
        } else if ((number == 2 || number == 3) && wire == 2) {
            crx_proof proof;
            if (!crx_parse_proof(bytes, length, &proof) ||
                !crx_proofs_push(number == 2 ? rsa : ecdsa, proof))
                return false;
        }
    }
    return *signed_data != NULL;
}

static bool crx_parse_signed_data(const uint8_t *data, size_t size,
                                  const uint8_t **id, size_t *id_size)
{
    *id = data;
    *id_size = 0;
    for (size_t pos = 0; pos < size;) {
        uint32_t number;
        unsigned wire;
        const uint8_t *bytes;
        size_t length;
        if (!crx_read_field(data, size, &pos, &number, &wire,
                            &bytes, &length, NULL)) return false;
        if (number == 1 && wire == 2) {
            *id = bytes;
            *id_size = length;
        }
    }
    return true;
}

static bool crx_output_signature(const uint8_t *data, size_t size,
                                 xx_crx_yx_signature *signature)
{
    for (size_t pos = 0; pos < size;) {
        uint32_t number;
        unsigned wire;
        const uint8_t *bytes;
        size_t length;
        uint64_t scalar = 0;
        if (!crx_read_field(data, size, &pos, &number, &wire,
                            &bytes, &length, &scalar)) return false;
        if (number == 1 && wire == 2) {
            char *key = crx_dup_length((const char *)bytes, length);
            if (!key) return false;
            free(signature->key);
            signature->key = key;
            signature->key_length = length;
        } else if (number == 2 && wire == 0) {
            signature->verified = scalar != 0;
        }
    }
    return true;
}

uint8_t *xx_crx_yx_complete_terminal_signature(const uint8_t *data, size_t size,
                                              size_t *adjusted_size)
{
    if (!adjusted_size || (!data && size)) return NULL;
    *adjusted_size = 0;
    for (size_t pos = 0; pos < size;) {
        size_t field_at = pos;
        uint64_t tag, claimed;
        uint32_t number;
        unsigned wire;
        const uint8_t *bytes;
        size_t length;
        if (!crx_read_varint(data, size, &pos, &tag) ||
            (tag >> 3) == 0 || (tag >> 3) > 0x1fffffff)
            return NULL;
        if ((tag & 7) != 2) {
            pos = field_at;
            if (!crx_read_field(data, size, &pos, &number, &wire,
                                &bytes, &length, NULL)) return NULL;
            continue;
        }
        size_t length_at = pos;
        if (!crx_read_varint(data, size, &pos, &claimed)) return NULL;
        if (claimed <= size - pos) {
            pos += (size_t)claimed;
            continue;
        }
        if ((tag >> 3) != 15) return NULL;
        xx_crx_yx_signature signature = {0};
        bool complete = crx_output_signature(data + pos, size - pos,
                                              &signature);
        free(signature.key);
        if (!complete) return NULL;
        size_t available = size - pos;
        uint8_t replacement[10];
        size_t replacement_size = 0;
        uint64_t value = available;
        do {
            uint8_t byte = (uint8_t)(value & 127);
            value >>= 7;
            replacement[replacement_size++] = byte | (value ? 128 : 0);
        } while (value);
        if (length_at > SIZE_MAX - replacement_size ||
            length_at + replacement_size > SIZE_MAX - available)
            return NULL;
        size_t total = length_at + replacement_size + available;
        uint8_t *adjusted = (uint8_t *)malloc(total);
        if (!adjusted) return NULL;
        memcpy(adjusted, data, length_at);
        memcpy(adjusted + length_at, replacement, replacement_size);
        memcpy(adjusted + length_at + replacement_size, data + pos,
               available);
        *adjusted_size = total;
        return adjusted;
    }
    return NULL;
}

static bool crx_output_text_slot(xx_crx_yx_result *result, uint32_t number,
                                  char ***text, size_t **length)
{
    switch (number) {
    case 4: *text = &result->id; *length = &result->id_length; break;
    case 5: *text = &result->name; *length = &result->name_length; break;
    case 6: *text = &result->description;
            *length = &result->description_length; break;
    case 7: *text = &result->raw_name;
            *length = &result->raw_name_length; break;
    case 8: *text = &result->raw_description;
            *length = &result->raw_description_length; break;
    case 9: *text = &result->minimum_chrome_version;
            *length = &result->minimum_chrome_version_length; break;
    case 10: *text = &result->homepage_url;
             *length = &result->homepage_url_length; break;
    case 16: *text = &result->version;
             *length = &result->version_length; break;
    default: return false;
    }
    return true;
}

bool xx_crx_yx_parse_output(const uint8_t *data, size_t size,
                          xx_crx_yx_result *result)
{
    if (!result || (!data && size)) return false;
    memset(result, 0, sizeof(*result));
    for (size_t pos = 0; pos < size;) {
        uint32_t number;
        unsigned wire;
        const uint8_t *bytes;
        size_t length;
        uint64_t scalar = 0;
        if (!crx_read_field(data, size, &pos, &number, &wire,
                            &bytes, &length, &scalar)) goto fail;
        if (number == 1 && wire == 0) {
            result->is_crx = scalar != 0;
            result->is_crx_present = true;
        } else if (number == 2 && wire == 0) {
            result->crx_version = (uint32_t)scalar;
            result->crx_version_present = true;
        } else if (number == 3 && wire == 0) {
            result->header_size = (uint32_t)scalar;
            result->header_size_present = true;
        } else if (wire == 2) {
            char **text;
            size_t *text_length;
            if (crx_output_text_slot(result, number, &text,
                                     &text_length)) {
                char *copy = crx_dup_length((const char *)bytes, length);
                if (!copy) goto fail;
                free(*text);
                *text = copy;
                *text_length = length;
            } else if (number >= 11 && number <= 14) {
                xx_crx_yx_string_list *list = number == 11 ?
                    &result->permissions : number == 12 ?
                    &result->host_permissions : number == 13 ?
                    &result->optional_permissions :
                    &result->optional_host_permissions;
                char *copy = crx_dup_length((const char *)bytes, length);
                if (!copy) goto fail;
                if (!crx_list_push(list, copy, length)) {
                    free(copy);
                    goto fail;
                }
            } else if (number == 15) {
                if (result->signature_count >=
                    SIZE_MAX / sizeof(xx_crx_yx_signature)) goto fail;
                size_t next = result->signature_count + 1;
                xx_crx_yx_signature *grown = (xx_crx_yx_signature *)realloc(
                    result->signatures, next * sizeof(*grown));
                if (!grown) goto fail;
                result->signatures = grown;
                xx_crx_yx_signature *signature =
                    &result->signatures[result->signature_count];
                memset(signature, 0, sizeof(*signature));
                result->signature_count = next;
                if (!crx_output_signature(bytes, length, signature))
                    goto fail;
            }
        }
    }
    return true;
fail:
    xx_crx_yx_result_free(result);
    return false;
}

static char *crx_printable_id(const uint8_t *data, size_t size)
{
    static const char digits[] = "abcdefghijklmnop";
    if (size > (SIZE_MAX - 1) / 2) return NULL;
    char *out = (char *)malloc(size * 2 + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < size; ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 15];
    }
    out[size * 2] = 0;
    return out;
}

static char *crx_base64(const uint8_t *data, size_t size)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (size > INT_MAX || size > SIZE_MAX - 2 ||
        (size + 2) / 3 > (SIZE_MAX - 1) / 4) return NULL;
    size_t output_size = xx_base64_encoded_size(size, true);
    char *out = (char *)malloc(output_size + 1);
    if (!out) return NULL;
    if (!xx_base64_encode_buffer(data, size, alphabet, true,
                                 out, output_size, NULL)) {
        free(out);
        return NULL;
    }
    out[output_size] = 0;
    return out;
}

typedef enum { CRX_DIGEST_SHA1, CRX_DIGEST_SHA256 } crx_digest_algorithm;

/* CRX2 signs the ZIP with SHA-1/RSA. CRX3 signs its prefix, little-endian
 * header length, signed protobuf header and ZIP with SHA-256. The PKI
 * verifier also accepts RSA signatures containing an unprefixed digest,
 * as the reference does. All key parsing and verification is in xxfclib. */
static bool crx_verify(const uint8_t *key_data, size_t key_size,
                       const uint8_t *signature, size_t signature_size,
                       crx_digest_algorithm algorithm,
                       const uint8_t *prefix, size_t prefix_size,
                       const uint8_t *signed_data, size_t signed_size,
                       const uint8_t *zip_data, size_t zip_size,
                       bool expect_rsa)
{
    static const uint8_t p256_parameters[] =
        {0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
    xx_asn1_tlv spki, key_algorithm;
    xx_asn1_cursor key_cursor;
    xx_x509_algorithm_id key_id;
    size_t used;
    uint8_t digest[32];
    bool use_sha1 = algorithm == CRX_DIGEST_SHA1;
    uint8_t length[4] = {(uint8_t)signed_size,
                         (uint8_t)(signed_size >> 8),
                         (uint8_t)(signed_size >> 16),
                         (uint8_t)(signed_size >> 24)};
    const xx_pki_hash_part parts[] = {
        {prefix, prefix_size}, {length, prefix_size ? 4 : 0},
        {signed_data, prefix_size ? signed_size : 0}, {zip_data, zip_size}};
    if (!xx_asn1_parse_tlv(key_data, key_size, &spki, &used) ||
        used != key_size || !xx_asn1_is_universal(&spki, XX_ASN1_TAG_SEQUENCE) ||
        !xx_asn1_cursor_init(&key_cursor, &spki, 0) ||
        !xx_asn1_cursor_next(&key_cursor, &key_algorithm) ||
        !xx_x509_parse_algorithm_identifier(&key_algorithm, &key_id))
        return false;
    if (expect_rsa) {
        if (strcmp(key_id.oid_text, "1.2.840.113549.1.1.1")) return false;
    } else {
        if (strcmp(key_id.oid_text, "1.2.840.10045.2.1") ||
            key_id.params_der_size != sizeof(p256_parameters) ||
            memcmp(key_id.params_der, p256_parameters,
                   sizeof(p256_parameters))) return false;
    }
    if (!xx_pki_hash_memoryv(use_sha1 ? XX_PKI_HASH_SHA1 : XX_PKI_HASH_SHA256,
                              parts, 4, digest)) return false;
    return xx_pki_verify_digest(key_data, key_size,
        expect_rsa ? (use_sha1 ? "1.2.840.113549.1.1.5" :
                                 "1.2.840.113549.1.1.11") :
                     "1.2.840.10045.4.3.2",
        NULL, 0, use_sha1 ? XX_PKI_HASH_SHA1 : XX_PKI_HASH_SHA256,
        digest, use_sha1 ? 20 : 32, signature, signature_size);
}

static bool crx_signature_push(xx_crx_yx_result *result, const uint8_t *key,
                               size_t key_size, bool verified)
{
    if (result->signature_count >= SIZE_MAX / sizeof(xx_crx_yx_signature))
        return false;
    char *base64 = crx_base64(key, key_size);
    if (!base64) return false;
    xx_crx_yx_signature *signatures = (xx_crx_yx_signature *)realloc(
        result->signatures,
        (result->signature_count + 1) * sizeof(xx_crx_yx_signature));
    if (!signatures) { free(base64); return false; }
    result->signatures = signatures;
    signatures[result->signature_count].key = base64;
    signatures[result->signature_count].key_length = strlen(base64);
    signatures[result->signature_count].verified = verified;
    result->signature_count++;
    return true;
}

static bool crx_json_at_end(const xx_json *json)
{
    size_t pos = json->position;
    while (pos < json->size &&
           (json->data[pos] == ' ' || json->data[pos] == '\n' ||
            json->data[pos] == '\r' || json->data[pos] == '\t')) ++pos;
    return pos == json->size;
}

/* serde_json rejects malformed UTF-8 before deserializing manifest fields. */
static bool crx_utf8_valid(const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size;) {
        uint8_t first = data[i];
        if (first < 0x80) { ++i; continue; }
        unsigned width = first >= 0xc2 && first <= 0xdf ? 2u :
                         first >= 0xe0 && first <= 0xef ? 3u :
                         first >= 0xf0 && first <= 0xf4 ? 4u : 0u;
        if (!width || width > size - i) return false;
        for (unsigned j = 1; j < width; ++j) {
            uint8_t next = data[i + j];
            bool valid = next >= 0x80 && next <= 0xbf;
            if (j == 1) {
                if (first == 0xe0) valid = next >= 0xa0 && next <= 0xbf;
                if (first == 0xed) valid = next >= 0x80 && next <= 0x9f;
                if (first == 0xf0) valid = next >= 0x90 && next <= 0xbf;
                if (first == 0xf4) valid = next >= 0x80 && next <= 0x8f;
            }
            if (!valid) return false;
        }
        i += width;
    }
    return true;
}

static unsigned crx_json_hex(uint8_t byte)
{
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
    return byte - 'A' + 10;
}

static size_t crx_decoded_string_length(const uint8_t *raw, size_t size)
{
    size_t length = 0;
    for (size_t i = 0; i < size; ++i) {
        if (raw[i] != '\\') { ++length; continue; }
        ++i;
        if (raw[i] != 'u') { ++length; continue; }
        uint32_t cp = 0;
        for (size_t j = 1; j <= 4; ++j)
            cp = (cp << 4) | crx_json_hex(raw[i + j]);
        i += 4;
        if (cp >= 0xd800 && cp <= 0xdbff) {
            i += 6; /* A validated surrogate pair consumes another \\uXXXX. */
            length += 4;
        } else if (cp < 0x80) {
            ++length;
        } else if (cp < 0x800) {
            length += 2;
        } else {
            length += 3;
        }
    }
    return length;
}

static bool crx_json_string(xx_json *json, char **out,
                            size_t *out_length, bool optional)
{
    xx_json_type_t type = xx_json_peek(json);
    if (optional && type == XX_JSON_TYPE_NULL) {
        *out_length = 0;
        return xx_json_null(json);
    }
    if (type != XX_JSON_TYPE_STRING) return false;
    size_t begin = json->position;
    char *tmp = NULL;
    if (!xx_json_string(json, &tmp)) return false;
    size_t length = crx_decoded_string_length(
        json->data + begin + 1, json->position - begin - 2);
    char *copy = crx_dup_length(tmp, length);
    xx_str_free(tmp);
    if (!copy) return false;
    free(*out);
    *out = copy;
    *out_length = length;
    return true;
}

static bool crx_json_string_list(xx_json *json, xx_crx_yx_string_list *list)
{
    if (!xx_json_array_begin(json)) return false;
    if (!xx_json_array_empty(json)) {
        do {
            char *value = NULL;
            size_t length = 0;
            if (!crx_json_string(json, &value, &length, false)) return false;
            if (!crx_list_push(list, value, length)) {
                free(value); return false;
            }
        } while (xx_json_more(json));
    }
    return xx_json_array_end(json);
}

static bool crx_parse_manifest(const uint8_t *data, size_t size,
                               crx_manifest *manifest)
{
    static const char *const names[] = {
        "version", "name", "description", "minimum_chrome_version",
        "homepage_url", "default_locale", "permissions", "host_permissions",
        "optional_permissions", "optional_host_permissions"};
    memset(manifest, 0, sizeof(*manifest));
    if (!crx_utf8_valid(data, size)) return false;
    xx_json json;
    xx_json_init(&json, data, size);
    if (!xx_json_object_begin(&json)) return false;
    uint16_t seen = 0;
    if (!xx_json_object_empty(&json)) {
        do {
            char *key = NULL;
            if (!xx_json_object_key(&json, &key)) return false;
            size_t field = 0;
            while (field < sizeof(names) / sizeof(names[0]) &&
                   strcmp(key, names[field]) != 0) ++field;
            xx_str_free(key);
            if (field < sizeof(names) / sizeof(names[0])) {
                uint16_t bit = (uint16_t)(1u << field);
                if (seen & bit) return false;
                seen |= bit;
            }
            bool ok;
            switch (field) {
                case 0: ok = crx_json_string(&json, &manifest->version,
                                             &manifest->version_length, true); break;
                case 1: ok = crx_json_string(&json, &manifest->name,
                                             &manifest->name_length, true); break;
                case 2: ok = crx_json_string(&json, &manifest->description,
                                             &manifest->description_length, true); break;
                case 3: ok = crx_json_string(&json,
                                   &manifest->minimum_chrome_version,
                                   &manifest->minimum_chrome_version_length,
                                   true); break;
                case 4: ok = crx_json_string(&json, &manifest->homepage_url,
                                             &manifest->homepage_url_length,
                                             true); break;
                case 5: ok = crx_json_string(&json, &manifest->default_locale,
                                             &manifest->default_locale_length,
                                             true); break;
                case 6: ok = crx_json_string_list(&json, &manifest->permissions); break;
                case 7: ok = crx_json_string_list(&json, &manifest->host_permissions); break;
                case 8: ok = crx_json_string_list(&json,
                                       &manifest->optional_permissions); break;
                case 9: ok = crx_json_string_list(&json,
                                       &manifest->optional_host_permissions); break;
                default: ok = xx_json_skip(&json); break;
            }
            if (!ok) return false;
        } while (xx_json_more(&json));
    }
    return xx_json_object_end(&json) && crx_json_at_end(&json);
}

static bool crx_locale_push(crx_locale *locale, char *key,
                            size_t key_length, char *message,
                            size_t message_length)
{
    if (locale->count >= SIZE_MAX / sizeof(crx_locale_entry)) return false;
    crx_locale_entry *items = (crx_locale_entry *)realloc(
        locale->items, (locale->count + 1) * sizeof(crx_locale_entry));
    if (!items) return false;
    locale->items = items;
    items[locale->count].key = key;
    items[locale->count].key_length = key_length;
    items[locale->count].message = message;
    items[locale->count].message_length = message_length;
    locale->count++;
    return true;
}

static bool crx_parse_locale(const uint8_t *data, size_t size,
                             crx_locale *locale)
{
    memset(locale, 0, sizeof(*locale));
    if (!crx_utf8_valid(data, size)) return false;
    xx_json json;
    xx_json_init(&json, data, size);
    if (!xx_json_object_begin(&json)) return false;
    if (!xx_json_object_empty(&json)) {
        do {
            char *raw_key = NULL;
            if (!xx_json_object_key(&json, &raw_key)) return false;
            char *key = crx_dup(raw_key);
            xx_str_free(raw_key);
            if (!key) return false;
            if (!xx_json_object_begin(&json)) { free(key); return false; }
            char *message = NULL;
            size_t message_length = 0;
            bool seen_message = false;
            if (!xx_json_object_empty(&json)) {
                do {
                    char *field = NULL;
                    if (!xx_json_object_key(&json, &field)) {
                        free(key); free(message); return false;
                    }
                    bool is_message = strcmp(field, "message") == 0;
                    xx_str_free(field);
                    bool ok;
                    if (is_message) {
                        ok = !seen_message &&
                             crx_json_string(&json, &message,
                                             &message_length, false);
                        seen_message = true;
                    } else {
                        ok = xx_json_skip(&json);
                    }
                    if (!ok) { free(key); free(message); return false; }
                } while (xx_json_more(&json));
            }
            if (!xx_json_object_end(&json) || !seen_message ||
                !crx_locale_push(locale, key, strlen(key), message,
                                 message_length)) {
                free(key); free(message); return false;
            }
        } while (xx_json_more(&json));
    }
    return xx_json_object_end(&json) && crx_json_at_end(&json);
}

static const crx_locale_entry *crx_resolve_locale(
    const crx_locale *locale, const char *raw, size_t length)
{
    if (!raw || length < 8 || memcmp(raw, "__MSG_", 6) != 0 ||
        raw[length - 2] != '_' || raw[length - 1] != '_')
        return NULL;
    const char *wanted = raw + 6;
    size_t wanted_size = length - 8;
    for (size_t i = 0; i < locale->count; ++i) {
        if (locale->items[i].key_length == wanted_size &&
            memcmp(locale->items[i].key, wanted, wanted_size) == 0)
            return &locale->items[i];
    }
    return NULL;
}

static bool crx_load_manifest(const crx_zip *zip, xx_crx_yx_result *result)
{
    uint8_t *data = NULL;
    size_t size = 0;
    if (!crx_zip_read(zip, "manifest.json", &data, &size)) return true;
    crx_manifest manifest;
    bool parsed = crx_parse_manifest(data, size, &manifest);
    free(data);
    if (!parsed) { crx_manifest_free(&manifest); return true; }

    const char *language = manifest.default_locale ?
                           manifest.default_locale : "en";
    crx_locale locale = {0};
    bool has_locale = false;
    size_t lang_size = strlen(language);
    if (lang_size < SIZE_MAX - sizeof("_locales//messages.json")) {
        size_t path_size = sizeof("_locales//messages.json") + lang_size;
        char *path = (char *)malloc(path_size);
        if (!path) { crx_manifest_free(&manifest); return false; }
        snprintf(path, path_size, "_locales/%s/messages.json", language);
        data = NULL;
        size = 0;
        if (crx_zip_read(zip, path, &data, &size)) {
            has_locale = crx_parse_locale(data, size, &locale);
            if (!has_locale) crx_locale_free(&locale);
            free(data);
        }
        free(path);
    }

    result->version = manifest.version;
    result->version_length = manifest.version_length;
    manifest.version = NULL;
    result->minimum_chrome_version = manifest.minimum_chrome_version;
    result->minimum_chrome_version_length =
        manifest.minimum_chrome_version_length;
    manifest.minimum_chrome_version = NULL;
    result->homepage_url = manifest.homepage_url;
    result->homepage_url_length = manifest.homepage_url_length;
    manifest.homepage_url = NULL;
    result->permissions = manifest.permissions;
    memset(&manifest.permissions, 0, sizeof(manifest.permissions));
    result->host_permissions = manifest.host_permissions;
    memset(&manifest.host_permissions, 0, sizeof(manifest.host_permissions));
    result->optional_permissions = manifest.optional_permissions;
    memset(&manifest.optional_permissions, 0,
           sizeof(manifest.optional_permissions));
    result->optional_host_permissions = manifest.optional_host_permissions;
    memset(&manifest.optional_host_permissions, 0,
           sizeof(manifest.optional_host_permissions));

    if (has_locale) {
        result->raw_name = manifest.name; manifest.name = NULL;
        result->raw_name_length = manifest.name_length;
        result->raw_description = manifest.description;
        result->raw_description_length = manifest.description_length;
        manifest.description = NULL;
        const crx_locale_entry *resolved = crx_resolve_locale(
            &locale, result->raw_name, result->raw_name_length);
        result->name_length = resolved ? resolved->message_length :
                              result->raw_name_length;
        result->name = crx_dup_length(
            resolved ? resolved->message :
            (result->raw_name ? result->raw_name : ""),
            result->name_length);
        if (!result->raw_name) { free(result->name); result->name = NULL; }
        resolved = crx_resolve_locale(&locale, result->raw_description,
                                      result->raw_description_length);
        result->description_length = resolved ? resolved->message_length :
                                     result->raw_description_length;
        result->description = crx_dup_length(
            resolved ? resolved->message :
            (result->raw_description ? result->raw_description : ""),
            result->description_length);
        if (!result->raw_description) {
            free(result->description);
            result->description = NULL;
        }
        if ((result->raw_name && !result->name) ||
            (result->raw_description && !result->description)) {
            crx_locale_free(&locale);
            crx_manifest_free(&manifest);
            return false;
        }
    } else {
        result->name = manifest.name; manifest.name = NULL;
        result->name_length = manifest.name_length;
        result->description = manifest.description;
        result->description_length = manifest.description_length;
        manifest.description = NULL;
    }
    crx_locale_free(&locale);
    crx_manifest_free(&manifest);
    return true;
}

bool xx_crx_yx_permhash(const xx_crx_yx_result *result, char hex[65])
{
    static const char digits[] = "0123456789abcdef";
    if (!result || !hex || !result->is_crx) return false;
    xx_sha256_context ctx;
    if (!xx_sha256_init(&ctx)) return false;
    for (size_t i = 0; i < result->permissions.count; ++i)
        xx_sha256_update(&ctx, result->permissions.items[i],
                           result->permissions.lengths[i]);
    uint8_t digest[32];
    if (!xx_sha256_final(&ctx, digest, sizeof(digest))) return false;
    for (size_t i = 0; i < 32; ++i) {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 15];
    }
    hex[64] = 0;
    return true;
}

static _Thread_local bool crx_permhash_cache_present;
static _Thread_local char crx_permhash_cache[65];

void xx_crx_yx_permhash_cache_reset(void)
{
    crx_permhash_cache_present = false;
}

bool xx_crx_yx_permhash_cached(const xx_crx_yx_result *result, char hex[65])
{
    if (!hex) return false;
    if (crx_permhash_cache_present) {
        memcpy(hex, crx_permhash_cache, sizeof(crx_permhash_cache));
        return true;
    }
    if (!xx_crx_yx_permhash(result, hex)) return false;
    memcpy(crx_permhash_cache, hex, sizeof(crx_permhash_cache));
    crx_permhash_cache_present = true;
    return true;
}

static bool crx_parse_v2(const uint8_t *data, size_t size,
                         xx_crx_yx_result *result)
{
    if (size < 16) return true;
    uint32_t key_size = crx_u32(data + 8);
    uint32_t signature_size = crx_u32(data + 12);
    if (key_size > size - 16 ||
        signature_size > size - 16 - key_size) return true;
    const uint8_t *key = data + 16;
    const uint8_t *signature = key + key_size;
    const uint8_t *zip_data = signature + signature_size;
    size_t zip_size = size - 16 - key_size - signature_size;
    crx_zip zip;
    if (!crx_zip_open(zip_data, zip_size, &zip)) return true;

    uint8_t hash[32];
    if (!xx_sha256_memory(key, key_size, hash)) return false;
    result->id = crx_printable_id(hash, 16);
    if (!result->id) return false;
    result->id_length = 32;
    bool verified = crx_verify(key, key_size, signature, signature_size,
                               CRX_DIGEST_SHA1, NULL, 0, NULL, 0,
                               zip_data, zip_size, true);
    if (!crx_signature_push(result, key, key_size, verified)) return false;
    result->is_crx = true;
    result->crx_version = 2;
    result->header_size = key_size + signature_size;
    return crx_load_manifest(&zip, result);
}

static bool crx_parse_v3(const uint8_t *data, size_t size,
                         xx_crx_yx_result *result)
{
    if (size < 12) return true;
    uint32_t header_size = crx_u32(data + 8);
    if (header_size > size - 12) return true;
    const uint8_t *header_data = data + 12;
    const uint8_t *zip_data = header_data + header_size;
    size_t zip_size = size - 12 - header_size;
    crx_proofs rsa = {0}, ecdsa = {0};
    const uint8_t *signed_data = NULL, *id_bytes = NULL;
    size_t signed_size = 0, id_size = 0;
    bool valid = crx_parse_header(header_data, header_size, &rsa, &ecdsa,
                                  &signed_data, &signed_size) &&
                 crx_parse_signed_data(signed_data, signed_size,
                                       &id_bytes, &id_size);
    crx_zip zip;
    if (valid) valid = crx_zip_open(zip_data, zip_size, &zip);
    if (!valid) { free(rsa.items); free(ecdsa.items); return true; }

    result->id = crx_printable_id(id_bytes, id_size);
    if (!result->id) { free(rsa.items); free(ecdsa.items); return false; }
    result->id_length = id_size * 2;
    static const uint8_t signed_prefix[] = "CRX3 SignedData";
    for (size_t list_no = 0; list_no < 2; ++list_no) {
        const crx_proofs *list = list_no == 0 ? &rsa : &ecdsa;
        for (size_t i = 0; i < list->count; ++i) {
            const crx_proof *proof = &list->items[i];
            if (!proof->key || !proof->signature) continue;
            bool verified = crx_verify(proof->key, proof->key_size,
                                       proof->signature, proof->signature_size,
                                       CRX_DIGEST_SHA256, signed_prefix,
                                       sizeof(signed_prefix), signed_data,
                                       signed_size, zip_data, zip_size,
                                       list_no == 0);
            if (!crx_signature_push(result, proof->key,
                                    proof->key_size, verified)) {
                free(rsa.items); free(ecdsa.items); return false;
            }
        }
    }
    free(rsa.items);
    free(ecdsa.items);
    result->is_crx = true;
    result->crx_version = 3;
    result->header_size = header_size;
    return crx_load_manifest(&zip, result);
}

bool xx_crx_yx_parse(const uint8_t *data, size_t size, xx_crx_yx_result *result)
{
    if (!result || (!data && size)) return false;
    memset(result, 0, sizeof(*result));
    result->is_crx_present = true;
    if (size < 8 || memcmp(data, "Cr24", 4) != 0) return true;
    uint32_t version = crx_u32(data + 4);
    bool ok = true;
    if (version == 2) ok = crx_parse_v2(data, size, result);
    else if (version == 3) ok = crx_parse_v3(data, size, result);
    if (!ok) xx_crx_yx_result_free(result);
    else if (result->is_crx) {
        result->crx_version_present = true;
        result->header_size_present = true;
    }
    return ok;
}
