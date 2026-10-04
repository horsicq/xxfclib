/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * INFTool MRI/CAB layout independently recovered from the installer stub.
 * At PE EOF: fallback Cabinet DLL, MRI header, UI strings, masked CAB.
 * Read callback 0x404080 masks CAB bytes by declared EXE size modulo 13.
 * UI strings use Delphi Random(seed=555); they are not archive members.
 * Cabinet framing/checksum reference: Microsoft [MS-CAB], sections 2/3.1.
 * Legacy MRI uses a 22-byte header and ZIP local records with RSFX EOF.
 * Only self-contained MRI type 1 packages are supported; no code is run.
 */
#include "xxfclib/formats/sfx_inftool/xx_sfx_inftool.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <limits.h>
#include <stdio.h>

#define INFTOOL_MAX_CABINET (256U * 1024U * 1024U)
#define INFTOOL_MAX_PE (16U * 1024U * 1024U)

typedef struct inf_folder_s {
    uint32_t at;
    uint16_t blocks, method;
    uint64_t raw;
    size_t end;
} inf_folder;
typedef struct inf_file_s { uint32_t at, size; uint16_t folder; } inf_file;

static uint16_t inf_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t inf_u32(const uint8_t *p) {
    return (uint32_t)inf_u16(p) | ((uint32_t)inf_u16(p + 2) << 16);
}
static bool inf_stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool inf_range(uint64_t total, uint64_t at, uint64_t size) {
    return at <= total && size <= total - at;
}
static bool inf_read(Abstractformat *f, int64_t at, void *data, size_t size) {
    size_t done = 0;
    if (!f || !f->device || at < 0 || f->base_address < 0 ||
        at > LONG_MAX - f->base_address ||
        xx_io_seek(f->device, (long)(f->base_address + at), SEEK_SET)) return false;
    while (done < size) {
        ssize_t got = xx_io_read(f->device, (uint8_t *)data + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* Use section bounds to locate both PE ends, rather than searching for MRI
 * or trusting an MZ marker inside an unrelated overlay. */
static bool inf_pe_extent(Abstractformat *f, uint64_t total, uint64_t at,
                          bool dll, uint64_t *extent, xx_pd_struct *pd) {
    uint8_t dos[64], coff[24], optional[64], section[40];
    uint32_t pe, headers;
    uint16_t sections, opt_size;
    uint64_t table, end;
    unsigned i, j;
    if (!inf_range(total, at, 64) || !inf_read(f, (int64_t)at, dos, 64) ||
        dos[0] != 'M' || dos[1] != 'Z' ||
        (pe = inf_u32(dos + 60)) < 64 || pe > 1048576 ||
        !inf_range(total - at, pe, 24) ||
        !inf_read(f, (int64_t)(at + pe), coff, 24) ||
        xx_rt_memcmp(coff, "PE\0\0", 4) || inf_u16(coff + 4) != 0x14c ||
        !(sections = inf_u16(coff + 6)) || sections > 96 ||
        (dll && !(inf_u16(coff + 22) & 0x2000U)) ||
        (opt_size = inf_u16(coff + 20)) < 96 || opt_size > 4096 ||
        !inf_range(total - at, (uint64_t)pe + 24, opt_size) ||
        !inf_read(f, (int64_t)(at + pe + 24), optional, 64) ||
        inf_u16(optional) != 0x10b) return false;
    table = (uint64_t)pe + 24 + opt_size;
    headers = inf_u32(optional + 60);
    if (headers < table + (uint64_t)sections * 40 ||
        headers > INFTOOL_MAX_PE || !inf_range(total - at, 0, headers) ||
        !inf_range(total - at, table, (uint64_t)sections * 40)) return false;
    end = headers;
    for (i = 0; i < sections; ++i) {
        uint32_t size, offset;
        if (inf_stop(pd) || !inf_read(f, (int64_t)(at + table + i * 40), section, 40)) return false;
        size = inf_u32(section + 16); offset = inf_u32(section + 20);
        if (!size) continue;
        if (offset < headers || !inf_range(total - at, offset, size) ||
            (uint64_t)offset + size > INFTOOL_MAX_PE) return false;
        for (j = 0; j < i; ++j) {
            uint8_t previous[8]; uint32_t ps, po;
            if (!inf_read(f, (int64_t)(at + table + j * 40 + 16), previous, 8)) return false;
            ps = inf_u32(previous); po = inf_u32(previous + 4);
            if (ps && offset < (uint64_t)po + ps && po < (uint64_t)offset + size) return false;
        }
        if ((uint64_t)offset + size > end) end = (uint64_t)offset + size;
    }
    *extent = end;
    return true;
}

static uint32_t inf_checksum(const uint8_t *p, size_t size, uint32_t sum) {
    uint32_t tail = 0;
    while (size >= 4) { sum ^= inf_u32(p); p += 4; size -= 4; }
    while (size--) tail = (tail << 8) | *p++;
    return sum ^ tail;
}
static bool inf_name(const uint8_t *p, size_t size) {
    size_t start = 0, i;
    if (!size || p[0] == '/' || p[0] == '\\') return false;
    for (i = 0; i <= size; ++i) {
        uint8_t ch = i < size ? p[i] : 0;
        if (i < size && (!ch || ch < 32 || ch == ':' || ch == '<' || ch == '>' ||
                        ch == '"' || ch == '|' || ch == '?' || ch == '*')) return false;
        if (!ch || ch == '/' || ch == '\\') {
            size_t n = i - start;
            if (!n || (n == 1 && p[start] == '.') ||
                (n == 2 && p[start] == '.' && p[start + 1] == '.')) return false;
            start = i + 1;
        }
    }
    return true;
}
static bool inf_cstring(const uint8_t *cab, size_t size, size_t *at,
                        size_t *length, bool safe) {
    size_t start = *at;
    while (*at < size && cab[*at] && *at - start <= 4096) ++*at;
    if (*at >= size || *at - start > 4096) return false;
    *length = *at - start;
    if (safe && !inf_name(cab + start, *length)) return false;
    ++*at;
    return true;
}
static bool inf_equal_name(const uint8_t *a, size_t na, const uint8_t *b, size_t nb) {
    size_t i;
    if (na != nb) return false;
    for (i = 0; i < na; ++i) {
        uint8_t ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return false;
    }
    return true;
}

/* Enforce the complete local CAB graph, including every CFDATA checksum.
 * The historical FCI stub leaves NEXT set even in its final cabinet. Accept
 * the advisory names only when all folders, blocks and files are local;
 * continuation file indices and zero-uncompressed split blocks are rejected. */
static bool inf_cabinet(const uint8_t *cab, size_t size, const uint8_t *inf,
                        size_t inf_size, xx_pd_struct *pd) {
    inf_folder *folders = NULL;
    inf_file *files = NULL;
    unsigned nf, nn, flags, fr = 0, dr = 0, i, j;
    size_t at = 36, files_at, metadata_end, last = 0;
    uint64_t raw_total = 0;
    bool found_inf = false, ok = false;
    if (size < 36 || xx_rt_memcmp(cab, "MSCF", 4) ||
        inf_u32(cab + 4) || inf_u32(cab + 12) || inf_u32(cab + 20) ||
        inf_u32(cab + 8) != size || cab[24] != 3 || cab[25] != 1 ||
        !(nf = inf_u16(cab + 26)) || nf > 4096 ||
        !(nn = inf_u16(cab + 28)) || nn > 65535 ||
        (flags = inf_u16(cab + 30)) & ~7U ||
        (files_at = inf_u32(cab + 16)) >= size) return false;
    if (flags & 4) {
        unsigned reserve;
        if (!inf_range(size, at, 4)) return false;
        reserve = inf_u16(cab + at); fr = cab[at + 2]; dr = cab[at + 3]; at += 4;
        if (!inf_range(size, at, reserve)) return false;
        at += reserve;
    }
    for (i = 0; i < 2; ++i) if (flags & (1U << i)) {
        size_t length;
        if (!inf_cstring(cab, size, &at, &length, false) ||
            !inf_cstring(cab, size, &at, &length, false)) return false;
    }
    folders = (inf_folder *)xx_mem_calloc(nf, sizeof(*folders));
    files = (inf_file *)xx_mem_calloc(nn, sizeof(*files));
    if (!folders || !files) goto done;
    for (i = 0; i < nf; ++i) {
        unsigned method, bits;
        if (inf_stop(pd) || !inf_range(size, at, 8U + fr)) goto done;
        folders[i].at = inf_u32(cab + at);
        folders[i].blocks = inf_u16(cab + at + 4);
        folders[i].method = inf_u16(cab + at + 6);
        method = folders[i].method & 15; bits = folders[i].method >> 8;
        if (!folders[i].blocks || method > 3 ||
            ((method < 2) && folders[i].method > 1) ||
            (method == 3 && (bits < 15 || bits > 21 || (folders[i].method & 0xf0))) ||
            (method == 2 && (bits < 10 || bits > 21 ||
                              ((folders[i].method >> 4) & 15) < 1 ||
                              ((folders[i].method >> 4) & 15) > 7))) goto done;
        at += 8U + fr;
    }
    if (files_at < at) goto done;
    at = files_at;
    for (i = 0; i < nn; ++i) {
        size_t start, length;
        if (inf_stop(pd) || !inf_range(size, at, 16)) goto done;
        files[i].size = inf_u32(cab + at);
        files[i].at = inf_u32(cab + at + 4);
        files[i].folder = inf_u16(cab + at + 8);
        if (files[i].folder >= nf) goto done;
        at += 16; start = at;
        if (!inf_cstring(cab, size, &at, &length, true)) goto done;
        if (inf_equal_name(cab + start, length, inf, inf_size)) found_inf = true;
    }
    if (!found_inf) goto done;
    metadata_end = at;
    for (i = 0; i < nf; ++i) {
        size_t p = folders[i].at;
        if (p < metadata_end) goto done;
        for (j = 0; j < folders[i].blocks; ++j) {
            uint32_t checksum; unsigned packed, plain;
            if (inf_stop(pd) || !inf_range(size, p, 8U + dr)) goto done;
            checksum = inf_u32(cab + p); packed = inf_u16(cab + p + 4);
            plain = inf_u16(cab + p + 6);
            if (!packed || !plain || plain > 32768 ||
                !inf_range(size, p + 8U + dr, packed) ||
                ((folders[i].method & 15) == 0 && packed != plain) ||
                (folders[i].raw += plain) > INFTOOL_MAX_CABINET) goto done;
            if (checksum && checksum != inf_checksum(cab + p + 4, 4U + dr,
                                   inf_checksum(cab + p + 8U + dr, packed, 0))) goto done;
            p += 8U + dr + packed;
        }
        folders[i].end = p;
        raw_total += folders[i].raw;
        if (raw_total > INFTOOL_MAX_CABINET) goto done;
        if (p > last) last = p;
        for (j = 0; j < i; ++j)
            if (folders[i].at < folders[j].end && folders[j].at < p) goto done;
    }
    if (last != size) goto done;
    for (i = 0; i < nn; ++i)
        if ((uint64_t)files[i].at + files[i].size > folders[files[i].folder].raw) goto done;
    ok = !inf_stop(pd);
done:
    xx_mem_free(folders); xx_mem_free(files);
    return ok;
}

#include "xx_inftool_zip.h"

static void inf_release(xx_sfx_inftool *a) {
    if (a->zip_ready) xx_zip_destroy(&a->legacy_zip);
    a->zip_ready = false;
    if (a->inner_ready) xx_cab_destroy(&a->inner);
    a->inner_ready = false;
    if (a->cabinet_device) xx_io_close(a->cabinet_device);
    a->cabinet_device = NULL;
    xx_mem_free(a->cabinet); a->cabinet = NULL;
}

static bool inf_open(xx_sfx_inftool *a, xx_pd_struct *pd) {
    Abstractformat *f;
    int64_t physical;
    uint64_t total, outer, dll, header_at, payload_at, payload_size;
    uint8_t h[24], inf[256], marker[3];
    uint32_t declared;
    size_t image_size, header_size, inf_size, i;
    bool legacy, encrypted = false;
    if (!a) return false;
    if (a->inner_ready || a->zip_ready) {
        if (inf_stop(pd)) return false;
        /* Format-wide parameter setters invalidate the public base flags.
         * Cached package framing remains valid regardless of the password
         * used later to decode an encrypted member. */
        a->format.is_valid = true;
        a->format.base_info_handled = true;
        return true;
    }
    f = &a->format;
    if (!f->device || f->base_address < 0 ||
        (physical = xx_io_total_size(f->device)) < f->base_address) return false;
    total = (uint64_t)(physical - f->base_address);
    if (total > INT32_MAX || !inf_pe_extent(f, total, 0, false, &outer, pd) ||
        !inf_range(total, outer, 3) || !inf_read(f, (int64_t)outer, marker, 3)) return false;
    legacy = xx_rt_memcmp(marker, "MRI", 3) == 0;
    if (legacy) {
        header_at = outer; header_size = 22;
    } else {
        if (!inf_pe_extent(f, total, outer, true, &dll, pd)) return false;
        header_at = outer + dll; header_size = 24;
    }
    xx_mem_zero(h, sizeof(h));
    if (!inf_range(total, header_at, header_size) ||
        !inf_read(f, (int64_t)header_at, h, header_size) ||
        xx_rt_memcmp(h, "MRI", 3) || h[3] != 1 || !h[4] || !h[5] ||
        h[6] > 1 || h[16] > 1 || h[17] != 0 ||
        (!legacy && (h[18] > 1 || h[19] > 1)) ||
        (declared = inf_u32(h + (legacy ? 18 : 20))) != total) return false;
    payload_at = header_at + header_size + h[4] + h[5] + h[7] +
                 (uint64_t)inf_u32(h + 8) + inf_u32(h + 12);
    if (!inf_range(total, payload_at, 36) ||
        !inf_read(f, (int64_t)(header_at + header_size + h[4]), inf, h[5])) return false;
    inf_size = h[5];
    /* Legacy launch-template parser 0x405f60 replaces "><" with its
     * temporary extraction directory. Resolve only that leading placeholder;
     * the archive member must still have an ordinary safe relative name. */
    if (legacy && inf_size > 2 && inf[0] == '>' && inf[1] == '<') {
        inf_size -= 2;
        for (i = 0; i < inf_size; ++i) inf[i] = inf[i + 2];
    }
    if (!inf_name(inf, inf_size)) return false;
    payload_size = total - payload_at;
    if (payload_size > INFTOOL_MAX_CABINET || inf_stop(pd)) return false;
    image_size = (size_t)payload_size;
    a->cabinet = (uint8_t *)xx_mem_alloc(image_size);
    if (!a->cabinet || !inf_read(f, (int64_t)payload_at, a->cabinet, image_size)) goto fail;
    if (legacy) {
        if (!inf_legacy_zip(&a->cabinet, &image_size, inf, inf_size, &encrypted, pd)) goto fail;
    } else {
        if (h[19]) {
            uint8_t mask = (uint8_t)(declared % 13U);
            for (i = 0; i < image_size; ++i) {
                if ((i & 65535U) == 0 && inf_stop(pd)) goto fail;
                a->cabinet[i] ^= mask;
            }
        }
        if (!inf_cabinet(a->cabinet, image_size, inf, h[5], pd)) goto fail;
    }
    a->cabinet_device = xx_io_mem_open_ro(a->cabinet, image_size);
    if (!a->cabinet_device) goto fail;
    if (legacy) {
        xx_zip_init(&a->legacy_zip, a->cabinet_device, 0);
        a->zip_ready = true;
        if (!xx_zip_handle_base_info(&a->legacy_zip.format, pd)) goto fail;
        f->number_of_archive_records = a->legacy_zip.number_of_records;
    } else {
        xx_cab_init(&a->inner, a->cabinet_device, 0);
        a->inner_ready = true;
        if (!xx_cab_handle_base_info(&a->inner.format, pd)) goto fail;
        f->number_of_archive_records = a->inner.number_of_records;
    }
    f->format_size = (int64_t)total; f->is_crypted = encrypted;
    f->is_valid = true; f->base_info_handled = true;
    return true;
fail:
    inf_release(a);
    return false;
}

static int64_t inf_size(Abstractformat *f, xx_pd_struct *pd) {
    return inf_open((xx_sfx_inftool *)f, pd) ? f->format_size : -1;
}
static uint64_t inf_count(Abstractformat *f, xx_pd_struct *pd) {
    return inf_open((xx_sfx_inftool *)f, pd) ? f->number_of_archive_records : 0;
}
static xx_archive_record_state *inf_records(Abstractformat *f, const xx_list_s *options,
                                           xx_pd_struct *pd) {
    xx_sfx_inftool *a = (xx_sfx_inftool *)f;
    size_t i;
    if (!inf_open(a, pd)) return NULL;
    if (a->zip_ready) {
        /* In addition to operation options, preserve format-level passwords
         * and memory limits for callers using xx_format_set_password().
         * Replace the child parameter set so a cleared outer password/limit
         * cannot survive in this cached reader across reading sessions. */
        xx_format_cleanup_extra_parameters(&a->legacy_zip.format);
        for (i = 0; i < f->list_extra_parameters.count; ++i) {
            const xx_meta *m = (const xx_meta *)xx_list_at(
                (const xx_list_t *)&f->list_extra_parameters, i);
            if (m && !xx_format_set_extra_parameter(&a->legacy_zip.format,
                                                     m->meta_id, &m->var)) return NULL;
        }
        return xx_zip_create_archive_records_reading(&a->legacy_zip.format, options, pd);
    }
    return xx_cab_create_archive_records_reading(&a->inner.format, options, pd);
}
static const xx_archive_record *inf_current(Abstractformat *f, xx_archive_record_state *s) {
    xx_sfx_inftool *a = (xx_sfx_inftool *)f;
    if (!a) return NULL;
    if (a->zip_ready) return xx_zip_get_current_archive_record(&a->legacy_zip.format, s);
    return a->inner_ready ? xx_cab_get_current_archive_record(&a->inner.format, s) : NULL;
}
static bool inf_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd) {
    xx_sfx_inftool *a = (xx_sfx_inftool *)f;
    if (!a || inf_stop(pd)) return false;
    if (a->zip_ready) return xx_zip_archive_record_move_to_next(&a->legacy_zip.format, s, pd);
    return a->inner_ready && xx_cab_archive_record_move_to_next(&a->inner.format, s, pd);
}
static bool inf_unpack(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd) {
    xx_sfx_inftool *a = (xx_sfx_inftool *)f;
    if (!a) return false;
    if (a->zip_ready) return xx_zip_unpack_current_archive_record(&a->legacy_zip.format, s, pd);
    return a->inner_ready && xx_cab_unpack_current_archive_record(&a->inner.format, s, pd);
}
static void inf_free_records(Abstractformat *f, xx_archive_record_state *s) {
    xx_sfx_inftool *a = (xx_sfx_inftool *)f;
    if (a && a->zip_ready) xx_zip_free_archive_records_reading(&a->legacy_zip.format, s);
    else if (a && a->inner_ready) xx_cab_free_archive_records_reading(&a->inner.format, s);
    else xx_archive_record_state_free(s);
}

void xx_sfx_inftool_init(xx_sfx_inftool *a, xx_io_device *device, int64_t base) {
    if (!a) return;
    xx_mem_zero(a, sizeof(*a)); xx_format_init(&a->format, device, base);
    a->format.file_type = XX_FILE_TYPE_SFX_INFTOOL;
    a->format.format_type = XX_TYPE_ARCHIVE; a->format.is_archive = true;
    xx_format_set_extension(&a->format, "exe");
    a->format.check_is_valid = xx_sfx_inftool_check_is_valid;
    a->format.handle_base_info = xx_sfx_inftool_handle_base_info;
    a->format.get_format_size = inf_size;
    a->format.get_number_of_archive_records = inf_count;
    a->format.create_archive_records_reading = inf_records;
    a->format.get_current_archive_record = inf_current;
    a->format.archive_record_move_to_next = inf_next;
    a->format.unpack_current_archive_record = inf_unpack;
    a->format.free_archive_records_reading = inf_free_records;
}
xx_sfx_inftool *xx_sfx_inftool_create(xx_io_device *d, int64_t base) {
    xx_sfx_inftool *a = (xx_sfx_inftool *)xx_mem_alloc(sizeof(*a));
    if (a) xx_sfx_inftool_init(a, d, base);
    return a;
}
void xx_sfx_inftool_destroy(xx_sfx_inftool *a) {
    if (!a) return;
    inf_release(a); xx_format_cleanup_extra_parameters(&a->format);
}
void xx_sfx_inftool_free(xx_sfx_inftool *a) {
    if (a) { xx_sfx_inftool_destroy(a); xx_mem_free(a); }
}
bool xx_sfx_inftool_check_is_valid(Abstractformat *f, xx_pd_struct *pd) {
    return inf_open((xx_sfx_inftool *)f, pd);
}
bool xx_sfx_inftool_handle_base_info(Abstractformat *f, xx_pd_struct *pd) {
    return inf_open((xx_sfx_inftool *)f, pd);
}
