/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ARDI self-extracting installer (Daniel F Valot, OS/2).  xx_ardi_installer.h
 * carries the layout.
 *
 * The container is located from the end of the file, the way the installer
 * stub itself finds it: the fixed trailer, then a backward walk over
 * length-suffixed blocks to the 0x98765432 sentinel, which must sit where
 * the LX image ends.  The executable is parsed only as far as its MZ
 * header, LX header and the tail of the object page table; no code is run
 * or emulated.
 *
 * Reference: XArchive sfx/xardi2sfx.cpp (MIT, Copyright (c) 2026
 * hors<horsicq@gmail.com>) - the tag set, the block size limits for member
 * headers and the backward walk follow it.  This is an independent C
 * implementation; the member naming, the name safety rules and the
 * extraction are this library's own.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ardi_installer/xx_ardi_installer.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder: xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * the real file type is picked up as soon as the format is registered. */
#ifdef ARDI_INSTALLER
#define XX_ARDI_INSTALLER_FILE_TYPE XX_FILE_TYPE_ARDI_INSTALLER
#else
#define XX_ARDI_INSTALLER_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define ARDI_TRAILER_SIZE 50
#define ARDI_TRAILER_HEAD "Copyright Daniel F Valot "
#define ARDI_TRAILER_HEAD_SIZE 25U
#define ARDI_TRAILER_TAIL "TSHTSH - 1991-"
#define ARDI_TRAILER_TAIL_OFFSET 31U
#define ARDI_TRAILER_TAIL_SIZE 14U
#define ARDI_TRAILER_YEAR_OFFSET 45U

#define ARDI_SENTINEL UINT32_C(0x98765432)
#define ARDI_TAG_HEADER UINT32_C(0x12345677)
#define ARDI_TAG_DATA UINT32_C(0x12345678)
#define ARDI_TAG_PATH UINT32_C(0x11221122)
#define ARDI_TAG_TITLE UINT32_C(0x97979797)
#define ARDI_TAG_BLURB UINT32_C(0x12121212)
#define ARDI_TAG_LICENCE UINT32_C(0x13131313)
#define ARDI_TAG_OPAQUE UINT32_C(0x98989898)

/* A block is its tag, its payload and its length word. */
#define ARDI_BLOCK_MIN 8U
/* Member header block: tag, time, at least "X!" - and at most the 0x1008
 * bytes of tag and payload the reference accepts, plus the length word. */
#define ARDI_HEADER_MIN 14U
#define ARDI_HEADER_MAX 0x100CU
/* The chain lies behind at least an MZ header. */
#define ARDI_STUB_MIN 0x40
/* LX header bytes read (through the data pages offset at +0x80), how many
 * object page table entries from the end are measured, and the page count
 * accepted. */
#define ARDI_LX_HEADER 0x84U
#define ARDI_LX_PAGES 64U
#define ARDI_LX_MAX_PAGES 0x100000U
/* The walk and the member table are capped; the known carriers hold at most
 * 70 blocks. */
#define ARDI_MAX_BLOCKS 0x20000U
#define ARDI_MAX_MEMBERS 0x8000U
/* Validity and base info check only this many blocks from the end (all of
 * a known carrier's chain); the rest is walked when the records are read.
 * This keeps a candidate's cost bounded when a format search opens many
 * candidates that all lead to one long chain. */
#define ARDI_QUICK_BLOCKS 128U
/* Stored names (raw bytes) and the text blocks worth publishing. */
#define ARDI_NAME_MAX 255U
#define ARDI_TEXT_MAX 255U
#define ARDI_DEDUP_TRIES 32U
#define ARDI_SSIZE_LIMIT (((size_t)-1) >> 1U)
/* Member time stamps before 1990-01-01 are not the tool's. */
#define ARDI_TIME_MIN UINT32_C(631152000)
#define ARDI_TIME_MAX UINT32_C(0x7FFFFFFF)

typedef struct ardi_pair_s {
    int64_t header;       /* tag of the header block, from base */
    uint32_t header_size; /* whole header block, length word included */
    int64_t data;         /* tag of the data block, from base */
    int64_t data_size;    /* whole data block, length word included */
} ardi_pair;

typedef struct ardi_layout_s {
    int64_t size;         /* bytes from base to the device end */
    int64_t sentinel;     /* from base */
    int64_t path;         /* tag of the destination block, -1 if none */
    int64_t path_size;
    int64_t title;        /* tag of the title block, -1 if none */
    int64_t title_size;
    uint32_t blocks;
    uint32_t pairs;
    bool complete;        /* walked to the sentinel; else only the last
                             ARDI_QUICK_BLOCKS blocks were checked */
    char year[5];
} ardi_layout;

typedef struct ardi_member_s {
    char *name;           /* published UTF-8 name */
    ardi_pair pair;
    uint32_t time;
    bool extractable;
} ardi_member;

typedef struct ardi_stream_s {
    ardi_layout layout;
    ardi_member *members;
    size_t count;
    size_t index;
    uint32_t *slots;      /* published names, case folded: index + 1 */
    size_t mask;
    uint64_t max_member;  /* UINT64_MAX when unset */
} ardi_stream;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */

static bool ardi_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* OS/2 stores 8-bit names in the system code page; code page 850 is taken,
 * the default for the western European installs these carriers target. */
static const uint16_t ardi_cp850[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00F8, 0x00A3, 0x00D8, 0x00D7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x00AE, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x00C1, 0x00C2, 0x00C0,
    0x00A9, 0x2563, 0x2551, 0x2557, 0x255D, 0x00A2, 0x00A5, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x00E3, 0x00C3,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x00A4,
    0x00F0, 0x00D0, 0x00CA, 0x00CB, 0x00C8, 0x0131, 0x00CD, 0x00CE,
    0x00CF, 0x2518, 0x250C, 0x2588, 0x2584, 0x00A6, 0x00CC, 0x2580,
    0x00D3, 0x00DF, 0x00D4, 0x00D2, 0x00F5, 0x00D5, 0x00B5, 0x00FE,
    0x00DE, 0x00DA, 0x00DB, 0x00D9, 0x00FD, 0x00DD, 0x00AF, 0x00B4,
    0x00AD, 0x00B1, 0x2017, 0x00BE, 0x00B6, 0x00A7, 0x00F7, 0x00B8,
    0x00B0, 0x00A8, 0x00B7, 0x00B9, 0x00B3, 0x00B2, 0x25A0, 0x00A0};

/* @p length code page 850 bytes as UTF-8 into @p out (NUL terminated).
 * Fails when a byte is a control character or the text does not fit. */
static bool ardi_decode(const uint8_t *raw, size_t length, char *out,
                        size_t capacity) {
    size_t used = 0U, index;
    if (!raw || !out || capacity == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint32_t c = raw[index];
        if (c < 0x20U || c == 0x7FU) return false;
        if (c >= 0x80U) c = ardi_cp850[c - 0x80U];
        if (c < 0x80U) {
            if (capacity - used < 2U) return false;
            out[used++] = (char)c;
        } else if (c < 0x800U) {
            if (capacity - used < 3U) return false;
            out[used++] = (char)(0xC0U | (c >> 6U));
            out[used++] = (char)(0x80U | (c & 0x3FU));
        } else {
            if (capacity - used < 4U) return false;
            out[used++] = (char)(0xE0U | (c >> 12U));
            out[used++] = (char)(0x80U | ((c >> 6U) & 0x3FU));
            out[used++] = (char)(0x80U | (c & 0x3FU));
        }
    }
    out[used] = 0;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Locating the chain                                                      */

static bool ardi_trailer(Abstractformat *format, int64_t size, char *year) {
    uint8_t trailer[ARDI_TRAILER_SIZE];
    unsigned index;
    if (size < ARDI_STUB_MIN + 4 + 2 * (int64_t)ARDI_BLOCK_MIN +
                   ARDI_TRAILER_SIZE ||
        !ardi_read_at(format->device,
                      format->base_address + size - ARDI_TRAILER_SIZE, trailer,
                      sizeof(trailer)) ||
        xx_rt_memcmp(trailer, ARDI_TRAILER_HEAD, ARDI_TRAILER_HEAD_SIZE) != 0 ||
        xx_rt_memcmp(trailer + ARDI_TRAILER_TAIL_OFFSET, ARDI_TRAILER_TAIL,
                     ARDI_TRAILER_TAIL_SIZE) != 0 ||
        trailer[ARDI_TRAILER_SIZE - 1] != ' ')
        return false;
    for (index = 0U; index < 4U; ++index) {
        uint8_t digit = trailer[ARDI_TRAILER_YEAR_OFFSET + index];
        if (digit < '0' || digit > '9') return false;
        year[index] = (char)digit;
    }
    year[4] = 0;
    return true;
}

/* The end of the LX image behind the MZ header at base, from base: the
 * largest "data pages offset + (page offset << shift) + page size" over the
 * last ARDI_LX_PAGES entries of the object page table.  The installer's
 * chain starts right there.  A constant number of small reads, whatever
 * the header says, so every MZ candidate of a format search is cheap. */
static bool ardi_image_end(Abstractformat *format, int64_t size,
                           int64_t *image_end) {
    uint8_t word[4];
    uint8_t lx[ARDI_LX_HEADER];
    uint8_t table[ARDI_LX_PAGES * 8U];
    int64_t lfa, table_offset, data_pages, end = -1;
    uint32_t pages, shift, count, index;
    if (!ardi_read_at(format->device, format->base_address + 0x3C, word, 4U))
        return false;
    lfa = (int64_t)xx_data_get_u32(word, 4, 0, false);
    if (lfa < ARDI_STUB_MIN || lfa > size - (int64_t)ARDI_LX_HEADER ||
        !ardi_read_at(format->device, format->base_address + lfa, lx,
                      sizeof(lx)) ||
        lx[0] != 'L' || lx[1] != 'X' || lx[2] != 0U || lx[3] != 0U)
        return false;
    pages = xx_data_get_u32(lx + 0x14, 4, 0, false);
    shift = xx_data_get_u32(lx + 0x2C, 4, 0, false);
    table_offset = lfa + (int64_t)xx_data_get_u32(lx + 0x48, 4, 0, false);
    data_pages = (int64_t)xx_data_get_u32(lx + 0x80, 4, 0, false);
    if (pages == 0U || pages > ARDI_LX_MAX_PAGES || shift > 15U) return false;
    count = pages < ARDI_LX_PAGES ? pages : ARDI_LX_PAGES;
    table_offset += (int64_t)(pages - count) * 8;
    if (table_offset < ARDI_STUB_MIN ||
        table_offset > size - (int64_t)count * 8 ||
        !ardi_read_at(format->device, format->base_address + table_offset,
                      table, (size_t)count * 8U))
        return false;
    for (index = 0U; index < count; ++index) {
        const uint8_t *entry = table + (size_t)index * 8U;
        int64_t page_end = data_pages +
                           ((int64_t)xx_data_get_u32(entry, 4, 0, false) << shift) +
                           (int64_t)((uint32_t)entry[4] |
                                     ((uint32_t)entry[5] << 8U));
        if (page_end > end) end = page_end;
    }
    /* The image holds its own LX header, and the chain and the trailer
     * follow it. */
    if (end < lfa + (int64_t)ARDI_LX_HEADER ||
        end > size - (4 + 2 * (int64_t)ARDI_BLOCK_MIN + ARDI_TRAILER_SIZE))
        return false;
    *image_end = end;
    return true;
}

/* Reads the header block at @p offset (from base) into @p block and returns
 * the text length and the position of the first '!' in it.  The name is
 * the text before that '!', which must not be empty. */
static bool ardi_read_header(Abstractformat *format, int64_t offset,
                             uint32_t size, uint8_t *block, size_t *text,
                             size_t *bang) {
    size_t index, length;
    if (size < ARDI_HEADER_MIN || size > ARDI_HEADER_MAX ||
        !ardi_read_at(format->device, format->base_address + offset, block,
                      size - 4U) ||
        xx_data_get_u32(block, 4, 0, false) != ARDI_TAG_HEADER)
        return false;
    length = size - 12U;
    for (index = 0U; index < length; ++index) {
        if (block[8U + index] == '!') break;
    }
    if (index == 0U || index >= length) return false;
    *text = length;
    *bang = index;
    return true;
}

/* A raw deflate stream starts with a block whose type is not reserved. */
static bool ardi_data_start(Abstractformat *format, int64_t offset,
                            int64_t size) {
    uint8_t first;
    return size > (int64_t)ARDI_BLOCK_MIN &&
           ardi_read_at(format->device, format->base_address + offset + 4,
                        &first, 1U) &&
           ((first >> 1U) & 3U) != 3U;
}

/* Walks the chain from the trailer back to the sentinel.  With @p pairs
 * NULL only the layout is measured; otherwise up to @p capacity member
 * pairs are stored, last member first.  Every step moves strictly
 * backwards by at least one block, so the walk ends.  With @p quick the
 * walk stops after ARDI_QUICK_BLOCKS good blocks and reports an incomplete
 * layout (sentinel known, counts not). */
static bool ardi_walk(Abstractformat *format, ardi_layout *layout,
                      ardi_pair *pairs, size_t capacity, bool quick,
                      xx_pd_struct *pd) {
    uint8_t *header = NULL;
    ardi_layout found;
    ardi_pair pending;
    bool have_data = false, result = false;
    int64_t position, total, image_end = 0;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    xx_mem_zero(&found, sizeof(found));
    xx_mem_zero(&pending, sizeof(pending));
    found.size = total - format->base_address;
    found.path = -1;
    found.title = -1;
    {
        uint8_t mz[2];
        if (found.size < ARDI_STUB_MIN ||
            !ardi_read_at(format->device, format->base_address, mz, 2U) ||
            mz[0] != 'M' || mz[1] != 'Z')
            return false;
    }
    if (!ardi_trailer(format, found.size, found.year)) return false;
    /* The sentinel must be where this candidate's own LX image ends; only
     * then is the chain walked, and never below that point. */
    {
        uint8_t word[4];
        if (!ardi_image_end(format, found.size, &image_end) ||
            !ardi_read_at(format->device, format->base_address + image_end,
                          word, 4U) ||
            xx_data_get_u32(word, 4, 0, false) != ARDI_SENTINEL)
            return false;
    }
    header = (uint8_t *)xx_mem_alloc(ARDI_HEADER_MAX);
    if (!header) return false;
    position = found.size - ARDI_TRAILER_SIZE - 4;
    found.sentinel = image_end;
    for (;;) {
        uint8_t word[4];
        uint32_t length, tag;
        int64_t tag_offset;
        if (quick && found.blocks >= ARDI_QUICK_BLOCKS) {
            *layout = found;
            result = true;
            goto done;
        }
        if (found.blocks > ARDI_MAX_BLOCKS ||
            ((found.blocks & 0xFFU) == 0U && pd && xx_pd_is_stopped(pd)) ||
            position < image_end ||
            !ardi_read_at(format->device, format->base_address + position,
                          word, 4U))
            goto done;
        length = xx_data_get_u32(word, 4, 0, false);
        if (length == ARDI_SENTINEL) {
            if (position != image_end) goto done;
            break;
        }
        /* The tag must lie behind the sentinel's own four bytes. */
        if (length < ARDI_BLOCK_MIN ||
            (int64_t)length > position + 4 - (image_end + 4))
            goto done;
        tag_offset = position + 4 - (int64_t)length;
        if (!ardi_read_at(format->device, format->base_address + tag_offset,
                          word, 4U))
            goto done;
        tag = xx_data_get_u32(word, 4, 0, false);
        ++found.blocks;
        if (tag == ARDI_TAG_DATA) {
            /* Two data blocks in a row: one of them has no header. */
            if (have_data || !ardi_data_start(format, tag_offset, length))
                goto done;
            pending.data = tag_offset;
            pending.data_size = length;
            have_data = true;
        } else if (tag == ARDI_TAG_HEADER) {
            size_t text, bang;
            if (!have_data || found.pairs >= ARDI_MAX_MEMBERS ||
                !ardi_read_header(format, tag_offset, length, header, &text,
                                  &bang))
                goto done;
            pending.header = tag_offset;
            pending.header_size = length;
            if (pairs) {
                if (found.pairs >= capacity) goto done;
                pairs[found.pairs] = pending;
            }
            ++found.pairs;
            have_data = false;
        } else if (tag == ARDI_TAG_PATH) {
            /* The first one in file order is kept. */
            found.path = tag_offset;
            found.path_size = length;
        } else if (tag == ARDI_TAG_TITLE) {
            found.title = tag_offset;
            found.title_size = length;
        } else if (tag != ARDI_TAG_BLURB && tag != ARDI_TAG_LICENCE &&
                   tag != ARDI_TAG_OPAQUE) {
            /* A block this reader does not know: its length cannot be
             * trusted to lead anywhere, so the file is not understood. */
            goto done;
        }
        position = tag_offset - 4;
    }
    if (have_data || found.pairs == 0U) goto done;
    found.complete = true;
    *layout = found;
    result = true;
done:
    xx_mem_free(header);
    return result;
}

/* The NUL-terminated text of a title or destination block, decoded. */
static void ardi_block_text(Abstractformat *format, int64_t offset,
                            int64_t size, char *out, size_t capacity) {
    uint8_t text[ARDI_TEXT_MAX + 1U];
    size_t length, index;
    out[0] = 0;
    if (offset < 0 || size <= (int64_t)ARDI_BLOCK_MIN) return;
    length = size - ARDI_BLOCK_MIN > (int64_t)sizeof(text)
                 ? sizeof(text)
                 : (size_t)(size - ARDI_BLOCK_MIN);
    if (!ardi_read_at(format->device, format->base_address + offset + 4, text,
                      length))
        return;
    for (index = 0U; index < length && text[index] != 0U; ++index) {
    }
    if (index > ARDI_TEXT_MAX || !ardi_decode(text, index, out, capacity))
        out[0] = 0;
}

/* ---------------------------------------------------------------------- */
/* Member names                                                            */

static char ardi_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* One path component that is safe to create: no separators, drive colons,
 * reserved punctuation or control characters, no trailing dot or space
 * (which also rules out "." and ".."), and no Windows device name in any
 * case, with or without an extension. */
static bool ardi_safe_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t length, index, stem = 0U, device;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    for (index = 0U; index < length; ++index) {
        unsigned char c = (unsigned char)name[index];
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*')
            return false;
    }
    if (name[length - 1U] == '.' || name[length - 1U] == ' ') return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (device = 0U; device < sizeof(devices) / sizeof(devices[0]);
         ++device) {
        const char *word = devices[device];
        size_t k = 0U;
        while (k < stem && word[k] && ardi_upper(name[k]) == word[k]) ++k;
        if (k == stem && word[k] == 0) return false;
    }
    if (stem >= 4U &&
        ((ardi_upper(name[0]) == 'C' && ardi_upper(name[1]) == 'O' &&
          ardi_upper(name[2]) == 'M') ||
         (ardi_upper(name[0]) == 'L' && ardi_upper(name[1]) == 'P' &&
          ardi_upper(name[2]) == 'T'))) {
        /* COM0..COM9, LPT0..LPT9 and the superscript digit forms (U+00B9,
         * U+00B2, U+00B3: C2 B9 / C2 B2 / C2 B3). */
        if (stem == 4U && name[3] >= '0' && name[3] <= '9') return false;
        if (stem == 5U && (unsigned char)name[3] == 0xC2U &&
            ((unsigned char)name[4] == 0xB9U ||
             (unsigned char)name[4] == 0xB2U ||
             (unsigned char)name[4] == 0xB3U))
            return false;
    }
    return true;
}

/* Byte @p index of a published name folded the way Windows compares file
 * names: ASCII letters, and the Latin-1 letters U+00E0..U+00FE (without
 * U+00F7), C3 A0..C3 BE in UTF-8, to upper case. */
static uint8_t ardi_fold(const char *name, size_t index) {
    uint8_t c = (uint8_t)name[index];
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 'a' + 'A');
    if (index > 0U && (uint8_t)name[index - 1U] == 0xC3U && c >= 0xA0U &&
        c <= 0xBEU && c != 0xB7U)
        return (uint8_t)(c - 0x20U);
    return c;
}

static uint32_t ardi_hash(const char *name) {
    uint32_t hash = UINT32_C(2166136261);
    size_t index;
    for (index = 0U; name[index]; ++index) {
        hash ^= ardi_fold(name, index);
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static bool ardi_same_name(const char *left, const char *right) {
    size_t index;
    for (index = 0U; left[index] && right[index]; ++index)
        if (ardi_fold(left, index) != ardi_fold(right, index)) return false;
    return left[index] == right[index];
}

static bool ardi_name_taken(const ardi_stream *stream, const char *name) {
    size_t slot = ardi_hash(name) & stream->mask, probes;
    for (probes = 0U; probes <= stream->mask; ++probes) {
        uint32_t value = stream->slots[slot];
        if (value == 0U) return false;
        if (ardi_same_name(stream->members[value - 1U].name, name))
            return true;
        slot = (slot + 1U) & stream->mask;
    }
    return true;
}

static void ardi_name_insert(ardi_stream *stream, size_t index) {
    size_t slot = ardi_hash(stream->members[index].name) & stream->mask,
           probes;
    for (probes = 0U; probes <= stream->mask; ++probes) {
        if (stream->slots[slot] == 0U) {
            stream->slots[slot] = (uint32_t)(index + 1U);
            return;
        }
        slot = (slot + 1U) & stream->mask;
    }
}

static char *ardi_copy(const char *text) {
    size_t length = xx_str_len(text);
    char *copy = (char *)xx_mem_alloc(length + 1U);
    if (copy) xx_rt_memcpy(copy, text, length + 1U);
    return copy;
}

/* UTF-16 units (what the Windows 255-unit component limit counts) in the
 * first @p size bytes of UTF-8 @p text; every code page 850 character is in
 * the BMP, so it is one unit per lead byte. */
static size_t ardi_units(const char *text, size_t size) {
    size_t index, units = 0U;
    for (index = 0U; index < size; ++index)
        if (((uint8_t)text[index] & 0xC0U) != 0x80U) ++units;
    return units;
}

/* Bytes of the longest prefix of @p text (@p size bytes) that ends on a
 * character boundary and holds at most @p units characters. */
static size_t ardi_cut(const char *text, size_t size, size_t units) {
    size_t index, seen = 0U;
    for (index = 0U; index < size; ++index) {
        if (((uint8_t)text[index] & 0xC0U) != 0x80U) {
            if (seen == units) return index;
            ++seen;
        }
    }
    return size;
}

/* A name of the 8.3 alias form Windows generates for long names ("LONGFI~1
 * .TXT", "LO1F2A~1"): a stem of at most 8 characters without dots holding
 * '~' and then only digits, and an extension of at most 3.  On a volume
 * with short names such a member would open an earlier long-named member
 * under its alias and overwrite it. */
static bool ardi_short_alias(const char *name) {
    size_t length = xx_str_len(name), dot = length, index, tilde = 0U;
    bool found = false;
    while (dot > 0U && name[dot - 1U] != '.') --dot;
    dot = dot > 0U ? dot - 1U : length;
    if (dot == 0U || dot > 8U || (dot < length && length - dot - 1U > 3U))
        return false;
    for (index = 0U; index < dot; ++index) {
        if (name[index] == '.') return false;
        if (name[index] == '~') {
            tilde = index;
            found = true;
        }
    }
    if (!found || tilde + 1U >= dot) return false;
    for (index = tilde + 1U; index < dot; ++index)
        if (name[index] < '0' || name[index] > '9') return false;
    return true;
}

/* Gives member @p index a unique, safe name.  A stored name that is not a
 * safe file name becomes "file_NNNN"; an 8.3 alias form has its '~' turned
 * into '_'; a name already published gets "_NNNN" (and a further counter
 * if needed) in front of its extension, the stem shortened so the result
 * stays within 255 characters, so no member overwrites another.  NNNN is
 * the member's 1-based position. */
static bool ardi_publish_name(ardi_stream *stream, size_t index,
                              const char *stored) {
    ardi_member *member = &stream->members[index];
    char base[ARDI_NAME_MAX * 3U + 1U];
    char candidate[sizeof(base) + 32U];
    size_t length, dot, attempt;
    unsigned ordinal = (unsigned)(index + 1U);
    if (stored && ardi_safe_name(stored)) {
        size_t size = xx_str_len(stored), k;
        xx_rt_memcpy(base, stored, size + 1U);
        if (ardi_short_alias(base))
            for (k = 0U; k < size; ++k)
                if (base[k] == '~') base[k] = '_';
    } else {
        (void)xx_rt_snprintf(base, sizeof(base), "file_%04u", ordinal);
    }
    member->extractable = false;
    for (attempt = 0U; attempt <= ARDI_DEDUP_TRIES; ++attempt) {
        if (attempt == 0U) {
            xx_rt_memcpy(candidate, base, xx_str_len(base) + 1U);
        } else {
            char suffix[32];
            size_t suffix_size, extension, stem;
            if (attempt == 1U)
                (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%04u", ordinal);
            else
                (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%04u_%u",
                                     ordinal, (unsigned)attempt);
            suffix_size = xx_str_len(suffix);
            length = xx_str_len(base);
            dot = length;
            while (dot > 0U && base[dot - 1U] != '.') --dot;
            dot = dot > 1U ? dot - 1U : length;
            extension = ardi_units(base + dot, length - dot);
            if (extension + suffix_size >= ARDI_NAME_MAX) {
                dot = length;
                extension = 0U;
            }
            stem = ardi_cut(base, dot,
                            ARDI_NAME_MAX - suffix_size - extension);
            (void)xx_rt_snprintf(candidate, sizeof(candidate), "%.*s%s%s",
                                 (int)stem, base, suffix, base + dot);
        }
        if (!ardi_name_taken(stream, candidate)) {
            member->extractable = true;
            break;
        }
    }
    /* A name that stays taken is listed under its base name, never
     * written. */
    member->name = ardi_copy(member->extractable ? candidate : base);
    if (!member->name) return false;
    if (member->extractable) ardi_name_insert(stream, index);
    return true;
}

static void ardi_stream_free(void *opaque) {
    ardi_stream *stream = (ardi_stream *)opaque;
    size_t index;
    if (!stream) return;
    if (stream->members) {
        for (index = 0U; index < stream->count; ++index)
            if (stream->members[index].name)
                xx_mem_free(stream->members[index].name);
        xx_mem_free(stream->members);
    }
    if (stream->slots) xx_mem_free(stream->slots);
    xx_mem_free(stream);
}

/* Walks the chain again, storing the members in file order and naming
 * them. */
static bool ardi_build(Abstractformat *format, ardi_stream *stream,
                       xx_pd_struct *pd) {
    ardi_pair *pairs;
    ardi_layout second;
    uint8_t *header = NULL;
    size_t count, index, slots = 16U;
    bool result = false;
    count = stream->layout.pairs;
    if (count == 0U || count > ARDI_MAX_MEMBERS) return false;
    pairs = (ardi_pair *)xx_mem_calloc(count, sizeof(ardi_pair));
    if (!pairs) return false;
    if (!ardi_walk(format, &second, pairs, count, false, pd) ||
        second.pairs != count || second.sentinel != stream->layout.sentinel)
        goto done;
    while (slots < count * 2U) slots <<= 1U;
    stream->members = (ardi_member *)xx_mem_calloc(count, sizeof(ardi_member));
    stream->slots = (uint32_t *)xx_mem_calloc(slots, sizeof(uint32_t));
    stream->mask = slots - 1U;
    header = (uint8_t *)xx_mem_alloc(ARDI_HEADER_MAX);
    if (!stream->members || !stream->slots || !header) goto done;
    for (index = 0U; index < count; ++index) {
        ardi_member *member = &stream->members[index];
        char name[ARDI_NAME_MAX * 3U + 1U];
        size_t text, bang;
        bool named;
        if ((index & 0xFFU) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
        member->pair = pairs[count - 1U - index];
        if (!ardi_read_header(format, member->pair.header,
                              member->pair.header_size, header, &text, &bang))
            goto done;
        member->time = xx_data_get_u32(header + 4, 4, 0, false);
        named = bang <= ARDI_NAME_MAX &&
                ardi_decode(header + 8, bang, name, sizeof(name));
        stream->count = index + 1U;
        if (!ardi_publish_name(stream, index, named ? name : NULL)) goto done;
    }
    result = true;
done:
    if (header) xx_mem_free(header);
    xx_mem_free(pairs);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                              */

/* Output sink: forwards to the destination (none when only verifying) and
 * enforces the size limit. */
typedef struct ardi_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
} ardi_sink;

static ssize_t ardi_sink_write(xx_io_device *self, const void *buffer,
                               size_t size) {
    ardi_sink *sink = self ? (ardi_sink *)self->priv : NULL;
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t done = 0U;
    if (!sink || (!bytes && size != 0U) || size > ARDI_SSIZE_LIMIT ||
        (uint64_t)size > sink->limit - sink->written)
        return -1;
    while (sink->target && done < size) {
        ssize_t wrote = xx_io_write(sink->target, bytes + done, size - done);
        if (wrote <= 0 || (size_t)wrote > size - done) return -1;
        done += (size_t)wrote;
    }
    sink->written += size;
    return (ssize_t)size;
}

static bool ardi_unpack_member(Abstractformat *format,
                               const ardi_stream *stream,
                               const ardi_member *member,
                               xx_io_device *target, xx_pd_struct *pd) {
    ardi_sink sink;
    int64_t packed = member->pair.data_size - (int64_t)ARDI_BLOCK_MIN;
    if (packed <= 0 || member->pair.data < 0 ||
        member->pair.data + member->pair.data_size > stream->layout.size)
        return false;
    xx_mem_zero(&sink, sizeof(sink));
    sink.device.write = ardi_sink_write;
    sink.device.priv = &sink;
    sink.target = target;
    sink.limit = stream->max_member;
    return xx_deflate_unpack_device(format->device,
                                    format->base_address + member->pair.data +
                                        4,
                                    packed, &sink.device, false, pd);
}

/* ---------------------------------------------------------------------- */
/* Options and records                                                     */

static bool ardi_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
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

/* XX_META_ID_OPT_MAX_MEMBER_SIZE, when set to a non-negative integer. */
static uint64_t ardi_max_member(const Abstractformat *format,
                                const xx_list_s *options) {
    const xx_var *limit = xx_format_resolve_extra_parameter(
        format, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (!limit) return UINT64_MAX;
    switch (limit->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return xx_var_get_u64(limit);
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(limit);
            return value >= 0 ? (uint64_t)value : UINT64_MAX;
        }
        default: return UINT64_MAX;
    }
}

static bool ardi_set_record(Abstractformat *format, xx_archive_record *record,
                            const ardi_member *member) {
    int64_t base = format->base_address;
    uint8_t *header;
    size_t text, bang;
    bool result;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base + member->pair.header;
    record->header_size = member->pair.header_size;
    record->data_offset = base + member->pair.data + 4;
    record->compressed_size = member->pair.data_size - ARDI_BLOCK_MIN;
    result =
        xx_archive_record_set_original_name(record, member->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                       (uint64_t)record->compressed_size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                       8U) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                        false) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
    if (result && member->time >= ARDI_TIME_MIN &&
        member->time <= ARDI_TIME_MAX)
        result = xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                                member->time);
    if (!result) return false;
    /* Behind the '!' the header holds a one-letter kind code and a
     * description; both are published verbatim as the comment. */
    header = (uint8_t *)xx_mem_alloc(ARDI_HEADER_MAX);
    if (!header) return false;
    if (ardi_read_header(format, member->pair.header,
                         member->pair.header_size, header, &text, &bang)) {
        const uint8_t *rest = header + 8U + bang + 1U;
        size_t length = 0U, available = text - bang - 1U;
        char comment[ARDI_TEXT_MAX * 3U + 1U];
        while (length < available && rest[length] != 0U) ++length;
        if (length > 0U && length <= ARDI_TEXT_MAX &&
            ardi_decode(rest, length, comment, sizeof(comment)))
            result = xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                                    comment);
    }
    xx_mem_free(header);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_ardi_installer_init(xx_ardi_installer *archive, xx_io_device *device,
                            int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_ARDI_INSTALLER_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.os = XX_OS_OS2;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-dosexec");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_ardi_installer_check_is_valid;
    archive->format.handle_base_info = xx_ardi_installer_handle_base_info;
    archive->format.get_format_size = xx_ardi_installer_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ardi_installer_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ardi_installer_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ardi_installer_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ardi_installer_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ardi_installer_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ardi_installer_free_archive_records_reading;
    archive->chain_offset = -1;
}

xx_ardi_installer *xx_ardi_installer_create(xx_io_device *device,
                                            int64_t base_address) {
    xx_ardi_installer *archive =
        (xx_ardi_installer *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ardi_installer_init(archive, device, base_address);
    return archive;
}

void xx_ardi_installer_destroy(xx_ardi_installer *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ardi_installer_free(xx_ardi_installer *archive) {
    if (!archive) return;
    xx_ardi_installer_destroy(archive);
    xx_mem_free(archive);
}

bool xx_ardi_installer_check_is_valid(Abstractformat *format,
                                      xx_pd_struct *pd) {
    ardi_layout layout;
    return ardi_walk(format, &layout, NULL, 0U, true, pd);
}

/* Publishes what a complete walk measured. */
static void ardi_set_counts(Abstractformat *format,
                            const ardi_layout *layout) {
    xx_ardi_installer *archive = (xx_ardi_installer *)format;
    archive->number_of_records = layout->pairs;
    archive->block_count = layout->blocks;
    ardi_block_text(format, layout->title, layout->title_size, archive->title,
                    sizeof(archive->title));
    ardi_block_text(format, layout->path, layout->path_size,
                    archive->install_path, sizeof(archive->install_path));
    format->number_of_archive_records = layout->pairs;
    archive->chain_walked = true;
}

/* A chain longer than ARDI_QUICK_BLOCKS is walked in full on the first
 * question about its counts. */
static bool ardi_walk_all(Abstractformat *format, xx_pd_struct *pd) {
    ardi_layout layout;
    if (((xx_ardi_installer *)format)->chain_walked) return true;
    if (!ardi_walk(format, &layout, NULL, 0U, false, pd)) return false;
    ardi_set_counts(format, &layout);
    return true;
}

bool xx_ardi_installer_handle_base_info(Abstractformat *format,
                                        xx_pd_struct *pd) {
    xx_ardi_installer *archive;
    ardi_layout layout;
    if (!format || !ardi_walk(format, &layout, NULL, 0U, true, pd))
        return false;
    archive = (xx_ardi_installer *)format;
    archive->chain_offset = layout.sentinel;
    xx_rt_memcpy(archive->year, layout.year, sizeof(archive->year));
    if (layout.complete) ardi_set_counts(format, &layout);
    format->format_size = layout.size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_ardi_installer_get_format_size(Abstractformat *format,
                                          xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ardi_installer_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_ardi_installer_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_ardi_installer_handle_base_info(format, pd)) &&
                   ardi_walk_all(format, pd)
               ? ((xx_ardi_installer *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_ardi_installer_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ardi_stream *stream;
    xx_archive_record_state *state;
    if (!format) return NULL;
    stream = (ardi_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!ardi_walk(format, &stream->layout, NULL, 0U, false, pd) ||
        !ardi_build(format, stream, pd) || stream->count == 0U) {
        ardi_stream_free(stream);
        return NULL;
    }
    stream->max_member = ardi_max_member(format, options);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ardi_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ardi_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!ardi_copy_options(&state->options, options) ||
        !ardi_set_record(format, &state->current_record,
                         &stream->members[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->current_index = 0;
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_ardi_installer_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ardi_installer_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    ardi_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ardi_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!ardi_set_record(format, &state->current_record,
                         &stream->members[stream->index])) {
        state->has_record = false;
        return false;
    }
    state->current_index = (int64_t)stream->index;
    state->has_record = true;
    return true;
}

bool xx_ardi_installer_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    ardi_stream *stream;
    const ardi_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ardi_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->members[stream->index];
    path_option = xx_format_resolve_extra_parameter(
        format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return ardi_unpack_member(format, stream, member, NULL, pd);
    if (!member->extractable || !ardi_safe_name(member->name)) return false;
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = ardi_unpack_member(format, stream, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_ardi_installer_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
