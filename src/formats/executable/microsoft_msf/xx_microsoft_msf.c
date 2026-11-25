/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded, lazy MSF stream extractor over the Microsoft PDB reader: the MSF
 * 7.00 ("DS") primary layout and the old "program database 2.00" ("JG")
 * layout, 512 through 4096-byte pages. The page structure is checked here
 * (every page inside the file, no page owned twice, owned pages not free in
 * the free page map, the directory consumed exactly); member bytes are read
 * on demand through xx_ms_pdb_read_stream, the file is never loaded.
 * Payloads are never executed.
 */
/* Primary layout: https://llvm.org/docs/PDB/MsfFile.html */
#include "xxfclib/formats/executable/microsoft_msf/xx_microsoft_msf.h"
#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb.h"
#include "../../common/xx_container_codec_helpers.h"

#define MSF_DS_HEADER 56U
#define MSF_JG_HEADER 60U      /* 44-byte magic, page size, FPM page, page count, root stream {size, reserved} */
#define MSF_MAX_STREAMS 65535U /* stream numbers are 16-bit in the PDB streams */

/* One parse owns one reader; every lazy member holds a reference. */
typedef struct msf_shared {
    xx_ms_pdb *pdb;
    xx_io_device *view;
    uint32_t page_size;
    uint32_t directory_size;
    uint32_t directory_page_count;
    uint32_t *directory_pages;
    unsigned refs;
} msf_shared;
typedef struct msf_member_context {
    msf_shared *shared;
    int32_t stream; /* -1: the stream directory */
} msf_member_context;

static void msf_shared_release(msf_shared *h)
{
    if (h && !--h->refs) {
        if (h->pdb) xx_ms_pdb_close(h->pdb);
        if (h->view) (void)xx_io_close(h->view);
        if (h->directory_pages) xx_mem_free(h->directory_pages);
        xx_mem_free(h);
    }
}
static void msf_member_free(void *p)
{
    msf_member_context *c = (msf_member_context *)p;
    if (c) {
        msf_shared_release(c->shared);
        xx_mem_free(c);
    }
}
static bool msf_member_read(Abstractformat *f, pm_member *m, uint64_t at, void *p, size_t n, xx_pd_struct *pd)
{
    msf_member_context *c = (msf_member_context *)m->context;
    msf_shared *h = c ? c->shared : NULL;
    if (!h || !p || at > (uint64_t)m->size || n > (uint64_t)m->size - at || binary_stop(pd)) return false;
    if (!n) return true;
    if (c->stream < 0) {
        /* The directory: its pages were checked by the parse. */
        uint8_t *out = (uint8_t *)p;
        while (n) {
            uint64_t page = at / h->page_size, inside = at % h->page_size, part = h->page_size - inside;
            if (page >= h->directory_page_count || binary_stop(pd)) return false;
            if (part > n) part = n;
            if (!pm_read(f, (int64_t)((uint64_t)h->directory_pages[(size_t)page] * h->page_size + inside), out, (size_t)part)) return false;
            out += (size_t)part;
            at += part;
            n -= (size_t)part;
        }
        return true;
    }
    {
        int64_t got;
        xx_ms_pdb_set_pd(h->pdb, pd);
        got = xx_ms_pdb_read_stream(h->pdb, c->stream, (int64_t)at, p, (int64_t)n);
        xx_ms_pdb_set_pd(h->pdb, NULL);
        return got == (int64_t)n && !binary_stop(pd);
    }
}
static bool msf_add_lazy(Abstractformat *f, pm_stream *s, const char *name, msf_shared *h, int32_t stream, uint64_t size)
{
    msf_member_context *c;
    pm_member *m;
    if (s->count >= 1000000U || !pm_add(f, s, name, 0, 0)) return false;
    m = &s->items[s->count - 1];
    c = (msf_member_context *)xx_mem_alloc(sizeof(*c));
    if (!c) {
        --s->count;
        return false;
    }
    c->shared = h;
    c->stream = stream;
    ++h->refs;
    m->context = c;
    m->free_context = msf_member_free;
    m->read_range = msf_member_read;
    m->size = (int64_t)size;
    m->packed_size = (int64_t)size;
    return true;
}
static bool msf_used(uint8_t *used, uint64_t page)
{
    bool was = (used[(size_t)(page >> 3)] >> (page & 7)) & 1U;
    used[(size_t)(page >> 3)] |= (uint8_t)(1U << (page & 7));
    return was;
}
static bool msf_is_used(const uint8_t *used, uint64_t page)
{
    return (used[(size_t)(page >> 3)] >> (page & 7)) & 1U;
}
static bool msf_directory_entry(const uint8_t *dir, uint64_t at, unsigned width, uint64_t *value)
{
    *value = width == 4 ? xx_data_get_u32(dir + (size_t)at, 4, 0, false) : xx_data_get_u16(dir + (size_t)at, 2, 0, false);
    return true;
}

/* Stream table of the loaded directory: DS {count, sizes[count], u32
 * pages...}; JG {count, {size, reserved}[count], u16 pages...} with a 32 or
 * 16-bit count. Owned pages are marked; stream 0 (the previous directory) is
 * neither marked nor listed. With s == NULL nothing is added (layout probe). */
static bool msf_streams(Abstractformat *f, pm_stream *s, msf_shared *h, const uint8_t *dir, uint8_t *used, uint64_t blocks, bool jg, unsigned count_width,
                        xx_pd_struct *pd)
{
    uint64_t j, k, count, at, size, need, id;
    uint64_t dn = h->directory_size, bs = h->page_size, entry = jg ? 8U : 4U;
    unsigned width = jg ? 2U : 4U;
    if (dn < count_width) return false;
    count = count_width == 4 ? xx_data_get_u32(dir, 4, 0, false) : xx_data_get_u16(dir, 2, 0, false);
    if (!(count <= MSF_MAX_STREAMS && count * entry + count_width <= dn)) return false;
    at = count * entry + count_width;
    for (j = 0; j < count; ++j) {
        size = xx_data_get_u32(dir + (size_t)(count_width + j * entry), 4, 0, false);
        if (size == 0xffffffffU) continue;
        need = (size + bs - 1) / bs;
        if (!record_span(at, need * width, dn)) return false;
        if (!j) {
            at += need * width;
            continue;
        }
        for (k = 0; k < need; ++k) {
            (void)msf_directory_entry(dir, at, width, &id);
            at += width;
            if (binary_stop(pd) || id >= blocks || msf_used(used, id)) return false;
        }
        if (s) {
            char name[40];
            xx_rt_snprintf(name, sizeof(name), "stream-%04u", (unsigned)j);
            /* The reader must see the same table this parse checked. */
            if (xx_ms_pdb_get_stream_size(h->pdb, (int32_t)j) != (int64_t)size || !msf_add_lazy(f, s, name, h, (int32_t)j, size)) return false;
        }
    }
    return at == dn && (!s || xx_ms_pdb_get_number_of_streams(h->pdb) == (int32_t)count);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t head[MSF_JG_HEADER], *dir = NULL, *used = NULL, *bitmap = NULL, *mark = NULL;
    uint64_t j, dn, db, map = 0, blocks, avail, groups, bit_span;
    uint32_t bs, fpm;
    unsigned count_width = 4;
    bool jg, ok = false;
    msf_shared *h = NULL;
    int64_t available = pm_available(f);
    size_t got;
    if (available < 32 || binary_stop(pd)) return false;
    avail = (uint64_t)available;
    got = avail < MSF_JG_HEADER ? (size_t)avail : MSF_JG_HEADER;
    if (!pm_read(f, 0, head, got)) return false;
    if (!xx_rt_memcmp(head,
                      "Microsoft C/C++ MSF 7.00\r\n\x1a"
                      "DS\0\0\0",
                      32)) {
        jg = false;
        if (got < MSF_DS_HEADER) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid MSF page or directory size");
            return false;
        }
        bs = xx_data_get_u32(head + 32, 4, 0, false);
        fpm = xx_data_get_u32(head + 36, 4, 0, false);
        blocks = xx_data_get_u32(head + 40, 4, 0, false);
        dn = xx_data_get_u32(head + 44, 4, 0, false);
        map = xx_data_get_u32(head + 52, 4, 0, false);
        if (!((bs == 512 || bs == 1024 || bs == 2048 || bs == 4096) && dn >= 4 && dn <= 4194304)) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid MSF page or directory size");
            return false;
        }
        if (!((fpm == 1 || fpm == 2) && blocks >= 4 && blocks * bs <= avail && map < blocks && !xx_data_get_u32(head + 48, 4, 0, false))) return false;
        /* Linkers that rewrite a PDB in place can leave stale pages behind the
         * last block (nim/lld PDBs: 7.3 MB of MSF in an 11 MB file). The MSF ends
         * at blocks * page size; anything after it is not part of the format. */
        avail = blocks * bs;
        db = (dn + bs - 1) / bs;
        if (db * 4 > bs) return false;
    } else if (got >= 44 && !xx_rt_memcmp(head,
                                          "Microsoft C/C++ program database 2.00\r\n\x1a"
                                          "JG\0\0",
                                          44)) {
        jg = true;
        if (got < MSF_JG_HEADER) return false;
        bs = xx_data_get_u32(head + 44, 4, 0, false);
        fpm = xx_data_get_u16(head + 48, 2, 0, false);
        blocks = xx_data_get_u16(head + 50, 2, 0, false);
        dn = xx_data_get_u32(head + 52, 4, 0, false);
        if (!((bs == 512 || bs == 1024 || bs == 2048 || bs == 4096) && dn >= 4 && dn <= 4194304)) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid MSF page or directory size");
            return false;
        }
        /* One free page map page covers the whole (16-bit numbered) file. */
        if (!(fpm >= 1 && blocks >= 3 && fpm < blocks && blocks <= (uint64_t)bs * 8U && blocks * bs <= avail)) return false;
        avail = blocks * bs;
        db = (dn + bs - 1) / bs;
        if (MSF_JG_HEADER + db * 2 > bs) return false;
    } else {
        return false;
    }
    h = (msf_shared *)xx_mem_alloc(sizeof(*h));
    if (!h) return false;
    xx_mem_zero(h, sizeof(*h));
    h->refs = 1;
    h->page_size = bs;
    h->directory_size = (uint32_t)dn;
    h->directory_page_count = (uint32_t)db;
    h->directory_pages = (uint32_t *)xx_mem_alloc((size_t)db * sizeof(uint32_t));
    dir = (uint8_t *)xx_mem_alloc((size_t)dn);
    used = (uint8_t *)xx_mem_alloc((size_t)((blocks + 7) / 8));
    bitmap = (uint8_t *)xx_mem_alloc(bs);
    mark = (uint8_t *)xx_mem_alloc(db * 4 > MSF_JG_HEADER ? (size_t)(db * 4) : MSF_JG_HEADER);
    BLOB_NEED(h->directory_pages && dir && used && bitmap && mark);
    xx_mem_zero(used, (size_t)((blocks + 7) / 8));
    (void)msf_used(used, 0);
    if (jg) {
        (void)msf_used(used, fpm);
        BLOB_NEED(pm_read(f, MSF_JG_HEADER, mark, (size_t)(db * 2)));
    } else {
        for (j = 0; j < blocks; j += bs) {
            if (j + 1 < blocks) (void)msf_used(used, j + 1);
            if (j + 2 < blocks) (void)msf_used(used, j + 2);
        }
        BLOB_NEED(!msf_used(used, map));
        BLOB_NEED(pm_read(f, (int64_t)(map * bs), mark, (size_t)(db * 4)));
    }
    for (j = 0; j < db; ++j) {
        uint64_t id = jg ? xx_data_get_u16(mark + (size_t)(j * 2), 2, 0, false) : xx_data_get_u32(mark + (size_t)(j * 4), 4, 0, false), n = dn - j * bs;
        BLOB_NEED(!binary_stop(pd) && id < blocks && !msf_used(used, id));
        h->directory_pages[(size_t)j] = (uint32_t)id;
        if (n > bs) n = bs;
        BLOB_NEED(pm_read(f, (int64_t)(id * bs), dir + (size_t)(j * bs), (size_t)n));
    }
    if (jg) {
        /* The width of the stream counter is not documented; the reader
         * tries 32 bits first, then 16. Probe on a copy of the page marks. */
        uint8_t *probe = (uint8_t *)xx_mem_alloc((size_t)((blocks + 7) / 8));
        bool wide;
        BLOB_NEED(probe);
        xx_rt_memcpy(probe, used, (size_t)((blocks + 7) / 8));
        wide = msf_streams(f, NULL, h, dir, probe, blocks, true, 4, pd);
        xx_mem_free(probe);
        count_width = wide ? 4U : 2U;
    }
    /* The reader works on the format's own byte range. */
    h->view = xx_io_sub_open_ro(f->device, f->base_address, (int64_t)avail);
    BLOB_NEED(h->view);
    h->pdb = xx_ms_pdb_open(h->view, pd);
    BLOB_NEED(h->pdb && xx_ms_pdb_get_type(h->pdb) == (jg ? XX_MS_PDB_TYPE_PDB2 : XX_MS_PDB_TYPE_PDB7));
    BLOB_NEED(pm_add(f, s, "superblock", 0, jg ? MSF_JG_HEADER : MSF_DS_HEADER));
    BLOB_NEED(msf_streams(f, s, h, dir, used, blocks, jg, count_width, pd));
    /* Every owned page must be marked in use (bit clear) in the free page
     * map. DS: each bitmap page covers bs*8 blocks, although the bitmap pages
     * are spaced bs blocks apart. JG: one bitmap page. */
    bit_span = (uint64_t)bs * 8U;
    groups = (blocks + bit_span - 1) / bit_span;
    for (j = 0; j < groups; ++j) {
        uint64_t first = j * bit_span, last = first + bit_span, page = jg ? fpm : j * bs + fpm, k;
        bool any = false;
        if (last > blocks) last = blocks;
        for (k = first; k < last && !any; ++k) any = msf_is_used(used, k);
        if (!any) continue;
        BLOB_NEED(!binary_stop(pd) && page < blocks && pm_read(f, (int64_t)(page * bs), bitmap, (size_t)((last - first + 7) / 8)));
        for (k = first; k < last; ++k)
            if (msf_is_used(used, k)) BLOB_NEED(!(bitmap[(size_t)((k - first) / 8)] & (1U << ((k - first) & 7))));
    }
    BLOB_NEED(!binary_stop(pd) && msf_add_lazy(f, s, "directory", h, -1, dn));
    s->size = (int64_t)avail;
    ok = true;
done:
    if (dir) xx_mem_free(dir);
    if (used) xx_mem_free(used);
    if (bitmap) xx_mem_free(bitmap);
    if (mark) xx_mem_free(mark);
    msf_shared_release(h);
    return ok;
}
void xx_microsoft_msf_init(xx_microsoft_msf *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_MICROSOFT_MSF, "pdb");
    }
}
xx_microsoft_msf *xx_microsoft_msf_create(xx_io_device *d, int64_t b)
{
    xx_microsoft_msf *r = (xx_microsoft_msf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_microsoft_msf_init(r, d, b);
    return r;
}
void xx_microsoft_msf_destroy(xx_microsoft_msf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_microsoft_msf_free(xx_microsoft_msf *r)
{
    if (r) {
        xx_microsoft_msf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_microsoft_msf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_microsoft_msf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
