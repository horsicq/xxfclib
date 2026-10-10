/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * ApriDisk record layout from the author-corrected LibDsk ApriDisk note.
 * This native reader incorporates no LibDsk implementation code.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/apridisk/xx_apridisk.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"
#ifdef APRIDISK
#define APRI_TYPE XX_FILE_TYPE_APRIDISK
#else
#define APRI_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define APRI_HEADER 128U
#define APRI_SOURCE_MAX (8U * 1024U * 1024U)
#define APRI_CYLINDERS 80U
#define APRI_HEADS 2U
#define APRI_SECTORS 36U
#define APRI_SECTOR_SIZE 512U
#define APRI_SLOTS (APRI_CYLINDERS * APRI_HEADS * APRI_SECTORS)
#define APRI_RECORD_HEADER_MAX 4096U
#define APRI_TEXT_MAX 4096U
#define APRI_SECTOR_TYPE 0xE31D0001U
#define APRI_DELETED_TYPE 0xE31D0000U
#define APRI_COMMENT_TYPE 0xE31D0002U
#define APRI_CREATOR_TYPE 0xE31D0003U
#define APRI_RAW 0x9E90U
#define APRI_RLE 0x3E5AU
static const uint8_t apri_magic[] = "ACT Apricot disk image\x1A\x04";
typedef struct apri_slot_s {
    int64_t offset;
    uint8_t encoding, active;
} apri_slot;
typedef struct apri_view_s {
    uint32_t refs, count, raw_bytes;
    uint16_t cylinders, heads, sectors;
    int64_t base, size;
    apri_slot slots[APRI_SLOTS];
} apri_view;
typedef struct apri_cursor_s {
    apri_view *view;
    uint32_t index;
} apri_cursor;
static bool apri_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static uint32_t apri_le16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}
static void apri_release(apri_view *v)
{
    if (v && !--v->refs) xx_mem_free(v);
}
static bool apri_read(xx_io_device *d, int64_t at, uint8_t *p, size_t n, xx_pd_struct *pd)
{
    size_t done = 0U;
    if (apri_stop(pd) || xx_io_seek64(d, at, SEEK_SET) != 0) return false;
    while (done < n) {
        ssize_t got;
        if (apri_stop(pd)) return false;
        got = xx_io_read(d, p + done, n - done);
        if (got <= 0 || (size_t)got > n - done || apri_stop(pd)) return false;
        done += (size_t)got;
    }
    return !apri_stop(pd);
}
static uint32_t apri_index(uint32_t cyl, uint32_t head, uint32_t sector)
{
    return (cyl * APRI_HEADS + head) * APRI_SECTORS + sector - 1U;
}
static bool apri_record_shape(uint32_t encoding, uint32_t size)
{
    return (encoding == APRI_RAW && size == APRI_SECTOR_SIZE) || (encoding == APRI_RLE && size == 3U);
}
static apri_view *apri_parse(Abstractformat *f, xx_pd_struct *pd)
{
    xx_io_device *d;
    int64_t saved, total, pos;
    uint8_t header[APRI_HEADER], record[16];
    apri_view *v = NULL;
    bool ok = false;
    uint32_t max_c = 0U, max_h = 0U, max_s = 0U;
    if (!f || !(d = f->device) || f->base_address < 0 || apri_stop(pd)) return NULL;
    saved = xx_io_tell(d);
    total = xx_io_total_size(d);
    if (saved < 0 || total < f->base_address || total - f->base_address < APRI_HEADER + 16U || total - f->base_address > APRI_SOURCE_MAX ||
        !apri_read(d, f->base_address, header, sizeof(header), pd) || memcmp(header, apri_magic, sizeof(apri_magic) - 1U))
        goto done;
    {
        size_t i;
        for (i = sizeof(apri_magic) - 1U; i < APRI_HEADER; ++i)
            if (header[i] != 0U) goto done;
    }
    v = (apri_view *)xx_mem_calloc(1U, sizeof(*v));
    if (!v) goto done;
    v->refs = 1U;
    v->base = f->base_address;
    v->size = total - f->base_address;
    pos = f->base_address + APRI_HEADER;
    while (pos < total) {
        uint32_t type, encoding, hsize, size, head, sector, cyl, index;
        int64_t data;
        if (total - pos < 16 || !apri_read(d, pos, record, sizeof(record), pd)) goto done;
        type = xx_data_get_u32(record, 4, 0, false);
        encoding = apri_le16(record + 4U);
        hsize = apri_le16(record + 6U);
        size = xx_data_get_u32(record + 8U, 4, 0, false);
        head = record[12];
        sector = record[13];
        cyl = apri_le16(record + 14U);
        if (hsize < 16U || hsize > APRI_RECORD_HEADER_MAX || (uint64_t)hsize + size > (uint64_t)(total - pos)) goto done;
        data = pos + (int64_t)hsize;
        if (type == APRI_SECTOR_TYPE || type == APRI_DELETED_TYPE) {
            if (head >= APRI_HEADS || sector == 0U || sector > APRI_SECTORS || cyl >= APRI_CYLINDERS || !apri_record_shape(encoding, size)) goto done;
            if (encoding == APRI_RLE) {
                uint8_t run[3];
                if (!apri_read(d, data, run, sizeof(run), pd) || apri_le16(run) != APRI_SECTOR_SIZE) goto done;
            }
            if (type == APRI_SECTOR_TYPE) {
                index = apri_index(cyl, head, sector);
                if (v->slots[index].active) goto done;
                v->slots[index].active = 1U;
                v->slots[index].encoding = (uint8_t)(encoding == APRI_RLE);
                v->slots[index].offset = data;
                ++v->count;
                if (cyl > max_c) max_c = cyl;
                if (head > max_h) max_h = head;
                if (sector > max_s) max_s = sector;
            }
        } else if (type == APRI_COMMENT_TYPE || type == APRI_CREATOR_TYPE) {
            uint8_t last;
            if (encoding != APRI_RAW || size == 0U || size > APRI_TEXT_MAX || head || sector || cyl || !apri_read(d, data + size - 1U, &last, 1U, pd) || last) goto done;
        } else goto done;
        pos = data + size;
    }
    if (!v->count || v->count != (max_c + 1U) * (max_h + 1U) * max_s) goto done;
    v->cylinders = (uint16_t)(max_c + 1U);
    v->heads = (uint16_t)(max_h + 1U);
    v->sectors = (uint16_t)max_s;
    v->raw_bytes = v->count * APRI_SECTOR_SIZE;
    ok = !apri_stop(pd);
done:
    if (d && saved >= 0 && xx_io_seek64(d, saved, SEEK_SET) != 0) ok = false;
    if (!ok) {
        apri_release(v);
        return NULL;
    }
    return v;
}
static void apri_destroy_format(Abstractformat *f)
{
    xx_apridisk_destroy((xx_apridisk *)f);
}
void xx_apridisk_init(xx_apridisk *a, xx_io_device *d, int64_t base)
{
    if (!a) return;
    xx_mem_zero(a, sizeof(*a));
    xx_format_init(&a->format, d, base);
    a->format.file_type = APRI_TYPE;
    a->format.format_type = XX_TYPE_ARCHIVE;
    a->format.is_archive = true;
    xx_format_set_mime_type(&a->format, "application/x-apridisk");
    xx_format_set_extension(&a->format, "apr");
    a->format.check_is_valid = xx_apridisk_check_is_valid;
    a->format.handle_base_info = xx_apridisk_handle_base_info;
    a->format.get_format_size = xx_apridisk_get_format_size;
    a->format.get_number_of_archive_records = xx_apridisk_get_number_of_archive_records;
    a->format.create_archive_records_reading = xx_apridisk_create_archive_records_reading;
    a->format.get_current_archive_record = xx_apridisk_get_current_archive_record;
    a->format.archive_record_move_to_next = xx_apridisk_archive_record_move_to_next;
    a->format.unpack_current_archive_record = xx_apridisk_unpack_current_archive_record;
    a->format.free_archive_records_reading = xx_apridisk_free_archive_records_reading;
    a->format.destroy = apri_destroy_format;
}
xx_apridisk *xx_apridisk_create(xx_io_device *d, int64_t base)
{
    xx_apridisk *a = (xx_apridisk *)xx_mem_alloc(sizeof(*a));
    if (a) {
        xx_apridisk_init(a, d, base);
    }
    return a;
}
void xx_apridisk_destroy(xx_apridisk *a)
{
    if (!a) return;
    apri_release((apri_view *)a->internal);
    a->internal = NULL;
    xx_format_cleanup_extra_parameters(&a->format);
}
void xx_apridisk_free(xx_apridisk *a)
{
    if (a) {
        xx_apridisk_destroy(a);
        xx_mem_free(a);
    }
}
bool xx_apridisk_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    apri_view *v = apri_parse(f, pd);
    if (!v) return false;
    apri_release(v);
    return true;
}
bool xx_apridisk_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    xx_apridisk *a = (xx_apridisk *)f;
    apri_view *v;
    if (!f || apri_stop(pd)) return false;
    if (f->base_info_handled && a->internal) return f->is_valid;
    v = apri_parse(f, pd);
    if (!v) {
        f->base_info_handled = false;
        f->is_valid = false;
        return false;
    }
    apri_release((apri_view *)a->internal);
    a->internal = v;
    a->number_of_sectors = v->count;
    f->number_of_archive_records = 1U;
    f->format_size = v->size;
    f->overlay_offset = -1;
    f->overlay_size = 0;
    f->is_valid = true;
    f->base_info_handled = true;
    return true;
}
int64_t xx_apridisk_get_format_size(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_apridisk_handle_base_info(f, pd) ? f->format_size : -1;
}
uint64_t xx_apridisk_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_apridisk_handle_base_info(f, pd) ? 1U : 0U;
}
static void apri_cursor_free(void *p)
{
    apri_cursor *c = (apri_cursor *)p;
    if (c) {
        apri_release(c->view);
        xx_mem_free(c);
    }
}
static bool apri_record(xx_archive_record *r, const apri_view *v)
{
    xx_archive_record_cleanup(r);
    xx_archive_record_init(r);
    r->header_offset = v->base;
    r->header_size = APRI_HEADER;
    r->data_offset = -1;
    r->compressed_size = v->size;
    return xx_archive_record_set_original_name(r, "apridisk-disk.img") && xx_archive_record_set_meta_u64(r, XX_META_ID_UNCOMPRESSED_SIZE, v->raw_bytes) &&
           xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSED_SIZE, (uint64_t)v->size) && xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSION_METHOD, 1U) &&
           xx_archive_record_set_meta_bool(r, XX_META_ID_IS_FOLDER, false) && xx_archive_record_set_meta_bool(r, XX_META_ID_IS_ENCRYPTED, false);
}
xx_archive_record_state *xx_apridisk_create_archive_records_reading(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_apridisk *a = (xx_apridisk *)f;
    apri_view *v;
    apri_cursor *c;
    xx_archive_record_state *state;
    size_t i;
    if (!xx_apridisk_handle_base_info(f, pd)) return NULL;
    v = (apri_view *)a->internal;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    c = (apri_cursor *)xx_mem_calloc(1U, sizeof(*c));
    if (!state || !c) {
        xx_mem_free(state);
        xx_mem_free(c);
        return NULL;
    }
    ++v->refs;
    c->view = v;
    xx_archive_record_state_init(state, f);
    state->internal_state = c;
    state->free_internal = apri_cursor_free;
    state->total_records = 1U;
    if (options)
        for (i = 0U; i < options->count; ++i) {
            const xx_meta *original = (const xx_meta *)xx_list_at(options, i);
            xx_meta copy;
            if (!original) continue;
            xx_meta_init(&copy, original->meta_id);
            if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(&state->options, &copy)) {
                xx_meta_cleanup(&copy);
                xx_archive_record_state_free(state);
                return NULL;
            }
        }
    if (!apri_record(&state->current_record, v)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}
const xx_archive_record *xx_apridisk_get_current_archive_record(Abstractformat *f, xx_archive_record_state *state)
{
    return f && state && state->format == f && state->has_record ? &state->current_record : NULL;
}
bool xx_apridisk_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    if (!f || !state || state->format != f || !state->has_record || apri_stop(pd)) return false;
    state->has_record = false;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    return false;
}
static bool apri_limits(Abstractformat *f, xx_archive_record_state *state, uint64_t length)
{
    const xx_var *max = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    const xx_var *mem = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t needed = sizeof(apri_view) + sizeof(apri_cursor) + APRI_SECTOR_SIZE;
    return (!max || length <= xx_var_get_u64(max)) && (!mem || needed <= xx_var_get_u64(mem));
}
bool xx_apridisk_extract_record_to_device(Abstractformat *f, xx_archive_record_state *state, xx_io_device *out, xx_pd_struct *pd)
{
    apri_cursor *c;
    xx_io_device *source;
    uint8_t buffer[APRI_SECTOR_SIZE];
    uint32_t cyl, head, sec;
    int64_t saved;
    bool ok = true;
    if (!f || !state || state->format != f || !state->has_record || apri_stop(pd) || !(c = (apri_cursor *)state->internal_state) || !(source = f->device) ||
        out == source || !apri_limits(f, state, c->view->raw_bytes) || (saved = xx_io_tell(source)) < 0)
        return false;
    for (cyl = 0U; cyl < c->view->cylinders && !apri_stop(pd); ++cyl)
        for (head = 0U; head < c->view->heads && !apri_stop(pd); ++head)
            for (sec = 1U; sec <= c->view->sectors && !apri_stop(pd); ++sec) {
                const apri_slot *slot = &c->view->slots[apri_index(cyl, head, sec)];
                size_t written = 0U;
                if (!slot->active) {
                    ok = false;
                    goto done;
                }
                if (slot->encoding) {
                    uint8_t run[3];
                    if (!apri_read(source, slot->offset, run, sizeof(run), pd) || apri_le16(run) != APRI_SECTOR_SIZE) {
                        ok = false;
                        goto done;
                    }
                    memset(buffer, run[2], sizeof(buffer));
                } else if (!apri_read(source, slot->offset, buffer, sizeof(buffer), pd)) {
                    ok = false;
                    goto done;
                }
                while (out && written < sizeof(buffer) && !apri_stop(pd)) {
                    ssize_t step = xx_io_write(out, buffer + written, sizeof(buffer) - written);
                    if (step <= 0 || (size_t)step > sizeof(buffer) - written || apri_stop(pd)) {
                        ok = false;
                        goto done;
                    }
                    written += (size_t)step;
                }
            }
done:
    if (xx_io_seek64(source, saved, SEEK_SET) != 0) ok = false;
    return ok && cyl == c->view->cylinders && !apri_stop(pd);
}
static bool apri_equal_path(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x == '\\') {
            x = '/';
        }
        if (y == '\\') y = '/';
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
    }
    return *a == *b;
}
static xx_io_device *apri_stage(const char *destination, char **stage)
{
    size_t i, parent = 0U;
    unsigned attempt;
    char *directory;
    *stage = NULL;
    directory = xx_str_dup(destination);
    if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i)
        if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[40], *candidate;
        xx_io_device *device;
        (void)xx_rt_snprintf(suffix, sizeof(suffix), ".xx_apri.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix);
        if (!candidate) break;
        if (apri_equal_path(candidate, destination)) {
            xx_str_free(candidate);
            continue;
        }
        device = xx_io_file_open(candidate, "wbx");
        if (device) {
            *stage = candidate;
            xx_str_free(directory);
            return device;
        }
        xx_str_free(candidate);
    }
    xx_str_free(directory);
    return NULL;
}
bool xx_apridisk_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    const xx_var *option, *ov;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage = NULL;
    bool ok = false, overwrite;
    if (!f || !state || state->format != f || !state->has_record || apri_stop(pd)) return false;
    option = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    ov = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = ov && xx_var_get_bool(ov);
    if (!option) return xx_apridisk_extract_record_to_device(f, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned;
    }
    if (!base) goto done;
    path = *base && base[strlen(base) - 1U] != '/' && base[strlen(base) - 1U] != '\\' ? xx_str_concat3(base, "/", "apridisk-disk.img")
                                                                                      : xx_str_concat(base, "apridisk-disk.img");
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false) || apri_stop(pd)) goto done;
    {
        xx_io_device *output = apri_stage(path, &stage);
        if (!output) goto done;
        ok = xx_apridisk_extract_record_to_device(f, state, output, pd);
        if (xx_io_close(output) != 0) ok = false;
    }
    if (ok && !apri_stop(pd)) ok = xx_io_file_replace_a(stage, path, overwrite);
    else ok = false;
done:
    if (stage) {
        if (!ok) (void)xx_io_file_remove_a(stage);
        xx_str_free(stage);
    }
    xx_str_free(path);
    xx_str_free(owned);
    return ok;
}
void xx_apridisk_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *state)
{
    (void)f;
    xx_archive_record_state_free(state);
}
bool xx_apridisk_test_magic(const uint8_t *p, size_t n)
{
    return p && n >= sizeof(apri_magic) - 1U && !memcmp(p, apri_magic, sizeof(apri_magic) - 1U);
}
