/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * SumatraPDF installers (SumatraPDF 3.x). The installer is the program with
 * its payload in an RCDATA resource (id 1): Sumatra's "LzSA" simple LZMA
 * archive (src/utils/LzmaSimpleArchive.cpp of SumatraPDF), measured on the
 * 3.2 x64 installer (libmupdf.dll, PdfFilter.dll, PdfPreview.dll):
 *
 *   u32 magic "LzSA"
 *   u32 number of files
 *   per file: u32 record size (this field included), u32 compressed size,
 *             u32 uncompressed size, u32 CRC-32 of the file, FILETIME,
 *             char name[] NUL-terminated
 *   u32 CRC-32 of everything above
 *   per file: u8 method (0xFF stored, 0 LZMA, 1 LZMA then the x86 BCJ
 *             filter), 5 LZMA property bytes, the LZMA stream
 *
 * Members are the files, decoded on demand (LZMA to a temporary device,
 * the BCJ filter while copying) and checked against their CRC-32. The
 * resource walk is the shared one; nothing is executed.
 */
#include "xxfclib/formats/installer/sumatra_lzsa/xx_sumatra_lzsa.h"
#include "../../common/xx_executable_carrier.h"
#include "../../common/xx_pe_resource_locator.h"
#include "../../common/xx_pe_resource_archives.h"
#include "../../archive/7zip/xx_7zip_branch.h"
#include "../../archive/7zip/xx_7zip_defs.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/io/xx_io.h"

#define LZ_MAX_FILES 256U
#define LZ_RECORD_FIXED 24U
#define LZ_MAX_NAME 260U
#define LZ_MAX_HEADER (8U + LZ_MAX_FILES * (LZ_RECORD_FIXED + LZ_MAX_NAME))
#define LZ_CHUNK 65536U
#define LZ_METHOD_STORED 0xFFU
#define LZ_METHOD_LZMA 0U
#define LZ_METHOD_LZMA_X86 1U
#define LZ_MAX_UNPACKED (INT64_C(1) * 1024 * 1024 * 1024)

static const uint8_t lz_magic[4] = {'L', 'z', 'S', 'A'};

typedef struct lz_member {
    uint8_t method;
    uint32_t crc;
    int64_t size;
} lz_member;

static bool lz_write_all(xx_io_device *device, const uint8_t *buffer, size_t count)
{
    size_t done = 0;
    while (done < count) {
        ssize_t w = xx_io_write(device, buffer + done, count - done);
        if (w <= 0 || (size_t)w > count - done) return false;
        done += (size_t)w;
    }
    return true;
}

/* `n` bytes from `at` of `src` into `dst`, through the x86 BCJ decoder when
 * `filter`, with the CRC-32 of what was written. */
static bool lz_copy(xx_io_device *src, int64_t at, int64_t n, xx_io_device *dst, bool filter, uint32_t *crc, xx_pd_struct *pd)
{
    xx_7zip_branch_state branch;
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(LZ_CHUNK + 16U);
    int64_t done = 0;
    size_t used = 0;
    bool ok = false;
    if (!buffer) return false;
    if (filter && !xx_7zip_branch_state_init(&branch, XX_7ZIP_METHOD_BCJ, NULL, 0)) goto out;
    *crc = 0;
    for (;;) {
        size_t want = n - done > (int64_t)LZ_CHUNK ? LZ_CHUNK : (size_t)(n - done), processed;
        bool final;
        if (pd && xx_pd_is_stopped(pd)) goto out;
        if (want && !xx_io_read_at(src, at + done, buffer + used, want)) goto out;
        done += (int64_t)want;
        used += want;
        final = done == n;
        processed = filter ? xx_7zip_branch_process(&branch, buffer, used, final) : used;
        if (processed == SIZE_MAX || processed > used) goto out;
        if (processed) {
            *crc = xx_crc32_calc(*crc, buffer, processed);
            if (!lz_write_all(dst, buffer, processed)) goto out;
        }
        used -= processed;
        if (used > 16U || (processed == 0U && !final)) goto out;
        if (used) xx_rt_memmove(buffer, buffer + processed, used);
        if (final) break;
    }
    ok = used == 0U;
out:
    xx_mem_free(buffer);
    return ok;
}

/* The decoded member into the destination (a temporary device validates). */
static bool lz_read_all(Abstractformat *f, pm_member *m, xx_io_device *device, xx_pd_struct *pd)
{
    lz_member *ctx = (lz_member *)m->context;
    xx_io_device *out = device ? device : xx_io_temp_open(), *decoded = NULL;
    uint32_t crc = 0;
    bool ok = false;
    if (!ctx || !out || m->packed_size <= 0) goto done;
    if (ctx->method == LZ_METHOD_STORED) {
        ok = m->packed_size == ctx->size && lz_copy(f->device, m->offset, m->packed_size, out, false, &crc, pd);
    } else {
        uint8_t props[5];
        decoded = xx_io_temp_open();
        if (!decoded || m->packed_size <= 6 || !xx_io_read_at(f->device, m->offset + 1, props, sizeof(props))) goto done;
        if (!xx_lzma_unpack_device(f->device, m->offset + 6, m->packed_size - 6, props, sizeof(props), ctx->size, decoded, pd) || xx_io_total_size(decoded) != ctx->size) goto done;
        ok = lz_copy(decoded, 0, ctx->size, out, ctx->method == LZ_METHOD_LZMA_X86, &crc, pd);
    }
    ok = ok && crc == ctx->crc;
done:
    if (decoded) xx_io_close(decoded);
    if (out && !device) xx_io_close(out);
    return ok && !(pd && xx_pd_is_stopped(pd));
}

/* A resource that starts with "LzSA": the archive, with its header checked
 * against its own CRC-32. */
static bool lz_candidate(Abstractformat *f, pm_stream *s, int64_t at, uint32_t bytes, xx_pd_struct *pd)
{
    uint8_t *header = NULL, method;
    uint32_t count, i, length = 8, stored;
    int64_t data;
    size_t first = s->count;
    bool ok = false;
    if (carrier_stop(pd)) return false;
    if (bytes < 8U + LZ_RECORD_FIXED + 2U + 4U + 7U) return true;
    header = (uint8_t *)xx_mem_alloc(LZ_MAX_HEADER);
    if (!header || !pm_read(f, at, header, 8)) goto done;
    count = xx_data_get_u32(header + 4, 4, 0, false);
    if (!count || count > LZ_MAX_FILES) goto done;
    for (i = 0; i < count; ++i) { /* the records, read whole for the CRC */
        uint32_t record, k;
        if (carrier_stop(pd) || length + 4U > bytes || !pm_read(f, at + length, header + length, 4)) goto done;
        record = xx_data_get_u32(header + length, 4, 0, false);
        if (record < LZ_RECORD_FIXED + 2U || record > LZ_RECORD_FIXED + LZ_MAX_NAME || record > bytes - length || length + record > LZ_MAX_HEADER ||
            !pm_read(f, at + length + 4, header + length + 4, record - 4U))
            goto done;
        for (k = LZ_RECORD_FIXED; k + 1U < record; ++k) { /* the name, NUL last */
            if (header[length + k] < 0x20U || header[length + k] == 0x7FU) goto done;
        }
        if (header[length + record - 1U] != 0) goto done;
        length += record;
    }
    if (length + 4U > bytes || !pm_read(f, at + length, header + length, 4)) goto done;
    stored = xx_data_get_u32(header + length, 4, 0, false);
    if (xx_crc32_calc(0, header, length) != stored) goto done;
    data = at + length + 4;
    length = 8;
    for (i = 0; i < count; ++i) { /* the members */
        uint32_t record = xx_data_get_u32(header + length, 4, 0, false), packed = xx_data_get_u32(header + length + 4, 4, 0, false),
                 unpacked = xx_data_get_u32(header + length + 8, 4, 0, false);
        lz_member *ctx;
        pm_member *m;
        if (carrier_stop(pd) || !packed || packed > (uint64_t)(at + bytes - data) || unpacked > (uint64_t)LZ_MAX_UNPACKED || !pm_read(f, data, &method, 1)) goto done;
        if (method == LZ_METHOD_STORED) {
            if (packed != unpacked) goto done;
        } else if (method == LZ_METHOD_LZMA || method == LZ_METHOD_LZMA_X86) {
            uint8_t props;
            if (packed < 7U || !pm_read(f, data + 1, &props, 1) || props >= 225U) goto done;
        } else {
            goto done;
        }
        if (!executable_carrier_component(f, s, data, (int64_t)packed, (const char *)(header + length + LZ_RECORD_FIXED))) goto done;
        m = &s->items[s->count - 1U];
        ctx = (lz_member *)xx_mem_calloc(1U, sizeof(*ctx));
        if (!ctx) goto done;
        ctx->method = method;
        ctx->crc = xx_data_get_u32(header + length + 12, 4, 0, false);
        ctx->size = (int64_t)unpacked;
        m->context = ctx;
        m->free_context = xx_mem_free;
        m->read_all = lz_read_all;
        m->size = (int64_t)unpacked;
        m->compression_method = method == LZ_METHOD_STORED ? 0U : 14U;
        data += packed;
        length += record;
    }
    ok = s->count > first;
done:
    xx_mem_free(header);
    return ok;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    /* The members' extents are the packed streams, validated in order by the
     * candidate; their sizes are the unpacked ones, so the extent check of
     * carrier_members (which uses the size) does not apply. */
    return xx_sfx_pe_resource_archives(f, s, lz_magic, sizeof(lz_magic), lz_candidate, pd) && s->count > 0 && !carrier_stop(pd);
}

void xx_sumatra_lzsa_init(xx_sumatra_lzsa *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SUMATRA_LZSA, "exe");
    }
}
xx_sumatra_lzsa *xx_sumatra_lzsa_create(xx_io_device *d, int64_t b)
{
    xx_sumatra_lzsa *r = (xx_sumatra_lzsa *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sumatra_lzsa_init(r, d, b);
    return r;
}
void xx_sumatra_lzsa_destroy(xx_sumatra_lzsa *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sumatra_lzsa_free(xx_sumatra_lzsa *r)
{
    if (r) {
        xx_sumatra_lzsa_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sumatra_lzsa_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sumatra_lzsa_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
