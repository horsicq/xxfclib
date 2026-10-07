/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * JVC floppy images of the Tandy Color Computer and the Dragon 32/64 (also
 * headerless .dsk), with a Dragon DOS or an OS-9 RBF volume on them.
 * xx_jvc.h carries the layout.  Written from the on-disk structures; MAME
 * imgtool (coco_jvc_dgndos, coco_jvc_os9) was used only as a black-box
 * oracle.  No code was taken from it.  The lifecycle and record plumbing
 * follow xx_rsdos_fs.c of this library.
 *
 * There is no magic.  The probe is bounded: a size test, at most one
 * 5-byte header read, then for OS-9 three sector reads (identification
 * sector, root descriptor, first root directory sector) before the walk,
 * and for Dragon DOS at most two single-sector reads plus one 4 KiB read
 * of the directory.  The OS-9 walk is capped in members, directories,
 * depth and directory sectors, with a visited map over the volume.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/jvc/xx_jvc.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef JVC
#define XX_JVC_FILE_TYPE XX_FILE_TYPE_JVC
#else
#define XX_JVC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define JVC_SECTOR 256U
#define JVC_MAX_HEADER 5U
#define JVC_MIN_SECTORS 16U
#define JVC_MAX_BYTES ((int64_t)16 * 1024 * 1024)
#define JVC_MAX_MEMBERS 4096U
#define JVC_FS_DRAGON 1U
#define JVC_FS_OS9 2U

#define DGN_DIR_TRACK 20U
#define DGN_SPT 18U
#define DGN_MIN_TRACKS (DGN_DIR_TRACK + 1U)
#define DGN_TRACK_SECTORS 18U  /* bitmap (2) + directory (16) */
#define DGN_DIR_SECTORS 16U
#define DGN_ENTRY 25U
#define DGN_PER_SECTOR 10U
#define DGN_ENTRIES (DGN_DIR_SECTORS * DGN_PER_SECTOR)
#define DGN_F_DELETED 0x80U
#define DGN_F_CONT_ENTRY 0x01U
#define DGN_F_END 0x08U
#define DGN_F_CONTINUED 0x20U
#define DGN_HEAD_EXTENTS 4U
#define DGN_CONT_EXTENTS 7U
#define DGN_MAX_EXTENTS (DGN_HEAD_EXTENTS + DGN_CONT_EXTENTS * DGN_ENTRIES)

#define OS9_MAX_SEGMENTS 48U
#define OS9_ENTRY 32U
#define OS9_NAME 29U
#define OS9_ATT_DIR 0x80U
#define OS9_MAX_DEPTH 16U
#define OS9_MAX_DIRS 1024U
#define OS9_MAX_DIR_SECTORS 65536U
#define OS9_MAX_DIR_BYTES ((uint32_t)4 * 1024 * 1024)

#define JVC_MAX_EXTENTS DGN_MAX_EXTENTS

typedef struct jvc_extent_s {
    uint32_t lsn;
    uint32_t count;
} jvc_extent;

typedef struct jvc_member_s {
    char *name;
    int64_t size;
    uint32_t locator;      /**< Dragon: head entry index; OS-9: FD LSN. */
    uint8_t attr;
    bool in_range;         /**< Every sector lies inside the image. */
} jvc_member;

typedef struct jvc_stream_s {
    int64_t image_size;
    int64_t data_offset;
    uint32_t header_size;
    uint32_t data_sectors;
    uint32_t filesystem;
    uint32_t total_sectors;
    uint32_t dragon_dir_lsn;  /**< LSN of directory track sector 1. */
    uint8_t header[JVC_MAX_HEADER];
    uint8_t dir[DGN_DIR_SECTORS * JVC_SECTOR];
    jvc_member *items;
    size_t count;
    size_t capacity;
    size_t index;
} jvc_stream;

static void xx_jvc_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint32_t jvc_be16(const uint8_t *p) {
    return ((uint32_t)p[0] << 8U) | (uint32_t)p[1];
}

static bool jvc_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static int64_t jvc_lsn_offset(const jvc_stream *s, uint32_t lsn) {
    return s->data_offset + (int64_t)lsn * JVC_SECTOR;
}

/* Read @p count sectors from @p lsn; false when any is outside the image. */
static bool jvc_read_sectors(Abstractformat *f, const jvc_stream *s,
                             uint32_t lsn, uint32_t count, uint8_t *buffer) {
    if (count == 0U || lsn >= s->data_sectors ||
        count > s->data_sectors - lsn)
        return false;
    return jvc_read_at(f->device, jvc_lsn_offset(s, lsn), buffer,
                       (size_t)count * JVC_SECTOR);
}

/* Size, header and the sector grid.  At most one 5-byte read. */
static bool jvc_geometry(Abstractformat *f, jvc_stream *s) {
    int64_t total, span, data;
    uint32_t h;
    if (!f || !f->device || f->base_address < 0) return false;
    total = xx_io_total_size(f->device);
    if (total < f->base_address) return false;
    span = total - f->base_address;
    h = (uint32_t)(span % (int64_t)JVC_SECTOR);
    if (h > JVC_MAX_HEADER) return false;
    data = span - (int64_t)h;
    if (data < (int64_t)JVC_MIN_SECTORS * JVC_SECTOR || data > JVC_MAX_BYTES)
        return false;
    if (h != 0U) {
        if (!jvc_read_at(f->device, f->base_address, s->header, h))
            return false;
        if (s->header[0] == 0U) return false;
        if (h > 1U && s->header[1] != 1U && s->header[1] != 2U) return false;
        if (h > 2U && s->header[2] != 1U) return false;
        if (h > 3U && s->header[3] > 1U) return false;
        if (h > 4U && s->header[4] != 0U) return false;
    }
    s->image_size = span;
    s->header_size = h;
    s->data_offset = f->base_address + (int64_t)h;
    s->data_sectors = (uint32_t)(data / (int64_t)JVC_SECTOR);
    return true;
}

static char jvc_fold(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool jvc_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || jvc_fold(name[index]) != word[index]) return false;
    return word[stem] == 0;
}

/* Windows resolves these stems to devices whatever the extension. */
static bool jvc_is_device_stem(const char *name, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && (name[stem - 1U] == ' ' || name[stem - 1U] == '.'))
        --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (jvc_stem_is(name, stem, devices[index])) return true;
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((jvc_fold(name[0]) == 'C' && jvc_fold(name[1]) == 'O' &&
             jvc_fold(name[2]) == 'M') ||
            (jvc_fold(name[0]) == 'L' && jvc_fold(name[1]) == 'P' &&
             jvc_fold(name[2]) == 'T'));
}

static char jvc_safe_char(uint8_t c) {
    if (c < 0x20U || c > 0x7EU || c == '/' || c == '\\' || c == ':' ||
        c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
        c == '|' || c == '~')
        return '_';
    return (char)c;
}

/* One path component from @p length raw characters into @p out (room for
 * length + 2).  Leading spaces and trailing dots / spaces become '_', an
 * all-dot name becomes underscores, a device stem gets a '_' in front. */
static size_t jvc_component(const char *raw, size_t length, char *out) {
    size_t index, at = 0U;
    bool meaningful = false;
    char tmp[64];
    if (length == 0U) {
        out[0] = '_';
        out[1] = 0;
        return 1U;
    }
    if (length > sizeof(tmp) - 2U) length = sizeof(tmp) - 2U;
    for (index = 0U; index < length; ++index) {
        tmp[index] = jvc_safe_char((uint8_t)raw[index]);
        if (tmp[index] != '.' && tmp[index] != ' ') meaningful = true;
    }
    for (index = 0U; index < length && tmp[index] == ' '; ++index)
        tmp[index] = '_';
    for (index = length; index > 0U &&
                         (tmp[index - 1U] == '.' || tmp[index - 1U] == ' ');
         --index)
        tmp[index - 1U] = '_';
    if (!meaningful)
        for (index = 0U; index < length; ++index) tmp[index] = '_';
    if (jvc_is_device_stem(tmp, length)) out[at++] = '_';
    for (index = 0U; index < length; ++index) out[at++] = tmp[index];
    out[at] = 0;
    return at;
}

static bool jvc_same_name(const char *a, const char *b) {
    for (;; ++a, ++b) {
        if (jvc_fold(*a) != jvc_fold(*b)) return false;
        if (*a == 0) return true;
    }
}

/* Insert "~<n>" before the extension of the last path component. */
static bool jvc_add_suffix(jvc_member *member, size_t n) {
    char digits[12];
    size_t length = xx_str_len(member->name), dot = length, count = 0U;
    size_t index, slash = 0U;
    char *grown;
    for (index = 0U; index < length; ++index)
        if (member->name[index] == '/') slash = index + 1U;
    for (index = slash; index < length; ++index)
        if (member->name[index] == '.') dot = index;
    if (dot == slash) dot = length;
    do {
        digits[count++] = (char)('0' + (char)(n % 10U));
        n /= 10U;
    } while (n != 0U && count < sizeof(digits));
    grown = (char *)xx_mem_alloc(length + 1U + count + 1U);
    if (!grown) return false;
    xx_rt_memcpy(grown, member->name, dot);
    grown[dot] = '~';
    for (index = 0U; index < count; ++index)
        grown[dot + 1U + index] = digits[count - 1U - index];
    xx_rt_memcpy(grown + dot + 1U + count, member->name + dot,
                 length - dot + 1U);
    xx_mem_free(member->name);
    member->name = grown;
    return true;
}

static bool jvc_make_names_unique(jvc_member *items, size_t count) {
    size_t index, earlier;
    bool *renamed;
    if (count < 2U) return true;
    renamed = (bool *)xx_mem_calloc(count, sizeof(*renamed));
    if (!renamed) return false;
    for (index = 1U; index < count; ++index)
        for (earlier = 0U; earlier < index; ++earlier)
            if (!renamed[earlier] &&
                jvc_same_name(items[earlier].name, items[index].name)) {
                renamed[index] = true;
                break;
            }
    for (index = 0U; index < count; ++index)
        if (renamed[index] && !jvc_add_suffix(&items[index], index)) {
            xx_mem_free(renamed);
            return false;
        }
    xx_mem_free(renamed);
    return true;
}

static void jvc_stream_free(void *opaque) {
    jvc_stream *s = (jvc_stream *)opaque;
    size_t index;
    if (!s) return;
    if (s->items) {
        for (index = 0U; index < s->count; ++index)
            if (s->items[index].name) xx_mem_free(s->items[index].name);
        xx_mem_free(s->items);
    }
    xx_mem_free(s);
}

/* Append a member that owns @p name.  False (name freed) on failure. */
static bool jvc_add_member(jvc_stream *s, char *name, int64_t size,
                           uint32_t locator, uint8_t attr, bool in_range) {
    jvc_member *member;
    if (s->count >= JVC_MAX_MEMBERS) {
        xx_mem_free(name);
        return false;
    }
    if (s->count == s->capacity) {
        size_t grown = s->capacity ? s->capacity * 2U : 32U;
        jvc_member *items;
        if (grown > JVC_MAX_MEMBERS) grown = JVC_MAX_MEMBERS;
        items = (jvc_member *)xx_mem_realloc(s->items,
                                             grown * sizeof(*items));
        if (!items) {
            xx_mem_free(name);
            return false;
        }
        s->items = items;
        s->capacity = grown;
    }
    member = &s->items[s->count++];
    member->name = name;
    member->size = size;
    member->locator = locator;
    member->attr = attr;
    member->in_range = in_range;
    return true;
}

/* ---------------------------------------------------------- Dragon DOS -- */

static const uint8_t *dgn_entry(const jvc_stream *s, uint32_t index) {
    return s->dir + (index / DGN_PER_SECTOR) * JVC_SECTOR +
           (index % DGN_PER_SECTOR) * DGN_ENTRY;
}

/* Trimmed length of a NUL / space padded field; false when a non-padding
 * byte is not printable. */
static bool dgn_field(const uint8_t *field, size_t length, size_t *used) {
    size_t end = length, index;
    while (end > 0U && (field[end - 1U] == 0x00U || field[end - 1U] == ' '))
        --end;
    for (index = 0U; index < end; ++index)
        if (field[index] < 0x20U || field[index] > 0x7EU) return false;
    *used = end;
    return true;
}

/* Walk the entry chain of @p head.  Fills @p extents (may be NULL), the
 * sector total and the last-sector byte count.  False when the chain is
 * broken or an extent leaves the volume. */
static bool dgn_chain(const jvc_stream *s, uint32_t head, jvc_extent *extents,
                      size_t *extent_count, uint64_t *sectors,
                      uint32_t *last, bool *in_range) {
    uint8_t visited[DGN_ENTRIES];
    uint32_t index = head, steps = 0U;
    size_t n = 0U;
    uint64_t total = 0U;
    bool inside = true;
    xx_mem_zero(visited, sizeof(visited));
    for (;;) {
        const uint8_t *e = dgn_entry(s, index);
        uint32_t first, slots, k;
        if (++steps > DGN_ENTRIES || visited[index]) return false;
        visited[index] = 1U;
        if (index == head) {
            first = 12U;
            slots = DGN_HEAD_EXTENTS;
        } else {
            first = 1U;
            slots = DGN_CONT_EXTENTS;
        }
        for (k = 0U; k < slots; ++k) {
            const uint8_t *x = e + first + k * 3U;
            uint32_t lsn = jvc_be16(x), count = x[2];
            if (count == 0U) continue;
            if (lsn >= s->total_sectors || count > s->total_sectors - lsn)
                return false;
            if (lsn >= s->data_sectors || count > s->data_sectors - lsn)
                inside = false;
            if (extents) {
                if (n >= JVC_MAX_EXTENTS) return false;
                extents[n].lsn = lsn;
                extents[n].count = count;
            }
            ++n;
            total += count;
        }
        if ((e[0] & DGN_F_CONTINUED) != 0U) {
            uint32_t next = e[24];
            const uint8_t *ne;
            if (next >= DGN_ENTRIES) return false;
            ne = dgn_entry(s, next);
            if ((ne[0] & DGN_F_CONT_ENTRY) == 0U ||
                (ne[0] & DGN_F_DELETED) != 0U)
                return false;
            index = next;
            continue;
        }
        *last = e[24];
        break;
    }
    if (extent_count) *extent_count = n;
    *sectors = total;
    if (in_range) *in_range = inside;
    return true;
}

static int64_t dgn_size(uint64_t sectors, uint32_t last) {
    if (sectors == 0U) return 0;
    return (int64_t)(sectors - 1U) * JVC_SECTOR +
           (int64_t)(last == 0U ? JVC_SECTOR : last);
}

/* Geometry quad at 0xFC..0xFF of the directory track's first sector. */
static bool dgn_locate(Abstractformat *f, jvc_stream *s) {
    uint8_t sector[JVC_SECTOR];
    uint32_t sides_first = 1U, sides_last = 2U, sides;
    if (s->header_size >= 1U && s->header[0] != DGN_SPT) return false;
    if (s->header_size >= 2U) sides_first = sides_last = s->header[1];
    for (sides = sides_first; sides <= sides_last; ++sides) {
        uint32_t spt = DGN_SPT * sides;
        uint32_t lsn = DGN_DIR_TRACK * spt;
        uint32_t tracks;
        if (lsn >= s->data_sectors ||
            DGN_TRACK_SECTORS > s->data_sectors - lsn)
            continue;
        if (!jvc_read_sectors(f, s, lsn, 1U, sector)) continue;
        tracks = sector[0xFC];
        if (sector[0xFD] != spt ||
            sector[0xFE] != (uint8_t)~sector[0xFC] ||
            sector[0xFF] != (uint8_t)~sector[0xFD] ||
            tracks < DGN_MIN_TRACKS)
            continue;
        s->dragon_dir_lsn = lsn;
        s->total_sectors = tracks * spt;
        return true;
    }
    return false;
}

static bool dgn_parse(Abstractformat *f, jvc_stream *s, xx_pd_struct *pd) {
    uint32_t index;
    if (!dgn_locate(f, s)) return false;
    if (!jvc_read_sectors(f, s, s->dragon_dir_lsn + 2U, DGN_DIR_SECTORS,
                          s->dir))
        return false;
    s->filesystem = JVC_FS_DRAGON;
    for (index = 0U; index < DGN_ENTRIES; ++index) {
        const uint8_t *e = dgn_entry(s, index);
        size_t name_len, ext_len, at = 0U;
        uint64_t sectors;
        uint32_t last;
        bool inside;
        char raw[12];
        char *name;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if ((e[0] & DGN_F_END) != 0U) break;
        if ((e[0] & (DGN_F_DELETED | DGN_F_CONT_ENTRY)) != 0U) continue;
        if (!dgn_field(e + 1, 8U, &name_len) ||
            !dgn_field(e + 9, 3U, &ext_len) || name_len == 0U ||
            e[1] == ' ')
            return false;
        if (!dgn_chain(s, index, NULL, NULL, &sectors, &last, &inside))
            return false;
        /* name + '.' + ext, each part sanitized on its own */
        name = (char *)xx_mem_alloc(8U + 2U + 1U + 3U + 2U + 1U);
        if (!name) return false;
        xx_rt_memcpy(raw, e + 1, name_len);
        at = jvc_component(raw, name_len, name);
        if (ext_len != 0U) {
            name[at++] = '.';
            xx_rt_memcpy(raw, e + 9, ext_len);
            (void)jvc_component(raw, ext_len, name + at);
        }
        if (!jvc_add_member(s, name, dgn_size(sectors, last), index, e[0],
                            inside))
            return false;
    }
    return true;
}

/* ---------------------------------------------------------------- OS-9 -- */

/* Segment list of a file descriptor.  False when a segment leaves the
 * volume.  @p inside is cleared when one leaves the image. */
static bool os9_segments(const jvc_stream *s, const uint8_t *fd,
                         jvc_extent *extents, size_t *count,
                         uint64_t *sectors, bool *inside) {
    uint32_t k;
    size_t n = 0U;
    uint64_t total = 0U;
    *inside = true;
    for (k = 0U; k < OS9_MAX_SEGMENTS; ++k) {
        const uint8_t *p = fd + 0x10U + k * 5U;
        uint32_t lsn = xx_data_get_u24(p, 3, 0, true), n_sec = jvc_be16(p + 3);
        if (n_sec == 0U) break;
        if (lsn == 0U || lsn >= s->total_sectors ||
            n_sec > s->total_sectors - lsn)
            return false;
        if (lsn >= s->data_sectors || n_sec > s->data_sectors - lsn)
            *inside = false;
        if (extents) {
            extents[n].lsn = lsn;
            extents[n].count = n_sec;
        }
        ++n;
        total += n_sec;
    }
    if (count) *count = n;
    *sectors = total;
    return true;
}

/* LSN 0 and the root directory head.  Three sector reads. */
static bool os9_identify(Abstractformat *f, jvc_stream *s, uint32_t *root) {
    uint8_t sector[JVC_SECTOR];
    uint8_t fd[JVC_SECTOR];
    uint32_t tot, tks, map, bit, dir, clusters, need, size;
    uint64_t sectors;
    size_t count;
    bool inside;
    if (!jvc_read_sectors(f, s, 0U, 1U, sector)) return false;
    tot = xx_data_get_u24(sector, 3, 0, true);
    tks = sector[3];
    map = jvc_be16(sector + 4);
    bit = jvc_be16(sector + 6);
    dir = xx_data_get_u24(sector + 8, 3, 0, true);
    if (tot < 4U || tot > s->data_sectors || tks == 0U) return false;
    if (bit == 0U || (bit & (bit - 1U)) != 0U || bit > 256U) return false;
    clusters = (tot + bit - 1U) / bit;
    need = (clusters + 7U) / 8U;
    if (map < need || map > ((need + 255U) / 256U) * 256U) return false;
    if (dir < 1U + (map + 255U) / 256U || dir >= tot) return false;
    s->total_sectors = tot;
    if (!jvc_read_sectors(f, s, dir, 1U, fd)) return false;
    if ((fd[0] & OS9_ATT_DIR) == 0U) return false;
    size = xx_data_get_u32(fd + 9, 4, 0, true);
    if (size < 2U * OS9_ENTRY || size % OS9_ENTRY != 0U ||
        size > OS9_MAX_DIR_BYTES)
        return false;
    count = 0U;
    if (!os9_segments(s, fd, NULL, &count, &sectors, &inside) || !inside ||
        count == 0U || sectors * JVC_SECTOR < size)
        return false;
    if (!jvc_read_sectors(f, s, xx_data_get_u24(fd + 0x10, 3, 0, true), 1U, sector))
        return false;
    /* ".." then ".", both naming the root itself. */
    if (sector[0] != '.' || sector[1] != 0xAEU || sector[32] != 0xAEU ||
        xx_data_get_u24(sector + 29, 3, 0, true) != dir || xx_data_get_u24(sector + 61, 3, 0, true) != dir)
        return false;
    *root = dir;
    return true;
}

typedef struct os9_pending_s {
    uint32_t fd;
    uint32_t depth;
    char *prefix;  /**< "" or "A/B/" */
} os9_pending;

/* Decode one entry name into @p raw; 0 when the entry is deleted,
 * -1 when the name is malformed. */
static int os9_entry_name(const uint8_t *e, char *raw, size_t *length) {
    size_t k;
    if (e[0] == 0U) return 0;
    for (k = 0U; k < OS9_NAME; ++k) {
        uint8_t c = (uint8_t)(e[k] & 0x7FU);
        if (c <= 0x20U || c > 0x7EU) return -1;
        raw[k] = (char)c;
        if ((e[k] & 0x80U) != 0U) {
            *length = k + 1U;
            return 1;
        }
    }
    return -1;
}

static char *os9_join(const char *prefix, const char *component,
                      bool directory) {
    size_t a = xx_str_len(prefix), b = xx_str_len(component);
    char *out = (char *)xx_mem_alloc(a + b + 2U);
    if (!out) return NULL;
    xx_rt_memcpy(out, prefix, a);
    xx_rt_memcpy(out + a, component, b);
    if (directory) out[a + b++] = '/';
    out[a + b] = 0;
    return out;
}

static bool os9_parse(Abstractformat *f, jvc_stream *s, xx_pd_struct *pd) {
    os9_pending *stack = NULL;
    uint8_t *visited = NULL;
    size_t depth_used = 0U, dirs_seen = 0U;
    uint32_t dir_sectors = 0U, root;
    uint8_t fd[JVC_SECTOR];
    uint8_t sector[JVC_SECTOR];
    bool ok = false;
    if (!os9_identify(f, s, &root)) return false;
    s->filesystem = JVC_FS_OS9;
    visited = (uint8_t *)xx_mem_calloc((s->total_sectors + 7U) / 8U, 1U);
    stack = (os9_pending *)xx_mem_calloc(OS9_MAX_DIRS, sizeof(*stack));
    if (!visited || !stack) goto done;
    stack[0].fd = root;
    stack[0].depth = 0U;
    stack[0].prefix = xx_str_dup("");
    if (!stack[0].prefix) goto done;
    depth_used = 1U;
    dirs_seen = 1U;
    visited[root / 8U] |= (uint8_t)(1U << (root % 8U));
    while (depth_used > 0U) {
        os9_pending cur = stack[--depth_used];
        jvc_extent segs[OS9_MAX_SEGMENTS];
        size_t nseg = 0U, seg;
        uint64_t sectors;
        uint32_t size, consumed = 0U;
        bool inside, fail = false;
        stack[depth_used].prefix = NULL;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !jvc_read_sectors(f, s, cur.fd, 1U, fd) ||
            (fd[0] & OS9_ATT_DIR) == 0U) {
            xx_mem_free(cur.prefix);
            goto done;
        }
        size = xx_data_get_u32(fd + 9, 4, 0, true);
        if (size % OS9_ENTRY != 0U || size > OS9_MAX_DIR_BYTES ||
            !os9_segments(s, fd, segs, &nseg, &sectors, &inside) ||
            !inside || sectors * JVC_SECTOR < size) {
            xx_mem_free(cur.prefix);
            goto done;
        }
        for (seg = 0U; seg < nseg && consumed < size && !fail; ++seg) {
            uint32_t k;
            for (k = 0U; k < segs[seg].count && consumed < size && !fail;
                 ++k) {
                uint32_t off;
                if (++dir_sectors > OS9_MAX_DIR_SECTORS ||
                    !jvc_read_sectors(f, s, segs[seg].lsn + k, 1U, sector)) {
                    fail = true;
                    break;
                }
                for (off = 0U; off < JVC_SECTOR && consumed < size;
                     off += OS9_ENTRY, consumed += OS9_ENTRY) {
                    const uint8_t *e = sector + off;
                    char raw[OS9_NAME + 1U];
                    char component[OS9_NAME + 4U];
                    size_t length = 0U;
                    uint32_t child;
                    uint8_t cfd[JVC_SECTOR];
                    int named = os9_entry_name(e, raw, &length);
                    if (named == 0) continue;
                    if (named < 0) {
                        fail = true;
                        break;
                    }
                    if ((length == 1U && raw[0] == '.') ||
                        (length == 2U && raw[0] == '.' && raw[1] == '.'))
                        continue;
                    child = xx_data_get_u24(e + 29, 3, 0, true);
                    if (child == 0U || child >= s->total_sectors ||
                        !jvc_read_sectors(f, s, child, 1U, cfd)) {
                        fail = true;
                        break;
                    }
                    (void)jvc_component(raw, length, component);
                    if ((cfd[0] & OS9_ATT_DIR) != 0U) {
                        char *prefix;
                        if (visited[child / 8U] & (1U << (child % 8U)))
                            continue;
                        visited[child / 8U] |= (uint8_t)(1U << (child % 8U));
                        if (cur.depth + 1U >= OS9_MAX_DEPTH ||
                            dirs_seen >= OS9_MAX_DIRS)
                            continue;
                        prefix = os9_join(cur.prefix, component, true);
                        if (!prefix) {
                            fail = true;
                            break;
                        }
                        stack[depth_used].fd = child;
                        stack[depth_used].depth = cur.depth + 1U;
                        stack[depth_used].prefix = prefix;
                        ++depth_used;
                        ++dirs_seen;
                    } else {
                        uint64_t fsec;
                        bool fin;
                        uint32_t fsize = xx_data_get_u32(cfd + 9, 4, 0, true);
                        char *name;
                        if (!os9_segments(s, cfd, NULL, NULL, &fsec, &fin)) {
                            fail = true;
                            break;
                        }
                        if (fsec * JVC_SECTOR < fsize) fin = false;
                        if (s->count >= JVC_MAX_MEMBERS) continue;
                        name = os9_join(cur.prefix, component, false);
                        if (!name ||
                            !jvc_add_member(s, name, (int64_t)fsize, child,
                                            cfd[0], fin)) {
                            fail = true;
                            break;
                        }
                    }
                }
            }
        }
        xx_mem_free(cur.prefix);
        if (fail) goto done;
    }
    ok = true;
done:
    while (stack && depth_used > 0U) xx_mem_free(stack[--depth_used].prefix);
    if (stack) xx_mem_free(stack);
    if (visited) xx_mem_free(visited);
    return ok;
}

/* -------------------------------------------------------------- common -- */

static bool jvc_parse(Abstractformat *f, jvc_stream **result,
                      xx_pd_struct *pd) {
    jvc_stream *s;
    bool ok;
    if (result) *result = NULL;
    s = (jvc_stream *)xx_mem_calloc(1U, sizeof(*s));
    if (!s) return false;
    if (!jvc_geometry(f, s)) {
        jvc_stream_free(s);
        return false;
    }
    ok = os9_parse(f, s, pd);
    if (!ok || s->count == 0U) {
        /* Not OS-9, or an empty OS-9 skeleton: a Dragon DOS directory
         * written over a formatted OS-9 disk wins when it has files. */
        jvc_stream *d = (jvc_stream *)xx_mem_calloc(1U, sizeof(*d));
        if (!d) {
            jvc_stream_free(s);
            return false;
        }
        xx_rt_memcpy(d->header, s->header, sizeof(d->header));
        d->image_size = s->image_size;
        d->data_offset = s->data_offset;
        d->header_size = s->header_size;
        d->data_sectors = s->data_sectors;
        if (dgn_parse(f, d, pd) && (!ok || d->count != 0U)) {
            jvc_stream_free(s);
            s = d;
            ok = true;
        } else {
            jvc_stream_free(d);
        }
    }
    if (ok) ok = jvc_make_names_unique(s->items, s->count);
    if (!ok) {
        jvc_stream_free(s);
        return false;
    }
    if (result)
        *result = s;
    else
        jvc_stream_free(s);
    return true;
}

/* Extents of @p member, re-derived from the image. */
static bool jvc_member_extents(Abstractformat *f, const jvc_stream *s,
                               const jvc_member *member, jvc_extent *extents,
                               size_t *count, int64_t *size) {
    uint64_t sectors;
    bool inside;
    if (s->filesystem == JVC_FS_DRAGON) {
        uint32_t last;
        if (member->locator >= DGN_ENTRIES ||
            !dgn_chain(s, member->locator, extents, count, &sectors, &last,
                       &inside) ||
            !inside)
            return false;
        *size = dgn_size(sectors, last);
        return true;
    }
    if (s->filesystem == JVC_FS_OS9) {
        uint8_t fd[JVC_SECTOR];
        if (!jvc_read_sectors(f, s, member->locator, 1U, fd) ||
            (fd[0] & OS9_ATT_DIR) != 0U ||
            !os9_segments(s, fd, extents, count, &sectors, &inside) ||
            !inside)
            return false;
        *size = (int64_t)xx_data_get_u32(fd + 9, 4, 0, true);
        return sectors * JVC_SECTOR >= (uint64_t)*size;
    }
    return false;
}

static bool jvc_copy_member(Abstractformat *f, const jvc_stream *s,
                            const jvc_member *member,
                            xx_io_device *destination, xx_pd_struct *pd) {
    jvc_extent *extents;
    uint8_t *buffer;
    size_t count = 0U, index;
    int64_t size = 0, remaining;
    bool ok = true;
    if (!member->in_range || member->size < 0) return false;
    extents = (jvc_extent *)xx_mem_alloc(JVC_MAX_EXTENTS * sizeof(*extents));
    buffer = (uint8_t *)xx_mem_alloc(JVC_SECTOR);
    if (!extents || !buffer ||
        !jvc_member_extents(f, s, member, extents, &count, &size) ||
        size != member->size) {
        if (extents) xx_mem_free(extents);
        if (buffer) xx_mem_free(buffer);
        return false;
    }
    remaining = size;
    for (index = 0U; ok && index < count && remaining > 0; ++index) {
        uint32_t k;
        for (k = 0U; k < extents[index].count && remaining > 0; ++k) {
            size_t amount = remaining > (int64_t)JVC_SECTOR
                                ? (size_t)JVC_SECTOR : (size_t)remaining;
            size_t written = 0U;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !jvc_read_sectors(f, s, extents[index].lsn + k, 1U,
                                  buffer)) {
                ok = false;
                break;
            }
            while (destination && written < amount) {
                ssize_t done = xx_io_write(destination, buffer + written,
                                           amount - written);
                if (done <= 0 || (size_t)done > amount - written) {
                    ok = false;
                    break;
                }
                written += (size_t)done;
            }
            if (!ok) break;
            remaining -= (int64_t)amount;
        }
    }
    if (ok && remaining != 0) ok = false;
    xx_mem_free(extents);
    xx_mem_free(buffer);
    return ok;
}

/* Re-check a stored name before anything is created on disk. */
static bool jvc_safe_output_name(const char *name) {
    size_t length, index, start = 0U;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    if (name[0] == '/' || name[length - 1U] == '/') return false;
    for (index = 0U; index <= length; ++index) {
        char c = name[index];
        if (c == '/' || c == 0) {
            size_t part = index - start, k;
            bool meaningful = false;
            if (part == 0U) return false;
            if (name[start] == ' ' || name[index - 1U] == ' ' ||
                name[index - 1U] == '.')
                return false;
            for (k = start; k < index; ++k)
                if (name[k] != '.' && name[k] != ' ') meaningful = true;
            if (!meaningful || jvc_is_device_stem(name + start, part))
                return false;
            start = index + 1U;
            continue;
        }
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU ||
            c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' ||
            c == '|' || c == '?' || c == '*')
            return false;
    }
    return true;
}

static bool jvc_copy_options(xx_list_s *destination,
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

static const xx_var *jvc_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool jvc_set_record(const jvc_stream *s, xx_archive_record *record,
                           const jvc_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (s->filesystem == JVC_FS_DRAGON) {
        record->header_offset =
            jvc_lsn_offset(s, s->dragon_dir_lsn + 2U) +
            (int64_t)(member->locator / DGN_PER_SECTOR) * JVC_SECTOR +
            (int64_t)(member->locator % DGN_PER_SECTOR) * DGN_ENTRY;
        record->header_size = DGN_ENTRY;
        {
            const uint8_t *e = dgn_entry(s, member->locator);
            uint32_t k;
            for (k = 0U; k < DGN_HEAD_EXTENTS; ++k)
                if (e[12U + k * 3U + 2U] != 0U) {
                    record->data_offset =
                        jvc_lsn_offset(s, jvc_be16(e + 12U + k * 3U));
                    break;
                }
        }
    } else {
        record->header_offset = jvc_lsn_offset(s, member->locator);
        record->header_size = JVC_SECTOR;
    }
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          (uint64_t)member->attr) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_jvc_init(xx_jvc *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_JVC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-coco-jvc-disk");
    xx_format_set_extension(&archive->format, "jvc");
    archive->format.check_is_valid = xx_jvc_check_is_valid;
    archive->format.handle_base_info = xx_jvc_handle_base_info;
    archive->format.get_format_size = xx_jvc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_jvc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_jvc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_jvc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_jvc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_jvc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_jvc_free_archive_records_reading;
    archive->format.destroy = xx_jvc_vtable_destroy;
    archive->image_size = -1;
}

xx_jvc *xx_jvc_create(xx_io_device *device, int64_t base_address) {
    xx_jvc *archive = (xx_jvc *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_jvc_init(archive, device, base_address);
    return archive;
}

void xx_jvc_destroy(xx_jvc *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_jvc_free(xx_jvc *archive) {
    if (!archive) return;
    xx_jvc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_jvc_vtable_destroy(Abstractformat *self) {
    xx_jvc_destroy((xx_jvc *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_jvc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    return jvc_parse(self, NULL, pd);
}

bool xx_jvc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_jvc *archive = (xx_jvc *)self;
    jvc_stream *s;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    if (!jvc_parse(self, &s, pd)) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    archive->number_of_records = s->count;
    archive->image_size = s->image_size;
    archive->header_size = s->header_size;
    archive->filesystem = s->filesystem;
    archive->total_sectors = s->total_sectors;
    self->number_of_archive_records = s->count;
    self->format_size = s->image_size;
    self->file_type = XX_JVC_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_valid = true;
    jvc_stream_free(s);
    return true;
}

int64_t xx_jvc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_jvc_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0U;
    return self->is_valid ? ((xx_jvc *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

xx_archive_record_state *xx_jvc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    jvc_stream *s;
    xx_archive_record_state *state;
    if (!self || !self->device) return NULL;
    if (!jvc_parse(self, &s, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        jvc_stream_free(s);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = s;
    state->free_internal = jvc_stream_free;
    state->total_records = (int64_t)s->count;
    if (!jvc_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (s->count != 0U) {
        if (!jvc_set_record(s, &state->current_record, &s->items[0])) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
    }
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_jvc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_jvc_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    jvc_stream *s;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    s = (jvc_stream *)state->internal_state;
    if (!s || s->index + 1U >= s->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++s->index;
    ++state->current_index;
    state->has_record =
        jvc_set_record(s, &state->current_record, &s->items[s->index]);
    return state->has_record;
}

bool xx_jvc_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    jvc_stream *s;
    const jvc_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!self || !state || state->format != self || !state->has_record ||
        !(s = (jvc_stream *)state->internal_state) || s->index >= s->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &s->items[s->index];
    if (!member->in_range) return false;
    path_option = jvc_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return jvc_copy_member(self, s, member, NULL, pd);
    if (!jvc_safe_output_name(member->name)) return false;
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
        result = jvc_copy_member(self, s, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_jvc_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
