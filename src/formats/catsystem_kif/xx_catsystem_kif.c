/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent native C implementation of CatSystem KIF/INT.
 * Format evidence: GARbro ArcFormats/CatSystem/ArcINT.cs (MIT, morkt),
 * https://github.com/morkt/GARbro/blob/master/ArcFormats/CatSystem/ArcINT.cs.
 * No upstream code is incorporated. Private MT19937/Blowfish helpers follow
 * the original algorithms; the supplied main key deciphers filenames.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/catsystem_kif/xx_catsystem_kif.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xx_kif_crypto_private.h"

#ifdef CATSYSTEM_KIF
#define KI_FILE_TYPE XX_FILE_TYPE_CATSYSTEM_KIF
#else
#define KI_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define KI_HEADER_SIZE 8U
#define KI_MAX_COUNT 0xFFFFFU
#define KI_MAX_EXPANDED (64U * 1024U * 1024U)

typedef struct ki_member {
    int64_t offset;
    uint32_t size;
    char *name;
    bool duplicate;
} ki_member;
typedef struct ki_layout {
    ki_member *members;
    uint32_t count;
    uint32_t name_size;
    uint32_t record_size;
    uint64_t index_end;
    int64_t format_size;
    size_t index;
    bool encrypted;
    ki_blowfish cipher;
} ki_layout;
typedef struct ki_name_key {
    const char *name;
    uint32_t index;
} ki_name_key;

static bool ki_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}
static uint32_t ki_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8U |
           (uint32_t)p[2] << 16U | (uint32_t)p[3] << 24U;
}
static void ki_put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U); p[3] = (uint8_t)(value >> 24U);
}
static bool ki_read(xx_io_device *device, int64_t at, void *buffer,
                    size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device || at < 0 || xx_io_seek64(device, at, XX_RT_SEEK_SET))
        return false;
    while (done < size) {
        ssize_t got;
        size_t take = size - done;
        if (ki_stopped(pd)) return false;
        if (take > 65536U) take = 65536U;
        got = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (got <= 0 || (size_t)got > take) return false;
        done += (size_t)got;
    }
    return true;
}
static void ki_layout_free(void *ptr) {
    ki_layout *layout = (ki_layout *)ptr;
    uint32_t i;
    if (!layout) return;
    if (layout->members) {
        for (i = 0U; i < layout->count; ++i)
            if (layout->members[i].name) xx_mem_free(layout->members[i].name);
        xx_mem_free(layout->members);
    }
    xx_mem_zero(&layout->cipher, sizeof(layout->cipher));
    xx_mem_free(layout);
}
static size_t ki_escape(char *out, uint8_t c) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = '%'; out[1] = hex[c >> 4U]; out[2] = hex[c & 15U];
    return 3U;
}
static bool ki_lead(uint8_t c) {
    return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU);
}
static bool ki_trail(uint8_t c) {
    return (c >= 0x40U && c <= 0x7eU) || (c >= 0x80U && c <= 0xfcU);
}
static char *ki_name(const uint8_t *raw, size_t size) {
    char *result = (char *)xx_mem_alloc(size * 3U + 14U);
    size_t i = 0U, at = 0U;
    if (!result) return NULL;
    while (i < size) {
        uint8_t c = raw[i++];
        if (ki_lead(c) && i < size && ki_trail(raw[i])) {
            at += ki_escape(result + at, c);
            at += ki_escape(result + at, raw[i++]);
        } else if (c >= 0x80U || c < 0x20U || c == 0x7fU || c == '%') {
            at += ki_escape(result + at, c);
        } else {
            result[at++] = c == '\\' ? '/' : (char)c;
        }
    }
    result[at] = 0;
    return result;
}
static int ki_fold_compare(const char *a, const char *b) {
    for (;;) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 'a' - 'A');
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 'a' - 'A');
        if (x != y) return x < y ? -1 : 1;
        if (x == 0U) return 0;
    }
}
static int ki_compare_keys(const void *a, const void *b) {
    const ki_name_key *x = (const ki_name_key *)a;
    const ki_name_key *y = (const ki_name_key *)b;
    int order = ki_fold_compare(x->name, y->name);
    if (order) return order;
    return x->index < y->index ? -1 : x->index > y->index ? 1 : 0;
}
static void ki_suffix(char *name, uint32_t index) {
    char suffix[14];
    size_t length = xx_str_len(name), component = 0U, dot = length, i;
    int amount = xx_rt_snprintf(suffix, sizeof(suffix), "%%_%u", index);
    for (i = 0U; i < length; ++i) if (name[i] == '/') component = i + 1U;
    for (i = length; i > component + 1U; --i)
        if (name[i - 1U] == '.') { dot = i - 1U; break; }
    if (amount <= 0 || (size_t)amount >= sizeof(suffix)) return;
    xx_rt_memmove(name + dot + (size_t)amount, name + dot, length - dot + 1U);
    xx_rt_memcpy(name + dot, suffix, (size_t)amount);
}

/* The format does not encode its name width. Try the 32-byte layout first,
 * then the 64-byte layout, validating the entire directory and every range.
 */
static bool ki_header(Abstractformat *format, uint32_t *count, bool *encrypted,
                       int64_t *available, xx_pd_struct *pd) {
    uint8_t header[19];
    int64_t total;
    if (!format || !format->device || format->base_address < 0 || ki_stopped(pd))
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    *available = total - format->base_address;
    if (*available < (int64_t)sizeof(header) ||
        !ki_read(format->device, format->base_address, header, sizeof(header), pd) ||
        xx_rt_memcmp(header, "KIF\0", 4U)) return false;
    *count = ki_le32(header + 4U);
    *encrypted = !xx_rt_memcmp(header + 8U, "__key__.dat\0", 11U);
    return *count > 0U && *count <= KI_MAX_COUNT;
}
static ki_layout *ki_parse_width(Abstractformat *format, uint32_t count,
                                  uint32_t width, int64_t available,
                                  xx_pd_struct *pd) {
    ki_layout *layout = NULL;
    ki_name_key *keys = NULL;
    uint8_t entry[72];
    uint64_t index_end = KI_HEADER_SIZE + (uint64_t)count * (width + 8U);
    size_t expanded = 0U;
    uint32_t i;
    bool ok = false;
    if (index_end > (uint64_t)available ||
        (uint64_t)count * sizeof(ki_member) > SIZE_MAX ||
        (uint64_t)count * sizeof(*keys) > SIZE_MAX) return NULL;
    layout = (ki_layout *)xx_mem_calloc(1U, sizeof(*layout));
    if (!layout) return NULL;
    layout->count = count; layout->name_size = width;
    layout->record_size = width + 8U; layout->index_end = index_end;
    layout->format_size = (int64_t)index_end;
    layout->members = (ki_member *)xx_mem_calloc(count, sizeof(*layout->members));
    keys = (ki_name_key *)xx_mem_alloc((size_t)count * sizeof(*keys));
    if (!layout->members || !keys) goto done;
    for (i = 0U; i < count; ++i) {
        size_t length = 0U;
        uint32_t offset, size;
        ki_member *member = &layout->members[i];
        if (ki_stopped(pd) ||
            !ki_read(format->device, format->base_address + KI_HEADER_SIZE +
                      (int64_t)i * layout->record_size, entry, layout->record_size, pd))
            goto done;
        while (length < width && entry[length]) ++length;
        if (!length || expanded > KI_MAX_EXPANDED - (length * 3U + 14U)) goto done;
        expanded += length * 3U + 14U;
        member->name = ki_name(entry, length);
        if (!member->name) goto done;
        offset = ki_le32(entry + width); size = ki_le32(entry + width + 4U);
        if ((uint64_t)offset < index_end || (uint64_t)offset > (uint64_t)available ||
            (uint64_t)size > (uint64_t)available - offset) goto done;
        member->offset = format->base_address + offset; member->size = size;
        if ((int64_t)offset + size > layout->format_size)
            layout->format_size = (int64_t)offset + size;
        keys[i].name = member->name; keys[i].index = i;
    }
    xx_rt_qsort(keys, count, sizeof(*keys), ki_compare_keys);
    if (ki_stopped(pd)) goto done;
    for (i = 1U; i < count; ++i)
        if (!ki_fold_compare(keys[i - 1U].name, keys[i].name))
            layout->members[keys[i].index].duplicate = true;
    for (i = 0U; i < count; ++i) {
        if (ki_stopped(pd)) goto done;
        if (layout->members[i].duplicate) ki_suffix(layout->members[i].name, i);
    }
    ok = true;
done:
    if (keys) xx_mem_free(keys);
    if (!ok) { ki_layout_free(layout); layout = NULL; }
    return layout;
}
static int ki_hex(unsigned c) {
    return c >= '0' && c <= '9' ? (int)(c - '0') :
           c >= 'a' && c <= 'f' ? (int)(c - 'a' + 10U) :
           c >= 'A' && c <= 'F' ? (int)(c - 'A' + 10U) : -1;
}
static bool ki_key(xx_catsystem_kif *archive, const xx_list_s *options, uint32_t *key) {
    const xx_var *password = xx_format_resolve_extra_parameter(
        &archive->format, options, XX_META_ID_OPT_PASSWORD);
    const char *text;
    unsigned i;
    uint32_t value = 0U;
    if (!password) {
        if (!archive->has_main_key) return false;
        *key = archive->main_key; return true;
    }
    if (password->type != XX_VAR_TYPE_STRING && password->type != XX_VAR_TYPE_STRING_VIEW)
        return false;
    text = xx_var_get_str(password);
    if (!text || xx_str_len(text) != 8U) return false;
    for (i = 0U; i < 8U; ++i) {
        int digit = ki_hex((uint8_t)text[i]);
        if (digit < 0) return false;
        value = (value << 4U) | (unsigned)digit;
    }
    *key = value; return true;
}
static void ki_decipher_name(uint8_t name[64], uint32_t key) {
    static const char alphabet[] = "zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA";
    unsigned step = ((key >> 24U) + (key >> 16U) + (key >> 8U) + key) & 255U;
    unsigned i, j;
    for (i = 0U; i < 64U && name[i]; ++i, ++step) {
        for (j = 0U; j < 52U; ++j) if ((uint8_t)alphabet[j] == name[i]) {
            unsigned shifted = (j + 52U - step % 52U) % 52U;
            name[i] = (uint8_t)alphabet[51U - shifted]; break;
        }
    }
}
static ki_layout *ki_parse_encrypted(Abstractformat *format, uint32_t count,
    int64_t available, const xx_list_s *options, xx_pd_struct *pd) {
    ki_layout *layout = NULL;
    ki_name_key *keys = NULL;
    uint8_t entry[72], cipher_key[4];
    uint32_t main_key, i;
    uint64_t index_end = KI_HEADER_SIZE + (uint64_t)count * 72U;
    size_t expanded = 0U;
    bool ok = false;
    if (count <= 1U || index_end > (uint64_t)available ||
        !ki_key((xx_catsystem_kif *)format, options, &main_key) ||
        (uint64_t)(count - 1U) * sizeof(ki_member) > SIZE_MAX ||
        (uint64_t)(count - 1U) * sizeof(*keys) > SIZE_MAX) return NULL;
    layout = (ki_layout *)xx_mem_calloc(1U, sizeof(*layout));
    if (!layout) return NULL;
    layout->count = count - 1U; layout->name_size = 64U;
    layout->record_size = 72U; layout->index_end = index_end;
    layout->format_size = (int64_t)index_end; layout->encrypted = true;
    layout->members = (ki_member *)xx_mem_calloc(layout->count, sizeof(*layout->members));
    keys = (ki_name_key *)xx_mem_alloc((size_t)layout->count * sizeof(*keys));
    if (!layout->members || !keys ||
        !ki_read(format->device, format->base_address + KI_HEADER_SIZE + 68U, entry, 4U, pd)) goto done;
    ki_put32(cipher_key, ki_mt_first(ki_le32(entry)));
    if (!ki_bf_init(&layout->cipher, cipher_key, sizeof(cipher_key))) goto done;
    for (i = 0U; i < layout->count; ++i) {
        uint32_t ordinal = i + 1U, offset, size;
        size_t length = 0U;
        ki_member *member = &layout->members[i];
        if (ki_stopped(pd) ||
            !ki_read(format->device, format->base_address + KI_HEADER_SIZE +
                (int64_t)ordinal * 72U, entry, sizeof(entry), pd)) goto done;
        ki_decipher_name(entry, ki_mt_first(main_key + ordinal));
        while (length < 64U && entry[length]) ++length;
        if (!length || expanded > KI_MAX_EXPANDED - (length * 3U + 14U)) goto done;
        expanded += length * 3U + 14U;
        member->name = ki_name(entry, length);
        if (!member->name) goto done;
        offset = ki_le32(entry + 64U) + ordinal; size = ki_le32(entry + 68U);
        ki_bf_decrypt(&layout->cipher, &offset, &size);
        if ((uint64_t)offset < index_end || (uint64_t)offset > (uint64_t)available ||
            (uint64_t)size > (uint64_t)available - offset) goto done;
        member->offset = format->base_address + offset; member->size = size;
        if ((int64_t)offset + size > layout->format_size)
            layout->format_size = (int64_t)offset + size;
        keys[i].name = member->name; keys[i].index = i;
    }
    xx_rt_qsort(keys, layout->count, sizeof(*keys), ki_compare_keys);
    if (ki_stopped(pd)) goto done;
    for (i = 1U; i < layout->count; ++i)
        if (!ki_fold_compare(keys[i - 1U].name, keys[i].name))
            layout->members[keys[i].index].duplicate = true;
    for (i = 0U; i < layout->count; ++i) {
        if (ki_stopped(pd)) goto done;
        if (layout->members[i].duplicate) ki_suffix(layout->members[i].name, i);
    }
    ok = true;
done:
    xx_mem_zero(cipher_key, sizeof(cipher_key)); main_key = 0U;
    if (keys) xx_mem_free(keys);
    if (!ok) { ki_layout_free(layout); layout = NULL; }
    return layout;
}
static ki_layout *ki_parse_inner(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    uint32_t count;
    bool encrypted;
    int64_t available;
    ki_layout *layout;
    if (!ki_header(format, &count, &encrypted, &available, pd)) return NULL;
    if (encrypted) return ki_parse_encrypted(format, count, available, options, pd);
    layout = ki_parse_width(format, count, 32U, available, pd);
    if (!layout && !ki_stopped(pd)) layout = ki_parse_width(format, count, 64U, available, pd);
    return layout;
}
static ki_layout *ki_parse(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    int64_t cursor = format && format->device ? xx_io_tell(format->device) : -1;
    ki_layout *layout = ki_parse_inner(format, options, pd);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) {
        ki_layout_free(layout); layout = NULL;
    }
    return layout;
}
static void ki_destroy_format(Abstractformat *format) {
    xx_catsystem_kif_destroy((xx_catsystem_kif *)format);
}
void xx_catsystem_kif_init(xx_catsystem_kif *archive, xx_io_device *device, int64_t base) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = KI_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_extension(&archive->format, "int");
    xx_format_set_mime_type(&archive->format, "application/x-catsystem-kif-archive");
    archive->format.destroy = ki_destroy_format;
    archive->format.check_is_valid = xx_catsystem_kif_check_is_valid;
    archive->format.handle_base_info = xx_catsystem_kif_handle_base_info;
    archive->format.get_format_size = xx_catsystem_kif_get_format_size;
    archive->format.get_number_of_archive_records = xx_catsystem_kif_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_catsystem_kif_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_catsystem_kif_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_catsystem_kif_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_catsystem_kif_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_catsystem_kif_free_archive_records_reading;
}
xx_catsystem_kif *xx_catsystem_kif_create(xx_io_device *device, int64_t base) {
    xx_catsystem_kif *archive = (xx_catsystem_kif *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_catsystem_kif_init(archive, device, base);
    return archive;
}
void xx_catsystem_kif_destroy(xx_catsystem_kif *archive) {
    if (archive) {
        xx_catsystem_kif_clear_key(archive);
        xx_format_cleanup_extra_parameters(&archive->format);
    }
}
bool xx_catsystem_kif_set_key(xx_catsystem_kif *archive, uint32_t key) {
    if (!archive) return false;
    archive->main_key = key; archive->has_main_key = true;
    archive->format.base_info_handled = false; archive->format.is_valid = false;
    return true;
}
void xx_catsystem_kif_clear_key(xx_catsystem_kif *archive) {
    if (!archive) return;
    archive->main_key = 0U; archive->has_main_key = false;
    archive->format.base_info_handled = false; archive->format.is_valid = false;
}
void xx_catsystem_kif_free(xx_catsystem_kif *archive) {
    if (!archive) return;
    xx_catsystem_kif_destroy(archive); xx_mem_free(archive);
}
bool xx_catsystem_kif_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    uint32_t count;
    bool encrypted, valid = false;
    int64_t available, cursor = format && format->device ? xx_io_tell(format->device) : -1;
    if (ki_header(format, &count, &encrypted, &available, pd)) {
        if (encrypted && !((xx_catsystem_kif *)format)->has_main_key &&
            !xx_format_find_extra_parameter(format, XX_META_ID_OPT_PASSWORD))
            valid = KI_HEADER_SIZE + (uint64_t)count * 72U <= (uint64_t)available;
        else {
            ki_layout *layout = ki_parse_inner(format, NULL, pd);
            valid = layout != NULL;
            ki_layout_free(layout);
        }
    }
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) valid = false;
    return valid;
}
bool xx_catsystem_kif_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    ki_layout *layout;
    xx_catsystem_kif *archive;
    if (!format || ki_stopped(pd)) return false;
    if (format->base_info_handled) return format->is_valid;
    layout = ki_parse(format, NULL, pd);
    if (!layout) return false;
    archive = (xx_catsystem_kif *)format;
    archive->number_of_records = layout->count;
    archive->name_size = layout->name_size;
    archive->index_size = layout->index_end - KI_HEADER_SIZE;
    archive->is_encrypted = layout->encrypted;
    format->number_of_archive_records = layout->count;
    format->format_size = layout->format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    ki_layout_free(layout);
    return true;
}
int64_t xx_catsystem_kif_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return xx_catsystem_kif_handle_base_info(format, pd) ? format->format_size : -1;
}
uint64_t xx_catsystem_kif_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd) {
    return xx_catsystem_kif_handle_base_info(format, pd) ? format->number_of_archive_records : 0U;
}
static bool ki_set_record(Abstractformat *format, xx_archive_record_state *state) {
    ki_layout *layout = (ki_layout *)state->internal_state;
    const ki_member *member = &layout->members[layout->index];
    xx_archive_record *record = &state->current_record;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + KI_HEADER_SIZE +
                            ((int64_t)layout->index + (layout->encrypted ? 1 : 0)) * layout->record_size;
    record->header_size = layout->record_size;
    record->data_offset = member->offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, layout->encrypted);
}
xx_archive_record_state *xx_catsystem_kif_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ki_layout *layout = ki_parse(format, options, pd);
    xx_archive_record_state *state;
    size_t i;
    if (!layout) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { ki_layout_free(layout); return NULL; }
    xx_archive_record_state_init(state, format);
    state->internal_state = layout;
    state->free_internal = ki_layout_free;
    state->total_records = layout->count;
    state->current_index = 0;
    if (options) for (i = 0U; i < options->count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        xx_meta copy;
        if (ki_stopped(pd) || !meta) goto fail;
        xx_meta_init(&copy, meta->meta_id);
        if (!xx_var_copy(&copy.var, &meta->var) || !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy); goto fail;
        }
    }
    state->has_record = ki_set_record(format, state);
    if (state->has_record) return state;
fail:
    xx_archive_record_state_free(state);
    return NULL;
}
const xx_archive_record *xx_catsystem_kif_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
        ? &state->current_record : NULL;
}
bool xx_catsystem_kif_archive_record_move_to_next(Abstractformat *format,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    ki_layout *layout;
    if (!format || !state || state->format != format || !state->has_record ||
        !(layout = (ki_layout *)state->internal_state)) return false;
    if (ki_stopped(pd) || layout->index + 1U >= layout->count) {
        state->has_record = false; return false;
    }
    ++layout->index;
    state->current_index = (int64_t)layout->index;
    state->has_record = ki_set_record(format, state);
    return state->has_record;
}
static const xx_var *ki_option(const xx_list_s *options, uint32_t id) {
    size_t i;
    for (i = 0U; options && i < options->count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}
bool xx_catsystem_kif_unpack_current_archive_record_to_device(Abstractformat *format,
    xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    ki_layout *layout;
    const ki_member *member;
    const xx_var *limit;
    uint8_t *buffer = NULL;
    size_t capacity = xx_get_file_buffer_size();
    uint32_t done = 0U;
    int64_t cursor, total;
    bool ok = false;
    if (!format || !format->device || !state || state->format != format ||
        !state->has_record || !(layout = (ki_layout *)state->internal_state) ||
        layout->index >= layout->count || destination == format->device || ki_stopped(pd))
        return false;
    member = &layout->members[layout->index];
    total = xx_io_total_size(format->device);
    if (member->offset < 0 || member->offset > total ||
        (int64_t)member->size > total - member->offset) return false;
    limit = ki_option(&state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit && member->size > xx_var_get_u64(limit)) return false;
    if (!capacity) capacity = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (capacity > 1048576U) capacity = 1048576U;
    if (capacity > member->size) capacity = member->size;
    limit = ki_option(&state->options, XX_META_ID_OPT_MEMORY_LIMIT);
    if (limit && xx_var_get_u64(limit) < capacity) capacity = (size_t)xx_var_get_u64(limit);
    if (layout->encrypted && member->size >= 8U) capacity &= ~(size_t)7U;
    cursor = xx_io_tell(format->device);
    if (!member->size) { ok = true; goto done; }
    if (!capacity || !(buffer = (uint8_t *)xx_mem_alloc(capacity))) goto done;
    while (done < member->size) {
        size_t take = member->size - done, wrote = 0U;
        if (take > capacity) take = capacity;
        if (!ki_read(format->device, member->offset + done, buffer, take, pd)) goto done;
        if (layout->encrypted) {
            size_t at;
            uint32_t aligned = member->size & ~UINT32_C(7);
            for (at = 0U; at + 8U <= take && (uint64_t)done + at < aligned; at += 8U) {
                uint32_t left = ki_le32(buffer + at), right = ki_le32(buffer + at + 4U);
                if ((at & 65535U) == 0U && ki_stopped(pd)) goto done;
                ki_bf_decrypt(&layout->cipher, &left, &right);
                ki_put32(buffer + at, left); ki_put32(buffer + at + 4U, right);
            }
        }
        while (destination && wrote < take) {
            ssize_t amount;
            if (ki_stopped(pd)) goto done;
            amount = xx_io_write(destination, buffer + wrote, take - wrote);
            if (amount <= 0 || (size_t)amount > take - wrote) goto done;
            wrote += (size_t)amount;
        }
        done += (uint32_t)take;
    }
    ok = !ki_stopped(pd);
done:
    if (buffer) xx_mem_free(buffer);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) ok = false;
    return ok;
}
static bool ki_reserved(const char *component, size_t length) {
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
static bool ki_safe_name(const char *name) {
    const char *component = name, *at;
    if (!name || !*name || *name == '/') return false;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || c == 127U || (c && c < 32U)) return false;
        if (c == '/' || !c) {
            size_t n = (size_t)(at - component);
            if (!n || component[0] == ' ' || component[n - 1U] == '.' ||
                component[n - 1U] == ' ' || ki_reserved(component, n)) return false;
            if (!c) return true;
            component = at + 1;
        }
    }
}
bool xx_catsystem_kif_unpack_current_archive_record(Abstractformat *format,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    ki_layout *layout;
    const xx_var *path_option, *overwrite_option;
    const char *base = NULL;
    char *owned_base = NULL, *path = NULL, *stage_path = NULL;
    xx_io_device *stage = NULL;
    bool ok = false, overwrite = false;
    unsigned attempt;
    if (!format || !state || state->format != format || !state->has_record ||
        !(layout = (ki_layout *)state->internal_state) ||
        layout->index >= layout->count || ki_stopped(pd)) return false;
    path_option = ki_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return xx_catsystem_kif_unpack_current_archive_record_to_device(format, state, NULL, pd);
    if (!ki_safe_name(layout->members[layout->index].name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option)); base = owned_base;
    }
    if (!base) goto done;
    overwrite_option = ki_option(&state->options, XX_META_ID_OPT_OVERWRITE);
    if (overwrite_option) overwrite = xx_var_get_bool(overwrite_option);
    path = !*base || base[xx_str_len(base) - 1U] == '/' || base[xx_str_len(base) - 1U] == '\\'
        ? xx_str_concat(base, layout->members[layout->index].name)
        : xx_str_concat3(base, "/", layout->members[layout->index].name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path, false)) goto done;
    stage_path = (char *)xx_mem_alloc(xx_str_len(path) + 50U);
    if (!stage_path) goto done;
    for (attempt = 0U; attempt < 128U && !ki_stopped(pd); ++attempt) {
        int wrote = xx_rt_snprintf(stage_path, xx_str_len(path) + 50U,
            "%s.xxfc-catsystem_kif-%u-%u.tmp", path, (unsigned)layout->index, attempt);
        if (wrote <= 0) goto done;
        stage = xx_io_file_open(stage_path, "wbx");
        if (stage) break;
    }
    if (!stage) goto done;
    ok = xx_catsystem_kif_unpack_current_archive_record_to_device(format, state, stage, pd);
    if (xx_io_close(stage)) ok = false;
    stage = NULL;
    if (ok && !ki_stopped(pd)) ok = xx_io_file_replace_a(stage_path, path, overwrite);
    else ok = false;
    if (!ok) (void)xx_io_file_remove_a(stage_path);
done:
    if (stage) { (void)xx_io_close(stage); (void)xx_io_file_remove_a(stage_path); }
    if (stage_path) xx_mem_free(stage_path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return ok;
}
void xx_catsystem_kif_free_archive_records_reading(Abstractformat *format,
    xx_archive_record_state *state) {
    (void)format; xx_archive_record_state_free(state);
}
