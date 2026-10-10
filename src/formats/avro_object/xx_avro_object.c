/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://avro.apache.org/docs/1.12.0/specification/
 * Object Container File metadata and decoded block payloads, no record decode.
 */
#include "xxfclib/formats/avro_object/xx_avro_object.h"
#include "xxfclib/json/xx_json.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/algo/adler32/xx_adler32.h"
#include "../xx_payload_members.h"
#include "../common/xx_utf8_validation.h"

#define AVRO_MAX_BLOCK (64U * 1024U * 1024U)
#define AVRO_MAX_TOTAL_DECODED (256U * 1024U * 1024U)

enum avro_codec {
    AVRO_NULL,
    AVRO_DEFLATE,
    AVRO_BZIP2,
    AVRO_ZSTANDARD
};

typedef struct avro_sink_s {
    uint8_t *data;
    size_t size;
    size_t capacity;
    xx_pd_struct *pd;
} avro_sink;

static ssize_t avro_write(xx_io_device *device, const void *bytes, size_t count)
{
    avro_sink *sink = device ? (avro_sink *)device->priv : NULL;
    size_t needed, capacity;
    uint8_t *next;
    if (!sink || (!bytes && count) || (sink->pd && xx_pd_is_stopped(sink->pd)) || count > AVRO_MAX_BLOCK - sink->size) return -1;
    needed = sink->size + count;
    if (needed > sink->capacity) {
        capacity = sink->capacity ? sink->capacity : 1024U;
        while (capacity < needed) {
            if (capacity > AVRO_MAX_BLOCK / 2U) {
                capacity = AVRO_MAX_BLOCK;
                break;
            }
            capacity *= 2U;
        }
        next = (uint8_t *)xx_mem_realloc(sink->data, capacity);
        if (!next) return -1;
        sink->data = next;
        sink->capacity = capacity;
    }
    if (count) xx_rt_memcpy(sink->data + sink->size, bytes, count);
    sink->size = needed;
    return (ssize_t)count;
}

static bool avro_decode_block(Abstractformat *f, int64_t at, int64_t size, enum avro_codec codec, uint8_t **output, size_t *output_size, xx_pd_struct *pd)
{
    uint8_t *packed = NULL;
    avro_sink sink;
    xx_io_device destination;
    bool ok = false;
    if (!f || !output || !output_size || size <= 0 || (uint64_t)size > AVRO_MAX_BLOCK || (pd && xx_pd_is_stopped(pd))) return false;
    *output = NULL;
    *output_size = 0U;
    packed = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!packed || !pm_read(f, at, packed, (size_t)size)) goto done;
    xx_mem_zero(&sink, sizeof(sink));
    xx_mem_zero(&destination, sizeof(destination));
    sink.pd = pd;
    destination.write = avro_write;
    destination.priv = &sink;
    if (codec == AVRO_DEFLATE) {
        size_t consumed = 0U;
        ok = xx_deflate_unpack_memory_to_device_ex(packed, (size_t)size, &destination, &consumed, false, pd);
        if (ok && consumed != (size_t)size) {
            /* Apache Avro Python 1.12.0 accidentally retains the first
             * three bytes of zlib's Adler-32 trailer after raw Deflate.
             * Accept that producer's output only when those bytes match. */
            uint32_t adler = xx_adler32(sink.data, sink.size);
            ok = (size_t)size - consumed == 3U && packed[consumed] == (uint8_t)(adler >> 24U) && packed[consumed + 1U] == (uint8_t)(adler >> 16U) &&
                 packed[consumed + 2U] == (uint8_t)(adler >> 8U);
        }
    } else if (codec == AVRO_BZIP2) {
        size_t consumed = 0U;
        ok = xx_bzip2_unpack_memory_to_device_ex(packed, (size_t)size, &destination, &consumed, pd) && consumed == (size_t)size;
    } else if (codec == AVRO_ZSTANDARD) {
        size_t capacity = 1024U;
        uint8_t *buffer = NULL;
        while (capacity <= AVRO_MAX_BLOCK && !(pd && xx_pd_is_stopped(pd))) {
            size_t written = 0U;
            bool needs_more_output = false;
            uint8_t *next = (uint8_t *)xx_mem_realloc(buffer, capacity);
            if (!next) break;
            buffer = next;
            if (xx_zstd_decompress_memory_bounded_ex(packed, (size_t)size, buffer, capacity, &written, &needs_more_output)) {
                sink.data = buffer;
                sink.size = written;
                sink.capacity = capacity;
                buffer = NULL;
                ok = true;
                break;
            }
            if (!needs_more_output || capacity == AVRO_MAX_BLOCK) break;
            capacity = capacity > AVRO_MAX_BLOCK / 2U ? AVRO_MAX_BLOCK : capacity * 2U;
        }
        if (buffer) xx_mem_free(buffer);
    }
    if (ok && !(pd && xx_pd_is_stopped(pd))) {
        *output = sink.data;
        *output_size = sink.size;
        sink.data = NULL;
    } else ok = false;
    if (sink.data) xx_mem_free(sink.data);
done:
    if (packed) xx_mem_free(packed);
    return ok;
}

static bool avro_long(Abstractformat *f, int64_t *at, int64_t *out)
{
    unsigned i;
    uint64_t raw = 0;
    uint8_t c;
    for (i = 0; i < 10; ++i) {
        if (!pm_read(f, (*at)++, &c, 1) || (i == 9 && c > 1)) return false;
        raw |= (uint64_t)(c & 127) << (7 * i);
        if (!(c & 128)) {
            *out = (raw & 1) ? -(int64_t)(raw >> 1) - 1 : (int64_t)(raw >> 1);
            return true;
        }
    }
    return false;
}
static bool avro_schema(Abstractformat *f, int64_t at, int64_t size, xx_pd_struct *pd)
{
    uint8_t *data;
    xx_json j;
    bool ok;
    size_t i;
    const xx_var *budget = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (size <= 0 || size > 1024 * 1024 || (budget && (uint64_t)size * 2U + 256U > xx_var_get_u64(budget))) return false;
    data = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!data) return false;
    ok = pm_read(f, at, data, (size_t)size) && bounded_utf8(data, (size_t)size, pd);
    xx_json_init(&j, data, (size_t)size);
    if (ok) {
        xx_json_type_t type = xx_json_peek(&j);
        ok = (type == XX_JSON_TYPE_STRING || type == XX_JSON_TYPE_OBJECT || type == XX_JSON_TYPE_ARRAY) && xx_json_skip(&j);
    }
    for (i = j.position; ok && i < (size_t)size; ++i)
        if (data[i] != ' ' && data[i] != '\r' && data[i] != '\n' && data[i] != '\t') ok = false;
    xx_mem_free(data);
    return ok;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[4], sync[16], check[16];
    int64_t at = 4, left = pm_available(f), count;
    uint32_t entries = 0, blocks = 0;
    bool schema = false, codec_seen = false;
    enum avro_codec codec = AVRO_NULL;
    uint64_t decoded_total = 0U;
    if (!pm_read(f, 0, h, 4) || xx_rt_memcmp(h, "Obj\1", 4)) return false;
    for (;;) {
        int64_t block_end = -1, n, i;
        if (!avro_long(f, &at, &count)) return false;
        if (!count) break;
        if (count == INT64_MIN) return false;
        if (count < 0) {
            count = -count;
            if (!avro_long(f, &at, &n) || n < 0 || n > left - at) return false;
            block_end = at + n;
        }
        if (count > 1024 || entries + (uint64_t)count > 1024) return false;
        for (i = 0; i < count; ++i) {
            char key[129], label[160];
            int64_t key_size, size;
            bool is_schema, is_codec;
            if ((pd && xx_pd_is_stopped(pd)) || !avro_long(f, &at, &key_size) || key_size <= 0 || key_size > 128 || !pm_read(f, at, key, (size_t)key_size)) return false;
            key[key_size] = 0;
            if (xx_rt_strlen(key) != (size_t)key_size || !bounded_utf8((const uint8_t *)key, (size_t)key_size, pd)) return false;
            at += key_size;
            if (!avro_long(f, &at, &size) || size < 0 || size > left - at) return false;
            is_schema = !xx_rt_strcmp(key, "avro.schema");
            is_codec = !xx_rt_strcmp(key, "avro.codec");
            if (is_schema) {
                if (schema || !avro_schema(f, at, size, pd)) return false;
                schema = true;
            }
            if (is_codec) {
                char value[9];
                if (codec_seen || size < 4 || size > 9 || !pm_read(f, at, value, (size_t)size)) return false;
                codec_seen = true;
                if (size == 4 && !xx_rt_memcmp(value, "null", 4)) codec = AVRO_NULL;
                else if (size == 7 && !xx_rt_memcmp(value, "deflate", 7)) codec = AVRO_DEFLATE;
                else if (size == 5 && !xx_rt_memcmp(value, "bzip2", 5)) codec = AVRO_BZIP2;
                else if (size == 9 && !xx_rt_memcmp(value, "zstandard", 9)) codec = AVRO_ZSTANDARD;
                else return false;
            }
            xx_rt_snprintf(label, sizeof(label), "metadata-%s.bin", key);
            if (!pm_add(f, s, label, at, size)) return false;
            at += size;
            ++entries;
        }
        if (block_end >= 0 && at != block_end) return false;
    }
    if (!schema || !pm_read(f, at, sync, 16)) {
        return false;
    }
    at += 16;
    while (at < left) {
        int64_t size;
        char label[80];
        if ((pd && xx_pd_is_stopped(pd)) || !avro_long(f, &at, &count) || count <= 0 || !avro_long(f, &at, &size) || size < 0 || size > left - at ||
            left - at - size < 16)
            return false;
        if (!pm_read(f, at + size, check, 16) || xx_rt_memcmp(check, sync, 16)) return false;
        xx_rt_snprintf(label, sizeof(label), "block-%u-objects-%llu.avrodata", (unsigned)blocks, (unsigned long long)count);
        if (codec == AVRO_NULL) {
            if (!pm_add(f, s, label, at, size)) return false;
        } else {
            uint8_t *decoded = NULL;
            size_t decoded_size = 0U;
            if (!avro_decode_block(f, at, size, codec, &decoded, &decoded_size, pd)) return false;
            if ((uint64_t)decoded_size > AVRO_MAX_TOTAL_DECODED - decoded_total) {
                if (decoded) xx_mem_free(decoded);
                return false;
            }
            if (!pm_add(f, s, label, at, size)) {
                if (decoded) xx_mem_free(decoded);
                return false;
            }
            s->items[s->count - 1U].memory = decoded;
            s->items[s->count - 1U].size = (int64_t)decoded_size;
            decoded_total += (uint64_t)decoded_size;
        }
        at += size + 16;
        ++blocks;
    }
    s->size = at;
    return blocks != 0;
}

void xx_avro_object_init(xx_avro_object *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AVRO_OBJECT, "avro");
    }
}
xx_avro_object *xx_avro_object_create(xx_io_device *d, int64_t b)
{
    xx_avro_object *r = (xx_avro_object *)xx_mem_alloc(sizeof(*r));
    if (r) xx_avro_object_init(r, d, b);
    return r;
}
void xx_avro_object_destroy(xx_avro_object *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_avro_object_free(xx_avro_object *r)
{
    if (r) {
        xx_avro_object_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_avro_object_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_avro_object_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
