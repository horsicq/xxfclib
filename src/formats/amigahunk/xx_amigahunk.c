/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "xxfclib/formats/amigahunk/xx_amigahunk.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/formats/xx_memory_map.h"

#include <limits.h>

/* Minimum plausible file: a hunk id plus one more longword. */
#define XX_AMIGAHUNK_MIN_SIZE 8

/* Loadable hunks are rounded up to this many bytes when mapped. */
#define XX_AMIGAHUNK_ALIGNMENT INT64_C(16)

/* Guard for the per-hunk inner lists (relocations, symbols, externals). */
#define XX_AMIGAHUNK_MAX_ENTRIES UINT32_C(0x100000)

/* EXT sub-record kinds, as stored in the top byte of the type/length word. */
#define XX_AMIGAHUNK_EXT_SYMB   0x00U
#define XX_AMIGAHUNK_EXT_ABS    0x02U
#define XX_AMIGAHUNK_EXT_COMMON 0x82U

static void xx_amigahunk_vtable_destroy(Abstractformat *self);

/* --- Bounded primitive reads ------------------------------------------- */

static bool xx_amigahunk_read_u32(Abstractformat *self, int64_t offset,
                                  int64_t end, uint32_t *value) {
    if (!self || !value || offset < self->base_address || offset < 0 ||
        end < 4 || offset > end - 4) {
        return false;
    }
    *value = xx_io_get_u32(self->device, offset, true);
    return true;
}

static bool xx_amigahunk_read_u16(Abstractformat *self, int64_t offset,
                                  int64_t end, uint16_t *value) {
    if (!self || !value || offset < self->base_address || offset < 0 ||
        end < 2 || offset > end - 2) {
        return false;
    }
    *value = xx_io_get_u16(self->device, offset, true);
    return true;
}

/* Advance by count longwords, refusing anything that leaves the device. */
static bool xx_amigahunk_skip_longwords(int64_t *cursor, uint32_t count,
                                        int64_t end) {
    int64_t bytes;
    if (!cursor || *cursor < 0 || *cursor > end) return false;
    if (count > XX_AMIGAHUNK_MAX_ENTRIES) return false;
    bytes = (int64_t)count * 4;
    if (bytes > end - *cursor) return false;
    *cursor += bytes;
    return true;
}

static int64_t xx_amigahunk_align_up(int64_t value, int64_t alignment) {
    int64_t remainder;
    if (value < 0 || alignment <= 0) return value;
    remainder = value % alignment;
    if (remainder == 0) return value;
    if (alignment - remainder > INT64_MAX - value) return value;
    return value + (alignment - remainder);
}

/* --- Hunk classification ----------------------------------------------- */

static bool xx_amigahunk_is_loadable(uint32_t id) {
    return id == XX_AMIGAHUNK_HUNK_CODE || id == XX_AMIGAHUNK_HUNK_DATA ||
           id == XX_AMIGAHUNK_HUNK_BSS || id == XX_AMIGAHUNK_HUNK_PPC_CODE;
}

static bool xx_amigahunk_is_code(uint32_t id) {
    return id == XX_AMIGAHUNK_HUNK_CODE || id == XX_AMIGAHUNK_HUNK_PPC_CODE;
}

const char *xx_amigahunk_hunk_id_to_string(uint32_t hunk_id) {
    switch (hunk_id) {
        case XX_AMIGAHUNK_HUNK_UNIT: return "HUNK_UNIT";
        case XX_AMIGAHUNK_HUNK_NAME: return "HUNK_NAME";
        case XX_AMIGAHUNK_HUNK_CODE: return "HUNK_CODE";
        case XX_AMIGAHUNK_HUNK_DATA: return "HUNK_DATA";
        case XX_AMIGAHUNK_HUNK_BSS: return "HUNK_BSS";
        case XX_AMIGAHUNK_HUNK_RELOC32: return "HUNK_RELOC32";
        case XX_AMIGAHUNK_HUNK_RELOC16: return "HUNK_RELOC16";
        case XX_AMIGAHUNK_HUNK_RELOC8: return "HUNK_RELOC8";
        case XX_AMIGAHUNK_HUNK_EXT: return "HUNK_EXT";
        case XX_AMIGAHUNK_HUNK_SYMBOL: return "HUNK_SYMBOL";
        case XX_AMIGAHUNK_HUNK_DEBUG: return "HUNK_DEBUG";
        case XX_AMIGAHUNK_HUNK_END: return "HUNK_END";
        case XX_AMIGAHUNK_HUNK_HEADER: return "HUNK_HEADER";
        case XX_AMIGAHUNK_HUNK_OVERLAY: return "HUNK_OVERLAY";
        case XX_AMIGAHUNK_HUNK_BREAK: return "HUNK_BREAK";
        case XX_AMIGAHUNK_HUNK_DREL32: return "HUNK_DREL32";
        case XX_AMIGAHUNK_HUNK_DREL16: return "HUNK_DREL16";
        case XX_AMIGAHUNK_HUNK_DREL8: return "HUNK_DREL8";
        case XX_AMIGAHUNK_HUNK_LIB: return "HUNK_LIB";
        case XX_AMIGAHUNK_HUNK_INDEX: return "HUNK_INDEX";
        case XX_AMIGAHUNK_HUNK_RELOC32SHORT: return "HUNK_RELOC32SHORT";
        case XX_AMIGAHUNK_HUNK_RELRELOC32: return "HUNK_RELRELOC32";
        case XX_AMIGAHUNK_HUNK_ABSRELOC16: return "HUNK_ABSRELOC16";
        case XX_AMIGAHUNK_HUNK_DREL32EXE: return "HUNK_DREL32EXE";
        case XX_AMIGAHUNK_HUNK_PPC_CODE: return "HUNK_PPC_CODE";
        case XX_AMIGAHUNK_HUNK_RELRELOC26: return "HUNK_RELRELOC26";
        default: return "Unknown";
    }
}

/* --- Per-hunk body walkers --------------------------------------------- */

/* HUNK_HEADER: resident library name list, then table_size/first/last, then
 * one size longword per loaded hunk. A size whose two memory-attribute bits
 * are both set is followed by an explicit 32-bit memory-attribute longword. */
static bool xx_amigahunk_walk_header(Abstractformat *self, int64_t *cursor,
                                     int64_t end, xx_amigahunk *amigahunk,
                                     xx_pd_struct *pd) {
    uint32_t name_longwords;
    uint32_t table_size;
    uint32_t first_hunk;
    uint32_t last_hunk;
    uint32_t entries;
    uint32_t index;
    uint32_t strings = 0;

    for (;;) {
        if (xx_pd_is_stopped(pd)) return false;
        if (!xx_amigahunk_read_u32(self, *cursor, end, &name_longwords)) {
            return false;
        }
        *cursor += 4;
        strings += 1U;
        if (name_longwords == 0U) break;
        if (!xx_amigahunk_skip_longwords(cursor, name_longwords, end)) {
            return false;
        }
        strings += name_longwords;
        if (strings > XX_AMIGAHUNK_MAX_ENTRIES) return false;
    }

    if (!xx_amigahunk_read_u32(self, *cursor, end, &table_size) ||
        !xx_amigahunk_read_u32(self, *cursor + 4, end, &first_hunk) ||
        !xx_amigahunk_read_u32(self, *cursor + 8, end, &last_hunk)) {
        return false;
    }
    *cursor += 12;

    if (last_hunk < first_hunk) return false;
    entries = last_hunk - first_hunk + 1U;
    if (entries > XX_AMIGAHUNK_MAX_HUNKS) return false;

    if (amigahunk) {
        amigahunk->strings_size = strings;
        amigahunk->table_size = table_size;
        amigahunk->first_hunk = first_hunk;
        amigahunk->last_hunk = last_hunk;
        amigahunk->size_table_offset = *cursor;
        amigahunk->size_table_count = entries;
    }

    for (index = 0U; index < entries; ++index) {
        uint32_t hunk_size;
        if (xx_pd_is_stopped(pd)) return false;
        if (!xx_amigahunk_read_u32(self, *cursor, end, &hunk_size)) {
            return false;
        }
        *cursor += 4;
        if ((hunk_size >> 30) == 3U) {
            /* MEMF_MEMFLAGS: an explicit attribute longword follows. */
            if (*cursor > end - 4) return false;
            *cursor += 4;
        }
    }
    return true;
}

/* HUNK_CODE / HUNK_DATA / HUNK_PPC_CODE / HUNK_DEBUG / HUNK_UNIT /
 * HUNK_NAME: one longword count followed by that many longwords. */
static bool xx_amigahunk_walk_counted(Abstractformat *self, int64_t *cursor,
                                      int64_t end) {
    uint32_t longwords;
    if (!xx_amigahunk_read_u32(self, *cursor, end, &longwords)) return false;
    *cursor += 4;
    return xx_amigahunk_skip_longwords(cursor, longwords, end);
}

/* HUNK_RELOC8/16/32 and HUNK_DREL8/16/32: repeated
 * <count><hunk number><count offsets>, terminated by a zero count. */
static bool xx_amigahunk_walk_reloc_long(Abstractformat *self, int64_t *cursor,
                                         int64_t end, xx_pd_struct *pd) {
    uint32_t guard;
    for (guard = 0U; guard < XX_AMIGAHUNK_MAX_ENTRIES; ++guard) {
        uint32_t count;
        if (xx_pd_is_stopped(pd)) return false;
        if (!xx_amigahunk_read_u32(self, *cursor, end, &count)) return false;
        *cursor += 4;
        if (count == 0U) return true;
        if (*cursor > end - 4) return false;
        *cursor += 4; /* hunk number the offsets refer to */
        if (!xx_amigahunk_skip_longwords(cursor, count, end)) return false;
    }
    return false;
}

/* HUNK_RELOC32SHORT / HUNK_DREL32EXE: the same list built from 16-bit
 * words, terminated by a zero count word and padded to a longword. */
static bool xx_amigahunk_walk_reloc_short(Abstractformat *self, int64_t *cursor,
                                          int64_t end, int64_t start,
                                          xx_pd_struct *pd) {
    uint32_t guard;
    for (guard = 0U; guard < XX_AMIGAHUNK_MAX_ENTRIES; ++guard) {
        uint16_t count;
        int64_t bytes;
        if (xx_pd_is_stopped(pd)) return false;
        if (!xx_amigahunk_read_u16(self, *cursor, end, &count)) return false;
        *cursor += 2;
        if (count == 0U) {
            int64_t aligned = start + xx_amigahunk_align_up(*cursor - start, 4);
            if (aligned > end) return false;
            *cursor = aligned;
            return true;
        }
        if (*cursor > end - 2) return false;
        *cursor += 2; /* hunk number the offsets refer to */
        bytes = (int64_t)count * 2;
        if (bytes > end - *cursor) return false;
        *cursor += bytes;
    }
    return false;
}

/* HUNK_SYMBOL: repeated <name length><name><value>, zero length ends it. */
static bool xx_amigahunk_walk_symbol(Abstractformat *self, int64_t *cursor,
                                     int64_t end, xx_pd_struct *pd) {
    uint32_t guard;
    for (guard = 0U; guard < XX_AMIGAHUNK_MAX_ENTRIES; ++guard) {
        uint32_t name_longwords;
        if (xx_pd_is_stopped(pd)) return false;
        if (!xx_amigahunk_read_u32(self, *cursor, end, &name_longwords)) {
            return false;
        }
        *cursor += 4;
        if (name_longwords == 0U) return true;
        if (!xx_amigahunk_skip_longwords(cursor, name_longwords, end)) {
            return false;
        }
        if (*cursor > end - 4) return false;
        *cursor += 4; /* symbol value */
    }
    return false;
}

/* HUNK_EXT: repeated <type:8|name length:24><name><payload>, zero ends it.
 * Ported from XAmigaHunk::_getHunkSize. */
static bool xx_amigahunk_walk_ext(Abstractformat *self, int64_t *cursor,
                                  int64_t end, xx_pd_struct *pd) {
    uint32_t guard;
    for (guard = 0U; guard < XX_AMIGAHUNK_MAX_ENTRIES; ++guard) {
        uint32_t type_length;
        uint32_t name_longwords;
        uint32_t references;
        uint8_t type;
        if (xx_pd_is_stopped(pd)) return false;
        if (!xx_amigahunk_read_u32(self, *cursor, end, &type_length)) {
            return false;
        }
        *cursor += 4;
        if (type_length == 0U) return true;

        type = (uint8_t)(type_length >> 24);
        name_longwords = type_length & UINT32_C(0x00FFFFFF);
        if (!xx_amigahunk_skip_longwords(cursor, name_longwords, end)) {
            return false;
        }

        if (type <= 3U) {
            /* EXT_SYMB / EXT_DEF / EXT_ABS / EXT_RES: a single value. */
            if (*cursor > end - 4) return false;
            *cursor += 4;
        } else if (type >= 0x80U) {
            /* EXT_REF* / EXT_COMMON: a reference list. */
            if (type == XX_AMIGAHUNK_EXT_COMMON) {
                if (*cursor > end - 4) return false;
                *cursor += 4; /* common block size */
            }
            if (!xx_amigahunk_read_u32(self, *cursor, end, &references)) {
                return false;
            }
            *cursor += 4;
            if (!xx_amigahunk_skip_longwords(cursor, references, end)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return false;
}

/**
 * Size of the hunk starting at offset, leading id longword included.
 * Returns 0 for an unparsable or out-of-bounds hunk, which ends the walk.
 * amigahunk may be NULL; when supplied, HUNK_HEADER fields are recorded.
 */
static int64_t xx_amigahunk_hunk_size(Abstractformat *self, int64_t offset,
                                      int64_t end, uint32_t *out_id,
                                      uint32_t *out_raw_id,
                                      xx_amigahunk *amigahunk,
                                      xx_pd_struct *pd) {
    int64_t cursor = offset;
    uint32_t raw_id;
    uint32_t id;
    bool ok;

    if (!xx_amigahunk_read_u32(self, cursor, end, &raw_id)) return 0;
    id = raw_id & XX_AMIGAHUNK_ID_MASK;
    cursor += 4;

    switch (id) {
        case XX_AMIGAHUNK_HUNK_HEADER:
            ok = xx_amigahunk_walk_header(self, &cursor, end, amigahunk, pd);
            break;
        case XX_AMIGAHUNK_HUNK_CODE:
        case XX_AMIGAHUNK_HUNK_DATA:
        case XX_AMIGAHUNK_HUNK_PPC_CODE:
        case XX_AMIGAHUNK_HUNK_DEBUG:
        case XX_AMIGAHUNK_HUNK_UNIT:
        case XX_AMIGAHUNK_HUNK_NAME:
            ok = xx_amigahunk_walk_counted(self, &cursor, end);
            break;
        case XX_AMIGAHUNK_HUNK_BSS:
            /* Length only; BSS occupies no file bytes. */
            ok = cursor <= end - 4;
            if (ok) cursor += 4;
            break;
        case XX_AMIGAHUNK_HUNK_RELOC32:
        case XX_AMIGAHUNK_HUNK_RELOC16:
        case XX_AMIGAHUNK_HUNK_RELOC8:
        case XX_AMIGAHUNK_HUNK_DREL32:
        case XX_AMIGAHUNK_HUNK_DREL16:
        case XX_AMIGAHUNK_HUNK_DREL8:
            ok = xx_amigahunk_walk_reloc_long(self, &cursor, end, pd);
            break;
        case XX_AMIGAHUNK_HUNK_RELOC32SHORT:
        case XX_AMIGAHUNK_HUNK_DREL32EXE:
            ok = xx_amigahunk_walk_reloc_short(self, &cursor, end, offset, pd);
            break;
        case XX_AMIGAHUNK_HUNK_SYMBOL:
            ok = xx_amigahunk_walk_symbol(self, &cursor, end, pd);
            break;
        case XX_AMIGAHUNK_HUNK_EXT:
            ok = xx_amigahunk_walk_ext(self, &cursor, end, pd);
            break;
        case XX_AMIGAHUNK_HUNK_END:
            ok = true;
            break;
        default:
            /* HUNK_OVERLAY, HUNK_BREAK, HUNK_LIB, HUNK_INDEX and the
             * remaining relocation dialects are not walked; the reference
             * stops here too. */
            ok = false;
            break;
    }

    if (!ok || cursor <= offset || cursor > end) return 0;
    if (out_id) *out_id = id;
    if (out_raw_id) *out_raw_id = raw_id;
    return cursor - offset;
}

/* --- Hunk table -------------------------------------------------------- */

static bool xx_amigahunk_append_hunk(xx_amigahunk *amigahunk,
                                     const xx_amigahunk_hunk *hunk,
                                     uint32_t *capacity) {
    if (amigahunk->hunk_count >= *capacity) {
        uint32_t next = *capacity ? (*capacity * 2U) : 16U;
        xx_amigahunk_hunk *grown;
        if (next > XX_AMIGAHUNK_MAX_HUNKS) next = XX_AMIGAHUNK_MAX_HUNKS;
        if (next <= amigahunk->hunk_count) return false;
        grown = (xx_amigahunk_hunk *)xx_mem_realloc(
            amigahunk->hunks, (size_t)next * sizeof(xx_amigahunk_hunk));
        if (!grown) return false;
        amigahunk->hunks = grown;
        *capacity = next;
    }
    amigahunk->hunks[amigahunk->hunk_count] = *hunk;
    amigahunk->hunk_count += 1U;
    return true;
}

/* Walk every hunk from base_address onwards, filling the hunk table. */
static bool xx_amigahunk_parse(xx_amigahunk *amigahunk, int64_t end,
                               int64_t *format_size, xx_pd_struct *pd) {
    Abstractformat *self;
    int64_t cursor;
    int64_t last_end = 0;
    int64_t last_end_hunk = 0;
    uint32_t capacity = 0U;

    if (!amigahunk || !format_size) return false;
    self = &amigahunk->format;
    cursor = self->base_address;

    while (cursor < end) {
        xx_amigahunk_hunk hunk;
        int64_t size;
        uint32_t id = 0U;
        uint32_t raw_id = 0U;

        if (xx_pd_is_stopped(pd)) return false;
        size = xx_amigahunk_hunk_size(self, cursor, end, &id, &raw_id,
                                      amigahunk, pd);
        if (size <= 0) break;

        hunk.id = id;
        hunk.raw_id = raw_id;
        hunk.offset = cursor;
        hunk.size = size;
        if (!xx_amigahunk_append_hunk(amigahunk, &hunk, &capacity)) {
            return false;
        }

        if (xx_amigahunk_is_loadable(id)) amigahunk->loadable_count += 1U;
        if (id == XX_AMIGAHUNK_HUNK_PPC_CODE) amigahunk->has_ppc_code = true;
        if (id == XX_AMIGAHUNK_HUNK_RELOC32) amigahunk->has_reloc32 = true;

        cursor += size;
        last_end = cursor;
        if (id == XX_AMIGAHUNK_HUNK_END) last_end_hunk = cursor;
        if (amigahunk->hunk_count >= XX_AMIGAHUNK_MAX_HUNKS) break;
    }

    if (amigahunk->hunk_count == 0U) return false;

    /* The reference measures the format by the last HUNK_END. Files that
     * stop short of one still get their walked extent rather than zero. */
    *format_size = (last_end_hunk > 0 ? last_end_hunk : last_end) -
                   self->base_address;
    return *format_size > 0;
}

/* --- Lifecycle --------------------------------------------------------- */

void xx_amigahunk_init(xx_amigahunk *amigahunk, xx_io_device *dev,
                       int64_t base_address) {
    if (!amigahunk) return;
    xx_mem_zero(amigahunk, sizeof(xx_amigahunk));

    xx_format_init(&amigahunk->format, dev, base_address);

    amigahunk->format.endian = XX_ENDIAN_BIG;
    amigahunk->format.file_type = XX_AMIGAHUNK_FILE_TYPE;
    amigahunk->format.os = XX_OS_GENERIC;
    amigahunk->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    amigahunk->format.arch = XX_ARCH_M68K;
    amigahunk->format.is_executable = true;
    amigahunk->format.is_archive = false;
    xx_format_set_mime_type(&amigahunk->format, "application/x-amiga-hunk");
    xx_format_set_extension(&amigahunk->format, "");

    amigahunk->format.check_is_valid = xx_amigahunk_check_is_valid;
    amigahunk->format.handle_base_info = xx_amigahunk_handle_base_info;
    amigahunk->format.get_format_size = xx_amigahunk_get_format_size;
    amigahunk->format.get_memory_map = xx_amigahunk_get_memory_map;
    amigahunk->format.destroy = xx_amigahunk_vtable_destroy;

    amigahunk->size_table_offset = -1;
    amigahunk->hunks = NULL;
}

xx_amigahunk *xx_amigahunk_create(xx_io_device *dev, int64_t base_address) {
    xx_amigahunk *amigahunk =
        (xx_amigahunk *)xx_mem_alloc(sizeof(xx_amigahunk));
    if (!amigahunk) return NULL;
    xx_amigahunk_init(amigahunk, dev, base_address);
    return amigahunk;
}

void xx_amigahunk_destroy(xx_amigahunk *amigahunk) {
    if (!amigahunk) return;
    if (amigahunk->hunks) {
        xx_mem_free(amigahunk->hunks);
        amigahunk->hunks = NULL;
    }
    amigahunk->hunk_count = 0U;
    if (amigahunk->format.close) {
        amigahunk->format.close(&amigahunk->format);
    }
    xx_format_cleanup_extra_parameters(&amigahunk->format);
}

static void xx_amigahunk_vtable_destroy(Abstractformat *self) {
    if (self) {
        xx_amigahunk_destroy((xx_amigahunk *)self);
    }
}

void xx_amigahunk_free(xx_amigahunk *amigahunk) {
    if (!amigahunk) return;
    xx_amigahunk_destroy(amigahunk);
    xx_mem_free(amigahunk);
}

/* --- Format callbacks --------------------------------------------------- */

bool xx_amigahunk_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    int64_t total_size;
    uint32_t magic;

    if (!self || !self->device || self->base_address < 0 ||
        xx_pd_is_stopped(pd)) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        total_size - self->base_address < XX_AMIGAHUNK_MIN_SIZE) {
        return false;
    }

    /* The whole signature: a big-endian HUNK_HEADER or HUNK_UNIT id. The
     * memory-attribute bits are not masked here, matching the reference. */
    magic = xx_io_get_u32(self->device, self->base_address, true);
    return magic == XX_AMIGAHUNK_HUNK_HEADER ||
           magic == XX_AMIGAHUNK_HUNK_UNIT;
}

bool xx_amigahunk_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_amigahunk *amigahunk;
    int64_t total_size;
    int64_t available;
    int64_t end;
    int64_t format_size = 0;

    if (!self || !self->device || self->base_address < 0 ||
        xx_pd_is_stopped(pd)) {
        return false;
    }
    if (!xx_amigahunk_check_is_valid(self, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        xx_format_invalidate_memory_map(self);
        return false;
    }

    total_size = xx_io_total_size(self->device);
    available = total_size - self->base_address;
    end = total_size;

    amigahunk = (xx_amigahunk *)self;
    xx_format_invalidate_memory_map(self);
    if (amigahunk->hunks) {
        xx_mem_free(amigahunk->hunks);
        amigahunk->hunks = NULL;
    }
    amigahunk->hunk_count = 0U;
    amigahunk->loadable_count = 0U;
    amigahunk->has_ppc_code = false;
    amigahunk->has_reloc32 = false;
    amigahunk->strings_size = 0U;
    amigahunk->table_size = 0U;
    amigahunk->first_hunk = 0U;
    amigahunk->last_hunk = 0U;
    amigahunk->size_table_offset = -1;
    amigahunk->size_table_count = 0U;

    amigahunk->magic = xx_io_get_u32(self->device, self->base_address, true);
    amigahunk->is_object = amigahunk->magic == XX_AMIGAHUNK_HUNK_UNIT;

    if (!xx_amigahunk_parse(amigahunk, end, &format_size, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (format_size > available) format_size = available;

    self->file_type = XX_AMIGAHUNK_FILE_TYPE;
    self->endian = XX_ENDIAN_BIG;
    self->os = XX_OS_GENERIC;
    self->arch = amigahunk->has_ppc_code ? XX_ARCH_PPC : XX_ARCH_M68K;
    self->format_type = amigahunk->is_object ? XX_TYPE_OBJECT
                                             : XX_TYPE_CONSOLE_APPLICATION;
    self->is_executable = !amigahunk->is_object;
    xx_format_set_extension(self, amigahunk->is_object ? "o" : "");
    self->format_size = format_size;
    if (format_size < available) {
        self->overlay_offset = self->base_address + format_size;
        self->overlay_size = available - format_size;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }

    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_amigahunk_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    return self && (self->base_info_handled ||
                    xx_amigahunk_handle_base_info(self, pd))
               ? self->format_size
               : -1;
}

/* Segment view: HUNK_HEADER as the header, then every loadable hunk laid
 * out sequentially from IMAGE_BASE, each rounded up to 16 bytes. */
static bool xx_amigahunk_map_segments(xx_amigahunk *amigahunk,
                                      xx_memory_map *output,
                                      uint64_t module_address,
                                      xx_pd_struct *pd) {
    uint64_t address = module_address;
    uint32_t index;
    int32_t part_number = 0;

    for (index = 0U; index < amigahunk->hunk_count; ++index) {
        const xx_amigahunk_hunk *hunk = &amigahunk->hunks[index];
        uint32_t longwords = 0U;
        int64_t file_size;
        int64_t virtual_size;
        int64_t body_offset;

        if (xx_pd_is_stopped(pd)) return false;

        if (hunk->id == XX_AMIGAHUNK_HUNK_HEADER) {
            if (!xx_memory_map_add_part(output, hunk->offset, hunk->size,
                                        XX_INVALID_ADDRESS, 0,
                                        XX_FILE_PART_HEADER, part_number++,
                                        "HUNK_HEADER", false)) {
                return false;
            }
            continue;
        }
        if (!xx_amigahunk_is_loadable(hunk->id)) continue;
        if (hunk->size < 8) continue;

        longwords = xx_io_get_u32(amigahunk->format.device, hunk->offset + 4,
                                  true);
        body_offset = hunk->offset + 8;
        file_size = hunk->id == XX_AMIGAHUNK_HUNK_BSS ? 0 : hunk->size - 8;
        virtual_size = xx_amigahunk_align_up((int64_t)longwords * 4,
                                             XX_AMIGAHUNK_ALIGNMENT);
        if (virtual_size < file_size) virtual_size = file_size;
        if (address == XX_INVALID_ADDRESS ||
            (uint64_t)virtual_size >= XX_INVALID_ADDRESS - address) {
            return false;
        }

        if (!xx_memory_map_add_part(output, body_offset, file_size, address,
                                    virtual_size, XX_FILE_PART_SEGMENT,
                                    part_number++,
                                    xx_amigahunk_hunk_id_to_string(hunk->id),
                                    false)) {
            return false;
        }
        if (output->start_load_offset < 0 || body_offset <
                                                 output->start_load_offset) {
            output->start_load_offset = body_offset;
        }
        if (xx_amigahunk_is_code(hunk->id) && output->code_base < 0 &&
            address <= (uint64_t)INT64_MAX) {
            output->code_base = (int64_t)address;
            output->entry_point_address = address;
        }
        address += (uint64_t)virtual_size;
    }
    return true;
}

/* Region view: one record per hunk, named after its type. */
static bool xx_amigahunk_map_regions(xx_amigahunk *amigahunk,
                                     xx_memory_map *output,
                                     xx_pd_struct *pd) {
    uint32_t index;
    for (index = 0U; index < amigahunk->hunk_count; ++index) {
        const xx_amigahunk_hunk *hunk = &amigahunk->hunks[index];
        if (xx_pd_is_stopped(pd)) return false;
        if (!xx_memory_map_add_part(output, hunk->offset, hunk->size,
                                    XX_INVALID_ADDRESS, 0,
                                    XX_FILE_PART_REGION,
                                    index <= (uint32_t)INT32_MAX
                                        ? (int32_t)index
                                        : -1,
                                    xx_amigahunk_hunk_id_to_string(hunk->id),
                                    false)) {
            return false;
        }
    }
    return true;
}

bool xx_amigahunk_get_memory_map(Abstractformat *self,
                                 xx_memory_map_mode_t mode,
                                 xx_memory_map *output, xx_pd_struct *pd) {
    xx_amigahunk *amigahunk;
    int64_t total_size;
    int64_t binary_size;
    uint64_t module_address;
    bool success;

    if (!self || !output || !self->device || !self->base_info_handled ||
        self->base_address < 0 || xx_pd_is_stopped(pd)) {
        return false;
    }
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) {
        mode = XX_MEMORY_MAP_MODE_SEGMENTS;
    }
    if (mode != XX_MEMORY_MAP_MODE_SEGMENTS &&
        mode != XX_MEMORY_MAP_MODE_REGIONS) {
        return false;
    }

    amigahunk = (xx_amigahunk *)self;
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address) return false;
    binary_size = total_size - self->base_address;

    module_address = self->module_address != XX_INVALID_ADDRESS
                         ? self->module_address
                         : XX_AMIGAHUNK_IMAGE_BASE;

    output->binary_offset = self->base_address;
    output->module_address = module_address;
    output->is_image = self->is_mapped;
    output->binary_size = binary_size;
    output->entry_point_address = XX_INVALID_ADDRESS;
    output->code_base = -1;
    output->start_load_offset = -1;
    output->file_type = self->file_type;
    output->format_type = self->format_type;
    output->endian = self->endian;
    output->arch = self->arch;
    output->mode = mode;

    success = mode == XX_MEMORY_MAP_MODE_SEGMENTS
                  ? xx_amigahunk_map_segments(amigahunk, output,
                                              module_address, pd)
                  : xx_amigahunk_map_regions(amigahunk, output, pd);
    if (!success) return false;

    if (self->format_size < binary_size &&
        !xx_memory_map_add_part(output, self->base_address + self->format_size,
                                binary_size - self->format_size,
                                XX_INVALID_ADDRESS, 0, XX_FILE_PART_OVERLAY,
                                -1, "Overlay", false)) {
        return false;
    }
    return !xx_pd_is_stopped(pd) && xx_memory_map_finalize(output);
}

/* --- Getters ------------------------------------------------------------ */

uint32_t xx_amigahunk_get_magic(const xx_amigahunk *amigahunk) {
    return amigahunk ? amigahunk->magic : 0U;
}

uint32_t xx_amigahunk_get_strings_size(const xx_amigahunk *amigahunk) {
    return amigahunk ? amigahunk->strings_size : 0U;
}

uint32_t xx_amigahunk_get_table_size(const xx_amigahunk *amigahunk) {
    return amigahunk ? amigahunk->table_size : 0U;
}

uint32_t xx_amigahunk_get_first_hunk(const xx_amigahunk *amigahunk) {
    return amigahunk ? amigahunk->first_hunk : 0U;
}

uint32_t xx_amigahunk_get_last_hunk(const xx_amigahunk *amigahunk) {
    return amigahunk ? amigahunk->last_hunk : 0U;
}

int64_t xx_amigahunk_get_size_table_offset(const xx_amigahunk *amigahunk) {
    return amigahunk ? amigahunk->size_table_offset : -1;
}

uint32_t xx_amigahunk_get_size_table_count(const xx_amigahunk *amigahunk) {
    return amigahunk ? amigahunk->size_table_count : 0U;
}

uint32_t xx_amigahunk_get_number_of_hunks(const xx_amigahunk *amigahunk) {
    return amigahunk ? amigahunk->hunk_count : 0U;
}

const xx_amigahunk_hunk *xx_amigahunk_get_hunk(const xx_amigahunk *amigahunk,
                                               uint32_t index) {
    return amigahunk && amigahunk->hunks && index < amigahunk->hunk_count
               ? &amigahunk->hunks[index]
               : NULL;
}

bool xx_amigahunk_is_hunk_present(const xx_amigahunk *amigahunk,
                                  uint32_t hunk_id) {
    uint32_t index;
    if (!amigahunk || !amigahunk->hunks) return false;
    for (index = 0U; index < amigahunk->hunk_count; ++index) {
        if (amigahunk->hunks[index].id == hunk_id) return true;
    }
    return false;
}

bool xx_amigahunk_is_object(const xx_amigahunk *amigahunk) {
    return amigahunk ? amigahunk->is_object : false;
}

bool xx_amigahunk_has_ppc_code(const xx_amigahunk *amigahunk) {
    return amigahunk ? amigahunk->has_ppc_code : false;
}
