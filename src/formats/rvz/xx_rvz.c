/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native GameCube RVZ v1 reader. The on-disk validation and junk-sector
 * reconstruction follow the local XArchive XRVZArchive reader.
 */
#include "xxfclib/formats/rvz/xx_rvz.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#define RVZ_FILE_HEADER 0x48U
#define RVZ_MIN_HEADER 0xd5U
#define RVZ_MAX_HEADER 0x1000U
#define RVZ_DISC_HEADER 0x80U
#define RVZ_RAW_ENTRY 0x18U
#define RVZ_GROUP_ENTRY 0x0cU
#define RVZ_SECTOR 0x8000U
#define RVZ_MAX_ISO INT64_C(0x57058000)
#define RVZ_MAX_CHUNK (2U * 1024U * 1024U)
#define RVZ_MAX_PACKED (8U * 1024U * 1024U)
#define RVZ_MAX_COMPRESSED (9U * 1024U * 1024U)
#define RVZ_MAX_TABLE (16U * 1024U * 1024U)
#define RVZ_MAX_RAW 8192U
#define RVZ_MAX_GROUPS 65536U
#define RVZ_HIGH_BIT UINT32_C(0x80000000)

typedef struct rvz_raw {
    int64_t offset, size, aligned, logical, skip, output;
    uint32_t first_group, group_count;
} rvz_raw;
typedef struct rvz_group {
    int64_t data_offset;
    uint32_t data_size, packed_size;
    bool compressed;
} rvz_group;
typedef struct rvz_context {
    rvz_raw *raw;
    rvz_group *groups;
    uint32_t raw_count, group_count, chunk_size, compression;
    int64_t source_size, iso_size;
    uint8_t disc_header[RVZ_DISC_HEADER];
    char *name;
} rvz_context;

static bool rvz_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static uint32_t rvz_be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24U | (uint32_t)p[1] << 16U |
           (uint32_t)p[2] << 8U | (uint32_t)p[3];
}
static bool rvz_be64_checked(const uint8_t *p, int64_t *result) {
    uint64_t value = (uint64_t)rvz_be32(p) << 32U | rvz_be32(p + 4U);
    if (value > INT64_MAX) return false;
    *result = (int64_t)value;
    return true;
}
static bool rvz_add(int64_t left, int64_t right, int64_t *result) {
    if (left < 0 || right < 0 || right > INT64_MAX - left) return false;
    *result = left + right;
    return true;
}
static bool rvz_range(int64_t total, int64_t at, int64_t length) {
    return at >= 0 && length >= 0 && at <= total && length <= total - at;
}
static bool rvz_overlap(int64_t a, int64_t an, int64_t b, int64_t bn) {
    return an > 0 && bn > 0 && a < b + bn && b < a + an;
}
static bool rvz_read(Abstractformat *format, int64_t at, void *buffer,
                     size_t length, xx_pd_struct *pd) {
    size_t done = 0U;
    int64_t total;
    if (!format || !format->device || at < 0 || rvz_stopped(pd)) return false;
    total = xx_io_total_size(format->device);
    if (!rvz_range(total - format->base_address, at, (int64_t)length) ||
        xx_io_seek64(format->device, format->base_address + at,
                     XX_RT_SEEK_SET) != 0) return false;
    while (done < length) {
        size_t take = length - done;
        ssize_t got;
        if (rvz_stopped(pd)) return false;
        if (take > 65536U) take = 65536U;
        got = xx_io_read(format->device, (uint8_t *)buffer + done, take);
        if (got <= 0 || (size_t)got > take) return false;
        done += (size_t)got;
    }
    return true;
}
static char *rvz_output_name(xx_io_device *device) {
    const char *path = xx_io_source_path(device), *last, *cursor, *dot;
    size_t length, i;
    char *name;
    if (!path || !*path) return xx_str_dup("gamecube-disc.iso");
    last = path;
    for (cursor = path; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\') last = cursor + 1;
    dot = NULL;
    for (cursor = last; *cursor; ++cursor) if (*cursor == '.') dot = cursor;
    length = dot && dot > last ? (size_t)(dot - last) : xx_str_len(last);
    if (!length || length > 240U) return xx_str_dup("gamecube-disc.iso");
    name = (char *)xx_mem_alloc(length + 5U);
    if (!name) return NULL;
    for (i = 0U; i < length; ++i) {
        unsigned char c = (unsigned char)last[i];
        name[i] = (char)(c < 0x20U || c == 0x7fU || c == '/' || c == '\\' ||
                         c == ':' || c == '<' || c == '>' || c == '"' ||
                         c == '|' || c == '?' || c == '*' ? '_' : c);
    }
    if (name[length - 1U] == '.' || name[length - 1U] == ' ')
        name[length - 1U] = '_';
    xx_rt_memcpy(name + length, ".iso", 5U);
    return name;
}
static void rvz_context_free(void *pointer) {
    rvz_context *context = (rvz_context *)pointer;
    if (!context) return;
    xx_mem_free(context->raw);
    xx_mem_free(context->groups);
    xx_mem_free(context->name);
    xx_mem_free(context);
}
static bool rvz_table(Abstractformat *format, int64_t at, uint32_t packed_size,
                      size_t decoded_size, uint32_t compression,
                      uint8_t **out, xx_pd_struct *pd) {
    uint8_t *packed = NULL, *decoded = NULL;
    size_t written = 0U;
    bool ok = false;
    *out = NULL;
    packed = (uint8_t *)xx_mem_alloc(packed_size);
    decoded = (uint8_t *)xx_mem_alloc(decoded_size);
    if (!packed || !decoded ||
        !rvz_read(format, at, packed, packed_size, pd)) goto done;
    if (compression == 0U) {
        if (packed_size != decoded_size) goto done;
        xx_rt_memcpy(decoded, packed, decoded_size);
    } else if (compression == 5U) {
        if (!xx_zstd_decompress_memory_bounded(packed, packed_size,
                                               decoded, decoded_size,
                                               &written) ||
            written != decoded_size) goto done;
    } else goto done;
    ok = !rvz_stopped(pd);
done:
    xx_mem_free(packed);
    if (ok) *out = decoded;
    else xx_mem_free(decoded);
    return ok;
}

static rvz_context *rvz_parse(Abstractformat *format, xx_pd_struct *pd) {
    uint8_t file_header[RVZ_FILE_HEADER], header[RVZ_MAX_HEADER], digest[20];
    uint8_t *raw_table = NULL, *group_table = NULL;
    rvz_context *context = NULL;
    int64_t total, available, iso_size, declared, header_end;
    int64_t raw_at, group_at, expected_output;
    uint64_t expected_group;
    uint32_t version, compatible, header_size, disc_type, compression;
    uint32_t chunk, partitions, raw_count, group_count;
    uint32_t raw_size, group_size, i;
    bool ok = false;
    if (!format || !format->device || format->base_address < 0 ||
        rvz_stopped(pd)) return NULL;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return NULL;
    available = total - format->base_address;
    if (available < RVZ_FILE_HEADER ||
        !rvz_read(format, 0, file_header, sizeof(file_header), pd) ||
        xx_rt_memcmp(file_header, "RVZ\x01", 4U) != 0) return NULL;
    version = rvz_be32(file_header + 4U);
    compatible = rvz_be32(file_header + 8U);
    header_size = rvz_be32(file_header + 12U);
    if (version != UINT32_C(0x01000000) ||
        compatible > UINT32_C(0x01000000) ||
        header_size < RVZ_MIN_HEADER || header_size > RVZ_MAX_HEADER ||
        !rvz_be64_checked(file_header + 0x24U, &iso_size) ||
        !rvz_be64_checked(file_header + 0x2cU, &declared) ||
        declared != available || iso_size < RVZ_DISC_HEADER ||
        iso_size > RVZ_MAX_ISO ||
        !xx_sha1_memory(file_header, 0x34U, digest) ||
        xx_rt_memcmp(digest, file_header + 0x34U, 20U) != 0 ||
        !rvz_add(RVZ_FILE_HEADER, header_size, &header_end) ||
        !rvz_range(available, RVZ_FILE_HEADER, header_size) ||
        !rvz_read(format, RVZ_FILE_HEADER, header, header_size, pd) ||
        !xx_sha1_memory(header, header_size, digest) ||
        xx_rt_memcmp(digest, file_header + 0x10U, 20U) != 0)
        return NULL;
    disc_type = rvz_be32(header);
    compression = rvz_be32(header + 4U);
    chunk = rvz_be32(header + 12U);
    partitions = rvz_be32(header + 0x90U);
    raw_count = rvz_be32(header + 0xb4U);
    raw_size = rvz_be32(header + 0xc0U);
    group_count = rvz_be32(header + 0xc4U);
    group_size = rvz_be32(header + 0xd0U);
    if (disc_type != 1U || partitions != 0U ||
        (compression != 0U && compression != 5U) ||
        header[0xd4U] != 0U ||
        xx_rt_memcmp(header + 0x2cU, "\xc2\x33\x9f\x3d", 4U) != 0 ||
        chunk < RVZ_SECTOR || chunk > RVZ_MAX_CHUNK ||
        (chunk < RVZ_MAX_CHUNK ? (chunk & (chunk - 1U)) != 0U
                               : (chunk % RVZ_MAX_CHUNK) != 0U) ||
        !rvz_be64_checked(header + 0xb8U, &raw_at) ||
        !rvz_be64_checked(header + 0xc8U, &group_at) ||
        raw_count == 0U || raw_count > RVZ_MAX_RAW ||
        group_count == 0U || group_count > RVZ_MAX_GROUPS ||
        raw_size == 0U || raw_size > RVZ_MAX_TABLE ||
        group_size == 0U || group_size > RVZ_MAX_TABLE ||
        !rvz_range(available, raw_at, raw_size) ||
        !rvz_range(available, group_at, group_size) ||
        raw_at < header_end || group_at < header_end ||
        rvz_overlap(raw_at, raw_size, group_at, group_size) ||
        (uint64_t)raw_count * RVZ_RAW_ENTRY > RVZ_MAX_PACKED ||
        (uint64_t)group_count * RVZ_GROUP_ENTRY > RVZ_MAX_PACKED)
        return NULL;
    if (!rvz_table(format, raw_at, raw_size,
                   (size_t)raw_count * RVZ_RAW_ENTRY,
                   compression, &raw_table, pd) ||
        !rvz_table(format, group_at, group_size,
                   (size_t)group_count * RVZ_GROUP_ENTRY,
                   compression, &group_table, pd)) goto done;
    context = (rvz_context *)xx_mem_calloc(1U, sizeof(*context));
    if (!context) goto done;
    context->raw = (rvz_raw *)xx_mem_calloc(raw_count, sizeof(*context->raw));
    context->groups = (rvz_group *)xx_mem_calloc(group_count,
                                                 sizeof(*context->groups));
    if (!context->raw || !context->groups) goto done;
    context->raw_count = raw_count;
    context->group_count = group_count;
    context->chunk_size = chunk;
    context->compression = compression;
    context->source_size = available;
    context->iso_size = iso_size;
    xx_rt_memcpy(context->disc_header, header + 0x10U, RVZ_DISC_HEADER);
    expected_output = RVZ_DISC_HEADER;
    expected_group = 0U;
    for (i = 0U; i < raw_count; ++i) {
        const uint8_t *record = raw_table + (size_t)i * RVZ_RAW_ENTRY;
        rvz_raw *raw = &context->raw[i];
        int64_t end, required;
        if (rvz_stopped(pd) ||
            !rvz_be64_checked(record, &raw->offset) ||
            !rvz_be64_checked(record + 8U, &raw->size) ||
            raw->size < 1 || !rvz_add(raw->offset, raw->size, &end) ||
            end > iso_size || raw->offset > expected_output ||
            (i > 0U && raw->offset != expected_output) ||
            end <= expected_output) goto done;
        raw->aligned = raw->offset - (raw->offset % RVZ_SECTOR);
        raw->logical = end - raw->aligned;
        raw->skip = expected_output - raw->aligned;
        raw->output = end - expected_output;
        raw->first_group = rvz_be32(record + 16U);
        raw->group_count = rvz_be32(record + 20U);
        required = (raw->logical + chunk - 1U) / chunk;
        if (raw->logical < 1 || raw->skip < 0 ||
            raw->skip >= raw->logical || raw->output < 1 || required < 1 ||
            raw->first_group != expected_group ||
            raw->group_count != (uint64_t)required ||
            expected_group > group_count ||
            raw->group_count > group_count - expected_group) goto done;
        expected_group += raw->group_count;
        expected_output = end;
    }
    if (expected_output != iso_size || expected_group != group_count) goto done;
    for (i = 0U; i < group_count; ++i) {
        const uint8_t *record = group_table + (size_t)i * RVZ_GROUP_ENTRY;
        rvz_group *group = &context->groups[i];
        uint32_t word = rvz_be32(record + 4U);
        group->data_offset = (int64_t)rvz_be32(record) * 4;
        group->data_size = word & ~RVZ_HIGH_BIT;
        group->packed_size = rvz_be32(record + 8U);
        group->compressed = (word & RVZ_HIGH_BIT) != 0U;
    }
    for (i = 0U; i < raw_count; ++i) {
        const rvz_raw *raw = &context->raw[i];
        uint32_t j;
        for (j = 0U; j < raw->group_count; ++j) {
            const rvz_group *group = &context->groups[raw->first_group + j];
            int64_t group_start = (int64_t)j * chunk;
            int64_t expected = raw->logical - group_start;
            int64_t intermediate;
            if (expected > chunk) expected = chunk;
            if (expected < 1) goto done;
            if (!group->data_size) {
                if (group->packed_size != 0U) goto done;
                continue;
            }
            intermediate = group->packed_size ? group->packed_size : expected;
            if (intermediate < 1 || intermediate > RVZ_MAX_PACKED ||
                (group->compressed && compression != 5U) ||
                (!group->compressed && group->data_size != intermediate) ||
                (group->compressed && group->data_size > RVZ_MAX_COMPRESSED) ||
                !rvz_range(available, group->data_offset, group->data_size) ||
                rvz_overlap(group->data_offset, group->data_size, 0, header_end) ||
                rvz_overlap(group->data_offset, group->data_size,
                            raw_at, raw_size) ||
                rvz_overlap(group->data_offset, group->data_size,
                            group_at, group_size)) goto done;
        }
    }
    context->name = rvz_output_name(format->device);
    ok = context->name && !rvz_stopped(pd);
done:
    xx_mem_free(raw_table);
    xx_mem_free(group_table);
    if (!ok) { rvz_context_free(context); context = NULL; }
    return context;
}
static rvz_context *rvz_open(Abstractformat *format, xx_pd_struct *pd) {
    int64_t saved;
    rvz_context *context;
    if (!format || !format->device) return NULL;
    saved = xx_io_tell(format->device);
    if (saved < 0) return NULL;
    context = rvz_parse(format, pd);
    if (xx_io_seek64(format->device, saved, XX_RT_SEEK_SET) != 0) {
        rvz_context_free(context); return NULL;
    }
    return context;
}

typedef struct rvz_junk {
    uint32_t words[521];
    uint32_t word, byte;
} rvz_junk;
static void rvz_junk_advance(rvz_junk *junk) {
    uint32_t i;
    for (i = 0U; i < 32U; ++i)
        junk->words[i] ^= junk->words[i + 521U - 32U];
    for (i = 32U; i < 521U; ++i)
        junk->words[i] ^= junk->words[i - 32U];
}
static uint8_t rvz_junk_next(rvz_junk *junk) {
    uint32_t value = junk->words[junk->word];
    uint8_t result = (uint8_t)(junk->byte == 0U ? value >> 24U :
                               junk->byte == 1U ? value >> 18U :
                               junk->byte == 2U ? value >> 8U : value);
    if (++junk->byte == 4U) {
        junk->byte = 0U;
        if (++junk->word == 521U) {
            rvz_junk_advance(junk);
            junk->word = 0U;
        }
    }
    return result;
}
static bool rvz_junk_fill(const uint8_t *seed, int64_t absolute,
                          uint8_t *output, size_t length, xx_pd_struct *pd) {
    rvz_junk junk = {0};
    size_t i, skip;
    if (!seed || !output || absolute < 0) return false;
    for (i = 0U; i < 17U; ++i) junk.words[i] = rvz_be32(seed + i * 4U);
    for (i = 17U; i < 521U; ++i)
        junk.words[i] = (junk.words[i - 17U] << 23U) ^
                        (junk.words[i - 16U] >> 9U) ^ junk.words[i - 1U];
    for (i = 0U; i < 4U; ++i) rvz_junk_advance(&junk);
    skip = (size_t)(absolute % RVZ_SECTOR);
    for (i = 0U; i < skip; ++i) (void)rvz_junk_next(&junk);
    for (i = 0U; i < length; ++i) {
        if ((i & 0xffffU) == 0U && rvz_stopped(pd)) return false;
        output[i] = rvz_junk_next(&junk);
    }
    return !rvz_stopped(pd);
}
static bool rvz_decode_packed(const uint8_t *packed, size_t packed_size,
                              int64_t absolute, uint8_t *output,
                              size_t output_size, xx_pd_struct *pd) {
    size_t in = 0U, out = 0U;
    while (in < packed_size && !rvz_stopped(pd)) {
        uint32_t word, amount;
        if (packed_size - in < 4U) return false;
        word = rvz_be32(packed + in);
        in += 4U;
        amount = word & ~RVZ_HIGH_BIT;
        if (!amount || amount > output_size - out) return false;
        if (word & RVZ_HIGH_BIT) {
            if (packed_size - in < 68U ||
                !rvz_junk_fill(packed + in, absolute + (int64_t)out,
                               output + out, amount, pd)) return false;
            in += 68U;
        } else {
            if (packed_size - in < amount) return false;
            xx_rt_memcpy(output + out, packed + in, amount);
            in += amount;
        }
        out += amount;
    }
    return !rvz_stopped(pd) && in == packed_size && out == output_size;
}
static bool rvz_decode_group(Abstractformat *format,
                              const rvz_context *context,
                              const rvz_group *group, size_t expected,
                              int64_t absolute, uint8_t *output,
                              xx_pd_struct *pd) {
    uint8_t *stored = NULL, *intermediate = NULL;
    size_t intermediate_size, written = 0U;
    bool ok = false;
    if (!expected || expected > RVZ_MAX_CHUNK || !output || !group) return false;
    if (!group->data_size) {
        xx_mem_zero(output, expected);
        return !rvz_stopped(pd);
    }
    intermediate_size = group->packed_size ? group->packed_size : expected;
    stored = (uint8_t *)xx_mem_alloc(group->data_size);
    intermediate = (uint8_t *)xx_mem_alloc(intermediate_size);
    if (!stored || !intermediate ||
        !rvz_read(format, group->data_offset, stored, group->data_size, pd))
        goto done;
    if (group->compressed) {
        if (context->compression != 5U ||
            !xx_zstd_decompress_memory_bounded(stored, group->data_size,
                                               intermediate, intermediate_size,
                                               &written) ||
            written != intermediate_size) goto done;
    } else {
        if (group->data_size != intermediate_size) goto done;
        xx_rt_memcpy(intermediate, stored, intermediate_size);
    }
    if (group->packed_size) {
        ok = rvz_decode_packed(intermediate, intermediate_size, absolute,
                               output, expected, pd);
    } else {
        xx_rt_memcpy(output, intermediate, expected);
        ok = !rvz_stopped(pd);
    }
done:
    xx_mem_free(intermediate);
    xx_mem_free(stored);
    return ok;
}
static xx_io_device *rvz_stage(const char *target, char **stage_name) {
    char *parent = xx_str_dup(target);
    size_t i, cut = 0U;
    unsigned attempt;
    if (!parent) return NULL;
    *stage_name = NULL;
    for (i = 0U; parent[i]; ++i)
        if (parent[i] == '/' || parent[i] == '\\') cut = i + 1U;
    parent[cut] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[40];
        char *candidate;
        xx_io_device *output;
        (void)xx_rt_snprintf(suffix, sizeof(suffix), ".xx_rvz.tmp.%u", attempt);
        candidate = xx_str_concat(parent, suffix);
        if (!candidate) break;
        output = xx_io_file_open(candidate, "wbx");
        if (output) {
            *stage_name = candidate;
            xx_str_free(parent);
            return output;
        }
        xx_str_free(candidate);
    }
    xx_str_free(parent);
    return NULL;
}
static bool rvz_write(xx_io_device *output, const uint8_t *bytes,
                      size_t length, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!output) return true;
    while (done < length) {
        size_t take = length - done;
        ssize_t sent;
        if (rvz_stopped(pd)) return false;
        if (take > 65536U) take = 65536U;
        sent = xx_io_write(output, bytes + done, take);
        if (sent <= 0 || (size_t)sent > take) return false;
        done += (size_t)sent;
    }
    return true;
}
static bool rvz_handle(Abstractformat *format, xx_pd_struct *pd) {
    rvz_context *context = rvz_open(format, pd);
    if (!context) return false;
    format->format_size = context->source_size;
    format->number_of_archive_records = 1U;
    format->overlay_offset = format->base_address + context->source_size;
    format->overlay_size = xx_io_total_size(format->device) - format->overlay_offset;
    format->is_valid = true;
    format->base_info_handled = true;
    rvz_context_free(context);
    return true;
}
static int64_t rvz_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled || rvz_handle(format, pd))
        ? format->format_size : 0;
}
static uint64_t rvz_count(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled || rvz_handle(format, pd))
        ? 1U : 0U;
}
static bool rvz_set_record(xx_archive_record_state *state) {
    rvz_context *context = (rvz_context *)state->internal_state;
    xx_archive_record *record = &state->current_record;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->data_offset = state->format->base_address;
    record->compressed_size = context->source_size;
    return xx_archive_record_set_original_name(record, context->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)context->source_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)context->iso_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          context->compression) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}
static xx_archive_record_state *rvz_create_records(Abstractformat *format,
                                                    const xx_list_s *options,
                                                    xx_pd_struct *pd) {
    rvz_context *context = rvz_open(format, pd);
    xx_archive_record_state *state;
    size_t i;
    if (!context) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { rvz_context_free(context); return NULL; }
    xx_archive_record_state_init(state, format);
    state->internal_state = context;
    state->free_internal = rvz_context_free;
    state->total_records = 1;
    for (i = 0U; options && i < options->count; ++i) {
        const xx_meta *source = (const xx_meta *)xx_list_at(options, i);
        xx_meta copy;
        if (!source) continue;
        xx_meta_init(&copy, source->meta_id);
        if (!xx_var_copy(&copy.var, &source->var) ||
            !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy);
            xx_archive_record_state_free(state);
            return NULL;
        }
    }
    state->has_record = rvz_set_record(state);
    if (!state->has_record) { xx_archive_record_state_free(state); return NULL; }
    return state;
}
static const xx_archive_record *rvz_current(Abstractformat *format,
                                            xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
        ? &state->current_record : NULL;
}
static bool rvz_next(Abstractformat *format, xx_archive_record_state *state,
                     xx_pd_struct *pd) {
    (void)pd;
    if (!format || !state || state->format != format || !state->has_record)
        return false;
    state->has_record = false;
    ++state->current_index;
    return false;
}
static bool rvz_unpack(Abstractformat *format, xx_archive_record_state *state,
                       xx_pd_struct *pd) {
    rvz_context *context;
    const xx_var *option;
    const char *base = NULL;
    char *owned = NULL, *target = NULL, *stage = NULL;
    xx_io_device *output = NULL;
    uint8_t *group_bytes = NULL;
    int64_t saved = -1, written = 0;
    uint32_t i;
    bool ok = false, overwrite = false;
    if (!format || !state || state->format != format || !state->has_record ||
        rvz_stopped(pd)) return false;
    context = (rvz_context *)state->internal_state;
    if (!context) return false;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                                XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)context->iso_size > xx_var_get_u64(option)) return false;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                                XX_META_ID_OPT_UNPACK_PATH);
    if (option) {
        if (option->type == XX_VAR_TYPE_STRING ||
            option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
        else if (option->type == XX_VAR_TYPE_WSTRING ||
                 option->type == XX_VAR_TYPE_WSTRING_VIEW)
            base = owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        if (!base) goto done;
        target = *base ? xx_str_concat3(base, "/", context->name)
                       : xx_str_dup(context->name);
        if (!target || !xx_store_create_dirs_a(target, false)) goto done;
        option = xx_format_resolve_extra_parameter(format, &state->options,
                                                    XX_META_ID_OPT_OVERWRITE);
        overwrite = option && xx_var_get_bool(option);
        if ((!overwrite && xx_io_file_exists_a(target)) || rvz_stopped(pd))
            goto done;
        output = rvz_stage(target, &stage);
        if (!output) goto done;
    }
    saved = xx_io_tell(format->device);
    if (saved < 0) goto done;
    group_bytes = (uint8_t *)xx_mem_alloc(context->chunk_size);
    if (!group_bytes) goto restore;
    ok = rvz_write(output, context->disc_header, RVZ_DISC_HEADER, pd);
    written = RVZ_DISC_HEADER;
    for (i = 0U; i < context->raw_count && ok; ++i) {
        const rvz_raw *raw = &context->raw[i];
        int64_t skip = raw->skip, left = raw->output;
        uint32_t j;
        for (j = 0U; j < raw->group_count && ok; ++j) {
            const rvz_group *group = &context->groups[raw->first_group + j];
            int64_t group_offset = (int64_t)j * context->chunk_size;
            int64_t absolute = raw->aligned + group_offset;
            int64_t expected = raw->logical - group_offset;
            int64_t ignored, available, take;
            if (expected > context->chunk_size) expected = context->chunk_size;
            if (expected < 1 || !rvz_range(context->iso_size, absolute, expected) ||
                !rvz_decode_group(format, context, group, (size_t)expected,
                                  absolute, group_bytes, pd)) { ok = false; break; }
            ignored = skip < expected ? skip : expected;
            skip -= ignored;
            available = expected - ignored;
            take = left < available ? left : available;
            if (take > 0 && !rvz_write(output, group_bytes + ignored,
                                       (size_t)take, pd)) { ok = false; break; }
            left -= take;
            written += take;
        }
        if (skip || left) ok = false;
    }
    if (written != context->iso_size || rvz_stopped(pd)) ok = false;
restore:
    if (xx_io_seek64(format->device, saved, XX_RT_SEEK_SET) != 0) ok = false;
done:
    xx_mem_free(group_bytes);
    if (output && xx_io_close(output) != 0) ok = false;
    if (ok && stage) ok = xx_io_file_replace_a(stage, target, overwrite);
    if (stage) {
        if (!ok) (void)xx_io_file_remove_a(stage);
        xx_str_free(stage);
    }
    xx_str_free(target);
    xx_str_free(owned);
    return ok;
}
static void rvz_free_records(Abstractformat *format,
                             xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
void xx_rvz_destroy(xx_rvz *reader) {
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}
void xx_rvz_init(xx_rvz *reader, xx_io_device *device, int64_t base_address) {
    Abstractformat *format;
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    format = &reader->format;
    xx_format_init(format, device, base_address);
    format->endian = XX_ENDIAN_BIG;
    format->file_type = XX_FILE_TYPE_RVZ;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    xx_format_set_extension(format, "rvz");
    xx_format_set_mime_type(format, "application/x-dolphin-rvz");
    format->check_is_valid = xx_rvz_check_is_valid;
    format->handle_base_info = rvz_handle;
    format->get_format_size = rvz_size;
    format->get_number_of_archive_records = rvz_count;
    format->create_archive_records_reading = rvz_create_records;
    format->get_current_archive_record = rvz_current;
    format->archive_record_move_to_next = rvz_next;
    format->unpack_current_archive_record = rvz_unpack;
    format->free_archive_records_reading = rvz_free_records;
    format->destroy = (void (*)(Abstractformat *))xx_rvz_destroy;
}
xx_rvz *xx_rvz_create(xx_io_device *device, int64_t base_address) {
    xx_rvz *reader = (xx_rvz *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_rvz_init(reader, device, base_address);
    return reader;
}
void xx_rvz_free(xx_rvz *reader) {
    if (reader) { xx_rvz_destroy(reader); xx_mem_free(reader); }
}
bool xx_rvz_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    rvz_context *context;
    if (!format || format->file_type != XX_FILE_TYPE_RVZ) return false;
    context = rvz_open(format, pd);
    if (!context) return false;
    rvz_context_free(context);
    return true;
}
