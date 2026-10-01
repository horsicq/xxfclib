/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/sfx_nss/xx_sfx_nss.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include "xx_sfx_nss_crypto_private.h"
#include "xx_sfx_nss_codec_private.h"
#include <stdio.h>

#ifdef SFX_NSS
#define NSS_TYPE XX_FILE_TYPE_SFX_NSS
#else
#define NSS_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define NSS_HEADER_SIZE 316U
#define NSS_NAME_OFFSET 46U
#define NSS_NAME_SIZE 256U
#define NSS_MAX_RECORDS 4096U
#define NSS_MAX_VOLUME UINT64_C(1073741824)
#define NSS_MAX_MEMBER UINT32_C(67108864)

/* Norton Secret Stuff uses these built-in four-byte keys for SFX archives
 * made without a user password. Password-protected members use MD5(password)
 * bytes 4..7 instead. The eight-byte SYMANTEC block selects the valid key. */
static const uint8_t nss_default_keys[2][4] = {
    {0x07U, 0x49U, 0xedU, 0xf5U},
    {0xffU, 0xd4U, 0xcdU, 0x14U}
};

typedef struct nss_entry_s {
    char *name;
    int64_t header_at, data_at;
    uint32_t packed_size, raw_size;
    uint8_t iv[8];
    bool compressed;
} nss_entry;
typedef struct nss_view_s {
    nss_entry *entries;
    size_t count, index;
} nss_view;

static bool nss_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}
static uint16_t nss_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t nss_u32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static bool nss_read(xx_io_device *device, int64_t at, void *out,
                     size_t size) {
    int64_t saved;
    size_t done = 0U;
    bool ok = false;
    if (!device || at < 0 || (!out && size)) return false;
    saved = xx_io_tell(device);
    if (saved < 0) return false;
    if (xx_io_seek64(device, at, SEEK_SET) == 0) {
        while (done < size) {
            ssize_t got = xx_io_read(device, (uint8_t *)out + done,
                                     size - done);
            if (got <= 0 || (size_t)got > size - done) break;
            done += (size_t)got;
        }
        ok = done == size;
    }
    if (xx_io_seek64(device, saved, SEEK_SET) != 0) ok = false;
    return ok;
}
static bool nss_name_length(const uint8_t *raw, size_t *length) {
    size_t i = 0U, j;
    while (i < NSS_NAME_SIZE && raw[i] != 0U) ++i;
    if (!i || i == NSS_NAME_SIZE || raw[i - 1U] == '.' ||
        raw[i - 1U] == ' ' ||
        (i == 1U && raw[0] == '.') ||
        (i == 2U && raw[0] == '.' && raw[1] == '.')) return false;
    for (j = 0U; j < i; ++j) {
        uint8_t c = raw[j];
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' ||
            c == ':' || c == '<' || c == '>' || c == '"' ||
            c == '|' || c == '?' || c == '*') return false;
    }
    for (j = i + 1U; j < NSS_NAME_SIZE; ++j)
        if (raw[j] != 0U) return false;
    *length = i;
    return true;
}
static void nss_view_free(void *pointer) {
    nss_view *view = (nss_view *)pointer;
    size_t i;
    if (!view) return;
    if (view->entries)
        for (i = 0U; i < view->count; ++i)
            xx_mem_free(view->entries[i].name);
    xx_mem_free(view->entries);
    xx_mem_free(view);
}

/* The MZ file-length fields point to the first header. Each later header
 * starts immediately after the preceding ciphertext. The cursor is kept at
 * the byte before a header, so the packed-size step lands on the final byte
 * of that record's ciphertext. Validate the whole chain before listing. */
static bool nss_scan(Abstractformat *f, xx_pd_struct *pd,
                     nss_entry *entries, size_t capacity,
                     size_t *out_count, int64_t *out_size) {
    uint8_t mz[64], header[NSS_HEADER_SIZE];
    int64_t total;
    uint64_t volume_size, mz_size, cursor;
    uint16_t pages, last, paragraphs;
    size_t count = 0U;
    if (!f || !f->device || f->base_address < 0 || nss_stopped(pd))
        return false;
    total = xx_io_total_size(f->device);
    if (total < f->base_address ||
        (uint64_t)(total - f->base_address) > NSS_MAX_VOLUME ||
        total - f->base_address < (int64_t)(64U + NSS_HEADER_SIZE + 8U + 1U) ||
        !nss_read(f->device, f->base_address, mz, sizeof(mz)) ||
        mz[0] != 'M' || mz[1] != 'Z') return false;
    volume_size = (uint64_t)(total - f->base_address);
    last = nss_u16(mz + 2U);
    pages = nss_u16(mz + 4U);
    paragraphs = nss_u16(mz + 8U);
    if (!pages || last > 511U || paragraphs < 4U) return false;
    mz_size = (uint64_t)(pages - 1U) * 512U +
              (last ? (uint64_t)last : 512U);
    if (mz_size < (uint64_t)paragraphs * 16U + 1U ||
        mz_size < 65U ||
        mz_size - 1U > volume_size - NSS_HEADER_SIZE - 9U)
        return false;
    cursor = mz_size - 1U;
    while (cursor < volume_size - 1U && !nss_stopped(pd)) {
        uint32_t packed, raw;
        size_t name_length;
        nss_entry *item;
        if (volume_size - 1U - cursor < NSS_HEADER_SIZE ||
            !nss_read(f->device, f->base_address + (int64_t)cursor + 1,
                      header, sizeof(header)) ||
            nss_u16(header) != NSS_HEADER_SIZE ||
            xx_rt_memcmp(header + 30U, "CCRITTER", 8U) != 0 ||
            !nss_name_length(header + NSS_NAME_OFFSET, &name_length))
            return false;
        packed = nss_u32(header + 2U);
        raw = nss_u32(header + 6U);
        if (packed < 8U || packed > NSS_MAX_MEMBER ||
            (packed & 7U) != 0U || raw > NSS_MAX_MEMBER ||
            (!raw && nss_u16(header + 24U) != 0U) ||
            (uint64_t)packed >
                volume_size - 1U - cursor - NSS_HEADER_SIZE ||
            count >= NSS_MAX_RECORDS || (entries && count >= capacity))
            return false;
        if (entries) {
            item = &entries[count];
            item->name = (char *)xx_mem_alloc(name_length + 1U);
            if (!item->name) return false;
            xx_mem_copy(item->name, header + NSS_NAME_OFFSET,
                        name_length);
            item->name[name_length] = '\0';
            item->header_at = f->base_address + (int64_t)cursor + 1;
            item->data_at = item->header_at + NSS_HEADER_SIZE;
            item->packed_size = packed;
            item->raw_size = raw;
            xx_mem_copy(item->iv, header + 38U, sizeof(item->iv));
            item->compressed = nss_u16(header + 24U) != 0U;
        }
        cursor += NSS_HEADER_SIZE + (uint64_t)packed;
        ++count;
    }
    if (nss_stopped(pd) || !count || cursor != volume_size - 1U ||
        (entries && count != capacity)) return false;
    *out_count = count;
    *out_size = (int64_t)volume_size;
    return true;
}
static nss_view *nss_parse(Abstractformat *f, xx_pd_struct *pd) {
    nss_view *view;
    size_t count, checked;
    int64_t size;
    if (!nss_scan(f, pd, NULL, 0U, &count, &size)) return NULL;
    view = (nss_view *)xx_mem_calloc(1U, sizeof(*view));
    if (!view) return NULL;
    view->count = count;
    view->entries = (nss_entry *)xx_mem_calloc(count,
                                               sizeof(*view->entries));
    if (!view->entries ||
        !nss_scan(f, pd, view->entries, count, &checked, &size) ||
        checked != count) {
        nss_view_free(view);
        return NULL;
    }
    return view;
}
static bool nss_set_record(xx_archive_record *record,
                           const nss_entry *item) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = item->header_at;
    record->header_size = NSS_HEADER_SIZE;
    record->data_offset = item->data_at;
    record->compressed_size = item->packed_size;
    return xx_archive_record_set_original_name(record, item->name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          item->raw_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSED_SIZE,
                                          item->packed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          item->compressed ? 1U : 0U) &&
           xx_archive_record_set_meta_bool(record,
                                           XX_META_ID_IS_ENCRYPTED, true) &&
           xx_archive_record_set_meta_bool(record,
                                           XX_META_ID_IS_FOLDER, false);
}

static bool nss_copy_options(xx_list_s *target, const xx_list_s *options) {
    size_t i;
    if (!target || !options) return options == NULL;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        xx_meta copy;
        bool copied = false;
        if (!source) continue;
        xx_meta_init(&copy, source->meta_id);
        if (source->var.type == XX_VAR_TYPE_STRING_VIEW) {
            const char *text = xx_var_get_str(&source->var);
            size_t length = source->var.val.str.len;
            char *owned = length < SIZE_MAX ?
                (char *)xx_mem_alloc(length + 1U) : NULL;
            if (owned && (text || !length)) {
                if (length) xx_mem_copy(owned, text, length);
                owned[length] = '\0';
                copied = xx_var_set_str_take(&copy.var, owned, length);
            } else xx_mem_free(owned);
        } else if (source->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            const wchar_t *text = xx_var_get_wstr(&source->var);
            size_t length = source->var.val.wstr.len;
            wchar_t *owned = length < SIZE_MAX / sizeof(wchar_t) ?
                (wchar_t *)xx_mem_alloc((length + 1U) * sizeof(wchar_t)) :
                NULL;
            if (owned && (text || !length)) {
                if (length) xx_mem_copy(owned, text,
                                        length * sizeof(wchar_t));
                owned[length] = L'\0';
                copied = xx_var_set_wstr_take(&copy.var, owned, length);
            } else xx_mem_free(owned);
        } else if (source->var.type == XX_VAR_TYPE_BYTES_VIEW) {
            size_t length;
            const void *bytes = xx_var_get_bytes(&source->var, &length);
            copied = xx_var_set_bytes(&copy.var, bytes, length);
        } else copied = xx_var_copy(&copy.var, &source->var);
        if (!copied || !xx_list_append(target, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

void xx_sfx_nss_init(xx_sfx_nss *archive, xx_io_device *device,
                     int64_t base) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = NSS_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-norton-secret-stuff");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_sfx_nss_check_is_valid;
    archive->format.handle_base_info = xx_sfx_nss_handle_base_info;
    archive->format.get_format_size = xx_sfx_nss_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_nss_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_nss_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_nss_get_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_nss_archive_record_move_to_next;
    archive->format.unpack_current_archive_record =
        xx_sfx_nss_unpack_current_archive_record;
    archive->format.free_archive_records_reading =
        xx_sfx_nss_free_archive_records_reading;
}
xx_sfx_nss *xx_sfx_nss_create(xx_io_device *device, int64_t base) {
    xx_sfx_nss *archive = (xx_sfx_nss *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_nss_init(archive, device, base);
    return archive;
}
void xx_sfx_nss_destroy(xx_sfx_nss *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_sfx_nss_free(xx_sfx_nss *archive) {
    if (!archive) return;
    xx_sfx_nss_destroy(archive);
    xx_mem_free(archive);
}
bool xx_sfx_nss_check_is_valid(Abstractformat *f, xx_pd_struct *pd) {
    size_t count;
    int64_t size;
    return nss_scan(f, pd, NULL, 0U, &count, &size);
}
bool xx_sfx_nss_handle_base_info(Abstractformat *f, xx_pd_struct *pd) {
    size_t count;
    int64_t size;
    if (!nss_scan(f, pd, NULL, 0U, &count, &size)) return false;
    ((xx_sfx_nss *)f)->number_of_records = count;
    f->number_of_archive_records = count;
    f->format_size = size;
    f->overlay_offset = -1;
    f->overlay_size = 0;
    f->is_valid = true;
    f->base_info_handled = true;
    return true;
}
int64_t xx_sfx_nss_get_format_size(Abstractformat *f, xx_pd_struct *pd) {
    return f && (f->base_info_handled ||
                 xx_sfx_nss_handle_base_info(f, pd)) ?
           f->format_size : -1;
}
uint64_t xx_sfx_nss_get_number_of_archive_records(
    Abstractformat *f, xx_pd_struct *pd) {
    return f && (f->base_info_handled ||
                 xx_sfx_nss_handle_base_info(f, pd)) ?
           f->number_of_archive_records : 0U;
}
xx_archive_record_state *xx_sfx_nss_create_archive_records_reading(
    Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    nss_view *view = nss_parse(f, pd);
    xx_archive_record_state *state;
    if (!view) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { nss_view_free(view); return NULL; }
    xx_archive_record_state_init(state, f);
    state->internal_state = view;
    state->free_internal = nss_view_free;
    state->total_records = (int64_t)view->count;
    if (!nss_copy_options(&state->options, options) ||
        !nss_set_record(&state->current_record, &view->entries[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}
const xx_archive_record *xx_sfx_nss_get_current_archive_record(
    Abstractformat *f, xx_archive_record_state *state) {
    return f && state && state->format == f && state->has_record ?
           &state->current_record : NULL;
}
bool xx_sfx_nss_archive_record_move_to_next(
    Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    nss_view *view;
    if (!f || !state || state->format != f || !state->has_record ||
        !(view = (nss_view *)state->internal_state) || nss_stopped(pd))
        return false;
    if (++view->index >= view->count ||
        !nss_set_record(&state->current_record,
                        &view->entries[view->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}
bool xx_sfx_nss_unpack_current_archive_record(
    Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    nss_view *view;
    const nss_entry *item;
    const xx_var *password_option, *path_option;
    const uint8_t *password = NULL;
    size_t password_size = 0U, decoded_size = 0U, written = 0U;
    size_t candidate, candidate_count;
    char *password_utf8 = NULL, *path_utf8 = NULL, *target = NULL;
    uint8_t *packed = NULL, *plain = NULL;
    uint8_t probe[8];
    nss_blowfish cipher;
    bool ok = false, created = false;
    if (!f || !state || state->format != f || !state->has_record ||
        !(view = (nss_view *)state->internal_state) ||
        view->index >= view->count || nss_stopped(pd)) return false;
    item = &view->entries[view->index];
    password_option = xx_format_resolve_extra_parameter(
        f, &state->options, XX_META_ID_OPT_PASSWORD);
    if (password_option) {
        switch (password_option->type) {
        case XX_VAR_TYPE_STRING:
        case XX_VAR_TYPE_STRING_VIEW:
            password = (const uint8_t *)xx_var_get_str(password_option);
            password_size = password_option->val.str.len;
            break;
        case XX_VAR_TYPE_BYTES:
        case XX_VAR_TYPE_BYTES_VIEW:
            password = (const uint8_t *)xx_var_get_bytes(password_option,
                                                          &password_size);
            break;
        case XX_VAR_TYPE_WSTRING:
        case XX_VAR_TYPE_WSTRING_VIEW: {
            const wchar_t *wide = xx_var_get_wstr(password_option);
            size_t length = password_option->val.wstr.len;
            wchar_t *terminated;
            if ((!wide && length) ||
                length > SIZE_MAX / sizeof(wchar_t) - 1U) return false;
            terminated = (wchar_t *)xx_mem_alloc(
                (length + 1U) * sizeof(wchar_t));
            if (!terminated) return false;
            if (length) xx_mem_copy(terminated, wide,
                                    length * sizeof(wchar_t));
            terminated[length] = L'\0';
            password_utf8 = xx_str_unicode_to_utf8(terminated);
            xx_mem_zero(terminated, (length + 1U) * sizeof(wchar_t));
            xx_mem_free(terminated);
            if (!password_utf8) return false;
            password = (const uint8_t *)password_utf8;
            password_size = xx_str_len(password_utf8);
            break;
        }
        default:
            return false;
        }
    }
    if (!password && password_size) goto done;
    packed = (uint8_t *)xx_mem_alloc(item->packed_size);
    plain = (uint8_t *)xx_mem_alloc(item->raw_size ? item->raw_size : 1U);
    if (!packed || !plain ||
        !nss_read(f->device, item->data_at, packed, item->packed_size))
        goto done;
    candidate_count = password_option ? 1U : 3U;
    for (candidate = 0U; candidate < candidate_count; ++candidate) {
        size_t probe_size = 0U;
        bool initialized = password_option || candidate == 2U ?
            nss_crypto_init_password_bytes(&cipher, password,
                                            password_size) :
            nss_crypto_init_key(&cipher, nss_default_keys[candidate]);
        if (!initialized) break;
        ok = nss_crypto_decrypt_member(&cipher, item->iv, packed,
                                       item->packed_size >= 16U ? 16U : 8U,
                                       probe, sizeof(probe), &probe_size) &&
             probe_size == (item->packed_size >= 16U ? sizeof(probe) : 0U) &&
             nss_crypto_decrypt_member(&cipher, item->iv, packed,
                                       item->packed_size, packed,
                                       item->packed_size, &decoded_size);
        nss_crypto_cleanup(&cipher);
        xx_mem_zero(probe, sizeof(probe));
        if (ok) break;
    }
    if (ok && item->compressed)
        ok = nss_decode_lzw_rle(packed, decoded_size, plain,
                                  item->raw_size);
    else if (ok) {
        ok = decoded_size >= item->raw_size &&
             decoded_size - item->raw_size <= 8U;
        if (ok) xx_mem_copy(plain, packed, item->raw_size);
    }
    if (!ok || nss_stopped(pd)) {
        ok = false;
        goto done;
    }
    path_option = xx_format_resolve_extra_parameter(
        f, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) goto done; /* Test the member without writing it. */
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        const char *base = xx_var_get_str(path_option);
        size_t length = path_option->val.str.len;
        if ((!base && length) || length == SIZE_MAX) {
            ok = false;
            goto done;
        }
        path_utf8 = (char *)xx_mem_alloc(length + 1U);
        if (!path_utf8) { ok = false; goto done; }
        if (length) xx_mem_copy(path_utf8, base, length);
        path_utf8[length] = '\0';
        if (xx_str_len(path_utf8) != length) {
            ok = false;
            goto done;
        }
        target = xx_str_concat3(*path_utf8 ? path_utf8 : ".", "/",
                                item->name);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        const wchar_t *base = xx_var_get_wstr(path_option);
        size_t length = path_option->val.wstr.len;
        wchar_t *terminated;
        if ((!base && length) ||
            length > SIZE_MAX / sizeof(wchar_t) - 1U) {
            ok = false;
            goto done;
        }
        terminated = (wchar_t *)xx_mem_alloc(
            (length + 1U) * sizeof(wchar_t));
        if (!terminated) { ok = false; goto done; }
        if (length) xx_mem_copy(terminated, base,
                                length * sizeof(wchar_t));
        terminated[length] = L'\0';
        if (xx_str_wlen(terminated) != length) {
            xx_mem_free(terminated);
            ok = false;
            goto done;
        }
        path_utf8 = xx_str_unicode_to_utf8(terminated);
        xx_mem_free(terminated);
        if (!path_utf8) { ok = false; goto done; }
        target = xx_str_concat3(*path_utf8 ? path_utf8 : ".", "/",
                                item->name);
    } else {
        ok = false;
        goto done;
    }
    if (!target || !xx_store_create_dirs_a(target, false)) {
        ok = false;
        goto done;
    }
    {
        xx_io_device *out = xx_io_file_open(target, "wb");
        created = out != NULL;
        ok = created;
        while (ok && written < item->raw_size) {
            ssize_t sent = xx_io_write(out, plain + written,
                                       item->raw_size - written);
            if (sent <= 0 || (size_t)sent > item->raw_size - written)
                ok = false;
            else written += (size_t)sent;
        }
        if (out && xx_io_close(out) != 0) ok = false;
    }
done:
    if (!ok && created) xx_rt_remove(target);
    if (plain) {
        xx_mem_zero(plain, item->raw_size);
        xx_mem_free(plain);
    }
    if (packed) {
        xx_mem_zero(packed, item->packed_size);
        xx_mem_free(packed);
    }
    if (password_utf8) {
        xx_mem_zero(password_utf8, password_size);
        xx_str_free(password_utf8);
    }
    xx_str_free(path_utf8);
    xx_str_free(target);
    return ok;
}
void xx_sfx_nss_free_archive_records_reading(
    Abstractformat *f, xx_archive_record_state *state) {
    (void)f;
    xx_archive_record_state_free(state);
}
