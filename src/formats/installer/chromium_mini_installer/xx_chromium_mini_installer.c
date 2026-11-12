/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Chromium mini_installer. The setup archive travels as one large resource
 * (id 102) whose type names the packing: "B7" a raw 7-Zip archive (that case
 * is the sfx_7zip reader's), "BL" an .lzma stream (5 property bytes, 8-byte
 * size, LZMA) around the 7-Zip archive, "B" Brave's variant: an .lzma stream
 * around a 20-byte table of five little-endian u32 (the output tar size and
 * the sizes of four BCJ2 streams: main, call-targets, jump-targets, range-
 * coder) followed by those four streams back to back; BCJ2 decoding restores
 * the original ustar tar of the browser distribution. The resource is decoded
 * to a temporary device, Brave's BCJ2 payload is decoded to a second temporary
 * device, and the archive inside is read by the 7-Zip or tar reader; its
 * records are this reader's records. Nothing is executed.
 */
#include "xxfclib/formats/installer/chromium_mini_installer/xx_chromium_mini_installer.h"
#include "../../common/xx_executable_carrier.h"
#include "../../common/xx_pe_resource_locator.h"
#include "../../common/xx_pe_resource_archives.h"
#include "xxfclib/formats/archive/7zip/xx_7zip.h"
#include "xxfclib/formats/archive/tar/xx_tar.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_data.h"
#include <wchar.h>

#define CM_LZMA_HEADER 13U
#define CM_MIN_RESOURCE (1024U * 1024U)
#define CM_PROBE 320U
#define CM_TABLE 20U
#define CM_MAX_UNPACKED (INT64_C(8) * 1024 * 1024 * 1024)

typedef struct cm_bundle {
    xx_io_device *store; /* the decoded .lzma payload */
    xx_io_device *view;  /* BCJ2-decoded tar of a Brave payload */
    xx_tar *tar;         /* the nested reader as created: Brave's tar ... */
    xx_7zip *archive;    /* ... or Chromium's 7-Zip archive */
    Abstractformat *nested; /* the same reader as a format */
} cm_bundle;
typedef struct cm_cursor {
    cm_bundle *bundle;
    xx_archive_record_state *nested;
} cm_cursor;

static const uint8_t cm_lzma_signature[3] = {0x5D, 0x00, 0x00};
static const uint8_t cm_7z_signature[6] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};

static void cm_bundle_free(cm_bundle *b)
{
    if (!b) return;
    /* Readers made by their own create functions need their own free
     * (the generic free only releases factory-made readers). */
    if (b->tar) xx_tar_free(b->tar);
    else if (b->archive) xx_7zip_free(b->archive);
    if (b->view) xx_io_close(b->view);
    if (b->store) xx_io_close(b->store);
    xx_mem_free(b);
}

/* The 13-byte .lzma header of a resource: properties 0x5D (lc 3, lp 0,
 * pb 2), a dictionary of at most 256 MiB, a known uncompressed size. */
static bool cm_lzma_header(Abstractformat *f, int64_t at, uint32_t bytes, uint8_t props[5], int64_t *unpacked)
{
    uint8_t h[CM_LZMA_HEADER];
    uint32_t dictionary;
    uint64_t size;
    if (bytes < CM_MIN_RESOURCE || !pm_read(f, at, h, sizeof(h))) return false;
    dictionary = xx_data_get_u32(h + 1, 4, 0, false);
    size = xx_data_get_u64(h + 5, 8, 0, false);
    if (h[0] != 0x5DU || dictionary < 4096U || dictionary > 256U * 1024U * 1024U || size < CM_PROBE || size > (uint64_t)CM_MAX_UNPACKED) return false;
    xx_rt_memcpy(props, h, 5U);
    *unpacked = (int64_t)size;
    return true;
}

/* Is the decoded payload a 7-Zip archive or Brave's table + tar? The first
 * CM_PROBE bytes decide; a tar header has "ustar" at offset 257. */
static bool cm_payload_kind(const uint8_t *head, size_t n, bool *brave)
{
    if (n >= sizeof(cm_7z_signature) && xx_rt_memcmp(head, cm_7z_signature, sizeof(cm_7z_signature)) == 0) {
        *brave = false;
        return true;
    }
    if (n >= CM_TABLE + 262U && xx_rt_memcmp(head + CM_TABLE + 257U, "ustar", 5U) == 0) {
        *brave = true;
        return true;
    }
    return false;
}

static bool cm_candidate(Abstractformat *f, pm_stream *s, int64_t at, uint32_t bytes, xx_pd_struct *pd)
{
    uint8_t props[5], head[CM_PROBE];
    int64_t unpacked;
    size_t n = 0;
    bool brave;
    if (carrier_stop(pd)) return false;
    if (!cm_lzma_header(f, at, bytes, props, &unpacked)) return true; /* some other LZMA resource */
    if (!xx_lzma_unpack_device_to_memory(f->device, f->base_address + at + CM_LZMA_HEADER, (int64_t)bytes - CM_LZMA_HEADER, props, 5U, CM_PROBE, head, sizeof(head), &n, pd) ||
        n != CM_PROBE || !cm_payload_kind(head, n, &brave))
        return !carrier_stop(pd);
    if (s->count) return true; /* one payload per installer */
    return executable_carrier_component(f, s, at, bytes, brave ? "brave.lzma" : "chrome.lzma");
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return xx_sfx_pe_resource_archives(f, s, cm_lzma_signature, sizeof(cm_lzma_signature), cm_candidate, pd) && s->count > 0 && carrier_members(s, pd);
}

static cm_bundle *cm_nested(Abstractformat *f, xx_pd_struct *pd)
{
    xx_chromium_mini_installer *outer = (xx_chromium_mini_installer *)f;
    if (!f || carrier_stop(pd)) return NULL;
    if (!outer->nested && !xx_chromium_mini_installer_handle_base_info(f, pd)) return NULL;
    return (cm_bundle *)outer->nested;
}
static int64_t cm_size(Abstractformat *f, xx_pd_struct *pd)
{
    return cm_nested(f, pd) ? f->format_size : -1;
}
static uint64_t cm_count(Abstractformat *f, xx_pd_struct *pd)
{
    return cm_nested(f, pd) ? f->number_of_archive_records : 0;
}
static bool cm_options(xx_list_s *dst, const xx_list_s *src)
{
    size_t i;
    xx_list_clear(dst);
    for (i = 0; src && i < src->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at(src, i);
        xx_meta copy;
        if (!m) continue;
        xx_meta_init(&copy, m->meta_id);
        if (!xx_var_copy(&copy.var, &m->var) || !xx_list_append(dst, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}
static void cm_cursor_free(void *p)
{
    cm_cursor *c = (cm_cursor *)p;
    if (c) {
        if (c->nested) xx_format_free_archive_records_reading(c->bundle->nested, c->nested);
        xx_mem_free(c);
    }
}
static xx_archive_record_state *cm_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    cm_bundle *b = cm_nested(f, pd);
    xx_archive_record_state *s;
    cm_cursor *c;
    if (!b || !b->nested) return NULL;
    s = (xx_archive_record_state *)xx_mem_alloc(sizeof(*s));
    if (!s) return NULL;
    xx_archive_record_state_init(s, f);
    c = (cm_cursor *)xx_mem_calloc(1, sizeof(*c));
    if (!c) {
        xx_archive_record_state_free(s);
        return NULL;
    }
    s->internal_state = c;
    s->free_internal = cm_cursor_free;
    c->bundle = b;
    if (!cm_options(&s->options, opts)) {
        xx_archive_record_state_free(s);
        return NULL;
    }
    c->nested = xx_format_create_archive_records_reading(b->nested, &s->options, pd);
    if (!c->nested || !c->nested->has_record) {
        xx_archive_record_state_free(s);
        return NULL;
    }
    s->has_record = true;
    s->current_index = 0;
    s->total_records = (int64_t)f->number_of_archive_records;
    return s;
}
static const xx_archive_record *cm_current(Abstractformat *f, xx_archive_record_state *s)
{
    cm_cursor *c;
    if (!f || !s || s->format != f || !s->has_record) return NULL;
    c = (cm_cursor *)s->internal_state;
    return c && c->nested ? xx_format_get_current_archive_record(c->bundle->nested, c->nested) : NULL;
}
static bool cm_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    cm_cursor *c;
    if (!f || !s || s->format != f || !s->has_record || carrier_stop(pd)) return false;
    c = (cm_cursor *)s->internal_state;
    if (!c || !c->nested) return false;
    if (xx_format_archive_record_move_to_next(c->bundle->nested, c->nested, pd)) {
        ++s->current_index;
        return true;
    }
    s->has_record = false;
    return false;
}
static bool cm_unpack(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    cm_cursor *c;
    if (!f || !s || s->format != f || !s->has_record || carrier_stop(pd)) return false;
    c = (cm_cursor *)s->internal_state;
    if (!c || !c->nested) return false;
    if (!cm_options(&c->nested->options, &s->options)) return false;
    return xx_format_unpack_current_archive_record(c->bundle->nested, c->nested, pd);
}
static void cm_free_records(Abstractformat *f, xx_archive_record_state *s)
{
    (void)f;
    xx_archive_record_state_free(s);
}

void xx_chromium_mini_installer_init(xx_chromium_mini_installer *r, xx_io_device *d, int64_t b)
{
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    pm_init(&r->format, d, b, XX_FILE_TYPE_CHROMIUM_MINI_INSTALLER, "exe");
    r->format.check_is_valid = xx_chromium_mini_installer_check_is_valid;
    r->format.handle_base_info = xx_chromium_mini_installer_handle_base_info;
    r->format.get_format_size = cm_size;
    r->format.get_number_of_archive_records = cm_count;
    r->format.create_archive_records_reading = cm_records;
    r->format.get_current_archive_record = cm_current;
    r->format.archive_record_move_to_next = cm_next;
    r->format.unpack_current_archive_record = cm_unpack;
    r->format.free_archive_records_reading = cm_free_records;
}
xx_chromium_mini_installer *xx_chromium_mini_installer_create(xx_io_device *d, int64_t b)
{
    xx_chromium_mini_installer *r = (xx_chromium_mini_installer *)xx_mem_alloc(sizeof(*r));
    if (r) xx_chromium_mini_installer_init(r, d, b);
    return r;
}
void xx_chromium_mini_installer_destroy(xx_chromium_mini_installer *r)
{
    if (!r) return;
    if (r->nested) {
        cm_bundle_free((cm_bundle *)r->nested);
        r->nested = NULL;
    }
    xx_format_cleanup_extra_parameters(&r->format);
}
void xx_chromium_mini_installer_free(xx_chromium_mini_installer *r)
{
    if (r) {
        xx_chromium_mini_installer_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_chromium_mini_installer_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
/* ---- BCJ2 streaming decoder for Brave payloads ---- */

#define CM_BCJ2_MAIN_BUF  65536U
#define CM_BCJ2_AUX_BUF   16384U
#define CM_BCJ2_OUT_BUF   65536U

typedef struct cm_bcj2_stream {
    xx_io_device *device;
    int64_t offset;     /* next absolute read offset in device */
    int64_t end;        /* absolute end offset (exclusive) */
    uint8_t *buf;
    size_t capacity;
    size_t fill;        /* valid bytes in buf */
    size_t pos;         /* current read position in buf */
} cm_bcj2_stream;

static void cm_bcj2_stream_init(cm_bcj2_stream *s, xx_io_device *dev,
                                int64_t start, int64_t size,
                                uint8_t *buf, size_t capacity)
{
    s->device   = dev;
    s->offset   = start;
    s->end      = start + size;
    s->buf      = buf;
    s->capacity = capacity;
    s->fill     = 0;
    s->pos      = 0;
}

/* Return the next byte, refilling through xx_io_read_at when the local
 * buffer is exhausted.  Returns -1 on stream exhaustion or I/O error. */
static int cm_bcj2_next(cm_bcj2_stream *s)
{
    if (s->pos >= s->fill) {
        int64_t remaining = s->end - s->offset;
        size_t to_read;
        if (remaining <= 0) return -1;
        to_read = (int64_t)s->capacity <= remaining
                      ? s->capacity : (size_t)remaining;
        if (!xx_io_read_at(s->device, s->offset, s->buf, to_read))
            return -1;
        s->offset += (int64_t)to_read;
        s->fill = to_read;
        s->pos = 0;
    }
    return s->buf[s->pos++];
}

static bool cm_bcj2_exhausted(const cm_bcj2_stream *s)
{
    return s->pos >= s->fill && s->offset >= s->end;
}

static int64_t cm_bcj2_remaining(const cm_bcj2_stream *s)
{
    return (s->end - s->offset) + (int64_t)(s->fill - s->pos);
}

/* Streaming BCJ2 decode: read four streams from store (starting at offset 20,
 * back to back in the order main/call/jump/rc) and write the reconstructed
 * output to out.  The algorithm is identical to fead_bcj2_decode but operates
 * on devices through small per-stream buffers instead of flat memory. */
static bool cm_bcj2_decode(xx_io_device *store,
                           const uint32_t sizes[4],
                           int64_t expected,
                           xx_io_device *out,
                           xx_pd_struct *pd)
{
    cm_bcj2_stream main_s, call_s, jump_s, rc_s;
    uint8_t *main_buf = NULL, *call_buf = NULL;
    uint8_t *jump_buf = NULL, *rc_buf = NULL, *out_buf = NULL;
    size_t out_pos = 0;
    int64_t produced = 0, main_consumed = 0, checkpoint = 0, base;
    uint32_t range = UINT32_MAX, code = 0, ip = 0;
    uint16_t probabilities[258];
    uint8_t previous = 0;
    int b, i;
    bool ok = false;

    if ((sizes[1] & 3U) || (sizes[2] & 3U) || sizes[3] < 5 ||
        carrier_stop(pd))
        return false;

    /* Heap buffers (five allocations, ~176 KiB total) */
    main_buf = (uint8_t *)xx_mem_calloc(1, CM_BCJ2_MAIN_BUF);
    call_buf = (uint8_t *)xx_mem_calloc(1, CM_BCJ2_AUX_BUF);
    jump_buf = (uint8_t *)xx_mem_calloc(1, CM_BCJ2_AUX_BUF);
    rc_buf   = (uint8_t *)xx_mem_calloc(1, CM_BCJ2_AUX_BUF);
    out_buf  = (uint8_t *)xx_mem_calloc(1, CM_BCJ2_OUT_BUF);
    if (!main_buf || !call_buf || !jump_buf || !rc_buf || !out_buf)
        goto done;

    /* Streams start at offset 20, back to back: main, call, jump, rc */
    base = (int64_t)CM_TABLE;
    cm_bcj2_stream_init(&main_s, store, base, (int64_t)sizes[0],
                        main_buf, CM_BCJ2_MAIN_BUF);
    base += (int64_t)sizes[0];
    cm_bcj2_stream_init(&call_s, store, base, (int64_t)sizes[1],
                        call_buf, CM_BCJ2_AUX_BUF);
    base += (int64_t)sizes[1];
    cm_bcj2_stream_init(&jump_s, store, base, (int64_t)sizes[2],
                        jump_buf, CM_BCJ2_AUX_BUF);
    base += (int64_t)sizes[2];
    cm_bcj2_stream_init(&rc_s,  store, base, (int64_t)sizes[3],
                        rc_buf, CM_BCJ2_AUX_BUF);

    /* Initialise the range coder: one zero byte then four code bytes */
    for (i = 0; i < 258; ++i) probabilities[i] = 1024;
    b = cm_bcj2_next(&rc_s);
    if (b != 0) goto done;
    for (i = 0; i < 4; ++i) {
        b = cm_bcj2_next(&rc_s);
        if (b < 0) goto done;
        code = (code << 8) | (uint32_t)b;
    }
    if (code == UINT32_MAX) goto done;

    /* Decode */
    while (!cm_bcj2_exhausted(&main_s)) {
        uint8_t opcode;
        bool branch;

        /* Cancellation check every 64 KiB of main input */
        if (main_consumed >= checkpoint) {
            if (carrier_stop(pd)) goto done;
            checkpoint = main_consumed > INT64_MAX - 65536
                             ? INT64_MAX : main_consumed + 65536;
        }

        b = cm_bcj2_next(&main_s);
        if (b < 0) goto done;
        opcode = (uint8_t)b;
        ++main_consumed;

        branch = opcode == 0xE8U || opcode == 0xE9U ||
                 (previous == 0x0FU && (opcode & 0xF0U) == 0x80U);

        if (produced >= expected) goto done;

        /* Emit the opcode byte */
        out_buf[out_pos++] = opcode;
        ++produced;
        ++ip;
        if (out_pos >= CM_BCJ2_OUT_BUF) {
            if (xx_io_write(out, out_buf, out_pos) != (ssize_t)out_pos)
                goto done;
            out_pos = 0;
        }

        if (branch) {
            uint16_t *prob;
            uint32_t bound;

            if (opcode == 0xE8U)
                prob = &probabilities[2U + previous];
            else if (opcode == 0xE9U)
                prob = &probabilities[1U];
            else
                prob = &probabilities[0U];

            /* Range normalisation */
            if (range < (UINT32_C(1) << 24)) {
                b = cm_bcj2_next(&rc_s);
                if (b < 0) goto done;
                range <<= 8;
                code = (code << 8) | (uint32_t)b;
            }

            bound = (range >> 11) * (uint32_t)(*prob);

            if (code < bound) {
                range = bound;
                *prob = (uint16_t)(*prob + ((2048U - *prob) >> 5));
                previous = opcode;
            } else {
                cm_bcj2_stream *target;
                uint32_t value = 0;
                int j;

                range -= bound;
                code  -= bound;
                *prob = (uint16_t)(*prob - (*prob >> 5));

                target = (opcode == 0xE8U) ? &call_s : &jump_s;

                if (cm_bcj2_remaining(target) < 4 ||
                    expected - produced < 4)
                    goto done;

                /* Four bytes, stored big-endian in the target stream */
                for (j = 0; j < 4; ++j) {
                    b = cm_bcj2_next(target);
                    if (b < 0) goto done;
                    value = (value << 8) | (uint32_t)b;
                }
                ip += 4;
                value -= ip;

                /* Emit four bytes little-endian */
                if (out_pos + 4 > CM_BCJ2_OUT_BUF) {
                    if (out_pos > 0 &&
                        xx_io_write(out, out_buf, out_pos) !=
                            (ssize_t)out_pos)
                        goto done;
                    out_pos = 0;
                }
                out_buf[out_pos++] = (uint8_t)value;
                out_buf[out_pos++] = (uint8_t)(value >> 8);
                out_buf[out_pos++] = (uint8_t)(value >> 16);
                out_buf[out_pos++] = (uint8_t)(value >> 24);
                produced += 4;
                previous = (uint8_t)(value >> 24);

                if (out_pos >= CM_BCJ2_OUT_BUF) {
                    if (xx_io_write(out, out_buf, out_pos) !=
                            (ssize_t)out_pos)
                        goto done;
                    out_pos = 0;
                }
                continue;
            }
        } else {
            previous = opcode;
        }
    }

    /* Flush remaining output */
    if (out_pos > 0) {
        if (xx_io_write(out, out_buf, out_pos) != (ssize_t)out_pos)
            goto done;
    }

    ok = produced == expected &&
         cm_bcj2_exhausted(&call_s) &&
         cm_bcj2_exhausted(&jump_s) &&
         code == 0 &&
         !carrier_stop(pd);

done:
    xx_mem_free(out_buf);
    xx_mem_free(rc_buf);
    xx_mem_free(jump_buf);
    xx_mem_free(call_buf);
    xx_mem_free(main_buf);
    return ok;
}

/* Decode the payload once and open the archive inside it. */
bool xx_chromium_mini_installer_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    xx_chromium_mini_installer *outer = (xx_chromium_mini_installer *)f;
    pm_stream *s;
    cm_bundle *bundle = NULL;
    uint8_t props[5], head[CM_PROBE];
    int64_t at, bytes, unpacked, decoded, end;
    bool brave = false, ok = false;
    if (!outer || carrier_stop(pd)) return false;
    if (outer->nested) return true;
    s = pm_open(f, pd);
    if (!s) return false;
    if (s->count != 1U || s->items[0].offset < f->base_address) goto done;
    at = s->items[0].offset - f->base_address;
    bytes = s->items[0].size;
    if (bytes <= (int64_t)CM_LZMA_HEADER || bytes > UINT32_MAX || !cm_lzma_header(f, at, (uint32_t)bytes, props, &unpacked)) goto done;
    bundle = (cm_bundle *)xx_mem_calloc(1, sizeof(*bundle));
    if (!bundle) goto done;
    bundle->store = xx_io_temp_open();
    if (!bundle->store || !xx_lzma_unpack_device(f->device, f->base_address + at + CM_LZMA_HEADER, bytes - CM_LZMA_HEADER, props, 5U, unpacked, bundle->store, pd)) goto done;
    decoded = xx_io_total_size(bundle->store);
    if (decoded != unpacked || !xx_io_read_at(bundle->store, 0, head, sizeof(head)) || !cm_payload_kind(head, sizeof(head), &brave)) goto done;
    if (brave) {
        uint32_t sizes[4];
        int64_t expected;
        xx_tar *tar;
        sizes[0] = xx_data_get_u32(head + 4, 4, 0, false);
        sizes[1] = xx_data_get_u32(head + 8, 4, 0, false);
        sizes[2] = xx_data_get_u32(head + 12, 4, 0, false);
        sizes[3] = xx_data_get_u32(head + 16, 4, 0, false);
        expected = (int64_t)xx_data_get_u32(head, 4, 0, false);
        /* Invariants: tar == main+call+jump; header+all streams == decoded;
         * call and jump are multiples of 4; rc has at least 5 bytes. */
        if (expected <= 512 || expected > CM_MAX_UNPACKED ||
            expected != (int64_t)sizes[0] + sizes[1] + sizes[2] ||
            (sizes[1] & 3U) || (sizes[2] & 3U) || sizes[3] < 5 ||
            (int64_t)CM_TABLE + sizes[0] + sizes[1] + sizes[2] +
                sizes[3] != decoded)
            goto done;
        bundle->view = xx_io_temp_open();
        if (!bundle->view ||
            !cm_bcj2_decode(bundle->store, sizes, expected,
                            bundle->view, pd))
            goto done;
        /* The decoded tar is now the only source; the .lzma payload can go. */
        xx_io_close(bundle->store);
        bundle->store = NULL;
        tar = xx_tar_create(bundle->view, 0);
        if (!tar) goto done;
        bundle->tar = tar;
        bundle->nested = xx_tar_to_format(tar);
    } else {
        xx_7zip *archive = xx_7zip_create(bundle->store, 0);
        if (!archive) goto done;
        bundle->archive = archive;
        bundle->nested = xx_7zip_to_format(archive);
    }
    if (!bundle->nested->check_is_valid || !bundle->nested->check_is_valid(bundle->nested, pd) || carrier_stop(pd) ||
        !bundle->nested->handle_base_info || !bundle->nested->handle_base_info(bundle->nested, pd) || !bundle->nested->number_of_archive_records)
        goto done;
    f->format_size = s->size;
    f->number_of_archive_records = bundle->nested->number_of_archive_records;
    end = f->base_address + s->size;
    f->overlay_size = xx_io_size(f->device) - end;
    f->overlay_offset = f->overlay_size > 0 ? end : -1;
    f->is_valid = true;
    f->base_info_handled = true;
    outer->nested = bundle;
    bundle = NULL;
    ok = true;
done:
    cm_bundle_free(bundle);
    pm_free_stream(s);
    return ok;
}
