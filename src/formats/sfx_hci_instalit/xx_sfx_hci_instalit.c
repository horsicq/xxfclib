/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * HCI Instalit / "Shadow": the Win16 self-extracting installer and the data
 * volumes it installs from.  xx_sfx_hci_instalit.h carries the layout.
 *
 * The executable is never run or emulated.  The reader walks the NE resource
 * table for the custom "EXEFILE" resources (the installer engine) and reads
 * the 29-byte footer at the end of the file, whose pointers lead to the
 * obfuscated install script, the packed member data and the catalog.  A data
 * volume carries the same footer and catalog without the executable.
 *
 * Four member methods are known.  0xF0 is a plain PKWARE DCL implode stream
 * (xx_dcl).  0xC8 is stored (every 0xC8 record seen declares a packed size
 * equal to its unpacked size).  0xD0 and 0xE0 are Instalit's own pre-DCL
 * codecs; both decoders below were written from the stream data of the
 * known files (each stream decodes to the size and CRC its catalog record
 * declares), not from any other implementation.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_hci_instalit/xx_sfx_hci_instalit.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the reader is registered. */
#ifdef SFX_HCI_INSTALIT
#define XX_SFX_HCI_INSTALIT_FILE_TYPE XX_FILE_TYPE_SFX_HCI_INSTALIT
#else
#define XX_SFX_HCI_INSTALIT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SH_FOOTER_SIZE 29
#define SH_SERIAL_DIGITS 12
#define SH_FOOTER_TAG 0x0CU
#define SH_SCRIPT_TAG_SIZE 9
#define SH_BLOCK_TAG_SIZE 6
#define SH_SCRIPT_KEY 0x67U

#define SH_REC_TAG_NEW 0x1CU
#define SH_REC_TAG_OLD 0x1AU
#define SH_REC_SIZE_OLD 47U
#define SH_REC_SIZE_NEW 55U
#define SH_REC_SIZE_WIDE 59U
#define SH_REC_NAME_SIZE 13U

#define SH_METHOD_DCL 0xF0U
#define SH_METHOD_LZW 0xE0U
#define SH_METHOD_LZH 0xD0U
#define SH_METHOD_STORE 0xC8U

/* Hostile-input caps.  The known files hold a few hundred records and
 * members of a few MiB; these leave generous room while bounding every
 * allocation a crafted header can ask for. */
#define SH_MAX_RECORDS 65536U
#define SH_MAX_EXEFILES 256U
#define SH_MAX_RESOURCE_TYPES 1024U
#define SH_MAX_NAMETABLE (64U * 1024U)
#define SH_MAX_PACKED ((int64_t)64 * 1024 * 1024)
#define SH_MAX_UNPACKED ((int64_t)64 * 1024 * 1024)
#define SH_MAX_SCRIPT ((int64_t)16 * 1024 * 1024)
#define SH_MAX_NAME 64U

/* Window and codes of the 0xD0 codec. */
#define SH_LZH_WINDOW 8192U
#define SH_LZH_MAX_NODES 0x276U
#define SH_LZH_MAX_SYMBOL 314U

enum {
    SH_ENTRY_EXEFILE_DCL = 1,
    SH_ENTRY_EXEFILE_RAW,
    SH_ENTRY_SCRIPT,
    SH_ENTRY_MEMBER
};

enum { SH_CRC_NONE = 0, SH_CRC_32, SH_CRC_16 };

typedef struct sh_entry_s {
    char *name;
    int64_t header_offset; /* absolute device offsets */
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    int64_t unpacked_size; /* -1: not known */
    uint32_t crc;
    uint8_t kind;
    uint8_t method;
    uint8_t crc_kind;
    uint8_t attributes;
    uint16_t dos_date;
    uint16_t dos_time;
    bool has_dos_time;
} sh_entry;

typedef struct sh_layout_s {
    sh_entry *entries;
    size_t count;
    size_t capacity;
    uint32_t kind;
    int64_t size; /* bytes from base_address */
    bool has_footer;
    bool has_script;
    uint32_t exefile_count;
    uint32_t catalog_records;
    uint32_t record_size;
    uint32_t members_elsewhere;
} sh_layout;

typedef struct sh_stream_s {
    sh_layout layout;
    size_t index;
} sh_stream;

static void sh_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------------ */
/* Little helpers                                                            */
/* ------------------------------------------------------------------------ */

static uint16_t sh_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t sh_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static char sh_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* All positioning goes through seek64: long is 32-bit on Win64. */
static bool sh_read_at(xx_io_device *device, int64_t offset, void *data,
                       size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0) return false;
    if (size == 0U) return true;
    if (xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* True when [offset, offset + size) lies inside [0, limit). */
static bool sh_within(int64_t limit, int64_t offset, int64_t size) {
    return limit >= 0 && offset >= 0 && size >= 0 && offset <= limit &&
           size <= limit - offset;
}

/* Read [base + offset, + size) into a fresh buffer; size is capped by the
 * caller and checked against the image here. */
static uint8_t *sh_read_block(Abstractformat *self, int64_t image_size,
                              int64_t offset, int64_t size) {
    uint8_t *buffer;
    if (!sh_within(image_size, offset, size) || size > SH_MAX_PACKED) {
        return NULL;
    }
    buffer = (uint8_t *)xx_mem_alloc(size ? (size_t)size : 1U);
    if (!buffer) return NULL;
    if (!sh_read_at(self->device, self->base_address + offset, buffer,
                    (size_t)size)) {
        xx_mem_free(buffer);
        return NULL;
    }
    return buffer;
}

/* ------------------------------------------------------------------------ */
/* LSB-first bit reader shared by the two Instalit codecs                    */
/* ------------------------------------------------------------------------ */

typedef struct sh_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint64_t buffer;
    unsigned count;
} sh_bits;

static void sh_bits_init(sh_bits *bits, const uint8_t *data, size_t size) {
    bits->data = data;
    bits->size = size;
    bits->position = 0U;
    bits->buffer = 0U;
    bits->count = 0U;
}

static void sh_bits_fill(sh_bits *bits, unsigned want) {
    while (bits->count < want && bits->position < bits->size) {
        bits->buffer |= (uint64_t)bits->data[bits->position++] << bits->count;
        bits->count += 8U;
    }
}

static bool sh_bits_get(sh_bits *bits, unsigned n, uint32_t *value) {
    if (n == 0U) {
        *value = 0U;
        return true;
    }
    if (n > 32U) return false;
    sh_bits_fill(bits, n);
    if (bits->count < n) return false;
    *value = (uint32_t)(bits->buffer & ((UINT64_C(1) << n) - 1U));
    bits->buffer >>= n;
    bits->count -= n;
    return true;
}

/* The next n bits without consuming them, zero padded past the end. */
static uint32_t sh_bits_peek(sh_bits *bits, unsigned n) {
    sh_bits_fill(bits, n);
    return (uint32_t)(bits->buffer & ((UINT64_C(1) << n) - 1U));
}

static size_t sh_bits_consumed(const sh_bits *bits) {
    size_t buffered = bits->count / 8U;
    return bits->position > buffered ? bits->position - buffered : 0U;
}

/* ------------------------------------------------------------------------ */
/* Method 0xD0: static-tree LZH                                              */
/* ------------------------------------------------------------------------ */

/* Match-position prefix code: 64 symbols with lengths 3, 4x3, 5x8, 6x12,
 * 7x24, 8x16.  The canonical code of each length is taken, and within one
 * length the symbols are handed out in the order of the codes' bit-reversed
 * values.  The table maps the next eight stream bits (first bit lowest) to
 * a symbol; sh_lzh_length gives how many of them it uses. */
static uint8_t sh_lzh_length(unsigned symbol) {
    if (symbol < 1U) return 3U;
    if (symbol < 4U) return 4U;
    if (symbol < 12U) return 5U;
    if (symbol < 24U) return 6U;
    if (symbol < 48U) return 7U;
    return 8U;
}

static unsigned sh_reverse(unsigned value, unsigned width) {
    unsigned result = 0U, index;
    for (index = 0U; index < width; ++index) {
        result = (result << 1U) | ((value >> index) & 1U);
    }
    return result;
}

static void sh_lzh_build_table(uint8_t table[256]) {
    unsigned code = 0U, previous = 0U, symbol = 0U;
    while (symbol < 64U) {
        unsigned length = sh_lzh_length(symbol);
        unsigned first = symbol, count = 0U, index, fill;
        unsigned reversed[48];
        code <<= (length - previous);
        previous = length;
        while (symbol < 64U && sh_lzh_length(symbol) == length) {
            reversed[count++] = sh_reverse(code, length);
            ++code;
            ++symbol;
        }
        /* Insertion sort: at most 24 values. */
        for (index = 1U; index < count; ++index) {
            unsigned value = reversed[index], at = index;
            while (at > 0U && reversed[at - 1U] > value) {
                reversed[at] = reversed[at - 1U];
                --at;
            }
            reversed[at] = value;
        }
        for (index = 0U; index < count; ++index) {
            for (fill = 0U; fill < (1U << (8U - length)); ++fill) {
                table[reversed[index] | (fill << length)] =
                    (uint8_t)(first + index);
            }
        }
    }
}

bool xx_sfx_hci_instalit_lzh_decode_memory(const uint8_t *input,
                                           size_t input_size,
                                           uint8_t *output,
                                           size_t output_size,
                                           size_t *consumed) {
    uint16_t tree[SH_LZH_MAX_NODES];
    uint8_t table[256];
    uint8_t window[SH_LZH_WINDOW];
    sh_bits bits;
    uint32_t nodes, width, value;
    size_t produced = 0U;
    unsigned position = 0U;
    bool wrapped = false;
    uint32_t index;

    if (consumed) *consumed = 0U;
    if (!input || (!output && output_size != 0U)) return false;
    sh_bits_init(&bits, input, input_size);
    if (!sh_bits_get(&bits, 16U, &nodes) || nodes < 2U ||
        nodes >= SH_LZH_MAX_NODES || !sh_bits_get(&bits, 8U, &width) ||
        width < 1U || width > 10U) {
        return false;
    }
    for (index = 0U; index < nodes; ++index) {
        if (!sh_bits_get(&bits, width, &value)) return false;
        tree[index] = (uint16_t)(value >= nodes ? ((value - nodes) | 0x8000U)
                                                : value);
    }
    sh_lzh_build_table(table);
    xx_rt_memset(window, ' ', sizeof(window));

    for (;;) {
        uint32_t node = nodes - 2U, bit, symbol, steps = 0U;
        while ((node & 0x8000U) == 0U) {
            if (!sh_bits_get(&bits, 1U, &bit)) return false;
            node += bit;
            /* A walk longer than the table has to cycle. */
            if (node >= nodes || ++steps > nodes) return false;
            node = tree[node];
        }
        symbol = node & 0x7FFFU;
        if (symbol < 256U) {
            if (produced >= output_size) return false;
            output[produced++] = (uint8_t)symbol;
            window[position] = (uint8_t)symbol;
            position = (position + 1U) & (SH_LZH_WINDOW - 1U);
            if (position == 0U) wrapped = true;
            continue;
        }
        if (symbol == 256U) break;
        if (symbol > SH_LZH_MAX_SYMBOL) return false;
        {
            unsigned length = symbol - 254U, high, low_bits, source;
            uint32_t low;
            high = table[sh_bits_peek(&bits, 8U)];
            if (!sh_bits_get(&bits, sh_lzh_length(high), &low)) return false;
            if (wrapped || position + 0x3CU > SH_LZH_WINDOW - 1U) {
                low_bits = 7U;
            } else {
                unsigned grown = (position + 0x3CU) >> 5U;
                low_bits = 0U;
                while (grown > 1U) {
                    ++low_bits;
                    grown >>= 1U;
                }
            }
            if (!sh_bits_get(&bits, low_bits, &low)) return false;
            source = (position - ((high << low_bits) + low + 1U)) &
                     (SH_LZH_WINDOW - 1U);
            if (length > output_size - produced) return false;
            while (length-- > 0U) {
                uint8_t byte = window[source];
                source = (source + 1U) & (SH_LZH_WINDOW - 1U);
                output[produced++] = byte;
                window[position] = byte;
                position = (position + 1U) & (SH_LZH_WINDOW - 1U);
                if (position == 0U) wrapped = true;
            }
        }
    }
    if (produced != output_size) return false;
    if (consumed) *consumed = sh_bits_consumed(&bits);
    return true;
}

/* ------------------------------------------------------------------------ */
/* Method 0xE0: LZW over escaped literals, then 0x90 run-length expansion    */
/* ------------------------------------------------------------------------ */

typedef struct sh_rle_s {
    uint8_t *output;
    size_t size;
    size_t produced;
    int last;
    bool escape;
} sh_rle;

static bool sh_rle_emit(sh_rle *rle, uint8_t byte, size_t repeat) {
    if (repeat > rle->size - rle->produced) return false;
    while (repeat-- > 0U) rle->output[rle->produced++] = byte;
    return true;
}

static bool sh_rle_push(sh_rle *rle, uint8_t byte) {
    if (rle->escape) {
        rle->escape = false;
        if (byte == 0U) {
            rle->last = 0x90;
            return sh_rle_emit(rle, 0x90U, 1U);
        }
        if (rle->last < 0) return false;
        return sh_rle_emit(rle, (uint8_t)rle->last, (size_t)byte - 1U);
    }
    if (byte == 0x90U) {
        rle->escape = true;
        return true;
    }
    rle->last = byte;
    return sh_rle_emit(rle, byte, 1U);
}

static bool sh_grow(void **buffer, size_t *capacity, size_t needed,
                    size_t element, size_t limit) {
    size_t next;
    void *grown;
    if (needed <= *capacity) return true;
    if (needed > limit) return false;
    next = *capacity ? *capacity : 1024U;
    while (next < needed) next = next > limit / 2U ? limit : next * 2U;
    if (next > SIZE_MAX / element) return false;
    grown = xx_mem_realloc(*buffer, next * element);
    if (!grown) return false;
    *buffer = grown;
    *capacity = next;
    return true;
}

bool xx_sfx_hci_instalit_lzw_decode_memory(const uint8_t *input,
                                           size_t input_size,
                                           uint8_t *output,
                                           size_t output_size,
                                           size_t *consumed) {
    sh_bits bits;
    sh_rle rle;
    uint8_t *history = NULL;
    uint32_t *starts = NULL;
    size_t history_size = 0U, history_capacity = 0U;
    size_t tokens = 0U, token_capacity = 0U;
    size_t history_limit;
    bool result = false;

    if (consumed) *consumed = 0U;
    if (!input || (!output && output_size != 0U) ||
        (int64_t)output_size > SH_MAX_UNPACKED) {
        return false;
    }
    /* Before run-length expansion a stream is at most twice its output
     * (0x90 0x00 per byte); a stream that keeps growing past that without
     * output is refused. */
    history_limit = output_size * 2U + 16U;
    sh_bits_init(&bits, input, input_size);
    xx_mem_zero(&rle, sizeof(rle));
    rle.output = output;
    rle.size = output_size;
    rle.last = -1;

    for (;;) {
        uint32_t flag, value;
        size_t start = history_size, length, index;
        if (!sh_bits_get(&bits, 1U, &flag)) goto done;
        if (flag == 0U) {
            if (!sh_bits_get(&bits, 8U, &value) ||
                !sh_grow((void **)&history, &history_capacity,
                         history_size + 1U, 1U, history_limit)) {
                goto done;
            }
            history[history_size++] = (uint8_t)value;
        } else {
            unsigned width = 0U;
            size_t highest = tokens > 0U ? tokens - 1U : 0U;
            size_t source;
            while (highest != 0U) {
                ++width;
                highest >>= 1U;
            }
            if (width == 0U) width = 1U;
            if (width > 31U || !sh_bits_get(&bits, width, &value)) goto done;
            if (value == 0U) break;
            if ((size_t)value > tokens) goto done;
            source = starts[value - 1U];
            /* Entry v is token v-1 plus the first byte of token v; tokens
             * are contiguous, so that is one run of the history.  v equal
             * to the token count is the usual LZW case of the entry being
             * defined by this very token. */
            length = ((size_t)value < tokens ? starts[value]
                                             : history_size) -
                     source + 1U;
            if (!sh_grow((void **)&history, &history_capacity,
                         history_size + length, 1U, history_limit)) {
                goto done;
            }
            for (index = 0U; index < length; ++index) {
                history[history_size + index] = history[source + index];
            }
            history_size += length;
        }
        if (!sh_grow((void **)&starts, &token_capacity, tokens + 1U,
                     sizeof(uint32_t), history_limit) ||
            start > UINT32_MAX) {
            goto done;
        }
        starts[tokens++] = (uint32_t)start;
        for (index = start; index < history_size; ++index) {
            if (!sh_rle_push(&rle, history[index])) goto done;
        }
    }
    if (rle.escape || rle.produced != output_size) goto done;
    if (consumed) *consumed = sh_bits_consumed(&bits);
    result = true;
done:
    if (history) xx_mem_free(history);
    if (starts) xx_mem_free(starts);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Layout bookkeeping                                                        */
/* ------------------------------------------------------------------------ */

static void sh_layout_cleanup(sh_layout *layout) {
    size_t index;
    if (!layout) return;
    for (index = 0U; index < layout->count; ++index) {
        if (layout->entries[index].name) {
            xx_str_free(layout->entries[index].name);
        }
    }
    if (layout->entries) xx_mem_free(layout->entries);
    xx_mem_zero(layout, sizeof(*layout));
}

static sh_entry *sh_layout_add(sh_layout *layout) {
    sh_entry *entry;
    if (layout->count >= SH_MAX_RECORDS + SH_MAX_EXEFILES + 1U) return NULL;
    if (!sh_grow((void **)&layout->entries, &layout->capacity,
                 layout->count + 1U, sizeof(sh_entry),
                 SH_MAX_RECORDS + SH_MAX_EXEFILES + 1U)) {
        return NULL;
    }
    entry = &layout->entries[layout->count++];
    xx_mem_zero(entry, sizeof(*entry));
    entry->unpacked_size = -1;
    return entry;
}

/* ------------------------------------------------------------------------ */
/* Member names                                                              */
/* ------------------------------------------------------------------------ */

static bool sh_stem_is(const char *name, size_t stem, const char *device) {
    size_t index;
    for (index = 0U; index < stem; ++index) {
        if (!device[index] || sh_upper(name[index]) != device[index]) {
            return false;
        }
    }
    return device[stem] == '\0';
}

/* A single path component that is safe to create on any host: printable
 * ASCII without separators or wildcards, not only dots and spaces, not
 * ending in a dot or space, and not a DOS device name. */
static bool sh_safe_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t length, stem = 0U, index;
    bool meaningful = false;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    if (length > SH_MAX_NAME) return false;
    for (index = 0U; index < length; ++index) {
        char c = name[index];
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU ||
            c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*') {
            return false;
        }
        if (c != '.' && c != ' ') meaningful = true;
    }
    /* Windows drops a trailing dot or space, so "A." would land on "A". */
    if (!meaningful || name[length - 1U] == '.' || name[length - 1U] == ' ') {
        return false;
    }
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        if (sh_stem_is(name, stem, devices[index])) return false;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((sh_upper(name[0]) == 'C' && sh_upper(name[1]) == 'O' &&
          sh_upper(name[2]) == 'M') ||
         (sh_upper(name[0]) == 'L' && sh_upper(name[1]) == 'P' &&
          sh_upper(name[2]) == 'T'))) {
        return false;
    }
    return true;
}

/* Take the stored name when it is safe, otherwise a generated one. */
static char *sh_make_name(const char *stored, const char *fallback_prefix,
                          size_t ordinal) {
    char buffer[32];
    if (stored && sh_safe_name(stored)) return xx_str_dup(stored);
    (void)xx_rt_snprintf(buffer, sizeof(buffer), "%s%u", fallback_prefix,
                         (unsigned)ordinal);
    return xx_str_dup(buffer);
}

static bool sh_same_name(const char *left, const char *right) {
    while (*left && *right) {
        if (sh_upper(*left) != sh_upper(*right)) return false;
        ++left;
        ++right;
    }
    return *left == *right;
}

static bool sh_name_taken(const sh_layout *layout, size_t upto,
                          const char *name) {
    size_t index;
    for (index = 0U; index < upto; ++index) {
        if (sh_same_name(layout->entries[index].name, name)) return true;
    }
    return false;
}

/* Catalogs repeat names (the same file for two target directories), and a
 * case-insensitive host would let the second overwrite the first.  A later
 * duplicate becomes "<stem>_<n><ext>" with the first free n. */
static bool sh_dedupe(sh_layout *layout) {
    size_t index;
    for (index = 1U; index < layout->count; ++index) {
        char *name = layout->entries[index].name;
        size_t length, stem, attempt;
        if (!sh_name_taken(layout, index, name)) continue;
        length = xx_str_len(name);
        stem = length;
        while (stem > 0U && name[stem - 1U] != '.') --stem;
        stem = stem > 0U ? stem - 1U : length;
        for (attempt = 2U; attempt < SH_MAX_RECORDS + 64U; ++attempt) {
            char candidate[SH_MAX_NAME + 32U];
            char head[SH_MAX_NAME + 1U];
            if (stem > SH_MAX_NAME) return false;
            xx_rt_memcpy(head, name, stem);
            head[stem] = '\0';
            (void)xx_rt_snprintf(candidate, sizeof(candidate), "%s_%u%s", head,
                                 (unsigned)attempt, name + stem);
            if (!sh_name_taken(layout, layout->count, candidate)) {
                char *copy = xx_str_dup(candidate);
                if (!copy) return false;
                xx_str_free(name);
                layout->entries[index].name = copy;
                break;
            }
        }
        if (attempt >= SH_MAX_RECORDS + 64U) return false;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* NE resources                                                              */
/* ------------------------------------------------------------------------ */

typedef struct sh_ne_s {
    int64_t ne_offset;
    int64_t table_offset;   /* resource table, relative to base */
    uint8_t *table;         /* resource table bytes */
    size_t table_size;
    unsigned shift;
    uint8_t *names;         /* RT_NAMETABLE data, concatenated */
    size_t names_size;
    int64_t extent;         /* end of the NE image as its tables describe it */
} sh_ne;

static void sh_ne_cleanup(sh_ne *ne) {
    if (ne->table) xx_mem_free(ne->table);
    if (ne->names) xx_mem_free(ne->names);
    xx_mem_zero(ne, sizeof(*ne));
}

/* Walk the type blocks; calls back through the loop in the callers below.
 * Returns the offset of the next type block or 0 at the end / on error. */
static bool sh_ne_type_at(const sh_ne *ne, size_t at, uint16_t *type_id,
                          uint16_t *count, size_t *rows) {
    if (at > ne->table_size || ne->table_size - at < 2U) return false;
    *type_id = sh_u16(ne->table + at);
    if (*type_id == 0U) {
        *count = 0U;
        *rows = at + 2U;
        return true;
    }
    if (ne->table_size - at < 8U) return false;
    *count = sh_u16(ne->table + at + 2U);
    *rows = at + 8U;
    if ((size_t)*count * 12U > ne->table_size - *rows) return false;
    return true;
}

/* A Pascal string inside the resource table. */
static bool sh_ne_pascal(const sh_ne *ne, uint16_t offset, char *out,
                         size_t out_size) {
    size_t length;
    if ((size_t)offset >= ne->table_size) return false;
    length = ne->table[offset];
    if (length == 0U || length >= out_size ||
        length > ne->table_size - offset - 1U) {
        return false;
    }
    xx_rt_memcpy(out, ne->table + offset + 1U, length);
    out[length] = '\0';
    return true;
}

static bool sh_is_exefile(const char *name) {
    return sh_same_name(name, "EXEFILE");
}

/* Look (type, id) up in the RT_NAMETABLE data.  want_type selects the type
 * name, otherwise the id name is returned. */
static bool sh_ne_nametable(const sh_ne *ne, uint16_t type_id, uint16_t id,
                            bool want_type, char *out, size_t out_size) {
    size_t at = 0U;
    unsigned guard = 0U;
    while (ne->names && at + 6U <= ne->names_size && guard++ < 65536U) {
        size_t size = sh_u16(ne->names + at), cursor, end, length;
        uint16_t entry_type, entry_id;
        const char *type_name, *id_name;
        if (size < 8U || size > ne->names_size - at) break;
        entry_type = (uint16_t)(sh_u16(ne->names + at + 2U) | 0x8000U);
        entry_id = sh_u16(ne->names + at + 4U);
        end = at + size;
        cursor = at + 6U;
        type_name = (const char *)ne->names + cursor;
        while (cursor < end && ne->names[cursor]) ++cursor;
        if (cursor >= end) break;
        ++cursor;
        id_name = (const char *)ne->names + cursor;
        while (cursor < end && ne->names[cursor]) ++cursor;
        if (cursor >= end) break;
        if (entry_type == (uint16_t)(type_id | 0x8000U) &&
            (want_type || (entry_id | 0x8000U) == (id | 0x8000U))) {
            const char *pick = want_type ? type_name : id_name;
            length = xx_str_len(pick);
            if (length > 0U && length < out_size) {
                xx_rt_memcpy(out, pick, length + 1U);
                return true;
            }
        }
        at = end;
    }
    return false;
}

/* Read the MZ and NE headers and the resource table, plus any RT_NAMETABLE
 * data.  With full set the image extent is measured too. */
static bool sh_ne_load(Abstractformat *self, int64_t image_size, sh_ne *ne,
                       bool full) {
    uint8_t mz[64], header[64];
    uint32_t lfanew;
    uint16_t table_rel, names_rel;
    size_t at;
    unsigned types = 0U;

    xx_mem_zero(ne, sizeof(*ne));
    if (image_size < 0x80 ||
        !sh_read_at(self->device, self->base_address, mz, sizeof(mz)) ||
        mz[0] != 'M' || mz[1] != 'Z') {
        return false;
    }
    lfanew = sh_u32(mz + 0x3C);
    if (lfanew < 0x40U || !sh_within(image_size, lfanew, 64) ||
        !sh_read_at(self->device, self->base_address + lfanew, header,
                    sizeof(header)) ||
        header[0] != 'N' || header[1] != 'E') {
        return false;
    }
    ne->ne_offset = lfanew;
    table_rel = sh_u16(header + 0x24);
    names_rel = sh_u16(header + 0x26);
    if (names_rel <= table_rel || names_rel - table_rel < 4U) return false;
    ne->table_offset = (int64_t)lfanew + table_rel;
    ne->table_size = (size_t)(names_rel - table_rel);
    if (!sh_within(image_size, ne->table_offset, (int64_t)ne->table_size)) {
        return false;
    }
    ne->table = (uint8_t *)xx_mem_alloc(ne->table_size);
    if (!ne->table ||
        !sh_read_at(self->device, self->base_address + ne->table_offset,
                    ne->table, ne->table_size)) {
        sh_ne_cleanup(ne);
        return false;
    }
    ne->shift = sh_u16(ne->table);
    if (ne->shift > 15U) {
        sh_ne_cleanup(ne);
        return false;
    }
    ne->extent = ne->table_offset + (int64_t)ne->table_size;

    /* Gather RT_NAMETABLE (type 15) data and the resource extent. */
    at = 2U;
    for (;;) {
        uint16_t type_id, count, row;
        size_t rows;
        if (!sh_ne_type_at(ne, at, &type_id, &count, &rows) ||
            ++types > SH_MAX_RESOURCE_TYPES) {
            sh_ne_cleanup(ne);
            return false;
        }
        if (type_id == 0U) break;
        for (row = 0U; row < count; ++row) {
            const uint8_t *entry = ne->table + rows + (size_t)row * 12U;
            int64_t offset = (int64_t)sh_u16(entry) << ne->shift;
            int64_t length = (int64_t)sh_u16(entry + 2U) << ne->shift;
            if (sh_within(image_size, offset, length) &&
                offset + length > ne->extent) {
                ne->extent = offset + length;
            }
            if (type_id == 0x800FU && length > 0 &&
                sh_within(image_size, offset, length) &&
                (size_t)length <= SH_MAX_NAMETABLE - ne->names_size) {
                uint8_t *grown = (uint8_t *)xx_mem_realloc(
                    ne->names, ne->names_size + (size_t)length);
                if (!grown) {
                    sh_ne_cleanup(ne);
                    return false;
                }
                ne->names = grown;
                if (!sh_read_at(self->device, self->base_address + offset,
                                ne->names + ne->names_size, (size_t)length)) {
                    sh_ne_cleanup(ne);
                    return false;
                }
                ne->names_size += (size_t)length;
            }
        }
        at = rows + (size_t)count * 12U;
    }

    if (full) {
        /* Segments (with their relocation blocks) and the non-resident
         * name table complete the extent of the stub. */
        uint16_t segments = sh_u16(header + 0x1C);
        uint16_t segment_rel = sh_u16(header + 0x22);
        unsigned align = sh_u16(header + 0x32);
        uint32_t nonres = sh_u32(header + 0x2C);
        uint16_t nonres_size = sh_u16(header + 0x20);
        uint16_t index;
        if (align == 0U) align = 9U;
        if (nonres != 0U && sh_within(image_size, nonres, nonres_size) &&
            (int64_t)nonres + nonres_size > ne->extent) {
            ne->extent = (int64_t)nonres + nonres_size;
        }
        if (align <= 15U && segments > 0U &&
            sh_within(image_size, (int64_t)lfanew + segment_rel,
                      (int64_t)segments * 8)) {
            for (index = 0U; index < segments; ++index) {
                uint8_t segment[8];
                int64_t offset, length, end;
                if (!sh_read_at(self->device,
                                self->base_address + lfanew + segment_rel +
                                    (int64_t)index * 8,
                                segment, sizeof(segment))) {
                    break;
                }
                if (sh_u16(segment) == 0U) continue;
                offset = (int64_t)sh_u16(segment) << align;
                length = sh_u16(segment + 2U) ? sh_u16(segment + 2U) : 0x10000;
                end = offset + length;
                if ((sh_u16(segment + 4U) & 0x0100U) != 0U &&
                    sh_within(image_size, end, 2)) {
                    uint8_t relocs[2];
                    if (sh_read_at(self->device, self->base_address + end,
                                   relocs, 2U)) {
                        end += 2 + (int64_t)sh_u16(relocs) * 8;
                    }
                }
                if (end <= image_size && end > ne->extent) ne->extent = end;
            }
        }
    }
    return true;
}

/* Find the EXEFILE resources.  With layout NULL this only reports whether
 * at least one plausible one exists (the detection probe). */
static bool sh_ne_exefiles(Abstractformat *self, int64_t image_size,
                           const sh_ne *ne, sh_layout *layout,
                           xx_pd_struct *pd) {
    size_t at = 2U;
    unsigned types = 0U, found = 0U;
    for (;;) {
        uint16_t type_id, count, row;
        size_t rows;
        char type_name[64];
        bool named;
        if (!sh_ne_type_at(ne, at, &type_id, &count, &rows) ||
            ++types > SH_MAX_RESOURCE_TYPES) {
            return false;
        }
        if (type_id == 0U) break;
        at = rows + (size_t)count * 12U;
        if ((type_id & 0x8000U) == 0U) {
            named = sh_ne_pascal(ne, type_id, type_name, sizeof(type_name));
        } else {
            named = sh_ne_nametable(ne, type_id, 0U, true, type_name,
                                    sizeof(type_name));
        }
        if (!named || !sh_is_exefile(type_name)) continue;
        for (row = 0U; row < count; ++row) {
            const uint8_t *info = ne->table + rows + (size_t)row * 12U;
            int64_t offset = (int64_t)sh_u16(info) << ne->shift;
            int64_t length = (int64_t)sh_u16(info + 2U) << ne->shift;
            uint16_t id = sh_u16(info + 6U);
            uint8_t head[10];
            bool dcl, raw;
            uint32_t packed = 0U;
            if (pd && xx_pd_is_stopped(pd)) return false;
            if (length < 10 || !sh_within(image_size, offset, length) ||
                !sh_read_at(self->device, self->base_address + offset, head,
                            sizeof(head))) {
                continue;
            }
            raw = head[0] == 'M' && head[1] == 'Z';
            packed = sh_u32(head + 4U);
            dcl = !raw && head[8] <= 1U && head[9] >= 4U && head[9] <= 6U &&
                  packed >= 3U && (int64_t)packed <= length - 8;
            if (!raw && !dcl) continue;
            ++found;
            if (!layout) return true;
            if (found > SH_MAX_EXEFILES) return false;
            {
                char stored[64];
                bool have_name;
                sh_entry *entry = sh_layout_add(layout);
                if (!entry) return false;
                if ((id & 0x8000U) == 0U) {
                    have_name = sh_ne_pascal(ne, id, stored, sizeof(stored));
                } else {
                    have_name = sh_ne_nametable(ne, type_id, id, false,
                                                stored, sizeof(stored));
                    if (!have_name) {
                        (void)xx_rt_snprintf(stored, sizeof(stored), "#%u",
                                             (unsigned)(id & 0x7FFFU));
                        have_name = true;
                    }
                }
                entry->name = sh_make_name(have_name ? stored : NULL,
                                           "EXEFILE_", found);
                if (!entry->name) return false;
                entry->header_offset = self->base_address + offset;
                if (raw) {
                    entry->kind = SH_ENTRY_EXEFILE_RAW;
                    entry->header_size = 0;
                    entry->data_offset = self->base_address + offset;
                    entry->packed_size = length;
                    entry->unpacked_size = length;
                    entry->method = 0U;
                } else {
                    entry->kind = SH_ENTRY_EXEFILE_DCL;
                    entry->header_size = 8;
                    entry->data_offset = self->base_address + offset + 8;
                    entry->packed_size = (int64_t)packed;
                    entry->crc = sh_u32(head);
                    entry->crc_kind = SH_CRC_32;
                    entry->method = SH_METHOD_DCL;
                }
                ++layout->exefile_count;
            }
        }
    }
    return found != 0U;
}

/* DCL streams in EXEFILE resources carry no unpacked size: measure it. */
static void sh_measure_exefiles(Abstractformat *self, int64_t image_size,
                                sh_layout *layout) {
    size_t index;
    for (index = 0U; index < layout->count; ++index) {
        sh_entry *entry = &layout->entries[index];
        uint8_t *packed;
        size_t consumed = 0U, produced = 0U;
        if (entry->kind != SH_ENTRY_EXEFILE_DCL) continue;
        packed = sh_read_block(self, image_size,
                               entry->data_offset - self->base_address,
                               entry->packed_size);
        if (!packed) continue;
        if (xx_dcl_scan_memory(packed, (size_t)entry->packed_size,
                               (size_t)SH_MAX_UNPACKED, &consumed,
                               &produced)) {
            entry->unpacked_size = (int64_t)produced;
        }
        xx_mem_free(packed);
    }
}

/* ------------------------------------------------------------------------ */
/* Footer and catalog                                                        */
/* ------------------------------------------------------------------------ */

typedef struct sh_footer_s {
    int64_t offset;
    uint32_t script;
    uint32_t records_end;
    uint32_t pvl;
    uint32_t pvm;
} sh_footer;

static bool sh_tag_at(Abstractformat *self, int64_t offset, const char *tag,
                      size_t size) {
    uint8_t buffer[16];
    if (size > sizeof(buffer) ||
        !sh_read_at(self->device, self->base_address + offset, buffer, size)) {
        return false;
    }
    return xx_rt_memcmp(buffer, tag, size) == 0;
}

static bool sh_read_footer(Abstractformat *self, int64_t image_size,
                           sh_footer *footer) {
    uint8_t raw[SH_FOOTER_SIZE];
    unsigned index;
    xx_mem_zero(footer, sizeof(*footer));
    if (image_size < SH_FOOTER_SIZE ||
        !sh_read_at(self->device,
                    self->base_address + image_size - SH_FOOTER_SIZE, raw,
                    sizeof(raw)) ||
        raw[0] != SH_FOOTER_TAG) {
        return false;
    }
    for (index = 1U; index <= SH_SERIAL_DIGITS; ++index) {
        if (raw[index] < '0' || raw[index] > '9') return false;
    }
    footer->offset = image_size - SH_FOOTER_SIZE;
    footer->script = sh_u32(raw + 13);
    footer->records_end = sh_u32(raw + 17);
    footer->pvl = sh_u32(raw + 21);
    footer->pvm = sh_u32(raw + 25);
    return true;
}

/* [PVM] must sit below the end of its records, which must sit below the
 * footer; [PVL], when present, must come first. */
static bool sh_footer_catalog_ok(Abstractformat *self,
                                 const sh_footer *footer) {
    if (footer->pvm == 0U) {
        return footer->pvl == 0U && footer->records_end == 0U;
    }
    if ((int64_t)footer->pvm + SH_BLOCK_TAG_SIZE > footer->records_end ||
        (int64_t)footer->records_end > footer->offset ||
        !sh_tag_at(self, footer->pvm, "\x05[PVM]", SH_BLOCK_TAG_SIZE)) {
        return false;
    }
    if (footer->pvl != 0U &&
        ((int64_t)footer->pvl + SH_BLOCK_TAG_SIZE > footer->pvm ||
         !sh_tag_at(self, footer->pvl, "\x05[PVL]", SH_BLOCK_TAG_SIZE))) {
        return false;
    }
    return true;
}

/* The stored name: NUL terminated inside its 13 bytes and not empty.  Its
 * characters are judged later, when the output name is chosen. */
static bool sh_record_name(const uint8_t *record, char *name) {
    size_t index;
    for (index = 0U; index < SH_REC_NAME_SIZE; ++index) {
        uint8_t c = record[6U + index];
        if (c == 0U) break;
        name[index] = (char)c;
    }
    if (index == 0U || index == SH_REC_NAME_SIZE) return false;
    name[index] = '\0';
    return true;
}

/* The catalog stride: 47 for 0x1A records; 55 or 59 for 0x1C records,
 * whichever tiles the catalog with a tag and a name at every step. */
static uint32_t sh_catalog_stride(const uint8_t *catalog, size_t size) {
    static const uint32_t strides[] = {SH_REC_SIZE_NEW, SH_REC_SIZE_WIDE};
    size_t pick, at;
    char name[SH_REC_NAME_SIZE + 1U];
    if (size == 0U) return 0U;
    if (catalog[0] == SH_REC_TAG_OLD) {
        if (size % SH_REC_SIZE_OLD != 0U) return 0U;
        for (at = 0U; at < size; at += SH_REC_SIZE_OLD) {
            if (catalog[at] != SH_REC_TAG_OLD ||
                !sh_record_name(catalog + at, name)) {
                return 0U;
            }
        }
        return SH_REC_SIZE_OLD;
    }
    if (catalog[0] != SH_REC_TAG_NEW) return 0U;
    for (pick = 0U; pick < 2U; ++pick) {
        bool ok = size % strides[pick] == 0U;
        for (at = 0U; ok && at < size; at += strides[pick]) {
            ok = catalog[at] == SH_REC_TAG_NEW &&
                 sh_record_name(catalog + at, name);
        }
        if (ok) return strides[pick];
    }
    return 0U;
}

/* Add the members this file holds: same disk as the first record and data
 * wholly between data_base and data_end. */
static bool sh_catalog_members(Abstractformat *self, const uint8_t *catalog,
                               size_t size, uint32_t stride,
                               int64_t catalog_offset, int64_t data_base,
                               int64_t data_end, bool data_here,
                               sh_layout *layout, xx_pd_struct *pd) {
    size_t at, ordinal = 0U;
    bool old = stride == SH_REC_SIZE_OLD;
    uint8_t first_disk = old ? catalog[0x1E] : catalog[0x20];
    for (at = 0U; at < size; at += stride) {
        const uint8_t *record = catalog + at;
        uint32_t offset = sh_u32(record + 1U);
        uint32_t packed = sh_u32(record + 0x14U);
        uint8_t disk = old ? record[0x1E] : record[0x20];
        uint32_t unpacked = sh_u32(record + (old ? 0x21U : 0x23U));
        char name[SH_REC_NAME_SIZE + 1U];
        sh_entry *entry;
        ++ordinal;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!data_here || disk != first_disk ||
            !sh_within(data_end, data_base + (int64_t)offset,
                       (int64_t)packed) ||
            (int64_t)unpacked > SH_MAX_UNPACKED) {
            ++layout->members_elsewhere;
            continue;
        }
        if (!sh_record_name(record, name)) return false;
        entry = sh_layout_add(layout);
        if (!entry) return false;
        entry->name = sh_make_name(name, "MEMBER_", ordinal);
        if (!entry->name) return false;
        entry->kind = SH_ENTRY_MEMBER;
        entry->header_offset = self->base_address + catalog_offset + (int64_t)at;
        entry->header_size = stride;
        entry->data_offset = self->base_address + data_base + offset;
        entry->packed_size = packed;
        entry->unpacked_size = unpacked;
        entry->method = record[5];
        entry->attributes = record[0x13];
        entry->dos_date = sh_u16(record + 0x18U);
        entry->dos_time = sh_u16(record + 0x1AU);
        entry->has_dos_time = true;
        if (old) {
            entry->crc = sh_u16(record + 0x1CU);
            entry->crc_kind = SH_CRC_16;
        } else {
            entry->crc = sh_u32(record + 0x1CU);
            entry->crc_kind = SH_CRC_32;
        }
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Parse                                                                     */
/* ------------------------------------------------------------------------ */

/* full = false is the detection probe: only headers, the resource table and
 * the footer are read, and nothing is decoded. */
static bool sh_parse(Abstractformat *self, sh_layout *layout, bool full,
                     xx_pd_struct *pd) {
    int64_t total, image_size;
    sh_footer footer;
    sh_ne ne;
    bool is_ne, has_footer, footer_ok;
    uint8_t *catalog = NULL;
    bool result = false;

    xx_mem_zero(layout, sizeof(*layout));
    xx_mem_zero(&ne, sizeof(ne));
    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total <= self->base_address) return false;
    image_size = total - self->base_address;

    has_footer = sh_read_footer(self, image_size, &footer);
    footer_ok = has_footer && sh_footer_catalog_ok(self, &footer);
    is_ne = sh_ne_load(self, image_size, &ne, full);

    if (is_ne) {
        /* The SFX stub: EXEFILE resources are required; a footer is
         * optional (stub-only releases have none). */
        layout->kind = XX_SFX_HCI_INSTALIT_KIND_SFX;
        if (!sh_ne_exefiles(self, image_size, &ne, full ? layout : NULL, pd)) {
            goto done;
        }
        if (has_footer && !footer_ok) has_footer = false;
    } else {
        /* A data volume: nothing but the footer and its catalog. */
        if (!footer_ok || footer.pvm == 0U || footer.pvl != 0U) goto done;
        layout->kind = XX_SFX_HCI_INSTALIT_KIND_VOLUME;
    }
    layout->has_footer = has_footer;

    if (has_footer && footer.pvm != 0U) {
        int64_t catalog_offset = (int64_t)footer.pvm + SH_BLOCK_TAG_SIZE;
        int64_t catalog_size = (int64_t)footer.records_end - catalog_offset;
        uint32_t stride;
        if (catalog_size <= 0 ||
            catalog_size > (int64_t)SH_MAX_RECORDS * SH_REC_SIZE_WIDE) {
            if (!is_ne) goto done;
        } else {
            catalog = sh_read_block(self, image_size, catalog_offset,
                                    catalog_size);
            stride = catalog ? sh_catalog_stride(catalog, (size_t)catalog_size)
                             : 0U;
            if (stride == 0U) {
                if (!is_ne) goto done;
            } else {
                layout->record_size = stride;
                layout->catalog_records =
                    (uint32_t)((size_t)catalog_size / stride);
                if (!is_ne) {
                    /* A volume's first member starts its data. */
                    if (sh_u32(catalog + 1U) != 0U) goto done;
                }
            }
        }
        if (full && layout->record_size != 0U) {
            /* SFX: data follows [PVL]; volume: data starts at offset 0. */
            bool data_here = !is_ne || footer.pvl != 0U;
            int64_t data_base =
                is_ne ? (int64_t)footer.pvl + SH_BLOCK_TAG_SIZE : 0;
            if (!sh_catalog_members(self, catalog, (size_t)catalog_size,
                                    layout->record_size, catalog_offset,
                                    data_base, footer.pvm, data_here, layout,
                                    pd)) {
                goto done;
            }
        }
    }

    /* The obfuscated script lives in the SFX only; a volume's pointer names
     * an offset in its setup program. */
    if (is_ne && has_footer && footer.script != 0U) {
        int64_t script_end = footer.pvl != 0U ? (int64_t)footer.pvl
                                              : footer.offset;
        int64_t body = (int64_t)footer.script + SH_SCRIPT_TAG_SIZE;
        if (body <= script_end && script_end - body <= SH_MAX_SCRIPT &&
            sh_tag_at(self, footer.script, "\x08[SCRIPT]",
                      SH_SCRIPT_TAG_SIZE)) {
            layout->has_script = true;
            if (full) {
                sh_entry *entry = sh_layout_add(layout);
                if (!entry) goto done;
                entry->name = xx_str_dup(XX_SFX_HCI_INSTALIT_SCRIPT_NAME);
                if (!entry->name) goto done;
                entry->kind = SH_ENTRY_SCRIPT;
                entry->header_offset = self->base_address + footer.script;
                entry->header_size = SH_SCRIPT_TAG_SIZE;
                entry->data_offset = self->base_address + body;
                entry->packed_size = script_end - body;
                entry->unpacked_size = script_end - body;
                entry->method = 0U;
            }
        }
    }

    if (layout->kind == XX_SFX_HCI_INSTALIT_KIND_SFX && !has_footer) {
        layout->size = ne.extent > 0 && ne.extent <= image_size ? ne.extent
                                                                : image_size;
    } else {
        layout->size = image_size;
    }

    if (full) {
        sh_measure_exefiles(self, image_size, layout);
        if (!sh_dedupe(layout)) goto done;
    }
    result = true;
done:
    if (catalog) xx_mem_free(catalog);
    sh_ne_cleanup(&ne);
    if (!result) sh_layout_cleanup(layout);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Decoding one entry                                                        */
/* ------------------------------------------------------------------------ */

static bool sh_crc_ok(const sh_entry *entry, const uint8_t *data,
                      size_t size) {
    if (entry->crc_kind == SH_CRC_32) {
        return xx_crc32_calc(0U, data, size) == entry->crc;
    }
    if (entry->crc_kind == SH_CRC_16) {
        return xx_crc16_arc_calc(0U, data, size) == (uint16_t)entry->crc;
    }
    return true;
}

/* The declared unpacked size is only a claim; before allocating for it,
 * hold it to what the codec can expand the packed bytes to.  DCL spends at
 * least ~22 bits on a 518-byte match, the 0xD0 codec at least 4 bits on a
 * 60-byte match; LZW phrases and the 0x90 runs can grow faster, so 0xE0 gets
 * a looser bound. */
static bool sh_ratio_ok(uint8_t method, int64_t packed, int64_t unpacked) {
    int64_t factor;
    switch (method) {
        case SH_METHOD_DCL: factor = 256; break;
        case SH_METHOD_LZH: factor = 128; break;
        case SH_METHOD_LZW: factor = 1024; break;
        case SH_METHOD_STORE: return unpacked == packed;
        default: return false;
    }
    return unpacked <= packed * factor + 65536;
}

/* Produce the entry's bytes in memory.  *out is owned by the caller. */
static bool sh_decode_entry(Abstractformat *self, const sh_entry *entry,
                            uint64_t max_member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    int64_t total = xx_io_total_size(self->device);
    int64_t image_size = total - self->base_address;
    uint8_t *packed = NULL, *plain = NULL;
    size_t consumed = 0U, index;
    bool ok = false;

    *out = NULL;
    *out_size = 0U;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (entry->unpacked_size < 0 || entry->unpacked_size > SH_MAX_UNPACKED ||
        (uint64_t)entry->unpacked_size > max_member ||
        entry->packed_size < 0 || entry->packed_size > SH_MAX_PACKED) {
        return false;
    }
    packed = sh_read_block(self, image_size,
                           entry->data_offset - self->base_address,
                           entry->packed_size);
    if (!packed) return false;
    if (entry->kind == SH_ENTRY_EXEFILE_RAW || entry->kind == SH_ENTRY_SCRIPT) {
        if (entry->kind == SH_ENTRY_SCRIPT) {
            for (index = 0U; index < (size_t)entry->packed_size; ++index) {
                packed[index] ^= SH_SCRIPT_KEY;
            }
        }
        *out = packed;
        *out_size = (size_t)entry->packed_size;
        return true;
    }
    if (!sh_ratio_ok(entry->method, entry->packed_size,
                     entry->unpacked_size)) {
        goto done;
    }
    if (entry->method == SH_METHOD_DCL && entry->unpacked_size > 0) {
        /* Measure first (a sliding window, no allocation): the buffer is
         * sized only for a stream that really produces the declared size. */
        size_t produced = 0U;
        if (!xx_dcl_scan_memory(packed, (size_t)entry->packed_size,
                                (size_t)entry->unpacked_size + 1U, &consumed,
                                &produced) ||
            produced != (size_t)entry->unpacked_size) {
            goto done;
        }
    }
    plain = (uint8_t *)xx_mem_alloc(entry->unpacked_size
                                        ? (size_t)entry->unpacked_size
                                        : 1U);
    if (!plain) goto done;
    switch (entry->method) {
        case SH_METHOD_DCL:
            if (entry->unpacked_size == 0) {
                /* xx_dcl_decode_memory wants room for one byte; an empty
                 * member is a stream that reaches its end marker at once. */
                size_t produced = 1U;
                ok = xx_dcl_scan_memory(packed, (size_t)entry->packed_size, 1U,
                                        &consumed, &produced) &&
                     produced == 0U;
            } else {
                ok = xx_dcl_decode_memory(packed, (size_t)entry->packed_size,
                                          plain, (size_t)entry->unpacked_size,
                                          &consumed);
            }
            break;
        case SH_METHOD_LZH:
            ok = xx_sfx_hci_instalit_lzh_decode_memory(
                packed, (size_t)entry->packed_size, plain,
                (size_t)entry->unpacked_size, &consumed);
            break;
        case SH_METHOD_LZW:
            ok = xx_sfx_hci_instalit_lzw_decode_memory(
                packed, (size_t)entry->packed_size, plain,
                (size_t)entry->unpacked_size, &consumed);
            break;
        case SH_METHOD_STORE:
            /* sh_ratio_ok has held the two sizes equal. */
            xx_rt_memcpy(plain, packed, (size_t)entry->unpacked_size);
            ok = true;
            break;
        default:
            /* An unknown method is refused, never passed through as stored:
             * that would hand back packed bytes that look like data. */
            ok = false;
            break;
    }
    if (ok) ok = sh_crc_ok(entry, plain, (size_t)entry->unpacked_size);
done:
    xx_mem_free(packed);
    if (!ok) {
        if (plain) xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = (size_t)entry->unpacked_size;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool sh_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *sh_find_option(const xx_list_s *options,
                                    uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool sh_populate_record(xx_archive_record *record,
                               const sh_entry *entry) {
    bool ok;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = entry->header_size;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->packed_size;
    ok = xx_archive_record_set_original_name(record, entry->name) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)entry->packed_size) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        entry->method) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                         false) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false);
    if (ok && entry->unpacked_size >= 0) {
        ok = xx_archive_record_set_meta_u64(
            record, XX_META_ID_UNCOMPRESSED_SIZE,
            (uint64_t)entry->unpacked_size);
    }
    if (ok && entry->crc_kind == SH_CRC_32) {
        ok = xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                            entry->crc);
    }
    if (ok && entry->has_dos_time) {
        ok = xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                            entry->attributes) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                            entry->dos_date) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                            entry->dos_time);
    }
    return ok;
}

static void sh_stream_free(void *pointer) {
    sh_stream *stream = (sh_stream *)pointer;
    if (!stream) return;
    sh_layout_cleanup(&stream->layout);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_sfx_hci_instalit_init(xx_sfx_hci_instalit *archive,
                              xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_HCI_INSTALIT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-instalit");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_sfx_hci_instalit_check_is_valid;
    archive->format.handle_base_info = xx_sfx_hci_instalit_handle_base_info;
    archive->format.get_format_size = xx_sfx_hci_instalit_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_hci_instalit_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_hci_instalit_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_hci_instalit_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_hci_instalit_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_hci_instalit_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_hci_instalit_free_archive_records_reading;
    archive->format.destroy = sh_vtable_destroy;
}

xx_sfx_hci_instalit *xx_sfx_hci_instalit_create(xx_io_device *device,
                                                int64_t base_address) {
    xx_sfx_hci_instalit *archive =
        (xx_sfx_hci_instalit *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_hci_instalit_init(archive, device, base_address);
    return archive;
}

void xx_sfx_hci_instalit_destroy(xx_sfx_hci_instalit *archive) {
    if (!archive) return;
    if (archive->internal) {
        sh_layout_cleanup((sh_layout *)archive->internal);
        xx_mem_free(archive->internal);
        archive->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&archive->format);
}

static void sh_vtable_destroy(Abstractformat *self) {
    xx_sfx_hci_instalit_destroy((xx_sfx_hci_instalit *)self);
}

void xx_sfx_hci_instalit_free(xx_sfx_hci_instalit *archive) {
    if (!archive) return;
    xx_sfx_hci_instalit_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_hci_instalit_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd) {
    sh_layout layout;
    bool result = sh_parse(self, &layout, false, pd);
    sh_layout_cleanup(&layout);
    return result;
}

bool xx_sfx_hci_instalit_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd) {
    xx_sfx_hci_instalit *archive = (xx_sfx_hci_instalit *)self;
    sh_layout *layout;
    int64_t total;
    if (!self) return false;
    layout = (sh_layout *)xx_mem_alloc(sizeof(*layout));
    if (!layout || !sh_parse(self, layout, true, pd)) {
        if (layout) xx_mem_free(layout);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (archive->internal) {
        sh_layout_cleanup((sh_layout *)archive->internal);
        xx_mem_free(archive->internal);
    }
    archive->internal = layout;
    archive->number_of_records = layout->count;
    archive->kind = layout->kind;
    archive->exefile_count = layout->exefile_count;
    archive->catalog_records = layout->catalog_records;
    archive->record_size = layout->record_size;
    archive->members_elsewhere = layout->members_elsewhere;
    archive->has_footer = layout->has_footer;
    archive->has_script = layout->has_script;
    xx_format_set_extension(self, layout->kind == XX_SFX_HCI_INSTALIT_KIND_SFX
                                      ? "exe"
                                      : "001");
    self->format_size = layout->size;
    total = xx_io_total_size(self->device);
    if (total > self->base_address + layout->size) {
        self->overlay_offset = self->base_address + layout->size;
        self->overlay_size = total - self->overlay_offset;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = layout->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_sfx_hci_instalit_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_sfx_hci_instalit_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_sfx_hci_instalit *)self)->number_of_records;
}

xx_archive_record_state *xx_sfx_hci_instalit_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    sh_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (sh_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!sh_copy_options(&state->options, options) ||
        !sh_parse(self, &stream->layout, true, pd)) {
        sh_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = sh_stream_free;
    state->total_records = (int64_t)stream->layout.count;
    if (stream->layout.count != 0U &&
        sh_populate_record(&state->current_record,
                           &stream->layout.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_sfx_hci_instalit_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sfx_hci_instalit_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    sh_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (sh_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->layout.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!sh_populate_record(&state->current_record,
                            &stream->layout.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_sfx_hci_instalit_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    sh_stream *stream;
    const sh_entry *entry;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    uint8_t *data = NULL;
    size_t size = 0U;
    uint64_t max_member = UINT64_MAX;
    bool created = false;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (sh_stream *)state->internal_state;
    if (stream->index >= stream->layout.count) return false;
    entry = &stream->layout.entries[stream->index];
    if (!sh_safe_name(entry->name)) return false;
    option = sh_find_option(&state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option) max_member = xx_var_get_u64(option);
    if (!sh_decode_entry(self, entry, max_member, &data, &size, pd)) {
        return false;
    }
    option = sh_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: the entry decoded and verified. */
        xx_mem_free(data);
        return true;
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", entry->name);
    } else {
        destination = xx_str_concat(base, entry->name);
    }
    if (!destination || !xx_store_create_dirs_a(destination, false)) {
        goto cleanup;
    }
    {
        xx_io_device *output = xx_io_file_open(destination, "wb");
        size_t done = 0U;
        if (!output) goto cleanup;
        created = true;
        result = true;
        while (done < size) {
            ssize_t wrote = xx_io_write(output, data + done, size - done);
            if (wrote <= 0 || (size_t)wrote > size - done) {
                result = false;
                break;
            }
            done += (size_t)wrote;
        }
        if (xx_io_close(output) != 0) result = false;
    }
    if (result && entry->has_dos_time) {
        (void)xx_store_apply_dos_time_and_attrs_a(destination, entry->dos_date,
                                                  entry->dos_time, 0U);
    }
cleanup:
    if (!result && destination && created) xx_rt_remove(destination);
    if (data) xx_mem_free(data);
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_sfx_hci_instalit_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
