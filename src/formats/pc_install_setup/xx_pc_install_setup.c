/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PC-Install self-extracting setup: the PC-Install setup engine (a 16-bit
 * NE or 32-bit PE program) with its installation set appended as the
 * overlay.  xx_pc_install_setup.h carries the field tables.
 *
 * Ported from XArchive installers/xpcinstallsfx.cpp (MIT License,
 * Copyright (c) 2026 hors).  The executable itself is never parsed: the
 * payload is found from its own markers.  The last sixteen bytes are the
 * trailer "[20/20]\0" + first-record offset + last-record offset, the head
 * tag "[20/20]\0" sits right in front of the first record, and the record
 * chain has to tile the space between the head tag and the trailer exactly.
 * On a stub that was re-linked at a different size after the payload was
 * built the trailer's offsets are stale while the head tag moved with the
 * payload, so when the stored offsets fail the head tag is searched for
 * (bounded) and the chain is walked from there; the first and the last
 * record must then still be the same distance apart as the trailer says.
 *
 * A record's payload is a member group when it parses as one completely --
 * zero link name, tag 0x0074, 1..4096 info blocks, each a printable name, a
 * zero reserved word and a DCL prelude, together tiling the payload
 * exactly.  Anything else is a file the setup engine consumes verbatim
 * (the .PIF shortcut, the scrambled setup .CFG, a spawn helper) and is
 * copied out unchanged under its staging name.
 *
 * Names are printable ASCII without separators by construction; names that
 * would still alias something on Windows ("..", "CON", "a:b", a trailing
 * dot) are listed but refused on extraction, and later duplicates (compared
 * ASCII case-insensitively) get a "_<member number>" suffix before the
 * extension.  Without the overwrite option an existing file is never
 * replaced.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pc_install_setup/xx_pc_install_setup.h"

#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

/* Registration placeholder: xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested. */
#ifdef PC_INSTALL_SETUP
#define XX_PC_INSTALL_SETUP_FILE_TYPE XX_FILE_TYPE_PC_INSTALL_SETUP
#else
#define XX_PC_INSTALL_SETUP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PCIS_TAG_SIZE 8
#define PCIS_TRAILER_SIZE 16
#define PCIS_RECORD_SIZE 0x114
#define PCIS_RECORD_NAME_OFFSET 0x14U
#define PCIS_RECORD_NAME_SIZE 256U
#define PCIS_PROLOGUE_SIZE 0x3a
#define PCIS_LINK_SIZE 0x0eU
#define PCIS_INFO_SIZE 0xa8
#define PCIS_INFO_NAME_SIZE 128U
#define PCIS_GROUP_TAG 0x0074U
/* The head tag follows the stub, and the stub is at least a DOS header. */
#define PCIS_MIN_STUB 0x1c
#define PCIS_MAX_RECORDS 65536U
#define PCIS_MAX_GROUP_MEMBERS 4096U
#define PCIS_MAX_MEMBERS 65536U
/* Headers (records and info blocks) one parse may read over all the chain
 * walks it tries; the largest set of the corpus spends 125 (one per record,
 * group prologue and installed file, plus the last-record check). */
#define PCIS_READ_BUDGET 16384U
/* The stale-trailer fallback: how many head tags are tried and how far into
 * the file they are looked for.  The stubs of the corpus end below 256 KiB. */
#define PCIS_MAX_CANDIDATES 64U
#define PCIS_SCAN_LIMIT (INT64_C(16) * 1024 * 1024)
#define PCIS_SCAN_CHUNK 65536U
/* This is a floppy/CD setup builder; the caps only keep a hostile field
 * from driving an allocation. */
#define PCIS_MAX_PACKED (INT64_C(64) * 1024 * 1024)
#define PCIS_MAX_RAW ((size_t)256U * 1024U * 1024U)
#define PCIS_COPY_CHUNK 65536U
#define PCIS_NAME_TRIES 16U

static const uint8_t pcis_tag[PCIS_TAG_SIZE] = {'[', '2', '0', '/',
                                                '2', '0', ']', 0U};

typedef struct pcis_member_s {
    char *name;            /* output name, unique case-insensitively */
    int64_t header_offset; /* absolute device offsets */
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    int64_t unpacked_size; /* -1 while unknown */
    uint32_t declared_size; /* info +0x9c; 0 when the builder left it blank */
    uint32_t attributes;
    uint16_t dos_date;
    uint16_t dos_time;
    bool stored;
    bool safe;
} pcis_member;

typedef struct pcis_stream_s {
    pcis_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    size_t chunks;
    size_t budget;
    int64_t archive_size;
    int64_t head_offset;
    int64_t trailer_offset;
    bool relocated;
    bool exhausted;
} pcis_stream;

static uint16_t pcis_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t pcis_le32(const uint8_t *bytes) {
    return (uint32_t)pcis_le16(bytes) |
           ((uint32_t)pcis_le16(bytes + 2U) << 16U);
}

static bool pcis_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool pcis_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}

/* Every name field is a fixed-width buffer whose tail is stale builder heap:
 * the name is the bytes before the first NUL, and it has to be printable
 * ASCII without a path separator.  Returns 0 when the field holds no name. */
static size_t pcis_field_name_length(const uint8_t *field, size_t size) {
    size_t length = 0U;
    while (length < size && field[length] != 0U) {
        uint8_t c = field[length];
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\') return 0U;
        ++length;
    }
    return length < size ? length : 0U;
}

static char *pcis_copy_name(const uint8_t *bytes, size_t length) {
    char *name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    xx_rt_memcpy(name, bytes, length);
    name[length] = 0;
    return name;
}

static void pcis_truncate(pcis_stream *stream, size_t count) {
    while (stream->count > count) {
        pcis_member *member = &stream->items[--stream->count];
        if (member->name) xx_mem_free(member->name);
        member->name = NULL;
    }
}

static void pcis_stream_free(void *opaque) {
    pcis_stream *stream = (pcis_stream *)opaque;
    if (!stream) return;
    pcis_truncate(stream, 0U);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static pcis_member *pcis_push(pcis_stream *stream) {
    pcis_member *member;
    if (stream->count >= PCIS_MAX_MEMBERS) return NULL;
    if (stream->count == stream->capacity) {
        size_t capacity = stream->capacity ? stream->capacity * 2U : 16U;
        pcis_member *grown;
        if (capacity > PCIS_MAX_MEMBERS) capacity = PCIS_MAX_MEMBERS;
        grown = (pcis_member *)xx_mem_realloc(stream->items,
                                              capacity * sizeof(*grown));
        if (!grown) return NULL;
        stream->items = grown;
        stream->capacity = capacity;
    }
    member = &stream->items[stream->count++];
    xx_mem_zero(member, sizeof(*member));
    member->unpacked_size = -1;
    return member;
}

/* Once the budget is gone every walk fails, rather than letting a group
 * that could not be read fall back to being a verbatim file. */
static bool pcis_spend(pcis_stream *stream) {
    if (stream->budget == 0U) {
        stream->exhausted = true;
        return false;
    }
    --stream->budget;
    return true;
}

/* A record payload that parses completely as a member group is expanded
 * into its members; on any mismatch nothing is added and the caller treats
 * the payload as a verbatim file. */
static bool pcis_parse_group(xx_io_device *device, int64_t group_offset,
                             int64_t group_size, pcis_stream *stream) {
    uint8_t prologue[PCIS_PROLOGUE_SIZE];
    size_t start = stream->count;
    size_t index;
    uint32_t count;
    int64_t position;
    if (group_size < PCIS_PROLOGUE_SIZE + PCIS_INFO_SIZE + 2 ||
        !pcis_spend(stream) ||
        !pcis_read_at(device, group_offset, prologue, sizeof(prologue)))
        return false;
    /* The cross-volume link name is always blank inside one file. */
    for (index = 0U; index < PCIS_LINK_SIZE; ++index)
        if (prologue[index] != 0U) return false;
    if (pcis_le16(prologue + 0x0eU) != PCIS_GROUP_TAG ||
        pcis_le16(prologue + 0x12U) != PCIS_GROUP_TAG)
        return false;
    count = pcis_le16(prologue + 0x10U);
    /* Each member costs at least its info block and a two-byte prelude. */
    if (count == 0U || count > PCIS_MAX_GROUP_MEMBERS ||
        (int64_t)count >
            (group_size - PCIS_PROLOGUE_SIZE) / (PCIS_INFO_SIZE + 2))
        return false;
    position = PCIS_PROLOGUE_SIZE;
    for (index = 0U; index < count; ++index) {
        uint8_t info[PCIS_INFO_SIZE];
        uint8_t prelude[2];
        pcis_member *member;
        size_t length;
        int64_t packed;
        if (position > group_size - PCIS_INFO_SIZE || !pcis_spend(stream) ||
            !pcis_read_at(device, group_offset + position, info,
                          sizeof(info)))
            goto fail;
        length = pcis_field_name_length(info, PCIS_INFO_NAME_SIZE);
        if (length == 0U || pcis_le32(info + 0x84U) != 0U) goto fail;
        packed = (int64_t)pcis_le32(info + 0x88U);
        if (packed < 2 || packed > group_size - position - PCIS_INFO_SIZE)
            goto fail;
        /* Raw PKWARE DCL prelude: literal mode 0/1, dictionary bits 4..6. */
        if (!pcis_read_at(device, group_offset + position + PCIS_INFO_SIZE,
                          prelude, sizeof(prelude)) ||
            prelude[0] > 1U || prelude[1] < 4U || prelude[1] > 6U)
            goto fail;
        member = pcis_push(stream);
        if (!member) goto fail;
        member->name = pcis_copy_name(info, length);
        if (!member->name) goto fail;
        member->header_offset = group_offset + position;
        member->header_size = PCIS_INFO_SIZE;
        member->data_offset = group_offset + position + PCIS_INFO_SIZE;
        member->packed_size = packed;
        member->declared_size = pcis_le32(info + 0x9cU);
        if (member->declared_size != 0U)
            member->unpacked_size = (int64_t)member->declared_size;
        member->attributes = pcis_le32(info + 0x80U);
        member->dos_date = pcis_le16(info + 0x8cU);
        member->dos_time = pcis_le16(info + 0x90U);
        member->stored = false;
        position += PCIS_INFO_SIZE + packed;
    }
    /* The member count must account for the payload exactly. */
    if (position != group_size) goto fail;
    return true;
fail:
    pcis_truncate(stream, start);
    return false;
}

/* Walks the chain whose first record is at @p start (relative to the base
 * address) and fills @p stream.  The chain must tile [start, trailer). */
static bool pcis_walk(Abstractformat *format, int64_t trailer, int64_t start,
                      uint32_t stored_first, uint32_t stored_last,
                      pcis_stream *stream, xx_pd_struct *pd) {
    xx_io_device *device = format->device;
    int64_t base = format->base_address;
    int64_t cursor = start;
    int64_t terminal;
    uint8_t tag[PCIS_TAG_SIZE];
    uint8_t header[PCIS_RECORD_SIZE];
    size_t chunks = 0U;
    if (start < PCIS_MIN_STUB + PCIS_TAG_SIZE ||
        start > trailer - PCIS_RECORD_SIZE ||
        !pcis_read_at(device, base + start - PCIS_TAG_SIZE, tag, sizeof(tag)) ||
        xx_rt_memcmp(tag, pcis_tag, sizeof(tag)) != 0)
        return false;
    /* The chain can only be accepted if its last record lies where the
     * trailer's first/last distance puts it, so that record is checked
     * before the walk: a stop record that ends at the trailer.  One read
     * then turns away every start the walk would reject at its end. */
    terminal = start + ((int64_t)stored_last - (int64_t)stored_first);
    if (terminal < start || terminal > trailer - PCIS_RECORD_SIZE ||
        !pcis_spend(stream) ||
        !pcis_read_at(device, base + terminal, header, sizeof(header)) ||
        pcis_le32(header) != 0U ||
        (int64_t)pcis_le32(header + 0x10U) !=
            trailer - terminal - PCIS_RECORD_SIZE ||
        pcis_field_name_length(header + PCIS_RECORD_NAME_OFFSET,
                               PCIS_RECORD_NAME_SIZE) == 0U)
        return false;
    pcis_truncate(stream, 0U);
    for (;;) {
        uint8_t record[PCIS_RECORD_SIZE];
        int64_t next, stored, payload;
        size_t length;
        if (pcis_stopped(pd) || chunks >= PCIS_MAX_RECORDS ||
            cursor > trailer - PCIS_RECORD_SIZE || !pcis_spend(stream) ||
            !pcis_read_at(device, base + cursor, record, sizeof(record)))
            goto fail;
        next = (int64_t)pcis_le32(record);
        stored = (int64_t)pcis_le32(record + 0x10U);
        payload = cursor + PCIS_RECORD_SIZE;
        if (stored > trailer - payload) goto fail;
        length = pcis_field_name_length(record + PCIS_RECORD_NAME_OFFSET,
                                        PCIS_RECORD_NAME_SIZE);
        if (length == 0U) goto fail;
        ++chunks;
        if (!pcis_parse_group(device, base + payload, stored, stream)) {
            pcis_member *member;
            if (stream->exhausted) goto fail;
            member = pcis_push(stream);
            if (!member) goto fail;
            member->name = pcis_copy_name(record + PCIS_RECORD_NAME_OFFSET,
                                          length);
            if (!member->name) goto fail;
            member->header_offset = base + cursor;
            member->header_size = PCIS_RECORD_SIZE;
            member->data_offset = base + payload;
            member->packed_size = stored;
            member->unpacked_size = stored;
            member->declared_size = (uint32_t)stored;
            member->attributes = pcis_le32(record + 0x04U) & 0xffffU;
            member->dos_time = pcis_le16(record + 0x08U);
            member->dos_date = pcis_le16(record + 0x0cU);
            member->stored = true;
        }
        if (next == 0) {
            /* The last record ends where the trailer begins, and the
             * trailer's first/last pair spans the same distance as the
             * chain (both move together when the stub is re-linked). */
            if (payload + stored != trailer ||
                (int64_t)stored_last - (int64_t)stored_first !=
                    cursor - start)
                goto fail;
            stream->chunks = chunks;
            stream->head_offset = start - PCIS_TAG_SIZE;
            return true;
        }
        if (next != payload + stored) goto fail;
        cursor = next;
    }
fail:
    pcis_truncate(stream, 0U);
    return false;
}

/* The trailer's offsets did not lead to a chain: look for the head tag in
 * the leading part of the file and walk from each candidate. */
static bool pcis_relocate(Abstractformat *format, int64_t trailer,
                          uint32_t stored_first, uint32_t stored_last,
                          pcis_stream *stream, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t position = PCIS_MIN_STUB;
    int64_t last_tag = trailer - PCIS_RECORD_SIZE - PCIS_TAG_SIZE;
    size_t candidates = 0U;
    bool found = false;
    if (last_tag > PCIS_SCAN_LIMIT) last_tag = PCIS_SCAN_LIMIT;
    if (last_tag < position) return false;
    buffer = (uint8_t *)xx_mem_alloc(PCIS_SCAN_CHUNK);
    if (!buffer) return false;
    while (!found && !stream->exhausted && candidates < PCIS_MAX_CANDIDATES &&
           !pcis_stopped(pd)) {
        int64_t window = last_tag - position + PCIS_TAG_SIZE;
        size_t amount = window > (int64_t)PCIS_SCAN_CHUNK ? PCIS_SCAN_CHUNK
                                                          : (size_t)window;
        size_t at;
        if (!pcis_read_at(format->device, format->base_address + position,
                          buffer, amount))
            break;
        for (at = 0U; at + PCIS_TAG_SIZE <= amount && !found &&
                      !stream->exhausted && candidates < PCIS_MAX_CANDIDATES;
             ++at) {
            int64_t start;
            if (buffer[at] != pcis_tag[0] ||
                xx_rt_memcmp(buffer + at, pcis_tag, PCIS_TAG_SIZE) != 0)
                continue;
            start = position + (int64_t)at + PCIS_TAG_SIZE;
            if (start == (int64_t)stored_first) continue; /* already tried */
            ++candidates;
            found = pcis_walk(format, trailer, start, stored_first,
                              stored_last, stream, pd);
        }
        if (amount < PCIS_SCAN_CHUNK) break;
        position += (int64_t)(amount - (PCIS_TAG_SIZE - 1U));
    }
    xx_mem_free(buffer);
    return found;
}

/* --- member names ------------------------------------------------------- */

static uint8_t pcis_fold(uint8_t c) {
    return (c >= 'a' && c <= 'z') ? (uint8_t)(c - 'a' + 'A') : c;
}

static uint32_t pcis_hash(const char *name) {
    uint32_t hash = UINT32_C(2166136261);
    while (*name) {
        hash ^= pcis_fold((uint8_t)*name++);
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static bool pcis_same_name(const char *a, const char *b) {
    while (*a && *b) {
        if (pcis_fold((uint8_t)*a) != pcis_fold((uint8_t)*b)) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

typedef struct pcis_name_set_s {
    const char **slots;
    size_t mask;
} pcis_name_set;

/* Returns the slot that holds @p name, or the empty slot it belongs in. */
static const char **pcis_name_slot(pcis_name_set *set, const char *name) {
    size_t slot = (size_t)pcis_hash(name) & set->mask;
    while (set->slots[slot] && !pcis_same_name(set->slots[slot], name))
        slot = (slot + 1U) & set->mask;
    return &set->slots[slot];
}

static size_t pcis_put_decimal(char *out, size_t value) {
    char digits[24];
    size_t count = 0U, index;
    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    for (index = 0U; index < count; ++index)
        out[index] = digits[count - 1U - index];
    return count;
}

/* "<stem>_<number>[_<attempt>]<ext>" where <ext> starts at the last dot
 * (a leading dot is part of the stem). */
static char *pcis_suffixed_name(const char *name, size_t number,
                                size_t attempt) {
    size_t length = xx_str_len(name);
    size_t stem = length, position = 0U;
    char *result;
    while (stem > 1U && name[stem - 1U] != '.') --stem;
    stem = (stem > 1U) ? stem - 1U : length;
    if (length > SIZE_MAX - 64U) return NULL;
    result = (char *)xx_mem_alloc(length + 64U);
    if (!result) return NULL;
    xx_rt_memcpy(result, name, stem);
    position = stem;
    result[position++] = '_';
    position += pcis_put_decimal(result + position, number);
    if (attempt != 0U) {
        result[position++] = '_';
        position += pcis_put_decimal(result + position, attempt);
    }
    xx_rt_memcpy(result + position, name + stem, length - stem);
    position += length - stem;
    result[position] = 0;
    return result;
}

/* CON, PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$ and CLOCK$, with or
 * without an extension, in any case. */
static bool pcis_device_name(const char *name) {
    static const char *const words[] = {"CON",    "PRN",     "AUX", "NUL",
                                        "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, word, index;
    while (name[stem] && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (word = 0U; word < sizeof(words) / sizeof(words[0]); ++word) {
        const char *text = words[word];
        for (index = 0U; index < stem && text[index]; ++index)
            if (pcis_fold((uint8_t)name[index]) != (uint8_t)text[index])
                break;
        if (index == stem && text[index] == 0) return true;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9') {
        uint8_t a = pcis_fold((uint8_t)name[0]);
        uint8_t b = pcis_fold((uint8_t)name[1]);
        uint8_t c = pcis_fold((uint8_t)name[2]);
        if ((a == 'C' && b == 'O' && c == 'M') ||
            (a == 'L' && b == 'P' && c == 'T'))
            return true;
    }
    return false;
}

static bool pcis_name_safe(const char *name) {
    size_t length = xx_str_len(name), index;
    if (length == 0U ||
        (name[0] == '.' && (length == 1U || (length == 2U && name[1] == '.'))))
        return false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)name[index];
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|')
            return false;
    }
    if (name[length - 1U] == '.' || name[length - 1U] == ' ') return false;
    return !pcis_device_name(name);
}

static bool pcis_finish_names(pcis_stream *stream) {
    pcis_name_set set;
    size_t capacity = 16U, index;
    while (capacity < stream->count * 2U + 1U) capacity *= 2U;
    set.slots = (const char **)xx_mem_calloc(capacity, sizeof(*set.slots));
    if (!set.slots) return false;
    set.mask = capacity - 1U;
    for (index = 0U; index < stream->count; ++index) {
        pcis_member *member = &stream->items[index];
        const char **slot = pcis_name_slot(&set, member->name);
        bool unique = *slot == NULL;
        size_t attempt;
        for (attempt = 0U; !unique && attempt < PCIS_NAME_TRIES; ++attempt) {
            char *candidate = pcis_suffixed_name(member->name, index + 1U,
                                                 attempt);
            if (!candidate) {
                xx_mem_free((void *)set.slots);
                return false;
            }
            slot = pcis_name_slot(&set, candidate);
            if (*slot == NULL) {
                xx_mem_free(member->name);
                member->name = candidate;
                unique = true;
            } else {
                xx_mem_free(candidate);
            }
        }
        if (unique) *slot = member->name;
        member->safe = unique && pcis_name_safe(member->name);
    }
    xx_mem_free((void *)set.slots);
    return true;
}

/* --- parse -------------------------------------------------------------- */

static bool pcis_parse(Abstractformat *format, pcis_stream **result,
                       xx_pd_struct *pd) {
    uint8_t mz[2];
    uint8_t trailer_bytes[PCIS_TRAILER_SIZE];
    pcis_stream *stream;
    int64_t total, size, trailer;
    uint32_t first, last;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < PCIS_MIN_STUB + PCIS_TAG_SIZE + PCIS_RECORD_SIZE +
                   PCIS_TRAILER_SIZE)
        return false;
    /* Two cheap reads decide every file that is not this format. */
    if (!pcis_read_at(format->device, format->base_address, mz, sizeof(mz)) ||
        mz[0] != 'M' || mz[1] != 'Z')
        return false;
    trailer = size - PCIS_TRAILER_SIZE;
    if (!pcis_read_at(format->device, format->base_address + trailer,
                      trailer_bytes, sizeof(trailer_bytes)) ||
        xx_rt_memcmp(trailer_bytes, pcis_tag, PCIS_TAG_SIZE) != 0)
        return false;
    first = pcis_le32(trailer_bytes + 8U);
    last = pcis_le32(trailer_bytes + 12U);
    stream = (pcis_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->budget = PCIS_READ_BUDGET;
    stream->archive_size = size;
    stream->trailer_offset = trailer;
    if (!pcis_walk(format, trailer, (int64_t)first, first, last, stream, pd)) {
        /* A walk that used up the budget is not followed by a search. */
        if (stream->exhausted ||
            !pcis_relocate(format, trailer, first, last, stream, pd))
            goto fail;
        stream->relocated = true;
    }
    if (stream->count == 0U || !pcis_finish_names(stream)) goto fail;
    *result = stream;
    return true;
fail:
    pcis_stream_free(stream);
    return false;
}

/* --- member data -------------------------------------------------------- */

static uint8_t *pcis_load_packed(Abstractformat *format,
                                 const pcis_member *member) {
    uint8_t *packed;
    if (member->packed_size < 2 || member->packed_size > PCIS_MAX_PACKED)
        return NULL;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    if (!packed) return NULL;
    if (!pcis_read_at(format->device, member->data_offset, packed,
                      (size_t)member->packed_size)) {
        xx_mem_free(packed);
        return NULL;
    }
    return packed;
}

/* Decodes a group member.  The stream must end exactly at the end of its
 * packed bytes and, when the info block records a size, produce exactly
 * that.  @p plain may be NULL to measure only. */
static bool pcis_decode(Abstractformat *format, pcis_member *member,
                        uint64_t max_member, uint64_t memory_limit,
                        uint8_t **plain, size_t *plain_size) {
    uint8_t *packed;
    uint8_t *output = NULL;
    size_t consumed = 0U, produced = 0U, written = 0U;
    size_t limit = PCIS_MAX_RAW;
    bool result = false;
    if (plain) *plain = NULL;
    if (plain_size) *plain_size = 0U;
    if ((uint64_t)limit > max_member) limit = (size_t)max_member;
    packed = pcis_load_packed(format, member);
    if (!packed) return false;
    if (!xx_dcl_scan_memory(packed, (size_t)member->packed_size, limit,
                            &consumed, &produced) ||
        consumed != (size_t)member->packed_size ||
        (member->declared_size != 0U &&
         (uint64_t)produced != (uint64_t)member->declared_size) ||
        (uint64_t)produced > memory_limit ||
        (uint64_t)member->packed_size > memory_limit - (uint64_t)produced)
        goto done;
    member->unpacked_size = (int64_t)produced;
    if (!plain) {
        result = true;
        goto done;
    }
    /* An empty file is a stream that is only the end marker; the scan has
     * already proved that, and the decoder refuses a zero-sized output. */
    output = (uint8_t *)xx_mem_alloc(produced != 0U ? produced : 1U);
    if (!output ||
        (produced != 0U &&
         (!xx_dcl_decode_memory(packed, (size_t)member->packed_size, output,
                                produced, &written) ||
          written != produced)))
        goto done;
    *plain = output;
    output = NULL;
    if (plain_size) *plain_size = written;
    result = true;
done:
    if (output) xx_mem_free(output);
    xx_mem_free(packed);
    return result;
}

static bool pcis_write_all(xx_io_device *destination, const uint8_t *data,
                           size_t size) {
    size_t written = 0U;
    while (written < size) {
        ssize_t amount = xx_io_write(destination, data + written,
                                     size - written);
        if (amount <= 0 || (size_t)amount > size - written) return false;
        written += (size_t)amount;
    }
    return true;
}

static bool pcis_copy_stored(Abstractformat *format,
                             const pcis_member *member,
                             xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t done = 0;
    bool result = true;
    if (member->packed_size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(PCIS_COPY_CHUNK);
    if (!buffer) return false;
    while (done < member->packed_size) {
        int64_t left = member->packed_size - done;
        size_t amount = left > (int64_t)PCIS_COPY_CHUNK ? PCIS_COPY_CHUNK
                                                        : (size_t)left;
        if (pcis_stopped(pd) ||
            !pcis_read_at(format->device, member->data_offset + done, buffer,
                          amount) ||
            !pcis_write_all(destination, buffer, amount)) {
            result = false;
            break;
        }
        done += (int64_t)amount;
    }
    xx_mem_free(buffer);
    return result;
}

/* --- records ------------------------------------------------------------ */

static bool pcis_copy_options(xx_list_s *destination,
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

static bool pcis_set_record(Abstractformat *format, xx_archive_record *record,
                            pcis_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    /* The info block leaves the size blank on about half of the members;
     * then it is measured, and published only when the stream checks out. */
    if (member->unpacked_size < 0 && !member->stored)
        (void)pcis_decode(format, member, UINT64_MAX, UINT64_MAX, NULL, NULL);
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)member->packed_size) ||
        !xx_archive_record_set_meta_u64(
            record, XX_META_ID_COMPRESSION_METHOD,
            member->stored ? XX_PC_INSTALL_SETUP_METHOD_STORE
                           : XX_PC_INSTALL_SETUP_METHOD_DCL) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                        member->attributes) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                        member->dos_date) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                        member->dos_time) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (member->unpacked_size >= 0 &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        (uint64_t)member->unpacked_size))
        return false;
    return true;
}

void xx_pc_install_setup_init(xx_pc_install_setup *archive,
                              xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_PC_INSTALL_SETUP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-pcinstall-sfx");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_pc_install_setup_check_is_valid;
    archive->format.handle_base_info = xx_pc_install_setup_handle_base_info;
    archive->format.get_format_size = xx_pc_install_setup_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pc_install_setup_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pc_install_setup_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pc_install_setup_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pc_install_setup_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pc_install_setup_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pc_install_setup_free_archive_records_reading;
    archive->head_offset = -1;
    archive->trailer_offset = -1;
}

xx_pc_install_setup *xx_pc_install_setup_create(xx_io_device *device,
                                                int64_t base_address) {
    xx_pc_install_setup *archive =
        (xx_pc_install_setup *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_pc_install_setup_init(archive, device, base_address);
    return archive;
}

void xx_pc_install_setup_destroy(xx_pc_install_setup *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_pc_install_setup_free(xx_pc_install_setup *archive) {
    if (!archive) return;
    xx_pc_install_setup_destroy(archive);
    xx_mem_free(archive);
}

bool xx_pc_install_setup_check_is_valid(Abstractformat *format,
                                        xx_pd_struct *pd) {
    pcis_stream *stream;
    if (!pcis_parse(format, &stream, pd)) return false;
    pcis_stream_free(stream);
    return true;
}

bool xx_pc_install_setup_handle_base_info(Abstractformat *format,
                                          xx_pd_struct *pd) {
    pcis_stream *stream;
    xx_pc_install_setup *archive;
    if (!format) return false;
    if (!pcis_parse(format, &stream, pd)) {
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        return false;
    }
    archive = (xx_pc_install_setup *)format;
    archive->number_of_records = stream->count;
    archive->number_of_chunks = stream->chunks;
    archive->head_offset = stream->head_offset;
    archive->trailer_offset = stream->trailer_offset;
    archive->relocated = stream->relocated;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_PC_INSTALL_SETUP_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    pcis_stream_free(stream);
    return true;
}

int64_t xx_pc_install_setup_get_format_size(Abstractformat *format,
                                            xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pc_install_setup_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_pc_install_setup_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pc_install_setup_handle_base_info(format, pd))
               ? ((xx_pc_install_setup *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_pc_install_setup_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    pcis_stream *stream;
    xx_archive_record_state *state;
    if (!pcis_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        pcis_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = pcis_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!pcis_copy_options(&state->options, options) ||
        !pcis_set_record(format, &state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_pc_install_setup_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_pc_install_setup_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    pcis_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (pcis_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = pcis_set_record(format, &state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_pc_install_setup_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    pcis_stream *stream;
    pcis_member *member;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, base_length;
    uint64_t max_member = UINT64_MAX, memory_limit = UINT64_MAX;
    xx_io_device *destination;
    bool overwrite, created = false, result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (pcis_stream *)state->internal_state) ||
        stream->index >= stream->count || pcis_stopped(pd))
        return false;
    member = &stream->items[stream->index];

    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option) max_member = xx_var_get_u64(option);
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MEMORY_LIMIT);
    if (option) memory_limit = xx_var_get_u64(option);
    if (member->unpacked_size >= 0 &&
        (uint64_t)member->unpacked_size > max_member)
        return false;

    /* Group members are decoded before any file is created, so a stream
     * that fails leaves nothing behind. */
    if (!member->stored &&
        !pcis_decode(format, member, max_member, memory_limit, &plain,
                     &plain_size))
        return false;

    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: the member was verified, nothing is written. */
        result = true;
        goto done;
    }
    if (!member->safe) goto done;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    base_length = xx_str_len(base);
    path = (base_length != 0U && base[base_length - 1U] != '/' &&
            base[base_length - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_OVERWRITE);
    overwrite = option && xx_var_get_bool(option);
    destination = xx_io_file_open(path, overwrite ? "wb" : "wbx");
    if (!destination) goto done;
    /* Only a file this call opened is removed again on failure. */
    created = true;
    result = member->stored
                 ? pcis_copy_stored(format, member, destination, pd)
                 : pcis_write_all(destination, plain, plain_size);
    if (xx_io_close(destination) != 0) result = false;
    if (!result && created) xx_rt_remove(path);
done:
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_pc_install_setup_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
