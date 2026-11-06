/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_scan_archives_internal.h"
#include "xxfclib/formats/archive/zip/xx_zip.h"
#include "xxfclib/formats/archive/lha/xx_lha.h"
#include "xxfclib/formats/archive/gz/xx_gz.h"
#include "xxfclib/formats/archive/bz2/xx_bz2.h"
#include "xxfclib/formats/archive/tar/xx_tar.h"
#include "xxfclib/formats/executable/machofat/xx_machofat.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/shrink/xx_shrink.h"
#include "xxfclib/algo/reduce/xx_reduce.h"
#include "xxfclib/algo/implode/xx_implode.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/global/xx_global.h"

#include <string.h>

/* No member size limit: a compressed member of any size it can declare is
 * decoded, into memory below the file buffer size and into a temporary file
 * from it on, as XBinary::createFileBuffer stores it. No ZIP entry without
 * ZIP64 can declare 4 GiB, so this bound only stops a stream that decodes on
 * past anything it could declare (XDecompress decodes to its end). */
#define XX_SCAN_ARCHIVE_MEMBER_DEFAULT (UINT64_C(4) * 1024U * 1024U * 1024U)
/* Readers that decode a whole member in RAM (ZIP methods 10/16/94/96/97,
 * natively capped at 64 MiB) keep their own budget, apart from that bound. */
#define XX_SCAN_ARCHIVE_READER_MEMORY (UINT64_C(512) * 1024U * 1024U)
/* Free space a member's temporary file of XX_SCAN_ARCHIVE_TEMP_RESERVE_FROM or
 * more must leave on the temporary volume (1 GiB): such a member that does not
 * fit with it is skipped, so a large member never takes the last GiB the
 * system and other programs use. A smaller member (output of unknown size,
 * each write while it is smaller) only needs to fit, and may fill the volume. */
#define XX_SCAN_ARCHIVE_TEMP_RESERVE (UINT64_C(1) * 1024U * 1024U * 1024U)
/* 16 MiB, XBinary::createFileBuffer's cut-off between memory and a temporary
 * file: Detect It Easy keeps a smaller member in memory, so such a member is
 * not held to the reserve. */
#define XX_SCAN_ARCHIVE_TEMP_RESERVE_FROM (UINT64_C(16) * 1024U * 1024U)
/* A temporary file of unknown final size checks for room in steps this big. */
#define XX_SCAN_ARCHIVE_TEMP_STEP (UINT64_C(64) * 1024U * 1024U)
#define XX_SCAN_ARCHIVE_TOTAL_DEFAULT UINT64_MAX
#define XX_SCAN_ARCHIVE_COUNT_DEFAULT UINT32_C(100000)
/* The head xx_scan_archive_is_scanable_header needs: a PDF marker may start
 * anywhere in the first 1024 bytes, and a TAR header is a 512-byte block. */
#define XX_SCAN_ARCHIVE_HEAD_SIZE 1029U
/* XFormats types an ISO 9660 image by its volume descriptor at 0x8001
 * ("CD001", version 1), beyond the head. */
#define XX_SCAN_ARCHIVE_ISO_OFFSET 0x8001U
#define XX_SCAN_ARCHIVE_ISO_END 0x8007U

/* XScanEngine's messages for a member it unpacks but never scans. */
static const char xx_scan_archive_invalid_crc[] = "Invalid CRC";
static const char xx_scan_archive_unknown_method[] = "Unknown compression method: Unknown";
/* xx_zip_unpack_current_archive_record_to_device's message for a stream that
 * decoded completely to the wrong CRC or size, i.e. XDecompress's CRC check. */
static const char xx_scan_archive_zip_crc_mismatch[] = "ZIP output CRC or size mismatch";

typedef struct xx_scan_archive_sink {
    uint8_t *data;
    uint64_t size;   /* Decoded bytes received. */
    uint64_t stored; /* Bytes kept in data or temporary; equals size for a kept member. */
    size_t capacity;
    uint64_t limit;
    uint64_t memory_threshold;
    uint64_t expected_size; /* UINT64_MAX when unknown. */
    xx_io_device *temporary;
    bool discard;
    bool failed;
    bool overflow;        /* More output than the limit: damaged data, not a storage fault. */
    bool filter_scanable; /* Keep the member only when its head can be scannable. */
    bool filtered;        /* Its head is never scannable: decoded for the checksum only. */
    bool head_decided;
    bool track_crc; /* Keep the CRC-32 of every decoded byte in crc. */
    bool allow_excess; /* Decode on past expected_size, counting only, for the CRC. */
    bool excess;       /* More output than expected_size: never scanned. */
    bool unavailable;  /* It needs a temporary file and has none (or no room): skipped. */
    bool iso_pending;  /* Kept until "CD001" at 0x8001 decides it. */
    uint8_t iso[6];
    uint32_t crc;
    uint64_t room;     /* Temporary-file size up to which free space was checked. */
    size_t head_size;
    const char *name;
    const char *method; /* Static label handed to the published member. */
    xx_pd_struct *pd;
    uint8_t head[XX_SCAN_ARCHIVE_HEAD_SIZE];
} xx_scan_archive_sink;

static bool xx_scan_archive_error(xx_pd_struct *pd, int error, const char *message)
{
    if (pd && !pd->last_error && !xx_pd_is_stopped(pd)) xx_pd_set_error(pd, error, message);
    return false;
}

static bool xx_scan_archive_stopped(xx_pd_struct *pd)
{
    return xx_pd_is_stopped(pd) || (pd && pd->last_error);
}

static bool xx_scan_archive_sink_error(xx_scan_archive_sink *sink, int error, const char *message)
{
    sink->failed = true;
    return xx_scan_archive_error(sink->pd, error, message);
}

/* The member needs a temporary file it cannot have: it cannot be unpacked,
 * as when XBinary::createFileBuffer returns nullptr and XScanEngine skips the
 * member. It is skipped without a report, never kept in memory instead, and
 * the walk goes on. Returns false, to stop the decoder. */
static bool xx_scan_archive_unavailable(xx_scan_archive_sink *sink)
{
    sink->unavailable = true;
    sink->discard = true;
    return false;
}

/* Whether the temporary volume takes size more bytes, and step more when the
 * final size is unknown, for a temporary file that reaches total bytes (its
 * declared size, or where output of unknown size checks again): from
 * XX_SCAN_ARCHIVE_TEMP_RESERVE_FROM on it must also keep
 * XX_SCAN_ARCHIVE_TEMP_RESERVE free. In a memory-only scope temporary streams
 * are RAM within the scope's own ceiling, which the writes enforce past size.
 * When the free space cannot be read, the writes decide. */
static bool xx_scan_archive_temp_room(uint64_t size, uint64_t step, uint64_t total)
{
    uint64_t available, reserve = total >= XX_SCAN_ARCHIVE_TEMP_RESERVE_FROM ? XX_SCAN_ARCHIVE_TEMP_RESERVE : 0U;
    if (xx_io_memory_only_active()) return size <= xx_io_memory_only_available();
    if (!xx_io_temp_free_space(&available)) return true;
    return available >= reserve && available - reserve >= size && available - reserve - size >= step;
}

/* Where output of unknown size that needs required bytes checks for room
 * again. While it is smaller than XX_SCAN_ARCHIVE_TEMP_RESERVE_FROM it needs
 * room only for the bytes it writes, so each write checks; from that size on,
 * with the reserve, one XX_SCAN_ARCHIVE_TEMP_STEP on. (A memory-only scope
 * keeps no reserve and checks every step.) */
static uint64_t xx_scan_archive_next_room(uint64_t required)
{
    if (required < XX_SCAN_ARCHIVE_TEMP_RESERVE_FROM && !xx_io_memory_only_active()) return required;
    return required > UINT64_MAX - XX_SCAN_ARCHIVE_TEMP_STEP ? UINT64_MAX : required + XX_SCAN_ARCHIVE_TEMP_STEP;
}

/* A short temporary-file write (a full disk, a race with another program)
 * leaves the member unavailable, never a failed walk. Do not retry a buffered
 * file write after an error: stdio may already have discarded buffered bytes. */
static bool xx_scan_archive_temp_write(xx_scan_archive_sink *sink, xx_io_device *temporary, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    while (size) {
        size_t amount = size > 65536U ? 65536U : size;
        if (xx_scan_archive_stopped(sink->pd)) return false;
        if (xx_io_write(temporary, bytes, amount) != (ssize_t)amount) return xx_scan_archive_unavailable(sink);
        bytes += amount;
        size -= amount;
    }
    return true;
}

/* Continue the member in a temporary file with room up to room bytes: its
 * declared size (size), or, when the final size is unknown, past the size
 * bytes kept so far up to xx_scan_archive_next_room. Without one it is
 * unavailable. */
static bool xx_scan_archive_spill(xx_scan_archive_sink *sink, uint64_t size, uint64_t room)
{
    xx_io_device *temporary;
    if (!xx_scan_archive_temp_room(size, room - size, room) || !(temporary = xx_io_temp_open())) return xx_scan_archive_unavailable(sink);
    sink->room = room;
    if (!xx_scan_archive_temp_write(sink, temporary, sink->data, (size_t)sink->stored)) {
        xx_io_close(temporary);
        return false;
    }
    xx_mem_free(sink->data);
    sink->data = NULL;
    sink->capacity = 0;
    sink->temporary = temporary;
    return true;
}

static bool xx_scan_archive_has_suffix_ci(const char *name, const char *suffix)
{
    size_t nlen, slen, i;
    if (!name || !suffix) return false;
    nlen = strlen(name);
    slen = strlen(suffix);
    if (nlen < slen) return false;
    name += (nlen - slen);
    for (i = 0; i < slen; ++i) {
        char c1 = name[i];
        char c2 = suffix[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + ('a' - 'A'));
        if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + ('a' - 'A'));
        if (c1 != c2) return false;
    }
    return true;
}

/* A TAR header block whose checksum holds, as XTAR's record walk requires. */
static bool xx_scan_archive_is_tar_header(const uint8_t *data, size_t size)
{
    uint32_t stored = 0, sum = 0;
    int32_t signed_sum = 0;
    size_t i;
    bool digits = false;
    if (size < 512) return false;
    if (!memcmp(data + 257, "ustar", 5)) return true;
    for (i = 148; i < 156 && (data[i] == ' ' || data[i] == '\0'); ++i) continue;
    for (; i < 156 && data[i] >= '0' && data[i] <= '7'; ++i) {
        stored = (stored << 3) | (uint32_t)(data[i] - '0');
        digits = true;
    }
    if (!digits) return false;
    for (i = 0; i < 512; ++i) {
        uint8_t value = i >= 148 && i < 156 ? (uint8_t)' ' : data[i];
        sum += value;
        signed_sum += (int8_t)value;
    }
    return stored == sum || stored == (uint32_t)signed_sum;
}

/* A cheap superset of XScanEngine::isScanable over the first
 * XX_SCAN_ARCHIVE_HEAD_SIZE bytes (or the whole member when it is shorter):
 * the engine's type check decides only for the members kept here. */
static bool xx_scan_archive_is_scanable_header(const uint8_t *data, size_t size, const char *name)
{
    size_t index;
    if (size >= 5) {
        /* PDF, also behind a BOM or other bytes within the first 1024. */
        if (data[0] == '%' && data[1] == 'P' && data[2] == 'D' && data[3] == 'F') return true;
        for (index = 1; index <= 1024U && index + 5U <= size; ++index)
            if (!memcmp(data + index, "%PDF-", 5)) return true;
    }
    if (xx_scan_archive_is_tar_header(data, size)) return true;
    if (size < 2) return false;
    /* MZ / ZM: DOS, PE, PE32, PE64, .NET, NE, LE, LX */
    if ((data[0] == 'M' && data[1] == 'Z') || (data[0] == 'Z' && data[1] == 'M')) return true;
    /* GZ / NPM */
    if (data[0] == 0x1f && data[1] == 0x8b) return true;
    /* BZ2 */
    if (size >= 3 && data[0] == 'B' && data[1] == 'Z' && data[2] == 'h') return true;
    /* ELF */
    if (size >= 4 && data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') return true;
    /* DEX */
    if (size >= 4 && data[0] == 'd' && data[1] == 'e' && data[2] == 'x' && data[3] == '\n') return true;
    /* PDF */
    if (size >= 4 && data[0] == '%' && data[1] == 'P' && data[2] == 'D' && data[3] == 'F') return true;
    /* ZIP / JAR / APK / IPA: PK\x03\x04, PK\x05\x06, PK\x07\x08 */
    if (size >= 4 && data[0] == 'P' && data[1] == 'K' &&
        (data[2] == 0x03 || data[2] == 0x05 || data[2] == 0x07) &&
        (data[3] == 0x04 || data[3] == 0x06 || data[3] == 0x08)) return true;
    /* RAR */
    if (size >= 4 && data[0] == 'R' && data[1] == 'a' && data[2] == 'r' && data[3] == '!') return true;
    /* 7Z */
    if (size >= 4 && data[0] == '7' && data[1] == 'z' && data[2] == 0xbc && data[3] == 0xaf) return true;
    /* CAB */
    if (size >= 4 && data[0] == 'M' && data[1] == 'S' && data[2] == 'C' && data[3] == 'F') return true;
    /* XZ */
    if (size >= 6 && data[0] == 0xfd && data[1] == '7' && data[2] == 'z' && data[3] == 'X' && data[4] == 'Z' && data[5] == 0x00) return true;
    /* Mach-O 32/64 and Fat */
    if (size >= 4) {
        if (!memcmp(data, "\xFE\xED\xFA\xCE", 4) || !memcmp(data, "\xCE\xFA\xED\xFE", 4) ||
            !memcmp(data, "\xFE\xED\xFA\xCF", 4) || !memcmp(data, "\xCF\xFA\xED\xFE", 4) ||
            !memcmp(data, "\xCA\xFE\xBA\xBE", 4) || !memcmp(data, "\xBE\xBA\xFE\xCA", 4) ||
            !memcmp(data, "\xCA\xFE\xBA\xBF", 4) || !memcmp(data, "\xBF\xBA\xFE\xCA", 4)) {
            return true;
        }
    }
    /* LHA: header method at offset 2 */
    if (size >= 7 && data[2] == '-' && data[3] == 'l' && (data[4] == 'h' || data[4] == 'z') && data[6] == '-') {
        return true;
    }
    /* compress (a .tar.Z is FT_ARCHIVE), RIB and SPIS/TCompress */
    if (data[0] == 0x1f && data[1] == 0x9d) return true;
    if (size >= 8 && !memcmp(data, "RIB\0", 4)) return true;
    if (size >= 21 && !memcmp(data, "SPIS\x1A", 5)) return true;
    /* Name-based fallbacks for formats where magic is not at offset 0 */
    if (name) {
        if (xx_scan_archive_has_suffix_ci(name, ".tar") ||
            xx_scan_archive_has_suffix_ci(name, ".com") ||
            xx_scan_archive_has_suffix_ci(name, ".pdf") ||
            xx_scan_archive_has_suffix_ci(name, ".iso")) {
            return true;
        }
    }
    return false;
}

/* xx_scan_archive_is_scanable_header for a stored member read in place,
 * including the ISO 9660 descriptor at 0x8001. */
static bool xx_scan_archive_is_scanable_at(xx_io_device *device, int64_t offset, uint64_t size, const char *name)
{
    uint8_t head[XX_SCAN_ARCHIVE_HEAD_SIZE];
    size_t head_size = size < XX_SCAN_ARCHIVE_HEAD_SIZE ? (size_t)size : XX_SCAN_ARCHIVE_HEAD_SIZE;
    if (!xx_io_read_at(device, offset, head, head_size)) return false;
    if (xx_scan_archive_is_scanable_header(head, head_size, name)) return true;
    return size >= XX_SCAN_ARCHIVE_ISO_END && xx_io_read_at(device, offset + XX_SCAN_ARCHIVE_ISO_OFFSET, head, 6) && !memcmp(head, "CD001\x01", 6);
}

/* Keep size more decoded bytes. A member stays in memory while it is smaller
 * than the file buffer size (xx_get_file_buffer_size()); one that reaches it,
 * or is known to, continues in a temporary file, so memory never holds that
 * much. A temporary file of unknown final size keeps checking for room. */
static bool xx_scan_archive_store(xx_scan_archive_sink *sink, const void *data, size_t size)
{
    uint64_t required = sink->stored + (uint64_t)size;
    size_t capacity;
    uint8_t *grown;
    if (!size) return true;
    if (!sink->temporary && required >= sink->memory_threshold &&
        !(sink->expected_size != UINT64_MAX ? xx_scan_archive_spill(sink, sink->expected_size, sink->expected_size) :
                                              xx_scan_archive_spill(sink, required, xx_scan_archive_next_room(required))))
        return false;
    if (sink->temporary) {
        if (required > sink->room) {
            /* The bytes already written are no longer free. */
            uint64_t room = xx_scan_archive_next_room(required);
            if (!xx_scan_archive_temp_room(required - sink->stored, room - required, room)) return xx_scan_archive_unavailable(sink);
            sink->room = room;
        }
        if (!xx_scan_archive_temp_write(sink, sink->temporary, data, size)) return false;
        sink->stored = required;
        return true;
    }
    if (required > (uint64_t)SIZE_MAX)
        return xx_scan_archive_sink_error(sink, XXFC_ERR_OUT_OF_BOUNDS, "Archive memory size cannot be represented");
    if (required > sink->capacity) {
        capacity = sink->capacity ? sink->capacity : 4096U;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2U) { capacity = (size_t)required; break; }
            capacity *= 2U;
        }
        /* No more than the member can keep here: its declared size, and less
         * than the file buffer size, where it moves to a temporary file. */
        if (sink->expected_size != UINT64_MAX && (uint64_t)capacity > sink->expected_size) capacity = (size_t)sink->expected_size;
        if ((uint64_t)capacity >= sink->memory_threshold) capacity = (size_t)(sink->memory_threshold - 1U);
        if ((uint64_t)capacity > sink->limit) capacity = (size_t)sink->limit;
        if ((uint64_t)capacity < required) capacity = (size_t)required;
        grown = (uint8_t *)xx_mem_realloc(sink->data, capacity);
        if (!grown) return xx_scan_archive_sink_error(sink, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate decoded archive member");
        sink->data = grown;
        sink->capacity = capacity;
    }
    xx_mem_copy(sink->data + (size_t)sink->stored, data, size);
    sink->stored = required;
    return true;
}

/* Decide from the collected head whether the member may be scannable. A
 * member that can never be is still decoded to its end, into nothing, so its
 * checksum is verified as XScanEngine verifies every member it unpacks. */
static bool xx_scan_archive_decide(xx_scan_archive_sink *sink)
{
    sink->head_decided = true;
    if (sink->discard) return true;
    if (!xx_scan_archive_is_scanable_header(sink->head, sink->head_size, sink->name)) {
        if (sink->head_size == XX_SCAN_ARCHIVE_HEAD_SIZE &&
            (sink->expected_size == UINT64_MAX || sink->expected_size >= XX_SCAN_ARCHIVE_ISO_END)) {
            /* Possibly ISO 9660: kept until the bytes at 0x8001 decide. */
            sink->iso_pending = true;
        } else {
            sink->filtered = true;
            sink->discard = true;
            return true;
        }
    }
    return xx_scan_archive_store(sink, sink->head, sink->head_size);
}

/* Decoders write here. Without the scannable filter every byte is kept (or,
 * for discard, only counted); with it, the first XX_SCAN_ARCHIVE_HEAD_SIZE
 * bytes are held back until the head is decided, so a member that is never
 * scannable is never stored, whatever sizes the decoder writes in. */
static ssize_t xx_scan_archive_write(xx_io_device *device, const void *data, size_t size)
{
    xx_scan_archive_sink *sink = (xx_scan_archive_sink *)device->priv;
    const uint8_t *bytes = (const uint8_t *)data;
    size_t held = 0;
    if (sink->failed || sink->unavailable || xx_scan_archive_stopped(sink->pd)) return -1;
    if ((!data && size) || size > (size_t)PTRDIFF_MAX || sink->size > (uint64_t)INT64_MAX ||
        (uint64_t)size > (uint64_t)INT64_MAX - sink->size) {
        xx_scan_archive_sink_error(sink, XXFC_ERR_OUT_OF_BOUNDS, "Decoded archive member exceeds the scan size limit");
        return -1;
    }
    if (sink->size > sink->limit || (uint64_t)size > sink->limit - sink->size) {
        /* More output than the member declares (or than the stream limit):
         * the decoder fails and the member is treated as damaged. */
        sink->overflow = true;
        return -1;
    }
    if (!size) return 0;
    if (sink->track_crc) sink->crc = xx_crc32_calc(sink->crc, bytes, size);
    if (sink->allow_excess && !sink->excess && sink->expected_size != UINT64_MAX && (uint64_t)size > sink->expected_size - sink->size) {
        /* More output than declared: XDecompress decodes on to the end of
         * the stream and checks the CRC; such a member is never scanned. */
        sink->excess = true;
        sink->discard = true;
        sink->head_decided = true;
        sink->iso_pending = false;
    }
    if (!sink->head_decided) {
        held = XX_SCAN_ARCHIVE_HEAD_SIZE - sink->head_size;
        if (held > size) held = size;
        xx_mem_copy(sink->head + sink->head_size, bytes, held);
        sink->head_size += held;
        if (sink->head_size == XX_SCAN_ARCHIVE_HEAD_SIZE && !xx_scan_archive_decide(sink)) return -1;
    }
    if (held < size && !sink->discard && !xx_scan_archive_store(sink, bytes + held, size - held)) return -1;
    if (sink->iso_pending) {
        uint64_t position;
        for (position = XX_SCAN_ARCHIVE_ISO_OFFSET; position < XX_SCAN_ARCHIVE_ISO_END; ++position)
            if (position >= sink->size && position - sink->size < (uint64_t)size)
                sink->iso[position - XX_SCAN_ARCHIVE_ISO_OFFSET] = bytes[position - sink->size];
        if (sink->size + (uint64_t)size >= XX_SCAN_ARCHIVE_ISO_END) {
            sink->iso_pending = false;
            if (memcmp(sink->iso, "CD001\x01", 6) != 0) {
                sink->filtered = true;
                sink->discard = true;
            }
        }
    }
    sink->size += (uint64_t)size;
    return (ssize_t)size;
}

static int64_t xx_scan_archive_sink_size(xx_io_device *device)
{
    return (int64_t)((xx_scan_archive_sink *)device->priv)->size;
}

/* filter_scanable keeps a member only when its head can be scannable
 * (xx_scan_archive_is_scanable_header). A member of expected_size at or above
 * the file buffer size starts in a temporary file, opened before anything is
 * decoded, as XScanEngine creates the member's buffer before it unpacks:
 * when there is none, or no room for expected_size, the member is
 * unavailable and not decoded at all, not even for its CRC. Only a stop
 * request returns false. */
static bool xx_scan_archive_sink_init(xx_scan_archive_sink *sink, xx_io_device *device, uint64_t limit,
                                      uint64_t expected_size, bool discard, const char *name,
                                      bool filter_scanable, xx_pd_struct *pd)
{
    xx_mem_zero(sink, sizeof(*sink));
    xx_mem_zero(device, sizeof(*device));
    sink->limit = limit;
    sink->memory_threshold = (uint64_t)xx_get_file_buffer_size();
    sink->expected_size = expected_size;
    sink->discard = discard;
    sink->filter_scanable = filter_scanable && !discard;
    sink->head_decided = !sink->filter_scanable;
    sink->name = name;
    sink->pd = pd;
    device->priv = sink;
    device->write = xx_scan_archive_write;
    device->tell = xx_scan_archive_sink_size;
    device->total_size = xx_scan_archive_sink_size;
    if (!discard && expected_size != UINT64_MAX && expected_size >= sink->memory_threshold) (void)xx_scan_archive_spill(sink, expected_size, expected_size);
    return !xx_pd_is_stopped(pd);
}

/* After a complete decode: decide a member shorter than the head. */
static bool xx_scan_archive_sink_finish(xx_scan_archive_sink *sink)
{
    if (!sink->head_decided && !xx_scan_archive_decide(sink)) return false;
    if (sink->iso_pending) {
        /* Shorter than an ISO 9660 descriptor. */
        sink->iso_pending = false;
        sink->filtered = true;
        sink->discard = true;
    }
    return true;
}

/* A temporary file is scratch, deleted on close, and a published member was
 * read back from it before this point: a failed close never fails the walk.
 * (POSIX stdio may write a buffered tail only now, to a full disk, for a
 * member that was skipped or failed its CRC.) */
static bool xx_scan_archive_sink_cleanup(xx_scan_archive_sink *sink)
{
    if (sink->temporary) (void)xx_io_close(sink->temporary);
    sink->temporary = NULL;
    xx_mem_free(sink->data);
    sink->data = NULL;
    return true;
}

/* Whether a member of the declared size can have the storage it needs:
 * memory below the file buffer size, else a temporary file with room for it,
 * opened and closed here. XScanEngine creates a member's buffer before
 * XDecompress reports anything about it, so a member that cannot have one is
 * skipped without a report. */
static bool xx_scan_archive_storable(uint64_t declared, xx_pd_struct *pd)
{
    xx_scan_archive_sink sink;
    xx_io_device device;
    bool storable;
    if (declared == UINT64_MAX || declared < (uint64_t)xx_get_file_buffer_size()) return true;
    (void)xx_scan_archive_sink_init(&sink, &device, declared, declared, false, NULL, false, pd);
    storable = !sink.unavailable;
    (void)xx_scan_archive_sink_cleanup(&sink);
    return storable;
}

static bool xx_scan_archive_publish(xx_scan_archive_sink *sink, const char *name, int64_t offset, int64_t source_size,
                                    bool compressed, xx_scan_archive_callback callback, void *context, xx_pd_struct *pd)
{
    xx_scan_archive_member member;
    bool result;
    if (sink->failed || sink->unavailable || sink->discard || sink->stored != sink->size || xx_scan_archive_stopped(pd)) return false;
    if (sink->temporary) {
        /* Repositioning is also the update-stream write/read barrier. Only
         * expose the completed, verified extent to the scanner. POSIX stdio
         * may write its buffered tail only here: a failure (a full disk) is
         * a temporary file the member cannot have, skipped, and the walk goes
         * on. */
        if (xx_io_seek64(sink->temporary, 0, SEEK_SET) != 0 || xx_io_total_size(sink->temporary) != (int64_t)sink->size) {
            (void)xx_scan_archive_unavailable(sink);
            return !xx_scan_archive_stopped(pd);
        }
        member.device = xx_io_sub_open_ro(sink->temporary, 0, (int64_t)sink->size);
    } else member.device = xx_io_mem_open_ro(sink->data, (size_t)sink->size);
    if (!member.device) return xx_scan_archive_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create decoded archive member device");
    member.name = name;
    member.source_offset = offset;
    member.source_size = source_size;
    member.size = (int64_t)sink->size;
    member.is_compressed = compressed;
    member.method = sink->method;
    result = callback(context, &member, pd);
    xx_io_close(member.device);
    return result && !xx_scan_archive_stopped(pd);
}

static bool xx_scan_archive_is_zip(xx_file_type_t type)
{
    return type == XX_FILE_TYPE_ZIP || type == XX_FILE_TYPE_ZIP64 || type == XX_FILE_TYPE_JAR ||
           type == XX_FILE_TYPE_APK || type == XX_FILE_TYPE_IPA;
}

/* The ZIP methods XZip maps to a decoder (XZip::zipToCompressMethod). For any
 * other method XDecompress reports "Unknown compression method: Unknown" and
 * XScanEngine does not scan the member, even where xx_zip could decode it. */
static bool xx_scan_archive_zip_method_known(uint64_t method)
{
    switch (method) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 8: case 9: case 12: case 14:
        case 93: case 95: case 96: case 97: case 98: case 99: return true;
        default: return false;
    }
}

/* The member label XScanEngine prints: XBinary::getHandleMethods of the
 * method XZip::zipToCompressMethod maps (flags pick the Implode variant).
 * NULL lets the result fall back to Deflate/Store. */
static const char *xx_scan_archive_zip_method_name(uint64_t method, uint64_t flags)
{
    static const char *const imploded[4] = {"Imploded 4K/2", "Imploded 8K/2", "Imploded 4K/3", "Imploded 8K/3"};
    switch (method) {
        case 1: return "Shrink";
        case 2: return "Reduce 1";
        case 3: return "Reduce 2";
        case 4: return "Reduce 3";
        case 5: return "Reduce 4";
        case 6: return imploded[(flags >> 1) & 3U];
        case 8: return "Deflate";
        case 9: return "Deflate64";
        case 12: return "Bzip2";
        case 14: return "LZMA";
        case 93: return "Zstandard";
        case 95: return "XZ";
        case 96: return "WinZip JPEG";
        case 97: return "WavPack";
        case 98: return "PPMD8";
        default: return NULL;
    }
}

/* What becomes of one member. As in XScanEngine, damage inside a member
 * never fails the scan: the member is not scanned, and only a checksum
 * mismatch is reported (XDecompress sets "Invalid CRC"). Neither does a
 * member that cannot have the temporary file it needs, or whose temporary file
 * cannot be written or read back (skipped silently). A fault reading the
 * archive, an allocation failure or a stop request fails it. */
typedef enum xx_scan_member_status {
    XX_SCAN_MEMBER_OK,
    XX_SCAN_MEMBER_SKIPPED,
    XX_SCAN_MEMBER_INVALID_CRC,
    XX_SCAN_MEMBER_FAILED
} xx_scan_member_status;

/* A member decoded completely but to the wrong checksum, as the readers say. */
static bool xx_scan_archive_is_crc_failure(const xx_pd_struct *pd)
{
    return pd && pd->last_error &&
           (!xx_rt_strcmp(pd->error_string, xx_scan_archive_zip_crc_mismatch) || !xx_rt_strcmp(pd->error_string, "LHA member payload CRC mismatch"));
}

/* A reader's error about one member, or about the rest of the directory, is
 * no scan error. A stop request is kept. */
static void xx_scan_archive_forgive(xx_pd_struct *pd)
{
    if (pd && !xx_pd_is_stopped(pd)) {
        pd->last_error = 0;
        pd->error_string[0] = '\0';
    }
}

/* A stored member is handed to the scanner as a view of the source, so its
 * checksum is verified here, before the callback, as a decoded member's is by
 * the decoder. ZIP Store: the record's CRC-32 over the payload (no size limit
 * applies, the view holds no copy). LHA LH0/LZ4: the reader's own
 * copy-and-verify into a discarding sink, which keeps nothing. */
static xx_scan_member_status xx_scan_archive_verify_stored(Abstractformat *format, xx_file_type_t type, xx_archive_record_state *state,
                                                           const xx_archive_record *record, uint64_t decoded_size, xx_pd_struct *pd)
{
    if (xx_scan_archive_is_zip(type)) {
        uint64_t expected = xx_archive_record_get_meta_u64(record, XX_META_ID_CRC32, UINT64_MAX);
        if (expected > UINT32_MAX) return XX_SCAN_MEMBER_SKIPPED;
        if (xx_crc32_device(format->device, record->data_offset, record->compressed_size, 0U) != (uint32_t)expected)
            return xx_scan_archive_stopped(pd) ? XX_SCAN_MEMBER_FAILED : XX_SCAN_MEMBER_INVALID_CRC;
        return xx_scan_archive_stopped(pd) ? XX_SCAN_MEMBER_FAILED : XX_SCAN_MEMBER_OK;
    }
    if (type == XX_FILE_TYPE_LHA) {
        xx_scan_archive_sink sink;
        xx_io_device destination;
        bool verified, crc;
        if (!xx_scan_archive_sink_init(&sink, &destination, decoded_size, UINT64_MAX, true, NULL, false, pd)) {
            xx_scan_archive_sink_cleanup(&sink);
            return XX_SCAN_MEMBER_FAILED;
        }
        verified = xx_lha_unpack_current_archive_record_to_device(format, state, &destination, pd) &&
                   !sink.failed && !sink.overflow && sink.size == decoded_size;
        xx_scan_archive_sink_cleanup(&sink);
        if (sink.failed || xx_pd_is_stopped(pd)) return XX_SCAN_MEMBER_FAILED;
        if (verified && !(pd && pd->last_error)) return XX_SCAN_MEMBER_OK;
        crc = xx_scan_archive_is_crc_failure(pd);
        xx_scan_archive_forgive(pd);
        return crc ? XX_SCAN_MEMBER_INVALID_CRC : XX_SCAN_MEMBER_SKIPPED;
    }
    return XX_SCAN_MEMBER_OK;
}

/* The ZIP methods decoded here as XDecompress decodes them: to the end of the
 * stream, whatever the declared size. Others go through the reader. */
static bool xx_scan_archive_zip_decodes_to_end(uint64_t method)
{
#if defined(XXFC_PROFILE_APK_EMUL)
    return method == 8U;
#else
    return (method >= 1U && method <= 6U) || method == 8U || method == 9U || method == 12U;
#endif
}

/* Decode one ZIP member into sink (initialised with the member limit and the
 * declared size) as XScanEngine does: the whole stream is decoded and its CRC
 * checked whatever its length. A wrong CRC is "Invalid CRC"; the right CRC
 * over a different size, or a stream that does not decode, is skipped
 * silently, except that Qt's Shrink, Reduce and Implode decoders report
 * success after a failure, so their failure is "Invalid CRC" too. A member
 * without the temporary file it needs is skipped silently, undecoded. */
static xx_scan_member_status xx_scan_archive_zip_decode(xx_scan_archive_sink *sink, xx_io_device *destination, xx_io_device *source,
                                                        int64_t offset, int64_t compressed, uint64_t method, uint64_t flags,
                                                        uint64_t declared, uint32_t crc, xx_pd_struct *pd)
{
    bool complete;
    if (sink->unavailable) return XX_SCAN_MEMBER_SKIPPED;
    sink->track_crc = true;
    sink->allow_excess = true;
    if (compressed == 0) {
        if (declared != 0U) return XX_SCAN_MEMBER_SKIPPED;
        complete = true;
    } else {
        switch (method) {
#if !defined(XXFC_PROFILE_APK_EMUL)
            case 1: complete = xx_shrink_unpack_device(source, offset, compressed, destination, (int64_t)declared, pd); break;
            case 2:
            case 3:
            case 4:
            case 5: complete = xx_reduce_unpack_device(source, offset, compressed, destination, (int64_t)declared, (int)method - 1, pd); break;
            case 6:
                complete = xx_implode_unpack_device(source, offset, compressed, destination, (int64_t)declared, (flags & 0x0002U) != 0U,
                                                    (flags & 0x0004U) != 0U, pd);
                break;
            case 9:
            case 12:
#endif
            case 8:
                complete = method == 12U ? xx_bzip2_unpack_device(source, offset, compressed, destination, pd) :
                                           xx_deflate_unpack_device(source, offset, compressed, destination, method == 9U, pd);
                break;
            default: return XX_SCAN_MEMBER_SKIPPED;
        }
    }
    if (sink->failed || xx_pd_is_stopped(pd)) return XX_SCAN_MEMBER_FAILED;
    complete = complete && !sink->overflow && !xx_scan_archive_stopped(pd);
    xx_scan_archive_forgive(pd);
    if (sink->overflow || sink->unavailable) return XX_SCAN_MEMBER_SKIPPED;
    if (complete) {
        if (sink->crc != crc) return XX_SCAN_MEMBER_INVALID_CRC;
        return sink->size == declared ? XX_SCAN_MEMBER_OK : XX_SCAN_MEMBER_SKIPPED;
    }
    return method >= 1U && method <= 6U ? XX_SCAN_MEMBER_INVALID_CRC : XX_SCAN_MEMBER_SKIPPED;
}

/* A ZIP without usable end records, which XZip reads from its local headers
 * as far as complete records reach (a stream, or an archive cut short).
 * Members follow the same rules as in xx_scan_archive_stream. cdie decodes
 * Store and Deflate members here; any other method is skipped silently. */
static bool xx_scan_archive_zip_local_stream(xx_io_device *device, int64_t origin, int64_t range_size, uint64_t member_limit,
                                             uint64_t total_limit, uint32_t count_limit, bool filter_scanable,
                                             xx_scan_archive_callback callback, xx_scan_archive_report_callback report,
                                             void *context, xx_pd_struct *pd)
{
    int64_t current = 0;
    uint64_t consumed = 0U;
    uint32_t count = 0U;
    char *name = (char *)xx_mem_alloc(65536U);
    bool result = false;
    if (!name) return xx_scan_archive_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate archive member name");
    while (count < count_limit && range_size - current >= 30) {
        uint8_t local[30];
        uint16_t flags, method, name_length;
        uint32_t crc, compressed, decoded;
        int64_t record_size, data;
        xx_scan_member_status status = XX_SCAN_MEMBER_OK;
        bool visited = true;
        if (xx_scan_archive_stopped(pd)) goto done;
        if (!xx_io_read_at(device, current, local, 30) || local[0] != 'P' || local[1] != 'K' || local[2] != 3U || local[3] != 4U) break;
        flags = (uint16_t)(local[6] | (local[7] << 8));
        method = (uint16_t)(local[8] | (local[9] << 8));
        crc = (uint32_t)local[14] | ((uint32_t)local[15] << 8) | ((uint32_t)local[16] << 16) | ((uint32_t)local[17] << 24);
        compressed = (uint32_t)local[18] | ((uint32_t)local[19] << 8) | ((uint32_t)local[20] << 16) | ((uint32_t)local[21] << 24);
        decoded = (uint32_t)local[22] | ((uint32_t)local[23] << 8) | ((uint32_t)local[24] << 16) | ((uint32_t)local[25] << 24);
        name_length = (uint16_t)(local[26] | (local[27] << 8));
        data = current + 30 + (int64_t)name_length + (int64_t)(uint16_t)(local[28] | (local[29] << 8));
        record_size = data - current + (int64_t)compressed;
        /* Without a central directory a data descriptor's sizes are unknown. */
        if (record_size > range_size - current || (flags & 8U) || !xx_io_read_at(device, current + 30, name, name_length)) break;
        name[name_length] = '\0';
        ++count;
        if (!(flags & 1U)) {
            uint64_t remaining = total_limit > consumed ? total_limit - consumed : 0;
            if (!xx_scan_archive_zip_method_known(method)) {
                if (report && xx_scan_archive_storable(decoded, pd)) report(context, xx_scan_archive_unknown_method);
                status = XX_SCAN_MEMBER_SKIPPED;
            } else if ((method != 0U && decoded > member_limit) || decoded > remaining) {
                status = XX_SCAN_MEMBER_SKIPPED;
            } else if (method == 0U) {
                xx_scan_archive_member member;
                if (compressed != decoded) status = XX_SCAN_MEMBER_SKIPPED;
                else if (xx_crc32_device(device, data, compressed, 0U) != crc) status = XX_SCAN_MEMBER_INVALID_CRC;
                if (status == XX_SCAN_MEMBER_OK && filter_scanable && !xx_scan_archive_is_scanable_at(device, data, decoded, name))
                    status = XX_SCAN_MEMBER_SKIPPED;
                if (status == XX_SCAN_MEMBER_OK) {
                    member.device = xx_io_sub_open_ro(device, data, compressed);
                    if (!member.device) {
                        xx_scan_archive_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create stored archive member view");
                        goto done;
                    }
                    member.name = name;
                    member.source_offset = origin + data;
                    member.source_size = compressed;
                    member.size = decoded;
                    member.is_compressed = false;
                    member.method = NULL;
                    visited = callback(context, &member, pd);
                    xx_io_close(member.device);
                }
            } else if (method == 8U || method == 9U) {
                xx_scan_archive_sink sink;
                xx_io_device destination;
                if (!xx_scan_archive_sink_init(&sink, &destination, member_limit, decoded, false, name, filter_scanable, pd)) {
                    xx_scan_archive_sink_cleanup(&sink);
                    goto done;
                }
                sink.method = xx_scan_archive_zip_method_name(method, flags);
                status = xx_scan_archive_zip_decode(&sink, &destination, device, data, compressed, method, flags, decoded, crc, pd);
                if (status == XX_SCAN_MEMBER_OK && xx_scan_archive_sink_finish(&sink) && !sink.filtered && !sink.discard)
                    visited = xx_scan_archive_publish(&sink, name, origin + data, compressed, true, callback, context, pd);
                if (!xx_scan_archive_sink_cleanup(&sink) || sink.failed || xx_pd_is_stopped(pd)) goto done;
            }
            if (status == XX_SCAN_MEMBER_INVALID_CRC && report) report(context, xx_scan_archive_invalid_crc);
            if (!visited) {
                result = !xx_scan_archive_stopped(pd);
                goto done;
            }
            if (xx_scan_archive_stopped(pd)) goto done;
            consumed += decoded;
        }
        current += record_size;
    }
    result = !xx_pd_is_stopped(pd);
done:
    xx_mem_free(name);
    return result;
}

/* ZIP (JAR/APK/IPA) and LHA members, in archive order, as XScanEngine walks
 * them: every member it would unpack is decoded or checksummed, a damaged or
 * undecodable one is skipped (report receives "Invalid CRC" or "Unknown
 * compression method: Unknown" where XScanEngine reports it), and only a member
 * whose head can be scannable is kept (filter_scanable) and handed to the
 * callback. A callback returning false ends the walk cleanly. So does an
 * unreadable directory or a damaged later record, after the members before it.
 * Only faults reading the archive, allocation failures and a stop request
 * fail; a member whose temporary file fails is skipped. */
static bool xx_scan_archive_stream(Abstractformat *format, xx_file_type_t type, int64_t origin, int64_t range_size,
                                   uint64_t member_limit, uint64_t total_limit, uint32_t count_limit,
                                   bool filter_scanable, xx_scan_archive_callback callback,
                                   xx_scan_archive_report_callback report, void *context, xx_pd_struct *pd)
{
    xx_archive_record limits;
    xx_archive_record_state *state = NULL;
    const xx_archive_record *record;
    uint64_t consumed = 0U;
    uint32_t count = 0U;
    bool result = false;
    char *owned_name = NULL;
    uint64_t memory_limit = member_limit > XX_SCAN_ARCHIVE_READER_MEMORY / 2U ? XX_SCAN_ARCHIVE_READER_MEMORY : member_limit * 2U;

    xx_archive_record_init(&limits);
    if (!xx_archive_record_set_meta_u64(&limits, XX_META_ID_OPT_MAX_MEMBER_SIZE, member_limit) ||
        !xx_archive_record_set_meta_u64(&limits, XX_META_ID_OPT_MEMORY_LIMIT, memory_limit)) {
        xx_scan_archive_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate archive scan limits");
        goto done;
    }

    state = xx_format_create_archive_records_reading(format, &limits.list_meta, pd);
    if (!state) {
        /* Records that cannot be read: nothing is expanded (initUnpack
         * fails), except that XZip reads a ZIP without end records from its
         * local headers. */
        if (!xx_pd_is_stopped(pd)) {
            xx_scan_archive_forgive(pd);
            result = !xx_scan_archive_is_zip(type) ||
                     xx_scan_archive_zip_local_stream(format->device, origin, range_size, member_limit, total_limit, count_limit, filter_scanable,
                                                      callback, report, context, pd);
        }
        goto done;
    }
    if (xx_scan_archive_stopped(pd)) goto done;
    while ((record = xx_format_get_current_archive_record(format, state)) != NULL) {
        uint64_t decoded_size, remaining, method;
        const char *name;
        xx_scan_member_status status = XX_SCAN_MEMBER_OK;
        bool visited = true;
        xx_str_free(owned_name);
        owned_name = NULL;
        if (xx_scan_archive_stopped(pd)) goto done;
        /* XScanEngine visits at most this many records of one archive. */
        if (count == count_limit) break;
        ++count;
        name = xx_archive_record_get_original_name(record);
        if (!name) {
            const wchar_t *wide_name = xx_archive_record_get_original_name_w(record);
            if (wide_name) {
                owned_name = xx_str_unicode_to_utf8(wide_name);
                if (!owned_name) {
                    xx_scan_archive_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot convert archive member name");
                    goto done;
                }
                name = owned_name;
            } else name = ""; /* XScanEngine prints a nameless member without a name. */
        }
        method = xx_archive_record_get_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, UINT64_MAX);
        decoded_size = xx_archive_record_get_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, UINT64_MAX);
        /* Without a password an encrypted member is not unpacked, silently. */
        if (record->compressed_size < 0 || decoded_size > (uint64_t)INT64_MAX ||
            xx_archive_record_get_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false))
            goto next;
        if (xx_scan_archive_is_zip(type) && !xx_scan_archive_zip_method_known(method)) {
            if (report && xx_scan_archive_storable(decoded_size, pd)) report(context, xx_scan_archive_unknown_method);
            goto next;
        }
        /* A caller's explicit limits (options.archive_max_*): a member over
         * them is skipped. The default member bound is beyond any size a ZIP
         * entry can declare. */
        remaining = total_limit > consumed ? total_limit - consumed : 0;
        if ((method != 0U && decoded_size > member_limit) || decoded_size > remaining) goto next;
        if (xx_scan_archive_is_zip(type) && record->data_offset < 0 &&
            !xx_zip_resolve_record_data_offset((xx_zip *)format, (xx_archive_record *)record)) {
            xx_scan_archive_forgive(pd);
            goto next;
        }
        if (record->data_offset < 0 || record->data_offset > range_size || record->compressed_size > range_size - record->data_offset) goto next;
        if ((xx_scan_archive_is_zip(type) && method == 0U) ||
            (type == XX_FILE_TYPE_LHA && !((xx_lha *)format)->sfx_boa_mask &&
             (method == UINT64_C(0x6c6830) || method == UINT64_C(0x6c7a34)))) {
            xx_scan_archive_member member;
            if (decoded_size != (uint64_t)record->compressed_size) goto next;
            status = xx_scan_archive_verify_stored(format, type, state, record, decoded_size, pd);
            if (status == XX_SCAN_MEMBER_FAILED) goto done;
            if (status == XX_SCAN_MEMBER_OK && filter_scanable &&
                !xx_scan_archive_is_scanable_at(format->device, record->data_offset, decoded_size, name))
                status = XX_SCAN_MEMBER_SKIPPED;
            if (status == XX_SCAN_MEMBER_OK) {
                member.device = xx_io_sub_open_ro(format->device, record->data_offset, record->compressed_size);
                if (!member.device) {
                    xx_scan_archive_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create stored archive member view");
                    goto done;
                }
                member.name = name;
                member.source_offset = origin + record->data_offset;
                member.source_size = record->compressed_size;
                member.size = (int64_t)decoded_size;
                member.is_compressed = false;
                member.method = NULL;
                visited = callback(context, &member, pd);
                xx_io_close(member.device);
            }
        } else {
            xx_scan_archive_sink sink;
            xx_io_device destination;
            bool decoded;
            bool to_end = xx_scan_archive_is_zip(type) && xx_scan_archive_zip_decodes_to_end(method);
            if (!xx_scan_archive_sink_init(&sink, &destination, to_end ? member_limit : decoded_size, decoded_size, false, name, filter_scanable, pd)) {
                xx_scan_archive_sink_cleanup(&sink);
                goto done;
            }
            if (xx_scan_archive_is_zip(type))
                sink.method = xx_scan_archive_zip_method_name(method, xx_archive_record_get_meta_u64(record, XX_META_ID_FLAGS, 0U));
            if (to_end) {
                uint64_t crc = xx_archive_record_get_meta_u64(record, XX_META_ID_CRC32, UINT64_MAX);
                status = crc > UINT32_MAX ? XX_SCAN_MEMBER_SKIPPED :
                                            xx_scan_archive_zip_decode(&sink, &destination, format->device, record->data_offset, record->compressed_size, method,
                                                                       xx_archive_record_get_meta_u64(record, XX_META_ID_FLAGS, 0U), decoded_size, (uint32_t)crc, pd);
                decoded = status == XX_SCAN_MEMBER_OK;
            } else if (sink.unavailable) {
                decoded = false; /* No temporary file for it: not decoded at all. */
            } else {
                decoded = type == XX_FILE_TYPE_LHA ? xx_lha_unpack_current_archive_record_to_device(format, state, &destination, pd) :
                                                    xx_zip_unpack_current_archive_record_to_device(format, state, &destination, pd);
                decoded = decoded && !sink.failed && !sink.unavailable && !sink.overflow && sink.size == decoded_size &&
                          !xx_scan_archive_stopped(pd);
            }
            if (decoded && xx_scan_archive_sink_finish(&sink) && !sink.filtered && !sink.discard)
                visited = xx_scan_archive_publish(&sink, name, origin + record->data_offset, record->compressed_size, true, callback, context, pd);
            if (!xx_scan_archive_sink_cleanup(&sink) || sink.failed || xx_pd_is_stopped(pd)) goto done;
            if (!decoded && !to_end) {
                status = !sink.unavailable && xx_scan_archive_is_crc_failure(pd) ? XX_SCAN_MEMBER_INVALID_CRC : XX_SCAN_MEMBER_SKIPPED;
                xx_scan_archive_forgive(pd);
            }
        }
        if (status == XX_SCAN_MEMBER_INVALID_CRC && report) report(context, xx_scan_archive_invalid_crc);
        if (!visited) {
            if (xx_scan_archive_stopped(pd)) goto done;
            result = true;
            goto done;
        }
        if (xx_scan_archive_stopped(pd)) goto done;
        consumed += decoded_size;
    next:
        if (!xx_format_archive_record_move_to_next(format, state, pd)) break;
    }
    /* The end, or a later record or directory that cannot be read: the walk
     * stops after the members before it, as XScanEngine's moveToNext does. */
    if (xx_pd_is_stopped(pd)) goto done;
    xx_scan_archive_forgive(pd);
    result = true;
done:
    xx_str_free(owned_name);
    if (state) xx_format_free_archive_records_reading(format, state);
    xx_archive_record_cleanup(&limits);
    return result || xx_scan_archive_error(pd, XXFC_ERR_INVALID_ARG, "Cannot enumerate archive members");
}

/* One logical member from concatenated GZIP/BZIP2 streams. A stream that does
 * not decode, decodes beyond the limit or cannot have the temporary file its
 * output grows into is skipped like a damaged member; a callback returning
 * false is a clean stop. */
static bool xx_scan_archive_compressed_stream(xx_io_device *view, xx_file_type_t type, int64_t origin,
                                              uint64_t limit, xx_scan_archive_callback callback, void *context, xx_pd_struct *pd)
{
    xx_scan_archive_sink sink;
    xx_io_device destination;
    int64_t stream_size = -1;
    bool decoded, result = true;
    if (!xx_scan_archive_sink_init(&sink, &destination, limit, UINT64_MAX, false, NULL, false, pd)) {
        xx_scan_archive_sink_cleanup(&sink);
        return false;
    }
    decoded = type == XX_FILE_TYPE_GZ ? xx_gz_decode_stream(view, 0, &destination, &stream_size, NULL, NULL, pd) :
                                       xx_bz2_decode_stream(view, 0, &destination, &stream_size, NULL, pd);
    decoded = decoded && !sink.failed && !sink.unavailable && !sink.overflow && stream_size > 0 && stream_size <= xx_io_total_size(view) &&
              !xx_scan_archive_stopped(pd);
    if (decoded)
        (void)xx_scan_archive_publish(&sink, type == XX_FILE_TYPE_GZ ? "gzip-content" : "bzip2-content",
                                      origin, stream_size, true, callback, context, pd);
    if (!xx_scan_archive_sink_cleanup(&sink) || sink.failed || xx_pd_is_stopped(pd)) result = false;
    else if (!decoded) xx_scan_archive_forgive(pd);
    else if (xx_scan_archive_stopped(pd)) result = false;
    return result || xx_scan_archive_error(pd, XXFC_ERR_INVALID_ARG, "Cannot decode complete compressed archive stream");
}

bool xx_scan_visit_archives_ex(xx_io_device *device, const xx_scan_options *options, xx_scan_archive_callback callback,
                               xx_scan_archive_report_callback report, void *context, xx_pd_struct *pd)
{
    xx_io_device *view = NULL;
    int64_t saved, total_size, range_size;
    uint64_t member_limit, total_limit;
    uint32_t count_limit;
    xx_file_type_t type;
    bool filter_scanable;
    bool result = false;
    if (!device || !options || !callback) return xx_scan_archive_error(pd, XXFC_ERR_NULL_PARAM, "Missing archive scan argument");
    if (xx_scan_archive_stopped(pd)) return false;
    if (options->offset < 0 || options->size < -1 || (options->size >= 0 && options->size > INT64_MAX - options->offset))
        return xx_scan_archive_error(pd, XXFC_ERR_INVALID_ARG, "Invalid archive scan range");
    type = options->file_type;
    if (type == XX_FILE_TYPE_NPM) type = XX_FILE_TYPE_GZ;
    if (!xx_scan_archive_is_zip(type) && type != XX_FILE_TYPE_LHA && type != XX_FILE_TYPE_GZ && type != XX_FILE_TYPE_BZ2)
        return true;
    filter_scanable = !options->aggressive_scan;
    member_limit = options->archive_max_member_size ? options->archive_max_member_size : XX_SCAN_ARCHIVE_MEMBER_DEFAULT;
    total_limit = options->archive_max_total_size ? options->archive_max_total_size : XX_SCAN_ARCHIVE_TOTAL_DEFAULT;
    count_limit = options->archive_max_members ? options->archive_max_members : XX_SCAN_ARCHIVE_COUNT_DEFAULT;
    saved = xx_io_tell(device);
    if (saved < 0) return xx_scan_archive_error(pd, XXFC_ERR_IO, "Cannot save archive scan input position");
    total_size = xx_io_total_size(device);
    if ((total_size >= 0 && (options->offset > total_size || (options->size >= 0 && options->size > total_size - options->offset))) ||
        (total_size < 0 && options->size < 0)) {
        xx_scan_archive_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Archive scan range exceeds input");
        goto done;
    }
    range_size = options->size >= 0 ? options->size : total_size - options->offset;
    view = xx_io_sub_open_ro(device, options->offset, range_size);
    if (!view) {
        xx_scan_archive_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create bounded archive scan view");
        goto done;
    }
    if (type == XX_FILE_TYPE_GZ || type == XX_FILE_TYPE_BZ2) {
        result = xx_scan_archive_compressed_stream(view, type, options->offset, member_limit < total_limit ? member_limit : total_limit,
                                                   callback, context, pd);
    } else if (type == XX_FILE_TYPE_LHA) {
        xx_lha reader;
        xx_lha_init(&reader, view, 0);
        result = xx_scan_archive_stream(&reader.format, type, options->offset, range_size, member_limit, total_limit, count_limit,
                                        filter_scanable, callback, report, context, pd);
        xx_lha_destroy(&reader);
    } else {
        xx_zip reader;
        xx_zip_init(&reader, view, 0);
        /* XZip finds the end records anywhere: a ZIP after other data, its
         * offsets counted from the file start (zip -A), is read as XZip reads it. */
        reader.allow_leading_data = true;
        result = xx_scan_archive_stream(&reader.format, type, options->offset, range_size, member_limit, total_limit, count_limit,
                                        filter_scanable, callback, report, context, pd);
        xx_zip_destroy(&reader);
    }
done:
    if (view) xx_io_close(view);
    if (xx_io_seek64(device, saved, SEEK_SET) != 0)
        return xx_scan_archive_error(pd, XXFC_ERR_IO, "Cannot restore archive scan input position");
    return result && !xx_scan_archive_stopped(pd);
}

bool xx_scan_visit_archives(xx_io_device *device, const xx_scan_options *options,
                            xx_scan_archive_callback callback, void *context, xx_pd_struct *pd)
{
    return xx_scan_visit_archives_ex(device, options, callback, NULL, context, pd);
}

