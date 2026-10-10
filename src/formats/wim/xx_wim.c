/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft Windows Imaging Format: WIM, split WIM parts (SWM) and the
 * solid-compressed ESD variant.
 *
 * Header (0x74..0xd0 bytes, little endian)
 *   +0x00  "MSWIM\0\0\0"
 *   +0x08  u32  header size
 *   +0x0c  u32  version (0x00010d00; 0x00000e00 for solid images)
 *   +0x10  u32  flags; bit 1 means the resources are compressed and bits
 *                17 / 18 / 19 / 21 select XPRESS / LZX / LZMS / XPRESS
 *   +0x14  u32  chunk size of compressed resources (0 means 32768)
 *   +0x18  16   GUID
 *   +0x28  u16  part number   +0x2a  u16  total parts
 *   +0x2c  u32  image count
 *   +0x30  RESHDR offset table    +0x48  RESHDR XML data
 *   +0x60  RESHDR boot metadata   +0x78  u32 boot index
 *   +0x7c  RESHDR integrity table
 *
 * A RESHDR is 24 bytes: a SEVEN byte size, a flag byte sharing its
 * quadword, then a u64 offset and a u64 uncompressed size.  Flags: 0x02
 * metadata, 0x04 compressed, 0x08 spanned, 0x10 solid.
 *
 * The offset table lists every stored stream ("blob"): a RESHDR, u16 part
 * number, u32 reference count and the stream's SHA-1, 50 bytes each.  A
 * compressed resource starts with a table of chunk start offsets (one per
 * chunk after the first, 4 bytes, or 8 when the resource exceeds 4 GiB);
 * a chunk whose packed size equals its unpacked size is stored raw.
 *
 * Solid images put SOLID entries in runs.  An entry whose uncompressed-size
 * field is 0x100000000 is a solid resource: {u64 size, u32 chunk size, u32
 * codec} followed by one u32 packed size per chunk.  The other entries of the
 * run are blobs: their offset field addresses the run's resources laid end to
 * end, and their size field is the blob's size.
 *
 * Every metadata blob is one image: a security block, then a tree of
 * directory entries (0x66 fixed bytes, the long name, the short name, then
 * the entry's extra stream entries).  File content is found by SHA-1.
 *
 * Members are the image's files and directories, "wim.xml" (the XML
 * resource), and "stream_NNNN.bin" / "metadata_NNNN.bin" for any blob no
 * image references, which is everything in a split part without metadata.
 * A file with several images puts each under "<index>/".  Named data
 * streams become "<file>.__streams__/<name>".  Symbolic links and junctions
 * are extracted as ordinary files holding their target text (no link is
 * created); a relative link that stays inside the image also reports its
 * target as XX_META_ID_LINK_TARGET.
 * Every stream is checked against its SHA-1 on extraction, and one that does
 * not match fails (its output file is removed), stored or decoded alike.
 *
 * Hostile-input bounds: blobs are found by bisection over a sorted index,
 * solid runs are indexed once, member paths are kept as parent index plus
 * component (all components together at most WIM_MAX_NAME_BYTES), decoded
 * metadata is budgeted against the file size, and only one solid chunk
 * table is held at a time.
 *
 * A split image is read one part at a time: the first part lists the whole
 * tree, and a file whose data lives in another part fails to extract; the
 * other parts list their streams as stream_NNNN.bin.  Header versions before
 * 1.13 (pre-release Longhorn layouts, 0x60-byte headers) are not handled.
 *
 * Layout cross-checked against XArchive packages/xwim.cpp (MIT, same
 * author); codecs are in xx_wim_codec.c.
 */
/* Request POSIX before any header; a strict -std=c11 hides it otherwise. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/formats/wim/xx_wim.h"
#include "xx_wim_codec.h"

#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef WIM
#define XX_WIM_FILE_TYPE XX_FILE_TYPE_WIM
#else
#define XX_WIM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define WIM_NONE 0xFFFFFFFFU

#define WIM_HEADER_MIN 0x74U
#define WIM_HEADER_MAX 0xD0U
#define WIM_RESHDR_SIZE 24U
#define WIM_LOOKUP_ENTRY_SIZE 50U
#define WIM_HASH_SIZE 20U
#define WIM_SOLID_MAGIC UINT64_C(0x100000000)
#define WIM_SOLID_HEADER 16U

#define WIM_HDR_COMPRESSION 0x00000002U
#define WIM_HDR_XPRESS 0x00020000U
#define WIM_HDR_LZX 0x00040000U
#define WIM_HDR_LZMS 0x00080000U
#define WIM_HDR_XPRESS2 0x00200000U

#define WIM_RES_METADATA 0x02U
#define WIM_RES_COMPRESSED 0x04U
#define WIM_RES_SPANNED 0x08U
#define WIM_RES_SOLID 0x10U

#define WIM_ATTR_DIRECTORY 0x00000010U
#define WIM_ATTR_REPARSE 0x00000400U
#define WIM_ATTR_ENCRYPTED 0x00004000U
#define WIM_TAG_MOUNT_POINT 0xA0000003U
#define WIM_TAG_SYMLINK 0xA000000CU

#define WIM_DENTRY_FIXED 0x66U
#define WIM_STREAM_FIXED 0x26U

/* Caps.  The offset table of a multi-edition Windows install image is a few
 * MiB and its largest metadata resource under 128 MiB. */
#define WIM_MAX_TABLE_BYTES (64U * 1024U * 1024U)
#define WIM_MAX_METADATA (256U * 1024U * 1024U)
#define WIM_MAX_MEMBERS (4U * 1024U * 1024U)
#define WIM_MAX_DEPTH 512U
#define WIM_MAX_PATH 8192U
#define WIM_MAX_COMPONENT_UNITS 255U
#define WIM_MAX_SOLID_CHUNKS (1U << 22)
#define WIM_MAX_REPARSE 16384U
/* Member names are stored once per component (a parent index plus the
 * component), and all components together may not exceed this. */
#define WIM_MAX_NAME_BYTES (128U * 1024U * 1024U)
/* Decoded metadata across all images may not exceed this plus 4 times the
 * file's size; chunk offset tables of solid resources, loaded one at a
 * time, may not total more than this plus twice the file's size. */
#define WIM_METADATA_FLOOR (64U * 1024U * 1024U)
#define WIM_SOLID_TABLE_FLOOR (64U * 1024U * 1024U)

enum {
    WIM_KIND_FILE = 0,
    WIM_KIND_DIR,
    WIM_KIND_LINK,
    WIM_KIND_XML,
    WIM_KIND_STREAM
};

typedef struct wim_reshdr_s {
    uint64_t packed;
    uint64_t offset;
    uint64_t size;
    uint8_t flags;
} wim_reshdr;

typedef struct wim_header_s {
    uint32_t header_size;
    uint32_t version;
    uint32_t flags;
    uint32_t chunk_size;
    uint32_t image_count;
    uint16_t part;
    uint16_t parts;
    wim_reshdr table;
    wim_reshdr xml;
    wim_reshdr boot;
    wim_reshdr integrity;
} wim_header;

typedef struct wim_blob_s {
    uint8_t hash[WIM_HASH_SIZE];
    uint8_t flags;
    bool present; /* its bytes are in this file */
    bool used;    /* referenced by a member */
    uint16_t part;
    uint32_t refcount;
    uint32_t table_index;
    uint32_t solid_run; /* WIM_NONE unless it lives in a solid run */
    uint64_t offset;    /* from the base, or within the solid run */
    uint64_t packed;
    uint64_t size;
} wim_blob;

typedef struct wim_solid_s {
    uint64_t offset;
    uint64_t packed;
    uint64_t usize;
    uint64_t run_base;
    uint64_t chunk_count;
    uint64_t *chunk_start; /* chunk_count + 1 entries, only while current */
    uint32_t run;
    uint32_t chunk_size;
    uint32_t codec;
    bool usable;
    bool failed;
} wim_solid;

/* A solid run's resources are consecutive in the solids array; the usable
 * ones are a prefix of them, laid end to end. */
typedef struct wim_run_s {
    uint32_t first;
    uint32_t count;
    uint32_t usable;
    uint64_t covered;
} wim_run;

typedef struct wim_member_s {
    uint64_t size;
    uint64_t timestamp;
    uint32_t attributes;
    uint32_t reparse_tag;
    uint32_t blob;
    uint32_t name;     /* offset of the last path component in the pool */
    uint32_t parent;   /* member holding the rest of the path, or WIM_NONE */
    uint16_t path_len; /* bytes of the full path, below WIM_MAX_PATH */
    uint8_t kind;
    bool missing;
} wim_member;

typedef struct wim_model_s {
    xx_io_device *device;
    int64_t base;
    int64_t avail;
    int64_t end;
    size_t io_capacity; /* captured at model creation */
    wim_header hdr;
    unsigned method;
    uint32_t chunk_size;
    bool method_ok;
    wim_blob *blobs;
    size_t blob_count;
    uint32_t xml_blob;
    uint32_t *sorted; /* blob indices by hash, one per distinct hash */
    size_t sorted_count;
    wim_solid *solids;
    size_t solid_count;
    wim_run *runs;
    uint32_t run_count;
    size_t table_solid; /* the solid whose chunk table is loaded */
    uint64_t table_budget;
    wim_member *members;
    size_t member_count;
    size_t member_cap;
    char *pool;
    size_t pool_len;
    size_t pool_cap;
    bool pool_full;
    char *path; /* WIM_MAX_PATH bytes for building one member's path */
    uint32_t *order;
    size_t index;
    uint64_t *names;
    size_t name_mask;
    size_t name_count;
    uint64_t name_seed;    /* secret per-model key for the slot mixer */
    unsigned name_reseeds; /* rebuilds forced by an over-long probe */
    uint32_t next_dir;
    bool has_solid;
    /* extraction workspace */
    xx_wim_codec *codecs[4];
    uint8_t *in_buf;
    size_t in_cap;
    uint8_t *out_buf;
    size_t out_cap;
    uint8_t *cache;
    size_t cache_cap;
    size_t cache_len;
    size_t cache_solid;
    uint64_t cache_chunk;
    bool cache_valid;
    uint8_t *ra; /* read-ahead window over the container */
    uint64_t ra_offset;
    size_t ra_len;
    bool memory_limited;
    uint64_t memory_limit;
    xx_pd_struct *pd;
} wim_model;

typedef struct wim_sink_s {
    xx_io_device *device;
    uint8_t *memory;
    size_t capacity;
    size_t limit; /* a memory sink grows up to this as data arrives */
    size_t io_capacity;
    uint64_t written;
    bool hashing;
    xx_hash_context sha;
    xx_pd_struct *pd;
} wim_sink;

/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */

static bool wim_is_zero_hash(const uint8_t *hash)
{
    unsigned i;
    for (i = 0U; i < WIM_HASH_SIZE; ++i)
        if (hash[i] != 0U) return false;
    return true;
}

static bool wim_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool wim_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size, size_t transfer_capacity)
{
    size_t done = 0U;

    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        size_t request = size - done;
        if (request > transfer_capacity) request = transfer_capacity;
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Reads @p size bytes at @p offset from the base, which the caller has
 * already bounded by the available size. */
static bool wim_read_rel(wim_model *m, uint64_t offset, void *buffer, size_t size)
{
    if (offset > (uint64_t)m->avail || (uint64_t)size > (uint64_t)m->avail - offset) return false;
    return wim_read_at(m->device, m->base + (int64_t)offset, buffer, size, m->io_capacity);
}

/* Small reads that walk forward through a resource (chunk offset tables,
 * the chunks themselves, stored streams) go through a read-ahead window of
 * up to 1 MiB that never extends past @p end, the end of the resource being
 * read: one device read per window instead of one per chunk, which is what
 * makes a high-latency device usable, without reading bytes of other
 * resources.  Larger reads go direct. */

static bool wim_read_ra(wim_model *m, uint64_t offset, void *buffer, size_t size, uint64_t end)
{
    uint64_t left;
    if (size == 0U) return true;
    if (offset > (uint64_t)m->avail || (uint64_t)size > (uint64_t)m->avail - offset) return false;
    if (size > m->io_capacity / 4U) return wim_read_rel(m, offset, buffer, size);
    if (!m->ra || offset < m->ra_offset || offset - m->ra_offset > m->ra_len || (uint64_t)size > m->ra_len - (offset - m->ra_offset)) {
        if (!m->ra && !(m->ra = (uint8_t *)xx_mem_alloc(m->io_capacity))) return wim_read_rel(m, offset, buffer, size);
        left = (end < (uint64_t)m->avail ? end : (uint64_t)m->avail) - offset;
        if (end < offset || left < (uint64_t)size) left = size;
        m->ra_len = left < m->io_capacity ? (size_t)left : m->io_capacity;
        m->ra_offset = offset;
        if (!wim_read_rel(m, offset, m->ra, m->ra_len)) {
            m->ra_len = 0U;
            return false;
        }
    }
    xx_rt_memcpy(buffer, m->ra + (size_t)(offset - m->ra_offset), size);
    return true;
}

static void wim_parse_reshdr(const uint8_t *raw, wim_reshdr *r)
{
    r->packed = xx_data_get_u64(raw, 8, 0, false) & UINT64_C(0x00FFFFFFFFFFFFFF);
    r->flags = raw[7];
    r->offset = xx_data_get_u64(raw + 8U, 8, 0, false);
    r->size = xx_data_get_u64(raw + 16U, 8, 0, false);
}

static bool wim_reshdr_fits(const wim_reshdr *r, int64_t avail)
{
    return avail >= 0 && r->offset <= (uint64_t)avail && r->packed <= (uint64_t)avail - r->offset && r->size <= (uint64_t)INT64_MAX;
}

static void wim_note_end(wim_model *m, uint64_t offset, uint64_t size)
{
    if (offset <= (uint64_t)m->avail && size <= (uint64_t)m->avail - offset && (int64_t)(offset + size) > m->end) m->end = (int64_t)(offset + size);
}

/* ---------------------------------------------------------------------- */
/* Header and offset table                                                 */

static bool wim_read_header(wim_model *m)
{
    uint8_t header[WIM_HEADER_MAX];
    wim_header *h = &m->hdr;
    size_t want = m->avail < (int64_t)WIM_HEADER_MAX ? (size_t)m->avail : WIM_HEADER_MAX;
    if (m->avail < (int64_t)WIM_HEADER_MIN) return false;
    xx_mem_zero(header, sizeof(header));
    if (!wim_read_at(m->device, m->base, header, want, m->io_capacity) || xx_rt_memcmp(header, "MSWIM\0\0\0", 8U) != 0) return false;
    h->header_size = xx_data_get_u32(header + 8U, 4, 0, false);
    h->version = xx_data_get_u32(header + 12U, 4, 0, false);
    h->flags = xx_data_get_u32(header + 16U, 4, 0, false);
    h->chunk_size = xx_data_get_u32(header + 20U, 4, 0, false);
    h->part = xx_data_get_u16(header + 0x28U, 2, 0, false);
    h->parts = xx_data_get_u16(header + 0x2AU, 2, 0, false);
    h->image_count = xx_data_get_u32(header + 0x2CU, 4, 0, false);
    if (h->header_size < WIM_HEADER_MIN || h->header_size > WIM_HEADER_MAX || (int64_t)h->header_size > m->avail) return false;
    /* Bytes past the declared header size are not header. */
    if (h->header_size < WIM_HEADER_MAX) xx_mem_zero(header + h->header_size, WIM_HEADER_MAX - h->header_size);
    wim_parse_reshdr(header + 0x30U, &h->table);
    wim_parse_reshdr(header + 0x48U, &h->xml);
    wim_parse_reshdr(header + 0x60U, &h->boot);
    wim_parse_reshdr(header + 0x7CU, &h->integrity);
    if (!wim_reshdr_fits(&h->table, m->avail) || !wim_reshdr_fits(&h->xml, m->avail)) return false;
    m->end = (int64_t)h->header_size;
    wim_note_end(m, h->table.offset, h->table.packed);
    wim_note_end(m, h->xml.offset, h->xml.packed);
    if (wim_reshdr_fits(&h->boot, m->avail)) wim_note_end(m, h->boot.offset, h->boot.packed);
    if (wim_reshdr_fits(&h->integrity, m->avail)) wim_note_end(m, h->integrity.offset, h->integrity.packed);

    m->chunk_size = h->chunk_size != 0U ? h->chunk_size : 32768U;
    m->method = XX_WIM_CODEC_NONE;
    m->method_ok = false;
    if ((h->flags & WIM_HDR_COMPRESSION) != 0U) {
        uint32_t select = h->flags & (WIM_HDR_XPRESS | WIM_HDR_LZX | WIM_HDR_LZMS | WIM_HDR_XPRESS2);
        uint32_t chunk = m->chunk_size;
        bool pow2 = (chunk & (chunk - 1U)) == 0U;
        if (select == WIM_HDR_LZX) {
            m->method = XX_WIM_CODEC_LZX;
            m->method_ok = pow2 && chunk >= (1U << 15) && chunk <= (1U << 21);
        } else if (select == WIM_HDR_XPRESS || select == WIM_HDR_XPRESS2) {
            m->method = XX_WIM_CODEC_XPRESS;
            m->method_ok = pow2 && chunk >= (1U << 12) && chunk <= (1U << 16);
        } else if (select == WIM_HDR_LZMS) {
            m->method = XX_WIM_CODEC_LZMS;
            m->method_ok = pow2 && chunk >= (1U << 15) && chunk <= XX_WIM_CODEC_MAX_CHUNK;
        }
    }
    return true;
}

/* Blob lookup by SHA-1.  The hashes come from the file, so no hash table
 * keyed on them can promise short probes; the blobs are sorted instead
 * (a merge sort, O(n log n) whatever the input) and searched by bisection.
 * When several entries share a hash the first one wins. */
static int wim_blob_cmp(const wim_model *m, uint32_t a, uint32_t b)
{
    int c = xx_rt_memcmp(m->blobs[a].hash, m->blobs[b].hash, WIM_HASH_SIZE);
    if (c != 0) return c;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static bool wim_sort_blobs(wim_model *m)
{
    size_t n = 0U, i, width, kept;
    uint32_t *tmp;
    m->sorted = (uint32_t *)xx_mem_alloc((m->blob_count + 1U) * sizeof(uint32_t));
    if (!m->sorted) return false;
    for (i = 0U; i < m->blob_count; ++i) {
        const wim_blob *b = &m->blobs[i];
        if ((uint32_t)i == m->xml_blob || (b->flags & WIM_RES_METADATA) != 0U || wim_is_zero_hash(b->hash)) continue;
        m->sorted[n++] = (uint32_t)i;
    }
    if (n > 1U) {
        tmp = (uint32_t *)xx_mem_alloc(n * sizeof(uint32_t));
        if (!tmp) return false;
        for (width = 1U; width < n; width *= 2U) {
            for (i = 0U; i < n; i += 2U * width) {
                size_t mid = i + width < n ? i + width : n, hi = i + 2U * width < n ? i + 2U * width : n, a = i, b = mid, k = i;
                while (a < mid && b < hi) tmp[k++] = wim_blob_cmp(m, m->sorted[b], m->sorted[a]) < 0 ? m->sorted[b++] : m->sorted[a++];
                while (a < mid) tmp[k++] = m->sorted[a++];
                while (b < hi) tmp[k++] = m->sorted[b++];
            }
            xx_rt_memcpy(m->sorted, tmp, n * sizeof(uint32_t));
        }
        xx_mem_free(tmp);
    }
    for (i = 0U, kept = 0U; i < n; ++i) {
        if (kept != 0U && xx_rt_memcmp(m->blobs[m->sorted[kept - 1U]].hash, m->blobs[m->sorted[i]].hash, WIM_HASH_SIZE) == 0) continue;
        m->sorted[kept++] = m->sorted[i];
    }
    m->sorted_count = kept;
    return true;
}

static uint32_t wim_find_blob(const wim_model *m, const uint8_t *hash)
{
    size_t lo = 0U, hi;
    if (!m->sorted || wim_is_zero_hash(hash)) return WIM_NONE;
    hi = m->sorted_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        int c = xx_rt_memcmp(m->blobs[m->sorted[mid]].hash, hash, WIM_HASH_SIZE);
        if (c == 0) return m->sorted[mid];
        if (c < 0) lo = mid + 1U;
        else hi = mid;
    }
    return WIM_NONE;
}

static bool wim_read_resource_memory(wim_model *m, const wim_blob *blob, uint64_t limit, uint8_t **out);

static bool wim_load_solid_header(wim_model *m, wim_solid *sd)
{
    uint8_t head[WIM_SOLID_HEADER];
    uint32_t chunk;
    if (sd->packed < WIM_SOLID_HEADER || !wim_read_rel(m, sd->offset, head, sizeof(head))) return false;
    sd->usize = xx_data_get_u64(head, 8, 0, false);
    chunk = xx_data_get_u32(head + 8U, 4, 0, false);
    sd->codec = xx_data_get_u32(head + 12U, 4, 0, false);
    sd->chunk_size = chunk;
    if (chunk < 4096U || chunk > XX_WIM_CODEC_MAX_CHUNK || (chunk & (chunk - 1U)) != 0U || sd->codec > XX_WIM_CODEC_LZMS || sd->usize > (uint64_t)INT64_MAX) return false;
    if (sd->codec == XX_WIM_CODEC_LZX && chunk > (1U << 21)) return false;
    sd->chunk_count = sd->usize == 0U ? 0U : (sd->usize - 1U) / chunk + 1U;
    if (sd->chunk_count > WIM_MAX_SOLID_CHUNKS || sd->chunk_count * 4U > sd->packed - WIM_SOLID_HEADER) return false;
    return true;
}

/* Loads every offset table entry.  Entries this part cannot supply are kept
 * (a split part lists only its own, but a damaged table may not) and simply
 * marked absent. */
static bool wim_load_table(wim_model *m)
{
    const wim_reshdr *t = &m->hdr.table;
    uint8_t *raw = NULL;
    size_t entries, i, cap;
    bool in_run = false;
    uint32_t run = 0U;
    uint64_t run_size = 0U;
    bool run_ok = true;
    wim_blob table_blob;

    if (t->size != 0U && (t->size > WIM_MAX_TABLE_BYTES || (t->size % WIM_LOOKUP_ENTRY_SIZE) != 0U)) return false;
    entries = (size_t)(t->size / WIM_LOOKUP_ENTRY_SIZE);
    /* The table is read before anything is sized from it, so a declared
     * size the file cannot back costs nothing. */
    if (entries != 0U) {
        xx_mem_zero(&table_blob, sizeof(table_blob));
        table_blob.flags = (uint8_t)(t->flags & ~WIM_RES_METADATA);
        table_blob.present = true;
        table_blob.solid_run = WIM_NONE;
        table_blob.offset = t->offset;
        table_blob.packed = t->packed;
        table_blob.size = t->size;
        if ((t->flags & (WIM_RES_SOLID | WIM_RES_SPANNED)) != 0U || !wim_read_resource_memory(m, &table_blob, WIM_MAX_TABLE_BYTES, &raw)) return false;
    }
    m->blobs = (wim_blob *)xx_mem_calloc(entries + 1U, sizeof(wim_blob));
    if (!m->blobs) {
        if (raw) xx_mem_free(raw);
        return false;
    }

    for (i = 0U, cap = 0U; i < entries; ++i) {
        const uint8_t *e = raw + i * WIM_LOOKUP_ENTRY_SIZE;
        if ((e[7] & WIM_RES_SOLID) != 0U && xx_data_get_u64(e + 16U, 8, 0, false) == WIM_SOLID_MAGIC) ++cap;
    }
    m->solids = (wim_solid *)xx_mem_calloc(cap + 1U, sizeof(wim_solid));
    if (!m->solids) {
        if (raw) xx_mem_free(raw);
        return false;
    }

    for (i = 0U; i < entries; ++i) {
        const uint8_t *e = raw + i * WIM_LOOKUP_ENTRY_SIZE;
        wim_reshdr r;
        uint16_t part = xx_data_get_u16(e + 24U, 2, 0, false);
        wim_parse_reshdr(e, &r);
        if ((r.flags & WIM_RES_SOLID) != 0U) {
            if (!in_run) {
                in_run = true;
                run = m->run_count++;
                run_size = 0U;
                run_ok = true;
            }
            if (r.size == WIM_SOLID_MAGIC) {
                wim_solid *sd = &m->solids[m->solid_count++];
                sd->offset = r.offset;
                sd->packed = r.packed;
                sd->run = run;
                m->has_solid = true;
                sd->usable = run_ok && part == m->hdr.part && wim_reshdr_fits(&r, m->avail) && wim_load_solid_header(m, sd) && sd->usize <= UINT64_MAX - run_size;
                if (sd->usable) {
                    sd->run_base = run_size;
                    run_size += sd->usize;
                    wim_note_end(m, r.offset, r.packed);
                } else {
                    run_ok = false; /* later offsets in this run are lost */
                }
                continue;
            }
        } else {
            in_run = false;
        }
        {
            wim_blob *b = &m->blobs[m->blob_count];
            xx_rt_memcpy(b->hash, e + 30U, WIM_HASH_SIZE);
            b->flags = r.flags;
            b->part = part;
            b->refcount = xx_data_get_u32(e + 26U, 4, 0, false);
            b->table_index = (uint32_t)i;
            b->offset = r.offset;
            if ((r.flags & WIM_RES_SOLID) != 0U) {
                b->solid_run = run;
                b->packed = 0U;
                b->size = r.packed;
                b->present = part == m->hdr.part;
            } else {
                b->solid_run = WIM_NONE;
                b->packed = r.packed;
                b->size = r.size;
                b->present = part == m->hdr.part && (r.flags & WIM_RES_SPANNED) == 0U && wim_reshdr_fits(&r, m->avail);
                if (b->present) wim_note_end(m, r.offset, r.packed);
            }
            ++m->blob_count;
        }
    }
    if (raw) xx_mem_free(raw);

    /* Each run's resources are consecutive in m->solids and the usable
     * ones come first, so one pass indexes them.  A solid blob is only
     * present when that usable prefix covers it. */
    m->runs = (wim_run *)xx_mem_calloc((size_t)m->run_count + 1U, sizeof(wim_run));
    if (!m->runs) return false;
    for (i = 0U; i < m->solid_count; ++i) {
        const wim_solid *sd = &m->solids[i];
        wim_run *r = &m->runs[sd->run];
        if (r->count == 0U) r->first = (uint32_t)i;
        ++r->count;
        if (sd->usable) {
            ++r->usable;
            r->covered = sd->run_base + sd->usize;
        }
    }
    for (i = 0U; i < m->blob_count; ++i) {
        wim_blob *b = &m->blobs[i];
        uint64_t covered;
        if (b->solid_run == WIM_NONE || !b->present) continue;
        covered = m->runs[b->solid_run].covered;
        b->present = b->offset <= covered && b->size <= covered - b->offset;
    }

    /* The XML resource rides along as one more blob. */
    if (m->hdr.xml.packed != 0U) {
        wim_blob *b = &m->blobs[m->blob_count];
        xx_mem_zero(b, sizeof(*b));
        b->flags = (uint8_t)(m->hdr.xml.flags & ~WIM_RES_METADATA);
        b->present = (b->flags & (WIM_RES_SOLID | WIM_RES_SPANNED)) == 0U;
        b->solid_run = WIM_NONE;
        b->offset = m->hdr.xml.offset;
        b->packed = m->hdr.xml.packed;
        b->size = m->hdr.xml.size;
        b->table_index = WIM_NONE;
        m->xml_blob = (uint32_t)m->blob_count++;
    }

    return wim_sort_blobs(m);
}

/* ---------------------------------------------------------------------- */
/* Resource reading                                                        */

static bool wim_sink_put(wim_sink *s, const uint8_t *data, size_t size)
{
    if (size == 0U) return true;
    if (wim_stopped(s->pd)) return false;
    if (s->memory) {
        if ((uint64_t)size > (uint64_t)s->limit - s->written) return false;
        if ((uint64_t)size > (uint64_t)s->capacity - s->written) {
            /* Grow with the data actually produced, never to a declared
             * size up front: a header can claim anything. */
            size_t need = (size_t)s->written + size, cap = s->capacity;
            uint8_t *grown;
            while (cap < need) cap = cap > s->limit / 2U ? s->limit : cap * 2U;
            grown = (uint8_t *)xx_mem_realloc(s->memory, cap + 1U);
            if (!grown) return false;
            s->memory = grown;
            s->capacity = cap;
        }
        xx_rt_memcpy(s->memory + (size_t)s->written, data, size);
    } else if (s->device) {
        size_t done = 0U;
        while (done < size) {
            size_t request = size - done;
            if (request > s->io_capacity) request = s->io_capacity;
            ssize_t amount = xx_io_write(s->device, data + done, request);
            if (amount <= 0 || (size_t)amount > request) return false;
            done += (size_t)amount;
        }
    }
    if (s->hashing) xx_hash_update(&s->sha, data, size);
    s->written += size;
    return true;
}

static bool wim_budget(wim_model *m, uint64_t extra)
{
    uint64_t used = (uint64_t)m->in_cap + (uint64_t)m->out_cap + (uint64_t)m->cache_cap;
    return !m->memory_limited || (extra <= m->memory_limit && used <= m->memory_limit - extra);
}

static bool wim_grow(wim_model *m, uint8_t **buffer, size_t *cap, size_t need)
{
    uint8_t *grown;
    if (*cap >= need) return true;
    if (!wim_budget(m, (uint64_t)(need - *cap))) return false;
    grown = (uint8_t *)xx_mem_alloc(need);
    if (!grown) return false;
    if (*buffer) xx_mem_free(*buffer);
    *buffer = grown;
    *cap = need;
    return true;
}

static xx_wim_codec *wim_codec(wim_model *m, unsigned method)
{
    if (method == XX_WIM_CODEC_NONE || method > XX_WIM_CODEC_LZMS) return NULL;
    if (!m->codecs[method]) {
        /* The LZMS workspace carries a 256 KiB x86 history table. */
        if (!wim_budget(m, method == XX_WIM_CODEC_LZMS ? 0x60000U : 0x10000U)) return NULL;
        m->codecs[method] = xx_wim_codec_create(method);
    }
    return m->codecs[method];
}

static bool wim_copy_to_sink_buffered(wim_model *m, uint64_t offset, uint64_t size, wim_sink *s, uint8_t *buffer, size_t buffer_capacity)
{
    uint64_t end = offset + size;
    while (size != 0U) {
        size_t want = size < buffer_capacity ? (size_t)size : buffer_capacity;
        if (!wim_read_ra(m, offset, buffer, want, end) || !wim_sink_put(s, buffer, want)) return false;
        offset += want;
        size -= want;
    }
    return true;
}

static bool wim_copy_to_sink(wim_model *m, uint64_t offset, uint64_t size, wim_sink *s)
{
    size_t buffer_capacity = m->io_capacity;
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(buffer_capacity);
    bool result;
    if (!buffer) return false;
    result = wim_copy_to_sink_buffered(m, offset, size, s, buffer, buffer_capacity);
    xx_mem_free(buffer);
    return result;
}

/* A plain compressed resource: chunk offset table, then the chunks. */
static bool wim_read_chunked_buffered(wim_model *m, uint64_t offset, uint64_t packed, uint64_t size, wim_sink *s, uint8_t *table, size_t table_capacity)
{
    uint8_t single_entry[8]; /* one complete offset protocol frame for tiny capacities */
    uint64_t chunks, table_bytes, data_len, i, prev = 0U;
    uint64_t cached_first = 0U, cached_count = 0U, end_all;
    unsigned entry = size > UINT64_C(0xFFFFFFFF) ? 8U : 4U;
    uint32_t chunk = m->chunk_size;
    xx_wim_codec *codec;
    if (size == 0U) return true;
    if (!m->method_ok || !(codec = wim_codec(m, m->method))) return false;
    chunks = (size - 1U) / chunk + 1U;
    table_bytes = (chunks - 1U) * entry;
    if (table_bytes > packed || offset > (uint64_t)m->avail || packed > (uint64_t)m->avail - offset) return false;
    data_len = packed - table_bytes;
    end_all = offset + packed;
    {
        /* Buffers follow the resource, not the chunk size: a small resource
         * in an image that declares 128 MiB chunks needs small ones. */
        uint64_t out_need = size < chunk ? size : chunk;
        uint64_t in_need = data_len < out_need ? data_len : out_need;
        if (!wim_grow(m, &m->in_buf, &m->in_cap, (size_t)in_need + 1U) || !wim_grow(m, &m->out_buf, &m->out_cap, (size_t)out_need)) return false;
    }
    for (i = 0U; i < chunks; ++i) {
        uint64_t end, csize, usize;
        if (i + 1U < chunks) {
            if (i >= cached_first + cached_count) {
                uint64_t left = chunks - 1U - i;
                cached_first = i;
                size_t entries_per_batch = table_capacity / entry;
                if (!entries_per_batch) entries_per_batch = 1U;
                cached_count = left < entries_per_batch ? left : entries_per_batch;
                if (!wim_read_ra(m, offset + i * entry, table_capacity < entry ? single_entry : table, (size_t)(cached_count * entry), end_all)) return false;
            }
            end = entry == 4U ? xx_data_get_u32((table_capacity < entry ? single_entry : table) + (size_t)(i - cached_first) * 4U, 4, 0, false)
                              : xx_data_get_u64((table_capacity < entry ? single_entry : table) + (size_t)(i - cached_first) * 8U, 8, 0, false);
        } else {
            end = data_len;
        }
        usize = i + 1U < chunks ? chunk : size - i * chunk;
        if (end < prev || end > data_len) return false;
        csize = end - prev;
        if (csize == 0U || csize > usize) return false;
        if (csize == usize) {
            if (!wim_read_ra(m, offset + table_bytes + prev, m->out_buf, (size_t)csize, end_all)) return false;
        } else if (!wim_read_ra(m, offset + table_bytes + prev, m->in_buf, (size_t)csize, end_all) ||
                   !xx_wim_codec_decode(codec, m->in_buf, (size_t)csize, m->out_buf, (size_t)usize, chunk)) {
            return false;
        }
        if (!wim_sink_put(s, m->out_buf, (size_t)usize)) return false;
        prev = end;
    }
    return true;
}

static bool wim_read_chunked(wim_model *m, uint64_t offset, uint64_t packed, uint64_t size, wim_sink *s)
{
    size_t capacity = m->io_capacity;
    uint8_t *table = (uint8_t *)xx_mem_alloc(capacity);
    bool ok = table && wim_read_chunked_buffered(m, offset, packed, size, s, table, capacity);
    xx_mem_free(table);
    return ok;
}

/* Only one solid resource keeps its chunk table at a time (members are
 * extracted in run order, so each is loaded once), and the tables loaded
 * over the model's life share a budget: resources that all point at the
 * same bytes cannot multiply a 16 MiB table into gigabytes, or into
 * endless rereads. */
static bool wim_solid_load(wim_model *m, size_t index)
{
    wim_solid *sd = &m->solids[index];
    uint8_t sizes[4096];
    uint64_t i, sum = 0U, room, table_bytes, table_end;
    if (sd->chunk_start) return true;
    if (sd->failed || !sd->usable || sd->chunk_count == 0U) return false;
    table_bytes = sd->chunk_count * 4U;
    if (m->table_solid < m->solid_count && m->table_solid != index && m->solids[m->table_solid].chunk_start) {
        xx_mem_free(m->solids[m->table_solid].chunk_start);
        m->solids[m->table_solid].chunk_start = NULL;
    }
    m->table_solid = (size_t)-1;
    if (table_bytes > m->table_budget) {
        sd->failed = true;
        return false;
    }
    m->table_budget -= table_bytes;
    room = sd->packed - WIM_SOLID_HEADER - table_bytes;
    sd->chunk_start = (uint64_t *)xx_mem_alloc((size_t)(sd->chunk_count + 1U) * sizeof(uint64_t));
    if (!sd->chunk_start) {
        sd->failed = true;
        return false;
    }
    table_end = sd->offset + WIM_SOLID_HEADER + table_bytes;
    for (i = 0U; i < sd->chunk_count; ++i) {
        size_t slot = (size_t)(i % (sizeof(sizes) / 4U));
        uint64_t csize, usize;
        if (slot == 0U) {
            uint64_t left = (sd->chunk_count - i) * 4U;
            size_t want = left < sizeof(sizes) ? (size_t)left : sizeof(sizes);
            if (!wim_read_ra(m, sd->offset + WIM_SOLID_HEADER + i * 4U, sizes, want, table_end)) goto fail;
        }
        csize = xx_data_get_u32(sizes + slot * 4U, 4, 0, false);
        usize = i + 1U < sd->chunk_count ? sd->chunk_size : sd->usize - i * sd->chunk_size;
        sd->chunk_start[i] = sum;
        if (csize == 0U || csize > usize || csize > room - sum) goto fail;
        sum += csize;
    }
    sd->chunk_start[sd->chunk_count] = sum;
    m->table_solid = index;
    return true;
fail:
    xx_mem_free(sd->chunk_start);
    sd->chunk_start = NULL;
    sd->failed = true;
    return false;
}

static bool wim_solid_chunk(wim_model *m, size_t index, uint64_t chunk)
{
    wim_solid *sd = &m->solids[index];
    uint64_t csize, usize, where;
    if (m->cache_valid && m->cache_solid == index && m->cache_chunk == chunk) return true;
    m->cache_valid = false;
    if (chunk >= sd->chunk_count || !wim_solid_load(m, index)) return false;
    csize = sd->chunk_start[chunk + 1U] - sd->chunk_start[chunk];
    usize = chunk + 1U < sd->chunk_count ? sd->chunk_size : sd->usize - chunk * sd->chunk_size;
    where = sd->offset + WIM_SOLID_HEADER + sd->chunk_count * 4U + sd->chunk_start[chunk];
    if (!wim_grow(m, &m->cache, &m->cache_cap, (size_t)usize)) return false;
    if (csize == usize) {
        if (!wim_read_rel(m, where, m->cache, (size_t)usize)) return false;
    } else {
        xx_wim_codec *codec = wim_codec(m, sd->codec);
        if (!codec || !wim_grow(m, &m->in_buf, &m->in_cap, (size_t)csize) || !wim_read_rel(m, where, m->in_buf, (size_t)csize) ||
            !xx_wim_codec_decode(codec, m->in_buf, (size_t)csize, m->cache, (size_t)usize, sd->chunk_size))
            return false;
    }
    m->cache_len = (size_t)usize;
    m->cache_solid = index;
    m->cache_chunk = chunk;
    m->cache_valid = true;
    return true;
}

static bool wim_read_solid(wim_model *m, const wim_blob *b, wim_sink *s)
{
    const wim_run *run = &m->runs[b->solid_run];
    uint64_t at = b->offset, left = b->size;
    size_t lo = run->first, hi = (size_t)run->first + run->usable, i;
    /* The usable resources are laid end to end: find the first one that
     * ends past the blob's start. */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        const wim_solid *sd = &m->solids[mid];
        if (sd->run_base + sd->usize > at) hi = mid;
        else lo = mid + 1U;
    }
    for (i = lo; i < (size_t)run->first + run->usable && left != 0U; ++i) {
        wim_solid *sd = &m->solids[i];
        uint64_t rel;
        if (at >= sd->run_base + sd->usize) continue;
        if (at < sd->run_base) return false;
        rel = at - sd->run_base;
        while (left != 0U && rel < sd->usize) {
            uint64_t chunk = rel / sd->chunk_size;
            size_t inside = (size_t)(rel - chunk * sd->chunk_size), take;
            if (!wim_solid_chunk(m, i, chunk) || inside >= m->cache_len) return false;
            take = m->cache_len - inside;
            if ((uint64_t)take > left) take = (size_t)left;
            if (!wim_sink_put(s, m->cache + inside, take)) return false;
            left -= take;
            rel += take;
            at += take;
        }
    }
    return left == 0U;
}

static bool wim_read_blob(wim_model *m, const wim_blob *b, wim_sink *s)
{
    bool ok;
    if (!b->present) return false;
    if (b->solid_run != WIM_NONE) {
        ok = wim_read_solid(m, b, s);
    } else if ((b->flags & WIM_RES_COMPRESSED) == 0U) {
        ok = b->packed == b->size && wim_copy_to_sink(m, b->offset, b->size, s);
    } else {
        ok = wim_read_chunked(m, b->offset, b->packed, b->size, s);
    }
    return ok && s->written == b->size;
}

/* A stored resource is the container's own bytes, so when it is read into
 * memory (metadata, XML, link targets) a stale SHA-1 is tolerated: the data
 * is parsed with full bounds checks anyway, and boot-screen tools patch
 * winload's WIM in place leaving hashes stale.  A resource that went through
 * a decoder must match, because a mismatch means the decoded bytes are
 * wrong.  Member extraction (wim_extract) is stricter: any mismatch fails,
 * like a CRC mismatch in the ZIP reader and 7-Zip's "CRC Failed". */
static bool wim_hash_required(const wim_blob *b)
{
    return b->solid_run != WIM_NONE || (b->flags & WIM_RES_COMPRESSED) != 0U;
}

static bool wim_hash_ok(const wim_blob *b, wim_sink *s, bool *stale)
{
    uint8_t digest[WIM_HASH_SIZE];
    *stale = false;
    if (!s->hashing) return true;
    s->hashing = false;
    if (!xx_hash_final(&s->sha, digest, sizeof(digest))) return false;
    if (xx_rt_memcmp(digest, b->hash, WIM_HASH_SIZE) == 0) return true;
    *stale = true;
    return !wim_hash_required(b);
}

static bool wim_read_resource_memory(wim_model *m, const wim_blob *blob, uint64_t limit, uint8_t **out)
{
    wim_sink s;
    bool stale;
    *out = NULL;
    if (blob->size > limit || blob->size > (uint64_t)SIZE_MAX - 1U) return false;
    xx_mem_zero(&s, sizeof(s));
    s.limit = (size_t)blob->size;
    s.capacity = s.limit < 0x10000U ? s.limit : 0x10000U;
    s.memory = (uint8_t *)xx_mem_alloc(s.capacity + 1U);
    if (!s.memory) return false;
    s.pd = m->pd;
    s.io_capacity = m->io_capacity;
    s.hashing = !wim_is_zero_hash(blob->hash) && xx_hash_init(&s.sha, XX_HASH_SHA1);
    if (!wim_read_blob(m, blob, &s) || !wim_hash_ok(blob, &s, &stale)) {
        xx_mem_free(s.memory);
        return false;
    }
    *out = s.memory;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

typedef struct wim_fold_range_s {
    uint32_t first;
    uint32_t last;
    int32_t delta;
    uint32_t step;
} wim_fold_range;

/* Code points with a case twin, and the value both map to.  fold(c) is
 * upper(lower(c)) using every single-code-point mapping of Unicode 15.1,
 * so it merges at least what NTFS's upcase table (and simple case
 * folding) merges: over-merging only costs a "_<n>" rename, while
 * under-merging would let one member overwrite another.  Entries are
 * {first, last, delta, step}. */
static const wim_fold_range wim_fold_ranges[] = {
    {0xB5, 0xB5, 743, 1},        {0xE0, 0xF6, -32, 1},        {0xF8, 0xFE, -32, 1},        {0xFF, 0xFF, 121, 1},       {0x101, 0x12F, -1, 2},
    {0x131, 0x131, -232, 1},     {0x133, 0x137, -1, 2},       {0x13A, 0x148, -1, 2},       {0x14B, 0x177, -1, 2},      {0x17A, 0x17E, -1, 2},
    {0x17F, 0x17F, -300, 1},     {0x180, 0x180, 195, 1},      {0x183, 0x185, -1, 2},       {0x188, 0x188, -1, 1},      {0x18C, 0x18C, -1, 1},
    {0x192, 0x192, -1, 1},       {0x195, 0x195, 97, 1},       {0x199, 0x199, -1, 1},       {0x19A, 0x19A, 163, 1},     {0x19E, 0x19E, 130, 1},
    {0x1A1, 0x1A5, -1, 2},       {0x1A8, 0x1A8, -1, 1},       {0x1AD, 0x1AD, -1, 1},       {0x1B0, 0x1B0, -1, 1},      {0x1B4, 0x1B6, -1, 2},
    {0x1B9, 0x1B9, -1, 1},       {0x1BD, 0x1BD, -1, 1},       {0x1BF, 0x1BF, 56, 1},       {0x1C5, 0x1C5, -1, 1},      {0x1C6, 0x1C6, -2, 1},
    {0x1C8, 0x1C8, -1, 1},       {0x1C9, 0x1C9, -2, 1},       {0x1CB, 0x1CB, -1, 1},       {0x1CC, 0x1CC, -2, 1},      {0x1CE, 0x1DC, -1, 2},
    {0x1DD, 0x1DD, -79, 1},      {0x1DF, 0x1EF, -1, 2},       {0x1F2, 0x1F2, -1, 1},       {0x1F3, 0x1F3, -2, 1},      {0x1F5, 0x1F5, -1, 1},
    {0x1F9, 0x21F, -1, 2},       {0x223, 0x233, -1, 2},       {0x23C, 0x23C, -1, 1},       {0x23F, 0x240, 10815, 1},   {0x242, 0x242, -1, 1},
    {0x247, 0x24F, -1, 2},       {0x250, 0x250, 10783, 1},    {0x251, 0x251, 10780, 1},    {0x252, 0x252, 10782, 1},   {0x253, 0x253, -210, 1},
    {0x254, 0x254, -206, 1},     {0x256, 0x257, -205, 1},     {0x259, 0x259, -202, 1},     {0x25B, 0x25B, -203, 1},    {0x25C, 0x25C, 42319, 1},
    {0x260, 0x260, -205, 1},     {0x261, 0x261, 42315, 1},    {0x263, 0x263, -207, 1},     {0x265, 0x265, 42280, 1},   {0x266, 0x266, 42308, 1},
    {0x268, 0x268, -209, 1},     {0x269, 0x269, -211, 1},     {0x26A, 0x26A, 42308, 1},    {0x26B, 0x26B, 10743, 1},   {0x26C, 0x26C, 42305, 1},
    {0x26F, 0x26F, -211, 1},     {0x271, 0x271, 10749, 1},    {0x272, 0x272, -213, 1},     {0x275, 0x275, -214, 1},    {0x27D, 0x27D, 10727, 1},
    {0x280, 0x280, -218, 1},     {0x282, 0x282, 42307, 1},    {0x283, 0x283, -218, 1},     {0x287, 0x287, 42282, 1},   {0x288, 0x288, -218, 1},
    {0x289, 0x289, -69, 1},      {0x28A, 0x28B, -217, 1},     {0x28C, 0x28C, -71, 1},      {0x292, 0x292, -219, 1},    {0x29D, 0x29D, 42261, 1},
    {0x29E, 0x29E, 42258, 1},    {0x345, 0x345, 84, 1},       {0x371, 0x373, -1, 2},       {0x377, 0x377, -1, 1},      {0x37B, 0x37D, 130, 1},
    {0x3AC, 0x3AC, -38, 1},      {0x3AD, 0x3AF, -37, 1},      {0x3B1, 0x3C1, -32, 1},      {0x3C2, 0x3C2, -31, 1},     {0x3C3, 0x3CB, -32, 1},
    {0x3CC, 0x3CC, -64, 1},      {0x3CD, 0x3CE, -63, 1},      {0x3D0, 0x3D0, -62, 1},      {0x3D1, 0x3D1, -57, 1},     {0x3D5, 0x3D5, -47, 1},
    {0x3D6, 0x3D6, -54, 1},      {0x3D7, 0x3D7, -8, 1},       {0x3D9, 0x3EF, -1, 2},       {0x3F0, 0x3F0, -86, 1},     {0x3F1, 0x3F1, -80, 1},
    {0x3F2, 0x3F2, 7, 1},        {0x3F3, 0x3F3, -116, 1},     {0x3F4, 0x3F4, -92, 1},      {0x3F5, 0x3F5, -96, 1},     {0x3F8, 0x3F8, -1, 1},
    {0x3FB, 0x3FB, -1, 1},       {0x430, 0x44F, -32, 1},      {0x450, 0x45F, -80, 1},      {0x461, 0x481, -1, 2},      {0x48B, 0x4BF, -1, 2},
    {0x4C2, 0x4CE, -1, 2},       {0x4CF, 0x4CF, -15, 1},      {0x4D1, 0x52F, -1, 2},       {0x561, 0x586, -48, 1},     {0x10D0, 0x10FA, 3008, 1},
    {0x10FD, 0x10FF, 3008, 1},   {0x13F8, 0x13FD, -8, 1},     {0x1C80, 0x1C80, -6254, 1},  {0x1C81, 0x1C81, -6253, 1}, {0x1C82, 0x1C82, -6244, 1},
    {0x1C83, 0x1C84, -6242, 1},  {0x1C85, 0x1C85, -6243, 1},  {0x1C86, 0x1C86, -6236, 1},  {0x1C87, 0x1C87, -6181, 1}, {0x1C88, 0x1C88, 35266, 1},
    {0x1D79, 0x1D79, 35332, 1},  {0x1D7D, 0x1D7D, 3814, 1},   {0x1D8E, 0x1D8E, 35384, 1},  {0x1E01, 0x1E95, -1, 2},    {0x1E9B, 0x1E9B, -59, 1},
    {0x1E9E, 0x1E9E, -7615, 1},  {0x1EA1, 0x1EFF, -1, 2},     {0x1F00, 0x1F07, 8, 1},      {0x1F10, 0x1F15, 8, 1},     {0x1F20, 0x1F27, 8, 1},
    {0x1F30, 0x1F37, 8, 1},      {0x1F40, 0x1F45, 8, 1},      {0x1F51, 0x1F57, 8, 2},      {0x1F60, 0x1F67, 8, 1},     {0x1F70, 0x1F71, 74, 1},
    {0x1F72, 0x1F75, 86, 1},     {0x1F76, 0x1F77, 100, 1},    {0x1F78, 0x1F79, 128, 1},    {0x1F7A, 0x1F7B, 112, 1},   {0x1F7C, 0x1F7D, 126, 1},
    {0x1F88, 0x1F8F, -8, 1},     {0x1F98, 0x1F9F, -8, 1},     {0x1FA8, 0x1FAF, -8, 1},     {0x1FB0, 0x1FB1, 8, 1},     {0x1FBC, 0x1FBC, -9, 1},
    {0x1FBE, 0x1FBE, -7205, 1},  {0x1FCC, 0x1FCC, -9, 1},     {0x1FD0, 0x1FD1, 8, 1},      {0x1FE0, 0x1FE1, 8, 1},     {0x1FE5, 0x1FE5, 7, 1},
    {0x1FFC, 0x1FFC, -9, 1},     {0x2126, 0x2126, -7549, 1},  {0x212A, 0x212A, -8415, 1},  {0x212B, 0x212B, -8294, 1}, {0x214E, 0x214E, -28, 1},
    {0x2170, 0x217F, -16, 1},    {0x2184, 0x2184, -1, 1},     {0x24D0, 0x24E9, -26, 1},    {0x2C30, 0x2C5F, -48, 1},   {0x2C61, 0x2C61, -1, 1},
    {0x2C65, 0x2C65, -10795, 1}, {0x2C66, 0x2C66, -10792, 1}, {0x2C68, 0x2C6C, -1, 2},     {0x2C73, 0x2C73, -1, 1},    {0x2C76, 0x2C76, -1, 1},
    {0x2C81, 0x2CE3, -1, 2},     {0x2CEC, 0x2CEE, -1, 2},     {0x2CF3, 0x2CF3, -1, 1},     {0x2D00, 0x2D25, -7264, 1}, {0x2D27, 0x2D27, -7264, 1},
    {0x2D2D, 0x2D2D, -7264, 1},  {0xA641, 0xA66D, -1, 2},     {0xA681, 0xA69B, -1, 2},     {0xA723, 0xA72F, -1, 2},    {0xA733, 0xA76F, -1, 2},
    {0xA77A, 0xA77C, -1, 2},     {0xA77F, 0xA787, -1, 2},     {0xA78C, 0xA78C, -1, 1},     {0xA791, 0xA793, -1, 2},    {0xA794, 0xA794, 48, 1},
    {0xA797, 0xA7A9, -1, 2},     {0xA7B5, 0xA7C3, -1, 2},     {0xA7C8, 0xA7CA, -1, 2},     {0xA7D1, 0xA7D1, -1, 1},    {0xA7D7, 0xA7D9, -1, 2},
    {0xA7F6, 0xA7F6, -1, 1},     {0xAB53, 0xAB53, -928, 1},   {0xAB70, 0xABBF, -38864, 1}, {0xFF41, 0xFF5A, -32, 1},   {0x10428, 0x1044F, -40, 1},
    {0x104D8, 0x104FB, -40, 1},  {0x10597, 0x105A1, -39, 1},  {0x105A3, 0x105B1, -39, 1},  {0x105B3, 0x105B9, -39, 1}, {0x105BB, 0x105BC, -39, 1},
    {0x10CC0, 0x10CF2, -64, 1},  {0x118C0, 0x118DF, -32, 1},  {0x16E60, 0x16E7F, -32, 1},  {0x1E922, 0x1E943, -34, 1},
};

static uint32_t wim_fold_ascii(uint32_t cp)
{
    return cp >= 'a' && cp <= 'z' ? cp - 0x20U : cp;
}

static uint32_t wim_fold(uint32_t cp)
{
    size_t lo = 0U, hi = sizeof(wim_fold_ranges) / sizeof(wim_fold_ranges[0]);
    if (cp < 0x80U) return wim_fold_ascii(cp);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        const wim_fold_range *r = &wim_fold_ranges[mid];
        if (cp < r->first) hi = mid;
        else if (cp > r->last) lo = mid + 1U;
        else return (cp - r->first) % r->step == 0U ? (uint32_t)((int32_t)cp + r->delta) : cp;
    }
    return cp;
}

/* FNV-1a over the directory id and the case-folded code points.  Names
 * come from wim_component, so they are well-formed UTF-8. */
static uint64_t wim_name_key(uint32_t dir, const char *name)
{
    const uint8_t *p = (const uint8_t *)name;
    uint64_t h = UINT64_C(1469598103934665603);
    unsigned i;
    for (i = 0U; i < 4U; ++i) {
        h ^= (dir >> (8U * i)) & 0xFFU;
        h *= UINT64_C(1099511628211);
    }
    while (*p) {
        uint32_t cp = *p++;
        unsigned more = cp >= 0xF0U ? 3U : cp >= 0xE0U ? 2U : cp >= 0xC0U ? 1U : 0U;
        if (more != 0U) {
            cp &= 0x3FU >> more;
            while (more-- != 0U && (*p & 0xC0U) == 0x80U) cp = (cp << 6U) | (*p++ & 0x3FU);
        }
        cp = wim_fold(cp);
        for (i = 0U; i < 4U; ++i) {
            h ^= (cp >> (8U * i)) & 0xFFU;
            h *= UINT64_C(1099511628211);
        }
    }
    return h == 0U ? 1U : h;
}

/* The name keys are a public, unkeyed function of attacker-chosen names, so
 * the table slot must not be: an attacker could otherwise pick names whose
 * slots all fall into one probe cluster and make the member walk O(n^2).
 * The slot is a keyed 64-bit mixer (splitmix64 finaliser over key + secret
 * seed).  The seed comes from what the process has that the file cannot
 * predict (ASLR heap and stack addresses, the clock).  A probe that still
 * runs longer than WIM_NAME_PROBE_CAP (far beyond what linear probing at
 * load <= 1/2 produces for random slots) rebuilds the table under a fresh
 * seed, at most WIM_NAME_MAX_RESEEDS times per model.  Slot placement never
 * changes which names are taken, so output does not depend on the seed. */
#define WIM_NAME_PROBE_CAP 256U
#define WIM_NAME_MAX_RESEEDS 16U

static uint64_t wim_mix64(uint64_t x)
{
    x ^= x >> 30U;
    x *= UINT64_C(0xBF58476D1CE4E5B9);
    x ^= x >> 27U;
    x *= UINT64_C(0x94D049BB133111EB);
    x ^= x >> 31U;
    return x;
}

static void wim_name_new_seed(wim_model *m)
{
    uint64_t local = 0U;
    uint64_t s = m->name_seed;
    s = wim_mix64(s + UINT64_C(0x9E3779B97F4A7C15) + (uint64_t)(uintptr_t)m);
    s = wim_mix64(s ^ (uint64_t)(uintptr_t)&local);
    s = wim_mix64(s ^ (uint64_t)xx_rt_clock_ms());
    s = wim_mix64(s ^ (uint64_t)(uintptr_t)m->names);
    s = wim_mix64(s ^ ((uint64_t)m->name_reseeds << 32U) ^ m->name_count);
    m->name_seed = s | 1U;
}

static size_t wim_name_slot(const wim_model *m, uint64_t key, size_t mask)
{
    return (size_t)wim_mix64(key * m->name_seed + (m->name_seed >> 17U)) & mask;
}

/* Moves every key of the current table into a new one of @p cap slots.
 * Returns 1 on success, 0 when allocation fails and -1 when a probe in the
 * new table exceeds the cap (the caller may reseed and retry); on 0 and -1
 * the old table is kept. */
static int wim_name_rebuild(wim_model *m, size_t cap)
{
    size_t i;
    uint64_t *grown = (uint64_t *)xx_mem_calloc(cap, sizeof(uint64_t));
    if (!grown) return 0;
    for (i = 0U; m->names && i <= m->name_mask; ++i) {
        if (m->names[i] != 0U) {
            size_t j = wim_name_slot(m, m->names[i], cap - 1U), probe = 0U;
            while (grown[j] != 0U) {
                if (++probe > WIM_NAME_PROBE_CAP && m->name_reseeds < WIM_NAME_MAX_RESEEDS) {
                    xx_mem_free(grown);
                    return -1;
                }
                j = (j + 1U) & (cap - 1U);
            }
            grown[j] = m->names[i];
        }
    }
    if (m->names) xx_mem_free(m->names);
    m->names = grown;
    m->name_mask = cap - 1U;
    return 1;
}

/* Rebuilds at @p cap slots, reseeding while the placement probes too long.
 * Once the reseed allowance is spent the rebuild accepts long probes. */
static bool wim_name_rehash(wim_model *m, size_t cap)
{
    for (;;) {
        int built = wim_name_rebuild(m, cap);
        if (built > 0) return true;
        if (built == 0 || m->name_reseeds >= WIM_NAME_MAX_RESEEDS) return false;
        /* the rebuild refused a long probe; try another seed */
        ++m->name_reseeds;
        wim_name_new_seed(m);
    }
}

static bool wim_name_claim(wim_model *m, uint32_t dir, const char *name)
{
    uint64_t key = wim_name_key(dir, name);
    for (;;) {
        size_t at, probe = 0U;
        if (!m->names) {
            wim_name_new_seed(m);
            if (!wim_name_rehash(m, 1024U)) return false;
        } else if (m->name_count * 2U >= m->name_mask + 1U) {
            if (!wim_name_rehash(m, (m->name_mask + 1U) * 2U)) return false;
        }
        at = wim_name_slot(m, key, m->name_mask);
        while (m->names[at] != 0U) {
            if (m->names[at] == key) return false;
            if (++probe > WIM_NAME_PROBE_CAP && m->name_reseeds < WIM_NAME_MAX_RESEEDS) break;
            at = (at + 1U) & m->name_mask;
        }
        if (m->names[at] == 0U) {
            m->names[at] = key;
            ++m->name_count;
            return true;
        }
        /* over-long probe: rebuild the same-size table under a new seed */
        ++m->name_reseeds;
        wim_name_new_seed(m);
        if (!wim_name_rehash(m, m->name_mask + 1U)) return false;
    }
}

static bool wim_is_device_name(const char *name)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, i;
    while (name[stem] && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i) {
        const char *d = devices[i];
        size_t k = 0U;
        while (k < stem && d[k] && (char)wim_fold_ascii((unsigned char)name[k]) == d[k]) ++k;
        if (k == stem && d[k] == 0) return true;
    }
    if (stem >= 4U) {
        bool com = wim_fold_ascii((unsigned char)name[0]) == 'C' && wim_fold_ascii((unsigned char)name[1]) == 'O' && wim_fold_ascii((unsigned char)name[2]) == 'M';
        bool lpt = wim_fold_ascii((unsigned char)name[0]) == 'L' && wim_fold_ascii((unsigned char)name[1]) == 'P' && wim_fold_ascii((unsigned char)name[2]) == 'T';
        if (com || lpt) {
            if (stem == 4U && name[3] >= '0' && name[3] <= '9') return true;
            /* superscript one, two and three */
            if (stem == 5U && (unsigned char)name[3] == 0xC2U && ((unsigned char)name[4] == 0xB9U || (unsigned char)name[4] == 0xB2U || (unsigned char)name[4] == 0xB3U))
                return true;
        }
    }
    return false;
}

/* One path component from UTF-16LE: separators, reserved punctuation and
 * control characters become '_', trailing dots and spaces go (Windows would
 * drop them), "." and ".." cannot survive that, and device names get a '_'
 * prefix.  The result is always a non-empty, safe component. */
static char *wim_component(const uint8_t *utf16, size_t units)
{
    size_t i = 0U, n = 0U;
    char *s;
    if (units > WIM_MAX_COMPONENT_UNITS) units = WIM_MAX_COMPONENT_UNITS;
    s = (char *)xx_mem_alloc(units * 3U + 3U);
    if (!s) return NULL;
    s[n++] = '_'; /* room for a device prefix; dropped below if unused */
    while (i < units) {
        uint32_t cp = xx_data_get_u16(utf16 + 2U * i++, 2, 0, false);
        if (cp >= 0xD800U && cp <= 0xDBFFU && i < units) {
            uint32_t lo = xx_data_get_u16(utf16 + 2U * i, 2, 0, false);
            if (lo >= 0xDC00U && lo <= 0xDFFFU) {
                cp = 0x10000U + ((cp - 0xD800U) << 10U) + (lo - 0xDC00U);
                ++i;
            } else {
                cp = 0xFFFDU;
            }
        } else if (cp >= 0xD800U && cp <= 0xDFFFU) {
            cp = 0xFFFDU;
        }
        if (cp < 0x20U || cp == '/' || cp == '\\' || cp == ':' || cp == '*' || cp == '?' || cp == '"' || cp == '<' || cp == '>' || cp == '|') cp = '_';
        if (cp < 0x80U) {
            s[n++] = (char)cp;
        } else if (cp < 0x800U) {
            s[n++] = (char)(0xC0U | (cp >> 6U));
            s[n++] = (char)(0x80U | (cp & 0x3FU));
        } else if (cp < 0x10000U) {
            s[n++] = (char)(0xE0U | (cp >> 12U));
            s[n++] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
            s[n++] = (char)(0x80U | (cp & 0x3FU));
        } else {
            s[n++] = (char)(0xF0U | (cp >> 18U));
            s[n++] = (char)(0x80U | ((cp >> 12U) & 0x3FU));
            s[n++] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
            s[n++] = (char)(0x80U | (cp & 0x3FU));
        }
    }
    while (n > 1U && (s[n - 1U] == '.' || s[n - 1U] == ' ')) --n;
    if (n == 1U) s[n++] = '_';
    s[n] = 0;
    if (!wim_is_device_name(s + 1)) xx_mem_move(s, s + 1, n);
    return s;
}

/* Appends a name component to the pool and returns its offset, or WIM_NONE
 * once the pool is full.  The last WIM_POOL_RESERVE bytes are kept for the
 * names given at the very end (wim.xml). */
#define WIM_POOL_RESERVE 256U

static uint32_t wim_pool_add(wim_model *m, const char *text, bool reserved)
{
    size_t len = xx_str_len(text) + 1U, offset;
    size_t limit = reserved ? WIM_MAX_NAME_BYTES : WIM_MAX_NAME_BYTES - WIM_POOL_RESERVE;
    if ((!reserved && m->pool_full) || m->pool_len > limit || len > limit - m->pool_len) {
        m->pool_full = true;
        return WIM_NONE;
    }
    if (len > m->pool_cap - m->pool_len) {
        size_t cap = m->pool_cap != 0U ? m->pool_cap : 65536U;
        char *grown;
        while (cap - m->pool_len < len) cap = cap > WIM_MAX_NAME_BYTES / 2U ? WIM_MAX_NAME_BYTES : cap * 2U;
        grown = (char *)xx_mem_realloc(m->pool, cap);
        if (!grown) {
            m->pool_full = true;
            return WIM_NONE;
        }
        m->pool = grown;
        m->pool_cap = cap;
    }
    offset = m->pool_len;
    xx_rt_memcpy(m->pool + offset, text, len);
    m->pool_len += len;
    return (uint32_t)offset;
}

/* The length of the path of a component of @p len bytes under @p parent,
 * or 0 when it would not fit in WIM_MAX_PATH. */
static size_t wim_path_len(const wim_model *m, uint32_t parent, size_t len)
{
    size_t total = parent == WIM_NONE ? len : (size_t)m->members[parent].path_len + 1U + len;
    return len == 0U || total + 1U > WIM_MAX_PATH ? 0U : total;
}

/* Builds a member's full path into the model's path buffer (valid until
 * the next call).  Parents always precede their children, and every step
 * consumes at least one byte of the recorded length, so this ends. */
static const char *wim_member_path(wim_model *m, const wim_member *member)
{
    const wim_member *cur = member;
    size_t at = member->path_len;
    if (!m->path || at == 0U || at >= WIM_MAX_PATH) return NULL;
    m->path[at] = 0;
    for (;;) {
        const char *component = m->pool + cur->name;
        size_t len = xx_str_len(component);
        if (len == 0U || len > at) return NULL;
        at -= len;
        xx_rt_memcpy(m->path + at, component, len);
        if (cur->parent == WIM_NONE) break;
        if (at == 0U || cur->parent >= m->member_count || &m->members[cur->parent] >= cur) return NULL;
        m->path[--at] = '/';
        cur = &m->members[cur->parent];
    }
    return at == 0U ? m->path : NULL;
}

/* Fills in a member's name: @p component (still owned by the caller)
 * under @p parent.  False when the path is too long or the pool is full;
 * *full tells the two apart. */
static bool wim_set_name(wim_model *m, wim_member *member, uint32_t parent, const char *component, bool *full)
{
    size_t len = wim_path_len(m, parent, xx_str_len(component));
    uint32_t at;
    *full = false;
    if (len == 0U) return false;
    at = wim_pool_add(m, component, false);
    if (at == WIM_NONE) {
        *full = true;
        return false;
    }
    member->name = at;
    member->parent = parent;
    member->path_len = (uint16_t)len;
    return true;
}

/* Claims @p component in directory @p dir, or a variant of it that carries
 * @p serial when the name (or its case-folded twin) is taken.  Takes
 * ownership of @p component. */
static char *wim_unique(wim_model *m, uint32_t dir, char *component, size_t serial)
{
    size_t len, dot, attempt;
    if (!component) return NULL;
    if (wim_name_claim(m, dir, component)) return component;
    len = xx_str_len(component);
    dot = len;
    while (dot > 1U && component[dot - 1U] != '.') --dot;
    dot = (dot > 1U && len - dot < 16U) ? dot - 1U : len;
    for (attempt = 0U; attempt < 8U; ++attempt) {
        char digits[48];
        char *variant;
        int written = attempt == 0U ? xx_rt_snprintf(digits, sizeof(digits), "_%u", (unsigned)serial)
                                    : xx_rt_snprintf(digits, sizeof(digits), "_%u_%u", (unsigned)serial, (unsigned)attempt);
        size_t extra;
        if (written <= 0) break;
        extra = (size_t)written;
        variant = (char *)xx_mem_alloc(len + extra + 1U);
        if (!variant) break;
        xx_rt_memcpy(variant, component, dot);
        xx_rt_memcpy(variant + dot, digits, extra);
        xx_rt_memcpy(variant + dot + extra, component + dot, len - dot + 1U);
        if (wim_name_claim(m, dir, variant)) {
            xx_mem_free(component);
            return variant;
        }
        xx_mem_free(variant);
    }
    xx_mem_free(component);
    return NULL;
}

static char *wim_make_name(const char *prefix, uint32_t index, const char *suffix)
{
    char buffer[64];
    int written = xx_rt_snprintf(buffer, sizeof(buffer), "%s%04u%s", prefix, (unsigned)index, suffix);
    char *name;
    if (written <= 0 || (size_t)written >= sizeof(buffer)) return NULL;
    name = (char *)xx_mem_alloc((size_t)written + 1U);
    if (name) xx_rt_memcpy(name, buffer, (size_t)written + 1U);
    return name;
}

/* Extraction writes <base>/<name>.  Names were built safe; this re-checks
 * every component anyway. */
static bool wim_safe_output_name(const char *name)
{
    const char *segment, *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' || (name[0] && name[1] == ':')) return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' || (c != 0U && c < 0x20U)) return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            char piece[16];
            if (length == 0U || (length == 1U && segment[0] == '.') || (length == 2U && segment[0] == '.' && segment[1] == '.') || segment[length - 1U] == '.' ||
                segment[length - 1U] == ' ')
                return false;
            if (length < sizeof(piece)) {
                xx_rt_memcpy(piece, segment, length);
                piece[length] = 0;
                if (wim_is_device_name(piece)) return false;
            }
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Members                                                                 */

static uint32_t wim_add_member(wim_model *m, const wim_member *member)
{
    if (m->member_count >= WIM_MAX_MEMBERS) return WIM_NONE;
    if (m->member_count == m->member_cap) {
        size_t cap = m->member_cap ? m->member_cap * 2U : 64U;
        wim_member *grown;
        if (cap > WIM_MAX_MEMBERS) cap = WIM_MAX_MEMBERS;
        grown = (wim_member *)xx_mem_realloc(m->members, cap * sizeof(*grown));
        if (!grown) return WIM_NONE;
        m->members = grown;
        m->member_cap = cap;
    }
    m->members[m->member_count] = *member;
    return (uint32_t)m->member_count++;
}

/* Resolves a stream hash to a member's blob, size and availability. */
static void wim_attach(wim_model *m, wim_member *member, const uint8_t *hash)
{
    uint32_t index;
    member->blob = WIM_NONE;
    member->size = 0U;
    member->missing = false;
    if (!hash || wim_is_zero_hash(hash)) return;
    index = wim_find_blob(m, hash);
    if (index == WIM_NONE || !m->blobs[index].present) {
        member->missing = true;
        if (index != WIM_NONE) member->size = m->blobs[index].size;
        return;
    }
    member->blob = index;
    member->size = m->blobs[index].size;
    m->blobs[index].used = true;
}

typedef struct wim_frame_s {
    uint64_t cursor;
    uint32_t parent; /* member index of the directory, or WIM_NONE */
    uint32_t dir;
    uint32_t depth;
} wim_frame;

typedef struct wim_walk_s {
    wim_model *m;
    const uint8_t *meta;
    size_t size;
    uint8_t *seen; /* one bit per 8-byte unit of the metadata */
} wim_walk;

static bool wim_mark(wim_walk *w, uint64_t at)
{
    size_t unit = (size_t)(at >> 3U);
    uint8_t bit = (uint8_t)(1U << (unit & 7U));
    if ((w->seen[unit >> 3U] & bit) != 0U) return false;
    w->seen[unit >> 3U] |= bit;
    return true;
}

/* One extra stream entry at @p at: its aligned length, hash and name. */
static bool wim_stream_entry(wim_walk *w, uint64_t at, uint64_t *step, const uint8_t **hash, const uint8_t **name, size_t *name_bytes)
{
    uint64_t length, aligned;
    size_t bytes;
    if (at > w->size - 8U || (at & 7U) != 0U) return false;
    length = xx_data_get_u64(w->meta + at, 8, 0, false);
    if (length < WIM_STREAM_FIXED || length > w->size) return false;
    aligned = (length + 7U) & ~(uint64_t)7U;
    if (aligned > w->size - at || !wim_mark(w, at)) return false;
    bytes = xx_data_get_u16(w->meta + at + 0x24U, 2, 0, false);
    if ((uint64_t)WIM_STREAM_FIXED + bytes > length || (bytes & 1U) != 0U) return false;
    *step = aligned;
    *hash = w->meta + at + 0x10U;
    *name = w->meta + at + WIM_STREAM_FIXED;
    *name_bytes = bytes;
    return true;
}

/* The entry's streams in order: the entry's own hash, then each extra
 * entry.  The unnamed ones fill the reparse slot (for a reparse point) and
 * then the data slot; a zero-hash unnamed stream stands in for whichever is
 * still empty. */
typedef struct wim_streams_s {
    const uint8_t *data;
    const uint8_t *reparse;
    uint32_t named;
    uint64_t end;
} wim_streams;

static bool wim_classify(wim_walk *w, const uint8_t *dentry, uint64_t first, unsigned extra_count, bool reparse, wim_streams *out)
{
    static const uint8_t zero[WIM_HASH_SIZE] = {0};
    const uint8_t *zero_unnamed = NULL;
    bool found_rp = false, found_data = false;
    uint64_t at = first;
    unsigned i;
    out->data = NULL;
    out->reparse = NULL;
    out->named = 0U;
    for (i = 0U; i <= extra_count; ++i) {
        const uint8_t *hash, *name = NULL;
        size_t name_bytes = 0U;
        if (i == 0U) {
            hash = dentry + 0x40U;
        } else {
            uint64_t step;
            if (!wim_stream_entry(w, at, &step, &hash, &name, &name_bytes)) return false;
            at += step;
        }
        if (name_bytes != 0U) {
            ++out->named;
        } else if (i != 0U || !wim_is_zero_hash(hash)) {
            if (reparse && !found_rp) {
                found_rp = true;
                out->reparse = hash;
            } else if (!found_data) {
                found_data = true;
                out->data = hash;
            }
        } else if (!zero_unnamed) {
            zero_unnamed = hash;
        }
    }
    if (zero_unnamed) {
        if (reparse && !found_rp) out->reparse = zero;
        else if (!found_data) out->data = zero;
    }
    out->end = at;
    return true;
}

static bool wim_add_named_streams(wim_walk *w, uint64_t first, unsigned extra_count, uint32_t owner_parent, const char *owner_component, uint32_t owner_dir,
                                  uint64_t timestamp, uint32_t attributes)
{
    wim_model *m = w->m;
    uint64_t at = first;
    unsigned i;
    char *holder = NULL;
    uint32_t holder_dir = 0U;
    bool ok = true;
    for (i = 0U; i < extra_count && ok; ++i) {
        const uint8_t *hash, *name;
        size_t name_bytes, hl, cl;
        uint64_t step;
        wim_member member;
        char *component, *joined;
        bool full, named;
        /* Already validated by wim_classify; re-read without re-marking. */
        step = (xx_data_get_u64(w->meta + at, 8, 0, false) + 7U) & ~(uint64_t)7U;
        hash = w->meta + at + 0x10U;
        name = w->meta + at + WIM_STREAM_FIXED;
        name_bytes = xx_data_get_u16(w->meta + at + 0x24U, 2, 0, false);
        at += step;
        if (name_bytes == 0U) continue;
        if (!holder) {
            size_t len = xx_str_len(owner_component);
            holder = (char *)xx_mem_alloc(len + 13U);
            if (!holder) return false;
            xx_rt_memcpy(holder, owner_component, len);
            xx_rt_memcpy(holder + len, ".__streams__", 13U);
            holder = wim_unique(m, owner_dir, holder, m->member_count);
            if (!holder) return true;
            holder_dir = m->next_dir++;
        }
        component = wim_unique(m, holder_dir, wim_component(name, name_bytes / 2U), m->member_count);
        if (!component) continue;
        /* The holder directory is not a member of its own: the stream's
         * component is "<holder>/<name>" under the owner's parent. */
        hl = xx_str_len(holder);
        cl = xx_str_len(component);
        joined = (char *)xx_mem_alloc(hl + cl + 2U);
        if (!joined) {
            xx_mem_free(component);
            ok = false;
            break;
        }
        xx_rt_memcpy(joined, holder, hl);
        joined[hl] = '/';
        xx_rt_memcpy(joined + hl + 1U, component, cl + 1U);
        xx_mem_free(component);
        xx_mem_zero(&member, sizeof(member));
        named = wim_set_name(m, &member, owner_parent, joined, &full);
        xx_mem_free(joined);
        if (!named) {
            if (full) ok = false;
            continue;
        }
        member.kind = WIM_KIND_FILE;
        member.timestamp = timestamp;
        member.attributes = attributes & ~(WIM_ATTR_DIRECTORY | WIM_ATTR_REPARSE);
        wim_attach(m, &member, hash);
        if (wim_add_member(m, &member) == WIM_NONE) ok = false;
    }
    if (holder) xx_mem_free(holder);
    return ok;
}

/* Walks one image's directory tree, depth first, with an explicit stack.
 * Every entry and stream entry may be read once (a bitmap over the
 * metadata), so overlapping or cyclic lists end instead of looping. */
static bool wim_walk_image(wim_model *m, const uint8_t *meta, size_t size, uint32_t image_member, uint32_t image_dir)
{
    wim_walk w;
    wim_frame *stack = NULL;
    size_t depth = 0U, cap = 0U;
    uint64_t dir_start, root_sub;
    uint32_t total;
    bool ok = true;
    if (size < 8U) return false;
    total = xx_data_get_u32(meta, 4, 0, false);
    dir_start = ((uint64_t)(total < 8U ? 8U : total) + 7U) & ~(uint64_t)7U;
    if (dir_start > size || size - dir_start < WIM_DENTRY_FIXED) return false;
    if ((xx_data_get_u32(meta + dir_start + 8U, 4, 0, false) & WIM_ATTR_DIRECTORY) == 0U) return false;
    root_sub = xx_data_get_u64(meta + dir_start + 0x10U, 8, 0, false);
    if (root_sub == 0U) return true;
    w.m = m;
    w.meta = meta;
    w.size = size;
    w.seen = (uint8_t *)xx_mem_calloc((size >> 6U) + 2U, 1U);
    if (!w.seen) return false;
    (void)wim_mark(&w, dir_start);
    cap = 64U;
    stack = (wim_frame *)xx_mem_alloc(cap * sizeof(*stack));
    if (!stack) {
        xx_mem_free(w.seen);
        return false;
    }
    stack[0].cursor = root_sub;
    stack[0].parent = image_member;
    stack[0].dir = image_dir;
    stack[0].depth = 1U;
    depth = 1U;

    while (depth != 0U && ok) {
        wim_frame *top = &stack[depth - 1U];
        uint64_t at = top->cursor, length, aligned, sub, timestamp;
        const uint8_t *d;
        uint32_t attributes, tag, parent = top->parent, dir = top->dir, level = top->depth;
        unsigned extra_count, name_bytes;
        wim_streams streams;
        wim_member member;
        char *component;
        bool reparse, is_link, is_dir, full;
        uint32_t index;

        if (wim_stopped(m->pd)) {
            ok = false;
            break;
        }
        if (at > size - 8U || (at & 7U) != 0U) {
            --depth;
            continue;
        }
        length = xx_data_get_u64(meta + at, 8, 0, false);
        aligned = (length + 7U) & ~(uint64_t)7U;
        if (length > size || aligned <= 8U || length < WIM_DENTRY_FIXED || aligned > size - at || !wim_mark(&w, at)) {
            --depth; /* end of list, or a damaged one */
            continue;
        }
        d = meta + at;
        attributes = xx_data_get_u32(d + 8U, 4, 0, false);
        sub = xx_data_get_u64(d + 0x10U, 8, 0, false);
        timestamp = xx_data_get_u64(d + 0x38U, 8, 0, false);
        tag = xx_data_get_u32(d + 0x58U, 4, 0, false);
        extra_count = xx_data_get_u16(d + 0x60U, 2, 0, false);
        name_bytes = xx_data_get_u16(d + 0x64U, 2, 0, false);
        reparse = (attributes & WIM_ATTR_REPARSE) != 0U;
        if ((uint64_t)WIM_DENTRY_FIXED + name_bytes > length || (name_bytes & 1U) != 0U || !wim_classify(&w, d, at + aligned, extra_count, reparse, &streams)) {
            --depth;
            continue;
        }
        top->cursor = streams.end;
        if (name_bytes == 0U) continue; /* only the root may be nameless */

        is_link = reparse && (tag == WIM_TAG_SYMLINK || tag == WIM_TAG_MOUNT_POINT);
        is_dir = !is_link && (attributes & WIM_ATTR_DIRECTORY) != 0U;
        component = wim_unique(m, dir, wim_component(d + WIM_DENTRY_FIXED, name_bytes / 2U), m->member_count);
        if (!component) continue;
        xx_mem_zero(&member, sizeof(member));
        if (!wim_set_name(m, &member, parent, component, &full)) {
            xx_mem_free(component);
            if (!full) continue; /* path too long */
            ok = false;
            break;
        }
        member.timestamp = timestamp;
        member.attributes = attributes;
        member.reparse_tag = reparse ? tag : 0U;
        member.blob = WIM_NONE;
        if (is_dir) {
            member.kind = WIM_KIND_DIR;
        } else if (is_link) {
            member.kind = WIM_KIND_LINK;
            wim_attach(m, &member, streams.reparse);
        } else {
            member.kind = WIM_KIND_FILE;
            wim_attach(m, &member, streams.data);
            /* A reparse point that is not a link keeps its reparse data
             * to itself; it is metadata, not an orphan stream. */
            if (streams.reparse) {
                uint32_t rp = wim_find_blob(m, streams.reparse);
                if (rp != WIM_NONE) m->blobs[rp].used = true;
            }
        }
        index = wim_add_member(m, &member);
        if (index == WIM_NONE) {
            xx_mem_free(component);
            ok = false;
            break;
        }
        if (streams.named != 0U && !wim_add_named_streams(&w, at + aligned, extra_count, parent, component, dir, timestamp, attributes)) {
            xx_mem_free(component);
            ok = false;
            break;
        }
        xx_mem_free(component);
        if (is_dir && sub != 0U && level < WIM_MAX_DEPTH) {
            if (depth == cap) {
                wim_frame *grown;
                cap *= 2U;
                grown = (wim_frame *)xx_mem_realloc(stack, cap * sizeof(*stack));
                if (!grown) {
                    ok = false;
                    break;
                }
                stack = grown;
            }
            stack[depth].cursor = sub;
            stack[depth].parent = index;
            stack[depth].dir = m->next_dir++;
            stack[depth].depth = level + 1U;
            ++depth;
        }
    }
    xx_mem_free(stack);
    xx_mem_free(w.seen);
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Model                                                                   */

static void wim_model_free(void *opaque)
{
    wim_model *m = (wim_model *)opaque;
    size_t i;
    if (!m) return;
    for (i = 0U; i < m->solid_count; ++i)
        if (m->solids[i].chunk_start) xx_mem_free(m->solids[i].chunk_start);
    for (i = 0U; i < 4U; ++i) xx_wim_codec_free(m->codecs[i]);
    if (m->members) xx_mem_free(m->members);
    if (m->order) xx_mem_free(m->order);
    if (m->blobs) xx_mem_free(m->blobs);
    if (m->sorted) xx_mem_free(m->sorted);
    if (m->solids) xx_mem_free(m->solids);
    if (m->runs) xx_mem_free(m->runs);
    if (m->pool) xx_mem_free(m->pool);
    if (m->path) xx_mem_free(m->path);
    if (m->names) xx_mem_free(m->names);
    if (m->in_buf) xx_mem_free(m->in_buf);
    if (m->out_buf) xx_mem_free(m->out_buf);
    if (m->cache) xx_mem_free(m->cache);
    if (m->ra) xx_mem_free(m->ra);
    xx_mem_free(m);
}

/* Members that own a solid blob are extracted in blob order, so each solid
 * chunk is decoded once; everything else keeps the tree order. */
static uint64_t wim_order_key(const wim_model *m, uint32_t index, uint64_t *low)
{
    const wim_member *member = &m->members[index];
    const wim_blob *b;
    *low = index;
    if (member->blob == WIM_NONE || member->kind == WIM_KIND_XML) return 0U;
    b = &m->blobs[member->blob];
    if (b->solid_run == WIM_NONE) return 0U;
    *low = b->offset;
    return 1U + (uint64_t)b->solid_run;
}

static bool wim_order_less(const wim_model *m, uint32_t a, uint32_t b)
{
    uint64_t la, lb, ka = wim_order_key(m, a, &la), kb = wim_order_key(m, b, &lb);
    if (ka != kb) return ka < kb;
    if (ka == 0U) return a < b;
    if (la != lb) return la < lb;
    return a < b;
}

static bool wim_build_order(wim_model *m)
{
    uint32_t *tmp;
    size_t n = m->member_count, width, i;
    m->order = (uint32_t *)xx_mem_alloc((n + 1U) * sizeof(uint32_t));
    if (!m->order) return false;
    for (i = 0U; i < n; ++i) m->order[i] = (uint32_t)i;
    if (!m->has_solid || n < 2U) return true;
    tmp = (uint32_t *)xx_mem_alloc(n * sizeof(uint32_t));
    if (!tmp) return false;
    for (width = 1U; width < n; width *= 2U) {
        for (i = 0U; i < n; i += 2U * width) {
            size_t lo = i, mid = i + width < n ? i + width : n, hi = i + 2U * width < n ? i + 2U * width : n, a = lo, b = mid, k = lo;
            while (a < mid && b < hi) tmp[k++] = wim_order_less(m, m->order[b], m->order[a]) ? m->order[b++] : m->order[a++];
            while (a < mid) tmp[k++] = m->order[a++];
            while (b < hi) tmp[k++] = m->order[b++];
        }
        xx_rt_memcpy(m->order, tmp, n * sizeof(uint32_t));
    }
    xx_mem_free(tmp);
    return true;
}

static bool wim_build_members(wim_model *m)
{
    uint32_t *images = NULL;
    size_t image_total = 0U, i;
    uint32_t xml_member = WIM_NONE;
    uint64_t budget;
    wim_member member;
    bool full;

    if (m->xml_blob != WIM_NONE) {
        xx_mem_zero(&member, sizeof(member));
        member.kind = WIM_KIND_XML;
        member.blob = m->xml_blob;
        member.size = m->blobs[m->xml_blob].size;
        m->blobs[m->xml_blob].used = true;
        xml_member = wim_add_member(m, &member);
        if (xml_member == WIM_NONE) return false;
    }

    images = (uint32_t *)xx_mem_alloc((m->blob_count + 1U) * sizeof(uint32_t));
    if (!images) return false;
    for (i = 0U; i < m->blob_count; ++i) {
        const wim_blob *b = &m->blobs[i];
        if ((uint32_t)i == m->xml_blob || (b->flags & WIM_RES_METADATA) == 0U || b->refcount == 0U || !b->present || m->hdr.part > 1U ||
            image_total >= m->hdr.image_count)
            continue;
        images[image_total++] = (uint32_t)i;
    }
    m->next_dir = 1U;
    /* Every image costs a decode and a walk of its metadata.  Real images
     * each have their own resource, and even well-compressed metadata stays
     * below 8 times the file's size (a 64 MiB floor covers small files); a
     * table that points thousands of image entries at the same (or
     * overlapping) bytes, or a small file that declares huge metadata, is
     * stopped by this budget instead of being decoded and walked without
     * end.  Each member costs at least 0x28 bytes of decoded metadata (an
     * entry is read once per image), so the budget also bounds the members. */
    budget = (uint64_t)m->avail * 8U + WIM_METADATA_FLOOR;
    for (i = 0U; i < image_total; ++i) {
        wim_blob *b = &m->blobs[images[i]];
        uint8_t *meta = NULL;
        uint32_t image_member = WIM_NONE, image_dir = 0U;
        size_t first = m->member_count, first_pool = m->pool_len;
        if (wim_stopped(m->pd) || m->member_count >= WIM_MAX_MEMBERS || m->pool_full) break;
        if (b->size > budget) continue;
        budget -= b->size;
        if (!wim_read_resource_memory(m, b, WIM_MAX_METADATA, &meta)) continue;
        if (image_total > 1U) {
            char label[16];
            char *name;
            (void)xx_rt_snprintf(label, sizeof(label), "%u", (unsigned)(i + 1U));
            name = wim_unique(m, 0U, xx_str_dup(label), m->member_count);
            if (name) {
                xx_mem_zero(&member, sizeof(member));
                if (wim_set_name(m, &member, WIM_NONE, name, &full)) {
                    member.kind = WIM_KIND_DIR;
                    member.attributes = WIM_ATTR_DIRECTORY;
                    member.blob = WIM_NONE;
                    image_member = wim_add_member(m, &member);
                }
                xx_mem_free(name);
                if (image_member == WIM_NONE) {
                    xx_mem_free(meta);
                    break;
                }
                image_dir = m->next_dir++;
            }
        }
        if (image_total > 1U && image_member == WIM_NONE) {
            xx_mem_free(meta);
            continue;
        }
        if (wim_walk_image(m, meta, (size_t)b->size, image_member, image_dir)) {
            b->used = true;
        } else if (m->member_count == first + (image_member != WIM_NONE)) {
            /* Nothing usable came out of it: offer the raw resource. */
            m->member_count = first;
            m->pool_len = first_pool;
        } else {
            b->used = true;
        }
        xx_mem_free(meta);
    }
    xx_mem_free(images);

    /* Blobs no image references: split parts, damaged metadata, free
     * entries.  Named by their offset table index. */
    for (i = 0U; i < m->blob_count; ++i) {
        wim_blob *b = &m->blobs[i];
        char *name;
        if ((uint32_t)i == m->xml_blob || b->used || !b->present) continue;
        name = wim_make_name((b->flags & WIM_RES_METADATA) ? "metadata_" : "stream_", b->table_index, ".bin");
        name = wim_unique(m, 0U, name, m->member_count);
        if (!name) continue;
        xx_mem_zero(&member, sizeof(member));
        if (!wim_set_name(m, &member, WIM_NONE, name, &full)) {
            xx_mem_free(name);
            if (full) break;
            continue;
        }
        xx_mem_free(name);
        member.kind = WIM_KIND_STREAM;
        member.blob = (uint32_t)i;
        member.size = b->size;
        b->used = true;
        if (wim_add_member(m, &member) == WIM_NONE) break;
    }

    if (xml_member != WIM_NONE) {
        wim_member *xml = &m->members[xml_member];
        char *name = wim_unique(m, 0U, xx_str_dup("wim.xml"), m->member_count);
        uint32_t at;
        if (!name) name = wim_make_name("wim_", xml_member, ".xml");
        if (!name) return false;
        at = wim_pool_add(m, name, true);
        xml->parent = WIM_NONE;
        xml->path_len = (uint16_t)xx_str_len(name);
        xx_mem_free(name);
        if (at == WIM_NONE) return false;
        xml->name = at;
    }
    return wim_build_order(m);
}

static bool wim_open(Abstractformat *format, bool full, xx_pd_struct *pd, wim_model **result)
{
    wim_model *m;
    int64_t total;
    *result = NULL;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    m = (wim_model *)xx_mem_calloc(1U, sizeof(*m));
    if (!m) return false;
    m->device = format->device;
    m->base = format->base_address;
    m->avail = total - format->base_address;
    m->xml_blob = WIM_NONE;
    m->pd = pd;
    m->io_capacity = xx_get_file_buffer_size();
    m->table_solid = (size_t)-1;
    m->table_budget = (uint64_t)m->avail * 2U + WIM_SOLID_TABLE_FLOOR;
    if (!wim_read_header(m) || !wim_load_table(m) || (m->blob_count == 0U)) {
        wim_model_free(m);
        return false;
    }
    if (full && (!(m->path = (char *)xx_mem_alloc(WIM_MAX_PATH + 1U)) || !wim_build_members(m) || m->member_count == 0U)) {
        wim_model_free(m);
        return false;
    }
    *result = m;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                              */

static const xx_var *wim_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool wim_option_u64(Abstractformat *format, const xx_list_s *options, uint32_t id, uint64_t *value)
{
    const xx_var *limit = xx_format_resolve_extra_parameter(format, options, id);
    if (!limit) return false;
    switch (limit->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: *value = xx_var_get_u64(limit); return true;
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t v = xx_var_get_i64(limit);
            if (v < 0) return false;
            *value = (uint64_t)v;
            return true;
        }
        default: return false;
    }
}

static bool wim_extract(wim_model *m, const wim_member *member, xx_io_device *destination, xx_pd_struct *pd)
{
    const wim_blob *b;
    wim_sink s;
    bool stale;
    if (member->missing) return false;
    if (member->blob == WIM_NONE) return true; /* empty stream */
    b = &m->blobs[member->blob];
    xx_mem_zero(&s, sizeof(s));
    s.device = destination;
    s.pd = pd;
    s.io_capacity = m->io_capacity;
    s.hashing = !wim_is_zero_hash(b->hash) && xx_hash_init(&s.sha, XX_HASH_SHA1);
    m->pd = pd;
    if (!wim_read_blob(m, b, &s)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "WIM stream could not be read or decoded");
        return false;
    }
    if (!wim_hash_ok(b, &s, &stale) || stale) {
        /* stale: a stored stream whose bytes do not match their SHA-1 --
         * silent corruption, so the member fails like any other. */
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, stale ? "WIM stored stream SHA-1 mismatch" : "WIM stream SHA-1 mismatch");
        return false;
    }
    return true;
}

/* Reads a link's reparse data (without its 8-byte reparse header, as WIM
 * stores it) and locates the target: the print name, or the substitute name
 * when there is none.  On success *data is owned by the caller and *name
 * points to len bytes of UTF-16LE inside it. */
static bool wim_link_name(wim_model *m, const wim_member *member, uint8_t **data, const uint8_t **name, size_t *len, bool *relative)
{
    const wim_blob *b;
    size_t header, size, off_a, len_a, off_b, len_b, off;
    *data = NULL;
    if (member->blob == WIM_NONE || member->missing) return false;
    b = &m->blobs[member->blob];
    if (b->size > WIM_MAX_REPARSE || !wim_read_resource_memory(m, b, WIM_MAX_REPARSE, data)) return false;
    size = (size_t)b->size;
    header = member->reparse_tag == WIM_TAG_SYMLINK ? 12U : 8U;
    if (size < header) goto fail;
    off_a = xx_data_get_u16(*data + 4U, 2, 0, false); /* print name */
    len_a = xx_data_get_u16(*data + 6U, 2, 0, false);
    off_b = xx_data_get_u16(*data, 2, 0, false); /* substitute name */
    len_b = xx_data_get_u16(*data + 2U, 2, 0, false);
    *relative = header == 12U && (xx_data_get_u32(*data + 8U, 4, 0, false) & 1U) != 0U;
    off = len_a != 0U ? off_a : off_b;
    *len = len_a != 0U ? len_a : len_b;
    if (*len == 0U || (*len & 1U) != 0U || off > size - header || *len > size - header - off) goto fail;
    *name = *data + header + off;
    return true;
fail:
    xx_mem_free(*data);
    *data = NULL;
    return false;
}

/* The target of a symbolic link or junction exactly as stored, in UTF-8, as
 * the bytes of an ordinary file: a link is extracted as a file holding its
 * target, so nothing is ever created that the file system would follow. */
static char *wim_link_text(wim_model *m, const wim_member *member, size_t *out_len)
{
    uint8_t *data = NULL;
    const uint8_t *p;
    size_t len, units, i = 0U, n = 0U;
    bool relative;
    char *s;
    if (!wim_link_name(m, member, &data, &p, &len, &relative)) return NULL;
    units = len / 2U;
    s = (char *)xx_mem_alloc(units * 3U + 1U);
    if (s) {
        while (i < units) {
            uint32_t cp = xx_data_get_u16(p + 2U * i++, 2, 0, false);
            if (cp >= 0xD800U && cp <= 0xDBFFU && i < units && xx_data_get_u16(p + 2U * i, 2, 0, false) >= 0xDC00U &&
                xx_data_get_u16(p + 2U * i, 2, 0, false) <= 0xDFFFU) {
                cp = 0x10000U + ((cp - 0xD800U) << 10U) + (xx_data_get_u16(p + 2U * i, 2, 0, false) - 0xDC00U);
                ++i;
            } else if (cp >= 0xD800U && cp <= 0xDFFFU) {
                cp = 0xFFFDU;
            }
            if (cp < 0x80U) {
                s[n++] = (char)cp;
            } else if (cp < 0x800U) {
                s[n++] = (char)(0xC0U | (cp >> 6U));
                s[n++] = (char)(0x80U | (cp & 0x3FU));
            } else if (cp < 0x10000U) {
                s[n++] = (char)(0xE0U | (cp >> 12U));
                s[n++] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
                s[n++] = (char)(0x80U | (cp & 0x3FU));
            } else {
                s[n++] = (char)(0xF0U | (cp >> 18U));
                s[n++] = (char)(0x80U | ((cp >> 12U) & 0x3FU));
                s[n++] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
                s[n++] = (char)(0x80U | (cp & 0x3FU));
            }
        }
        s[n] = 0;
        *out_len = n;
    }
    xx_mem_free(data);
    return s;
}

/* The target of a symbolic link or junction, relative to the image root, or
 * NULL when it is absolute or leaves the image. */
static char *wim_link_target(wim_model *m, const wim_member *member, const char *name)
{
    uint8_t *data = NULL;
    const uint8_t *p;
    size_t len, i, n = 0U;
    bool relative;
    char *target = NULL, *out = NULL;
    const char *slash;
    if (!wim_link_name(m, member, &data, &p, &len, &relative)) return NULL;
    if (!relative) goto done;
    /* Start from the link's own directory and apply the target's parts. */
    slash = name;
    for (i = 0U; name[i]; ++i)
        if (name[i] == '/') slash = name + i;
    out = (char *)xx_mem_alloc(xx_str_len(name) + len * 3U + 8U);
    if (!out) goto done;
    if (slash != name) {
        n = (size_t)(slash - name);
        xx_rt_memcpy(out, name, n);
    }
    out[n] = 0;
    {
        size_t units = len / 2U, start = 0U, k;
        for (k = 0U; k <= units; ++k) {
            bool end = k == units || xx_data_get_u16(p + 2U * k, 2, 0, false) == '\\' || xx_data_get_u16(p + 2U * k, 2, 0, false) == '/';
            if (!end) continue;
            if (k - start == 1U && xx_data_get_u16(p + 2U * start, 2, 0, false) == '.') {
                /* stays */
            } else if (k - start == 2U && xx_data_get_u16(p + 2U * start, 2, 0, false) == '.' && xx_data_get_u16(p + 2U * start + 2U, 2, 0, false) == '.') {
                if (n == 0U) goto done; /* leaves the image */
                while (n > 0U && out[n - 1U] != '/') --n;
                if (n > 0U) --n;
                out[n] = 0;
            } else if (k > start) {
                char *piece = wim_component(p + 2U * start, k - start);
                size_t plen;
                if (!piece) goto done;
                plen = xx_str_len(piece);
                if (n != 0U) out[n++] = '/';
                xx_rt_memcpy(out + n, piece, plen + 1U);
                n += plen;
                xx_mem_free(piece);
            }
            start = k + 1U;
        }
    }
    if (n != 0U) {
        target = out;
        out = NULL;
    }
done:
    if (out) xx_mem_free(out);
    if (data) xx_mem_free(data);
    return target;
}

static unsigned wim_method_of(const wim_model *m, const wim_member *member)
{
    const wim_blob *b;
    if (member->blob == WIM_NONE) return 0U;
    b = &m->blobs[member->blob];
    if (b->solid_run != WIM_NONE) {
        const wim_run *run = &m->runs[b->solid_run];
        return run->count != 0U ? m->solids[run->first].codec : 0U;
    }
    return (b->flags & WIM_RES_COMPRESSED) ? m->method : 0U;
}

static bool wim_set_record(wim_model *m, xx_archive_record *record, const wim_member *member)
{
    const wim_blob *b = member->blob != WIM_NONE ? &m->blobs[member->blob] : NULL;
    uint64_t packed = b && b->solid_run == WIM_NONE ? b->packed : 0U;
    const char *name;
    bool ok;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!(name = wim_member_path(m, member))) return false;
    if (b && b->table_index != WIM_NONE) record->header_offset = m->base + (int64_t)m->hdr.table.offset + (int64_t)b->table_index * WIM_LOOKUP_ENTRY_SIZE;
    else if (member->kind == WIM_KIND_XML) record->header_offset = m->base + 0x48;
    record->header_size = b ? (b->table_index != WIM_NONE ? WIM_LOOKUP_ENTRY_SIZE : WIM_RESHDR_SIZE) : 0;
    if (b && b->solid_run == WIM_NONE) {
        record->data_offset = m->base + (int64_t)b->offset;
        record->compressed_size = (int64_t)b->packed;
    }
    ok = xx_archive_record_set_original_name(record, name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, packed) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->size) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, wim_method_of(m, member)) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->attributes) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, member->timestamp) && xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS, b ? b->flags : 0U) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, (member->attributes & WIM_ATTR_ENCRYPTED) != 0U) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, member->kind == WIM_KIND_DIR);
    if (ok && member->kind == WIM_KIND_LINK) {
        /* The path buffer is reused by nothing in between. */
        char *target = wim_link_target(m, member, name);
        if (target) {
            ok = xx_archive_record_set_meta_str(record, XX_META_ID_LINK_TARGET, target);
            xx_mem_free(target);
        }
    }
    return ok;
}

static bool wim_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

#include "xx_wim_writer.inc"

void xx_wim_init(xx_wim *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_WIM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ms-wim");
    xx_format_set_extension(&archive->format, "wim");
    archive->format.check_is_valid = xx_wim_check_is_valid;
    archive->format.handle_base_info = xx_wim_handle_base_info;
    archive->format.get_format_size = xx_wim_get_format_size;
    archive->format.get_number_of_archive_records = xx_wim_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_wim_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_wim_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_wim_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_wim_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_wim_free_archive_records_reading;
    archive->format.create_archive_records_writing = xx_wim_create_archive_records_writing;
    archive->format.pack_archive_record = xx_wim_pack_archive_record;
    archive->format.finalize_archive_records_writing = xx_wim_finalize_archive_records_writing;
    archive->format.free_archive_records_writing = xx_wim_free_archive_records_writing;
    archive->archive_end = -1;
}

xx_wim *xx_wim_create(xx_io_device *device, int64_t base_address)
{
    xx_wim *archive = (xx_wim *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_wim_init(archive, device, base_address);
    return archive;
}

void xx_wim_destroy(xx_wim *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_wim_free(xx_wim *archive)
{
    if (!archive) return;
    xx_wim_destroy(archive);
    xx_mem_free(archive);
}

/* Header plus offset table only: no metadata is decoded here. */
bool xx_wim_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    wim_model *m;
    if (!wim_open(format, false, pd, &m)) return false;
    wim_model_free(m);
    return true;
}

bool xx_wim_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    wim_model *m;
    xx_wim *archive;
    if (!format || !wim_open(format, true, pd, &m)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_wim *)format;
    archive->number_of_records = m->member_count;
    archive->archive_end = format->base_address + m->end;
    format->number_of_archive_records = m->member_count;
    format->format_size = m->end;
    format->file_type = XX_WIM_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    wim_model_free(m);
    return true;
}

int64_t xx_wim_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_wim_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_wim_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_wim_handle_base_info(format, pd)) ? ((xx_wim *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_wim_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    wim_model *m;
    xx_archive_record_state *state;
    if (!wim_open(format, true, pd, &m)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        wim_model_free(m);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = m;
    state->free_internal = wim_model_free;
    state->total_records = m->member_count;
    if (!wim_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    {
        uint64_t limit;
        if (wim_option_u64(format, &state->options, XX_META_ID_OPT_MEMORY_LIMIT, &limit)) {
            m->memory_limited = true;
            m->memory_limit = limit;
        }
    }
    if (!wim_set_record(m, &state->current_record, &m->members[m->order[0]])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_wim_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_wim_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    wim_model *m;
    if (!format || !state || state->format != format || !(m = (wim_model *)state->internal_state) || ++m->index >= m->member_count) {
        if (state) state->has_record = false;
        return false;
    }
    m->pd = pd;
    ++state->current_index;
    state->has_record = wim_set_record(m, &state->current_record, &m->members[m->order[m->index]]);
    return state->has_record;
}

bool xx_wim_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    wim_model *m;
    wim_member *member;
    const xx_var *path_option;
    const char *base = NULL, *name;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    char *link_text = NULL;
    size_t link_len = 0U;
    uint64_t limit;
    if (!format || !state || state->format != format || !state->has_record || !(m = (wim_model *)state->internal_state) || m->index >= m->member_count || wim_stopped(pd))
        return false;
    member = &m->members[m->order[m->index]];
    name = wim_member_path(m, member);
    if (!name || !wim_safe_output_name(name)) return false;
    if (wim_option_u64(format, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE, &limit) && member->size > limit) return false;
    if (member->missing) {
        xx_pd_set_error(pd, XXFC_ERR_IO, "WIM stream is stored in another part of a split image");
        return false;
    }
    /* A symbolic link or junction is extracted as an ordinary file holding
     * its target text (the reparse data itself would pass binary metadata
     * off as content).  No link is created, so nothing is ever followed. */
    if (member->kind == WIM_KIND_LINK) {
        m->pd = pd;
        link_text = wim_link_text(m, member, &link_len);
        if (!link_text) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "WIM link reparse data could not be read");
            return false;
        }
    }
    path_option = wim_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        if (link_text) {
            xx_mem_free(link_text);
            return true;
        }
        return member->kind == WIM_KIND_DIR || wim_extract(m, member, NULL, pd);
    }
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", name) : xx_str_concat(base, name);
    if (!path) goto done;
    if (member->kind == WIM_KIND_DIR) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    created = destination != NULL;
    if (!destination) goto done;
    if (link_text) result = link_len == 0U || xx_io_write(destination, link_text, link_len) == (ssize_t)link_len;
    else result = wim_extract(m, member, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && member->kind != WIM_KIND_DIR && created) xx_rt_remove(path);
    if (link_text) xx_mem_free(link_text);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_wim_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
