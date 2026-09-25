/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OpenVMS BACKUP save set.
 *
 * THERE IS NO CODEC.  A save set is block and record chaining, nothing
 * more, and it is a different container from the two OpenVMS readers this
 * library already has: vmspcsi is the DCX-compressed PCSI kit and vmsdb is
 * the BER-ish PCSI product database.  Neither shares a byte of layout with
 * this one.
 *
 * Block header (0x100 bytes, little endian) -- also the whole detection test
 *   +0x00  u16  size       == 0x0100
 *   +0x02  u16  opsys      in {0x400, 0x800, 0x1000}
 *   +0x04  u16  subsys     == 1           (BACKUP)
 *   +0x06  u16  applic     1, or 2 for a filler block that is skipped
 *   +0x10  u64  == 0       +0x18  u64  == 0
 *   +0x20  u32  == 0x00010101
 *   +0x28  i32  blocksize  > 0x100
 *   +0xec  u64  == 0       +0xf4  u64  == 0     +0xfc  u16  == 0
 *
 * Record header (0x10 bytes): u16 rsize, u16 rtype, u32 flags, u32 address,
 * u32 spare (validated as zero).  Records never span a block.
 *
 * rtype 3 opens a file and carries u16 0x0101 then (u16 len, u16 tag, len
 * bytes) attributes: tag 0x2a is the name, 0x33 a flag word whose 0x2000 bit
 * marks a directory, and 0x34 the 0x20-byte FAT whose word-swapped efblk and
 * ffbyte give the size as (efblk - 1) * 512 + ffbyte.  The body is the
 * payloads of the FOLLOWING rtype-4 records concatenated, with interleaved
 * rtype-0 records skipped and the last record truncated to fit.
 *
 * Ported from XArchive Algos/xvmssavesetdecoder.cpp and
 * packages/xvmssavesetarchive.cpp; U3 implements the same format as
 * archive/581 (class ghb, VMT 0x0066e658).
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/vmssaveset/xx_vmssaveset.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef VMSSAVESET
#define XX_VMSSAVESET_FILE_TYPE XX_FILE_TYPE_VMSSAVESET
#else
#define XX_VMSSAVESET_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define VMSSAVESET_MAX_MEMBERS 100000U

/* One enumerated member.  The aux slots carry whatever the format needs to
 * rebuild the member later without re-parsing the container. */
typedef struct vmssaveset_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint64_t timestamp;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
    uint32_t method;
    uint32_t crc32;
    uint32_t attributes;
    uint32_t flags;
    bool has_crc;
    bool encrypted;
    bool folder;
} vmssaveset_member;

typedef struct vmssaveset_stream_s {
    vmssaveset_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
} vmssaveset_stream;

static uint16_t vmssaveset_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t vmssaveset_le32(const uint8_t *b) {
    return (uint32_t)vmssaveset_le16(b) | ((uint32_t)vmssaveset_le16(b + 2U) << 16U);
}

static uint64_t vmssaveset_le64(const uint8_t *b) {
    return (uint64_t)vmssaveset_le32(b) | ((uint64_t)vmssaveset_le32(b + 4U) << 32U);
}

static uint32_t vmssaveset_be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24U) | ((uint32_t)b[1] << 16U) |
           ((uint32_t)b[2] << 8U) | (uint32_t)b[3];
}

static uint64_t vmssaveset_be64(const uint8_t *b) {
    return ((uint64_t)vmssaveset_be32(b) << 32U) | (uint64_t)vmssaveset_be32(b + 4U);
}

static bool vmssaveset_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool vmssaveset_write_all(xx_io_device *device, const void *data, size_t size,
                          xx_pd_struct *pd) {
    size_t done = 0U;
    if (!data && size != 0U) return false;
    if (!device) return true; /* verify-only pass: nothing is materialized */
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done,
                             size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Copy a run of source bytes straight through to the destination. */
static bool vmssaveset_copy_range(xx_io_device *source, int64_t offset, uint64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!source || offset < 0) return false;
    if (!destination) return true;
    if (xx_io_seek64(source, offset, SEEK_SET) != 0) return false;
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < want) {
            ssize_t amount = xx_io_read(source, buffer + done, want - done);
            if (amount <= 0 || (size_t)amount > want - done) return false;
            done += (size_t)amount;
        }
        if (!vmssaveset_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Emit `size` zero bytes: the filler every sparse disk image needs. */
static bool vmssaveset_write_zeros(xx_io_device *destination, uint64_t size,
                            xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!destination) return true;
    xx_mem_zero(buffer, sizeof(buffer));
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        if (!vmssaveset_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction. */
static char *vmssaveset_make_name(const char *prefix, int64_t index,
                           const char *suffix) {
    char buffer[96];
    size_t used = 0U;
    size_t at;
    char *result;
    for (at = 0U; prefix && prefix[at]; ++at) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = prefix[at];
    }
    if (index >= 0) {
        char digits[24];
        size_t count = 0U;
        int64_t value = index;
        do {
            digits[count++] = (char)('0' + (int)(value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < 4U && count < sizeof(digits)) digits[count++] = '0';
        while (count != 0U) {
            if (used >= sizeof(buffer) - 1U) return NULL;
            buffer[used++] = digits[--count];
        }
    }
    for (at = 0U; suffix && suffix[at]; ++at) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = suffix[at];
    }
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

/* Names that DO come from the container are normalized here: separators are
 * unified, traversal components are removed and anything a filesystem would
 * choke on becomes '_'. */
static char *vmssaveset_clean_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U, output = 0U;
    if ((!bytes && size != 0U) || size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    while (input < size) {
        size_t start, end, component_start;
        while (input < size && (bytes[input] == '/' || bytes[input] == '\\'))
            ++input;
        start = input;
        while (input < size && bytes[input] != '/' && bytes[input] != '\\')
            ++input;
        end = input;
        if (end == start || (end - start == 1U && bytes[start] == '.'))
            continue;
        if (end - start == 2U && bytes[start] == '.' &&
            bytes[start + 1U] == '.') {
            if (output != 0U) {
                while (output != 0U && name[output - 1U] != '/') --output;
                if (output != 0U) --output;
            }
            continue;
        }
        if (output != 0U) name[output++] = '/';
        component_start = output;
        while (start < end) {
            uint8_t c = bytes[start++];
            if (c < 0x20U || c == '"' || c == '*' || c == ':' || c == '<' ||
                c == '>' || c == '?' || c == '|' || c == 0U)
                name[output++] = '_';
            else
                name[output++] = (char)c;
        }
        while (output > component_start &&
               (name[output - 1U] == ' ' || name[output - 1U] == '.'))
            --output;
        if (output == component_start) name[output++] = '_';
    }
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static bool vmssaveset_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void vmssaveset_stream_free(void *opaque) {
    vmssaveset_stream *stream = (vmssaveset_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool vmssaveset_add_member(vmssaveset_stream *stream, const vmssaveset_member *member) {
    vmssaveset_member *grown;
    if (!stream || !member || stream->count >= VMSSAVESET_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (vmssaveset_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define VMSSAVESET_BLOCK_HEADER_SIZE 0x100
#define VMSSAVESET_RECORD_HEADER_SIZE 0x10
#define VMSSAVESET_MAX_RECORD 0x10000
#define VMSSAVESET_MAX_MEMBER ((uint64_t)512U * 1024U * 1024U)
#define VMSSAVESET_MAX_BODY_RECORDS 40000000
#define VMSSAVESET_MAX_NAME 512U
#define VMSSAVESET_FLAG_VARREC 0x01U

/* A save set is a flat sequence of fixed-size BLOCKS.  Every block opens with
 * a 0x100-byte block header and the rest of it holds a packed sequence of
 * 0x10-byte record headers, each followed by its payload.  Records never span
 * a block: the block header's blocksize bounds them.  Ported from XArchive's
 * Algos/xvmssavesetdecoder.cpp. */
typedef struct vmssaveset_cursor_s {
    xx_io_device *device;
    int64_t base;
    int64_t size;
    int64_t position;
} vmssaveset_cursor;

typedef struct vmssaveset_walker_s {
    vmssaveset_cursor *cursor;
    int64_t remaining;
    int32_t block_size;
} vmssaveset_walker;

static bool vmssaveset_cursor_read(vmssaveset_cursor *cursor, size_t count,
                                   uint8_t *out) {
    if (!cursor || cursor->position < 0 ||
        (int64_t)count > cursor->size - cursor->position)
        return false;
    if (!vmssaveset_read_at(cursor->device, cursor->base + cursor->position,
                            out, count))
        return false;
    cursor->position += (int64_t)count;
    return true;
}

static bool vmssaveset_cursor_skip(vmssaveset_cursor *cursor, int64_t count) {
    if (!cursor || count < 0 || cursor->position < 0 ||
        count > cursor->size - cursor->position)
        return false;
    cursor->position += count;
    return true;
}

static bool vmssaveset_cursor_seek(vmssaveset_cursor *cursor, int64_t target) {
    if (!cursor || target < 0 || target > cursor->size) return false;
    cursor->position = target;
    return true;
}

static bool vmssaveset_opsys_ok(uint16_t opsys) {
    return opsys == 0x400U || opsys == 0x800U || opsys == 0x1000U;
}

static bool vmssaveset_block_header_ok(const uint8_t *header) {
    return vmssaveset_le16(header) == 0x100U &&
           vmssaveset_opsys_ok(vmssaveset_le16(header + 2U)) &&
           vmssaveset_le16(header + 4U) == 1U &&
           vmssaveset_le16(header + 6U) == 1U &&
           (int32_t)vmssaveset_le32(header + 0x28U) > 0x100 &&
           vmssaveset_le64(header + 0x10U) == 0U &&
           vmssaveset_le64(header + 0x18U) == 0U &&
           vmssaveset_le32(header + 0x20U) == 0x10101U &&
           vmssaveset_le64(header + 0xecU) == 0U &&
           vmssaveset_le64(header + 0xf4U) == 0U &&
           vmssaveset_le16(header + 0xfcU) == 0U;
}

/* The block-and-record iterator.  `remaining` is allowed to go negative
 * exactly as the reference lets it: the next block header is only fetched
 * when it is EXACTLY zero. */
static bool vmssaveset_next_record(vmssaveset_walker *walker,
                                   int32_t *record_size,
                                   int32_t *record_type) {
    uint8_t header[VMSSAVESET_BLOCK_HEADER_SIZE];
    if (!walker || !walker->cursor || !record_size || !record_type)
        return false;
    if (walker->remaining == 0) {
        for (;;) {
            uint16_t applic;
            int32_t block;
            if (!vmssaveset_cursor_read(walker->cursor,
                                        VMSSAVESET_BLOCK_HEADER_SIZE, header))
                return false;
            applic = vmssaveset_le16(header + 6U);
            block = (int32_t)vmssaveset_le32(header + 0x28U);
            if (walker->block_size == 0) walker->block_size = block;
            if (vmssaveset_le16(header) != 0x100U ||
                !vmssaveset_opsys_ok(vmssaveset_le16(header + 2U)) ||
                vmssaveset_le16(header + 4U) != 1U)
                return false;
            if (applic != 1U && applic != 2U) return false;
            if (vmssaveset_le64(header + 0x10U) != 0U ||
                vmssaveset_le64(header + 0x18U) != 0U ||
                vmssaveset_le64(header + 0xecU) != 0U ||
                vmssaveset_le64(header + 0xf4U) != 0U ||
                vmssaveset_le16(header + 0xfcU) != 0U)
                return false;
            if (applic == 2U && block == 0) block = walker->block_size;
            if (block < 0x101) return false;
            walker->remaining = (int64_t)block - VMSSAVESET_BLOCK_HEADER_SIZE;
            if (applic != 2U) break;
            if (!vmssaveset_cursor_skip(walker->cursor, walker->remaining))
                return false;
        }
    }
    {
        uint8_t record[VMSSAVESET_RECORD_HEADER_SIZE];
        if (!vmssaveset_cursor_read(walker->cursor,
                                    VMSSAVESET_RECORD_HEADER_SIZE, record))
            return false;
        walker->remaining -= VMSSAVESET_RECORD_HEADER_SIZE;
        if (vmssaveset_le32(record + 0x0cU) != 0U) return false;
        *record_size = (int32_t)vmssaveset_le16(record);
        *record_type = (int32_t)vmssaveset_le16(record + 2U);
        walker->remaining -= *record_size;
    }
    return true;
}

/* The rtype-3 file-attributes record.  The reference bounds this loop with
 * the FULL record size without subtracting the two magic bytes and then seeks
 * to the record end, so the over-read is harmless; that is reproduced here,
 * not "fixed". */
static bool vmssaveset_attributes(vmssaveset_cursor *cursor,
                                  int32_t record_size, char *name,
                                  size_t name_size, uint64_t *file_size,
                                  bool *is_directory, bool *var_rec) {
    uint8_t scratch[0x40];
    int64_t left = record_size;
    if (!cursor || !name || !file_size || !is_directory || !var_rec)
        return false;
    name[0] = '\0';
    *file_size = 0U;
    *is_directory = false;
    *var_rec = false;
    if (!vmssaveset_cursor_read(cursor, 2U, scratch)) return false;
    if (scratch[0] != 1U || scratch[1] != 1U) return false;
    while (left > 3) {
        int32_t length, tag;
        if (!vmssaveset_cursor_read(cursor, 4U, scratch)) return false;
        length = (int32_t)vmssaveset_le16(scratch);
        tag = (int32_t)vmssaveset_le16(scratch + 2U);
        if (left - 4 < length) return false;
        left = left - 4 - length;
        if (tag == 0x2a) {
            uint8_t raw[VMSSAVESET_MAX_NAME];
            size_t take;
            if (length == 0 || (size_t)length > sizeof(raw)) return false;
            if (!vmssaveset_cursor_read(cursor, (size_t)length, raw))
                return false;
            take = (size_t)length < name_size - 1U ? (size_t)length
                                                   : name_size - 1U;
            {
                size_t at;
                for (at = 0U; at < take; ++at) {
                    uint8_t ch = raw[at];
                    bool safe = (ch >= 'a' && ch <= 'z') ||
                                (ch >= 'A' && ch <= 'Z') ||
                                (ch >= '0' && ch <= '9') || ch == '.' ||
                                ch == '_' || ch == '-' || ch == ';';
                    name[at] = safe ? (char)ch : '_';
                }
                name[take] = '\0';
            }
        } else if (tag == 0x33) {
            if (length != 4) return false;
            if (!vmssaveset_cursor_read(cursor, 4U, scratch)) return false;
            *is_directory = (vmssaveset_le32(scratch) & 0x2000U) != 0U;
        } else if (tag == 0x34) {
            uint64_t high, low, first_free, end_block;
            if (length != 0x20) return false;
            if (!vmssaveset_cursor_read(cursor, 0x20U, scratch)) return false;
            high = vmssaveset_le16(scratch + 8U);
            low = vmssaveset_le16(scratch + 10U);
            first_free = vmssaveset_le16(scratch + 12U);
            /* efblk is a VAX word-swapped longword. */
            end_block = high * 0x10000U + low;
            if (end_block == 0U) return false;
            *file_size = (end_block - 1U) * 512U + first_free;
            *var_rec = scratch[0] == 2U && scratch[1] == 2U;
        } else if (tag == 0x36 || tag == 0x37) {
            if (length != 8) return false;
            if (!vmssaveset_cursor_read(cursor, 8U, scratch)) return false;
        } else {
            if (!vmssaveset_cursor_skip(cursor, length)) return false;
        }
    }
    return true;
}

/* VMS variable-length records to CRLF text.  The body is {u16 length, that
 * many bytes, one pad byte when the length is odd} repeated, and a length word
 * can straddle two body records, so the conversion runs as a small state
 * machine fed record by record rather than over one assembled buffer.  With a
 * NULL destination it only COUNTS, which is how the reported member size is
 * known before anything is written. */
typedef struct vmssaveset_varrec_s {
    xx_io_device *destination;
    uint64_t produced;
    uint32_t remaining;
    uint8_t header[2];
    unsigned header_have;
    unsigned phase; /* 0 length word, 1 payload, 2 pad byte */
    bool pad;
} vmssaveset_varrec;

static const uint8_t vmssaveset_crlf[2] = { 0x0dU, 0x0aU };

static bool vmssaveset_varrec_push(vmssaveset_varrec *state,
                                   const uint8_t *data, size_t size,
                                   xx_pd_struct *pd) {
    size_t at = 0U;
    if (!state) return false;
    while (at < size) {
        if (state->phase == 0U) {
            state->header[state->header_have++] = data[at++];
            if (state->header_have < 2U) continue;
            state->header_have = 0U;
            state->remaining = vmssaveset_le16(state->header);
            state->pad = (state->remaining & 1U) != 0U;
            state->phase = 1U;
            if (state->remaining != 0U) continue;
            if (!vmssaveset_write_all(state->destination, vmssaveset_crlf, 2U,
                                      pd))
                return false;
            state->produced += 2U;
            state->phase = state->pad ? 2U : 0U;
        } else if (state->phase == 1U) {
            size_t take = size - at;
            if (take > state->remaining) take = state->remaining;
            if (!vmssaveset_write_all(state->destination, data + at, take, pd))
                return false;
            state->produced += take;
            at += take;
            state->remaining -= (uint32_t)take;
            if (state->remaining != 0U) continue;
            if (!vmssaveset_write_all(state->destination, vmssaveset_crlf, 2U,
                                      pd))
                return false;
            state->produced += 2U;
            state->phase = state->pad ? 2U : 0U;
        } else {
            ++at;
            state->phase = 0U;
        }
    }
    return true;
}

/* Gather the rtype-4 payloads that follow until `size` bytes exist;
 * interleaved rtype-0 records are skipped and the last record is truncated to
 * fit.  A NULL destination consumes the body without writing it. */
static bool vmssaveset_collect(vmssaveset_walker *walker, uint64_t size,
                               xx_io_device *destination,
                               vmssaveset_varrec *convert, xx_pd_struct *pd) {
    uint8_t payload[VMSSAVESET_MAX_RECORD];
    uint64_t left = size;
    int64_t records = 0;
    if (!walker || size > VMSSAVESET_MAX_MEMBER) return false;
    if (convert) {
        xx_mem_zero(convert, sizeof(*convert));
        convert->destination = destination;
    }
    while (left >= 1U) {
        int32_t record_size = 0, record_type = 0;
        uint64_t take;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (++records > VMSSAVESET_MAX_BODY_RECORDS) return false;
        if (!vmssaveset_next_record(walker, &record_size, &record_type))
            return false;
        if (record_type == 0) {
            if (!vmssaveset_cursor_skip(walker->cursor, record_size))
                return false;
            continue;
        }
        if (record_type != 4 || record_size < 0 ||
            record_size > VMSSAVESET_MAX_RECORD)
            return false;
        if (!vmssaveset_cursor_read(walker->cursor, (size_t)record_size,
                                    payload))
            return false;
        take = left < (uint64_t)record_size ? left : (uint64_t)record_size;
        if (convert) {
            if (!vmssaveset_varrec_push(convert, payload, (size_t)take, pd))
                return false;
        } else if (destination &&
                   !vmssaveset_write_all(destination, payload, (size_t)take,
                                         pd)) {
            return false;
        }
        left -= (uint64_t)record_size < left ? (uint64_t)record_size : left;
    }
    /* A variable-record stream that does not parse out exactly is malformed. */
    if (convert && (convert->phase != 0U || convert->header_have != 0U))
        return false;
    return true;
}

static bool vmssaveset_parse(Abstractformat *format,
                             vmssaveset_stream **result) {
    uint8_t header[VMSSAVESET_BLOCK_HEADER_SIZE];
    vmssaveset_stream *stream = NULL;
    vmssaveset_cursor cursor;
    vmssaveset_walker walker;
    int64_t total, size;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < VMSSAVESET_BLOCK_HEADER_SIZE ||
        (uint64_t)size > VMSSAVESET_MAX_MEMBER ||
        !vmssaveset_read_at(format->device, format->base_address, header,
                            sizeof(header)) ||
        !vmssaveset_block_header_ok(header))
        return false;

    stream = (vmssaveset_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->aux0 = vmssaveset_le32(header + 0x28U);

    cursor.device = format->device;
    cursor.base = format->base_address;
    cursor.size = size;
    cursor.position = 0;
    walker.cursor = &cursor;
    walker.remaining = 0;
    walker.block_size = 0;

    /* Stop at the first malformed record and keep what is already there,
     * which is what the reference does. */
    for (;;) {
        int32_t record_size = 0, record_type = 0;
        int64_t header_offset, record_end;
        char name[VMSSAVESET_MAX_NAME];
        uint64_t file_size;
        bool is_directory, var_rec;
        vmssaveset_member member;
        if (stream->count >= VMSSAVESET_MAX_MEMBERS) break;
        if (!vmssaveset_next_record(&walker, &record_size, &record_type))
            break;
        header_offset = cursor.position - VMSSAVESET_RECORD_HEADER_SIZE;
        record_end = cursor.position + record_size;
        if (record_type != 3) {
            if (record_type != 0 && record_type != 1 && record_type != 0x0b &&
                record_type != 4)
                break;
            if (!vmssaveset_cursor_seek(&cursor, record_end)) break;
            continue;
        }
        if (!vmssaveset_attributes(&cursor, record_size, name, sizeof(name),
                                   &file_size, &is_directory, &var_rec))
            break;
        if (!vmssaveset_cursor_seek(&cursor, record_end)) break;

        xx_mem_zero(&member, sizeof(member));
        member.header_offset = format->base_address + header_offset;
        member.header_size = VMSSAVESET_RECORD_HEADER_SIZE + record_size;
        member.data_offset = format->base_address + cursor.position;
        member.unpacked_size = file_size;
        member.aux0 = (uint64_t)walker.remaining;
        member.aux1 = (uint64_t)(uint32_t)walker.block_size;
        member.aux2 = file_size;
        member.flags = var_rec ? VMSSAVESET_FLAG_VARREC : 0U;
        member.folder = is_directory;

        /* A directory's body still has to be consumed or the walk
         * desynchronises, but the reference emits no member for it.  For a
         * variable-record file the same pass also COUNTS the converted length,
         * which is what the member then reports. */
        if (var_rec && !is_directory) {
            vmssaveset_varrec convert;
            if (!vmssaveset_collect(&walker, file_size, NULL, &convert, NULL))
                break;
            member.unpacked_size = convert.produced;
        } else if (!vmssaveset_collect(&walker, file_size, NULL, NULL, NULL)) {
            break;
        }
        member.packed_size = format->base_address + cursor.position -
                             member.data_offset;
        if (is_directory) continue;
        if (name[0] == '\0') {
            member.name =
                vmssaveset_make_name("member_", (int64_t)stream->count, "");
        } else {
            member.name =
                vmssaveset_clean_name((const uint8_t *)name, xx_str_len(name));
        }
        if (!member.name) break;
        if (!vmssaveset_add_member(stream, &member)) {
            xx_mem_free(member.name);
            break;
        }
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    vmssaveset_stream_free(stream);
    return false;
}

static bool vmssaveset_write_member(Abstractformat *format,
                                    vmssaveset_stream *stream,
                                    const vmssaveset_member *member,
                                    xx_io_device *destination,
                                    xx_pd_struct *pd) {
    vmssaveset_cursor cursor;
    vmssaveset_walker walker;
    int64_t total;
    (void)stream;
    if (!format || !format->device || !member) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    cursor.device = format->device;
    cursor.base = format->base_address;
    cursor.size = total - format->base_address;
    cursor.position = member->data_offset - format->base_address;
    if (cursor.position < 0 || cursor.position > cursor.size) return false;
    walker.cursor = &cursor;
    walker.remaining = (int64_t)member->aux0;
    walker.block_size = (int32_t)(uint32_t)member->aux1;
    /* The body is the payloads of the following rtype-4 records concatenated,
     * with the variable-record-to-CRLF reformat applied when the FAT said the
     * file is one (flags bit 0). */
    if ((member->flags & VMSSAVESET_FLAG_VARREC) != 0U) {
        vmssaveset_varrec convert;
        return vmssaveset_collect(&walker, member->aux2, destination, &convert,
                                  pd) &&
               convert.produced == member->unpacked_size;
    }
    return vmssaveset_collect(&walker, member->aux2, destination, NULL, pd);
}

static bool vmssaveset_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *vmssaveset_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool vmssaveset_set_record(xx_archive_record *record,
                           const vmssaveset_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc32) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           member->encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

void xx_vmssaveset_init(xx_vmssaveset *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_VMSSAVESET_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-vms-saveset");
    xx_format_set_extension(&archive->format, "bck");
    archive->format.check_is_valid = xx_vmssaveset_check_is_valid;
    archive->format.handle_base_info = xx_vmssaveset_handle_base_info;
    archive->format.get_format_size = xx_vmssaveset_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_vmssaveset_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_vmssaveset_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_vmssaveset_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_vmssaveset_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_vmssaveset_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_vmssaveset_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_vmssaveset *xx_vmssaveset_create(xx_io_device *device, int64_t base_address) {
    xx_vmssaveset *archive = (xx_vmssaveset *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_vmssaveset_init(archive, device, base_address);
    return archive;
}

void xx_vmssaveset_destroy(xx_vmssaveset *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_vmssaveset_free(xx_vmssaveset *archive) {
    if (!archive) return;
    xx_vmssaveset_destroy(archive);
    xx_mem_free(archive);
}

bool xx_vmssaveset_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    vmssaveset_stream *stream;
    (void)pd;
    if (!vmssaveset_parse(format, &stream)) return false;
    vmssaveset_stream_free(stream);
    return true;
}

bool xx_vmssaveset_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    vmssaveset_stream *stream;
    xx_vmssaveset *archive;
    (void)pd;
    if (!format || !vmssaveset_parse(format, &stream)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_vmssaveset *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_VMSSAVESET_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    vmssaveset_stream_free(stream);
    return true;
}

int64_t xx_vmssaveset_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vmssaveset_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_vmssaveset_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vmssaveset_handle_base_info(format, pd))
               ? ((xx_vmssaveset *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_vmssaveset_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    vmssaveset_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!vmssaveset_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        vmssaveset_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = vmssaveset_stream_free;
    state->total_records = stream->count;
    if (!vmssaveset_copy_options(&state->options, options) ||
        !vmssaveset_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_vmssaveset_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_vmssaveset_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    vmssaveset_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (vmssaveset_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        vmssaveset_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_vmssaveset_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    vmssaveset_stream *stream;
    vmssaveset_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (vmssaveset_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!vmssaveset_safe_output_name(member->name)) return false;
    path_option = vmssaveset_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return vmssaveset_write_member(format, stream, member, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    created = destination != NULL;
    if (!destination) goto done;
    result = vmssaveset_write_member(format, stream, member, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && !member->folder && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_vmssaveset_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
