/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/console/xx_archive_console.h"
#include "core.h"
#if !XFU_FORMAT_SUPPORT_FULL
#include "native_writer_backend.h"
#endif
#if defined(XFU_SUPPORT_FORMAT_TAR) && !XFU_FORMAT_SUPPORT_FULL
#include "native_tar_writer.h"
#include "xxfclib/algo/lzma_alone/xx_lzma_alone.h"
#include "../formats/archive/tar_common/xx_tar_common.h"
#endif
#if XFU_FORMAT_SUPPORT_FULL
#include "native_reader_backend.h"
#include "native_tar_writer.h"
#include "supported_types.h"
#include "xxfclib/algo/lzma_alone/xx_lzma_alone.h"
#include "../formats/archive/tar_common/xx_tar_common.h"
#include <xxfclib/formats/filesystem/hxc_raw_floppy/xx_hxc_raw_floppy.h>
#endif
#include "xxfclib/formats/xx_format.h"

#include <xxfclib/terminal/xx_terminal.h>

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <xxfclib/strings/xx_string.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct encryption_notice {
    unsigned seen;
    bool output_failed;
} encryption_notice;

typedef struct console_test {
    xx_pd_struct progress;
    int last_percent;
    bool verbose, terminal, line_open, output_failed;
    encryption_notice encryption;
} console_test;

typedef enum output_format {
    OUTPUT_TEXT = 0,
    OUTPUT_JSON,
    OUTPUT_XML
} output_format;

typedef struct structured_output {
    output_format format;
    bool first_event;
    bool output_failed;
    encryption_notice encryption;
} structured_output;

/* One row per format profile, in xx_archive_console_format order. reader is
 * the family's name in xxfclib/formats/xx_format_support_rows.inc; type is
 * what the core opens when no reader is named; create and filter are the
 * defaults for `a` (NULL create: the family has no writer here; NULL filter:
 * TAR takes it from the destination name). A "raw" create is a one-file
 * stream behind the family's filter. */
typedef struct profile_info {
    xx_archive_console_format format;
    const char *label;
    const char *reader;
    xx_file_type_t type;
    const char *create;
    const char *filter;
} profile_info;

/* The profiles this build links: ALL and the families whose flags are
 * defined, from the generated xxfclib/console/xx_archive_console_profiles.inc.
 * A tool carries its own family's row, not the whole catalog. */
static const profile_info profiles[] = {
    {XX_ARCHIVE_CONSOLE_ALL, "", NULL, XX_FILE_TYPE_UNKNOWN, NULL, NULL},
#define XX_ARCHIVE_PROFILE_ID(id)
#define XX_ARCHIVE_PROFILE(id, label, reader, type, create, filter) \
    {XX_ARCHIVE_CONSOLE_##id, label, reader, type, create, filter},
#include "xxfclib/console/xx_archive_console_profiles.inc"
#undef XX_ARCHIVE_PROFILE
#undef XX_ARCHIVE_PROFILE_ID
};

static const profile_info *profile_find(xx_archive_console_format format) {
    size_t i;
    for (i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i)
        if (profiles[i].format == format) return &profiles[i];
    return NULL;
}

/* The configuration was validated by xx_archive_console_main(). */
static const profile_info *profile_of(const xx_archive_console_config *config) {
    const profile_info *profile = profile_find(config->format);
    return profile ? profile : &profiles[0];
}

static const char *profile_description(const xx_archive_console_config *config) {
    return profile_of(config)->label;
}

/* Whether this build links the profile's family. */
static bool profile_compiled(xx_archive_console_format format) {
    return profile_find(format) != NULL;
}

/* A one-file stream: `a` writes --format=raw behind the family's filter. */
static bool profile_stream(const profile_info *profile) {
    return profile->create && !strcmp(profile->create, "raw");
}

static bool profile_type_allowed(const xx_archive_console_config *config,
                                  xx_file_type_t type) {
    xx_file_type_t chain[XX_FILE_TYPE_CHAIN_MAX];
    size_t i, count;
    if (config->format == XX_ARCHIVE_CONSOLE_ALL) return true;
    if (config->format == XX_ARCHIVE_CONSOLE_TAR) {
        switch (type) {
            case XX_FILE_TYPE_TAR:
            case XX_FILE_TYPE_TAR_GZ:
            case XX_FILE_TYPE_TAR_BZ2:
            case XX_FILE_TYPE_TAR_XZ:
            case XX_FILE_TYPE_TAR_LZ4:
            case XX_FILE_TYPE_TAR_ZSTD:
            case XX_FILE_TYPE_TAR_COMPRESS:
            case XX_FILE_TYPE_TAR_LZIP:
            case XX_FILE_TYPE_TAR_LZMA:
            case XX_FILE_TYPE_TAR_LZOP: return true;
            default: return false;
        }
    }
    /* The family's own type and what the catalog derives from it: ZIP-based
     * packages such as JAR open as ZIP. */
    count = xx_format_get_file_type_chain(type, chain, XX_FILE_TYPE_CHAIN_MAX);
    for (i = 0; i < count; ++i)
        if (chain[i] == profile_of(config)->type) return true;
    return false;
}

static bool profile_tar_can_create(const char *name) {
#ifdef XFU_SUPPORT_FORMAT_TAR
    return xfu_native_tar_can_create(name);
#else
    (void)name;
    return false;
#endif
}

static bool profile_ar_can_create(const char *name) {
#ifdef XFU_SUPPORT_FORMAT_AR
    return name && (!strcmp(name, "ar") || !strcmp(name, "ar_bsd") ||
        !strcmp(name, "ar_svr4") || !strcmp(name, "ar_gnu") ||
        !strcmp(name, "arbsd") || !strcmp(name, "argnu") || !strcmp(name, "arsvr4"));
#else
    (void)name;
    return false;
#endif
}

/* The --format names the profile creates. */
static bool profile_can_create(const xx_archive_console_config *config, const char *name) {
    if (!name) return false;
    switch (config->format) {
        case XX_ARCHIVE_CONSOLE_ALL: return true;
        case XX_ARCHIVE_CONSOLE_ZIP: return !strcmp(name, "zip");
        case XX_ARCHIVE_CONSOLE_TAR: return profile_tar_can_create(name);
        case XX_ARCHIVE_CONSOLE_AR: return profile_ar_can_create(name);
        case XX_ARCHIVE_CONSOLE_7ZIP: return !strcmp(name, "7zip") || !strcmp(name, "7z");
        case XX_ARCHIVE_CONSOLE_ISO9660:
            return !strcmp(name, "iso9660") || !strcmp(name, "iso") || !strcmp(name, "cd9660");
        case XX_ARCHIVE_CONSOLE_CPIO:
            return !strcmp(name, "cpio") || !strcmp(name, "cpio_newc") || !strcmp(name, "newc");
        default: return profile_stream(profile_of(config)) && !strcmp(name, "raw");
    }
}

/* The --filter names the profile creates with. */
static bool profile_filter_allowed(const xx_archive_console_config *config, const char *name) {
    const profile_info *profile = profile_of(config);
    if (!name) return false;
    if (config->format == XX_ARCHIVE_CONSOLE_ALL) return true;
    if (config->format == XX_ARCHIVE_CONSOLE_TAR) return xfu_native_can_create_filter(name);
    return profile->filter && !strcmp(name, profile->filter);
}

/* Keep capability answers derived from the backend, limited to this tool's
 * scope. A format label never advertises a writer that is not compiled. */
static char *profile_capabilities(const xx_archive_console_config *config) {
    const profile_info *profile = profile_of(config);
    char *all, *result, *line;
    size_t used = 0;
#if XFU_FORMAT_SUPPORT_FULL
    if (config->format == XX_ARCHIVE_CONSOLE_ALL) return xfu_capabilities_text();
#else
    if (config->format == XX_ARCHIVE_CONSOLE_ALL) return xfu_native_capabilities_text();
#endif
    all = xfu_native_capabilities_text();
    if (!all) return NULL;
    result = (char *)malloc(strlen(all) + 1);
    if (!result) { free(all); return NULL; }
    for (line = all; *line;) {
        char name[64];
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) + 1 : strlen(line);
        bool keep = !strncmp(line, "backend ", 8);
        if (sscanf(line, "format %63s", name) == 1)
            keep = profile_can_create(config, name) ||
                   (profile->reader && !strcmp(name, profile->reader)) ||
                   (config->format == XX_ARCHIVE_CONSOLE_TAR && !strcmp(name, "tar_solaris"));
        else if (sscanf(line, "filter %63s", name) == 1)
            keep = profile_filter_allowed(config, name);
        if (keep) { memcpy(result + used, line, length); used += length; }
        line += length;
    }
    result[used] = '\0';
    free(all);
    return result;
}

static char *profile_supported_types(const xx_archive_console_config *config) {
    xx_list_t *types;
    char *capabilities, *text;
    size_t i, count = 0, capacity = 256, used;
#if XFU_FORMAT_SUPPORT_FULL
    if (config->format == XX_ARCHIVE_CONSOLE_ALL) return xfu_supported_types_text(NULL);
#endif
    types = xx_format_get_supported_file_types();
    if (!types) return NULL;
    capabilities = profile_capabilities(config);
    if (!capabilities) { xx_list_destroy(types); return NULL; }
    capacity += strlen(capabilities);
    for (i = 0; i < xx_list_count(types); ++i) {
        xx_file_type_t type = *(const xx_file_type_t *)xx_list_at(types, i);
        if (!profile_type_allowed(config, type)) continue;
        ++count;
        capacity += strlen(xx_format_file_type_to_string(type)) + 32;
    }
    text = (char *)malloc(capacity);
    if (text) {
        used = (size_t)snprintf(text, capacity,
            "Supported %sfile types (xxfclib): %zu\n\n%s\n   ID  File type\n",
            profile_description(config), count, capabilities);
        for (i = 0; i < xx_list_count(types); ++i) {
            xx_file_type_t type = *(const xx_file_type_t *)xx_list_at(types, i);
            if (profile_type_allowed(config, type))
                used += (size_t)snprintf(text + used, capacity - used, "%5u  %s\n",
                    (unsigned)type, xx_format_file_type_to_string(type));
        }
    }
    free(capabilities);
    xx_list_destroy(types);
    return text;
}

static bool profile_error(const xfu_request *request, const char *message) {
    if (request->callbacks.log)
        request->callbacks.log(request->callbacks.user, true, message);
    return false;
}

static int handle_name_equal(const char *left, const char *right);

static bool profile_reader_allowed(const xx_archive_console_config *config,
                                    const char *name) {
    static const char *const tar_readers[] = {
        "tar", "tar_gz", "tar_bz2", "tar_xz", "tar_lz4", "tar_zstd",
        "tar_compress", "tar_lzip", "tar_lzma", "tar_lzop", "tar_lz", "tar_zst"
    };
    /* The engine's reader aliases (xxfc_readers.c) of the restricted families. */
    static const struct { const char *name; xx_file_type_t type; } aliases[] = {
        {"zip64", XX_FILE_TYPE_ZIP64}, {"7z", XX_FILE_TYPE_7ZIP},
        {"gzip", XX_FILE_TYPE_GZ}, {"bzip2", XX_FILE_TYPE_BZ2}
    };
    xx_list_t *types;
    size_t i;
    bool allowed = false;
    if (config->format == XX_ARCHIVE_CONSOLE_TAR)
        for (i = 0; i < sizeof(tar_readers) / sizeof(tar_readers[0]); ++i)
            if (handle_name_equal(name, tar_readers[i])) return true;
    for (i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i)
        if (handle_name_equal(name, aliases[i].name))
            return profile_type_allowed(config, aliases[i].type);
    types = xx_format_get_supported_file_types();
    if (!types) return false;
    for (i = 0; i < xx_list_count(types); ++i) {
        xx_file_type_t type = *(const xx_file_type_t *)xx_list_at(types, i);
        if (profile_type_allowed(config, type) &&
            handle_name_equal(name, xx_format_file_type_to_string(type))) {
            allowed = true;
            break;
        }
    }
    xx_list_destroy(types);
    return allowed;
}

#ifdef XFU_SUPPORT_FORMAT_TAR
/* The compound LZ4 reader owns single content-sized frames. The unpacker
 * handles unsized or multiple frames by decoding a layer, then reading TAR.
 * Inspect framing only, with a bounded number of reads and no decompression. */
static bool profile_lz4_single_sized(xx_io_device *device, int64_t offset, int64_t size) {
    uint8_t header[6], block[4];
    int64_t cursor;
    size_t i;
    if (!xx_io_read_at(device, offset, header, sizeof(header)) ||
        memcmp(header, "\x04\x22\x4d\x18", 4) || !(header[4] & 0x08U)) return false;
    cursor = offset + 7 + 8 + ((header[4] & 1U) ? 4 : 0);
    for (i = 0; i < 65536 && cursor <= size - 4; ++i) {
        uint32_t length;
        if (!xx_io_read_at(device, cursor, block, sizeof(block))) return true;
        cursor += 4;
        length = (uint32_t)block[0] | ((uint32_t)block[1] << 8) |
                 ((uint32_t)block[2] << 16) | ((uint32_t)block[3] << 24);
        if (!length) {
            if (header[4] & 0x04U) cursor += 4;
            return cursor >= size; /* A cut checksum stays with the compound reader. */
        }
        length &= UINT32_C(0x7fffffff);
        if ((uint64_t)length > (uint64_t)(size - cursor)) return true;
        cursor += length;
        if (header[4] & 0x10U) cursor += 4;
    }
    return i < 65536; /* A cut block retains the compound reader's recovery. */
}

/* Select only a candidate compound reader here. Fixed signatures perform no
 * decoding; the core validates the complete TAR and compression stream under
 * the operation's progress and memory limits. This also works after renaming
 * a compressed archive, without trusting its filename extension. */
static const char *profile_tar_reader(const char *path) {
    static const struct { xx_file_type_t type; const char *reader; } codecs[] = {
        {XX_FILE_TYPE_GZ, "tar_gz"}, {XX_FILE_TYPE_BZ2, "tar_bz2"},
        {XX_FILE_TYPE_XZ, "tar_xz"}, {XX_FILE_TYPE_LZIP, "tar_lzip"},
        {XX_FILE_TYPE_LZOP, "tar_lzop"}, {XX_FILE_TYPE_UNIX_COMPRESS, "tar_compress"},
        {XX_FILE_TYPE_LZ4, "tar_lz4"}, {XX_FILE_TYPE_ZSTD, "tar_zstd"}
    };
    xx_io_device *device = xx_io_file_open(path, "rb");
    Abstractextractor *extractor;
    const char *reader = NULL;
    uint8_t head[18];
    uint8_t tar_block[512];
    int64_t offset = 0, size;
    size_t i;
    if (!device) return NULL; /* The core supplies the normal open diagnostic. */
    /* V7 has no ustar magic. Its checksum must take precedence over names
     * that happen to resemble a compression header (for example "cb"). */
    if (xx_io_read_at(device, 0, tar_block, sizeof(tar_block)) &&
        xx_tar_block_is_header(tar_block)) goto done;
    extractor = xx_abstract_extractor_get(XX_FILE_TYPE_TAR);
    if (extractor && extractor->signature && extractor->signature(device, 0, false) == 1)
        goto done;
    size = xx_io_total_size(device);
    /* LZ4 and Zstandard share skippable frames. Read at most 64 framing
     * headers, skipping payloads by offset, to find the actual codec magic. */
    for (i = 0; i < 64 && offset <= size - 8 && xx_io_read_at(device, offset, head, 8); ++i) {
        uint32_t length;
        if ((head[0] & 0xf0U) != 0x50U || head[1] != 0x2a || head[2] != 0x4d || head[3] != 0x18)
            break;
        length = (uint32_t)head[4] | ((uint32_t)head[5] << 8) |
                 ((uint32_t)head[6] << 16) | ((uint32_t)head[7] << 24);
        if ((uint64_t)length > (uint64_t)(size - offset - 8)) goto done;
        offset += 8 + (int64_t)length;
    }
    if (i == 64) goto done;
    for (i = 0; i < sizeof(codecs) / sizeof(codecs[0]); ++i) {
        extractor = xx_abstract_extractor_get(codecs[i].type);
        if (extractor && extractor->signature && extractor->signature(device, offset, false) == 1) {
            if (codecs[i].type != XX_FILE_TYPE_LZ4 || profile_lz4_single_sized(device, offset, size))
                reader = codecs[i].reader;
            goto done;
        }
    }
    /* LZMA-Alone has a property header instead of a fixed magic. Requiring
     * its range-coder prefix and nonzero dictionary avoids treating a plain
     * TAR's short, zero-padded member name as a compression header. */
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_LZMA)
    if (!offset && xx_io_read_at(device, 0, head, sizeof(head)) &&
        !head[13] && (head[1] || head[2] || head[3] || head[4]) &&
        xx_lzma_alone_has_header(head, sizeof(head))) reader = "tar_lzma";
#endif
done:
    xx_io_close(device);
    return reader;
}
#endif

/* Apply the same policy after parsing every alias. In particular --reader
 * and --handle cannot bypass the application's archive family. The core
 * validates selected types against file contents, never just extensions. */
static bool profile_apply(const xx_archive_console_config *config, xfu_request *request) {
    const profile_info *profile = profile_of(config);
    if (config->format == XX_ARCHIVE_CONSOLE_ALL) return true;
    if (request->command == XFU_COMMAND_ADD) {
        static char message[96];
        if (!profile->create)
            return profile_error(request, "a: this archive family has no writer in this tool");
        if (request->archive_format && !profile_can_create(config, request->archive_format))
            return profile_error(request, "--format: select a creation format for this archive family");
        if (request->volume_size)
            return profile_error(request, "--volume-size requires the split writer; unavailable for this archive family");
        if (request->archive_filter && !profile_filter_allowed(config, request->archive_filter)) {
            if (!profile->filter)
                return profile_error(request, "--filter: select a filter listed by --capabilities");
            snprintf(message, sizeof(message), "--filter: select %s for this archive family",
                     profile->filter);
            return profile_error(request, message);
        }
        if (!request->archive_format) request->archive_format = profile->create;
        /* ZIP must remain ZIP even when the output filename ends in .gz, and
         * a one-file stream always gets its codec. TAR intentionally inherits
         * its outer filter from the extension (a NULL default). */
        if (!request->archive_filter) request->archive_filter = profile->filter;
        return true;
    }
    if (request->handle_type != XX_HANDLE_TYPE_UNKNOWN) {
        if (!profile_type_allowed(config, xx_format_handle_type_file_type(request->handle_type)))
            return profile_error(request, "--handle: select an unpack handle for this archive family");
        return true;
    }
    if (request->reader_name) {
        const char *name = request->reader_name;
#if !XFU_FORMAT_SUPPORT_FULL
        if (!strcmp(name, "native")) name = NULL;
        else if (!strncmp(name, "native:", 7)) name += 7;
#else
        bool raw = false;
        if (!strncmp(name, "native", 6)) {
            if (!xfu_native_reader_resolve(name, &name, &raw) || raw)
                return profile_error(request, "--reader: select a reader for this archive format");
        }
#endif
        if (name && !profile_reader_allowed(config, name))
            return profile_error(request, "--reader: select a reader for this archive family");
        /* A bare native selector must still be constrained by this profile. */
        request->reader_name = name;
        if (name) return true;
    }
#ifdef XFU_SUPPORT_FORMAT_TAR
    if (config->format == XX_ARCHIVE_CONSOLE_TAR)
        request->reader_name = profile_tar_reader(request->archive_path);
#endif
    if (!request->reader_name) request->file_type = profile->type;
    return true;
}

static bool console_is_terminal(void) {
#ifdef _WIN32
    DWORD mode;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    return output != INVALID_HANDLE_VALUE && GetConsoleMode(output, &mode) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

/* Whether the well-formed UTF-8 sequence of length bytes at p is a format
 * character (Unicode Cf: the bidirectional controls U+061C, U+200E, U+200F,
 * U+202A-U+202E and U+2066-U+2069, U+00AD, U+200B, U+FEFF, the tags
 * U+E0001 and U+E0020-U+E007F, those of an emoji flag too), a line or
 * paragraph separator (U+2028, U+2029), or another code point that draws
 * nothing (U+034F, the Hangul fillers U+115F, U+1160, U+3164 and U+FFA0,
 * U+17B4, U+17B5, and the unassigned default-ignorable ones): able to show a
 * name reversed, on two lines, or the same as another. U+200C and U+200D
 * stay: scripts and emoji need them; the variation selectors are
 * console_hidden_selector's. */
static int console_format_control(const unsigned char *p, size_t length) {
    static const unsigned long ranges[][2] = {
        {0x00AD, 0x00AD}, {0x034F, 0x034F}, {0x0600, 0x0605}, {0x061C, 0x061C},
        {0x06DD, 0x06DD}, {0x070F, 0x070F}, {0x0890, 0x0891}, {0x08E2, 0x08E2},
        {0x115F, 0x1160}, {0x17B4, 0x17B5}, {0x180E, 0x180E}, {0x200B, 0x200B},
        {0x200E, 0x200F}, {0x2028, 0x202E}, {0x2060, 0x206F}, {0x3164, 0x3164},
        {0xFEFF, 0xFEFF}, {0xFFA0, 0xFFA0}, {0xFFF0, 0xFFFB}, {0x110BD, 0x110BD},
        {0x110CD, 0x110CD}, {0x13430, 0x1343F}, {0x1BCA0, 0x1BCA3}, {0x1D173, 0x1D17A},
        {0xE0000, 0xE00FF}, {0xE01F0, 0xE0FFF}};
    unsigned long c;
    size_t i;
    if (length == 2) c = (unsigned long)(p[0] & 0x1f) << 6 | (p[1] & 0x3f);
    else if (length == 3) c = (unsigned long)(p[0] & 0x0f) << 12 | (unsigned long)(p[1] & 0x3f) << 6 | (p[2] & 0x3f);
    else if (length == 4) c = (unsigned long)(p[0] & 0x07) << 18 | (unsigned long)(p[1] & 0x3f) << 12 |
                              (unsigned long)(p[2] & 0x3f) << 6 | (p[3] & 0x3f);
    else return 0;
    for (i = 0; i < sizeof(ranges) / sizeof(ranges[0]); ++i)
        if (c >= ranges[i][0] && c <= ranges[i][1]) return 1;
    return 0;
}

/* The code point of the well-formed UTF-8 sequence of length bytes at p. */
static unsigned long console_code_point(const unsigned char *p, size_t length) {
    if (length == 2) return (unsigned long)(p[0] & 0x1f) << 6 | (p[1] & 0x3f);
    if (length == 3) return (unsigned long)(p[0] & 0x0f) << 12 | (unsigned long)(p[1] & 0x3f) << 6 | (p[2] & 0x3f);
    if (length == 4) return (unsigned long)(p[0] & 0x07) << 18 | (unsigned long)(p[1] & 0x3f) << 12 |
                            (unsigned long)(p[2] & 0x3f) << 6 | (p[3] & 0x3f);
    return p[0];
}

/* Whether the well-formed UTF-8 sequence of length bytes at p is a variation
 * selector (U+FE00-U+FE0F, U+E0100-U+E01EF, the Mongolian U+180B-U+180D and
 * U+180F) that draws nothing after previous, the code point before it (0 for
 * none), and so could hide a difference. One stays only after a base it
 * applies to: U+FE0E and U+FE0F after an emoji or a symbol with an emoji
 * form, or between a keycap's 0-9, # or * and U+20E3; U+FE00-U+FE02 and
 * U+E0100-U+E01EF after a CJK ideograph; the Mongolian ones after a
 * Mongolian letter. */
static int console_hidden_selector(unsigned long previous, const unsigned char *p, size_t length) {
    static const unsigned long emoji[][2] = {
        {0x00A9, 0x00A9}, {0x00AE, 0x00AE}, {0x203C, 0x203C}, {0x2049, 0x2049},
        {0x2122, 0x2122}, {0x2139, 0x2139}, {0x2194, 0x2199}, {0x21A9, 0x21AA},
        {0x231A, 0x231B}, {0x2328, 0x2328}, {0x23CF, 0x23CF}, {0x23E9, 0x23FA},
        {0x24C2, 0x24C2}, {0x25AA, 0x25AB}, {0x25B6, 0x25B6}, {0x25C0, 0x25C0},
        {0x25FB, 0x25FE}, {0x2600, 0x27BF}, {0x2934, 0x2935}, {0x2B05, 0x2B07},
        {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50}, {0x2B55, 0x2B55}, {0x3030, 0x3030},
        {0x303D, 0x303D}, {0x3297, 0x3297}, {0x3299, 0x3299}, {0x1F000, 0x1FAFF}};
    static const unsigned long ideographs[][2] = {
        {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xF900, 0xFAFF}, {0x20000, 0x3FFFF}};
    unsigned long c = length >= 3 ? console_code_point(p, length) : 0;
    size_t i;
    if ((c >= 0x180B && c <= 0x180D) || c == 0x180F) return previous < 0x1820 || previous > 0x18AA;
    if (c == 0xFE0E || c == 0xFE0F) {
        if ((previous >= '0' && previous <= '9') || previous == '#' || previous == '*')
            return p[3] != 0xe2 || p[4] != 0x83 || p[5] != 0xa3;
        for (i = 0; i < sizeof(emoji) / sizeof(emoji[0]); ++i)
            if (previous >= emoji[i][0] && previous <= emoji[i][1]) return 0;
        return 1;
    }
    if ((c >= 0xFE00 && c <= 0xFE02) || (c >= 0xE0100 && c <= 0xE01EF)) {
        for (i = 0; i < sizeof(ideographs) / sizeof(ideographs[0]); ++i)
            if (previous >= ideographs[i][0] && previous <= ideographs[i][1]) return 0;
        return 1;
    }
    return c >= 0xFE03 && c <= 0xFE0D;
}

/* text with what is not printable text as \xNN, in a new string (NULL when
 * out of memory), ending in a newline when asked: control characters (C0,
 * DEL, C1), the format controls and hidden variation selectors above, and
 * bytes of no well-formed UTF-8 sequence, which an archive's member names can
 * carry and which would otherwise drive the terminal or disguise the name. */
static char *console_escape(const char *text, int newline) {
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *p = (const unsigned char *)text;
    size_t size = strlen(text), position = 0;
    char *result = size <= ((size_t)-1 - 2) / 4 ? (char *)malloc(size * 4 + 2) : NULL;
    unsigned long previous = 0;
    if (!result) return NULL;
    while (*p) {
        size_t length = *p < 0x80 ? 1 : *p >= 0xc2 && *p <= 0xdf ? 2 :
            *p >= 0xe0 && *p <= 0xef ? 3 : *p >= 0xf0 && *p <= 0xf4 ? 4 : 0, i;
        for (i = 1; i < length; ++i)
            if ((p[i] & 0xc0) != 0x80) { length = 0; break; }
        if (length > 1 && ((*p == 0xc2 && p[1] < 0xa0) || (*p == 0xe0 && p[1] < 0xa0) ||
            (*p == 0xed && p[1] >= 0xa0) || (*p == 0xf0 && p[1] < 0x90) || (*p == 0xf4 && p[1] >= 0x90)))
            length = 0;
        if (console_format_control(p, length) || console_hidden_selector(previous, p, length)) length = 0;
        if (!length || *p < 0x20 || *p == 0x7f) {
            result[position++] = '\\'; result[position++] = 'x';
            result[position++] = hex[*p >> 4]; result[position++] = hex[*p & 15];
            previous = 0;
            ++p;
            continue;
        }
        previous = console_code_point(p, length);
        memcpy(result + position, p, length);
        position += length; p += length;
    }
    if (newline) result[position++] = '\n';
    result[position] = '\0';
    return result;
}

static bool select_output_format(output_format *current, output_format requested,
                                 const char *option) {
    if (*current != OUTPUT_TEXT) {
        fprintf(stderr, *current == requested ?
                "output format specified more than once: %s\n" :
                "output formats are mutually exclusive: %s\n", option);
        return false;
    }
    *current = requested;
    return true;
}

/* 1 consumed, 0 is not a format option, -1 is duplicate/conflicting. */
static int consume_output_option(const char *argument, output_format *format) {
    output_format requested;
    if (!strcmp(argument, "--json") || !strcmp(argument, "-j")) requested = OUTPUT_JSON;
    else if (!strcmp(argument, "--xml") || !strcmp(argument, "-x")) requested = OUTPUT_XML;
    else return 0;
    return select_output_format(format, requested, argument) ? 1 : -1;
}

/* console_escape first turns invalid UTF-8 and controls into printable ASCII
 * \xNN sequences. These writers then apply the syntax-specific quoting, so
 * arbitrary archive member bytes cannot break a JSON or XML document. */
static xxfc_status_t structured_file_write(void *context, const char *data,
                                           size_t size) {
    FILE *out = (FILE *)context;
    return size == 0U || fwrite(data, 1U, size, out) == size
               ? XXFC_OK
               : XXFC_ERR_IO;
}

static int json_string(FILE *out, const char *text) {
    char *safe = console_escape(text ? text : "", 0);
    xx_terminal_writer writer;
    xxfc_status_t status;
    if (!safe) return -1;
    xx_terminal_writer_init(&writer, structured_file_write, out);
    status = xx_terminal_writer_json_string(&writer, safe);
    free(safe);
    return status == XXFC_OK ? 0 : -1;
}

static int xml_string(FILE *out, const char *text) {
    char *safe = console_escape(text ? text : "", 0);
    xx_terminal_writer writer;
    xxfc_status_t status;
    if (!safe) return -1;
    xx_terminal_writer_init(&writer, structured_file_write, out);
    status = xx_terminal_writer_xml_attribute(&writer, safe);
    free(safe);
    return status == XXFC_OK ? 0 : -1;
}

static const char *command_name(xfu_command command) {
    switch (command) {
        case XFU_COMMAND_EXTRACT: return "extract";
        case XFU_COMMAND_LIST: return "list";
        case XFU_COMMAND_TEST: return "test";
        case XFU_COMMAND_ADD: return "add";
        default: return "information";
    }
}

static void structured_begin(const xx_archive_console_config *config,
                             structured_output *output, const char *command,
                             const char *archive) {
    output->first_event = true;
    output->output_failed = false;
    if (output->format == OUTPUT_JSON) {
        if (fputs("{\"tool\":", stdout) == EOF ||
            json_string(stdout, config->application_name) < 0 ||
            fputs(",\"version\":", stdout) == EOF ||
            json_string(stdout, config->application_version) < 0 ||
            fputs(",\"command\":", stdout) == EOF ||
            json_string(stdout, command) < 0 ||
            fputs(",\"archive\":", stdout) == EOF ||
            (archive ? json_string(stdout, archive) : fputs("null", stdout)) < 0 ||
            fputs(",\"events\":[", stdout) == EOF) output->output_failed = true;
    } else {
        if (fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<xx_xfuc tool=\"", stdout) == EOF ||
            xml_string(stdout, config->application_name) < 0 ||
            fputs("\" version=\"", stdout) == EOF ||
            xml_string(stdout, config->application_version) < 0 ||
            fputs("\" command=\"", stdout) == EOF ||
            xml_string(stdout, command) < 0 ||
            fputs("\"", stdout) == EOF) output->output_failed = true;
        if (archive && !output->output_failed &&
            (fputs(" archive=\"", stdout) == EOF || xml_string(stdout, archive) < 0 ||
             fputc('"', stdout) == EOF)) output->output_failed = true;
        if (!output->output_failed && fputs(">\n  <events>\n", stdout) == EOF)
            output->output_failed = true;
    }
}

static int structured_event_prefix(structured_output *output) {
    if (output->output_failed) return -1;
    if (output->format == OUTPUT_JSON) {
        if ((!output->first_event && fputc(',', stdout) == EOF) ||
            fputc('{', stdout) == EOF) {
            output->output_failed = true;
            return -1;
        }
    }
    output->first_event = false;
    return 0;
}

static void structured_message(structured_output *output, bool error,
                               const char *line) {
    if (structured_event_prefix(output) < 0) return;
    if (output->format == OUTPUT_JSON) {
        if (fputs("\"type\":\"message\",\"level\":", stdout) == EOF ||
            json_string(stdout, error ? "error" : "info") < 0 ||
            fputs(",\"text\":", stdout) == EOF || json_string(stdout, line) < 0 ||
            fputc('}', stdout) == EOF) output->output_failed = true;
    } else if (fputs("    <message level=\"", stdout) == EOF ||
               xml_string(stdout, error ? "error" : "info") < 0 ||
               fputs("\">", stdout) == EOF || xml_string(stdout, line) < 0 ||
               fputs("</message>\n", stdout) == EOF) output->output_failed = true;
}

static const char *encryption_status_name(xfu_encryption_status status) {
    switch (status) {
        case XFU_ENCRYPTION_DETECTED: return "detected";
        case XFU_ENCRYPTION_PASSWORD_REQUIRED: return "password_required";
        case XFU_ENCRYPTION_PASSWORD_REJECTED: return "password_rejected";
        default: return NULL;
    }
}

static const char *encryption_message(xfu_encryption_status status) {
    switch (status) {
        case XFU_ENCRYPTION_DETECTED:
            return "Encrypted archive or member detected. Testing or extraction may require -p<password> or --password-env=<name>.";
        case XFU_ENCRYPTION_PASSWORD_REQUIRED:
            return "Password required for encrypted data. Supply -p<password> or --password-env=<name>.";
        case XFU_ENCRYPTION_PASSWORD_REJECTED:
            return "Encrypted data could not be opened with the supplied password. Check the password or archive integrity; use -p<password> or --password-env=<name>.";
        default: return NULL;
    }
}

/* Detection may be reported for both the archive and every encrypted member.
 * One notice per status keeps large solid archives and scripts readable. The
 * per-entry encrypted field preserves the complete member information. */
static bool encryption_first(encryption_notice *notice, xfu_encryption_status status) {
    unsigned bit;
    if (!encryption_status_name(status)) return false;
    bit = 1U << (unsigned)status;
    if (notice->seen & bit) return false;
    notice->seen |= bit;
    return true;
}

static void structured_encryption(void *user, xfu_encryption_status status,
                                  const char *member_name) {
    structured_output *output = (structured_output *)user;
    if (!encryption_first(&output->encryption, status) || structured_event_prefix(output) < 0) return;
    if (output->format == OUTPUT_JSON) {
        if (fputs("\"type\":\"encryption\",\"encrypted\":true,\"status\":", stdout) == EOF ||
            json_string(stdout, encryption_status_name(status)) < 0 ||
            fputs(",\"member\":", stdout) == EOF ||
            (member_name ? json_string(stdout, member_name) : fputs("null", stdout)) < 0 ||
            fputs(",\"text\":", stdout) == EOF || json_string(stdout, encryption_message(status)) < 0 ||
            fputc('}', stdout) == EOF) output->output_failed = true;
    } else {
        if (fputs("    <encryption encrypted=\"true\" status=\"", stdout) == EOF ||
            xml_string(stdout, encryption_status_name(status)) < 0 || fputc('"', stdout) == EOF ||
            (member_name && (fputs(" member=\"", stdout) == EOF || xml_string(stdout, member_name) < 0 || fputc('"', stdout) == EOF)) ||
            fputc('>', stdout) == EOF || xml_string(stdout, encryption_message(status)) < 0 ||
            fputs("</encryption>\n", stdout) == EOF) output->output_failed = true;
    }
}

static void structured_report(structured_output *output, const char *kind,
                              const char *text) {
    if (structured_event_prefix(output) < 0) return;
    if (output->format == OUTPUT_JSON) {
        if (fputs("\"type\":", stdout) == EOF || json_string(stdout, kind) < 0 ||
            fputs(",\"text\":", stdout) == EOF || json_string(stdout, text) < 0 ||
            fputc('}', stdout) == EOF) output->output_failed = true;
    } else if (fputs("    <report type=\"", stdout) == EOF ||
               xml_string(stdout, kind) < 0 || fputs("\">", stdout) == EOF ||
               xml_string(stdout, text) < 0 || fputs("</report>\n", stdout) == EOF)
        output->output_failed = true;
}

static void structured_password(structured_output *output, const char *password,
                                size_t members) {
    if (structured_event_prefix(output) < 0) return;
    if (output->format == OUTPUT_JSON) {
        if (fputs("\"type\":\"password\",\"value\":", stdout) == EOF ||
            json_string(stdout, password) < 0 ||
            fprintf(stdout, ",\"members\":%zu}", members) < 0) output->output_failed = true;
    } else if (fputs("    <password members=\"", stdout) == EOF ||
               fprintf(stdout, "%zu\">", members) < 0 || xml_string(stdout, password) < 0 ||
               fputs("</password>\n", stdout) == EOF) output->output_failed = true;
}

static void structured_entry(structured_output *output, const xfu_entry *entry,
                             int has_result, bool success) {
    size_t i;
    if (structured_event_prefix(output) < 0) return;
    if (output->format == OUTPUT_JSON) {
        if (fputs("\"type\":\"entry\",\"name\":", stdout) == EOF ||
            json_string(stdout, entry->name) < 0 ||
            fprintf(stdout, ",\"directory\":%s,\"encrypted\":%s,\"packed_size\":",
                    entry->is_directory ? "true" : "false", entry->is_encrypted ? "true" : "false") < 0) goto failed;
        if (entry->packed_size < 0 ? fputs("null", stdout) == EOF :
            fprintf(stdout, "%lld", (long long)entry->packed_size) < 0) goto failed;
        if (fputs(",\"unpacked_size\":", stdout) == EOF) goto failed;
        if (entry->unpacked_size < 0 ? fputs("null", stdout) == EOF :
            fprintf(stdout, "%lld", (long long)entry->unpacked_size) < 0) goto failed;
        if (fputs(",\"modified\":", stdout) == EOF ||
            (entry->modified[0] ? json_string(stdout, entry->modified) : fputs("null", stdout)) < 0 ||
            fputs(",\"attributes\":", stdout) == EOF ||
            (entry->attributes[0] ? json_string(stdout, entry->attributes) : fputs("null", stdout)) < 0)
            goto failed;
        if (has_result && (fprintf(stdout, ",\"success\":%s,\"failure\":",
                                  success ? "true" : "false") < 0 ||
            (entry->failure ? json_string(stdout, entry->failure) : fputs("null", stdout)) < 0)) goto failed;
        if (fputs(",\"properties\":[", stdout) == EOF) goto failed;
        for (i = 0; i < entry->property_count; ++i) {
            if ((i && fputc(',', stdout) == EOF) || fputs("{\"name\":", stdout) == EOF ||
                json_string(stdout, entry->properties[i].name) < 0 ||
                fputs(",\"value\":", stdout) == EOF ||
                json_string(stdout, entry->properties[i].value) < 0 ||
                fputc('}', stdout) == EOF) goto failed;
        }
        if (fputs("]}", stdout) == EOF) goto failed;
        return;
    }
    if (fputs("    <entry name=\"", stdout) == EOF || xml_string(stdout, entry->name) < 0 ||
        fprintf(stdout, "\" directory=\"%s\" encrypted=\"%s\"", entry->is_directory ? "true" : "false",
                entry->is_encrypted ? "true" : "false") < 0)
        goto failed;
    if (entry->packed_size >= 0 && fprintf(stdout, " packed_size=\"%lld\"", (long long)entry->packed_size) < 0)
        goto failed;
    if (entry->unpacked_size >= 0 && fprintf(stdout, " unpacked_size=\"%lld\"", (long long)entry->unpacked_size) < 0)
        goto failed;
    if (has_result && fprintf(stdout, " success=\"%s\"", success ? "true" : "false") < 0) goto failed;
    if (fputs(">\n", stdout) == EOF) goto failed;
    if (entry->modified[0] && (fputs("      <modified>", stdout) == EOF ||
        xml_string(stdout, entry->modified) < 0 || fputs("</modified>\n", stdout) == EOF)) goto failed;
    if (entry->attributes[0] && (fputs("      <attributes>", stdout) == EOF ||
        xml_string(stdout, entry->attributes) < 0 || fputs("</attributes>\n", stdout) == EOF)) goto failed;
    if (entry->failure && (fputs("      <failure>", stdout) == EOF ||
        xml_string(stdout, entry->failure) < 0 || fputs("</failure>\n", stdout) == EOF)) goto failed;
    for (i = 0; i < entry->property_count; ++i) {
        if (fputs("      <property name=\"", stdout) == EOF ||
            xml_string(stdout, entry->properties[i].name) < 0 ||
            fputs("\">", stdout) == EOF || xml_string(stdout, entry->properties[i].value) < 0 ||
            fputs("</property>\n", stdout) == EOF) goto failed;
    }
    if (fputs("    </entry>\n", stdout) == EOF) goto failed;
    return;
failed:
    output->output_failed = true;
}

static void structured_log(void *user, bool error, const char *line) {
    structured_message((structured_output *)user, error, line);
}

static void structured_list_entry(void *user, const xfu_entry *entry) {
    structured_entry((structured_output *)user, entry, 0, false);
}

static void structured_test_result(void *user, const xfu_entry *entry, bool success) {
    structured_entry((structured_output *)user, entry, 1, success);
}

static int structured_end(structured_output *output, int status) {
    if (!output->output_failed) {
        if (output->format == OUTPUT_JSON) {
            if (fprintf(stdout, "],\"status\":%d}\n", status) < 0) output->output_failed = true;
        } else if (fprintf(stdout, "  </events>\n  <status>%d</status>\n</xx_xfuc>\n", status) < 0)
            output->output_failed = true;
    }
    if (fflush(stdout) == EOF || ferror(stdout)) output->output_failed = true;
    return output->output_failed ? 2 : status;
}

/* One line of text, escaped. A stderr line goes out in one write (fprintf
 * to unbuffered stderr writes in small pieces, so lines of processes sharing
 * it would mix), after stdout's pending lines (2>&1 must keep the order). */
static int console_line(FILE *out, const char *text) {
    char *escaped = console_escape(text, 1);
    int status = -1;
    if (out == stderr) fflush(stdout);
    if (escaped) status = fputs(escaped, out) == EOF || (out == stderr && fflush(out) == EOF) ? -1 : 0;
    free(escaped);
    return status;
}

/* message, then an argument escaped like a name (it can be a file name),
 * then after, as one stderr write like console_line's. */
static void console_error_argument(const char *message, const char *argument, const char *after) {
    char *escaped = console_escape(argument, 0);
    size_t size = strlen(message) + (escaped ? strlen(escaped) : 1) + strlen(after) + 2;
    char *line = (char *)malloc(size);
    fflush(stdout);
    if (line) {
        snprintf(line, size, "%s%s\n%s", message, escaped ? escaped : "?", after);
        fputs(line, stderr);
        fflush(stderr);
    }
    free(line);
    free(escaped);
}

static void console_encryption(void *user, xfu_encryption_status status,
                               const char *member_name) {
    encryption_notice *notice = (encryption_notice *)user;
    (void)member_name;
    if (encryption_first(notice, status) &&
        (console_line(stderr, encryption_message(status)) < 0 || fflush(stderr) == EOF))
        notice->output_failed = true;
}

static void test_output_failed(console_test *test) {
    test->output_failed = true;
    xx_pd_stop(&test->progress);
}

static void test_finish_line(console_test *test) {
    if (test->line_open) {
        test->line_open = false;
        if (fputc('\n', stdout) == EOF || fflush(stdout) == EOF)
            test_output_failed(test);
    }
}

static void test_encryption(void *user, xfu_encryption_status status,
                            const char *member_name) {
    console_test *test = (console_test *)user;
    if (!encryption_status_name(status) || (test->encryption.seen & (1U << (unsigned)status))) return;
    test_finish_line(test);
    console_encryption(&test->encryption, status, member_name);
    if (test->encryption.output_failed) test_output_failed(test);
}

static void test_log(void *user, bool error, const char *line) {
    console_test *test = (console_test *)user;
    if (!error || test->output_failed) return;
    test_finish_line(test);
    if (console_line(stderr, line) < 0 || fflush(stderr) == EOF)
        test_output_failed(test);
}

static void test_progress(void *user, unsigned percent) {
    console_test *test = (console_test *)user;
    if (test->verbose || test->output_failed) return;
    if (percent > 100U) percent = 100U;
    if ((int)percent <= test->last_percent) return;
    test->last_percent = (int)percent;
    if (fprintf(stdout, test->terminal ? "\r%3u%%" : "%u%%\n", percent) < 0 ||
        fflush(stdout) == EOF) {
        test_output_failed(test);
        return;
    }
    test->line_open = test->terminal;
}

static void test_result(void *user, const xfu_entry *entry, bool success) {
    console_test *test = (console_test *)user;
    char *name, *reason = NULL;
    /* Installing this callback also suppresses the core's member log in the
     * percentage-only mode. No member name is printed in that mode. */
    if (!test->verbose || test->output_failed) return;
    name = console_escape(entry->name, 0);
    /* A failure says why when the core can tell, as the log line would. */
    if (!success && entry->failure) reason = console_escape(entry->failure, 0);
    if (!name || (!success && entry->failure && !reason) ||
        (reason ? fprintf(stdout, "%s -- FAILED (%s)%s\n", name, reason, entry->is_encrypted ? " [encrypted]" : "") :
                  fprintf(stdout, "%s -- %s%s\n", name, success ? "OK" : "FAILED", entry->is_encrypted ? " [encrypted]" : "")) < 0 ||
        fflush(stdout) == EOF) test_output_failed(test);
    free(name);
    free(reason);
}

/* Handle names are case-insensitive; punctuation separates words only.
 * Thus TAR_GZ and tar.gz name the same unpack operation. Numeric IDs must
 * belong to the unpack-handle catalog, never the file-inspection enum. */
static int handle_name_equal(const char *left, const char *right) {
    for (;;) {
        unsigned char a, b;
        while (*left && !((*left >= '0' && *left <= '9') || (*left >= 'A' && *left <= 'Z') || (*left >= 'a' && *left <= 'z'))) ++left;
        while (*right && !((*right >= '0' && *right <= '9') || (*right >= 'A' && *right <= 'Z') || (*right >= 'a' && *right <= 'z'))) ++right;
        a = (unsigned char)*left; b = (unsigned char)*right;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 'a' - 'A');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 'a' - 'A');
        if (a != b) return 0;
        if (!a) return 1;
        ++left; ++right;
    }
}

static int parse_unpack_handle(const char *name, xx_handle_type_t *handle) {
    xx_list_t *supported;
    size_t i;
    unsigned long numeric = 0;
    int is_numeric = 0, found = 0;
    char *end;
    if (!name || !*name) return 0;
    if (strlen(name) >= 15) {
        char prefix[16];
        memcpy(prefix, name, 15); prefix[15] = 0;
        if (handle_name_equal(prefix, "XX_HANDLE_TYPE_")) name += 15;
    }
    if (handle_name_equal(name, "auto") || handle_name_equal(name, "automatic")) {
        *handle = XX_HANDLE_TYPE_UNKNOWN; return 1;
    }
    if (handle_name_equal(name, "gzip")) name = "gz";
    else if (handle_name_equal(name, "bzip2")) name = "bz2";
    else if (handle_name_equal(name, "tar.zst")) name = "tar.zstd";
    if (*name >= '0' && *name <= '9') {
        errno = 0; numeric = strtoul(name, &end, 10);
        /* 7ZIP is a name, not a malformed numeric ID. */
        if (!*end) {
            if (errno || numeric > UINT32_MAX) return 0;
            if (!numeric) { *handle = XX_HANDLE_TYPE_UNKNOWN; return 1; }
            is_numeric = 1;
        }
    }
    supported = xx_format_get_supported_unpack_handle_types();
    if (!supported) return 0;
    for (i = 0; i < xx_list_count(supported); ++i) {
        xx_handle_type_t candidate = *(const xx_handle_type_t *)xx_list_at(supported, i);
        if ((is_numeric && numeric == (unsigned long)candidate) ||
            (!is_numeric && handle_name_equal(name, xx_format_handle_type_to_string(candidate)))) {
            *handle = candidate; found = 1; break;
        }
    }
    xx_list_destroy(supported);
    return found;
}

static void usage(const xx_archive_console_config *config, FILE *out, const char *program) {
    /* The program name is an argument like the others: escaped. */
    char *shown = console_escape(program ? program : config->application_name, 0);
    fprintf(out,
            "%s -- unpack %sarchives with xxfclib\n"
            "\n"
            "Usage: %s <command> <archive> [arguments]\n"
            "\n"
            "Commands (7-Zip letters):\n"
            "  x <archive> [-o<dir>]     Extract with full paths\n"
            "  l <archive> [--advanced]  List contents and optional metadata\n"
            "  p <archive>              Retrieve verified embedded passwords\n"
            "  t <archive> [--verbose]  Test in memory; percentage or member results\n"
            "  a <archive> <file>...     Add files to a new archive\n"
            "  i                         Show all supported file types\n"
            "\n"
            "Options:\n"
            "  --json, -j  Write one JSON document to standard output\n"
            "  --xml, -x   Write one XML document to standard output\n"
            "  -o<dir>   Output directory for x (default: the current directory)\n"
            "  --advanced, -slt  Show all available metadata for each listed file\n"
            "  --verbose, -v  Show each member's OK/FAILED result for t\n"
            "  --handle <name|id>  Select an unpack handle for this archive format\n"
            "  --handle auto  Detect an unpack handle automatically (default)\n"
            "  --reader <name>  Select a validated reader for this archive format\n"
            "  --reader native[:format]  Select a native archive reader\n"
            "  --get-password <archive>  Retrieve embedded passwords (same as p)\n"
            "  l <archive> --get-password  Retrieve instead of listing; conflicts with --advanced\n"
            "  -p<password>  Password where supported (empty password: -p)\n"
            "  --password-env=<name>  Read the password from an environment variable\n"
            "  --format=<name>  Creation format or variant (see --capabilities)\n"
            "  --filter=<name>  Outer creation filter (see --capabilities)\n"
            "  --options=<text>  Format-specific reader or writer options\n"
            "  --compression=<method>  Writer-specific compression method\n"
            "  --compression-level=<n>  Writer-specific compression level\n"
            "  --volume-size=<bytes>  Split writer only; binary K/M/G suffixes accepted\n"
            "  --capabilities  List compiled read/create format and filter support\n"
            "  --formats List all supported file types (same as i; no archive needed)\n"
            "  --raw-profiles  List raw-image geometries (none for format-specific tools)\n"
            "  --version  Show the program version\n"
            "  --color=auto|always|never  Colors for the file type list (default: auto)\n"
            "\n"
            "Notes:\n",
            config->application_name, profile_description(config), shown ? shown : config->application_name);
    if (config->format == XX_ARCHIVE_CONSOLE_ZIP)
        fputs("  Reads and creates ZIP archives. Creation defaults to ZIP regardless\n"
              "  of the destination extension. The outer filter must be none.\n", out);
    else if (config->format == XX_ARCHIVE_CONSOLE_TAR)
        fputs("  Reads and creates TAR archives, including compressed TAR. Creation\n"
              "  defaults to TAR; .tar.gz/.tgz, .tar.bz2 and other compressed\n"
              "  suffixes select the outer filter. --filter overrides that choice.\n"
              "  --format selects a TAR variant, such as pax, ustar or gnutar.\n", out);
    else if (config->format == XX_ARCHIVE_CONSOLE_AR)
        fputs("  Reads and creates uncompressed AR archives. Creation defaults to\n"
              "  BSD AR regardless of the destination extension. --format=ar_svr4\n"
              "  or ar_gnu selects GNU/SVR4 long names; ar_bsd selects BSD names.\n"
              "  Add files individually; AR does not store directory members.\n", out);
    else if (config->format == XX_ARCHIVE_CONSOLE_ALL)
        fputs("  Creation follows the destination extension unless --format overrides it.\n"
              "  See --capabilities for available containers and filters.\n", out);
    else if (!profile_of(config)->create)
        fprintf(out, "  Reads %sarchives. This tool has no %swriter: `a` is unavailable.\n",
                profile_description(config), profile_description(config));
    else if (profile_stream(profile_of(config)))
        fprintf(out, "  Reads and creates %sstreams. A stream holds one file: `a` takes\n"
                     "  exactly one input file and writes --format=raw --filter=%s.\n",
                profile_description(config), profile_of(config)->filter);
    else
        fprintf(out, "  Reads and creates %sarchives. Creation defaults to --format=%s\n"
                     "  regardless of the destination extension. The outer filter must be none.\n",
                profile_description(config), profile_of(config)->create);
    fputs("  `a` creates an archive; it does not append to an existing archive.\n"
          "  Use -- before input filenames that begin with a dash.\n", out);
    free(shown);
}

static bool parse_volume_size(const char *text, uint64_t *size) {
    char *end;
    unsigned long long value;
    unsigned shift = 0;
    if (!text || text[0] < '0' || text[0] > '9') return false;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno == ERANGE || !value) return false;
    if (*end) {
        switch (*end++) {
            case 'k': case 'K': shift = 10; break;
            case 'm': case 'M': shift = 20; break;
            case 'g': case 'G': shift = 30; break;
            default: return false;
        }
        if (*end == 'i' || *end == 'I') ++end;
        if (*end == 'b' || *end == 'B') ++end;
        if (*end) return false;
    }
    if (value > (UINT64_MAX >> shift)) return false;
    *size = (uint64_t)value << shift;
    return true;
}

/* An archive written to standard output itself ('xfu a /dev/stdout ...', or
 * any name for the same file) keeps that stream for its own bytes: the member
 * lines and the summary then go to standard error. */
static bool console_log_to_stderr;

static bool console_names_stdout(const char *path) {
#ifdef _WIN32
    (void)path; /* No path names standard output there. */
    return false;
#else
    struct stat named, out;
    return path && stat(path, &named) == 0 && fstat(STDOUT_FILENO, &out) == 0 &&
           named.st_dev == out.st_dev && named.st_ino == out.st_ino;
#endif
}

static void console_log(void *user, bool error, const char *line) {
    (void)user;
    console_line(error || console_log_to_stderr ? stderr : stdout, line);
}

static void console_advanced_entry(void *user, const xfu_entry *entry) {
    size_t i;
    (void)user;
    fprintf(stdout, "    Type: %s\n", entry->is_directory ? "Directory" : "File");
    fprintf(stdout, "    Encrypted: %s\n", entry->is_encrypted ? "yes" : "no");
    if (entry->packed_size >= 0) fprintf(stdout, "    Packed size: %lld\n", (long long)entry->packed_size);
    else fputs("    Packed size: unknown\n", stdout);
    if (entry->unpacked_size >= 0) fprintf(stdout, "    Unpacked size: %lld\n", (long long)entry->unpacked_size);
    else fputs("    Unpacked size: unknown\n", stdout);
    fputs("    Modified: ", stdout);
    console_line(stdout, entry->modified[0] ? entry->modified : "unknown");
    fputs("    Attributes: ", stdout);
    console_line(stdout, entry->attributes[0] ? entry->attributes : "unknown");
    for (i = 0; i < entry->property_count; ++i) {
        char *name = console_escape(entry->properties[i].name, 0), *value = console_escape(entry->properties[i].value, 0);
        if (name && value) fprintf(stdout, "    %s: %s\n", name, value);
        free(name); free(value);
    }
}

typedef struct password_group {
    char *raw;
    char *display;
    size_t members;
} password_group;

typedef struct password_retrieval {
    password_group *groups;
    size_t count, capacity;
    int failed;
    xx_pd_struct progress;
    structured_output *output;
    encryption_notice encryption;
} password_retrieval;

static void retrieval_encryption(void *user, xfu_encryption_status status,
                                 const char *member_name) {
    password_retrieval *retrieval = (password_retrieval *)user;
    if (retrieval->output) structured_encryption(retrieval->output, status, member_name);
    else console_encryption(&retrieval->encryption, status, member_name);
}

static void wipe_password(char *password) {
    if (password) {
        volatile char *byte = password;
        size_t size = strlen(password) + 1U;
        while (size--) *byte++ = 0;
        free(password);
    }
}

static void retrieval_cleanup(password_retrieval *retrieval) {
    size_t i;
    for (i = 0; i < retrieval->count; ++i) {
        wipe_password(retrieval->groups[i].raw);
        wipe_password(retrieval->groups[i].display);
    }
    free(retrieval->groups);
    memset(retrieval, 0, sizeof(*retrieval));
}

/* Password metadata already escapes ASCII controls and literal backslashes.
 * Preserve that reversible text, while escaping any remaining invalid UTF-8
 * bytes or Unicode C1 controls so the summary is safe single-line output. */
static char *retrieval_display(const char *property) {
    static const char digits[] = "0123456789ABCDEF";
    size_t size = strlen(property), i = 0, used = 0;
    char *text;
    if (size > (SIZE_MAX - 1U) / 4U) return NULL;
    text = (char *)malloc(size * 4U + 1U);
    if (!text) return NULL;
    while (i < size) {
        unsigned char c = (unsigned char)property[i];
        size_t length = 0, j;
        if (c >= 0x20U && c < 0x7fU) length = 1;
        else if (c >= 0xc2U && c <= 0xdfU) length = 2;
        else if (c >= 0xe0U && c <= 0xefU) length = 3;
        else if (c >= 0xf0U && c <= 0xf4U) length = 4;
        if (length > size - i) length = 0;
        for (j = 1; j < length; ++j)
            if (((unsigned char)property[i + j] & 0xc0U) != 0x80U) {
                length = 0; break;
            }
        if (length > 1) {
            unsigned char second = (unsigned char)property[i + 1];
            if ((c == 0xc2U && second < 0xa0U) ||
                (c == 0xe0U && second < 0xa0U) ||
                (c == 0xedU && second >= 0xa0U) ||
                (c == 0xf0U && second < 0x90U) ||
                (c == 0xf4U && second >= 0x90U)) length = 0;
        }
        if (length) {
            memcpy(text + used, property + i, length);
            used += length; i += length;
        } else {
            text[used++] = '\\'; text[used++] = 'x';
            text[used++] = digits[c >> 4]; text[used++] = digits[c & 15U];
            ++i;
        }
    }
    text[used] = 0;
    return text;
}

static void retrieval_fail(password_retrieval *retrieval, int reason) {
    retrieval->failed = reason;
    xx_pd_stop(&retrieval->progress);
}

static void retrieval_log(void *user, bool error, const char *line) {
    password_retrieval *retrieval = (password_retrieval *)user;
    if (!error) return;
    if (retrieval->output) {
        structured_message(retrieval->output, true, line);
        if (retrieval->output->output_failed) retrieval_fail(retrieval, 2);
    } else if (console_line(stderr, line) < 0) retrieval_fail(retrieval, 2);
}

static void retrieval_entry(void *user, const xfu_entry *entry) {
    password_retrieval *retrieval = (password_retrieval *)user;
    const char *property = NULL;
    char *raw, *display;
    size_t i, size;
    if (retrieval->failed || !entry->embedded_password) return;
    for (i = 0; i < retrieval->count; ++i) {
        if (!strcmp(retrieval->groups[i].raw, entry->embedded_password)) {
            if (retrieval->groups[i].members == SIZE_MAX) retrieval_fail(retrieval, 1);
            else ++retrieval->groups[i].members;
            return;
        }
    }
    for (i = 0; i < entry->property_count; ++i)
        if (!strcmp(entry->properties[i].name, "Password")) {
            property = entry->properties[i].value;
            break;
        }
    if (!property) { retrieval_fail(retrieval, 3); return; }
    size = strlen(entry->embedded_password);
    if (size == SIZE_MAX) { retrieval_fail(retrieval, 1); return; }
    raw = (char *)malloc(size + 1U);
    if (raw) memcpy(raw, entry->embedded_password, size + 1U);
    display = retrieval_display(property);
    if (!raw || !display) {
        wipe_password(raw); wipe_password(display); retrieval_fail(retrieval, 1); return;
    }
    if (retrieval->count == retrieval->capacity) {
        size_t capacity = retrieval->capacity ? retrieval->capacity * 2U : 4U;
        password_group *grown;
        if (capacity < retrieval->capacity || capacity > SIZE_MAX / sizeof(*grown)) {
            wipe_password(raw); wipe_password(display); retrieval_fail(retrieval, 1); return;
        }
        grown = (password_group *)realloc(retrieval->groups, capacity * sizeof(*grown));
        if (!grown) {
            wipe_password(raw); wipe_password(display); retrieval_fail(retrieval, 1); return;
        }
        retrieval->groups = grown;
        retrieval->capacity = capacity;
    }
    retrieval->groups[retrieval->count].raw = raw;
    retrieval->groups[retrieval->count].display = display;
    retrieval->groups[retrieval->count++].members = 1;
}

static int retrieval_result(password_retrieval *retrieval, int status) {
    size_t i;
    if (!retrieval->output && (fflush(stderr) == EOF || ferror(stderr))) return 2;
    if (retrieval->failed) {
        const char *message = retrieval->failed == 1 ?
            "cannot allocate recovered password summary" : retrieval->failed == 2 ?
            "cannot write password diagnostics" : "recovered password display is unavailable";
        if (retrieval->output) structured_message(retrieval->output, true, message);
        else fprintf(stderr, "%s\n", message);
        return 2;
    }
    if (status == 2) return 2;
    if (!retrieval->count) {
        if (retrieval->output) {
            structured_report(retrieval->output, "password", "No embedded password found.");
            return 1;
        }
        if (fputs("No embedded password found.\n", stdout) == EOF ||
            fflush(stdout) == EOF || ferror(stdout)) return 2;
        return 1;
    }
    if (retrieval->output) {
        for (i = 0; i < retrieval->count; ++i)
            structured_password(retrieval->output, retrieval->groups[i].display,
                                retrieval->groups[i].members);
        return retrieval->output->output_failed ? 2 : 0;
    }
    for (i = 0; i < retrieval->count; ++i) {
        if ((i && fputc('\n', stdout) == EOF) ||
            fprintf(stdout, "Password: %s\nMembers: %zu\n",
                    retrieval->groups[i].display, retrieval->groups[i].members) < 0)
            return 2;
    }
    return fflush(stdout) == EOF || ferror(stdout) ? 2 : 0;
}

static int show_supported_types(const xx_archive_console_config *config,
                                int argc, char **argv, output_format initial_format)
{
    char *text, *rows;
    int i, color = 0, ansi = 0, mode = 0, result;
    output_format format = initial_format;
#ifdef _WIN32
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD original_mode = 0;
    CONSOLE_SCREEN_BUFFER_INFO info = {0};
    int console = GetConsoleMode(output, &original_mode) && GetConsoleScreenBufferInfo(output, &info);
#endif
    for (i = 2; i < argc; ++i) {
        int consumed = consume_output_option(argv[i], &format);
        if (consumed < 0) return 2;
        if (consumed > 0) continue;
        if (!strcmp(argv[i], "--color=auto")) mode = 0;
        else if (!strcmp(argv[i], "--color=always")) mode = 1;
        else if (!strcmp(argv[i], "--color=never")) mode = 2;
        else { console_error_argument("unexpected argument: ", argv[i], ""); return 2; }
    }
    text = profile_supported_types(config);
    if (!text) { fprintf(stderr, "cannot allocate supported file type list\n"); return 2; }
    if (format != OUTPUT_TEXT) {
        structured_output document = {0};
        document.format = format;
        structured_begin(config, &document, "formats", NULL);
        structured_report(&document, "formats", text);
        result = structured_end(&document, 0);
        free(text);
        return result;
    }
    if (mode != 2 && (mode == 1 || !getenv("NO_COLOR"))) {
#ifdef _WIN32
        color = console || mode == 1;
        if (console) ansi = SetConsoleMode(output, original_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
        else ansi = mode == 1;
#else
        color = mode == 1 || isatty(STDOUT_FILENO);
        ansi = color;
#endif
    }
    rows = strstr(text, "   ID  File type\n");
    if (rows) rows += strlen("   ID  File type\n");
    if (color && ansi) fputs("\033[1;36m", stdout);
#ifdef _WIN32
    if (color && !ansi) { fflush(stdout); SetConsoleTextAttribute(output, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY); }
#endif
    if (rows) {
        fwrite(text, 1, (size_t)(rows - text), stdout);
        if (color && ansi) fputs("\033[0;32m", stdout);
#ifdef _WIN32
        if (color && !ansi) { fflush(stdout); SetConsoleTextAttribute(output, FOREGROUND_GREEN | FOREGROUND_INTENSITY); }
#endif
        fputs(rows, stdout);
    } else fputs(text, stdout);
    if (color && ansi) fputs("\033[0m", stdout);
    fflush(stdout);
#ifdef _WIN32
    if (console) {
        if (ansi) SetConsoleMode(output, original_mode);
        else if (color) SetConsoleTextAttribute(output, info.wAttributes);
    }
#endif
    result = ferror(stdout) ? 2 : 0;
    free(text);
    return result;
}

static int show_report(const xx_archive_console_config *config,
                       output_format format, const char *command,
                       const char *kind, const char *text) {
    if (format == OUTPUT_TEXT) {
        if (fputs(text, stdout) == EOF || fflush(stdout) == EOF || ferror(stdout)) return 2;
        return 0;
    }
    {
        structured_output document = {0};
        document.format = format;
        structured_begin(config, &document, command, NULL);
        structured_report(&document, kind, text);
        return structured_end(&document, 0);
    }
}

static int show_raw_profiles(const xx_archive_console_config *config, output_format format) {
#if XFU_FORMAT_SUPPORT_FULL
    size_t index;
#endif
    if (format == OUTPUT_TEXT) {
#if XFU_FORMAT_SUPPORT_FULL
        for (index = 0; config->format == XX_ARCHIVE_CONSOLE_ALL && index < xx_hxc_raw_floppy_profile_count(); ++index) {
            const xx_hxc_raw_profile *profile = xx_hxc_raw_floppy_profile_at(index);
            printf("hxc-raw:%s\t%llu bytes\t%u cylinders\t%u heads\n", profile->name,
                   (unsigned long long)profile->source_size,
                   (unsigned)profile->tracks, (unsigned)profile->sides);
        }
#endif
        return ferror(stdout) ? 2 : 0;
    }
    {
        structured_output document = {0};
        document.format = format;
        structured_begin(config, &document, "raw-profiles", NULL);
#if XFU_FORMAT_SUPPORT_FULL
        for (index = 0; config->format == XX_ARCHIVE_CONSOLE_ALL && index < xx_hxc_raw_floppy_profile_count(); ++index) {
            const xx_hxc_raw_profile *profile = xx_hxc_raw_floppy_profile_at(index);
            if (structured_event_prefix(&document) < 0) break;
            if (format == OUTPUT_JSON) {
                if (fputs("\"type\":\"raw-profile\",\"name\":", stdout) == EOF ||
                    json_string(stdout, profile->name) < 0 ||
                    fprintf(stdout, ",\"source_size\":%llu,"
                                    "\"cylinders\":%u,\"heads\":%u}",
                            (unsigned long long)profile->source_size,
                            (unsigned)profile->tracks, (unsigned)profile->sides) < 0)
                    document.output_failed = true;
            } else if (fputs("    <raw-profile name=\"", stdout) == EOF ||
                       xml_string(stdout, profile->name) < 0 ||
                       fprintf(stdout, "\" source_size=\"%llu\" cylinders=\"%u\" heads=\"%u\"/>\n",
                               (unsigned long long)profile->source_size,
                               (unsigned)profile->tracks, (unsigned)profile->sides) < 0)
                document.output_failed = true;
        }
#endif
        return structured_end(&document, 0);
    }
}

static int run_cli_arguments(const xx_archive_console_config *config,
                             int argc, char **argv, output_format initial_output) {
    xfu_request request = {0};
    const char **files = NULL;
    bool literal_files = false;
    bool get_password = false;
    bool verbose_test = false;
    bool advanced_listing = false;
    bool handle_specified = false;
    console_test test = {0};
    password_retrieval retrieval = {0};
    structured_output document = {0};
    encryption_notice encryption = {0};
    output_format output = initial_output;
    char *environment_password = NULL;
    int i, result;

    if (argc >= 2 && !strcmp(argv[1], "--version")) {
        for (i = 2; i < argc; ++i) {
            int consumed = consume_output_option(argv[i], &output);
            if (consumed < 0) return 2;
            if (!consumed) { fprintf(stderr, "--version takes no arguments except --json or --xml\n"); return 2; }
        }
        if (output == OUTPUT_TEXT) {
            printf("%s %s\n", config->application_name, config->application_version);
            return ferror(stdout) ? 2 : 0;
        }
        document.format = output;
        structured_begin(config, &document, "version", NULL);
        return structured_end(&document, 0);
    }
    if (argc >= 2 && (!strcmp(argv[1], "--formats") || !strcmp(argv[1], "i")))
        return show_supported_types(config, argc, argv, output);
    if (argc >= 2 && !strcmp(argv[1], "--capabilities")) {
        char *capabilities;
        for (i = 2; i < argc; ++i) {
            int consumed = consume_output_option(argv[i], &output);
            if (consumed < 0) return 2;
            if (!consumed) { fprintf(stderr, "--capabilities takes no arguments except --json or --xml\n"); return 2; }
        }
        capabilities = profile_capabilities(config);
        if (!capabilities) { fprintf(stderr, "cannot allocate capability list\n"); return 2; }
        result = show_report(config, output, "capabilities", "capabilities", capabilities);
        free(capabilities);
        return result;
    }
    if (argc >= 2 && !strcmp(argv[1], "--raw-profiles")) {
        for (i = 2; i < argc; ++i) {
            int consumed = consume_output_option(argv[i], &output);
            if (consumed < 0) return 2;
            if (!consumed) { fprintf(stderr, "--raw-profiles takes no arguments except --json or --xml\n"); return 2; }
        }
        return show_raw_profiles(config, output);
    }

    if (argc == 1 || (argc == 2 &&
        (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))) {
        if (output != OUTPUT_TEXT)
            return show_report(config, output, "help", "usage",
                               "<command> <archive> [arguments]; use --help for text details");
        usage(config, stdout, argv[0]);
        return 0;
    }
    if (argc < 3) {
        usage(config, stderr, argv[0]);
        return 2;
    }
    get_password = !strcmp(argv[1], "--get-password") ||
                   !strcmp(argv[1], "p") || !strcmp(argv[1], "P");
    request.command = get_password ? XFU_COMMAND_LIST : xfu_parse_command(argv[1]);
    if (request.command == XFU_COMMAND_NONE) {
        console_error_argument("unknown command: ", argv[1], "\n");
        usage(config, stderr, argv[0]);
        return 2;
    }
    request.archive_path = argv[2];
    request.callbacks.log = console_log;
    request.callbacks.user = &encryption;
    request.callbacks.encryption = console_encryption;
    if (request.command == XFU_COMMAND_ADD) {
        console_log_to_stderr = console_names_stdout(request.archive_path);
        files = (const char **)calloc((size_t)argc, sizeof(*files));
        if (!files) {
            fprintf(stderr, "cannot allocate file arguments\n");
            return 2;
        }
        request.files = files;
    }
    for (i = 3; i < argc; ++i) {
        int output_option = !literal_files ? consume_output_option(argv[i], &output) : 0;
        if (output_option < 0) { result = 2; goto cleanup; }
        if (output_option > 0) continue;
        if (!literal_files && (strncmp(argv[i], "-p", 2) == 0 ||
                   strncmp(argv[i], "--password-env=", 15) == 0)) {
            if (request.password) {
                fprintf(stderr, "password specified more than once\n");
                result = 2;
                goto cleanup;
            }
            if (strncmp(argv[i], "-p", 2) == 0) request.password = argv[i] + 2;
            else {
                const char *name = argv[i] + 15;
                if (!*name) { fprintf(stderr, "name a password environment variable\n"); result = 2; goto cleanup; }
#ifdef _WIN32
                wchar_t *wide_name = xx_str_utf8_to_unicode(name);
                wchar_t *wide_value;
                char *utf8_value;
                DWORD needed, copied;
                if (!wide_name) { fprintf(stderr, "cannot allocate environment variable name\n"); result = 2; goto cleanup; }
                SetLastError(ERROR_SUCCESS);
                needed = GetEnvironmentVariableW(wide_name, NULL, 0);
                if (!needed && GetLastError() != ERROR_SUCCESS) {
                    xx_str_wfree(wide_name);
                    fprintf(stderr, "password environment variable is not set\n");
                    result = 2; goto cleanup;
                }
                wide_value = (wchar_t *)calloc(needed ? needed : 1, sizeof(wchar_t));
                SetLastError(ERROR_SUCCESS);
                copied = wide_value && needed ? GetEnvironmentVariableW(wide_name, wide_value, needed) : 0;
                if (!wide_value || copied >= (needed ? needed : 1) ||
                    (!copied && GetLastError() != ERROR_SUCCESS)) {
                    xx_str_wfree(wide_name); free(wide_value);
                    fprintf(stderr, "cannot read password environment variable\n");
                    result = 2; goto cleanup;
                }
                xx_str_wfree(wide_name);
                utf8_value = xx_str_unicode_to_utf8(wide_value);
                SecureZeroMemory(wide_value, (needed ? needed : 1) * sizeof(wchar_t));
                free(wide_value);
                if (utf8_value) {
                    size_t length = strlen(utf8_value);
                    environment_password = (char *)malloc(length + 1);
                    if (environment_password) memcpy(environment_password, utf8_value, length + 1);
                    SecureZeroMemory(utf8_value, length);
                    xx_str_free(utf8_value);
                }
#else
                const char *value = getenv(name);
                if (!value) { fprintf(stderr, "password environment variable is not set\n"); result = 2; goto cleanup; }
                environment_password = (char *)malloc(strlen(value) + 1);
                if (environment_password) strcpy(environment_password, value);
#endif
                if (!environment_password) { fprintf(stderr, "cannot allocate password\n"); result = 2; goto cleanup; }
                request.password = environment_password;
            }

        } else if (!literal_files && (!strcmp(argv[i], "--handle") || !strncmp(argv[i], "--handle=", 9))) {
            const char *name = argv[i][8] == '=' ? argv[i] + 9 : (++i < argc ? argv[i] : NULL);
            if (request.command == XFU_COMMAND_ADD || handle_specified || request.reader_name ||
                !parse_unpack_handle(name, &request.handle_type)) {
                fprintf(stderr, "--handle: select one supported unpack handle for l, t, p or x; cannot combine with --reader\n");
                result = 2; goto cleanup;
            }
            handle_specified = true;
        } else if (request.command != XFU_COMMAND_ADD &&
                   (!strcmp(argv[i], "--options") || !strncmp(argv[i], "--options=", 10))) {
            const char *value = argv[i][9] == '=' ? argv[i] + 10 : (++i < argc ? argv[i] : NULL);
            if (request.archive_options || !value || !*value || value[0] == '-') {
                fprintf(stderr, "--options: specify one value\n"); result = 2; goto cleanup;
            }
            request.archive_options = value;
        } else if (request.command == XFU_COMMAND_ADD) {
            static const struct { const char *name; size_t length; } selectors[] = {
                { "--format", 8 }, { "--filter", 8 }, { "--options", 9 }
            };
            size_t option;
            bool consumed = false;
            if (!literal_files) for (option = 0; option < sizeof(selectors) / sizeof(selectors[0]); ++option) {
                const char *name = selectors[option].name;
                size_t length = selectors[option].length;
                const char **target = option == 0 ? &request.archive_format :
                                      option == 1 ? &request.archive_filter : &request.archive_options;
                const char *value;
                if (strncmp(argv[i], name, length) || (argv[i][length] && argv[i][length] != '=')) continue;
                value = argv[i][length] == '=' ? argv[i] + length + 1 : (++i < argc ? argv[i] : NULL);
                if (*target || !value || !*value || value[0] == '-') {
                    fprintf(stderr, "%s: specify one value\n", name); result = 2; goto cleanup;
                }
                *target = value;
                consumed = true;
                break;
            }
            if (consumed) continue;
            if (!literal_files && (!strcmp(argv[i], "--volume-size") || !strncmp(argv[i], "--volume-size=", 14))) {
                const char *value = argv[i][13] == '=' ? argv[i] + 14 : (++i < argc ? argv[i] : NULL);
                if (request.volume_size || !parse_volume_size(value, &request.volume_size)) {
                    fprintf(stderr, "--volume-size: specify one positive size in bytes, K, M or G\n");
                    result = 2; goto cleanup;
                }
                continue;
            }
            if (!literal_files && (!strcmp(argv[i], "--compression") || !strncmp(argv[i], "--compression=", 14))) {
                const char *value = argv[i][13] == '=' ? argv[i] + 14 : (++i < argc ? argv[i] : NULL);
                if (request.compression_method || !value || !*value || value[0] == '-') {
                    fprintf(stderr, "--compression: specify one method\n"); result = 2; goto cleanup;
                }
                request.compression_method = value; continue;
            }
            if (!literal_files && (!strcmp(argv[i], "--compression-level") || !strncmp(argv[i], "--compression-level=", 20))) {
                const char *value = argv[i][19] == '=' ? argv[i] + 20 : (++i < argc ? argv[i] : NULL);
                char *end = NULL;
                long level;
                if (request.compression_level_set || !value || !*value ||
                    (value[0] != '-' && (value[0] < '0' || value[0] > '9')) ||
                    (value[0] == '-' && (value[1] < '0' || value[1] > '9'))) {
                    fprintf(stderr, "--compression-level: specify one integer\n"); result = 2; goto cleanup;
                }
                errno = 0;
                level = strtol(value, &end, 10);
                if (errno == ERANGE || !end || *end || level < INT_MIN || level > INT_MAX) {
                    fprintf(stderr, "--compression-level: invalid integer\n"); result = 2; goto cleanup;
                }
                request.compression_level = (int)level; request.compression_level_set = true; continue;
            }
            if (!literal_files && strcmp(argv[i], "--") == 0) {
                literal_files = true;
                continue;
            }
            if (!literal_files && argv[i][0] == '-') {
                console_error_argument("unknown option: ", argv[i], "");
                result = 2;
                goto cleanup;
            }
            files[request.file_count++] = argv[i];
        } else if (request.command == XFU_COMMAND_TEST &&
                   (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v"))) {
            if (verbose_test) {
                fprintf(stderr, "verbose testing specified more than once\n");
                result = 2;
                goto cleanup;
            }
            verbose_test = true;
        } else if (!strcmp(argv[i], "--reader") || !strncmp(argv[i], "--reader=", 9)) {
            const char *name = argv[i][8] == '=' ? argv[i] + 9 : (++i < argc ? argv[i] : NULL);
            if (request.reader_name || handle_specified || !name || !name[0] || name[0] == '-') {
                fprintf(stderr, "--reader: specify one reader name; cannot combine with --handle\n"); result = 2; goto cleanup;
            }
            request.reader_name = name;
        } else if (!strcmp(argv[i], "--get-password")) {
            if (request.command != XFU_COMMAND_LIST || get_password || advanced_listing) {
                fprintf(stderr, "%s\n", request.command != XFU_COMMAND_LIST ?
                        "--get-password requires l or p" : get_password ?
                        "password retrieval specified more than once" :
                        "--get-password cannot be combined with --advanced or -slt");
                result = 2;
                goto cleanup;
            }
            get_password = true;
        } else if (request.command == XFU_COMMAND_LIST &&
                   (!strcmp(argv[i], "--advanced") || !strcmp(argv[i], "-slt"))) {
            if (get_password) {
                fprintf(stderr, "--get-password cannot be combined with --advanced or -slt\n");
                result = 2;
                goto cleanup;
            }
            if (advanced_listing) {
                fprintf(stderr, "advanced listing specified more than once\n");
                result = 2;
                goto cleanup;
            }
            advanced_listing = true;
        } else if (request.command == XFU_COMMAND_EXTRACT &&
                   strncmp(argv[i], "-o", 2) == 0) {
            if (!argv[i][2]) {
                fprintf(stderr, "-o: name an output directory\n");
                result = 2;
                goto cleanup;
            }
            if (request.output_dir) {
                fprintf(stderr, "-o: output directory specified more than once\n");
                result = 2;
                goto cleanup;
            }
            request.output_dir = argv[i] + 2;
        } else {
            console_error_argument(argv[i][0] == '-' ? "unknown option: " : "unexpected argument: ", argv[i], "");
            result = 2;
            goto cleanup;
        }
    }
    if (output != OUTPUT_TEXT && request.command == XFU_COMMAND_ADD &&
        console_names_stdout(request.archive_path)) {
        fprintf(stderr, "--json and --xml cannot be used when the archive is written to standard output\n");
        result = 2;
        goto cleanup;
    }
    if (output != OUTPUT_TEXT) {
        document.format = output;
        structured_begin(config, &document, get_password ? "password" : command_name(request.command),
                         request.archive_path);
        request.callbacks.user = &document;
        request.callbacks.log = structured_log;
        request.callbacks.encryption = structured_encryption;
        if (!get_password && (request.command == XFU_COMMAND_LIST ||
                              request.command == XFU_COMMAND_EXTRACT))
            request.callbacks.entry = structured_list_entry;
        if (!get_password && request.command == XFU_COMMAND_TEST)
            request.callbacks.test_result = structured_test_result;
    } else if (advanced_listing) {
        request.callbacks.entry = console_advanced_entry;
    }
    if (get_password) {
        retrieval.progress = xx_pd_init();
        retrieval.output = output == OUTPUT_TEXT ? NULL : &document;
        request.progress_state = &retrieval.progress;
        request.callbacks.user = &retrieval;
        request.callbacks.log = retrieval_log;
        request.callbacks.entry = retrieval_entry;
        request.callbacks.encryption = retrieval_encryption;
    } else if (request.command == XFU_COMMAND_TEST && output == OUTPUT_TEXT) {
        test.progress = xx_pd_init();
        test.last_percent = -1;
        test.verbose = verbose_test;
        test.terminal = console_is_terminal();
        request.progress_state = &test.progress;
        request.callbacks.user = &test;
        request.callbacks.log = test_log;
        request.callbacks.test_progress = test_progress;
        request.callbacks.test_result = test_result;
        request.callbacks.encryption = test_encryption;
    }
    result = profile_apply(config, &request) ? xfu_run(&request) : 2;
    if (get_password) result = retrieval_result(&retrieval, result);
    else if (request.command == XFU_COMMAND_TEST && output == OUTPUT_TEXT) {
        test_finish_line(&test);
        if (fflush(stdout) == EOF || fflush(stderr) == EOF ||
            ferror(stdout) || ferror(stderr)) test_output_failed(&test);
        if (test.output_failed) result = 2;
    }
    if (output != OUTPUT_TEXT) result = structured_end(&document, result);
    else if (encryption.output_failed || retrieval.encryption.output_failed) result = 2;
cleanup:
    retrieval_cleanup(&retrieval);
    if (environment_password) {
        volatile char *wipe = environment_password;
        size_t size = strlen(environment_password);
        while (size--) *wipe++ = 0;
        free(environment_password);
    }
    free(files);
    return result;
}

static int run_cli(const xx_archive_console_config *config, int argc, char **argv) {
    output_format output = OUTPUT_TEXT;
    int first = 1, destination = 1, result;
    char **arguments;

    /* Accept the selectors as global options before the command too. Keep a
     * separate pointer vector so Windows main still owns and frees the
     * original UTF-8 argv entries exactly once. */
    while (first < argc) {
        int consumed = consume_output_option(argv[first], &output);
        if (consumed < 0) return 2;
        if (!consumed) break;
        ++first;
    }
    if (first == 1) return run_cli_arguments(config, argc, argv, OUTPUT_TEXT);

    arguments = (char **)calloc((size_t)(argc - first + 2), sizeof(*arguments));
    if (!arguments) {
        fprintf(stderr, "cannot allocate command line\n");
        return 2;
    }
    arguments[0] = argv[0];
    while (first < argc) arguments[destination++] = argv[first++];
    result = run_cli_arguments(config, destination, arguments, output);
    free(arguments);
    return result;
}

int xx_archive_console_main(int argc, char **argv,
                            const xx_archive_console_config *config) {
    if (!config || !config->application_name || !config->application_version ||
        config->format < XX_ARCHIVE_CONSOLE_ALL || config->format >= XX_ARCHIVE_CONSOLE_COUNT ||
        argc < 1 || !argv || !argv[0]) return 2;
    if (!profile_compiled(config->format)) return 2;
    console_log_to_stderr = false;
#ifdef SIGXFSZ
    /* A write past the file size limit fails (EFBIG), as a full disk does,
     * instead of ending the process. */
    signal(SIGXFSZ, SIG_IGN);
#endif
#ifdef _WIN32
    wchar_t **wide;
    char **utf8;
    int count = 0, i, result;
    (void)argc;
    (void)argv;
    SetConsoleOutputCP(CP_UTF8);
    wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide) {
        fprintf(stderr, "cannot read command line\n");
        return 2;
    }
    utf8 = (char **)calloc((size_t)count + 1, sizeof(*utf8));
    if (!utf8) {
        LocalFree(wide);
        fprintf(stderr, "cannot allocate command line\n");
        return 2;
    }
    for (i = 0; i < count; ++i) {
        utf8[i] = xx_str_unicode_to_utf8(wide[i]);
        if (!utf8[i]) break;
    }
    LocalFree(wide);
    if (i == count) result = run_cli(config, count, utf8);
    else {
        fprintf(stderr, "cannot convert command line to UTF-8\n");
        result = 2;
    }
    for (i = 0; i < count; ++i) xx_str_free(utf8[i]);
    free(utf8);
    return result;
#else
    return run_cli(config, argc, argv);
#endif
}
