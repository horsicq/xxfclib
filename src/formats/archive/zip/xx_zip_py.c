/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* Python-zipfile-exact ZIP reader (see xx_zip_py.h): the archive, the
 * member reader with its output sink, and lzma.decompress() for XZ data. */

#include <xxfclib/formats/archive/zip/xx_zip_py.h>

#include <xxfclib/algo/bzip2/xx_bzip2.h>
#include <xxfclib/algo/crc/xx_crc.h>
#include <xxfclib/algo/deflate/xx_deflate.h>
#include <xxfclib/algo/lzma/xx_lzma.h>
#include <xxfclib/algo/sha/xx_sha.h>
#include <xxfclib/buf/xx_buf.h>
#include <xxfclib/io/xx_io.h>
#include <xxfclib/rt/xx_rt.h>

#include <string.h>

#if defined(_WIN64)
#include <windows.h>
#define XX_ZIP_PY_SPACE 1
#endif

#define XX_ZIP_PY_SIZE_END_CENT_DIR 22
#define XX_ZIP_PY_SIZE_END_CENT_DIR64 56
#define XX_ZIP_PY_SIZE_END_CENT_DIR64_LOCATOR 20
#define XX_ZIP_PY_SIZE_CENTRAL_DIR 46
#define XX_ZIP_PY_SIZE_FILE_HEADER 30
#define XX_ZIP_PY_MAX_COMMENT 0xFFFF

#define XX_ZIP_PY_MASK_ENCRYPTED 0x0001
#define XX_ZIP_PY_MASK_COMPRESSED_PATCH 0x0020
#define XX_ZIP_PY_MASK_STRONG_ENCRYPTION 0x0040
#define XX_ZIP_PY_MASK_UTF_FILENAME 0x0800

/* The most output reserved up front for one compressed member. The declared
   size can lie, so it is trusted only this far; a larger member grows from
   here. */
#define XX_ZIP_PY_RESERVE_MAX ((size_t)16 << 20)

/* ------------------------------------------------------------------ texts */

void xx_zip_py_free_text(char *text)
{
    xx_rt_free(text);
}

static char *xx_zip_py_strndup(const char *text, size_t length)
{
    char *copy = (char *)xx_rt_malloc(length + 1);
    if (!copy) return NULL;
    if (length) memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static char *xx_zip_py_strdup(const char *text)
{
    return xx_zip_py_strndup(text, strlen(text));
}

/* The concatenation of three texts (NULL when out of memory). */
static char *xx_zip_py_concat(const char *a, const char *b, const char *c)
{
    xx_buf_t out;
    xx_buf_init(&out);
    xx_buf_append_str(&out, a);
    xx_buf_append_str(&out, b);
    xx_buf_append_str(&out, c);
    return xx_buf_detach(&out, NULL);
}

/* CPython's cp437 codec, upper half. The lower half is ASCII. */
static const uint16_t xx_zip_py_cp437_high[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

static uint16_t xx_zip_py_rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t xx_zip_py_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t xx_zip_py_rd64(const uint8_t *p)
{
    return (uint64_t)xx_zip_py_rd32(p) | ((uint64_t)xx_zip_py_rd32(p + 4) << 32);
}

/* One UTF-8 code point as Python's strict codec accepts it: the bytes used
 * (>= 1) and the code point, or -1 in *code_point for an invalid sequence
 * (one byte is used then). Surrogates and overlong forms are invalid. */
static size_t xx_zip_py_utf8_decode(const uint8_t *text, size_t length, int32_t *code_point)
{
    uint32_t c = text[0], minimum;
    size_t needed;
    *code_point = -1;
    if (c < 0x80) {
        *code_point = (int32_t)c;
        return 1;
    }
    if (c >= 0xC2 && c <= 0xDF) {
        needed = 1;
        c &= 0x1F;
        minimum = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
        needed = 2;
        c &= 0x0F;
        minimum = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
        needed = 3;
        c &= 0x07;
        minimum = 0x10000;
    } else {
        return 1;
    }
    if (length < needed + 1) return 1;
    for (size_t i = 1; i <= needed; ++i) {
        if ((text[i] & 0xC0) != 0x80) return 1;
        c = (c << 6) | (text[i] & 0x3F);
    }
    if (c < minimum || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return 1;
    *code_point = (int32_t)c;
    return needed + 1;
}

static void xx_zip_py_utf8_encode(xx_buf_t *buffer, uint32_t code_point)
{
    char out[4];
    size_t length;
    if (code_point < 0x80) {
        out[0] = (char)code_point;
        length = 1;
    } else if (code_point < 0x800) {
        out[0] = (char)(0xC0 | (code_point >> 6));
        out[1] = (char)(0x80 | (code_point & 0x3F));
        length = 2;
    } else if (code_point < 0x10000) {
        out[0] = (char)(0xE0 | (code_point >> 12));
        out[1] = (char)(0x80 | ((code_point >> 6) & 0x3F));
        out[2] = (char)(0x80 | (code_point & 0x3F));
        length = 3;
    } else {
        out[0] = (char)(0xF0 | (code_point >> 18));
        out[1] = (char)(0x80 | ((code_point >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((code_point >> 6) & 0x3F));
        out[3] = (char)(0x80 | (code_point & 0x3F));
        length = 4;
    }
    xx_buf_append(buffer, out, length);
}

/* Decode a member name: UTF-8 (invalid bytes become U+FFFD each) or cp437.
 * NULL when out of memory. */
static char *xx_zip_py_decode_name(const uint8_t *raw, size_t length, bool utf8, size_t *out_length)
{
    xx_buf_t out;
    xx_buf_init(&out);
    for (size_t i = 0; i < length;) {
        if (utf8) {
            int32_t code_point;
            size_t used = xx_zip_py_utf8_decode(raw + i, length - i, &code_point);
            if (code_point < 0)
                xx_zip_py_utf8_encode(&out, 0xFFFD);
            else
                xx_buf_append(&out, raw + i, used);
            i += used;
        } else {
            uint8_t c = raw[i++];
            if (c < 0x80)
                xx_buf_append_char(&out, (char)c);
            else
                xx_zip_py_utf8_encode(&out, xx_zip_py_cp437_high[c - 0x80]);
        }
    }
    return xx_buf_detach(&out, out_length);
}

/* zipfile._sanitize_filename(): the name up to a NUL; '\\' becomes '/' on
 * Windows (os.sep). */
static char *xx_zip_py_sanitize(const char *name, size_t length)
{
    size_t end = 0;
    while (end < length && name[end]) ++end;
    char *result = xx_zip_py_strndup(name, end);
#ifdef _WIN32
    if (result)
        for (char *p = result; *p; ++p)
            if (*p == '\\') *p = '/';
#endif
    return result;
}

/* A readable stand-in for Python's repr() in error messages. */
static char *xx_zip_py_repr(const char *text, size_t length)
{
    static const char hex[] = "0123456789abcdef";
    xx_buf_t out;
    xx_buf_init(&out);
    xx_buf_append_char(&out, '\'');
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\'' || c == '\\') {
            xx_buf_append_char(&out, '\\');
            xx_buf_append_char(&out, (char)c);
        } else if (c < 0x20 || c == 0x7F) {
            char escape[4] = {'\\', 'x', hex[c >> 4], hex[c & 0x0F]};
            xx_buf_append(&out, escape, sizeof(escape));
        } else {
            xx_buf_append_char(&out, (char)c);
        }
    }
    xx_buf_append_char(&out, '\'');
    return xx_buf_detach(&out, NULL);
}

/* "<before><repr(text)><after>" */
static char *xx_zip_py_message(const char *before, const char *text, size_t length, const char *after)
{
    char *repr = xx_zip_py_repr(text, length), *message;
    if (!repr) return NULL;
    message = xx_zip_py_concat(before, repr, after);
    xx_rt_free(repr);
    return message;
}

/* Store a failure. A message that could not be allocated turns the status
 * into XX_ZIP_PY_NO_MEMORY. */
static xx_zip_py_status xx_zip_py_fail(char **error, xx_zip_py_status status, char *message)
{
    if (!message) status = XX_ZIP_PY_NO_MEMORY;
    if (error)
        *error = message;
    else
        xx_rt_free(message);
    return status;
}

static xx_zip_py_status xx_zip_py_fail_text(char **error, xx_zip_py_status status, const char *text)
{
    return xx_zip_py_fail(error, status, xx_zip_py_strdup(text));
}

/* ------------------------------------------------------------------ sink */

/* A write-only, growable memory device for the decoders. It keeps at most
 * `limit` bytes; once the limit is reached it refuses further output so the
 * decoder stops early (`full`). Memory grows with the output, so a member
 * that declares a huge size cannot force a huge allocation beyond what is
 * reserved up front. */
typedef struct xx_zip_py_sink {
    xx_io_device device;
    uint8_t *data;
    size_t size;
    size_t capacity; /* usable bytes at data (committed ones in a reservation) */
    size_t limit;
    size_t reserved; /* address space reserved at data; 0: heap memory */
    uint32_t crc;    /* CRC-32 of the bytes kept, when `checksum` is set */
    bool checksum;
    bool full;
    bool no_memory;
} xx_zip_py_sink;

/* Committed memory grows in steps of at least this much (and at most 64 MiB
   ahead of the output). Readers use only the `size` bytes written: there is
   no guaranteed slack past them (later writes may fill the committed part). */
#define XX_ZIP_PY_COMMIT_STEP ((size_t)1 << 20)
#define XX_ZIP_PY_COMMIT_AHEAD ((size_t)64 << 20)
#define XX_ZIP_PY_SLACK ((size_t)64 << 10)

#ifdef XX_ZIP_PY_SPACE
/* Make at least `needed` bytes usable in the reservation. */
static bool xx_zip_py_sink_commit(xx_zip_py_sink *sink, size_t needed)
{
    size_t step = sink->capacity < XX_ZIP_PY_COMMIT_STEP ? XX_ZIP_PY_COMMIT_STEP : sink->capacity;
    if (step > XX_ZIP_PY_COMMIT_AHEAD) step = XX_ZIP_PY_COMMIT_AHEAD;
    size_t target = needed + 4096;
    if (target < sink->capacity + step) target = sink->capacity + step;
    target = (target + 0xFFFF) & ~(size_t)0xFFFF;
    if (target > sink->reserved) target = sink->reserved;
    if (!VirtualAlloc(sink->data + sink->capacity, target - sink->capacity, MEM_COMMIT, PAGE_READWRITE)) return false;
    sink->capacity = target;
    return true;
}
#endif

/* Room for `needed` bytes in total. */
static bool xx_zip_py_sink_grow(xx_zip_py_sink *sink, size_t needed)
{
#ifdef XX_ZIP_PY_SPACE
    if (sink->reserved) {
        if (needed <= sink->reserved - 4096) return xx_zip_py_sink_commit(sink, needed);
        /* Beyond the reservation: the heap from here on. */
        size_t capacity = sink->reserved;
        while (capacity < needed) capacity = capacity > SIZE_MAX / 2 ? needed : capacity * 2;
        if (capacity > sink->limit) capacity = sink->limit;
        uint8_t *heap = (uint8_t *)xx_rt_malloc(capacity);
        if (!heap) return false;
        if (sink->size) memcpy(heap, sink->data, sink->size);
        VirtualFree(sink->data, 0, MEM_RELEASE);
        sink->data = heap;
        sink->capacity = capacity;
        sink->reserved = 0;
        return true;
    }
#endif
    size_t capacity = sink->capacity ? sink->capacity : 65536;
    while (capacity < needed) capacity = capacity > SIZE_MAX / 2 ? needed : capacity * 2;
    if (capacity > sink->limit) capacity = sink->limit;
    uint8_t *larger = (uint8_t *)xx_rt_realloc(sink->data, capacity);
    if (!larger) return false;
    sink->data = larger;
    sink->capacity = capacity;
    return true;
}

static ssize_t xx_zip_py_sink_write(xx_io_device *self, const void *buffer, size_t count)
{
    xx_zip_py_sink *sink = (xx_zip_py_sink *)self->priv;
    size_t room = sink->limit - sink->size, take = count < room ? count : room;
    if (take > sink->capacity - sink->size && !xx_zip_py_sink_grow(sink, sink->size + take)) {
        sink->no_memory = true;
        return -1;
    }
    if (take) {
        memcpy(sink->data + sink->size, buffer, take);
        /* The CRC-32 of the kept bytes, while they are still in cache:
           the same value as one pass over all of them afterwards. */
        if (sink->checksum) sink->crc = xx_crc32_calc(sink->crc, buffer, take);
    }
    sink->size += take;
    if (take < count) {
        sink->full = true;
        return -1;
    }
    return (ssize_t)count;
}

static int64_t xx_zip_py_sink_size(xx_io_device *self)
{
    return (int64_t)((xx_zip_py_sink *)self->priv)->size;
}

static int xx_zip_py_sink_seek64(xx_io_device *self, int64_t offset, int whence)
{
    /* Sequential output only: allow "seeks" that do not move. */
    xx_zip_py_sink *sink = (xx_zip_py_sink *)self->priv;
    int64_t target = whence == 0 ? offset : (int64_t)sink->size + offset;
    return target == (int64_t)sink->size ? 0 : -1;
}

static int xx_zip_py_sink_seek(xx_io_device *self, long offset, int whence)
{
    return xx_zip_py_sink_seek64(self, offset, whence);
}

static int xx_zip_py_sink_close(xx_io_device *self)
{
    (void)self;
    return 0;
}

static void xx_zip_py_sink_init(xx_zip_py_sink *sink, size_t limit)
{
    memset(sink, 0, sizeof(*sink));
    sink->limit = limit;
    sink->device.write = xx_zip_py_sink_write;
    sink->device.seek = xx_zip_py_sink_seek;
    sink->device.seek64 = xx_zip_py_sink_seek64;
    sink->device.tell = xx_zip_py_sink_size;
    sink->device.size = xx_zip_py_sink_size;
    sink->device.total_size = xx_zip_py_sink_size;
    sink->device.get_total_size = xx_zip_py_sink_size;
    sink->device.close = xx_zip_py_sink_close;
    sink->device.priv = sink;
}

/* Allocate `capacity` bytes (at most the limit) before decoding, for output
 * whose size is known. A failed allocation is not an error: the sink then
 * grows with the output, as without the reservation. */
static void xx_zip_py_sink_reserve(xx_zip_py_sink *sink, size_t capacity)
{
    if (capacity > sink->limit) capacity = sink->limit;
    if (capacity <= sink->capacity || sink->reserved) return;
    uint8_t *larger = (uint8_t *)xx_rt_realloc(sink->data, capacity);
    if (!larger) return;
    sink->data = larger;
    sink->capacity = capacity;
}

/* Reserve address space for `capacity` bytes (at most the limit) and commit
 * memory only as the output arrives, so large output is never copied while
 * it grows and a size that is never reached costs no memory. Output beyond
 * the reservation moves to the heap and grows as usual. Returns false when
 * this is not available (32-bit or non-Windows builds, or the reservation
 * failed); the sink is then unchanged. Call it before any output. */
static bool xx_zip_py_sink_reserve_space(xx_zip_py_sink *sink, size_t capacity)
{
#ifdef XX_ZIP_PY_SPACE
    if (capacity > sink->limit) capacity = sink->limit;
    if (sink->data || capacity > SIZE_MAX - 2 * XX_ZIP_PY_SLACK) return false;
    size_t reserved = (capacity + XX_ZIP_PY_SLACK + 0xFFFF) & ~(size_t)0xFFFF;
    void *base = VirtualAlloc(NULL, reserved, MEM_RESERVE, PAGE_READWRITE);
    if (!base) return false;
    sink->data = (uint8_t *)base;
    sink->capacity = 0;
    sink->reserved = reserved;
    return true;
#else
    (void)sink;
    (void)capacity;
    return false;
#endif
}

static void xx_zip_py_release(uint8_t *data, bool mapped)
{
#ifdef XX_ZIP_PY_SPACE
    if (mapped) {
        if (data) VirtualFree(data, 0, MEM_RELEASE);
        return;
    }
#else
    (void)mapped;
#endif
    xx_rt_free(data);
}

/* Detach the bytes (never NULL on success; NULL when out of memory). */
static uint8_t *xx_zip_py_sink_detach(xx_zip_py_sink *sink, size_t *size, bool *mapped)
{
    uint8_t *data = sink->data ? sink->data : (uint8_t *)xx_rt_malloc(1);
    *mapped = sink->data && sink->reserved;
    *size = data ? sink->size : 0;
    sink->data = NULL;
    sink->size = sink->capacity = sink->reserved = 0;
    return data;
}

static void xx_zip_py_sink_free(xx_zip_py_sink *sink)
{
    xx_zip_py_release(sink->data, sink->reserved != 0);
    sink->data = NULL;
    sink->size = sink->capacity = sink->reserved = 0;
}

void xx_zip_py_free(uint8_t *data, xx_zip_py_owner owner)
{
    if (owner != XX_ZIP_PY_BORROWED) xx_zip_py_release(data, owner == XX_ZIP_PY_MAPPED);
}

/* --------------------------------------------------------------- archive */

typedef struct xx_zip_py_end_record {
    bool zip64;
    uint64_t disk_number;
    uint64_t disk_start;
    uint64_t entries_this_disk;
    uint64_t entries_total;
    uint64_t size;
    uint64_t offset;
    uint64_t location;
} xx_zip_py_end_record;

static void xx_zip_py_parse_end(const uint8_t *p, size_t location, xx_zip_py_end_record *rec)
{
    memset(rec, 0, sizeof(*rec));
    rec->disk_number = xx_zip_py_rd16(p + 4);
    rec->disk_start = xx_zip_py_rd16(p + 6);
    rec->entries_this_disk = xx_zip_py_rd16(p + 8);
    rec->entries_total = xx_zip_py_rd16(p + 10);
    rec->size = xx_zip_py_rd32(p + 12);
    rec->offset = xx_zip_py_rd32(p + 16);
    rec->location = location;
}

/* zipfile._EndRecData64(): XX_ZIP_PY_OK or the failure (*error set when
 * error is not NULL). */
static xx_zip_py_status xx_zip_py_end_rec64(const uint8_t *data, xx_zip_py_end_record *rec, char **error)
{
    if (rec->location < XX_ZIP_PY_SIZE_END_CENT_DIR64_LOCATOR) return XX_ZIP_PY_OK;
    const uint8_t *locator = data + rec->location - XX_ZIP_PY_SIZE_END_CENT_DIR64_LOCATOR;
    if (memcmp(locator, "PK\x06\x07", 4) != 0) return XX_ZIP_PY_OK;
    uint32_t disk_number = xx_zip_py_rd32(locator + 4);
    uint32_t disks = xx_zip_py_rd32(locator + 16);
    if (disk_number != 0 || disks > 1) {
        if (!error) return XX_ZIP_PY_BAD;
        return xx_zip_py_fail_text(error, XX_ZIP_PY_BAD, "zipfiles that span multiple disks are not supported");
    }
    if (rec->location < XX_ZIP_PY_SIZE_END_CENT_DIR64_LOCATOR + XX_ZIP_PY_SIZE_END_CENT_DIR64) {
        if (!error) return XX_ZIP_PY_BAD;
        return xx_zip_py_fail_text(error, XX_ZIP_PY_BAD, "negative seek value");
    }
    const uint8_t *end64 = locator - XX_ZIP_PY_SIZE_END_CENT_DIR64;
    if (memcmp(end64, "PK\x06\x06", 4) != 0) return XX_ZIP_PY_OK;
    rec->zip64 = true;
    rec->disk_number = xx_zip_py_rd32(end64 + 16);
    rec->disk_start = xx_zip_py_rd32(end64 + 20);
    rec->entries_this_disk = xx_zip_py_rd64(end64 + 24);
    rec->entries_total = xx_zip_py_rd64(end64 + 32);
    rec->size = xx_zip_py_rd64(end64 + 40);
    rec->offset = xx_zip_py_rd64(end64 + 48);
    return XX_ZIP_PY_OK;
}

/* zipfile._EndRecData(): *found tells whether a record was found; a failure
 * status is BadZipFile (or out of memory for its text). */
static xx_zip_py_status xx_zip_py_end_rec(const uint8_t *data, size_t size, xx_zip_py_end_record *rec, bool *found,
                                          char **error)
{
    *found = false;
    if (size < XX_ZIP_PY_SIZE_END_CENT_DIR) return XX_ZIP_PY_OK;
    const uint8_t *tail = data + size - XX_ZIP_PY_SIZE_END_CENT_DIR;
    if (memcmp(tail, "PK\x05\x06", 4) == 0 && tail[20] == 0 && tail[21] == 0) {
        xx_zip_py_parse_end(tail, size - XX_ZIP_PY_SIZE_END_CENT_DIR, rec);
        *found = true;
        return xx_zip_py_end_rec64(data, rec, error);
    }
    size_t start = size > (size_t)XX_ZIP_PY_MAX_COMMENT + XX_ZIP_PY_SIZE_END_CENT_DIR
                       ? size - XX_ZIP_PY_MAX_COMMENT - XX_ZIP_PY_SIZE_END_CENT_DIR
                       : 0;
    /* data.rfind(stringEndArchive) */
    for (size_t at = size - 4 + 1; at-- > start;) {
        if (data[at] != 'P' || memcmp(data + at, "PK\x05\x06", 4) != 0) continue;
        if (size - at < XX_ZIP_PY_SIZE_END_CENT_DIR) return XX_ZIP_PY_OK;
        xx_zip_py_parse_end(data + at, at, rec);
        *found = true;
        return xx_zip_py_end_rec64(data, rec, error);
    }
    return XX_ZIP_PY_OK;
}

bool xx_zip_py_is_zipfile(const uint8_t *data, size_t size)
{
    xx_zip_py_end_record rec;
    bool found;
    return xx_zip_py_end_rec(data, size, &rec, &found, NULL) == XX_ZIP_PY_OK && found;
}

/* ZipInfo._decodeExtra(). CPython raises BadZipFile for a truncated extra
 * record, which aborts the whole archive; parsing just stops here. False
 * when out of memory. */
static bool xx_zip_py_decode_extra(xx_zip_py_info *info, const uint8_t *extra, size_t length, uint32_t name_crc)
{
    while (length >= 4) {
        uint16_t type = xx_zip_py_rd16(extra);
        size_t record = xx_zip_py_rd16(extra + 2);
        if (record + 4 > length) break;
        const uint8_t *p = extra + 4;
        if (type == 0x0001) {
            size_t left = record;
            if (info->file_size == 0xFFFFFFFFu || info->file_size == UINT64_MAX) {
                if (left < 8) goto next;
                info->file_size = xx_zip_py_rd64(p);
                p += 8;
                left -= 8;
            }
            if (info->compress_size == 0xFFFFFFFFu) {
                if (left < 8) goto next;
                info->compress_size = xx_zip_py_rd64(p);
                p += 8;
                left -= 8;
            }
            if (info->header_offset == 0xFFFFFFFF) {
                if (left < 8) goto next;
                uint64_t offset = xx_zip_py_rd64(p);
                info->header_offset = offset > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)offset;
            }
        } else if (type == 0x7075 && record >= 5) {
            /* Info-ZIP Unicode Path Extra Field */
            if (p[0] == 1 && xx_zip_py_rd32(p + 1) == name_crc && record > 5) {
                size_t name_length = record - 5, decoded_length = 0;
                char *decoded = xx_zip_py_decode_name(p + 5, name_length, true, &decoded_length);
                if (!decoded) return false;
                bool valid = true;
                for (size_t i = 0; i < name_length;) {
                    int32_t code_point;
                    i += xx_zip_py_utf8_decode(p + 5 + i, name_length - i, &code_point);
                    if (code_point < 0) valid = false;
                }
                if (valid) {
                    char *sanitized = xx_zip_py_sanitize(decoded, decoded_length);
                    if (!sanitized) {
                        xx_rt_free(decoded);
                        return false;
                    }
                    xx_rt_free(info->filename);
                    info->filename = sanitized;
                }
                xx_rt_free(decoded);
            }
        }
    next:
        extra += record + 4;
        length -= record + 4;
    }
    return true;
}

typedef struct xx_zip_py_order {
    int64_t offset;
    size_t index;
} xx_zip_py_order;

static int xx_zip_py_order_compare(const void *a, const void *b)
{
    const xx_zip_py_order *left = (const xx_zip_py_order *)a, *right = (const xx_zip_py_order *)b;
    if (left->offset != right->offset) return left->offset < right->offset ? -1 : 1;
    return left->index < right->index ? -1 : (left->index > right->index);
}

static xx_zip_py_status xx_zip_py_open_fail(xx_zip_py *zip, char **error, xx_zip_py_status status, const char *text)
{
    xx_zip_py_close(zip);
    return xx_zip_py_fail_text(error, status, text);
}

xx_zip_py_status xx_zip_py_open(xx_zip_py *zip, const uint8_t *data, size_t size, char **error)
{
    xx_zip_py_end_record rec;
    bool found = false;
    memset(zip, 0, sizeof(*zip));
    zip->data = data;
    zip->size = size;
    if (error) *error = NULL;

    xx_zip_py_status status = xx_zip_py_end_rec(data, size, &rec, &found, error);
    if (status != XX_ZIP_PY_OK) return status;
    if (!found) return xx_zip_py_fail_text(error, XX_ZIP_PY_BAD, "File is not a zip file");

    /* concat = location - size_cd - offset_cd [- zip64 records];
       start_dir = offset_cd + concat */
    uint64_t trailer = rec.zip64 ? XX_ZIP_PY_SIZE_END_CENT_DIR64 + XX_ZIP_PY_SIZE_END_CENT_DIR64_LOCATOR : 0;
    if (rec.size > rec.location || trailer > rec.location - rec.size)
        return xx_zip_py_fail_text(error, XX_ZIP_PY_BAD, "Bad offset for central directory");
    uint64_t start_dir = rec.location - rec.size - trailer;
    int64_t concat = (int64_t)start_dir - (rec.offset > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)rec.offset);

    const uint8_t *cd = data + start_dir;
    size_t cd_size = (size_t)(rec.size < size - start_dir ? rec.size : size - start_dir);
    size_t capacity = 0, position = 0;
    uint64_t total = 0;
    while (total < rec.size) {
        if (cd_size - position < XX_ZIP_PY_SIZE_CENTRAL_DIR)
            return xx_zip_py_open_fail(zip, error, XX_ZIP_PY_BAD, "Truncated central directory");
        const uint8_t *entry = cd + position;
        if (memcmp(entry, "PK\x01\x02", 4) != 0)
            return xx_zip_py_open_fail(zip, error, XX_ZIP_PY_BAD, "Bad magic number for central directory");
        size_t name_length = xx_zip_py_rd16(entry + 28), extra_length = xx_zip_py_rd16(entry + 30),
               comment_length = xx_zip_py_rd16(entry + 32);
        position += XX_ZIP_PY_SIZE_CENTRAL_DIR;
        /* io.BytesIO.read() returns short data at the end of the buffer. */
        size_t name_read = name_length < cd_size - position ? name_length : cd_size - position;
        const uint8_t *raw_name = cd + position;
        position += name_read;
        size_t extra_read = extra_length < cd_size - position ? extra_length : cd_size - position;
        const uint8_t *extra = cd + position;
        position += extra_read;
        position += comment_length < cd_size - position ? comment_length : cd_size - position;

        if (zip->count == capacity) {
            size_t larger = capacity ? capacity * 2 : 16;
            xx_zip_py_info *infos = (xx_zip_py_info *)xx_rt_realloc(zip->infos, larger * sizeof(*zip->infos));
            if (!infos) return xx_zip_py_open_fail(zip, error, XX_ZIP_PY_NO_MEMORY, "out of memory");
            zip->infos = infos;
            capacity = larger;
        }
        xx_zip_py_info *info = &zip->infos[zip->count++];
        memset(info, 0, sizeof(*info));
        info->flag_bits = xx_zip_py_rd16(entry + 8);
        info->orig_filename = xx_zip_py_decode_name(raw_name, name_read, (info->flag_bits & XX_ZIP_PY_MASK_UTF_FILENAME) != 0,
                                                    &info->orig_filename_length);
        if (info->orig_filename) info->filename = xx_zip_py_sanitize(info->orig_filename, info->orig_filename_length);
        if (!info->filename) return xx_zip_py_open_fail(zip, error, XX_ZIP_PY_NO_MEMORY, "out of memory");
        info->extract_version = entry[6];
        info->compress_type = xx_zip_py_rd16(entry + 10);
        info->crc = xx_zip_py_rd32(entry + 16);
        info->compress_size = xx_zip_py_rd32(entry + 20);
        info->file_size = xx_zip_py_rd32(entry + 24);
        info->header_offset = xx_zip_py_rd32(entry + 42);
        /* CPython rejects extract_version > 63 with NotImplementedError for
           the whole archive; the field is ignored here. */
        if (!xx_zip_py_decode_extra(info, extra, extra_read, xx_crc32_calc(0, raw_name, name_read)))
            return xx_zip_py_open_fail(zip, error, XX_ZIP_PY_NO_MEMORY, "out of memory");
        info->header_offset += concat;
        total += XX_ZIP_PY_SIZE_CENTRAL_DIR + name_length + extra_length + comment_length;
    }

    /* end_offset: the start of the next local header in offset order. */
    if (zip->count) {
        xx_zip_py_order *order = (xx_zip_py_order *)xx_rt_malloc(zip->count * sizeof(*order));
        if (!order) return xx_zip_py_open_fail(zip, error, XX_ZIP_PY_NO_MEMORY, "out of memory");
        for (size_t i = 0; i < zip->count; ++i) {
            order[i].offset = zip->infos[i].header_offset;
            order[i].index = i;
        }
        /* A total order: any sort gives the same result. */
        xx_rt_qsort(order, zip->count, sizeof(*order), xx_zip_py_order_compare);
        int64_t end_offset = (int64_t)start_dir;
        for (size_t i = zip->count; i-- > 0;) {
            zip->infos[order[i].index].end_offset = end_offset;
            end_offset = order[i].offset;
        }
        xx_rt_free(order);
    }
    return XX_ZIP_PY_OK;
}

void xx_zip_py_close(xx_zip_py *zip)
{
    for (size_t i = 0; i < zip->count; ++i) {
        xx_rt_free(zip->infos[i].filename);
        xx_rt_free(zip->infos[i].orig_filename);
    }
    xx_rt_free(zip->infos);
    zip->infos = NULL;
    zip->count = 0;
}

bool xx_zip_py_info_is_dir(const xx_zip_py_info *info)
{
    size_t length = strlen(info->filename);
    if (!length) return false;
#ifdef _WIN32
    return info->filename[length - 1] == '/' || info->filename[length - 1] == '\\';
#else
    return info->filename[length - 1] == '/';
#endif
}

xx_zip_py_status xx_zip_py_open_entry(const xx_zip_py *zip, const xx_zip_py_info *info, uint64_t *data_offset,
                                      char **warning, char **error)
{
    if (warning) *warning = NULL;
    if (error) *error = NULL;
    if (info->header_offset < 0 || (uint64_t)info->header_offset > zip->size ||
        zip->size - (size_t)info->header_offset < XX_ZIP_PY_SIZE_FILE_HEADER)
        return xx_zip_py_fail_text(error, XX_ZIP_PY_BAD, "Truncated file header");
    const uint8_t *header = zip->data + info->header_offset;
    if (memcmp(header, "PK\x03\x04", 4) != 0)
        return xx_zip_py_fail_text(error, XX_ZIP_PY_BAD, "Bad magic number for file header");
    size_t name_length = xx_zip_py_rd16(header + 26), extra_length = xx_zip_py_rd16(header + 28);
    size_t available = zip->size - (size_t)info->header_offset - XX_ZIP_PY_SIZE_FILE_HEADER;
    size_t name_read = name_length < available ? name_length : available;
    uint64_t position = (uint64_t)info->header_offset + XX_ZIP_PY_SIZE_FILE_HEADER + name_read + extra_length;

    if (info->flag_bits & XX_ZIP_PY_MASK_COMPRESSED_PATCH)
        return xx_zip_py_fail_text(error, XX_ZIP_PY_NOT_IMPLEMENTED, "compressed patched data (flag bit 5)");
    if (info->flag_bits & XX_ZIP_PY_MASK_STRONG_ENCRYPTION)
        return xx_zip_py_fail_text(error, XX_ZIP_PY_NOT_IMPLEMENTED, "strong encryption (flag bit 6)");

    size_t local_length = 0;
    char *local_name = xx_zip_py_decode_name(header + XX_ZIP_PY_SIZE_FILE_HEADER, name_read,
                                             (xx_zip_py_rd16(header + 6) & XX_ZIP_PY_MASK_UTF_FILENAME) != 0, &local_length);
    if (!local_name) return xx_zip_py_fail(error, XX_ZIP_PY_NO_MEMORY, NULL);
    bool same = local_length == info->orig_filename_length && memcmp(local_name, info->orig_filename, local_length) == 0;
    if (!same) {
        char *directory = xx_zip_py_repr(info->orig_filename, info->orig_filename_length);
        char *local = xx_zip_py_repr(local_name, local_length);
        char *message = NULL;
        if (directory && local) {
            xx_buf_t out;
            xx_buf_init(&out);
            xx_buf_append_str(&out, "File name in directory ");
            xx_buf_append_str(&out, directory);
            xx_buf_append_str(&out, " and header ");
            xx_buf_append_str(&out, local);
            xx_buf_append_str(&out, " differ.");
            message = xx_buf_detach(&out, NULL);
        }
        xx_rt_free(directory);
        xx_rt_free(local);
        xx_rt_free(local_name);
        return xx_zip_py_fail(error, XX_ZIP_PY_BAD, message);
    }
    xx_rt_free(local_name);

    if (position + info->compress_size < position || (int64_t)(position + info->compress_size) > info->end_offset ||
        position + info->compress_size > (uint64_t)INT64_MAX) {
        char *message = xx_zip_py_message("Overlapped entries: ", info->orig_filename, info->orig_filename_length,
                                          " (possible zip bomb)");
        if (info->end_offset != info->header_offset) return xx_zip_py_fail(error, XX_ZIP_PY_OVERLAPPED, message);
        if (!message) return xx_zip_py_fail(error, XX_ZIP_PY_NO_MEMORY, NULL);
        if (warning)
            *warning = message;
        else
            xx_rt_free(message);
    }

    if (info->flag_bits & XX_ZIP_PY_MASK_ENCRYPTED) {
        char *message = xx_zip_py_message("File ", info->filename, strlen(info->filename),
                                          " is encrypted, password required for extraction");
        return xx_zip_py_fail(error, XX_ZIP_PY_ENCRYPTED, message);
    }
    if (info->compress_type != XX_ZIP_PY_STORED && info->compress_type != XX_ZIP_PY_DEFLATED &&
        info->compress_type != XX_ZIP_PY_BZIP2 && info->compress_type != XX_ZIP_PY_LZMA) {
        return xx_zip_py_fail_text(error, XX_ZIP_PY_NOT_IMPLEMENTED, "That compression method is not supported");
    }
    *data_offset = position;
    return XX_ZIP_PY_OK;
}

/* `crc` is the CRC-32 of the member's bytes. */
static xx_zip_py_status xx_zip_py_check_crc(const xx_zip_py_info *info, uint32_t crc, char **error)
{
    if (crc == info->crc) return XX_ZIP_PY_OK;
    return xx_zip_py_fail(error, XX_ZIP_PY_BAD, xx_zip_py_message("Bad CRC-32 for file ", info->filename, strlen(info->filename), ""));
}

/* The most output a member's data can decompress to, for the reservation
   only: Deflate expands at most 1032:1. Others can go further; their output
   then moves from the reservation to the heap (one copy). */
static size_t xx_zip_py_output_bound(size_t input_size)
{
    size_t slack = (size_t)1 << 20;
    if (input_size > (SIZE_MAX - slack) / 1032) return SIZE_MAX;
    return input_size * 1032 + slack;
}

xx_zip_py_status xx_zip_py_read(const xx_zip_py *zip, const xx_zip_py_info *info, uint64_t data_offset, size_t limit,
                                uint8_t **out, size_t *out_size, bool *complete, xx_zip_py_owner *owner, char **error)
{
    *out = NULL;
    *out_size = 0;
    *complete = false;
    *owner = XX_ZIP_PY_OWNED;
    if (error) *error = NULL;

    size_t available = data_offset < zip->size ? zip->size - (size_t)data_offset : 0;
    const uint8_t *input = zip->data + (data_offset < zip->size ? data_offset : zip->size);
    bool input_truncated = info->compress_size > available;
    size_t input_size = input_truncated ? available : (size_t)info->compress_size;
    size_t target = info->file_size > SIZE_MAX ? SIZE_MAX : (size_t)info->file_size;
    size_t cap = (limit && limit < target) ? limit : target;
    bool limited = cap < target;

    if (info->compress_type == XX_ZIP_PY_STORED) {
        uint64_t total = info->compress_size < info->file_size ? info->compress_size : info->file_size;
        size_t wanted = total < cap ? (size_t)total : cap;
        if (wanted > available) return xx_zip_py_fail_text(error, XX_ZIP_PY_DATA_ERROR, "EOFError");
        /* The bytes already sit uncompressed in the archive: hand them out
           in place, no copy. The CRC-32 checks the very same bytes. */
        *out = (uint8_t *)input;
        *owner = XX_ZIP_PY_BORROWED;
        *out_size = wanted;
        *complete = wanted == total;
        if (!*complete) return XX_ZIP_PY_OK;
        xx_zip_py_status status = xx_zip_py_check_crc(info, xx_crc32_calc(0, input, wanted), error);
        if (status != XX_ZIP_PY_OK) {
            *out = NULL;
            *out_size = 0;
            *owner = XX_ZIP_PY_OWNED;
        }
        return status;
    }

    xx_zip_py_sink sink;
    xx_zip_py_sink_init(&sink, cap);
    sink.checksum = true;
    /* One allocation of the declared size instead of growing by doubling.
       A larger member gets address space for as much as its data can
       plausibly hold, committed as the output arrives: a lying size costs
       no memory, and the output is never copied while it grows. */
    size_t plausible = xx_zip_py_output_bound(input_size);
    if (plausible > cap) plausible = cap;
    if (plausible <= XX_ZIP_PY_RESERVE_MAX || !xx_zip_py_sink_reserve_space(&sink, plausible))
        xx_zip_py_sink_reserve(&sink, cap < XX_ZIP_PY_RESERVE_MAX ? cap : XX_ZIP_PY_RESERVE_MAX);
    bool ok = false;
    if (cap == 0) {
        ok = true; /* nothing is kept from a member declared empty */
    } else if (info->compress_type == XX_ZIP_PY_DEFLATED) {
        size_t consumed = 0;
        ok = input_size && xx_deflate_unpack_memory_to_device_ex(input, input_size, &sink.device, &consumed, false, NULL);
    } else if (info->compress_type == XX_ZIP_PY_BZIP2) {
        size_t consumed = 0;
        ok = input_size && xx_bzip2_unpack_memory_to_device_ex(input, input_size, &sink.device, &consumed, NULL);
    } else if (info->compress_type == XX_ZIP_PY_LZMA) {
        /* zipfile.LZMADecompressor: version (2), properties size (2),
           LZMA1 properties, then the raw stream. */
        if (input_size > 4) {
            size_t props_size = xx_zip_py_rd16(input + 2);
            if (input_size > 4 + props_size) {
                if (props_size != 5) {
                    xx_zip_py_sink_free(&sink);
                    return xx_zip_py_fail_text(error, XX_ZIP_PY_DATA_ERROR, "Invalid or unsupported options");
                }
                int64_t expected = (info->flag_bits & 0x0002) ? -1 : (int64_t)info->file_size;
                ok = xx_lzma_unpack_memory_to_device(input + 4 + props_size, input_size - 4 - props_size, input + 4,
                                                     props_size, expected, &sink.device, NULL);
            }
        }
    }

    if (sink.no_memory) {
        xx_zip_py_sink_free(&sink);
        return xx_zip_py_fail_text(error, XX_ZIP_PY_NO_MEMORY, "out of memory");
    }
    if (!sink.full && !ok && input_truncated) {
        xx_zip_py_sink_free(&sink);
        return xx_zip_py_fail_text(error, XX_ZIP_PY_DATA_ERROR, "EOFError");
    }
    /* A decoder failure without truncated input: CPython either raised a
       decompressor error or reached the end of the member's data, and in
       the latter case the CRC-32 decides. Both end the same way unless the
       partial output is exactly the member, which the CRC check accepts. */
    bool full = sink.full, mapped = false;
    uint32_t crc = sink.crc;
    *out = xx_zip_py_sink_detach(&sink, out_size, &mapped);
    if (!*out) return xx_zip_py_fail_text(error, XX_ZIP_PY_NO_MEMORY, "out of memory");
    *owner = mapped ? XX_ZIP_PY_MAPPED : XX_ZIP_PY_OWNED;
    *complete = !(full && limited);
    if (!*complete) return XX_ZIP_PY_OK;
    /* The sink's CRC-32 covers exactly the *out_size bytes it kept. */
    xx_zip_py_status status = xx_zip_py_check_crc(info, crc, error);
    if (status != XX_ZIP_PY_OK) {
        if (status == XX_ZIP_PY_BAD && !ok && !full) {
            if (error) xx_rt_free(*error);
            status = xx_zip_py_fail_text(error, XX_ZIP_PY_DATA_ERROR, "invalid compressed data");
        }
        xx_zip_py_free(*out, *owner);
        *out = NULL;
        *out_size = 0;
        *owner = XX_ZIP_PY_OWNED;
    }
    return status;
}

bool xx_zip_py_raw_data(const xx_zip_py *zip, const xx_zip_py_info *info, const uint8_t **data, size_t *size)
{
    if (info->header_offset < 0 || (uint64_t)info->header_offset > zip->size ||
        zip->size - (size_t)info->header_offset < XX_ZIP_PY_SIZE_FILE_HEADER)
        return false;
    const uint8_t *header = zip->data + info->header_offset;
    uint64_t offset =
        (uint64_t)info->header_offset + XX_ZIP_PY_SIZE_FILE_HEADER + xx_zip_py_rd16(header + 26) + xx_zip_py_rd16(header + 28);
    if (offset > zip->size) offset = zip->size;
    size_t available = zip->size - (size_t)offset;
    *data = zip->data + offset;
    *size = info->compress_size < available ? (size_t)info->compress_size : available;
    return true;
}

/* ------------------------------------------------------- lzma.decompress */

#define XX_ZIP_PY_XZ_FILTER_LZMA2 0x21

static const uint8_t xx_zip_py_xz_magic[6] = {0xFD, '7', 'z', 'X', 'Z', 0x00};

/* The first failure wins; returns false. */
static bool xx_zip_py_xz_fail(char **error, const char *message)
{
    if (error && !*error) *error = xx_zip_py_strdup(message);
    return false;
}

/* Variable-length integer of the .xz format. */
static bool xx_zip_py_xz_vli(const uint8_t *data, size_t end, size_t *position, uint64_t *value)
{
    uint64_t result = 0;
    for (unsigned i = 0; i < 9; ++i) {
        if (*position >= end) return false;
        uint8_t byte = data[(*position)++];
        result |= (uint64_t)(byte & 0x7F) << (7 * i);
        if (!(byte & 0x80)) {
            if (i && byte == 0) return false; /* not minimally encoded */
            *value = result;
            return true;
        }
    }
    return false;
}

static size_t xx_zip_py_xz_check_size(unsigned check)
{
    static const uint8_t sizes[16] = {0, 4, 4, 4, 8, 8, 8, 16, 16, 16, 32, 32, 32, 64, 64, 64};
    return sizes[check & 0x0F];
}

static bool xx_zip_py_xz_verify_check(unsigned check, const uint8_t *data, size_t size, const uint8_t *expected)
{
    switch (check) {
    case 0x01:
        return xx_crc32_calc(0, data, size) == xx_zip_py_rd32(expected);
    case 0x04:
        return xx_crc64_xz_calc(0, data, size) == xx_zip_py_rd64(expected);
    case 0x0A: {
        uint8_t digest[32];
        return xx_sha256_memory(data, size, digest) && memcmp(digest, expected, sizeof(digest)) == 0;
    }
    default:
        return true; /* liblzma skips check types it does not know */
    }
}

/* Walk the LZMA2 chunks of a block: the compressed extent (end marker
 * included) and the uncompressed size. */
static bool xx_zip_py_xz_lzma2_extent(const uint8_t *data, size_t size, size_t *compressed, uint64_t *uncompressed)
{
    size_t position = 0;
    uint64_t total = 0;
    for (;;) {
        if (position >= size) return false;
        uint8_t control = data[position];
        if (control == 0x00) {
            *compressed = position + 1;
            *uncompressed = total;
            return true;
        }
        if (control == 0x01 || control == 0x02) {
            if (size - position < 3) return false;
            size_t chunk = ((size_t)data[position + 1] << 8 | data[position + 2]) + 1;
            if (size - position - 3 < chunk) return false;
            position += 3 + chunk;
            total += chunk;
        } else if (control >= 0x80) {
            size_t header = control >= 0xC0 ? 6 : 5;
            if (size - position < header) return false;
            uint64_t unpacked =
                (((uint64_t)(control & 0x1F) << 16) | ((uint64_t)data[position + 1] << 8) | data[position + 2]) + 1;
            size_t packed = ((size_t)data[position + 3] << 8 | data[position + 4]) + 1;
            if (size - position - header < packed) return false;
            position += header + packed;
            total += unpacked;
        } else {
            return false;
        }
    }
}

typedef struct xx_zip_py_xz_record {
    uint64_t unpadded;
    uint64_t uncompressed;
} xx_zip_py_xz_record;

/* "unsupported XZ filter 0x<id in lower-case hex>" */
static void xx_zip_py_xz_filter_error(char **error, uint64_t id)
{
    static const char hex[] = "0123456789abcdef";
    char digits[17];
    size_t count = 0;
    if (!error || *error) return;
    do {
        digits[16 - ++count] = hex[id & 0x0F];
        id >>= 4;
    } while (id);
    digits[16] = '\0';
    *error = xx_zip_py_concat("unsupported XZ filter 0x", digits + 16 - count, "");
}

/* One .xz stream starting at data[0], appended to out. */
static bool xx_zip_py_xz_stream(const uint8_t *data, size_t size, size_t *used, xx_buf_t *out, char **error)
{
    xx_zip_py_xz_record *records = NULL;
    size_t record_count = 0, record_capacity = 0;
    bool ok = false;

    if (size < 12 || memcmp(data, xx_zip_py_xz_magic, 6) != 0)
        return xx_zip_py_xz_fail(error, "Input format not supported by decoder");
    if (data[6] != 0 || (data[7] & 0xF0) || xx_crc32_calc(0, data + 6, 2) != xx_zip_py_rd32(data + 8))
        return xx_zip_py_xz_fail(error, "Corrupt input data");
    unsigned check = data[7] & 0x0F;
    size_t check_size = xx_zip_py_xz_check_size(check), position = 12;

    for (;;) {
        if (position >= size) {
            xx_zip_py_xz_fail(error, "Compressed data ended before the end-of-stream marker was reached");
            goto done;
        }
        if (data[position] == 0x00) break; /* index indicator */

        size_t block_start = position, header_size = ((size_t)data[position] + 1) * 4;
        if (size - position < header_size ||
            xx_crc32_calc(0, data + position, header_size - 4) != xx_zip_py_rd32(data + position + header_size - 4)) {
            xx_zip_py_xz_fail(error, "Corrupt input data");
            goto done;
        }
        size_t header_end = position + header_size - 4, cursor = position + 2;
        uint8_t flags = data[position + 1];
        uint64_t declared_compressed = UINT64_MAX, declared_uncompressed = UINT64_MAX;
        if (flags & 0x3C) {
            xx_zip_py_xz_fail(error, "Unsupported options");
            goto done;
        }
        if ((flags & 0x40) && !xx_zip_py_xz_vli(data, header_end, &cursor, &declared_compressed)) {
            xx_zip_py_xz_fail(error, "Corrupt input data");
            goto done;
        }
        if ((flags & 0x80) && !xx_zip_py_xz_vli(data, header_end, &cursor, &declared_uncompressed)) {
            xx_zip_py_xz_fail(error, "Corrupt input data");
            goto done;
        }
        unsigned filters = (flags & 0x03) + 1;
        uint8_t lzma2_props = 0;
        for (unsigned i = 0; i < filters; ++i) {
            uint64_t id, props_size;
            if (!xx_zip_py_xz_vli(data, header_end, &cursor, &id) || !xx_zip_py_xz_vli(data, header_end, &cursor, &props_size) ||
                props_size > header_end - cursor) {
                xx_zip_py_xz_fail(error, "Corrupt input data");
                goto done;
            }
            if (id != XX_ZIP_PY_XZ_FILTER_LZMA2 || i + 1 != filters || props_size != 1) {
                xx_zip_py_xz_filter_error(error, id);
                goto done;
            }
            lzma2_props = data[cursor];
            cursor += (size_t)props_size;
        }
        for (; cursor < header_end; ++cursor)
            if (data[cursor]) {
                xx_zip_py_xz_fail(error, "Corrupt input data");
                goto done;
            }

        position = block_start + header_size;
        size_t compressed;
        uint64_t uncompressed;
        if (!xx_zip_py_xz_lzma2_extent(data + position, size - position, &compressed, &uncompressed) ||
            (declared_compressed != UINT64_MAX && declared_compressed != compressed) ||
            (declared_uncompressed != UINT64_MAX && declared_uncompressed != uncompressed) || uncompressed > SIZE_MAX) {
            xx_zip_py_xz_fail(error, "Corrupt input data");
            goto done;
        }
        size_t first = out->size;
        if (uncompressed) {
            uint8_t *block = (uint8_t *)xx_rt_malloc((size_t)uncompressed);
            size_t written = 0;
            if (!block) {
                xx_zip_py_xz_fail(error, "Cannot allocate memory");
                goto done;
            }
            bool decoded = xx_lzma2_decompress_memory(data + position, compressed, lzma2_props, block, (size_t)uncompressed,
                                                      &written) &&
                           written == uncompressed;
            if (decoded && !xx_buf_append(out, block, written)) {
                xx_rt_free(block);
                xx_zip_py_xz_fail(error, "Cannot allocate memory");
                goto done;
            }
            xx_rt_free(block);
            if (!decoded) {
                xx_zip_py_xz_fail(error, "Corrupt input data");
                goto done;
            }
        }
        position += compressed;
        size_t unpadded = header_size + compressed;
        while ((position - block_start) % 4) {
            if (position >= size || data[position]) {
                xx_zip_py_xz_fail(error, "Corrupt input data");
                goto done;
            }
            ++position;
        }
        if (size - position < check_size ||
            !xx_zip_py_xz_verify_check(check, (const uint8_t *)out->data + first, out->size - first, data + position)) {
            xx_zip_py_xz_fail(error, "Corrupt input data");
            goto done;
        }
        position += check_size;
        if (record_count == record_capacity) {
            size_t larger = record_capacity ? record_capacity * 2 : 8;
            xx_zip_py_xz_record *grown = (xx_zip_py_xz_record *)xx_rt_realloc(records, larger * sizeof(*records));
            if (!grown) {
                xx_zip_py_xz_fail(error, "Cannot allocate memory");
                goto done;
            }
            records = grown;
            record_capacity = larger;
        }
        records[record_count].unpadded = unpadded + check_size;
        records[record_count].uncompressed = uncompressed;
        ++record_count;
    }

    /* Index */
    size_t index_start = position++;
    uint64_t count;
    if (!xx_zip_py_xz_vli(data, size, &position, &count) || count != record_count) {
        xx_zip_py_xz_fail(error, "Corrupt input data");
        goto done;
    }
    for (size_t i = 0; i < record_count; ++i) {
        uint64_t unpadded, uncompressed;
        if (!xx_zip_py_xz_vli(data, size, &position, &unpadded) || !xx_zip_py_xz_vli(data, size, &position, &uncompressed) ||
            unpadded != records[i].unpadded || uncompressed != records[i].uncompressed) {
            xx_zip_py_xz_fail(error, "Corrupt input data");
            goto done;
        }
    }
    while ((position - index_start) % 4) {
        if (position >= size || data[position]) {
            xx_zip_py_xz_fail(error, "Corrupt input data");
            goto done;
        }
        ++position;
    }
    if (size - position < 4 + 12 ||
        xx_crc32_calc(0, data + index_start, position - index_start) != xx_zip_py_rd32(data + position)) {
        xx_zip_py_xz_fail(error, "Corrupt input data");
        goto done;
    }
    position += 4;

    /* Footer */
    const uint8_t *footer = data + position;
    if (memcmp(footer + 10, "YZ", 2) != 0 || xx_crc32_calc(0, footer + 4, 6) != xx_zip_py_rd32(footer) ||
        memcmp(footer + 8, data + 6, 2) != 0 ||
        ((uint64_t)xx_zip_py_rd32(footer + 4) + 1) * 4 != (uint64_t)(position - index_start)) {
        xx_zip_py_xz_fail(error, "Corrupt input data");
        goto done;
    }
    *used = position + 12;
    ok = true;
done:
    xx_rt_free(records);
    return ok;
}

/* .lzma (LZMA_Alone) as liblzma's picky auto decoder accepts it. */
static bool xx_zip_py_xz_alone(const uint8_t *data, size_t size, xx_buf_t *out, char **error)
{
    if (size < 13) return xx_zip_py_xz_fail(error, "Compressed data ended before the end-of-stream marker was reached");
    unsigned props = data[0], lc = props % 9, lp = (props / 9) % 5;
    uint32_t dictionary = xx_zip_py_rd32(data + 1);
    uint64_t declared = xx_zip_py_rd64(data + 5);
    if (props > (4 * 5 + 4) * 9 + 8 || lc + lp > 4) return xx_zip_py_xz_fail(error, "Input format not supported by decoder");
    if (dictionary != UINT32_MAX) {
        uint32_t d = dictionary - 1;
        d |= d >> 2;
        d |= d >> 3;
        d |= d >> 4;
        d |= d >> 8;
        d |= d >> 16;
        ++d;
        if (d != dictionary) return xx_zip_py_xz_fail(error, "Input format not supported by decoder");
    }
    if (declared != UINT64_MAX && declared >= ((uint64_t)1 << 38))
        return xx_zip_py_xz_fail(error, "Input format not supported by decoder");

    xx_zip_py_sink sink;
    xx_zip_py_sink_init(&sink, declared == UINT64_MAX || declared > SIZE_MAX ? SIZE_MAX : (size_t)declared);
    bool decoded = declared == 0 ||
                   (size > 13 && xx_lzma_unpack_memory_to_device(data + 13, size - 13, data, 5,
                                                                 declared == UINT64_MAX ? -1 : (int64_t)declared,
                                                                 &sink.device, NULL));
    if (!decoded || sink.no_memory) {
        xx_zip_py_sink_free(&sink);
        return xx_zip_py_xz_fail(error, sink.no_memory ? "Cannot allocate memory" : "Corrupt input data");
    }
    size_t length;
    bool mapped;
    uint8_t *bytes = xx_zip_py_sink_detach(&sink, &length, &mapped);
    if (!bytes) return xx_zip_py_xz_fail(error, "Cannot allocate memory");
    bool appended = xx_buf_append(out, bytes, length);
    xx_zip_py_release(bytes, mapped);
    return appended || xx_zip_py_xz_fail(error, "Cannot allocate memory");
}

bool xx_zip_py_xz_decompress(const uint8_t *data, size_t size, uint8_t **out, size_t *out_size, char **error)
{
    xx_buf_t result;
    *out = NULL;
    *out_size = 0;
    if (error) *error = NULL;
    xx_buf_init(&result);
    /* Allocated from the start: the checks read result.data even when no
       block produced output. */
    if (!xx_buf_reserve(&result, 0)) return xx_zip_py_xz_fail(error, "Cannot allocate memory");

    if (size >= 6 && memcmp(data, xx_zip_py_xz_magic, 6) == 0) {
        size_t used = 0;
        if (!xx_zip_py_xz_stream(data, size, &used, &result, error)) {
            xx_buf_free(&result);
            return false;
        }
        /* Further concatenated streams; anything else after a decoded
           stream is ignored, as lzma.decompress() does for invalid data. */
        while (used < size && size - used >= 6 && memcmp(data + used, xx_zip_py_xz_magic, 6) == 0) {
            size_t next = 0;
            size_t before = result.size;
            char *ignored = NULL;
            if (!xx_zip_py_xz_stream(data + used, size - used, &next, &result, &ignored)) {
                xx_rt_free(ignored);
                result.size = before;
                result.data[before] = '\0';
                break;
            }
            used += next;
        }
    } else if (!xx_zip_py_xz_alone(data, size, &result, error)) {
        xx_buf_free(&result);
        return false;
    }
    char *bytes = xx_buf_detach(&result, out_size);
    if (!bytes) {
        *out_size = 0;
        return xx_zip_py_xz_fail(error, "Cannot allocate memory");
    }
    *out = (uint8_t *)bytes;
    return true;
}
