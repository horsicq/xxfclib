/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#endif

#include "native_tar_writer.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAR_BLOCK 512U
#define TAR_MAX_NAME (1024U * 1024U)
/* The latest time 11 octal digits hold (2242-03-16). */
#define TAR_MAX_TIME INT64_C(077777777777)
/* The largest owner or group id 7 octal digits hold, and what a ustar field
 * gets for a larger one that a pax record stores: nobody's, never root's 0. */
#define TAR_MAX_ID 07777777U
#define TAR_NOBODY_ID 65534U

typedef enum tar_variant {
    TAR_UNKNOWN = 0,
    TAR_USTAR,
    TAR_PAX,
    TAR_PAX_RESTRICTED,
    TAR_GNU,
    TAR_V7
} tar_variant;

typedef struct tar_source {
    const char *path;
    const xfu_native_member *member; /* As walked: opened with its checks. */
    char *name;
    int64_t size;
    int64_t stamp;  /* The modification time as walked: negative before 1970. */
    uint64_t mtime; /* The time a ustar header holds: stamp, clamped to its range. */
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    bool directory;
} tar_source;

static tar_variant tar_parse(const char *format)
{
    if (!format) return TAR_UNKNOWN;
    if (!strcmp(format, "ustar")) return TAR_USTAR;
    if (!strcmp(format, "pax") || !strcmp(format, "posix")) return TAR_PAX;
    if (!strcmp(format, "tar") || !strcmp(format, "pax_restricted") || !strcmp(format, "paxr") || !strcmp(format, "rpax") || !strcmp(format, "bsdtar"))
        return TAR_PAX_RESTRICTED;
    if (!strcmp(format, "gnutar")) return TAR_GNU;
    if (!strcmp(format, "v7tar") || !strcmp(format, "v7") || !strcmp(format, "oldtar")) return TAR_V7;
    return TAR_UNKNOWN;
}

bool xfu_native_tar_can_create(const char *format)
{
    return tar_parse(format) != TAR_UNKNOWN;
}

/* Whole lines: a long path is never cut. */
static void tar_log(const xfu_request *request, const char *format, ...)
{
    char local[1024], *line = local;
    va_list arguments, copy;
    int count;
    if (!request->callbacks.log) return;
    va_start(arguments, format);
    va_copy(copy, arguments);
    count = vsnprintf(local, sizeof(local), format, arguments);
    va_end(arguments);
    if (count >= (int)sizeof(local)) {
        line = (char *)malloc((size_t)count + 1U);
        if (line) (void)vsnprintf(line, (size_t)count + 1U, format, copy);
        else line = local;
    }
    va_end(copy);
    request->callbacks.log(request->callbacks.user, true, line);
    if (line != local) free(line);
}

static bool tar_cancelled(const xfu_request *request, xx_pd_struct *pd)
{
    if (request->callbacks.cancelled && request->callbacks.cancelled(request->callbacks.user)) xx_pd_stop(pd);
    return xx_pd_is_stopped(pd);
}

static bool tar_write_all(xx_io_device *output, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    while (size) {
        ssize_t written = xx_io_write(output, bytes, size);
        if (written <= 0 || (size_t)written > size) return false;
        bytes += written;
        size -= (size_t)written;
    }
    return true;
}

/* A member name the caller already formed: kept as given, a folder's with
 * the trailing slash TAR uses. */
static char *tar_given_name(const char *given, bool directory)
{
    size_t size = strlen(given);
    char *name;
    if (!size || size > TAR_MAX_NAME - 2U) return NULL;
    name = (char *)malloc(size + 2U);
    if (!name) return NULL;
    memcpy(name, given, size);
    if (directory) name[size++] = '/';
    name[size] = '\0';
    return name;
}

static bool tar_utf8(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        size_t length = *p < 0x80U ? 1U : *p >= 0xC2U && *p <= 0xDFU ? 2U : *p >= 0xE0U && *p <= 0xEFU ? 3U : *p >= 0xF0U && *p <= 0xF4U ? 4U : 0U, i;
        if (!length) return false;
        for (i = 1U; i < length; ++i)
            if ((p[i] & 0xC0U) != 0x80U) return false;
        if (length > 1U && ((*p == 0xE0U && p[1] < 0xA0U) || (*p == 0xEDU && p[1] >= 0xA0U) || (*p == 0xF0U && p[1] < 0x90U) || (*p == 0xF4U && p[1] >= 0x90U)))
            return false;
        p += length;
    }
    return true;
}

/* The header fields: the metadata xfu_native_create walked (for a link
 * named as an input, that of the file or folder it names). */
static bool tar_describe(tar_source *source, const xfu_native_member *member)
{
    source->directory = member->directory;
    source->size = member->directory ? 0 : member->size;
    source->mode = member->mode & 07777U;
    source->stamp = member->mtime;
    source->mtime = member->mtime < 0 ? 0U : member->mtime > TAR_MAX_TIME ? (uint64_t)TAR_MAX_TIME : (uint64_t)member->mtime;
    source->uid = member->uid;
    source->gid = member->gid;
    source->name = member->name ? tar_given_name(member->name, member->directory) : NULL;
    return member->size >= 0 && source->name != NULL;
}

static bool tar_octal(uint8_t *destination, size_t length, uint64_t number)
{
    uint64_t limit = 1U;
    size_t i;
    int count;
    if (length < 2U) return false;
    for (i = 0; i < length - 1U; ++i) {
        if (limit > UINT64_MAX / 8U) {
            limit = UINT64_MAX;
            break;
        }
        limit *= 8U;
    }
    if (number >= limit) return false;
    count = snprintf((char *)destination, length, "%0*llo", (int)(length - 1U), (unsigned long long)number);
    return count == (int)(length - 1U);
}

static bool tar_split_ustar(const char *name, uint8_t *header)
{
    size_t length = strlen(name), split;
    if (length <= 100U) {
        memcpy(header, name, length);
        return true;
    }
    if (length > 256U) return false; /* 155 bytes, '/' and 100. */
    for (split = length; split > 0U; --split) {
        size_t suffix;
        if (name[split - 1U] != '/') continue;
        suffix = length - split;
        if (split - 1U <= 155U && suffix <= 100U && suffix) {
            memcpy(header, name + split, suffix);
            memcpy(header + 345U, name, split - 1U);
            return true;
        }
    }
    return false;
}

static bool tar_header(uint8_t header[TAR_BLOCK], tar_variant variant, const char *name, uint64_t size, uint64_t mtime, uint32_t mode, uint32_t uid, uint32_t gid,
                       char type)
{
    uint64_t checksum = 0U;
    size_t i, length = strlen(name);
    memset(header, 0, TAR_BLOCK);
    if (variant == TAR_USTAR || variant == TAR_PAX || variant == TAR_PAX_RESTRICTED) {
        if (!tar_split_ustar(name, header)) return false;
        memcpy(header + 257U, "ustar", 5U);
        header[263U] = '0';
        header[264U] = '0';
    } else {
        if (length > 100U) return false;
        memcpy(header, name, length);
        if (variant == TAR_GNU) memcpy(header + 257U, "ustar  ", 7U);
    }
    if (!tar_octal(header + 100U, 8U, mode) || !tar_octal(header + 108U, 8U, uid) || !tar_octal(header + 116U, 8U, gid) || !tar_octal(header + 124U, 12U, size) ||
        !tar_octal(header + 136U, 12U, mtime))
        return false;
    memset(header + 148U, ' ', 8U);
    header[156U] = (uint8_t)type;
    for (i = 0; i < TAR_BLOCK; ++i) checksum += header[i];
    if (!tar_octal(header + 148U, 7U, checksum)) return false;
    header[155U] = ' ';
    return true;
}

static bool tar_pad(xx_io_device *output, uint64_t size)
{
    static const uint8_t zero[TAR_BLOCK] = {0};
    size_t padding = (size_t)((TAR_BLOCK - size % TAR_BLOCK) % TAR_BLOCK);
    return padding == 0U || tar_write_all(output, zero, padding);
}

/* Whether a ustar header holds the member's time as it is; else a pax
 * header stores it (any time, as a decimal, negative before 1970). */
static bool tar_time_fits(const tar_source *source)
{
    return source->stamp >= 0 && source->stamp <= TAR_MAX_TIME;
}

/* Whether a ustar header holds the member's owner and group ids; else a
 * pax header stores them (uid and gid). */
static bool tar_owner_fits(const tar_source *source)
{
    return source->uid <= TAR_MAX_ID && source->gid <= TAR_MAX_ID;
}

/* An id for the ustar field of a member whose pax header holds it. */
static uint32_t tar_header_id(uint32_t id)
{
    return id > TAR_MAX_ID ? TAR_NOBODY_ID : id;
}

/* Appends one pax record, "<length> <key>=<value>\n", whose length counts
 * its own digits. */
static bool tar_pax_add(char **payload, size_t *used, const char *key, const char *value)
{
    size_t body = strlen(key) + strlen(value) + 3U, length = body, previous;
    char *grown;
    int count;
    if (body > SIZE_MAX / 2U - 64U || *used > SIZE_MAX / 2U - body - 64U) return false;
    do {
        previous = length;
        count = snprintf(NULL, 0, "%zu", length);
        if (count < 0) return false;
        length = body + (size_t)count;
    } while (length != previous);
    if (!(grown = (char *)realloc(*payload, *used + length + 1U))) return false;
    *payload = grown;
    count = snprintf(grown + *used, length + 1U, "%zu %s=%s\n", length, key, value);
    if (count != (int)length) return false;
    *used += length;
    return true;
}

static bool tar_pax_record(xx_io_device *output, const tar_source *source)
{
    uint8_t header[TAR_BLOCK];
    char *payload = NULL, stamp[32], uid[16], gid[16];
    size_t length = 0U;
    bool success;
    /* POSIX pax values are UTF-8 unless hdrcharset says the bytes are as
     * the file system gave them: then readers keep the name's bytes. */
    (void)snprintf(stamp, sizeof(stamp), "%" PRId64, source->stamp);
    (void)snprintf(uid, sizeof(uid), "%lu", (unsigned long)source->uid);
    (void)snprintf(gid, sizeof(gid), "%lu", (unsigned long)source->gid);
    success = (tar_utf8(source->name) || tar_pax_add(&payload, &length, "hdrcharset", "BINARY")) && tar_pax_add(&payload, &length, "path", source->name) &&
              (tar_time_fits(source) || tar_pax_add(&payload, &length, "mtime", stamp)) && (source->uid <= TAR_MAX_ID || tar_pax_add(&payload, &length, "uid", uid)) &&
              (source->gid <= TAR_MAX_ID || tar_pax_add(&payload, &length, "gid", gid)) &&
              tar_header(header, TAR_USTAR, "PaxHeaders.0/path", length, source->mtime, 0644U, tar_header_id(source->uid), tar_header_id(source->gid), 'x') &&
              tar_write_all(output, header, TAR_BLOCK) && tar_write_all(output, payload, length) && tar_pad(output, length);
    free(payload);
    return success;
}

static bool tar_gnu_long_name(xx_io_device *output, const tar_source *source)
{
    uint8_t header[TAR_BLOCK];
    size_t size = strlen(source->name) + 1U;
    return tar_header(header, TAR_GNU, "././@LongLink", size, source->mtime, 0644U, source->uid, source->gid, 'L') && tar_write_all(output, header, TAR_BLOCK) &&
           tar_write_all(output, source->name, size) && tar_pad(output, size);
}

static bool tar_copy_member(const xfu_request *request, const tar_source *item, xx_io_device *output, xx_pd_struct *pd)
{
    uint8_t buffer[65536U];
    int64_t remaining = item->size;
    xx_io_device *input;
    bool success = true;
    if (item->directory) return true;
    if (!(input = xfu_native_open_member(item->member))) {
        char why[128];
        tar_log(request, "%s: %s", item->path, xfu_native_open_failure(errno, why, sizeof(why)));
        return false;
    }
    while (remaining > 0 && !tar_cancelled(request, pd)) {
        size_t wanted = remaining > (int64_t)sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        ssize_t count = xx_io_read(input, buffer, wanted);
        if (count <= 0 || (size_t)count > wanted) {
            tar_log(request, "%s: source changed or cannot be read", item->path);
            success = false;
            break;
        }
        if (!tar_write_all(output, buffer, (size_t)count)) {
            success = false;
            break;
        }
        remaining -= count;
    }
    if (xx_io_close(input) != 0) success = false;
    return success && remaining == 0 && !tar_cancelled(request, pd) && tar_pad(output, (uint64_t)item->size);
}

bool xfu_native_tar_write(const xfu_request *request, const char *format, const xfu_native_member *members, xx_io_device *output, xx_pd_struct *pd)
{
    static const uint8_t trailer[2U * TAR_BLOCK] = {0};
    tar_variant variant = tar_parse(format);
    tar_source *sources = NULL;
    size_t i;
    bool success = false;
    if (!request || !output || !pd || variant == TAR_UNKNOWN || (request->file_count && !members) || request->file_count > SIZE_MAX / sizeof(*sources)) return false;
    sources = (tar_source *)calloc(request->file_count ? request->file_count : 1U, sizeof(*sources));
    if (!sources) return false;
    for (i = 0; i < request->file_count; ++i) {
        sources[i].path = members[i].path;
        sources[i].member = &members[i];
        if (!sources[i].path || !tar_describe(&sources[i], &members[i])) {
            if (sources[i].path) tar_log(request, "%s: cannot describe the input for a %s member", sources[i].path, format);
            goto done;
        }
    }
    success = true;
    for (i = 0; i < request->file_count; ++i) {
        const tar_source *item = &sources[i];
        uint8_t header[TAR_BLOCK];
        const char *header_name = item->name;
        bool requires_extension = false, name_fits = true;
        size_t j;
        if (tar_cancelled(request, pd)) {
            success = false;
            break;
        }
        if (request->callbacks.progress) request->callbacks.progress(request->callbacks.user, i, request->file_count, item->path);
        if (variant == TAR_PAX) requires_extension = true;
        else if (variant == TAR_PAX_RESTRICTED) {
            uint8_t probe[TAR_BLOCK] = {0};
            name_fits = tar_split_ustar(item->name, probe);
            for (j = 0; item->name[j]; ++j)
                if ((unsigned char)item->name[j] >= 0x80U) name_fits = false;
            requires_extension = !name_fits || !tar_time_fits(item) || !tar_owner_fits(item);
        } else if (!tar_owner_fits(item)) {
            /* ustar, GNU and v7 headers: refused before any header of the
             * member (a GNU long name's too) is written. */
            tar_log(request,
                    "%s: its owner or group id is larger than a %s header holds; "
                    "--format=pax stores it",
                    item->path, format);
            success = false;
            break;
        } else if (!tar_time_fits(item)) {
            /* ustar, GNU and v7 headers: the nearest time they hold. */
            xfu_native_warn_time(request, item->path, format, item->stamp, (int64_t)item->mtime, true);
        }
        if (requires_extension) {
            if (!tar_pax_record(output, item)) {
                success = false;
                break;
            }
            /* A name the ustar header holds stays there for readers without
             * pax (when only the time needs the pax header). */
            if (variant == TAR_PAX || !name_fits) header_name = "PaxPath";
        } else if (variant == TAR_GNU && strlen(item->name) > 100U) {
            if (!tar_gnu_long_name(output, item)) {
                success = false;
                break;
            }
            header_name = "GNUPath";
        }
        if (!tar_header(header, variant, header_name, item->directory ? 0U : (uint64_t)item->size, item->mtime, item->mode,
                        requires_extension ? tar_header_id(item->uid) : item->uid, requires_extension ? tar_header_id(item->gid) : item->gid,
                        item->directory ? '5' : '0')) {
            /* A name or size the header has no room for: which. */
            if (!item->directory && (uint64_t)item->size > UINT64_C(077777777777))
                tar_log(request,
                        "%s: the file is 8 GiB or larger, which the %s writer cannot "
                        "store",
                        item->path, format);
            else
                tar_log(request,
                        "%s: the name does not fit a %s header (%s); --format=pax "
                        "stores it",
                        item->path, format, variant == TAR_V7 ? "100 bytes" : "100 bytes, or 256 split at a '/' into 155 and 100");
            success = false;
            break;
        }
        if (!tar_write_all(output, header, TAR_BLOCK) || !tar_copy_member(request, item, output, pd)) {
            success = false;
            break;
        }
        if (request->callbacks.progress) request->callbacks.progress(request->callbacks.user, i + 1U, request->file_count, NULL);
    }
    if (success && !tar_cancelled(request, pd)) success = tar_write_all(output, trailer, sizeof(trailer));
    else success = false;
done:
    for (i = 0; i < request->file_count; ++i) free(sources[i].name);
    free(sources);
    return success;
}
