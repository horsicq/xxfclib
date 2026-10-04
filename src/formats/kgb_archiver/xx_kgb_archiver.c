/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * KGB Archiver 2 (.kgb).  xx_kgb_archiver.h carries the field table, which
 * was measured on archives made by kgb2_console.exe (2 beta 1) with every
 * algorithm, with and without a password.
 *
 * The file table is plain in every archive, so all members are listed.
 * Algorithm 0 stores the data as is and is extracted here, each member
 * checked against the byte sum its entry carries. Algorithms 1..7 are
 * decoded in the separately licensed, bounded RAM-only codec helper.
 * Every member checksum is verified before any output file is opened.
 *
 * Hostile input: the entry count is bounded by what the file can hold, each
 * member size by 1 TiB and its byte sum by 255 * size, names must be
 * NUL-terminated inside their 260-unit field, and extraction refuses names
 * with separators, drive colons, control or reserved characters, names that
 * resolve to "." or "..", and Windows device names.  Later members whose
 * name equals an earlier one get an index suffix so nothing is overwritten.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/kgb_archiver/xx_kgb_archiver.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/aes/xx_aes.h"

#include <limits.h>
#include <stdio.h>

/* The private codec pipe operates only on this explicitly bounded blob. */
typedef struct ac_blob {
    uint8_t *p;
    uint32_t n;
    uint64_t used, limit;
    xx_pd_struct *pd;
} ac_blob;
static bool ac_error(ac_blob *b, const char *why) {
    xx_pd_set_error(b->pd, 1, why);
    return false;
}
#include "../xx_archive_codec_pipe.h"

/* Registration placeholder: picks up the real file type as soon as
 * KGB_ARCHIVER is registered in xxfc_defs.h. */
#ifdef KGB_ARCHIVER
#define XX_KGB_ARCHIVER_FILE_TYPE XX_FILE_TYPE_KGB_ARCHIVER
#else
#define XX_KGB_ARCHIVER_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define KGB_RECORD_SIZE 560U
#define KGB_NAME_OFFSET 40U
#define KGB_NAME_UNITS 260U
#define KGB_NAME_BUFFER 800U /* 259 units * 3 UTF-8 bytes + NUL fits */
#define KGB_MAX_ALGORITHM 7U
#define KGB_MAX_MODE 9U
#define KGB_MAX_COUNT 1000000U
#define KGB_MAX_MEMBER ((int64_t)1 << 40)
#define KGB_ENCRYPTED_PREFIX 8
#define KGB_COPY_CHUNK 65536U

typedef struct kgb_item {
    char *name;
    uint64_t size;
    uint64_t sum;
    int64_t mtime;
    int64_t data_offset; /* absolute; -1 when not addressable */
    uint32_t attrib;
    uint32_t index;
    bool unsafe;   /* name refused for extraction */
    bool renamed;  /* duplicate: an index suffix was added */
    bool blocked;  /* still collides after renaming */
} kgb_item;

typedef struct kgb_header {
    uint32_t count;
    uint8_t algorithm;
    uint8_t mode;
    bool encrypted;
    bool truncated;
    int64_t entries_end; /* relative to the base */
    int64_t data_offset; /* relative to the base */
    int64_t format_size;
    uint64_t total;
} kgb_header;

typedef struct kgb_stream {
    kgb_header header;
    kgb_item *items;
    uint32_t count;
    uint32_t index;
    uint8_t *decoded;
    bool decoded_valid;
} kgb_stream;

static uint32_t kgb_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t kgb_le64(const uint8_t *p) {
    return (uint64_t)kgb_le32(p) | ((uint64_t)kgb_le32(p + 4) << 32);
}

static bool kgb_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static size_t kgb_put_utf8(char *out, uint32_t cp) {
    if (cp < 0x80U) {
        out[0] = (char)cp;
        return 1U;
    }
    if (cp < 0x800U) {
        out[0] = (char)(0xC0U | (cp >> 6));
        out[1] = (char)(0x80U | (cp & 0x3FU));
        return 2U;
    }
    if (cp < 0x10000U) {
        out[0] = (char)(0xE0U | (cp >> 12));
        out[1] = (char)(0x80U | ((cp >> 6) & 0x3FU));
        out[2] = (char)(0x80U | (cp & 0x3FU));
        return 3U;
    }
    out[0] = (char)(0xF0U | (cp >> 18));
    out[1] = (char)(0x80U | ((cp >> 12) & 0x3FU));
    out[2] = (char)(0x80U | ((cp >> 6) & 0x3FU));
    out[3] = (char)(0x80U | (cp & 0x3FU));
    return 4U;
}

/* Decodes the NUL-terminated UTF-16LE name field.  False when there is no
 * terminator inside the field or the name is empty.  *unsafe is set when a
 * character makes the name unusable as one path component; an unpaired
 * surrogate becomes '_'. */
static bool kgb_decode_name(const uint8_t *field, char *out, size_t *length,
                            bool *unsafe) {
    size_t unit = 0U, used = 0U;
    bool bad = false;
    for (;;) {
        uint32_t cp;
        if (unit >= KGB_NAME_UNITS) return false;
        cp = (uint32_t)field[2U * unit] | ((uint32_t)field[2U * unit + 1U] << 8);
        ++unit;
        if (cp == 0U) break;
        if (cp >= 0xD800U && cp <= 0xDBFFU && unit < KGB_NAME_UNITS) {
            uint32_t low = (uint32_t)field[2U * unit] |
                           ((uint32_t)field[2U * unit + 1U] << 8);
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                cp = 0x10000U + ((cp - 0xD800U) << 10) + (low - 0xDC00U);
                ++unit;
            } else {
                cp = '_';
            }
        } else if (cp >= 0xD800U && cp <= 0xDFFFU) {
            cp = '_';
        }
        if (cp < 0x20U || cp == 0x7FU || cp == '/' || cp == '\\' ||
            cp == ':' || cp == '<' || cp == '>' || cp == '"' || cp == '|' ||
            cp == '?' || cp == '*')
            bad = true;
        if (used + 4U >= KGB_NAME_BUFFER) return false;
        used += kgb_put_utf8(out + used, cp);
    }
    if (used == 0U) return false;
    out[used] = 0;
    *length = used;
    *unsafe = bad;
    return true;
}

static char kgb_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* A Windows device name as the part of the name before its first '.',
 * trailing spaces ignored. */
static bool kgb_reserved_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",    "AUX",
                                          "NUL",    "CLOCK$", "CONIN$",
                                          "CONOUT$"};
    char stem[8];
    size_t stem_length = 0U, index;
    while (name[stem_length] != 0 && name[stem_length] != '.') ++stem_length;
    while (stem_length > 0U && name[stem_length - 1U] == ' ') --stem_length;
    if (stem_length < 3U || stem_length > sizeof(stem) - 1U) return false;
    for (index = 0U; index < stem_length; ++index)
        stem[index] = kgb_upper(name[index]);
    stem[stem_length] = 0;
    /* COM0-9 / LPT0-9, and the superscript digits 1-3 (UTF-8 C2 B9/B2/B3)
     * that Windows also maps to those devices. */
    if (((stem_length == 4U && stem[3] >= '0' && stem[3] <= '9') ||
         (stem_length == 5U && (unsigned char)stem[3] == 0xC2U &&
          ((unsigned char)stem[4] == 0xB9U || (unsigned char)stem[4] == 0xB2U ||
           (unsigned char)stem[4] == 0xB3U))) &&
        ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') ||
         (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T')))
        return true;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (xx_str_len(devices[index]) == stem_length &&
            xx_rt_memcmp(stem, devices[index], stem_length) == 0)
            return true;
    return false;
}

static bool kgb_safe_name(const char *name) {
    size_t length, index;
    bool meaningful = false;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    for (index = 0U; index < length; ++index)
        if (name[index] != '.' && name[index] != ' ') meaningful = true;
    if (!meaningful || name[length - 1U] == '.' || name[length - 1U] == ' ')
        return false;
    return !kgb_reserved_name(name);
}

/* ---- parsing ------------------------------------------------------------ */

static void kgb_free_items(kgb_item *items, uint32_t count) {
    uint32_t index;
    if (!items) return;
    for (index = 0U; index < count; ++index)
        if (items[index].name) xx_mem_free(items[index].name);
    xx_mem_free(items);
}

/* Parses the header and every entry.  With @p items_out NULL nothing is
 * allocated (the probe path). */
static bool kgb_parse(Abstractformat *format, kgb_header *out,
                      kgb_item **items_out) {
    uint8_t head[XX_KGB_ARCHIVER_HEADER_SIZE];
    uint8_t entry[XX_KGB_ARCHIVER_ENTRY_SIZE];
    char name[KGB_NAME_BUFFER];
    static const uint8_t magic[8] = {0x4B, 0, 0x47, 0, 0x42, 0, 0x32, 0};
    kgb_header header;
    kgb_item *items = NULL;
    int64_t total_size, avail, offset;
    uint32_t index;
    if (items_out) *items_out = NULL;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total_size = xx_io_total_size(format->device);
    if (total_size < format->base_address) return false;
    avail = total_size - format->base_address;
    if (avail < (int64_t)(XX_KGB_ARCHIVER_HEADER_SIZE +
                          XX_KGB_ARCHIVER_ENTRY_SIZE) ||
        !kgb_read_at(format->device, format->base_address, head, sizeof(head)) ||
        xx_rt_memcmp(head, magic, sizeof(magic)) != 0 || head[8] > 1U ||
        head[9] > KGB_MAX_ALGORITHM || head[10] > KGB_MAX_MODE)
        return false;
    xx_mem_zero(&header, sizeof(header));
    header.encrypted = head[8] != 0U;
    header.algorithm = head[9];
    header.mode = head[10];
    header.count = kgb_le32(head + 11);
    if (header.count == 0U || header.count > KGB_MAX_COUNT ||
        (int64_t)header.count >
            (avail - XX_KGB_ARCHIVER_HEADER_SIZE) / XX_KGB_ARCHIVER_ENTRY_SIZE)
        return false;
    header.entries_end = XX_KGB_ARCHIVER_HEADER_SIZE +
                         (int64_t)header.count * XX_KGB_ARCHIVER_ENTRY_SIZE;
    if (items_out) {
        items = (kgb_item *)xx_mem_calloc(header.count, sizeof(*items));
        if (!items) return false;
    }
    offset = format->base_address + XX_KGB_ARCHIVER_HEADER_SIZE;
    for (index = 0U; index < header.count; ++index) {
        const uint8_t *record = entry + 4;
        uint64_t size, sum;
        uint32_t attrib;
        size_t length;
        bool unsafe;
        if (!kgb_read_at(format->device, offset, entry, sizeof(entry)) ||
            kgb_le32(entry) != KGB_RECORD_SIZE)
            goto fail;
        attrib = kgb_le32(record);
        size = kgb_le64(record + 32);
        sum = kgb_le64(entry + 4 + KGB_RECORD_SIZE);
        if ((attrib & 0xFF000000U) != 0U || size > (uint64_t)KGB_MAX_MEMBER ||
            sum > size * 255U ||
            !kgb_decode_name(record + KGB_NAME_OFFSET, name, &length, &unsafe))
            goto fail;
        header.total += size;
        if (items) {
            kgb_item *item = &items[index];
            item->name = (char *)xx_mem_alloc(length + 1U);
            if (!item->name) goto fail;
            xx_rt_memcpy(item->name, name, length + 1U);
            item->size = size;
            item->sum = sum;
            item->attrib = attrib;
            item->mtime = (int64_t)kgb_le64(record + 24);
            item->index = index;
            item->unsafe = unsafe || !kgb_safe_name(name);
            item->data_offset = -1;
        }
        offset += XX_KGB_ARCHIVER_ENTRY_SIZE;
    }
    header.data_offset = header.entries_end;
    if (header.encrypted) {
        /* An 8-byte value, then the block-encrypted data. */
        header.data_offset += KGB_ENCRYPTED_PREFIX;
        if (header.data_offset > avail) goto fail;
        header.format_size = avail;
    } else if (header.algorithm == XX_KGB_ARCHIVER_METHOD_STORED) {
        int64_t end = header.data_offset;
        header.truncated = header.total > (uint64_t)(avail - end);
        header.format_size = header.truncated ? avail : end + (int64_t)header.total;
        if (items) {
            /* end never passes avail: a member that does not fit is left
             * unaddressable and every later one then fits only if empty. */
            for (index = 0U; index < header.count; ++index) {
                if (items[index].size > (uint64_t)(avail - end)) {
                    end = avail;
                    continue;
                }
                items[index].data_offset = format->base_address + end;
                end += (int64_t)items[index].size;
            }
        }
    } else {
        /* One solid stream whose end only its decoder knows. */
        if (header.total != 0U && avail <= header.data_offset) goto fail;
        header.format_size = avail;
    }
    *out = header;
    if (items_out) *items_out = items;
    return true;
fail:
    kgb_free_items(items, header.count);
    return false;
}

/* ---- duplicate names ---------------------------------------------------- */

static int kgb_compare_folded(const char *left, const char *right) {
    for (;; ++left, ++right) {
        char a = kgb_upper(*left), b = kgb_upper(*right);
        if (a != b) return (unsigned char)a < (unsigned char)b ? -1 : 1;
        if (!a) return 0;
    }
}

/* Folded name, then original names before suffixed ones, then index: in a
 * group of equal names the first keeps its name. */
static int kgb_compare_items(const void *left, const void *right) {
    const kgb_item *a = *(const kgb_item *const *)left;
    const kgb_item *b = *(const kgb_item *const *)right;
    int result = kgb_compare_folded(a->name, b->name);
    if (result) return result;
    if (a->renamed != b->renamed) return a->renamed ? 1 : -1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* "stem.ext" -> "stem_<index>.ext" (suffix before the last '.'). */
static bool kgb_add_suffix(kgb_item *item) {
    char suffix[16];
    size_t length = xx_str_len(item->name), dot = length, suffix_length;
    char *renamed;
    size_t index;
    for (index = length; index > 0U; --index)
        if (item->name[index - 1U] == '.') {
            dot = index - 1U;
            break;
        }
    if (dot == 0U) dot = length;
    (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%u", (unsigned)item->index);
    suffix_length = xx_str_len(suffix);
    renamed = (char *)xx_mem_alloc(length + suffix_length + 1U);
    if (!renamed) return false;
    xx_rt_memcpy(renamed, item->name, dot);
    xx_rt_memcpy(renamed + dot, suffix, suffix_length);
    xx_rt_memcpy(renamed + dot + suffix_length, item->name + dot,
                 length - dot + 1U);
    xx_mem_free(item->name);
    item->name = renamed;
    item->renamed = true;
    return true;
}

#define KGB_RENAME_ROUNDS 8U

/* Later members whose name equals an earlier one (ASCII case folded) get
 * "_<index>" before the extension.  A suffixed name can meet a genuine one
 * ("a_3.txt"), so this repeats; whatever still collides after the last
 * round is blocked from extraction rather than overwriting anything. */
static bool kgb_resolve_duplicates(kgb_item *items, uint32_t count) {
    kgb_item **order;
    uint32_t index, round;
    if (count < 2U) return true;
    order = (kgb_item **)xx_mem_alloc((size_t)count * sizeof(*order));
    if (!order) return false;
    for (round = 0U; round <= KGB_RENAME_ROUNDS; ++round) {
        bool any = false;
        for (index = 0U; index < count; ++index) order[index] = &items[index];
        xx_rt_qsort(order, count, sizeof(*order), kgb_compare_items);
        for (index = 1U; index < count; ++index) {
            if (kgb_compare_folded(order[index]->name,
                                   order[index - 1U]->name) != 0)
                continue;
            any = true;
            if (round == KGB_RENAME_ROUNDS) {
                order[index]->blocked = true;
            } else if (!kgb_add_suffix(order[index])) {
                xx_mem_free(order);
                return false;
            }
        }
        if (!any) break;
    }
    xx_mem_free(order);
    return true;
}

/* ---- extraction --------------------------------------------------------- */

static bool kgb_decode_stream(Abstractformat *format,
                              xx_archive_record_state *state,
                              kgb_stream *stream, xx_pd_struct *pd) {
    ac_blob blob;
    const xx_var *option;
    uint64_t metadata = sizeof(*stream), at = 0U;
    uint32_t index;
    uint8_t key[32] = {0}, iv[16] = {0};
    bool result = false;
    xx_mem_zero(&blob, sizeof(blob));
    if (stream->decoded_valid) return true;
    blob.pd = pd;
    /* Algorithms 4/5 have fixed workspaces larger than 256 MiB. */
    blob.limit = UINT64_C(512) * 1024U * 1024U;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MEMORY_LIMIT);
    if (option) blob.limit = xx_var_get_u64(option);
    for (index = 0U; index < stream->count; ++index)
        metadata += sizeof(kgb_item) + xx_str_len(stream->items[index].name) + 1U;
    if (stream->header.format_size <= 0 ||
        stream->header.format_size > 64 * 1024 * 1024 ||
        stream->header.total > 64U * 1024U * 1024U ||
        metadata > blob.limit ||
        (uint64_t)stream->header.format_size > blob.limit - metadata ||
        stream->header.total + 1U > blob.limit - metadata -
                                     (uint64_t)stream->header.format_size)
        return ac_error(&blob, "KGB solid stream exceeds archive memory limit");
    blob.n = (uint32_t)stream->header.format_size;
    blob.used = metadata + blob.n + stream->header.total + 1U;
    blob.p = (uint8_t *)xx_mem_alloc(blob.n);
    stream->decoded = (uint8_t *)xx_mem_alloc((size_t)stream->header.total + 1U);
    if (!blob.p || !stream->decoded ||
        !kgb_read_at(format->device, format->base_address, blob.p, blob.n))
        goto done;
    if (pd && xx_pd_is_stopped(pd)) goto done;
    if (stream->header.encrypted) {
        const uint8_t *password = NULL;
        size_t password_size = 0U;
        char *converted = NULL;
        uint64_t sum = 0U, expected;
        uint32_t begin = (uint32_t)stream->header.data_offset;
        uint32_t packed = blob.n - begin;
        option = xx_format_resolve_extra_parameter(format, &state->options,
                                                   XX_META_ID_OPT_PASSWORD);
        if (!option || (packed & 15U)) {
            ac_error(&blob, "KGB password required or encrypted payload damaged");
            goto done;
        }
        if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) {
            password = (const uint8_t *)xx_var_get_str(option);
            if (password) password_size = xx_str_len((const char *)password);
        } else if (option->type == XX_VAR_TYPE_BYTES || option->type == XX_VAR_TYPE_BYTES_VIEW) {
            password = (const uint8_t *)xx_var_get_bytes(option, &password_size);
        } else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
            converted = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
            password = (const uint8_t *)converted;
            if (converted) password_size = xx_str_len(converted);
        }
        if (password_size > sizeof(key)) password_size = sizeof(key);
        if (password_size && !password) {
            if (converted) xx_str_free(converted);
            goto done;
        }
        if (password_size) xx_rt_memcpy(key, password, password_size);
        if (converted) xx_str_free(converted);
        expected = kgb_le64(blob.p + begin - 8U);
        for (uint32_t block = 0U; block < packed; block += 16U) {
            if ((pd && xx_pd_is_stopped(pd)) ||
                !xx_aes_cbc_decrypt(blob.p + begin + block, 16U, key,
                                    sizeof(key), iv, blob.p + begin + block))
                goto done;
            for (unsigned byte = 0U; byte < 16U; ++byte)
                sum += blob.p[begin + block + byte];
        }
        if (sum != expected) {
            ac_error(&blob, "KGB password incorrect or encrypted payload damaged");
            goto done;
        }
        /* The helper flag 2 means authenticated ECB padding may remain. */
        xx_rt_memmove(blob.p + begin - 8U, blob.p + begin, packed);
        blob.n -= 8U;
        blob.p[8] = 2U;
    }
    if (stream->header.algorithm == XX_KGB_ARCHIVER_METHOD_STORED) {
        uint32_t begin = (uint32_t)stream->header.entries_end;
        if (stream->header.total > blob.n - begin ||
            blob.n - begin - stream->header.total > 15U) goto done;
        xx_rt_memcpy(stream->decoded, blob.p + begin, (size_t)stream->header.total);
    } else if (!af_decode(&blob, stream->header.algorithm + 6U,
                           stream->decoded, (uint32_t)stream->header.total))
        goto done;
    for (index = 0U; index < stream->count; ++index) {
        uint64_t sum = 0U;
        for (uint64_t byte = 0U; byte < stream->items[index].size; ++byte) {
            if (!(byte & 65535U) && pd && xx_pd_is_stopped(pd)) goto done;
            sum += stream->decoded[at + byte];
        }
        if (sum != stream->items[index].sum) {
            ac_error(&blob, "KGB member checksum mismatch");
            goto done;
        }
        at += stream->items[index].size;
    }
    stream->decoded_valid = result = true;
done:
    xx_mem_zero(key, sizeof(key));
    if (blob.p) {
        if (stream->header.encrypted) xx_mem_zero(blob.p, blob.n);
        xx_mem_free(blob.p);
    }
    if (!result) {
        if (stream->decoded) xx_mem_zero(stream->decoded, (size_t)stream->header.total);
        xx_mem_free(stream->decoded);
        stream->decoded = NULL;
    }
    return result;
}

static bool kgb_extract(Abstractformat *format, const kgb_stream *stream,
                        const kgb_item *item, xx_io_device *destination,
                        xx_pd_struct *pd) {
    uint8_t *buffer;
    uint64_t remaining, sum = 0U;
    int64_t offset;
    bool result = false;
    if (!format || !stream || !item) return false;
    if (stream->decoded_valid) {
        uint64_t begin = 0U;
        for (uint32_t index = 0U; index < item->index; ++index)
            begin += stream->items[index].size;
        remaining = item->size;
        while (remaining) {
            size_t chunk = remaining > KGB_COPY_CHUNK ? KGB_COPY_CHUNK : (size_t)remaining;
            if (pd && xx_pd_is_stopped(pd)) return false;
            if (destination) {
                size_t done = 0U;
                while (done < chunk) {
                    ssize_t wrote = xx_io_write(destination, stream->decoded + begin + done, chunk - done);
                    if (wrote <= 0 || (size_t)wrote > chunk - done) return false;
                    done += (size_t)wrote;
                }
            }
            begin += chunk;
            remaining -= chunk;
        }
        return true;
    }
    if (stream->header.encrypted ||
        stream->header.algorithm != XX_KGB_ARCHIVER_METHOD_STORED ||
        item->data_offset < 0) return false;
    if (item->size == 0U) return item->sum == 0U;
    buffer = (uint8_t *)xx_mem_alloc(KGB_COPY_CHUNK);
    if (!buffer) return false;
    remaining = item->size;
    offset = item->data_offset;
    while (remaining) {
        size_t chunk = remaining > KGB_COPY_CHUNK ? KGB_COPY_CHUNK
                                                  : (size_t)remaining;
        size_t index;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !kgb_read_at(format->device, offset, buffer, chunk))
            goto done;
        for (index = 0U; index < chunk; ++index) sum += buffer[index];
        if (destination) {
            size_t written = 0U;
            while (written < chunk) {
                ssize_t amount = xx_io_write(destination, buffer + written,
                                             chunk - written);
                if (amount <= 0 || (size_t)amount > chunk - written) goto done;
                written += (size_t)amount;
            }
        }
        offset += (int64_t)chunk;
        remaining -= chunk;
    }
    result = sum == item->sum;
done:
    xx_mem_free(buffer);
    return result;
}

static void kgb_stream_free(void *opaque) {
    kgb_stream *stream = (kgb_stream *)opaque;
    if (!stream) return;
    kgb_free_items(stream->items, stream->count);
    if (stream->decoded && stream->header.encrypted)
        xx_mem_zero(stream->decoded, (size_t)stream->header.total);
    xx_mem_free(stream->decoded);
    xx_mem_free(stream);
}

static bool kgb_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static bool kgb_set_record(Abstractformat *format, xx_archive_record *record,
                           const kgb_stream *stream) {
    const kgb_item *item = &stream->items[stream->index];
    bool stored = !stream->header.encrypted &&
                  stream->header.algorithm == XX_KGB_ARCHIVER_METHOD_STORED;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + XX_KGB_ARCHIVER_HEADER_SIZE +
                            (int64_t)item->index * XX_KGB_ARCHIVER_ENTRY_SIZE;
    record->header_size = XX_KGB_ARCHIVER_ENTRY_SIZE;
    record->data_offset = item->data_offset >= 0
                              ? item->data_offset
                              : format->base_address + stream->header.data_offset;
    record->compressed_size = stored ? (int64_t)item->size : 0;
    if (!xx_archive_record_set_original_name(record, item->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        item->size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        stream->header.algorithm) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                        item->attrib) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         stream->header.encrypted) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (stored &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        item->size))
        return false;
    if (item->mtime >= 0 &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                        (uint64_t)item->mtime))
        return false;
    return true;
}

/* ---- public API --------------------------------------------------------- */

void xx_kgb_archiver_init(xx_kgb_archiver *archive, xx_io_device *device,
                          int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_KGB_ARCHIVER_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-kgb-archive");
    xx_format_set_extension(&archive->format, "kgb");
    archive->format.check_is_valid = xx_kgb_archiver_check_is_valid;
    archive->format.handle_base_info = xx_kgb_archiver_handle_base_info;
    archive->format.get_format_size = xx_kgb_archiver_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_kgb_archiver_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_kgb_archiver_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_kgb_archiver_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_kgb_archiver_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_kgb_archiver_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_kgb_archiver_free_archive_records_reading;
    archive->data_offset = -1;
}

xx_kgb_archiver *xx_kgb_archiver_create(xx_io_device *device,
                                        int64_t base_address) {
    xx_kgb_archiver *archive =
        (xx_kgb_archiver *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_kgb_archiver_init(archive, device, base_address);
    return archive;
}

void xx_kgb_archiver_destroy(xx_kgb_archiver *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_kgb_archiver_free(xx_kgb_archiver *archive) {
    if (!archive) return;
    xx_kgb_archiver_destroy(archive);
    xx_mem_free(archive);
}

bool xx_kgb_archiver_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    kgb_header header;
    (void)pd;
    return kgb_parse(format, &header, NULL);
}

bool xx_kgb_archiver_handle_base_info(Abstractformat *format,
                                      xx_pd_struct *pd) {
    kgb_header header;
    xx_kgb_archiver *archive;
    (void)pd;
    if (!format || !kgb_parse(format, &header, NULL)) return false;
    archive = (xx_kgb_archiver *)format;
    archive->number_of_records = header.count;
    archive->total_unpacked_size = header.total;
    archive->data_offset = format->base_address + header.data_offset;
    archive->algorithm = header.algorithm;
    archive->mode = header.mode;
    archive->encrypted = header.encrypted;
    format->number_of_archive_records = header.count;
    format->format_size = header.format_size;
    format->is_crypted = header.encrypted;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_kgb_archiver_get_format_size(Abstractformat *format,
                                        xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_kgb_archiver_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_kgb_archiver_get_number_of_archive_records(Abstractformat *format,
                                                       xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_kgb_archiver_handle_base_info(format, pd))
               ? ((xx_kgb_archiver *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_kgb_archiver_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    kgb_stream *stream;
    xx_archive_record_state *state;
    kgb_header header;
    kgb_item *items = NULL;
    (void)pd;
    if (!kgb_parse(format, &header, &items)) return NULL;
    stream = (kgb_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) {
        kgb_free_items(items, header.count);
        return NULL;
    }
    stream->header = header;
    stream->items = items;
    stream->count = header.count;
    if (!kgb_resolve_duplicates(items, header.count)) {
        kgb_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        kgb_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = kgb_stream_free;
    state->total_records = header.count;
    state->current_index = 0;
    if (!kgb_copy_options(&state->options, options) ||
        !kgb_set_record(format, &state->current_record, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_kgb_archiver_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_kgb_archiver_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    kgb_stream *stream;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (kgb_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd)) || stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    state->current_index = stream->index;
    if (!kgb_set_record(format, &state->current_record, stream)) {
        state->has_record = false;
        return false;
    }
    return true;
}

bool xx_kgb_archiver_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    kgb_stream *stream;
    const kgb_item *item;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (kgb_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    item = &stream->items[stream->index];
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && item->size > xx_var_get_u64(option)) return false;
    if ((stream->header.encrypted ||
         stream->header.algorithm != XX_KGB_ARCHIVER_METHOD_STORED) &&
        !kgb_decode_stream(format, state, stream, pd)) return false;
    if (!stream->decoded_valid && item->data_offset < 0) return false;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return kgb_extract(format, stream, item, NULL, pd);
    if (item->unsafe || item->blocked || !kgb_safe_name(item->name))
        return false;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", item->name)
               : xx_str_concat(base, item->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = kgb_extract(format, stream, item, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_kgb_archiver_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
