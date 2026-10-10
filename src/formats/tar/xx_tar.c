/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/formats/tar/xx_tar.h"
#include "xx_tar_defs.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

/* GNU sparse maps use PAX metadata, a packed member prefix, or old GNU
 * extension blocks. These limits bound parser memory and extraction work. */
#define XX_TAR_MAX_SPARSE_EXTENTS 65536U
#define XX_TAR_MAX_SPARSE_TOTAL_EXTENTS 262144U
#define XX_TAR_MAX_SPARSE_MAP_BYTES (1024U * 1024U)
#define XX_TAR_MAX_SPARSE_LOGICAL_BYTES (UINT64_C(16) * 1024U * 1024U * 1024U)
#define XX_TAR_SPARSE_COPY_BYTES 65536U

typedef struct xx_tar_sparse_extent_s {
    uint64_t offset;
    uint64_t size;
    int64_t packed_offset;
} xx_tar_sparse_extent;

typedef struct xx_tar_member_s {
    int64_t header_offset;
    int64_t data_offset;
    int64_t data_size;
    int64_t padded_size;
    uint64_t mode;
    uint64_t uid;
    uint64_t gid;
    uint64_t mtime;
    char typeflag;
    char *name;
    char *linkname;
    xx_tar_sparse_extent *sparse_extents;
    size_t sparse_count;
    uint64_t logical_size;
    bool sparse;
    bool metadata;
    bool directory;
    bool regular;
} xx_tar_member;

typedef struct xx_tar_private_s {
    xx_tar_member *members;
    size_t count;
    size_t capacity;
    uint64_t visible_count;
    size_t sparse_extent_total;
    int64_t end_marker_offset;
    int64_t archive_end;
} xx_tar_private;

typedef struct xx_tar_pax_s {
    char *path;
    char *linkpath;
    char *sparse_name;
    uint64_t size;
    uint64_t mtime;
    uint64_t sparse_major;
    uint64_t sparse_minor;
    uint64_t sparse_realsize;
    uint64_t sparse_size;
    uint64_t sparse_numblocks;
    uint64_t sparse_pending_offset;
    xx_tar_sparse_extent *sparse_v0_extents;
    size_t sparse_v0_count;
    size_t sparse_v0_capacity;
    bool path_present;
    bool linkpath_present;
    bool size_present;
    bool mtime_present;
    bool has_size;
    bool has_mtime;
    bool sparse_keyword_seen;
    bool sparse_major_present;
    bool sparse_minor_present;
    bool sparse_name_present;
    bool sparse_realsize_present;
    bool sparse_size_present;
    bool sparse_numblocks_present;
    bool sparse_map_present;
    bool sparse_pairs_present;
    bool sparse_pending;
} xx_tar_pax;

typedef struct xx_tar_archive_stream_s {
    size_t member_index;
} xx_tar_archive_stream;

typedef struct xx_tar_write_stream_s {
    int64_t current_offset;
    uint64_t count;
    bool finalized;
    bool failed;
} xx_tar_write_stream;

typedef struct xx_tar_ds_stream_s {
    xx_data_struct *items;
    size_t count;
} xx_tar_ds_stream;

typedef struct xx_tar_record_stream_s {
    const xx_data_struct_field_desc *fields;
    size_t count;
} xx_tar_record_stream;

static void xx_tar_vtable_destroy(Abstractformat *self);
static void xx_tar_normalize_name(char *name);

static bool xx_tar_read_exact_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t transfer_capacity = xx_get_file_buffer_size();
    size_t done = 0U;
    uint8_t *bytes = (uint8_t *)buffer;
    if (!device || (!buffer && size != 0U) || offset < 0 || (uint64_t)offset > (uint64_t)LONG_MAX || xx_io_seek(device, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        size_t request = size - done;
        ssize_t amount;
        if (request > transfer_capacity) request = transfer_capacity;
        amount = xx_io_read(device, bytes + done, request);
        if (amount <= 0 || (size_t)amount > request) {
            return false;
        }
        done += (size_t)amount;
    }
    return true;
}

static bool xx_tar_block_is_zero(const uint8_t *block)
{
    size_t i;
    if (!block) return false;
    for (i = 0U; i < XX_TAR_BLOCK_SIZE; ++i) {
        if (block[i] != 0U) return false;
    }
    return true;
}

static bool xx_tar_parse_octal(const char *field, size_t size, uint64_t *value)
{
    size_t begin = 0U;
    size_t end = size;
    uint64_t result = 0U;
    bool have_digit = false;
    if (!field || !value) return false;
    while (begin < end && (field[begin] == ' ' || field[begin] == '\0')) {
        ++begin;
    }
    while (end > begin && (field[end - 1U] == ' ' || field[end - 1U] == '\0')) {
        --end;
    }
    if (begin == end) {
        *value = 0U;
        return true;
    }
    for (; begin < end; ++begin) {
        unsigned digit;
        if (field[begin] < '0' || field[begin] > '7') return false;
        digit = (unsigned)(field[begin] - '0');
        if (result > (UINT64_MAX - digit) / 8U) return false;
        result = result * 8U + digit;
        have_digit = true;
    }
    if (!have_digit) return false;
    *value = result;
    return true;
}

static bool xx_tar_parse_number(const char *field, size_t size, uint64_t *value)
{
    uint64_t result;
    size_t i;
    const uint8_t *bytes = (const uint8_t *)field;
    if (!field || !value || size == 0U) return false;
    if ((bytes[0] & 0x80U) == 0U) {
        return xx_tar_parse_octal(field, size, value);
    }
    if ((bytes[0] & 0x40U) != 0U) {
        return false;
    }
    result = (uint64_t)(bytes[0] & 0x3FU);
    for (i = 1U; i < size; ++i) {
        if (result > (UINT64_MAX - bytes[i]) / 256U) return false;
        result = result * 256U + bytes[i];
    }
    *value = result;
    return true;
}

static uint64_t xx_tar_checksum_unsigned(const uint8_t *header)
{
    uint64_t result = 0U;
    size_t i;
    for (i = 0U; i < XX_TAR_BLOCK_SIZE; ++i) {
        result += (i >= 148U && i < 156U) ? (uint8_t)' ' : header[i];
    }
    return result;
}

static int64_t xx_tar_checksum_signed(const uint8_t *header)
{
    int64_t result = 0;
    size_t i;
    for (i = 0U; i < XX_TAR_BLOCK_SIZE; ++i) {
        result += (i >= 148U && i < 156U) ? (int64_t)' ' : (int64_t)(int8_t)header[i];
    }
    return result;
}

static bool xx_tar_header_checksum_valid(const xx_tar_header *header)
{
    uint64_t stored;
    uint64_t unsigned_sum;
    int64_t signed_sum;
    if (!header || !xx_tar_parse_octal(header->checksum, sizeof(header->checksum), &stored)) {
        return false;
    }
    unsigned_sum = xx_tar_checksum_unsigned((const uint8_t *)header);
    signed_sum = xx_tar_checksum_signed((const uint8_t *)header);
    return stored == unsigned_sum || (signed_sum >= 0 && stored == (uint64_t)signed_sum);
}

static char *xx_tar_field_string(const char *field, size_t size)
{
    size_t length = 0U;
    char *result;
    if (!field) return NULL;
    while (length < size && field[length] != '\0') ++length;
    result = (char *)xx_mem_alloc(length + 1U);
    if (!result) return NULL;
    if (length != 0U) xx_mem_copy(result, field, length);
    result[length] = '\0';
    return result;
}

static char *xx_tar_join_name(const xx_tar_header *header)
{
    char *name;
    char *prefix = NULL;
    char *result = NULL;
    bool posix_prefix;
    if (!header) return NULL;
    name = xx_tar_field_string(header->name, sizeof(header->name));
    if (!name) return NULL;
    posix_prefix = xx_rt_memcmp(header->magic, "ustar", 5U) == 0 && header->magic[5] == '\0';
    if (posix_prefix) {
        prefix = xx_tar_field_string(header->prefix, sizeof(header->prefix));
    }
    if (prefix && prefix[0]) {
        result = name[0] ? xx_str_concat3(prefix, "/", name) : xx_str_dup(prefix);
    } else {
        result = xx_str_dup(name);
    }
    if (prefix) xx_str_free(prefix);
    xx_str_free(name);
    return result;
}

static char *xx_tar_read_text_payload(xx_io_device *device, int64_t offset, uint64_t size)
{
    char *result;
    size_t length;
    if (size > XX_TAR_MAX_NAME_SIZE || size > (uint64_t)(SIZE_MAX - 1U)) {
        return NULL;
    }
    length = (size_t)size;
    result = (char *)xx_mem_alloc(length + 1U);
    if (!result || (length != 0U && !xx_tar_read_exact_at(device, offset, result, length))) {
        if (result) xx_mem_free(result);
        return NULL;
    }
    while (length > 0U && (result[length - 1U] == '\0' || result[length - 1U] == '\n')) {
        --length;
    }
    result[length] = '\0';
    return result;
}

static void xx_tar_pax_cleanup(xx_tar_pax *pax)
{
    if (!pax) return;
    if (pax->path) xx_str_free(pax->path);
    if (pax->linkpath) xx_str_free(pax->linkpath);
    if (pax->sparse_name) xx_str_free(pax->sparse_name);
    if (pax->sparse_v0_extents) xx_mem_free(pax->sparse_v0_extents);
    xx_mem_zero(pax, sizeof(*pax));
}

static bool xx_tar_parse_decimal_bytes(const uint8_t *data, size_t size, uint64_t *value)
{
    uint64_t result = 0U;
    size_t i;
    if (!data || !value || size == 0U) return false;
    for (i = 0U; i < size; ++i) {
        unsigned digit;
        if (data[i] < '0' || data[i] > '9') return false;
        digit = (unsigned)(data[i] - '0');
        if (result > (UINT64_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    *value = result;
    return true;
}

static bool xx_tar_pax_set_string(char **target, const uint8_t *data, size_t size)
{
    char *copy;
    if (!target || (!data && size != 0U) || size > XX_TAR_MAX_NAME_SIZE || size > SIZE_MAX - 1U) {
        return false;
    }
    copy = (char *)xx_mem_alloc(size + 1U);
    if (!copy) return false;
    if (size != 0U) xx_mem_copy(copy, data, size);
    copy[size] = '\0';
    if (*target) xx_str_free(*target);
    *target = copy;
    return true;
}

static bool xx_tar_pax_append_sparse(xx_tar_pax *pax, uint64_t offset, uint64_t size);
static bool xx_tar_pax_parse_sparse_map(xx_tar_pax *pax, const uint8_t *data, size_t size);

static bool xx_tar_parse_pax_payload(xx_io_device *device, int64_t offset, uint64_t size, xx_tar_pax *pax)
{
    uint8_t *data;
    size_t length;
    size_t cursor = 0U;
    if (!device || !pax || size > XX_TAR_MAX_NAME_SIZE || size > (uint64_t)SIZE_MAX) {
        return false;
    }
    length = (size_t)size;
    data = (uint8_t *)xx_mem_alloc(length ? length : 1U);
    if (!data || (length != 0U && !xx_tar_read_exact_at(device, offset, data, length))) {
        if (data) xx_mem_free(data);
        return false;
    }
    while (cursor < length) {
        size_t digits_end = cursor;
        uint64_t record_length_u64;
        size_t record_length;
        size_t record_end;
        size_t key_start;
        size_t equal;
        size_t value_start;
        size_t value_size;
        while (digits_end < length && data[digits_end] >= '0' && data[digits_end] <= '9') {
            ++digits_end;
        }
        if (digits_end == cursor || digits_end >= length || data[digits_end] != ' ' ||
            !xx_tar_parse_decimal_bytes(data + cursor, digits_end - cursor, &record_length_u64) || record_length_u64 > (uint64_t)(length - cursor) ||
            record_length_u64 > (uint64_t)SIZE_MAX) {
            xx_mem_free(data);
            return false;
        }
        record_length = (size_t)record_length_u64;
        record_end = cursor + record_length;
        key_start = digits_end + 1U;
        if (record_length == 0U || record_end <= key_start || data[record_end - 1U] != '\n') {
            xx_mem_free(data);
            return false;
        }
        equal = key_start;
        while (equal < record_end - 1U && data[equal] != '=') ++equal;
        if (equal >= record_end - 1U) {
            xx_mem_free(data);
            return false;
        }
        value_start = equal + 1U;
        value_size = record_end - 1U - value_start;
        if (equal - key_start == 4U && xx_rt_memcmp(data + key_start, "path", 4U) == 0) {
            pax->path_present = true;
            if (value_size == 0U) {
                if (pax->path) xx_str_free(pax->path);
                pax->path = NULL;
            } else if (!xx_tar_pax_set_string(&pax->path, data + value_start, value_size)) {
                xx_mem_free(data);
                return false;
            }
        } else if (equal - key_start == 8U && xx_rt_memcmp(data + key_start, "linkpath", 8U) == 0) {
            pax->linkpath_present = true;
            if (value_size == 0U) {
                if (pax->linkpath) xx_str_free(pax->linkpath);
                pax->linkpath = NULL;
            } else if (!xx_tar_pax_set_string(&pax->linkpath, data + value_start, value_size)) {
                xx_mem_free(data);
                return false;
            }
        } else if (equal - key_start == 4U && xx_rt_memcmp(data + key_start, "size", 4U) == 0) {
            pax->size_present = true;
            if (value_size == 0U) {
                pax->size = 0U;
                pax->has_size = false;
            } else if (!xx_tar_parse_decimal_bytes(data + value_start, value_size, &pax->size)) {
                xx_mem_free(data);
                return false;
            } else {
                pax->has_size = true;
            }
        } else if (equal - key_start == 5U && xx_rt_memcmp(data + key_start, "mtime", 5U) == 0) {
            size_t integer_size = 0U;
            if (value_size == 0U) {
                pax->mtime = 0U;
                pax->mtime_present = true;
                pax->has_mtime = false;
            }
            while (integer_size < value_size && data[value_start + integer_size] >= '0' && data[value_start + integer_size] <= '9') {
                ++integer_size;
            }
            if (integer_size != 0U && xx_tar_parse_decimal_bytes(data + value_start, integer_size, &pax->mtime)) {
                pax->mtime_present = true;
                pax->has_mtime = true;
            }
        } else if (equal - key_start == 16U && xx_rt_memcmp(data + key_start, "GNU.sparse.major", 16U) == 0) {
            pax->sparse_keyword_seen = true;
            pax->sparse_major_present = true;
            if (!xx_tar_parse_decimal_bytes(data + value_start, value_size, &pax->sparse_major)) {
                xx_mem_free(data);
                return false;
            }
        } else if (equal - key_start == 16U && xx_rt_memcmp(data + key_start, "GNU.sparse.minor", 16U) == 0) {
            pax->sparse_keyword_seen = true;
            pax->sparse_minor_present = true;
            if (!xx_tar_parse_decimal_bytes(data + value_start, value_size, &pax->sparse_minor)) {
                xx_mem_free(data);
                return false;
            }
        } else if (equal - key_start == 15U && xx_rt_memcmp(data + key_start, "GNU.sparse.name", 15U) == 0) {
            size_t j;
            pax->sparse_keyword_seen = true;
            pax->sparse_name_present = true;
            for (j = 0U; j < value_size; ++j) {
                if (data[value_start + j] == 0U) {
                    xx_mem_free(data);
                    return false;
                }
            }
            if (value_size == 0U || !xx_tar_pax_set_string(&pax->sparse_name, data + value_start, value_size)) {
                xx_mem_free(data);
                return false;
            }
        } else if (equal - key_start == 19U && xx_rt_memcmp(data + key_start, "GNU.sparse.realsize", 19U) == 0) {
            pax->sparse_keyword_seen = true;
            pax->sparse_realsize_present = true;
            if (!xx_tar_parse_decimal_bytes(data + value_start, value_size, &pax->sparse_realsize)) {
                xx_mem_free(data);
                return false;
            }
        } else if (equal - key_start == 15U && xx_rt_memcmp(data + key_start, "GNU.sparse.size", 15U) == 0) {
            pax->sparse_keyword_seen = true;
            if (pax->sparse_size_present || !xx_tar_parse_decimal_bytes(data + value_start, value_size, &pax->sparse_size)) {
                xx_mem_free(data);
                return false;
            }
            pax->sparse_size_present = true;
        } else if (equal - key_start == 20U && xx_rt_memcmp(data + key_start, "GNU.sparse.numblocks", 20U) == 0) {
            pax->sparse_keyword_seen = true;
            if (pax->sparse_numblocks_present || !xx_tar_parse_decimal_bytes(data + value_start, value_size, &pax->sparse_numblocks)) {
                xx_mem_free(data);
                return false;
            }
            pax->sparse_numblocks_present = true;
        } else if (equal - key_start == 17U && xx_rt_memcmp(data + key_start, "GNU.sparse.offset", 17U) == 0) {
            pax->sparse_keyword_seen = true;
            if (pax->sparse_map_present || pax->sparse_pending || !xx_tar_parse_decimal_bytes(data + value_start, value_size, &pax->sparse_pending_offset)) {
                xx_mem_free(data);
                return false;
            }
            pax->sparse_pending = true;
            pax->sparse_pairs_present = true;
        } else if (equal - key_start == 19U && xx_rt_memcmp(data + key_start, "GNU.sparse.numbytes", 19U) == 0) {
            uint64_t length_value;
            pax->sparse_keyword_seen = true;
            if (!pax->sparse_pending || !xx_tar_parse_decimal_bytes(data + value_start, value_size, &length_value) ||
                !xx_tar_pax_append_sparse(pax, pax->sparse_pending_offset, length_value)) {
                xx_mem_free(data);
                return false;
            }
            pax->sparse_pending = false;
        } else if (equal - key_start == 14U && xx_rt_memcmp(data + key_start, "GNU.sparse.map", 14U) == 0) {
            pax->sparse_keyword_seen = true;
            if (!xx_tar_pax_parse_sparse_map(pax, data + value_start, value_size)) {
                xx_mem_free(data);
                return false;
            }
        } else if (equal - key_start >= 11U && xx_rt_memcmp(data + key_start, "GNU.sparse.", 11U) == 0) {
            /* A different sparse version must never be unpacked as a
             * seemingly ordinary, condensed regular file. */
            pax->sparse_keyword_seen = true;
        }
        cursor = record_end;
    }
    xx_mem_free(data);
    return true;
}

static bool xx_tar_pax_merge(xx_tar_pax *destination, xx_tar_pax *source)
{
    char *copy;
    if (!destination || !source) return false;
    if (source->sparse_keyword_seen && destination->sparse_keyword_seen) return false;
    if (source->path_present) {
        copy = source->path ? xx_str_dup(source->path) : NULL;
        if (source->path && !copy) return false;
        if (destination->path) xx_str_free(destination->path);
        destination->path = copy;
        destination->path_present = true;
    }
    if (source->linkpath_present) {
        copy = source->linkpath ? xx_str_dup(source->linkpath) : NULL;
        if (source->linkpath && !copy) return false;
        if (destination->linkpath) xx_str_free(destination->linkpath);
        destination->linkpath = copy;
        destination->linkpath_present = true;
    }
    if (source->size_present) {
        destination->size = source->size;
        destination->has_size = source->has_size;
        destination->size_present = true;
    }
    if (source->mtime_present) {
        destination->mtime = source->mtime;
        destination->has_mtime = source->has_mtime;
        destination->mtime_present = true;
    }
    if (source->sparse_keyword_seen) {
        destination->sparse_keyword_seen = true;
        destination->sparse_size = source->sparse_size;
        destination->sparse_size_present = source->sparse_size_present;
        destination->sparse_numblocks = source->sparse_numblocks;
        destination->sparse_numblocks_present = source->sparse_numblocks_present;
        destination->sparse_map_present = source->sparse_map_present;
        destination->sparse_pairs_present = source->sparse_pairs_present;
        destination->sparse_pending = source->sparse_pending;
        destination->sparse_v0_extents = source->sparse_v0_extents;
        destination->sparse_v0_count = source->sparse_v0_count;
        destination->sparse_v0_capacity = source->sparse_v0_capacity;
        source->sparse_v0_extents = NULL;
        source->sparse_v0_count = source->sparse_v0_capacity = 0U;
    }
    if (source->sparse_major_present) {
        destination->sparse_major = source->sparse_major;
        destination->sparse_major_present = true;
    }
    if (source->sparse_minor_present) {
        destination->sparse_minor = source->sparse_minor;
        destination->sparse_minor_present = true;
    }
    if (source->sparse_realsize_present) {
        destination->sparse_realsize = source->sparse_realsize;
        destination->sparse_realsize_present = true;
    }
    if (source->sparse_name_present) {
        copy = source->sparse_name ? xx_str_dup(source->sparse_name) : NULL;
        if (source->sparse_name && !copy) return false;
        if (destination->sparse_name) xx_str_free(destination->sparse_name);
        destination->sparse_name = copy;
        destination->sparse_name_present = true;
    }
    return true;
}

static void xx_tar_member_cleanup(xx_tar_member *member)
{
    if (!member) return;
    if (member->name) xx_str_free(member->name);
    if (member->linkname) xx_str_free(member->linkname);
    if (member->sparse_extents) xx_mem_free(member->sparse_extents);
    xx_mem_zero(member, sizeof(*member));
}

static void xx_tar_private_free(void *pointer)
{
    xx_tar_private *priv = (xx_tar_private *)pointer;
    size_t i;
    if (!priv) return;
    for (i = 0U; i < priv->count; ++i) {
        xx_tar_member_cleanup(&priv->members[i]);
    }
    if (priv->members) xx_mem_free(priv->members);
    xx_mem_free(priv);
}

static bool xx_tar_append_member(xx_tar_private *priv, xx_tar_member *member)
{
    xx_tar_member *resized;
    size_t capacity;
    if (!priv || !member || priv->count >= XX_TAR_MAX_MEMBERS) return false;
    if (priv->count == priv->capacity) {
        capacity = priv->capacity ? priv->capacity * 2U : 16U;
        if (capacity < priv->count || capacity > SIZE_MAX / sizeof(*priv->members)) {
            return false;
        }
        resized = (xx_tar_member *)xx_mem_realloc(priv->members, capacity * sizeof(*priv->members));
        if (!resized) return false;
        priv->members = resized;
        priv->capacity = capacity;
    }
    priv->members[priv->count++] = *member;
    xx_mem_zero(member, sizeof(*member));
    return true;
}

static bool xx_tar_add_i64(int64_t left, uint64_t right, int64_t *result)
{
    if (!result || left < 0 || right > (uint64_t)INT64_MAX || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

static bool xx_tar_pax_append_sparse(xx_tar_pax *pax, uint64_t offset, uint64_t size)
{
    xx_tar_sparse_extent *grown;
    size_t capacity;
    if (!pax || pax->sparse_v0_count >= XX_TAR_MAX_SPARSE_EXTENTS) return false;
    if (pax->sparse_v0_count == pax->sparse_v0_capacity) {
        capacity = pax->sparse_v0_capacity ? pax->sparse_v0_capacity * 2U : 8U;
        if (capacity > XX_TAR_MAX_SPARSE_EXTENTS) capacity = XX_TAR_MAX_SPARSE_EXTENTS;
        grown = (xx_tar_sparse_extent *)xx_mem_realloc(pax->sparse_v0_extents, capacity * sizeof(*grown));
        if (!grown) return false;
        pax->sparse_v0_extents = grown;
        pax->sparse_v0_capacity = capacity;
    }
    pax->sparse_v0_extents[pax->sparse_v0_count].offset = offset;
    pax->sparse_v0_extents[pax->sparse_v0_count].size = size;
    pax->sparse_v0_extents[pax->sparse_v0_count].packed_offset = 0;
    ++pax->sparse_v0_count;
    return true;
}

static bool xx_tar_pax_parse_sparse_map(xx_tar_pax *pax, const uint8_t *data, size_t size)
{
    size_t cursor = 0U;
    if (!pax || !data || size == 0U || pax->sparse_map_present || pax->sparse_pairs_present || pax->sparse_pending) return false;
    while (cursor < size) {
        uint64_t offset, length;
        size_t start = cursor;
        while (cursor < size && data[cursor] != ',') ++cursor;
        if (cursor == size || !xx_tar_parse_decimal_bytes(data + start, cursor - start, &offset)) return false;
        start = ++cursor;
        while (cursor < size && data[cursor] != ',') ++cursor;
        if (!xx_tar_parse_decimal_bytes(data + start, cursor - start, &length) || !xx_tar_pax_append_sparse(pax, offset, length)) return false;
        if (cursor < size) ++cursor;
        if (cursor == size && data[size - 1U] == ',') return false;
    }
    pax->sparse_map_present = true;
    return true;
}

typedef struct xx_tar_sparse_map_reader_s {
    xx_io_device *device;
    xx_pd_struct *pd;
    int64_t offset;
    uint64_t limit;
    uint64_t position;
    uint64_t block_start;
    size_t block_size;
    uint8_t block[XX_TAR_BLOCK_SIZE];
} xx_tar_sparse_map_reader;

static bool xx_tar_sparse_map_byte(xx_tar_sparse_map_reader *reader, uint8_t *result)
{
    uint64_t start;
    size_t amount;
    if (!reader || !result || reader->position >= reader->limit || (reader->pd && xx_pd_is_stopped(reader->pd))) return false;
    start = reader->position & ~(uint64_t)(XX_TAR_BLOCK_SIZE - 1U);
    if (reader->block_start != start || reader->block_size == 0U) {
        amount = reader->limit - start > XX_TAR_BLOCK_SIZE ? XX_TAR_BLOCK_SIZE : (size_t)(reader->limit - start);
        if (!xx_tar_read_exact_at(reader->device, reader->offset + (int64_t)start, reader->block, amount) || (reader->pd && xx_pd_is_stopped(reader->pd))) return false;
        reader->block_start = start;
        reader->block_size = amount;
    }
    *result = reader->block[(size_t)(reader->position - start)];
    ++reader->position;
    return true;
}

static bool xx_tar_sparse_map_number(xx_tar_sparse_map_reader *reader, uint64_t *value)
{
    uint64_t parsed = 0U;
    unsigned digits = 0U;
    uint8_t ch;
    if (!reader || !value) return false;
    while (xx_tar_sparse_map_byte(reader, &ch)) {
        unsigned digit;
        if (ch == '\n') {
            if (digits == 0U) return false;
            *value = parsed;
            return true;
        }
        if (ch < '0' || ch > '9') return false;
        digit = (unsigned)(ch - '0');
        if (parsed > (UINT64_MAX - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
        ++digits;
    }
    return false;
}

static bool xx_tar_parse_sparse_map(xx_io_device *device, xx_tar_member *member, uint64_t logical_size, xx_pd_struct *pd)
{
    xx_tar_sparse_map_reader map;
    xx_tar_sparse_extent *extents = NULL;
    uint64_t count, packed_map, packed_data = 0U, last_end = 0U;
    size_t i;
    if (!device || !member || member->data_offset < 0 || member->data_size < 0 || logical_size > XX_TAR_MAX_SPARSE_LOGICAL_BYTES) return false;
    xx_mem_zero(&map, sizeof(map));
    map.device = device;
    map.pd = pd;
    map.offset = member->data_offset;
    map.limit = (uint64_t)member->data_size;
    if (map.limit > XX_TAR_MAX_SPARSE_MAP_BYTES) map.limit = XX_TAR_MAX_SPARSE_MAP_BYTES;
    map.block_start = UINT64_MAX;
    if (!xx_tar_sparse_map_number(&map, &count) || count > XX_TAR_MAX_SPARSE_EXTENTS || count > SIZE_MAX / sizeof(*extents)) return false;
    if (count) {
        extents = (xx_tar_sparse_extent *)xx_mem_alloc((size_t)count * sizeof(*extents));
        if (!extents) return false;
    }
    for (i = 0U; i < (size_t)count; ++i) {
        uint64_t offset, size;
        if (!xx_tar_sparse_map_number(&map, &offset) || !xx_tar_sparse_map_number(&map, &size) || offset < last_end || offset > logical_size ||
            size > logical_size - offset || (size == 0U && (i + 1U != (size_t)count || offset != logical_size)) || size > (uint64_t)member->data_size - packed_data)
            goto fail;
        extents[i].offset = offset;
        extents[i].size = size;
        extents[i].packed_offset = 0;
        packed_data += size;
        last_end = offset + size;
    }
    if (map.position > UINT64_MAX - (XX_TAR_BLOCK_SIZE - 1U)) goto fail;
    packed_map = (map.position + XX_TAR_BLOCK_SIZE - 1U) & ~(uint64_t)(XX_TAR_BLOCK_SIZE - 1U);
    if (packed_map > map.limit || packed_map > (uint64_t)member->data_size || packed_data != (uint64_t)member->data_size - packed_map) goto fail;
    while (map.position < packed_map) {
        uint8_t pad;
        if (!xx_tar_sparse_map_byte(&map, &pad) || pad != 0U) goto fail;
    }
    packed_data = 0U;
    for (i = 0U; i < (size_t)count; ++i) {
        if (!xx_tar_add_i64(member->data_offset, packed_map + packed_data, &extents[i].packed_offset)) goto fail;
        packed_data += extents[i].size;
    }
    member->sparse = true;
    member->logical_size = logical_size;
    member->sparse_extents = extents;
    member->sparse_count = (size_t)count;
    return true;
fail:
    if (extents) xx_mem_free(extents);
    return false;
}

static bool xx_tar_attach_inline_sparse(xx_tar_member *member, const xx_tar_sparse_extent *source, size_t count, uint64_t logical_size)
{
    xx_tar_sparse_extent *extents = NULL;
    uint64_t last_end = 0U, packed = 0U;
    size_t i;
    if (!member || member->data_offset < 0 || member->data_size < 0 || logical_size > XX_TAR_MAX_SPARSE_LOGICAL_BYTES || count > XX_TAR_MAX_SPARSE_EXTENTS ||
        (count && !source))
        return false;
    if (count) {
        extents = (xx_tar_sparse_extent *)xx_mem_alloc(count * sizeof(*extents));
        if (!extents) return false;
    }
    for (i = 0U; i < count; ++i) {
        uint64_t offset = source[i].offset, size = source[i].size;
        if (offset < last_end || offset > logical_size || size > logical_size - offset || (size == 0U && (i + 1U != count || offset != logical_size)) ||
            size > (uint64_t)member->data_size - packed || !xx_tar_add_i64(member->data_offset, packed, &extents[i].packed_offset))
            goto fail;
        extents[i].offset = offset;
        extents[i].size = size;
        packed += size;
        last_end = offset + size;
    }
    if (packed != (uint64_t)member->data_size) goto fail;
    member->sparse = true;
    member->logical_size = logical_size;
    member->sparse_extents = extents;
    member->sparse_count = count;
    return true;
fail:
    if (extents) xx_mem_free(extents);
    return false;
}

static bool xx_tar_oldgnu_flag(uint8_t value, bool *extended)
{
    if (!extended) return false;
    if (value == 1U || value == '1') {
        *extended = true;
        return true;
    }
    if (value == 0U || value == '0') {
        *extended = false;
        return true;
    }
    return false;
}

static bool xx_tar_oldgnu_map_block(xx_tar_pax *map, const uint8_t *block, size_t count)
{
    size_t i;
    if (!map || !block) return false;
    for (i = 0U; i < count; ++i) {
        uint64_t offset, size;
        const char *entry = (const char *)block + i * 24U;
        if (!xx_tar_parse_number(entry, 12U, &offset) || !xx_tar_parse_number(entry + 12U, 12U, &size)) return false;
        if (offset || size) {
            if (!xx_tar_pax_append_sparse(map, offset, size)) return false;
        }
    }
    return true;
}

static bool xx_tar_parse_oldgnu_sparse(xx_io_device *device, const xx_tar_header *header, int64_t total_size, xx_tar_member *member)
{
    xx_tar_pax map = {0};
    const uint8_t *bytes = (const uint8_t *)header;
    uint64_t logical_size;
    int64_t data_offset;
    bool extended, success = false;
    if (!device || !header || !member || member->data_offset < 0 || member->data_size < 0 || xx_rt_memcmp(bytes + 257U, "ustar  ", 7U) != 0 ||
        !xx_tar_parse_number((const char *)bytes + 483U, 12U, &logical_size) || !xx_tar_oldgnu_flag(bytes[482U], &extended) ||
        !xx_tar_oldgnu_map_block(&map, bytes + 386U, 4U))
        goto done;
    data_offset = member->data_offset;
    while (extended) {
        uint8_t block[XX_TAR_BLOCK_SIZE];
        if (data_offset > total_size - XX_TAR_BLOCK_SIZE || !xx_tar_read_exact_at(device, data_offset, block, sizeof(block)) ||
            !xx_tar_oldgnu_map_block(&map, block, 21U) || !xx_tar_oldgnu_flag(block[504U], &extended))
            goto done;
        data_offset += XX_TAR_BLOCK_SIZE;
    }
    member->data_offset = data_offset;
    success = xx_tar_attach_inline_sparse(member, map.sparse_v0_extents, map.sparse_v0_count, logical_size);
done:
    xx_tar_pax_cleanup(&map);
    return success;
}

static bool xx_tar_all_zero_range_buffered(xx_io_device *device, int64_t offset, int64_t size, uint8_t *buffer, size_t buffer_capacity)
{
    int64_t remaining = size;
    if (!device || offset < 0 || size < 0) return false;
    while (remaining > 0) {
        size_t amount = (uint64_t)remaining > (uint64_t)buffer_capacity ? buffer_capacity : (size_t)remaining;
        size_t i;
        if (!xx_tar_read_exact_at(device, offset, buffer, amount)) return false;
        for (i = 0U; i < amount; ++i) {
            if (buffer[i] != 0U) return false;
        }
        offset += (int64_t)amount;
        remaining -= (int64_t)amount;
    }
    return true;
}

static bool xx_tar_all_zero_range(xx_io_device *device, int64_t offset, int64_t size)
{
    size_t buffer_capacity = xx_get_file_buffer_size();
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(buffer_capacity);
    bool buffer_result;
    if (!buffer) return false;
    buffer_result = xx_tar_all_zero_range_buffered(device, offset, size, buffer, buffer_capacity);
    xx_mem_free(buffer);
    return buffer_result;
}

static bool xx_tar_parse_archive(Abstractformat *self, xx_tar_private **out_private, xx_pd_struct *pd)
{
    xx_tar_private *priv = NULL;
    xx_tar_pax global_pax = {0};
    xx_tar_pax local_pax = {0};
    char *gnu_long_name = NULL;
    char *gnu_long_link = NULL;
    int64_t total_size;
    int64_t saved_position;
    int64_t offset;
    bool success = false;

    if (!self || !self->device || !out_private || self->base_address < 0) {
        return false;
    }
    *out_private = NULL;
    saved_position = xx_io_tell(self->device);
    if (saved_position < 0) return false;
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address || total_size - self->base_address < XX_TAR_BLOCK_SIZE) {
        (void)xx_io_seek64(self->device, saved_position, SEEK_SET);
        return false;
    }
    priv = (xx_tar_private *)xx_mem_calloc(1U, sizeof(*priv));
    if (!priv) {
        (void)xx_io_seek64(self->device, saved_position, SEEK_SET);
        return false;
    }
    offset = self->base_address;

    while (offset < total_size) {
        xx_tar_header header;
        uint64_t stored_size;
        uint64_t data_size;
        uint64_t padded_size;
        uint64_t mode;
        uint64_t uid;
        uint64_t gid;
        uint64_t mtime;
        uint64_t header_mtime;
        int64_t data_offset;
        int64_t next_offset;
        xx_tar_member member;
        char *header_name = NULL;
        char *header_link = NULL;
        bool metadata;
        bool local_applies;

        xx_mem_zero(&member, sizeof(member));
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        if (total_size - offset < XX_TAR_BLOCK_SIZE || !xx_tar_read_exact_at(self->device, offset, &header, sizeof(header))) {
            goto cleanup;
        }
        if (xx_tar_block_is_zero((const uint8_t *)&header)) {
            uint8_t second[XX_TAR_BLOCK_SIZE];
            int64_t end_offset;
            int64_t remaining = total_size - offset;
            if (local_pax.sparse_keyword_seen || global_pax.sparse_keyword_seen) goto cleanup;
            if (remaining >= XX_TAR_END_SIZE) {
                if (!xx_tar_read_exact_at(self->device, offset + XX_TAR_BLOCK_SIZE, second, sizeof(second)) || !xx_tar_block_is_zero(second)) {
                    goto cleanup;
                }
                end_offset = offset + XX_TAR_END_SIZE;
            } else if (remaining == XX_TAR_BLOCK_SIZE) {
                /* Some historical V7 writers terminate with a single zero
                 * record.  Accept it only at physical EOF so embedded data
                 * cannot be mistaken for a complete archive. */
                end_offset = offset + XX_TAR_BLOCK_SIZE;
            } else {
                goto cleanup;
            }
            priv->end_marker_offset = offset;
            while (total_size - end_offset >= XX_TAR_BLOCK_SIZE) {
                uint8_t block[XX_TAR_BLOCK_SIZE];
                if (!xx_tar_read_exact_at(self->device, end_offset, block, sizeof(block))) {
                    goto cleanup;
                }
                if (!xx_tar_block_is_zero(block)) break;
                end_offset += XX_TAR_BLOCK_SIZE;
            }
            if (end_offset < total_size && xx_tar_all_zero_range(self->device, end_offset, total_size - end_offset)) {
                end_offset = total_size;
            }
            priv->archive_end = end_offset;
            success = true;
            break;
        }
        if (!xx_tar_header_checksum_valid(&header) || !xx_tar_parse_number(header.size, sizeof(header.size), &stored_size) ||
            !xx_tar_parse_number(header.mode, sizeof(header.mode), &mode) || !xx_tar_parse_number(header.uid, sizeof(header.uid), &uid) ||
            !xx_tar_parse_number(header.gid, sizeof(header.gid), &gid) || !xx_tar_parse_number(header.mtime, sizeof(header.mtime), &mtime)) {
            goto cleanup;
        }
        header_mtime = mtime;
        metadata = header.typeflag == 'L' || header.typeflag == 'K' || header.typeflag == 'x' || header.typeflag == 'g';
        local_applies = !metadata;
        data_size = stored_size;
        if (local_applies) {
            if (global_pax.has_size) data_size = global_pax.size;
            if (local_pax.size_present) {
                data_size = local_pax.has_size ? local_pax.size : stored_size;
            }
            if (global_pax.has_mtime) mtime = global_pax.mtime;
            if (local_pax.mtime_present) {
                mtime = local_pax.has_mtime ? local_pax.mtime : header_mtime;
            }
        }
        if (data_size > UINT64_MAX - (XX_TAR_BLOCK_SIZE - 1U)) goto cleanup;
        padded_size = (data_size + XX_TAR_BLOCK_SIZE - 1U) & ~(uint64_t)(XX_TAR_BLOCK_SIZE - 1U);
        data_offset = offset + XX_TAR_BLOCK_SIZE;
        if (header.typeflag == 'S') {
            if (data_size > (uint64_t)INT64_MAX) goto cleanup_member;
            member.data_offset = data_offset;
            member.data_size = (int64_t)data_size;
            if (!xx_tar_parse_oldgnu_sparse(self->device, &header, total_size, &member)) goto cleanup_member;
            data_offset = member.data_offset;
        }
        if (!xx_tar_add_i64(data_offset, padded_size, &next_offset) || next_offset > total_size) {
            goto cleanup;
        }

        header_name = header.typeflag == 'S' ? xx_tar_field_string(header.name, sizeof(header.name)) : xx_tar_join_name(&header);
        header_link = xx_tar_field_string(header.linkname, sizeof(header.linkname));
        if (!header_name || !header_link) goto cleanup_member;
        member.header_offset = offset;
        member.data_offset = data_offset;
        member.data_size = (int64_t)data_size;
        member.padded_size = (int64_t)padded_size;
        if (!member.sparse) member.logical_size = data_size;
        member.mode = mode;
        member.uid = uid;
        member.gid = gid;
        member.mtime = mtime;
        member.typeflag = header.typeflag;
        member.metadata = metadata;
        member.name = header_name;
        member.linkname = header_link;
        header_name = NULL;
        header_link = NULL;

        if (metadata) {
            if (header.typeflag == 'L') {
                char *value = xx_tar_read_text_payload(self->device, data_offset, stored_size);
                if (!value) goto cleanup_member;
                if (gnu_long_name) xx_str_free(gnu_long_name);
                gnu_long_name = value;
            } else if (header.typeflag == 'K') {
                char *value = xx_tar_read_text_payload(self->device, data_offset, stored_size);
                if (!value) goto cleanup_member;
                if (gnu_long_link) xx_str_free(gnu_long_link);
                gnu_long_link = value;
            } else if (header.typeflag == 'x') {
                xx_tar_pax parsed = {0};
                if (!xx_tar_parse_pax_payload(self->device, data_offset, stored_size, &parsed) || !xx_tar_pax_merge(&local_pax, &parsed)) {
                    xx_tar_pax_cleanup(&parsed);
                    goto cleanup_member;
                }
                xx_tar_pax_cleanup(&parsed);
            } else {
                xx_tar_pax parsed = {0};
                if (!xx_tar_parse_pax_payload(self->device, data_offset, stored_size, &parsed) || !xx_tar_pax_merge(&global_pax, &parsed)) {
                    xx_tar_pax_cleanup(&parsed);
                    goto cleanup_member;
                }
                xx_tar_pax_cleanup(&parsed);
            }
        } else {
            char *chosen_name = NULL;
            char *chosen_link = NULL;
            const char *name_source = member.name;
            const char *link_source = member.linkname;
            size_t name_length;
            if (global_pax.path) name_source = global_pax.path;
            if (gnu_long_name) name_source = gnu_long_name;
            if (local_pax.path_present) {
                name_source = local_pax.path ? local_pax.path : (gnu_long_name ? gnu_long_name : member.name);
            }
            if (global_pax.sparse_keyword_seen) goto cleanup_member;
            if (local_pax.sparse_keyword_seen) {
                if (header.typeflag == 'S') goto cleanup_member;
                if (header.typeflag != '0' && header.typeflag != '\0') goto cleanup_member;
                if (local_pax.sparse_major_present || local_pax.sparse_minor_present) {
                    if (!local_pax.sparse_major_present || !local_pax.sparse_minor_present || !local_pax.sparse_name_present || !local_pax.sparse_realsize_present ||
                        local_pax.sparse_size_present || local_pax.sparse_numblocks_present || local_pax.sparse_v0_count || local_pax.sparse_major != 1U ||
                        local_pax.sparse_minor != 0U || !local_pax.sparse_name)
                        goto cleanup_member;
                    name_source = local_pax.sparse_name;
                } else {
                    if (!local_pax.sparse_size_present || !local_pax.sparse_numblocks_present || local_pax.sparse_realsize_present || local_pax.sparse_pending ||
                        local_pax.sparse_numblocks != local_pax.sparse_v0_count || (local_pax.sparse_map_present == local_pax.sparse_pairs_present) ||
                        (local_pax.sparse_map_present && !local_pax.sparse_name_present))
                        goto cleanup_member;
                    if (local_pax.sparse_name_present) name_source = local_pax.sparse_name;
                }
            }
            if (global_pax.linkpath) link_source = global_pax.linkpath;
            if (gnu_long_link) link_source = gnu_long_link;
            if (local_pax.linkpath_present) {
                link_source = local_pax.linkpath ? local_pax.linkpath : (gnu_long_link ? gnu_long_link : member.linkname);
            }
            chosen_name = xx_str_dup(name_source ? name_source : "");
            chosen_link = xx_str_dup(link_source ? link_source : "");
            if (!chosen_name || !chosen_link) {
                if (chosen_name) xx_str_free(chosen_name);
                if (chosen_link) xx_str_free(chosen_link);
                goto cleanup_member;
            }
            xx_str_free(member.name);
            xx_str_free(member.linkname);
            member.name = chosen_name;
            member.linkname = chosen_link;
            xx_tar_normalize_name(member.name);
            name_length = xx_str_len(member.name);
            member.directory = header.typeflag == '5' || (name_length != 0U && (member.name[name_length - 1U] == '/' || member.name[name_length - 1U] == '\\'));
            while (name_length > 0U && (member.name[name_length - 1U] == '/' || member.name[name_length - 1U] == '\\')) {
                member.name[--name_length] = '\0';
            }
            member.regular = header.typeflag == '\0' || header.typeflag == ' ' || header.typeflag == '0' || header.typeflag == '7' || header.typeflag == 'S';
            if (local_pax.sparse_keyword_seen &&
                (!member.regular ||
                 !(local_pax.sparse_major_present ? xx_tar_parse_sparse_map(self->device, &member, local_pax.sparse_realsize, pd)
                                                  : xx_tar_attach_inline_sparse(&member, local_pax.sparse_v0_extents, local_pax.sparse_v0_count, local_pax.sparse_size))))
                goto cleanup_member;
            if (member.sparse) {
                if (member.sparse_count > XX_TAR_MAX_SPARSE_TOTAL_EXTENTS - priv->sparse_extent_total) goto cleanup_member;
                priv->sparse_extent_total += member.sparse_count;
            }
            ++priv->visible_count;
        }

        if (!xx_tar_append_member(priv, &member)) goto cleanup_member;
        if (!metadata) {
            if (gnu_long_name) {
                xx_str_free(gnu_long_name);
                gnu_long_name = NULL;
            }
            if (gnu_long_link) {
                xx_str_free(gnu_long_link);
                gnu_long_link = NULL;
            }
            xx_tar_pax_cleanup(&local_pax);
        }
        offset = next_offset;
        continue;

    cleanup_member:
        if (header_name) xx_str_free(header_name);
        if (header_link) xx_str_free(header_link);
        xx_tar_member_cleanup(&member);
        goto cleanup;
    }

cleanup:
    if (gnu_long_name) xx_str_free(gnu_long_name);
    if (gnu_long_link) xx_str_free(gnu_long_link);
    xx_tar_pax_cleanup(&global_pax);
    xx_tar_pax_cleanup(&local_pax);
    if (xx_io_seek64(self->device, saved_position, SEEK_SET) != 0) success = false;
    if (!success || priv->archive_end <= self->base_address) {
        xx_tar_private_free(priv);
        return false;
    }
    *out_private = priv;
    return true;
}

static bool xx_tar_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t i;
    if (!destination || !source) return source == NULL;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)source, i);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static bool xx_tar_ascii_equal_nocase(const char *value, size_t length, const char *expected)
{
    size_t i;
    if (!value || !expected || xx_str_len(expected) != length) return false;
    for (i = 0U; i < length; ++i) {
        unsigned char left = (unsigned char)value[i];
        unsigned char right = (unsigned char)expected[i];
        if (left >= 'a' && left <= 'z') left = (unsigned char)(left - 32U);
        if (right >= 'a' && right <= 'z') right = (unsigned char)(right - 32U);
        if (left != right) return false;
    }
    return true;
}

/* True for a reserved Win32 device name, with or without an extension,
 * including COM/LPT followed by a UTF-8 superscript digit (as CPIO). */
static bool xx_tar_component_is_windows_device(const char *component, size_t length)
{
    size_t base_length = 0U;
    if (!component) return false;
    while (base_length < length && component[base_length] != '.') ++base_length;
    if (xx_tar_ascii_equal_nocase(component, base_length, "CON") || xx_tar_ascii_equal_nocase(component, base_length, "PRN") ||
        xx_tar_ascii_equal_nocase(component, base_length, "AUX") || xx_tar_ascii_equal_nocase(component, base_length, "NUL") ||
        xx_tar_ascii_equal_nocase(component, base_length, "CONIN$") || xx_tar_ascii_equal_nocase(component, base_length, "CONOUT$") ||
        xx_tar_ascii_equal_nocase(component, base_length, "CLOCK$")) {
        return true;
    }
    if (base_length < 4U || !(xx_tar_ascii_equal_nocase(component, 3U, "COM") || xx_tar_ascii_equal_nocase(component, 3U, "LPT"))) {
        return false;
    }
    return (base_length == 4U && component[3] >= '1' && component[3] <= '9') ||
           (base_length == 5U && (unsigned char)component[3] == 0xC2U &&
            ((unsigned char)component[4] == 0xB9U || (unsigned char)component[4] == 0xB2U || (unsigned char)component[4] == 0xB3U));
}

static bool xx_tar_safe_name(const char *name)
{
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    if ((((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z'))) && name[1] == ':') {
        return false;
    }
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) {
            return false;
        }
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') || (length == 2U && component[0] == '.' && component[1] == '.') || component[length - 1U] == '.' ||
                component[length - 1U] == ' ' || xx_tar_component_is_windows_device(component, length)) {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

/* Returns an independently allocated, slash-normalized extraction name for
 * the stored member name @p source, or NULL when it cannot be extracted
 * safely.  As GNU tar and 7-Zip do, absolute names ("/etc/x", "C:\x",
 * "//srv/x") are made relative by dropping leading separators and drive
 * prefixes, and "." and empty components ("./x", "a//b") are dropped.  Any
 * ".." component is still refused, so "/../x" and "C:\..\x" cannot escape,
 * as are control and reserved characters, Windows device names and trailing
 * dots/spaces.  A name with nothing left ("", "." or "/") denotes the output
 * root: NULL is returned and *is_root, when supplied, is set.  This is the
 * CPIO reader's policy (cpio_safe_output_name); xx_tar_safe_name keeps the
 * strict form for names the writer stores. */
static char *xx_tar_safe_output_name(const char *source, bool *is_root)
{
    size_t length;
    size_t index;
    size_t start;
    size_t out = 0U;
    char *name;
    if (is_root) *is_root = false;
    if (!source) return NULL;
    for (;;) {
        if (source[0] == '/' || source[0] == '\\') {
            ++source;
        } else if (((source[0] >= 'A' && source[0] <= 'Z') || (source[0] >= 'a' && source[0] <= 'z')) && source[1] == ':') {
            source += 2;
        } else {
            break;
        }
    }
    length = xx_str_len(source);
    if (length > XX_TAR_MAX_NAME_SIZE) return NULL;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    for (start = 0U, index = 0U; index <= length; ++index) {
        unsigned char c = (unsigned char)source[index];
        size_t size;
        if (index != length && c != '/' && c != '\\') {
            if (c < 0x20U || c == 0x7fU || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') {
                xx_mem_free(name);
                return NULL;
            }
            continue;
        }
        size = index - start;
        if (size == 2U && source[start] == '.' && source[start + 1U] == '.') {
            xx_mem_free(name);
            return NULL;
        }
        if (size != 0U && !(size == 1U && source[start] == '.')) {
            if (source[index - 1U] == '.' || source[index - 1U] == ' ' || xx_tar_component_is_windows_device(source + start, size)) {
                xx_mem_free(name);
                return NULL;
            }
            if (out != 0U) name[out++] = '/';
            xx_mem_copy(name + out, source + start, size);
            out += size;
        }
        start = index + 1U;
    }
    if (out == 0U) {
        xx_mem_free(name);
        if (is_root) *is_root = true;
        return NULL;
    }
    name[out] = '\0';
    return name;
}

/* A link placeholder is an empty file; its target is never followed, but it
 * must still name an extractable member under the same name policy. */
static bool xx_tar_link_target_safe(const char *linkname)
{
    char *safe;
    if (!linkname || xx_str_len(linkname) > XX_TAR_MAX_NAME_SIZE) return false;
    safe = xx_tar_safe_output_name(linkname, NULL);
    if (!safe) return false;
    xx_mem_free(safe);
    return true;
}

/* TAR writers commonly prefix every member with "./".  Removing only
 * complete leading dot components preserves the archive spelling otherwise
 * and makes the platform-independent safety check unambiguous. */
static void xx_tar_normalize_name(char *name)
{
    size_t length;
    size_t skip = 0U;
    if (!name) return;
    length = xx_str_len(name);
    while (length - skip > 2U && name[skip] == '.' && (name[skip + 1U] == '/' || name[skip + 1U] == '\\')) {
        skip += 2U;
    }
    if (skip != 0U) {
        xx_mem_move(name, name + skip, length - skip + 1U);
    }
}

static bool xx_tar_populate_record(xx_archive_record *record, const xx_tar_member *member)
{
    if (!record || !member || member->metadata) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = XX_TAR_BLOCK_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->logical_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->sparse ? 1U : 0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, member->mtime) && xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->mode) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, member->directory) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           (member->typeflag != '2' || !member->linkname || !member->linkname[0] || xx_archive_record_set_meta_str(record, XX_META_ID_LINK_TARGET, member->linkname)) &&
           (!member->linkname || !member->linkname[0] || xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT, member->linkname));
}

static void xx_tar_archive_stream_free(void *pointer)
{
    if (pointer) xx_mem_free(pointer);
}

void xx_tar_init(xx_tar *tar, xx_io_device *dev, int64_t base_address)
{
    if (!tar) return;
    xx_mem_zero(tar, sizeof(*tar));
    xx_format_init(&tar->format, dev, base_address);
    tar->format.endian = XX_ENDIAN_UNKNOWN;
    tar->format.file_type = XX_FILE_TYPE_TAR;
    tar->format.format_type = XX_TYPE_ARCHIVE;
    tar->format.is_archive = true;
    xx_format_set_mime_type(&tar->format, "application/x-tar");
    xx_format_set_extension(&tar->format, "tar");
    tar->format.check_is_valid = xx_tar_check_is_valid;
    tar->format.handle_base_info = xx_tar_handle_base_info;
    tar->format.get_format_size = xx_tar_get_format_size;
    tar->format.get_number_of_archive_records = xx_tar_get_number_of_archive_records;
    tar->format.create_archive_records_reading = xx_tar_create_archive_records_reading;
    tar->format.get_current_archive_record = xx_tar_get_current_archive_record;
    tar->format.unpack_current_archive_record = xx_tar_unpack_current_archive_record;
    tar->format.archive_record_move_to_next = xx_tar_archive_record_move_to_next;
    tar->format.free_archive_records_reading = xx_tar_free_archive_records_reading;
    tar->format.create_archive_records_writing = xx_tar_create_archive_records_writing;
    tar->format.pack_archive_record = xx_tar_pack_archive_record;
    tar->format.finalize_archive_records_writing = xx_tar_finalize_archive_records_writing;
    tar->format.free_archive_records_writing = xx_tar_free_archive_records_writing;
    tar->format.data_struct_id_to_string = xx_tar_data_struct_id_to_string;
    tar->format.data_struct_string_to_id = xx_tar_data_struct_string_to_id;
    tar->format.create_data_structs_reading = xx_tar_create_data_structs_reading;
    tar->format.get_current_data_struct = xx_tar_get_current_data_struct;
    tar->format.data_struct_move_to_next = xx_tar_data_struct_move_to_next;
    tar->format.free_data_structs_reading = xx_tar_free_data_structs_reading;
    tar->format.create_data_struct_records_reading = xx_tar_create_data_struct_records_reading;
    tar->format.get_current_data_struct_record = xx_tar_get_current_data_struct_record;
    tar->format.data_struct_record_move_to_next = xx_tar_data_struct_record_move_to_next;
    tar->format.free_data_struct_records_reading = xx_tar_free_data_struct_records_reading;
    tar->format.destroy = xx_tar_vtable_destroy;
    tar->archive_end = -1;
}

xx_tar *xx_tar_create(xx_io_device *dev, int64_t base_address)
{
    xx_tar *tar = (xx_tar *)xx_mem_alloc(sizeof(*tar));
    if (tar) xx_tar_init(tar, dev, base_address);
    return tar;
}

void xx_tar_destroy(xx_tar *tar)
{
    if (!tar) return;
    if (tar->internal) {
        xx_tar_private_free(tar->internal);
        tar->internal = NULL;
    }
    if (tar->format.close) tar->format.close(&tar->format);
    xx_format_cleanup_extra_parameters(&tar->format);
}

static void xx_tar_vtable_destroy(Abstractformat *self)
{
    xx_tar_destroy((xx_tar *)self);
}

void xx_tar_free(xx_tar *tar)
{
    if (!tar) return;
    xx_tar_destroy(tar);
    xx_mem_free(tar);
}

bool xx_tar_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_tar_private *priv = NULL;
    bool result;
    result = xx_tar_parse_archive(self, &priv, pd);
    if (priv) xx_tar_private_free(priv);
    return result;
}

bool xx_tar_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_tar *tar;
    xx_tar_private *priv = NULL;
    int64_t total_size;
    int64_t saved_position;
    if (!self || !xx_tar_parse_archive(self, &priv, pd)) {
        if (self) self->is_valid = false;
        return false;
    }
    tar = (xx_tar *)self;
    saved_position = xx_io_tell(self->device);
    total_size = xx_io_total_size(self->device);
    if (saved_position < 0 || total_size < priv->archive_end || xx_io_seek64(self->device, saved_position, SEEK_SET) != 0) {
        xx_tar_private_free(priv);
        self->is_valid = false;
        return false;
    }
    if (tar->internal) xx_tar_private_free(tar->internal);
    tar->internal = priv;
    tar->number_of_members = (uint64_t)priv->count;
    tar->number_of_records = priv->visible_count;
    tar->archive_end = priv->archive_end;
    self->format_size = priv->archive_end - self->base_address;
    self->overlay_offset = priv->archive_end < total_size ? priv->archive_end : -1;
    self->overlay_size = priv->archive_end < total_size ? total_size - priv->archive_end : 0;
    self->number_of_archive_records = priv->visible_count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_tar_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_tar_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_tar *)self)->number_of_records;
}

xx_archive_record_state *xx_tar_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_tar *tar;
    xx_tar_private *priv;
    xx_archive_record_state *state;
    xx_tar_archive_stream *stream;
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) || !self->is_valid) {
        return NULL;
    }
    tar = (xx_tar *)self;
    priv = (xx_tar_private *)tar->internal;
    if (!priv) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_tar_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_tar_copy_options(&state->options, options)) {
        xx_mem_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    while (stream->member_index < priv->count && priv->members[stream->member_index].metadata) {
        ++stream->member_index;
    }
    state->internal_state = stream;
    state->free_internal = xx_tar_archive_stream_free;
    state->total_records = (int64_t)priv->visible_count;
    if (stream->member_index < priv->count && xx_tar_populate_record(&state->current_record, &priv->members[stream->member_index])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_tar_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_tar_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_tar_private *priv;
    xx_tar_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    priv = (xx_tar_private *)((xx_tar *)self)->internal;
    stream = (xx_tar_archive_stream *)state->internal_state;
    if (!priv) return false;
    ++stream->member_index;
    while (stream->member_index < priv->count && priv->members[stream->member_index].metadata) {
        ++stream->member_index;
    }
    if (stream->member_index >= priv->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_tar_populate_record(&state->current_record, &priv->members[stream->member_index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

static bool xx_tar_sparse_write(xx_io_device *output, const uint8_t *bytes, size_t count, xx_pd_struct *pd)
{
    size_t done = 0U;
    if (!output || (pd && xx_pd_is_stopped(pd))) return false;
    while (done < count) {
        ssize_t put = xx_io_write(output, bytes + done, count - done);
        if (put <= 0 || (size_t)put > count - done || (pd && xx_pd_is_stopped(pd))) return false;
        done += (size_t)put;
    }
    return true;
}

static bool xx_tar_sparse_emit(Abstractformat *self, const xx_tar_member *member, xx_io_device *output, xx_pd_struct *pd)
{
    uint8_t *buffer;
    int64_t saved;
    uint64_t logical = 0U;
    size_t i;
    bool result = true;
    if (!self || !self->device || !member || !member->sparse || !output || output == self->device || (pd && xx_pd_is_stopped(pd))) return false;
    saved = xx_io_tell(self->device);
    if (saved < 0) return false;
    buffer = (uint8_t *)xx_mem_alloc(XX_TAR_SPARSE_COPY_BYTES);
    if (!buffer) return false;
    for (i = 0U; i <= member->sparse_count && result; ++i) {
        const xx_tar_sparse_extent *extent = i < member->sparse_count ? &member->sparse_extents[i] : NULL;
        uint64_t end = extent ? extent->offset : member->logical_size;
        uint64_t zero_bytes;
        if (end < logical) {
            result = false;
            break;
        }
        zero_bytes = end - logical;
        xx_rt_memset(buffer, 0, XX_TAR_SPARSE_COPY_BYTES);
        while (zero_bytes && result) {
            size_t n = zero_bytes > XX_TAR_SPARSE_COPY_BYTES ? XX_TAR_SPARSE_COPY_BYTES : (size_t)zero_bytes;
            result = xx_tar_sparse_write(output, buffer, n, pd);
            zero_bytes -= n;
        }
        if (!result || !extent) break;
        if (xx_io_seek64(self->device, extent->packed_offset, SEEK_SET) != 0) {
            result = false;
            break;
        }
        {
            uint64_t remaining = extent->size;
            while (remaining && result) {
                size_t n = remaining > XX_TAR_SPARSE_COPY_BYTES ? XX_TAR_SPARSE_COPY_BYTES : (size_t)remaining;
                size_t done = 0U;
                while (done < n) {
                    ssize_t got = xx_io_read(self->device, buffer + done, n - done);
                    if (got <= 0 || (size_t)got > n - done || (pd && xx_pd_is_stopped(pd))) {
                        result = false;
                        break;
                    }
                    done += (size_t)got;
                }
                if (result) result = xx_tar_sparse_write(output, buffer, n, pd);
                remaining -= n;
            }
        }
        logical = extent->offset + extent->size;
    }
    xx_mem_free(buffer);
    if (xx_io_seek64(self->device, saved, SEEK_SET) != 0) result = false;
    return result && !(pd && xx_pd_is_stopped(pd));
}

static bool xx_tar_sparse_limits(Abstractformat *self, const xx_list_s *options, const xx_tar_member *member)
{
    const xx_var *v;
    uint64_t work = (uint64_t)member->sparse_count * sizeof(xx_tar_sparse_extent) + XX_TAR_SPARSE_COPY_BYTES + 8192U;
    v = xx_format_resolve_extra_parameter(self, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (v && member->logical_size > xx_var_get_u64(v)) return false;
    v = xx_format_resolve_extra_parameter(self, options, XX_META_ID_OPT_MEMORY_LIMIT);
    return !v || xx_var_get_u64(v) >= work;
}

static ssize_t xx_tar_sparse_discard(xx_io_device *output, const void *bytes, size_t n)
{
    (void)output;
    (void)bytes;
    return (ssize_t)n;
}

static bool xx_tar_sparse_same_path(const char *left, const char *right)
{
    while (*left && *right) {
        char a = *left++, b = *right++;
        if (a == '\\') a = '/';
        if (b == '\\') b = '/';
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return false;
    }
    return *left == *right;
}

static xx_io_device *xx_tar_sparse_stage(const char *destination, char **stage)
{
    char *parent = xx_str_dup(destination);
    size_t i, cut = 0U;
    unsigned attempt;
    *stage = NULL;
    if (!parent) return NULL;
    for (i = 0U; parent[i]; ++i)
        if (parent[i] == '/' || parent[i] == '\\') cut = i + 1U;
    parent[cut] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[40], *candidate;
        xx_io_device *device;
        (void)xx_rt_snprintf(suffix, sizeof(suffix), ".xx_tar_sparse.tmp.%u", attempt);
        candidate = xx_str_concat(parent, suffix);
        if (!candidate) break;
        if (xx_tar_sparse_same_path(candidate, destination)) {
            xx_str_free(candidate);
            continue;
        }
        device = xx_io_file_open(candidate, "wbx");
        if (device) {
            *stage = candidate;
            xx_str_free(parent);
            return device;
        }
        xx_str_free(candidate);
    }
    xx_str_free(parent);
    return NULL;
}

static bool xx_tar_sparse_to_path(Abstractformat *self, const xx_tar_member *member, const xx_list_s *options, const char *destination, xx_pd_struct *pd)
{
    const xx_var *v;
    xx_io_device *output = NULL;
    char *stage = NULL;
    bool overwrite, result = false;
    v = xx_format_resolve_extra_parameter(self, options, XX_META_ID_OPT_OVERWRITE);
    overwrite = v && xx_var_get_bool(v);
    if ((!overwrite && xx_io_file_exists_a(destination)) || !xx_store_create_dirs_a(destination, false) || (pd && xx_pd_is_stopped(pd))) return false;
    output = xx_tar_sparse_stage(destination, &stage);
    if (!output) return false;
    result = xx_tar_sparse_emit(self, member, output, pd);
    if (xx_io_close(output) != 0) result = false;
    if (result && !(pd && xx_pd_is_stopped(pd))) result = xx_io_file_replace_a(stage, destination, overwrite);
    else result = false;
    if (!result) (void)xx_io_file_remove_a(stage);
    xx_str_free(stage);
    return result;
}

bool xx_tar_unpack_current_archive_record_as(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd, const char *relative_name)
{
    xx_tar_private *priv;
    xx_tar_archive_stream *stream;
    const xx_tar_member *member;
    const char *output_name;
    const xx_var *path_value;
    const char *base = NULL;
    char *owned_base = NULL;
    char *safe_name = NULL;
    char *destination = NULL;
    bool result = false;
    bool is_root = false;
    bool link_placeholder;
    if (!self || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    priv = (xx_tar_private *)((xx_tar *)self)->internal;
    stream = (xx_tar_archive_stream *)state->internal_state;
    if (!priv || stream->member_index >= priv->count) return false;
    member = &priv->members[stream->member_index];
    output_name = relative_name ? relative_name : member->name;
    /* A link has no independent payload. Test its relative target structure
     * without following it; explicit extraction may create a placeholder. */
    link_placeholder = (member->typeflag == '1' || member->typeflag == '2') && member->data_size == 0;
    if (!member->regular && !member->directory && !link_placeholder) return false;
    if (member->sparse && !xx_tar_sparse_limits(self, &state->options, member)) return false;
    /* Absolute names extract beneath the destination; a name that reduces
     * to nothing ("/", "./") is the destination directory itself. */
    safe_name = xx_tar_safe_output_name(output_name, &is_root);
    if (!safe_name && !(is_root && member->directory)) return false;
    if (link_placeholder && !xx_tar_link_target_safe(member->linkname)) goto cleanup;
    path_value = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        if (link_placeholder) {
            result = true;
        } else if (member->sparse) {
            xx_io_device discard;
            xx_rt_memset(&discard, 0, sizeof(discard));
            discard.write = xx_tar_sparse_discard;
            result = xx_tar_sparse_emit(self, member, &discard, pd);
        } else {
            result = member->directory || (member->data_offset >= 0 && member->data_size >= 0 && member->data_offset <= xx_io_total_size(self->device) &&
                                           member->data_size <= xx_io_total_size(self->device) - member->data_offset);
        }
        goto cleanup;
    }
    if (path_value->type == XX_VAR_TYPE_STRING || path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING || path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (!safe_name) {
        destination = xx_str_dup(base);
    } else if (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", safe_name);
    } else {
        destination = xx_str_concat(base, safe_name);
    }
    if (!destination) goto cleanup;
    if (member->directory) {
        result = xx_store_create_dirs_a(destination, true);
    } else if (link_placeholder && xx_store_create_dirs_a(destination, false)) {
        /* Exclusive creation refuses an existing file or a leaf symlink. */
        xx_io_device *output = xx_io_file_open(destination, "wbx");
        if (output) result = xx_io_close(output) == 0;
    } else if (member->sparse) {
        result = xx_tar_sparse_to_path(self, member, &state->options, destination, pd);
    } else if (xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(self->device, member->data_offset, member->data_size, destination, pd);
    }
cleanup:
    if (destination) xx_str_free(destination);
    if (safe_name) xx_mem_free(safe_name);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

bool xx_tar_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    return xx_tar_unpack_current_archive_record_as(self, state, pd, NULL);
}

void xx_tar_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}

static void xx_tar_write_stream_free(void *pointer)
{
    if (pointer) xx_mem_free(pointer);
}

static bool xx_tar_write_exact(xx_io_device *device, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U)) return false;
    while (done < size) {
        ssize_t amount = xx_io_write(device, bytes + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_tar_write_octal(char *field, size_t size, uint64_t value)
{
    size_t cursor;
    if (!field || size < 2U) return false;
    xx_mem_zero(field, size);
    cursor = size - 1U;
    while (cursor != 0U) {
        field[--cursor] = (char)('0' + (value & 7U));
        value >>= 3U;
    }
    return value == 0U;
}

static bool xx_tar_utf8_is_valid(const char *text)
{
    const uint8_t *p = (const uint8_t *)text;
    if (!p) return false;
    while (*p != 0U) {
        if (*p <= 0x7FU) {
            ++p;
        } else if (*p >= 0xC2U && *p <= 0xDFU && p[1] >= 0x80U && p[1] <= 0xBFU) {
            p += 2;
        } else if (*p == 0xE0U && p[1] >= 0xA0U && p[1] <= 0xBFU && p[2] >= 0x80U && p[2] <= 0xBFU) {
            p += 3;
        } else if (((*p >= 0xE1U && *p <= 0xECU) || (*p >= 0xEEU && *p <= 0xEFU)) && p[1] >= 0x80U && p[1] <= 0xBFU && p[2] >= 0x80U && p[2] <= 0xBFU) {
            p += 3;
        } else if (*p == 0xEDU && p[1] >= 0x80U && p[1] <= 0x9FU && p[2] >= 0x80U && p[2] <= 0xBFU) {
            p += 3;
        } else if (*p == 0xF0U && p[1] >= 0x90U && p[1] <= 0xBFU && p[2] >= 0x80U && p[2] <= 0xBFU && p[3] >= 0x80U && p[3] <= 0xBFU) {
            p += 4;
        } else if (*p >= 0xF1U && *p <= 0xF3U && p[1] >= 0x80U && p[1] <= 0xBFU && p[2] >= 0x80U && p[2] <= 0xBFU && p[3] >= 0x80U && p[3] <= 0xBFU) {
            p += 4;
        } else if (*p == 0xF4U && p[1] >= 0x80U && p[1] <= 0x8FU && p[2] >= 0x80U && p[2] <= 0xBFU && p[3] >= 0x80U && p[3] <= 0xBFU) {
            p += 4;
        } else {
            return false;
        }
    }
    return true;
}

static bool xx_tar_split_ustar_name(const char *path, size_t length, size_t *prefix_length, size_t *name_offset, size_t *name_length)
{
    size_t cursor;
    if (!path || !prefix_length || !name_offset || !name_length || length == 0U) {
        return false;
    }
    if (length <= 100U) {
        *prefix_length = 0U;
        *name_offset = 0U;
        *name_length = length;
        return true;
    }
    cursor = length;
    while (cursor != 0U) {
        --cursor;
        if (path[cursor] == '/') {
            size_t suffix_length = length - cursor - 1U;
            if (cursor != 0U && cursor <= 155U && suffix_length != 0U && suffix_length <= 100U) {
                *prefix_length = cursor;
                *name_offset = cursor + 1U;
                *name_length = suffix_length;
                return true;
            }
        }
    }
    return false;
}

static bool xx_tar_stream_payload_buffered(xx_io_device *source, xx_io_device *destination, uint64_t size, xx_pd_struct *pd, uint8_t *buffer, size_t buffer_capacity)
{
    uint64_t remaining = size;
    if (!source || !destination) return size == 0U;
    while (remaining != 0U) {
        size_t amount = remaining > (uint64_t)buffer_capacity ? buffer_capacity : (size_t)remaining;
        ssize_t received;
        if (pd && xx_pd_is_stopped(pd)) return false;
        received = xx_io_read(source, buffer, amount);
        if (received <= 0 || (size_t)received > amount || !xx_tar_write_exact(destination, buffer, (size_t)received)) {
            return false;
        }
        remaining -= (uint64_t)received;
    }
    return true;
}

static bool xx_tar_stream_payload(xx_io_device *source, xx_io_device *destination, uint64_t size, xx_pd_struct *pd)
{
    size_t buffer_capacity = xx_get_file_buffer_size();
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(buffer_capacity);
    bool buffer_result;
    if (!buffer) return false;
    buffer_result = xx_tar_stream_payload_buffered(source, destination, size, pd, buffer, buffer_capacity);
    xx_mem_free(buffer);
    return buffer_result;
}

static char *xx_tar_record_name_utf8(const xx_archive_record *record)
{
    const char *name;
    const wchar_t *wide_name;
    char *result;
    size_t length;
    if (!record) return NULL;
    name = xx_archive_record_get_original_name(record);
    wide_name = xx_archive_record_get_original_name_w(record);
    if (name && name[0]) {
        result = xx_str_dup(name);
    } else if (wide_name && wide_name[0]) {
        result = xx_str_unicode_to_utf8(wide_name);
    } else {
        return NULL;
    }
    if (!result) return NULL;
    length = xx_str_len(result);
    while (length != 0U) {
        if (result[length - 1U] != '/' && result[length - 1U] != '\\') break;
        result[--length] = '\0';
    }
    for (length = 0U; result[length] != '\0'; ++length) {
        if (result[length] == '\\') result[length] = '/';
    }
    if (!result[0] || !xx_tar_utf8_is_valid(result) || !xx_tar_safe_name(result)) {
        xx_str_free(result);
        return NULL;
    }
    return result;
}

static bool xx_tar_build_ustar_header(xx_tar_header *header, const char *name, uint64_t size, bool directory, uint64_t mode, uint64_t mtime)
{
    size_t length;
    size_t prefix_length;
    size_t name_offset;
    size_t name_length;
    uint64_t checksum;
    if (!header || !name) return false;
    length = xx_str_len(name);
    if (!xx_tar_split_ustar_name(name, length, &prefix_length, &name_offset, &name_length)) {
        return false;
    }
    xx_mem_zero(header, sizeof(*header));
    xx_mem_copy(header->name, name + name_offset, name_length);
    if (prefix_length != 0U) {
        xx_mem_copy(header->prefix, name, prefix_length);
    }
    if (!xx_tar_write_octal(header->mode, sizeof(header->mode), mode) || !xx_tar_write_octal(header->uid, sizeof(header->uid), 0U) ||
        !xx_tar_write_octal(header->gid, sizeof(header->gid), 0U) || !xx_tar_write_octal(header->size, sizeof(header->size), directory ? 0U : size) ||
        !xx_tar_write_octal(header->mtime, sizeof(header->mtime), mtime)) {
        return false;
    }
    xx_rt_memset(header->checksum, ' ', sizeof(header->checksum));
    header->typeflag = directory ? '5' : '0';
    xx_mem_copy(header->magic, "ustar", 5U);
    header->magic[5] = '\0';
    header->version[0] = '0';
    header->version[1] = '0';
    checksum = xx_tar_checksum_unsigned((const uint8_t *)header);
    if (!xx_tar_write_octal(header->checksum, 7U, checksum)) return false;
    header->checksum[7] = ' ';
    return true;
}

xx_archive_write_state *xx_tar_create_archive_records_writing(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_write_state *state;
    xx_tar_write_stream *stream;
    xx_tar *tar;
    if (!self || !self->device || !self->device->write || !self->device->seek || self->base_address < 0 || (uint64_t)self->base_address > (uint64_t)LONG_MAX ||
        (pd && xx_pd_is_stopped(pd)) || xx_io_seek(self->device, (long)self->base_address, SEEK_SET) != 0) {
        return NULL;
    }
    state = (xx_archive_write_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_tar_write_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_write_state_init(state, self);
    if (!xx_tar_copy_options(&state->options, options)) {
        xx_mem_free(stream);
        xx_archive_write_state_free(state);
        return NULL;
    }
    stream->current_offset = self->base_address;
    state->internal_state = stream;
    state->free_internal = xx_tar_write_stream_free;
    state->total_records = 0;

    tar = (xx_tar *)self;
    if (tar->internal) {
        xx_tar_private_free(tar->internal);
        tar->internal = NULL;
    }
    tar->number_of_records = 0U;
    tar->number_of_members = 0U;
    tar->archive_end = -1;
    self->number_of_archive_records = 0U;
    self->format_size = -1;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->is_valid = false;
    self->base_info_handled = false;
    return state;
}

bool xx_tar_pack_archive_record(Abstractformat *self, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev, xx_pd_struct *pd)
{
    static const uint8_t zero_block[XX_TAR_BLOCK_SIZE] = {0};
    xx_tar_write_stream *stream;
    xx_tar_header header;
    char *name;
    const xx_var *declared_size;
    const xx_var *method;
    int64_t source_size_i64 = 0;
    uint64_t source_size = 0U;
    uint64_t padding;
    uint64_t mode;
    uint64_t mtime;
    uint64_t extent;
    bool directory;
    bool name_has_trailing_separator;
    const char *original_name;
    const wchar_t *original_name_w;

    if (!self || !self->device || !state || state->format != self || !state->internal_state || !record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_tar_write_stream *)state->internal_state;
    if (stream->finalized || stream->failed || state->has_record || stream->count >= XX_TAR_MAX_MEMBERS) {
        return false;
    }
    method = xx_archive_record_find_meta(record, XX_META_ID_COMPRESSION_METHOD);
    if ((method && xx_var_get_u64(method) != 0U) || xx_archive_record_get_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false)) {
        return false;
    }

    original_name = xx_archive_record_get_original_name(record);
    original_name_w = xx_archive_record_get_original_name_w(record);
    name_has_trailing_separator =
        (original_name && original_name[0] && (original_name[xx_str_len(original_name) - 1U] == '/' || original_name[xx_str_len(original_name) - 1U] == '\\')) ||
        (original_name_w && original_name_w[0] &&
         (original_name_w[xx_rt_wcslen(original_name_w) - 1U] == L'/' || original_name_w[xx_rt_wcslen(original_name_w) - 1U] == L'\\'));
    directory = xx_archive_record_get_meta_bool(record, XX_META_ID_IS_FOLDER, false) || name_has_trailing_separator;
    if (directory && source_dev) return false;

    declared_size = xx_archive_record_find_meta(record, XX_META_ID_UNCOMPRESSED_SIZE);
    if (!directory && source_dev) {
        source_size_i64 = xx_io_total_size(source_dev);
        if (source_size_i64 < 0 || xx_io_seek(source_dev, 0L, SEEK_SET) != 0) {
            return false;
        }
        source_size = (uint64_t)source_size_i64;
        if (declared_size && xx_var_get_u64(declared_size) != source_size) {
            return false;
        }
    } else if (!directory && declared_size && xx_var_get_u64(declared_size) != 0U) {
        return false;
    }
    padding = directory ? 0U : (XX_TAR_BLOCK_SIZE - (source_size % XX_TAR_BLOCK_SIZE)) % XX_TAR_BLOCK_SIZE;
    if (source_size > UINT64_MAX - padding || source_size + padding > UINT64_MAX - XX_TAR_BLOCK_SIZE) {
        return false;
    }
    extent = XX_TAR_BLOCK_SIZE + source_size + padding;
    if (extent > (uint64_t)INT64_MAX || stream->current_offset < 0 || extent > (uint64_t)(INT64_MAX - stream->current_offset)) {
        return false;
    }

    name = xx_tar_record_name_utf8(record);
    if (!name) return false;
    mode = xx_archive_record_get_meta_u64(record, XX_META_ID_ATTRIBUTES, directory ? 0755U : 0644U);
    mtime = xx_archive_record_get_meta_u64(record, XX_META_ID_TIMESTAMP, 0U);
    if (!xx_tar_build_ustar_header(&header, name, source_size, directory, mode, mtime)) {
        xx_str_free(name);
        return false;
    }
    xx_str_free(name);

    state->has_record = true;
    if (!xx_tar_write_exact(self->device, &header, sizeof(header)) ||
        (!directory && source_size != 0U && !xx_tar_stream_payload(source_dev, self->device, source_size, pd)) ||
        (padding != 0U && !xx_tar_write_exact(self->device, zero_block, (size_t)padding))) {
        state->has_record = false;
        stream->failed = true;
        return false;
    }
    state->has_record = false;
    stream->current_offset += (int64_t)extent;
    ++stream->count;
    state->current_index = (int64_t)stream->count - 1;
    state->total_records = (int64_t)stream->count;
    return true;
}

bool xx_tar_finalize_archive_records_writing(Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd)
{
    static const uint8_t end_records[XX_TAR_END_SIZE] = {0};
    xx_tar_write_stream *stream;
    xx_tar *tar;
    int64_t total_size;
    if (!self || !self->device || !state || state->format != self || !state->internal_state || state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_tar_write_stream *)state->internal_state;
    if (stream->finalized || stream->failed || stream->current_offset > INT64_MAX - XX_TAR_END_SIZE) {
        return false;
    }
    if (!xx_tar_write_exact(self->device, end_records, sizeof(end_records))) {
        stream->failed = true;
        return false;
    }
    stream->current_offset += XX_TAR_END_SIZE;
    stream->finalized = true;

    tar = (xx_tar *)self;
    tar->number_of_records = stream->count;
    tar->number_of_members = stream->count;
    tar->archive_end = stream->current_offset;
    self->format_size = stream->current_offset - self->base_address;
    self->number_of_archive_records = stream->count;
    total_size = xx_io_total_size(self->device);
    if (total_size > stream->current_offset) {
        self->overlay_offset = stream->current_offset;
        self->overlay_size = total_size - stream->current_offset;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->is_valid = true;
    /* No parsed member table exists yet. A later read operation reparses the
     * finished output and installs the ordinary reader state. */
    self->base_info_handled = false;
    return true;
}

void xx_tar_free_archive_records_writing(Abstractformat *self, xx_archive_write_state *state)
{
    (void)self;
    xx_archive_write_state_free(state);
}

typedef struct xx_tar_ds_name_s {
    xx_tar_data_struct_id_t id;
    const char *name;
} xx_tar_ds_name;

static const xx_tar_ds_name xx_tar_ds_names[] = {
    {XX_TAR_DS_UNKNOWN, "UNKNOWN"}, {XX_TAR_DS_HEADER, "HEADER"}, {XX_TAR_DS_DATA, "DATA"}, {XX_TAR_DS_PADDING, "PADDING"}, {XX_TAR_DS_END_MARKERS, "END_MARKERS"}};

const char *xx_tar_data_struct_id_to_string(Abstractformat *self, uint32_t id)
{
    size_t i;
    (void)self;
    for (i = 0U; i < sizeof(xx_tar_ds_names) / sizeof(xx_tar_ds_names[0]); ++i) {
        if ((uint32_t)xx_tar_ds_names[i].id == id) return xx_tar_ds_names[i].name;
    }
    return "UNKNOWN";
}

uint32_t xx_tar_data_struct_string_to_id(Abstractformat *self, const char *name)
{
    size_t i;
    (void)self;
    if (!name) return XX_TAR_DS_UNKNOWN;
    for (i = 0U; i < sizeof(xx_tar_ds_names) / sizeof(xx_tar_ds_names[0]); ++i) {
        if (xx_str_cmp(name, xx_tar_ds_names[i].name) == 0) return (uint32_t)xx_tar_ds_names[i].id;
    }
    return XX_TAR_DS_UNKNOWN;
}

static void xx_tar_ds_stream_free(void *pointer)
{
    xx_tar_ds_stream *stream = (xx_tar_ds_stream *)pointer;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static void xx_tar_set_ds(xx_data_struct *item, uint32_t id, int64_t offset, int64_t size, xx_data_struct_type_t type, bool mapped)
{
    item->id = id;
    item->offset = offset;
    item->address = mapped ? offset : -1;
    item->entry_size = size;
    item->total_size = size;
    item->count = 1U;
    item->type = type;
}

xx_data_struct_state *xx_tar_create_data_structs_reading(Abstractformat *self, xx_pd_struct *pd)
{
    xx_tar_private *priv;
    xx_data_struct_state *state;
    xx_tar_ds_stream *stream;
    size_t capacity;
    size_t i;
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    priv = (xx_tar_private *)((xx_tar *)self)->internal;
    if (!priv || priv->count > (SIZE_MAX - 1U) / 3U) return NULL;
    capacity = priv->count * 3U + 1U;
    if (capacity > SIZE_MAX / sizeof(xx_data_struct)) return NULL;
    state = (xx_data_struct_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_tar_ds_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_data_struct_state_init(state, self);
    stream->items = (xx_data_struct *)xx_mem_alloc(capacity * sizeof(*stream->items));
    if (!stream->items) {
        xx_mem_free(stream);
        xx_data_struct_state_free(state);
        return NULL;
    }
    for (i = 0U; i < priv->count; ++i) {
        const xx_tar_member *member = &priv->members[i];
        int64_t padding = member->padded_size - member->data_size;
        xx_tar_set_ds(&stream->items[stream->count++], XX_TAR_DS_HEADER, member->header_offset, XX_TAR_BLOCK_SIZE, XX_DATA_STRUCT_TYPE_STRUCT, self->is_mapped);
        if (member->data_size != 0) {
            xx_tar_set_ds(&stream->items[stream->count++], XX_TAR_DS_DATA, member->data_offset, member->data_size, XX_DATA_STRUCT_TYPE_RAW_DATA, self->is_mapped);
        }
        if (padding != 0) {
            xx_tar_set_ds(&stream->items[stream->count++], XX_TAR_DS_PADDING, member->data_offset + member->data_size, padding, XX_DATA_STRUCT_TYPE_RAW_DATA,
                          self->is_mapped);
        }
    }
    xx_tar_set_ds(&stream->items[stream->count++], XX_TAR_DS_END_MARKERS, priv->end_marker_offset, priv->archive_end - priv->end_marker_offset,
                  XX_DATA_STRUCT_TYPE_FOOTER, self->is_mapped);
    state->internal_state = stream;
    state->free_internal = xx_tar_ds_stream_free;
    state->total_structs = (int64_t)stream->count;
    state->current_index = 0;
    state->has_struct = stream->count != 0U;
    if (state->has_struct) state->current_struct = stream->items[0];
    return state;
}

const xx_data_struct *xx_tar_get_current_data_struct(Abstractformat *self, xx_data_struct_state *state)
{
    return self && state && state->format == self && state->has_struct ? &state->current_struct : NULL;
}

bool xx_tar_data_struct_move_to_next(Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd)
{
    xx_tar_ds_stream *stream;
    int64_t next;
    if (!self || !state || state->format != self || !state->has_struct || !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_tar_ds_stream *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || (size_t)next >= stream->count) {
        state->has_struct = false;
        return false;
    }
    state->current_index = next;
    state->current_struct = stream->items[next];
    return true;
}

void xx_tar_free_data_structs_reading(Abstractformat *self, xx_data_struct_state *state)
{
    (void)self;
    xx_data_struct_state_free(state);
}

static const xx_data_struct_field_desc xx_tar_header_fields[] = {{L"name", L"char[100]", 0, 100, XX_DATA_STRUCT_RECORD_PROPERTY_STRING},
                                                                 {L"mode", L"char[8]", 100, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
                                                                 {L"uid", L"char[8]", 108, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
                                                                 {L"gid", L"char[8]", 116, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
                                                                 {L"size", L"char[12]", 124, 12, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
                                                                 {L"mtime", L"char[12]", 136, 12, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP},
                                                                 {L"checksum", L"char[8]", 148, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
                                                                 {L"typeflag", L"char", 156, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
                                                                 {L"linkname", L"char[100]", 157, 100, XX_DATA_STRUCT_RECORD_PROPERTY_STRING},
                                                                 {L"magic", L"char[6]", 257, 6, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
                                                                 {L"version", L"char[2]", 263, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
                                                                 {L"uname", L"char[32]", 265, 32, XX_DATA_STRUCT_RECORD_PROPERTY_STRING},
                                                                 {L"gname", L"char[32]", 297, 32, XX_DATA_STRUCT_RECORD_PROPERTY_STRING},
                                                                 {L"devmajor", L"char[8]", 329, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
                                                                 {L"devminor", L"char[8]", 337, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
                                                                 {L"prefix", L"char[155]", 345, 155, XX_DATA_STRUCT_RECORD_PROPERTY_STRING}};

static void xx_tar_record_stream_free(void *pointer)
{
    if (pointer) xx_mem_free(pointer);
}

static bool xx_tar_populate_field(xx_io_device *device, const xx_data_struct *parent, const xx_data_struct_field_desc *field, xx_data_struct_record *record)
{
    char *text;
    wchar_t *display;
    size_t length;
    int64_t total_size;
    if (!device || !parent || !field || !record || field->size < 0 || (uint64_t)field->size > (uint64_t)(SIZE_MAX - 1U)) {
        return false;
    }
    total_size = xx_io_total_size(device);
    if (parent->offset < 0 || field->rel_offset < 0 || parent->offset > INT64_MAX - field->rel_offset || parent->offset + field->rel_offset > total_size ||
        field->size > total_size - (parent->offset + field->rel_offset)) {
        return false;
    }
    length = (size_t)field->size;
    text = (char *)xx_mem_alloc(length + 1U);
    if (!text || !xx_tar_read_exact_at(device, parent->offset + field->rel_offset, text, length)) {
        if (text) xx_mem_free(text);
        return false;
    }
    while (length > 0U && (text[length - 1U] == ' ' || text[length - 1U] == '\0')) {
        --length;
    }
    text[length] = '\0';
    display = xx_str_utf8_to_unicode(text);
    if (!display) {
        xx_mem_free(text);
        return false;
    }
    xx_data_struct_record_init(record);
    record->offset = field->rel_offset;
    record->size = field->size;
    record->property = field->property;
    if (!xx_var_set_str(&record->value, text) || !xx_data_struct_record_set_name(record, field->name) || !xx_data_struct_record_set_type(record, field->type) ||
        !xx_data_struct_record_set_display_value(record, display)) {
        xx_str_wfree(display);
        xx_mem_free(text);
        xx_data_struct_record_cleanup(record);
        return false;
    }
    xx_str_wfree(display);
    xx_mem_free(text);
    return true;
}

xx_data_struct_record_state *xx_tar_create_data_struct_records_reading(Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd)
{
    xx_data_struct_record_state *state;
    xx_tar_record_stream *stream;
    size_t count;
    (void)pd;
    if (!self || !self->device || !ds || ds->id != XX_TAR_DS_HEADER) {
        return NULL;
    }
    count = sizeof(xx_tar_header_fields) / sizeof(xx_tar_header_fields[0]);
    state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_tar_record_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_data_struct_record_state_init(state, self, ds);
    stream->fields = xx_tar_header_fields;
    stream->count = count;
    state->internal_state = stream;
    state->free_internal = xx_tar_record_stream_free;
    state->total_records = (int64_t)count;
    if (xx_tar_populate_field(self->device, ds, &stream->fields[0], &state->current_record)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_data_struct_record *xx_tar_get_current_data_struct_record(Abstractformat *self, xx_data_struct_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_tar_data_struct_record_move_to_next(Abstractformat *self, xx_data_struct_record_state *state, xx_pd_struct *pd)
{
    xx_tar_record_stream *stream;
    int64_t next;
    if (!self || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_tar_record_stream *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || (size_t)next >= stream->count) {
        state->has_record = false;
        return false;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    if (!xx_tar_populate_field(self->device, &state->parent_struct, &stream->fields[next], &state->current_record)) {
        state->has_record = false;
        return false;
    }
    state->current_index = next;
    return true;
}

void xx_tar_free_data_struct_records_reading(Abstractformat *self, xx_data_struct_record_state *state)
{
    (void)self;
    xx_data_struct_record_state_free(state);
}

uint64_t xx_tar_get_number_of_records(const xx_tar *tar)
{
    return tar ? tar->number_of_records : 0U;
}

uint64_t xx_tar_get_number_of_members(const xx_tar *tar)
{
    return tar ? tar->number_of_members : 0U;
}

int64_t xx_tar_get_archive_end(const xx_tar *tar)
{
    return tar ? tar->archive_end : -1;
}
