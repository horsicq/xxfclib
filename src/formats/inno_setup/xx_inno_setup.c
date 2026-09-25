/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Inno Setup installer reader.  xx_inno_setup.h describes the layout.
 *
 * The per-version record layouts (loader offset tables, the setup header
 * field walk, the entry arrays that precede [Files], and the file location
 * table) are ported from XArchive installers/xinnosetup.cpp
 * (Copyright (c) 2017-2026 hors, MIT License).  The ISX 2.0.x deltas (one
 * more header string, four more header bytes and a Check string on every
 * conditional entry) were measured on an "Inno Setup Setup Data (2.0.18)
 * with ISX (2.0.11)" installer: with them the entry walk ends exactly at the
 * end of its block.
 *
 * The loader is only read, never run: the table is located from fixed
 * places (0x30, the last 40 bytes, RCDATA 11111, the 1.09 loader's DATA
 * section), every offset in it is checked against the file, and all blocks
 * are CRC or Adler-32 framed.  Decompressed setup data is capped at
 * INNO_BLOCK_CAP.  Solid LZMA / LZMA2 chunks are read by a resumable decoder
 * that continues from one member to the next; solid zlib / bzip2 chunks are
 * cached in memory up to INNO_CACHE_MAX and otherwise re-decoded per member.
 *
 * The 6.4 .. 7.x layouts were checked against the record declarations in
 * Inno Setup's Projects/Src/Shared.Struct.pas and Compression.Base.pas
 * (tags is-6_4_2 .. is-7_1_0; read for the layout only, no code taken).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/inno_setup/xx_inno_setup.h"

#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and not edited here, so
 * the alias macro defined next to the enumerator is tested instead. */
#ifdef INNO_SETUP
#define XX_INNO_SETUP_FILE_TYPE XX_FILE_TYPE_INNO_SETUP
#else
#define XX_INNO_SETUP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define IV(a, b, c, d)                                               \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | \
     (uint32_t)(d))

/* Largest decompressed setup-0 block (header, entries, wizard images); a
 * 40,000-file installer needs about 15 MB. */
#define INNO_BLOCK_CAP ((size_t)64U << 20)
/* Largest stored (CRC framed) setup-0 block. */
#define INNO_STORED_CAP (INNO_BLOCK_CAP + (INNO_BLOCK_CAP / 4096U) * 4U + 4096U)
#define INNO_MAX_COUNT 1000000U
#define INNO_MAX_FILES 500000U
#define INNO_LEGACY_BLOCK_CAP ((uint32_t)16U << 20)
#define INNO_LEGACY_MAX_RECORDS 100000U
/* A solid chunk shared by several members is decoded once into memory when
 * the part the members need is at most this large. */
#define INNO_CACHE_MAX ((uint64_t)64U << 20)
#define INNO_PIECE 65536U
/* The 1.09 loader keeps its table in a small DATA section. */
#define INNO_DATA_SCAN_MAX ((uint32_t)256U << 10)
#define INNO_MAX_SECTIONS 96U
#define INNO_MAX_RES_ENTRIES 4096U

enum {
    INNO_C_STORE = 0,
    INNO_C_ZLIB = 1,
    INNO_C_BZIP2 = 2,
    INNO_C_LZMA1 = 3,
    INNO_C_LZMA2 = 4,
    INNO_C_UNKNOWN = 0xFF
};

enum {
    INNO_H_NONE = 0,
    INNO_H_ADLER32,
    INNO_H_CRC32,
    INNO_H_MD5,
    INNO_H_SHA1,
    INNO_H_SHA256
};

enum { INNO_F_NONE = 0, INNO_F_4108, INNO_F_5200, INNO_F_5309 };

enum {
    INNO_T_NONE = 0,
    INNO_T_VX,    /* 1.09: table inside the loader's DATA section */
    INNO_T_EOF40, /* 1.11: table in the last 40 bytes */
    INNO_T_PTR,   /* 1.2.10 .. 5.1.4: pointer at 0x30 */
    INNO_T_REV1,  /* 5.1.5 .. 6.4.x: RCDATA 11111, 32-bit fields */
    INNO_T_REV2   /* 6.5 and later: RCDATA 11111, 64-bit fields */
};

static const uint8_t g_inno_tag_new[6] = {0xCD, 0xE6, 0xD7, 0x7B, 0x0B, 0x2A};

typedef struct inno_ver_s {
    uint32_t v;
    bool unicode;
    bool isx;
    bool win16;
    uint8_t short_id; /* 109 or 111 for the 8-byte legacy IDs, else 0 */
    uint32_t id_size;
    char text[72];
} inno_ver;

typedef struct inno_table_s {
    int kind;
    int loader; /* 2, 4, 5, 6, 7 for INNO_T_PTR */
    int64_t table_offset;
    uint64_t total_size;
    int64_t exe_offset;
    int64_t header_offset;
    int64_t data_offset; /* 0: external slices */
} inno_table;

typedef struct inno_loc_s {
    uint32_t first_slice;
    uint32_t last_slice;
    int64_t chunk_offset; /* relative to the data area */
    int64_t sub_offset;   /* inside the decoded chunk */
    int64_t size;
    int64_t chunk_size;   /* bytes after "zlb\x1a" */
    uint64_t filetime;
    uint64_t extent;  /* bytes of its chunk that the run of locations needs */
    uint32_t users;   /* locations in that run */
    uint8_t checksum[32];
    uint8_t hash_kind;
    uint8_t compression;
    uint8_t filter;
    bool encrypted;
} inno_loc;

typedef struct inno_member_s {
    char *name;
    uint32_t loc;
    bool safe;
} inno_member;

typedef struct inno_ctx_s {
    inno_table table;
    inno_ver ver;
    inno_loc *locs;
    uint32_t loc_count;
    inno_member *members;
    uint32_t member_count;
    int64_t data_base; /* absolute device offset of the data area, -1 external */
    int64_t format_size;
    size_t index;
    int64_t cache_chunk;
    uint8_t *cache;
    uint64_t cache_size;
    struct ilz_s *lz;  /* decoder of the solid LZMA chunk being read */
    int64_t lz_chunk;
    /* A solid chunk known to decode only up to bad_at: later members of it
     * fail at once instead of decoding the damaged prefix again. */
    int64_t bad_chunk;
    uint64_t bad_at;
} inno_ctx;

static void ilz_free(struct ilz_s *z);

/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */

static uint16_t ile16(const uint8_t *b) {
    return (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
}

static uint32_t ile32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
}

static uint64_t ile64(const uint8_t *b) {
    return (uint64_t)ile32(b) | ((uint64_t)ile32(b + 4) << 32);
}

static bool inno_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }

static bool inno_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t got = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static uint32_t inno_crc32(const uint8_t *data, size_t size) {
    return xx_crc32_calc(0U, data, size);
}

/* ---------------------------------------------------------------------- */
/* Output sink: a write-only device the decoders write into                */

typedef struct inno_filter_s {
    int kind;
    uint64_t pos;   /* file position of the next byte */
    uint64_t total; /* file size */
    uint32_t addr;
    int left;
    uint8_t hold[8];
    size_t held;
} inno_filter;

typedef struct inno_out_s {
    inno_filter filter;
    int hash_kind;
    xx_hash_context hash;
    uint32_t adler;
    uint32_t crc;
    xx_io_device *dest;
    bool failed;
    uint8_t work[INNO_PIECE + 8U];
} inno_out;

typedef struct inno_sink_s {
    uint64_t skip; /* decoder bytes to drop first */
    uint64_t want; /* bytes to keep after them */
    uint64_t seen;
    uint64_t kept;
    uint8_t *mem;  /* memory mode */
    size_t mem_cap;
    size_t mem_limit;
    inno_out *out; /* streaming mode */
    bool failed;
} inno_sink;

static void inno_emit(inno_out *out, const uint8_t *data, size_t size) {
    size_t done = 0U;
    if (out->failed || !size) return;
    switch (out->hash_kind) {
    case INNO_H_ADLER32: out->adler = xx_adler32_update(out->adler, data, size); break;
    case INNO_H_CRC32: out->crc = xx_crc32_calc(out->crc, data, size); break;
    case INNO_H_MD5:
    case INNO_H_SHA1:
    case INNO_H_SHA256: xx_hash_update(&out->hash, data, size); break;
    default: break;
    }
    if (!out->dest) return;
    while (done < size) {
        ssize_t wrote = xx_io_write(out->dest, data + done, size - done);
        if (wrote <= 0 || (size_t)wrote > size - done) {
            out->failed = true;
            return;
        }
        done += (size_t)wrote;
    }
}

/* Inno 4.1.8 .. 5.1.x: CALL/JMP operands made absolute, byte by byte. */
static void inno_filter_4108(inno_filter *f, uint8_t *data, size_t size) {
    size_t i;
    for (i = 0U; i < size; ++i, ++f->pos) {
        uint8_t b = data[i];
        if (f->left == 0) {
            if (b == 0xE8U || b == 0xE9U) {
                f->addr = (uint32_t)(0U - (uint32_t)(f->pos + 5U));
                f->left = 4;
            }
        } else {
            f->addr += b;
            data[i] = (uint8_t)f->addr;
            f->addr >>= 8U;
            --f->left;
        }
    }
}

/* Inno 5.2.0 and later: 5-byte instructions that do not cross a 64 KiB
 * boundary, operand high byte 00 or FF; 5.3.9 also flips that byte.
 * Returns how many leading bytes are final; the rest wait for more data. */
static size_t inno_filter_5200(inno_filter *f, uint8_t *data, size_t size) {
    size_t i = 0U;
    while (i < size) {
        uint64_t pos = f->pos + i;
        uint8_t op = data[i];
        if ((op != 0xE8U && op != 0xE9U) || (0x10000U - (pos % 0x10000U)) < 5U ||
            pos + 5U > f->total) {
            ++i;
            continue;
        }
        if (size - i < 5U) break;
        {
            uint8_t high = data[i + 4U];
            if (high == 0x00U || high == 0xFFU) {
                uint32_t rel = (uint32_t)data[i + 1U] |
                               ((uint32_t)data[i + 2U] << 8U) |
                               ((uint32_t)data[i + 3U] << 16U);
                rel -= (uint32_t)(pos + 5U) & 0x00FFFFFFU;
                data[i + 1U] = (uint8_t)rel;
                data[i + 2U] = (uint8_t)(rel >> 8U);
                data[i + 3U] = (uint8_t)(rel >> 16U);
                if (f->kind == INNO_F_5309 && (rel & 0x00800000U))
                    data[i + 4U] = (uint8_t)~high;
            }
        }
        i += 5U;
    }
    f->pos += i;
    return i;
}

static void inno_out_feed(inno_out *out, const uint8_t *data, size_t size) {
    while (size && !out->failed) {
        size_t piece = size < INNO_PIECE ? size : INNO_PIECE;
        if (out->filter.kind == INNO_F_NONE) {
            inno_emit(out, data, piece);
        } else if (out->filter.kind == INNO_F_4108) {
            xx_rt_memcpy(out->work, data, piece);
            inno_filter_4108(&out->filter, out->work, piece);
            inno_emit(out, out->work, piece);
        } else {
            size_t total = out->filter.held + piece, done;
            if (out->filter.held)
                xx_rt_memcpy(out->work, out->filter.hold, out->filter.held);
            xx_rt_memcpy(out->work + out->filter.held, data, piece);
            done = inno_filter_5200(&out->filter, out->work, total);
            inno_emit(out, out->work, done);
            out->filter.held = total - done;
            if (out->filter.held > 4U) {
                out->failed = true;
                return;
            }
            if (out->filter.held)
                xx_rt_memcpy(out->filter.hold, out->work + done, out->filter.held);
        }
        data += piece;
        size -= piece;
    }
}

static bool inno_out_init(inno_out *out, const inno_loc *loc, xx_io_device *dest) {
    xx_mem_zero(out, sizeof(*out));
    out->dest = dest;
    out->filter.kind = loc->filter;
    out->filter.total = (uint64_t)loc->size;
    out->hash_kind = loc->hash_kind;
    out->adler = 1U;
    out->crc = 0U;
    if (out->hash_kind == INNO_H_MD5) return xx_hash_init(&out->hash, XX_HASH_MD5);
    if (out->hash_kind == INNO_H_SHA1) return xx_hash_init(&out->hash, XX_HASH_SHA1);
    if (out->hash_kind == INNO_H_SHA256) return xx_hash_init(&out->hash, XX_HASH_SHA256);
    return true;
}

/* Flush what the filter still holds and compare the stored checksum. */
static bool inno_out_finish(inno_out *out, const inno_loc *loc) {
    uint8_t digest[XX_HASH_MAX_DIGEST_SIZE];
    if (out->filter.held) {
        inno_emit(out, out->filter.hold, out->filter.held);
        out->filter.held = 0U;
    }
    if (out->failed) return false;
    switch (out->hash_kind) {
    case INNO_H_ADLER32: return out->adler == ile32(loc->checksum);
    case INNO_H_CRC32: return out->crc == ile32(loc->checksum);
    case INNO_H_MD5:
        return xx_hash_final(&out->hash, digest, sizeof(digest)) &&
               xx_rt_memcmp(digest, loc->checksum, XX_MD5_DIGEST_SIZE) == 0;
    case INNO_H_SHA1:
        return xx_hash_final(&out->hash, digest, sizeof(digest)) &&
               xx_rt_memcmp(digest, loc->checksum, XX_SHA1_DIGEST_SIZE) == 0;
    case INNO_H_SHA256:
        return xx_hash_final(&out->hash, digest, sizeof(digest)) &&
               xx_rt_memcmp(digest, loc->checksum, XX_SHA256_DIGEST_SIZE) == 0;
    default: return true;
    }
}

static bool inno_sink_keep(inno_sink *s, const uint8_t *data, size_t size) {
    if (s->out) {
        inno_out_feed(s->out, data, size);
        return !s->out->failed;
    }
    if (s->kept + size > s->mem_cap) {
        size_t need = (size_t)s->kept + size, cap = s->mem_cap ? s->mem_cap : 65536U;
        uint8_t *grown;
        if (need > s->mem_limit) return false;
        while (cap < need) cap = cap > s->mem_limit / 2U ? s->mem_limit : cap * 2U;
        grown = (uint8_t *)xx_mem_realloc(s->mem, cap);
        if (!grown) return false;
        s->mem = grown;
        s->mem_cap = cap;
    }
    xx_rt_memcpy(s->mem + s->kept, data, size);
    return true;
}

static ssize_t inno_sink_write(xx_io_device *self, const void *buffer, size_t n) {
    inno_sink *s = self ? (inno_sink *)self->priv : NULL;
    const uint8_t *p = (const uint8_t *)buffer;
    size_t left = n;
    if (!s || s->failed || (!buffer && n)) return -1;
    if (!n) return 0;
    if (s->kept >= s->want) return -1; /* everything needed is in: stop */
    if (s->seen < s->skip) {
        uint64_t drop = s->skip - s->seen;
        if (drop > left) drop = left;
        p += drop;
        left -= (size_t)drop;
        s->seen += drop;
    }
    if (left) {
        uint64_t take = s->want - s->kept;
        if (take > left) take = left;
        if (!inno_sink_keep(s, p, (size_t)take)) {
            s->failed = true;
            return -1;
        }
        s->kept += take;
        s->seen += take;
        if (left > take) return -1;
    }
    return (ssize_t)n;
}

static ssize_t inno_dev_read(xx_io_device *self, void *buffer, size_t n) {
    (void)self; (void)buffer; (void)n;
    return -1;
}

static int inno_dev_seek(xx_io_device *self, long offset, int whence) {
    (void)self; (void)offset; (void)whence;
    return -1;
}

static int inno_dev_close(xx_io_device *self) {
    (void)self;
    return 0;
}

static int64_t inno_dev_size(xx_io_device *self) {
    inno_sink *s = self ? (inno_sink *)self->priv : NULL;
    return s ? (int64_t)s->kept : -1;
}

static void inno_sink_device(xx_io_device *device, inno_sink *sink) {
    xx_mem_zero(device, sizeof(*device));
    device->read = inno_dev_read;
    device->write = inno_sink_write;
    device->seek = inno_dev_seek;
    device->close = inno_dev_close;
    device->total_size = inno_dev_size;
    device->priv = sink;
}

/* ---------------------------------------------------------------------- */
/* Byte cursor over a decompressed block                                   */

typedef struct icur_s {
    const uint8_t *p;
    size_t n;
    size_t o;
} icur;

static bool ic_skip(icur *c, uint64_t k) {
    if (k > (uint64_t)(c->n - c->o)) return false;
    c->o += (size_t)k;
    return true;
}

static bool ic_str(icur *c, const uint8_t **s, uint32_t *len) {
    uint32_t l;
    if (c->n - c->o < 4U) return false;
    l = ile32(c->p + c->o);
    if (l > 0x10000000U || (size_t)l > c->n - c->o - 4U) return false;
    if (s) *s = c->p + c->o + 4U;
    if (len) *len = l;
    c->o += 4U + (size_t)l;
    return true;
}

static bool ic_wstr(icur *c, const uint8_t **s, uint32_t *len) {
    uint32_t l;
    if (c->n - c->o < 4U) return false;
    l = ile32(c->p + c->o);
    if ((l & 1U) || l > 0x1000000U || (size_t)l > c->n - c->o - 4U) return false;
    if (s) *s = c->p + c->o + 4U;
    if (len) *len = l;
    c->o += 4U + (size_t)l;
    return true;
}

static bool ic_strs(icur *c, int count) {
    int i;
    for (i = 0; i < count; ++i)
        if (!ic_str(c, NULL, NULL)) return false;
    return true;
}

/* count records of `wide` UTF-16 strings, `ansi` byte strings and `fixed`
 * bytes each */
static bool ic_entries(icur *c, uint32_t count, int wide, int ansi, uint32_t fixed) {
    uint32_t i;
    int j;
    for (i = 0U; i < count; ++i) {
        for (j = 0; j < wide; ++j)
            if (!ic_wstr(c, NULL, NULL)) return false;
        for (j = 0; j < ansi; ++j)
            if (!ic_str(c, NULL, NULL)) return false;
        if (!ic_skip(c, fixed)) return false;
    }
    return true;
}

/* 1.2.10: a Longint record size, `strings` strings and a `tail` byte tail
 * that must end exactly at the record end. */
static bool ic_sized(icur *c, int strings, uint32_t tail, const uint8_t **name,
                     uint32_t *name_len, size_t *tail_at) {
    uint32_t size;
    size_t end;
    int i;
    if (c->n - c->o < 4U) return false;
    size = ile32(c->p + c->o);
    if (size < (uint32_t)strings * 4U + tail || (size_t)size > c->n - c->o - 4U)
        return false;
    end = c->o + 4U + size;
    c->o += 4U;
    for (i = 0; i < strings; ++i) {
        const uint8_t *s;
        uint32_t l;
        if (!ic_str(c, &s, &l) || c->o > end) return false;
        if (i == 1 && name) {
            *name = s;
            *name_len = l;
        }
    }
    if (end - c->o != tail) return false;
    if (tail_at) *tail_at = c->o;
    c->o = end;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Version ID                                                              */

static bool inno_digits(const char *s, size_t n, size_t *pos, uint32_t *value) {
    uint32_t v = 0U;
    size_t start = *pos;
    while (*pos < n && s[*pos] >= '0' && s[*pos] <= '9') {
        v = v * 10U + (uint32_t)(s[*pos] - '0');
        if (v > 65535U) return false;
        ++*pos;
    }
    if (*pos == start) return false;
    *value = v;
    return true;
}

static bool inno_prefix(const char *s, size_t n, const char *prefix, size_t *pos) {
    size_t i = 0U;
    while (prefix[i]) {
        if (*pos + i >= n || s[*pos + i] != prefix[i]) return false;
        ++i;
    }
    *pos += i;
    return true;
}

/* "a.b.c" or "a.b.c.d", each part a number */
static bool inno_dotted(const char *s, size_t n, size_t *pos, uint32_t parts[4],
                        int *count) {
    int k = 0;
    for (;;) {
        if (k == 4 || !inno_digits(s, n, pos, &parts[k])) return false;
        ++k;
        if (*pos < n && s[*pos] == '.') {
            ++*pos;
            continue;
        }
        break;
    }
    *count = k;
    return k >= 3;
}

static bool inno_parse_version_id(const uint8_t *id, size_t avail, inno_ver *out) {
    const char *s = (const char *)id;
    uint32_t parts[4] = {0U, 0U, 0U, 0U}, isx[4];
    size_t n = 0U, pos = 0U, i;
    int count = 0, isx_count = 0;
    bool isx_prefix = false;
    xx_mem_zero(out, sizeof(*out));
    /* 1.09 / 1.11: "iNNN-32\x1a" */
    if (avail >= 8U && id[0] == 'i' && id[4] == '-' && id[5] == '3' && id[6] == '2' &&
        id[7] == 0x1AU) {
        if (xx_rt_memcmp(id + 1, "109", 3U) == 0 || xx_rt_memcmp(id + 1, "111", 3U) == 0) {
            out->short_id = id[2] == '0' ? 109U : 111U;
            out->v = out->short_id == 109U ? IV(1, 0, 9, 0) : IV(1, 1, 1, 0);
            out->id_size = 8U;
            xx_rt_memcpy(out->text, out->short_id == 109U ? "1.09" : "1.11", 5U);
            return true;
        }
        return false;
    }
    /* 1.2.10: native-width 12-byte IDs */
    if (avail >= 12U && (xx_rt_memcmp(id, "i1.2.10--16\x1a", 12U) == 0 ||
                         xx_rt_memcmp(id, "i1.2.10--32\x1a", 12U) == 0)) {
        out->v = IV(1, 2, 10, 0);
        out->win16 = id[9] == '1';
        out->id_size = 12U;
        xx_rt_memcpy(out->text, out->win16 ? "1.2.10--16" : "1.2.10--32", 11U);
        return true;
    }
    if (avail < 64U) return false;
    while (n < 64U && id[n]) ++n;
    for (i = n; i < 64U; ++i)
        if (id[i]) return false;
    if (inno_prefix(s, n, "Inno Setup Setup Data (", &pos)) {
    } else if (inno_prefix(s, n, "My Inno Setup Extensions Setup Data (", &pos)) {
        isx_prefix = true;
    } else {
        return false;
    }
    if (!inno_dotted(s, n, &pos, parts, &count)) return false;
    if (pos < n && s[pos] == 'a') ++pos; /* 2.0.6a, 3.0.0a, 4.0.0a */
    if (pos >= n || s[pos] != ')') return false;
    ++pos;
    if (pos < n) {
        if (inno_prefix(s, n, " (u)", &pos) || inno_prefix(s, n, " (U)", &pos)) {
            out->unicode = true;
        } else if (inno_prefix(s, n, " with ISX (", &pos)) {
            if (!inno_dotted(s, n, &pos, isx, &isx_count)) return false;
            if (pos < n && s[pos] == 'a') ++pos;
            if (pos >= n || s[pos] != ')') return false;
            ++pos;
            out->isx = true;
        } else {
            return false;
        }
        if (pos != n) return false;
    }
    if (isx_prefix) out->isx = true;
    if (parts[0] < 1U || parts[0] > 7U || parts[1] > 255U || parts[2] > 255U ||
        parts[3] > 255U)
        return false;
    out->v = IV(parts[0], parts[1], parts[2], parts[3]);
    if (out->v < IV(1, 3, 0, 0)) return false;
    if (parts[0] >= 6U) out->unicode = true;
    if (out->isx) out->unicode = false;
    if (out->unicode && out->v < IV(5, 2, 5, 0)) return false;
    out->id_size = 64U;
    {
        size_t open = 0U, k = 0U;
        while (open < n && s[open] != '(') ++open;
        ++open;
        while (open + k < n && s[open + k] != ')' && k < sizeof(out->text) - 1U) {
            out->text[k] = s[open + k];
            ++k;
        }
        out->text[k] = 0;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Loader table                                                            */

static bool inno_tag_is(const uint8_t *b, const char *tag2) {
    return xx_rt_memcmp(b, "rDlPtS", 6U) == 0 && b[6] == (uint8_t)tag2[0] &&
           b[7] == (uint8_t)tag2[1] && b[8] == 0x87U && b[9] == 0x65U &&
           b[10] == 0x56U && b[11] == 0x78U;
}

/* 1.09 (Vx) and 1.11 (EOF40): ID, TotalSize, ExeOffset, ExeCompressedSize,
 * ExeUncompressedSize, ExeAdler, Offset0, Offset1. */
static bool inno_table_legacy(const uint8_t *t, int64_t table_offset, int64_t size,
                              int kind, inno_table *out) {
    uint64_t total = ile32(t + 12), exe = ile32(t + 16), exe_csize = ile32(t + 20),
             exe_usize = ile32(t + 24), off0 = ile32(t + 32), off1 = ile32(t + 36);
    uint64_t exe_end = exe + exe_csize;
    bool layout;
    if (total != (uint64_t)size || exe == 0U || exe_csize == 0U || exe_usize == 0U ||
        exe_end != off0 || size < 12 || off0 > (uint64_t)(size - 8) ||
        off1 > (uint64_t)(size - 12))
        return false;
    if (kind == INNO_T_EOF40)
        layout = table_offset == size - 40 && off1 >= 12U && off1 < exe &&
                 off0 + 8U <= (uint64_t)table_offset;
    else
        layout = (uint64_t)table_offset + 40U <= exe && off0 + 8U <= off1;
    if (!layout) return false;
    xx_mem_zero(out, sizeof(*out));
    out->kind = kind;
    out->table_offset = table_offset;
    out->total_size = total;
    out->exe_offset = (int64_t)exe;
    out->header_offset = (int64_t)off0;
    out->data_offset = (int64_t)off1;
    return true;
}

/* 1.2.10 .. 5.1.4: "Inno" at 0x30, then the table pointer and its
 * complement.  The tag selects the packed layout. */
static bool inno_table_ptr(xx_io_device *d, int64_t base, int64_t size,
                           const uint8_t *head, inno_table *out) {
    uint8_t t[44];
    uint32_t ptr = ile32(head + 0x34), nptr = ile32(head + 0x38);
    int loader = 0;
    uint32_t tsize;
    uint64_t total, exe, exe_csize = 0U, exe_usize, hdr, data, table_end;
    bool exe_ok, total_ok;
    if (ile32(head + 0x30) != 0x6F6E6E49U || ptr != ~nptr || size < 12 ||
        (uint64_t)ptr > (uint64_t)(size - 12) ||
        !inno_read_at(d, base + ptr, t, 12U))
        return false;
    if (inno_tag_is(t, "02")) loader = 2;
    else if (inno_tag_is(t, "04")) loader = 4;
    else if (inno_tag_is(t, "05")) loader = 5;
    else if (inno_tag_is(t, "06")) loader = 6;
    else if (inno_tag_is(t, "07")) loader = 7;
    else return false;
    tsize = (loader == 2 || loader == 6) ? 44U : 40U;
    if ((uint64_t)ptr > (uint64_t)(size - (int64_t)tsize) ||
        !inno_read_at(d, base + ptr, t, tsize))
        return false;
    if (loader >= 6 && ile32(t + tsize - 4U) != inno_crc32(t, tsize - 4U)) return false;
    total = ile32(t + 12);
    exe = ile32(t + 16);
    if (loader != 7) {
        exe_csize = ile32(t + 20);
        exe_usize = ile32(t + 24);
        hdr = ile32(t + (loader == 2 ? 36 : 32));
        data = ile32(t + (loader == 2 ? 40 : 36));
    } else {
        exe_usize = ile32(t + 20);
        hdr = ile32(t + 28);
        data = ile32(t + 32);
    }
    table_end = (uint64_t)ptr + tsize;
    exe_ok = loader == 7 ? exe < total
                         : (exe_csize > 0U && exe <= total && exe_csize <= total - exe);
    total_ok = loader == 2 ? total == table_end : total >= table_end;
    if (!total_ok || total > (uint64_t)size || exe_usize == 0U || exe == 0U || !exe_ok ||
        hdr == 0U || hdr + 64U > total || (data != 0U && (total < 4U || data > total - 4U)))
        return false;
    xx_mem_zero(out, sizeof(*out));
    out->kind = INNO_T_PTR;
    out->loader = loader;
    out->table_offset = ptr;
    out->total_size = total;
    out->exe_offset = (int64_t)exe;
    out->header_offset = (int64_t)hdr;
    out->data_offset = (int64_t)data;
    return true;
}

/* 5.1.5 and later: revision 1 (44 bytes) or 2 (64 bytes), CRC-32 last. */
static bool inno_table_new(xx_io_device *d, int64_t base, int64_t size,
                           int64_t at, inno_table *out) {
    uint8_t t[64];
    uint32_t rev, tsize;
    uint64_t total, exe, exe_usize, hdr, data, table_end;
    if (at < 0 || at > size - 16 || !inno_read_at(d, base + at, t, 16U)) return false;
    if (xx_rt_memcmp(t, "rDlPtS", 6U) != 0 || xx_rt_memcmp(t + 6, g_inno_tag_new, 6U) != 0)
        return false;
    rev = ile32(t + 12);
    tsize = rev == 1U ? 44U : (rev == 2U ? 64U : 0U);
    if (!tsize || at > size - (int64_t)tsize || !inno_read_at(d, base + at, t, tsize))
        return false;
    if (ile32(t + tsize - 4U) != inno_crc32(t, tsize - 4U)) return false;
    if (rev == 1U) {
        total = ile32(t + 16);
        exe = ile32(t + 20);
        exe_usize = ile32(t + 24);
        hdr = ile32(t + 32);
        data = ile32(t + 36);
    } else {
        total = ile64(t + 16);
        exe = ile64(t + 24);
        exe_usize = ile32(t + 32);
        hdr = ile64(t + 40);
        data = ile64(t + 48);
        if (ile32(t + 56) != 0U) return false;
    }
    table_end = (uint64_t)at + tsize;
    if (total < table_end || total > (uint64_t)size || exe_usize == 0U || exe == 0U ||
        exe >= total || hdr == 0U || hdr > total || total - hdr < 64U ||
        (data != 0U && data >= total))
        return false;
    xx_mem_zero(out, sizeof(*out));
    out->kind = rev == 1U ? INNO_T_REV1 : INNO_T_REV2;
    out->table_offset = at;
    out->total_size = total;
    out->exe_offset = (int64_t)exe;
    out->header_offset = (int64_t)hdr;
    out->data_offset = (int64_t)data;
    return true;
}

typedef struct inno_section_s {
    char name[9];
    uint32_t va, vsize, raw, rawsize;
} inno_section;

typedef struct inno_pe_s {
    uint32_t count;
    inno_section s[INNO_MAX_SECTIONS];
    uint32_t res_rva;
    uint32_t res_size;
} inno_pe;

static bool inno_pe_parse(xx_io_device *d, int64_t base, int64_t size,
                          const uint8_t *head, inno_pe *pe) {
    uint8_t h[24 + 240];
    uint32_t lfanew = ile32(head + 0x3C), i, opt, dirs;
    uint16_t magic;
    int64_t sect;
    pe->count = 0U;
    pe->res_rva = pe->res_size = 0U;
    if (lfanew < 0x40U || (int64_t)lfanew > size - 24 - 96 ||
        !inno_read_at(d, base + lfanew, h, 24U + 96U))
        return false;
    if (xx_rt_memcmp(h, "PE\0\0", 4U) != 0) return false;
    pe->count = ile16(h + 6);
    opt = ile16(h + 20);
    if (pe->count == 0U || pe->count > INNO_MAX_SECTIONS || opt < 96U || opt > 240U ||
        (int64_t)lfanew + 24 + (int64_t)opt > size ||
        !inno_read_at(d, base + lfanew, h, 24U + opt))
        return false;
    magic = ile16(h + 24);
    if (magic == 0x10BU) {
        dirs = 96U;
    } else if (magic == 0x20BU) {
        dirs = 112U;
    } else {
        return false;
    }
    if (opt >= dirs + 3U * 8U && ile32(h + 24 + dirs - 4U) >= 3U) {
        pe->res_rva = ile32(h + 24 + dirs + 16U);
        pe->res_size = ile32(h + 24 + dirs + 20U);
    }
    sect = (int64_t)lfanew + 24 + opt;
    if (sect > size - (int64_t)pe->count * 40) return false;
    for (i = 0U; i < pe->count; ++i) {
        uint8_t e[40];
        if (!inno_read_at(d, base + sect + (int64_t)i * 40, e, 40U)) return false;
        xx_rt_memcpy(pe->s[i].name, e, 8U);
        pe->s[i].name[8] = 0;
        pe->s[i].vsize = ile32(e + 8);
        pe->s[i].va = ile32(e + 12);
        pe->s[i].rawsize = ile32(e + 16);
        pe->s[i].raw = ile32(e + 20);
    }
    return true;
}

static int64_t inno_rva_to_offset(const inno_pe *pe, uint32_t rva, uint32_t len,
                                  int64_t size) {
    uint32_t i;
    for (i = 0U; i < pe->count; ++i) {
        const inno_section *s = &pe->s[i];
        uint32_t span = s->vsize > s->rawsize ? s->vsize : s->rawsize;
        if (rva >= s->va && rva - s->va < span) {
            uint64_t off = (uint64_t)s->raw + (rva - s->va);
            if ((uint64_t)(rva - s->va) + len > s->rawsize) return -1;
            if (off + len > (uint64_t)size) return -1;
            return (int64_t)off;
        }
    }
    return -1;
}

/* Resource directory: find the entry with numeric `id` (or the first entry
 * when id is 0xFFFFFFFF) in the directory at `dir` (relative to the
 * resource section start). */
static bool inno_res_find(xx_io_device *d, int64_t base, int64_t res_off,
                          uint32_t res_size, uint32_t dir, uint32_t id,
                          uint32_t *value) {
    uint8_t h[16], e[64 * 8];
    uint32_t named, ids, i, k, batch;
    if (dir > res_size || res_size - dir < 16U ||
        !inno_read_at(d, base + res_off + dir, h, 16U))
        return false;
    named = ile16(h + 12);
    ids = ile16(h + 14);
    if (named + ids > INNO_MAX_RES_ENTRIES || (uint64_t)dir + 16U + (uint64_t)(named + ids) * 8U > res_size)
        return false;
    for (i = 0U; i < named + ids; i += batch) {
        batch = named + ids - i < 64U ? named + ids - i : 64U;
        if (!inno_read_at(d, base + res_off + dir + 16 + (int64_t)i * 8, e, (size_t)batch * 8U))
            return false;
        for (k = 0U; k < batch; ++k) {
            const uint8_t *entry = e + k * 8U;
            if (id == 0xFFFFFFFFU || (i + k >= named && ile32(entry) == id)) {
                *value = ile32(entry + 4);
                return true;
            }
        }
    }
    return false;
}

static bool inno_table_resource(xx_io_device *d, int64_t base, int64_t size,
                                const inno_pe *pe, inno_table *out) {
    int64_t res_off;
    uint32_t v, entry;
    uint8_t de[16];
    int64_t data;
    if (!pe->res_rva || pe->res_size < 16U) return false;
    res_off = inno_rva_to_offset(pe, pe->res_rva, 16U, size);
    if (res_off < 0) return false;
    {
        uint32_t cap = pe->res_size;
        if ((uint64_t)res_off + cap > (uint64_t)size) cap = (uint32_t)(size - res_off);
        /* RT_RCDATA -> 11111 -> first language */
        if (!inno_res_find(d, base, res_off, cap, 0U, 10U, &v) || !(v & 0x80000000U) ||
            !inno_res_find(d, base, res_off, cap, v & 0x7FFFFFFFU, 11111U, &v) ||
            !(v & 0x80000000U) ||
            !inno_res_find(d, base, res_off, cap, v & 0x7FFFFFFFU, 0xFFFFFFFFU, &entry) ||
            (entry & 0x80000000U) || entry > cap || cap - entry < 16U ||
            !inno_read_at(d, base + res_off + entry, de, 16U))
            return false;
    }
    if (ile32(de + 4) < 44U) return false;
    data = inno_rva_to_offset(pe, ile32(de), 44U, size);
    return data >= 0 && inno_table_new(d, base, size, data, out);
}

/* 1.09 keeps "rDlPtSVx" in the loader's DATA section.  Only the first
 * section of that name is scanned (the Delphi loader has one), so a crafted
 * section table cannot multiply the read. */
static bool inno_table_vx(xx_io_device *d, int64_t base, int64_t size,
                          const inno_pe *pe, inno_table *out) {
    uint32_t i;
    for (i = 0U; i < pe->count; ++i) {
        const inno_section *s = &pe->s[i];
        uint8_t *buf;
        uint32_t len = s->rawsize, k;
        bool found = false;
        if (xx_rt_memcmp(s->name, "DATA\0\0\0\0", 8U) != 0) continue;
        if (len < 40U || len > INNO_DATA_SCAN_MAX || (int64_t)s->raw > size - (int64_t)len)
            return false;
        buf = (uint8_t *)xx_mem_alloc(len);
        if (!buf) return false;
        if (inno_read_at(d, base + s->raw, buf, len)) {
            for (k = 0U; k + 40U <= len && !found; ++k)
                if (buf[k] == 'r' && inno_tag_is(buf + k, "Vx"))
                    found = inno_table_legacy(buf + k, (int64_t)s->raw + k, size,
                                              INNO_T_VX, out);
        }
        xx_mem_free(buf);
        return found;
    }
    return false;
}

static bool inno_find_table(Abstractformat *f, inno_table *out, int64_t *size_out) {
    xx_io_device *d;
    int64_t total, base, size;
    uint8_t head[0x40], t[40];
    inno_pe pe;
    bool have_pe;
    if (!f || !(d = f->device) || f->base_address < 0) return false;
    base = f->base_address;
    total = xx_io_total_size(d);
    if (total < base + 0x40) return false;
    size = total - base;
    if (!inno_read_at(d, base, head, sizeof(head)) || head[0] != 'M' || head[1] != 'Z')
        return false;
    if (size_out) *size_out = size;
    if (ile32(head + 0x30) == 0x6F6E6E49U && inno_table_ptr(d, base, size, head, out))
        return true;
    if (size >= 40 + 0x40 && inno_read_at(d, base + size - 40, t, 40U) &&
        inno_tag_is(t, "02") && inno_table_legacy(t, size - 40, size, INNO_T_EOF40, out))
        return true;
    have_pe = inno_pe_parse(d, base, size, head, &pe);
    if (!have_pe) return false;
    if (inno_table_resource(d, base, size, &pe, out)) return true;
    return inno_table_vx(d, base, size, &pe, out);
}

/* The table generation and the setup-data version must agree. */
static bool inno_generation_ok(const inno_table *t, const inno_ver *ver) {
    uint32_t v = ver->v;
    switch (t->kind) {
    case INNO_T_VX: return ver->short_id == 109U;
    case INNO_T_EOF40: return ver->short_id == 111U;
    case INNO_T_PTR:
        if (ver->short_id) return false;
        switch (t->loader) {
        case 2: return v >= IV(1, 2, 10, 0) && v < IV(4, 0, 0, 0);
        case 4: return v >= IV(4, 0, 0, 0) && v < IV(4, 0, 3, 0);
        case 5: return v >= IV(4, 0, 3, 0) && v < IV(4, 0, 10, 0);
        case 6: return v >= IV(4, 0, 10, 0) && v < IV(4, 1, 6, 0);
        case 7: return v >= IV(4, 1, 6, 0) && v < IV(5, 1, 5, 0);
        default: return false;
        }
    case INNO_T_REV1:
        return !ver->short_id && !ver->win16 && v >= IV(5, 1, 5, 0) && v < IV(6, 5, 0, 0);
    case INNO_T_REV2:
        return !ver->short_id && !ver->win16 && v >= IV(6, 5, 0, 0);
    default: return false;
    }
}

static bool inno_probe(Abstractformat *f, inno_table *table, inno_ver *ver,
                       int64_t *size_out) {
    uint8_t id[64];
    int64_t size = 0, avail;
    xx_mem_zero(id, sizeof(id));
    if (!inno_find_table(f, table, &size)) return false;
    if (table->header_offset < 0 || table->header_offset > size - 8) return false;
    avail = size - table->header_offset;
    if (avail > 64) avail = 64;
    if (!inno_read_at(f->device, f->base_address + table->header_offset, id, (size_t)avail) ||
        !inno_parse_version_id(id, (size_t)avail, ver))
        return false;
    if (ver->win16 && table->kind != INNO_T_PTR) return false;
    if (!inno_generation_ok(table, ver)) return false;
    if (size_out) *size_out = size;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Setup-0 blocks                                                          */

typedef struct ibuf_s {
    uint8_t *p;
    size_t n;
    bool inexact; /* LZMA stream that ended with its input, not a marker */
} ibuf;

static void ibuf_free(ibuf *b) {
    if (b->p) xx_mem_free(b->p);
    b->p = NULL;
    b->n = 0U;
}

/* Decode a zlib stream held in memory; `expected` is the exact size when
 * known, else 0 and the output may be anything up to the cap. */
static bool inno_zlib_memory(const uint8_t *src, size_t n, size_t expected, ibuf *out,
                             xx_pd_struct *pd) {
    inno_sink sink;
    xx_io_device dev;
    bool ok;
    xx_mem_zero(&sink, sizeof(sink));
    if (n < 3U || !xx_zlib_stream_header_is_valid(src, n)) return false;
    sink.want = expected ? expected : INNO_BLOCK_CAP + 1U;
    sink.mem_limit = expected ? expected : INNO_BLOCK_CAP + 1U;
    inno_sink_device(&dev, &sink);
    ok = xx_deflate_unpack_memory_to_device(src + 2, n - 2U, &dev, false, pd);
    if (!ok || sink.failed || sink.kept > INNO_BLOCK_CAP ||
        (expected && sink.kept != expected) || !sink.kept) {
        if (sink.mem) xx_mem_free(sink.mem);
        return false;
    }
    out->p = sink.mem;
    out->n = (size_t)sink.kept;
    return true;
}

static bool inno_lzma_memory(const uint8_t *src, size_t n, ibuf *out, xx_pd_struct *pd) {
    inno_sink sink;
    xx_io_device dev;
    uint8_t props[5];
    uint32_t dict;
    bool ok;
    xx_mem_zero(&sink, sizeof(sink));
    if (n < 5U + 5U) return false;
    xx_rt_memcpy(props, src, 5U);
    dict = ile32(props + 1);
    if (props[0] >= 9U * 5U * 5U) return false;
    if (dict > (uint32_t)INNO_BLOCK_CAP) {
        dict = (uint32_t)INNO_BLOCK_CAP;
        props[1] = (uint8_t)dict;
        props[2] = (uint8_t)(dict >> 8U);
        props[3] = (uint8_t)(dict >> 16U);
        props[4] = (uint8_t)(dict >> 24U);
    }
    sink.want = INNO_BLOCK_CAP + 1U;
    sink.mem_limit = INNO_BLOCK_CAP + 1U;
    inno_sink_device(&dev, &sink);
    ok = xx_lzma_unpack_memory_to_device(src + 5, n - 5U, props, 5U, -1, &dev, pd);
    if (sink.failed || sink.kept > INNO_BLOCK_CAP || !sink.kept || inno_stopped(pd)) {
        if (sink.mem) xx_mem_free(sink.mem);
        return false;
    }
    out->p = sink.mem;
    out->n = (size_t)sink.kept;
    out->inexact = !ok;
    return true;
}

/* Strip the per-4096-byte CRC-32 prefixes in place. */
static bool inno_strip_crc(uint8_t *data, size_t n, size_t *out_n) {
    size_t in = 0U, o = 0U;
    while (in < n) {
        size_t chunk;
        uint32_t crc;
        if (n - in <= 4U) return false;
        crc = ile32(data + in);
        in += 4U;
        chunk = n - in < 4096U ? n - in : 4096U;
        if (inno_crc32(data + in, chunk) != crc) return false;
        xx_rt_memmove(data + o, data + in, chunk);
        o += chunk;
        in += chunk;
    }
    *out_n = o;
    return true;
}

/* One compressed setup-0 block at `off`; `limit` bounds it. */
static bool inno_read_block(Abstractformat *f, int64_t off, int64_t limit,
                            const inno_ver *ver, ibuf *out, int64_t *consumed,
                            xx_pd_struct *pd) {
    uint8_t h[13];
    uint32_t hsize, uncomp = 0U;
    uint64_t stored;
    bool compressed, ok = false;
    uint8_t *raw = NULL;
    size_t plain = 0U;
    bool old = ver->v < IV(4, 0, 9, 0);
    bool wide = ver->v >= IV(6, 7, 0, 0);
    xx_mem_zero(out, sizeof(*out));
    hsize = old ? 12U : (wide ? 13U : 9U);
    if (off < 0 || limit - off < (int64_t)hsize ||
        !inno_read_at(f->device, f->base_address + off, h, hsize))
        return false;
    if (ile32(h) != inno_crc32(h + 4, hsize - 4U)) return false;
    if (old) {
        int32_t csize = (int32_t)ile32(h + 4);
        uint64_t payload;
        uncomp = ile32(h + 8);
        if (uncomp == 0U || uncomp > INNO_BLOCK_CAP || (csize != -1 && csize <= 0))
            return false;
        payload = csize == -1 ? uncomp : (uint32_t)csize;
        stored = payload + ((payload + 4095U) / 4096U) * 4U;
        compressed = csize != -1;
    } else {
        stored = wide ? ile64(h + 4) : ile32(h + 4);
        if (h[hsize - 1U] > 1U) return false;
        compressed = h[hsize - 1U] == 1U;
    }
    if (stored == 0U || stored > INNO_STORED_CAP ||
        stored > (uint64_t)(limit - off - (int64_t)hsize))
        return false;
    raw = (uint8_t *)xx_mem_alloc((size_t)stored);
    if (!raw) return false;
    if (!inno_read_at(f->device, f->base_address + off + hsize, raw, (size_t)stored) ||
        !inno_strip_crc(raw, (size_t)stored, &plain) || plain == 0U)
        goto done;
    if (!compressed) {
        if (old && plain != uncomp) goto done;
        out->p = raw;
        out->n = plain;
        raw = NULL;
        ok = true;
    } else if (ver->v < IV(4, 1, 6, 0)) {
        ok = inno_zlib_memory(raw, plain, old ? uncomp : 0U, out, pd);
    } else {
        ok = inno_lzma_memory(raw, plain, out, pd);
    }
    if (ok) *consumed = (int64_t)hsize + (int64_t)stored;
done:
    if (raw) xx_mem_free(raw);
    return ok;
}

/* 1.09 / 1.11 record block: Adler-32 of the next 12 bytes, compressed size
 * (0xFFFFFFFF: stored), uncompressed size, Adler-32 of the data. */
static bool inno_read_legacy_block(Abstractformat *f, int64_t off, int64_t limit,
                                   ibuf *out, int64_t *consumed, xx_pd_struct *pd) {
    uint8_t h[16];
    uint32_t csize, usize, adler;
    uint64_t payload;
    uint8_t *raw;
    bool ok = false;
    xx_mem_zero(out, sizeof(*out));
    if (off < 0 || limit - off < 16 || !inno_read_at(f->device, f->base_address + off, h, 16U))
        return false;
    if (xx_adler32(h + 4, 12U) != ile32(h)) return false;
    csize = ile32(h + 4);
    usize = ile32(h + 8);
    adler = ile32(h + 12);
    payload = csize == 0xFFFFFFFFU ? usize : csize;
    if (usize == 0U || usize > INNO_LEGACY_BLOCK_CAP || payload == 0U ||
        payload > INNO_LEGACY_BLOCK_CAP || payload > (uint64_t)(limit - off - 16))
        return false;
    raw = (uint8_t *)xx_mem_alloc((size_t)payload);
    if (!raw) return false;
    if (inno_read_at(f->device, f->base_address + off + 16, raw, (size_t)payload)) {
        if (csize == 0xFFFFFFFFU) {
            out->p = raw;
            out->n = usize;
            raw = NULL;
            ok = true;
        } else {
            ok = inno_zlib_memory(raw, (size_t)payload, usize, out, pd);
        }
    }
    if (raw) xx_mem_free(raw);
    if (ok && xx_adler32(out->p, out->n) != adler) {
        ibuf_free(out);
        ok = false;
    }
    if (ok) *consumed = 16 + (int64_t)payload;
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Parsed setup data                                                       */

typedef struct inno_raw_s {
    const uint8_t *name;
    uint32_t len;
    bool wide;
    uint32_t loc;
} inno_raw;

typedef struct inno_rawlist_s {
    inno_raw *items;
    uint32_t count;
    uint32_t cap;
} inno_rawlist;

static bool inno_raw_add(inno_rawlist *l, const uint8_t *name, uint32_t len, bool wide,
                         uint32_t loc) {
    if (l->count == l->cap) {
        uint32_t cap = l->cap ? l->cap * 2U : 64U;
        inno_raw *grown;
        if (l->count >= INNO_MAX_FILES) return false;
        if (cap > INNO_MAX_FILES) cap = INNO_MAX_FILES;
        grown = (inno_raw *)xx_mem_realloc(l->items, (size_t)cap * sizeof(*grown));
        if (!grown) return false;
        l->items = grown;
        l->cap = cap;
    }
    l->items[l->count].name = name;
    l->items[l->count].len = len;
    l->items[l->count].wide = wide;
    l->items[l->count].loc = loc;
    ++l->count;
    return true;
}

typedef struct inno_hdr_s {
    uint32_t counts[17];
    int ncounts;
    uint32_t file_count;
    uint32_t loc_count;
    uint8_t compression;
    bool encryption_used;
    size_t end;
} inno_hdr;

static bool inno_counts(icur *c, inno_hdr *h, int n) {
    int i;
    if (!ic_skip(c, (uint64_t)n * 4U)) return false;
    for (i = 0; i < n; ++i) {
        h->counts[i] = ile32(c->p + c->o - (size_t)(n - i) * 4U);
        if (h->counts[i] > INNO_MAX_COUNT) return false;
    }
    h->ncounts = n;
    return true;
}

/* 1.3 .. 4.2 setup header (ISX adds one string and four bytes). */
static bool inno_hdr_pre5(icur *c, const inno_ver *ver, inno_hdr *h) {
    uint32_t v = ver->v, wvr = v >= IV(1, 3, 19, 0) ? 20U : 8U;
    int nstr = 12, ncount = 10, prefix = 0, nflags = 0, bzip_flag = -1, enc_flag = -1;
    int i, obytes;
    size_t comp_at = (size_t)-1, opt_at;
    bool fl[64];
    int nfl = 0;
    xx_mem_zero(h, sizeof(*h));
    if (v < IV(3, 0, 0, 0)) ++nstr;
    nstr += 3;
    if (v >= IV(1, 3, 3, 0)) ++nstr;
    if (v >= IV(1, 3, 6, 0)) nstr += 2;
    if (v >= IV(1, 3, 14, 0)) ++nstr;
    if (v >= IV(3, 0, 0, 0)) nstr += 2;
    if (v >= IV(4, 0, 0, 0)) nstr += 2;
    if (v >= IV(4, 2, 4, 0)) nstr += 4;
    if (ver->isx) ++nstr;
    if (!ic_strs(c, nstr)) return false;
    if (v >= IV(2, 0, 6, 0) && !ic_skip(c, 32U)) return false;
    if (v >= IV(4, 0, 0, 0)) { ++ncount; ++prefix; }
    if (v >= IV(4, 2, 1, 0)) { ++ncount; ++prefix; }
    if (v >= IV(4, 1, 0, 0)) { ++ncount; ++prefix; }
    if (v >= IV(2, 0, 0, 0)) { ncount += 3; prefix += 3; }
    if (!inno_counts(c, h, ncount)) return false;
    h->file_count = h->counts[prefix + 1];
    h->loc_count = h->counts[prefix + 2];
    if (!ic_skip(c, wvr + 4U) || (v >= IV(1, 3, 3, 0) && !ic_skip(c, 4U)) ||
        !ic_skip(c, 4U) || (v >= IV(2, 0, 0, 0) && !ic_skip(c, 4U)) ||
        !ic_skip(c, v >= IV(4, 2, 0, 0) ? 16U : 4U) ||
        (v >= IV(4, 2, 2, 0) && !ic_skip(c, 8U)) ||
        !ic_skip(c, v >= IV(4, 0, 0, 0) ? 12U : 4U) ||
        (v >= IV(2, 0, 0, 0) && !ic_skip(c, 1U)) || !ic_skip(c, 1U) ||
        (v >= IV(2, 0, 0, 0) && !ic_skip(c, 1U)) ||
        (v >= IV(1, 3, 6, 0) && !ic_skip(c, 1U)) ||
        (v >= IV(3, 0, 0, 0) && v < IV(3, 0, 3, 0) && !ic_skip(c, 1U)) ||
        (v >= IV(3, 0, 4, 0) && !ic_skip(c, 1U)) ||
        (v >= IV(4, 0, 10, 0) && !ic_skip(c, 2U)))
        return false;
    if (v >= IV(4, 1, 5, 0)) {
        comp_at = c->o;
        if (!ic_skip(c, 1U)) return false;
    }
    if (ver->isx && !ic_skip(c, 4U)) return false;
    /* Header option bits, in declaration order. */
#define FL(x) (fl[nfl++] = (x))
    FL(true); FL(true); FL(true); FL(true);
    FL(v < IV(1, 3, 6, 0));
    FL(true); FL(true);
    FL(v < IV(3, 0, 0, 0) || v >= IV(3, 0, 3, 0));
    FL(v < IV(1, 3, 3, 0));
    FL(true); FL(true); FL(true); FL(true); FL(true); FL(true);
    FL(v < IV(4, 1, 2, 0));
    FL(true); FL(true); FL(true);
    FL(v < IV(3, 0, 4, 0));
    FL(v < IV(3, 0, 0, 0));
    FL(v < IV(1, 3, 6, 0));
    FL(true); FL(true); FL(true);
    FL(v >= IV(1, 3, 3, 0));
    FL(v >= IV(1, 3, 10, 0));
    FL(v >= IV(1, 3, 20, 0));
    FL(v >= IV(2, 0, 0, 0));
    for (i = 0; i < 6; ++i) FL(v >= IV(2, 0, 0, 0));
    FL(v >= IV(2, 0, 7, 0));
    FL(v >= IV(2, 0, 7, 0));
    if (v >= IV(2, 0, 17, 0) && v < IV(4, 1, 5, 0)) {
        for (i = 0; i < nfl; ++i)
            if (fl[i]) ++nflags;
        bzip_flag = nflags;
        nflags = 0;
        FL(true);
    }
    FL(v >= IV(2, 0, 18, 0));
    FL(v >= IV(3, 0, 0, 0));
    FL(v >= IV(3, 0, 0, 0));
    FL(v >= IV(3, 0, 1, 0));
    FL(v >= IV(3, 0, 3, 0));
    FL(v >= IV(4, 0, 0, 0));
    FL(v >= IV(4, 0, 0, 0) && v < IV(4, 0, 10, 0));
    FL(v >= IV(4, 0, 1, 0) && v < IV(4, 0, 10, 0));
    FL(v >= IV(4, 0, 9, 0));
    FL(v >= IV(4, 1, 3, 0));
    FL(v >= IV(4, 1, 8, 0));
    FL(v >= IV(4, 1, 8, 0));
    if (v >= IV(4, 2, 2, 0)) {
        for (i = 0; i < nfl; ++i)
            if (fl[i]) ++nflags;
        enc_flag = nflags;
        nflags = 0;
        FL(true);
    }
#undef FL
    for (i = 0; i < nfl; ++i)
        if (fl[i]) ++nflags;
    obytes = (nflags + 7) / 8;
    if (obytes == 3) obytes = 4;
    opt_at = c->o;
    if (!ic_skip(c, (uint64_t)obytes)) return false;
    if (comp_at != (size_t)-1) {
        uint8_t m = c->p[comp_at];
        if (v < IV(4, 2, 5, 0)) {
            static const uint8_t map[3] = {INNO_C_ZLIB, INNO_C_BZIP2, INNO_C_LZMA1};
            if (m > 2U) return false;
            h->compression = map[m];
        } else if (v < IV(4, 2, 6, 0)) {
            static const uint8_t map[3] = {INNO_C_STORE, INNO_C_BZIP2, INNO_C_LZMA1};
            if (m > 2U) return false;
            h->compression = map[m];
        } else {
            if (m > INNO_C_LZMA1) return false;
            h->compression = m;
        }
    } else {
        bool bzip = bzip_flag >= 0 &&
                    (c->p[opt_at + (size_t)bzip_flag / 8U] & (1U << (bzip_flag & 7))) != 0;
        h->compression = bzip ? INNO_C_BZIP2 : INNO_C_ZLIB;
    }
    h->encryption_used = enc_flag >= 0 &&
                         (c->p[opt_at + (size_t)enc_flag / 8U] & (1U << (enc_flag & 7))) != 0;
    h->end = c->o;
    return true;
}

/* 1.3 .. 4.2: every array up to the end of the block.  Collects the file
 * entries' destination strings and location indices. */
static bool inno_files_pre5(icur *c, const inno_ver *ver, const inno_hdr *h,
                            bool exact, inno_rawlist *out) {
    uint32_t v = ver->v, wvr = v >= IV(1, 3, 19, 0) ? 20U : 8U, i;
    int ci = 0, lang = -1, msg = -1, perm = -1, type = -1, comp = -1, task = -1;
    int dir, file, data, icon, ini, reg, del, undel, run, unrun, x = ver->isx ? 1 : 0;
    int cond = 0, fflags, fbytes;
    uint32_t ffixed;
    if (v >= IV(4, 0, 0, 0)) lang = ci++;
    if (v >= IV(4, 2, 1, 0)) msg = ci++;
    if (v >= IV(4, 1, 0, 0)) perm = ci++;
    if (v >= IV(2, 0, 0, 0)) {
        type = ci++;
        comp = ci++;
        task = ci++;
    }
    dir = ci++; file = ci++; data = ci++; icon = ci++; ini = ci++; reg = ci++;
    del = ci++; undel = ci++; run = ci++; unrun = ci++;
    if (ci != h->ncounts || h->counts[file] != h->file_count ||
        h->counts[data] != h->loc_count)
        return false;
    if (lang >= 0) {
        int strings = 7 + (v >= IV(4, 0, 1, 0) ? 3 : 0);
        uint32_t fixed = 4U + (v >= IV(4, 2, 2, 0) ? 4U : 0U) + (v < IV(4, 1, 0, 0) ? 20U : 16U);
        if (!ic_entries(c, h->counts[lang], 0, strings, fixed)) return false;
    } else if (v >= IV(2, 0, 1, 0)) {
        if (!ic_entries(c, 1U, 0, 5, 24U)) return false;
    }
    if (v < IV(4, 0, 0, 0)) {
        if (!ic_strs(c, 1) || (v >= IV(2, 0, 0, 0) && !ic_strs(c, 1)) ||
            (h->compression == INNO_C_BZIP2 && !ic_strs(c, 1)))
            return false;
    }
    if (msg >= 0 && !ic_entries(c, h->counts[msg], 0, 2, 4U)) return false;
    if (perm >= 0 && !ic_entries(c, h->counts[perm], 0, 1, 0U)) return false;
    if (type >= 0) {
        int ts = 2 + (v >= IV(4, 0, 0, 0)) + (v >= IV(4, 0, 1, 0)) + x;
        uint32_t tf = wvr + 1U + (v >= IV(4, 0, 3, 0) ? 1U : 0U) + (v >= IV(4, 0, 0, 0) ? 8U : 4U);
        int cs = 3 + (v >= IV(4, 0, 0, 0)) + (v >= IV(4, 0, 1, 0)) + x;
        uint32_t cf = (v >= IV(4, 0, 0, 0) ? 8U : 4U) + (v >= IV(4, 0, 0, 0) ? 5U : 0U) +
                      wvr + 1U + (v >= IV(4, 0, 0, 0) ? 8U : 4U);
        int ks = 4 + (v >= IV(4, 0, 0, 0)) + (v >= IV(4, 0, 1, 0)) + x;
        uint32_t kf = (v >= IV(4, 0, 0, 0) ? 5U : 0U) + wvr + 1U;
        if (!ic_entries(c, h->counts[type], 0, ts, tf) ||
            !ic_entries(c, h->counts[comp], 0, cs, cf) ||
            !ic_entries(c, h->counts[task], 0, ks, kf))
            return false;
    }
    if (v >= IV(2, 0, 0, 0)) cond += 2;
    if (v >= IV(4, 0, 0, 0)) ++cond;
    if (v >= IV(4, 0, 1, 0)) ++cond;
    if (v >= IV(4, 1, 0, 0)) cond += 2;
    cond += x;
    if (!ic_entries(c, h->counts[dir], 0,
                    1 + cond + (v >= IV(4, 0, 11, 0) && v < IV(4, 1, 0, 0)),
                    wvr + 1U + (v >= IV(2, 0, 11, 0) ? 4U : 0U) + (v >= IV(4, 1, 0, 0) ? 2U : 0U)))
        return false;
    fflags = 11;
    if (v < IV(2, 0, 0, 0)) ++fflags;
    if (v >= IV(1, 3, 21, 0)) fflags += 2;
    if (v >= IV(1, 3, 25, 0)) ++fflags;
    if (v >= IV(2, 0, 5, 0)) ++fflags;
    if (v >= IV(3, 0, 1, 0)) ++fflags;
    if (v >= IV(3, 0, 5, 0)) fflags += 3;
    if (v >= IV(4, 0, 0, 0)) ++fflags;
    if (v >= IV(4, 0, 5, 0)) ++fflags;
    if (v >= IV(4, 1, 8, 0)) ++fflags;
    if (v >= IV(4, 2, 1, 0)) ++fflags;
    if (v >= IV(4, 2, 5, 0)) ++fflags;
    fbytes = (fflags + 7) / 8;
    if (fbytes == 3) fbytes = 4;
    ffixed = wvr + 4U + 4U + (v >= IV(4, 0, 0, 0) ? 8U : 4U) + (v < IV(3, 0, 5, 0) ? 1U : 0U) +
             (v >= IV(4, 1, 0, 0) ? 2U : 0U) + (uint32_t)fbytes + 1U;
    for (i = 0U; i < h->file_count; ++i) {
        const uint8_t *name = NULL;
        uint32_t len = 0U, loc;
        int j;
        size_t tail;
        for (j = 0; j < 3 + cond; ++j) {
            const uint8_t *s;
            uint32_t l;
            if (!ic_str(c, &s, &l)) return false;
            if (j == 1) {
                name = s;
                len = l;
            }
        }
        tail = c->o;
        if (!ic_skip(c, ffixed)) return false;
        loc = ile32(c->p + tail + wvr);
        if (c->p[tail + ffixed - 1U] > 2U ||
            (loc != 0xFFFFFFFFU && loc >= h->loc_count))
            return false;
        if (len && loc != 0xFFFFFFFFU && !inno_raw_add(out, name, len, false, loc))
            return false;
    }
    {
        int ics = 6 + cond;
        uint32_t icf = wvr + 4U + (v >= IV(1, 3, 24, 0) ? 4U : 0U) +
                       (v >= IV(1, 3, 15, 0) ? 1U : 0U) + (v >= IV(2, 0, 7, 0) ? 2U : 0U) + 1U;
        int rs = 3 + cond + (v >= IV(4, 0, 11, 0) && v < IV(4, 1, 0, 0));
        uint32_t rf = wvr + 4U + (v >= IV(4, 1, 0, 0) ? 2U : 0U) + 1U +
                      (v >= IV(1, 3, 12, 0) ? 2U : 1U);
        int runs = 3 + cond + (v >= IV(1, 3, 9, 0)) + (v >= IV(2, 0, 2, 0)) +
                   (v >= IV(2, 0, 0, 0));
        uint32_t runf = wvr + (v >= IV(1, 3, 24, 0) ? 4U : 0U) + 2U;
        if (!ic_entries(c, h->counts[icon], 0, ics, icf) ||
            !ic_entries(c, h->counts[ini], 0, 4 + cond, wvr + 1U) ||
            !ic_entries(c, h->counts[reg], 0, rs, rf) ||
            !ic_entries(c, h->counts[del], 0, 1 + cond, wvr + 1U) ||
            !ic_entries(c, h->counts[undel], 0, 1 + cond, wvr + 1U) ||
            !ic_entries(c, h->counts[run], 0, runs, runf) ||
            !ic_entries(c, h->counts[unrun], 0, runs, runf))
            return false;
    }
    if (v >= IV(4, 0, 0, 0)) {
        bool decompressor = h->compression == INNO_C_BZIP2 ||
                            (v == IV(4, 1, 5, 0) && h->compression == INNO_C_LZMA1) ||
                            (v >= IV(4, 2, 6, 0) && h->compression == INNO_C_ZLIB);
        if (!ic_strs(c, 2) || (decompressor && !ic_strs(c, 1)) ||
            (h->encryption_used && !ic_strs(c, 1)))
            return false;
    }
    if (exact) return c->o == c->n;
    return c->n - c->o <= 16U;
}

/* 1.2.10 (native 16- or 32-bit compiler): Longint-sized records. */
static bool inno_hdr_1210(icur *c, const inno_ver *ver, inno_hdr *h) {
    uint32_t tail = ver->win16 ? 53U : 80U, width = ver->win16 ? 2U : 4U, i;
    uint32_t lic, before, after, image;
    size_t tail_at;
    xx_mem_zero(h, sizeof(*h));
    if (!ic_sized(c, 7, tail, NULL, NULL, &tail_at)) return false;
    for (i = 0U; i < 10U; ++i) {
        uint32_t n;
        if (ver->win16) {
            int16_t s = (int16_t)ile16(c->p + tail_at + i * width);
            if (s < 0) return false;
            n = (uint32_t)s;
        } else {
            n = ile32(c->p + tail_at + i * width);
        }
        if (n > INNO_MAX_COUNT) return false;
        h->counts[i] = n;
    }
    h->ncounts = 10;
    h->file_count = h->counts[1];
    h->loc_count = h->counts[2];
    if (ver->win16 ? (c->p[tail_at + 52U] & 0xF8U) != 0
                   : (ile32(c->p + tail_at + 76U) & 0xFF800000U) != 0)
        return false;
    if (ver->win16) {
        lic = ile16(c->p + tail_at + 20U);
        before = ile16(c->p + tail_at + 22U);
        after = ile16(c->p + tail_at + 24U);
    } else {
        lic = ile32(c->p + tail_at + 40U);
        before = ile32(c->p + tail_at + 44U);
        after = ile32(c->p + tail_at + 48U);
    }
    if (!ic_skip(c, lic) || !ic_skip(c, before) || !ic_skip(c, after) || c->n - c->o < 4U)
        return false;
    image = ile32(c->p + c->o);
    if (image > 0x7FFFFFFFU || !ic_skip(c, 4U) || !ic_skip(c, image)) return false;
    h->compression = INNO_C_ZLIB;
    h->end = c->o;
    return true;
}

static bool inno_files_1210(icur *c, const inno_ver *ver, const inno_hdr *h,
                            inno_rawlist *out) {
    bool w16 = ver->win16;
    uint32_t i, k;
    size_t t;
    for (i = 0U; i < h->counts[0]; ++i)
        if (!ic_sized(c, 1, 9U, NULL, NULL, &t) || (c->p[t + 8U] & 0xF8U)) return false;
    for (i = 0U; i < h->file_count; ++i) {
        const uint8_t *name = NULL;
        uint32_t len = 0U, ftail = w16 ? 20U : 24U;
        int32_t loc;
        if (!ic_sized(c, 3, ftail, &name, &len, &t)) return false;
        loc = w16 ? (int32_t)(int16_t)ile16(c->p + t + 8U) : (int32_t)ile32(c->p + t + 8U);
        if (c->p[t + (w16 ? 16U : 20U)] > 3U ||
            (c->p[t + (w16 ? 18U : 22U)] & (w16 ? 0xFEU : 0xF0U)) ||
            c->p[t + ftail - 1U] > (w16 ? 1U : 2U) || loc < -1 ||
            (loc >= 0 && (uint32_t)loc >= h->loc_count))
            return false;
        if (len && loc >= 0 && !inno_raw_add(out, name, len, false, (uint32_t)loc))
            return false;
    }
    for (i = 0U; i < h->counts[3]; ++i) {
        uint32_t it = w16 ? 11U : 13U;
        if (!ic_sized(c, 6, it, NULL, NULL, &t) ||
            (c->p[t + it - 1U] & (w16 ? 0xF8U : 0xF0U)))
            return false;
    }
    for (i = 0U; i < h->counts[4]; ++i)
        if (!ic_sized(c, 4, 9U, NULL, NULL, &t) || (c->p[t + 8U] & 0xE0U)) return false;
    for (i = 0U; i < h->counts[5]; ++i) {
        if (!ic_sized(c, w16 ? 2 : 3, w16 ? 10U : 14U, NULL, NULL, &t) ||
            c->p[t + (w16 ? 8U : 12U)] > (w16 ? 1U : 5U) ||
            (c->p[t + (w16 ? 9U : 13U)] & (w16 ? 0xF0U : 0xC0U)))
            return false;
    }
    for (k = 6U; k <= 7U; ++k)
        for (i = 0U; i < h->counts[k]; ++i)
            if (!ic_sized(c, 1, 9U, NULL, NULL, &t) || c->p[t + 8U] > 2U) return false;
    for (k = 8U; k <= 9U; ++k)
        for (i = 0U; i < h->counts[k]; ++i)
            if (!ic_sized(c, 3, 10U, NULL, NULL, &t) || c->p[t + 8U] > 2U ||
                (c->p[t + 9U] & 0xFEU))
                return false;
    return c->o == c->n;
}

/* 5.0 .. 7.x setup header.  CloseApplicationsFilterExcludes is new in
 * 6.4.2: installers with the "6.4.0.1" ID do not have it. */
static bool inno_hdr_5(icur *c, const inno_ver *ver, inno_hdr *h) {
    uint32_t v = ver->v;
    uint8_t m;
    xx_mem_zero(h, sizeof(*h));
    if (!ic_strs(c, 6) || (v >= IV(5, 1, 13, 0) && !ic_strs(c, 1)) || !ic_strs(c, 6) ||
        (v < IV(5, 2, 5, 0) && !ic_strs(c, 3)) || !ic_strs(c, 7) ||
        (v < IV(5, 2, 5, 0) && !ic_strs(c, 1)) || !ic_strs(c, 4) ||
        (v >= IV(5, 3, 8, 0) && !ic_strs(c, 1)) || (v >= IV(5, 3, 10, 0) && !ic_strs(c, 1)) ||
        (v >= IV(5, 5, 0, 0) && !ic_strs(c, 1)) || (v >= IV(5, 5, 6, 0) && !ic_strs(c, 1)) ||
        (v >= IV(5, 6, 1, 0) && !ic_strs(c, 2)) || (v >= IV(6, 3, 0, 0) && !ic_strs(c, 2)) ||
        (v >= IV(6, 4, 2, 0) && !ic_strs(c, 1)) || (v >= IV(6, 7, 0, 0) && !ic_strs(c, 6)) ||
        (v >= IV(6, 5, 0, 0) && v < IV(6, 7, 0, 0) && !ic_strs(c, 1)) ||
        (v >= IV(5, 2, 5, 0) && !ic_strs(c, 3)) ||
        (v >= IV(5, 2, 1, 0) && v < IV(5, 3, 10, 0) && !ic_strs(c, 1)) ||
        (v >= IV(5, 2, 5, 0) && !ic_strs(c, 1)))
        return false;
    if (!ver->unicode && !ic_skip(c, 32U)) return false;
    if (!inno_counts(c, h, v >= IV(6, 5, 0, 0) ? 17 : 16)) return false;
    h->file_count = h->counts[h->ncounts == 17 ? 8 : 7];
    h->loc_count = h->counts[h->ncounts == 17 ? 9 : 8];
    if ((v >> 24) >= 7U && !ic_skip(c, 4U)) return false;
    if (v >= IV(6, 7, 0, 0)) {
        if (!ic_skip(c, 75U)) return false;
    } else if (v >= IV(6, 6, 1, 0)) {
        if (!ic_skip(c, 65U)) return false;
    } else if (v >= IV(6, 6, 0, 0)) {
        if (!ic_skip(c, 64U)) return false;
    } else if (v >= IV(6, 5, 2, 0)) {
        if (!ic_skip(c, 56U)) return false;
    } else if (v >= IV(6, 5, 0, 0)) {
        if (!ic_skip(c, 48U)) return false;
    } else {
        if (!ic_skip(c, 20U) || (v < IV(6, 4, 0, 1) && !ic_skip(c, 8U)) ||
            (v < IV(5, 5, 7, 0) && !ic_skip(c, 4U)) || (v < IV(5, 0, 4, 0) && !ic_skip(c, 4U)) ||
            (v >= IV(6, 0, 0, 0) && !ic_skip(c, 9U)) || (v >= IV(5, 5, 7, 0) && !ic_skip(c, 1U)))
            return false;
        if (v >= IV(6, 4, 0, 0)) {
            if (!ic_skip(c, 48U)) return false;
        } else if (v >= IV(5, 3, 9, 0)) {
            if (!ic_skip(c, 28U)) return false;
        } else {
            if (!ic_skip(c, 24U)) return false;
        }
        if (!ic_skip(c, 12U + 3U) || (v >= IV(5, 7, 0, 0) && !ic_skip(c, 1U)) || !ic_skip(c, 2U))
            return false;
    }
    if (c->o >= c->n) return false;
    m = c->p[c->o++];
    if (m > (v >= IV(5, 3, 9, 0) ? 4U : 3U)) return false;
    h->compression = m;
    if (v >= IV(6, 7, 0, 0)) {
        if (!ic_skip(c, 18U)) return false;
    } else if (v >= IV(6, 5, 0, 0)) {
        if (!ic_skip(c, 16U)) return false;
    } else {
        if ((v >= IV(5, 1, 0, 0) && v < IV(6, 3, 0, 0) && !ic_skip(c, 2U)) ||
            (v >= IV(5, 2, 1, 0) && v < IV(5, 3, 10, 0) && !ic_skip(c, 8U)) ||
            (v >= IV(5, 3, 3, 0) && !ic_skip(c, 2U)))
            return false;
        if (v >= IV(5, 5, 0, 0)) {
            if (!ic_skip(c, 8U)) return false;
        } else if (v >= IV(5, 3, 6, 0) && !ic_skip(c, 4U)) {
            return false;
        }
        if (!ic_skip(c, (v >= IV(6, 3, 0, 0) && v < IV(6, 4, 0, 0)) ? 7U : 6U)) return false;
    }
    h->end = c->o;
    return true;
}

/* 5.x ANSI builds: arrays up to and including [Files]. */
static bool inno_files_5ansi(icur *c, const inno_ver *ver, const inno_hdr *h,
                             inno_rawlist *out) {
    uint32_t v = ver->v, i;
    int nstr = v >= IV(5, 2, 5, 0) ? 10 : 9;
    if (!ic_entries(c, h->counts[0], 0, 10, 24U + (v >= IV(5, 2, 3, 0) ? 1U : 0U)) ||
        !ic_entries(c, h->counts[1], 0, 2, 4U) || !ic_entries(c, h->counts[2], 0, 1, 0U) ||
        !ic_entries(c, h->counts[3], 0, 4, 30U) || !ic_entries(c, h->counts[4], 0, 5, 42U) ||
        !ic_entries(c, h->counts[5], 0, 6, 26U) || !ic_entries(c, h->counts[6], 0, 7, 27U))
        return false;
    for (i = 0U; i < h->file_count; ++i) {
        const uint8_t *name = NULL;
        uint32_t len = 0U, loc;
        size_t tail;
        int j;
        for (j = 0; j < nstr; ++j) {
            const uint8_t *s;
            uint32_t l;
            if (!ic_str(c, &s, &l)) return false;
            if (j == 1) {
                name = s;
                len = l;
            }
        }
        tail = c->o;
        if (!ic_skip(c, 43U)) return false;
        loc = ile32(c->p + tail + 20U);
        if (c->p[tail + 42U] > 1U || (loc != 0xFFFFFFFFU && loc >= h->loc_count)) return false;
        if (len && loc != 0xFFFFFFFFU && !inno_raw_add(out, name, len, false, loc))
            return false;
    }
    return true;
}

/* 5.2.5 .. 6.x Unicode builds (UTF-16 strings). */
static bool inno_files_unicode(icur *c, const inno_ver *ver, const inno_hdr *h, bool rev2,
                               inno_rawlist *out) {
    uint32_t v = ver->v, i, major = v >> 24, minor = (v >> 16) & 0xFFU;
    bool legacy = !rev2 && (major == 5U || (major == 6U && minor <= 4U));
    bool modern = rev2 && ((major == 6U && minor >= 5U) || major == 7U);
    int fwide, fansi = 0;
    uint32_t ffixed, loc_at, type_at, verify_at = 0xFFFFFFFFU, bits_at = 0xFFFFFFFFU;
    if ((!legacy && !modern) || (legacy && h->ncounts != 16) || (modern && h->ncounts != 17))
        return false;
    if (legacy) {
        if (!ic_entries(c, h->counts[0], 6, 4, v < IV(5, 3, 0, 0) ? 25U : 21U) ||
            !ic_entries(c, h->counts[1], 2, 0, 4U) || !ic_entries(c, h->counts[2], 0, 1, 0U) ||
            !ic_entries(c, h->counts[3], 4, 0, 30U) || !ic_entries(c, h->counts[4], 5, 0, 42U) ||
            !ic_entries(c, h->counts[5], 6, 0, 26U) || !ic_entries(c, h->counts[6], 7, 0, 27U))
            return false;
        fwide = 10;
        ffixed = 43U;
        loc_at = 20U;
        type_at = 42U;
    } else {
        int lw = 6;
        uint32_t lf = 21U, cf = 42U, tf = 26U;
        if (v >= IV(6, 6, 0, 0)) {
            lw = 4;
            lf = 19U;
        }
        if (v >= IV(6, 7, 0, 0)) {
            cf = 39U;
            tf = 23U;
        }
        if (!ic_entries(c, h->counts[0], lw, 4, lf) || !ic_entries(c, h->counts[1], 2, 0, 4U) ||
            !ic_entries(c, h->counts[2], 0, 1, 0U) || !ic_entries(c, h->counts[3], 4, 0, 30U) ||
            !ic_entries(c, h->counts[4], 5, 0, cf) || !ic_entries(c, h->counts[5], 6, 0, tf) ||
            !ic_entries(c, h->counts[6], 7, 0, 27U) || !ic_entries(c, h->counts[7], 3, 0, 0U))
            return false;
        fwide = 15;
        fansi = 1;
        ffixed = v >= IV(6, 7, 0, 0) ? (major >= 7U ? 81U : 80U) : 77U;
        verify_at = 32U;
        loc_at = 53U;
        bits_at = major >= 7U ? 71U : 0xFFFFFFFFU;
        type_at = ffixed - 1U;
    }
    for (i = 0U; i < h->file_count; ++i) {
        const uint8_t *name = NULL;
        uint32_t len = 0U, loc;
        size_t tail;
        int j;
        for (j = 0; j < fwide; ++j) {
            const uint8_t *s;
            uint32_t l;
            if (!ic_wstr(c, &s, &l)) return false;
            if (j == 1) {
                name = s;
                len = l;
            }
        }
        for (j = 0; j < fansi; ++j)
            if (!ic_str(c, NULL, NULL)) return false;
        tail = c->o;
        if (!ic_skip(c, ffixed)) return false;
        loc = ile32(c->p + tail + loc_at);
        if (c->p[tail + type_at] > 1U ||
            (verify_at != 0xFFFFFFFFU && c->p[tail + verify_at] > 2U) ||
            (bits_at != 0xFFFFFFFFU && c->p[tail + bits_at] > 4U) ||
            (loc != 0xFFFFFFFFU && loc >= h->loc_count))
            return false;
        if (len && loc != 0xFFFFFFFFU && !inno_raw_add(out, name, len, true, loc))
            return false;
    }
    return true;
}

/* File location table (setup-0 second block). */
static bool inno_parse_locs(const ibuf *b, const inno_ver *ver, bool rev2, uint32_t count,
                            uint8_t hdr_comp, inno_loc **out) {
    uint32_t v = ver->v, i, esize = 0U, digest = 4U, time_at = 0U, flags_at = 0U,
             flags_size = 1U, digest_at = 36U, shift = 0U;
    uint8_t hkind = INNO_H_NONE;
    inno_loc *locs;
    bool wide_start = false;
    *out = NULL;
    if (!count || hdr_comp == INNO_C_UNKNOWN) return false;
    if (v < IV(5, 0, 0, 0)) {
        if (v == IV(1, 2, 10, 0) && ver->win16) {
            esize = 33U;
        } else if (v < IV(4, 0, 0, 0)) {
            esize = 41U; time_at = 24U; flags_at = 40U; hkind = INNO_H_ADLER32; digest_at = 20U;
        } else if (v == IV(4, 0, 0, 0)) {
            esize = 49U; time_at = 32U; flags_at = 48U; hkind = INNO_H_ADLER32; digest_at = 28U;
        } else if (v < IV(4, 2, 0, 0)) {
            esize = 57U; time_at = 40U; flags_at = 56U; hkind = INNO_H_CRC32; digest_at = 36U;
        } else {
            esize = 69U; digest = 16U; time_at = 52U; flags_at = 68U; hkind = INNO_H_MD5;
            digest_at = 36U;
        }
    } else if (rev2) {
        if (v < IV(6, 5, 0, 0)) return false;
        wide_start = v >= IV(6, 5, 2, 0);
        shift = wide_start ? 4U : 0U;
        esize = wide_start ? 89U : 85U;
        digest_at = 36U + shift; digest = 32U; time_at = 68U + shift; flags_at = 84U + shift;
        hkind = INNO_H_SHA256;
    } else if (v >= IV(6, 4, 3, 0)) {
        esize = 85U; digest = 32U; time_at = 68U; flags_at = 84U; hkind = INNO_H_SHA256;
    } else if (v >= IV(6, 4, 0, 0)) {
        esize = 87U; digest = 32U; time_at = 68U; flags_at = 84U; flags_size = 2U;
        hkind = INNO_H_SHA256;
    } else if (v >= IV(6, 3, 0, 0)) {
        esize = 75U; digest = 20U; time_at = 56U; flags_at = 72U; flags_size = 2U;
        hkind = INNO_H_SHA1;
    } else if (v >= IV(5, 3, 9, 0)) {
        esize = 74U; digest = 20U; time_at = 56U; flags_at = 72U; flags_size = 2U;
        hkind = INNO_H_SHA1;
    } else if (v >= IV(5, 1, 13, 0)) {
        esize = 70U; digest = 16U; time_at = 52U; flags_at = 68U; flags_size = 2U;
        hkind = INNO_H_MD5;
    } else {
        esize = 69U; digest = 16U; time_at = 52U; flags_at = 68U; hkind = INNO_H_MD5;
    }
    if ((uint64_t)count * esize > b->n) return false;
    if ((uint64_t)count * esize != b->n && !(b->inexact && b->n - (uint64_t)count * esize <= 16U))
        return false;
    locs = (inno_loc *)xx_mem_calloc(count, sizeof(*locs));
    if (!locs) return false;
    for (i = 0U; i < count; ++i) {
        const uint8_t *e = b->p + (size_t)i * esize;
        inno_loc *l = &locs[i];
        uint32_t flags;
        if (esize == 33U) {
            int16_t first = (int16_t)ile16(e), last = (int16_t)ile16(e + 2);
            int32_t start = (int32_t)ile32(e + 4), osize = (int32_t)ile32(e + 8),
                    csize = (int32_t)ile32(e + 12);
            if (first <= 0 || last < first || start < 12 || osize < 0 || csize < 0 ||
                (e[32] & ~0x03U))
                goto fail;
            l->first_slice = (uint32_t)(first - 1);
            l->last_slice = (uint32_t)(last - 1);
            l->chunk_offset = start;
            l->size = osize;
            l->chunk_size = csize;
            xx_rt_memcpy(l->checksum, e + 16, 4U);
            l->hash_kind = INNO_H_ADLER32;
            l->filetime = ile32(e + 20);
            l->compression = INNO_C_ZLIB;
            continue;
        }
        if (v < IV(5, 0, 0, 0)) {
            int32_t first = (int32_t)ile32(e), last = (int32_t)ile32(e + 4);
            if (v < IV(4, 0, 0, 0)) {
                if (first <= 0 || last < first) goto fail;
                l->first_slice = (uint32_t)(first - 1);
                l->last_slice = (uint32_t)(last - 1);
                l->chunk_offset = (int32_t)ile32(e + 8);
                l->size = (int32_t)ile32(e + 12);
                l->chunk_size = (int32_t)ile32(e + 16);
            } else if (v == IV(4, 0, 0, 0)) {
                if (first < 0 || last < first) goto fail;
                l->first_slice = (uint32_t)first;
                l->last_slice = (uint32_t)last;
                l->chunk_offset = (int32_t)ile32(e + 8);
                l->size = (int64_t)ile64(e + 12);
                l->chunk_size = (int64_t)ile64(e + 20);
            } else {
                if (first < 0 || last < first) goto fail;
                l->first_slice = (uint32_t)first;
                l->last_slice = (uint32_t)last;
                l->chunk_offset = (int32_t)ile32(e + 8);
                l->sub_offset = (int64_t)ile64(e + 12);
                l->size = (int64_t)ile64(e + 20);
                l->chunk_size = (int64_t)ile64(e + 28);
            }
            flags = e[flags_at];
            l->filter = (v >= IV(4, 1, 8, 0) && (flags & 0x10U)) ? INNO_F_4108 : INNO_F_NONE;
            l->encrypted = v >= IV(4, 2, 2, 0) && (flags & 0x40U);
            if (v >= IV(2, 0, 17, 0) && v < IV(4, 0, 1, 0)) {
                l->compression = (flags & 0x04U) ? INNO_C_BZIP2 : INNO_C_ZLIB;
            } else {
                bool comp = v < IV(4, 2, 5, 0) || (flags & 0x80U);
                l->compression = comp ? hdr_comp : INNO_C_STORE;
            }
        } else {
            l->first_slice = ile32(e);
            l->last_slice = ile32(e + 4);
            l->chunk_offset = wide_start ? (int64_t)ile64(e + 8) : (int64_t)ile32(e + 8);
            l->sub_offset = (int64_t)ile64(e + 12 + shift);
            l->size = (int64_t)ile64(e + 20 + shift);
            l->chunk_size = (int64_t)ile64(e + 28 + shift);
            flags = e[flags_at];
            if (flags_size == 2U) flags |= (uint32_t)e[flags_at + 1U] << 8U;
            if (rev2 || v >= IV(6, 4, 3, 0)) {
                if (flags & 0x04U) l->filter = INNO_F_5200;
                l->encrypted = (flags & 0x08U) != 0;
                l->compression = (flags & 0x10U) ? hdr_comp : INNO_C_STORE;
            } else {
                if (flags & 0x10U) l->filter = INNO_F_5200;
                l->encrypted = (flags & 0x40U) != 0;
                l->compression = (flags & 0x80U) ? hdr_comp : INNO_C_STORE;
            }
            if (l->filter == INNO_F_5200) {
                if (v < IV(5, 2, 0, 0)) l->filter = INNO_F_4108;
                else if (v >= IV(5, 3, 9, 0)) l->filter = INNO_F_5309;
            }
            if (l->first_slice > l->last_slice) goto fail;
        }
        if (l->last_slice > 0x7FFFFFFFU || l->chunk_offset < 0 || l->sub_offset < 0 ||
            l->size < 0 || l->chunk_size < 0 || l->sub_offset > INT64_MAX - l->size ||
            l->compression == INNO_C_UNKNOWN || l->compression > INNO_C_LZMA2)
            goto fail;
        xx_rt_memcpy(l->checksum, e + digest_at, digest);
        l->hash_kind = hkind;
        l->filetime = ile64(e + time_at);
    }
    *out = locs;
    return true;
fail:
    xx_mem_free(locs);
    return false;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

static const uint16_t g_cp1252_c1[32] = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};

static size_t inno_put_utf8(char *o, uint32_t cp) {
    if (cp < 0x80U) {
        if (o) o[0] = (char)cp;
        return 1U;
    }
    if (cp < 0x800U) {
        if (o) {
            o[0] = (char)(0xC0U | (cp >> 6U));
            o[1] = (char)(0x80U | (cp & 0x3FU));
        }
        return 2U;
    }
    if (cp < 0x10000U) {
        if (o) {
            o[0] = (char)(0xE0U | (cp >> 12U));
            o[1] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
            o[2] = (char)(0x80U | (cp & 0x3FU));
        }
        return 3U;
    }
    if (o) {
        o[0] = (char)(0xF0U | (cp >> 18U));
        o[1] = (char)(0x80U | ((cp >> 12U) & 0x3FU));
        o[2] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
        o[3] = (char)(0x80U | (cp & 0x3FU));
    }
    return 4U;
}

/* Next code point of an ANSI (Windows-1252) or UTF-16LE name. */
static uint32_t inno_next_cp(const inno_raw *r, uint32_t *i) {
    if (!r->wide) {
        uint8_t b = r->name[(*i)++];
        return (b >= 0x80U && b <= 0x9FU) ? g_cp1252_c1[b - 0x80U] : b;
    } else {
        uint32_t u = ile16(r->name + *i);
        *i += 2U;
        if (u >= 0xD800U && u <= 0xDBFFU && *i + 2U <= r->len) {
            uint32_t lo = ile16(r->name + *i);
            if (lo >= 0xDC00U && lo <= 0xDFFFU) {
                *i += 2U;
                return 0x10000U + ((u - 0xD800U) << 10U) + (lo - 0xDC00U);
            }
        }
        if (u >= 0xD800U && u <= 0xDFFFU) return 0xFFFDU;
        return u;
    }
}

static char inno_upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

static bool inno_is_device(const char *comp, size_t length) {
    static const char *const names[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, index, k;
    while (stem < length && comp[stem] != '.') ++stem;
    while (stem > 0U && comp[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        const char *n = names[index];
        for (k = 0U; k < stem && n[k]; ++k)
            if (inno_upper(comp[k]) != n[k]) break;
        if (k == stem && n[k] == 0) return true;
    }
    if (stem == 4U && comp[3] >= '0' && comp[3] <= '9') {
        char a = inno_upper(comp[0]), b = inno_upper(comp[1]), c = inno_upper(comp[2]);
        if ((a == 'C' && b == 'O' && c == 'M') || (a == 'L' && b == 'P' && c == 'T')) return true;
    }
    return false;
}

/* UTF-8 output name with '/' separators.  Reserved punctuation becomes
 * '_', and "." or empty components are dropped ("{app}\.\bin\x.dll" is
 * common).  *safe is cleared for absolute or drive paths, an empty name,
 * components made of dots and spaces only (".."), control characters and
 * device names. */
static char *inno_make_name(const inno_raw *r, bool *safe) {
    uint32_t i = 0U;
    size_t length = 0U, o = 0U, start, rd = 0U, wr = 0U;
    char *name;
    *safe = true;
    if (r->wide && (r->len & 1U)) return NULL;
    while (i < r->len) length += inno_put_utf8(NULL, inno_next_cp(r, &i));
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    i = 0U;
    while (i < r->len) {
        uint32_t cp = inno_next_cp(r, &i);
        if (cp == '\\') cp = '/';
        if (o == 0U && cp == '/') *safe = false;
        if (o == 1U && cp == ':') *safe = false;
        if (cp < 0x20U || cp == 0x7FU) *safe = false;
        if (cp == ':' || cp == '*' || cp == '?' || cp == '"' || cp == '<' || cp == '>' ||
            cp == '|')
            cp = '_';
        o += inno_put_utf8(name + o, cp);
    }
    while (rd <= o) {
        size_t end = rd;
        while (end < o && name[end] != '/') ++end;
        if (end != rd && !(end - rd == 1U && name[rd] == '.')) {
            if (wr) name[wr++] = '/';
            xx_rt_memmove(name + wr, name + rd, end - rd);
            wr += end - rd;
        }
        rd = end + 1U;
    }
    o = wr;
    name[o] = 0;
    if (!o) *safe = false;
    start = 0U;
    for (i = 0U; i <= o; ++i) {
        if (i == o || name[i] == '/') {
            size_t k;
            bool meaningful = false;
            if ((size_t)i == start) *safe = false;
            for (k = start; k < i; ++k)
                if (name[k] != '.' && name[k] != ' ') meaningful = true;
            if (!meaningful || inno_is_device(name + start, i - start)) *safe = false;
            start = (size_t)i + 1U;
        }
    }
    return name;
}

static uint32_t inno_hash_name(const char *s) {
    uint32_t h = 2166136261U;
    while (*s) {
        h ^= (uint8_t)inno_upper(*s++);
        h *= 16777619U;
    }
    return h;
}

static bool inno_same_name(const char *a, const char *b) {
    while (*a && *b && inno_upper(*a) == inno_upper(*b)) ++a, ++b;
    return *a == 0 && *b == 0;
}

typedef struct inno_names_s {
    const char **slots;
    size_t mask;
} inno_names;

static bool inno_names_has(const inno_names *set, const char *name) {
    size_t at = inno_hash_name(name) & set->mask;
    while (set->slots[at]) {
        if (inno_same_name(set->slots[at], name)) return true;
        at = (at + 1U) & set->mask;
    }
    return false;
}

static void inno_names_put(inno_names *set, const char *name) {
    size_t at = inno_hash_name(name) & set->mask;
    while (set->slots[at]) at = (at + 1U) & set->mask;
    set->slots[at] = name;
}

/* name + "_N" before the extension of the last component */
static char *inno_suffixed(const char *name, uint32_t n) {
    char digits[12];
    size_t nd = 0U, length = xx_str_len(name), insert = length, i, o = 0U, last = 0U;
    char *out;
    while (n && nd < sizeof(digits)) {
        digits[nd++] = (char)('0' + n % 10U);
        n /= 10U;
    }
    for (i = 0U; i < length; ++i)
        if (name[i] == '/') last = i + 1U;
    for (i = length; i > last; --i)
        if (name[i - 1U] == '.') {
            insert = i - 1U;
            break;
        }
    if (insert == last) insert = length;
    out = (char *)xx_mem_alloc(length + nd + 2U);
    if (!out) return NULL;
    for (i = 0U; i <= length; ++i) {
        if (i == insert) {
            size_t d;
            out[o++] = '_';
            for (d = nd; d > 0U; --d) out[o++] = digits[d - 1U];
        }
        if (i == length) break;
        out[o++] = name[i];
    }
    out[o] = 0;
    return out;
}

static char *inno_number_name(uint32_t n) {
    char digits[12], *out;
    size_t nd = 0U, i;
    do {
        digits[nd++] = (char)('0' + n % 10U);
        n /= 10U;
    } while (n && nd < sizeof(digits));
    out = (char *)xx_mem_alloc(nd + 5U);
    if (!out) return NULL;
    for (i = 0U; i < nd; ++i) out[i] = digits[nd - 1U - i];
    xx_rt_memcpy(out + nd, ".bin", 5U);
    return out;
}

static void inno_ctx_free(void *opaque) {
    inno_ctx *ctx = (inno_ctx *)opaque;
    uint32_t i;
    if (!ctx) return;
    for (i = 0U; i < ctx->member_count; ++i)
        if (ctx->members[i].name) xx_mem_free(ctx->members[i].name);
    if (ctx->members) xx_mem_free(ctx->members);
    if (ctx->locs) xx_mem_free(ctx->locs);
    if (ctx->cache) xx_mem_free(ctx->cache);
    ilz_free(ctx->lz);
    xx_mem_free(ctx);
}

/* Build members from the raw [Files] list (or, when names is NULL, one
 * member per location called "<n>.bin"). */
static bool inno_build_members(inno_ctx *ctx, const inno_rawlist *names) {
    uint32_t count = names ? names->count : ctx->loc_count, i, seq = 2U;
    inno_names set;
    size_t slots = 16U;
    bool ok = true;
    if (!count) return true;
    ctx->members = (inno_member *)xx_mem_calloc(count, sizeof(*ctx->members));
    if (!ctx->members) return false;
    while (slots < (size_t)count * 2U) slots <<= 1U;
    set.slots = (const char **)xx_mem_calloc(slots, sizeof(*set.slots));
    set.mask = slots - 1U;
    if (!set.slots) return false;
    for (i = 0U; i < count && ok; ++i) {
        inno_member *m = &ctx->members[ctx->member_count];
        char *name;
        bool safe = true;
        if (names) {
            name = inno_make_name(&names->items[i], &safe);
            m->loc = names->items[i].loc;
        } else {
            name = inno_number_name(i + 1U);
            m->loc = i;
        }
        if (!name) {
            ok = false;
            break;
        }
        if (inno_names_has(&set, name)) {
            /* One running suffix for all duplicates keeps this linear even
             * when thousands of entries share a destination. */
            uint32_t tries;
            char *alt = NULL;
            for (tries = 0U; tries <= count + 1U; ++tries) {
                alt = inno_suffixed(name, seq++);
                if (!alt || !inno_names_has(&set, alt)) break;
                xx_mem_free(alt);
                alt = NULL;
            }
            xx_mem_free(name);
            if (!alt) {
                ok = false;
                break;
            }
            name = alt;
        }
        m->name = name;
        m->safe = safe;
        inno_names_put(&set, name);
        ++ctx->member_count;
    }
    xx_mem_free((void *)set.slots);
    return ok;
}

/* Solid chunks: the compiler writes the locations of one chunk as a run,
 * so a linear pass finds how many share it and how far they reach. */
static void inno_group_locs(inno_ctx *ctx) {
    uint32_t i = 0U, j, k;
    while (i < ctx->loc_count) {
        uint64_t extent = 0U;
        j = i;
        while (j < ctx->loc_count && ctx->locs[j].chunk_offset == ctx->locs[i].chunk_offset &&
               ctx->locs[j].first_slice == ctx->locs[i].first_slice) {
            uint64_t e = (uint64_t)(ctx->locs[j].sub_offset + ctx->locs[j].size);
            if (e > extent) extent = e;
            ++j;
        }
        for (k = i; k < j; ++k) {
            ctx->locs[k].users = j - i;
            ctx->locs[k].extent = extent;
        }
        i = j;
    }
}

/* ---------------------------------------------------------------------- */
/* Setup-0 parsing                                                         */

/* 1.09 / 1.11: one Adler-32 framed block per record. */
static bool inno_parse_legacy(Abstractformat *f, inno_ctx *ctx, int64_t size,
                              xx_pd_struct *pd) {
    const inno_table *t = &ctx->table;
    bool b109 = ctx->ver.short_id == 109U;
    int64_t end = b109 ? t->data_offset : t->table_offset, cur, consumed = 0, data_end,
            expected;
    ibuf blk;
    uint32_t counts[9], infos[3] = {0U, 0U, 0U}, ncount, i, k;
    uint64_t blocks = 1U;
    inno_rawlist names;
    bool ok = false;
    uint8_t idsk[12];
    xx_mem_zero(&names, sizeof(names));
    xx_mem_zero(counts, sizeof(counts));
    if (end > size || t->header_offset + 8 + 16 > end) return false;
    cur = t->header_offset + 8;
    if (!inno_read_legacy_block(f, cur, end, &blk, &consumed, pd)) return false;
    cur += consumed;
    if (b109) {
        if (blk.n != 434U) goto bad_header;
        ncount = 8U;
        for (i = 0U; i < ncount; ++i) counts[i] = ile32(blk.p + 0x180 + i * 4U);
    } else {
        icur c;
        c.p = blk.p;
        c.n = blk.n;
        c.o = 0U;
        if (!ic_strs(&c, 6) || c.n - c.o != 62U) goto bad_header;
        ncount = 9U;
        for (i = 0U; i < ncount; ++i) counts[i] = ile32(blk.p + c.o + i * 4U);
        for (i = 0U; i < 3U; ++i) infos[i] = ile32(blk.p + c.o + (9U + i) * 4U);
    }
    ibuf_free(&blk);
    for (i = 0U; i < ncount; ++i) {
        if (counts[i] > INNO_LEGACY_MAX_RECORDS) return false;
        blocks += counts[i];
    }
    blocks += counts[1];
    for (i = 0U; i < 3U; ++i) {
        if (infos[i] > INNO_LEGACY_BLOCK_CAP) return false;
        if (infos[i]) ++blocks;
    }
    if (blocks > INNO_LEGACY_MAX_RECORDS) return false;
    for (i = 0U; i < 3U; ++i) {
        if (!infos[i]) continue;
        if (!inno_read_legacy_block(f, cur, end, &blk, &consumed, pd)) return false;
        k = blk.n == infos[i];
        ibuf_free(&blk);
        if (!k) return false;
        cur += consumed;
    }
    for (i = 0U; i < counts[0]; ++i) {
        if (!inno_read_legacy_block(f, cur, end, &blk, &consumed, pd)) return false;
        ibuf_free(&blk);
        cur += consumed;
    }
    if (!inno_read_at(f->device, f->base_address + t->data_offset, idsk, 12U) ||
        xx_rt_memcmp(idsk, "idska32\x1a", 8U) != 0)
        return false;
    data_end = b109 ? size : t->exe_offset;
    if (data_end <= t->data_offset + 12 || data_end > size ||
        (b109 ? (uint64_t)ile32(idsk + 8) != (uint64_t)(data_end - t->data_offset)
              : ile32(idsk + 8) != 0U))
        return false;
    ctx->loc_count = counts[1];
    if (counts[1]) {
        ctx->locs = (inno_loc *)xx_mem_calloc(counts[1], sizeof(*ctx->locs));
        if (!ctx->locs) return false;
    }
    /* File records: names and sizes; the location blocks follow them. */
    {
        uint8_t **fnames = counts[1] ? (uint8_t **)xx_mem_calloc(counts[1], sizeof(uint8_t *)) : NULL;
        uint32_t *flens = counts[1] ? (uint32_t *)xx_mem_calloc(counts[1], sizeof(uint32_t)) : NULL;
        if (counts[1] && (!fnames || !flens)) {
            if (fnames) xx_mem_free(fnames);
            if (flens) xx_mem_free(flens);
            return false;
        }
        for (i = 0U; i < counts[1]; ++i) {
            inno_loc *l = &ctx->locs[i];
            const uint8_t *nm;
            uint32_t nl, csize, osize;
            if (!inno_read_legacy_block(f, cur, end, &blk, &consumed, pd)) goto files_fail;
            cur += consumed;
            if (b109) {
                if (blk.n != 95U || blk.p[0] > 63U) goto files_fail_blk;
                nm = blk.p + 1;
                nl = blk.p[0];
                l->filetime = ile64(blk.p + 0x40);
                osize = ile32(blk.p + 0x50);
                csize = ile32(blk.p + 0x54);
                xx_rt_memcpy(l->checksum, blk.p + 0x58, 4U);
            } else {
                if (blk.n < 51U) goto files_fail_blk;
                nl = ile32(blk.p);
                if ((uint64_t)nl + 51U != blk.n) goto files_fail_blk;
                nm = blk.p + 4;
                l->filetime = ile64(blk.p + 4 + nl + 0x0C);
                osize = ile32(blk.p + blk.n - 19U);
                csize = ile32(blk.p + blk.n - 15U);
                xx_rt_memcpy(l->checksum, blk.p + blk.n - 11U, 4U);
            }
            if (osize > 0x7FFFFFFFU || csize == 0U || csize > 0x7FFFFFFFU) goto files_fail_blk;
            l->size = osize;
            l->chunk_size = csize;
            l->hash_kind = INNO_H_ADLER32;
            l->compression = INNO_C_ZLIB;
            if (nl) {
                fnames[i] = (uint8_t *)xx_mem_alloc(nl);
                if (!fnames[i]) goto files_fail_blk;
                xx_rt_memcpy(fnames[i], nm, nl);
                flens[i] = nl;
            }
            ibuf_free(&blk);
            continue;
        files_fail_blk:
            ibuf_free(&blk);
            goto files_fail;
        }
        expected = 12;
        for (i = 0U; i < counts[1]; ++i) {
            inno_loc *l = &ctx->locs[i];
            uint8_t magic[4];
            bool good;
            if (!inno_read_legacy_block(f, cur, end, &blk, &consumed, pd)) goto files_fail;
            cur += consumed;
            good = blk.n == 12U && ile32(blk.p) == 1U && ile32(blk.p + 4) == 1U &&
                   (int64_t)ile32(blk.p + 8) == expected;
            ibuf_free(&blk);
            if (!good || expected > data_end - t->data_offset - 4 ||
                l->chunk_size > data_end - t->data_offset - expected - 4 ||
                !inno_read_at(f->device, f->base_address + t->data_offset + expected, magic, 4U) ||
                xx_rt_memcmp(magic, "zlb\x1a", 4U) != 0)
                goto files_fail;
            l->chunk_offset = expected;
            expected += 4 + l->chunk_size;
        }
        if (expected != data_end - t->data_offset) goto files_fail;
        for (k = 2U; k < ncount; ++k)
            for (i = 0U; i < counts[k]; ++i) {
                if (!inno_read_legacy_block(f, cur, end, &blk, &consumed, pd)) goto files_fail;
                ibuf_free(&blk);
                cur += consumed;
            }
        if (cur != end) goto files_fail;
        for (i = 0U; i < counts[1]; ++i)
            if (flens[i] && !inno_raw_add(&names, fnames[i], flens[i], false, i)) goto files_fail;
        ctx->data_base = f->base_address + t->data_offset;
        inno_group_locs(ctx);
        ok = inno_build_members(ctx, &names);
    files_fail:
        for (i = 0U; fnames && i < counts[1]; ++i)
            if (fnames[i]) xx_mem_free(fnames[i]);
        if (fnames) xx_mem_free(fnames);
        if (flens) xx_mem_free(flens);
    }
    if (names.items) xx_mem_free(names.items);
    return ok;
bad_header:
    ibuf_free(&blk);
    return false;
}

static bool inno_walk_files(icur *c, const inno_ver *ver, const inno_hdr *h, bool rev2,
                            bool exact, inno_rawlist *names) {
    uint32_t v = ver->v;
    if (v == IV(1, 2, 10, 0)) return inno_files_1210(c, ver, h, names);
    if (v < IV(5, 0, 0, 0)) return inno_files_pre5(c, ver, h, exact, names);
    if (ver->unicode) return inno_files_unicode(c, ver, h, rev2, names);
    if ((v >> 24) != 5U) return false;
    return inno_files_5ansi(c, ver, h, names);
}

static bool inno_parse_header(icur *c, const inno_ver *ver, inno_hdr *h) {
    if (ver->v == IV(1, 2, 10, 0)) return inno_hdr_1210(c, ver, h);
    if (ver->v < IV(5, 0, 0, 0)) return inno_hdr_pre5(c, ver, h);
    return inno_hdr_5(c, ver, h);
}

static bool inno_parse(Abstractformat *f, inno_ctx *ctx, xx_pd_struct *pd) {
    int64_t size = 0, cur, consumed = 0;
    ibuf b1, b2;
    bool rev2, ok = false;
    inno_ver cands[2];
    int ncand = 1, k, chosen = -1;
    inno_rawlist names, best_names;
    inno_loc *best_locs = NULL;
    uint32_t best_loc_count = 0U;
    xx_mem_zero(&b1, sizeof(b1));
    xx_mem_zero(&b2, sizeof(b2));
    xx_mem_zero(&names, sizeof(names));
    xx_mem_zero(&best_names, sizeof(best_names));
    ctx->data_base = -1;
    ctx->cache_chunk = -1;
    ctx->lz_chunk = -1;
    ctx->bad_chunk = -1;
    if (!inno_probe(f, &ctx->table, &ctx->ver, &size)) return false;
    ctx->format_size = (int64_t)ctx->table.total_size;
    if (ctx->ver.short_id) return inno_parse_legacy(f, ctx, size, pd);
    rev2 = ctx->table.kind == INNO_T_REV2;
    cur = ctx->table.header_offset + (int64_t)ctx->ver.id_size;
    if (rev2) {
        uint8_t e[53];
        if (cur > size - 53 || !inno_read_at(f->device, f->base_address + cur, e, 53U) ||
            ile32(e) != inno_crc32(e + 4, 49U) || e[4] > 2U || e[4] == 2U)
            return false;
        cur += 53;
    }
    if (!inno_read_block(f, cur, size, &ctx->ver, &b1, &consumed, pd)) return false;
    cur += consumed;
    if (!inno_read_block(f, cur, size, &ctx->ver, &b2, &consumed, pd)) goto done;
    cur += consumed;
    if (ctx->ver.v == IV(1, 2, 10, 0)) {
        if (cur != ctx->table.table_offset) goto done;
        if (ctx->table.data_offset > 0) {
            uint8_t idsk[12];
            if (ctx->table.data_offset > size - 12 ||
                !inno_read_at(f->device, f->base_address + ctx->table.data_offset, idsk, 12U) ||
                xx_rt_memcmp(idsk, ctx->ver.win16 ? "idska16\x1a" : "idska32\x1a", 8U) != 0 ||
                ile32(idsk + 8) < 12U || ctx->table.exe_offset <= ctx->table.data_offset ||
                (int64_t)ile32(idsk + 8) != ctx->table.exe_offset - ctx->table.data_offset)
                goto done;
        }
    }
    cands[0] = ctx->ver;
    cands[1] = ctx->ver;
    switch (ctx->ver.v) {
    case IV(1, 3, 21, 0): cands[1].v = IV(1, 3, 24, 0); ncand = 2; break;
    case IV(2, 0, 1, 0): cands[1].v = IV(2, 0, 2, 0); ncand = 2; break;
    case IV(3, 0, 3, 0): cands[1].v = IV(3, 0, 4, 0); ncand = 2; break;
    case IV(4, 2, 3, 0): cands[1].v = IV(4, 2, 4, 0); ncand = 2; break;
    default: break;
    }
    for (k = 0; k < ncand; ++k) {
        inno_hdr h;
        icur c;
        inno_loc *locs = NULL;
        c.p = b1.p;
        c.n = b1.n;
        c.o = 0U;
        names.count = 0U;
        if (inno_stopped(pd)) goto done;
        if (!inno_parse_header(&c, &cands[k], &h)) continue;
        if (!h.loc_count) {
            if (!inno_walk_files(&c, &cands[k], &h, rev2, !b1.inexact, &names) || names.count)
                continue;
        } else {
            if (!inno_parse_locs(&b2, &cands[k], rev2, h.loc_count, h.compression, &locs))
                continue;
            if (!inno_walk_files(&c, &cands[k], &h, rev2, !b1.inexact, &names)) {
                /* Unknown entry layout: keep the locations for "<n>.bin". */
                if (chosen < 0 && !best_locs) {
                    best_locs = locs;
                    best_loc_count = h.loc_count;
                    best_names.count = 0U;
                    chosen = -2 - k;
                } else {
                    xx_mem_free(locs);
                }
                continue;
            }
        }
        if (chosen >= 0) { /* two layouts fit: refuse to guess */
            if (locs) xx_mem_free(locs);
            goto done;
        }
        if (best_locs) xx_mem_free(best_locs);
        best_locs = locs;
        best_loc_count = h.loc_count;
        {
            inno_rawlist swap = best_names;
            best_names = names;
            names = swap;
        }
        chosen = k;
    }
    if (chosen == -1) goto done;
    if (chosen >= 0) ctx->ver.v = cands[chosen].v;
    ctx->locs = best_locs;
    ctx->loc_count = best_loc_count;
    best_locs = NULL;
    ctx->data_base = ctx->table.data_offset > 0 ? f->base_address + ctx->table.data_offset : -1;
    inno_group_locs(ctx);
    ok = inno_build_members(ctx, chosen >= 0 ? &best_names : NULL);
done:
    ibuf_free(&b1);
    ibuf_free(&b2);
    if (names.items) xx_mem_free(names.items);
    if (best_names.items) xx_mem_free(best_names.items);
    if (best_locs) xx_mem_free(best_locs);
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Resumable LZMA / LZMA2 decoder for solid chunks                         */
/*
 * A solid chunk holds many files back to back (one chunk of several hundred
 * MB is common in current installers).  The library decoders run a stream
 * from its start until they are stopped, so member k would cost a decode of
 * everything before it.  This decoder keeps its state between members.
 * Its range coder, probability model and window handling follow xxfclib's
 * own src/algo/lzma/xx_lzma_dec.c (MIT; the model is Igor Pavlov's public
 * domain LZMA SDK), with the loop reshaped so that it can stop after any
 * output byte and resume later.  LZMA2 control bytes follow the SDK's rules
 * (a dictionary reset first, new properties after an uncompressed chunk
 * that reset the dictionary).
 */

#define ILZ_LIT_MAX (0x300U << 4) /* lc + lp <= 4 */
#define ILZ_DICT_MAX ((uint64_t)512U << 20)
#define ILZ_DICT_START ((uint64_t)1U << 20)
#define ILZ_INBUF 65536U

typedef struct ilz_model_s {
    uint16_t is_match[12][16];
    uint16_t is_rep[12];
    uint16_t is_rep_g0[12];
    uint16_t is_rep_g1[12];
    uint16_t is_rep_g2[12];
    uint16_t is_rep0_long[12][16];
    uint16_t pos_slot[4][64];
    uint16_t pos_dec[128]; /* indexed from 1 by (dist - slot) + m */
    uint16_t pos_align[16];
    uint16_t len_choice[2];
    uint16_t len_choice2[2];
    uint16_t len_low[2][16][8];
    uint16_t len_mid[2][16][8];
    uint16_t len_high[2][256];
} ilz_model;

typedef struct ilz_s {
    xx_io_device *dev;
    int64_t in_pos;     /* next input byte (device offset) */
    int64_t in_limit;   /* end of the current packed run */
    int64_t in_end;     /* end of the chunk */
    int64_t next_chunk; /* LZMA2: offset of the next control byte */
    int64_t buf_base;
    size_t buf_len;
    uint32_t range;
    uint32_t code;
    bool error;
    bool finished;
    bool lzma2;
    bool copy;          /* LZMA2 uncompressed chunk */
    uint8_t need;       /* LZMA2: lowest control byte the next LZMA chunk may use */
    uint32_t chunk_left;/* LZMA2: unpacked bytes left in the chunk */
    int lc, lp, pb;
    uint8_t *dict;
    uint64_t dict_mask; /* current allocation - 1 */
    uint64_t dict_alloc;/* full window allocation (a power of two) */
    uint64_t dict_limit;/* declared window */
    uint64_t ppos;      /* position since the last dictionary reset */
    uint64_t filled;    /* valid window bytes */
    uint64_t total;     /* bytes produced */
    int state;
    uint32_t rep[4];
    uint32_t pending;   /* match bytes still to copy */
    ilz_model m;
    uint16_t lit[ILZ_LIT_MAX];
    uint8_t buf[ILZ_INBUF];
} ilz;

static void ilz_free(ilz *z) {
    if (!z) return;
    if (z->dict) xx_mem_free(z->dict);
    xx_mem_free(z);
}

static uint8_t ilz_byte(ilz *z) {
    int64_t at;
    if (z->in_pos >= z->in_limit) {
        z->error = true;
        return 0U;
    }
    at = z->in_pos - z->buf_base;
    if (at < 0 || at >= (int64_t)z->buf_len) {
        int64_t left = z->in_end - z->in_pos;
        size_t want = left < (int64_t)ILZ_INBUF ? (size_t)left : ILZ_INBUF;
        if (!want || !inno_read_at(z->dev, z->in_pos, z->buf, want)) {
            z->error = true;
            return 0U;
        }
        z->buf_base = z->in_pos;
        z->buf_len = want;
        at = 0;
    }
    ++z->in_pos;
    return z->buf[at];
}

static int ilz_bit(ilz *z, uint16_t *p) {
    uint32_t bound = (z->range >> 11U) * (uint32_t)*p;
    int bit;
    if (z->code < bound) {
        *p = (uint16_t)(*p + ((2048U - *p) >> 5U));
        z->range = bound;
        bit = 0;
    } else {
        *p = (uint16_t)(*p - (*p >> 5U));
        z->code -= bound;
        z->range -= bound;
        bit = 1;
    }
    if (z->range < (1U << 24U)) {
        z->range <<= 8U;
        z->code = (z->code << 8U) | ilz_byte(z);
    }
    return bit;
}

static uint32_t ilz_tree(ilz *z, uint16_t *p, int bits) {
    uint32_t m = 1U;
    int i;
    for (i = 0; i < bits; ++i) m = (m << 1U) | (uint32_t)ilz_bit(z, p + m);
    return m - (1U << bits);
}

static uint32_t ilz_tree_rev(ilz *z, uint16_t *p, int bits) {
    uint32_t m = 1U, sym = 0U;
    int i;
    for (i = 0; i < bits; ++i) {
        uint32_t bit = (uint32_t)ilz_bit(z, p + m);
        m = (m << 1U) | bit;
        sym |= bit << i;
    }
    return sym;
}

static uint32_t ilz_direct(ilz *z, int bits) {
    uint32_t result = 0U;
    int i;
    for (i = bits - 1; i >= 0; --i) {
        uint32_t t;
        z->range >>= 1U;
        z->code -= z->range;
        t = 0U - (z->code >> 31U);
        z->code += z->range & t;
        result |= ((t + 1U) & 1U) << i;
        if (z->range < (1U << 24U)) {
            z->range <<= 8U;
            z->code = (z->code << 8U) | ilz_byte(z);
        }
    }
    return result;
}

static uint32_t ilz_len(ilz *z, int which, uint32_t ps) {
    if (!ilz_bit(z, &z->m.len_choice[which]))
        return 2U + ilz_tree(z, z->m.len_low[which][ps], 3);
    if (!ilz_bit(z, &z->m.len_choice2[which]))
        return 10U + ilz_tree(z, z->m.len_mid[which][ps], 3);
    return 18U + ilz_tree(z, z->m.len_high[which], 8);
}

static void ilz_reset_state(ilz *z) {
    uint16_t *p = (uint16_t *)&z->m;
    size_t i, n = sizeof(z->m) / sizeof(uint16_t), lits = (size_t)0x300U << (z->lc + z->lp);
    for (i = 0U; i < n; ++i) p[i] = 1024U;
    for (i = 0U; i < lits; ++i) z->lit[i] = 1024U;
    z->state = 0;
    z->rep[0] = z->rep[1] = z->rep[2] = z->rep[3] = 1U;
    z->pending = 0U;
}

static bool ilz_set_props(ilz *z, uint32_t d) {
    if (d >= 9U * 5U * 5U) return false;
    z->lc = (int)(d % 9U);
    d /= 9U;
    z->lp = (int)(d % 5U);
    z->pb = (int)(d / 5U);
    return z->lc + z->lp <= 4;
}

static void ilz_rc_init(ilz *z) {
    int i;
    z->range = 0xFFFFFFFFU;
    z->code = 0U;
    if (ilz_byte(z) != 0U) z->error = true;
    for (i = 0; i < 4; ++i) z->code = (z->code << 8U) | ilz_byte(z);
}

/* The window starts small and doubles while it has not wrapped yet, so a
 * large declared window costs memory only once that much was produced. */
static void ilz_grow(ilz *z) {
    uint64_t cap = (z->dict_mask + 1U) * 2U;
    uint8_t *grown;
    if (cap > z->dict_alloc) cap = z->dict_alloc;
    grown = (uint8_t *)xx_mem_realloc(z->dict, (size_t)cap);
    if (!grown) {
        z->error = true;
        return;
    }
    z->dict = grown;
    z->dict_mask = cap - 1U;
}

static void ilz_put(ilz *z, uint8_t b) {
    if (z->ppos > z->dict_mask && z->dict_mask + 1U < z->dict_alloc) ilz_grow(z);
    z->dict[(size_t)(z->ppos & z->dict_mask)] = b;
    ++z->ppos;
    if (z->filled <= z->dict_mask) ++z->filled;
    ++z->total;
}

/* LZMA2: read the next control byte and set up its chunk. */
static void ilz2_chunk(ilz *z) {
    uint8_t c;
    z->in_pos = z->next_chunk;
    z->in_limit = z->in_end;
    c = ilz_byte(z);
    if (z->error) return;
    if (c == 0U) {
        z->finished = true;
        return;
    }
    if (c == 1U || c == 2U) {
        uint32_t size = (uint32_t)ilz_byte(z) << 8U;
        size = (size | ilz_byte(z)) + 1U;
        if (z->error) return;
        if (c == 1U) {
            z->ppos = 0U;
            z->filled = 0U;
            z->need = 0xC0U;
        } else if (z->need == 0xE0U) {
            z->error = true;
            return;
        }
        z->copy = true;
        z->chunk_left = size;
    } else {
        uint8_t h[4];
        uint32_t packed;
        int i;
        if (c < 0x80U || c < z->need) {
            z->error = true;
            return;
        }
        for (i = 0; i < 4; ++i) h[i] = ilz_byte(z);
        if (z->error) return;
        z->chunk_left = ((((uint32_t)c & 0x1FU) << 16U) | ((uint32_t)h[0] << 8U) | h[1]) + 1U;
        packed = (((uint32_t)h[2] << 8U) | h[3]) + 1U;
        if (c >= 0xE0U) {
            z->ppos = 0U;
            z->filled = 0U;
        }
        if (c >= 0xC0U && !ilz_set_props(z, ilz_byte(z))) {
            z->error = true;
            return;
        }
        if (c >= 0xA0U) ilz_reset_state(z);
        if (z->error || packed < 5U || (int64_t)packed > z->in_end - z->in_pos) {
            z->error = true;
            return;
        }
        z->need = 0U;
        z->copy = false;
        z->next_chunk = z->in_pos + (int64_t)packed;
        z->in_limit = z->next_chunk;
        ilz_rc_init(z);
        return;
    }
    if ((int64_t)z->chunk_left > z->in_end - z->in_pos) {
        z->error = true;
        return;
    }
    z->next_chunk = z->in_pos + (int64_t)z->chunk_left;
    z->in_limit = z->next_chunk;
}

/* Up to n more bytes of the chunk; fewer only at its end or on damage. */
static size_t ilz_read(ilz *z, uint8_t *out, size_t n) {
    size_t done = 0U;
    while (done < n && !z->error && !z->finished) {
        uint32_t ps, len;
        int st;
        if (z->pending) {
            uint32_t k = z->pending;
            if ((size_t)k > n - done) k = (uint32_t)(n - done);
            z->pending -= k;
            while (k--) {
                uint8_t b = z->dict[(size_t)((z->ppos - z->rep[0]) & z->dict_mask)];
                ilz_put(z, b);
                out[done++] = b;
            }
            continue;
        }
        if (z->lzma2) {
            if (!z->chunk_left) {
                ilz2_chunk(z);
                continue;
            }
            if (z->copy) {
                uint8_t b = ilz_byte(z);
                if (z->error) break;
                ilz_put(z, b);
                out[done++] = b;
                --z->chunk_left;
                continue;
            }
        }
        ps = (uint32_t)z->ppos & ((1U << z->pb) - 1U);
        st = z->state;
        if (!ilz_bit(z, &z->m.is_match[st][ps])) {
            uint8_t prev = z->filled ? z->dict[(size_t)((z->ppos - 1U) & z->dict_mask)] : 0U;
            uint32_t ctx = (((uint32_t)z->ppos & ((1U << z->lp) - 1U)) << z->lc) |
                           ((uint32_t)prev >> (8 - z->lc));
            uint16_t *lit = z->lit + (size_t)ctx * 0x300U;
            uint32_t sym;
            if (st < 7) {
                sym = ilz_tree(z, lit, 8);
            } else {
                uint32_t match;
                if (z->rep[0] > z->filled) {
                    z->error = true;
                    break;
                }
                match = z->dict[(size_t)((z->ppos - z->rep[0]) & z->dict_mask)];
                sym = 1U;
                do {
                    uint32_t mbit = (match >> 7U) & 1U, bit;
                    match <<= 1U;
                    bit = (uint32_t)ilz_bit(z, lit + ((1U + mbit) << 8U) + sym);
                    sym = (sym << 1U) | bit;
                    if (mbit != bit) {
                        while (sym < 0x100U) sym = (sym << 1U) | (uint32_t)ilz_bit(z, lit + sym);
                        break;
                    }
                } while (sym < 0x100U);
            }
            if (z->error) break;
            ilz_put(z, (uint8_t)sym);
            out[done++] = (uint8_t)sym;
            z->state = st < 4 ? 0 : (st < 10 ? st - 3 : st - 6);
            if (z->lzma2) --z->chunk_left;
            continue;
        }
        if (!ilz_bit(z, &z->m.is_rep[st])) {
            uint32_t dist, slot, lstate;
            len = ilz_len(z, 0, ps);
            lstate = len - 2U < 4U ? len - 2U : 3U;
            slot = ilz_tree(z, z->m.pos_slot[lstate], 6);
            if (slot < 4U) {
                dist = slot;
            } else {
                int direct = (int)(slot >> 1U) - 1;
                dist = (2U | (slot & 1U)) << direct;
                if (slot < 14U) {
                    dist += ilz_tree_rev(z, z->m.pos_dec + (dist - slot), direct);
                } else {
                    dist += ilz_direct(z, direct - 4) << 4U;
                    dist += ilz_tree_rev(z, z->m.pos_align, 4);
                    if (dist == 0xFFFFFFFFU) { /* end marker */
                        if (z->lzma2) z->error = true;
                        else z->finished = true;
                        break;
                    }
                }
            }
            z->rep[3] = z->rep[2];
            z->rep[2] = z->rep[1];
            z->rep[1] = z->rep[0];
            z->rep[0] = dist + 1U;
            z->state = st < 7 ? 7 : 10;
        } else if (!ilz_bit(z, &z->m.is_rep_g0[st])) {
            if (!ilz_bit(z, &z->m.is_rep0_long[st][ps])) {
                uint8_t b;
                if (z->error || z->rep[0] > z->filled) {
                    z->error = true;
                    break;
                }
                b = z->dict[(size_t)((z->ppos - z->rep[0]) & z->dict_mask)];
                ilz_put(z, b);
                out[done++] = b;
                z->state = st < 7 ? 9 : 11;
                if (z->lzma2) --z->chunk_left;
                continue;
            }
            len = ilz_len(z, 1, ps);
            z->state = st < 7 ? 8 : 11;
        } else {
            uint32_t d;
            if (!ilz_bit(z, &z->m.is_rep_g1[st])) {
                d = z->rep[1];
            } else if (!ilz_bit(z, &z->m.is_rep_g2[st])) {
                d = z->rep[2];
                z->rep[2] = z->rep[1];
            } else {
                d = z->rep[3];
                z->rep[3] = z->rep[2];
                z->rep[2] = z->rep[1];
            }
            z->rep[1] = z->rep[0];
            z->rep[0] = d;
            len = ilz_len(z, 1, ps);
            z->state = st < 7 ? 8 : 11;
        }
        if (z->error || z->rep[0] == 0U || z->rep[0] > z->filled ||
            (uint64_t)z->rep[0] > z->dict_limit) {
            z->error = true;
            break;
        }
        if (z->lzma2) {
            if (len > z->chunk_left) {
                z->error = true;
                break;
            }
            z->chunk_left -= len;
        }
        z->pending = len;
    }
    return done;
}

/* Start decoding the chunk of `l`: "zlb\x1a", then 5 LZMA properties or
 * one LZMA2 dictionary byte.  The window is the declared one, but never
 * more than the part of the chunk the members need. */
static ilz *ilz_open(xx_io_device *d, int64_t data_base, const inno_loc *l) {
    int64_t total = xx_io_total_size(d), at;
    uint8_t magic[4];
    uint64_t window, alloc = 4096U;
    ilz *z;
    if (data_base < 0 || l->chunk_offset > INT64_MAX - data_base) return NULL;
    at = data_base + l->chunk_offset;
    if (total < 0 || at > total - 4 || l->chunk_size > total - at - 4 ||
        !inno_read_at(d, at, magic, 4U) || xx_rt_memcmp(magic, "zlb\x1a", 4U) != 0)
        return NULL;
    z = (ilz *)xx_mem_calloc(1U, sizeof(*z));
    if (!z) return NULL;
    z->dev = d;
    z->in_pos = at + 4;
    z->in_end = at + 4 + l->chunk_size;
    z->in_limit = z->in_end;
    z->buf_base = -1;
    z->lzma2 = l->compression == INNO_C_LZMA2;
    if (z->lzma2) {
        uint8_t p = ilz_byte(z);
        if (z->error || p > 40U) goto fail;
        z->dict_limit = p == 40U ? 0xFFFFFFFFU : ((uint64_t)(2U | (p & 1U)) << (p / 2U + 11U));
        z->need = 0xE0U;
        z->next_chunk = z->in_pos;
        z->lc = 3;
        z->lp = 0;
        z->pb = 2;
    } else {
        uint8_t props[5];
        int i;
        for (i = 0; i < 5; ++i) props[i] = ilz_byte(z);
        if (z->error || !ilz_set_props(z, props[0])) goto fail;
        z->dict_limit = ile32(props + 1);
    }
    if (z->dict_limit < 4096U) z->dict_limit = 4096U;
    window = z->dict_limit < l->extent ? z->dict_limit : l->extent;
    if (window > ILZ_DICT_MAX) goto fail;
    while (alloc < window) alloc <<= 1U;
    z->dict_alloc = alloc;
    if (alloc > ILZ_DICT_START) alloc = ILZ_DICT_START;
    z->dict = (uint8_t *)xx_mem_alloc((size_t)alloc);
    if (!z->dict) goto fail;
    z->dict_mask = alloc - 1U;
    if (!z->lzma2) {
        ilz_reset_state(z);
        ilz_rc_init(z);
        if (z->error) goto fail;
    }
    return z;
fail:
    ilz_free(z);
    return NULL;
}

/* One member of a solid LZMA / LZMA2 chunk.  The chunk's decoder carries
 * on from the previous member when this one starts at or after it. */
static bool inno_extract_solid_lzma(Abstractformat *f, inno_ctx *ctx, const inno_loc *l,
                                    inno_out *out, xx_pd_struct *pd) {
    uint8_t *piece;
    uint64_t skip, left;
    bool ok = true;
    if (ctx->bad_chunk == l->chunk_offset &&
        (uint64_t)l->sub_offset + (uint64_t)l->size > ctx->bad_at)
        return false;
    if (!ctx->lz || ctx->lz_chunk != l->chunk_offset || ctx->lz->error ||
        ctx->lz->total > (uint64_t)l->sub_offset) {
        ilz_free(ctx->lz);
        ctx->lz = ilz_open(f->device, ctx->data_base, l);
        ctx->lz_chunk = l->chunk_offset;
        if (!ctx->lz) {
            ctx->bad_chunk = l->chunk_offset;
            ctx->bad_at = 0U;
            return false;
        }
    }
    piece = (uint8_t *)xx_mem_alloc(INNO_PIECE);
    if (!piece) return false;
    skip = (uint64_t)l->sub_offset - ctx->lz->total;
    while (skip && ok) {
        size_t n = skip < INNO_PIECE ? (size_t)skip : INNO_PIECE;
        ok = !inno_stopped(pd) && ilz_read(ctx->lz, piece, n) == n;
        skip -= n;
    }
    left = (uint64_t)l->size;
    while (left && ok) {
        size_t n = left < INNO_PIECE ? (size_t)left : INNO_PIECE;
        ok = !inno_stopped(pd) && ilz_read(ctx->lz, piece, n) == n;
        if (ok) inno_out_feed(out, piece, n);
        ok = ok && !out->failed;
        left -= n;
    }
    xx_mem_free(piece);
    if (!ok && (ctx->lz->error || ctx->lz->finished)) {
        ctx->bad_chunk = l->chunk_offset;
        ctx->bad_at = ctx->lz->total;
    }
    /* Drop the window once the run is done, or after damage. */
    if (!ok || ctx->lz->error || ctx->lz->total >= l->extent) {
        ilz_free(ctx->lz);
        ctx->lz = NULL;
    }
    return ok;
}

/* ---------------------------------------------------------------------- */
/* File data                                                               */

/* Decode chunk bytes [skip, skip + want) into sink.  The caller checks
 * sink->kept. */
static bool inno_decode_chunk(Abstractformat *f, int64_t data_base, const inno_loc *l,
                              inno_sink *sink, xx_pd_struct *pd) {
    xx_io_device *d = f->device;
    int64_t total = xx_io_total_size(d), at, payload = l->chunk_size;
    uint8_t magic[4];
    xx_io_device dev;
    uint64_t need = sink->skip + sink->want;
    if (data_base < 0 || l->chunk_offset > INT64_MAX - data_base) return false;
    at = data_base + l->chunk_offset;
    if (at > total - 4 || payload > total - at - 4 ||
        !inno_read_at(d, at, magic, 4U) || xx_rt_memcmp(magic, "zlb\x1a", 4U) != 0)
        return false;
    at += 4;
    inno_sink_device(&dev, sink);
    switch (l->compression) {
    case INNO_C_STORE: {
        uint8_t *buf;
        uint64_t pos = sink->skip, left = sink->want;
        bool ok = true;
        if (need > (uint64_t)payload) return false;
        buf = (uint8_t *)xx_mem_alloc(INNO_PIECE);
        if (!buf) return false;
        sink->seen = sink->skip;
        while (left && ok) {
            size_t piece = left < INNO_PIECE ? (size_t)left : INNO_PIECE;
            ok = !inno_stopped(pd) && inno_read_at(d, at + (int64_t)pos, buf, piece) &&
                 inno_sink_write(&dev, buf, piece) == (ssize_t)piece;
            pos += piece;
            left -= piece;
        }
        xx_mem_free(buf);
        return ok;
    }
    case INNO_C_ZLIB: {
        uint8_t zh[2];
        if (payload < 3 || !inno_read_at(d, at, zh, 2U) ||
            !xx_zlib_stream_header_is_valid(zh, 2U))
            return false;
        (void)xx_deflate_unpack_device(d, at + 2, payload - 2, &dev, false, pd);
        return true;
    }
    case INNO_C_BZIP2:
        (void)xx_bzip2_unpack_device(d, at, payload, &dev, pd);
        return true;
    case INNO_C_LZMA1: {
        uint8_t props[5];
        uint32_t dict, cap = 4096U;
        if (payload < 5 + 5 || !inno_read_at(d, at, props, 5U) || props[0] >= 9U * 5U * 5U)
            return false;
        dict = ile32(props + 1);
        while ((uint64_t)cap < need && cap < 0x80000000U) cap <<= 1U;
        if (dict > cap) {
            props[1] = (uint8_t)cap;
            props[2] = (uint8_t)(cap >> 8U);
            props[3] = (uint8_t)(cap >> 16U);
            props[4] = (uint8_t)(cap >> 24U);
        }
        (void)xx_lzma_unpack_device(d, at + 5, payload - 5, props, 5U, (int64_t)need, &dev, pd);
        return true;
    }
    case INNO_C_LZMA2: {
        uint8_t p;
        if (payload < 2 || !inno_read_at(d, at, &p, 1U) || p > 40U) return false;
        while (p > 0U) {
            uint8_t q = (uint8_t)(p - 1U);
            uint64_t dsz = (uint64_t)(2U | (q & 1U)) << (q / 2U + 11U);
            if (dsz < need) break;
            p = q;
        }
        (void)xx_lzma2_unpack_device(d, at + 1, payload - 1, p, &dev, pd);
        return true;
    }
    default: return false;
    }
}

/* Produce one location's bytes into dest (NULL: verify only). */
static bool inno_extract_loc(Abstractformat *f, inno_ctx *ctx, uint32_t index,
                             xx_io_device *dest, xx_pd_struct *pd) {
    const inno_loc *l;
    inno_out *out;
    bool ok = false;
    uint64_t extent;
    if (index >= ctx->loc_count || ctx->data_base < 0) return false;
    l = &ctx->locs[index];
    if (l->encrypted || l->first_slice != 0U || l->last_slice != 0U ||
        l->compression > INNO_C_LZMA2)
        return false;
    out = (inno_out *)xx_mem_alloc(sizeof(*out));
    if (!out) return false;
    if (!inno_out_init(out, l, dest)) goto done;
    if (l->size == 0) {
        ok = inno_out_finish(out, l);
        goto done;
    }
    extent = l->extent;
    if (l->users > 1U &&
        (l->compression == INNO_C_LZMA1 || l->compression == INNO_C_LZMA2)) {
        ok = inno_extract_solid_lzma(f, ctx, l, out, pd) && inno_out_finish(out, l);
    } else if (l->users > 1U && extent <= INNO_CACHE_MAX && l->compression != INNO_C_STORE) {
        /* The cache holds the decodable prefix of the chunk; a damaged
         * chunk is decoded once, and only its intact members succeed. */
        if (ctx->cache_chunk != l->chunk_offset ||
            (ctx->cache_size < extent && ctx->bad_chunk != l->chunk_offset)) {
            inno_sink sink;
            bool started;
            if (ctx->cache) xx_mem_free(ctx->cache);
            ctx->cache = NULL;
            ctx->cache_chunk = -1;
            ctx->cache_size = 0U;
            xx_mem_zero(&sink, sizeof(sink));
            sink.want = extent;
            sink.mem_limit = (size_t)extent;
            started = inno_decode_chunk(f, ctx->data_base, l, &sink, pd);
            if (sink.failed || inno_stopped(pd)) {
                if (sink.mem) xx_mem_free(sink.mem);
                goto done;
            }
            ctx->cache = sink.mem;
            ctx->cache_size = sink.kept;
            ctx->cache_chunk = l->chunk_offset;
            if (!started || sink.kept != extent) {
                ctx->bad_chunk = l->chunk_offset;
                ctx->bad_at = sink.kept;
            }
        }
        if ((uint64_t)l->sub_offset + (uint64_t)l->size > ctx->cache_size) goto done;
        inno_out_feed(out, ctx->cache + l->sub_offset, (size_t)l->size);
        ok = inno_out_finish(out, l);
    } else {
        inno_sink sink;
        xx_mem_zero(&sink, sizeof(sink));
        sink.skip = (uint64_t)l->sub_offset;
        sink.want = (uint64_t)l->size;
        sink.out = out;
        ok = inno_decode_chunk(f, ctx->data_base, l, &sink, pd) && !sink.failed &&
             sink.kept == sink.want && inno_out_finish(out, l);
    }
done:
    xx_mem_free(out);
    return ok && !inno_stopped(pd);
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static bool inno_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *inno_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool inno_set_record(xx_archive_record *record, const inno_ctx *ctx,
                            const inno_member *m) {
    const inno_loc *l = &ctx->locs[m->loc];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = -1;
    record->header_size = 0;
    record->data_offset = ctx->data_base >= 0 ? ctx->data_base + l->chunk_offset + 4 : -1;
    record->compressed_size = l->chunk_size;
    return xx_archive_record_set_original_name(record, m->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)l->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)l->chunk_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          l->compression) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, l->filetime) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, l->encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_inno_setup_init(xx_inno_setup *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_INNO_SETUP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-innosetup");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_inno_setup_check_is_valid;
    archive->format.handle_base_info = xx_inno_setup_handle_base_info;
    archive->format.get_format_size = xx_inno_setup_get_format_size;
    archive->format.get_number_of_archive_records = xx_inno_setup_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_inno_setup_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_inno_setup_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_inno_setup_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_inno_setup_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_inno_setup_free_archive_records_reading;
}

xx_inno_setup *xx_inno_setup_create(xx_io_device *device, int64_t base_address) {
    xx_inno_setup *archive = (xx_inno_setup *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_inno_setup_init(archive, device, base_address);
    return archive;
}

void xx_inno_setup_destroy(xx_inno_setup *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_inno_setup_free(xx_inno_setup *archive) {
    if (!archive) return;
    xx_inno_setup_destroy(archive);
    xx_mem_free(archive);
}

/* Cheap: the MZ header, the loader table and the 64-byte version ID. */
bool xx_inno_setup_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    inno_table table;
    inno_ver ver;
    (void)pd;
    return inno_probe(format, &table, &ver, NULL);
}

bool xx_inno_setup_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    inno_ctx *ctx;
    xx_inno_setup *archive;
    if (!format) return false;
    ctx = (inno_ctx *)xx_mem_calloc(1U, sizeof(*ctx));
    if (!ctx) return false;
    if (!inno_parse(format, ctx, pd)) {
        inno_ctx_free(ctx);
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        return false;
    }
    archive = (xx_inno_setup *)format;
    archive->number_of_records = ctx->member_count;
    archive->version = ctx->ver.v;
    archive->unicode = ctx->ver.unicode;
    archive->isx = ctx->ver.isx;
    archive->external_data = ctx->data_base < 0;
    xx_rt_memcpy(archive->version_text, ctx->ver.text, sizeof(archive->version_text));
    format->number_of_archive_records = ctx->member_count;
    format->format_size = ctx->format_size;
    format->file_type = XX_INNO_SETUP_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    inno_ctx_free(ctx);
    return true;
}

int64_t xx_inno_setup_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled || xx_inno_setup_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_inno_setup_get_number_of_archive_records(Abstractformat *format,
                                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled || xx_inno_setup_handle_base_info(format, pd))
               ? ((xx_inno_setup *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_inno_setup_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    inno_ctx *ctx;
    xx_archive_record_state *state;
    if (!format) return NULL;
    ctx = (inno_ctx *)xx_mem_calloc(1U, sizeof(*ctx));
    if (!ctx) return NULL;
    if (!inno_parse(format, ctx, pd) || ctx->member_count == 0U) {
        inno_ctx_free(ctx);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        inno_ctx_free(ctx);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = ctx;
    state->free_internal = inno_ctx_free;
    state->total_records = (int64_t)ctx->member_count;
    if (!inno_copy_options(&state->options, options) ||
        !inno_set_record(&state->current_record, ctx, &ctx->members[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_inno_setup_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_inno_setup_archive_record_move_to_next(Abstractformat *format,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    inno_ctx *ctx;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(ctx = (inno_ctx *)state->internal_state) || ++ctx->index >= ctx->member_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = inno_set_record(&state->current_record, ctx, &ctx->members[ctx->index]);
    return state->has_record;
}

bool xx_inno_setup_unpack_current_archive_record(Abstractformat *format,
                                                 xx_archive_record_state *state,
                                                 xx_pd_struct *pd) {
    inno_ctx *ctx;
    inno_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL, *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(ctx = (inno_ctx *)state->internal_state) || ctx->index >= ctx->member_count ||
        inno_stopped(pd))
        return false;
    member = &ctx->members[ctx->index];
    path_option = inno_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return inno_extract_loc(format, ctx, member->loc, NULL, pd);
    if (!member->safe || !member->name) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = inno_extract_loc(format, ctx, member->loc, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_inno_setup_free_archive_records_reading(Abstractformat *format,
                                                xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
