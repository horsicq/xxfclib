/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Small archive writers for wire formats outside xxfclib's generic writer
 * catalog. All output goes to the caller's temporary device.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "native_extra_formats.h"

#include <inttypes.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xxfclib/algo/deflate/xx_deflate.h>
#include <xxfclib/algo/sha/xx_sha.h>

typedef struct extra_source {
    const char *path;
    const xfu_native_member *member; /* As walked: opened with its checks. */
    char *name;
    uint64_t size;
    int64_t mtime;
    unsigned mode;
    unsigned uid;
    unsigned gid;
    bool directory;
} extra_source;

typedef enum extra_kind {
    EXTRA_NONE, EXTRA_AR_BSD, EXTRA_AR_GNU, EXTRA_MTREE,
    EXTRA_MTREE_CLASSIC, EXTRA_SHAR, EXTRA_SHAR_DUMP, EXTRA_WARC,
    EXTRA_CPIO_ODC, EXTRA_CPIO_BIN, EXTRA_CPIO_PWB, EXTRA_XAR,
    EXTRA_ISO9660
} extra_kind;

/* Byte helpers of the ar, cpio, xar and ISO 9660 writers. */
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_ISO9660)
static void extra_put16le(unsigned char *buffer, uint16_t value) {
    buffer[0] = (unsigned char)value;
    buffer[1] = (unsigned char)(value >> 8U);
}

static void extra_put16be(unsigned char *buffer, uint16_t value) {
    buffer[0] = (unsigned char)(value >> 8U);
    buffer[1] = (unsigned char)value;
}

static void extra_put32be(unsigned char *buffer, uint32_t value) {
    buffer[0] = (unsigned char)(value >> 24U);
    buffer[1] = (unsigned char)(value >> 16U);
    buffer[2] = (unsigned char)(value >> 8U);
    buffer[3] = (unsigned char)value;
}
#endif

static extra_kind extra_select(const char *format) {
    if (!format) return EXTRA_NONE;
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_AR)
    if (!strcmp(format, "ar") || !strcmp(format, "ar_bsd") ||
        !strcmp(format, "arbsd")) return EXTRA_AR_BSD;
    if (!strcmp(format, "ar_svr4") || !strcmp(format, "ar_gnu") ||
        !strcmp(format, "argnu") || !strcmp(format, "arsvr4")) return EXTRA_AR_GNU;
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_ISO9660)
    if (!strcmp(format, "iso9660") || !strcmp(format, "iso") ||
        !strcmp(format, "cd9660")) return EXTRA_ISO9660;
#endif
#if XFU_FORMAT_SUPPORT_FULL
    if (!strcmp(format, "mtree")) return EXTRA_MTREE;
    if (!strcmp(format, "mtree_classic") || !strcmp(format, "mtree-classic"))
        return EXTRA_MTREE_CLASSIC;
    if (!strcmp(format, "shar")) return EXTRA_SHAR;
    if (!strcmp(format, "shar_dump") || !strcmp(format, "shardump"))
        return EXTRA_SHAR_DUMP;
    if (!strcmp(format, "warc")) return EXTRA_WARC;
    if (!strcmp(format, "cpio") || !strcmp(format, "cpio_odc") ||
        !strcmp(format, "odc")) return EXTRA_CPIO_ODC;
    if (!strcmp(format, "cpio_bin") || !strcmp(format, "cpio_bin_le") ||
        !strcmp(format, "bin")) return EXTRA_CPIO_BIN;
    if (!strcmp(format, "cpio_pwb") || !strcmp(format, "pwb"))
        return EXTRA_CPIO_PWB;
    if (!strcmp(format, "xar")) return EXTRA_XAR;
#endif
    return EXTRA_NONE;
}

bool xfu_native_extra_can_create(const char *format) {
    return extra_select(format) != EXTRA_NONE;
}

/* ar, shar and WARC records carry file data only: no folder members. */
bool xfu_native_extra_stores_folders(const char *format) {
    extra_kind kind = extra_select(format);
    return kind != EXTRA_NONE && kind != EXTRA_AR_BSD && kind != EXTRA_AR_GNU &&
        kind != EXTRA_SHAR && kind != EXTRA_SHAR_DUMP && kind != EXTRA_WARC;
}

/* XAR's table is XML, WARC names are URIs, and xxfclib reads mtree names as
 * UTF-8: none of them holds other bytes. ar, CPIO and ISO 9660 do. */
bool xfu_native_extra_needs_utf8(const char *format) {
    extra_kind kind = extra_select(format);
    return kind == EXTRA_XAR || kind == EXTRA_WARC || kind == EXTRA_MTREE ||
        kind == EXTRA_MTREE_CLASSIC;
}

/* Whole lines: a long path is formatted again on the heap, never cut. */
static void extra_log(const xfu_request *request, const char *format, ...) {
    char local[512], *buffer = local;
    va_list arguments, copy;
    int count;
    if (!request || !request->callbacks.log) return;
    va_start(arguments, format);
    va_copy(copy, arguments);
    count = vsnprintf(local, sizeof(local), format, arguments);
    va_end(arguments);
    if (count >= (int)sizeof(local)) {
        buffer = (char *)malloc((size_t)count + 1U);
        if (buffer) vsnprintf(buffer, (size_t)count + 1U, format, copy);
        else buffer = local;
    }
    va_end(copy);
    request->callbacks.log(request->callbacks.user, true, buffer);
    if (buffer != local) free(buffer);
}

/* The article for a format name in a message: "an iso9660", "an xar",
 * "an ar", "an mtree", but "a cpio", "a shar". */
static const char *extra_article(const char *format) {
    return format && format[0] && (strchr("aeioAEIO", format[0]) || format[0] == 'x' ||
                                   !strncmp(format, "mtree", 5U)) ? "an" : "a";
}

/* A format's name in messages: the ar variants are all "ar". */
static const char *extra_display(const char *format) {
    extra_kind kind = extra_select(format);
    return kind == EXTRA_AR_BSD || kind == EXTRA_AR_GNU ? "ar" : format;
}

static bool extra_cancelled(const xfu_request *request, xx_pd_struct *pd) {
    return (pd && xx_pd_is_stopped(pd)) ||
        (request->callbacks.cancelled &&
         request->callbacks.cancelled(request->callbacks.user));
}

static bool extra_write(xx_io_device *output, const void *data, size_t length) {
    const unsigned char *bytes = (const unsigned char *)data;
    size_t done = 0;
    while (done < length) {
        ssize_t amount = xx_io_write(output, bytes + done, length - done);
        if (amount <= 0 || (size_t)amount > length - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool extra_text(xx_io_device *output, const char *text) {
    return extra_write(output, text, strlen(text));
}

#if XFU_FORMAT_SUPPORT_FULL
static bool extra_printf(xx_io_device *output, const char *format, ...) {
    va_list arguments, copy;
    char *buffer;
    int length;
    bool result;
    va_start(arguments, format);
    va_copy(copy, arguments);
    length = vsnprintf(NULL, 0, format, arguments);
    va_end(arguments);
    if (length < 0 || (size_t)length > SIZE_MAX - 1U) {
        va_end(copy);
        return false;
    }
    buffer = (char *)malloc((size_t)length + 1U);
    if (!buffer) {
        va_end(copy);
        return false;
    }
    if (vsnprintf(buffer, (size_t)length + 1U, format, copy) != length) {
        free(buffer);
        va_end(copy);
        return false;
    }
    va_end(copy);
    result = extra_write(output, buffer, (size_t)length);
    free(buffer);
    return result;
}
#endif

/* A folder separator: '/', and on Windows also '\\'. Elsewhere '\\' is part
 * of a name, as the file system has it. */
static bool extra_separator(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

/* Preserve relative input paths and reduce absolute/traversing paths to a
 * basename, matching the CLI's established source-name behavior. NULL for a
 * name the records cannot hold, with what is wrong with it in *reason. */
static char *extra_name(const char *path, const char **reason) {
    const char *start = path, *last = path, *at;
    bool basename;
    char *name;
    size_t length, out = 0;
    *reason = "is empty";
    if (!path || !path[0]) return NULL;
    basename = extra_separator(path[0]);
#ifdef _WIN32
    /* A drive ("C:") only on Windows; elsewhere ':' is refused below. */
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':') basename = true;
#endif
    for (at = path; *at; ++at) {
        if (extra_separator(*at)) last = at + 1;
        if (*at == '.' && at[1] == '.' &&
            (at == path || extra_separator(at[-1])) &&
            (!at[2] || extra_separator(at[2]))) basename = true;
    }
    if (basename) start = last;
    length = strlen(start);
    if (length > 32767U) *reason = "is longer than 32767 bytes";
    if (!length || length > 32767U) return NULL;
    name = (char *)malloc(length + 1U);
    if (!name) return NULL;
    for (at = start; *at;) {
        const char *begin = at;
        size_t n, i;
        while (*at && !extra_separator(*at)) ++at;
        n = (size_t)(at - begin);
        while (extra_separator(*at)) ++at;
        if (!n || (n == 1U && begin[0] == '.')) continue;
        if (n == 2U && begin[0] == '.' && begin[1] == '.') {
            *reason = "has a \"..\" component";
            goto fail;
        }
        if (out) name[out++] = '/';
        for (i = 0; i < n; ++i) {
            unsigned char byte = (unsigned char)begin[i];
            if (byte < 32U || byte == 127U || byte == ':') {
                *reason = byte == ':' ? "contains ':'" : "contains a control character";
                goto fail;
            }
            name[out++] = (char)byte;
        }
    }
    name[out] = '\0';
    if (!out) goto fail;
    return name;
fail:
    free(name);
    return NULL;
}

/* The records' fields: the metadata xfu_native_create walked (for a link
 * named as an input, that of the file or folder it names). */
static bool extra_sources(const xfu_request *request, const xfu_native_member *members,
                          const char *format, extra_source **result) {
    extra_source *sources;
    size_t i;
#ifndef _WIN32
    const extra_kind kind = extra_select(format);
#endif
    if (!request || !result || (request->file_count && !members) ||
        request->file_count > SIZE_MAX / sizeof(*sources))
        return false;
    sources = (extra_source *)calloc(request->file_count ? request->file_count : 1U,
                                     sizeof(*sources));
    if (!sources) return false;
    for (i = 0; i < request->file_count; ++i) {
        const xfu_native_member *member = &members[i];
        const char *reason = "";
        sources[i].path = member->path;
        sources[i].member = member;
        sources[i].name = extra_name(member->name, &reason);
#ifndef _WIN32
        /* A '\\' is part of a POSIX name, but xfu's XAR, WARC and mtree
         * readers refuse it, and ISO 9660 (Joliet) names cannot hold it. */
        if (sources[i].name && strchr(sources[i].name, '\\') &&
            (kind == EXTRA_XAR || kind == EXTRA_WARC || kind == EXTRA_MTREE ||
             kind == EXTRA_MTREE_CLASSIC || kind == EXTRA_ISO9660)) {
            free(sources[i].name);
            sources[i].name = NULL;
            reason = "contains '\\'";
        }
#endif
        if (!sources[i].name) {
            /* Control characters, ':', a '..' component or (off Windows) a
             * '\\' that the format cannot hold. */
            extra_log(request, "%s: the name %s, which %s %s archive cannot store",
                      sources[i].path ? sources[i].path : "", reason,
                      extra_article(extra_display(format)), extra_display(format));
            goto fail;
        }
        if (!sources[i].path || member->size < 0) {
            extra_log(request, "%s: regular file/directory metadata unavailable",
                      sources[i].path ? sources[i].path : "");
            goto fail;
        }
        sources[i].directory = member->directory;
        sources[i].size = member->directory ? 0U : (uint64_t)member->size;
        sources[i].mtime = member->mtime;
        sources[i].mode = (unsigned)member->mode & 0777U;
        sources[i].uid = (unsigned)member->uid;
        sources[i].gid = (unsigned)member->gid;
    }
    *result = sources;
    return true;
fail:
    for (i = 0; i < request->file_count; ++i) free(sources[i].name);
    free(sources);
    return false;
}

static void extra_free_sources(extra_source *sources, size_t count) {
    size_t i;
    if (!sources) return;
    for (i = 0; i < count; ++i) free(sources[i].name);
    free(sources);
}

/* A modification time outside what a header holds is stored as the nearest
 * one it does, with one warning line for the member. */
static void extra_clamp_time(const xfu_request *request, extra_source *source,
                             const char *label, int64_t first, int64_t last) {
    int64_t stored = source->mtime < first ? first : source->mtime > last ? last : source->mtime;
    if (stored == source->mtime) return;
    xfu_native_warn_time(request, source->path, label, source->mtime, stored, true);
    source->mtime = stored;
}

static bool extra_copy(const extra_source *source, xx_io_device *output,
                       const xfu_request *request, xx_pd_struct *pd) {
    unsigned char buffer[65536];
    xx_io_device *input;
    uint64_t remaining;
    bool okay = true;
    if (source->directory) return true;
    if (!(input = xfu_native_open_member(source->member))) {
        char why[128];
        extra_log(request, "%s: %s", source->path, xfu_native_open_failure(errno, why, sizeof(why)));
        return false;
    }
    remaining = source->size;
    while (okay && remaining) {
        size_t wanted = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        ssize_t received;
        if (extra_cancelled(request, pd)) { okay = false; break; }
        received = xx_io_read(input, buffer, wanted);
        if (received <= 0 || (size_t)received > wanted) {
            extra_log(request, "%s: source changed or cannot be read", source->path);
            okay = false;
            break;
        }
        if (!extra_write(output, buffer, (size_t)received)) {
            okay = false;
            break;
        }
        remaining -= (uint64_t)received;
    }
    if (xx_io_close(input) != 0) okay = false;
    return okay && !remaining;
}

static bool extra_ar_header(xx_io_device *output, const char *name,
                            const extra_source *source, uint64_t stored_size) {
    char header[61];
    unsigned mode = (source ? source->mode : 0644U) | 0100000U;
    int length = snprintf(header, sizeof(header),
                          "%-16s%-12" PRId64 "%-6u%-6u%-8o%-10" PRIu64 "`\n",
                          name, source ? source->mtime : 0,
                          source ? source->uid : 0U, source ? source->gid : 0U,
                          mode, stored_size);
    return length == 60 && extra_write(output, header, 60U);
}

/* Why an ar header has no room for a member's fields, or NULL. Its time
 * (12 digits) is clamped instead. */
static const char *extra_ar_problem(const extra_source *source) {
    if (source->size > UINT64_C(9999999999) - 32768U)
        return "the file is 10 GB or larger, which an ar header cannot hold";
    if (source->uid > 999999U || source->gid > 999999U)
        return "its owner or group id has more than 6 digits, which an ar header cannot hold";
    return NULL;
}

static bool extra_ar_write(const xfu_request *request, extra_source *sources,
                           extra_kind kind, xx_io_device *output, xx_pd_struct *pd) {
    size_t *table_offsets = NULL;
    char *table = NULL;
    size_t table_size = 0, i;
    bool okay = false;
    /* ar stores a member's file name only, so "a/x" and "b/x" are both "x":
     * as with GNU ar (static libraries hold such objects), both are kept. */
    for (i = 0; i < request->file_count; ++i) {
        const char *problem = extra_ar_problem(&sources[i]);
        if (problem) {
            extra_log(request, "%s: %s", sources[i].path, problem);
            return false;
        }
    }
    for (i = 0; i < request->file_count; ++i)
        extra_clamp_time(request, &sources[i], "ar", -INT64_C(99999999999), INT64_C(999999999999));
    if (!extra_text(output, "!<arch>\n")) return false;
    if (kind == EXTRA_AR_GNU && request->file_count) {
        table_offsets = (size_t *)calloc(request->file_count, sizeof(*table_offsets));
        if (!table_offsets) goto done;
        for (i = 0; i < request->file_count; ++i) {
            const char *name = strrchr(sources[i].name, '/');
            size_t length;
            name = name ? name + 1 : sources[i].name;
            length = strlen(name);
            if (length > 15U) {
                if (length > SIZE_MAX - table_size - 2U) goto done;
                table_offsets[i] = table_size;
                table_size += length + 2U;
            }
        }
        if (table_size) {
            size_t at = 0;
            table = (char *)malloc(table_size);
            if (!table) goto done;
            for (i = 0; i < request->file_count; ++i) {
                const char *name = strrchr(sources[i].name, '/');
                size_t length;
                name = name ? name + 1 : sources[i].name;
                length = strlen(name);
                if (length <= 15U) continue;
                memcpy(table + at, name, length);
                at += length;
                table[at++] = '/';
                table[at++] = '\n';
            }
            if (at != table_size || !extra_ar_header(output, "//", NULL, table_size) ||
                !extra_write(output, table, table_size) ||
                ((table_size & 1U) && !extra_text(output, "\n"))) goto done;
        }
    }
    for (i = 0; i < request->file_count; ++i) {
        const extra_source *source = &sources[i];
        const char *name = strrchr(source->name, '/');
        char field[32];
        size_t length;
        uint64_t stored_size = source->size;
        if (extra_cancelled(request, pd) || source->directory) goto done;
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i,
                                        request->file_count, source->path);
        name = name ? name + 1 : source->name;
        length = strlen(name);
        if (kind == EXTRA_AR_BSD && length > 15U) {
            if (source->size > UINT64_MAX - length) goto done;
            stored_size += length;
            if (snprintf(field, sizeof(field), "#1/%zu", length) < 0 ||
                strlen(field) > 16U) goto done;
        } else if (kind == EXTRA_AR_GNU && length > 15U) {
            if (snprintf(field, sizeof(field), "/%zu", table_offsets[i]) < 0 ||
                strlen(field) > 16U) goto done;
        } else {
            if (length > 15U) goto done;
            if (snprintf(field, sizeof(field), "%s%s", name,
                         kind == EXTRA_AR_GNU ? "/" : "") < 0) goto done;
        }
        if (!extra_ar_header(output, field, source, stored_size) ||
            (kind == EXTRA_AR_BSD && length > 15U &&
             !extra_write(output, name, length)) ||
            !extra_copy(source, output, request, pd) ||
            ((stored_size & 1U) && !extra_text(output, "\n"))) goto done;
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i + 1U,
                                        request->file_count, NULL);
    }
    okay = !extra_cancelled(request, pd);
done:
    free(table_offsets);
    free(table);
    return okay;
}

/* mtree(5) names are printable ASCII: every other byte (spaces, control
 * characters and those of UTF-8 sequences), and '#', '=' and '\\', which the
 * format gives a meaning, are written as a backslash and three octal digits,
 * as libarchive and the BSD mtree do. */
#if XFU_FORMAT_SUPPORT_FULL
static bool extra_mtree_name(xx_io_device *output, const char *name) {
    const unsigned char *at = (const unsigned char *)name;
    if (!extra_text(output, "./")) return false;
    while (*at) {
        if (*at <= ' ' || *at >= 0x7FU || *at == '#' || *at == '=' || *at == '\\') {
            if (!extra_printf(output, "\\%03o", (unsigned)*at)) return false;
        } else if (!extra_write(output, at, 1U)) return false;
        ++at;
    }
    return true;
}

static bool extra_mtree_write(const xfu_request *request, extra_source *sources,
                              extra_kind kind, xx_io_device *output, xx_pd_struct *pd) {
    size_t i;
    if (!extra_text(output, "#mtree\n")) return false;
    if (kind == EXTRA_MTREE_CLASSIC &&
        !extra_text(output, "\n/set type=file uid=0 gid=0 mode=644\n. type=dir\n"))
        return false;
    for (i = 0; i < request->file_count; ++i) {
        const extra_source *source = &sources[i];
        if (extra_cancelled(request, pd)) return false;
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i,
                                        request->file_count, source->path);
        if (!extra_mtree_name(output, source->name) ||
            !extra_printf(output,
                          " type=%s mode=%o uid=%u gid=%u time=%" PRId64 ".0 size=%" PRIu64 "\n",
                          source->directory ? "dir" : "file", source->mode,
                          source->uid, source->gid, source->mtime,
                          source->size)) return false;
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i + 1U,
                                        request->file_count, NULL);
    }
    return !extra_cancelled(request, pd);
}

/* Whether a shell archive cannot hold a name, with what is wrong with it in
 * problem ("contains '*'"). The script quotes every name, so sh would take
 * any; but xxfclib's shar reader, which never runs it, takes a name with a
 * quote, '\' or one of <>|$`;&*? for a misparse and leaves its file out,
 * and reads printable ASCII names of at most 255 bytes only. */
static bool extra_shar_name_problem(const char *name, char *problem, size_t size) {
    const unsigned char *at = (const unsigned char *)name;
    size_t length = strlen(name);
    if (length > 255U) {
        (void)snprintf(problem, size, "is longer than 255 bytes");
        return true;
    }
    for (; *at; ++at) {
        if (*at < 32U || *at == 127U) {
            (void)snprintf(problem, size, "contains a control character");
            return true;
        }
        if (*at > 127U) {
            (void)snprintf(problem, size, "contains a byte outside printable ASCII");
            return true;
        }
        if (strchr("'\"\\<>|$`;&*?", *at)) {
            if (*at == '\'') (void)snprintf(problem, size, "contains \"'\"");
            else (void)snprintf(problem, size, "contains '%c'", *at);
            return true;
        }
    }
    return false;
}

static unsigned char extra_uu_char(unsigned value) {
    return value ? (unsigned char)(value + 32U) : (unsigned char)'`';
}

/* Whether a file's bytes come back unchanged from sed in a here-document:
 * none at all, or UTF-8 text with no NUL that ends in a newline (sed in a
 * UTF-8 locale may refuse other bytes, and a here-document adds a final
 * newline and loses NULs). 1: text; 0: other bytes; -1: unreadable (logged). */
static int extra_shar_text(const extra_source *source, const xfu_request *request,
                           xx_pd_struct *pd) {
    xx_io_device *input = xfu_native_open_member(source->member);
    unsigned char buffer[65536], last = '\n', low = 0x80U, high = 0xBFU;
    uint64_t remaining = source->size;
    unsigned need = 0U;
    bool text = true;
    int result;
    if (!input) {
        char why[128];
        extra_log(request, "%s: %s", source->path, xfu_native_open_failure(errno, why, sizeof(why)));
        return -1;
    }
    while (remaining && text) {
        size_t wanted = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining, at;
        ssize_t got;
        if (extra_cancelled(request, pd)) break;
        got = xx_io_read(input, buffer, wanted);
        if (got <= 0 || (size_t)got > wanted) break;
        for (at = 0; at < (size_t)got && text; ++at) {
            unsigned char c = buffer[at];
            if (need) {
                text = c >= low && c <= high;
                low = 0x80U;
                high = 0xBFU;
                --need;
            } else if (!c || (c >= 0x80U && c < 0xC2U) || c > 0xF4U) {
                text = false;
            } else if (c >= 0xC2U) {
                need = c < 0xE0U ? 1U : c < 0xF0U ? 2U : 3U;
                if (c == 0xE0U) low = 0xA0U;
                else if (c == 0xEDU) high = 0x9FU;
                else if (c == 0xF0U) low = 0x90U;
                else if (c == 0xF4U) high = 0x8FU;
            }
        }
        last = buffer[(size_t)got - 1U];
        remaining -= (uint64_t)got;
    }
    result = remaining && text ? -1 : text && !need && last == '\n' ? 1 : 0;
    if (xx_io_close(input) != 0) result = -1;
    if (result < 0 && !extra_cancelled(request, pd))
        extra_log(request, "%s: source changed or cannot be read", source->path);
    return result;
}

/* The data in 45-byte uuencode lines, read in large blocks (the member is an
 * unbuffered descriptor): a block is filled before it is encoded, so only
 * the last line is shorter. */
static bool extra_shar_dump_data(const extra_source *source, xx_io_device *output,
                                  const xfu_request *request, xx_pd_struct *pd) {
    xx_io_device *input = xfu_native_open_member(source->member);
    unsigned char buffer[45U * 1456U], line[62];
    uint64_t remaining = source->size;
    bool okay = input != NULL;
    if (!okay) {
        char why[128];
        extra_log(request, "%s: %s", source->path, xfu_native_open_failure(errno, why, sizeof(why)));
        return false;
    }
    while (okay && remaining) {
        size_t wanted = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        size_t have = 0, start;
        while (okay && have < wanted) {
            ssize_t got;
            if (extra_cancelled(request, pd)) { okay = false; break; }
            got = xx_io_read(input, buffer + have, wanted - have);
            if (got <= 0 || (size_t)got > wanted - have) {
                extra_log(request, "%s: source changed or cannot be read", source->path);
                okay = false;
                break;
            }
            have += (size_t)got;
        }
        for (start = 0; okay && start < have; start += 45U) {
            const unsigned char *data = buffer + start;
            size_t count = have - start > 45U ? 45U : have - start, at, out = 0;
            line[out++] = extra_uu_char((unsigned)count);
            for (at = 0; at < count; at += 3U) {
                unsigned a = data[at];
                unsigned b = at + 1U < count ? data[at + 1U] : 0U;
                unsigned c = at + 2U < count ? data[at + 2U] : 0U;
                line[out++] = extra_uu_char((a >> 2U) & 63U);
                line[out++] = extra_uu_char(((a << 4U) | (b >> 4U)) & 63U);
                line[out++] = extra_uu_char(((b << 2U) | (c >> 6U)) & 63U);
                line[out++] = extra_uu_char(c & 63U);
            }
            line[out++] = '\n';
            okay = extra_write(output, line, out);
        }
        if (okay) remaining -= (uint64_t)have;
    }
    if (xx_io_close(input) != 0) okay = false;
    return okay && !remaining && extra_text(output, "`\nend\n");
}

/* Text through sed: each line behind an 'X' that sed takes off again, so no
 * line can end the here-document. */
static bool extra_shar_plain_data(const extra_source *source, xx_io_device *output,
                                   const xfu_request *request, xx_pd_struct *pd) {
    xx_io_device *input = xfu_native_open_member(source->member);
    unsigned char buffer[65536];
    uint64_t remaining = source->size;
    bool line_start = true;
    bool okay = input != NULL;
    if (!okay) {
        char why[128];
        extra_log(request, "%s: %s", source->path, xfu_native_open_failure(errno, why, sizeof(why)));
        return false;
    }
    while (okay && remaining) {
        size_t wanted = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        ssize_t got;
        size_t at, start = 0;
        if (extra_cancelled(request, pd)) { okay = false; break; }
        got = xx_io_read(input, buffer, wanted);
        if (got <= 0 || (size_t)got > wanted) {
            extra_log(request, "%s: source changed or cannot be read", source->path);
            okay = false;
            break;
        }
        for (at = 0; at < (size_t)got; ++at) {
            if (line_start) {
                if (!extra_text(output, "X")) { okay = false; break; }
                line_start = false;
                start = at;
            }
            if (buffer[at] == '\n') {
                if (!extra_write(output, buffer + start, at + 1U - start)) {
                    okay = false; break;
                }
                line_start = true;
                start = at + 1U;
            }
        }
        if (okay && !line_start && start < (size_t)got)
            okay = extra_write(output, buffer + start, (size_t)got - start);
        remaining -= (uint64_t)got;
    }
    if (xx_io_close(input) != 0) okay = false;
    /* The text was checked to end in a newline; a file changed since is
     * refused rather than stored with one added. */
    if (okay && !line_start) {
        extra_log(request, "%s: source changed or cannot be read", source->path);
        okay = false;
    }
    return okay && !remaining;
}

/* The first length bytes of name as one shell word, in single quotes: a quote
 * in it is written '\'' (extra_shar_name_problem lets none through today).
 * With dot, a name that starts with '-' is written behind "./", so that it is
 * not an option. Every name in the script goes out this way. */
static bool extra_shar_word(xx_io_device *output, const char *name, size_t length,
                            bool dot) {
    size_t at, start = 0;
    if (!extra_text(output, dot && name[0] == '-' ? "'./" : "'")) return false;
    for (at = 0; at < length; ++at) {
        if (name[at] != '\'') continue;
        if (!extra_write(output, name + start, at - start) || !extra_text(output, "'\\''"))
            return false;
        start = at + 1U;
    }
    return extra_write(output, name + start, length - start) && extra_text(output, "'");
}

/* A script that sh runs to recreate each file byte for byte: its folders
 * with mkdir -p, text through sed and other bytes (and every file of
 * shar_dump) through uudecode -o, then its mode. uudecode takes the name
 * from -o, and xxfclib's shar reader from the "begin" line. */
static bool extra_shar_write(const xfu_request *request, extra_source *sources,
                             extra_kind kind, xx_io_device *output, xx_pd_struct *pd) {
    size_t i, folder = 0U;
    const char *previous = NULL;
    if (!extra_text(output, "#!/bin/sh\n# This is a shell archive\n")) return false;
    for (i = 0; i < request->file_count; ++i) {
        const extra_source *source = &sources[i];
        const char *slash;
        char problem[48];
        int text;
        if (extra_cancelled(request, pd) || source->directory) return false;
        if (extra_shar_name_problem(source->name, problem, sizeof(problem))) {
            extra_log(request, "%s: the name %s, which a shell archive cannot store (xfu reads "
                      "one without running it, and would leave the file out); --format=pax "
                      "stores it", source->path, problem);
            return false;
        }
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i,
                                        request->file_count, source->path);
        slash = strrchr(source->name, '/');
        if (slash && (!previous || folder != (size_t)(slash - source->name) ||
                      strncmp(previous, source->name, folder))) {
            folder = (size_t)(slash - source->name);
            previous = source->name;
            if (!extra_text(output, "mkdir -p ") ||
                !extra_shar_word(output, source->name, folder, true) || !extra_text(output, "\n"))
                return false;
        }
        text = kind == EXTRA_SHAR_DUMP ? 0 : extra_shar_text(source, request, pd);
        if (text < 0 || !extra_text(output, "echo x ") ||
            !extra_shar_word(output, source->name, strlen(source->name), false) ||
            !extra_text(output, text ? "\nsed 's/^X//' > " : "\nuudecode -o ") ||
            !extra_shar_word(output, source->name, strlen(source->name), false) ||
            !extra_text(output, " << 'SHAR_END'\n"))
            return false;
        if (text) {
            if (!extra_shar_plain_data(source, output, request, pd)) return false;
        } else {
            /* Here-document text, not shell words: the name as it is. */
            if (!extra_printf(output, "begin %03o %s\n", source->mode, source->name) ||
                !extra_shar_dump_data(source, output, request, pd)) return false;
        }
        if (!extra_text(output, "SHAR_END\n") ||
            !extra_printf(output, "chmod %03o ", source->mode) ||
            !extra_shar_word(output, source->name, strlen(source->name), true) ||
            !extra_text(output, "\n"))
            return false;
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i + 1U,
                                        request->file_count, NULL);
    }
    return extra_text(output, "exit 0\n") && !extra_cancelled(request, pd);
}

static bool extra_warc_write(const xfu_request *request, extra_source *sources,
                             xx_io_device *output, xx_pd_struct *pd) {
    char date[32];
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    uint64_t id_base = (((uint64_t)now << 16U) ^
                        (uint64_t)(uintptr_t)request) & UINT64_C(0xffffffffffff);
    size_t i;
    if (!utc || !strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", utc))
        return false;
    for (i = 0; i < request->file_count; ++i) {
        const extra_source *source = &sources[i];
        if (extra_cancelled(request, pd) || source->directory) return false;
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i,
                                        request->file_count, source->path);
        if (!extra_printf(output,
                          "WARC/1.0\r\nWARC-Type: resource\r\n"
                          "WARC-Target-URI: file://%s\r\nWARC-Date: %s\r\n"
                          "WARC-Record-ID: <urn:uuid:00000000-0000-4000-8000-%012" PRIx64 ">\r\n"
                          "Content-Length: %" PRIu64 "\r\n\r\n",
                          source->name, date,
                          (id_base + i) & UINT64_C(0xffffffffffff), source->size) ||
            !extra_copy(source, output, request, pd) ||
            !extra_text(output, "\r\n\r\n")) return false;
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i + 1U,
                                        request->file_count, NULL);
    }
    return !extra_cancelled(request, pd);
}

/* Binary CPIO stores 32-bit values as two 16-bit words in host order. This
 * produces the documented little-endian wire variant on every host. */
static void extra_put32words_le(unsigned char *buffer, uint32_t value) {
    extra_put16le(buffer, (uint16_t)(value >> 16U));
    extra_put16le(buffer + 2U, (uint16_t)value);
}

static bool extra_cpio_odc_header(xx_io_device *output, const extra_source *source,
                                  const char *name, uint64_t size, size_t index,
                                  bool trailer) {
    char header[77];
    uint64_t namesize = strlen(name) + 1U;
    uint64_t mode = trailer ? 0U :
        (source->directory ? 0040000U : 0100000U) | source->mode;
    uint64_t mtime = trailer ? 0U : (uint64_t)source->mtime;
    uint64_t uid = trailer ? 0U : source->uid;
    uint64_t gid = trailer ? 0U : source->gid;
    int length;
    if (namesize > 0777777U || size > 077777777777ULL ||
        mtime > 077777777777ULL || uid > 0777777U || gid > 0777777U ||
        index > 0777777U) return false;
    length = snprintf(header, sizeof(header),
                      "070707%06o%06o%06" PRIo64 "%06" PRIo64 "%06" PRIo64
                      "%06o%06o%011" PRIo64 "%06" PRIo64 "%011" PRIo64,
                      0U, (unsigned)index, mode, uid, gid,
                      trailer ? 1U : 1U, 0U, mtime, namesize, size);
    return length == 76 && extra_write(output, header, 76U);
}

static bool extra_cpio_bin_header(xx_io_device *output, const extra_source *source,
                                  const char *name, uint64_t size, size_t index,
                                  bool trailer) {
    unsigned char header[26] = {0};
    uint64_t namesize = strlen(name) + 1U;
    uint32_t mode = trailer ? 0U :
        (source->directory ? 0040000U : 0100000U) | source->mode;
    if (namesize > UINT16_MAX || size > UINT32_MAX || index > UINT16_MAX ||
        (!trailer && (source->mtime < 0 || (uint64_t)source->mtime > UINT32_MAX ||
                      source->uid > UINT16_MAX || source->gid > UINT16_MAX)))
        return false;
    extra_put16le(header, UINT16_C(0x71c7));
    extra_put16le(header + 2U, 0U); /* device */
    extra_put16le(header + 4U, (uint16_t)index);
    extra_put16le(header + 6U, (uint16_t)mode);
    extra_put16le(header + 8U, trailer ? 0U : (uint16_t)source->uid);
    extra_put16le(header + 10U, trailer ? 0U : (uint16_t)source->gid);
    extra_put16le(header + 12U, 1U); /* hard-link count */
    extra_put16le(header + 14U, 0U); /* device number */
    extra_put32words_le(header + 16U, trailer ? 0U : (uint32_t)source->mtime);
    extra_put16le(header + 20U, (uint16_t)namesize);
    extra_put32words_le(header + 22U, (uint32_t)size);
    return extra_write(output, header, sizeof(header));
}

static bool extra_cpio_record(const xfu_request *request, const extra_source *source,
                              const char *name, size_t index, bool trailer,
                              extra_kind kind, xx_io_device *output,
                              xx_pd_struct *pd) {
    uint64_t size = trailer || source->directory ? 0U : source->size;
    size_t namesize = strlen(name) + 1U;
    bool binary = kind != EXTRA_CPIO_ODC;
    if (extra_cancelled(request, pd) ||
        !(binary ? extra_cpio_bin_header(output, source, name, size, index, trailer) :
                    extra_cpio_odc_header(output, source, name, size, index, trailer)) ||
        !extra_write(output, name, namesize) ||
        (binary && (namesize & 1U) && !extra_write(output, "\0", 1U)))
        return false;
    if (!trailer && !extra_copy(source, output, request, pd)) return false;
    return !binary || !(size & 1U) || extra_write(output, "\0", 1U);
}

/* Why a CPIO header has no room for a member's fields (index: its inode
 * number), or NULL. The reader caps a member name at 64 KiB, and the binary
 * variants have 16-bit fields. A time is clamped instead. */
static const char *extra_cpio_problem(const extra_source *source, size_t index,
                                      extra_kind kind) {
    bool binary = kind != EXTRA_CPIO_ODC;
    if (strlen(source->name) >= 65535U) return "the name is 65535 bytes or longer";
    if (!source->directory && source->size > (binary ? UINT32_MAX : UINT64_C(077777777777)))
        return binary ? "the file is 4 GiB or larger" : "the file is 8 GiB or larger";
    if (source->uid > (binary ? 0xFFFFU : 0777777U) || source->gid > (binary ? 0xFFFFU : 0777777U))
        return "its owner or group id is larger than the header holds";
    if (index > (binary ? 0xFFFFU : 0777777U)) return "the archive has more members than the header numbers";
    return NULL;
}

static bool extra_cpio_write(const xfu_request *request, extra_source *sources,
                             extra_kind kind, xx_io_device *output,
                             xx_pd_struct *pd) {
    extra_source trailer = {0};
    size_t i;
    const char *label = kind == EXTRA_CPIO_ODC ? "cpio_odc" : kind == EXTRA_CPIO_BIN ? "cpio_bin" :
        "cpio_pwb";
    for (i = 0; i < request->file_count; ++i) {
        const char *problem = extra_cpio_problem(&sources[i], i + 1U, kind);
        if (problem) {
            extra_log(request, "%s: %s, which %s cannot store", sources[i].path, problem, label);
            return false;
        }
    }
    /* odc: 11 octal digits; the binary variants: 32 bits. */
    for (i = 0; i < request->file_count; ++i)
        extra_clamp_time(request, &sources[i], label, 0,
                         kind == EXTRA_CPIO_ODC ? INT64_C(077777777777) : INT64_C(0xFFFFFFFF));
    for (i = 0; i < request->file_count; ++i) {
        if (extra_cancelled(request, pd)) return false;
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i,
                                        request->file_count, sources[i].path);
        if (!extra_cpio_record(request, &sources[i], sources[i].name, i + 1U,
                               false, kind, output, pd)) return false;
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i + 1U,
                                        request->file_count, NULL);
    }
    trailer.name = "TRAILER!!!";
    return extra_cpio_record(request, &trailer, trailer.name, 0U, true,
                             kind, output, pd) && !extra_cancelled(request, pd);
}

typedef struct extra_memory {
    xx_io_device device;
    unsigned char *bytes;
    size_t size;
    size_t capacity;
} extra_memory;

static ssize_t extra_memory_write(xx_io_device *device, const void *data, size_t size) {
    extra_memory *memory = device ? (extra_memory *)device->priv : NULL;
    size_t wanted, capacity;
    unsigned char *grown;
    if (!memory || (!data && size) || size > SIZE_MAX - memory->size ||
        memory->size + size > 128U * 1024U * 1024U) return -1;
    wanted = memory->size + size;
    if (wanted > memory->capacity) {
        capacity = memory->capacity ? memory->capacity : 1024U;
        while (capacity < wanted) {
            if (capacity > SIZE_MAX / 2U) return -1;
            capacity *= 2U;
        }
        grown = (unsigned char *)realloc(memory->bytes, capacity);
        if (!grown) return -1;
        memory->bytes = grown;
        memory->capacity = capacity;
    }
    if (size) memcpy(memory->bytes + memory->size, data, size);
    memory->size = wanted;
    return (ssize_t)size;
}

static void extra_memory_init(extra_memory *memory) {
    memset(memory, 0, sizeof(*memory));
    memory->device.priv = memory;
    memory->device.write = extra_memory_write;
}

static bool extra_xml_text(xx_io_device *output, const char *text, size_t size) {
    size_t at;
    for (at = 0; at < size; ++at) {
        const char *escape = NULL;
        switch (text[at]) {
            case '&': escape = "&amp;"; break;
            case '<': escape = "&lt;"; break;
            case '>': escape = "&gt;"; break;
            case '"': escape = "&quot;"; break;
            case '\'': escape = "&apos;"; break;
            default: break;
        }
        if (!(escape ? extra_text(output, escape) : extra_write(output, text + at, 1U)))
            return false;
    }
    return true;
}

static void extra_put64be(unsigned char *buffer, uint64_t value) {
    extra_put32be(buffer, (uint32_t)(value >> 32U));
    extra_put32be(buffer + 4U, (uint32_t)value);
}

/* Component order: a folder's members follow it directly ('/' sorts first). */
#endif
static int extra_compare_members(const void *left, const void *right) {
    const extra_source *x = *(const extra_source *const *)left;
    const extra_source *y = *(const extra_source *const *)right;
    const unsigned char *a = (const unsigned char *)x->name, *b = (const unsigned char *)y->name;
    for (; *a && *a == *b; ++a, ++b) {}
    if (*a != *b) {
        unsigned ka = !*a ? 0U : *a == '/' ? 1U : *a + 1U;
        unsigned kb = !*b ? 0U : *b == '/' ? 1U : *b + 1U;
        return ka < kb ? -1 : 1;
    }
    return x < y ? -1 : x > y;
}

static extra_source **extra_member_order(const xfu_request *request, extra_source *sources) {
    extra_source **order;
    size_t i;
    if (request->file_count > SIZE_MAX / sizeof(*order)) return NULL;
    order = (extra_source **)malloc((request->file_count ? request->file_count : 1U) *
                                    sizeof(*order));
    if (!order) return NULL;
    for (i = 0; i < request->file_count; ++i) order[i] = &sources[i];
    if (request->file_count > 1U)
        qsort(order, request->file_count, sizeof(*order), extra_compare_members);
    return order;
}

#if XFU_FORMAT_SUPPORT_FULL
/* XAR's times are ISO 8601 in UTC, "2024-03-04T05:06:07Z": years 1 to 9999. */
#define EXTRA_XAR_FIRST_TIME (-INT64_C(62135596800))
#define EXTRA_XAR_LAST_TIME INT64_C(253402300799)

static bool extra_xar_time(xx_io_device *output, const char *element, int64_t seconds,
                           const char *zone) {
    xfu_native_time utc;
    xfu_native_civil(seconds, &utc);
    return extra_printf(output, "<%s>%04" PRId64 "-%02u-%02uT%02u:%02u:%02u%s</%s>", element,
                        utc.year, utc.month, utc.day, utc.hour, utc.minute, utc.second, zone,
                        element);
}

/* One <file> element; a folder's stays open for its members. data: the
 * member (a folder's too), whose owner and time are stored; NULL for a
 * folder on a member's path that was not given. */
static bool extra_xar_element(xx_io_device *output, size_t id, const char *name,
                              size_t length, bool directory, unsigned mode,
                              const extra_source *data, uint64_t offset) {
    if (!extra_printf(output, "<file id=\"%zu\"><name>", id) ||
        !extra_xml_text(output, name, length) ||
        !extra_printf(output, "</name><type>%s</type><mode>0%03o</mode>",
                      directory ? "directory" : "file", mode & 0777U))
        return false;
    if (data && (!extra_printf(output, "<uid>%u</uid><gid>%u</gid>", data->uid, data->gid) ||
                 !extra_xar_time(output, "mtime", data->mtime, "Z")))
        return false;
    if (directory) return true;
    return extra_printf(output,
                        "<data><offset>%" PRIu64 "</offset>"
                        "<length>%" PRIu64 "</length>"
                        "<size>%" PRIu64 "</size>"
                        "<encoding style=\"application/octet-stream\"/>"
                        "</data></file>", offset, data->size, data->size);
}

/* xxfclib's XAR reader nests at most 256 elements: <xar>, <toc>, then the
 * member's <file> elements, and its <data> and <offset>. So a member path
 * has at most 252 components, and 4095 bytes. */
#define EXTRA_XAR_DEPTH 252U
#define EXTRA_XAR_NAME 4095U

/* XAR nests each member's element in its folders' elements, one element per
 * folder however many members it holds, each with its own id. Members come
 * in component order, so a folder's element is open while its members are
 * written; folders on a path that were not given get mode 0755. */
static bool extra_xar_toc(xx_io_device *output, const xfu_request *request,
                          extra_source *sources, extra_source **order,
                          const uint64_t *offsets) {
    const char *open_name[EXTRA_XAR_DEPTH];
    size_t open_length[EXTRA_XAR_DEPTH];
    size_t open_depth = 0, id = 0, i;
    for (i = 0; i < request->file_count; ++i) {
        const extra_source *source = order[i];
        const char *part = source->name;
        size_t depth = 0;
        for (;;) {
            const char *end = strchr(part, '/');
            size_t length = end ? (size_t)(end - part) : strlen(part);
            if (!length || depth >= EXTRA_XAR_DEPTH) return false;
            if (!end) {
                while (open_depth > depth) {
                    if (!extra_text(output, "</file>")) return false;
                    --open_depth;
                }
                if (!extra_xar_element(output, ++id, part, length, source->directory,
                                       source->mode, source, offsets[source - sources]))
                    return false;
                if (source->directory) {
                    open_name[open_depth] = part;
                    open_length[open_depth++] = length;
                }
                break;
            }
            if (depth < open_depth && (open_length[depth] != length ||
                                       memcmp(open_name[depth], part, length))) {
                while (open_depth > depth) {
                    if (!extra_text(output, "</file>")) return false;
                    --open_depth;
                }
            }
            if (depth == open_depth) {
                if (!extra_xar_element(output, ++id, part, length, true, 0755U, NULL, 0U))
                    return false;
                open_name[open_depth] = part;
                open_length[open_depth++] = length;
            }
            ++depth;
            part = end + 1U;
        }
    }
    while (open_depth--) if (!extra_text(output, "</file>")) return false;
    return true;
}

static bool extra_xar_write(const xfu_request *request, extra_source *sources,
                            xx_io_device *output, xx_pd_struct *pd) {
    extra_memory toc;
    unsigned char header[28] = {0}, digest[20];
    unsigned char *packed = NULL;
    extra_source **order = NULL;
    uint64_t *offsets = NULL;
    size_t packed_size;
    uint64_t heap_size = 0;
    size_t i;
    bool okay = false;
    extra_memory_init(&toc);
    if (request->file_count > SIZE_MAX / sizeof(*offsets) ||
        !(offsets = (uint64_t *)malloc((request->file_count ? request->file_count : 1U) *
                                       sizeof(*offsets))) ||
        !(order = extra_member_order(request, sources))) goto done;
    /* The heap holds the TOC's SHA-1 digest, which xar(1) requires and
     * checks, then the files' data in input order. */
    heap_size = sizeof(digest);
    for (i = 0; i < request->file_count; ++i) {
        const extra_source *source = &sources[i];
        const char *at;
        size_t components = 1U;
        if (extra_cancelled(request, pd)) goto done;
        for (at = source->name; *at; ++at) components += *at == '/';
        if (strlen(source->name) > EXTRA_XAR_NAME || components > EXTRA_XAR_DEPTH) {
            extra_log(request, components > EXTRA_XAR_DEPTH ?
                      "%s: the name has more than %u components, which xar cannot store" :
                      "%s: the name is longer than %u bytes, which xar cannot store", source->path,
                      components > EXTRA_XAR_DEPTH ? EXTRA_XAR_DEPTH : EXTRA_XAR_NAME);
            goto done;
        }
        if (source->size > (uint64_t)INT64_MAX - heap_size) goto done;
        offsets[i] = heap_size;
        heap_size += source->size;
        extra_clamp_time(request, &sources[i], "xar", EXTRA_XAR_FIRST_TIME, EXTRA_XAR_LAST_TIME);
    }
    if (!extra_text(&toc.device, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                                 "<xar><toc><checksum style=\"sha1\"><offset>0</offset>"
                                 "<size>20</size></checksum>") ||
        !extra_xar_time(&toc.device, "creation-time", (int64_t)time(NULL), "") ||
        !extra_xar_toc(&toc.device, request, sources, order, offsets) ||
        !extra_text(&toc.device, "</toc></xar>") ||
        !toc.size || toc.size > 128U * 1024U * 1024U)
        goto done;
    packed_size = xx_zlib_stream_compress_bound(toc.size);
    if (packed_size > 64U * 1024U * 1024U) goto done;
    packed = (unsigned char *)malloc((size_t)packed_size);
    if (!packed || !xx_zlib_stream_encode_memory(toc.bytes, toc.size, packed,
                                                  packed_size, &packed_size, 6) ||
        !xx_sha1_memory(packed, (size_t)packed_size, digest))
        goto done;
    memcpy(header, "xar!", 4U);
    extra_put16be(header + 4U, 28U);
    extra_put16be(header + 6U, 1U);
    extra_put64be(header + 8U, packed_size);
    extra_put64be(header + 16U, toc.size);
    extra_put32be(header + 24U, 1U); /* SHA-1 of the packed TOC, first in the heap */
    if (!extra_write(output, header, sizeof(header)) ||
        !extra_write(output, packed, (size_t)packed_size) ||
        !extra_write(output, digest, sizeof(digest))) goto done;
    for (i = 0; i < request->file_count; ++i) {
        if (extra_cancelled(request, pd)) goto done;
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i,
                                        request->file_count, sources[i].path);
        if (!extra_copy(&sources[i], output, request, pd)) goto done;
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i + 1U,
                                        request->file_count, NULL);
    }
    okay = !extra_cancelled(request, pd);
done:
    free(packed);
    free(toc.bytes);
    free(order);
    free(offsets);
    return okay;
}
#endif

#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_ISO9660)
#define EXTRA_ISO_BLOCK 2048U
/* libarchive (bsdtar) reads 48 KiB before it takes a file for ISO 9660 and
 * otherwise sees an empty tar: a smaller image is padded with zero sectors. */
#define EXTRA_ISO_MIN_SECTORS 32U

static void extra_put32le(unsigned char *buffer, uint32_t value) {
    buffer[0] = (unsigned char)value;
    buffer[1] = (unsigned char)(value >> 8U);
    buffer[2] = (unsigned char)(value >> 16U);
    buffer[3] = (unsigned char)(value >> 24U);
}

static void extra_iso_both16(unsigned char *buffer, uint16_t value) {
    extra_put16le(buffer, value);
    extra_put16be(buffer + 2U, value);
}

static void extra_iso_both32(unsigned char *buffer, uint32_t value) {
    extra_put32le(buffer, value);
    extra_put32be(buffer + 4U, value);
}

static bool extra_iso_zero(xx_io_device *output, uint64_t size) {
    static const unsigned char zero[EXTRA_ISO_BLOCK] = {0};
    while (size) {
        size_t chunk = size > sizeof(zero) ? sizeof(zero) : (size_t)size;
        if (!extra_write(output, zero, chunk)) return false;
        size -= chunk;
    }
    return true;
}

/* The years a directory record holds: 1900 to 2155 (a byte past 1900). */
#define EXTRA_ISO_FIRST_TIME (-INT64_C(2208988800))
#define EXTRA_ISO_LAST_TIME INT64_C(5869583999)

/* A directory record's date, in UTC; a time outside its years (clamped
 * before, with a warning) leaves it unspecified. */
static void extra_iso_date(unsigned char *out, int64_t seconds) {
    xfu_native_time utc;
    memset(out, 0, 7U);
    if (seconds < EXTRA_ISO_FIRST_TIME || seconds > EXTRA_ISO_LAST_TIME) return;
    xfu_native_civil(seconds, &utc);
    out[0] = (unsigned char)(utc.year - 1900);
    out[1] = (unsigned char)utc.month;
    out[2] = (unsigned char)utc.day;
    out[3] = (unsigned char)utc.hour;
    out[4] = (unsigned char)utc.minute;
    out[5] = (unsigned char)utc.second;
    out[6] = 0;
}

static size_t extra_iso_record_length(size_t identifier, size_t rr_name,
                                       bool suspended) {
    size_t length = 33U + identifier + ((identifier & 1U) ? 0U : 1U);
    if (suspended) length += 7U + 18U; /* SUSP SP and RRIP ER */
    if (rr_name) {
        if (rr_name > 250U) return 0U;
        length += 5U + rr_name;
    }
    if (length & 1U) ++length;
    return length <= 255U ? length : 0U;
}

static size_t extra_iso_record(unsigned char *output, size_t capacity,
                               uint32_t extent, uint32_t size,
                               const unsigned char *identifier,
                               size_t identifier_length, const char *rr_name,
                               bool directory, bool suspended,
                               int64_t mtime) {
    size_t rr_length = rr_name ? strlen(rr_name) : 0U;
    size_t length = extra_iso_record_length(identifier_length, rr_length,
                                            suspended);
    size_t position;
    if (!length || length > capacity || identifier_length > 255U) return 0U;
    memset(output, 0, length);
    output[0] = (unsigned char)length;
    extra_iso_both32(output + 2U, extent);
    extra_iso_both32(output + 10U, size);
    extra_iso_date(output + 18U, mtime);
    output[25] = directory ? 2U : 0U;
    extra_iso_both16(output + 28U, 1U);
    output[32] = (unsigned char)identifier_length;
    memcpy(output + 33U, identifier, identifier_length);
    position = 33U + identifier_length + ((identifier_length & 1U) ? 0U : 1U);
    if (suspended) {
        memcpy(output + position, "SP", 2U);
        output[position + 2U] = 7U;
        output[position + 3U] = 1U;
        output[position + 4U] = 0xBEU;
        output[position + 5U] = 0xEFU;
        output[position + 6U] = 0U;
        position += 7U;
        memcpy(output + position, "ER", 2U);
        output[position + 2U] = 18U;
        output[position + 3U] = 1U;
        output[position + 4U] = 10U;
        output[position + 5U] = 0U;
        output[position + 6U] = 0U;
        output[position + 7U] = 1U;
        memcpy(output + position + 8U, "RRIP_1991A", 10U);
        position += 18U;
    }
    if (rr_name) {
        memcpy(output + position, "NM", 2U);
        output[position + 2U] = (unsigned char)(5U + rr_length);
        output[position + 3U] = 1U;
        output[position + 4U] = 0U;
        memcpy(output + position + 5U, rr_name, rr_length);
    }
    return length;
}

static bool extra_iso_add_record(unsigned char *directory, size_t directory_size,
                                 size_t *position, uint32_t extent,
                                 uint32_t size, const unsigned char *identifier,
                                 size_t identifier_length, const char *rr_name,
                                 bool is_directory, bool suspended,
                                 int64_t mtime) {
    size_t length = extra_iso_record_length(identifier_length,
                                            rr_name ? strlen(rr_name) : 0U,
                                            suspended);
    size_t offset = *position % EXTRA_ISO_BLOCK;
    if (!length) return false;
    if (offset + length > EXTRA_ISO_BLOCK) *position += EXTRA_ISO_BLOCK - offset;
    if (*position > directory_size || length > directory_size - *position) return false;
    if (!extra_iso_record(directory + *position, length, extent, size,
                          identifier, identifier_length, rr_name,
                          is_directory, suspended, mtime)) return false;
    *position += length;
    return true;
}

typedef struct extra_iso_node {
    char *name;
    char identifier[16];
    size_t parent;
    size_t source;
    uint32_t extent;
    uint32_t size;
    uint16_t number;
    bool directory;
} extra_iso_node;

static bool extra_iso_push(extra_iso_node **nodes, size_t *count,
                           size_t *capacity, size_t parent, const char *name,
                           size_t length, bool directory, size_t source) {
    extra_iso_node *node;
    size_t new_capacity;
    if (*count >= 999999U || !length || length > 175U) return false;
    if (*count == *capacity) {
        extra_iso_node *grown;
        new_capacity = *capacity ? *capacity * 2U : 16U;
        if (new_capacity < *capacity ||
            new_capacity > SIZE_MAX / sizeof(**nodes)) return false;
        grown = (extra_iso_node *)realloc(*nodes,
                                         new_capacity * sizeof(**nodes));
        if (!grown) return false;
        *nodes = grown;
        *capacity = new_capacity;
    }
    node = &(*nodes)[*count];
    memset(node, 0, sizeof(*node));
    node->name = (char *)malloc(length + 1U);
    if (!node->name) return false;
    memcpy(node->name, name, length);
    node->name[length] = '\0';
    node->parent = parent;
    node->source = source;
    node->directory = directory;
    if (directory)
        snprintf(node->identifier, sizeof(node->identifier), "D%06zu", *count);
    else
        snprintf(node->identifier, sizeof(node->identifier), "F%06zu.TXT;1", *count);
    ++*count;
    return true;
}

static bool extra_iso_tree(const xfu_request *request, extra_source *sources,
                           extra_iso_node **result, size_t *result_count,
                           size_t *source_node) {
    extra_iso_node *nodes = NULL;
    size_t count = 0, capacity = 0, i;
    if (!extra_iso_push(&nodes, &count, &capacity, 0U, ".", 1U,
                        true, SIZE_MAX)) goto fail;
    nodes[0].identifier[0] = '\0';
    for (i = 0; i < request->file_count; ++i) {
        const char *part = sources[i].name;
        size_t parent = 0;
        while (*part) {
            const char *end = strchr(part, '/');
            size_t length = end ? (size_t)(end - part) : strlen(part);
            bool directory = end != NULL || sources[i].directory;
            size_t j;
            if (length > 175U)
                extra_log(request, "%s: a name component is longer than 175 bytes, which "
                          "iso9660 cannot store", sources[i].path);
            if (!length || length > 175U) goto fail;
            for (j = 1U; j < count; ++j) {
                if (nodes[j].parent == parent &&
                    strlen(nodes[j].name) == length &&
                    !memcmp(nodes[j].name, part, length)) break;
            }
            if (j == count) {
                if (!extra_iso_push(&nodes, &count, &capacity, parent,
                                    part, length, directory,
                                    end ? SIZE_MAX : i)) goto fail;
            } else if (nodes[j].directory != directory ||
                       (!end && nodes[j].source != SIZE_MAX)) {
                goto fail;
            } else if (!end) {
                nodes[j].source = i;
            }
            parent = j;
            if (!end) break;
            part = end + 1U;
        }
        source_node[i] = parent;
    }
    *result = nodes;
    *result_count = count;
    return true;
fail:
    for (i = 0; i < count; ++i) free(nodes[i].name);
    free(nodes);
    return false;
}

static bool extra_iso_consume(size_t *used, size_t length) {
    size_t offset = *used % EXTRA_ISO_BLOCK;
    if (!length || length > EXTRA_ISO_BLOCK) return false;
    if (offset + length > EXTRA_ISO_BLOCK) {
        if (*used > SIZE_MAX - (EXTRA_ISO_BLOCK - offset)) return false;
        *used += EXTRA_ISO_BLOCK - offset;
    }
    if (*used > SIZE_MAX - length) return false;
    *used += length;
    return true;
}

static bool extra_iso_path_entry(unsigned char *table, size_t capacity,
                                 size_t *position, const extra_iso_node *node,
                                 uint16_t parent, bool big_endian,
                                 bool root) {
    size_t length = root ? 1U : strlen(node->identifier);
    size_t record = 8U + length + (length & 1U);
    unsigned char *entry;
    if (length > 255U || *position > capacity ||
        record > capacity - *position) return false;
    entry = table + *position;
    entry[0] = (unsigned char)length;
    if (big_endian) {
        extra_put32be(entry + 2U, node->extent);
        extra_put16be(entry + 6U, parent);
    } else {
        extra_put32le(entry + 2U, node->extent);
        extra_put16le(entry + 6U, parent);
    }
    if (!root) memcpy(entry + 8U, node->identifier, length);
    *position += record;
    return true;
}

static bool extra_iso_write(const xfu_request *request, extra_source *sources,
                            xx_io_device *output, xx_pd_struct *pd) {
    static const unsigned char dot = 0U, dotdot = 1U;
    unsigned char pvd[EXTRA_ISO_BLOCK] = {0};
    unsigned char end[EXTRA_ISO_BLOCK] = {0};
    unsigned char *path_le = NULL, *path_be = NULL, *directory = NULL;
    extra_iso_node *nodes = NULL;
    size_t *source_node = NULL, *order = NULL;
    size_t count = 0, directory_count = 0, table_used = 0;
    size_t table_size, table_position, i, j;
    uint32_t next_sector, root_sector;
    bool okay = false;
    if (request->file_count > 999999U ||
        request->file_count > SIZE_MAX / sizeof(*source_node)) return false;
    for (i = 0; i < request->file_count; ++i)
        extra_clamp_time(request, &sources[i], "iso9660", EXTRA_ISO_FIRST_TIME, EXTRA_ISO_LAST_TIME);
    source_node = (size_t *)calloc(request->file_count ? request->file_count : 1U,
                                   sizeof(*source_node));
    if (!source_node ||
        !extra_iso_tree(request, sources, &nodes, &count, source_node) ||
        count > SIZE_MAX / sizeof(*order)) goto done;
    order = (size_t *)calloc(count, sizeof(*order));
    if (!order) goto done;
    order[directory_count++] = 0U;
    nodes[0].number = 1U;
    /* ISO path tables list directories by depth, with the parent first. */
    for (i = 0; i < directory_count; ++i) {
        for (j = 1U; j < count; ++j) {
            if (nodes[j].directory && nodes[j].parent == order[i]) {
                if (directory_count >= UINT16_MAX) goto done;
                nodes[j].number = (uint16_t)(directory_count + 1U);
                order[directory_count++] = j;
            }
        }
    }
    for (i = 0; i < directory_count; ++i) {
        extra_iso_node *node = &nodes[order[i]];
        size_t used = 0, id_length = i ? strlen(node->identifier) : 1U;
        size_t path_record = 8U + id_length + (id_length & 1U);
        if (table_used > UINT32_MAX - path_record) goto done;
        table_used += path_record;
        if (!extra_iso_consume(&used, extra_iso_record_length(1U, 0U,
                                                              i == 0U)) ||
            !extra_iso_consume(&used, extra_iso_record_length(1U, 0U,
                                                              false))) goto done;
        for (j = 1U; j < count; ++j) {
            if (nodes[j].parent == order[i] &&
                !extra_iso_consume(&used,
                    extra_iso_record_length(strlen(nodes[j].identifier),
                                            strlen(nodes[j].name), false))) goto done;
        }
        if (used > UINT32_MAX - (EXTRA_ISO_BLOCK - 1U)) goto done;
        node->size = (uint32_t)((used + EXTRA_ISO_BLOCK - 1U) /
                                EXTRA_ISO_BLOCK * EXTRA_ISO_BLOCK);
    }
    if (!table_used || table_used > SIZE_MAX - (EXTRA_ISO_BLOCK - 1U)) goto done;
    table_size = (table_used + EXTRA_ISO_BLOCK - 1U) /
        EXTRA_ISO_BLOCK * EXTRA_ISO_BLOCK;
    if (table_size / EXTRA_ISO_BLOCK > (UINT32_MAX - 18U) / 2U) goto done;
    root_sector = 18U + (uint32_t)(table_size / EXTRA_ISO_BLOCK) * 2U;
    next_sector = root_sector;
    for (i = 0; i < directory_count; ++i) {
        extra_iso_node *node = &nodes[order[i]];
        uint32_t sectors = node->size / EXTRA_ISO_BLOCK;
        if (sectors > UINT32_MAX - next_sector) goto done;
        node->extent = next_sector;
        next_sector += sectors;
    }
    for (i = 0; i < request->file_count; ++i) {
        extra_iso_node *node = &nodes[source_node[i]];
        uint64_t sectors;
        if (node->directory) continue;
        if (sources[i].size > UINT32_MAX) {
            extra_log(request, "%s: the file is 4 GiB or larger, which iso9660 cannot store",
                      sources[i].path);
            goto done;
        }
        sectors = (sources[i].size + EXTRA_ISO_BLOCK - 1U) / EXTRA_ISO_BLOCK;
        if (sectors > UINT32_MAX - next_sector) goto done;
        node->size = (uint32_t)sources[i].size;
        node->extent = next_sector;
        next_sector += (uint32_t)sectors;
    }
    path_le = (unsigned char *)calloc(table_size, 1U);
    path_be = (unsigned char *)calloc(table_size, 1U);
    if (!path_le || !path_be) goto done;
    table_position = 0;
    for (i = 0; i < directory_count; ++i) {
        extra_iso_node *node = &nodes[order[i]];
        uint16_t parent = i ? nodes[node->parent].number : 1U;
        if (!extra_iso_path_entry(path_le, table_size, &table_position,
                                  node, parent, false, i == 0U)) goto done;
    }
    table_position = 0;
    for (i = 0; i < directory_count; ++i) {
        extra_iso_node *node = &nodes[order[i]];
        uint16_t parent = i ? nodes[node->parent].number : 1U;
        if (!extra_iso_path_entry(path_be, table_size, &table_position,
                                  node, parent, true, i == 0U)) goto done;
    }
    pvd[0] = 1U;
    memcpy(pvd + 1U, "CD001", 5U);
    pvd[6] = 1U;
    memset(pvd + 8U, ' ', 64U);
    memcpy(pvd + 8U, "XFILEUNPACKER", 13U);
    memcpy(pvd + 40U, "XFILEUNPACKER", 13U);
    extra_iso_both32(pvd + 80U, next_sector < EXTRA_ISO_MIN_SECTORS ?
                     EXTRA_ISO_MIN_SECTORS : next_sector);
    extra_iso_both16(pvd + 120U, 1U);
    extra_iso_both16(pvd + 124U, 1U);
    extra_iso_both16(pvd + 128U, EXTRA_ISO_BLOCK);
    extra_iso_both32(pvd + 132U, (uint32_t)table_used);
    extra_put32le(pvd + 140U, 18U);
    extra_put32be(pvd + 148U, 18U + (uint32_t)(table_size / EXTRA_ISO_BLOCK));
    if (!extra_iso_record(pvd + 156U, sizeof(pvd) - 156U,
                          root_sector, nodes[0].size, &dot, 1U, NULL,
                          true, false, time(NULL))) goto done;
    pvd[881] = 1U;
    end[0] = 255U;
    memcpy(end + 1U, "CD001", 5U);
    end[6] = 1U;
    if (!extra_iso_zero(output, 16U * EXTRA_ISO_BLOCK) ||
        !extra_write(output, pvd, sizeof(pvd)) ||
        !extra_write(output, end, sizeof(end)) ||
        !extra_write(output, path_le, table_size) ||
        !extra_write(output, path_be, table_size)) goto done;
    for (i = 0; i < directory_count; ++i) {
        extra_iso_node *node = &nodes[order[i]];
        extra_iso_node *parent = &nodes[i ? node->parent : 0U];
        size_t used = 0;
        int64_t mtime = node->source == SIZE_MAX ? (int64_t)time(NULL) :
                        sources[node->source].mtime;
        if (extra_cancelled(request, pd)) goto done;
        directory = (unsigned char *)calloc(node->size, 1U);
        if (!directory ||
            !extra_iso_add_record(directory, node->size, &used,
                                  node->extent, node->size, &dot, 1U, NULL,
                                  true, i == 0U, mtime) ||
            !extra_iso_add_record(directory, node->size, &used,
                                  parent->extent, parent->size, &dotdot, 1U,
                                  NULL, true, false, mtime)) goto done;
        for (j = 1U; j < count; ++j) {
            extra_iso_node *child = &nodes[j];
            int64_t child_mtime;
            if (child->parent != order[i]) continue;
            child_mtime = child->source == SIZE_MAX ? (int64_t)time(NULL) :
                          sources[child->source].mtime;
            if (!extra_iso_add_record(directory, node->size, &used,
                                      child->extent, child->size,
                                      (const unsigned char *)child->identifier,
                                      strlen(child->identifier), child->name,
                                      child->directory, false,
                                      child_mtime)) goto done;
        }
        if (!extra_write(output, directory, node->size)) goto done;
        free(directory);
        directory = NULL;
    }
    for (i = 0; i < request->file_count; ++i) {
        uint64_t padding;
        if (sources[i].directory) continue;
        padding = (EXTRA_ISO_BLOCK -
            sources[i].size % EXTRA_ISO_BLOCK) % EXTRA_ISO_BLOCK;
        if (extra_cancelled(request, pd)) goto done;
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i,
                                        request->file_count, sources[i].path);
        if (!extra_copy(&sources[i], output, request, pd) ||
            !extra_iso_zero(output, padding)) goto done;
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i + 1U,
                                        request->file_count, NULL);
    }
    if (next_sector < EXTRA_ISO_MIN_SECTORS &&
        !extra_iso_zero(output, (uint64_t)(EXTRA_ISO_MIN_SECTORS - next_sector) * EXTRA_ISO_BLOCK))
        goto done;
    okay = !extra_cancelled(request, pd);
done:
    if (nodes) for (i = 0; i < count; ++i) free(nodes[i].name);
    free(nodes);
    free(source_node);
    free(order);
    free(path_le);
    free(path_be);
    free(directory);
    return okay;
}

#endif
bool xfu_native_extra_write(const xfu_request *request, const char *format,
                            const xfu_native_member *members, xx_io_device *output,
                            xx_pd_struct *pd) {
    extra_kind kind = extra_select(format);
    extra_source *sources = NULL, **order = NULL;
    size_t i;
    bool okay = false;
    /* Each failure logs its reason once, where it arises; the caller adds one
     * generic line for a failure that logged none. */
    if (!request || !output || kind == EXTRA_NONE ||
        !extra_sources(request, members, format ? format : "archive", &sources))
        return false;
    /* Sorted, equal names are neighbours: no quadratic scan of a big tree. */
    if (!(order = extra_member_order(request, sources))) goto done;
    for (i = 1U; i < request->file_count; ++i) {
        if (!strcmp(order[i - 1U]->name, order[i]->name)) {
            extra_log(request, "duplicate archive member name: %s", order[i]->name);
            goto done;
        }
    }
    switch (kind) {
        case EXTRA_AR_BSD: case EXTRA_AR_GNU:
            okay = extra_ar_write(request, sources, kind, output, pd); break;
#if XFU_FORMAT_SUPPORT_FULL
        case EXTRA_MTREE: case EXTRA_MTREE_CLASSIC:
            okay = extra_mtree_write(request, sources, kind, output, pd); break;
        case EXTRA_SHAR: case EXTRA_SHAR_DUMP:
            okay = extra_shar_write(request, sources, kind, output, pd); break;
        case EXTRA_WARC:
            okay = extra_warc_write(request, sources, output, pd); break;
        case EXTRA_CPIO_ODC: case EXTRA_CPIO_BIN: case EXTRA_CPIO_PWB:
            okay = extra_cpio_write(request, sources, kind, output, pd); break;
        case EXTRA_XAR:
            okay = extra_xar_write(request, sources, output, pd); break;
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_ISO9660)
        case EXTRA_ISO9660:
            okay = extra_iso_write(request, sources, output, pd); break;
#endif
        default: break;
    }
done:
    free(order);
    extra_free_sources(sources, request->file_count);
    return okay;
}
