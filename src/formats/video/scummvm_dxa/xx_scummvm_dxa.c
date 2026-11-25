/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent implementation of the DEXA container and its simple zlib/XOR
 * video modes. Format facts verified against the primary implementations:
 * https://github.com/scummvm/scummvm/blob/master/video/dxa_decoder.cpp
 * https://github.com/scummvm/scummvm-tools/blob/master/encode_dxa.cpp
 * No GPL decoder code is incorporated. The library's native RFC1950 decoder
 * supplies decompression; BMP serialization and stream traversal are original.
 */
#include "xxfclib/formats/video/scummvm_dxa/xx_scummvm_dxa.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>

#define SDXA_MAX_PACKED (16U * 1024U * 1024U)
typedef struct sdxa_picture {
    int64_t header, palette, data;
    uint32_t packed;
    uint8_t method;
} sdxa_picture;
typedef struct sdxa_stream {
    uint32_t width, height, coded_height, frames, at, decoded;
    uint8_t flags, palette[768];
    int64_t wav, wav_size, end;
    sdxa_picture *pictures;
    uint8_t *pixels, *delta;
} sdxa_stream;

static bool sdxa_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool sdxa_read(Abstractformat *f, int64_t p, void *buffer, size_t n)
{
    uint8_t *out = (uint8_t *)buffer;
    if (!f || !f->device || p < 0 || f->base_address < 0 || p > INT64_MAX - f->base_address || xx_io_seek64(f->device, f->base_address + p, SEEK_SET)) return false;
    while (n) {
        ssize_t got = xx_io_read(f->device, out, n);
        if (got <= 0 || (size_t)got > n) return false;
        out += got;
        n -= (size_t)got;
    }
    return true;
}
static bool sdxa_write(xx_io_device *d, const void *buffer, size_t n)
{
    const uint8_t *p = (const uint8_t *)buffer;
    if (!d) return true;
    while (n) {
        ssize_t done = xx_io_write(d, p, n);
        if (done <= 0 || (size_t)done > n) return false;
        p += done;
        n -= (size_t)done;
    }
    return true;
}
static void sdxa_free(void *v)
{
    sdxa_stream *s = (sdxa_stream *)v;
    if (!s) return;
    xx_mem_free(s->pictures);
    xx_mem_free(s->pixels);
    xx_mem_free(s->delta);
    xx_mem_free(s);
}
static bool sdxa_span(int64_t p, uint64_t n, int64_t limit)
{
    return p >= 0 && p <= limit && n <= (uint64_t)(limit - p);
}
static bool sdxa_parse(Abstractformat *f, sdxa_stream *s, bool index, xx_pd_struct *pd)
{
    uint8_t h[23], tag[9], wave[12];
    int64_t total, p;
    uint32_t i, timing;
    if (!f || !f->device || f->base_address < 0 || sdxa_stop(pd)) return false;
    total = xx_io_size(f->device);
    if (total < f->base_address) return false;
    total -= f->base_address;
    if (!sdxa_span(0, sizeof(h), total) || !sdxa_read(f, 0, h, sizeof(h)) || xx_rt_memcmp(h, "DEXA", 4)) return false;
    s->flags = h[4];
    s->frames = xx_data_get_u16(h + 5, 2, 0, true);
    timing = xx_data_get_u32(h + 7, 4, 0, true);
    /* Zero means the documented default of 10 fps. Otherwise the signed
     * field is a positive millisecond duration or a negative 1/100000-second
     * duration; INT32_MIN has no representable positive counterpart. */
    if (timing == UINT32_C(0x80000000)) return false;
    s->width = xx_data_get_u16(h + 11, 2, 0, true);
    s->height = xx_data_get_u16(h + 13, 2, 0, true);
    if ((s->flags & 0x3fU) || !s->frames || !s->width || s->width > 2048 || !s->height || s->height > 2048 || ((s->flags & 0xc0U) && (s->height & 1U))) return false;
    s->coded_height = (s->flags & 0xc0U) ? s->height / 2U : s->height;
    p = 19;
    if (!xx_rt_memcmp(h + 15, "WAVE", 4)) {
        s->wav_size = xx_data_get_u32(h + 19, 4, 0, true);
        s->wav = 23;
        if (s->wav_size < 12 || !sdxa_span(s->wav, (uint64_t)s->wav_size, total) || !sdxa_read(f, s->wav, wave, 12) || xx_rt_memcmp(wave, "RIFF", 4) ||
            xx_rt_memcmp(wave + 8, "WAVE", 4) || (uint64_t)xx_data_get_u32(wave + 4, 4, 0, false) + 8U != (uint64_t)s->wav_size)
            return false;
        p = s->wav + s->wav_size;
    } else if (xx_rt_memcmp(h + 15, "NULL", 4)) return false;
    if (index) {
        s->pictures = (sdxa_picture *)xx_mem_alloc((size_t)s->frames * sizeof(*s->pictures));
        if (!s->pictures) return false;
        xx_mem_zero(s->pictures, (size_t)s->frames * sizeof(*s->pictures));
    }
    for (i = 0; i < s->frames; ++i) {
        sdxa_picture picture;
        if (sdxa_stop(pd) || !sdxa_span(p, 4, total) || !sdxa_read(f, p, tag, 4)) return false;
        xx_mem_zero(&picture, sizeof(picture));
        picture.header = p;
        if (!xx_rt_memcmp(tag, "CMAP", 4)) {
            if (!sdxa_span(p, 772, total)) return false;
            picture.palette = p + 4;
            p += 772;
        } else if (!xx_rt_memcmp(tag, "NULL", 4)) p += 4;
        else return false;
        if (!sdxa_span(p, 4, total) || !sdxa_read(f, p, tag, 4)) return false;
        if (!xx_rt_memcmp(tag, "FRAM", 4)) {
            if (!sdxa_span(p, 9, total) || !sdxa_read(f, p, tag, 9)) return false;
            picture.method = tag[4];
            picture.packed = xx_data_get_u32(tag + 5, 4, 0, true);
            picture.data = p + 9;
            if ((picture.method != 2 && picture.method != 3) || picture.packed < 6 || picture.packed > SDXA_MAX_PACKED || !sdxa_span(picture.data, picture.packed, total))
                return false;
            p = picture.data + picture.packed;
        } else if (!xx_rt_memcmp(tag, "NULL", 4)) {
            picture.data = p;
            p += 4;
        } else return false;
        if (index) s->pictures[i] = picture;
    }
    s->end = p;
    return !sdxa_stop(pd);
}
static bool sdxa_decode_to(Abstractformat *f, sdxa_stream *s, uint32_t target, xx_pd_struct *pd)
{
    size_t area = (size_t)s->width * s->coded_height;
    if (target >= s->frames || sdxa_stop(pd)) return false;
    if (!s->pixels) {
        uint8_t *pixels = (uint8_t *)xx_mem_alloc(area), *delta = (uint8_t *)xx_mem_alloc(area);
        if (!pixels || !delta) {
            xx_mem_free(pixels);
            xx_mem_free(delta);
            return false;
        }
        s->pixels = pixels;
        s->delta = delta;
        xx_mem_zero(s->pixels, area);
    }
    if (target < s->decoded) {
        s->decoded = 0;
        xx_mem_zero(s->pixels, area);
        xx_mem_zero(s->palette, sizeof(s->palette));
    }
    while (s->decoded <= target) {
        const sdxa_picture *p = &s->pictures[s->decoded];
        if (sdxa_stop(pd)) return false;
        if (p->palette && !sdxa_read(f, p->palette, s->palette, sizeof(s->palette))) return false;
        if (p->method) {
            uint8_t *packed = (uint8_t *)xx_mem_alloc(p->packed);
            size_t produced = 0, i;
            bool ok = packed && sdxa_read(f, p->data, packed, p->packed) && xx_zlib_stream_decode_memory(packed, p->packed, s->delta, area, &produced) &&
                      produced == area && xx_zlib_stream_trailer_matches(packed, p->packed, s->delta, area);
            xx_mem_free(packed);
            if (!ok || sdxa_stop(pd)) return false;
            if (p->method == 2) xx_rt_memcpy(s->pixels, s->delta, area);
            else
                for (i = 0; i < area; ++i) s->pixels[i] ^= s->delta[i];
        }
        ++s->decoded;
    }
    return true;
}
static void sdxa_u32(uint8_t *p, uint32_t n)
{
    unsigned i;
    for (i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (i * 8U));
}
static uint32_t sdxa_bmp_size(const sdxa_stream *s)
{
    return 1078U + ((s->width + 3U) & ~3U) * s->height;
}
static bool sdxa_bmp(Abstractformat *f, sdxa_stream *s, uint32_t frame, xx_io_device *out, xx_pd_struct *pd)
{
    uint8_t header[1078], padding[3] = {0, 0, 0}, black[2048];
    uint32_t i, y, stride = (s->width + 3U) & ~3U;
    if (!sdxa_decode_to(f, s, frame, pd)) return false;
    if (!out) return true;
    xx_mem_zero(header, sizeof(header));
    xx_mem_zero(black, sizeof(black));
    header[0] = 'B';
    header[1] = 'M';
    sdxa_u32(header + 2, sdxa_bmp_size(s));
    sdxa_u32(header + 10, 1078);
    sdxa_u32(header + 14, 40);
    sdxa_u32(header + 18, s->width);
    sdxa_u32(header + 22, 0U - s->height);
    header[26] = 1;
    header[28] = 8;
    sdxa_u32(header + 34, stride * s->height);
    sdxa_u32(header + 46, 256);
    for (i = 0; i < 256; ++i) {
        header[54 + i * 4] = s->palette[i * 3 + 2];
        header[55 + i * 4] = s->palette[i * 3 + 1];
        header[56 + i * 4] = s->palette[i * 3];
    }
    if (!sdxa_write(out, header, sizeof(header))) return false;
    for (y = 0; y < s->height; ++y) {
        const uint8_t *row = s->pixels + (size_t)((s->flags & 0xc0U) ? y / 2U : y) * s->width;
        if ((s->flags & 0x80U) && (y & 1U)) row = black;
        if (sdxa_stop(pd) || !sdxa_write(out, row, s->width) || !sdxa_write(out, padding, stride - s->width)) return false;
    }
    return true;
}
static bool sdxa_wave(Abstractformat *f, sdxa_stream *s, xx_io_device *out, xx_pd_struct *pd)
{
    uint8_t buffer[65536];
    int64_t p = 0;
    while (p < s->wav_size) {
        size_t n = (uint64_t)(s->wav_size - p) > sizeof(buffer) ? sizeof(buffer) : (size_t)(s->wav_size - p);
        if (sdxa_stop(pd) || !sdxa_read(f, s->wav + p, buffer, n) || !sdxa_write(out, buffer, n)) return false;
        p += (int64_t)n;
    }
    return true;
}
static bool sdxa_record(Abstractformat *f, xx_archive_record_state *state)
{
    sdxa_stream *s = (sdxa_stream *)state->internal_state;
    xx_archive_record *r = &state->current_record;
    bool wave = s->wav_size && s->at == 0;
    uint32_t frame = s->at - (s->wav_size ? 1U : 0U);
    char name[40];
    xx_archive_record_cleanup(r);
    xx_archive_record_init(r);
    if (wave) {
        xx_rt_snprintf(name, sizeof(name), "audio.wav");
        r->data_offset = f->base_address + s->wav;
        r->compressed_size = s->wav_size;
    } else {
        const sdxa_picture *p = &s->pictures[frame];
        xx_rt_snprintf(name, sizeof(name), "frame-%06u.bmp", frame + 1U);
        r->header_offset = f->base_address + p->header;
        r->header_size = p->data - p->header;
        r->data_offset = f->base_address + p->data;
        r->compressed_size = p->packed;
    }
    return xx_archive_record_set_original_name(r, name) && xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSED_SIZE, (uint64_t)r->compressed_size) &&
           xx_archive_record_set_meta_u64(r, XX_META_ID_UNCOMPRESSED_SIZE, wave ? (uint64_t)s->wav_size : sdxa_bmp_size(s)) &&
           xx_archive_record_set_meta_bool(r, XX_META_ID_IS_FOLDER, false) && xx_archive_record_set_meta_bool(r, XX_META_ID_IS_ENCRYPTED, false);
}
static void sdxa_destroy(Abstractformat *f)
{
    xx_format_cleanup_extra_parameters(f);
}
void xx_scummvm_dxa_init(xx_scummvm_dxa *r, xx_io_device *d, int64_t base)
{
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    xx_format_init(&r->format, d, base);
    r->format.file_type = XX_FILE_TYPE_SCUMMVM_DXA;
    r->format.format_type = XX_TYPE_ARCHIVE;
    r->format.is_archive = true;
    r->format.endian = XX_ENDIAN_BIG;
    xx_format_set_extension(&r->format, "dxa");
    xx_format_set_mime_type(&r->format, "video/x-dxa");
    r->format.check_is_valid = xx_scummvm_dxa_check_is_valid;
    r->format.handle_base_info = xx_scummvm_dxa_handle_base_info;
    r->format.get_format_size = xx_scummvm_dxa_get_format_size;
    r->format.get_number_of_archive_records = xx_scummvm_dxa_get_number_of_archive_records;
    r->format.create_archive_records_reading = xx_scummvm_dxa_create_archive_records_reading;
    r->format.get_current_archive_record = xx_scummvm_dxa_get_current_archive_record;
    r->format.unpack_current_archive_record = xx_scummvm_dxa_unpack_current_archive_record;
    r->format.archive_record_move_to_next = xx_scummvm_dxa_archive_record_move_to_next;
    r->format.free_archive_records_reading = xx_scummvm_dxa_free_archive_records_reading;
    r->format.destroy = sdxa_destroy;
}
xx_scummvm_dxa *xx_scummvm_dxa_create(xx_io_device *d, int64_t base)
{
    xx_scummvm_dxa *r = (xx_scummvm_dxa *)xx_mem_alloc(sizeof(*r));
    if (r) xx_scummvm_dxa_init(r, d, base);
    return r;
}
void xx_scummvm_dxa_destroy(xx_scummvm_dxa *r)
{
    if (r) sdxa_destroy(&r->format);
}
void xx_scummvm_dxa_free(xx_scummvm_dxa *r)
{
    if (r) {
        xx_scummvm_dxa_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_scummvm_dxa_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    sdxa_stream s;
    xx_mem_zero(&s, sizeof(s));
    return sdxa_parse(f, &s, false, pd);
}
bool xx_scummvm_dxa_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    sdxa_stream s;
    xx_scummvm_dxa *r = (xx_scummvm_dxa *)f;
    xx_mem_zero(&s, sizeof(s));
    if (!sdxa_parse(f, &s, false, pd)) return false;
    r->width = s.width;
    r->height = s.height;
    r->frame_count = s.frames;
    r->flags = s.flags;
    f->number_of_archive_records = s.frames + (s.wav_size ? 1U : 0U);
    f->format_size = s.end;
    f->is_valid = true;
    f->base_info_handled = true;
    return true;
}
int64_t xx_scummvm_dxa_get_format_size(Abstractformat *f, xx_pd_struct *pd)
{
    return f && (f->base_info_handled || xx_scummvm_dxa_handle_base_info(f, pd)) ? f->format_size : -1;
}
uint64_t xx_scummvm_dxa_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *pd)
{
    return f && (f->base_info_handled || xx_scummvm_dxa_handle_base_info(f, pd)) ? f->number_of_archive_records : 0;
}
xx_archive_record_state *xx_scummvm_dxa_create_archive_records_reading(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    size_t i;
    sdxa_stream *s = (sdxa_stream *)xx_mem_alloc(sizeof(*s));
    xx_archive_record_state *state;
    if (!s) return NULL;
    xx_mem_zero(s, sizeof(*s));
    if (!sdxa_parse(f, s, true, pd)) {
        sdxa_free(s);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        sdxa_free(s);
        return NULL;
    }
    xx_archive_record_state_init(state, f);
    state->internal_state = s;
    state->free_internal = sdxa_free;
    state->total_records = s->frames + (s->wav_size ? 1U : 0U);
    if (options)
        for (i = 0; i < options->count; ++i) {
            const xx_meta *source = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
            xx_meta copy;
            if (!source) continue;
            xx_meta_init(&copy, source->meta_id);
            if (!xx_var_copy(&copy.var, &source->var) || !xx_list_append(&state->options, &copy)) {
                xx_meta_cleanup(&copy);
                xx_archive_record_state_free(state);
                return NULL;
            }
        }
    state->has_record = sdxa_record(f, state);
    if (!state->has_record) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    return state;
}
const xx_archive_record *xx_scummvm_dxa_get_current_archive_record(Abstractformat *f, xx_archive_record_state *state)
{
    return f && state && state->format == f && state->has_record ? &state->current_record : NULL;
}
bool xx_scummvm_dxa_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    sdxa_stream *s;
    if (!f || !state || state->format != f || !state->has_record || !(s = (sdxa_stream *)state->internal_state) || sdxa_stop(pd)) return false;
    if (++s->at >= state->total_records) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = sdxa_record(f, state);
    return state->has_record;
}
bool xx_scummvm_dxa_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    sdxa_stream *s;
    size_t i;
    const xx_var *option = NULL;
    const char *base = NULL;
    char *owned = NULL, *path = NULL;
    xx_io_device *out = NULL;
    bool result = false, created = false, wave;
    if (!f || !state || state->format != f || !state->has_record || !(s = (sdxa_stream *)state->internal_state) || sdxa_stop(pd)) return false;
    for (i = 0; i < state->options.count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)&state->options, i);
        if (m && m->meta_id == XX_META_ID_OPT_UNPACK_PATH) {
            option = &m->var;
            break;
        }
    }
    if (option) {
        if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
        else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
            owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
            base = owned;
        }
        if (!base) goto done;
        path = xx_str_concat3(base, "/", xx_archive_record_get_original_name(&state->current_record));
        if (!path || !xx_store_create_dirs_a(path, false)) goto done;
        out = xx_io_file_open(path, "wb");
        if (!out) goto done;
        created = true;
    }
    wave = s->wav_size && s->at == 0;
    result = wave ? sdxa_wave(f, s, out, pd) : sdxa_bmp(f, s, s->at - (s->wav_size ? 1U : 0U), out, pd);
done:
    if (out && xx_io_close(out)) result = false;
    if (!result && created && path) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned) xx_str_free(owned);
    return result;
}
void xx_scummvm_dxa_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *state)
{
    (void)f;
    xx_archive_record_state_free(state);
}
