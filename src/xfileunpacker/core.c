/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "core.h"
#include "metadata.h"
#include "native_reader_backend.h"
#include "native_writer_backend.h"
#if XFU_FORMAT_SUPPORT_FULL
#include "peazip_backend.h"
#include "pea_backend.h"
#include "freearc_backend.h"
#include "freearc_sfx_backend.h"
#include "split_backend.h"
#include "sfx_backend.h"
#include "upx_backend.h"
#endif

#include "xxfc_readers.h"
/* The readers whose member names the engine maps before the path guard
 * (mapped_extract_name below): the complete library has them all, a family
 * tool the ones of its own readers. Without the mapping a rooted TARX or
 * SHAR name, or an ARQ name with a DOS-reserved character, is refused as
 * unsafe, which the complete library does not do. */
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_CAB) || defined(XFU_SUPPORT_FORMAT_SFX_CAB) || \
    defined(XFU_SUPPORT_FORMAT_RESOURCEFORK) || defined(XFU_SUPPORT_FORMAT_ARQ) || \
    defined(XFU_SUPPORT_FORMAT_SFX_ARQ) || defined(XFU_SUPPORT_FORMAT_TARX1) || \
    defined(XFU_SUPPORT_FORMAT_TARX2) || defined(XFU_SUPPORT_FORMAT_CLAYLZ) || \
    defined(XFU_SUPPORT_FORMAT_LHA) || defined(XFU_SUPPORT_FORMAT_SFX_LHA) || \
    defined(XFU_SUPPORT_FORMAT_IS3) || defined(XFU_SUPPORT_FORMAT_NOTETAB) || \
    defined(XFU_SUPPORT_FORMAT_AMIGALZX) || defined(XFU_SUPPORT_FORMAT_SFX_LZX) || \
    defined(XFU_SUPPORT_FORMAT_SHAR) || defined(XFU_SUPPORT_FORMAT_BEOSPKG) || \
    defined(XFU_SUPPORT_FORMAT_IBMPACK) || defined(XFU_SUPPORT_FORMAT_QUANTUM)
#define XFU_MAPPED_NAMES 1
#else
#define XFU_MAPPED_NAMES 0
#endif
#if XFU_MAPPED_NAMES
#include "../formats/xx_archive_output_name.h"
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_RESOURCEFORK)
#include "../formats/executable/resourcefork/xx_resourcefork_output_name.h"
#endif
#if XFU_FORMAT_SUPPORT_FULL
#include "xxfclib/formats/installer/rtpatch_setup_data/xx_rtpatch_setup_data.h"
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <xxfclib/io/xx_io.h>
#include <xxfclib/strings/xx_string.h>
#if XFU_FORMAT_SUPPORT_FULL
#include <xxfclib/formats/executable/upx_engine/xx_upx_engine.h>
#include <xxfclib/formats/executable/molebox/xx_molebox.h>
#include <xxfclib/formats/archive/cpio/xx_cpio.h>
#include <xxfclib/formats/archive/tar/xx_tar.h>
#endif

#include <xxfclib/formats/archive/zip/xx_zip.h>
#if defined(XFU_SUPPORT_FORMAT_TAR) && !XFU_FORMAT_SUPPORT_FULL
#include <xxfclib/formats/archive/tar/xx_tar.h>
#endif
#if XFU_FORMAT_SUPPORT_FULL
#include <xxfclib/formats/archive/xorarchive/xx_xorarchive.h>
#include <xxfclib/formats/installer/lifkd/xx_lifkd.h>

#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define RECORD_LIMIT 1000000

/* The next pair of a ';'-separated option list, trimmed, or NULL. */
static const char *option_next(const char *at, size_t *length) {
    const char *end;
    while (*at == ';' || *at == ' ' || *at == '\t') ++at;
    if (!*at) return NULL;
    end = strchr(at, ';');
    *length = end ? (size_t)(end - at) : strlen(at);
    while (*length && (at[*length - 1] == ' ' || at[*length - 1] == '\t')) --*length;
    return at;
}

bool xfu_option_listed(const char *options, const char *pair) {
    size_t length, wanted = strlen(pair);
    const char *at;
    if (!options) return false;
    for (at = option_next(options, &length); at; at = option_next(at + length, &length))
        if (length == wanted && !memcmp(at, pair, wanted)) return true;
    return false;
}

const char *xfu_option_unknown(const char *options, const char *const *known,
                               size_t count, size_t *length) {
    const char *at;
    size_t i, current;
    if (!options) return NULL;
    for (at = option_next(options, &current); at; at = option_next(at + current, &current)) {
        for (i = 0; i < count; ++i)
            if (strlen(known[i]) == current && !memcmp(at, known[i], current)) break;
        if (i == count) { *length = current; return at; }
    }
    return NULL;
}

static void emit(const xfu_request *request, bool error, const char *format, ...) {
    va_list args, copy;
    char local[1024] = {0};
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
    request->callbacks.log(request->callbacks.user, error, line);
    if (line != local) free(line);
}

xfu_command xfu_parse_command(const char *text) {
    if (!text || !text[0] || text[1]) return XFU_COMMAND_NONE;
    switch (text[0]) {
        case 'x': case 'X': return XFU_COMMAND_EXTRACT;
        case 'l': case 'L': return XFU_COMMAND_LIST;
        case 't': case 'T': return XFU_COMMAND_TEST;
        case 'a': case 'A': return XFU_COMMAND_ADD;
        default: return XFU_COMMAND_NONE;
    }
}

static bool cancelled(const xfu_request *request, xx_pd_struct *pd) {
    if (request->callbacks.cancelled &&
        request->callbacks.cancelled(request->callbacks.user)) xx_pd_stop(pd);
    return xx_pd_is_stopped(pd);
}

static void progress(const xfu_request *request, uint64_t completed,
                     uint64_t total, const char *name) {
    if (request->callbacks.progress)
        request->callbacks.progress(request->callbacks.user, completed, total, name);
}

typedef struct test_monitor {
    const xfu_request *request;
    xx_pd_struct *progress;
    xx_pd_observer previous;
    uint64_t completed, total;
    unsigned percent;
    bool active;
    bool busy_before[XX_PD_LEVELS];
    /* Observer work runs at most every MONITOR_POLL_MS (see monitor_due);
     * a stop once seen stays. */
    uint64_t polled_at;
    bool stopped;
    /* The device a reader decodes one stream from, and the level that shows
     * how far into it it has read (see stream_attach). */
    xx_io_device *stream;
    int stream_level;
    uint64_t stream_seen;
    /* The passes over a stream that showed how far they read, so far, and
     * whether the current one has (see stream_label). */
    unsigned stream_passes;
    bool stream_shown;
    /* The levels that were busy when the stream was attached (the caller's
     * own): the others are the reader's, labelled with the pass too. */
    bool stream_before[XX_PD_LEVELS];
    /* Some decoders use cursor-preserving read_at calls, so their own byte
     * level also identifies a new pass when the input cursor cannot. */
    uint64_t decoder_seen[XX_PD_LEVELS];
    char decoder_status[XX_PD_LEVELS][sizeof(((xx_pd_record *)0)->status)];
    /* TEST: the current member's unpacked and packed sizes (-1: not
     * reported) and the size of what the reader reads, for a level that
     * decodes more than the member (see test_observe). */
    int64_t member_size, member_packed;
    uint64_t archive_size;
} test_monitor;

/* The label of a level for a pass over a stream after passes earlier ones in
 * the job: each pass starts its bytes again, so from the second one on the
 * label says which pass it is ("Reading archive, pass 2"). */
static void stream_label(xx_pd_record *record, unsigned passes) {
    if (passes)
        snprintf(record->status, sizeof(record->status), "Reading archive, pass %u", passes + 1U);
    else
        snprintf(record->status, sizeof(record->status), "Reading archive");
}

/* The same for a level of the reader's own in such a pass (a decoder's byte
 * count, "Unpacking Deflate, pass 2"), which starts again as well: its label
 * keeps its own text, cut at a whole character to make room. */
static void pass_label(xx_pd_record *record, unsigned passes) {
    char suffix[24];
    size_t length, room, size = sizeof(record->status);
    int count = snprintf(suffix, sizeof(suffix), ", pass %u", passes + 1U);
    if (count <= 0 || (size_t)count >= sizeof(suffix)) return;
    record->status[size - 1] = '\0';
    length = strlen(record->status);
    if (length >= (size_t)count && !strcmp(record->status + length - (size_t)count, suffix)) return;
    {
        /* One of an earlier pass gives way. */
        char *earlier = strstr(record->status, ", pass ");
        if (earlier && earlier[7] && strspn(earlier + 7, "0123456789") == strlen(earlier + 7))
            length = (size_t)(earlier - record->status);
    }
    room = size - 1 - (size_t)count;
    if (length > room) {
        length = room;
        while (length && ((unsigned char)record->status[length] & 0xC0) == 0x80) --length;
    }
    memcpy(record->status + length, suffix, (size_t)count + 1);
}

#define MONITOR_POLL_MS 10U

/* Milliseconds; without a monotonic clock, the processor time spent. It is
 * read on every observer call: a coarse clock, where there is one, costs
 * the least, and its ticks of a few milliseconds are fine enough. */
static uint64_t monitor_clock(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
#if defined(CLOCK_MONOTONIC_COARSE) || defined(CLOCK_MONOTONIC)
    struct timespec now;
#endif
#ifdef CLOCK_MONOTONIC_COARSE
    if (clock_gettime(CLOCK_MONOTONIC_COARSE, &now) == 0)
        return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
#endif
#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0)
        return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
#endif
    return (uint64_t)((double)clock() * 1000.0 / CLOCKS_PER_SEC);
#endif
}

/* Decoders notify the observer up to millions of times a second, and in the
 * GUI a Cancel poll takes the job's lock. The observer does its work about
 * every MONITOR_POLL_MS. The clock is read on every call: skipping calls
 * between reads would hold Cancel up for as many calls, and a decoder whose
 * calls turn sparse (bzip2 between its blocks) makes those seconds. */
static bool monitor_due(test_monitor *monitor) {
    uint64_t now = monitor_clock();
    if (monitor->polled_at && now - monitor->polled_at < MONITOR_POLL_MS) return false;
    monitor->polled_at = now;
    return true;
}

static void test_percent(test_monitor *monitor, unsigned percent) {
    if (percent > 100) percent = 100;
    if (percent <= monitor->percent) return;
    monitor->percent = percent;
    if (monitor->request->callbacks.test_progress)
        monitor->request->callbacks.test_progress(
            monitor->request->callbacks.user, percent);
}

/* Keep the caller's observer (GUI/TUI cancellation and progress) intact.
 * The first new decoder level measures work inside the current member;
 * existing levels belong to the caller's whole-archive monitor. */
static bool test_observe(const xx_pd_struct *pd, void *user) {
    test_monitor *monitor = (test_monitor *)user;
    bool stop = false;
    int i;
    if (monitor->stopped) return true;
    if (!monitor_due(monitor)) return false;
    /* Before the caller's observer, whose snapshot then shows this look. */
    if (monitor->stream && monitor->progress == pd) {
        /* The lower of two looks: a reader that reads an index at the end of
         * the stream first and then decodes it from the start is not done. */
        xx_pd_record *record = &monitor->progress->records[monitor->stream_level];
        int64_t at = xx_io_tell(monitor->stream);
        if (at >= 0) {
            uint64_t seen = (uint64_t)at < record->total ? (uint64_t)at : record->total;
            uint64_t current = seen < monitor->stream_seen ? seen : monitor->stream_seen;
            /* Back at the start after a shown part: the reader reads the
             * stream again (it validated it, now it decodes it). */
            if (monitor->stream_shown && current + record->total / 50U < record->current) {
                stream_label(record, ++monitor->stream_passes);
                monitor->stream_shown = false;
            }
            record->current = current;
            monitor->stream_seen = seen;
            if (current) monitor->stream_shown = true;
        }
        if (monitor->stream_passes)
            for (i = 0; i < XX_PD_LEVELS; ++i)
                if (i != monitor->stream_level && !monitor->stream_before[i] &&
                    monitor->progress->records[i].is_busy)
                    pass_label(&monitor->progress->records[i], monitor->stream_passes);
        for (i = 0; i < XX_PD_LEVELS; ++i) {
            xx_pd_record *decoder = &monitor->progress->records[i];
            if (i == monitor->stream_level || monitor->stream_before[i] || !decoder->is_busy) continue;
            if (!strcmp(monitor->decoder_status[i], decoder->status) &&
                monitor->decoder_seen[i] > (UINT64_C(1) << 20) &&
                decoder->current < monitor->decoder_seen[i] / 2U) {
                /* A changed pass label means the cursor already counted
                 * this rewind. Matching labels mean only the decoder saw
                 * it; count it before the caller sees a restarted figure. */
                stream_label(record, ++monitor->stream_passes);
                monitor->stream_shown = false;
                break;
            }
        }
        for (i = 0; i < XX_PD_LEVELS; ++i) {
            xx_pd_record *decoder = &monitor->progress->records[i];
            if (i == monitor->stream_level || monitor->stream_before[i] || !decoder->is_busy) continue;
            if (monitor->stream_passes) pass_label(decoder, monitor->stream_passes);
            snprintf(monitor->decoder_status[i], sizeof(monitor->decoder_status[i]), "%s", decoder->status);
            monitor->decoder_seen[i] = decoder->current;
        }
    }
    if (monitor->previous.progress == pd && monitor->previous.callback)
        stop = monitor->previous.callback(pd, monitor->previous.user_data);
    if (!stop && monitor->request->callbacks.cancelled &&
        monitor->request->callbacks.cancelled(monitor->request->callbacks.user))
        stop = true;
    if (stop) {
        monitor->stopped = true;
        if (monitor->progress == pd && !pd->is_stop) xx_pd_stop(monitor->progress);
        return true;
    }
    if (!pd->is_stop && monitor->active && monitor->total) {
        for (i = 0; i < XX_PD_LEVELS; ++i) {
            const xx_pd_record *record = &pd->records[i];
            if (!monitor->busy_before[i] && record->is_busy && record->total) {
                uint64_t current = record->current < record->total ?
                    record->current : record->total;
                uint64_t fraction = (uint64_t)(
                    (long double)current * 1000.0L / (long double)record->total);
                /* The members it covers, in thousandths: this one, or for a
                 * level that decodes much more than this member (a solid
                 * 7-Zip folder, whose later files then pass at once) the
                 * share of the members that its packed share of the archive
                 * suggests. */
                uint64_t span = 1000U;
                unsigned percent;
                if (monitor->member_size >= 0 && monitor->member_packed > 0 &&
                    monitor->archive_size && record->total / 2U > (uint64_t)monitor->member_size) {
                    uint64_t left = (monitor->total - monitor->completed) * 1000U;
                    long double share = (long double)monitor->member_packed /
                        (long double)monitor->archive_size;
                    uint64_t estimate = share >= 1.0L ? left :
                        (uint64_t)(share * (long double)monitor->total * 1000.0L);
                    if (estimate > left) estimate = left;
                    if (estimate > span) span = estimate;
                }
                percent = (unsigned)((monitor->completed * 1000U +
                    fraction * span / 1000U) * 100U / (monitor->total * 1000U));
                /* Member completion and end-of-archive validation publish 100. */
                test_percent(monitor, percent < 100 ? percent : 99);
                break;
            }
        }
    }
    return false;
}

/* A reader that decodes one stream from the start of device (a compressed
 * stream or TAR, an outer filter) shows how far it has read as a level of
 * the progress state: the byte progress of a read whose member count cannot
 * move. monitor is walk_observed's, NULL without a Cancel callback. Each
 * pass over the stream (a probe for an archive inside it, the reader that
 * validates it, then the one that decodes it) starts its bytes again: after
 * the first pass that showed some, the level says which pass it is, so that
 * the figure starting again within one job says why (stream_label). */
#if XFU_FORMAT_SUPPORT_FULL || XFU_FORMAT_SUPPORT_TAR_FILTERS
static void stream_attach(test_monitor *monitor, xx_io_device *device) {
    int64_t size;
    int i;
    if (!monitor || monitor->stream || !device) return;
    size = xx_io_total_size(device);
    if (size <= 0) return;
    for (i = 0; i < XX_PD_LEVELS; ++i)
        monitor->stream_before[i] = monitor->progress->records[i].is_busy;
    memset(monitor->decoder_seen, 0, sizeof(monitor->decoder_seen));
    memset(monitor->decoder_status, 0, sizeof(monitor->decoder_status));
    monitor->stream_level = xx_pd_enter_level(monitor->progress, (uint64_t)size,
                                              "Reading archive");
    monitor->stream_seen = 0;
    monitor->stream_shown = false;
    if (monitor->stream_level >= 0) {
        stream_label(&monitor->progress->records[monitor->stream_level],
                     monitor->stream_passes);
        monitor->stream = device;
    }
}

/* Before the device is closed or its members are counted instead. */
#endif

static void stream_detach(test_monitor *monitor) {
    if (!monitor || !monitor->stream) return;
    if (monitor->stream_shown) ++monitor->stream_passes;
    monitor->stream = NULL;
    xx_pd_leave_level(monitor->progress, monitor->stream_level);
}

static bool test_count_records(const xfu_request *request, Abstractformat *format,
                               xx_archive_record_state *state, xx_pd_struct *pd,
                               uint64_t *count) {
    *count = 0;
    while (xx_format_get_current_archive_record(format, state)) {
        if (cancelled(request, pd) || *count >= RECORD_LIMIT) return false;
        ++*count;
        if (!xx_format_archive_record_move_to_next(format, state, pd)) break;
    }
    return !cancelled(request, pd) && !pd->last_error;
}

static const char *record_name(const xx_archive_record *record, char **owned) {
    const char *name = xx_archive_record_get_original_name(record);
    const wchar_t *wide;
    *owned = NULL;
    if (name && name[0]) return name;
    wide = xx_archive_record_get_original_name_w(record);
    if (wide && wide[0]) {
        char *utf8 = xx_str_unicode_to_utf8(wide);
        if (utf8 && utf8[0]) {
            *owned = utf8;
            return utf8;
        }
        xx_str_free(utf8);
    }
    return "<unnamed>";
}

static void encryption_detected(const xfu_request *request, bool *reported,
                                 const char *member_name) {
    if (*reported) return;
    *reported = true;
    if (request->callbacks.encryption)
        request->callbacks.encryption(request->callbacks.user,
            XFU_ENCRYPTION_DETECTED, member_name);
}

static void encryption_blocked(const xfu_request *request, bool *reported,
                                bool *blocked, const char *member_name) {
    encryption_detected(request, reported, member_name);
    if (*blocked) return;
    *blocked = true;
    if (request->callbacks.encryption)
        request->callbacks.encryption(request->callbacks.user, request->password
            ? XFU_ENCRYPTION_PASSWORD_REJECTED : XFU_ENCRYPTION_PASSWORD_REQUIRED,
            member_name);
}

/* A member name that extraction can write below the output folder: relative,
 * without a ".." component or a control character. ':' is an ordinary
 * character on POSIX systems ("9:00-notes.txt"); on Windows it names a drive
 * or a file's stream, so a name with one is refused there. */
static bool safe_extract_name(const char *name) {
    const char *part, *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    part = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == '/' || ch == '\\' || ch == '\0') {
            size_t length = (size_t)(cursor - part);
            if (length == 2 && part[0] == '.' && part[1] == '.') return false;
            if (!ch) break;
            part = cursor + 1;
        } else if (ch < 32 || ch == 127) return false;
#ifdef _WIN32
        else if (ch == ':') return false;
#endif
    }
    return true;
}

/* A directory member that is the output folder itself ("/", "./", "."):
 * nothing to create below the output folder, and no escape from it. */
static bool names_output_folder(const char *name) {
    const char *part, *cursor;
    if (!name || !name[0]) return false;
    part = name;
    for (cursor = name;; ++cursor) {
        char ch = *cursor;
        if (ch == '/' || ch == '\\' || ch == '\0') {
            size_t length = (size_t)(cursor - part);
            if (length != 0 && !(length == 1 && part[0] == '.')) return false;
            if (!ch) return true;
            part = cursor + 1;
        }
    }
}

/* Match the selected reader's filesystem mapping before the generic guard.
 * This is deliberately a small allowlist: a ZIP with a rooted path, a drive
 * escape or traversal still has exactly the strict policy above. */
static char *mapped_extract_name(xx_file_type_t type, const char *name, size_t ordinal) {
    (void)ordinal;
    /* Each reader's case is compiled for the complete library and for that
     * reader's own family tool, so both map a member name the same way. */
    switch (type) {
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_RESOURCEFORK)
    case XX_FILE_TYPE_RESOURCEFORK:
        return rsrc_output_name(name, ordinal);
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_ARQ) || defined(XFU_SUPPORT_FORMAT_SFX_ARQ)
    case XX_FILE_TYPE_ARQ: case XX_FILE_TYPE_SFX_ARQ: {
        char *mapped;
        if (!name || name[0] == '/' || name[0] == '\\') return NULL;
        mapped = xx_str_dup(name);
        if (mapped) {
            char *p;
            for (p = mapped; *p; ++p)
                if (*p == ':' || *p == '<' || *p == '>' || *p == '"' || *p == '|' || *p == '?' || *p == '*') *p = '_';
        }
        return mapped;
    }
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_TARX1) || defined(XFU_SUPPORT_FORMAT_TARX2)
    case XX_FILE_TYPE_TARX1: case XX_FILE_TYPE_TARX2:
        return xx_archive_output_name(name, 0, XX_ARCHIVE_NAME_ROOTED | XX_ARCHIVE_NAME_DRIVE | XX_ARCHIVE_NAME_NATIVE_TAR);
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_CAB) || defined(XFU_SUPPORT_FORMAT_SFX_CAB)
    case XX_FILE_TYPE_CAB: case XX_FILE_TYPE_SFX_CAB:
        return xx_archive_output_name(name, 0, XX_ARCHIVE_NAME_ROOTED);
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_CLAYLZ) || defined(XFU_SUPPORT_FORMAT_LHA) || \
    defined(XFU_SUPPORT_FORMAT_SFX_LHA)
    case XX_FILE_TYPE_CLAYLZ: case XX_FILE_TYPE_LHA: case XX_FILE_TYPE_SFX_LHA:
        return xx_archive_output_name(name, 437, 0);
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_IS3) || defined(XFU_SUPPORT_FORMAT_NOTETAB)
    case XX_FILE_TYPE_IS3: case XX_FILE_TYPE_NOTETAB:
        return xx_archive_output_name(name, 1252, 0);
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_AMIGALZX) || defined(XFU_SUPPORT_FORMAT_SFX_LZX)
    case XX_FILE_TYPE_AMIGALZX: case XX_FILE_TYPE_SFX_LZX:
        return xx_archive_output_name(name, 28591, 0);
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_SHAR)
    case XX_FILE_TYPE_SHAR:
        return xx_archive_output_name(name, 0, 0);
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_BEOSPKG)
    case XX_FILE_TYPE_BEOSPKG:
        return xx_archive_output_name(name, 65001, 0);
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_IBMPACK)
    case XX_FILE_TYPE_IBMPACK:
        return xx_archive_output_name(name, 0, 0);
#endif
    default:
        return xx_str_dup(name);
    }
}

#if XFU_MAPPED_NAMES
static bool mapped_name_can_suffix(xx_file_type_t type) {
    return type == XX_FILE_TYPE_CAB || type == XX_FILE_TYPE_SFX_CAB ||
        type == XX_FILE_TYPE_CLAYLZ || type == XX_FILE_TYPE_LHA || type == XX_FILE_TYPE_SFX_LHA || type == XX_FILE_TYPE_IS3 ||
        type == XX_FILE_TYPE_NOTETAB || type == XX_FILE_TYPE_AMIGALZX ||
        type == XX_FILE_TYPE_SFX_LZX || type == XX_FILE_TYPE_SHAR || type == XX_FILE_TYPE_BEOSPKG || type == XX_FILE_TYPE_IBMPACK || type == XX_FILE_TYPE_QUANTUM;
}

#endif

/* Enough of what is at a path to tell that nothing replaced or changed it. */
typedef struct path_stamp {
    bool present;
    bool is_directory;
    uint64_t id[4];
} path_stamp;

static void stamp_path(const char *path, path_stamp *stamp) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data;
    wchar_t *wide = xx_str_utf8_to_unicode(path);
    memset(stamp, 0, sizeof(*stamp));
    if (wide && GetFileAttributesExW(wide, GetFileExInfoStandard, &data)) {
        stamp->present = true;
        stamp->is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        stamp->id[0] = data.dwFileAttributes;
        stamp->id[1] = (uint64_t)data.nFileSizeHigh << 32 | data.nFileSizeLow;
        stamp->id[2] = (uint64_t)data.ftLastWriteTime.dwHighDateTime << 32 |
                       data.ftLastWriteTime.dwLowDateTime;
        stamp->id[3] = (uint64_t)data.ftCreationTime.dwHighDateTime << 32 |
                       data.ftCreationTime.dwLowDateTime;
    }
    if (wide) xx_str_wfree(wide);
#else
    struct stat info;
    memset(stamp, 0, sizeof(*stamp));
    if (lstat(path, &info) == 0) {
        stamp->present = true;
        stamp->is_directory = S_ISDIR(info.st_mode);
        stamp->id[0] = (uint64_t)info.st_dev;
        stamp->id[1] = (uint64_t)info.st_ino;
        stamp->id[2] = (uint64_t)info.st_size;
        stamp->id[3] = (uint64_t)info.st_mtime ^ (uint64_t)info.st_ctime << 32;
    }
#endif
}

/* Where a member is written: below the output folder, at its name. With
 * split, a '\' in the name is a separator, as the ZIP, TAR, CPIO and 7z
 * readers make it everywhere (Windows does for every reader); the system
 * resolves the "." and empty components they drop. */
static char *member_destination(const char *unpack_to, const char *name, bool split) {
    size_t base = strlen(unpack_to), length = strlen(name), i;
    bool separator = base && unpack_to[base - 1] != '/' && unpack_to[base - 1] != '\\';
    char *path = (char *)malloc(base + length + 2);
    if (!path) return NULL;
    memcpy(path, unpack_to, base);
    if (separator) path[base++] = '/';
    memcpy(path + base, name, length + 1);
    for (i = base; split && path[i]; ++i)
        if (path[i] == '\\') path[i] = '/';
    return path;
}

static bool is_compressed_tar_type(xx_file_type_t type);

/* Whether the reader of type splits a member name at '\' (see
 * member_destination): what is at that path is what the member replaces. */
static bool splits_backslash(xx_file_type_t type) {
#ifdef _WIN32
    (void)type;
    return true;
#else
    return type == XX_FILE_TYPE_ZIP || type == XX_FILE_TYPE_ZIP64 || type == XX_FILE_TYPE_TAR ||
           type == XX_FILE_TYPE_CPIO || type == XX_FILE_TYPE_7ZIP || is_compressed_tar_type(type);
#endif
}

#if XFU_FORMAT_SUPPORT_FULL || defined(_WIN32)
static bool same_file(const char *left, const char *right);
#endif

/* Whether what is at destination (not what a link there names) is the file
 * of the archive being read: a member there would replace it. */
static bool is_archive_file(const char *destination, const char *archive) {
#ifdef _WIN32
    return same_file(destination, archive);
#else
    struct stat entry, file;
    return lstat(destination, &entry) == 0 && stat(archive, &file) == 0 &&
           entry.st_dev == file.st_dev && entry.st_ino == file.st_ino;
#endif
}

static void member_metadata(const xx_archive_record *record, xfu_entry *entry,
                            bool sevenzip_engine) {
    uint64_t date = xx_archive_record_get_meta_u64(record, XX_META_ID_LAST_MOD_DATE, UINT64_MAX);
    uint64_t clock = xx_archive_record_get_meta_u64(record, XX_META_ID_LAST_MOD_TIME, UINT64_MAX);
    uint64_t stamp = xx_archive_record_get_meta_u64(record, XX_META_ID_TIMESTAMP, UINT64_MAX);
    /* Engine and 7z records use the library's FILETIME timestamp convention. */
    if (sevenzip_engine && stamp != UINT64_MAX) {
        stamp = stamp >= UINT64_C(116444736000000000)
            ? stamp / UINT64_C(10000000) - UINT64_C(11644473600) : UINT64_MAX;
    }
    uint64_t attributes = xx_archive_record_get_meta_u64(record, XX_META_ID_ATTRIBUTES, UINT64_MAX);
    if (attributes == UINT64_MAX)
        attributes = xx_archive_record_get_meta_u64(record, XX_META_ID_EXTERNAL_ATTRS, UINT64_MAX);
    if (date <= UINT16_MAX && clock <= UINT16_MAX &&
        (date & 31) && ((date >> 5) & 15) >= 1 && ((date >> 5) & 15) <= 12 &&
        ((clock >> 11) & 31) < 24 && ((clock >> 5) & 63) < 60 && (clock & 31) < 30) {
        snprintf(entry->modified, sizeof(entry->modified), "%04u-%02u-%02u %02u:%02u:%02u",
            1980u + (unsigned)(date >> 9), (unsigned)((date >> 5) & 15), (unsigned)(date & 31),
            (unsigned)((clock >> 11) & 31), (unsigned)((clock >> 5) & 63), (unsigned)(clock & 31) * 2u);
    } else if (stamp <= INT64_MAX) {
        time_t value = (time_t)stamp;
        struct tm decoded;
        int valid = 0;
        if (value >= 0 && (uint64_t)value == stamp) {
#ifdef _WIN32
            valid = localtime_s(&decoded, &value) == 0;
#else
            valid = localtime_r(&value, &decoded) != NULL;
#endif
        }
        if (valid) strftime(entry->modified, sizeof(entry->modified), "%Y-%m-%d %H:%M:%S", &decoded);
    }
    if (attributes != UINT64_MAX)
        snprintf(entry->attributes, sizeof(entry->attributes), "0x%llX", (unsigned long long)attributes);
    else if (entry->is_directory) strcpy(entry->attributes, "D");
}

/* xx_zip reads a member's local header, and so the record's header size and
 * data offset, only on demand. These readers publish xx_zip's own records
 * (the format is the xx_zip), so resolve them before the fields are shown.
 * The reader's enum identity decides the cast, as in xxfc_is_incomplete. */
static void resolve_zip_record(const xxfc_opened *opened, const xx_archive_record *record) {
#ifdef XFU_SUPPORT_FORMAT_ZIP
    xx_file_type_t type;
    if (!opened->format || !record || record->data_offset >= 0) return;
    type = opened->format->file_type != XX_FILE_TYPE_UNKNOWN ? opened->format->file_type : opened->type;
#if XFU_FORMAT_SUPPORT_FULL
    if (type == XX_FILE_TYPE_XORARCHIVE) {
        if (((const xx_xorarchive *)opened->format)->family != XX_XORARCHIVE_FAMILY_ZIP) return;
    } else if (type != XX_FILE_TYPE_ZIP && type != XX_FILE_TYPE_ZIP64 && type != XX_FILE_TYPE_JAR &&
               type != XX_FILE_TYPE_APK && type != XX_FILE_TYPE_APKS && type != XX_FILE_TYPE_IPA &&
               type != XX_FILE_TYPE_ZIP_PSC && type != XX_FILE_TYPE_CRX) {
        return;
    }
#else
    if (type != XX_FILE_TYPE_ZIP && type != XX_FILE_TYPE_ZIP64) return;
#endif
    (void)xx_zip_resolve_record_data_offset((xx_zip *)opened->format, (xx_archive_record *)record);
#else
    (void)opened; (void)record;
#endif
}

static bool is_compressed_tar_type(xx_file_type_t type) {
    switch (type) {
    case XX_FILE_TYPE_TAR_GZ: case XX_FILE_TYPE_TAR_BZ2:
    case XX_FILE_TYPE_TAR_XZ: case XX_FILE_TYPE_TAR_LZMA:
    case XX_FILE_TYPE_TAR_LZIP: case XX_FILE_TYPE_TAR_LZ4:
    case XX_FILE_TYPE_TAR_LZOP: case XX_FILE_TYPE_TAR_ZSTD:
    case XX_FILE_TYPE_TAR_COMPRESS:
        return true;
    default:
        return false;
    }
}

/* Readers that decode one compressed stream from the start of the file: how
 * far they have read is how far they are. */
#if XFU_FORMAT_SUPPORT_FULL
static bool is_stream_type(xx_file_type_t type) {
    switch (type) {
    case XX_FILE_TYPE_GZ: case XX_FILE_TYPE_BZ2: case XX_FILE_TYPE_XZ:
    case XX_FILE_TYPE_LZMA: case XX_FILE_TYPE_LZIP: case XX_FILE_TYPE_LZ4:
    case XX_FILE_TYPE_LZOP: case XX_FILE_TYPE_ZSTD: case XX_FILE_TYPE_UNIX_COMPRESS:
        return true;
    default:
        return is_compressed_tar_type(type);
    }
}

/* The compressed stream of a compressed TAR type (TAR.ZST holds ZSTD), or
 * the type itself. */
static xx_file_type_t stream_of(xx_file_type_t type) {
    switch (type) {
    case XX_FILE_TYPE_TAR_GZ: return XX_FILE_TYPE_GZ;
    case XX_FILE_TYPE_TAR_BZ2: return XX_FILE_TYPE_BZ2;
    case XX_FILE_TYPE_TAR_XZ: return XX_FILE_TYPE_XZ;
    case XX_FILE_TYPE_TAR_LZMA: return XX_FILE_TYPE_LZMA;
    case XX_FILE_TYPE_TAR_LZIP: return XX_FILE_TYPE_LZIP;
    case XX_FILE_TYPE_TAR_LZ4: return XX_FILE_TYPE_LZ4;
    case XX_FILE_TYPE_TAR_LZOP: return XX_FILE_TYPE_LZOP;
    case XX_FILE_TYPE_TAR_ZSTD: return XX_FILE_TYPE_ZSTD;
    case XX_FILE_TYPE_TAR_COMPRESS: return XX_FILE_TYPE_UNIX_COMPRESS;
    default: return type;
    }
}

/* A file the detectors take for Binary that starts like a compressed stream
 * (their frame checks failed: a damaged or cut stream), or UNKNOWN. LZMA
 * has no signature: its header must look like one and the name say so. */
static xx_file_type_t damaged_stream_type(xx_io_device *device, const char *name) {
    static const struct {
        unsigned char magic[6];
        size_t length;
        xx_file_type_t type;
    } streams[] = {
        { { 0x1F, 0x8B, 0x08 }, 3, XX_FILE_TYPE_GZ },
        { { 'B', 'Z', 'h' }, 3, XX_FILE_TYPE_BZ2 },
        { { 0xFD, '7', 'z', 'X', 'Z', 0x00 }, 6, XX_FILE_TYPE_XZ },
        { { 0x28, 0xB5, 0x2F, 0xFD }, 4, XX_FILE_TYPE_ZSTD },
        { { 0x04, 0x22, 0x4D, 0x18 }, 4, XX_FILE_TYPE_LZ4 },
        { { 0x02, 0x21, 0x4C, 0x18 }, 4, XX_FILE_TYPE_LZ4 },
        { { 'L', 'Z', 'I', 'P' }, 4, XX_FILE_TYPE_LZIP },
        { { 0x89, 'L', 'Z', 'O', 0x00 }, 5, XX_FILE_TYPE_LZOP },
        { { 0x1F, 0x9D }, 2, XX_FILE_TYPE_UNIX_COMPRESS }
    };
    unsigned char head[13];
    int64_t size = xx_io_total_size(device);
    size_t length = size > 0 && size < (int64_t)sizeof(head) ? (size_t)size : sizeof(head), i;
    uint32_t dictionary, rounded;
    if (size <= 0 || !xx_io_read_at(device, 0, head, length)) return XX_FILE_TYPE_UNKNOWN;
    for (i = 0; i < sizeof(streams) / sizeof(streams[0]); ++i)
        if (length >= streams[i].length && !memcmp(head, streams[i].magic, streams[i].length) &&
            (streams[i].type != XX_FILE_TYPE_BZ2 || (length > 3 && head[3] >= '1' && head[3] <= '9')))
            return streams[i].type;
    if (length < sizeof(head) || head[0] >= 225 ||
        stream_of(xx_format_get_file_type_extension(name)) != XX_FILE_TYPE_LZMA)
        return XX_FILE_TYPE_UNKNOWN;
    /* As xz-utils accepts: 2^n or 2^n + 2^(n-1) bytes, or unset. */
    dictionary = (uint32_t)head[1] | (uint32_t)head[2] << 8 |
                 (uint32_t)head[3] << 16 | (uint32_t)head[4] << 24;
    rounded = dictionary - 1U;
    rounded |= rounded >> 2; rounded |= rounded >> 3; rounded |= rounded >> 4;
    rounded |= rounded >> 8; rounded |= rounded >> 16;
    return dictionary == UINT32_MAX || rounded + 1U == dictionary
        ? XX_FILE_TYPE_LZMA : XX_FILE_TYPE_UNKNOWN;
}

/* The name of the stream that a filter layer holds, for the extension hints
 * of the read inside it: name without its last suffix when that suffix names
 * the filter (x.tgz.gz holds x.tgz), else without a suffix, so that the read
 * inside goes by content alone. NULL when out of memory. */
static char *layer_name(const char *name, xx_file_type_t filter) {
    const char *base = name, *dot = NULL, *cursor;
    size_t length = 0;
    char *result;
    for (cursor = name; *cursor; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') { base = cursor + 1; dot = NULL; }
        else if (*cursor == '.') dot = cursor;
    }
    if (dot && dot != base && stream_of(xx_format_get_file_type_extension(dot)) == filter)
        length = (size_t)(dot - name);
    result = (char *)malloc(length + 1);
    if (!result) return NULL;
    memcpy(result, name, length);
    result[length] = '\0';
    return result;
}

/* Whether the TAR and CPIO readers refuse name in Test as well, without
 * reading the member's data, for what safe_extract_name refuses too: a
 * control character, a ".." component or, on Windows, ':'. They make an
 * absolute name ("/x", and on Windows "C:\x") relative first, so its leading
 * separators and drive are no reason: such a member fails for its data. */
#endif

static bool name_refused_in_test(xx_file_type_t type, const char *name) {
    if (type != XX_FILE_TYPE_TAR && type != XX_FILE_TYPE_CPIO && !is_compressed_tar_type(type))
        return false;
    if (!name) return true;
    for (;;) {
        if (name[0] == '/' || name[0] == '\\') ++name;
#ifdef _WIN32
        else if (((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z')) &&
                 name[1] == ':') name += 2;
#endif
        else break;
    }
    return !safe_extract_name(name);
}

/* Whether a TAR or CPIO member that is no folder has a name that ends in a
 * separator: their readers fail such a file instead of making it a folder. */
static bool file_named_as_folder(xx_file_type_t type, const xfu_entry *entry) {
    size_t length = strlen(entry->name);
    return !entry->is_directory && length &&
           (entry->name[length - 1] == '/' || entry->name[length - 1] == '\\') &&
           (type == XX_FILE_TYPE_TAR || type == XX_FILE_TYPE_CPIO || is_compressed_tar_type(type));
}

/* A failed filesystem change (xx_io_extract_errno) as a member's reason. */
static const char *change_failure(int error) {
    switch (error) {
    case EEXIST: return "already exists; not replaced";
#ifdef ELOOP
    case ELOOP: return "a link is in the way";
#endif
    case ENOTDIR: return "a file is in the way";
    case EISDIR: return "a folder is in the way";
    case ENAMETOOLONG: return "path too long";
    case EACCES: case EPERM: return "permission denied";
    case ENOSPC: return "no space left on the device";
#ifdef EDQUOT
    case EDQUOT: return "disk quota exceeded";
#endif
    case EROFS: return "read-only file system";
    case EFBIG: return "file too large";
    case XX_IO_EXTRACT_TEMP_FAILED: return "no room for a temporary copy";
    default: return strerror(error);
    }
}

/* Why a member failed, for its "-- FAILED" line, or NULL when nothing tells
 * (a defect of its data): a name the TAR and CPIO readers refuse in Test as
 * well; in Extract, a filesystem change that failed while it was written, or
 * a file already at its path (before: as it was) that the reader left alone
 * without opening a file to write; in Test, a temporary stream that Test's
 * memory limit refused (over_memory); else that it is encrypted. */
static const char *failure_reason(const xfu_request *request, xx_file_type_t type,
                                  const char *name, bool encrypted,
                                  const char *destination, const path_stamp *before,
                                  bool over_memory, bool *password_failure) {
    *password_failure = false;
    if (request->command == XFU_COMMAND_TEST) {
        if (name_refused_in_test(type, name)) return "unsafe member path";
        if (over_memory) return "too large to test in memory";
    } else {
        int error = xx_io_extract_errno();
        /* Win32 reports ERROR_ACCESS_DENIED when CreateFileW targets an
         * existing directory.  The pre-write stamp distinguishes that case
         * from an actual permissions failure. */
        if ((error == EACCES || error == EPERM) && before->is_directory)
            return change_failure(EISDIR);
        if (error) return change_failure(error);
        /* Encrypted readers can reject a password or ciphertext before
         * opening the destination. An unchanged existing file alone is not
         * evidence of an overwrite refusal in that case. Actual filesystem
         * errors above still take precedence over encryption notifications. */
        if (!encrypted && destination && before->present && !xx_io_extract_files()) {
            path_stamp after;
            stamp_path(destination, &after);
            if (after.present && !memcmp(before->id, after.id, sizeof(after.id)))
                return "already exists; not replaced";
        }
    }
    if (encrypted) {
        *password_failure = true;
        return request->password ? "wrong password, or damaged encrypted data" :
            "encrypted; a password is needed";
    }
    return NULL;
}

/* The type a damaged compressed stream of type is named as: the compressed
 * TAR that hint's extension says it holds (a cut .tar.xz), or type. */
#if XFU_FORMAT_SUPPORT_FULL
static xx_file_type_t damaged_name(xx_file_type_t type, const char *hint) {
    if (!is_compressed_tar_type(type)) {
        xx_file_type_t extension = xx_format_get_file_type_extension(hint);
        if (is_compressed_tar_type(extension) && stream_of(extension) == type) return extension;
    }
    return type;
}

/* A compressed stream of type, or a compressed TAR, that could not be read:
 * one wording, with the type's display name (damaged_name). */
static void damaged_stream(const xfu_request *request, xx_file_type_t type,
                           const char *hint) {
    xx_file_type_t named = damaged_name(type, hint);
    emit(request, true, is_compressed_tar_type(named)
         ? "%s: %s could not be read; the compressed stream or the TAR inside is damaged"
         : "%s: %s could not be read; the compressed stream is damaged",
         request->archive_path, xx_format_file_type_to_string(named));
}

/* Binary, or a recognised format without members: not an archive. */
#endif

static void not_archive(const xfu_request *request, xx_file_type_t type) {
    if (request->callbacks.not_archive)
        request->callbacks.not_archive(request->callbacks.user, type);
    emit(request, true, "%s: %s cannot be opened as an archive", request->archive_path,
         type == XX_FILE_TYPE_BINARY ? "Binary" : xx_format_file_type_to_string(type));
}

/* The interpretations of the layers a nested read has decoded (an envelope,
 * an outer compressed stream), outermost first and Binary at the start. */
typedef struct outer_layers {
    xx_file_type_t types[XX_FILE_TYPE_CHAIN_MAX];
    size_t count;
    /* The innermost layer's own name: what its extension says about the
     * stream decoded, not the archive file's (see layer_name). */
    const char *name;
} outer_layers;

/* The outer layers, then the chain read inside them without its Binary and
 * the types already listed. When that is too long the innermost layers stay,
 * next to Binary. */
static void layers_append(outer_layers *result, const outer_layers *outer,
                          const xx_file_type_t *types, size_t count) {
    xx_file_type_t merged[2 * XX_FILE_TYPE_CHAIN_MAX];
    size_t total = 0, i, j;
    if (outer)
        for (i = 0; i < outer->count; ++i) merged[total++] = outer->types[i];
    for (i = 0; i < count && i < XX_FILE_TYPE_CHAIN_MAX; ++i) {
        for (j = 0; j < total && merged[j] != types[i]; ++j) {}
        if (j == total) merged[total++] = types[i];
    }
    result->count = 0;
    for (i = 0; i < total; ++i)
        if (!i || total - i < XX_FILE_TYPE_CHAIN_MAX)
            result->types[result->count++] = merged[i];
}

/* The file_types callback for a chain read inside the outer layers, if any.
 * A pick from the merged list reads the file through those layers again. */
static bool handle_append(xx_list_t *handles, xx_handle_type_t handle) {
    size_t i;
    if (handle == XX_HANDLE_TYPE_UNKNOWN) return true;
    for (i = 0; i < handles->count; ++i)
        if (*(const xx_handle_type_t *)xx_list_at(handles, i) == handle) return true;
    return xx_list_append(handles, &handle);
}

static bool handle_contains(const xx_list_t *handles, xx_handle_type_t handle) {
    size_t i;
    if (!handles || handle == XX_HANDLE_TYPE_UNKNOWN) return false;
    for (i = 0; i < handles->count; ++i)
        if (*(const xx_handle_type_t *)xx_list_at(handles, i) == handle) return true;
    return false;
}

static bool handle_matches_device(xx_io_device *device, xx_handle_type_t handle) {
    xx_list_t *handles;
    bool matches;
    if (handle == XX_HANDLE_TYPE_UNKNOWN) return true;
    handles = xx_format_get_unpack_handle_types_device(device, true);
    matches = handle_contains(handles, handle);
    xx_list_destroy(handles);
    return matches;
}

static void report_types_with_handle(const xfu_request *request, const outer_layers *outer,
                                     const xx_file_type_t *types, size_t count,
                                     xx_file_type_t selected, xx_io_device *device,
                                     xx_handle_type_t proven_handle) {
    outer_layers merged;
    xx_list_t handles;
    xx_list_t *detected = NULL;
    xx_handle_type_t selected_handle = XX_HANDLE_TYPE_UNKNOWN;
    size_t i;
    if (request->callbacks.file_types) {
        if (!outer || !outer->count)
            request->callbacks.file_types(request->callbacks.user, types, count, selected);
        else {
            layers_append(&merged, outer, types, count);
            request->callbacks.file_types(request->callbacks.user, merged.types, merged.count,
                                          selected);
        }
    }
    if (!request->callbacks.unpack_handle_types ||
        !xx_list_init(&handles, sizeof(xx_handle_type_t), NULL)) return;
    if (outer)
        for (i = 0; i < outer->count; ++i)
            handle_append(&handles, xx_format_handle_type_from_file_type(outer->types[i]));
    if (device) detected = xx_format_get_unpack_handle_types_device(device, true);
    if (detected && detected->count) {
        for (i = 0; i < detected->count; ++i)
            handle_append(&handles, *(const xx_handle_type_t *)xx_list_at(detected, i));
    } else if (!device) {
        /* The caller has already proved a decoded envelope or damaged
         * stream interpretation. An ordinary file with no actionable device
         * detections must never acquire handles from its inspection chain. */
        for (i = 0; i < count; ++i)
            handle_append(&handles, xx_format_handle_type_from_file_type(types[i]));
    }
    /* A password-dependent archive may only become identifiable once its
     * reader has validated the supplied password and parsed its members. */
    if (!detected || !detected->count) handle_append(&handles, proven_handle);
    for (i = 0; i < handles.count; ++i) {
        xx_handle_type_t candidate = *(const xx_handle_type_t *)xx_list_at(&handles, i);
        if (xx_format_handle_type_file_type(candidate) == selected) selected_handle = candidate;
    }
    if (request->handle_type != XX_HANDLE_TYPE_UNKNOWN &&
        handle_contains(&handles, request->handle_type)) selected_handle = request->handle_type;
    request->callbacks.unpack_handle_types(request->callbacks.user,
        (const xx_handle_type_t *)handles.data, handles.count, selected_handle);
    xx_list_destroy(detected);
    xx_list_cleanup(&handles);
}

static void report_types(const xfu_request *request, const outer_layers *outer,
                         const xx_file_type_t *types, size_t count,
                         xx_file_type_t selected, xx_io_device *device) {
    report_types_with_handle(request, outer, types, count, selected, device,
                             XX_HANDLE_TYPE_UNKNOWN);
}

/* outer plus one more layer read as type: its chain up to and including
 * stop (the outer compressed stream), or all of it. */
#if XFU_FORMAT_SUPPORT_FULL
static void layers_add(outer_layers *result, const outer_layers *outer,
                       xx_file_type_t type, xx_file_type_t stop) {
    xx_file_type_t chain[XX_FILE_TYPE_CHAIN_MAX];
    size_t count = xx_format_get_file_type_chain(type, chain, XX_FILE_TYPE_CHAIN_MAX), i;
    for (i = 0; i < count && chain[i] != stop; ++i) {}
    if (i < count) count = i + 1;
    else if (stop != XX_FILE_TYPE_UNKNOWN)
        count = xx_format_get_file_type_chain(stop, chain, XX_FILE_TYPE_CHAIN_MAX);
    layers_append(result, outer, chain, count);
}

/* A layer that does not fit what a decoded nested layer may hold (4 GiB,
 * XFU_NATIVE_INNER_MAX_BYTES; in TEST the RAM it may use): the archive in
 * it, layer (UNKNOWN when not known), is not opened, and the envelope or
 * filter around it is read as one file instead. */
static void layer_too_large(const xfu_request *request, xx_file_type_t layer,
                            xx_file_type_t around, bool envelope) {
    const char *path = request->archive_path;
    const char *inside = layer == XX_FILE_TYPE_UNKNOWN ? "archive" :
        xx_format_file_type_to_string(layer);
    const char *holder = xx_format_file_type_to_string(around);
    if (request->command == XFU_COMMAND_TEST && envelope)
        emit(request, true, "%s: the %s payload is larger than the 256 MiB that Test keeps in "
             "memory; an archive in it was not tested, only the payload as one file", path, holder);
    else if (request->command == XFU_COMMAND_TEST)
        emit(request, true, "%s: the %s inside is larger than the 256 MiB that Test keeps in "
             "memory; it was not tested, only the %s stream around it", path, inside, holder);
    else if (envelope)
        emit(request, true, "%s: the %s payload is larger than the 4 GiB a nested layer may "
             "hold; an archive in it was not opened, and the payload is read as one file",
             path, holder);
    else
        emit(request, true, "%s: the %s inside is larger than the 4 GiB a nested layer may "
             "hold; it was not opened, and the %s stream is read as one file", path, inside,
             holder);
}

/* The same for a layer with no room for its temporary copy (the temporary
 * folder is full, say): the cause is said, never a damaged stream. */
static void layer_no_room(const xfu_request *request, xx_file_type_t layer,
                          xx_file_type_t around, bool envelope) {
    const char *inside = layer == XX_FILE_TYPE_UNKNOWN ? "archive" :
        xx_format_file_type_to_string(layer);
    const char *holder = xx_format_file_type_to_string(around);
    if (envelope)
        emit(request, true, "%s: no space for a temporary copy of the %s payload; an archive "
             "in it was not opened, and the payload is read as one file",
             request->archive_path, holder);
    else
        emit(request, true, "%s: no space for a temporary copy of the %s inside; it was not "
             "opened, and the %s stream is read as one file", request->archive_path, inside,
             holder);
}

/* A complete executable LHA container can itself be named .001. Validate
 * its native structure before applying the filename-only split fallback.
 * Explicit split selections and incomplete first volumes keep their meaning. */
static bool numbered_lha_sfx(const char *path, xx_io_device *borrowed,
                             xx_pd_struct *pd) {
    xx_io_device *source = borrowed ? borrowed : xx_io_file_open(path, "rb");
    xxfc_opened probe = {0};
    unsigned char magic[2];
    int64_t saved;
    bool valid = false;
    if (!source) return false;
    saved = xx_io_tell(source);
    if (saved >= 0 && xx_io_seek64(source, 0, SEEK_SET) == 0 &&
        xx_io_read(source, magic, sizeof(magic)) == (ssize_t)sizeof(magic) &&
        magic[0] == 'M' && magic[1] == 'Z' &&
        xxfc_open_named(&probe, source, 0, "sfx_lha") &&
        xx_format_is_valid(probe.format, pd) &&
        xx_format_handle_base_info(probe.format, pd) &&
        probe.format->number_of_archive_records > 0 &&
        !xxfc_is_incomplete(&probe)) valid = true;
    xxfc_close(&probe);
    if (borrowed) {
        if (saved >= 0) (void)xx_io_seek64(source, saved, SEEK_SET);
    } else xx_io_close(source);
    return valid;
}

/* A complete ToolBook archive uses the same .001 suffix as a raw split.
 * Require both its fixed header and its entire declared directory before
 * preferring the native archive reader over the filename-only wrapper. */
static bool numbered_asymetrix(const char *path, xx_io_device *borrowed,
                              xx_pd_struct *pd) {
    static const unsigned char signature[8] = {0x60,0x22,0x13,0x63,0x6c,0,0,0};
    unsigned char header[44];
    xx_io_device *source = borrowed ? borrowed : xx_io_file_open(path, "rb");
    xxfc_opened probe = {0};
    int64_t saved;
    bool valid = false;
    if (!source) return false;
    saved = xx_io_tell(source);
    if (saved >= 0 && xx_io_read_at(source, 0, header, sizeof(header)) &&
        !memcmp(header, signature, sizeof(signature)) && header[40] == 1 && !header[41] &&
        (header[42] || header[43]) &&
        xxfc_open_named(&probe, source, 0, "asymetrix") &&
        xx_format_is_valid(probe.format, pd) &&
        xx_format_handle_base_info(probe.format, pd) &&
        probe.format->number_of_archive_records == (uint64_t)header[42] + 256U * header[43]) valid = true;
    xxfc_close(&probe);
    if (borrowed) {
        if (saved >= 0) (void)xx_io_seek64(source, saved, SEEK_SET);
    } else xx_io_close(source);
    return valid;
}

/* GTU kits commonly use .001 but are self-contained indexed archives.
 * The size word is only a cheap prefilter; require the complete native
 * index/block/frame validation before overriding a filename-only split. */
static bool numbered_gtu(const char *path, xx_io_device *borrowed,
                         xx_pd_struct *pd) {
    unsigned char header[16];
    xx_io_device *source = borrowed ? borrowed : xx_io_file_open(path, "rb");
    xxfc_opened probe = {0};
    int64_t saved, physical;
    uint32_t stored;
    bool valid = false;
    if (!source) return false;
    saved = xx_io_tell(source);
    physical = xx_io_total_size(source);
    if (saved >= 0 && physical >= 16 && (uint64_t)physical <= UINT32_MAX &&
        xx_io_read_at(source, 0, header, sizeof(header))) {
        stored = (uint32_t)header[0] | ((uint32_t)header[1] << 8U) |
                 ((uint32_t)header[2] << 16U) | ((uint32_t)header[3] << 24U);
        if ((uint64_t)stored == (uint64_t)physical && header[8] == 1U && !header[9] &&
            header[10] <= 1U && !header[11] && !header[12] && !header[13] &&
            (header[14] || header[15]) && xxfc_open_named(&probe, source, 0, "gtu") &&
            xx_format_is_valid(probe.format, pd) && xx_format_handle_base_info(probe.format, pd) &&
            probe.format->number_of_archive_records > 0 && probe.format->format_size == physical &&
            !xxfc_is_incomplete(&probe)) valid = true;
    }
    xxfc_close(&probe);
    if (borrowed) {
        if (saved >= 0) (void)xx_io_seek64(source, saved, SEEK_SET);
    } else xx_io_close(source);
    return valid;
}

/* IBM PACK installation kits use numbered suffixes for independent chained
 * archives. The signature schedules validation; the full native chain and
 * physical extent must fit before replacing the suffix-only split fallback. */
static bool numbered_ibmpack(const char *path, xx_io_device *borrowed,
                             xx_pd_struct *pd) {
    unsigned char h[4];
    xx_io_device *source = borrowed ? borrowed : xx_io_file_open(path, "rb");
    xxfc_opened probe = {0};
    int64_t saved, physical;
    bool valid = false;
    if (!source) return false;
    saved = xx_io_tell(source);
    physical = xx_io_total_size(source);
    if (saved >= 0 && physical >= 10 && xx_io_read_at(source, 0, h, sizeof(h)) &&
        h[0] == 0xa5 && h[1] == 0x96 &&
        ((h[2] == 0xfe && h[3] == 0xff) || (h[2] == 0xff && h[3] == 0xff) ||
         (h[2] == 0x14 && h[3] == 0x0a) || (h[2] == 0 && h[3] == 0x14)) &&
        xxfc_open_named(&probe, source, 0, "ibmpack") &&
        xx_format_is_valid(probe.format, pd) && xx_format_handle_base_info(probe.format, pd) &&
        probe.format->number_of_archive_records > 0 && probe.format->format_size == physical &&
        !xxfc_is_incomplete(&probe)) valid = true;
    xxfc_close(&probe);
    if (borrowed) {
        if (saved >= 0) (void)xx_io_seek64(source, saved, SEEK_SET);
    } else xx_io_close(source);
    return valid;
}

/* Knowledge Dynamics libraries also use numbered suffixes. A valid first
 * member merely schedules a probe: the entire chain must end at physical EOF
 * before taking precedence over the filename-only split interpretation. */
static bool numbered_lifkd(const char *path, xx_io_device *borrowed,
                           xx_pd_struct *pd) {
    unsigned char h[54];
    xx_io_device *source = borrowed ? borrowed : xx_io_file_open(path, "rb");
    xxfc_opened probe = {0};
    int64_t saved, physical;
    bool valid = false;
    if (!source) return false;
    saved = xx_io_tell(source);
    physical = xx_io_total_size(source);
    if (saved >= 0 && physical >= (int64_t)sizeof(h) &&
        xx_io_read_at(source, 0, h, sizeof(h)) && xx_lifkd_is_member_header(h, sizeof(h)) &&
        xxfc_open_named(&probe, source, 0, "lifkd") &&
        xx_format_is_valid(probe.format, pd) && xx_format_handle_base_info(probe.format, pd) &&
        probe.format->number_of_archive_records > 0 && probe.format->format_size == physical &&
        !xxfc_is_incomplete(&probe)) valid = true;
    xxfc_close(&probe);
    if (borrowed) {
        if (saved >= 0) (void)xx_io_seek64(source, saved, SEEK_SET);
    } else xx_io_close(source);
    return valid;
}

/* Novell Packed File streams can retain a .001 member suffix. Require the
 * native header and complete reader validation before overriding raw split. */
static bool numbered_netwarepacked(const char *path, xx_io_device *borrowed,
                                   xx_pd_struct *pd) {
    unsigned char h[31];
    xx_io_device *source = borrowed ? borrowed : xx_io_file_open(path, "rb");
    xxfc_opened probe = {0};
    int64_t saved, physical;
    bool valid = false;
    if (!source) return false;
    saved = xx_io_tell(source);
    physical = xx_io_total_size(source);
    if (saved >= 0 && physical > (int64_t)sizeof(h) && xx_io_read_at(source, 0, h, sizeof(h)) &&
        !memcmp(h, "Packed File ", 12U) && h[24] == 0x1aU && h[25] == 1U && h[26] == 10U &&
        xxfc_open_named(&probe, source, 0, "netwarepacked") &&
        xx_format_is_valid(probe.format, pd) && xx_format_handle_base_info(probe.format, pd) &&
        probe.format->number_of_archive_records == 1 && probe.format->format_size == physical &&
        !xxfc_is_incomplete(&probe)) valid = true;
    xxfc_close(&probe);
    if (borrowed) {
        if (saved >= 0) (void)xx_io_seek64(source, saved, SEEK_SET);
    } else xx_io_close(source);
    return valid;
}

/* Later NetWare record chains also retain .001 suffixes. The first tagged
 * record is a scheduling hint; the complete exact-fit chain is decisive. */
static bool numbered_netware2(const char *path, xx_io_device *borrowed,
                              xx_pd_struct *pd) {
    unsigned char h[20];
    xx_io_device *source = borrowed ? borrowed : xx_io_file_open(path, "rb");
    xxfc_opened probe = {0};
    int64_t saved, physical;
    bool valid = false;
    if (!source) return false;
    saved = xx_io_tell(source);
    physical = xx_io_total_size(source);
    if (saved >= 0 && physical > 35 && xx_io_read_at(source, 0, h, sizeof(h)) &&
        !memcmp(h, "\x23\0\0\0\x10" "NetWareFileInfo", sizeof(h)) &&
        xxfc_open_named(&probe, source, 0, "netware2") &&
        xx_format_is_valid(probe.format, pd) && xx_format_handle_base_info(probe.format, pd) &&
        probe.format->number_of_archive_records == 1 && probe.format->format_size == physical &&
        !xxfc_is_incomplete(&probe)) valid = true;
    xxfc_close(&probe);
    if (borrowed) {
        if (saved >= 0) (void)xx_io_seek64(source, saved, SEEK_SET);
    } else xx_io_close(source);
    return valid;
}

/* Quantum DS archives are complete solid streams even with a .001 suffix.
 * Check the full native directory and extent before overriding raw split. */
static bool numbered_quantum(const char *path, xx_io_device *borrowed,
                             xx_pd_struct *pd) {
    unsigned char h[8];
    xx_io_device *source = borrowed ? borrowed : xx_io_file_open(path, "rb");
    xxfc_opened probe = {0};
    int64_t saved, physical;
    bool valid = false;
    if (!source) return false;
    saved = xx_io_tell(source);physical = xx_io_total_size(source);
    if (saved >= 0 && physical > 8 && xx_io_read_at(source,0,h,sizeof(h)) &&
        h[0]=='D' && h[1]=='S' && !h[2] && h[3] && (h[4]||h[5]) && h[6]>=10U && h[6]<=21U &&
        xxfc_open_named(&probe,source,0,"quantum") && xx_format_is_valid(probe.format,pd) &&
        xx_format_handle_base_info(probe.format,pd) && probe.format->number_of_archive_records>0 &&
        probe.format->format_size==physical && !xxfc_is_incomplete(&probe)) valid=true;
    xxfc_close(&probe);
    if (borrowed) { if (saved>=0) (void)xx_io_seek64(source,saved,SEEK_SET); }
    else xx_io_close(source);
    return valid;
}

static bool numbered_red(const char *path, xx_io_device *borrowed,
                             xx_pd_struct *pd) {
    unsigned char h[4];
    xx_io_device *source = borrowed ? borrowed : xx_io_file_open(path, "rb");
    xxfc_opened probe = {0};
    int64_t saved, physical;
    bool valid = false;
    if (!source) return false;
    saved = xx_io_tell(source);physical = xx_io_total_size(source);
    if (saved >= 0 && physical >= 39 && xx_io_read_at(source,0,h,sizeof(h)) &&
        h[0]=='R' && h[1]=='R' && h[2]==1U && h[3]>=39U &&
        xxfc_open_named(&probe,source,0,"red") && xx_format_is_valid(probe.format,pd) &&
        xx_format_handle_base_info(probe.format,pd) && probe.format->number_of_archive_records>0 &&
        probe.format->format_size==physical && !xxfc_is_incomplete(&probe)) valid=true;
    xxfc_close(&probe);
    if (borrowed) { if (saved>=0) (void)xx_io_seek64(source,saved,SEEK_SET); }
    else xx_io_close(source);
    return valid;
}

static bool numbered_rtpatch_setup(const char *path, xx_io_device *borrowed,
                             xx_pd_struct *pd) {
    unsigned char h[16];
    xx_io_device *source = borrowed ? borrowed : xx_io_file_open(path, "rb");
    xxfc_opened probe = {0};
    int64_t saved, physical;
    bool valid = false;
    unsigned name_length;
    if (!source) return false;
    saved = xx_io_tell(source);physical = xx_io_total_size(source);
    if (saved >= 0 && physical >= 26 && xx_io_read_at(source,0,h,sizeof(h)) &&
        (name_length=(unsigned)h[14]|((unsigned)h[15]<<8U))>=2U && name_length<=13U &&
        xx_io_read_at(source,16,h,name_length+2U) && !h[name_length-1U] && h[name_length]==0xb5U && h[name_length+1U]==0x9cU &&
        xxfc_open_named(&probe,source,0,"rtpatch_setup_data") && xx_format_is_valid(probe.format,pd) &&
        xx_format_handle_base_info(probe.format,pd) && probe.format->number_of_archive_records>0 &&
        probe.format->format_size==physical && !((const xx_rtpatch_setup_data *)probe.format)->split) valid=true;
    xxfc_close(&probe);
    if (borrowed) { if (saved>=0) (void)xx_io_seek64(source,saved,SEEK_SET); }
    else xx_io_close(source);
    return valid;
}

static bool numbered_rnc(const char *path, xx_io_device *borrowed,
                             xx_pd_struct *pd) {
    unsigned char h[12];
    xx_io_device *source = borrowed ? borrowed : xx_io_file_open(path, "rb");
    xxfc_opened probe = {0};
    int64_t saved, physical;
    bool valid = false;
    if (!source) return false;
    saved = xx_io_tell(source);physical = xx_io_total_size(source);
    if (saved >= 0 && physical >= 18 && xx_io_read_at(source,0,h,sizeof(h)) &&
        ((h[0]=='R' && h[1]=='N' && h[2]=='C' && (h[3]==1U || h[3]==2U)) ||
         (!xx_rt_memcmp(h,"BULLFROGRNC",11U) && (h[11]==1U || h[11]==2U))) &&
        xxfc_open_named(&probe,source,0,"rnc") && xx_format_is_valid(probe.format,pd) &&
        xx_format_handle_base_info(probe.format,pd) && probe.format->number_of_archive_records>0 &&
        probe.format->format_size==physical && !xxfc_is_incomplete(&probe)) valid=true;
    xxfc_close(&probe);
    if (borrowed) { if (saved>=0) (void)xx_io_seek64(source,saved,SEEK_SET); }
    else xx_io_close(source);
    return valid;
}

#endif

static int walk_device(const xfu_request *request, const char *unpack_to,
                       xx_pd_struct *pd, xx_io_device *preopened,
                       unsigned nested_depth, const outer_layers *outer,
                       test_monitor *stream) {
#if XFU_FORMAT_SUPPORT_FULL
    xfu_request resolved_request;
    const char *resolved_reader = NULL;
    bool raw_reader = false;
#endif
    const char *archive_path = request->archive_path;
    /* Extension hints for what this layer holds: the archive's own name, or
     * the name the outer layers give it. */
#if XFU_FORMAT_SUPPORT_FULL
    const char *hint = outer && outer->name ? outer->name : archive_path;
#endif
    xx_io_device *device;
    xxfc_opened opened = {0};
    xx_archive_record_state *state;
    xx_list_t options;
    uint64_t total = 0, failed = 0, known_total = 0, interrupted = 0;
    uint64_t visited = 0;
#if XFU_FORMAT_SUPPORT_FULL
    int64_t freearc_sfx_base;
#endif
    size_t selected_cursor = 0;
    int status = 0;
    xx_file_type_t types[XX_FILE_TYPE_CHAIN_MAX], selected;
    size_t type_count;
#if XFU_FORMAT_SUPPORT_FULL
    xx_file_type_t detected, extension_hint;
    xx_file_type_t compound_hint = XX_FILE_TYPE_UNKNOWN;
    size_t type_index;
    bool type_matches = false;
#endif
    bool extension_opened = false;
    bool engine_attempted = false;
    bool reported_encryption = false, reported_password_failure = false;
    bool testing = request->command == XFU_COMMAND_TEST;
    bool reached_end = false;
#if XFU_FORMAT_SUPPORT_FULL
    bool cpio_cp437 = false;
    bool cpio_pwb = false;
    bool layer_pick = false;
#endif
    /* The layer inside was too large to open (layer_too_large): what is
     * read is not all of the archive. */
    bool partial = false;
    /* A cut compressed TAR that holds members no check covers: Test and
     * Extract say after the walk whether they read one (unverified). */
    bool unverified_note = false, unverified = false;
    test_monitor monitor = {0};

#if XFU_FORMAT_SUPPORT_FULL
    if (request->reader_name && !strncmp(request->reader_name, "native", 6)) {
        if (!xfu_native_reader_resolve(request->reader_name,
                                       &resolved_reader, &raw_reader)) {
            emit(request, true, "%s: invalid native reader selection", archive_path);
            return 2;
        }
        resolved_request = *request;
        resolved_request.reader_name = resolved_reader;
        request = &resolved_request;
    }
    if (raw_reader) {
        xx_file_type_t stream_type = xfu_native_raw_filter_type(archive_path);
        if (stream_type == XX_FILE_TYPE_UNKNOWN) {
            if (preopened) xx_io_close(preopened);
            return xfu_native_raw_read(request, unpack_to, pd);
        }
        resolved_request.file_type = stream_type;
    }
    if (request->archive_options && request->archive_options[0]) {
        static const char *const known[] = {"cpio:hdrcharset=CP437", "cpio:pwb=1"};
        size_t length = 0;
        const char *unknown = xfu_option_unknown(request->archive_options, known, 2, &length);
        if (unknown) {
            emit(request, true, "unsupported native read option: %.*s", (int)length, unknown);
            if (preopened) xx_io_close(preopened);
            return 2;
        }
        cpio_cp437 = xfu_option_listed(request->archive_options, "cpio:hdrcharset=CP437");
        cpio_pwb = xfu_option_listed(request->archive_options, "cpio:pwb=1");
    }
    if (resolved_reader && !strcmp(resolved_reader, "empty")) {
        if (cpio_cp437 || cpio_pwb) {
            emit(request, true, "CPIO read option requires a CPIO archive");
            if (preopened) xx_io_close(preopened);
            return 2;
        }
        if (preopened) xx_io_close(preopened);
        return xfu_native_empty_read(request, pd);
    }
    if (!request->reader_name && request->file_type == XX_FILE_TYPE_UNKNOWN &&
        xfu_split_is_first_volume(archive_path) && !cancelled(request, pd)) {
        const char *numbered_reader = numbered_ibmpack(archive_path, preopened, pd) ? "ibmpack" :
            numbered_gtu(archive_path, preopened, pd) ? "gtu" :
            numbered_lifkd(archive_path, preopened, pd) ? "lifkd" :
            numbered_netwarepacked(archive_path, preopened, pd) ? "netwarepacked" :
            numbered_netware2(archive_path, preopened, pd) ? "netware2" :
            numbered_quantum(archive_path, preopened, pd) ? "quantum" :
            numbered_red(archive_path, preopened, pd) ? "red" :
            numbered_rnc(archive_path, preopened, pd) ? "rnc" :
            numbered_rtpatch_setup(archive_path, preopened, pd) ? "rtpatch_setup_data" :
            numbered_asymetrix(archive_path, preopened, pd) ? "asymetrix" :
            numbered_lha_sfx(archive_path, preopened, pd) ? "sfx_lha" : NULL;
        if (numbered_reader) {
            resolved_request = *request;
            resolved_request.reader_name = numbered_reader;
            request = &resolved_request;
        }
    }
    if ((request->reader_name && !strcmp(request->reader_name, "split")) ||
        (!request->reader_name && (request->file_type == XX_FILE_TYPE_NUMBERED_SPLIT ||
         (request->file_type == XX_FILE_TYPE_UNKNOWN &&
          xfu_split_is_first_volume(archive_path)))))
    {
        /* A split series has no password: one typed for another archive,
         * or an explicitly empty one, does not stop it from being read. */
        xfu_request split = *request;
        split.password = NULL;
        if (preopened) xx_io_close(preopened);
        return xfu_split_run(&split, pd);
    }

    if (testing && request->callbacks.test_progress)
        request->callbacks.test_progress(request->callbacks.user, 0);

    if (cancelled(request, pd)) {
        emit(request, true, "operation cancelled");
        if (preopened) xx_io_close(preopened);
        return 1;
    }
    /* A type picked from the list that the file's own chain lacks can be one
     * read inside its envelope or outer stream (layer_pick, see below). */
layers:
    if (!raw_reader && !request->reader_name &&
        (request->file_type == XX_FILE_TYPE_UNKNOWN || layer_pick)) {
        outer_layers next;
        if (nested_depth > 8U) {
            emit(request, true, "%s: too many nested archive filters", archive_path);
            if (preopened) xx_io_close(preopened);
            return 2;
        }
        {
            xx_io_device *decoded = NULL;
            char *decoded_name = NULL;
            xx_file_type_t envelope_type = XX_FILE_TYPE_BASE64;
            /* The envelope probes report a file that cannot be opened (a
             * mistyped path, a file gone) as an envelope they cannot decode.
             * Opened here, so that how far it is read shows the decoding. */
            xx_io_device *source = preopened ? preopened : xx_io_file_open(archive_path, "rb");
            xfu_native_envelope_status envelope;
            if (!source) {
                emit(request, true, "cannot open %s", archive_path);
                return 2;
            }
            stream_attach(stream, source);
            envelope = xfu_native_base64_open_device(source, pd, &decoded,
                                                     &decoded_name);
            if (envelope == XFU_NATIVE_ENVELOPE_NOT_FOUND) {
                envelope_type = XX_FILE_TYPE_UUE;
                envelope = xfu_native_uue_open_device(source, pd, &decoded,
                                                      &decoded_name);
            }
            stream_detach(stream);
            if (source != preopened) xx_io_close(source);
            if (envelope == XFU_NATIVE_ENVELOPE_ARCHIVE) {
                int result;
                if (preopened) xx_io_close(preopened);
                layers_add(&next, outer, envelope_type, XX_FILE_TYPE_UNKNOWN);
                next.name = decoded_name ? decoded_name : "";
                result = walk_device(request, unpack_to, pd, decoded,
                                     nested_depth + 1U, &next, stream);
                free(decoded_name);
                return result;
            }
            if (envelope == XFU_NATIVE_ENVELOPE_RAW && layer_pick) {
                /* One raw payload is no interpretation that was picked. */
                xx_io_close(decoded);
                free(decoded_name);
            } else if (envelope == XFU_NATIVE_ENVELOPE_RAW) {
                /* The payload is a member of the envelope type: reported as
                 * Binary, it would leave the GUI no Test or Extract. */
                xfu_request raw = *request;
                xx_file_type_t chain[XX_FILE_TYPE_CHAIN_MAX];
                size_t count = xx_format_get_file_type_chain(envelope_type, chain,
                                                             XX_FILE_TYPE_CHAIN_MAX);
                int result;
                report_types(request, outer, chain, count, envelope_type, NULL);
                raw.callbacks.file_types = NULL;
                raw.callbacks.unpack_handle_types = NULL;
                result = xfu_native_raw_device_read(&raw, unpack_to, pd, decoded,
                                                    decoded_name);
                xx_io_close(decoded);
                if (preopened) xx_io_close(preopened);
                free(decoded_name);
                return result;
            }
            if (envelope == XFU_NATIVE_ENVELOPE_DECODE_ERROR) {
                bool stopped = cancelled(request, pd);
                if (stopped) emit(request, true, "operation cancelled");
                else emit(request, true, "%s: encoded envelope could not be decoded", archive_path);
                if (preopened) xx_io_close(preopened);
                return stopped ? 1 : 2;
            }
            if (envelope == XFU_NATIVE_ENVELOPE_TOO_LARGE ||
                envelope == XFU_NATIVE_ENVELOPE_NO_ROOM) {
                /* The ordinary Base64/uuencode reader below streams it. */
                if (envelope == XFU_NATIVE_ENVELOPE_NO_ROOM)
                    layer_no_room(request, XX_FILE_TYPE_UNKNOWN, envelope_type, true);
                else
                    layer_too_large(request, XX_FILE_TYPE_UNKNOWN, envelope_type, true);
                partial = true;
            }
        }
        xx_io_device *probe = preopened ? preopened : xx_io_file_open(archive_path, "rb");
        xx_file_type_t original = probe
            ? xx_format_get_file_type_device(probe) : XX_FILE_TYPE_UNKNOWN;
        if (probe && request->file_type == XX_FILE_TYPE_UNKNOWN &&
            is_stream_type(original) && !is_compressed_tar_type(original)) {
            xx_list_t *handles = xx_format_get_unpack_handle_types_device(probe, false);
            size_t i;
            /* A structural compound reader can recognize TAR in a stream
             * whose framing does not disclose the decoded size. Prefer that
             * precise handle before recursively opening the inner TAR. */
            for (i = 0; handles && i < handles->count; ++i) {
                xx_file_type_t candidate = xx_format_handle_type_file_type(
                    *(const xx_handle_type_t *)xx_list_at(handles, i));
                if (is_compressed_tar_type(candidate)) original = candidate;
            }
            xx_list_destroy(handles);
        }
        if (is_compressed_tar_type(original)) compound_hint = original;
        if (probe && !preopened) xx_io_close(probe);
        /* The native composite readers retain the original BINARY/filter/TAR
         * interpretation and verify their compressed stream themselves. */
        if (!is_compressed_tar_type(original)) {
            xx_file_type_t outer_type = preopened
                ? xfu_native_raw_filter_type_device(preopened)
                : xfu_native_raw_filter_type(archive_path);
            if (outer_type != XX_FILE_TYPE_UNKNOWN) {
            xx_io_device *inner = NULL;
            /* Opened here, so that how far it is read shows the decoding. */
            xx_io_device *source = preopened ? preopened : xx_io_file_open(archive_path, "rb");
            xfu_native_inner_status inner_status = XFU_NATIVE_INNER_DECODE_ERROR;
            xx_file_type_t layer_type = XX_FILE_TYPE_UNKNOWN;
            if (source) {
                stream_attach(stream, source);
                inner_status = xfu_native_open_inner_device_ex(source, outer_type, pd, &inner,
                                                               &layer_type);
                stream_detach(stream);
                if (source != preopened) xx_io_close(source);
            }
            if (inner_status == XFU_NATIVE_INNER_TOO_LARGE && !partial) {
                /* Read below as the outer stream's one file. */
                layer_too_large(request, layer_type, outer_type, false);
                partial = true;
            }
            if (inner_status == XFU_NATIVE_INNER_NO_ROOM && !partial) {
                layer_no_room(request, layer_type, outer_type, false);
                partial = true;
            }
            if (inner_status == XFU_NATIVE_INNER_ARCHIVE) {
                int result;
                char *inner_name = layer_name(hint, outer_type);
                if (preopened) xx_io_close(preopened);
                layers_add(&next, outer, original, outer_type);
                next.name = inner_name ? inner_name : "";
                result = walk_device(request, unpack_to, pd, inner,
                                     nested_depth + 1U, &next, stream);
                free(inner_name);
                return result;
            }
            if (inner_status == XFU_NATIVE_INNER_DECODE_ERROR) {
                bool stopped = cancelled(request, pd);
                if (stopped) emit(request, true, "operation cancelled");
                else {
                    /* The File type list shows the type the log names, not
                     * Binary: Test and Extract can run on it and say so. */
                    xx_file_type_t named = damaged_name(outer_type, hint);
                    type_count = xx_format_get_file_type_chain(named, types, XX_FILE_TYPE_CHAIN_MAX);
                    report_types(request, outer, types, type_count, named, NULL);
                    damaged_stream(request, outer_type, hint);
                }
                if (preopened) xx_io_close(preopened);
                return stopped ? 1 : 2;
            }
            }
        }
        if (preopened && xx_io_seek64(preopened, 0, SEEK_SET) != 0) {
            xx_io_close(preopened);
            return 2;
        }
        if (partial) {
            if (request->handle_type != XX_HANDLE_TYPE_UNKNOWN) {
                emit(request, true, "%s: selected unpack handle %s could not be opened through its outer layers",
                     archive_path, xx_format_handle_type_to_string(request->handle_type));
                if (preopened) xx_io_close(preopened);
                return 2;
            }
            /* The stream is what is read, as its own type even when the
             * archive inside was picked. Test reads less than List shows:
             * the File type list and the members listed stay as they are. */
            resolved_request = *request;
            resolved_request.file_type = XX_FILE_TYPE_UNKNOWN;
            if (testing) {
                resolved_request.callbacks.file_types = NULL;
                resolved_request.callbacks.unpack_handle_types = NULL;
                resolved_request.callbacks.entry = NULL;
                resolved_request.callbacks.walk_end = NULL;
            }
            request = &resolved_request;
        }
    }
    device = preopened ? preopened : xx_io_file_open(archive_path, "rb");
    if (!device) {
        emit(request, true, "cannot open %s", archive_path);
        return 2;
    }
    freearc_sfx_base = xfu_freearc_sfx_payload_offset(device);
    if (freearc_sfx_base >= 0 &&
        handle_matches_device(device, request->handle_type) &&
        ((!request->reader_name && (request->file_type == XX_FILE_TYPE_UNKNOWN ||
                                   request->file_type == XX_FILE_TYPE_FREEARC)) ||
         (request->reader_name && request->file_type == XX_FILE_TYPE_UNKNOWN &&
          (!strcmp(request->reader_name, "freearc") ||
           !strcmp(request->reader_name, "freearc_sfx"))))) {
        if (!xxfc_open_named(&opened, device, freearc_sfx_base, "freearc")) {
            emit(request, true, "%s: FreeArc SFX reader is unavailable", archive_path);
            xx_io_close(device); return 2;
        }
        selected = opened.type;
        engine_attempted = true;
        type_count = xx_format_get_file_type_chain(selected, types, XX_FILE_TYPE_CHAIN_MAX);
        report_types(request, outer, types, type_count, selected, device);
        if (cancelled(request, pd)) {
            emit(request, true, "operation cancelled");
            xxfc_close(&opened); xx_io_close(device); return 1;
        }
        goto reader_opened;
    }
    if (request->reader_name) {
        if (!request->reader_name[0] || request->file_type != XX_FILE_TYPE_UNKNOWN ||
            !xxfc_open_named(&opened, device, 0, request->reader_name)) {
            emit(request, true, "%s: invalid explicit reader selection", archive_path);
            xx_io_close(device); return 2;
        }
        selected = opened.type;
        type_count = xx_format_get_file_type_chain(selected, types, XX_FILE_TYPE_CHAIN_MAX);
        report_types(request, outer, types, type_count, selected, device);
        if (cancelled(request, pd)) {
            emit(request, true, "operation cancelled"); xxfc_close(&opened); xx_io_close(device); return 1;
        }
        goto reader_opened;
    }
    /* Probe packed carriers before extension/content detection. In particular,
     * renamed DOS COM inputs must reach the bounded UPX reader without nesting
     * its helper protocol beneath the broad content detector's reader stack. */
    detected = xx_upx_has_marker_device(device,pd)
                   ? XX_FILE_TYPE_UPX
                   : xx_format_get_file_type_device_fast(device, hint);
    extension_hint=xx_format_get_file_type_extension(hint);
    switch (detected) {
    case XX_FILE_TYPE_PE32: case XX_FILE_TYPE_PE64:
    case XX_FILE_TYPE_ELF32: case XX_FILE_TYPE_ELF64:
    case XX_FILE_TYPE_MACHO32: case XX_FILE_TYPE_MACHO64:
        detected=xx_format_get_file_type_device(device);
        break;
    default: break;
    }
    /* The layer probe already authenticated the decoded TAR header. The
     * fast signature reader may name only its outer codec, especially when
     * a checksum farther into that stream is damaged. Keep the proven
     * compound identity for the reader's full validation and diagnostics. */
    if (compound_hint != XX_FILE_TYPE_UNKNOWN &&
        stream_of(detected) == stream_of(compound_hint)) detected = compound_hint;
    if (request->password) {
        xx_file_type_t password_type=xx_molebox_detect_device(device,request->password,pd);
        if (password_type!=XX_FILE_TYPE_UNKNOWN) detected=password_type;
    }
    /* Such a stream is decoded as soon as its reader validates it. */
    if (is_stream_type(detected) || is_stream_type(request->file_type))
        stream_attach(stream, device);
    if (extension_hint != XX_FILE_TYPE_UNKNOWN && detected == extension_hint) {
        extension_opened = xxfc_open_extension_fast(&opened, device, 0,
                                                    hint, pd);
        if (extension_opened) detected = opened.type;
        else if (!xx_pd_is_stopped(pd))
            detected = xx_format_get_file_type_device(device);
    }
    if (!extension_opened && !xx_pd_is_stopped(pd) &&
        (detected == XX_FILE_TYPE_BINARY || detected == XX_FILE_TYPE_UNKNOWN)) {
        extension_opened = xxfc_open_extension(&opened, device, 0,
                                               hint, pd);
        if (extension_opened) detected = opened.type;
    }
    {
        xx_list_t *handles = xx_format_get_unpack_handle_types_device(device, true);
        size_t i;
        /* The handle detector orders a stream before its compound archive,
         * and a generic reader before a more specific one. Reader selection
         * follows that order independently from the physical file identity. */
        if (handles)
            for (i = 0; i < handles->count; ++i) {
                xx_handle_type_t handle = *(const xx_handle_type_t *)xx_list_at(handles, i);
                xx_file_type_t candidate = xx_format_handle_type_file_type(handle);
                if (candidate != XX_FILE_TYPE_UNKNOWN) {
                    size_t known_count = xx_format_get_file_type_chain(detected, types,
                                                                        XX_FILE_TYPE_CHAIN_MAX);
                    size_t j;
                    for (j = 0; j < known_count && types[j] != candidate; ++j) {}
                    /* A valid TAR header decoded from the stream's prefix
                     * proves a more specific interpretation even when a
                     * later checksum is damaged. Keep that reader so it can
                     * diagnose the archive, rather than changing to its raw
                     * stream parent after full compound validation fails. */
                    if (j == known_count || candidate == detected) detected = candidate;
                }
            }
        if (request->handle_type != XX_HANDLE_TYPE_UNKNOWN)
            type_matches = handle_contains(handles, request->handle_type);
        xx_list_destroy(handles);
    }
    type_count = xx_format_get_file_type_chain(detected, types, XX_FILE_TYPE_CHAIN_MAX);
    selected = request->file_type == XX_FILE_TYPE_UNKNOWN ? detected : request->file_type;
    if (request->handle_type == XX_HANDLE_TYPE_UNKNOWN)
        for (type_index = 0; type_index < type_count; ++type_index)
            if (types[type_index] == selected) type_matches = true;
    /* The File type list of a nested read offers the inner interpretations
     * (the TAR in an LZ4 stream without a content size, the archive in a
     * base64 envelope): read through the layers again for such a pick. */
    if (request->file_type != XX_FILE_TYPE_UNKNOWN && !type_matches && !layer_pick &&
        selected != XX_FILE_TYPE_BINARY && !raw_reader && !xx_pd_is_stopped(pd)) {
        layer_pick = true;
        if (extension_opened) xxfc_close(&opened);
        extension_opened = false;
        stream_detach(stream);
        if (!preopened) xx_io_close(device);
        else if (xx_io_seek64(preopened, 0, SEEK_SET) != 0) {
            xx_io_close(preopened); return 2;
        }
        goto layers;
    }
    report_types(request, outer, types, type_count, selected, device);
    if (cancelled(request, pd)) {
        emit(request, true, "operation cancelled");
        if (extension_opened) xxfc_close(&opened);
        xx_io_close(device); return 1;
    }
    if (request->file_type != XX_FILE_TYPE_UNKNOWN && !type_matches) {
        /* A damaged stream picked as the File type list offers it (below):
         * it is damaged, not of another type. */
        xx_file_type_t damaged = damaged_stream_type(device, hint);
        if (damaged != XX_FILE_TYPE_UNKNOWN &&
            (selected == damaged || selected == damaged_name(damaged, hint))) {
            type_count = xx_format_get_file_type_chain(selected, types, XX_FILE_TYPE_CHAIN_MAX);
            report_types(request, outer, types, type_count, selected, NULL);
            damaged_stream(request, damaged, hint);
        } else
            emit(request, true, "%s: selected type %s does not match this file", archive_path,
                 request->handle_type == XX_HANDLE_TYPE_UNKNOWN
                     ? xx_format_file_type_to_string(selected)
                     : xx_format_handle_type_to_string(request->handle_type));
        if (extension_opened) xxfc_close(&opened);
        xx_io_close(device); return 2;
    }
    if (selected == XX_FILE_TYPE_BINARY) {
        xx_file_type_t damaged = XX_FILE_TYPE_UNKNOWN;
        uint64_t limit = 0;
        engine_attempted = true;
        if (request->file_type == XX_FILE_TYPE_UNKNOWN &&
            xxfc_open_fallback(&opened, device, 0, archive_path, request->password, selected, pd)) {
            extension_opened = true;
            goto reader_opened;
        }
        if (xx_pd_is_stopped(pd)) {
            emit(request, true, "operation cancelled");
            if (extension_opened) xxfc_close(&opened);
            xx_io_close(device); return 1;
        }
        /* A compressed stream whose frames failed the detectors is no
         * Binary file: it is a damaged archive, unless it is whole and only
         * too large for its reader, which decodes it in memory. */
        if (request->file_type == XX_FILE_TYPE_UNKNOWN)
            damaged = damaged_stream_type(device, hint);
        if (damaged != XX_FILE_TYPE_UNKNOWN && xfu_native_stream_over_limit(device, damaged, &limit))
            emit(request, true, "%s: the %s stream is too large to open: its reader decodes at "
                 "most %llu %s in memory", archive_path, xx_format_file_type_to_string(damaged),
                 (unsigned long long)(limit % (UINT64_C(1) << 30) ? limit >> 20 : limit >> 30),
                 limit % (UINT64_C(1) << 30) ? "MiB" : "GiB");
        else if (damaged != XX_FILE_TYPE_UNKNOWN) {
            damaged_stream(request, damaged, hint);
            damaged = damaged_name(damaged, hint);
        } else not_archive(request, XX_FILE_TYPE_BINARY);
        /* The File type list shows the type the log names, not Binary: Test
         * and Extract can run on it, and they detect it and say so again. */
        if (damaged != XX_FILE_TYPE_UNKNOWN) {
            type_count = xx_format_get_file_type_chain(damaged, types, XX_FILE_TYPE_CHAIN_MAX);
            report_types(request, outer, types, type_count, damaged, NULL);
        }
        if (extension_opened) xxfc_close(&opened);
        xx_io_close(device); return 2;
    }
    if (extension_opened && opened.type != selected) {
        xxfc_close(&opened);
        extension_opened = false;
    }
    /* The composite reader owns recovery and checksum validation. Falling
     * back to its raw filter would hide a damaged TAR or failed stream
     * checksum behind an unrelated single-payload interpretation. */
    if (is_compressed_tar_type(selected)) engine_attempted = true;
    if (is_stream_type(selected)) stream_attach(stream, device);
    if (!extension_opened && !xxfc_open_type(&opened, device, 0, selected)) {
        if (!engine_attempted && request->file_type == XX_FILE_TYPE_UNKNOWN && !xx_pd_is_stopped(pd)) {
            engine_attempted = true;
            if (xxfc_open_fallback(&opened, device, 0, archive_path, request->password, selected, pd)) {
                extension_opened = true; goto reader_opened;
            }
        }
        if (xx_pd_is_stopped(pd)) {
            emit(request, true, "operation cancelled");
            xx_io_close(device);
            return 1;
        }
        /* A recognised type with no reader here (PE, ELF, ...) is, like
         * Binary, a file that is not an archive. */
        if (selected == XX_FILE_TYPE_UNKNOWN)
            emit(request, true, "%s: not a recognised format", archive_path);
        else
            not_archive(request, selected);
        xx_io_close(device);
        return 2;
    }
reader_opened:
#else
    (void)nested_depth;
    xfu_request selected_request = *request;
    if (selected_request.reader_name) {
        if (!strcmp(selected_request.reader_name, "native")) selected_request.reader_name = NULL;
        else if (!strncmp(selected_request.reader_name, "native:", 7))
            selected_request.reader_name += 7;
    }
    request = &selected_request;
    if (request->archive_options && request->archive_options[0]) {
        emit(request, true, "unsupported read option: %s", request->archive_options);
        if (preopened) xx_io_close(preopened);
        return 2;
    }
    device = preopened ? preopened : xx_io_file_open(archive_path, "rb");
    if (!device) { emit(request, true, "cannot open %s", archive_path); return 2; }
#if XFU_FORMAT_SUPPORT_TAR_FILTERS
    if (cancelled(request, pd) || nested_depth > 8U) {
        bool stopped = cancelled(request, pd);
        emit(request, true, stopped ? "operation cancelled" : "too many nested TAR filters");
        xx_io_close(device);
        return stopped ? 1 : 2;
    }
    if (!request->reader_name &&
        (request->file_type == XX_FILE_TYPE_UNKNOWN || request->file_type == XX_FILE_TYPE_TAR)) {
        xx_io_device *decoded = NULL;
        char *decoded_name = NULL;
        xx_file_type_t envelope_type = XX_FILE_TYPE_BASE64;
        xfu_native_envelope_status envelope;
        stream_attach(stream, device);
        envelope = xfu_native_base64_open_device(device, pd, &decoded, &decoded_name);
        if (envelope == XFU_NATIVE_ENVELOPE_NOT_FOUND) {
            envelope_type = XX_FILE_TYPE_UUE;
            envelope = xfu_native_uue_open_device(device, pd, &decoded, &decoded_name);
        }
        stream_detach(stream);
        if (envelope != XFU_NATIVE_ENVELOPE_NOT_FOUND) {
            int result;
            xx_io_close(device);
            if (envelope == XFU_NATIVE_ENVELOPE_ARCHIVE) {
                outer_layers next;
                xx_file_type_t chain[XX_FILE_TYPE_CHAIN_MAX];
                size_t count = xx_format_get_file_type_chain(envelope_type, chain, XX_FILE_TYPE_CHAIN_MAX);
                layers_append(&next, outer, chain, count);
                next.name = decoded_name;
                result = walk_device(request, unpack_to, pd, decoded, nested_depth + 1U, &next, stream);
            } else {
                if (decoded) xx_io_close(decoded);
                emit(request, true, "encoded TAR could not be decoded or does not contain a TAR archive");
                result = cancelled(request, pd) ? 1 : 2;
            }
            free(decoded_name);
            return result;
        }
    }
#endif
    selected = request->file_type != XX_FILE_TYPE_UNKNOWN ? request->file_type :
               xx_format_get_file_type_device(device);
#ifdef XFU_SUPPORT_FORMAT_TAR
    if (request->file_type == XX_FILE_TYPE_UNKNOWN && !request->reader_name) {
        xx_file_type_t hint_type = xx_format_get_file_type_extension(archive_path);
        /* A broken compressed TAR must not silently become an unrelated raw
         * stream merely because its final checksum/inner header is damaged. */
        if (is_compressed_tar_type(hint_type)) selected = hint_type;
    }
    if (selected == XX_FILE_TYPE_TAR) {
        xx_file_type_t detected_type = xx_format_get_file_type_device(device);
        if (is_compressed_tar_type(detected_type)) selected = detected_type;
#if XFU_FORMAT_SUPPORT_TAR_FILTERS
        else if (detected_type == XX_FILE_TYPE_LZ4 && !request->reader_name) {
            xx_io_device *inner = NULL;
            xfu_native_inner_status decoded;
            stream_attach(stream, device);
            decoded = xfu_native_open_inner_device(device, detected_type, pd, &inner);
            stream_detach(stream);
            xx_io_close(device);
            if (decoded == XFU_NATIVE_INNER_ARCHIVE) {
                outer_layers next;
                xx_file_type_t chain[XX_FILE_TYPE_CHAIN_MAX];
                size_t count = xx_format_get_file_type_chain(detected_type, chain, XX_FILE_TYPE_CHAIN_MAX);
                layers_append(&next, outer, chain, count);
                next.name = "";
                return walk_device(request, unpack_to, pd, inner, nested_depth + 1U, &next, stream);
            }
            emit(request, true, "LZ4 stream could not be decoded as TAR");
            return cancelled(request, pd) ? 1 : 2;
        }
#endif
    }
#endif
    if (!handle_matches_device(device, request->handle_type) ||
        !(request->reader_name ? xxfc_open_named(&opened, device, 0, request->reader_name) :
                                xxfc_open_type(&opened, device, 0, selected))) {
        emit(request, true, "%s: unavailable reader, invalid archive or unpack handle selection", archive_path);
        xx_io_close(device);
        return 2;
    }
    selected = opened.type;
    type_count = xx_format_get_file_type_chain(selected, types, XX_FILE_TYPE_CHAIN_MAX);
    report_types(request, outer, types, type_count, selected, device);
    engine_attempted = true; /* There are no foreign-format fallbacks in this component. */
reader_opened:
#endif
    xxfc_attach_source_files(&opened, archive_path);
#if XFU_FORMAT_SUPPORT_FULL
    if (cpio_cp437 || cpio_pwb) {
        if (!opened.reader_name || strcmp(opened.reader_name, "cpio") ||
            !xx_cpio_set_read_options((xx_cpio *)opened.format,
                                      cpio_cp437, cpio_pwb)) {
            emit(request, true, "%s: CPIO read option requires a CPIO archive",
                 archive_path);
            status = 2;
            goto done;
        }
        extension_opened = false;
    }
#endif

    if (request->password) {
        if (!xx_format_set_password(opened.format, request->password)) {
            emit(request, true, "cannot set archive password");
            status = 2;
            goto done;
        }
        /* Password assignment invalidates the reader's cached validation and
         * base information, including readers opened by the extension hint. */
        extension_opened = false;
    }
    if (!extension_opened && !xx_format_is_valid(opened.format, pd)) {
#if XFU_FORMAT_SUPPORT_FULL
        uint64_t limit = 0;
        uint32_t ratio = 0;
#endif
        bool encrypted_format = opened.format && opened.format->is_crypted;
        if (!engine_attempted && !request->reader_name &&
            request->file_type == XX_FILE_TYPE_UNKNOWN &&
            !xxfc_reader_uses_helper(&opened) && !xx_pd_is_stopped(pd)) {
            engine_attempted = true;
            if (xxfc_open_fallback(&opened, device, 0, archive_path, request->password, selected, pd)) {
                extension_opened = true; goto reader_opened;
            }
        }
        if (xx_pd_is_stopped(pd)) emit(request, true, "operation cancelled");
        else if (encrypted_format) {
            encryption_blocked(request, &reported_encryption, &reported_password_failure, NULL);
            emit(request, true, request->password
                ? "%s: archive is encrypted; the password may be incorrect or the encrypted data may be damaged"
                : "%s: archive is encrypted; a password is needed", archive_path);
        }
#if XFU_FORMAT_SUPPORT_FULL
        /* A compressed TAR is decoded once, in RAM and beyond a threshold in
         * a temporary file, which Test (memory only) cannot write. */
        else if (xx_tar_compressed_too_large(opened.format, &limit))
            emit(request, true, "%s: the TAR inside is larger than the %llu GiB that Test keeps "
                 "in memory; it was not tested", archive_path,
                 (unsigned long long)(limit >> 30));
        /* Nor, past 1 GiB, one over a ratio of its compressed size: a
         * decompression bomb would fill the disk. */
        else if (xx_tar_compressed_too_dense(opened.format, &ratio))
            emit(request, true, "%s: the TAR inside is larger than 1 GiB and more than %u times "
                 "the size of its compressed stream, as a decompression bomb is; it was not "
                 "opened", archive_path, (unsigned)ratio);
        else if (xx_tar_compressed_no_room(opened.format))
            emit(request, true, "%s: no space for a temporary copy of the TAR inside; it was "
                 "not opened", archive_path);
        /* A compressed TAR reader checks its stream and the TAR in it: a
         * damaged stream fails here, too. */
        else if (is_compressed_tar_type(opened.type)) damaged_stream(request, opened.type, hint);
#endif

        else emit(request, true, "%s: %s header does not hold up", archive_path,
                  opened.reader_name);
        status = xx_pd_is_stopped(pd) ? 1 : 2;
        goto done;
    }
    if (!extension_opened && !xx_format_handle_base_info(opened.format, pd)) {
        bool encrypted_format = opened.format && opened.format->is_crypted;
        xx_file_type_t failed_type = opened.type;
        if (!engine_attempted && !request->reader_name &&
            request->file_type == XX_FILE_TYPE_UNKNOWN &&
            !xxfc_reader_uses_helper(&opened) && !xx_pd_is_stopped(pd)) {
            engine_attempted = true;
            if (xxfc_open_fallback(&opened, device, 0, archive_path, request->password, selected, pd)) {
                extension_opened = true; goto reader_opened;
            }
        }
        if (xx_pd_is_stopped(pd)) emit(request, true, "operation cancelled");
        else if (encrypted_format) {
            encryption_blocked(request, &reported_encryption, &reported_password_failure, NULL);
            if (failed_type == XX_FILE_TYPE_7ZIP)
                emit(request, true, request->password
                     ? "%s: the password may be incorrect, or the encrypted 7z headers are damaged"
                     : "%s: the 7z headers are encrypted; a password is needed", archive_path);
            else emit(request, true, request->password
                     ? "%s: archive is encrypted; the password may be incorrect or the encrypted metadata may be damaged"
                     : "%s: archive metadata is encrypted; a password is needed", archive_path);
        }
        else emit(request, true, "%s: %s could not be parsed", archive_path,
                  opened.reader_name);
        status = xx_pd_is_stopped(pd) ? 1 : 2;
        goto done;
    }
    xxfc_attach_source_files(&opened, archive_path);
    xxfc_refresh_sevenzip_type(&opened);
    if (opened.format->is_crypted)
        encryption_detected(request, &reported_encryption, NULL);
    if (opened.reader_name && !strncmp(opened.reader_name, "sevenzip", 8) &&
        (request->callbacks.file_types || request->callbacks.unpack_handle_types)) {
        type_count = xx_format_get_file_type_chain(opened.type, types, XX_FILE_TYPE_CHAIN_MAX);
        report_types(request, outer, types, type_count, opened.type, device);
    }
    /* Prefer upstream decoding for native readers with narrower payload
     * coverage, after validating the engine against the actual input. */
    if (!engine_attempted && !request->reader_name &&
        request->file_type == XX_FILE_TYPE_UNKNOWN && (selected == XX_FILE_TYPE_APFS ||
        selected == XX_FILE_TYPE_CHM || selected == XX_FILE_TYPE_HXS)) {
        engine_attempted = true;
        if (xxfc_open_fallback(&opened, device, 0, archive_path, request->password, selected, pd)) {
            extension_opened = true; goto reader_opened;
        }
        xx_pd_clear_error(pd);
    }
    if (!opened.format->is_archive) {
        if (!engine_attempted && !request->reader_name &&
            request->file_type == XX_FILE_TYPE_UNKNOWN &&
            !xxfc_reader_uses_helper(&opened) && !xx_pd_is_stopped(pd)) {
            engine_attempted = true;
            if (xxfc_open_fallback(&opened, device, 0, archive_path, request->password, selected, pd)) {
                extension_opened = true; goto reader_opened;
            }
        }
        /* Like Binary: a recognised format without members (Text, MSDOS) is
         * a file that is not an archive, not an archive with errors. */
        if (xx_pd_is_stopped(pd)) {
            emit(request, true, "operation cancelled");
            status = 1;
        } else {
            not_archive(request, opened.type);
            status = 2;
        }
        goto done;
    }
    if (request->callbacks.unpack_handle_types) {
        xfu_request report = *request;
        report.callbacks.file_types = NULL;
        report_types_with_handle(&report, outer, types, type_count, opened.type, device,
                                  xx_format_get_handle_type(opened.format));
    }
    emit(request, false, "%s: %s", archive_path,
         xx_format_file_type_to_string(opened.type));
    xx_list_init(&options, sizeof(xx_meta), xx_meta_free_elem);
    if (request->password) {
        xx_meta meta;
        xx_meta_init(&meta, XX_META_ID_OPT_PASSWORD);
        if (!xx_var_set_str(&meta.var, request->password) ||
            !xx_list_append(&options, &meta)) {
            xx_meta_cleanup(&meta);
            xx_list_cleanup(&options);
            emit(request, true, "cannot set archive password");
            status = 2;
            goto done;
        }
    }
    if (unpack_to) {
        xx_meta meta;
        xx_meta_init(&meta, XX_META_ID_OPT_UNPACK_PATH);
        if (!xx_var_set_str(&meta.var, unpack_to) ||
            !xx_list_append(&options, &meta)) {
            xx_meta_cleanup(&meta);
            xx_list_cleanup(&options);
            emit(request, true, "%s: members could not be read", archive_path);
            status = 2;
            goto done;
        }
        /* One policy for a file already at a member's path, whatever the
         * reader (each one's default differs): it is replaced, never written
         * through (xx_io_extract_begin), and its line says so. */
        xx_meta_init(&meta, XX_META_ID_OPT_OVERWRITE);
        xx_var_set_bool(&meta.var, true);
        if (!xx_list_append(&options, &meta)) {
            xx_meta_cleanup(&meta);
            xx_list_cleanup(&options);
            emit(request, true, "%s: members could not be read", archive_path);
            status = 2;
            goto done;
        }
    }
    state = xx_format_create_archive_records_reading(
        opened.format, (const xx_list_s *)&options, pd);
    if (!state) {
        bool encrypted_format = opened.format && opened.format->is_crypted;
        xx_file_type_t failed_type = opened.type;
        if (!engine_attempted && !request->reader_name &&
            request->file_type == XX_FILE_TYPE_UNKNOWN &&
            !xxfc_reader_uses_helper(&opened) && !xx_pd_is_stopped(pd)) {
            engine_attempted = true;
            if (xxfc_open_fallback(&opened, device, 0, archive_path, request->password, selected, pd)) {
                xx_list_cleanup(&options); extension_opened = true; goto reader_opened;
            }
        }
        if (xx_pd_is_stopped(pd)) emit(request, true, "operation cancelled");
        else if (encrypted_format) {
            encryption_blocked(request, &reported_encryption, &reported_password_failure, NULL);
            if (failed_type == XX_FILE_TYPE_FREEARC)
                emit(request, true, request->password
                     ? "%s: the FreeArc password may be incorrect, or the encrypted directory is damaged"
                     : "%s: the FreeArc directory is encrypted; a password is needed", archive_path);
            else emit(request, true, request->password
                     ? "%s: archive is encrypted; the password may be incorrect or the encrypted metadata may be damaged"
                     : "%s: archive metadata is encrypted; a password is needed", archive_path);
        }
        else emit(request, true, "%s: members could not be read", archive_path);
        xx_list_cleanup(&options);
        status = xx_pd_is_stopped(pd) ? 1 : 2;
        goto done;
    }
    if (opened.format->is_crypted)
        encryption_detected(request, &reported_encryption, NULL);
    if (state->total_records > 0) known_total = (uint64_t)state->total_records;
    if (request->extract_selected) known_total = (uint64_t)request->selected_record_count;
    if (testing && known_total > RECORD_LIMIT) {
        emit(request, true, "archive has too many members to test");
        xx_format_free_archive_records_reading(opened.format, state);
        xx_list_cleanup(&options); status = 1; goto done;
    }
    if (testing && request->callbacks.test_progress &&
        (state->total_records < 0 ||
         (!known_total && xx_format_get_current_archive_record(opened.format, state)))) {
        /* Streaming readers need a metadata pass to give an honest percentage.
         * Neither pass has an extraction path or writes decoded output. */
        bool counted = test_count_records(request, opened.format, state, pd, &known_total);
        xx_format_free_archive_records_reading(opened.format, state);
        state = NULL;
        if (!counted) {
            emit(request, true, xx_pd_is_stopped(pd) ? "operation cancelled" :
                 "cannot determine archive member count");
            xx_list_cleanup(&options); status = 1; goto done;
        }
        state = xx_format_create_archive_records_reading(
            opened.format, (const xx_list_s *)&options, pd);
        if (!state) {
            emit(request, true, "members could not be read");
            xx_list_cleanup(&options); status = 2; goto done;
        }
    }
    /* Companions attached after detection can complete a disk descriptor.
     * Use the iterator's fresh parse to assess completeness. */
    if (xxfc_is_incomplete(&opened)) {
        /* Test and Extract read the members before the cut of a compressed
         * TAR whether or not a check covers them, as GNU tar does. */
#ifdef XFU_SUPPORT_FORMAT_TAR
        unverified_note = (testing || unpack_to) && xx_tar_compressed_unverified(opened.format);
#endif
        if (!unverified_note)
            emit(request, true, "%s: incomplete archive; showing recovered members", archive_path);
        status = 1;
    }
    /* With several members, their count shows the progress from here. */
    if (known_total > 1) stream_detach(stream);
    if (testing) {
        int64_t size = xx_io_total_size(device);
        monitor.request = request;
        monitor.progress = pd;
        monitor.total = known_total;
        monitor.archive_size = size > 0 ? (uint64_t)size : 0U;
        monitor.previous = xx_pd_set_observer(pd, test_observe, &monitor);
    }
    progress(request, 0, known_total, NULL);
    for (;;) {
        const xx_archive_record *record;
        char *owned = NULL;
        xfu_entry entry;
        xfu_property *properties = NULL;
        if (cancelled(request, pd)) {
            emit(request, true, "operation cancelled");
            status = 1;
            break;
        }
        record = xx_format_get_current_archive_record(opened.format, state);
        if (!record) break;
        if (request->extract_selected) {
            if (visited != request->selected_records[selected_cursor]) goto next_record;
            ++selected_cursor;
        }
        memset(&entry, 0, sizeof(entry));
        entry.name = record_name(record, &owned);
        entry.packed_size = record->compressed_size;
        entry.unpacked_size = xx_archive_record_get_meta_i64(
            record, XX_META_ID_UNCOMPRESSED_SIZE, -1);
        entry.is_directory = xx_archive_record_get_meta_bool(
            record, XX_META_ID_IS_FOLDER, false);
        entry.is_encrypted = xx_archive_record_get_meta_bool(
            record, XX_META_ID_IS_ENCRYPTED, false);
        if (entry.is_encrypted)
            encryption_detected(request, &reported_encryption, entry.name);
        entry.embedded_password = xx_archive_record_get_meta_str(
            record, XX_META_ID_PASSWORD);
        /* The 7z reader's time, like 7-Zip's (and xfu's 7z writers'), is a
         * FILETIME. */
        member_metadata(record, &entry, xxfc_reader_uses_helper(&opened) ||
                        (opened.reader_name && !strcmp(opened.reader_name, "7zip")));
        if (!unpack_to && !testing)
            emit(request, false, "  %12lld  %s%s",
                 (long long)entry.packed_size, entry.name,
                 entry.is_encrypted ? " [encrypted]" : "");
        if (request->callbacks.entry) {
            resolve_zip_record(&opened, record);
            if (!xfu_record_properties(record, opened.type == XX_FILE_TYPE_ZIP || opened.type == XX_FILE_TYPE_ZIP64,
                                       &properties, &entry.property_count)) {
                emit(request, true, "cannot allocate member metadata");
                xx_str_free(owned); status = 2; break;
            }
            entry.properties = properties;
            request->callbacks.entry(request->callbacks.user, &entry);
            xfu_free_properties(properties, entry.property_count);
            entry.properties = NULL;
            entry.property_count = 0;
        }
        progress(request, total, known_total, entry.name);
        ++total;
        if (unpack_to || testing) {
            bool success, cut, unsafe, itself, root_folder, password_failure = false;
            bool encrypted = entry.is_encrypted;
            char *destination = NULL;
            char *output_name = unpack_to ? mapped_extract_name(opened.type, entry.name, (size_t)visited) : NULL;
            path_stamp before = {0};
            unsigned long refusals;
            int level;
            if (cancelled(request, pd)) {
                emit(request, true, "operation cancelled");
                --total; /* Never started: not one of the members done. */
                status = 1;
                xx_str_free(output_name);
                xx_str_free(owned);
                break;
            }
            if (testing) {
                monitor.completed = total - 1;
                monitor.member_size = entry.unpacked_size;
                monitor.member_packed = entry.packed_size;
                for (level = 0; level < XX_PD_LEVELS; ++level)
                    monitor.busy_before[level] = pd->records[level].is_busy;
                monitor.active = true;
            }
            /* ZIPs written with a root entry: the output folder itself. */
            root_folder = output_name && names_output_folder(output_name) && (entry.is_directory || entry.unpacked_size == 0);
            unsafe = unpack_to && !root_folder && !safe_extract_name(output_name);
            success = !unpack_to || !unsafe;
            itself = false;
            if (success && unpack_to) {
                /* What is at the member's path now, and no failed change yet:
                 * for the reason of a failure (failure_reason). */
                bool split = splits_backslash(opened.type);
                if (!entry.is_directory &&
                    (destination = member_destination(unpack_to, output_name, split)) != NULL)
                    stamp_path(destination, &before);
                /* The archive is never replaced by one of its members,
                 * whether the reader splits a '\' in its name or keeps it. */
                itself = before.present && is_archive_file(destination, request->archive_path);
                if (!itself && destination && strchr(output_name, '\\')) {
                    char *other = member_destination(unpack_to, output_name, !split);
                    itself = other && is_archive_file(other, request->archive_path);
                    free(other);
                }
#if XFU_MAPPED_NAMES
                /* A name collision may give the reader a stable record
                 * suffix. Protect that possible destination as well. */
                if (!itself && output_name && mapped_name_can_suffix(opened.type)) {
                    char *suffixed = xx_str_dup(output_name);
                    if (suffixed && xx_archive_output_suffix(&suffixed, (size_t)visited)) {
                        char *other = member_destination(unpack_to, suffixed, split);
                        itself = other && is_archive_file(other, request->archive_path);
                        free(other);
                    }
                    xx_str_free(suffixed);
                }
#endif

                if (itself) success = false;
                xx_io_extract_clear();
            }
            refusals = xx_io_memory_only_refusals();
            if (success && !root_folder)
                success = xx_format_unpack_current_archive_record(opened.format, state, pd);
            monitor.active = false;
            if (testing && cancelled(request, pd)) success = false;
            cut = !success && cancelled(request, pd);
            if (!success && !cut)
                entry.failure = unsafe ? "unsafe member path" :
                    itself ? "it would replace the archive" :
                    file_named_as_folder(opened.type, &entry) ? "the name ends in a separator" :
                    failure_reason(request, opened.type, entry.name, encrypted, destination,
                                   &before, xx_io_memory_only_refusals() != refusals, &password_failure);
            if (password_failure)
                encryption_blocked(request, &reported_encryption, &reported_password_failure, entry.name);
#ifdef XFU_SUPPORT_FORMAT_TAR
            if (success && unverified_note && xx_tar_compressed_last_unverified(opened.format))
                unverified = true;
            /* In a cut compressed TAR, the member the cut runs through, or a
             * hard link to it: the reader wrote nothing for it, and left a
             * file at its path alone. Any other member fails as it would in
             * a whole archive. */
            if (!success && !cut && !unsafe && !itself && xx_tar_compressed_last_cut(opened.format) &&
                (!entry.failure || !strcmp(entry.failure, "already exists; not replaced")))
                entry.failure = "cut";
#endif

            free(destination);
            xx_str_free(output_name);
            if (cut) {
                /* Cut short by Cancel: not a defect of the member, so it is
                 * neither reported nor counted as failed. */
                emit(request, true, "  %s -- cancelled", entry.name);
                ++interrupted;
            } else if (testing && request->callbacks.test_result)
                request->callbacks.test_result(request->callbacks.user, &entry, success);
            else if (success && before.present)
                emit(request, false, "  %s -- replaced an existing file", entry.name);
            else if (success)
                emit(request, false, testing ? "  %s -- OK" : "  %s", entry.name);
            else if (entry.failure)
                emit(request, true, "  %s -- FAILED (%s)", entry.name, entry.failure);
            else
                emit(request, true, "  %s -- FAILED", entry.name);
            if (!success && !cut) {
                ++failed;
            }
        }
        progress(request, total, known_total, entry.name);
        if (testing && known_total && !cancelled(request, pd)) {
            unsigned percent = (unsigned)(total * 100U / known_total);
            test_percent(&monitor, percent < 100 ? percent : 99);
        }
        xx_str_free(owned);
        if (request->extract_selected && selected_cursor == request->selected_record_count) break;
next_record:
        ++visited;
        if (visited >= RECORD_LIMIT) {
            emit(request, true, "%s: stopping after %d members", archive_path,
                 RECORD_LIMIT);
            status = 1;
            break;
        }
        if (testing) {
            bool next;
            /* A decoder error belongs to that member. Detect a fresh iterator
             * error even when it occurs after the advertised final member. */
            xx_pd_clear_error(pd);
            next = xx_format_archive_record_move_to_next(opened.format, state, pd);
            if (pd->last_error) {
                emit(request, true, xx_pd_is_stopped(pd) ? "operation cancelled" :
                     "archive iteration failed");
                status = 1; break;
            }
            if (!next) { reached_end = true; break; }
        } else if (!xx_format_archive_record_move_to_next(opened.format, state, pd)) {
            reached_end = true; break;
        }
    }
    xx_format_free_archive_records_reading(opened.format, state);
    xx_list_cleanup(&options);
    if (testing) {
        xx_pd_set_observer(monitor.previous.progress, monitor.previous.callback,
                           monitor.previous.user_data);
        if (known_total && total != known_total && !xx_pd_is_stopped(pd)) {
            emit(request, true, "archive iteration ended before all members were tested");
            status = 1;
        }
        if (!xx_pd_is_stopped(pd) && !status && !partial && !xxfc_is_incomplete(&opened) &&
            (!known_total || total == known_total)) test_percent(&monitor, 100);
    }
    if (failed)
        emit(request, testing, "%llu member(s), %llu failed",
             (unsigned long long)(total - interrupted), (unsigned long long)failed);
    else
        emit(request, false, "%llu member(s)", (unsigned long long)(total - interrupted));
    if (unverified_note)
        emit(request, true, unverified ?
             "%s: incomplete archive; members before the cut were read without a checksum" :
             "%s: incomplete archive; showing recovered members", archive_path);
    if (failed || xx_pd_is_stopped(pd)) status = 1;
    if (!status && request->extract_selected && selected_cursor != request->selected_record_count) {
        emit(request, true, "selected members are no longer present in the archive");
        status = 1;
    }
    if (!status && xxfc_is_incomplete(&opened)) {
        emit(request, true, "%s: incomplete archive; showing recovered members",
             archive_path);
        status = 1;
    }
    /* Not all of what the file holds was read (layer_too_large logged it). */
    if (partial && !status) status = 1;
    if (request->callbacks.walk_end)
        request->callbacks.walk_end(request->callbacks.user,
            reached_end && !request->extract_selected && !xx_pd_is_stopped(pd) &&
            (!known_total || total == known_total) && !xxfc_is_incomplete(&opened));
done:
    xxfc_close(&opened);
    xx_io_close(device);
    return status;
}

static int walk(const xfu_request *request, const char *unpack_to,
                xx_pd_struct *pd, test_monitor *stream) {
    int result = walk_device(request, unpack_to, pd, NULL, 0U, NULL, stream);
    stream_detach(stream);
    return result;
}

#if XFU_FORMAT_SUPPORT_FULL
static bool ends_with(const char *text, const char *suffix) {
    size_t n = strlen(text), m = strlen(suffix), i;
    if (m > n) return false;
    text += n - m;
    for (i = 0; i < m; ++i) {
        char a = text[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != suffix[i]) return false;
    }
    return true;
}

#endif

#if XFU_FORMAT_SUPPORT_FULL
static const char *writer_kind(const char *path) {
    static const struct { const char *suffix; const char *kind; } table[] = {
        { ".tar.gz", "tar.gz" }, { ".tgz", "tar.gz" },
        { ".tar.bz2", "tar.bz2" }, { ".tbz2", "tar.bz2" },
        { ".tar.xz", "tar.xz" }, { ".txz", "tar.xz" },
        { ".tar.zst", "tar.zst" }, { ".tar.lz4", "tar.lz4" },
        { ".tar", "tar" }, { ".zip", "zip" }, { ".cpio", "cpio" },
        { ".7z", "7z" }, { ".gz", "gz" }, { ".gzip", "gz" },
        { ".bz2", "bz2" }, { ".bzip2", "bz2" },
        { ".xz", "xz" }, { ".wim", "wim" }
    };
    size_t i;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
        if (ends_with(path, table[i].suffix)) return table[i].kind;
    return NULL;
}

static void compatibility_writer(const char *path, const char **format,
                                 const char **filter) {
    static const struct { const char *suffix, *name; } filters[] = {
        { ".gzip", "gzip" }, { ".gz", "gzip" },
        { ".bzip2", "bzip2" }, { ".bz2", "bzip2" },
        { ".xz", "xz" }, { ".lzma", "lzma" },
        { ".lzip", "lzip" }, { ".lz", "lzip" },
        { ".lz4", "lz4" }, { ".zstd", "zstd" }, { ".zst", "zstd" },
        { ".lzop", "lzop" }, { ".lzo", "lzop" }, { ".z", "compress" },
        { ".uue", "uuencode" }, { ".uu", "uuencode" }, { ".b64", "b64encode" }
    };
    static const struct { const char *suffix, *name; } formats[] = {
        { ".tar", "pax_restricted" }, { ".pax", "pax" }, { ".ustar", "ustar" },
        { ".cbt", "pax_restricted" },
        { ".zip", "zip" }, { ".zipx", "zip" }, { ".7z", "7zip" },
        { ".cb7", "7zip" }, { ".cbz", "zip" }, { ".smzip", "zip" },
        { ".jar", "zip" }, { ".ear", "zip" }, { ".war", "zip" },
        { ".apk", "zip" }, { ".xpi", "zip" }, { ".kmz", "zip" }, { ".epub", "zip" },
        { ".docx", "zip" }, { ".dotx", "zip" }, { ".xlsx", "zip" }, { ".xltx", "zip" },
        { ".pptx", "zip" }, { ".potx", "zip" }, { ".ppsx", "zip" },
        { ".odt", "zip" }, { ".ott", "zip" }, { ".ods", "zip" }, { ".ots", "zip" },
        { ".odg", "zip" }, { ".otg", "zip" }, { ".odp", "zip" }, { ".otp", "zip" },
        { ".odf", "zip" }, { ".odb", "zip" }, { ".oxt", "zip" },
        { ".cpio", "cpio_newc" }, { ".ar", "ar_bsd" }, { ".a", "ar_bsd" },
        { ".deb", "ar_bsd" }, { ".mtree", "mtree" }, { ".iso", "iso9660" },
        { ".shar", "shar" }, { ".warc", "warc" }, { ".xar", "xar" }
    };
    static const struct { const char *suffix, *filter; } compressed_tar[] = {
        { ".tgz", "gzip" }, { ".tpz", "gzip" },
        { ".tbz", "bzip2" }, { ".tbz2", "bzip2" }, { ".tb2", "bzip2" },
        { ".tbzip", "bzip2" }, { ".tbzip2", "bzip2" },
        { ".txz", "xz" }, { ".tlz", "lzma" }, { ".tzst", "zstd" },
        { ".taz", "compress" }, { ".tz", "compress" }
    };
    char *base;
    size_t i;
    *format = NULL;
    *filter = "none";
    for (i = 0; i < sizeof(compressed_tar) / sizeof(compressed_tar[0]); ++i) {
        if (ends_with(path, compressed_tar[i].suffix)) {
            *format = "pax_restricted";
            *filter = compressed_tar[i].filter;
            return;
        }
    }
    base = (char *)malloc(strlen(path) + 1);
    if (!base) return;
    strcpy(base, path);
    for (i = 0; i < sizeof(filters) / sizeof(filters[0]); ++i) {
        if (!ends_with(base, filters[i].suffix)) continue;
        base[strlen(base) - strlen(filters[i].suffix)] = '\0';
        *filter = filters[i].name;
        *format = "raw";
        break;
    }
    for (i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        if (ends_with(base, formats[i].suffix)) {
            *format = formats[i].name;
            break;
        }
    }
    free(base);
}

#endif

#if XFU_FORMAT_SUPPORT_FULL || defined(_WIN32)
static bool same_file(const char *left, const char *right) {
#ifdef _WIN32
    wchar_t *a = xx_str_utf8_to_unicode(left);
    wchar_t *b = xx_str_utf8_to_unicode(right);
    HANDLE ah = INVALID_HANDLE_VALUE, bh = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION ai, bi;
    bool same = false;
    if (a && b) {
        ah = CreateFileW(a, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        bh = CreateFileW(b, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (ah != INVALID_HANDLE_VALUE && bh != INVALID_HANDLE_VALUE &&
            GetFileInformationByHandle(ah, &ai) &&
            GetFileInformationByHandle(bh, &bi))
            same = ai.dwVolumeSerialNumber == bi.dwVolumeSerialNumber &&
                   ai.nFileIndexHigh == bi.nFileIndexHigh &&
                   ai.nFileIndexLow == bi.nFileIndexLow;
        if (!same) {
            DWORD an = GetFullPathNameW(a, 0, NULL, NULL);
            DWORD bn = GetFullPathNameW(b, 0, NULL, NULL);
            wchar_t *af = (wchar_t *)malloc((size_t)an * sizeof(wchar_t));
            wchar_t *bf = (wchar_t *)malloc((size_t)bn * sizeof(wchar_t));
            if (an && bn && af && bf && GetFullPathNameW(a, an, af, NULL) &&
                GetFullPathNameW(b, bn, bf, NULL))
                same = CompareStringOrdinal(af, -1, bf, -1, TRUE) == CSTR_EQUAL;
            free(af);
            free(bf);
        }
    }
    if (ah != INVALID_HANDLE_VALUE) CloseHandle(ah);
    if (bh != INVALID_HANDLE_VALUE) CloseHandle(bh);
    xx_str_wfree(a);
    xx_str_wfree(b);
    return same;
#else
    struct stat a, b;
    if (stat(left, &a) == 0 && stat(right, &b) == 0)
        return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
    return strcmp(left, right) == 0;
#endif
}

#endif

/* The suffix-selected xxfclib writer, filling the stage with the members
 * xfu_native_create_with walked: relative subdirectories are kept, and an
 * absolute or parent-relative input keeps only its last component. */
#if XFU_FORMAT_SUPPORT_FULL
typedef struct add_writer {
    const char *kind;
    const xx_list_s *options;
} add_writer;

/* The xxfclib TAR writers write ustar headers: a name of at most 100 bytes,
 * or one split at a '/' into at most 155 and 100 bytes (256 in all). */
static bool add_ustar_name(const char *name) {
    size_t length = strlen(name), cursor = length;
    if (length <= 100U) return true;
    while (cursor--)
        if (name[cursor] == '/' && cursor && cursor <= 155U &&
            length - cursor - 1U && length - cursor - 1U <= 100U) return true;
    return false;
}

static bool add_members(void *context, const xfu_request *request,
                        const xfu_native_member *members, size_t count,
                        xx_io_device *output, xx_pd_struct *pd) {
    const add_writer *writer = (const add_writer *)context;
    xxfc_release_fn release = NULL;
    Abstractformat *format = xxfc_make_writer(writer->kind, output, &release);
    xx_archive_write_state *state = NULL;
    bool encrypt = request->password && request->password[0] && !strcmp(writer->kind, "zip");
    bool success = false;
    size_t i;
    /* Refused before anything is written, with the reason the writer would
     * not give. */
    for (i = 0; i < count; ++i) {
        char reason[256];
        if (!strncmp(writer->kind, "tar", 3U) && !add_ustar_name(members[i].name)) {
            emit(request, true, "%s: the name does not fit the ustar header of %s (100 bytes, "
                 "or 256 split at a '/' into 155 and 100); --format=pax stores it",
                 members[i].path, writer->kind);
            if (format) release(format);
            return false;
        }
        if (!xfu_native_xxfclib_member_fits(writer->kind, writer->kind, &members[i],
                                            reason, sizeof(reason))) {
            emit(request, true, "%s: %s", members[i].path, reason);
            if (format) release(format);
            return false;
        }
    }
    if (!format) {
        emit(request, true, "%s: cannot start writing", request->archive_path);
        return false;
    }
    if (request->password && !xx_format_set_password(format, request->password)) {
        emit(request, true, "cannot set archive password");
        goto done;
    }
    state = xx_format_create_archive_records_writing(format, writer->options, pd);
    if (!state) {
        if (!cancelled(request, pd))
            emit(request, true, "%s: cannot start writing", request->archive_path);
        goto done;
    }
    for (i = 0; i < count; ++i) {
        const xfu_native_member *member = &members[i];
        xx_io_device *source = NULL;
        xx_archive_record record;
        bool packed;
        if (cancelled(request, pd)) goto done;
        progress(request, (uint64_t)i, (uint64_t)count, member->path);
        /* The file the walk found, never one swapped in since. */
        if (!member->directory && !(source = xfu_native_open_member(member))) {
            char why[128];
            emit(request, true, "%s: %s", member->path,
                 xfu_native_open_failure(errno, why, sizeof(why)));
            goto done;
        }
        xfu_native_record_warnings(request, writer->kind, writer->kind, member);
        xx_archive_record_init(&record);
        packed = xx_archive_record_set_original_name(&record, member->name) &&
            (!member->directory ||
             xx_archive_record_set_meta_bool(&record, XX_META_ID_IS_FOLDER, true)) &&
            xfu_native_record_metadata(&record, writer->kind, member) &&
            (!encrypt || member->directory ||
             xx_archive_record_set_meta_bool(&record, XX_META_ID_IS_ENCRYPTED, true)) &&
            xx_format_pack_archive_record(format, state, &record, source, pd);
        xx_archive_record_cleanup(&record);
        if (source && xx_io_close(source) != 0) packed = false;
        if (!packed) {
            /* A failed write is said as such: a writer may give its own,
             * unrelated reason after it (the single-stream ones do). */
            char failure[160];
            const char *written = xfu_native_output_failure(output, failure, sizeof(failure));
            if (!cancelled(request, pd))
                emit(request, true, "%s: cannot pack member: %s", member->path,
                     written ? written : pd->error_string[0] ? pd->error_string :
                     "the writer failed (a read or write error, or one of its limits)");
            goto done;
        }
        progress(request, (uint64_t)i + 1, (uint64_t)count, member->path);
    }
    if (cancelled(request, pd)) goto done;
    success = xx_format_finalize_archive_records_writing(format, state, pd);
    if (!success && !cancelled(request, pd)) {
        char failure[160];
        const char *written = xfu_native_output_failure(output, failure, sizeof(failure));
        if (!written) written = pd->error_string;
        emit(request, true, "%s: could not be finished%s%s", request->archive_path,
             written[0] ? ": " : "", written);
    }
done:
    if (state) xx_format_free_archive_records_writing(format, state);
    release(format);
    return success;
}

#endif

#if XFU_FORMAT_SUPPORT_FULL
static int add(const xfu_request *given, xx_pd_struct *pd) {
    /* An empty password (-p) protects nothing: an archive is written without
     * one, whatever its format. */
    xfu_request unprotected = *given;
    const xfu_request *request = given;
    const char *archive_path = request->archive_path;
    const char *kind = writer_kind(archive_path);
    xx_io_device *probe;
    Abstractformat *format;
    xxfc_release_fn release = NULL;
    xx_archive_write_state *state;
    size_t i;
    xx_meta compression_meta[2];
    xx_list_s compression_options = {0};
    const xx_list_s *write_options = NULL;
    const char *compat_format, *compat_filter;
    bool use_compatibility;
    static const char *const specialized[] = {
        "balz", "bcm", "quad", "lpaq1", "lpaq5", "lpaq8",
        "paq8", "paq8f", "paq8jd", "paq8l", "paq8o"
    };
    bool explicit_wim = request->archive_format && !strcmp(request->archive_format, "wim");
    if (given->password && !given->password[0]) {
        unprotected.password = NULL;
        request = &unprotected;
    }
    if ((request->archive_format && !strcmp(request->archive_format, "split")) ||
        (!request->archive_format && xfu_split_is_first_volume(archive_path)))
        return xfu_split_run(request, pd);
    if (request->volume_size) {
        emit(request, true, "--volume-size requires split output");
        return 2;
    }
    if (request->archive_format && !strcmp(request->archive_format, "7zip_sfx"))
        return xfu_sfx_create(request, pd);
    if (request->archive_format && !strcmp(request->archive_format, "freearc_sfx"))
        return xfu_freearc_sfx_create(request, pd);
    if ((request->archive_format && !strcmp(request->archive_format, "upx")) ||
        (!request->archive_format && ends_with(archive_path, ".upx")))
        return xfu_upx_create(request, pd);
    if ((request->archive_format && !strcmp(request->archive_format, "pea")) ||
        (!request->archive_format && ends_with(archive_path, ".pea")))
        return xfu_pea_create(request, pd);
    if ((request->archive_format && (!strcmp(request->archive_format, "freearc") ||
                                    !strcmp(request->archive_format, "arc") ||
                                    !strcmp(request->archive_format, "wrc"))) ||
        (!request->archive_format && (ends_with(archive_path, ".arc") ||
                                     ends_with(archive_path, ".wrc"))))
        return xfu_freearc_create(request, pd);
    for (i = 0; i < sizeof(specialized) / sizeof(specialized[0]); ++i) {
        char extension[16];
        snprintf(extension, sizeof(extension), ".%s", specialized[i]);
        if ((request->archive_format && !strcmp(request->archive_format, specialized[i])) ||
            (!request->archive_format && ends_with(archive_path, extension)))
            return xfu_peazip_create(request,
                !strcmp(specialized[i], "paq8") ? "paq8o" : specialized[i], pd);
    }
    if ((request->archive_format && (!strcmp(request->archive_format, "brotli") ||
                                    !strcmp(request->archive_format, "br") ||
                                    !strcmp(request->archive_format, "zpaq"))) ||
        (!request->archive_format && (ends_with(archive_path, ".br") ||
                                     ends_with(archive_path, ".zpaq"))))
        return xfu_peazip_create(request, request->archive_format ? request->archive_format :
                                (ends_with(archive_path, ".br") ? "brotli" : "zpaq"), pd);
    if (explicit_wim) {
        kind = "wim";
        if ((request->archive_filter && strcmp(request->archive_filter, "none")) || request->archive_options) {
            emit(request, true, "WIM uses --compression and --compression-level; output filters/options are unavailable");
            return 2;
        }
    }
    compatibility_writer(archive_path, &compat_format, &compat_filter);
    use_compatibility = (request->archive_format && !explicit_wim) ||
                        (request->archive_filter && !explicit_wim) ||
                        request->archive_options || !kind ||
                        (compat_format && strcmp(compat_format, "raw") &&
                         (!strcmp(kind, "gz") || !strcmp(kind, "bz2") || !strcmp(kind, "xz"))) ||
                        ((request->compression_method || request->compression_level_set) &&
                         strcmp(kind, "wim"));
    for (i = 0; i < request->file_count; ++i) {
        if (same_file(archive_path, request->files[i])) {
            emit(request, true, "%s: cannot add the archive to itself", archive_path);
            return 2;
        }
    }
    if (use_compatibility) {
        if (request->archive_format) compat_format = request->archive_format;
        if (request->archive_filter) compat_filter = request->archive_filter;
        if (!compat_format) {
            emit(request, true, "%s: unknown output format; select --format (see --capabilities)", archive_path);
            return 2;
        }
        return xfu_native_create(request, compat_format, compat_filter, pd);
    }
    if (!kind) {
        emit(request, true,
             "%s: xxfclib cannot write this container. Writable: .tar "
             ".tar.gz .tar.bz2 .tar.xz .tar.zst .tar.lz4 .zip .cpio .7z .gz .bz2 .xz .wim", archive_path);
        return 2;
    }
    if ((!strcmp(kind, "gz") || !strcmp(kind, "bz2") || !strcmp(kind, "xz")) &&
        request->file_count != 1) {
        emit(request, true, "%s: a compressed stream requires exactly one input file; use .tar.%s for multiple files", archive_path, kind);
        return 2;
    }
    if (request->compression_method || request->compression_level_set) {
        if (strcmp(kind, "wim")) {
            emit(request, true, "compression options require a .wim destination");
            return 2;
        }
    }
    if (!strcmp(kind, "wim")) {
        const char *method = request->compression_method ? request->compression_method : "stored";
        uint64_t value;
#ifdef XFU_XXFCLIB_ONLY
        if (strcmp(method, "stored")) {
            emit(request, true,
                 "the xxfclib-only build supports stored WIM creation; external WIM compressors are disabled");
            return 2;
        }
#endif
        if (!strcmp(method, "stored")) value = 0;
        else if (!strcmp(method, "xpress")) value = 1;
        else if (!strcmp(method, "lzx")) value = 2;
        else if (!strcmp(method, "lzms")) value = 3;
        else { emit(request, true, "unknown WIM compression method: %s", method); return 2; }
        if (request->compression_level_set &&
            (request->compression_level < 0 || request->compression_level > 100 ||
             (!value && request->compression_level != 0))) {
            emit(request, true, "WIM compression level must be 0..100; stored permits only 0"); return 2;
        }
        xx_meta_init(&compression_meta[0], XX_META_ID_COMPRESSION_METHOD);
        xx_var_set_u64(&compression_meta[0].var, value);
        compression_options.data = (uint8_t *)compression_meta;
        compression_options.elem_size = sizeof(xx_meta);
        compression_options.capacity = 2;
        compression_options.count = 1;
        if (request->compression_level_set) {
            xx_meta_init(&compression_meta[1], XX_META_ID_COMPRESSION_LEVEL);
            xx_var_set_u64(&compression_meta[1].var, (uint64_t)request->compression_level);
            compression_options.count = 2;
        }
        write_options = &compression_options;
    }
    if (request->password && strcmp(kind, "7z") && strcmp(kind, "zip")) {
        emit(request, true, "%s: password protection is supported for .7z and .zip", archive_path);
        return 2;
    }
    if (cancelled(request, pd)) {
        emit(request, true, "operation cancelled");
        return 1;
    }
    /* Resolve the built-in writer before reading any input. */
    probe = xx_io_mem_open(NULL, 0);
    format = probe ? xxfc_make_writer(kind, probe, &release) : NULL;
    if (!format) {
        if (probe) xx_io_close(probe);
        emit(request, true,
             "%s: xxfclib cannot write this container. Writable: .tar "
             ".tar.gz .tar.bz2 .tar.xz .tar.zst .tar.lz4 .zip .cpio .7z .gz .bz2 .xz .wim", archive_path);
        return 2;
    }
    release(format);
    xx_io_close(probe);
    if (write_options) {
        uint8_t header[208];
        bool ready;
        probe = xx_io_mem_open(header, sizeof(header));
        format = probe ? xxfc_make_writer(kind, probe, &release) : NULL;
        state = format ? xx_format_create_archive_records_writing(format, write_options, pd) : NULL;
        ready = state != NULL;
        if (state) xx_format_free_archive_records_writing(format, state);
        if (format) release(format);
        if (probe) xx_io_close(probe);
        if (!ready) {
            emit(request, true, "%s: cannot initialize WIM compression: %s", archive_path,
                 pd && pd->error_string[0] ? pd->error_string : "compression helper unavailable");
            return xx_pd_is_stopped(pd) ? 1 : 2;
        }
    }
    /* As the native writers: the inputs are walked (folders with what they
     * hold) and checked, written once each, and the archive replaces the
     * destination only when it is complete. */
    {
        add_writer writer;
        unsigned flags = XFU_NATIVE_WITH_FOLDERS;
        writer.kind = kind;
        writer.options = write_options;
        /* gz, bz2 and xz hold one file and no name; the xxfclib TAR, ZIP,
         * 7z and WIM writers store UTF-8 names only, and CPIO the bytes. */
        if (!strcmp(kind, "gz") || !strcmp(kind, "bz2") || !strcmp(kind, "xz"))
            flags = XFU_NATIVE_WITH_STREAM;
        else if (strcmp(kind, "cpio")) flags |= XFU_NATIVE_WITH_UTF8;
        return xfu_native_create_with(request, kind, flags, add_members, &writer, pd);
    }
}

#else
/* Reuse the native writer's traversal and atomic publication, constrained
 * to the compiled families. Full-engine helpers are not linked here. */
static int add(const xfu_request *given, xx_pd_struct *pd) {
    xfu_request request = *given;
    const char *format = request.archive_format;
    const char *filter = request.archive_filter;
    const char *path = request.archive_path;
    size_t length = strlen(path);
    static const struct { const char *suffix, *format, *filter; } outputs[] = {
        {".tar.gz", "tar", "gzip"}, {".tgz", "tar", "gzip"},
        {".tar.bz2", "tar", "bzip2"}, {".tar.xz", "tar", "xz"},
        {".tbz2", "tar", "bzip2"}, {".tbz", "tar", "bzip2"},
        {".txz", "tar", "xz"}, {".tar.lzma", "tar", "lzma"},
        {".tar.lz", "tar", "lzip"}, {".tlz", "tar", "lzip"},
        {".tar.lz4", "tar", "lz4"}, {".tar.lzo", "tar", "lzop"},
        {".tar.Z", "tar", "compress"}, {".taz", "tar", "compress"},
        {".tzst", "tar", "zstd"}, {".tar.zstd", "tar", "zstd"},
        {".tar.zst", "tar", "zstd"}, {".tar", "tar", "none"},
        {".zip", "zip", "none"}, {".7z", "7zip", "none"},
        {".cpio", "cpio", "none"}, {".gz", "raw", "gzip"},
        {".bz2", "raw", "bzip2"}, {".xz", "raw", "xz"},
        {".zst", "raw", "zstd"}
    };
    size_t i;
    for (i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
        size_t n = strlen(outputs[i].suffix);
        if (length >= n && !strcmp(path + length - n, outputs[i].suffix)) {
            if (!format) format = outputs[i].format;
            if (!filter) filter = outputs[i].filter;
            break;
        }
    }
    if (!format) format = "zip";
    if (!filter) filter = "none";
    if (request.volume_size || !xfu_native_can_create_format(format) ||
        !xfu_native_can_create_filter(filter)) {
        emit(&request, true, "output format/filter is unavailable in this format profile");
        return 2;
    }
    if (request.password && !request.password[0]) request.password = NULL;
    return xfu_native_create(&request, format, filter, pd);
}
#endif

static int walk_observed(const xfu_request *request, const char *unpack_to,
                         xx_pd_struct *pd) {
    test_monitor observer = {0};
    int result;
    /* Native readers may decode during validation, before member iteration. */
    if (request->callbacks.cancelled) {
        observer.request = request;
        observer.progress = pd;
        observer.previous = xx_pd_set_observer(pd, test_observe, &observer);
    }
    result = walk(request, unpack_to, pd,
                  request->callbacks.cancelled ? &observer : NULL);
    if (request->callbacks.cancelled)
        xx_pd_set_observer(observer.previous.progress, observer.previous.callback,
                           observer.previous.user_data);
    if (result && pd->is_stop) result = 1;
    return result;
}

int xfu_run(const xfu_request *request) {
    xfu_request resolved_request;
    xx_pd_struct local = xx_pd_init();
    xx_pd_struct *pd;
    size_t i;
    if (!request) return 2;
    if (request->handle_type != XX_HANDLE_TYPE_UNKNOWN) {
        xx_file_type_t interpretation = xx_format_handle_type_file_type(request->handle_type);
        if (interpretation == XX_FILE_TYPE_UNKNOWN ||
            (request->file_type != XX_FILE_TYPE_UNKNOWN && request->file_type != interpretation)) {
            emit(request, true, "invalid or conflicting unpack handle selection");
            return 2;
        }
        resolved_request = *request;
        resolved_request.file_type = interpretation;
        request = &resolved_request;
    }
    if (!request->archive_path || !request->archive_path[0] ||
        request->command < XFU_COMMAND_EXTRACT || request->command > XFU_COMMAND_ADD) {
        emit(request, true, "invalid archive operation");
        return 2;
    }
    if (request->command != XFU_COMMAND_ADD &&
        (request->archive_format || request->archive_filter ||
         request->compression_method || request->compression_level_set || request->volume_size)) {
        emit(request, true, "format, filter, compression and volume options require a");
        return 2;
    }
    if (request->command == XFU_COMMAND_ADD &&
        (request->reader_name || request->file_type != XX_FILE_TYPE_UNKNOWN)) {
        emit(request, true, "a: select an output format with --format");
        return 2;
    }
    if (request->reader_name && request->file_type != XX_FILE_TYPE_UNKNOWN) {
        emit(request, true, "select either a reader or an unpack type");
        return 2;
    }
    if (request->command == XFU_COMMAND_ADD) {
        if (!request->files || !request->file_count) {
            emit(request, true, "a: name at least one file to add");
            return 2;
        }
        for (i = 0; i < request->file_count; ++i)
            if (!request->files[i] || !request->files[i][0]) {
                emit(request, true, "a: name at least one file to add");
                return 2;
            }
    }
    if (request->extract_selected) {
        if (request->command != XFU_COMMAND_EXTRACT || !request->selected_records ||
            !request->selected_record_count) {
            emit(request, true, "select at least one member to extract"); return 2;
        }
        for (i = 0; i < request->selected_record_count; ++i)
            if (request->selected_records[i] >= RECORD_LIMIT ||
                (i && request->selected_records[i] <= request->selected_records[i - 1])) {
                emit(request, true, "invalid member selection"); return 2;
            }
    }
    if (request->command == XFU_COMMAND_EXTRACT && request->output_dir &&
        !request->output_dir[0]) {
        emit(request, true, "-o: name an output directory");
        return 2;
    }
    pd = request->progress_state ? request->progress_state : &local;
    switch (request->command) {
        case XFU_COMMAND_EXTRACT: {
            /* The output folder, and the folders above it, may be links (as
             * /tmp is on macOS); what extraction makes or meets below it is
             * never followed, nor a link already at a member's path. */
            xx_io_extract_scope scope = {0};
            const char *root = request->output_dir ? request->output_dir : ".";
            int status;
            if (!xx_io_extract_begin(&scope, root)) {
                emit(request, true, "cannot start extraction");
                return 2;
            }
            status = walk_observed(request, root, pd);
            xx_io_extract_end(&scope);
            return status;
        }
        case XFU_COMMAND_LIST:
            return walk_observed(request, NULL, pd);
        case XFU_COMMAND_TEST: {
            xx_io_memory_only_scope scope = {0};
            int status;
            if (cancelled(request, pd)) {
                emit(request, true, "operation cancelled");
                return 1;
            }
            /* All reader setup, decoding and cleanup run under this policy.
             * Seekable decoder workspaces live in RAM; disk fallback is forbidden. */
            if (!xx_io_memory_only_begin(&scope, UINT64_C(256) * 1024U * 1024U)) {
                emit(request, true, "cannot start in-memory archive verification");
                return 2;
            }
            xx_pd_clear_error(pd);
            status = walk_observed(request, NULL, pd);
            if (!xx_io_memory_only_end(&scope)) {
                const char *reason = "invalid temporary workspace state";
                switch (xx_io_memory_only_error(&scope)) {
                    case XX_IO_MEMORY_ONLY_DISK_WRITE:
                        reason = "reader attempted a filesystem mutation"; break;
                    case XX_IO_MEMORY_ONLY_LIMIT:
                        reason = "temporary workspace exceeds the 256 MiB RAM limit"; break;
                    case XX_IO_MEMORY_ONLY_ALLOCATION:
                        reason = "cannot allocate temporary workspace in RAM"; break;
                    default: break;
                }
                emit(request, true, "in-memory verification failed: %s", reason);
                if (!status) status = 1;
            }
            return status;
        }
        case XFU_COMMAND_ADD:
            return add(request, pd);
        default:
            return 2;
    }
}
