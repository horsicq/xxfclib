/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent native C implementation. Format evidence: GARbro's MIT
 * ArcFormats/NitroPlus/ArcNPK.cs, including its archive writer. Cryptography
 * and Deflate use this library's existing native codecs; no upstream C#
 * implementation or per-game key material is incorporated.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/nitroplus_npk2/xx_nitroplus_npk2.h"
#include "xxfclib/algo/aes/xx_aes.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/data/xx_data.h"

#ifdef NITROPLUS_NPK2
#define NP_FILE_TYPE XX_FILE_TYPE_NITROPLUS_NPK2
#else
#define NP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define NP_MAX_COUNT 0xFFFFFU
#define NP_MAX_INDEX (64U * 1024U * 1024U)
#define NP_MAX_SEGMENT (16U * 1024U * 1024U)
#define NP_MAX_DECODED (256U * 1024U * 1024U)

typedef struct np_segment {
    int64_t offset;
    uint32_t encrypted_size, size, unpacked_size;
} np_segment;
typedef struct np_member {
    char *name;
    np_segment *segments;
    uint32_t count, unpacked_size;
    uint64_t encrypted_size;
    uint8_t hash[32];
    bool compressed, duplicate;
} np_member;
typedef struct np_layout {
    np_member *members;
    uint32_t count;
    uint32_t encrypted_index_size;
    size_t index;
    int64_t format_size;
    uint8_t key[32], iv[16];
    size_t key_size;
} np_layout;
typedef struct np_name_key { const char *name; uint32_t index; } np_name_key;
typedef struct np_sink {
    xx_io_device device;
    xx_io_device *destination;
    xx_hash_context *hash;
    xx_pd_struct *pd;
    uint64_t written, expected;
} np_sink;

static bool np_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static const xx_var *np_option(Abstractformat *format, const xx_list_s *options, uint32_t id) {
    return xx_format_resolve_extra_parameter(format, options, id);
}
static bool np_read(xx_io_device *device, int64_t at, void *buffer,
                    size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device || at < 0 || xx_io_seek64(device, at, XX_RT_SEEK_SET)) return false;
    while (done < size) {
        size_t take = size - done;
        ssize_t got;
        if (np_stopped(pd)) return false;
        if (take > 65536U) take = 65536U;
        got = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (got <= 0 || (size_t)got > take) return false;
        done += (size_t)got;
    }
    return true;
}
static bool np_header(Abstractformat *format, uint8_t header[32], xx_pd_struct *pd) {
    int64_t total;
    uint32_t count, size;
    if (!format || !format->device || format->base_address < 0 || np_stopped(pd)) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address || total - format->base_address < 48 ||
        !np_read(format->device, format->base_address, header, 32U, pd) ||
        xx_rt_memcmp(header, "NPK2", 4U)) return false;
    count = xx_data_get_u32(header + 24U, 4, 0, false); size = xx_data_get_u32(header + 28U, 4, 0, false);
    return count && count <= NP_MAX_COUNT && size && !(size & 15U) &&
        size <= NP_MAX_INDEX && size <= (uint64_t)(total - format->base_address - 32);
}
static void np_layout_free(void *ptr) {
    np_layout *layout = (np_layout *)ptr;
    uint32_t i;
    if (!layout) return;
    if (layout->members) {
        for (i = 0U; i < layout->count; ++i) {
            if (layout->members[i].name) xx_mem_free(layout->members[i].name);
            if (layout->members[i].segments) xx_mem_free(layout->members[i].segments);
        }
        xx_mem_free(layout->members);
    }
    xx_mem_zero(layout->key, sizeof(layout->key));
    xx_mem_free(layout);
}
static int np_hex(unsigned c) {
    return c >= '0' && c <= '9' ? (int)(c - '0') :
           c >= 'a' && c <= 'f' ? (int)(c - 'a' + 10U) :
           c >= 'A' && c <= 'F' ? (int)(c - 'A' + 10U) : -1;
}
static bool np_key(xx_nitroplus_npk2 *archive, const xx_list_s *options,
                   uint8_t key[32], size_t *size) {
    const xx_var *password = xx_format_resolve_extra_parameter(
        &archive->format, options, XX_META_ID_OPT_PASSWORD);
    const uint8_t *bytes;
    size_t length = 0U, i;
    *size = 0U;
    if (!password) {
        if (!archive->key_size) return false;
        xx_mem_copy(key, archive->key, archive->key_size);
        *size = archive->key_size; return true;
    }
    if (password->type == XX_VAR_TYPE_BYTES || password->type == XX_VAR_TYPE_BYTES_VIEW) {
        bytes = (const uint8_t *)xx_var_get_bytes(password, &length);
        if (!bytes || (length != 16U && length != 24U && length != 32U)) return false;
        xx_mem_copy(key, bytes, length); *size = length; return true;
    }
    if (password->type == XX_VAR_TYPE_STRING || password->type == XX_VAR_TYPE_STRING_VIEW) {
        const char *text = xx_var_get_str(password);
        if (!text) return false;
        length = xx_str_len(text);
        if (length != 32U && length != 48U && length != 64U) return false;
        for (i = 0U; i < length / 2U; ++i) {
            int a = np_hex((uint8_t)text[i * 2U]), b = np_hex((uint8_t)text[i * 2U + 1U]);
            if (a < 0 || b < 0) { xx_mem_zero(key, 32U); return false; }
            key[i] = (uint8_t)((unsigned)a * 16U + (unsigned)b);
        }
        *size = length / 2U; return true;
    }
    return false;
}
/* Decrypt bounded CBC blocks, polling between calls and preserving the last
 * ciphertext block as the next IV. Padding is checked before it is removed. */
static bool np_decrypt(uint8_t *bytes, size_t size, const uint8_t *key,
    size_t key_size, const uint8_t iv[16], size_t *plain_size, xx_pd_struct *pd) {
    uint8_t chain[16], next[16], padding;
    size_t at = 0U, i;
    if (!size || (size & 15U)) return false;
    xx_mem_copy(chain, iv, 16U);
    while (at < size) {
        size_t take = size - at;
        if (take > 65536U) take = 65536U;
        if (np_stopped(pd)) return false;
        xx_mem_copy(next, bytes + at + take - 16U, 16U);
        if (!xx_aes_cbc_decrypt(bytes + at, take, key, key_size, chain, bytes + at)) return false;
        xx_mem_copy(chain, next, 16U); at += take;
    }
    padding = bytes[size - 1U];
    if (!padding || padding > 16U || padding > size) return false;
    for (i = 0U; i < padding; ++i) if (bytes[size - 1U - i] != padding) return false;
    *plain_size = size - padding;
    return !np_stopped(pd);
}
static bool np_utf8(const uint8_t *name, size_t size) {
    size_t at = 0U;
    while (at < size) {
        uint32_t value;
        unsigned extra, c = name[at++];
        if (!c) return false;
        if (c < 128U) continue;
        if (c >= 0xc2U && c <= 0xdfU) { extra = 1U; value = c & 31U; }
        else if (c >= 0xe0U && c <= 0xefU) { extra = 2U; value = c & 15U; }
        else if (c >= 0xf0U && c <= 0xf4U) { extra = 3U; value = c & 7U; }
        else return false;
        if (size - at < extra) return false;
        for (c = 0U; c < extra; ++c) {
            unsigned byte = name[at++];
            if ((byte & 0xc0U) != 0x80U) return false;
            value = value << 6U | (byte & 63U);
        }
        if ((extra == 2U && value < 0x800U) || (extra == 3U && value < 0x10000U) ||
            value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU)) return false;
    }
    return true;
}
static char *np_name(const uint8_t *raw, size_t size) {
    char *name = (char *)xx_mem_alloc(size * 3U + 14U);
    size_t at = 0U, i;
    if (!name) return NULL;
    for (i = 0U; i < size; ++i) {
        if (raw[i] == '%') { name[at++] = '%'; name[at++] = '2'; name[at++] = '5'; }
        else name[at++] = raw[i] == '\\' ? '/' : (char)raw[i];
    }
    name[at] = 0; return name;
}
static int np_fold_compare(const char *a, const char *b) {
    for (;;) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 'a' - 'A');
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 'a' - 'A');
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}
static int np_compare_keys(const void *a, const void *b) {
    const np_name_key *x = (const np_name_key *)a, *y = (const np_name_key *)b;
    int result = np_fold_compare(x->name, y->name);
    return result ? result : x->index < y->index ? -1 : x->index > y->index ? 1 : 0;
}
static void np_suffix(char *name, uint32_t index) {
    char suffix[14];
    size_t length = xx_str_len(name), start = 0U, dot = length, i;
    int size = xx_rt_snprintf(suffix, sizeof(suffix), "%%_%u", index);
    for (i = 0U; i < length; ++i) if (name[i] == '/') start = i + 1U;
    for (i = length; i > start + 1U; --i) if (name[i - 1U] == '.') { dot = i - 1U; break; }
    if (size <= 0 || (size_t)size >= sizeof(suffix)) return;
    xx_rt_memmove(name + dot + (size_t)size, name + dot, length - dot + 1U);
    xx_rt_memcpy(name + dot, suffix, (size_t)size);
}
static np_layout *np_parse_inner(Abstractformat *format, const xx_list_s *options,
                                 xx_pd_struct *pd) {
    uint8_t header[32], *index = NULL;
    np_layout *layout = NULL;
    np_name_key *keys = NULL;
    size_t key_size, plain_size = 0U, at = 0U;
    uint8_t key[32] = {0};
    uint32_t encrypted_size, i;
    int64_t available;
    bool ok = false;
    if (!np_header(format, header, pd) ||
        !np_key((xx_nitroplus_npk2 *)format, options, key, &key_size)) return NULL;
    encrypted_size = xx_data_get_u32(header + 28U, 4, 0, false);
    available = xx_io_total_size(format->device) - format->base_address;
    layout = (np_layout *)xx_mem_calloc(1U, sizeof(*layout));
    if (!layout) goto done;
    layout->count = xx_data_get_u32(header + 24U, 4, 0, false);
    layout->encrypted_index_size = encrypted_size;
    layout->format_size = 32 + (int64_t)encrypted_size;
    layout->key_size = key_size;
    xx_mem_copy(layout->key, key, key_size); xx_mem_copy(layout->iv, header + 8U, 16U);
    if ((uint64_t)layout->count * sizeof(*layout->members) > SIZE_MAX ||
        (uint64_t)layout->count * sizeof(*keys) > SIZE_MAX ||
        (uint64_t)layout->count * 43U >= encrypted_size) goto done;
    index = (uint8_t *)xx_mem_alloc(encrypted_size);
    layout->members = (np_member *)xx_mem_calloc(layout->count, sizeof(*layout->members));
    keys = (np_name_key *)xx_mem_alloc((size_t)layout->count * sizeof(*keys));
    if (!index || !layout->members || !keys ||
        !np_read(format->device, format->base_address + 32, index, encrypted_size, pd) ||
        !np_decrypt(index, encrypted_size, key, key_size, layout->iv, &plain_size, pd)) goto done;
    for (i = 0U; i < layout->count; ++i) {
        np_member *member = &layout->members[i];
        size_t name_length;
        uint32_t j;
        uint64_t sum = 0U;
        if (np_stopped(pd) || plain_size - at < 3U || index[at] > 1U) goto done;
        name_length = (size_t)index[at + 1U] | (size_t)index[at + 2U] << 8U;
        at += 3U;
        if (!name_length || name_length > 260U || name_length > plain_size - at ||
            !np_utf8(index + at, name_length)) goto done;
        member->name = np_name(index + at, name_length);
        if (!member->name) goto done;
        at += name_length;
        if (plain_size - at < 40U) goto done;
        member->unpacked_size = xx_data_get_u32(index + at, 4, 0, false);
        xx_mem_copy(member->hash, index + at + 4U, 32U);
        member->count = xx_data_get_u32(index + at + 36U, 4, 0, false);
        at += 40U;
        if ((uint64_t)member->count * 20U > plain_size - at ||
            (uint64_t)member->count * sizeof(*member->segments) > SIZE_MAX ||
            (!member->count && member->unpacked_size)) goto done;
        if (member->count) {
            member->segments = (np_segment *)xx_mem_calloc(member->count, sizeof(*member->segments));
            if (!member->segments) goto done;
        }
        for (j = 0U; j < member->count; ++j) {
            np_segment *segment = &member->segments[j];
            uint64_t offset = xx_data_get_u64(index + at, 8, 0, false);
            if (np_stopped(pd)) goto done;
            segment->encrypted_size = xx_data_get_u32(index + at + 8U, 4, 0, false);
            segment->size = xx_data_get_u32(index + at + 12U, 4, 0, false);
            segment->unpacked_size = xx_data_get_u32(index + at + 16U, 4, 0, false);
            at += 20U;
            if (offset < 32U + encrypted_size || offset > (uint64_t)available ||
                !segment->encrypted_size || (segment->encrypted_size & 15U) ||
                segment->encrypted_size > NP_MAX_SEGMENT ||
                segment->encrypted_size > (uint64_t)available - offset ||
                segment->size >= segment->encrypted_size ||
                segment->encrypted_size - segment->size > 16U ||
                segment->size > segment->unpacked_size ||
                segment->unpacked_size > NP_MAX_DECODED) goto done;
            segment->offset = format->base_address + (int64_t)offset;
            sum += segment->unpacked_size;
            if (sum > member->unpacked_size) goto done;
            member->encrypted_size += segment->encrypted_size;
            member->compressed |= segment->size < segment->unpacked_size;
            if ((int64_t)offset + segment->encrypted_size > layout->format_size)
                layout->format_size = (int64_t)offset + segment->encrypted_size;
        }
        if (sum != member->unpacked_size) goto done;
        keys[i].name = member->name; keys[i].index = i;
    }
    if (at != plain_size) goto done;
    xx_rt_qsort(keys, layout->count, sizeof(*keys), np_compare_keys);
    if (np_stopped(pd)) goto done;
    for (i = 1U; i < layout->count; ++i)
        if (!np_fold_compare(keys[i - 1U].name, keys[i].name)) layout->members[keys[i].index].duplicate = true;
    for (i = 0U; i < layout->count; ++i) {
        if (np_stopped(pd)) goto done;
        if (layout->members[i].duplicate) np_suffix(layout->members[i].name, i);
    }
    ok = true;
done:
    xx_mem_zero(key, sizeof(key));
    if (index) { xx_mem_zero(index, encrypted_size); xx_mem_free(index); }
    if (keys) xx_mem_free(keys);
    if (!ok) { np_layout_free(layout); layout = NULL; }
    return layout;
}
static np_layout *np_parse(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    int64_t cursor = format && format->device ? xx_io_tell(format->device) : -1;
    np_layout *layout = np_parse_inner(format, options, pd);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) { np_layout_free(layout); layout = NULL; }
    return layout;
}
static void np_destroy_format(Abstractformat *format) { xx_nitroplus_npk2_destroy((xx_nitroplus_npk2 *)format); }
void xx_nitroplus_npk2_init(xx_nitroplus_npk2 *archive, xx_io_device *device, int64_t base) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive)); xx_format_init(&archive->format, device, base);
    archive->format.endian = XX_ENDIAN_LITTLE; archive->format.file_type = NP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE; archive->format.is_archive = true;
    xx_format_set_extension(&archive->format, "npk"); xx_format_set_mime_type(&archive->format, "application/x-npk2");
    archive->format.destroy = np_destroy_format;
    archive->format.check_is_valid = xx_nitroplus_npk2_check_is_valid;
    archive->format.handle_base_info = xx_nitroplus_npk2_handle_base_info;
    archive->format.get_format_size = xx_nitroplus_npk2_get_format_size;
    archive->format.get_number_of_archive_records = xx_nitroplus_npk2_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_nitroplus_npk2_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_nitroplus_npk2_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_nitroplus_npk2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_nitroplus_npk2_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_nitroplus_npk2_free_archive_records_reading;
}
xx_nitroplus_npk2 *xx_nitroplus_npk2_create(xx_io_device *device, int64_t base) {
    xx_nitroplus_npk2 *archive = (xx_nitroplus_npk2 *)xx_mem_alloc(sizeof(*archive));
    if (archive) { xx_nitroplus_npk2_init(archive, device, base); } return archive;
}
bool xx_nitroplus_npk2_set_key(xx_nitroplus_npk2 *archive, const uint8_t *key, size_t size) {
    uint8_t copy[32] = {0};
    if (!archive || (!key && size) || (size && size != 16U && size != 24U && size != 32U)) return false;
    if (size) xx_mem_copy(copy, key, size);
    xx_mem_zero(archive->key, sizeof(archive->key));
    if (size) xx_mem_copy(archive->key, copy, size);
    xx_mem_zero(copy, sizeof(copy));
    archive->key_size = size; archive->format.base_info_handled = false; archive->format.is_valid = false;
    return true;
}
void xx_nitroplus_npk2_destroy(xx_nitroplus_npk2 *archive) {
    if (!archive) return;
    xx_mem_zero(archive->key, sizeof(archive->key)); archive->key_size = 0U;
    xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_nitroplus_npk2_free(xx_nitroplus_npk2 *archive) {
    if (!archive) { return; } xx_nitroplus_npk2_destroy(archive); xx_mem_free(archive);
}
bool xx_nitroplus_npk2_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    uint8_t header[32];
    int64_t cursor = format && format->device ? xx_io_tell(format->device) : -1;
    bool result = np_header(format, header, pd);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) result = false;
    if (result && (((xx_nitroplus_npk2 *)format)->key_size ||
        xx_format_find_extra_parameter(format, XX_META_ID_OPT_PASSWORD))) {
        np_layout *layout = np_parse(format, NULL, pd); result = layout != NULL; np_layout_free(layout);
    }
    return result;
}
bool xx_nitroplus_npk2_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    np_layout *layout;
    xx_nitroplus_npk2 *archive;
    uint8_t header[32];
    int64_t cursor;
    bool header_ok;
    if (!format || np_stopped(pd)) return false;
    if (format->base_info_handled) return format->is_valid;
    layout = np_parse(format, NULL, pd);
    if (!layout) return false;
    cursor = xx_io_tell(format->device); header_ok = np_header(format, header, pd);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) header_ok = false;
    if (!header_ok) { np_layout_free(layout); return false; }
    archive = (xx_nitroplus_npk2 *)format;
    archive->number_of_records = layout->count; archive->encrypted_index_size = xx_data_get_u32(header + 28U, 4, 0, false);
    xx_mem_copy(archive->iv, header + 8U, 16U);
    format->number_of_archive_records = layout->count; format->format_size = layout->format_size;
    format->is_valid = true; format->base_info_handled = true;
    np_layout_free(layout); return true;
}
int64_t xx_nitroplus_npk2_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return xx_nitroplus_npk2_handle_base_info(format, pd) ? format->format_size : -1;
}
uint64_t xx_nitroplus_npk2_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd) {
    return xx_nitroplus_npk2_handle_base_info(format, pd) ? format->number_of_archive_records : 0U;
}
static bool np_set_record(Abstractformat *format, xx_archive_record_state *state) {
    np_layout *layout = (np_layout *)state->internal_state;
    const np_member *member = &layout->members[layout->index];
    xx_archive_record *record = &state->current_record;
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset = format->base_address + 32;
    record->header_size = layout->encrypted_index_size;
    record->data_offset = member->count ? member->segments[0].offset : -1;
    record->compressed_size = (int64_t)member->encrypted_size;
    return xx_archive_record_set_original_name(record, member->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->unpacked_size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->encrypted_size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->compressed ? 8U : 0U) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, true);
}
xx_archive_record_state *xx_nitroplus_npk2_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    np_layout *layout = np_parse(format, options, pd);
    xx_archive_record_state *state;
    size_t i;
    if (!layout) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { np_layout_free(layout); return NULL; }
    xx_archive_record_state_init(state, format);
    state->internal_state = layout; state->free_internal = np_layout_free;
    state->total_records = layout->count; state->current_index = 0;
    if (options) for (i = 0U; i < options->count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        xx_meta copy;
        if (!meta || np_stopped(pd)) goto fail;
        xx_meta_init(&copy, meta->meta_id);
        if (!xx_var_copy(&copy.var, &meta->var) || !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy); goto fail;
        }
    }
    state->has_record = np_set_record(format, state);
    if (state->has_record) return state;
fail:
    xx_archive_record_state_free(state); return NULL;
}
const xx_archive_record *xx_nitroplus_npk2_get_current_archive_record(Abstractformat *format,
    xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}
bool xx_nitroplus_npk2_archive_record_move_to_next(Abstractformat *format,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    np_layout *layout;
    if (!format || !state || state->format != format || !state->has_record ||
        !(layout = (np_layout *)state->internal_state)) return false;
    if (np_stopped(pd) || layout->index + 1U >= layout->count) { state->has_record = false; return false; }
    ++layout->index; state->current_index = (int64_t)layout->index;
    state->has_record = np_set_record(format, state); return state->has_record;
}
static ssize_t np_sink_write(xx_io_device *device, const void *bytes, size_t size) {
    np_sink *sink = (np_sink *)device->priv;
    size_t done = 0U;
    if (np_stopped(sink->pd) || size > sink->expected - sink->written) return -1;
    while (sink->destination && done < size) {
        ssize_t amount;
        if (np_stopped(sink->pd)) return -1;
        amount = xx_io_write(sink->destination, (const uint8_t *)bytes + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return -1;
        done += (size_t)amount;
    }
    xx_hash_update(sink->hash, bytes, size); sink->written += size;
    return (ssize_t)size;
}
static bool np_decode(Abstractformat *format, xx_archive_record_state *state,
                       xx_io_device *destination, xx_pd_struct *pd) {
    np_layout *layout = (np_layout *)state->internal_state;
    const np_member *member = &layout->members[layout->index];
    const xx_var *limit;
    xx_hash_context hash;
    uint8_t digest[32];
    uint32_t i;
    if (!xx_hash_init(&hash, XX_HASH_SHA256)) return false;
    limit = np_option(format, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit && member->unpacked_size > xx_var_get_u64(limit)) return false;
    for (i = 0U; i < member->count; ++i) {
        const np_segment *segment = &member->segments[i];
        uint8_t *buffer;
        size_t plain = 0U;
        np_sink sink;
        bool ok;
        int64_t total = xx_io_total_size(format->device);
        if (np_stopped(pd) || segment->offset > total ||
            segment->encrypted_size > (uint64_t)(total - segment->offset)) return false;
        limit = np_option(format, &state->options, XX_META_ID_OPT_MEMORY_LIMIT);
        if (limit && segment->encrypted_size > xx_var_get_u64(limit)) return false;
        buffer = (uint8_t *)xx_mem_alloc(segment->encrypted_size);
        if (!buffer) return false;
        ok = np_read(format->device, segment->offset, buffer, segment->encrypted_size, pd) &&
            np_decrypt(buffer, segment->encrypted_size, layout->key, layout->key_size, layout->iv, &plain, pd) &&
            plain == segment->size;
        xx_mem_zero(&sink, sizeof(sink)); sink.device.priv = &sink; sink.device.write = np_sink_write;
        sink.destination = destination; sink.hash = &hash; sink.expected = segment->unpacked_size; sink.pd = pd;
        if (ok && segment->size < segment->unpacked_size) {
            size_t consumed = 0U;
            ok = xx_deflate_unpack_memory_to_device_ex(buffer, plain, &sink.device, &consumed, false, pd) && consumed == plain;
        } else if (ok) ok = np_sink_write(&sink.device, buffer, plain) == (ssize_t)plain;
        ok = ok && sink.written == segment->unpacked_size;
        xx_mem_zero(buffer, segment->encrypted_size); xx_mem_free(buffer);
        if (!ok) return false;
    }
    return !np_stopped(pd) && xx_hash_final(&hash, digest, sizeof(digest)) && xx_hash_equal(digest, member->hash, 32U);
}
bool xx_nitroplus_npk2_unpack_current_archive_record_to_device(Abstractformat *format,
    xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    np_layout *layout;
    int64_t cursor;
    bool ok;
    if (!format || !format->device || !state || state->format != format || !state->has_record ||
        !(layout = (np_layout *)state->internal_state) || layout->index >= layout->count ||
        destination == format->device || np_stopped(pd)) return false;
    cursor = xx_io_tell(format->device);
    ok = np_decode(format, state, NULL, pd);
    if (ok && destination) ok = np_decode(format, state, destination, pd);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) ok = false;
    return ok;
}
void xx_nitroplus_npk2_free_archive_records_reading(Abstractformat *format,
    xx_archive_record_state *state) { (void)format; xx_archive_record_state_free(state); }
static bool np_reserved(const char *component, size_t length) {
    static const char *const names[] = {"CON", "PRN", "AUX", "NUL", "CLOCK$", "CONIN$", "CONOUT$"};
    char stem[9];
    size_t n = 0U, i;
    while (n < length && component[n] != '.') ++n;
    while (n && component[n - 1U] == ' ') --n;
    if (n >= sizeof(stem)) return false;
    for (i = 0U; i < n; ++i) {
        char c = component[i]; stem[i] = c >= 'a' && c <= 'z' ? (char)(c + 'A' - 'a') : c;
    }
    stem[n] = 0;
    if (n == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        (!xx_rt_memcmp(stem, "COM", 3U) || !xx_rt_memcmp(stem, "LPT", 3U))) return true;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!xx_str_cmp(stem, names[i])) return true;
    return false;
}
static bool np_safe_name(const char *name) {
    const char *component = name, *at;
    if (!name || !*name || *name == '/') return false;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || c == 127U || (c && c < 32U)) return false;
        if (c == '/' || !c) {
            size_t n = (size_t)(at - component);
            if (!n || component[0] == ' ' || component[n - 1U] == '.' ||
                component[n - 1U] == ' ' || np_reserved(component, n)) return false;
            if (!c) return true;
            component = at + 1;
        }
    }
}
bool xx_nitroplus_npk2_unpack_current_archive_record(Abstractformat *format,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    np_layout *layout;
    const xx_var *path_option, *overwrite_option;
    const char *base = NULL;
    char *owned_base = NULL, *path = NULL, *stage_path = NULL;
    xx_io_device *stage = NULL;
    bool ok = false, overwrite = false;
    unsigned attempt;
    size_t prefix = 0U, n;
    if (!format || !state || state->format != format || !state->has_record ||
        !(layout = (np_layout *)state->internal_state) ||
        layout->index >= layout->count || np_stopped(pd)) return false;
    path_option = np_option(format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return xx_nitroplus_npk2_unpack_current_archive_record_to_device(format, state, NULL, pd);
    if (!np_safe_name(layout->members[layout->index].name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option)); base = owned_base;
    }
    if (!base) goto done;
    overwrite_option = np_option(format, &state->options, XX_META_ID_OPT_OVERWRITE);
    if (overwrite_option) overwrite = xx_var_get_bool(overwrite_option);
    path = !*base || base[xx_str_len(base) - 1U] == '/' || base[xx_str_len(base) - 1U] == '\\'
        ? xx_str_concat(base, layout->members[layout->index].name)
        : xx_str_concat3(base, "/", layout->members[layout->index].name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path, false)) goto done;
    for (n = 0U; path[n]; ++n) if (path[n] == '/' || path[n] == '\\') prefix = n + 1U;
    stage_path = (char *)xx_mem_alloc(prefix + 50U);
    if (!stage_path) goto done;
    xx_rt_memcpy(stage_path, path, prefix);
    for (attempt = 0U; attempt < 128U && !np_stopped(pd); ++attempt) {
        int wrote = xx_rt_snprintf(stage_path + prefix, 50U,
            ".xxfc-npk2-%u-%u.tmp", (unsigned)layout->index, attempt);
        if (wrote <= 0) goto done;
        /* An archive member may legally use the staging component itself. */
        if (!np_fold_compare(stage_path, path)) continue;
        stage = xx_io_file_open(stage_path, "wbx");
        if (stage) break;
    }
    if (!stage) goto done;
    ok = xx_nitroplus_npk2_unpack_current_archive_record_to_device(format, state, stage, pd);
    if (xx_io_close(stage)) ok = false;
    stage = NULL;
    if (ok && !np_stopped(pd)) ok = xx_io_file_replace_a(stage_path, path, overwrite);
    else ok = false;
    if (!ok) (void)xx_io_file_remove_a(stage_path);
done:
    if (stage) { (void)xx_io_close(stage); (void)xx_io_file_remove_a(stage_path); }
    if (stage_path) xx_mem_free(stage_path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return ok;
}
