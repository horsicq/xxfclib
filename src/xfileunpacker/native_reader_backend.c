/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#endif

#include "native_reader_backend.h"
#include "xxfc_readers.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xxfclib/io/xx_io.h>
#include <xxfclib/formats/xx_format.h>
#include <xxfclib/formats/other/base64/xx_base64.h>
#include <xxfclib/formats/archive/gz/xx_gz.h>
#include <xxfclib/formats/archive/bz2/xx_bz2.h>
#include <xxfclib/formats/archive/xz/xx_xz.h>
#include <xxfclib/formats/archive/lzma/xx_lzma.h>
#include <xxfclib/formats/archive/lzip/xx_lzip.h>
#include <xxfclib/formats/archive/lz4/xx_lz4.h>
#include <xxfclib/formats/archive/lzop/xx_lzop.h>
#include <xxfclib/formats/archive/zstd/xx_zstd.h>
#include <xxfclib/formats/archive/unixcompress/xx_unixcompress.h>
#include <xxfclib/formats/archive/uue/xx_uue.h>
#include <xxfclib/formats/archive/is_stored/xx_is_stored.h>
#include <xxfclib/strings/xx_string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#define XFU_NATIVE_READ_BUFFER 65536U
/* The decoded start of a nested layer that decides whether the whole layer
 * is decoded (xfu_native_open_inner_device), and the part of it whose bytes
 * are classified. */
#define XFU_NATIVE_PREFIX_BYTES (4U * 1024U * 1024U)
#define XFU_NATIVE_SAMPLE_BYTES 65536U

typedef struct native_alias {
    const char *selection;
    const char *reader;
} native_alias;

/* Format aliases describe equivalent container readers, not different wire
 * variants. The reader validates the actual input before listing members. */
static const native_alias native_aliases[] = {
    {"tar", "tar"}, {"pax", "tar"}, {"pax_restricted", "tar"},
    {"gnutar", "tar"}, {"ustar", "tar"}, {"v7tar", "tar"},
    {"tar_solaris", "tar"},
    {"tar_gz", "tar_gz"}, {"tar_bz2", "tar_bz2"},
    {"tar_xz", "tar_xz"}, {"tar_lzma", "tar_lzma"},
    {"tar_lzip", "tar_lzip"}, {"tar_lz4", "tar_lz4"},
    {"tar_lzop", "tar_lzop"}, {"tar_zstd", "tar_zstd"},
    {"tar_compress", "tar_compress"},
    {"cpio", "cpio"}, {"cpio_newc", "cpio"}, {"cpio_odc", "cpio"},
    {"cpio_bin", "cpio"}, {"cpio_bin_le", "cpio"},
    {"cpio_bin_be", "cpio"}, {"cpio_crc", "cpio"},
    {"cpio_afio", "cpio"}, {"cpio_pwb", "cpio"},
    {"ar", "ar"}, {"ar_bsd", "ar"}, {"ar_svr4", "ar"},
    {"ar_gnu", "ar"},
    {"mtree", "mtree"}, {"mtree_classic", "mtree"},
    {"iso9660", "iso9660"}, {"shar", "shar"},
    {"shar_dump", "shar"}, {"zip", "zip"},
    {"7zip", "7zip"}, {"warc", "warc"}, {"xar", "xar"},
    {"cab", "cab"}, {"lha", "lha"}, {"rar", "rar"},
    {"rar5", "rar"}, {"rpm", "rpm"},
    {"empty", "empty"},
    {"gzip", "gz"}, {"bzip2", "bz2"}, {"compress", "unixcompress"},
    {"xz", "xz"}, {"lzma", "lzma"}, {"lzip", "lzip"},
    {"lz4", "lz4"}, {"lzop", "lzop"}, {"zstd", "zstd"},
    {"uuencode", "uue"}
};

bool xfu_native_reader_resolve(const char *selection, const char **name,
                               bool *raw) {
    const char *suffix;
    size_t i;
    if (!selection || !name || !raw) return false;
    if (!strcmp(selection, "native")) {
        *name = NULL; *raw = false; return true;
    }
    if (strncmp(selection, "native:", 7)) return false;
    suffix = selection + 7;
    if (!strcmp(suffix, "raw")) {
        *name = NULL; *raw = true; return true;
    }
    for (i = 0; i < sizeof(native_aliases) / sizeof(native_aliases[0]); ++i) {
        if (!strcmp(suffix, native_aliases[i].selection)) {
            *name = native_aliases[i].reader;
            *raw = false;
            return true;
        }
    }
    return false;
}

static bool native_is_filter_type(xx_file_type_t type) {
    switch (type) {
    case XX_FILE_TYPE_GZ: case XX_FILE_TYPE_BZ2:
    case XX_FILE_TYPE_XZ: case XX_FILE_TYPE_LZMA:
    case XX_FILE_TYPE_LZIP: case XX_FILE_TYPE_LZ4:
    case XX_FILE_TYPE_LZOP: case XX_FILE_TYPE_ZSTD:
    case XX_FILE_TYPE_UNIX_COMPRESS: case XX_FILE_TYPE_UUE: case XX_FILE_TYPE_IS_STORED:
        return true;
    default: return false;
    }
}

xx_file_type_t xfu_native_raw_filter_type_device(xx_io_device *device) {
    xx_file_type_t chain[XX_FILE_TYPE_CHAIN_MAX];
    xx_file_type_t detected, result = XX_FILE_TYPE_UNKNOWN;
    size_t count, i;
    if (!device) return result;
    detected = xx_format_get_file_type_device(device);
    switch (detected) {
    case XX_FILE_TYPE_TAR_LZMA: result = XX_FILE_TYPE_LZMA; break;
    case XX_FILE_TYPE_TAR_LZIP: result = XX_FILE_TYPE_LZIP; break;
    case XX_FILE_TYPE_TAR_LZ4: result = XX_FILE_TYPE_LZ4; break;
    case XX_FILE_TYPE_TAR_LZOP: result = XX_FILE_TYPE_LZOP; break;
    case XX_FILE_TYPE_TAR_ZSTD: result = XX_FILE_TYPE_ZSTD; break;
    case XX_FILE_TYPE_TAR_COMPRESS: result = XX_FILE_TYPE_UNIX_COMPRESS; break;
    default: break;
    }
    if (result != XX_FILE_TYPE_UNKNOWN) return result;
    count = xx_format_get_file_type_chain(detected, chain, XX_FILE_TYPE_CHAIN_MAX);
    for (i = 0; i < count; ++i) {
        if (native_is_filter_type(chain[i])) {
            result = chain[i];
            break;
        }
    }
    return result;
}

xx_file_type_t xfu_native_raw_filter_type(const char *archive_path) {
    xx_io_device *device;
    xx_file_type_t result;
    if (!archive_path) return XX_FILE_TYPE_UNKNOWN;
    device = xx_io_file_open(archive_path, "rb");
    if (!device) return XX_FILE_TYPE_UNKNOWN;
    result = xfu_native_raw_filter_type_device(device);
    xx_io_close(device);
    return result;
}

bool xfu_native_member_name_safe(const char *name, bool is_directory) {
    const char *at;
    size_t components = 0;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    at = name;
    while (*at) {
        const char *start = at;
        size_t length, i;
        while (*at && *at != '/' && *at != '\\') ++at;
        length = (size_t)(at - start);
        while (*at == '/' || *at == '\\') ++at;
        if (!length || (length == 1 && start[0] == '.')) continue;
        if (length == 2 && start[0] == '.' && start[1] == '.') return false;
        for (i = 0; i < length; ++i) {
            unsigned char ch = (unsigned char)start[i];
            if (ch < 32 || ch == 127) return false;
#ifdef _WIN32
            /* ':' names a drive or a stream there; elsewhere it is ordinary. */
            if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
                ch == '?' || ch == '*') return false;
#endif
        }
#ifdef _WIN32
        if (start[length - 1] == '.' || start[length - 1] == ' ') return false;
#endif
        ++components;
    }
    return components != 0 || is_directory;
}

/* Where a decoded layer or envelope payload is written: the first `hold`
 * bytes in memory, and the whole of a longer one in a temporary copy that
 * is opened then. */
typedef struct native_limited_sink {
    xx_io_device device;
    xx_io_device *target; /* The temporary copy, once there is one. */
    unsigned char *held;
    size_t held_size, held_capacity, hold;
    uint64_t written;
    uint64_t limit;
    bool over;
    bool failed; /* No room for the copy: it could not be made or written. */
} native_limited_sink;

/* What one decoded layer may hold: XFU_NATIVE_INNER_MAX_BYTES, and within
 * TEST's memory-only scope no more than the RAM that scope has left, so a
 * layer too large for it stops here and never fails the whole scope. */
static uint64_t native_layer_limit(void) {
    uint64_t limit = XFU_NATIVE_INNER_MAX_BYTES;
    if (xx_io_memory_only_active() && xx_io_memory_only_available() < limit)
        limit = xx_io_memory_only_available();
    return limit;
}

/* A short count is a failed write: a temporary file's stdio buffer that a
 * full disk refused returns one, and what it held is lost. */
static ssize_t native_limited_write(xx_io_device *device, const void *buffer,
                                    size_t length) {
    native_limited_sink *sink = (native_limited_sink *)device->priv;
    if (!sink || (!buffer && length) || length > (size_t)PTRDIFF_MAX || sink->failed) return -1;
    if (length > sink->limit - sink->written) {
        sink->over = true;
        return -1;
    }
    if (!sink->target && length <= sink->hold - sink->held_size) {
        if (length > sink->held_capacity - sink->held_size) {
            size_t capacity = sink->held_capacity ? sink->held_capacity : XFU_NATIVE_SAMPLE_BYTES;
            unsigned char *grown;
            while (capacity - sink->held_size < length) capacity *= 2U;
            if (capacity > sink->hold) capacity = sink->hold;
            if (!(grown = (unsigned char *)realloc(sink->held, capacity))) {
                sink->failed = true;
                return -1;
            }
            sink->held = grown;
            sink->held_capacity = capacity;
        }
        if (length) memcpy(sink->held + sink->held_size, buffer, length);
        sink->held_size += length;
        sink->written += length;
        return (ssize_t)length;
    }
    if (!sink->target) {
        sink->target = xx_io_temp_open();
        if (!sink->target || (sink->held_size &&
            xx_io_write(sink->target, sink->held, sink->held_size) != (ssize_t)sink->held_size)) {
            sink->failed = true;
            return -1;
        }
        /* What was held stays, its start for native_sink_names_archive
         * should the copy fail later; it is freed with the sink. */
    }
    if (length && xx_io_write(sink->target, buffer, length) != (ssize_t)length) {
        sink->failed = true;
        return -1;
    }
    sink->written += length;
    return (ssize_t)length;
}

/* hold: the bytes kept in memory before a temporary copy is opened. */
static void native_sink_init(native_limited_sink *sink, size_t hold) {
    memset(sink, 0, sizeof(*sink));
    sink->device.write = native_limited_write;
    sink->device.priv = sink;
    sink->hold = hold;
    sink->limit = native_layer_limit();
}

static void native_sink_cleanup(native_limited_sink *sink) {
    if (sink->target) xx_io_close(sink->target);
    free(sink->held);
    sink->target = NULL;
    sink->held = NULL;
}

/* A decoded layer or payload held in memory (see native_limited_sink). */
typedef struct native_memory {
    xx_io_device device;
    xx_io_device *view;
    unsigned char *data;
} native_memory;

static ssize_t native_memory_read(xx_io_device *device, void *buffer, size_t length) {
    return xx_io_read(((native_memory *)device->priv)->view, buffer, length);
}

static int native_memory_seek(xx_io_device *device, long offset, int whence) {
    return xx_io_seek64(((native_memory *)device->priv)->view, offset, whence);
}

static int native_memory_seek64(xx_io_device *device, int64_t offset, int whence) {
    return xx_io_seek64(((native_memory *)device->priv)->view, offset, whence);
}

static int64_t native_memory_tell(xx_io_device *device) {
    return xx_io_tell(((native_memory *)device->priv)->view);
}

static int64_t native_memory_size(xx_io_device *device) {
    return xx_io_total_size(((native_memory *)device->priv)->view);
}

static int native_memory_close(xx_io_device *device) {
    native_memory *memory = (native_memory *)device->priv;
    int result = xx_io_close(memory->view);
    free(memory->data);
    free(memory);
    return result;
}

/* Takes data (freed when this fails). */
static xx_io_device *native_memory_open(unsigned char *data, size_t size) {
    native_memory *memory = (native_memory *)calloc(1, sizeof(*memory));
    if (!data) data = (unsigned char *)malloc(1);
    if (!memory || !data || !(memory->view = xx_io_mem_open_ro(data, size))) {
        free(memory);
        free(data);
        return NULL;
    }
    memory->data = data;
    memory->device.read = native_memory_read;
    memory->device.seek = native_memory_seek;
    memory->device.seek64 = native_memory_seek64;
    memory->device.tell = native_memory_tell;
    memory->device.total_size = native_memory_size;
    memory->device.get_total_size = native_memory_size;
    memory->device.size = native_memory_size;
    memory->device.close = native_memory_close;
    memory->device.priv = memory;
    return &memory->device;
}

/* What was written, rewound, for the caller to close; NULL, with `failed`
 * set, when there is no room for it, as when the temporary copy cannot be
 * read back because a full disk refused the last of it. */
static xx_io_device *native_sink_take(native_limited_sink *sink) {
    xx_io_device *result = NULL;
    if (sink->failed) return NULL;
    if (sink->target) {
        if (xx_io_seek64(sink->target, 0, SEEK_SET) == 0) {
            result = sink->target;
            sink->target = NULL;
        }
    } else {
        result = native_memory_open(sink->held, sink->held_size);
        sink->held = NULL;
        sink->held_size = sink->held_capacity = 0;
    }
    if (!result) sink->failed = true;
    return result;
}

typedef enum native_prefix_verdict {
    NATIVE_PREFIX_OPEN = 0, /* The buffer has not filled. */
    NATIVE_PREFIX_COPYING,  /* It may hold an archive: the layer goes on into the copy. */
    NATIVE_PREFIX_NOT_ARCHIVE,
    NATIVE_PREFIX_TOO_LARGE
} native_prefix_verdict;

typedef struct native_prefix_sink {
    xx_io_device device;
    unsigned char *data;
    size_t capacity, size;
    bool full;
    bool ended; /* The decoder finished. */
    /* For a reader that decodes its whole stream before it writes any of it
     * (native_one_shot): the copy the layer goes on into once the full buffer
     * may hold an archive, whether the layer is known not to fit that copy,
     * and the verdict on the buffer with the type it names. */
    native_limited_sink *layer;
    bool exceeds;
    native_prefix_verdict verdict;
    xx_file_type_t candidate;
} native_prefix_sink;

static bool native_prefix_may_hold_archive(xx_io_device *prefix,
                                           const unsigned char *data, size_t size,
                                           xx_file_type_t *type);

/* The full buffer of a one-shot reader: whether its layer goes on into the
 * copy, which then holds the buffer. */
static bool native_prefix_decide(native_prefix_sink *sink) {
    xx_io_device *view = xx_io_mem_open_ro(sink->data, sink->size);
    bool may;
    if (!view) return false;
    may = native_prefix_may_hold_archive(view, sink->data, sink->size, &sink->candidate);
    xx_io_close(view);
    sink->verdict = !may ? NATIVE_PREFIX_NOT_ARCHIVE
        : sink->exceeds ? NATIVE_PREFIX_TOO_LARGE : NATIVE_PREFIX_COPYING;
    return sink->verdict == NATIVE_PREFIX_COPYING &&
        native_limited_write(&sink->layer->device, sink->data, sink->size) == (ssize_t)sink->size;
}

/* Keeps what fits and refuses the rest, which stops the decoder, unless the
 * layer goes on into the copy (native_prefix_decide). */
static ssize_t native_prefix_write(xx_io_device *device, const void *buffer,
                                   size_t length) {
    native_prefix_sink *sink = (native_prefix_sink *)device->priv;
    size_t amount;
    if (!sink || (!buffer && length) || length > (size_t)PTRDIFF_MAX) return -1;
    if (sink->verdict == NATIVE_PREFIX_COPYING)
        return native_limited_write(&sink->layer->device, buffer, length);
    amount = length < sink->capacity - sink->size ? length : sink->capacity - sink->size;
    if (amount) memcpy(sink->data + sink->size, buffer, amount);
    sink->size += amount;
    if (amount == length) return (ssize_t)length;
    sink->full = true;
    if (!sink->layer || sink->verdict != NATIVE_PREFIX_OPEN || !native_prefix_decide(sink) ||
        native_limited_write(&sink->layer->device, (const unsigned char *)buffer + amount,
                             length - amount) != (ssize_t)(length - amount))
        return -1;
    return (ssize_t)length;
}

static const char *native_filter_reader(xx_file_type_t filter_type) {
    switch (filter_type) {
    case XX_FILE_TYPE_GZ: return "gz";
    case XX_FILE_TYPE_BZ2: return "bz2";
    case XX_FILE_TYPE_XZ: return "xz";
    case XX_FILE_TYPE_LZMA: return "lzma";
    case XX_FILE_TYPE_LZIP: return "lzip";
    case XX_FILE_TYPE_LZ4: return "lz4";
    case XX_FILE_TYPE_LZOP: return "lzop";
    case XX_FILE_TYPE_ZSTD: return "zstd";
    case XX_FILE_TYPE_UNIX_COMPRESS: return "unixcompress";
    case XX_FILE_TYPE_IS_STORED: return "is_stored";
    default: return NULL;
    }
}

static bool native_filter_unpack(Abstractformat *format, xx_file_type_t filter_type,
                                 xx_io_device *destination, xx_pd_struct *pd) {
    switch (filter_type) {
    case XX_FILE_TYPE_GZ: return xx_gz_unpack_to_device((xx_gz *)format, destination, pd);
    case XX_FILE_TYPE_BZ2: return xx_bz2_unpack_to_device((xx_bz2 *)format, destination, pd);
    case XX_FILE_TYPE_XZ: return xx_xz_unpack_to_device((xx_xz *)format, destination, pd);
    case XX_FILE_TYPE_LZMA: return xx_lzma_unpack_to_device((xx_lzma *)format, destination, pd);
    case XX_FILE_TYPE_LZIP: return xx_lzip_unpack_to_device((xx_lzip *)format, destination, pd);
    case XX_FILE_TYPE_LZ4: return xx_lz4_unpack_to_device((xx_lz4 *)format, destination, pd);
    case XX_FILE_TYPE_LZOP: return xx_lzop_unpack_to_device((xx_lzop *)format, destination, pd);
    case XX_FILE_TYPE_ZSTD: return xx_zstd_unpack_to_device((xx_zstd *)format, destination, pd);
    case XX_FILE_TYPE_UNIX_COMPRESS: return xx_unixcompress_unpack_to_device((xx_unixcompress *)format, destination, pd);
#if XFU_FORMAT_SUPPORT_FULL
    case XX_FILE_TYPE_IS_STORED: return xx_is_stored_unpack_to_device((xx_is_stored *)format, destination, pd);
#endif
    default: return false;
    }
}

/* The stream's reader in *opened (the caller closes it), verified as its
 * handle_base_info does; kept open, it is not verified twice. */
static bool native_open_filter(xx_io_device *source, xx_file_type_t filter_type,
                               xxfc_opened *opened, xx_pd_struct *pd) {
    if (opened->format) return true;
    if (xxfc_open_named(opened, source, 0, native_filter_reader(filter_type)) &&
        xx_format_is_valid(opened->format, pd) &&
        xx_format_handle_base_info(opened->format, pd)) return true;
    xxfc_close(opened);
    return false;
}

/* zstd and LZ4: readers that decode their whole stream in memory before
 * they write any of it, so that a prefix of it costs a whole decode. */
static bool native_one_shot(xx_file_type_t filter_type) {
    return filter_type == XX_FILE_TYPE_ZSTD || filter_type == XX_FILE_TYPE_LZ4;
}

static uint64_t native_filter_size(const xxfc_opened *opened, xx_file_type_t filter_type);
static bool native_packed_exceeds(xx_file_type_t filter_type, int64_t packed, uint64_t limit);

/* The first bytes of the decoded layer in the sink's buffer. gzip and bzip2
 * decode only those; the other readers first verify their stream in
 * *opened (xz reads its index), and a one-shot reader's layer goes on into
 * the sink's copy when its start may hold an archive, so that it is decoded
 * once. complete: the layer ended inside the buffer and was verified. False
 * on a decode error or Cancel. */
static bool native_decode_prefix(xx_io_device *source, xx_file_type_t filter_type,
                                 xxfc_opened *opened, native_prefix_sink *sink,
                                 bool *complete, xx_pd_struct *pd) {
    *complete = false;
    if (filter_type == XX_FILE_TYPE_GZ)
        return xx_gz_decode_prefix(source, 0, sink->data, sink->capacity, &sink->size, complete, pd);
    if (filter_type == XX_FILE_TYPE_BZ2)
        return xx_bz2_decode_prefix(source, 0, sink->data, sink->capacity, &sink->size, complete, pd);
    if (!native_open_filter(source, filter_type, opened, pd)) return false;
    if (sink->layer) {
        uint64_t known = native_filter_size(opened, filter_type);
        sink->exceeds = known ? known > sink->layer->limit
            : native_packed_exceeds(filter_type, xx_io_total_size(source), sink->layer->limit);
    }
    sink->ended = native_filter_unpack(opened->format, filter_type, &sink->device, pd);
    *complete = sink->ended && sink->verdict == NATIVE_PREFIX_OPEN;
    return sink->ended || (sink->full && !xx_pd_is_stopped(pd));
}

/* Whether a whole decoded layer holds an archive (or another filter): the
 * test every nested layer and envelope payload gets. */
static bool native_layer_is_archive(xx_io_device *device, xx_pd_struct *pd) {
    xxfc_opened candidate = {0};
    xx_file_type_t detected;
    bool archive;
    if (xx_io_seek64(device, 0, SEEK_SET) != 0) return false;
    detected = xx_format_get_file_type_device(device);
    archive = native_is_filter_type(detected) ||
        (detected != XX_FILE_TYPE_UNKNOWN && detected != XX_FILE_TYPE_BINARY &&
         xxfc_open_type(&candidate, device, 0, detected) &&
         candidate.format->is_archive &&
         xx_format_is_valid(candidate.format, pd) &&
         xx_format_handle_base_info(candidate.format, pd));
    xxfc_close(&candidate);
    if (!archive) xx_pd_clear_error(pd);
    return archive;
}

/* A filter, or a type whose reader reads members: what the layer test above
 * would take further. */
static bool native_type_has_members(xx_io_device *device, xx_file_type_t type) {
    xxfc_opened candidate = {0};
    bool result;
    if (native_is_filter_type(type)) return true;
    if (type == XX_FILE_TYPE_UNKNOWN || type == XX_FILE_TYPE_BINARY ||
        !xxfc_open_type(&candidate, device, 0, type)) return false;
    result = candidate.format->is_archive;
    xxfc_close(&candidate);
    return result;
}

typedef enum native_sample_kind {
    NATIVE_SAMPLE_OTHER = 0,
    NATIVE_SAMPLE_TEXT,
    NATIVE_SAMPLE_SPREAD
} native_sample_kind;

/* What the first XFU_NATIVE_SAMPLE_BYTES are: text, bytes as evenly spread
 * as compressed or encrypted data (what a large dump or backup stream is,
 * and what no archive the detector and the signature rules missed starts
 * with), or other structured data. */
static native_sample_kind native_sample_kind_of(const unsigned char *data, size_t size) {
    size_t counts[256] = {0}, i, low, high;
    bool text = true;
    if (size > XFU_NATIVE_SAMPLE_BYTES) size = XFU_NATIVE_SAMPLE_BYTES;
    if (size < XFU_NATIVE_SAMPLE_BYTES) return NATIVE_SAMPLE_OTHER;
    for (i = 0; i < size; ++i) {
        unsigned char c = data[i];
        ++counts[c];
        if ((c < 32 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != 27) ||
            c == 127) text = false;
    }
    if (text) return NATIVE_SAMPLE_TEXT;
    low = size / 256U / 2U;
    high = size / 256U * 2U;
    for (i = 0; i < 256U; ++i)
        if (counts[i] < low || counts[i] > high) return NATIVE_SAMPLE_OTHER;
    return NATIVE_SAMPLE_SPREAD;
}

/* For a layer longer than its decoded prefix: whether it may hold an
 * archive. The detector and the signature rules name what the prefix
 * starts, and only evenly spread data is ruled out from the prefix alone.
 * Text is not: the readers of text record formats (FASTA, SAM, Wavefront
 * OBJ, ...) check every line of the whole file, so neither a prefix cut
 * mid-line nor one cut at a line break settles them, and text is decoded
 * whole like other structured data (a disk image, say), as every layer was
 * before. What the prefix cut at its last line break is detected as, when
 * that is a type with members, names the layer for a TOO_LARGE status.
 * *type: the archive or filter type named, else UNKNOWN. */
static bool native_prefix_may_hold_archive(xx_io_device *prefix,
                                           const unsigned char *data, size_t size,
                                           xx_file_type_t *type) {
    xx_file_type_t matches[16];
    size_t count, i;
    native_sample_kind kind;
    *type = xx_format_get_file_type_device(prefix);
    if (native_type_has_members(prefix, *type)) return true;
    count = xx_format_fast_signature_types(prefix, 0, matches,
                                           sizeof(matches) / sizeof(matches[0]));
    for (i = 0; i < count; ++i) {
        if (native_type_has_members(prefix, matches[i])) {
            *type = matches[i];
            return true;
        }
    }
    *type = XX_FILE_TYPE_UNKNOWN;
    kind = native_sample_kind_of(data, size);
    if (kind == NATIVE_SAMPLE_TEXT) {
        size_t cut = size;
        xx_io_device *lines;
        while (cut && data[cut - 1U] != '\n') --cut;
        if (cut && cut < size && (lines = xx_io_mem_open_ro(data, cut)) != NULL) {
            xx_file_type_t named = xx_format_get_file_type_device(lines);
            if (native_type_has_members(lines, named)) *type = named;
            xx_io_close(lines);
        }
    }
    return kind != NATIVE_SAMPLE_SPREAD;
}

/* For a payload that outgrew native_layer_limit: whether the part written
 * (rewound here) starts an archive. Only then is it TOO_LARGE; anything else
 * is read as the payload's one file by the ordinary reader, as before. */
static bool native_partial_names_archive(xx_io_device *partial) {
    xx_file_type_t matches[16];
    size_t count, i;
    if (xx_io_seek64(partial, 0, SEEK_SET) != 0) return false;
    if (native_type_has_members(partial, xx_format_get_file_type_device(partial))) return true;
    count = xx_format_fast_signature_types(partial, 0, matches,
                                           sizeof(matches) / sizeof(matches[0]));
    for (i = 0; i < count; ++i)
        if (native_type_has_members(partial, matches[i])) return true;
    return false;
}

/* The same for what a sink stopped by its limit holds, or, when its copy
 * failed, for the start it held in memory (the copy may not read back). */
static bool native_sink_names_archive(native_limited_sink *sink) {
    xx_io_device *view;
    bool named;
    if (sink->target && !(sink->failed && sink->held_size))
        return native_partial_names_archive(sink->target);
    view = xx_io_mem_open_ro(sink->held ? sink->held : (const unsigned char *)"", sink->held_size);
    named = view && native_partial_names_archive(view);
    if (view) xx_io_close(view);
    return named;
}

/* ZSTD or LZ4 when a decoded layer starts with one's frame, else UNKNOWN:
 * their detectors check the whole stream, so a prefix of it names neither
 * (native_prefix_may_hold_archive), but it names the layer that found no
 * room for its copy. */
static xx_file_type_t native_stream_start(const unsigned char *data, size_t size) {
    static const unsigned char zstd[4] = { 0x28, 0xB5, 0x2F, 0xFD };
    static const unsigned char lz4[4] = { 0x04, 0x22, 0x4D, 0x18 };
    static const unsigned char lz4_legacy[4] = { 0x02, 0x21, 0x4C, 0x18 };
    if (!data || size < 4U) return XX_FILE_TYPE_UNKNOWN;
    if (!memcmp(data, zstd, 4U)) return XX_FILE_TYPE_ZSTD;
    if (!memcmp(data, lz4, 4U) || !memcmp(data, lz4_legacy, 4U)) return XX_FILE_TYPE_LZ4;
    return XX_FILE_TYPE_UNKNOWN;
}

/* The decoded size a verified reader already knows, else 0. */
static uint64_t native_filter_size(const xxfc_opened *opened, xx_file_type_t filter_type) {
    if (!opened->format) return 0;
    switch (filter_type) {
    case XX_FILE_TYPE_XZ: return xx_xz_get_uncompressed_size((const xx_xz *)opened->format);
    case XX_FILE_TYPE_LZIP: return xx_lzip_get_uncompressed_size((const xx_lzip *)opened->format);
    case XX_FILE_TYPE_ZSTD: return xx_zstd_get_uncompressed_size((const xx_zstd *)opened->format);
    case XX_FILE_TYPE_IS_STORED: return (uint64_t)opened->format->format_size;
    default: return 0;
    }
}

/* Whether a packed stream of this size cannot decode to `limit` bytes or
 * fewer. Their encoders let incompressible data grow by no more than a
 * small framing overhead, far less than the 1/16 allowed here: stored
 * deflate blocks, bzip2 and LZMA coding (about 1.5% at most), raw LZ4,
 * LZO and zstd blocks. Not so LZW (compress): a code of 9 to 16 bits can
 * stand for a single byte, so a stream can be larger than its data (by
 * about a fifth for incompressible data, by about an eighth for any data in
 * the literal streams our own encoder writes), and a CLEAR code stands for
 * none, so the size of a .Z stream bounds nothing. */
static bool native_packed_exceeds(xx_file_type_t filter_type, int64_t packed, uint64_t limit) {
    if (packed <= 0 || filter_type == XX_FILE_TYPE_UNIX_COMPRESS) return false;
    return (uint64_t)packed / 17U > limit / 16U;
}

/* The whole layer into a rewound temporary device: ARCHIVE when that is
 * done, TOO_LARGE when it outgrows native_layer_limit. That is known
 * beforehand, without writing anything, from an xz index or an lzip or zstd
 * size, or else from the compressed size (native_packed_exceeds); gzip and
 * bzip2, whose readers verify the whole stream when opened, are checked
 * that way before they are opened. */
static xfu_native_inner_status native_decode_layer(xx_io_device *source,
                                                   xx_file_type_t filter_type,
                                                   xxfc_opened *opened,
                                                   xx_pd_struct *pd,
                                                   xx_io_device **output,
                                                   bool *no_room) {
    native_limited_sink sink;
    xfu_native_inner_status status = XFU_NATIVE_INNER_DECODE_ERROR;
    uint64_t limit = native_layer_limit(), known;
    int64_t packed = xx_io_total_size(source);
    *no_room = false;
    if (!opened->format && native_packed_exceeds(filter_type, packed, limit))
        return XFU_NATIVE_INNER_TOO_LARGE;
    if (!native_open_filter(source, filter_type, opened, pd)) return status;
    known = native_filter_size(opened, filter_type);
    if (known ? known > limit : native_packed_exceeds(filter_type, packed, limit))
        return XFU_NATIVE_INNER_TOO_LARGE;
    native_sink_init(&sink, 0);
    if (native_filter_unpack(opened->format, filter_type, &sink.device, pd) &&
        !xx_pd_is_stopped(pd)) {
        if ((*output = native_sink_take(&sink)) != NULL) status = XFU_NATIVE_INNER_ARCHIVE;
        else *no_room = true;
    } else if (sink.over && !xx_pd_is_stopped(pd)) {
        status = XFU_NATIVE_INNER_TOO_LARGE;
        xx_pd_clear_error(pd);
    } else if (sink.failed && !xx_pd_is_stopped(pd)) {
        *no_room = true;
    }
    native_sink_cleanup(&sink);
    return status;
}

xfu_native_inner_status xfu_native_open_inner_device_ex(xx_io_device *source,
                                                         xx_file_type_t filter_type,
                                                         xx_pd_struct *pd,
                                                         xx_io_device **output,
                                                         xx_file_type_t *layer_type) {
    unsigned char *prefix = NULL;
    xx_io_device *view = NULL, *inner = NULL;
    xxfc_opened opened = {0};
    native_prefix_sink sink;
    native_limited_sink layer;
    size_t size = 0;
    bool complete = false, no_room = false;
    xx_file_type_t candidate = XX_FILE_TYPE_UNKNOWN, start = XX_FILE_TYPE_UNKNOWN;
    xfu_native_inner_status status = XFU_NATIVE_INNER_DECODE_ERROR;
    if (layer_type) *layer_type = XX_FILE_TYPE_UNKNOWN;
    if (!output) return status;
    *output = NULL;
    if (!source || !pd || xx_pd_is_stopped(pd)) return status;
    if (!native_filter_reader(filter_type)) return XFU_NATIVE_INNER_NOT_ARCHIVE;
    native_sink_init(&layer, 0);
    /* Decide from the decoded start whether the layer can hold an archive,
     * and decode it whole only then: a large stream of evenly spread data
     * is left to its own reader, which reads it without a temporary copy. */
    prefix = (unsigned char *)malloc(XFU_NATIVE_PREFIX_BYTES);
    if (!prefix) goto done;
    memset(&sink, 0, sizeof(sink));
    sink.device.write = native_prefix_write;
    sink.device.priv = &sink;
    sink.data = prefix;
    sink.capacity = XFU_NATIVE_PREFIX_BYTES;
    if (native_one_shot(filter_type)) sink.layer = &layer;
    if (!native_decode_prefix(source, filter_type, &opened, &sink, &complete, pd) ||
        xx_pd_is_stopped(pd) || !(view = xx_io_mem_open_ro(prefix, sink.size))) goto done;
    size = sink.size;
    if (complete) {
        /* The whole layer is in the prefix: the usual decision on it, and
         * the prefix is the decoded layer, kept in memory. */
        if (!native_layer_is_archive(view, pd)) {
            status = XFU_NATIVE_INNER_NOT_ARCHIVE;
        } else if ((uint64_t)size > native_layer_limit()) {
            status = XFU_NATIVE_INNER_TOO_LARGE;
            candidate = xx_format_get_file_type_device(view);
        } else {
            unsigned char *kept;
            xx_io_close(view);
            view = NULL;
            if ((kept = (unsigned char *)realloc(prefix, size ? size : 1U)) != NULL) prefix = kept;
            if ((*output = native_memory_open(prefix, size)) != NULL)
                status = XFU_NATIVE_INNER_ARCHIVE;
            prefix = NULL;
        }
        goto done;
    }
    start = native_stream_start(prefix, size);
    if (sink.layer) {
        /* A one-shot reader's layer was decided on as it was decoded. */
        candidate = sink.candidate;
        if (sink.verdict == NATIVE_PREFIX_NOT_ARCHIVE) {
            status = XFU_NATIVE_INNER_NOT_ARCHIVE;
            goto done;
        }
        if (sink.verdict == NATIVE_PREFIX_TOO_LARGE || layer.over) {
            status = XFU_NATIVE_INNER_TOO_LARGE;
            xx_pd_clear_error(pd);
        } else if (sink.verdict == NATIVE_PREFIX_COPYING && sink.ended) {
            if ((inner = native_sink_take(&layer)) != NULL) status = XFU_NATIVE_INNER_ARCHIVE;
            else no_room = true;
        } else if (layer.failed) {
            no_room = true;
        } else {
            goto done;
        }
    } else {
        if (!native_prefix_may_hold_archive(view, prefix, size, &candidate)) {
            status = XFU_NATIVE_INNER_NOT_ARCHIVE;
            goto done;
        }
        xx_io_close(view);
        view = NULL;
        free(prefix);
        prefix = NULL;
        status = native_decode_layer(source, filter_type, &opened, pd, &inner, &no_room);
    }
    /* A zstd or LZ4 layer with no room is said like any other. */
    if (no_room && candidate == XX_FILE_TYPE_UNKNOWN) candidate = start;
    if ((status == XFU_NATIVE_INNER_TOO_LARGE || no_room) &&
        candidate == XX_FILE_TYPE_UNKNOWN) {
        /* Nothing named an archive: a stream too large to check, or one with
         * no room for its temporary copy, is read as one file, as one that
         * is not an archive is. */
        if (no_room) xx_pd_clear_error(pd);
        status = XFU_NATIVE_INNER_NOT_ARCHIVE;
    } else if (no_room) {
        xx_pd_clear_error(pd);
        status = XFU_NATIVE_INNER_NO_ROOM;
    } else if (status == XFU_NATIVE_INNER_ARCHIVE) {
        if (native_layer_is_archive(inner, pd) && xx_io_seek64(inner, 0, SEEK_SET) == 0) {
            *output = inner;
            inner = NULL;
        } else {
            status = XFU_NATIVE_INNER_NOT_ARCHIVE;
        }
    }
done:
    if (view) xx_io_close(view);
    if (inner) xx_io_close(inner);
    native_sink_cleanup(&layer);
    xxfc_close(&opened);
    free(prefix);
    if ((status == XFU_NATIVE_INNER_TOO_LARGE || status == XFU_NATIVE_INNER_NO_ROOM) && layer_type)
        *layer_type = candidate;
    return status;
}

bool xfu_native_stream_over_limit(xx_io_device *device, xx_file_type_t type,
                                  uint64_t *limit) {
    if (limit) *limit = 0;
    if (type == XX_FILE_TYPE_ZSTD) return xx_zstd_exceeds_stream_limit(device, 0, limit);
    if (type == XX_FILE_TYPE_LZ4) return xx_lz4_exceeds_stream_limit(device, 0, limit);
    return false;
}

xfu_native_inner_status xfu_native_open_inner_device(xx_io_device *source,
                                                      xx_file_type_t filter_type,
                                                      xx_pd_struct *pd,
                                                      xx_io_device **output) {
    return xfu_native_open_inner_device_ex(source, filter_type, pd, output, NULL);
}

xfu_native_inner_status xfu_native_open_inner_ex(const char *archive_path,
                                                  xx_file_type_t filter_type,
                                                  xx_pd_struct *pd,
                                                  xx_io_device **output,
                                                  xx_file_type_t *layer_type) {
    xx_io_device *source;
    xfu_native_inner_status status;
    if (layer_type) *layer_type = XX_FILE_TYPE_UNKNOWN;
    if (!output) return XFU_NATIVE_INNER_DECODE_ERROR;
    *output = NULL;
    if (!archive_path) return XFU_NATIVE_INNER_DECODE_ERROR;
    source = xx_io_file_open(archive_path, "rb");
    if (!source) return XFU_NATIVE_INNER_DECODE_ERROR;
    status = xfu_native_open_inner_device_ex(source, filter_type, pd, output, layer_type);
    xx_io_close(source);
    return status;
}

xfu_native_inner_status xfu_native_open_inner(const char *archive_path,
                                               xx_file_type_t filter_type,
                                               xx_pd_struct *pd,
                                               xx_io_device **output) {
    return xfu_native_open_inner_ex(archive_path, filter_type, pd, output, NULL);
}

typedef struct native_line_reader {
    xx_io_device *source;
    unsigned char buffer[XFU_NATIVE_READ_BUFFER];
    size_t at, count;
    bool eof;
} native_line_reader;

static bool native_safe_basename(const char *path, const char **name);

/* 1: line, 0: EOF, -1: overlong line or a NUL byte, -2: I/O error. */
static int native_next_line(native_line_reader *reader, char *line,
                            size_t capacity, size_t *length) {
    size_t used = 0;
    for (;;) {
        unsigned char c;
        if (reader->at == reader->count) {
            ssize_t got;
            if (reader->eof) {
                if (!used) return 0;
                break;
            }
            got = xx_io_read(reader->source, reader->buffer, sizeof(reader->buffer));
            if (got < 0) return -2;
            if (!got) { reader->eof = true; continue; }
            reader->at = 0;
            reader->count = (size_t)got;
        }
        c = reader->buffer[reader->at++];
        if (c == '\n') break;
        if (used + 1 >= capacity || c == 0) return -1;
        line[used++] = (char)c;
    }
    if (used && line[used - 1] == '\r') --used;
    line[used] = '\0';
    *length = used;
    return 1;
}

/* Next byte of the source: 0..255, -1 at its end, -2 on a read error. */
static int native_next_byte(native_line_reader *reader) {
    if (reader->at == reader->count) {
        ssize_t got;
        if (reader->eof) return -1;
        got = xx_io_read(reader->source, reader->buffer, sizeof(reader->buffer));
        if (got < 0) return -2;
        if (!got) { reader->eof = true; return -1; }
        reader->at = 0;
        reader->count = (size_t)got;
    }
    return reader->buffer[reader->at++];
}

static int native_base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static bool native_base64_group(const char group[4],
                                native_limited_sink *sink, bool *padded) {
    int a = native_base64_value(group[0]);
    int b = native_base64_value(group[1]);
    int c = group[2] == '=' ? -2 : native_base64_value(group[2]);
    int d = group[3] == '=' ? -2 : native_base64_value(group[3]);
    unsigned char bytes[3];
    size_t count;
    if (a < 0 || b < 0 || c == -1 || d == -1 || (c == -2 && d != -2))
        return false;
    bytes[0] = (unsigned char)((a << 2) | (b >> 4));
    count = 1;
    if (c >= 0) {
        bytes[1] = (unsigned char)((b << 4) | (c >> 2));
        count = 2;
        if (d >= 0) {
            bytes[2] = (unsigned char)((c << 6) | d);
            count = 3;
        }
    }
    if ((c == -2 && (b & 15)) || (d == -2 && c >= 0 && (c & 3)) ||
        xx_io_write(&sink->device, bytes, count) != (ssize_t)count) return false;
    if (c == -2 || d == -2) *padded = true;
    return true;
}

/* Decode the data lines up to the "====" line, which may end in blanks. A
 * line may have any length (base64 -w0 writes the whole payload as one) but
 * holds whole groups of four, and padding ends the data. 1: terminated, 0:
 * the input ended before the terminator, -1: malformed data, an I/O or size
 * error or Cancel. */
static int native_base64_decode(native_line_reader *reader,
                                native_limited_sink *sink, xx_pd_struct *pd) {
    char group[4];
    size_t used = 0, column = 0;
    uint64_t counted = 0;
    bool padded = false;
    for (;;) {
        int ch = native_next_byte(reader);
        if (ch == '\r') {
            ch = native_next_byte(reader);
            if (ch >= 0 && ch != '\n') return -1;
        }
        if (ch == -2) return -1;
        if (ch == '\n' || ch == -1) {
            if (column == 4U && used == 4U && !memcmp(group, "====", 4U)) return 1;
            if (used) return -1;
            if (ch == -1) return 0;
            column = 0;
            continue;
        }
        if (!(++counted & 0xFFFFU) && xx_pd_is_stopped(pd)) return -1;
        /* A held "====" that does not end its line is not the terminator;
         * blanks may follow it, as xxfclib's Base64 reader allows. */
        if (used == 4U) {
            if (ch == ' ' || ch == '\t') continue;
            return -1;
        }
        ++column;
        group[used++] = (char)ch;
        if (used < 4U || (column == 4U && !memcmp(group, "====", 4U))) continue;
        if (padded || !native_base64_group(group, sink, &padded)) return -1;
        used = 0;
    }
}

/* An envelope's raw payload: the decoded temporary device, and the
 * envelope type the raw reader reports for it wherever the envelope sits. */
typedef struct native_payload {
    xx_io_device device;
    xx_io_device *inner;
    xx_file_type_t type;
} native_payload;

static ssize_t native_payload_read(xx_io_device *device, void *buffer, size_t length) {
    return xx_io_read(((native_payload *)device->priv)->inner, buffer, length);
}

static int native_payload_seek(xx_io_device *device, long offset, int whence) {
    return xx_io_seek64(((native_payload *)device->priv)->inner, offset, whence);
}

static int native_payload_seek64(xx_io_device *device, int64_t offset, int whence) {
    return xx_io_seek64(((native_payload *)device->priv)->inner, offset, whence);
}

static int64_t native_payload_tell(xx_io_device *device) {
    return xx_io_tell(((native_payload *)device->priv)->inner);
}

static int64_t native_payload_size(xx_io_device *device) {
    return xx_io_total_size(((native_payload *)device->priv)->inner);
}

static int native_payload_close(xx_io_device *device) {
    native_payload *payload = (native_payload *)device->priv;
    int result = xx_io_close(payload->inner);
    free(payload);
    return result;
}

/* Takes inner, which is closed when this fails. */
static xx_io_device *native_payload_wrap(xx_io_device *inner, xx_file_type_t type) {
    native_payload *payload = (native_payload *)calloc(1, sizeof(*payload));
    if (!payload) {
        xx_io_close(inner);
        return NULL;
    }
    payload->inner = inner;
    payload->type = type;
    payload->device.read = native_payload_read;
    payload->device.seek = native_payload_seek;
    payload->device.seek64 = native_payload_seek64;
    payload->device.tell = native_payload_tell;
    payload->device.total_size = native_payload_size;
    payload->device.get_total_size = native_payload_size;
    payload->device.size = native_payload_size;
    payload->device.close = native_payload_close;
    payload->device.priv = payload;
    return &payload->device;
}

/* An archive payload goes to its reader as it is; a raw one carries the
 * envelope type for the raw reader. */
static bool native_envelope_hand_over(xfu_native_envelope_status status,
                                      xx_io_device **temp, xx_file_type_t type,
                                      xx_io_device **decoded) {
    if (xx_io_seek64(*temp, 0, SEEK_SET) != 0) return false;
    *decoded = status == XFU_NATIVE_ENVELOPE_RAW ? native_payload_wrap(*temp, type) : *temp;
    *temp = NULL;
    return *decoded != NULL;
}

xfu_native_envelope_status xfu_native_base64_open_device(xx_io_device *source,
                                                          xx_pd_struct *pd,
                                                          xx_io_device **decoded,
                                                          char **raw_name) {
    static const char prefix[] = "begin-base64 ";
    xx_io_device *temp = NULL;
    native_line_reader *reader = NULL;
    native_limited_sink sink = {0};
    /* The header rules of xxfclib's Base64 reader, which reads what this
     * probe leaves to it: at most XX_BASE64_HEADER_MAX (4096) bytes with the
     * line break, and 1 to 6 octal digits of mode. */
    char line[XX_BASE64_HEADER_MAX], *owned_name = NULL;
    const char *safe;
    size_t length, at, digits = 0;
    int line_status, decoded_status;
    xfu_native_envelope_status status = XFU_NATIVE_ENVELOPE_DECODE_ERROR;
    if (!decoded || !raw_name) return status;
    *decoded = NULL;
    *raw_name = NULL;
    if (!source || !pd || xx_pd_is_stopped(pd)) return status;
    {
        char first[sizeof(prefix) - 1U];
        if (!xx_io_read_at(source, 0, first, sizeof(first)) ||
            memcmp(first, prefix, sizeof(first))) {
            status = XFU_NATIVE_ENVELOPE_NOT_FOUND;
            goto done;
        }
    }
    reader = (native_line_reader *)calloc(1, sizeof(*reader));
    if (!reader) goto done;
    reader->source = source;
    /* Only "begin-base64 <octal mode> <name>" makes the file an envelope;
     * prose that starts with "begin-base64 " is left to the detector, and
     * a name this reader cannot use to the ordinary Base64 reader. */
    status = XFU_NATIVE_ENVELOPE_NOT_FOUND;
    line_status = native_next_line(reader, line, sizeof(line), &length);
    if (line_status == -2) status = XFU_NATIVE_ENVELOPE_DECODE_ERROR;
    if (line_status != 1 || length < sizeof(prefix) + 2U ||
        memcmp(line, prefix, sizeof(prefix) - 1U)) goto done;
    at = sizeof(prefix) - 1U;
    while (at < length && digits < 6U && line[at] >= '0' && line[at] <= '7') {
        ++at; ++digits;
    }
    if (!digits || at >= length || line[at++] != ' ' || at >= length ||
        !native_safe_basename(line + at, &safe)) goto done;
    status = XFU_NATIVE_ENVELOPE_DECODE_ERROR;
    owned_name = (char *)malloc(strlen(safe) + 1U);
    if (!owned_name) goto done;
    strcpy(owned_name, safe);
    native_sink_init(&sink, XFU_NATIVE_PREFIX_BYTES);
    decoded_status = native_base64_decode(reader, &sink, pd);
    if (decoded_status != 1 || xx_pd_is_stopped(pd)) {
        /* Too large for the in-memory Test: the ordinary reader streams it. */
        if (sink.over && !xx_pd_is_stopped(pd))
            status = native_sink_names_archive(&sink) ? XFU_NATIVE_ENVELOPE_TOO_LARGE
                                                       : XFU_NATIVE_ENVELOPE_NOT_FOUND;
        else if (sink.failed && !xx_pd_is_stopped(pd))
            status = native_sink_names_archive(&sink) ? XFU_NATIVE_ENVELOPE_NO_ROOM
                                                       : XFU_NATIVE_ENVELOPE_NOT_FOUND;
        goto done;
    }
    /* Text after the envelope (another envelope, a signature) is not this
     * single-member form; the ordinary Base64 reader takes such a file. */
    for (;;) {
        int ch = native_next_byte(reader);
        if (ch == -1) break;
        if (ch == -2) goto done;
        if (ch != '\n' && ch != '\r' && ch != ' ' && ch != '\t') {
            status = XFU_NATIVE_ENVELOPE_NOT_FOUND;
            goto done;
        }
    }
    if (!(temp = native_sink_take(&sink))) {
        /* No room for the payload's copy: said only when its start names an
         * archive; else the ordinary reader takes it as one file. */
        status = native_sink_names_archive(&sink) ? XFU_NATIVE_ENVELOPE_NO_ROOM
                                                   : XFU_NATIVE_ENVELOPE_NOT_FOUND;
        goto done;
    }
    status = native_layer_is_archive(temp, pd) ? XFU_NATIVE_ENVELOPE_ARCHIVE
                                              : XFU_NATIVE_ENVELOPE_RAW;
    if (!native_envelope_hand_over(status, &temp, XX_FILE_TYPE_BASE64, decoded)) {
        status = XFU_NATIVE_ENVELOPE_DECODE_ERROR;
        goto done;
    }
    *raw_name = owned_name;
    owned_name = NULL;
done:
    free(reader);
    if (temp) xx_io_close(temp);
    native_sink_cleanup(&sink);
    free(owned_name);
    return status;
}

xfu_native_envelope_status xfu_native_base64_open(const char *archive_path,
                                                   xx_pd_struct *pd,
                                                   xx_io_device **decoded,
                                                   char **raw_name) {
    xx_io_device *source;
    xfu_native_envelope_status status;
    if (!decoded || !raw_name) return XFU_NATIVE_ENVELOPE_DECODE_ERROR;
    *decoded = NULL;
    *raw_name = NULL;
    if (!archive_path) return XFU_NATIVE_ENVELOPE_DECODE_ERROR;
    source = xx_io_file_open(archive_path, "rb");
    if (!source) return XFU_NATIVE_ENVELOPE_DECODE_ERROR;
    status = xfu_native_base64_open_device(source, pd, decoded, raw_name);
    xx_io_close(source);
    return status;
}

/* "begin <octal mode> <name>" on the first line: only then is the file
 * taken for a uuencoded envelope, so prose that starts with "begin " is
 * left to the detector. */
static bool native_uue_header(xx_io_device *source) {
    char line[1024];
    int64_t total = xx_io_total_size(source);
    size_t length, at = 6, digits = 0, name;
    if (total < 9 || !xx_io_read_at(source, 0, line, 6) || memcmp(line, "begin ", 6))
        return false;
    length = total < (int64_t)sizeof(line) ? (size_t)total : sizeof(line);
    if (!xx_io_read_at(source, 0, line, length)) return false;
    while (at < length && digits < 6U && line[at] >= '0' && line[at] <= '7') {
        ++at; ++digits;
    }
    if (!digits || at >= length || line[at++] != ' ') return false;
    for (name = at; at < length && line[at] != '\n'; ++at) {
        unsigned char ch = (unsigned char)line[at];
        if ((ch < 32 && ch != '\r') || ch == 127) return false;
    }
    if (at < length && at > name && line[at - 1] == '\r') --at;
    return at > name && (at < length || length == (size_t)total);
}

xfu_native_envelope_status xfu_native_uue_open_device(xx_io_device *source,
                                                       xx_pd_struct *pd,
                                                       xx_io_device **decoded,
                                                       char **raw_name) {
    xx_io_device *temp = NULL;
    xxfc_opened opened = {0};
    xx_archive_record_state *records = NULL;
    const xx_archive_record *record;
    const char *original, *safe;
    char *owned_name = NULL;
    native_limited_sink sink = {0};
    xfu_native_envelope_status status = XFU_NATIVE_ENVELOPE_DECODE_ERROR;
    if (!decoded || !raw_name) return status;
    *decoded = NULL;
    *raw_name = NULL;
    if (!source || !pd || xx_pd_is_stopped(pd)) return status;
    if (!native_uue_header(source)) {
        status = XFU_NATIVE_ENVELOPE_NOT_FOUND;
        goto done;
    }
    if (!xxfc_open_named(&opened, source, 0, "uue") ||
        !xx_format_is_valid(opened.format, pd) ||
        !xx_format_handle_base_info(opened.format, pd)) goto done;
    /* Several encoded blocks are an archive in their own right. */
    if (((xx_uue *)opened.format)->number_of_records != 1U) {
        status = XFU_NATIVE_ENVELOPE_NOT_FOUND;
        goto done;
    }
    if (!((xx_uue *)opened.format)->is_terminated) goto done;
    records = xx_uue_create_archive_records_reading(opened.format, NULL, pd);
    if (!records || !(record = xx_uue_get_current_archive_record(opened.format, records)) ||
        !(original = xx_archive_record_get_original_name(record)) ||
        !native_safe_basename(original, &safe)) goto done;
    owned_name = (char *)malloc(strlen(safe) + 1U);
    if (!owned_name) goto done;
    strcpy(owned_name, safe);
    native_sink_init(&sink, XFU_NATIVE_PREFIX_BYTES);
    if (!xx_uue_unpack_current_to_device(opened.format, records, &sink.device, pd) ||
        xx_pd_is_stopped(pd)) {
        /* Too large for the in-memory Test: the ordinary reader streams it. */
        if (sink.over && !xx_pd_is_stopped(pd)) {
            xx_pd_clear_error(pd);
            status = native_sink_names_archive(&sink) ? XFU_NATIVE_ENVELOPE_TOO_LARGE
                                                       : XFU_NATIVE_ENVELOPE_NOT_FOUND;
        } else if (sink.failed && !xx_pd_is_stopped(pd)) {
            xx_pd_clear_error(pd);
            status = native_sink_names_archive(&sink) ? XFU_NATIVE_ENVELOPE_NO_ROOM
                                                       : XFU_NATIVE_ENVELOPE_NOT_FOUND;
        }
        goto done;
    }
    if (!(temp = native_sink_take(&sink))) {
        /* No room for the payload's copy: said only when its start names an
         * archive; else the ordinary reader takes it as one file. */
        status = native_sink_names_archive(&sink) ? XFU_NATIVE_ENVELOPE_NO_ROOM
                                                   : XFU_NATIVE_ENVELOPE_NOT_FOUND;
        goto done;
    }
    status = native_layer_is_archive(temp, pd) ? XFU_NATIVE_ENVELOPE_ARCHIVE
                                              : XFU_NATIVE_ENVELOPE_RAW;
    if (!native_envelope_hand_over(status, &temp, XX_FILE_TYPE_UUE, decoded)) {
        status = XFU_NATIVE_ENVELOPE_DECODE_ERROR;
        goto done;
    }
    *raw_name = owned_name;
    owned_name = NULL;
done:
    if (records) xx_uue_free_archive_records_reading(opened.format, records);
    xxfc_close(&opened);
    if (temp) xx_io_close(temp);
    native_sink_cleanup(&sink);
    free(owned_name);
    return status;
}

xfu_native_envelope_status xfu_native_uue_open(const char *archive_path,
                                                xx_pd_struct *pd,
                                                xx_io_device **decoded,
                                                char **raw_name) {
    xx_io_device *source;
    xfu_native_envelope_status status;
    if (!decoded || !raw_name) return XFU_NATIVE_ENVELOPE_DECODE_ERROR;
    *decoded = NULL;
    *raw_name = NULL;
    if (!archive_path) return XFU_NATIVE_ENVELOPE_DECODE_ERROR;
    source = xx_io_file_open(archive_path, "rb");
    if (!source) return XFU_NATIVE_ENVELOPE_DECODE_ERROR;
    status = xfu_native_uue_open_device(source, pd, decoded, raw_name);
    xx_io_close(source);
    return status;
}

static void native_log(const xfu_request *request, bool error,
                       const char *message) {
    if (request->callbacks.log)
        request->callbacks.log(request->callbacks.user, error, message);
}

/* A member name has no length limit, so a line is never cut to a buffer. */
static void native_logf(const xfu_request *request, bool error,
                        const char *format, ...) {
    va_list args, copy;
    char local[256];
    char *line = local;
    int count;
    if (!request->callbacks.log) return;
    va_start(args, format);
    va_copy(copy, args);
    count = vsnprintf(local, sizeof(local), format, args);
    va_end(args);
    if (count >= (int)sizeof(local)) {
        line = (char *)malloc((size_t)count + 1);
        if (line) vsnprintf(line, (size_t)count + 1, format, copy);
        else line = local;
    }
    va_end(copy);
    if (count >= 0) native_log(request, error, line);
    if (line != local) free(line);
}

static bool native_cancelled(const xfu_request *request, xx_pd_struct *pd) {
    if (request->callbacks.cancelled &&
        request->callbacks.cancelled(request->callbacks.user)) xx_pd_stop(pd);
    return xx_pd_is_stopped(pd);
}

/* Why the last component of `path` cannot be a member name, or NULL when
 * it can (*name then points at it). */
static const char *native_basename_problem(const char *path, const char **name) {
    const char *part = path, *cursor;
    size_t length, i;
    if (!path) return "it has no name";
    for (cursor = path; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\') part = cursor + 1;
#ifdef _WIN32
    /* "C:name" names a file in the current folder of drive C. */
    if (part == path && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':') part = path + 2;
#endif
    length = strlen(part);
    if (!length) return "it has no file name";
    if (!strcmp(part, ".") || !strcmp(part, "..")) return "its name is a folder reference";
    for (i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)part[i];
        if (ch < 32 || ch == 127) return "its name holds a control character";
#ifdef _WIN32
        /* ':' names a drive or a stream there; elsewhere it is ordinary. */
        if (ch == ':') return "its name holds ':'";
        if (ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*') return "its name holds a character Windows does not allow";
#endif
    }
#ifdef _WIN32
    if (part[length - 1] == '.' || part[length - 1] == ' ')
        return "its name ends with a dot or a space";
#endif
    *name = part;
    return NULL;
}

/* The name of an envelope this probe decodes. One with ':' is left to the
 * ordinary Base64 and uuencode readers, which take what follows its last
 * '/', '\' or ':' (once a folder separator too) for the member's name. */
static bool native_safe_basename(const char *path, const char **name) {
    return native_basename_problem(path, name) == NULL && !strchr(path, ':');
}

static char *native_join(const char *directory, const char *name) {
    size_t a = strlen(directory), b = strlen(name);
    char *result;
    if (a > SIZE_MAX - b - 2) return NULL;
    result = (char *)malloc(a + b + 2);
    if (!result) return NULL;
    memcpy(result, directory, a);
    if (a && directory[a - 1] != '/' && directory[a - 1] != '\\')
        result[a++] = '/';
    memcpy(result + a, name, b + 1);
    return result;
}

static bool native_same_file(const char *left, const char *right) {
#ifdef _WIN32
    wchar_t *a = xx_str_utf8_to_unicode(left), *b = xx_str_utf8_to_unicode(right);
    HANDLE ah = INVALID_HANDLE_VALUE, bh = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION ai, bi;
    bool same = false;
    if (a && b) {
        ah = CreateFileW(a, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        bh = CreateFileW(b, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        same = ah != INVALID_HANDLE_VALUE && bh != INVALID_HANDLE_VALUE &&
            GetFileInformationByHandle(ah, &ai) && GetFileInformationByHandle(bh, &bi) &&
            ai.dwVolumeSerialNumber == bi.dwVolumeSerialNumber &&
            ai.nFileIndexHigh == bi.nFileIndexHigh && ai.nFileIndexLow == bi.nFileIndexLow;
    }
    if (ah != INVALID_HANDLE_VALUE) CloseHandle(ah);
    if (bh != INVALID_HANDLE_VALUE) CloseHandle(bh);
    xx_str_wfree(a); xx_str_wfree(b);
    return same;
#else
    struct stat a, b;
    return (stat(left, &a) == 0 && stat(right, &b) == 0 &&
        a.st_dev == b.st_dev && a.st_ino == b.st_ino) || !strcmp(left, right);
#endif
}

/* A short name in the destination's folder (the same filesystem, so the
 * final rename is atomic), whatever the length of the member's own name. */
static xx_io_device *native_stage(const char *destination, char **stage,
                                   const void *nonce) {
    size_t length = strlen(destination), folder = 0, i;
    unsigned long tag = (unsigned long)(((uintptr_t)nonce >> 4) ^ (uintptr_t)time(NULL)) & 0xFFFFFFFFUL;
    char *name;
    unsigned attempt;
    *stage = NULL;
    for (i = 0; i < length; ++i)
        if (destination[i] == '/' || destination[i] == '\\') folder = i + 1;
    if (folder > SIZE_MAX - 32) return NULL;
    name = (char *)malloc(folder + 32);
    if (!name) return NULL;
    memcpy(name, destination, folder);
    for (attempt = 0; attempt < 128; ++attempt) {
        xx_io_device *output;
        snprintf(name + folder, 32, ".xfu-%08lx-%u.tmp", tag, attempt);
        if (!strcmp(name, destination)) continue;
        output = xx_io_file_open(name, "wbx");
        if (output) { *stage = name; return output; }
    }
    free(name);
    return NULL;
}

/* The type that holds the member: the envelope a payload came from (see
 * native_payload), or an archive that is itself a Base64 or uuencoded
 * envelope (as its own reader did before envelopes were decoded here);
 * anything else is BINARY. Frontends treat BINARY as "not an archive" and
 * offer no extraction for it. */
static xx_file_type_t native_envelope_type(const char *archive_path,
                                           xx_io_device *input) {
    static const char base64[] = "begin-base64 ", uue[] = "begin ";
    char prefix[sizeof(base64) - 1U];
    xx_io_device *device;
    bool read;
    if (input && input->close == native_payload_close)
        return ((native_payload *)input->priv)->type;
    device = xx_io_file_open(archive_path, "rb");
    read = device && xx_io_read_at(device, 0, prefix, sizeof(prefix));
    if (device) xx_io_close(device);
    if (read && !memcmp(prefix, base64, sizeof(base64) - 1U))
        return XX_FILE_TYPE_BASE64;
    if (read && !memcmp(prefix, uue, sizeof(uue) - 1U))
        return XX_FILE_TYPE_UUE;
    return XX_FILE_TYPE_BINARY;
}

static int native_raw_device_read(const xfu_request *request,
                                  const char *unpack_to, xx_pd_struct *pd,
                                  xx_io_device *input, const char *name,
                                  xx_file_type_t type) {
    xx_io_device *output = NULL;
    char *destination = NULL, *stage = NULL;
    const char *safe_name;
    xfu_entry entry = {0};
    xx_file_type_t types[XX_FILE_TYPE_CHAIN_MAX];
    size_t type_count;
    unsigned char buffer[XFU_NATIVE_READ_BUFFER];
    int64_t size, position = 0;
    int level = -1, result = 0;
    bool selected, success = true, complete = false, cut = false, replacing = false;
    bool testing = request && request->command == XFU_COMMAND_TEST;
    const char *problem, *reason = NULL;
    if (!request || !request->archive_path || !pd || !input) return 2;
    problem = native_basename_problem(name, &safe_name);
    if (!problem && safe_name != name) problem = "its member name holds a path separator";
    if (problem) {
        native_logf(request, true, "%s: cannot be read as one file: %s",
                    request->archive_path, problem);
        return 2;
    }
    if (xx_io_seek64(input, 0, SEEK_SET) != 0) {
        native_logf(request, true, "%s: cannot rewind raw input", request->archive_path);
        return 2;
    }
    if (testing && request->callbacks.test_progress)
        request->callbacks.test_progress(request->callbacks.user, 0);
    if (native_cancelled(request, pd)) {
        native_log(request, true, "operation cancelled");
        return 1;
    }
    size = xx_io_total_size(input);
    if (size < 0) { native_log(request, true, "cannot size raw input"); result = 2; goto done; }
    level = xx_pd_enter_level(pd, (uint64_t)size, "raw");
    type_count = xx_format_get_file_type_chain(type, types, XX_FILE_TYPE_CHAIN_MAX);
    if (!type_count || type_count > XX_FILE_TYPE_CHAIN_MAX) {
        types[0] = type = XX_FILE_TYPE_BINARY;
        type_count = 1;
    }
    if (request->callbacks.file_types)
        request->callbacks.file_types(request->callbacks.user, types, type_count, type);
    if (request->callbacks.unpack_handle_types) {
        xx_handle_type_t handle = xx_format_handle_type_from_file_type(type);
        request->callbacks.unpack_handle_types(request->callbacks.user,
            handle == XX_HANDLE_TYPE_UNKNOWN ? NULL : &handle,
            handle == XX_HANDLE_TYPE_UNKNOWN ? 0 : 1, handle);
    }
    /* The same log shape as the member walk: the interpretation, one line
     * per member and the member count. */
    native_logf(request, false, "%s: %s", request->archive_path,
                type == XX_FILE_TYPE_BINARY ? "Binary" : xx_format_file_type_to_string(type));
    selected = !request->extract_selected ||
        (request->selected_record_count == 1 && request->selected_records &&
         request->selected_records[0] == 0);
    entry.name = name;
    entry.packed_size = size;
    entry.unpacked_size = size;
    if (selected && request->callbacks.entry)
        request->callbacks.entry(request->callbacks.user, &entry);
    if (selected && request->callbacks.progress)
        request->callbacks.progress(request->callbacks.user, 0, 1, name);
    if (request->command == XFU_COMMAND_LIST)
        native_logf(request, false, "  %12lld  %s", (long long)size, name);
    if (selected && request->command == XFU_COMMAND_EXTRACT) {
        /* A failure is the member's "-- FAILED" line, as in the walk. */
        destination = native_join(unpack_to ? unpack_to : ".", name);
        /* Replaced, as the walk replaces one (core.h, output_dir). */
        replacing = destination && xx_io_file_exists_a(destination);
        if (destination && native_same_file(destination, request->archive_path))
            reason = "it would replace the archive";
        if (!destination || reason || !xx_io_create_dirs_a(destination, false) ||
            !(output = native_stage(destination, &stage, input)))
            success = false;
    }
    if (success && selected && (request->command == XFU_COMMAND_EXTRACT ||
                                request->command == XFU_COMMAND_TEST)) {
        while (position < size && !native_cancelled(request, pd)) {
            size_t requested = (uint64_t)(size - position) < sizeof(buffer)
                ? (size_t)(size - position) : sizeof(buffer);
            ssize_t count = xx_io_read(input, buffer, requested);
            size_t written = 0;
            if (count <= 0 || (size_t)count > requested) { success = false; break; }
            while (output && written < (size_t)count) {
                ssize_t out = xx_io_write(output, buffer + written, (size_t)count - written);
                if (out <= 0 || (size_t)out > (size_t)count - written) {
                    success = false; break;
                }
                written += (size_t)out;
            }
            if (!success) break;
            position += count;
            xx_pd_set_current(pd, level, (uint64_t)position);
            if (request->command == XFU_COMMAND_TEST && size > 0 &&
                request->callbacks.test_progress) {
                unsigned percent = (unsigned)((long double)position * 99.0L / size);
                request->callbacks.test_progress(request->callbacks.user, percent);
            }
        }
        if (position != size || native_cancelled(request, pd)) success = false;
    }
    if (output) {
        if (xx_io_close(output) != 0) success = false;
        output = NULL;
    }
    if (stage) {
        /* A Cancel at this point still leaves no staged or partial file. */
        if (success && native_cancelled(request, pd)) success = false;
        if (success && !xx_io_file_replace_a(stage, destination, true)) success = false;
        if (!success) (void)xx_io_file_remove_a(stage);
    }
    if (selected && (request->command == XFU_COMMAND_EXTRACT || testing)) {
        cut = !success && native_cancelled(request, pd);
        if (cut)
            native_logf(request, true, "  %s -- cancelled", name);
        else if (testing && request->callbacks.test_result)
            request->callbacks.test_result(request->callbacks.user, &entry, success);
        else if (success && replacing)
            native_logf(request, false, "  %s -- replaced an existing file", name);
        else if (success)
            native_logf(request, false, testing ? "  %s -- OK" : "  %s", name);
        else if (reason)
            native_logf(request, true, "  %s -- FAILED (%s)", name, reason);
        else
            native_logf(request, true, "  %s -- FAILED", name);
    }
    if (selected && request->callbacks.progress)
        request->callbacks.progress(request->callbacks.user, 1, 1, name);
    /* A member cut short by Cancel is neither failed nor counted. */
    if (!success && !cut)
        native_logf(request, testing, "%d member(s), 1 failed", selected ? 1 : 0);
    else
        native_logf(request, false, "%d member(s)", selected && !cut ? 1 : 0);
    if (!success) result = 1;
    complete = !native_cancelled(request, pd) && success;
    if (complete && request->extract_selected && !selected) {
        native_log(request, true, "selected members are no longer present in the archive");
        result = 1;
    }
    if (complete && testing && request->callbacks.test_progress)
        request->callbacks.test_progress(request->callbacks.user, 100);
done:
    if (output) xx_io_close(output);
    if (stage && !success) (void)xx_io_file_remove_a(stage);
    free(stage); free(destination);
    if (level >= 0) xx_pd_leave_level(pd, level);
    if (request->callbacks.walk_end)
        request->callbacks.walk_end(request->callbacks.user, complete);
    return result;
}

int xfu_native_raw_device_read(const xfu_request *request,
                               const char *unpack_to, xx_pd_struct *pd,
                               xx_io_device *input, const char *name) {
    if (!request || !request->archive_path) return 2;
    return native_raw_device_read(request, unpack_to, pd, input, name,
                                  native_envelope_type(request->archive_path, input));
}

int xfu_native_raw_read(const xfu_request *request, const char *unpack_to,
                        xx_pd_struct *pd) {
    xx_io_device *input;
    const char *name, *problem;
    int result;
    if (!request || !request->archive_path) return 2;
    /* The file's own name is the member's: one it cannot be is said so. */
    problem = native_basename_problem(request->archive_path, &name);
    if (problem) {
        native_logf(request, true, "%s: cannot be read as one file: %s",
                    request->archive_path, problem);
        return 2;
    }
    input = xx_io_file_open(request->archive_path, "rb");
    if (!input) { native_logf(request, true, "cannot open %s", request->archive_path); return 2; }
    result = native_raw_device_read(request, unpack_to, pd, input, name,
                                    XX_FILE_TYPE_BINARY);
    xx_io_close(input);
    return result;
}

int xfu_native_empty_read(const xfu_request *request, xx_pd_struct *pd) {
    xx_io_device *input;
    int64_t size;
    if (!request || !request->archive_path || !pd) return 2;
    if (request->command == XFU_COMMAND_TEST && request->callbacks.test_progress)
        request->callbacks.test_progress(request->callbacks.user, 0);
    if (native_cancelled(request, pd)) {
        native_log(request, true, "operation cancelled");
        return 1;
    }
    input = xx_io_file_open(request->archive_path, "rb");
    if (!input) {
        native_log(request, true, "cannot open empty archive");
        return 2;
    }
    size = xx_io_total_size(input);
    xx_io_close(input);
    if (size != 0) {
        native_log(request, true, "empty archive has content");
        return 2;
    }
    native_log(request, false, "0 member(s)");
    if (request->extract_selected) {
        native_log(request, true, "selected member does not exist");
        if (request->callbacks.walk_end)
            request->callbacks.walk_end(request->callbacks.user, true);
        return 1;
    }
    if (request->command == XFU_COMMAND_TEST && request->callbacks.test_progress)
        request->callbacks.test_progress(request->callbacks.user, 100);
    if (request->callbacks.walk_end)
        request->callbacks.walk_end(request->callbacks.user, true);
    return 0;
}
