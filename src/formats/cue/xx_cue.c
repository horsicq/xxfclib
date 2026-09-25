/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CDRWIN cue sheet.  xx_cue.h carries the statement table and the member
 * layout.  Written from the cue-sheet conventions: FILE / TRACK / INDEX
 * statements, mm:ss:ff at 75 frames a second, one sector size per track
 * mode.  libmirage's image-cue parser (GPL) was read only to confirm how
 * producers use the sheet - an INDEX 01 past 00:00:00 with no INDEX 00 before
 * it means the pregap is stored in the file, an INDEX 00 may sit in the FILE
 * before the one that holds its INDEX 01, and the byte position of an index
 * accumulates the sizes of the sectors in front of it.  No code was taken
 * from it.
 *
 * The sheet is plain text and holds no payload.  It is parsed completely and
 * strictly enough that ordinary text cannot pass: every track needs an
 * INDEX 01, track numbers rise, index numbers rise inside a track, and index
 * positions never go backwards inside one data file.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cue/xx_cue.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as CUE is registered there. */
#ifdef CUE
#define XX_CUE_FILE_TYPE XX_FILE_TYPE_CUE
#else
#define XX_CUE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The shortest complete sheet ("FILE a BINARY", "TRACK 1 AUDIO",
 * "INDEX 1 0:0:0") is 41 bytes. */
#define CUE_MIN_SHEET 32
#define CUE_PREFIX 64U
#define CUE_MAX_TRACKS 99U
#define CUE_MAX_INDEX 99U
/* 99 tracks rarely carry more than two or three indexes each. */
#define CUE_MAX_POINTS 1024U
#define CUE_MAX_NAME 1024U
#define CUE_MAX_TYPE 16U
#define CUE_MAX_MEMBERS (2U * CUE_MAX_TRACKS)
/* One segment per index point plus one leading segment per file. */
#define CUE_MAX_SEGMENTS (CUE_MAX_POINTS + XX_CUE_MAX_FILES)
#define CUE_MAX_MINUTES 999U
#define CUE_FRAMES_PER_SECOND 75U
#define CUE_AUDIO_FRAME 2352U
#define CUE_WAVE_MAX_CHUNKS 256U
#define CUE_COPY_CHUNK 65536U
#define CUE_MAX_BASENAME 255U

enum cue_file_type_e {
    CUE_FT_BINARY = 0,
    CUE_FT_MOTOROLA,
    CUE_FT_WAVE,
    CUE_FT_OTHER /* AIFF, MP3, FLAC, ...: listed, never decoded */
};

enum cue_kind_e {
    CUE_KIND_AUDIO = 0,
    CUE_KIND_CDG,
    CUE_KIND_COOKED, /* 2048-byte user data only */
    CUE_KIND_RAW
};

typedef struct cue_mode_s {
    const char *name;
    uint32_t sector_size;
    uint8_t kind;
} cue_mode;

/* The modes CDRWIN defines, plus the /2048 and /2448 spellings other
 * producers write for cooked mode 2 and for sectors with subchannel. */
static const cue_mode cue_modes[] = {
    {"AUDIO", 2352U, CUE_KIND_AUDIO},
    {"CDG", 2448U, CUE_KIND_CDG},
    {"MODE1/2048", 2048U, CUE_KIND_COOKED},
    {"MODE1/2352", 2352U, CUE_KIND_RAW},
    {"MODE1/2448", 2448U, CUE_KIND_RAW},
    {"MODE2/2048", 2048U, CUE_KIND_COOKED},
    {"MODE2/2324", 2324U, CUE_KIND_RAW},
    {"MODE2/2336", 2336U, CUE_KIND_RAW},
    {"MODE2/2352", 2352U, CUE_KIND_RAW},
    {"MODE2/2448", 2448U, CUE_KIND_RAW},
    {"CDI/2336", 2336U, CUE_KIND_RAW},
    {"CDI/2352", 2352U, CUE_KIND_RAW}
};

typedef struct cue_file_s {
    char *name;           /* as written, quotes removed */
    uint8_t type;
    uint32_t first_point;
    uint32_t point_count;
    /* Filled in once a data device is attached. */
    bool usable;
    int64_t base;         /* device offset of byte 0 of the track data */
    int64_t size;         /* bytes of track data available */
} cue_file;

typedef struct cue_track_s {
    uint32_t number;
    uint32_t mode;        /* index into cue_modes */
    int32_t last_index;   /* highest INDEX number so far, -1 before any */
    bool has_index1;
    int64_t line_offset;  /* of the TRACK statement, in the sheet */
    uint32_t line_size;
} cue_track;

typedef struct cue_point_s {
    uint32_t track;
    uint32_t index;
    uint32_t file;
    uint32_t frame;
} cue_point;

typedef struct cue_segment_s {
    uint32_t track;
    bool pregap;
    uint32_t file;
    uint32_t frame_size;  /* bytes per frame of the owning track */
    int64_t offset;       /* inside the file's track data */
    int64_t length;       /* -1: runs to the end of the file */
} cue_segment;

typedef struct cue_member_s {
    char name[24];        /* "track99.pregap.cdda" is the longest */
    uint32_t track;
    bool pregap;
    uint32_t first_segment;
    uint32_t segment_count;
    int64_t size;         /* -1: not known from the sheet, or does not fit */
} cue_member;

typedef struct cue_sheet_s {
    cue_file files[XX_CUE_MAX_FILES];
    uint32_t file_count;
    cue_track tracks[CUE_MAX_TRACKS];
    uint32_t track_count;
    cue_point points[CUE_MAX_POINTS];
    uint32_t point_count;
    cue_segment segments[CUE_MAX_SEGMENTS];
    uint32_t segment_count;
    cue_member members[CUE_MAX_MEMBERS];
    uint32_t member_count;
    uint32_t index;       /* record iteration cursor */
    int64_t sheet_size;
    xx_cue *owner;
} cue_sheet;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */

static bool cue_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static uint32_t cue_le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U) | ((uint32_t)b[2] << 16U) |
           ((uint32_t)b[3] << 24U);
}

static uint32_t cue_le16(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U);
}

static uint8_t cue_upper(uint8_t c) {
    return (c >= 'a' && c <= 'z') ? (uint8_t)(c - 'a' + 'A') : c;
}

static bool cue_blank(uint8_t c) { return c == ' ' || c == '\t'; }

static bool cue_digit(uint8_t c) { return c >= '0' && c <= '9'; }

/* Case-insensitive: do the @p size bytes at @p text spell @p word? */
static bool cue_word_is(const uint8_t *text, size_t size, const char *word) {
    size_t index;
    for (index = 0U; index < size; ++index)
        if (!word[index] || cue_upper(text[index]) != (uint8_t)word[index])
            return false;
    return word[size] == 0;
}

/* ---------------------------------------------------------------------- */
/* Prefilter                                                               */

bool xx_cue_test_magic(const uint8_t *magic, size_t magic_size) {
    static const char *const openers[] = {"REM",        "FILE",
                                          "TITLE",      "PERFORMER",
                                          "SONGWRITER", "CATALOG",
                                          "CDTEXTFILE"};
    size_t position = 0U, start, index;
    if (!magic || magic_size < 8U) return false;
    if (magic[0] == 0xEFU && magic[1] == 0xBBU && magic[2] == 0xBFU)
        position = 3U;
    while (position < magic_size &&
           (cue_blank(magic[position]) || magic[position] == '\r' ||
            magic[position] == '\n'))
        ++position;
    start = position;
    while (position < magic_size &&
           ((magic[position] >= 'A' && magic[position] <= 'Z') ||
            (magic[position] >= 'a' && magic[position] <= 'z')))
        ++position;
    /* The keyword must be followed by a blank (or end the line for REM)
     * inside the window; a keyword running to the window's end is not
     * enough evidence. */
    if (position == start || position >= magic_size) return false;
    for (index = 0U; index < sizeof(openers) / sizeof(openers[0]); ++index) {
        if (!cue_word_is(magic + start, position - start, openers[index]))
            continue;
        if (cue_blank(magic[position])) return true;
        return index == 0U &&
               (magic[position] == '\r' || magic[position] == '\n');
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* Sheet parser                                                            */

typedef struct cue_line_s {
    const uint8_t *text;
    size_t size;
    size_t position;
} cue_line;

/* Next blank-separated token; false at the end of the line. */
static bool cue_token(cue_line *line, const uint8_t **token, size_t *size) {
    size_t start;
    while (line->position < line->size && cue_blank(line->text[line->position]))
        ++line->position;
    if (line->position >= line->size) return false;
    start = line->position;
    while (line->position < line->size &&
           !cue_blank(line->text[line->position]))
        ++line->position;
    *token = line->text + start;
    *size = line->position - start;
    return true;
}

static bool cue_line_done(cue_line *line) {
    const uint8_t *token;
    size_t size;
    return !cue_token(line, &token, &size);
}

static bool cue_number(const uint8_t *text, size_t size, uint32_t max_digits,
                       uint32_t *value) {
    uint32_t result = 0U;
    size_t index;
    if (size == 0U || size > max_digits) return false;
    for (index = 0U; index < size; ++index) {
        if (!cue_digit(text[index])) return false;
        result = result * 10U + (uint32_t)(text[index] - '0');
    }
    *value = result;
    return true;
}

/* mm:ss:ff, minutes 1..3 digits, seconds < 60, frames < 75. */
static bool cue_msf(const uint8_t *text, size_t size, uint32_t *frames) {
    size_t first = 0U, second;
    uint32_t minutes, seconds, frame;
    while (first < size && text[first] != ':') ++first;
    if (first >= size) return false;
    second = first + 1U;
    while (second < size && text[second] != ':') ++second;
    if (second >= size) return false;
    if (!cue_number(text, first, 3U, &minutes) ||
        !cue_number(text + first + 1U, second - first - 1U, 2U, &seconds) ||
        !cue_number(text + second + 1U, size - second - 1U, 2U, &frame) ||
        minutes > CUE_MAX_MINUTES || seconds >= 60U ||
        frame >= CUE_FRAMES_PER_SECOND)
        return false;
    *frames = (minutes * 60U + seconds) * CUE_FRAMES_PER_SECOND + frame;
    return true;
}

static bool cue_parse_file(cue_sheet *sheet, cue_line *line) {
    const uint8_t *rest, *type;
    size_t rest_size, type_start, type_size, name_size, index;
    cue_file *file;
    while (line->position < line->size && cue_blank(line->text[line->position]))
        ++line->position;
    rest = line->text + line->position;
    rest_size = line->size - line->position;
    while (rest_size != 0U && cue_blank(rest[rest_size - 1U])) --rest_size;
    /* The type is the last token; the name is everything in front of it. */
    type_start = rest_size;
    while (type_start != 0U && !cue_blank(rest[type_start - 1U])) --type_start;
    if (type_start == 0U) return false;
    type = rest + type_start;
    type_size = rest_size - type_start;
    name_size = type_start;
    while (name_size != 0U && cue_blank(rest[name_size - 1U])) --name_size;
    if (type_size == 0U || type_size > CUE_MAX_TYPE || name_size == 0U)
        return false;
    for (index = 0U; index < type_size; ++index)
        if (!cue_digit(type[index]) && !(cue_upper(type[index]) >= 'A' &&
                                         cue_upper(type[index]) <= 'Z'))
            return false;
    if (rest[0] == '"') {
        if (name_size < 3U || rest[name_size - 1U] != '"') return false;
        ++rest;
        name_size -= 2U;
    }
    if (name_size > CUE_MAX_NAME || sheet->file_count >= XX_CUE_MAX_FILES)
        return false;
    file = &sheet->files[sheet->file_count];
    xx_mem_zero(file, sizeof(*file));
    file->name = (char *)xx_mem_alloc(name_size + 1U);
    if (!file->name) return false;
    xx_mem_copy(file->name, rest, name_size);
    file->name[name_size] = 0;
    if (cue_word_is(type, type_size, "BINARY")) file->type = CUE_FT_BINARY;
    else if (cue_word_is(type, type_size, "MOTOROLA")) file->type = CUE_FT_MOTOROLA;
    else if (cue_word_is(type, type_size, "WAVE")) file->type = CUE_FT_WAVE;
    else file->type = CUE_FT_OTHER;
    file->first_point = sheet->point_count;
    ++sheet->file_count;
    return true;
}

static bool cue_parse_track(cue_sheet *sheet, cue_line *line,
                            int64_t line_offset) {
    const uint8_t *token;
    size_t size, index;
    uint32_t number;
    cue_track *track;
    if (sheet->file_count == 0U || sheet->track_count >= CUE_MAX_TRACKS ||
        !cue_token(line, &token, &size) || !cue_number(token, size, 3U, &number) ||
        number < 1U || number > CUE_MAX_TRACKS)
        return false;
    if (sheet->track_count != 0U) {
        const cue_track *previous = &sheet->tracks[sheet->track_count - 1U];
        if (!previous->has_index1 || number <= previous->number) return false;
    }
    if (!cue_token(line, &token, &size)) return false;
    track = &sheet->tracks[sheet->track_count];
    xx_mem_zero(track, sizeof(*track));
    for (index = 0U; index < sizeof(cue_modes) / sizeof(cue_modes[0]); ++index)
        if (cue_word_is(token, size, cue_modes[index].name)) break;
    if (index == sizeof(cue_modes) / sizeof(cue_modes[0]) ||
        !cue_line_done(line))
        return false;
    track->number = number;
    track->mode = (uint32_t)index;
    track->last_index = -1;
    track->line_offset = line_offset;
    track->line_size = (uint32_t)line->size;
    ++sheet->track_count;
    return true;
}

static bool cue_parse_index(cue_sheet *sheet, cue_line *line) {
    const uint8_t *token;
    size_t size;
    uint32_t number, frames, file_index;
    cue_track *track;
    cue_file *file;
    cue_point *point;
    if (sheet->track_count == 0U || sheet->point_count >= CUE_MAX_POINTS ||
        !cue_token(line, &token, &size) || !cue_number(token, size, 2U, &number) ||
        number > CUE_MAX_INDEX || !cue_token(line, &token, &size) ||
        !cue_msf(token, size, &frames) || !cue_line_done(line))
        return false;
    track = &sheet->tracks[sheet->track_count - 1U];
    /* Indexes rise inside a track; the first one is 00 or 01, and the
     * first one past 00 is 01. */
    if ((int32_t)number <= track->last_index) return false;
    if (number >= 1U && !track->has_index1 && number != 1U) return false;
    file_index = sheet->file_count - 1U;
    file = &sheet->files[file_index];
    if (file->point_count != 0U &&
        frames < sheet->points[sheet->point_count - 1U].frame)
        return false;
    point = &sheet->points[sheet->point_count++];
    point->track = sheet->track_count - 1U;
    point->index = number;
    point->file = file_index;
    point->frame = frames;
    ++file->point_count;
    track->last_index = (int32_t)number;
    if (number == 1U) track->has_index1 = true;
    return true;
}

static bool cue_parse_line(cue_sheet *sheet, const uint8_t *text, size_t size,
                           int64_t line_offset) {
    cue_line line;
    const uint8_t *keyword, *token;
    size_t keyword_size, token_size;
    uint32_t frames;
    line.text = text;
    line.size = size;
    line.position = 0U;
    if (!cue_token(&line, &keyword, &keyword_size)) return true;
    if (cue_word_is(keyword, keyword_size, "FILE"))
        return cue_parse_file(sheet, &line);
    if (cue_word_is(keyword, keyword_size, "TRACK"))
        return cue_parse_track(sheet, &line, line_offset);
    if (cue_word_is(keyword, keyword_size, "INDEX"))
        return cue_parse_index(sheet, &line);
    if (cue_word_is(keyword, keyword_size, "PREGAP") ||
        cue_word_is(keyword, keyword_size, "POSTGAP")) {
        /* Silence the player generates: nothing of it is stored. */
        return sheet->track_count != 0U &&
               cue_token(&line, &token, &token_size) &&
               cue_msf(token, token_size, &frames) && cue_line_done(&line);
    }
    /* REM, CATALOG, CDTEXTFILE, TITLE, PERFORMER, SONGWRITER, FLAGS, ISRC
     * and producer extensions carry no layout. */
    return true;
}

static void cue_sheet_free(void *opaque) {
    cue_sheet *sheet = (cue_sheet *)opaque;
    uint32_t index;
    if (!sheet) return;
    for (index = 0U; index < sheet->file_count; ++index)
        if (sheet->files[index].name) xx_mem_free(sheet->files[index].name);
    xx_mem_free(sheet);
}

/* Text: printable bytes, tab and line breaks; bytes >= 0x80 pass as UTF-8
 * or code page names.  A DOS end-of-file byte ends the sheet when only
 * more of them and line breaks follow it. */
static bool cue_parse_text(cue_sheet *sheet, const uint8_t *text, size_t size,
                           int64_t base) {
    size_t position = 0U;
    bool end = false;
    if (size >= 3U && text[0] == 0xEFU && text[1] == 0xBBU && text[2] == 0xBFU)
        position = 3U;
    while (position < size && !end) {
        size_t start = position, stop;
        while (position < size && text[position] != '\n' &&
               text[position] != '\r') {
            uint8_t c = text[position];
            if (c == 0x1AU) {
                size_t tail;
                for (tail = position; tail < size; ++tail)
                    if (text[tail] != 0x1AU && text[tail] != '\r' &&
                        text[tail] != '\n')
                        return false;
                end = true;
                break;
            }
            if ((c < 0x20U && c != '\t') || c == 0x7FU) return false;
            ++position;
        }
        stop = position;
        if (!cue_parse_line(sheet, text + start, stop - start,
                            base + (int64_t)start))
            return false;
        if (end) break;
        if (position < size && text[position] == '\r') ++position;
        if (position < size && text[position] == '\n') ++position;
    }
    return sheet->file_count != 0U && sheet->track_count != 0U &&
           sheet->tracks[sheet->track_count - 1U].has_index1;
}

/* ---------------------------------------------------------------------- */
/* Layout                                                                  */

static uint32_t cue_frame_size(const cue_sheet *sheet, uint32_t file,
                               uint32_t track) {
    /* WAVE and the other audio containers count frames of their PCM. */
    if (sheet->files[file].type == CUE_FT_WAVE ||
        sheet->files[file].type == CUE_FT_OTHER)
        return CUE_AUDIO_FRAME;
    return cue_modes[sheet->tracks[track].mode].sector_size;
}

static const char *cue_extension(uint8_t kind, bool pregap) {
    switch (kind) {
    case CUE_KIND_AUDIO: return "cdda";
    case CUE_KIND_CDG: return "cdg";
    case CUE_KIND_COOKED: return pregap ? "bin" : "iso";
    default: return "bin";
    }
}

static void cue_make_name(cue_member *member, uint32_t number, uint8_t kind) {
    const char *suffix = cue_extension(kind, member->pregap);
    size_t used = 0U, index;
    static const char prefix[] = "track";
    static const char pregap[] = ".pregap.";
    for (index = 0U; prefix[index]; ++index) member->name[used++] = prefix[index];
    member->name[used++] = (char)('0' + (number / 10U) % 10U);
    member->name[used++] = (char)('0' + number % 10U);
    if (member->pregap) {
        for (index = 0U; pregap[index]; ++index)
            member->name[used++] = pregap[index];
    } else {
        member->name[used++] = '.';
    }
    for (index = 0U; suffix[index] && used < sizeof(member->name) - 1U; ++index)
        member->name[used++] = suffix[index];
    member->name[used] = 0;
}

/* Cut every data file at its index points.  A segment belongs to the track
 * of the point it starts at: to its pregap when that point is INDEX 00, to
 * the track body otherwise.  Bytes in front of a file's first point are the
 * pregap of that point's track (or body, past INDEX 01).  Segment lengths
 * accumulate the sector size of their own track, so a file may mix modes. */
static bool cue_build(cue_sheet *sheet) {
    cue_segment *raw;
    uint32_t raw_count = 0U, file, track;
    raw = (cue_segment *)xx_mem_calloc(CUE_MAX_SEGMENTS, sizeof(*raw));
    if (!raw) return false;
    for (file = 0U; file < sheet->file_count; ++file) {
        const cue_file *entry = &sheet->files[file];
        const cue_point *points = sheet->points + entry->first_point;
        int64_t cursor = 0;
        uint32_t index;
        if (entry->point_count == 0U) continue;
        if (points[0].frame != 0U) {
            cue_segment *segment = &raw[raw_count++];
            segment->track = points[0].track;
            segment->pregap = points[0].index <= 1U;
            segment->file = file;
            segment->frame_size = cue_frame_size(sheet, file, points[0].track);
            segment->offset = 0;
            segment->length = (int64_t)points[0].frame * segment->frame_size;
            cursor = segment->length;
        }
        for (index = 0U; index < entry->point_count; ++index) {
            cue_segment *segment = &raw[raw_count++];
            segment->track = points[index].track;
            segment->pregap = points[index].index == 0U;
            segment->file = file;
            segment->frame_size = cue_frame_size(sheet, file, points[index].track);
            segment->offset = cursor;
            if (index + 1U < entry->point_count) {
                segment->length =
                    (int64_t)(points[index + 1U].frame - points[index].frame) *
                    segment->frame_size;
                cursor += segment->length;
            } else {
                segment->length = -1;
            }
        }
    }
    sheet->segment_count = 0U;
    sheet->member_count = 0U;
    for (track = 0U; track < sheet->track_count; ++track) {
        int pass;
        for (pass = 0; pass < 2; ++pass) {
            bool pregap = pass == 0;
            cue_member *member = &sheet->members[sheet->member_count];
            uint32_t index, count = 0U;
            int64_t total = 0;
            bool unknown = false;
            member->first_segment = sheet->segment_count;
            for (index = 0U; index < raw_count; ++index) {
                if (raw[index].track != track || raw[index].pregap != pregap ||
                    raw[index].length == 0)
                    continue;
                sheet->segments[sheet->segment_count++] = raw[index];
                ++count;
                if (raw[index].length < 0) unknown = true;
                else total += raw[index].length;
            }
            /* A stored pregap is its own member; the body always is one. */
            if (pregap && count == 0U) continue;
            member->track = track;
            member->pregap = pregap;
            member->segment_count = count;
            member->size = unknown ? -1 : total;
            cue_make_name(member, sheet->tracks[track].number,
                          cue_modes[sheet->tracks[track].mode].kind);
            ++sheet->member_count;
        }
    }
    xx_mem_free(raw);
    return sheet->member_count != 0U;
}

/* ---------------------------------------------------------------------- */
/* Loading                                                                 */

static cue_sheet *cue_load(Abstractformat *format) {
    uint8_t prefix[CUE_PREFIX];
    uint8_t *text = NULL;
    cue_sheet *sheet = NULL;
    int64_t total, size;
    size_t prefix_size;
    if (!format || !format->device || format->base_address < 0) return NULL;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return NULL;
    size = total - format->base_address;
    if (size < CUE_MIN_SHEET || size > XX_CUE_MAX_SHEET) return NULL;
    prefix_size = size < (int64_t)CUE_PREFIX ? (size_t)size : CUE_PREFIX;
    if (!cue_read_at(format->device, format->base_address, prefix, prefix_size) ||
        !xx_cue_test_magic(prefix, prefix_size))
        return NULL;
    text = (uint8_t *)xx_mem_alloc((size_t)size);
    sheet = (cue_sheet *)xx_mem_calloc(1U, sizeof(*sheet));
    if (!text || !sheet ||
        !cue_read_at(format->device, format->base_address, text, (size_t)size) ||
        !cue_parse_text(sheet, text, (size_t)size, format->base_address) ||
        !cue_build(sheet)) {
        if (text) xx_mem_free(text);
        cue_sheet_free(sheet);
        return NULL;
    }
    xx_mem_free(text);
    sheet->sheet_size = size;
    sheet->owner = (xx_cue *)format;
    return sheet;
}

/* WAVE data file: RIFF, a CD-audio "fmt " chunk (PCM, 2 channels, 44.1 kHz,
 * 16 bits), then the "data" chunk whose PCM the frames count.  A data size
 * past the end of the file (streamed writers leave 0xFFFFFFFF) is clamped. */
static bool cue_wave_locate(xx_io_device *device, int64_t total, int64_t *base,
                            int64_t *size) {
    uint8_t head[12], chunk[8], format_chunk[40];
    int64_t position = 12;
    uint32_t count;
    bool have_format = false;
    if (total < 12 || !cue_read_at(device, 0, head, sizeof(head)) ||
        xx_rt_memcmp(head, "RIFF", 4U) != 0 ||
        xx_rt_memcmp(head + 8, "WAVE", 4U) != 0)
        return false;
    for (count = 0U; count < CUE_WAVE_MAX_CHUNKS && position <= total - 8;
         ++count) {
        int64_t body = position + 8, length;
        if (!cue_read_at(device, position, chunk, sizeof(chunk))) return false;
        length = (int64_t)cue_le32(chunk + 4);
        if (xx_rt_memcmp(chunk, "fmt ", 4U) == 0) {
            uint32_t tag;
            size_t want = length >= 40 ? 40U : 16U;
            if (length < 16 || length > total - body ||
                !cue_read_at(device, body, format_chunk, want))
                return false;
            tag = cue_le16(format_chunk);
            if (tag == 0xFFFEU &&
                (want < 40U || cue_le16(format_chunk + 24) != 1U))
                return false;
            if ((tag != 1U && tag != 0xFFFEU) ||
                cue_le16(format_chunk + 2) != 2U ||
                cue_le32(format_chunk + 4) != 44100U ||
                cue_le16(format_chunk + 12) != 4U ||
                cue_le16(format_chunk + 14) != 16U)
                return false;
            have_format = true;
        } else if (xx_rt_memcmp(chunk, "data", 4U) == 0) {
            if (!have_format) return false;
            *base = body;
            *size = length < total - body ? length : total - body;
            return true;
        }
        if (length > total - body) return false;
        position = body + length + (length & 1);
    }
    return false;
}

/* Bytes a segment covers once its file is measured: the stated length, or
 * for the last segment of a file every whole frame up to the file's end (a
 * trailing partial sector is not part of any track). -1 if it does not fit. */
static int64_t cue_segment_length(const cue_segment *segment,
                                  const cue_file *entry) {
    int64_t room;
    if (!entry->usable || segment->offset > entry->size) return -1;
    room = entry->size - segment->offset;
    if (segment->length < 0)
        return room - room % (int64_t)segment->frame_size;
    return segment->length <= room ? segment->length : -1;
}

/* Measure what each attached data device holds and settle every member's
 * size: -1 when a data file is missing, unusable, or shorter than the sheet
 * says. */
static void cue_resolve(cue_sheet *sheet) {
    const xx_cue *owner = sheet->owner;
    uint32_t file, member;
    for (file = 0U; file < sheet->file_count; ++file) {
        cue_file *entry = &sheet->files[file];
        xx_io_device *device = owner ? owner->data[file] : NULL;
        int64_t total;
        entry->usable = false;
        entry->base = 0;
        entry->size = 0;
        if (!device || (total = xx_io_total_size(device)) < 0) continue;
        if (entry->type == CUE_FT_BINARY || entry->type == CUE_FT_MOTOROLA) {
            entry->size = total;
            entry->usable = true;
        } else if (entry->type == CUE_FT_WAVE) {
            entry->usable = cue_wave_locate(device, total, &entry->base,
                                            &entry->size);
        }
    }
    for (member = 0U; member < sheet->member_count; ++member) {
        cue_member *item = &sheet->members[member];
        int64_t total = 0;
        uint32_t index;
        bool fits = true;
        for (index = 0U; index < item->segment_count && fits; ++index) {
            const cue_segment *segment =
                &sheet->segments[item->first_segment + index];
            int64_t length =
                cue_segment_length(segment, &sheet->files[segment->file]);
            if (length < 0 || length > INT64_MAX - total) {
                fits = false;
                break;
            }
            total += length;
        }
        item->size = fits ? total : -1;
    }
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static bool cue_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *cue_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool cue_set_record(xx_archive_record *record, const cue_sheet *sheet,
                           const cue_member *member) {
    const cue_track *track = &sheet->tracks[member->track];
    bool ok;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = track->line_offset;
    record->header_size = (int64_t)track->line_size;
    /* The payload lives in a data file, not in the sheet's device. */
    record->data_offset = -1;
    record->compressed_size = member->size >= 0 ? member->size : 0;
    ok = xx_archive_record_set_original_name(record, member->name) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        0U) &&
         xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                        cue_modes[track->mode].name) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
    if (ok && member->size >= 0)
        ok = xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                            (uint64_t)member->size) &&
             xx_archive_record_set_meta_u64(record,
                                            XX_META_ID_UNCOMPRESSED_SIZE,
                                            (uint64_t)member->size);
    return ok;
}

/* Copy one member's segments to @p destination (NULL only reads them). */
static bool cue_copy_member(const cue_sheet *sheet, const cue_member *member,
                            xx_io_device *destination, xx_pd_struct *pd) {
    const xx_cue *owner = sheet->owner;
    uint8_t *buffer;
    uint32_t index;
    bool result = true;
    if (!owner || member->size < 0) return false;
    buffer = (uint8_t *)xx_mem_alloc(CUE_COPY_CHUNK);
    if (!buffer) return false;
    for (index = 0U; index < member->segment_count && result; ++index) {
        const cue_segment *segment =
            &sheet->segments[member->first_segment + index];
        const cue_file *entry = &sheet->files[segment->file];
        xx_io_device *source = owner->data[segment->file];
        int64_t length = cue_segment_length(segment, entry), done = 0;
        if (!source || length < 0) {
            result = false;
            break;
        }
        while (done < length) {
            size_t amount = (length - done) > (int64_t)CUE_COPY_CHUNK
                                ? CUE_COPY_CHUNK
                                : (size_t)(length - done);
            size_t written = 0U;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !cue_read_at(source, entry->base + segment->offset + done,
                             buffer, amount)) {
                result = false;
                break;
            }
            while (destination && written < amount) {
                ssize_t step = xx_io_write(destination, buffer + written,
                                           amount - written);
                if (step <= 0 || (size_t)step > amount - written) {
                    result = false;
                    break;
                }
                written += (size_t)step;
            }
            if (!result) break;
            done += (int64_t)amount;
        }
    }
    xx_mem_free(buffer);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Data files                                                              */

static bool cue_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || cue_upper((uint8_t)name[index]) != (uint8_t)word[index])
            return false;
    return word[stem] == 0;
}

/* The last path component of a FILE name, or NULL when it is not a name the
 * reader will open: empty, "."/"..", only dots and spaces, control or
 * reserved characters, a drive prefix alone, or a Windows device name (CON,
 * NUL, COM1, LPT1.TXT, CONIN$, COM superscript digits ...). */
static const char *cue_safe_basename(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    const char *base = name;
    size_t length, stem = 0U, index;
    bool meaningful = false;
    if (!name) return NULL;
    for (index = 0U; name[index]; ++index)
        if (name[index] == '/' || name[index] == '\\') base = name + index + 1U;
    if (((base[0] >= 'A' && base[0] <= 'Z') || (base[0] >= 'a' && base[0] <= 'z')) &&
        base[1] == ':')
        base += 2;
    length = xx_str_len(base);
    if (length == 0U || length > CUE_MAX_BASENAME) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)base[index];
        if (c < 0x20U || c == 0x7FU || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*')
            return NULL;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return NULL;
    while (stem < length && base[stem] != '.') ++stem;
    while (stem > 0U && base[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (cue_stem_is(base, stem, devices[index])) return NULL;
    if ((cue_stem_is(base, 3U, "COM") || cue_stem_is(base, 3U, "LPT")) &&
        stem >= 4U) {
        const uint8_t *tail = (const uint8_t *)base + 3;
        if (stem == 4U && cue_digit(tail[0])) return NULL;
        /* U+00B9, U+00B2, U+00B3 in UTF-8 */
        if (stem == 5U && tail[0] == 0xC2U &&
            (tail[1] == 0xB9U || tail[1] == 0xB2U || tail[1] == 0xB3U))
            return NULL;
    }
    return base;
}

static void cue_release_data(xx_cue *archive, uint32_t index) {
    if (archive->data_owned[index] && archive->data[index])
        xx_io_close(archive->data[index]);
    archive->data[index] = NULL;
    archive->data_owned[index] = false;
}

bool xx_cue_set_data_device(xx_cue *archive, uint32_t file_index,
                            xx_io_device *device) {
    if (!archive || file_index >= XX_CUE_MAX_FILES) return false;
    cue_release_data(archive, file_index);
    archive->data[file_index] = device;
    return true;
}

uint32_t xx_cue_open_data_files(xx_cue *archive, const char *cue_path) {
    cue_sheet *sheet;
    size_t directory = 0U, index;
    uint32_t file, opened = 0U;
    if (!archive || !cue_path) return 0U;
    sheet = cue_load(&archive->format);
    if (!sheet) return 0U;
    for (index = 0U; cue_path[index]; ++index)
        if (cue_path[index] == '/' || cue_path[index] == '\\')
            directory = index + 1U;
    for (file = 0U; file < sheet->file_count; ++file) {
        const char *base;
        char *path;
        size_t base_length;
        xx_io_device *device;
        if (archive->data[file]) {
            ++opened;
            continue;
        }
        base = cue_safe_basename(sheet->files[file].name);
        if (!base) continue;
        base_length = xx_str_len(base);
        path = (char *)xx_mem_alloc(directory + base_length + 1U);
        if (!path) continue;
        xx_mem_copy(path, cue_path, directory);
        xx_mem_copy(path + directory, base, base_length + 1U);
        device = xx_io_file_open(path, "rb");
        xx_mem_free(path);
        if (!device) continue;
        archive->data[file] = device;
        archive->data_owned[file] = true;
        ++opened;
    }
    cue_sheet_free(sheet);
    return opened;
}

uint32_t xx_cue_get_number_of_files(xx_cue *archive) {
    return archive && (archive->format.base_info_handled ||
                       xx_cue_handle_base_info(&archive->format, NULL))
               ? archive->number_of_files : 0U;
}

uint32_t xx_cue_get_number_of_tracks(xx_cue *archive) {
    return archive && (archive->format.base_info_handled ||
                       xx_cue_handle_base_info(&archive->format, NULL))
               ? archive->number_of_tracks : 0U;
}

char *xx_cue_get_file_name(xx_cue *archive, uint32_t file_index) {
    cue_sheet *sheet;
    char *result = NULL;
    if (!archive || !(sheet = cue_load(&archive->format))) return NULL;
    if (file_index < sheet->file_count)
        result = xx_str_dup(sheet->files[file_index].name);
    cue_sheet_free(sheet);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Format interface                                                        */

void xx_cue_init(xx_cue *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CUE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-cue");
    xx_format_set_extension(&archive->format, "cue");
    archive->format.check_is_valid = xx_cue_check_is_valid;
    archive->format.handle_base_info = xx_cue_handle_base_info;
    archive->format.get_format_size = xx_cue_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_cue_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_cue_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_cue_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_cue_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_cue_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_cue_free_archive_records_reading;
    archive->sheet_size = -1;
}

xx_cue *xx_cue_create(xx_io_device *device, int64_t base_address) {
    xx_cue *archive = (xx_cue *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_cue_init(archive, device, base_address);
    return archive;
}

void xx_cue_destroy(xx_cue *archive) {
    uint32_t index;
    if (!archive) return;
    for (index = 0U; index < XX_CUE_MAX_FILES; ++index)
        cue_release_data(archive, index);
    xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_cue_free(xx_cue *archive) {
    if (!archive) return;
    xx_cue_destroy(archive);
    xx_mem_free(archive);
}

bool xx_cue_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    cue_sheet *sheet;
    (void)pd;
    sheet = cue_load(format);
    if (!sheet) return false;
    cue_sheet_free(sheet);
    return true;
}

bool xx_cue_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    cue_sheet *sheet;
    xx_cue *archive;
    (void)pd;
    if (!format || !(sheet = cue_load(format))) return false;
    archive = (xx_cue *)format;
    archive->number_of_records = sheet->member_count;
    archive->number_of_tracks = sheet->track_count;
    archive->number_of_files = sheet->file_count;
    archive->sheet_size = sheet->sheet_size;
    format->number_of_archive_records = sheet->member_count;
    format->format_size = sheet->sheet_size;
    format->is_valid = true;
    format->base_info_handled = true;
    cue_sheet_free(sheet);
    return true;
}

int64_t xx_cue_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_cue_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_cue_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_cue_handle_base_info(format, pd))
               ? ((xx_cue *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_cue_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    cue_sheet *sheet;
    xx_archive_record_state *state;
    (void)pd;
    if (!(sheet = cue_load(format))) return NULL;
    cue_resolve(sheet);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        cue_sheet_free(sheet);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = sheet;
    state->free_internal = cue_sheet_free;
    state->total_records = (int64_t)sheet->member_count;
    if (!cue_copy_options(&state->options, options) ||
        !cue_set_record(&state->current_record, sheet, &sheet->members[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_cue_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_cue_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    cue_sheet *sheet;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(sheet = (cue_sheet *)state->internal_state) ||
        ++sheet->index >= sheet->member_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = cue_set_record(&state->current_record, sheet,
                                       &sheet->members[sheet->index]);
    return state->has_record;
}

bool xx_cue_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    cue_sheet *sheet;
    const cue_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(sheet = (cue_sheet *)state->internal_state) ||
        sheet->index >= sheet->member_count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &sheet->members[sheet->index];
    if (member->size < 0) return false;
    path_option = cue_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return cue_copy_member(sheet, member, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* Member names are built by the reader ("trackNN..."), never taken
     * from the sheet, so they are safe by construction. */
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        /* Only a file this call created may be removed on failure. */
        created = true;
        result = cue_copy_member(sheet, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_cue_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
