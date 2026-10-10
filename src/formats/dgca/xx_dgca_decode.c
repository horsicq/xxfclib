/* Independent archive-byte implementation; SPDX-License-Identifier: MIT. */
#include "dgca_native.h"
#include "dgca_codec.h"
#include "dgca_cipher.h"
#include <string.h>
#include "xxfclib/data/xx_data.h"

typedef struct dg_chunk {
    unsigned char header[32];
    uint64_t offset, payload, size, end, next;
    uint32_t flags;
} dg_chunk;
typedef struct dg_block_cache {
    dg_chunk chunk;
    unsigned char *bytes;
    size_t raw;
    int valid;
} dg_block_cache;
typedef struct dg_key {
    const char *password;
    size_t size;
    int encrypted;
} dg_key;

static uint32_t dg_crc_update(uint32_t crc, const unsigned char *p, size_t count)
{
    size_t i;
    unsigned j;
    for (i = 0; i < count; ++i) {
        crc ^= p[i];
        for (j = 0; j < 8; ++j) crc = (crc >> 1) ^ ((0U - (crc & 1U)) & UINT32_C(0xEDB88320));
    }
    return crc;
}
static uint32_t dg_crc(const unsigned char *p, size_t n)
{
    return ~dg_crc_update(UINT32_MAX, p, n);
}
static int dg_cancel(dg_result *r)
{
    return r->callbacks.cancelled && r->callbacks.cancelled(r->callbacks.opaque);
}
static dg_status dg_fail(dg_result *r, dg_status status, const char *detail)
{
    r->status = status;
    r->detail = detail;
    return status;
}
static dg_status dg_read(dg_result *r, uint64_t offset, void *buffer, size_t size)
{
    unsigned char *out = (unsigned char *)buffer;
    while (size) {
        size_t actual;
        if (dg_cancel(r)) return dg_fail(r, DG_CANCELLED, "DGCA cancelled");
        actual = r->callbacks.read_at(r->callbacks.opaque, offset, out, size);
        if (!actual || actual > size) return dg_fail(r, DG_IO, "DGCA short input");
        offset += actual;
        out += actual;
        size -= actual;
    }
    return DG_OK;
}
static void *dg_alloc(dg_result *r, size_t size)
{
    void *p;
    if (dg_cancel(r)) {
        dg_fail(r, DG_CANCELLED, "DGCA cancelled");
        return NULL;
    }
    p = r->callbacks.allocate(r->callbacks.opaque, size ? size : 1);
    if (!p) dg_fail(r, DG_MEMORY, "DGCA memory budget");
    return p;
}
static void dg_release(dg_result *r, void *p)
{
    if (p) r->callbacks.release(r->callbacks.opaque, p);
}
static void dg_cache_clear(dg_result *r, dg_block_cache *cache)
{
    dg_release(r, cache->bytes);
    memset(cache, 0, sizeof(*cache));
}
static dg_status dg_memory_crc(dg_result *r, const unsigned char *p, size_t size, uint32_t *out)
{
    uint32_t crc = UINT32_MAX;
    while (size) {
        size_t n = size > 4096 ? 4096 : size;
        if (dg_cancel(r)) return dg_fail(r, DG_CANCELLED, "DGCA cancelled");
        crc = dg_crc_update(crc, p, n);
        p += n;
        size -= n;
    }
    *out = ~crc;
    return DG_OK;
}
static dg_status dg_decrypt(dg_result *r, const dg_key *key, const void *seed, size_t seed_size, unsigned char *p, size_t size)
{
    dg_cipher *cipher = (dg_cipher *)dg_alloc(r, sizeof(*cipher));
    if (!cipher) return r->status;
    dg_cipher_init(cipher, seed, seed_size, key->password, key->size);
    while (size) {
        size_t n = size > 4096 ? 4096 : size;
        if (dg_cancel(r)) {
            memset(cipher, 0, sizeof(*cipher));
            dg_release(r, cipher);
            return dg_fail(r, DG_CANCELLED, "DGCA cancelled");
        }
        dg_cipher_decrypt(cipher, p, n);
        p += n;
        size -= n;
    }
    memset(cipher, 0, sizeof(*cipher));
    dg_release(r, cipher);
    return DG_OK;
}
static dg_status dg_header_fields(dg_result *r, uint64_t offset, uint64_t bound, dg_chunk *out)
{
    unsigned char header[32];
    uint64_t padded;
    memcpy(header, out->header, 32);
    memset(header + 28, 0, 4);
    if (xx_data_get_u32(header + 4, 4, 0, false) != 32 || xx_data_get_u32(header + 20, 4, 0, false) != 16) return dg_fail(r, DG_FORMAT, "DGCA chunk layout");
    if (dg_crc(header, 32) != xx_data_get_u32(out->header + 28, 4, 0, false)) return dg_fail(r, DG_CHECKSUM, "DGCA header CRC32 or password");
    out->offset = offset;
    out->payload = offset + 32;
    out->size = xx_data_get_u64(header + 8, 8, 0, false);
    out->flags = xx_data_get_u32(header + 16, 4, 0, false);
    if (out->size > bound - out->payload || out->size > UINT64_MAX - 15) return dg_fail(r, DG_FORMAT, "DGCA chunk size");
    out->end = out->payload + out->size;
    padded = (out->size + 15) & ~UINT64_C(15);
    if (padded > bound - out->payload) return dg_fail(r, DG_FORMAT, "DGCA chunk padding");
    out->next = out->payload + padded;
    return DG_OK;
}

/* Chunks independently authenticate their header and exact (unpadded) body.
 * Header CRC includes the body CRC, with its own final four bytes zeroed. */
static dg_status dg_chunk_read(dg_result *r, uint64_t offset, uint64_t bound, dg_chunk *out)
{
    unsigned char buffer[4096];
    uint64_t pos, remaining;
    uint32_t crc = UINT32_MAX;
    if (offset > bound || bound - offset < 32) return dg_fail(r, DG_FORMAT, "DGCA truncated chunk header");
    memset(out, 0, sizeof(*out));
    if (dg_read(r, offset, out->header, 32) != DG_OK) return r->status;
    if (dg_header_fields(r, offset, bound, out) != DG_OK) return r->status;
    pos = out->payload;
    remaining = out->size;
    while (remaining) {
        size_t n = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        if (dg_read(r, pos, buffer, n) != DG_OK) return r->status;
        crc = dg_crc_update(crc, buffer, n);
        remaining -= n;
        pos += n;
    }
    if (~crc != xx_data_get_u32(out->header + 24, 4, 0, false)) return dg_fail(r, DG_CHECKSUM, "DGCA payload CRC32");
    return DG_OK;
}
static dg_status dg_codec_header(dg_result *r, uint64_t offset, uint64_t bound, const dg_key *key, dg_chunk *out)
{
    static const unsigned char zero[8] = {0};
    if (offset > bound || bound - offset < 32) return dg_fail(r, DG_FORMAT, "DGCA truncated codec header");
    memset(out, 0, sizeof(*out));
    if (dg_read(r, offset, out->header, 32) != DG_OK) return r->status;
    if (key->encrypted && dg_decrypt(r, key, zero, sizeof(zero), out->header, 32) != DG_OK) return r->status;
    return dg_header_fields(r, offset, bound, out);
}
static int dg_tag(const dg_chunk *c, const char *tag)
{
    return !memcmp(c->header, tag, 4);
}
static dg_status dg_find(dg_result *r, const dg_chunk *parent, const char *tag, dg_chunk *out)
{
    dg_chunk child;
    uint64_t pos = parent->payload;
    int found = 0;
    while (pos < parent->end) {
        if (dg_chunk_read(r, pos, parent->end, &child) != DG_OK) return r->status;
        if (dg_tag(&child, tag)) {
            if (found) return dg_fail(r, DG_FORMAT, "DGCA duplicate section");
            *out = child;
            found = 1;
        }
        if (child.next <= pos) return dg_fail(r, DG_FORMAT, "DGCA chunk loop");
        pos = child.next;
    }
    if (pos != parent->end || !found) return dg_fail(r, DG_FORMAT, "DGCA missing section");
    return DG_OK;
}
static dg_status dg_codec(dg_result *r, const dg_chunk *chunk, const dg_key *key, uint64_t expected, unsigned char **bytes, size_t *size)
{
    unsigned char *packed = NULL, *output = NULL;
    uint32_t crc;
    dg_status status;
    size_t raw_size;
    *bytes = NULL;
    *size = 0;
    if (!dg_tag(chunk, "DGCC") || chunk->flags || chunk->size < 8) return dg_fail(r, DG_FORMAT, "DGCA codec chunk");
    if (chunk->size > SIZE_MAX || (expected != UINT64_MAX && expected > SIZE_MAX)) return dg_fail(r, DG_MEMORY, "DGCA address space limit");
    packed = (unsigned char *)dg_alloc(r, (size_t)chunk->size);
    if (!packed) return r->status;
    if (dg_read(r, chunk->payload, packed, (size_t)chunk->size) != DG_OK) goto done;
    if (key->encrypted && dg_decrypt(r, key, chunk->header, 32, packed, (size_t)chunk->size) != DG_OK) goto done;
    if (dg_memory_crc(r, packed, (size_t)chunk->size, &crc) != DG_OK) goto done;
    if (crc != xx_data_get_u32(chunk->header + 24, 4, 0, false)) {
        dg_fail(r, DG_CHECKSUM, "DGCA codec CRC32 or password");
        goto done;
    }
    status = dg_codec_output_size(&r->callbacks, packed, (size_t)chunk->size, &raw_size);
    if (status != DG_OK) {
        dg_fail(r, status, "DGCA invalid codec size");
        goto done;
    }
    if (expected == UINT64_MAX) expected = raw_size;
    if (expected != raw_size) {
        dg_fail(r, DG_FORMAT, "DGCA decoded size");
        goto done;
    }
    if (expected) {
        output = (unsigned char *)dg_alloc(r, (size_t)expected);
        if (!output) goto done;
    }
    status = dg_codec_decode(&r->callbacks, packed, (size_t)chunk->size, output, (size_t)expected);
    if (status != DG_OK) {
        dg_fail(r, status, status == DG_MEMORY ? "DGCA codec memory budget" : status == DG_CANCELLED ? "DGCA cancelled" : "DGCA invalid compressed payload");
        goto done;
    }
    *bytes = output;
    output = NULL;
    *size = (size_t)expected;
done:
    dg_release(r, packed);
    dg_release(r, output);
    return r->status;
}
static dg_status dg_slice(dg_result *r, const dg_chunk *data, const dg_key *key, uint64_t offset, uint64_t skip, unsigned char *out, size_t wanted, dg_block_cache *cache)
{
    uint64_t position;
    size_t done = 0;
    if (offset > data->size) return dg_fail(r, DG_FORMAT, "DGCA solid group offset");
    position = data->payload + offset;
    while (done < wanted) {
        size_t raw, start, take, part;
        if (dg_cancel(r)) return dg_fail(r, DG_CANCELLED, "DGCA cancelled");
        if (!cache->valid || cache->chunk.offset != position) {
            dg_cache_clear(r, cache);
            if (dg_codec_header(r, position, data->end, key, &cache->chunk) != DG_OK) return r->status;
            if (dg_codec(r, &cache->chunk, key, UINT64_MAX, &cache->bytes, &cache->raw) != DG_OK) return r->status;
            cache->valid = 1;
        }
        raw = cache->raw;
        if (skip >= raw) {
            skip -= raw;
            position = cache->chunk.next;
            continue;
        }
        start = (size_t)skip;
        skip = 0;
        take = raw - start;
        if (take > wanted - done) take = wanted - done;
        part = 0;
        while (part < take) {
            size_t n = take - part > 4096 ? 4096 : take - part;
            if (dg_cancel(r)) return dg_fail(r, DG_CANCELLED, "DGCA cancelled");
            memcpy(out + done + part, cache->bytes + start + part, n);
            part += n;
        }
        done += take;
        position = cache->chunk.next;
    }
    return DG_OK;
}
static dg_status dg_nested_codec(dg_result *r, const dg_chunk *section, const dg_key *key, uint64_t expected, unsigned char **bytes, size_t *size)
{
    dg_chunk chunk;
    if (dg_codec_header(r, section->payload, section->end, key, &chunk) != DG_OK) return r->status;
    if (chunk.next != section->end) return dg_fail(r, DG_FORMAT, "DGCA trailing codec data");
    return dg_codec(r, &chunk, key, expected, bytes, size);
}
void dg_native_result_free(dg_result *r)
{
    size_t i;
    if (!r) return;
    for (i = 0; i < r->count; ++i) {
        dg_release(r, r->members[i].name);
        dg_release(r, r->members[i].bytes);
    }
    dg_release(r, r->members);
    r->members = NULL;
    r->count = 0;
}
dg_status dg_native_decode(const dg_callbacks *callbacks, uint64_t input_size, const char *password_utf8, uint64_t member_limit, uint64_t max_members, dg_result *r)
{
    dg_chunk root, data, info, iarc, ifdt, ifnm;
    unsigned char metadata[80], record[64];
    unsigned char *table = NULL, *names = NULL;
    size_t table_size = 0, names_size = 0, names_pos = 0, i, j;
    uint64_t count, total_raw = 0;
    dg_status status;
    dg_key key;
    static const unsigned char zero[8] = {0};
    dg_block_cache cache = {0};
    if (!r) return DG_FORMAT;
    memset(r, 0, sizeof(*r));
    if (!callbacks || !callbacks->read_at || !callbacks->allocate || !callbacks->release) return dg_fail(r, DG_FORMAT, "DGCA missing callbacks");
    r->callbacks = *callbacks;
    key.password = password_utf8;
    key.size = password_utf8 ? strlen(password_utf8) : 0;
    key.encrypted = 0;
    if (dg_chunk_read(r, 0, input_size, &root) != DG_OK) goto fail;
    if (!dg_tag(&root, "DGCA") || root.flags != 1) {
        dg_fail(r, DG_FORMAT, "DGCA archive header");
        goto fail;
    }
    r->format_size = root.end;
    if (dg_find(r, &root, "DATA", &data) != DG_OK || dg_find(r, &root, "INFO", &info) != DG_OK) goto fail;
    if (data.flags || info.flags != 1) {
        dg_fail(r, DG_FORMAT, "DGCA archive sections");
        goto fail;
    }
    if (dg_find(r, &info, "IARC", &iarc) != DG_OK) goto fail;
    if (iarc.size != 80 || iarc.flags) {
        dg_fail(r, DG_FORMAT, "DGCA IARC layout");
        goto fail;
    }
    if (dg_read(r, iarc.payload, metadata, 80) != DG_OK) goto fail;
    if (xx_data_get_u32(metadata, 4, 0, false) & 1U) {
        key.encrypted = 1;
        if (!password_utf8) {
            dg_fail(r, DG_PASSWORD_REQUIRED, "DGCA password required");
            goto fail;
        }
        if (dg_decrypt(r, &key, zero, sizeof(zero), metadata + 32, 48) != DG_OK) goto fail;
        if (xx_data_get_u64(metadata + 32, 8, 0, false)) {
            dg_fail(r, DG_CHECKSUM, "DGCA incorrect password");
            goto fail;
        }
    }
    if (memcmp(metadata + 4, " 001", 4) && memcmp(metadata + 4, " 990", 4) && memcmp(metadata + 4, " a90", 4)) {
        dg_fail(r, DG_FORMAT, "DGCA archive version");
        goto fail;
    }
    count = xx_data_get_u64(metadata + 40, 8, 0, false);
    if (count > max_members || count > SIZE_MAX / sizeof(dg_member) || count > SIZE_MAX / 64) {
        dg_fail(r, DG_MEMBER_LIMIT, "DGCA record limit");
        goto fail;
    }
    if (dg_find(r, &info, "IFDT", &ifdt) != DG_OK || dg_find(r, &info, "IFNM", &ifnm) != DG_OK) goto fail;
    if (dg_nested_codec(r, &ifdt, &key, count * 64, &table, &table_size) != DG_OK ||
        dg_nested_codec(r, &ifnm, &key, xx_data_get_u64(metadata + 64, 8, 0, false), &names, &names_size) != DG_OK)
        goto fail;
    if (table_size != count * 64 || table_size != xx_data_get_u64(metadata + 56, 8, 0, false) || names_size != xx_data_get_u64(metadata + 64, 8, 0, false)) {
        dg_fail(r, DG_FORMAT, "DGCA directory size");
        goto fail;
    }
    if (count) {
        r->members = (dg_member *)dg_alloc(r, (size_t)count * sizeof(*r->members));
        if (!r->members) goto fail;
        memset(r->members, 0, (size_t)count * sizeof(*r->members));
        r->count = (size_t)count;
    }
    for (i = 0; i < (size_t)count; ++i) {
        dg_member *member = &r->members[i];
        uint32_t name_size;
        uint64_t skip;
        if (dg_cancel(r)) {
            dg_fail(r, DG_CANCELLED, "DGCA cancelled");
            goto fail;
        }
        for (j = 0; j < 64; ++j) record[j] = table[j * (size_t)count + i];
        member->crc32 = xx_data_get_u32(record, 4, 0, false);
        member->attributes = xx_data_get_u32(record + 4, 4, 0, false);
        member->timestamp = xx_data_get_u64(record + 8, 8, 0, false);
        member->size = xx_data_get_u64(record + 24, 8, 0, false);
        member->compressed_size = xx_data_get_u64(record + 32, 8, 0, false);
        member->data_offset = xx_data_get_u64(record + 40, 8, 0, false);
        skip = xx_data_get_u64(record + 48, 8, 0, false);
        if (member->size > member_limit || member->size > SIZE_MAX) {
            dg_fail(r, DG_MEMBER_LIMIT, "DGCA member size limit");
            goto fail;
        }
        if (member->size > UINT64_MAX - total_raw) {
            dg_fail(r, DG_FORMAT, "DGCA total size overflow");
            goto fail;
        }
        total_raw += member->size;
        if (skip > xx_data_get_u64(metadata + 48, 8, 0, false) || member->size > xx_data_get_u64(metadata + 48, 8, 0, false) - skip) {
            dg_fail(r, DG_FORMAT, "DGCA solid member extent");
            goto fail;
        }
        /* A retained block can replace the next decode, so it has the same
         * live-memory peak as that decode. Drop it before allocating another
         * member when it cannot be reused, including empty/dir records. */
        if (cache.valid && (!member->size || (member->attributes & 0x10) || member->data_offset > data.size || cache.chunk.offset != data.payload + member->data_offset ||
                            skip >= cache.raw))
            dg_cache_clear(r, &cache);
        if (names_pos > names_size || names_size - names_pos < 4) {
            dg_fail(r, DG_FORMAT, "DGCA missing member name");
            goto fail;
        }
        name_size = xx_data_get_u32(names + names_pos, 4, 0, false);
        names_pos += 4;
        if (!name_size || name_size > 32768 || name_size > names_size - names_pos || memchr(names + names_pos, 0, name_size)) {
            dg_fail(r, DG_FORMAT, "DGCA member name size");
            goto fail;
        }
        member->name = (char *)dg_alloc(r, (size_t)name_size + 1);
        if (!member->name) goto fail;
        memcpy(member->name, names + names_pos, name_size);
        member->name[name_size] = 0;
        names_pos += name_size;
        for (j = 0; j < name_size; ++j)
            if (member->name[j] == '\\') member->name[j] = '/';
        if (member->attributes & 0x10) {
            if (member->size || member->compressed_size) {
                dg_fail(r, DG_FORMAT, "DGCA directory payload");
                goto fail;
            }
            continue;
        }
        if (!member->size) {
            if (member->crc32 || member->compressed_size) {
                dg_fail(r, DG_FORMAT, "DGCA empty member payload");
                goto fail;
            }
            continue;
        }
        if (member->data_offset > data.size || data.size - member->data_offset < 32) {
            dg_fail(r, DG_FORMAT, "DGCA member offset");
            goto fail;
        }
        if (member->compressed_size > data.size - member->data_offset) {
            dg_fail(r, DG_FORMAT, "DGCA member packed extent");
            goto fail;
        }
        member->bytes = (unsigned char *)dg_alloc(r, (size_t)member->size);
        if (!member->bytes) goto fail;
        if (dg_slice(r, &data, &key, member->data_offset, skip, member->bytes, (size_t)member->size, &cache) != DG_OK) goto fail;
        {
            uint32_t crc;
            if (dg_memory_crc(r, member->bytes, (size_t)member->size, &crc) != DG_OK) goto fail;
            if (crc != member->crc32) {
                dg_fail(r, DG_CHECKSUM, "DGCA member CRC32");
                goto fail;
            }
        }
    }
    if (names_pos != names_size) {
        dg_fail(r, DG_FORMAT, "DGCA trailing member names");
        goto fail;
    }
    if (total_raw != xx_data_get_u64(metadata + 48, 8, 0, false)) {
        dg_fail(r, DG_FORMAT, "DGCA total raw size");
        goto fail;
    }
    dg_cache_clear(r, &cache);
    dg_release(r, table);
    dg_release(r, names);
    r->status = DG_OK;
    r->detail = "DGCA native decode";
    return DG_OK;
fail:
    status = r->status;
    dg_cache_clear(r, &cache);
    dg_release(r, table);
    dg_release(r, names);
    dg_native_result_free(r);
    return status;
}
