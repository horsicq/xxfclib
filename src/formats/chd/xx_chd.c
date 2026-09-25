/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MAME CHD (Compressed Hunks of Data), versions 1 to 5.  xx_chd.h has the
 * overview; the header tables are with chd_parse_header below.
 *
 * Written from the structure of the format.  Its layout is documented in
 * MAME's own sources (src/lib/util/chd.cpp, chdcodec.cpp, huffman.cpp, all
 * BSD-3-Clause); nothing here is a port of that code.  The CD-ROM parity
 * bytes are recomputed from ECMA-130 (RS(26,24) and RS(45,43) over
 * GF(2^8)), and the FLAC decoder follows RFC 9639.  Every stored checksum
 * the format carries is checked: the v5 map CRC-16, each hunk's CRC-16
 * (v5) or CRC-32 (v3/v4), each FLAC frame's CRC-8/CRC-16, and after a full
 * pass the raw-data SHA-1 (v3-v5) or MD5 (v1/v2) of the header.
 *
 * Hostile input: every offset and length from the file is checked against
 * the file size before use, hunks are capped at 16 MiB and a decoded v5 map
 * at 2^23 hunks, self references are followed at most 16 deep, and every
 * decoder loop is bounded by its output size or by the input consumed.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/chd/xx_chd.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder, keyed on the alias macro that xxfc_defs.h
 * defines next to the enumerator once CHD is registered there. */
#ifdef CHD
#define XX_CHD_FILE_TYPE XX_FILE_TYPE_CHD
#else
#define XX_CHD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define CHD_TAG(a, b, c, d)                                              \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | \
     (uint32_t)(d))

#define CHD_CODEC_ZLIB CHD_TAG('z', 'l', 'i', 'b')
#define CHD_CODEC_ZSTD CHD_TAG('z', 's', 't', 'd')
#define CHD_CODEC_LZMA CHD_TAG('l', 'z', 'm', 'a')
#define CHD_CODEC_HUFF CHD_TAG('h', 'u', 'f', 'f')
#define CHD_CODEC_FLAC CHD_TAG('f', 'l', 'a', 'c')
#define CHD_CODEC_CDZL CHD_TAG('c', 'd', 'z', 'l')
#define CHD_CODEC_CDLZ CHD_TAG('c', 'd', 'l', 'z')
#define CHD_CODEC_CDFL CHD_TAG('c', 'd', 'f', 'l')
#define CHD_CODEC_CDZS CHD_TAG('c', 'd', 'z', 's')
#define CHD_CODEC_AVHU CHD_TAG('a', 'v', 'h', 'u')
/* v3/v4 A/V codec: never decodable here, kept apart from avhu. */
#define CHD_CODEC_V34AV CHD_TAG('A', 'V', '3', '4')

#define CHD_META_GDDD CHD_TAG('G', 'D', 'D', 'D')
#define CHD_META_CHT2 CHD_TAG('C', 'H', 'T', '2')
#define CHD_META_CHTR CHD_TAG('C', 'H', 'T', 'R')
#define CHD_META_CHCD CHD_TAG('C', 'H', 'C', 'D')
#define CHD_META_CHGD CHD_TAG('C', 'H', 'G', 'D')
#define CHD_META_CHGT CHD_TAG('C', 'H', 'G', 'T')
#define CHD_META_DVD CHD_TAG('D', 'V', 'D', ' ')
#define CHD_META_AVAV CHD_TAG('A', 'V', 'A', 'V')

#define CHD_MAGIC "MComprHD"
#define CHD_MAX_HEADER 124U
#define CHD_FRAME 2448U
#define CHD_SECTOR 2352U
#define CHD_SUBCODE 96U
#define CHD_MAX_TRACKS 99U
#define CHD_TRACK_PAD 4U

#define CHD_MAX_HUNK (16U * 1024U * 1024U)
#define CHD_MAX_MAP_HUNKS (1U << 23)
#define CHD_MAX_META_ENTRIES 4096U
#define CHD_MAX_META_TEXT 512U
#define CHD_MAX_SELF_DEPTH 16U
#define CHD_V5_MAP_ENTRY 12U
#define CHD_SHEET_MAX 16384U

/* Decoded map entry kinds. */
enum {
    CHD_E_COMPRESSED = 0,
    CHD_E_STORED,
    CHD_E_MINI,
    CHD_E_ZERO,
    CHD_E_SELF,
    CHD_E_PARENT
};

/* v5 map codes. */
enum {
    V5_COMP0 = 0,
    V5_COMP3 = 3,
    V5_NONE = 4,
    V5_SELF = 5,
    V5_PARENT = 6,
    V5_RLE_SMALL = 7,
    V5_RLE_LARGE = 8,
    V5_SELF_0 = 9,
    V5_SELF_1 = 10,
    V5_PARENT_SELF = 11,
    V5_PARENT_0 = 12,
    V5_PARENT_1 = 13
};

/* CD track and subcode types, numbered as the binary CHCD table stores
 * them. */
enum {
    TRK_MODE1 = 0,
    TRK_MODE1_RAW,
    TRK_MODE2,
    TRK_MODE2_FORM1,
    TRK_MODE2_FORM2,
    TRK_MODE2_FORM_MIX,
    TRK_MODE2_RAW,
    TRK_AUDIO,
    TRK_COUNT
};
enum { SUB_RW = 0, SUB_RW_RAW, SUB_NONE, SUB_COUNT };

static const char *const chd_track_names[TRK_COUNT] = {
    "MODE1", "MODE1_RAW", "MODE2", "MODE2_FORM1", "MODE2_FORM2",
    "MODE2_FORM_MIX", "MODE2_RAW", "AUDIO"};
static const uint32_t chd_track_sizes[TRK_COUNT] = {2048U, 2352U, 2336U, 2048U,
                                                    2324U, 2336U, 2352U, 2352U};
static const char *const chd_sub_names[SUB_COUNT] = {"RW", "RW_RAW", "NONE"};
static const uint32_t chd_sub_sizes[SUB_COUNT] = {96U, 96U, 0U};

/* Members. */
enum { CHD_M_DATA = 0, CHD_M_CUE, CHD_M_BIN, CHD_M_GDI, CHD_M_TRACK };

typedef struct chd_track_s {
    uint32_t type;
    uint32_t subtype;
    uint32_t datasize;
    uint32_t subsize;
    uint32_t frames;
    uint32_t extraframes;
    uint32_t padframes;
    uint32_t pregap;
    uint32_t postgap;
    uint32_t pgdatasize;
    uint64_t chdframeofs;
    uint64_t physframeofs;
    bool defined;
} chd_track;

typedef struct chd_member_s {
    char name[16];
    uint8_t kind;
    uint8_t track;
    uint64_t size;
} chd_member;

typedef struct chd_info_s {
    int64_t base;
    int64_t size;          /**< Bytes from base to the end of the device. */
    uint32_t version;
    uint32_t header_len;
    uint32_t flags;
    uint32_t codecs[4];
    uint64_t logical_bytes;
    uint64_t map_offset;
    uint64_t meta_offset;
    uint32_t hunk_bytes;
    uint32_t unit_bytes;
    uint32_t hunk_count;
    uint32_t map_entry_size; /**< 8 (v1/v2), 16 (v3/v4), 4 (v5 raw), 0. */
    bool v5_compressed_map;
    bool has_parent;
    uint8_t digest[20];      /**< Raw data SHA-1 (v3-v5) or MD5 (v1/v2). */
    uint32_t digest_size;
    uint32_t kind;
    uint32_t track_count;
    chd_track tracks[CHD_MAX_TRACKS];
    uint32_t member_count;
    chd_member members[CHD_MAX_TRACKS + 1U];
    uint64_t total_frames;   /**< CD: frames the CHD holds, padding included. */
    bool gd_old_tag;         /**< GD-ROM tracks came from 'CHGT' entries. */
} chd_info;

/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */

static uint32_t chd_be16(const uint8_t *p) {
    return ((uint32_t)p[0] << 8) | (uint32_t)p[1];
}

static uint32_t chd_be24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

static uint32_t chd_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t chd_be48(const uint8_t *p) {
    return ((uint64_t)chd_be16(p) << 32) | (uint64_t)chd_be32(p + 2);
}

static uint64_t chd_be64(const uint8_t *p) {
    return ((uint64_t)chd_be32(p) << 32) | (uint64_t)chd_be32(p + 4);
}

static uint32_t chd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void chd_put_be16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void chd_put_be24(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)v;
}

static void chd_put_be48(uint8_t *p, uint64_t v) {
    uint32_t index;
    for (index = 0U; index < 6U; ++index)
        p[index] = (uint8_t)(v >> (8U * (5U - index)));
}

static bool chd_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Read @p size bytes at CHD offset @p offset, which must lie in the file. */
static bool chd_read(const chd_info *info, xx_io_device *device,
                     uint64_t offset, void *buffer, size_t size) {
    if (offset > (uint64_t)info->size ||
        (uint64_t)size > (uint64_t)info->size - offset)
        return false;
    return chd_read_at(device, info->base + (int64_t)offset, buffer, size);
}

static bool chd_all_zero(const uint8_t *p, size_t size) {
    size_t index;
    for (index = 0U; index < size; ++index)
        if (p[index] != 0U) return false;
    return true;
}

static bool chd_known_codec(uint32_t codec) {
    return codec == 0U || codec == CHD_CODEC_ZLIB || codec == CHD_CODEC_ZSTD ||
           codec == CHD_CODEC_LZMA || codec == CHD_CODEC_HUFF ||
           codec == CHD_CODEC_FLAC || codec == CHD_CODEC_CDZL ||
           codec == CHD_CODEC_CDLZ || codec == CHD_CODEC_CDFL ||
           codec == CHD_CODEC_CDZS || codec == CHD_CODEC_AVHU;
}

/* ---------------------------------------------------------------------- */
/* Header                                                                  */
/*
 * v1 (76)  / v2 (80):
 *   0x10 flags  0x14 compression  0x18 hunk size in sectors  0x1C hunks
 *   0x20 cylinders  0x24 heads  0x28 sectors  0x2C md5[16]  0x3C parent md5
 *   0x4C (v2) bytes per sector; v1 sectors are 512 bytes
 * v3 (120):
 *   0x10 flags  0x14 compression  0x18 hunks  0x1C logical bytes (u64)
 *   0x24 metadata offset (u64)  0x2C md5  0x3C parent md5  0x4C hunk bytes
 *   0x50 sha1  0x64 parent sha1
 * v4 (108):
 *   0x10 flags  0x14 compression  0x18 hunks  0x1C logical bytes
 *   0x24 metadata offset  0x2C hunk bytes  0x30 sha1  0x44 parent sha1
 *   0x58 raw sha1
 * v5 (124):
 *   0x10 codecs[4]  0x20 logical bytes  0x28 map offset  0x30 metadata
 *   offset  0x38 hunk bytes  0x3C unit bytes  0x40 raw sha1  0x54 sha1
 *   0x68 parent sha1
 * v1-v4 keep the map right after the header (8- or 16-byte entries); flag
 * bit 0 marks a parent.
 */

static uint32_t chd_header_length(uint32_t version) {
    switch (version) {
    case 1: return 76U;
    case 2: return 80U;
    case 3: return 120U;
    case 4: return 108U;
    case 5: return 124U;
    default: return 0U;
    }
}

static bool chd_parse_header(xx_io_device *device, int64_t base,
                             chd_info *info) {
    uint8_t h[CHD_MAX_HEADER];
    int64_t total;
    uint32_t length, version, index;
    uint64_t hunks;
    if (!device || !info || base < 0) return false;
    xx_mem_zero(info, sizeof(*info));
    total = xx_io_total_size(device);
    if (total < base || total - base < 76) return false;
    info->base = base;
    info->size = total - base;
    if (!chd_read_at(device, base, h, 16U) ||
        xx_rt_memcmp(h, CHD_MAGIC, 8U) != 0)
        return false;
    length = chd_be32(h + 8);
    version = chd_be32(h + 12);
    if (chd_header_length(version) == 0U || length != chd_header_length(version) ||
        (int64_t)length > info->size ||
        !chd_read_at(device, base, h, length))
        return false;
    info->version = version;
    info->header_len = length;
    if (version <= 2U) {
        uint32_t hunk_sectors = chd_be32(h + 24);
        uint32_t cylinders = chd_be32(h + 32);
        uint32_t heads = chd_be32(h + 36);
        uint32_t sectors = chd_be32(h + 40);
        uint32_t sector_bytes = version == 1U ? 512U : chd_be32(h + 76);
        uint64_t chs;
        info->flags = chd_be32(h + 16);
        if (chd_be32(h + 20) > 2U) return false;
        if (hunk_sectors == 0U || sector_bytes == 0U ||
            (uint64_t)hunk_sectors * sector_bytes > CHD_MAX_HUNK)
            return false;
        info->hunk_bytes = hunk_sectors * sector_bytes;
        info->hunk_count = chd_be32(h + 28);
        chs = (uint64_t)cylinders * heads;
        if (sectors != 0U && chs > UINT64_MAX / sectors) return false;
        chs *= sectors;
        if (chs > UINT64_MAX / sector_bytes) return false;
        info->logical_bytes = chs * sector_bytes;
        info->unit_bytes = sector_bytes;
        info->map_offset = length;
        info->map_entry_size = 8U;
        for (index = 0U; index < 4U; ++index) info->codecs[index] = 0U;
        info->codecs[0] = CHD_CODEC_ZLIB;
        info->has_parent = (info->flags & 1U) != 0U;
        xx_rt_memcpy(info->digest, h + 44, 16U);
        info->digest_size = 16U;
    } else if (version <= 4U) {
        uint32_t compression = chd_be32(h + 20);
        info->flags = chd_be32(h + 16);
        if (compression > 3U) return false;
        info->codecs[0] = compression == 3U ? CHD_CODEC_V34AV : CHD_CODEC_ZLIB;
        info->hunk_count = chd_be32(h + 24);
        info->logical_bytes = chd_be64(h + 28);
        info->meta_offset = chd_be64(h + 36);
        info->hunk_bytes = chd_be32(h + (version == 3U ? 76 : 44));
        info->unit_bytes = info->hunk_bytes;
        info->map_offset = length;
        info->map_entry_size = 16U;
        info->has_parent = (info->flags & 1U) != 0U;
        xx_rt_memcpy(info->digest, h + (version == 3U ? 80 : 88), 20U);
        info->digest_size = 20U;
    } else {
        for (index = 0U; index < 4U; ++index) {
            info->codecs[index] = chd_be32(h + 16 + 4U * index);
            if (!chd_known_codec(info->codecs[index])) return false;
        }
        info->logical_bytes = chd_be64(h + 32);
        info->map_offset = chd_be64(h + 40);
        info->meta_offset = chd_be64(h + 48);
        info->hunk_bytes = chd_be32(h + 56);
        info->unit_bytes = chd_be32(h + 60);
        if (info->unit_bytes == 0U || info->hunk_bytes == 0U ||
            info->unit_bytes > info->hunk_bytes ||
            info->hunk_bytes % info->unit_bytes != 0U)
            return false;
        if (info->hunk_bytes <= CHD_MAX_HUNK) {
            hunks = (info->logical_bytes + info->hunk_bytes - 1U) /
                    info->hunk_bytes;
            if (hunks > 0xFFFFFFFFULL) return false;
            info->hunk_count = (uint32_t)hunks;
        }
        info->v5_compressed_map = info->codecs[0] != 0U;
        info->map_entry_size = info->v5_compressed_map ? 0U : 4U;
        info->has_parent = !chd_all_zero(h + 104, 20U);
        xx_rt_memcpy(info->digest, h + 64, 20U);
        info->digest_size = 20U;
    }
    if (info->hunk_bytes == 0U || info->hunk_bytes > CHD_MAX_HUNK ||
        info->logical_bytes == 0U || info->hunk_count == 0U)
        return false;
    /* The hunks must cover the logical data. */
    if ((uint64_t)info->hunk_count * info->hunk_bytes < info->logical_bytes)
        return false;
    if (chd_all_zero(info->digest, info->digest_size)) info->digest_size = 0U;
    /* Map placement. */
    if (info->map_offset < length || info->map_offset > (uint64_t)info->size)
        return false;
    if (info->map_entry_size != 0U) {
        uint64_t map_bytes = (uint64_t)info->hunk_count * info->map_entry_size;
        if (map_bytes > (uint64_t)info->size - info->map_offset) return false;
    } else if ((uint64_t)info->size - info->map_offset < 16U) {
        return false;
    }
    if (info->meta_offset != 0U &&
        (info->meta_offset < length || info->meta_offset > (uint64_t)info->size ||
         (uint64_t)info->size - info->meta_offset < 16U))
        return false;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Metadata                                                                */

static int32_t chd_find_name(const char *const *names, uint32_t count,
                             const char *value) {
    uint32_t index;
    for (index = 0U; index < count; ++index)
        if (xx_str_cmp(names[index], value) == 0) return (int32_t)index;
    return -1;
}

static bool chd_parse_u32(const char *text, uint32_t *out) {
    uint64_t value = 0U;
    if (!text || !*text) return false;
    while (*text) {
        if (*text < '0' || *text > '9') return false;
        value = value * 10U + (uint64_t)(*text - '0');
        if (value > 0xFFFFFFFFULL) return false;
        ++text;
    }
    *out = (uint32_t)value;
    return true;
}

/* "KEY:VALUE KEY:VALUE ..." -> the value of @p key, copied to @p out. */
static bool chd_meta_value(const char *text, const char *key, char *out,
                           size_t out_size) {
    size_t key_length = xx_str_len(key);
    const char *p = text;
    while (*p) {
        const char *token;
        size_t length = 0U;
        while (*p == ' ') ++p;
        token = p;
        while (p[length] && p[length] != ' ') ++length;
        if (length > key_length && token[key_length] == ':' &&
            xx_rt_memcmp(token, key, key_length) == 0) {
            size_t value_length = length - key_length - 1U;
            if (value_length + 1U > out_size) return false;
            xx_rt_memcpy(out, token + key_length + 1U, value_length);
            out[value_length] = 0;
            return true;
        }
        p += length;
    }
    return false;
}

static bool chd_meta_number(const char *text, const char *key,
                            uint32_t *out) {
    char value[16];
    return chd_meta_value(text, key, value, sizeof(value)) &&
           chd_parse_u32(value, out);
}

/* One CHTR / CHT2 / CHGD text entry. */
static bool chd_parse_track_text(chd_info *info, const char *text,
                                 uint32_t tag) {
    char value[32];
    uint32_t number, frames;
    int32_t type, subtype;
    chd_track *track;
    if (!chd_meta_number(text, "TRACK", &number) || number == 0U ||
        number > CHD_MAX_TRACKS)
        return false;
    track = &info->tracks[number - 1U];
    if (track->defined) return false;
    if (!chd_meta_value(text, "TYPE", value, sizeof(value)) ||
        (type = chd_find_name(chd_track_names, TRK_COUNT, value)) < 0)
        return false;
    if (!chd_meta_value(text, "SUBTYPE", value, sizeof(value)) ||
        (subtype = chd_find_name(chd_sub_names, SUB_COUNT, value)) < 0)
        return false;
    if (!chd_meta_number(text, "FRAMES", &frames) || frames == 0U ||
        frames > 0x10000000U)
        return false;
    xx_mem_zero(track, sizeof(*track));
    track->type = (uint32_t)type;
    track->subtype = (uint32_t)subtype;
    track->datasize = chd_track_sizes[type];
    track->subsize = chd_sub_sizes[subtype];
    track->frames = frames;
    track->extraframes =
        ((frames + CHD_TRACK_PAD - 1U) / CHD_TRACK_PAD) * CHD_TRACK_PAD - frames;
    if (tag != CHD_META_CHTR) {
        if (!chd_meta_number(text, "PREGAP", &track->pregap) ||
            !chd_meta_number(text, "POSTGAP", &track->postgap) ||
            !chd_meta_value(text, "PGTYPE", value, sizeof(value)))
            return false;
        /* A 'V' prefix means the pregap frames are stored in the image. */
        if (value[0] == 'V') {
            int32_t pgtype = chd_find_name(chd_track_names, TRK_COUNT, value + 1);
            track->pgdatasize = pgtype >= 0 ? chd_track_sizes[pgtype] : 0U;
        }
        if (track->pregap > 0x10000000U || track->postgap > 0x10000000U)
            return false;
    }
    if (tag == CHD_META_CHGD || tag == CHD_META_CHGT) {
        if (!chd_meta_number(text, "PAD", &track->padframes) ||
            track->padframes > frames)
            return false;
    }
    track->defined = true;
    return true;
}

/* The old binary TOC: u32 track count, then 99 x {type, subtype, data
 * size, subcode size, frames, extra frames}, in the byte order of the
 * machine that wrote it (a track count over 99 means the other one). */
static bool chd_parse_chcd(chd_info *info, const uint8_t *data,
                           uint32_t size) {
    uint32_t count, index;
    bool big;
    if (size < 4U + CHD_MAX_TRACKS * 24U) return false;
    count = chd_le32(data);
    big = count > CHD_MAX_TRACKS;
    if (big) count = chd_be32(data);
    if (count == 0U || count > CHD_MAX_TRACKS) return false;
    for (index = 0U; index < count; ++index) {
        const uint8_t *p = data + 4U + index * 24U;
        chd_track *track = &info->tracks[index];
        uint32_t f[6], field;
        for (field = 0U; field < 6U; ++field)
            f[field] = big ? chd_be32(p + 4U * field) : chd_le32(p + 4U * field);
        if (f[0] >= TRK_COUNT || f[1] >= SUB_COUNT || f[2] == 0U ||
            f[2] > CHD_SECTOR || f[3] > CHD_SUBCODE || f[4] == 0U ||
            f[4] > 0x10000000U || f[5] > 0x10000000U)
            return false;
        xx_mem_zero(track, sizeof(*track));
        track->type = f[0];
        track->subtype = f[1];
        track->datasize = f[2];
        track->subsize = f[3];
        track->frames = f[4];
        track->extraframes = f[5];
        track->defined = true;
    }
    info->track_count = count;
    return true;
}

typedef struct chd_meta_scan_s {
    uint32_t text_tracks[3]; /**< CHTR, CHT2, CHGD/CHGT entries seen. */
    bool has_chcd, has_dvd, has_gddd, has_av;
} chd_meta_scan;

/* Walk the metadata list and collect what classifies the image.  Track
 * text entries are parsed for one tag family only: the first of CHTR,
 * CHT2 and CHGD that is present, as MAME looks them up. */
static bool chd_scan_metadata(xx_io_device *device, chd_info *info,
                              chd_meta_scan *scan, uint32_t want_tag) {
    uint64_t offset = info->meta_offset;
    uint32_t count = 0U;
    xx_mem_zero(scan, sizeof(*scan));
    while (offset != 0U) {
        uint8_t head[16];
        uint32_t tag, length;
        if (++count > CHD_MAX_META_ENTRIES || offset < info->header_len ||
            !chd_read(info, device, offset, head, sizeof(head)))
            return false;
        tag = chd_be32(head);
        length = chd_be32(head + 4) & 0x00FFFFFFU;
        if ((uint64_t)length > (uint64_t)info->size - offset - 16U)
            return false;
        if (tag == CHD_META_CHTR) ++scan->text_tracks[0];
        else if (tag == CHD_META_CHT2) ++scan->text_tracks[1];
        else if (tag == CHD_META_CHGD || tag == CHD_META_CHGT) ++scan->text_tracks[2];
        else if (tag == CHD_META_CHCD) scan->has_chcd = true;
        else if (tag == CHD_META_DVD) scan->has_dvd = true;
        else if (tag == CHD_META_GDDD) scan->has_gddd = true;
        else if (tag == CHD_META_AVAV) scan->has_av = true;
        if (want_tag != 0U &&
            (tag == want_tag ||
             (want_tag == CHD_META_CHGD && tag == CHD_META_CHGT))) {
            if (tag == CHD_META_CHCD) {
                uint8_t *table;
                bool ok;
                if (length < 4U + CHD_MAX_TRACKS * 24U || length > 65536U)
                    return false;
                table = (uint8_t *)xx_mem_alloc(length);
                if (!table) return false;
                ok = chd_read(info, device, offset + 16U, table, length) &&
                     chd_parse_chcd(info, table, length);
                xx_mem_free(table);
                if (!ok) return false;
                want_tag = 0U;
            } else {
                char text[CHD_MAX_META_TEXT + 1U];
                uint32_t used = length > CHD_MAX_META_TEXT ? CHD_MAX_META_TEXT
                                                           : length;
                uint32_t index;
                if (!chd_read(info, device, offset + 16U, text, used))
                    return false;
                text[used] = 0;
                for (index = 0U; index < used; ++index)
                    if (text[index] == 0) break;
                    else if ((unsigned char)text[index] < 0x20U) text[index] = ' ';
                if (!chd_parse_track_text(info, text, tag)) return false;
                if (tag == CHD_META_CHGT) info->gd_old_tag = true;
                ++info->track_count;
            }
        }
        offset = chd_be64(head + 8);
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Classification and members                                              */

static void chd_msf(char *out, size_t size, uint64_t frames) {
    (void)xx_rt_snprintf(out, size, "%02u:%02u:%02u",
                         (unsigned)(frames / (75U * 60U)),
                         (unsigned)((frames / 75U) % 60U),
                         (unsigned)(frames % 75U));
}

static void chd_add_member(chd_info *info, const char *name, uint8_t kind,
                           uint8_t track, uint64_t size) {
    chd_member *member = &info->members[info->member_count++];
    size_t length = xx_str_len(name);
    if (length >= sizeof(member->name)) length = sizeof(member->name) - 1U;
    xx_rt_memcpy(member->name, name, length);
    member->name[length] = 0;
    member->kind = kind;
    member->track = track;
    member->size = size;
}

/* The cue sheet or GDI list chdman writes for this image (CRLF lines, as
 * the Windows build emits them). */
static size_t chd_build_sheet(const chd_info *info, char *out, size_t size) {
    size_t used = 0U;
    uint64_t frameofs = 0U;
    uint32_t index;
    char line[160];
#define CHD_EMIT(...)                                                        \
    do {                                                                     \
        int n_ = xx_rt_snprintf(line, sizeof(line), __VA_ARGS__);            \
        if (n_ < 0 || (size_t)n_ >= sizeof(line) || used + (size_t)n_ > size) \
            return 0U;                                                       \
        if (out) xx_rt_memcpy(out + used, line, (size_t)n_);                  \
        used += (size_t)n_;                                                  \
    } while (0)
    if (info->kind == XX_CHD_KIND_GD) {
        CHD_EMIT("%u\r\n", (unsigned)info->track_count);
        for (index = 0U; index < info->track_count; ++index) {
            const chd_track *track = &info->tracks[index];
            CHD_EMIT("%u %u %u %u disc%02u.%s 0\r\n", (unsigned)(index + 1U),
                     (unsigned)frameofs,
                     track->type == TRK_AUDIO ? 0U : 4U,
                     (unsigned)track->datasize, (unsigned)(index + 1U),
                     track->type == TRK_AUDIO ? "raw" : "bin");
            frameofs += track->frames;
        }
        return used;
    }
    for (index = 0U; index < info->track_count; ++index) {
        const chd_track *track = &info->tracks[index];
        char msf[16], msf2[16];
        if (index == 0U) CHD_EMIT("FILE \"disc.bin\" BINARY\r\n");
        if (track->type == TRK_AUDIO)
            CHD_EMIT("  TRACK %02u AUDIO\r\n", (unsigned)(index + 1U));
        else
            CHD_EMIT("  TRACK %02u %s/%04u\r\n", (unsigned)(index + 1U),
                     (track->type == TRK_MODE1 || track->type == TRK_MODE1_RAW)
                         ? "MODE1" : "MODE2",
                     (unsigned)track->datasize);
        chd_msf(msf, sizeof(msf), frameofs);
        if (track->pregap > 0U && track->pgdatasize == 0U) {
            chd_msf(msf2, sizeof(msf2), track->pregap);
            CHD_EMIT("    PREGAP %s\r\n", msf2);
            CHD_EMIT("    INDEX 01 %s\r\n", msf);
        } else if (track->pregap > 0U) {
            chd_msf(msf2, sizeof(msf2), frameofs + track->pregap);
            CHD_EMIT("    INDEX 00 %s\r\n", msf);
            CHD_EMIT("    INDEX 01 %s\r\n", msf2);
        } else {
            CHD_EMIT("    INDEX 01 %s\r\n", msf);
        }
        if (track->postgap > 0U) {
            chd_msf(msf2, sizeof(msf2), track->postgap);
            CHD_EMIT("    POSTGAP %s\r\n", msf2);
        }
        frameofs += track->frames;
    }
#undef CHD_EMIT
    return used;
}

static bool chd_layout_tracks(chd_info *info) {
    uint64_t chd = 0U, phys = 0U, hunk_frames;
    uint32_t index;
    if (info->track_count == 0U || info->hunk_bytes % CHD_FRAME != 0U)
        return false;
    for (index = 0U; index < info->track_count; ++index) {
        chd_track *track = &info->tracks[index];
        if (!track->defined || track->datasize == 0U ||
            track->datasize > CHD_SECTOR || track->padframes > track->frames)
            return false;
        track->chdframeofs = chd;
        track->physframeofs = phys;
        chd += (uint64_t)track->frames + track->extraframes;
        phys += track->frames;
    }
    hunk_frames = (uint64_t)info->hunk_count * (info->hunk_bytes / CHD_FRAME);
    if (chd > hunk_frames) return false;
    info->total_frames = chd;
    return true;
}

/* Header + metadata -> kind, tracks and members. */
static bool chd_parse(xx_io_device *device, int64_t base, chd_info *info) {
    chd_meta_scan scan;
    uint32_t want = 0U, index;
    if (!chd_parse_header(device, base, info)) return false;
    if (info->version >= 3U) {
        if (!chd_scan_metadata(device, info, &scan, 0U)) return false;
        if (scan.text_tracks[0]) want = CHD_META_CHTR;
        else if (scan.text_tracks[1]) want = CHD_META_CHT2;
        else if (scan.text_tracks[2]) want = CHD_META_CHGD;
        else if (scan.has_chcd) want = CHD_META_CHCD;
    } else {
        xx_mem_zero(&scan, sizeof(scan));
    }
    info->kind = XX_CHD_KIND_RAW;
    if (want != 0U) {
        chd_meta_scan again;
        info->track_count = 0U;
        for (index = 0U; index < CHD_MAX_TRACKS; ++index)
            info->tracks[index].defined = false;
        if (chd_scan_metadata(device, info, &again, want) &&
            chd_layout_tracks(info))
            info->kind = want == CHD_META_CHGD ? XX_CHD_KIND_GD : XX_CHD_KIND_CD;
        else
            info->track_count = 0U;
    }
    if (info->kind == XX_CHD_KIND_RAW) {
        bool av = scan.has_av || info->codecs[0] == CHD_CODEC_V34AV;
        for (index = 0U; index < 4U; ++index)
            if (info->codecs[index] == CHD_CODEC_AVHU) av = true;
        if (av) info->kind = XX_CHD_KIND_AV;
        else if (scan.has_dvd) info->kind = XX_CHD_KIND_DVD;
        else if (scan.has_gddd || info->version <= 2U) info->kind = XX_CHD_KIND_HD;
    }
    info->member_count = 0U;
    switch (info->kind) {
    case XX_CHD_KIND_HD:
        chd_add_member(info, "disk.img", CHD_M_DATA, 0U, info->logical_bytes);
        break;
    case XX_CHD_KIND_DVD:
        chd_add_member(info, "disc.iso", CHD_M_DATA, 0U, info->logical_bytes);
        break;
    case XX_CHD_KIND_CD: {
        uint64_t bin = 0U;
        for (index = 0U; index < info->track_count; ++index)
            bin += (uint64_t)(info->tracks[index].frames -
                              info->tracks[index].padframes) *
                   info->tracks[index].datasize;
        chd_add_member(info, "disc.cue", CHD_M_CUE, 0U,
                       chd_build_sheet(info, NULL, CHD_SHEET_MAX));
        chd_add_member(info, "disc.bin", CHD_M_BIN, 0U, bin);
        break;
    }
    case XX_CHD_KIND_GD:
        chd_add_member(info, "disc.gdi", CHD_M_GDI, 0U,
                       chd_build_sheet(info, NULL, CHD_SHEET_MAX));
        for (index = 0U; index < info->track_count; ++index) {
            char name[16];
            const chd_track *track = &info->tracks[index];
            (void)xx_rt_snprintf(name, sizeof(name), "disc%02u.%s",
                                 (unsigned)(index + 1U),
                                 track->type == TRK_AUDIO ? "raw" : "bin");
            chd_add_member(info, name, CHD_M_TRACK, (uint8_t)index,
                           (uint64_t)(track->frames - track->padframes) *
                               track->datasize);
        }
        break;
    default:
        chd_add_member(info, "data.bin", CHD_M_DATA, 0U, info->logical_bytes);
        break;
    }
    if ((info->kind == XX_CHD_KIND_CD || info->kind == XX_CHD_KIND_GD) &&
        info->members[0].size == 0U)
        return false;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Bit input (MSB first; zeros past the end, remembered as overflow)       */

typedef struct chd_bits_s {
    const uint8_t *data;
    size_t size;
    size_t next;
    uint64_t cache;
    uint32_t count;
} chd_bits;

static void chd_bits_init(chd_bits *b, const uint8_t *data, size_t size) {
    b->data = data;
    b->size = size;
    b->next = 0U;
    b->cache = 0U;
    b->count = 0U;
}

static void chd_bits_fill(chd_bits *b) {
    while (b->count <= 56U) {
        uint64_t byte = b->next < b->size ? b->data[b->next] : 0U;
        ++b->next;
        b->cache |= byte << (56U - b->count);
        b->count += 8U;
    }
}

/* Bits consumed so far. */
static uint64_t chd_bits_used(const chd_bits *b) {
    return (uint64_t)b->next * 8U - b->count;
}

static bool chd_bits_overflow(const chd_bits *b) {
    return chd_bits_used(b) > (uint64_t)b->size * 8U;
}

static uint32_t chd_bits_peek(chd_bits *b, uint32_t n) {
    if (n == 0U) return 0U;
    if (b->count < n) chd_bits_fill(b);
    return (uint32_t)(b->cache >> (64U - n));
}

static void chd_bits_skip(chd_bits *b, uint32_t n) {
    if (n == 0U) return;
    if (b->count < n) chd_bits_fill(b);
    b->cache = n >= 64U ? 0U : b->cache << n;
    b->count -= n;
}

static uint32_t chd_bits_read(chd_bits *b, uint32_t n) {
    uint32_t value = chd_bits_peek(b, n);
    chd_bits_skip(b, n);
    return value;
}

static uint64_t chd_bits_read_wide(chd_bits *b, uint32_t n) {
    if (n <= 32U) return chd_bits_read(b, n);
    return ((uint64_t)chd_bits_read(b, n - 32U) << 32) | chd_bits_read(b, 32U);
}

static int32_t chd_bits_signed(chd_bits *b, uint32_t n) {
    uint32_t value;
    if (n == 0U) return 0;
    value = chd_bits_read(b, n);
    if (n < 32U && (value & (1U << (n - 1U))) != 0U) value |= ~0U << n;
    return (int32_t)value;
}

/* Count zero bits up to the next 1 (consumed too).  Fails past the end. */
static bool chd_bits_unary(chd_bits *b, uint32_t limit, uint32_t *out) {
    uint32_t zeros = 0U;
    for (;;) {
        if (b->count == 0U || b->cache == 0U) {
            /* The whole cache is zero: take it and look further. */
            zeros += b->count;
            b->cache = 0U;
            b->count = 0U;
            if (zeros > limit || chd_bits_overflow(b)) return false;
            chd_bits_fill(b);
            if (chd_bits_overflow(b) && b->cache == 0U) return false;
            continue;
        }
        {
            uint32_t lead = 0U;
            uint64_t probe = b->cache;
            while ((probe & 0x8000000000000000ULL) == 0U) {
                probe <<= 1;
                ++lead;
            }
            if (lead >= b->count) {
                zeros += b->count;
                b->cache = 0U;
                b->count = 0U;
                if (zeros > limit) return false;
                chd_bits_fill(b);
                continue;
            }
            zeros += lead;
            if (zeros > limit) return false;
            chd_bits_skip(b, lead + 1U);
            *out = zeros;
            return true;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* MAME's canonical Huffman trees                                          */
/*
 * Code lengths are assigned from the longest down: the longest codes start
 * at zero, each shorter length starts at half of where the longer one
 * ended, and a length other than 1 must end on an even count.  The decode
 * table is indexed by the next max_bits bits and holds symbol << 5 | length;
 * zero marks a bit pattern no code uses.
 */

typedef struct chd_huff_s {
    uint32_t codes;
    uint32_t max_bits;
    uint8_t lengths[256];
    uint16_t *table; /**< 1 << max_bits entries. */
} chd_huff;

static bool chd_huff_build(chd_huff *h) {
    uint32_t histogram[33], index, start = 0U, length;
    uint32_t assigned[256];
    for (index = 0U; index < 33U; ++index) histogram[index] = 0U;
    for (index = 0U; index < h->codes; ++index) {
        if (h->lengths[index] > h->max_bits) return false;
        ++histogram[h->lengths[index]];
    }
    for (length = 32U; length > 0U; --length) {
        uint32_t next = (start + histogram[length]) >> 1;
        if (length != 1U && next * 2U != start + histogram[length]) return false;
        /* Past one bit only the root is left: at most two codes there. */
        if (length == 1U && start + histogram[1] > 2U) return false;
        histogram[length] = start;
        start = next;
    }
    xx_mem_zero(h->table, sizeof(uint16_t) << h->max_bits);
    for (index = 0U; index < h->codes; ++index) {
        uint32_t bits = h->lengths[index], shift, first, last, slot;
        if (bits == 0U) continue;
        assigned[index] = histogram[bits]++;
        shift = h->max_bits - bits;
        if (assigned[index] >= (1U << bits)) return false;
        first = assigned[index] << shift;
        last = ((assigned[index] + 1U) << shift);
        for (slot = first; slot < last; ++slot)
            h->table[slot] = (uint16_t)((index << 5) | bits);
    }
    return true;
}

static bool chd_huff_decode(chd_huff *h, chd_bits *b, uint32_t *symbol) {
    uint16_t entry = h->table[chd_bits_peek(b, h->max_bits)];
    if (entry == 0U) return false;
    chd_bits_skip(b, entry & 0x1FU);
    *symbol = entry >> 5;
    return true;
}

/* Lengths as runs: a value other than 1 is a length; 1 escapes, and is
 * followed by 1 (a literal 1) or by a length and a repeat count - 3. */
static bool chd_huff_import_rle(chd_huff *h, chd_bits *b) {
    uint32_t width = h->max_bits >= 16U ? 5U : (h->max_bits >= 8U ? 4U : 3U);
    uint32_t node = 0U;
    while (node < h->codes) {
        uint32_t bits = chd_bits_read(b, width);
        if (bits != 1U) {
            h->lengths[node++] = (uint8_t)bits;
        } else {
            bits = chd_bits_read(b, width);
            if (bits == 1U) {
                h->lengths[node++] = 1U;
            } else {
                uint32_t repeat = chd_bits_read(b, width) + 3U;
                if (repeat > h->codes - node) return false;
                while (repeat--) h->lengths[node++] = (uint8_t)bits;
            }
        }
    }
    return chd_huff_build(h) && !chd_bits_overflow(b);
}

/* Lengths coded with a small 24-symbol Huffman tree of their own. */
static bool chd_huff_import_huffman(chd_huff *h, chd_bits *b) {
    chd_huff small;
    uint16_t small_table[1U << 6];
    uint32_t start, count = 0U, index, rle_bits = 0U, temp, last = 0U,
                    node = 0U;
    small.codes = 24U;
    small.max_bits = 6U;
    small.table = small_table;
    small.lengths[0] = (uint8_t)chd_bits_read(b, 3U);
    start = chd_bits_read(b, 3U) + 1U;
    for (index = 1U; index < 24U; ++index) {
        if (index < start || count == 7U) {
            small.lengths[index] = 0U;
        } else {
            count = chd_bits_read(b, 3U);
            small.lengths[index] = (uint8_t)(count == 7U ? 0U : count);
        }
    }
    if (!chd_huff_build(&small)) return false;
    for (temp = h->codes - 9U; temp != 0U; temp >>= 1) ++rle_bits;
    while (node < h->codes) {
        uint32_t value;
        if (!chd_huff_decode(&small, b, &value)) return false;
        if (value != 0U) {
            last = value - 1U;
            if (last > 32U) return false;
            h->lengths[node++] = (uint8_t)last;
        } else {
            uint32_t repeat = chd_bits_read(b, 3U) + 2U;
            if (repeat == 9U) repeat += chd_bits_read(b, rle_bits);
            while (repeat != 0U && node < h->codes) {
                h->lengths[node++] = (uint8_t)last;
                --repeat;
            }
        }
        if (chd_bits_overflow(b)) return false;
    }
    return chd_huff_build(h) && !chd_bits_overflow(b);
}

/* ---------------------------------------------------------------------- */
/* Decoder context                                                         */

typedef struct chd_ctx_s {
    const chd_info *info;
    xx_io_device *device;
    uint8_t *map;          /**< Decoded v5 map, 12 bytes per hunk. */
    uint8_t *packed;       /**< One stored hunk (<= hunk_bytes). */
    uint8_t *work;         /**< CD codecs: sector data then subcode. */
    uint16_t *huff_table;  /**< 1 << 16 entries for the huff codec. */
    int32_t *flac[2];      /**< One FLAC block per channel. */
    uint8_t *mapbuf;       /**< v1-v4 / v5 raw map window. */
    uint32_t mapbuf_first;
    uint32_t mapbuf_count;
    uint16_t crc16_table[256];
    uint8_t gf_exp[512];
    uint8_t gf_log[256];
    bool map_failed;
} chd_ctx;

#define CHD_FLAC_MAX_BLOCK 65536U
#define CHD_MAPBUF_ENTRIES 4096U

static void chd_ctx_free(chd_ctx *c) {
    if (!c) return;
    if (c->map) xx_mem_free(c->map);
    if (c->packed) xx_mem_free(c->packed);
    if (c->work) xx_mem_free(c->work);
    if (c->huff_table) xx_mem_free(c->huff_table);
    if (c->flac[0]) xx_mem_free(c->flac[0]);
    if (c->flac[1]) xx_mem_free(c->flac[1]);
    if (c->mapbuf) xx_mem_free(c->mapbuf);
    xx_mem_free(c);
}

static chd_ctx *chd_ctx_create(const chd_info *info, xx_io_device *device) {
    chd_ctx *c = (chd_ctx *)xx_mem_calloc(1U, sizeof(*c));
    uint32_t index, x = 1U;
    if (!c) return NULL;
    c->info = info;
    c->device = device;
    c->packed = (uint8_t *)xx_mem_alloc(info->hunk_bytes);
    c->work = (uint8_t *)xx_mem_alloc(info->hunk_bytes);
    if (!c->packed || !c->work) {
        chd_ctx_free(c);
        return NULL;
    }
    /* FLAC frame CRC-16: polynomial 0x8005, MSB first, initial 0. */
    for (index = 0U; index < 256U; ++index) {
        uint32_t crc = index << 8, bit;
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc & 0x8000U) ? ((crc << 1) ^ 0x8005U) : (crc << 1);
        c->crc16_table[index] = (uint16_t)crc;
    }
    /* GF(2^8) with x^8 + x^4 + x^3 + x^2 + 1 (ECMA-130). */
    for (index = 0U; index < 255U; ++index) {
        c->gf_exp[index] = (uint8_t)x;
        c->gf_log[x] = (uint8_t)index;
        x <<= 1;
        if (x & 0x100U) x ^= 0x11DU;
    }
    for (index = 255U; index < 512U; ++index)
        c->gf_exp[index] = c->gf_exp[index - 255U];
    return c;
}

/* ---------------------------------------------------------------------- */
/* v5 map                                                                  */
/*
 * At the map offset: u32 compressed size, u48 offset of the first hunk
 * data, u16 CRC-16 of the decoded map, and the bit widths of a compressed
 * length, a self reference and a parent reference.  Then one bit stream:
 * a Huffman tree over the 16 map codes, one code per hunk (7 and 8 repeat
 * the previous code 3 + n and 19 + n times), and then per hunk the fields
 * its code needs.  Each decoded entry is {code, u24 length, u48 offset,
 * u16 CRC}; the CRC-16/CCITT of all of them must match the header's.
 */
static bool chd_v5_decode_map(chd_ctx *c) {
    const chd_info *info = c->info;
    uint8_t head[16];
    uint8_t *packed = NULL;
    uint16_t table[256];
    chd_huff huff;
    chd_bits bits;
    uint32_t packed_size, length_bits, self_bits, parent_bits, hunk,
        repeat = 0U, last_code = 0U;
    uint64_t current, last_self = 0U, last_parent = 0U;
    bool ok = false;
    if (c->map) return true;
    if (c->map_failed || info->hunk_count > CHD_MAX_MAP_HUNKS) return false;
    c->map_failed = true;
    if (!chd_read(info, c->device, info->map_offset, head, sizeof(head)))
        return false;
    packed_size = chd_be32(head);
    current = chd_be48(head + 4);
    length_bits = head[12];
    self_bits = head[13];
    parent_bits = head[14];
    if (packed_size == 0U || length_bits > 24U || self_bits > 32U ||
        parent_bits > 48U ||
        (uint64_t)packed_size > (uint64_t)info->size - info->map_offset - 16U)
        return false;
    /* Every map code takes at least one bit and the longest run (three
     * codes) covers 274 hunks, so no map describes more than ~92 hunks
     * per bit: refuse a hunk count the packed map cannot hold before
     * allocating the decoded map. */
    if ((uint64_t)info->hunk_count > (uint64_t)packed_size * 8U * 92U)
        return false;
    packed = (uint8_t *)xx_mem_alloc(packed_size);
    c->map = (uint8_t *)xx_mem_alloc((size_t)info->hunk_count *
                                     CHD_V5_MAP_ENTRY);
    if (!packed || !c->map ||
        !chd_read(info, c->device, info->map_offset + 16U, packed, packed_size))
        goto done;
    chd_bits_init(&bits, packed, packed_size);
    huff.codes = 16U;
    huff.max_bits = 8U;
    huff.table = table;
    if (!chd_huff_import_rle(&huff, &bits)) goto done;
    for (hunk = 0U; hunk < info->hunk_count; ++hunk) {
        uint8_t *entry = c->map + (size_t)hunk * CHD_V5_MAP_ENTRY;
        if (repeat > 0U) {
            entry[0] = (uint8_t)last_code;
            --repeat;
        } else {
            uint32_t code, extra;
            if (!chd_huff_decode(&huff, &bits, &code)) goto done;
            if (code == V5_RLE_SMALL) {
                if (!chd_huff_decode(&huff, &bits, &extra)) goto done;
                entry[0] = (uint8_t)last_code;
                repeat = 2U + extra;
            } else if (code == V5_RLE_LARGE) {
                uint32_t low;
                if (!chd_huff_decode(&huff, &bits, &extra) ||
                    !chd_huff_decode(&huff, &bits, &low))
                    goto done;
                entry[0] = (uint8_t)last_code;
                repeat = 2U + 16U + (extra << 4) + low;
            } else {
                if (code > V5_PARENT_1) goto done;
                entry[0] = (uint8_t)code;
                last_code = code;
            }
        }
        if ((hunk & 0xFFFFU) == 0U && chd_bits_overflow(&bits)) goto done;
    }
    if (chd_bits_overflow(&bits)) goto done;
    for (hunk = 0U; hunk < info->hunk_count; ++hunk) {
        uint8_t *entry = c->map + (size_t)hunk * CHD_V5_MAP_ENTRY;
        uint64_t offset = current;
        uint32_t length = 0U, crc = 0U;
        switch (entry[0]) {
        case 0: case 1: case 2: case 3:
            length = chd_bits_read(&bits, length_bits);
            current += length;
            crc = chd_bits_read(&bits, 16U);
            break;
        case V5_NONE:
            length = info->hunk_bytes;
            current += length;
            crc = chd_bits_read(&bits, 16U);
            break;
        case V5_SELF:
            offset = chd_bits_read_wide(&bits, self_bits);
            last_self = offset;
            break;
        case V5_PARENT:
            offset = chd_bits_read_wide(&bits, parent_bits);
            last_parent = offset;
            break;
        case V5_SELF_1:
            ++last_self;
            /* fall through */
        case V5_SELF_0:
            entry[0] = V5_SELF;
            offset = last_self;
            break;
        case V5_PARENT_SELF:
            entry[0] = V5_PARENT;
            offset = ((uint64_t)hunk * info->hunk_bytes) / info->unit_bytes;
            last_parent = offset;
            break;
        case V5_PARENT_1:
            last_parent += info->hunk_bytes / info->unit_bytes;
            /* fall through */
        case V5_PARENT_0:
            entry[0] = V5_PARENT;
            offset = last_parent;
            break;
        default:
            goto done;
        }
        chd_put_be24(entry + 1, length);
        chd_put_be48(entry + 4, offset);
        chd_put_be16(entry + 10, crc);
        if ((hunk & 0xFFFFU) == 0U && chd_bits_overflow(&bits)) goto done;
    }
    if (chd_bits_overflow(&bits)) goto done;
    if (xx_crc16_ccitt_calc(0xFFFFU, c->map,
                            (size_t)info->hunk_count * CHD_V5_MAP_ENTRY) !=
        chd_be16(head + 10))
        goto done;
    ok = true;
    c->map_failed = false;
done:
    if (packed) xx_mem_free(packed);
    if (!ok && c->map) {
        xx_mem_free(c->map);
        c->map = NULL;
    }
    return ok;
}

typedef struct chd_entry_s {
    uint32_t kind;
    uint32_t slot;     /**< Codec slot of a compressed hunk. */
    uint32_t length;
    uint64_t offset;   /**< File offset, hunk number or mini pattern. */
    uint32_t crc;
    uint32_t crc_kind; /**< 0 none, 16, 32. */
} chd_entry;

/* Raw map entries of v1-v4 and uncompressed v5 maps, read in windows. */
static bool chd_map_raw(chd_ctx *c, uint32_t hunk, const uint8_t **out) {
    const chd_info *info = c->info;
    uint32_t size = info->map_entry_size;
    if (!c->mapbuf) {
        c->mapbuf = (uint8_t *)xx_mem_alloc((size_t)CHD_MAPBUF_ENTRIES * 16U);
        if (!c->mapbuf) return false;
        c->mapbuf_count = 0U;
    }
    if (hunk < c->mapbuf_first || hunk - c->mapbuf_first >= c->mapbuf_count) {
        uint32_t count = info->hunk_count - hunk;
        if (count > CHD_MAPBUF_ENTRIES) count = CHD_MAPBUF_ENTRIES;
        c->mapbuf_count = 0U;
        if (!chd_read(info, c->device,
                      info->map_offset + (uint64_t)hunk * size, c->mapbuf,
                      (size_t)count * size))
            return false;
        c->mapbuf_first = hunk;
        c->mapbuf_count = count;
    }
    *out = c->mapbuf + (size_t)(hunk - c->mapbuf_first) * size;
    return true;
}

static bool chd_get_entry(chd_ctx *c, uint32_t hunk, chd_entry *e) {
    const chd_info *info = c->info;
    const uint8_t *raw;
    xx_mem_zero(e, sizeof(*e));
    if (hunk >= info->hunk_count) return false;
    if (info->version <= 2U) {
        uint64_t value;
        if (!chd_map_raw(c, hunk, &raw)) return false;
        value = chd_be64(raw);
        e->offset = value & 0xFFFFFFFFFFFULL;
        e->length = (uint32_t)(value >> 44);
        e->kind = e->length == info->hunk_bytes ? CHD_E_STORED : CHD_E_COMPRESSED;
        return true;
    }
    if (info->version <= 4U) {
        uint32_t flags;
        if (!chd_map_raw(c, hunk, &raw)) return false;
        e->offset = chd_be64(raw);
        e->crc = chd_be32(raw + 8);
        e->length = chd_be16(raw + 12) | ((uint32_t)raw[14] << 16);
        flags = raw[15];
        e->crc_kind = (flags & 0x10U) ? 0U : 32U;
        switch (flags & 0x0FU) {
        case 1: e->kind = CHD_E_COMPRESSED; break;
        case 2: e->kind = CHD_E_STORED; break;
        case 3: e->kind = CHD_E_MINI; break;
        case 4: e->kind = CHD_E_SELF; break;
        case 5: e->kind = CHD_E_PARENT; break;
        default: return false; /* invalid, or the A/V second codec */
        }
        return true;
    }
    if (!info->v5_compressed_map) {
        if (!chd_map_raw(c, hunk, &raw)) return false;
        e->offset = (uint64_t)chd_be32(raw) * info->hunk_bytes;
        e->length = info->hunk_bytes;
        e->kind = e->offset == 0U ? CHD_E_ZERO : CHD_E_STORED;
        return true;
    }
    if (!chd_v5_decode_map(c)) return false;
    raw = c->map + (size_t)hunk * CHD_V5_MAP_ENTRY;
    e->length = chd_be24(raw + 1);
    e->offset = chd_be48(raw + 4);
    e->crc = chd_be16(raw + 10);
    e->crc_kind = 16U;
    if (raw[0] <= V5_COMP3) {
        e->kind = CHD_E_COMPRESSED;
        e->slot = raw[0];
    } else if (raw[0] == V5_NONE) {
        e->kind = CHD_E_STORED;
    } else if (raw[0] == V5_SELF) {
        e->kind = CHD_E_SELF;
        e->crc_kind = 0U;
    } else if (raw[0] == V5_PARENT) {
        e->kind = CHD_E_PARENT;
        e->crc_kind = 0U;
    } else {
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* FLAC (RFC 9639): the frames of a two-channel stream, no stream header    */

static uint32_t chd_crc8(const uint8_t *p, size_t size) {
    uint32_t crc = 0U, bit;
    size_t index;
    for (index = 0U; index < size; ++index) {
        crc ^= p[index];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc & 0x80U) ? ((crc << 1) ^ 0x07U) & 0xFFU : (crc << 1) & 0xFFU;
    }
    return crc;
}

static uint32_t chd_crc16_flac(const chd_ctx *c, const uint8_t *p,
                               size_t size) {
    uint32_t crc = 0U;
    size_t index;
    for (index = 0U; index < size; ++index)
        crc = ((crc << 8) ^ c->crc16_table[((crc >> 8) ^ p[index]) & 0xFFU]) &
              0xFFFFU;
    return crc;
}

static bool chd_flac_residual(chd_bits *b, int32_t *out, uint32_t block,
                              uint32_t order) {
    uint32_t method = chd_bits_read(b, 2U), param_bits, escape, partition_order,
             partitions, size, part, index = order;
    if (method > 1U) return false;
    param_bits = method == 0U ? 4U : 5U;
    escape = method == 0U ? 15U : 31U;
    partition_order = chd_bits_read(b, 4U);
    partitions = 1U << partition_order;
    size = block >> partition_order;
    if ((size << partition_order) != block || size < order) return false;
    for (part = 0U; part < partitions; ++part) {
        uint32_t count = part == 0U ? size - order : size;
        uint32_t k = chd_bits_read(b, param_bits), n;
        if (k == escape) {
            uint32_t width = chd_bits_read(b, 5U);
            for (n = 0U; n < count; ++n)
                out[index++] = width ? chd_bits_signed(b, width) : 0;
        } else {
            for (n = 0U; n < count; ++n) {
                uint32_t quotient, value;
                if (!chd_bits_unary(b, (k >= 32U) ? 0U : (0xFFFFFFFFU >> k), &quotient))
                    return false;
                value = (quotient << k) | chd_bits_read(b, k);
                out[index++] = (int32_t)((value >> 1) ^ (0U - (value & 1U)));
            }
        }
        if (chd_bits_overflow(b)) return false;
    }
    return true;
}

static bool chd_flac_subframe(chd_bits *b, int32_t *out, uint32_t block,
                              uint32_t bps) {
    uint32_t type, wasted = 0U, index;
    if (chd_bits_read(b, 1U) != 0U) return false;
    type = chd_bits_read(b, 6U);
    if (chd_bits_read(b, 1U)) {
        uint32_t zeros;
        if (!chd_bits_unary(b, 32U, &zeros)) return false;
        wasted = zeros + 1U;
        if (wasted >= bps) return false;
        bps -= wasted;
    }
    if (type == 0U) {
        int32_t value = chd_bits_signed(b, bps);
        for (index = 0U; index < block; ++index) out[index] = value;
    } else if (type == 1U) {
        for (index = 0U; index < block; ++index) out[index] = chd_bits_signed(b, bps);
    } else if (type >= 8U && type <= 12U) {
        uint32_t order = type - 8U;
        if (order > block) return false;
        for (index = 0U; index < order; ++index) out[index] = chd_bits_signed(b, bps);
        if (!chd_flac_residual(b, out, block, order)) return false;
        for (index = order; index < block; ++index) {
            int64_t r = out[index], p = 0;
            switch (order) {
            case 1: p = out[index - 1]; break;
            case 2: p = 2 * (int64_t)out[index - 1] - out[index - 2]; break;
            case 3:
                p = 3 * (int64_t)out[index - 1] - 3 * (int64_t)out[index - 2] +
                    out[index - 3];
                break;
            case 4:
                p = 4 * (int64_t)out[index - 1] - 6 * (int64_t)out[index - 2] +
                    4 * (int64_t)out[index - 3] - out[index - 4];
                break;
            default: break;
            }
            out[index] = (int32_t)(r + p);
        }
    } else if (type >= 32U) {
        uint32_t order = type - 31U, precision, j;
        int32_t shift, coefs[32];
        if (order > block) return false;
        for (index = 0U; index < order; ++index) out[index] = chd_bits_signed(b, bps);
        precision = chd_bits_read(b, 4U) + 1U;
        if (precision == 16U) return false;
        shift = chd_bits_signed(b, 5U);
        if (shift < 0) return false;
        for (j = 0U; j < order; ++j) coefs[j] = chd_bits_signed(b, precision);
        if (!chd_flac_residual(b, out, block, order)) return false;
        for (index = order; index < block; ++index) {
            int64_t sum = 0;
            for (j = 0U; j < order; ++j)
                sum += (int64_t)coefs[j] * out[index - 1U - j];
            out[index] = (int32_t)(out[index] + (sum >> shift));
        }
    } else {
        return false;
    }
    if (wasted)
        for (index = 0U; index < block; ++index)
            out[index] = (int32_t)((uint32_t)out[index] << wasted);
    return !chd_bits_overflow(b);
}

/* One frame at the (byte aligned) bit position of @p b.  Two channels are
 * left in c->flac[0..1]; the block size goes to *block. */
static bool chd_flac_frame(chd_ctx *c, chd_bits *b, uint32_t *block) {
    uint64_t start_bits = chd_bits_used(b);
    size_t start, here;
    uint32_t bs_code, sr_code, channels, ss_code, bps, blocksize, byte, index;
    int32_t *l = c->flac[0], *r = c->flac[1];
    if ((start_bits & 7U) != 0U) return false;
    start = (size_t)(start_bits >> 3);
    if (start + 6U > b->size) return false;
    if (chd_bits_read(b, 15U) != 0x7FFCU) return false; /* sync + reserved 0 */
    (void)chd_bits_read(b, 1U);                          /* blocking strategy */
    bs_code = chd_bits_read(b, 4U);
    sr_code = chd_bits_read(b, 4U);
    channels = chd_bits_read(b, 4U);
    ss_code = chd_bits_read(b, 3U);
    if (chd_bits_read(b, 1U) != 0U) return false;
    /* Frame or sample number, UTF-8 style. */
    byte = chd_bits_read(b, 8U);
    if (byte & 0x80U) {
        uint32_t extra = 0U;
        if ((byte & 0xC0U) != 0xC0U || byte == 0xFFU) return false;
        while (byte & (0x40U >> extra)) ++extra;
        for (index = 0U; index < extra; ++index)
            if ((chd_bits_read(b, 8U) & 0xC0U) != 0x80U) return false;
    }
    if (bs_code == 0U) return false;
    if (bs_code == 1U) blocksize = 192U;
    else if (bs_code <= 5U) blocksize = 576U << (bs_code - 2U);
    else if (bs_code == 6U) blocksize = chd_bits_read(b, 8U) + 1U;
    else if (bs_code == 7U) blocksize = chd_bits_read(b, 16U) + 1U;
    else blocksize = 256U << (bs_code - 8U);
    if (sr_code == 12U) (void)chd_bits_read(b, 8U);
    else if (sr_code == 13U || sr_code == 14U) (void)chd_bits_read(b, 16U);
    else if (sr_code == 15U) return false;
    here = (size_t)(chd_bits_used(b) >> 3);
    if (here + 1U > b->size || chd_bits_read(b, 8U) != chd_crc8(b->data + start, here - start))
        return false;
    switch (ss_code) {
    case 0: case 4: bps = 16U; break;
    case 1: bps = 8U; break;
    case 2: bps = 12U; break;
    case 5: bps = 20U; break;
    case 6: bps = 24U; break;
    default: return false;
    }
    if (blocksize > CHD_FLAC_MAX_BLOCK) return false;
    if (channels == 1U) {
        if (!chd_flac_subframe(b, l, blocksize, bps) ||
            !chd_flac_subframe(b, r, blocksize, bps))
            return false;
    } else if (channels == 8U) {
        if (!chd_flac_subframe(b, l, blocksize, bps) ||
            !chd_flac_subframe(b, r, blocksize, bps + 1U))
            return false;
        for (index = 0U; index < blocksize; ++index)
            r[index] = (int32_t)((int64_t)l[index] - r[index]);
    } else if (channels == 9U) {
        if (!chd_flac_subframe(b, l, blocksize, bps + 1U) ||
            !chd_flac_subframe(b, r, blocksize, bps))
            return false;
        for (index = 0U; index < blocksize; ++index)
            l[index] = (int32_t)((int64_t)l[index] + r[index]);
    } else if (channels == 10U) {
        if (!chd_flac_subframe(b, l, blocksize, bps) ||
            !chd_flac_subframe(b, r, blocksize, bps + 1U))
            return false;
        for (index = 0U; index < blocksize; ++index) {
            int64_t side = r[index];
            int64_t mid = ((int64_t)l[index] * 2) | (side & 1);
            l[index] = (int32_t)((mid + side) >> 1);
            r[index] = (int32_t)((mid - side) >> 1);
        }
    } else {
        return false;
    }
    /* Pad to a byte, then the CRC-16 of the whole frame. */
    {
        uint64_t used = chd_bits_used(b);
        if (used & 7U) chd_bits_skip(b, (uint32_t)(8U - (used & 7U)));
    }
    here = (size_t)(chd_bits_used(b) >> 3);
    if (here + 2U > b->size ||
        chd_bits_read(b, 16U) != chd_crc16_flac(c, b->data + start, here - start))
        return false;
    *block = blocksize;
    return true;
}

/* Decode @p samples stereo samples of 16 bits into @p out, big-endian when
 * @p big.  *consumed gets the end of the last frame used. */
static bool chd_flac_decode(chd_ctx *c, const uint8_t *src, size_t size,
                            uint8_t *out, uint32_t samples, bool big,
                            size_t *consumed) {
    chd_bits bits;
    uint32_t done = 0U;
    if (!c->flac[0]) {
        c->flac[0] = (int32_t *)xx_mem_alloc(CHD_FLAC_MAX_BLOCK * sizeof(int32_t));
        c->flac[1] = (int32_t *)xx_mem_alloc(CHD_FLAC_MAX_BLOCK * sizeof(int32_t));
        if (!c->flac[0] || !c->flac[1]) return false;
    }
    chd_bits_init(&bits, src, size);
    while (done < samples) {
        uint32_t block, take, index;
        if (!chd_flac_frame(c, &bits, &block)) return false;
        take = samples - done < block ? samples - done : block;
        for (index = 0U; index < take; ++index) {
            uint8_t *p = out + ((size_t)done + index) * 4U;
            uint32_t left = (uint32_t)c->flac[0][index] & 0xFFFFU;
            uint32_t right = (uint32_t)c->flac[1][index] & 0xFFFFU;
            if (big) {
                p[0] = (uint8_t)(left >> 8); p[1] = (uint8_t)left;
                p[2] = (uint8_t)(right >> 8); p[3] = (uint8_t)right;
            } else {
                p[0] = (uint8_t)left; p[1] = (uint8_t)(left >> 8);
                p[2] = (uint8_t)right; p[3] = (uint8_t)(right >> 8);
            }
        }
        done += take;
    }
    if (consumed) *consumed = (size_t)(chd_bits_used(&bits) >> 3);
    return true;
}

/* ---------------------------------------------------------------------- */
/* CD sectors: ECC regeneration (ECMA-130 Annex A)                         */
/*
 * The P parity protects 86 columns of 24 bytes (bytes 12..2075, stride 86)
 * and goes to 0x81C; the Q parity protects 52 diagonals of 43 bytes over
 * bytes 12..2247 and goes to 0x8C8.  Both are RS codes whose two check
 * symbols p0, p1 satisfy sum(v) = 0 and sum(v_i * a^(n-1-i)) = 0.
 */
static void chd_rs_pair(const chd_ctx *c, uint32_t a, uint32_t b,
                        uint8_t *p0, uint8_t *p1) {
    uint32_t s = a ^ b, v;
    /* v = s / (a + 1) with a = alpha, so divide by 3. */
    v = s ? c->gf_exp[(c->gf_log[s] + 255U - c->gf_log[3]) % 255U] : 0U;
    *p0 = (uint8_t)v;
    *p1 = (uint8_t)(a ^ v);
}

static void chd_ecc_generate(const chd_ctx *c, uint8_t *sector) {
    uint32_t major, minor;
    uint8_t header[4];
    /* Mode 2 Form 1 computes its parity with the 4 header bytes taken as
     * zero (ECMA-130 14.5); they are put back afterwards. */
    bool mode2 = sector[15] == 2U;
    if (mode2) {
        xx_rt_memcpy(header, sector + 12, sizeof(header));
        xx_rt_memset(sector + 12, 0, sizeof(header));
    }
    for (major = 0U; major < 86U; ++major) {
        uint32_t a = 0U, b = 0U;
        for (minor = 0U; minor < 24U; ++minor) {
            uint32_t v = sector[12U + major + 86U * minor];
            a ^= v;
            if (v) b ^= c->gf_exp[c->gf_log[v] + (25U - minor)];
        }
        chd_rs_pair(c, a, b, &sector[0x81CU + major], &sector[0x81CU + 86U + major]);
    }
    for (major = 0U; major < 52U; ++major) {
        uint32_t a = 0U, b = 0U, diagonal = major >> 1, half = major & 1U;
        for (minor = 0U; minor < 43U; ++minor) {
            uint32_t word = (44U * minor + 43U * diagonal) % 1118U;
            uint32_t v = sector[12U + 2U * word + half];
            a ^= v;
            if (v) b ^= c->gf_exp[c->gf_log[v] + (44U - minor)];
        }
        chd_rs_pair(c, a, b, &sector[0x8C8U + major], &sector[0x8C8U + 52U + major]);
    }
    if (mode2) xx_rt_memcpy(sector + 12, header, sizeof(header));
}

static const uint8_t chd_sync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

/* ---------------------------------------------------------------------- */
/* Hunk codecs                                                             */

static bool chd_inflate(const uint8_t *src, size_t size, uint8_t *dst,
                        size_t out) {
    size_t written = 0U;
    return size != 0U &&
           xx_deflate_decompress_memory(src, size, dst, out, &written, false) &&
           written == out;
}

static bool chd_unlzma(const uint8_t *src, size_t size, uint8_t *dst,
                       size_t out) {
    uint8_t props[5];
    uint32_t dict = 4096U;
    size_t written = 0U;
    /* The hunk codec writes no properties: lc 3, lp 0, pb 2, and any
     * dictionary as large as the hunk decodes it. */
    while (dict < out && dict < 0x40000000U) dict <<= 1;
    props[0] = 0x5DU;
    props[1] = (uint8_t)dict;
    props[2] = (uint8_t)(dict >> 8);
    props[3] = (uint8_t)(dict >> 16);
    props[4] = (uint8_t)(dict >> 24);
    return size != 0U &&
           xx_lzma_decompress_memory(src, size, props, sizeof(props),
                                     (int64_t)out, dst, out, &written) &&
           written == out;
}

static bool chd_unzstd(const uint8_t *src, size_t size, uint8_t *dst,
                       size_t out) {
    size_t written = 0U;
    return size != 0U &&
           xx_zstd_decompress_memory(src, size, dst, out, &written) &&
           written == out;
}

static bool chd_unhuff(chd_ctx *c, const uint8_t *src, size_t size,
                       uint8_t *dst, size_t out) {
    chd_huff huff;
    chd_bits bits;
    size_t index;
    if (!c->huff_table) {
        c->huff_table = (uint16_t *)xx_mem_alloc(sizeof(uint16_t) << 16);
        if (!c->huff_table) return false;
    }
    huff.codes = 256U;
    huff.max_bits = 16U;
    huff.table = c->huff_table;
    chd_bits_init(&bits, src, size);
    if (!chd_huff_import_huffman(&huff, &bits)) return false;
    for (index = 0U; index < out; ++index) {
        uint32_t symbol;
        if (!chd_huff_decode(&huff, &bits, &symbol)) return false;
        dst[index] = (uint8_t)symbol;
    }
    return !chd_bits_overflow(&bits);
}

/* CD codecs: a bitmap of frames whose sync and ECC were stripped, the size
 * of the sector-data stream (2 bytes, 3 for hunks of 64 KiB or more), the
 * sector data of all frames through the base codec, then their subcode
 * through Deflate (Zstandard for cdzs).  cdfl instead starts directly with
 * FLAC frames of the sector data, as big-endian 16-bit stereo, and follows
 * them with the Deflate subcode. */
static bool chd_uncd(chd_ctx *c, uint32_t codec, const uint8_t *src,
                     size_t size, uint8_t *dst) {
    uint32_t hunk = c->info->hunk_bytes, frames, frame;
    size_t data_bytes, sub_bytes, sub_offset;
    const uint8_t *bitmap = NULL;
    uint8_t *data = c->work;
    if (hunk % CHD_FRAME != 0U) return false;
    frames = hunk / CHD_FRAME;
    data_bytes = (size_t)frames * CHD_SECTOR;
    sub_bytes = (size_t)frames * CHD_SUBCODE;
    if (codec == CHD_CODEC_CDFL) {
        size_t used = 0U;
        if (!chd_flac_decode(c, src, size, data, (uint32_t)(data_bytes / 4U), true,
                             &used) ||
            used > size || !chd_inflate(src + used, size - used, data + data_bytes,
                                        sub_bytes))
            return false;
    } else {
        uint32_t ecc_bytes = (frames + 7U) / 8U;
        uint32_t len_bytes = hunk < 65536U ? 2U : 3U;
        size_t header = (size_t)ecc_bytes + len_bytes, base;
        bool ok;
        if (size < header) return false;
        bitmap = src;
        base = ((size_t)src[ecc_bytes] << 8) | src[ecc_bytes + 1U];
        if (len_bytes > 2U) base = (base << 8) | src[ecc_bytes + 2U];
        if (base > size - header) return false;
        if (codec == CHD_CODEC_CDLZ) ok = chd_unlzma(src + header, base, data, data_bytes);
        else if (codec == CHD_CODEC_CDZS) ok = chd_unzstd(src + header, base, data, data_bytes);
        else ok = chd_inflate(src + header, base, data, data_bytes);
        if (!ok) return false;
        sub_offset = header + base;
        if (codec == CHD_CODEC_CDZS)
            ok = chd_unzstd(src + sub_offset, size - sub_offset, data + data_bytes, sub_bytes);
        else
            ok = chd_inflate(src + sub_offset, size - sub_offset, data + data_bytes, sub_bytes);
        if (!ok) return false;
    }
    for (frame = 0U; frame < frames; ++frame) {
        uint8_t *sector = dst + (size_t)frame * CHD_FRAME;
        xx_rt_memcpy(sector, data + (size_t)frame * CHD_SECTOR, CHD_SECTOR);
        xx_rt_memcpy(sector + CHD_SECTOR, data + data_bytes + (size_t)frame * CHD_SUBCODE,
                     CHD_SUBCODE);
        if (bitmap && (bitmap[frame / 8U] & (1U << (frame % 8U))) != 0U) {
            xx_rt_memcpy(sector, chd_sync, sizeof(chd_sync));
            chd_ecc_generate(c, sector);
        }
    }
    return true;
}

static bool chd_decompress(chd_ctx *c, uint32_t codec, const uint8_t *src,
                           size_t size, uint8_t *dst) {
    uint32_t out = c->info->hunk_bytes;
    switch (codec) {
    case CHD_CODEC_ZLIB: return chd_inflate(src, size, dst, out);
    case CHD_CODEC_LZMA: return chd_unlzma(src, size, dst, out);
    case CHD_CODEC_ZSTD: return chd_unzstd(src, size, dst, out);
    case CHD_CODEC_HUFF: return chd_unhuff(c, src, size, dst, out);
    case CHD_CODEC_FLAC:
        /* 'L' or 'B': byte order of the 16-bit samples. */
        if (size < 2U || (out % 4U) != 0U || (src[0] != 'L' && src[0] != 'B'))
            return false;
        return chd_flac_decode(c, src + 1, size - 1U, dst, out / 4U,
                               src[0] == 'B', NULL);
    case CHD_CODEC_CDZL:
    case CHD_CODEC_CDLZ:
    case CHD_CODEC_CDZS:
    case CHD_CODEC_CDFL:
        return chd_uncd(c, codec, src, size, dst);
    default:
        return false;
    }
}

/* Decode hunk @p hunk into @p dst (hunk_bytes). */
static bool chd_read_hunk(chd_ctx *c, uint32_t hunk, uint8_t *dst,
                          uint32_t depth) {
    const chd_info *info = c->info;
    chd_entry e;
    uint32_t bytes = info->hunk_bytes;
    if (depth > CHD_MAX_SELF_DEPTH || !chd_get_entry(c, hunk, &e)) return false;
    switch (e.kind) {
    case CHD_E_COMPRESSED: {
        uint32_t codec = info->codecs[e.slot];
        if (e.length == 0U || e.length > bytes || codec == 0U ||
            !chd_read(info, c->device, e.offset, c->packed, e.length) ||
            !chd_decompress(c, codec, c->packed, e.length, dst))
            return false;
        break;
    }
    case CHD_E_STORED:
        if (!chd_read(info, c->device, e.offset, dst, bytes)) return false;
        break;
    case CHD_E_MINI: {
        uint32_t index;
        for (index = 0U; index < bytes; ++index)
            dst[index] = index < 8U ? (uint8_t)(e.offset >> (56U - 8U * index))
                                    : dst[index - 8U];
        break;
    }
    case CHD_E_ZERO:
        /* An unallocated hunk of a child image lives in its parent. */
        if (info->has_parent) return false;
        xx_mem_zero(dst, bytes);
        break;
    case CHD_E_SELF:
        if (e.offset >= info->hunk_count || e.offset == hunk) return false;
        return chd_read_hunk(c, (uint32_t)e.offset, dst, depth + 1U);
    default:
        return false; /* parent hunks need the parent CHD */
    }
    if (e.crc_kind == 16U) {
        if (xx_crc16_ccitt_calc(0xFFFFU, dst, bytes) != e.crc) return false;
    } else if (e.crc_kind == 32U) {
        if (xx_crc32_calc(0U, dst, bytes) != e.crc) return false;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                              */

typedef struct chd_sink_s {
    xx_io_device *device;
    uint64_t written;
} chd_sink;

static bool chd_sink_write(chd_sink *sink, const uint8_t *data, size_t size) {
    size_t done = 0U;
    if (sink->device) {
        while (done < size) {
            ssize_t wrote = xx_io_write(sink->device, data + done, size - done);
            if (wrote <= 0 || (size_t)wrote > size - done) return false;
            done += (size_t)wrote;
        }
    }
    sink->written += size;
    return true;
}

static bool chd_stopped(xx_pd_struct *pd, uint32_t hunk) {
    return (hunk & 0x3FU) == 0U && pd && xx_pd_is_stopped(pd);
}

/* Check the whole-image digest after a full pass. */
static bool chd_digest_matches(const chd_info *info, xx_hash_context *hash) {
    uint8_t digest[XX_HASH_MAX_DIGEST_SIZE];
    if (!xx_hash_final(hash, digest, sizeof(digest))) return false;
    return info->digest_size == 0U ||
           xx_rt_memcmp(digest, info->digest, info->digest_size) == 0;
}

/* The logical data: hard disk, DVD, raw. */
static bool chd_extract_data(chd_ctx *c, uint8_t *hunkbuf, chd_sink *sink,
                             xx_pd_struct *pd) {
    const chd_info *info = c->info;
    xx_hash_context hash, padded;
    uint64_t left = info->logical_bytes;
    uint32_t hunk;
    bool have_padded = false;
    if (!xx_hash_init(&hash, info->digest_size == 16U ? XX_HASH_MD5 : XX_HASH_SHA1))
        return false;
    for (hunk = 0U; hunk < info->hunk_count && left != 0U; ++hunk) {
        size_t take = left < info->hunk_bytes ? (size_t)left : info->hunk_bytes;
        if (chd_stopped(pd, hunk) || !chd_read_hunk(c, hunk, hunkbuf, 0U))
            return false;
        xx_hash_update(&hash, hunkbuf, take);
        if (take < info->hunk_bytes && info->version <= 2U) {
            /* No v1/v2 writer is at hand to show whether their MD5 stops
             * at the logical size or covers the padded last hunk; accept
             * either. */
            padded = hash;
            xx_hash_update(&padded, hunkbuf + take, info->hunk_bytes - take);
            have_padded = true;
        }
        if (!chd_sink_write(sink, hunkbuf, take)) return false;
        left -= take;
    }
    if (left != 0U) return false;
    return chd_digest_matches(info, &hash) ||
           (have_padded && chd_digest_matches(info, &padded));
}

/* Write the part of one frame that belongs in the output (swapping 16-bit
 * audio samples back to little-endian where chdman does). */
static bool chd_emit_frame(const chd_track *track, bool swap, uint8_t *frame,
                           chd_sink *sink) {
    if (swap && track->type == TRK_AUDIO) {
        uint32_t index;
        for (index = 0U; index + 1U < track->datasize; index += 2U) {
            uint8_t t = frame[index];
            frame[index] = frame[index + 1U];
            frame[index + 1U] = t;
        }
    }
    return chd_sink_write(sink, frame, track->datasize);
}

/* The single cue/bin image: every track's frames in order, all hunks read
 * so the raw-data digest can be checked. */
static bool chd_extract_bin(chd_ctx *c, uint8_t *hunkbuf, chd_sink *sink,
                            xx_pd_struct *pd) {
    const chd_info *info = c->info;
    xx_hash_context hash;
    uint32_t per_hunk = info->hunk_bytes / CHD_FRAME, hunk, track = 0U;
    uint64_t frame = 0U, left = info->logical_bytes;
    if (!xx_hash_init(&hash, info->digest_size == 16U ? XX_HASH_MD5 : XX_HASH_SHA1))
        return false;
    for (hunk = 0U; hunk < info->hunk_count; ++hunk) {
        uint32_t index;
        size_t take = left < info->hunk_bytes ? (size_t)left : info->hunk_bytes;
        if (chd_stopped(pd, hunk) || !chd_read_hunk(c, hunk, hunkbuf, 0U))
            return false;
        xx_hash_update(&hash, hunkbuf, take);
        left -= take;
        for (index = 0U; index < per_hunk; ++index, ++frame) {
            const chd_track *t;
            while (track < info->track_count &&
                   frame >= info->tracks[track].chdframeofs +
                                info->tracks[track].frames +
                                info->tracks[track].extraframes)
                ++track;
            if (track >= info->track_count) break;
            t = &info->tracks[track];
            if (frame >= t->chdframeofs &&
                frame < t->chdframeofs + t->frames - t->padframes &&
                !chd_emit_frame(t, true, hunkbuf + (size_t)index * CHD_FRAME, sink))
                return false;
        }
    }
    return chd_digest_matches(info, &hash);
}

/* One GD-ROM track file. */
static bool chd_extract_track(chd_ctx *c, uint8_t *hunkbuf, uint32_t number,
                              chd_sink *sink, xx_pd_struct *pd) {
    const chd_info *info = c->info;
    const chd_track *t = &info->tracks[number];
    uint32_t per_hunk = info->hunk_bytes / CHD_FRAME;
    uint64_t first = t->chdframeofs, end = first + t->frames - t->padframes,
             frame = first;
    uint32_t loaded = 0xFFFFFFFFU;
    /* chdman writes GD-ROM audio byte-swapped for 'CHGD' tracks of v5
     * images and for 'CHGT' tracks of v3/v4 images, and as stored in the
     * other two cases (checked against chdman 0.289 on all four). */
    bool swap = (info->version > 4U) != info->gd_old_tag;
    while (frame < end) {
        uint32_t hunk = (uint32_t)(frame / per_hunk);
        uint32_t index = (uint32_t)(frame % per_hunk);
        if (hunk != loaded) {
            if (chd_stopped(pd, hunk) || !chd_read_hunk(c, hunk, hunkbuf, 0U))
                return false;
            loaded = hunk;
        }
        if (!chd_emit_frame(t, swap, hunkbuf + (size_t)index * CHD_FRAME, sink))
            return false;
        ++frame;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

typedef struct chd_stream_s {
    chd_info info;
    size_t index;
    chd_ctx *ctx;
    uint8_t *hunkbuf;
} chd_stream;

static void chd_stream_free(void *opaque) {
    chd_stream *stream = (chd_stream *)opaque;
    if (!stream) return;
    chd_ctx_free(stream->ctx);
    if (stream->hunkbuf) xx_mem_free(stream->hunkbuf);
    xx_mem_free(stream);
}

static bool chd_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *chd_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool chd_set_record(xx_archive_record *record, const chd_info *info,
                           size_t index) {
    const chd_member *member = &info->members[index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = 0;
    record->header_size = info->header_len;
    record->data_offset = -1;
    record->compressed_size =
        (member->kind == CHD_M_CUE || member->kind == CHD_M_GDI)
            ? (int64_t)member->size : info->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          info->codecs[0]) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Produce member @p stream->index into @p destination (NULL: decode and
 * verify only). */
static bool chd_unpack_member(Abstractformat *format, chd_stream *stream,
                              xx_io_device *destination, xx_pd_struct *pd) {
    const chd_info *info = &stream->info;
    const chd_member *member = &info->members[stream->index];
    chd_sink sink;
    bool ok;
    sink.device = destination;
    sink.written = 0U;
    if (member->kind == CHD_M_CUE || member->kind == CHD_M_GDI) {
        char *sheet = (char *)xx_mem_alloc(CHD_SHEET_MAX);
        size_t used;
        if (!sheet) return false;
        used = chd_build_sheet(info, sheet, CHD_SHEET_MAX);
        ok = used == member->size &&
             chd_sink_write(&sink, (const uint8_t *)sheet, used);
        xx_mem_free(sheet);
        return ok;
    }
    if (!stream->ctx) {
        stream->ctx = chd_ctx_create(info, format->device);
        stream->hunkbuf = (uint8_t *)xx_mem_alloc(info->hunk_bytes);
        if (!stream->ctx || !stream->hunkbuf) return false;
    }
    stream->ctx->device = format->device;
    if (member->kind == CHD_M_BIN)
        ok = chd_extract_bin(stream->ctx, stream->hunkbuf, &sink, pd);
    else if (member->kind == CHD_M_TRACK)
        ok = chd_extract_track(stream->ctx, stream->hunkbuf, member->track, &sink, pd);
    else
        ok = chd_extract_data(stream->ctx, stream->hunkbuf, &sink, pd);
    return ok && sink.written == member->size;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_chd_init(xx_chd *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_CHD_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-mame-chd");
    xx_format_set_extension(&archive->format, "chd");
    archive->format.check_is_valid = xx_chd_check_is_valid;
    archive->format.handle_base_info = xx_chd_handle_base_info;
    archive->format.get_format_size = xx_chd_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_chd_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_chd_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_chd_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_chd_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_chd_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_chd_free_archive_records_reading;
}

xx_chd *xx_chd_create(xx_io_device *device, int64_t base_address) {
    xx_chd *archive = (xx_chd *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_chd_init(archive, device, base_address);
    return archive;
}

void xx_chd_destroy(xx_chd *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_chd_free(xx_chd *archive) {
    if (!archive) return;
    xx_chd_destroy(archive);
    xx_mem_free(archive);
}

bool xx_chd_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    chd_info *info;
    bool ok;
    (void)pd;
    if (!format) return false;
    info = (chd_info *)xx_mem_alloc(sizeof(*info));
    if (!info) return false;
    ok = chd_parse_header(format->device, format->base_address, info);
    xx_mem_free(info);
    return ok;
}

bool xx_chd_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    chd_info *info;
    xx_chd *archive;
    char version[8];
    (void)pd;
    if (!format) return false;
    info = (chd_info *)xx_mem_alloc(sizeof(*info));
    if (!info) return false;
    if (!chd_parse(format->device, format->base_address, info)) {
        xx_mem_free(info);
        return false;
    }
    archive = (xx_chd *)format;
    archive->version = info->version;
    archive->hunk_bytes = info->hunk_bytes;
    archive->unit_bytes = info->unit_bytes;
    archive->hunk_count = info->hunk_count;
    archive->logical_bytes = info->logical_bytes;
    xx_rt_memcpy(archive->codecs, info->codecs, sizeof(archive->codecs));
    archive->kind = info->kind;
    archive->track_count = info->track_count;
    archive->number_of_records = info->member_count;
    archive->has_parent = info->has_parent;
    (void)xx_rt_snprintf(version, sizeof(version), "%u", (unsigned)info->version);
    xx_format_set_version(format, version);
    format->number_of_archive_records = info->member_count;
    format->format_size = info->size;
    format->is_valid = true;
    format->base_info_handled = true;
    xx_mem_free(info);
    return true;
}

int64_t xx_chd_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_chd_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_chd_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_chd_handle_base_info(format, pd))
               ? ((xx_chd *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_chd_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    chd_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!format) return NULL;
    stream = (chd_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!chd_parse(format->device, format->base_address, &stream->info) ||
        stream->info.member_count == 0U) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = chd_stream_free;
    state->total_records = (int64_t)stream->info.member_count;
    if (!chd_copy_options(&state->options, options) ||
        !chd_set_record(&state->current_record, &stream->info, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_chd_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_chd_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    chd_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (chd_stream *)state->internal_state) ||
        stream->index + 1U >= stream->info.member_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    state->current_index = (int64_t)stream->index;
    if (!chd_set_record(&state->current_record, &stream->info, stream->index)) {
        state->has_record = false;
        return false;
    }
    return true;
}

bool xx_chd_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    chd_stream *stream;
    const xx_var *path_option;
    const char *base = NULL, *name;
    char *owned_base = NULL, *path = NULL;
    bool result = false, created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (chd_stream *)state->internal_state) ||
        stream->index >= stream->info.member_count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = chd_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return chd_unpack_member(format, stream, NULL, pd);
    /* Member names are the reader's own constants (disk.img, disc.cue,
     * disc07.bin, ...), never taken from the file. */
    name = stream->info.members[stream->index].name;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", name)
               : xx_str_concat(base, name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = chd_unpack_member(format, stream, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_chd_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
