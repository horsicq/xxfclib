/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * BSD mtree specifications are line-oriented manifests.  The parser below
 * reads their textual grammar directly and deliberately keeps all limits and
 * path handling local to the format implementation.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mtree/xx_mtree.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define XX_MTREE_MAX_LINE_SIZE (1024U * 1024U)
#define XX_MTREE_MAX_CONTINUATIONS 1024U
#define XX_MTREE_MAX_ENTRIES 100000U
#define XX_MTREE_MAX_LINES 1000000U
#define XX_MTREE_MAX_NAME_SIZE 32768U
#define XX_MTREE_READ_CHUNK_SIZE 4096U

typedef enum mtree_key_e {
    MTREE_KEY_CHECKFS = 0,
    MTREE_KEY_CKSUM,
    MTREE_KEY_CONTENT,
    MTREE_KEY_CONTENTS,
    MTREE_KEY_DEVICE,
    MTREE_KEY_FLAGS,
    MTREE_KEY_GID,
    MTREE_KEY_GNAME,
    MTREE_KEY_IGNORE,
    MTREE_KEY_INODE,
    MTREE_KEY_LINK,
    MTREE_KEY_MD5,
    MTREE_KEY_MD5DIGEST,
    MTREE_KEY_MODE,
    MTREE_KEY_NLINK,
    MTREE_KEY_NOCHANGE,
    MTREE_KEY_OPTIONAL,
    MTREE_KEY_RESDEVICE,
    MTREE_KEY_RMD160,
    MTREE_KEY_RMD160DIGEST,
    MTREE_KEY_SHA1,
    MTREE_KEY_SHA1DIGEST,
    MTREE_KEY_SHA256,
    MTREE_KEY_SHA256DIGEST,
    MTREE_KEY_SHA384,
    MTREE_KEY_SHA384DIGEST,
    MTREE_KEY_SHA512,
    MTREE_KEY_SHA512DIGEST,
    MTREE_KEY_SIZE,
    MTREE_KEY_TAGS,
    MTREE_KEY_TIME,
    MTREE_KEY_TYPE,
    MTREE_KEY_UID,
    MTREE_KEY_UNAME,
    MTREE_KEY_COUNT
} mtree_key;

typedef struct mtree_value_s {
    bool present;
    bool has_value;
    char *value;
} mtree_value;

typedef struct mtree_options_s {
    mtree_value values[MTREE_KEY_COUNT];
} mtree_options;

typedef struct mtree_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    bool directory;
    bool has_mode;
    uint32_t mode;
    bool has_uid;
    uint32_t uid;
    bool has_gid;
    uint32_t gid;
    bool has_mtime;
    int64_t mtime;
    mtree_options resolved_options;
} mtree_member;

typedef struct mtree_stream_s {
    mtree_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} mtree_stream;

static const char *const mtree_key_names[MTREE_KEY_COUNT] = {
    "checkfs", "cksum", "content", "contents", "device", "flags",
    "gid", "gname", "ignore", "inode", "link", "md5", "md5digest",
    "mode", "nlink", "nochange", "optional", "resdevice", "rmd160",
    "rmd160digest", "sha1", "sha1digest", "sha256", "sha256digest",
    "sha384", "sha384digest", "sha512", "sha512digest", "size", "tags",
    "time", "type", "uid", "uname"
};

static void mtree_stream_free(void *pointer);
static void mtree_vtable_destroy(Abstractformat *self);

static char *mtree_dup_n(const char *value, size_t length) {
    char *copy;
    if ((!value && length != 0U) || length == SIZE_MAX) return NULL;
    copy = (char *)xx_mem_alloc(length + 1U);
    if (!copy) return NULL;
    if (length != 0U) xx_mem_copy(copy, value, length);
    copy[length] = '\0';
    return copy;
}

static bool mtree_add_i64(int64_t value, int64_t increment,
                          int64_t *result) {
    if (!result || value < 0 || increment < 0 ||
        value > INT64_MAX - increment) {
        return false;
    }
    *result = value + increment;
    return true;
}

static bool mtree_read_relative(Abstractformat *format, int64_t relative,
                                void *buffer, size_t size) {
    int64_t absolute;
    size_t done = 0U;
    if (!format || !format->device || (!buffer && size != 0U) ||
        relative < 0 ||
        !mtree_add_i64(format->base_address, relative, &absolute) ||
        xx_io_seek64(format->device, absolute, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(format->device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool mtree_append(char **buffer, size_t *length, size_t *capacity,
                         const char *data, size_t size) {
    size_t required;
    size_t next_capacity;
    char *next;
    if (!buffer || !length || !capacity || (!data && size != 0U) ||
        *length > XX_MTREE_MAX_LINE_SIZE ||
        size > XX_MTREE_MAX_LINE_SIZE - *length) {
        return false;
    }
    required = *length + size + 1U;
    if (required > *capacity) {
        next_capacity = *capacity ? *capacity : 64U;
        while (next_capacity < required) {
            if (next_capacity > (XX_MTREE_MAX_LINE_SIZE + 1U) / 2U) {
                next_capacity = XX_MTREE_MAX_LINE_SIZE + 1U;
                break;
            }
            next_capacity *= 2U;
        }
        if (next_capacity < required) return false;
        next = (char *)xx_mem_realloc(*buffer, next_capacity);
        if (!next) return false;
        *buffer = next;
        *capacity = next_capacity;
    }
    if (size != 0U) xx_mem_copy(*buffer + *length, data, size);
    *length += size;
    (*buffer)[*length] = '\0';
    return true;
}

static bool mtree_read_physical_line(Abstractformat *format,
                                     int64_t relative_offset, char **line,
                                     size_t *line_length,
                                     int64_t *next_offset,
                                     bool *had_newline, xx_pd_struct *pd) {
    char *result = NULL;
    size_t result_length = 0U;
    size_t result_capacity = 0U;
    int64_t total_size;
    int64_t archive_size;
    int64_t current;
    if (!format || !line || !line_length || !next_offset || !had_newline ||
        relative_offset < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    *line = NULL;
    *line_length = 0U;
    *next_offset = relative_offset;
    *had_newline = false;
    total_size = xx_io_total_size(format->device);
    if (total_size < format->base_address) return false;
    archive_size = total_size - format->base_address;
    if (relative_offset >= archive_size) return false;
    current = relative_offset;
    while (current < archive_size && result_length < XX_MTREE_MAX_LINE_SIZE) {
        uint8_t chunk[XX_MTREE_READ_CHUNK_SIZE];
        int64_t remaining = archive_size - current;
        size_t chunk_size = remaining > (int64_t)sizeof(chunk)
                                ? sizeof(chunk)
                                : (size_t)remaining;
        size_t index;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !mtree_read_relative(format, current, chunk, chunk_size)) {
            xx_mem_free(result);
            return false;
        }
        for (index = 0U; index < chunk_size; ++index) {
            if (chunk[index] == '\n') break;
        }
        if (!mtree_append(&result, &result_length, &result_capacity,
                          (const char *)chunk, index)) {
            xx_mem_free(result);
            return false;
        }
        if (index < chunk_size) {
            if (!mtree_add_i64(current, (int64_t)index + 1,
                               next_offset)) {
                xx_mem_free(result);
                return false;
            }
            *had_newline = true;
            if (result_length != 0U && result[result_length - 1U] == '\r') {
                result[--result_length] = '\0';
            }
            *line = result;
            *line_length = result_length;
            return true;
        }
        if (!mtree_add_i64(current, (int64_t)chunk_size, &current)) {
            xx_mem_free(result);
            return false;
        }
    }
    if (current != archive_size || result_length > XX_MTREE_MAX_LINE_SIZE ||
        !mtree_append(&result, &result_length, &result_capacity, NULL, 0U)) {
        xx_mem_free(result);
        return false;
    }
    *line = result;
    *line_length = result_length;
    *next_offset = archive_size;
    return true;
}

static bool mtree_read_logical_line(Abstractformat *format,
                                    int64_t relative_offset, char **line,
                                    size_t *line_length,
                                    int64_t *next_offset,
                                    xx_pd_struct *pd) {
    char *result = NULL;
    size_t result_length = 0U;
    size_t result_capacity = 0U;
    int64_t current = relative_offset;
    unsigned continuation;
    if (!format || !line || !line_length || !next_offset ||
        relative_offset < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    *line = NULL;
    *line_length = 0U;
    *next_offset = relative_offset;
    for (continuation = 0U; continuation <= XX_MTREE_MAX_CONTINUATIONS;
         ++continuation) {
        char *physical = NULL;
        size_t physical_length = 0U;
        int64_t physical_next = 0;
        bool had_newline = false;
        size_t trailing = 0U;
        bool continues;
        if (!mtree_read_physical_line(format, current, &physical,
                                      &physical_length, &physical_next,
                                      &had_newline, pd) ||
            physical_next <= current) {
            xx_mem_free(physical);
            xx_mem_free(result);
            return false;
        }
        while (trailing < physical_length &&
               physical[physical_length - trailing - 1U] == '\\') {
            ++trailing;
        }
        continues = (trailing & 1U) != 0U;
        if (continues) --physical_length;
        if (!mtree_append(&result, &result_length, &result_capacity,
                          physical, physical_length)) {
            xx_mem_free(physical);
            xx_mem_free(result);
            return false;
        }
        xx_mem_free(physical);
        current = physical_next;
        if (!continues) {
            *line = result;
            *line_length = result_length;
            *next_offset = current;
            return true;
        }
        if (!had_newline) {
            xx_mem_free(result);
            return false;
        }
    }
    xx_mem_free(result);
    return false;
}

static char *mtree_next_token(char **cursor) {
    char *start;
    if (!cursor || !*cursor) return NULL;
    while (**cursor == ' ' || **cursor == '\t') ++*cursor;
    if (!**cursor) return NULL;
    start = *cursor;
    while (**cursor && **cursor != ' ' && **cursor != '\t') ++*cursor;
    if (**cursor) *(*cursor)++ = '\0';
    return start;
}

static int mtree_key_from_name(const char *name) {
    size_t index;
    if (!name || !name[0]) return -1;
    for (index = 0U; index < MTREE_KEY_COUNT; ++index) {
        if (xx_rt_strcmp(name, mtree_key_names[index]) == 0) return (int)index;
    }
    return -1;
}

static bool mtree_key_is_flag(mtree_key key) {
    return key == MTREE_KEY_CHECKFS || key == MTREE_KEY_IGNORE ||
           key == MTREE_KEY_NOCHANGE || key == MTREE_KEY_OPTIONAL;
}

static void mtree_options_cleanup(mtree_options *options) {
    size_t index;
    if (!options) return;
    for (index = 0U; index < MTREE_KEY_COUNT; ++index) {
        xx_mem_free(options->values[index].value);
    }
    xx_mem_zero(options, sizeof(*options));
}

static bool mtree_options_set(mtree_options *options, mtree_key key,
                              bool has_value, const char *value) {
    char *copy = NULL;
    if (!options || key < 0 || key >= MTREE_KEY_COUNT ||
        (has_value && !value)) {
        return false;
    }
    if (has_value) {
        copy = mtree_dup_n(value, xx_rt_strlen(value));
        if (!copy) return false;
    }
    xx_mem_free(options->values[key].value);
    options->values[key].present = true;
    options->values[key].has_value = has_value;
    options->values[key].value = copy;
    return true;
}

static bool mtree_options_copy(mtree_options *destination,
                               const mtree_options *source) {
    size_t index;
    if (!destination || !source) return false;
    mtree_options_cleanup(destination);
    for (index = 0U; index < MTREE_KEY_COUNT; ++index) {
        if (source->values[index].present &&
            !mtree_options_set(destination, (mtree_key)index,
                               source->values[index].has_value,
                               source->values[index].value)) {
            mtree_options_cleanup(destination);
            return false;
        }
    }
    return true;
}

static bool mtree_options_overlay(mtree_options *destination,
                                  const mtree_options *source) {
    size_t index;
    if (!destination || !source) return false;
    for (index = 0U; index < MTREE_KEY_COUNT; ++index) {
        if (source->values[index].present &&
            !mtree_options_set(destination, (mtree_key)index,
                               source->values[index].has_value,
                               source->values[index].value)) {
            return false;
        }
    }
    return true;
}

static bool mtree_parse_unsigned(const char *value, unsigned base,
                                 uint64_t maximum, uint64_t *result) {
    uint64_t number = 0U;
    size_t index;
    if (!value || !value[0] || !result || (base != 8U && base != 10U)) {
        return false;
    }
    for (index = 0U; value[index]; ++index) {
        unsigned digit;
        if (value[index] < '0' || value[index] > '9') return false;
        digit = (unsigned)(value[index] - '0');
        if (digit >= base || digit > maximum ||
            number > (maximum - digit) / base) return false;
        number = number * base + digit;
    }
    *result = number;
    return true;
}

static bool mtree_parse_time(const char *value, int64_t *seconds) {
    const char *fraction;
    size_t index = 0U;
    bool negative = false;
    uint64_t number = 0U;
    uint64_t maximum;
    if (!value || !value[0] || !seconds) return false;
    if (value[index] == '-') {
        negative = true;
        ++index;
    }
    if (!value[index]) return false;
    maximum = negative ? (uint64_t)INT64_MAX + 1U : (uint64_t)INT64_MAX;
    while (value[index] && value[index] != '.') {
        unsigned digit;
        if (value[index] < '0' || value[index] > '9') return false;
        digit = (unsigned)(value[index++] - '0');
        if (number > (maximum - digit) / 10U) return false;
        number = number * 10U + digit;
    }
    fraction = value + index;
    if (*fraction == '.') {
        ++fraction;
        if (!*fraction) return false;
        while (*fraction) {
            if (*fraction < '0' || *fraction > '9') return false;
            ++fraction;
        }
    }
    if (negative) {
        *seconds = number == (uint64_t)INT64_MAX + 1U
                       ? INT64_MIN : -(int64_t)number;
    } else {
        *seconds = (int64_t)number;
    }
    return true;
}

static bool mtree_is_hex(const char *value, size_t expected_length) {
    size_t index;
    if (!value || xx_rt_strlen(value) != expected_length) return false;
    for (index = 0U; index < expected_length; ++index) {
        char c = value[index];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

static bool mtree_validate_option(mtree_key key, bool has_value,
                                  const char *value) {
    uint64_t number;
    int64_t seconds;
    if (key == MTREE_KEY_CONTENT || key == MTREE_KEY_CONTENTS ||
        key == MTREE_KEY_CHECKFS || key == MTREE_KEY_LINK ||
        key == MTREE_KEY_DEVICE || key == MTREE_KEY_RESDEVICE) {
        return false;
    }
    if (key == MTREE_KEY_IGNORE || key == MTREE_KEY_NOCHANGE ||
        key == MTREE_KEY_OPTIONAL) {
        return !has_value;
    }
    if (!has_value || !value || !value[0]) return false;
    switch (key) {
        case MTREE_KEY_TYPE:
            return xx_rt_strcmp(value, "file") == 0 || xx_rt_strcmp(value, "dir") == 0;
        case MTREE_KEY_SIZE:
            return mtree_parse_unsigned(value, 10U, (uint64_t)INT64_MAX,
                                        &number) && number == 0U;
        case MTREE_KEY_MODE:
            return mtree_parse_unsigned(value, 8U, 07777U, &number);
        case MTREE_KEY_UID:
        case MTREE_KEY_GID:
        case MTREE_KEY_NLINK:
            return mtree_parse_unsigned(value, 10U, UINT32_MAX, &number);
        case MTREE_KEY_INODE:
        case MTREE_KEY_CKSUM:
            return mtree_parse_unsigned(value, 10U, UINT64_MAX, &number);
        case MTREE_KEY_TIME:
            return mtree_parse_time(value, &seconds);
        case MTREE_KEY_MD5:
        case MTREE_KEY_MD5DIGEST:
            return mtree_is_hex(value, 32U);
        case MTREE_KEY_RMD160:
        case MTREE_KEY_RMD160DIGEST:
        case MTREE_KEY_SHA1:
        case MTREE_KEY_SHA1DIGEST:
            return mtree_is_hex(value, 40U);
        case MTREE_KEY_SHA256:
        case MTREE_KEY_SHA256DIGEST:
            return mtree_is_hex(value, 64U);
        case MTREE_KEY_SHA384:
        case MTREE_KEY_SHA384DIGEST:
            return mtree_is_hex(value, 96U);
        case MTREE_KEY_SHA512:
        case MTREE_KEY_SHA512DIGEST:
            return mtree_is_hex(value, 128U);
        case MTREE_KEY_FLAGS:
        case MTREE_KEY_GNAME:
        case MTREE_KEY_TAGS:
        case MTREE_KEY_UNAME:
            return true;
        default:
            return false;
    }
}

static bool mtree_parse_option(char *token, mtree_key *key,
                               bool *has_value, char **value) {
    char *equals;
    int index;
    if (!token || !key || !has_value || !value || !token[0]) return false;
    equals = xx_rt_strchr(token, '=');
    if (equals) *equals = '\0';
    index = mtree_key_from_name(token);
    if (index < 0) return false;
    *key = (mtree_key)index;
    *has_value = equals != NULL;
    *value = equals ? equals + 1U : NULL;
    if ((mtree_key_is_flag(*key) && *has_value) ||
        (!mtree_key_is_flag(*key) && (!*has_value || !(*value)[0])) ||
        !mtree_validate_option(*key, *has_value, *value)) {
        return false;
    }
    return true;
}

static bool mtree_decode_escapes(const char *input, char **output,
                                 size_t *output_length) {
    char *decoded = NULL;
    size_t decoded_length = 0U;
    size_t decoded_capacity = 0U;
    size_t input_length;
    size_t index = 0U;
    if (!input || !output || !output_length) return false;
    *output = NULL;
    *output_length = 0U;
    input_length = xx_rt_strlen(input);
    while (index < input_length) {
        char c = input[index++];
        if (c != '\\') {
            if (!mtree_append(&decoded, &decoded_length, &decoded_capacity,
                              &c, 1U)) goto fail;
            continue;
        }
        if (index >= input_length) goto fail;
        c = input[index];
        if (c >= '0' && c <= '3') {
            unsigned value;
            if (index + 2U >= input_length || input[index + 1U] < '0' ||
                input[index + 1U] > '7' || input[index + 2U] < '0' ||
                input[index + 2U] > '7') {
                goto fail;
            }
            value = ((unsigned)(c - '0') << 6U) |
                    ((unsigned)(input[index + 1U] - '0') << 3U) |
                    (unsigned)(input[index + 2U] - '0');
            c = (char)value;
            index += 3U;
        } else {
            switch (c) {
                case 'a': c = '\a'; ++index; break;
                case 'b': c = '\b'; ++index; break;
                case 'f': c = '\f'; ++index; break;
                case 'n': c = '\n'; ++index; break;
                case 'r': c = '\r'; ++index; break;
                case 's': c = ' '; ++index; break;
                case 't': c = '\t'; ++index; break;
                case 'v': c = '\v'; ++index; break;
                case '\\': c = '\\'; ++index; break;
                default:
                    c = '\\';
                    break;
            }
        }
        if (!mtree_append(&decoded, &decoded_length, &decoded_capacity,
                          &c, 1U)) goto fail;
    }
    *output = decoded;
    *output_length = decoded_length;
    return true;
fail:
    xx_mem_free(decoded);
    return false;
}

static bool mtree_utf8_is_valid(const char *text, size_t length) {
    size_t index = 0U;
    if (!text) return false;
    while (index < length) {
        uint8_t first = (uint8_t)text[index];
        if (first <= 0x7fU) {
            ++index;
        } else if (first >= 0xc2U && first <= 0xdfU && index + 1U < length &&
                   (uint8_t)text[index + 1U] >= 0x80U &&
                   (uint8_t)text[index + 1U] <= 0xbfU) {
            index += 2U;
        } else if (first == 0xe0U && index + 2U < length &&
                   (uint8_t)text[index + 1U] >= 0xa0U &&
                   (uint8_t)text[index + 1U] <= 0xbfU &&
                   (uint8_t)text[index + 2U] >= 0x80U &&
                   (uint8_t)text[index + 2U] <= 0xbfU) {
            index += 3U;
        } else if (((first >= 0xe1U && first <= 0xecU) ||
                    (first >= 0xeeU && first <= 0xefU)) &&
                   index + 2U < length &&
                   (uint8_t)text[index + 1U] >= 0x80U &&
                   (uint8_t)text[index + 1U] <= 0xbfU &&
                   (uint8_t)text[index + 2U] >= 0x80U &&
                   (uint8_t)text[index + 2U] <= 0xbfU) {
            index += 3U;
        } else if (first == 0xedU && index + 2U < length &&
                   (uint8_t)text[index + 1U] >= 0x80U &&
                   (uint8_t)text[index + 1U] <= 0x9fU &&
                   (uint8_t)text[index + 2U] >= 0x80U &&
                   (uint8_t)text[index + 2U] <= 0xbfU) {
            index += 3U;
        } else if (first == 0xf0U && index + 3U < length &&
                   (uint8_t)text[index + 1U] >= 0x90U &&
                   (uint8_t)text[index + 1U] <= 0xbfU &&
                   (uint8_t)text[index + 2U] >= 0x80U &&
                   (uint8_t)text[index + 2U] <= 0xbfU &&
                   (uint8_t)text[index + 3U] >= 0x80U &&
                   (uint8_t)text[index + 3U] <= 0xbfU) {
            index += 4U;
        } else if (first >= 0xf1U && first <= 0xf3U && index + 3U < length &&
                   (uint8_t)text[index + 1U] >= 0x80U &&
                   (uint8_t)text[index + 1U] <= 0xbfU &&
                   (uint8_t)text[index + 2U] >= 0x80U &&
                   (uint8_t)text[index + 2U] <= 0xbfU &&
                   (uint8_t)text[index + 3U] >= 0x80U &&
                   (uint8_t)text[index + 3U] <= 0xbfU) {
            index += 4U;
        } else if (first == 0xf4U && index + 3U < length &&
                   (uint8_t)text[index + 1U] >= 0x80U &&
                   (uint8_t)text[index + 1U] <= 0x8fU &&
                   (uint8_t)text[index + 2U] >= 0x80U &&
                   (uint8_t)text[index + 2U] <= 0xbfU &&
                   (uint8_t)text[index + 3U] >= 0x80U &&
                   (uint8_t)text[index + 3U] <= 0xbfU) {
            index += 4U;
        } else {
            return false;
        }
    }
    return true;
}

static bool mtree_name_equal(const char *left, const char *right) {
    size_t index = 0U;
    if (!left || !right) return false;
    while (left[index] && right[index]) {
        unsigned char a = (unsigned char)left[index];
        unsigned char b = (unsigned char)right[index];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return false;
        ++index;
    }
    return left[index] == '\0' && right[index] == '\0';
}

static bool mtree_output_component_is_safe(const char *path, size_t length) {
    size_t index;
    if (!path || length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char c = (unsigned char)path[index];
        if (c < 0x20U || c == 0x7fU || c == '\\' || c == ':' || c == '<' ||
            c == '>' || c == '"' || c == '|' || c == '?' || c == '*') {
            return false;
        }
    }
    return true;
}

static char *mtree_join_path(const char *prefix, const char *name) {
    size_t prefix_length;
    size_t name_length;
    char *result;
    if (!prefix || !name) return NULL;
    prefix_length = xx_rt_strlen(prefix);
    name_length = xx_rt_strlen(name);
    if (prefix_length == 0U) return mtree_dup_n(name, name_length);
    if (name_length > SIZE_MAX - prefix_length - 2U) return NULL;
    result = (char *)xx_mem_alloc(prefix_length + 1U + name_length + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, prefix, prefix_length);
    result[prefix_length] = '/';
    xx_mem_copy(result + prefix_length + 1U, name, name_length);
    result[prefix_length + 1U + name_length] = '\0';
    return result;
}

static bool mtree_make_safe_path(const char *raw_name,
                                 const char *current_directory,
                                 char **result, bool *is_full) {
    char *decoded = NULL;
    size_t decoded_length = 0U;
    char *path = NULL;
    size_t index;
    bool full;
    if (!raw_name || !result || !is_full || !raw_name[0] ||
        xx_rt_strlen(raw_name) > XX_MTREE_MAX_NAME_SIZE) {
        return false;
    }
    *result = NULL;
    if (!mtree_decode_escapes(raw_name, &decoded, &decoded_length) ||
        decoded_length == 0U || xx_rt_memchr(decoded, '\0', decoded_length) ||
        !mtree_utf8_is_valid(decoded, decoded_length) ||
        !mtree_output_component_is_safe(decoded, decoded_length)) {
        xx_mem_free(decoded);
        return false;
    }
    full = xx_rt_strcmp(raw_name, ".") == 0 || xx_rt_strchr(raw_name, '/') != NULL;
    *is_full = full;
    if (xx_rt_strcmp(decoded, ".") == 0) {
        *result = decoded;
        return true;
    }
    if (full) {
        if (decoded_length >= 2U && decoded[0] == '.' && decoded[1] == '/') {
            path = mtree_dup_n(decoded + 2U, decoded_length - 2U);
        } else {
            path = mtree_dup_n(decoded, decoded_length);
        }
    } else {
        path = mtree_join_path(current_directory ? current_directory : "",
                               decoded);
    }
    xx_mem_free(decoded);
    if (!path || !path[0] || path[0] == '/') {
        xx_mem_free(path);
        return false;
    }
    for (index = 0U; ; ++index) {
        size_t start = index;
        while (path[index] && path[index] != '/') ++index;
        if (index == start ||
            (index - start == 1U && path[start] == '.') ||
            (index - start == 2U && path[start] == '.' && path[start + 1U] == '.')) {
            xx_mem_free(path);
            return false;
        }
        if (!path[index]) break;
    }
    *result = path;
    return true;
}

static void mtree_member_cleanup(mtree_member *member) {
    if (!member) return;
    xx_mem_free(member->name);
    mtree_options_cleanup(&member->resolved_options);
    xx_mem_zero(member, sizeof(*member));
}

static void mtree_stream_free(void *pointer) {
    mtree_stream *stream = (mtree_stream *)pointer;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        mtree_member_cleanup(&stream->items[index]);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static int mtree_find_member(const mtree_stream *stream, const char *name) {
    size_t index;
    if (!stream || !name) return -1;
    for (index = 0U; index < stream->count; ++index) {
        if (mtree_name_equal(stream->items[index].name, name)) return (int)index;
    }
    return -1;
}

static bool mtree_store_member(mtree_stream *stream, mtree_member *member) {
    int existing;
    if (!stream || !member || !member->name) return false;
    existing = mtree_find_member(stream, member->name);
    if (existing >= 0) {
        mtree_member_cleanup(&stream->items[existing]);
        stream->items[existing] = *member;
        xx_mem_zero(member, sizeof(*member));
        return true;
    }
    if (stream->count >= XX_MTREE_MAX_ENTRIES) return false;
    if (stream->count == stream->capacity) {
        size_t capacity = stream->capacity ? stream->capacity * 2U : 16U;
        mtree_member *items;
        if (capacity < stream->capacity ||
            capacity > SIZE_MAX / sizeof(*items)) return false;
        items = (mtree_member *)xx_mem_realloc(
            stream->items, capacity * sizeof(*items));
        if (!items) return false;
        stream->items = items;
        stream->capacity = capacity;
    }
    stream->items[stream->count++] = *member;
    xx_mem_zero(member, sizeof(*member));
    return true;
}

static void mtree_pop_directory(char *path) {
    char *slash;
    if (!path) return;
    slash = xx_rt_strrchr(path, '/');
    if (slash) *slash = '\0';
    else path[0] = '\0';
}

static bool mtree_parse_member_fields(mtree_member *member,
                                      const mtree_options *options) {
    const mtree_value *value;
    uint64_t number;
    if (!member || !options) return false;
    value = &options->values[MTREE_KEY_TYPE];
    if (!value->present || !value->has_value || !value->value) return false;
    member->directory = xx_rt_strcmp(value->value, "dir") == 0;
    if (!member->directory && xx_rt_strcmp(value->value, "file") != 0) return false;
    value = &options->values[MTREE_KEY_MODE];
    if (value->present) {
        if (!value->has_value ||
            !mtree_parse_unsigned(value->value, 8U, 07777U, &number)) {
            return false;
        }
        member->has_mode = true;
        member->mode = (uint32_t)number;
    }
    value = &options->values[MTREE_KEY_UID];
    if (value->present) {
        if (!value->has_value ||
            !mtree_parse_unsigned(value->value, 10U, UINT32_MAX, &number)) {
            return false;
        }
        member->has_uid = true;
        member->uid = (uint32_t)number;
    }
    value = &options->values[MTREE_KEY_GID];
    if (value->present) {
        if (!value->has_value ||
            !mtree_parse_unsigned(value->value, 10U, UINT32_MAX, &number)) {
            return false;
        }
        member->has_gid = true;
        member->gid = (uint32_t)number;
    }
    value = &options->values[MTREE_KEY_TIME];
    if (value->present) {
        if (!value->has_value || !mtree_parse_time(value->value,
                                                    &member->mtime)) {
            return false;
        }
        member->has_mtime = true;
    }
    return true;
}

static bool mtree_copy_options(xx_list_s *destination,
                               const xx_list_s *source) {
    size_t index;
    if (!destination) return false;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *mtree_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool mtree_scan(Abstractformat *format, mtree_stream **result,
                       xx_pd_struct *pd) {
    mtree_stream *stream = NULL;
    mtree_options defaults;
    char *current_directory = NULL;
    int64_t total_size;
    int64_t archive_size;
    int64_t offset = 0;
    unsigned line_count = 0U;
    char *line = NULL;
    size_t line_length = 0U;
    int64_t next_offset = 0;
    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    *result = NULL;
    xx_mem_zero(&defaults, sizeof(defaults));
    total_size = xx_io_total_size(format->device);
    if (total_size < format->base_address) goto fail;
    archive_size = total_size - format->base_address;
    if (archive_size < 6) goto fail;
    stream = (mtree_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream || !mtree_read_logical_line(format, 0, &line, &line_length,
                                            &next_offset, pd) ||
        xx_rt_strcmp(line, "#mtree") != 0 || next_offset <= 0) {
        goto fail;
    }
    xx_mem_free(line);
    line = NULL;
    offset = next_offset;
    line_count = 1U;
    while (offset < archive_size) {
        int64_t line_offset = offset;
        char *cursor;
        char *first;
        if (line_count >= XX_MTREE_MAX_LINES ||
            !mtree_read_logical_line(format, line_offset, &line, &line_length,
                                     &next_offset, pd) ||
            next_offset <= line_offset || next_offset > archive_size) {
            goto fail;
        }
        offset = next_offset;
        ++line_count;
        cursor = line;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (!*cursor || *cursor == '#') {
            xx_mem_free(line);
            line = NULL;
            continue;
        }
        {
            size_t index;
            for (index = 0U; cursor[index]; ++index) {
                unsigned char c = (unsigned char)cursor[index];
                if ((c < 0x20U && c != '\t') || c == 0x7fU) goto fail;
            }
        }
        first = mtree_next_token(&cursor);
        if (!first) goto fail;
        if (xx_rt_strcmp(first, "/set") == 0) {
            char *token;
            bool saw_option = false;
            while ((token = mtree_next_token(&cursor)) != NULL) {
                mtree_key key;
                bool has_value;
                char *value;
                if (!mtree_parse_option(token, &key, &has_value, &value) ||
                    !mtree_options_set(&defaults, key, has_value, value)) {
                    goto fail;
                }
                saw_option = true;
            }
            if (!saw_option) goto fail;
            xx_mem_free(line);
            line = NULL;
            continue;
        }
        if (xx_rt_strcmp(first, "/unset") == 0) {
            char *token;
            bool saw_key = false;
            while ((token = mtree_next_token(&cursor)) != NULL) {
                int key;
                if (xx_rt_strchr(token, '=')) goto fail;
                if (xx_rt_strcmp(token, "all") == 0) {
                    mtree_options_cleanup(&defaults);
                } else if ((key = mtree_key_from_name(token)) >= 0) {
                    xx_mem_free(defaults.values[key].value);
                    xx_mem_zero(&defaults.values[key],
                                sizeof(defaults.values[key]));
                } else {
                    goto fail;
                }
                saw_key = true;
            }
            if (!saw_key) goto fail;
            xx_mem_free(line);
            line = NULL;
            continue;
        }
        {
            char *decoded_name = NULL;
            size_t decoded_length = 0U;
            char *path = NULL;
            bool is_full = false;
            mtree_options line_options;
            mtree_options final_options;
            mtree_member member;
            int existing;
            size_t stored_index;
            char *token;
            xx_mem_zero(&line_options, sizeof(line_options));
            xx_mem_zero(&final_options, sizeof(final_options));
            xx_mem_zero(&member, sizeof(member));
            if (!mtree_decode_escapes(first, &decoded_name, &decoded_length) ||
                decoded_length == 0U ||
                xx_rt_memchr(decoded_name, '\0', decoded_length) ||
                !mtree_utf8_is_valid(decoded_name, decoded_length)) {
                xx_mem_free(decoded_name);
                goto fail;
            }
            if (xx_rt_strcmp(decoded_name, "..") == 0) {
                if (mtree_next_token(&cursor) != NULL) {
                    xx_mem_free(decoded_name);
                    goto fail;
                }
                mtree_pop_directory(current_directory);
                xx_mem_free(decoded_name);
                xx_mem_free(line);
                line = NULL;
                continue;
            }
            xx_mem_free(decoded_name);
            if (!mtree_options_copy(&line_options, &defaults)) {
                mtree_options_cleanup(&line_options);
                goto fail;
            }
            while ((token = mtree_next_token(&cursor)) != NULL) {
                mtree_key key;
                bool has_value;
                char *value;
                if (!mtree_parse_option(token, &key, &has_value, &value) ||
                    !mtree_options_set(&line_options, key, has_value, value)) {
                    mtree_options_cleanup(&line_options);
                    goto fail;
                }
            }
            if (!mtree_make_safe_path(first, current_directory, &path,
                                      &is_full)) {
                mtree_options_cleanup(&line_options);
                goto fail;
            }
            if (xx_rt_strcmp(path, ".") == 0) {
                const mtree_value *type =
                    &line_options.values[MTREE_KEY_TYPE];
                bool root_valid = type->present && type->has_value &&
                                  type->value && xx_rt_strcmp(type->value, "dir") == 0;
                xx_mem_free(path);
                mtree_options_cleanup(&line_options);
                if (!root_valid) goto fail;
                xx_mem_free(line);
                line = NULL;
                continue;
            }
            existing = mtree_find_member(stream, path);
            if (existing >= 0) {
                if (!mtree_options_copy(&final_options,
                                        &stream->items[existing].resolved_options) ||
                    !mtree_options_overlay(&final_options, &line_options)) {
                    xx_mem_free(path);
                    mtree_options_cleanup(&line_options);
                    mtree_options_cleanup(&final_options);
                    goto fail;
                }
            } else if (!mtree_options_copy(&final_options, &line_options)) {
                xx_mem_free(path);
                mtree_options_cleanup(&line_options);
                goto fail;
            }
            mtree_options_cleanup(&line_options);
            member.name = path;
            member.header_offset = format->base_address + line_offset;
            member.header_size = next_offset - line_offset;
            member.data_offset = format->base_address + next_offset;
            if (member.header_offset < 0 || member.header_size <= 0 ||
                member.data_offset < 0 ||
                !mtree_options_copy(&member.resolved_options, &final_options) ||
                !mtree_parse_member_fields(&member, &member.resolved_options) ||
                !mtree_store_member(stream, &member)) {
                mtree_member_cleanup(&member);
                mtree_options_cleanup(&final_options);
                goto fail;
            }
            mtree_options_cleanup(&final_options);
            stored_index = existing >= 0 ? (size_t)existing : stream->count - 1U;
            if (!is_full && stream->items[stored_index].directory) {
                char *next_directory = mtree_dup_n(
                    stream->items[stored_index].name,
                    xx_rt_strlen(stream->items[stored_index].name));
                if (!next_directory) goto fail;
                xx_mem_free(current_directory);
                current_directory = next_directory;
            }
        }
        xx_mem_free(line);
        line = NULL;
    }
    mtree_options_cleanup(&defaults);
    xx_mem_free(current_directory);
    stream->archive_size = offset;
    *result = stream;
    return true;
fail:
    xx_mem_free(line);
    mtree_options_cleanup(&defaults);
    xx_mem_free(current_directory);
    mtree_stream_free(stream);
    return false;
}

static bool mtree_set_record(xx_archive_record *record,
                             const mtree_member *member) {
    if (!record || !member || !member->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = 0;
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        0U) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        0U) ||
        !xx_archive_record_set_meta_u64(record,
                                        XX_META_ID_COMPRESSION_METHOD, 0U) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                         member->directory) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false)) {
        return false;
    }
    if (member->has_mode &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                        member->mode)) {
        return false;
    }
    return !member->has_mtime || member->mtime < 0 ||
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          (uint64_t)member->mtime);
}

void xx_mtree_init(xx_mtree *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_UNKNOWN;
    archive->format.file_type = XX_FILE_TYPE_MTREE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-mtree");
    xx_format_set_extension(&archive->format, "mtree");
    archive->format.check_is_valid = xx_mtree_check_is_valid;
    archive->format.handle_base_info = xx_mtree_handle_base_info;
    archive->format.get_format_size = xx_mtree_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_mtree_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_mtree_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_mtree_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_mtree_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_mtree_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_mtree_free_archive_records_reading;
    archive->format.destroy = mtree_vtable_destroy;
    archive->archive_end = -1;
}

xx_mtree *xx_mtree_create(xx_io_device *device, int64_t base_address) {
    xx_mtree *archive = (xx_mtree *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_mtree_init(archive, device, base_address);
    return archive;
}

void xx_mtree_destroy(xx_mtree *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

static void mtree_vtable_destroy(Abstractformat *self) {
    xx_mtree_destroy((xx_mtree *)self);
}

void xx_mtree_free(xx_mtree *archive) {
    if (!archive) return;
    xx_mtree_destroy(archive);
    xx_mem_free(archive);
}

bool xx_mtree_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    mtree_stream *stream = NULL;
    bool result = mtree_scan(self, &stream, pd);
    mtree_stream_free(stream);
    return result;
}

bool xx_mtree_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    mtree_stream *stream = NULL;
    xx_mtree *archive;
    int64_t archive_end;
    int64_t total_size;
    if (!self || !mtree_scan(self, &stream, pd) ||
        !mtree_add_i64(self->base_address, stream->archive_size,
                       &archive_end)) {
        if (stream) mtree_stream_free(stream);
        if (self) self->is_valid = false;
        return false;
    }
    archive = (xx_mtree *)self;
    total_size = xx_io_total_size(self->device);
    archive->number_of_records = (uint64_t)stream->count;
    archive->archive_end = archive_end;
    self->file_type = XX_FILE_TYPE_MTREE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->number_of_archive_records = (uint64_t)stream->count;
    self->format_size = stream->archive_size;
    self->overlay_offset = archive_end < total_size ? archive_end : -1;
    self->overlay_size = archive_end < total_size ? total_size - archive_end : 0;
    self->is_valid = true;
    self->base_info_handled = true;
    mtree_stream_free(stream);
    return true;
}

int64_t xx_mtree_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_mtree_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_mtree_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_mtree_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_mtree *)self)->number_of_records;
}

xx_archive_record_state *xx_mtree_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    mtree_stream *stream = NULL;
    xx_archive_record_state *state;
    if (!self || (!self->base_info_handled &&
                  !xx_mtree_handle_base_info(self, pd)) ||
        !self->is_valid || !mtree_scan(self, &stream, pd)) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        mtree_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = mtree_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!mtree_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !mtree_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->count != 0U) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_mtree_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_mtree_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    mtree_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (mtree_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    ++stream->index;
    if (stream->index >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!mtree_set_record(&state->current_record, &stream->items[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_mtree_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    mtree_stream *stream;
    const mtree_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    bool created = false;
    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (mtree_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    member = &stream->items[stream->index];
    path_option = mtree_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    destination = (base[0] && base[xx_rt_strlen(base) - 1U] != '/' &&
                   base[xx_rt_strlen(base) - 1U] != '\\')
                      ? xx_str_concat3(base, "/", member->name)
                      : xx_str_concat(base, member->name);
    if (!destination) goto done;
    if (member->directory) {
        result = xx_store_create_dirs_a(destination, true);
    } else if (xx_store_create_dirs_a(destination, false)) {
        xx_io_device *output = xx_io_file_open(destination, "wb");
        created = output != NULL;
        if (output) {
            result = xx_io_close(output) == 0;
            if (!result && created) xx_rt_remove(destination);
        }
    }
done:
    if (destination) xx_str_free(destination);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_mtree_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
