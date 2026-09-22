/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * POSIX shell archives (shar).
 *
 * A shar is a /bin/sh script that writes its members out of here-documents.
 * THIS READER NEVER EXECUTES ANYTHING: the file is parsed as text and only
 * the here-document bodies are extracted.  That is the whole point of
 * treating a shar as an archive format - the alternative, and what the
 * banner in every one of these files invites the user to do, is to run an
 * untrusted script.  No process is spawned, no command is interpreted, and
 * anything in the script other than the here-document framing is ignored.
 *
 * A member starts at a line of the shape
 *
 *     sed 's/^X//' >'name' <<'DELIM'
 *     sed "s/^X//" >'name' <<'DELIM'
 *     sed 's/^X//' << \DELIM > 'name'
 *     sed 's/^\t//' << \DELIM > 'name' &&
 *     cat > "name" << 'DELIM'
 *     cat >name <<-'DELIM'
 *
 * so the redirection may sit on either side of the here-document operator,
 * the delimiter may be quoted with ', " or a leading backslash, and the
 * `<<-` form strips leading tabs from the terminator line.  The body runs to
 * the first line that equals the delimiter exactly (after the `<<-` tab
 * strip and after an optional CR, so CRLF copies still parse).
 *
 * `sed 's/^X//'` exists because a naked here-document cannot carry a line
 * that equals the delimiter, nor keep leading whitespace through mailers:
 * the packer prefixes every body line with one character and the sed
 * expression removes it again.  The prefix is read out of the expression and
 * applied on extraction; a body behind a `cat` carries no prefix.
 *
 * A second shape has no redirection at all - the body is piped into an
 * unpacker instead of being written:
 *
 *     $unpacker <<'@eof'
 *     uudecode <<'EOF'
 *
 * The member's name is then not on that line; it is in the uuencode "begin
 * <mode> <name>" header at the top of the body, which is also the only thing
 * that makes such a line a member at all.  These packers pipe the file
 * through compress(1) before uuencoding it, so a decoded body that carries
 * the .Z header is uncompressed as well - that is the whole of the script's
 * pipeline, reproduced without running any of it.
 *
 * Strictness: a plain shell script is full of `cat <<EOF` blocks, so a
 * here-document alone must not make a file an archive.  A file is accepted
 * only when it starts with '#', carries a shar banner ("shell archive",
 * "shar archive" or "SHAR_EOF") inside its first 4 KiB, and yields at least
 * one member.
 *
 * Truncation is normal in this corpus - multi-part archives are routinely
 * stored with the tail cut off - so a final here-document with no terminator
 * is published as a member running to end of file rather than dropped, and
 * scanning stops there.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/shar/xx_shar.h"

#include "xxfclib/algo/compress/xx_compress.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* The enum arrives with the registration; the shim keeps this file building
 * until then and never invents a value. */
#ifdef SHAR
#define XX_SHAR_FILE_TYPE XX_FILE_TYPE_SHAR
#else
#define XX_SHAR_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_SHAR_MAX_INPUT ((int64_t)64 * 1024 * 1024)
#define XX_SHAR_MIN_INPUT 32
#define XX_SHAR_BANNER_WINDOW 4096
#define XX_SHAR_MAX_MEMBERS 4096
#define XX_SHAR_MAX_NAME 255
#define XX_SHAR_MAX_DELIM 128
/* Storage methods, as published through XX_META_ID_COMPRESSION_METHOD.
 * 0 stored, 1 stored behind a sed prefix, 2 uuencoded (and, when the decoded
 * bytes turn out to be a Unix compress stream, uncompressed as well). */
#define XX_SHAR_METHOD_UU 2U
#define XX_SHAR_MAX_PLAIN ((int64_t)256 * 1024 * 1024)

typedef struct xx_shar_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;   /* raw body bytes, prefix included */
    int64_t uncompressed_size; /* body bytes after the prefix strip */
    uint32_t method;           /* 0: stored.  1: stored behind a sed prefix. */
    uint8_t prefix;            /* the character sed removes, 0 when none */
    bool truncated;
    bool is_folder;
} xx_shar_member;

typedef struct xx_shar_stream_s {
    xx_shar_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_shar_stream;

static void xx_shar_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_shar_read_at(Abstractformat *self, int64_t offset,
                            uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

/* Refuse anything that would escape the extraction directory.  Shar member
 * names legitimately carry '/', so the separator itself stays legal and only
 * the traversal is rejected. */
static bool xx_shar_path_safe(const char *name) {
    const char *cursor = name;
    bool has_component = false;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        /* A "." component is redundant, not dangerous - shar writes "./name"
         * constantly - so only the traversal is refused. */
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        /* "C:..." is drive-relative on Windows and would leave the
         * extraction directory even though the name looks relative. */
        if (length >= 2U && cursor[1] == ':') return false;
        if (!(length == 1U && cursor[0] == '.')) has_component = true;
        cursor = *end ? end + 1 : end;
    }
    /* "." or "./." names nothing to write. */
    return has_component;
}

/* Build the relative path a member is written to.
 *
 * The stored name stays verbatim in the record meta, because it is what the
 * archive says.  The path on disk cannot always be that name: these are Unix
 * names and ":mh-e.ml" is a real one, but a ':' makes a filename unopenable
 * on Windows, so the byte is folded for the write and only for the write.
 * Redundant "." components - shar emits "./name" constantly - are dropped,
 * and a component is trimmed of the trailing dots and spaces Windows strips
 * silently.  Returns NULL when nothing usable is left. */
static char *xx_shar_make_extract_name(const char *name) {
    char *result;
    size_t output = 0U;
    const char *component;
    const char *cursor;

    if (!xx_shar_path_safe(name)) return NULL;
    result = xx_str_create_len(xx_str_len(name) + 1U);
    if (!result) return NULL;
    component = name;
    for (cursor = name;; ++cursor) {
        char character = *cursor;
        if (character == '/' || character == '\0') {
            size_t length = (size_t)(cursor - component);
            /* xx_shar_path_safe has already refused these, but the check is
             * repeated where the path is actually built so the two can never
             * drift apart. */
            if (length == 2U && component[0] == '.' && component[1] == '.') {
                xx_str_free(result);
                return NULL;
            }
            if (!(length == 1U && component[0] == '.') && length != 0U) {
                size_t start = output;
                size_t index;
                if (output != 0U) result[output++] = '/';
                start = output;
                for (index = 0U; index < length; ++index) {
                    char byte = component[index];
                    result[output++] = (byte == ':') ? '_' : byte;
                }
                while (output > start && (result[output - 1U] == ' ' ||
                                          result[output - 1U] == '.'))
                    --output;
                if (output == start) result[output++] = '_';
            }
            if (character == '\0') break;
            component = cursor + 1;
        }
    }
    result[output] = '\0';
    if (output == 0U) {
        xx_str_free(result);
        return NULL;
    }
    return result;
}

static bool xx_shar_name_ok(const uint8_t *bytes, size_t length) {
    size_t index;

    if (length < 1U || length > (size_t)XX_SHAR_MAX_NAME) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t character = bytes[index];
        if (character < 0x20U || character >= 0x7FU) return false;
        /* A name that still carries shell syntax was mis-parsed, not
         * mis-typed: refuse it rather than publish it. */
        /* ':' stays legal: these are Unix names and ":patch.c" is a real one.
         * The drive-letter shape it could otherwise smuggle in is refused by
         * xx_shar_path_safe instead. */
        if (character == '\\' || character == '*' ||
            character == '?' || character == '"' || character == '\'' ||
            character == '<' || character == '>' || character == '|' ||
            character == '$' || character == '`' || character == ';' ||
            character == '&') {
            return false;
        }
    }
    return true;
}

static void xx_shar_stream_free(void *pointer) {
    xx_shar_stream *stream = (xx_shar_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_shar_add(xx_shar_stream *stream, const xx_shar_member *member) {
    xx_shar_member *grown = (xx_shar_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool xx_shar_blank(uint8_t character) {
    return character == ' ' || character == '\t';
}

/* Case-insensitive substring search over a bounded window; the banner test
 * is the only place that needs it. */
static bool xx_shar_contains_ci(const uint8_t *data, size_t size,
                                const char *needle, size_t needle_size) {
    size_t start;

    if (needle_size == 0U || size < needle_size) return false;
    for (start = 0U; start + needle_size <= size; ++start) {
        size_t index;
        for (index = 0U; index < needle_size; ++index) {
            uint8_t left = data[start + index];
            uint8_t right = (uint8_t)needle[index];
            if (left >= 'A' && left <= 'Z') left = (uint8_t)(left + 32);
            if (right >= 'A' && right <= 'Z') right = (uint8_t)(right + 32);
            if (left != right) break;
        }
        if (index == needle_size) return true;
    }
    return false;
}

/* ---------------------------------------------------------- line parse -- */

/* One shell word starting at @p position inside [@p line, @p line + size):
 * a single- or double-quoted string, or a run of non-separator bytes.  A
 * leading backslash (the `<< \DELIM` form) is skipped.  Returns false when
 * there is no word. */
static bool xx_shar_word(const uint8_t *line, size_t size, size_t position,
                         size_t *word_offset, size_t *word_size,
                         size_t *next_position) {
    size_t start;
    size_t end;

    while (position < size && xx_shar_blank(line[position])) ++position;
    if (position >= size) return false;
    if (line[position] == '\\') {
        ++position;
        if (position >= size) return false;
    }
    if (line[position] == '\'' || line[position] == '"') {
        uint8_t quote = line[position];
        start = position + 1U;
        end = start;
        while (end < size && line[end] != quote) ++end;
        if (end >= size) return false; /* unterminated quote */
        *word_offset = start;
        *word_size = end - start;
        *next_position = end + 1U;
        return *word_size != 0U;
    }
    start = position;
    end = position;
    while (end < size) {
        uint8_t character = line[end];
        if (xx_shar_blank(character) || character == ';' || character == '&' ||
            character == '|' || character == '<' || character == '>' ||
            character == '(' || character == ')') {
            break;
        }
        ++end;
    }
    if (end == start) return false;
    *word_offset = start;
    *word_size = end - start;
    *next_position = end;
    return true;
}

/* A candidate member header.  All offsets are relative to the line start. */
typedef struct xx_shar_head_s {
    size_t name_offset;
    size_t name_size;
    size_t delim_offset;
    size_t delim_size;
    bool strip_tabs; /* the `<<-` form */
    bool uu;         /* body is uuencoded and carries its own name */
    uint8_t prefix;  /* `sed 's/^X//'` - 0 when there is none */
} xx_shar_head;

/* Reads the one-character quoting prefix out of an `s/^X//` expression that
 * precedes the here-document operator.  `s/^\t//` is the other spelling the
 * packers use.  Anything else leaves the body untouched, which is always
 * safe: a wrong strip would corrupt every line. */
static uint8_t xx_shar_prefix(const uint8_t *line, size_t limit) {
    size_t index;

    if (limit < 5U) return 0U;
    for (index = 0U; index + 5U <= limit; ++index) {
        if (line[index] != 's' || line[index + 1U] != '/' ||
            line[index + 2U] != '^') {
            continue;
        }
        if (line[index + 4U] == '/' && index + 5U < limit &&
            line[index + 5U] == '/') {
            return line[index + 3U];
        }
        if (line[index + 3U] == '\\' && line[index + 4U] == 't' &&
            index + 6U < limit && line[index + 5U] == '/' &&
            line[index + 6U] == '/') {
            return (uint8_t)'\t';
        }
    }
    return 0U;
}

/* Matches a `cat`/`sed` here-document line and fills @p head.  Everything
 * else on the line - test guards, trailing `&&`, echo noise - is ignored. */
static bool xx_shar_match_head(const uint8_t *line, size_t size,
                               xx_shar_head *head) {
    size_t start = 0U;
    size_t here;
    size_t position;
    size_t delim_end;
    size_t redirect;
    size_t name_next;

    while (start < size && xx_shar_blank(line[start])) ++start;
    if (size - start < 8U) return false;
    /* Only these two commands write a here-document to a file in a shar.
     * Widening this is what turns an ordinary script into a false
     * positive. */
    if (!(xx_rt_memcmp(line + start, "cat ", 4U) == 0 ||
          xx_rt_memcmp(line + start, "sed ", 4U) == 0 ||
          xx_rt_memcmp(line + start, "cat\t", 4U) == 0 ||
          xx_rt_memcmp(line + start, "sed\t", 4U) == 0)) {
        return false;
    }

    here = start;
    while (here + 1U < size &&
           !(line[here] == '<' && line[here + 1U] == '<')) {
        ++here;
    }
    if (here + 1U >= size) return false;
    position = here + 2U;
    /* `<<<` is a here-string, not a here-document. */
    if (position < size && line[position] == '<') return false;
    head->strip_tabs = false;
    if (position < size && line[position] == '-') {
        head->strip_tabs = true;
        ++position;
    }
    if (!xx_shar_word(line, size, position, &head->delim_offset,
                      &head->delim_size, &delim_end)) {
        return false;
    }
    if (head->delim_size > (size_t)XX_SHAR_MAX_DELIM) return false;

    /* The redirection sits on either side of the operator, so the first '>'
     * outside the delimiter word is the one that names the member. */
    redirect = start;
    while (redirect < size) {
        if (line[redirect] == '>' && !(redirect >= here && redirect < delim_end)) {
            break;
        }
        ++redirect;
    }
    if (redirect >= size) return false;
    ++redirect;
    if (redirect < size && line[redirect] == '>') ++redirect; /* `>>` */
    if (!xx_shar_word(line, size, redirect, &head->name_offset,
                      &head->name_size, &name_next)) {
        return false;
    }
    if (!xx_shar_name_ok(line + head->name_offset, head->name_size)) {
        return false;
    }
    head->prefix = xx_shar_prefix(line + start, here - start);
    head->uu = false;
    return true;
}

/* Matches a here-document line that does NOT redirect to a file.
 *
 * The HP-UX era packers pipe the body into an unpacker instead of writing it
 * with sed: `$unpacker <<'@eof'`, `uudecode <<'EOF'`.  The member's name is
 * then not on this line at all - it is in the uuencode "begin" header at the
 * top of the body - so this matcher only recovers the delimiter, and the
 * caller decides whether the body really is uuencoded before publishing
 * anything.  A line carrying a '>' is left to xx_shar_match_head. */
static bool xx_shar_match_here(const uint8_t *line, size_t size,
                               xx_shar_head *head) {
    size_t start = 0U;
    size_t here;
    size_t position;
    size_t delim_end;
    size_t index;

    while (start < size && xx_shar_blank(line[start])) ++start;
    if (size <= start) return false;
    here = start;
    while (here + 1U < size &&
           !(line[here] == '<' && line[here + 1U] == '<')) {
        ++here;
    }
    if (here + 1U >= size) return false;
    position = here + 2U;
    if (position < size && line[position] == '<') return false; /* here-string */
    head->strip_tabs = false;
    if (position < size && line[position] == '-') {
        head->strip_tabs = true;
        ++position;
    }
    if (!xx_shar_word(line, size, position, &head->delim_offset,
                      &head->delim_size, &delim_end)) {
        return false;
    }
    if (head->delim_size > (size_t)XX_SHAR_MAX_DELIM) return false;
    for (index = start; index < size; ++index) {
        if (line[index] == '>' && !(index >= here && index < delim_end)) {
            return false;
        }
    }
    head->name_offset = 0U;
    head->name_size = 0U;
    head->prefix = 0U;
    head->uu = true;
    return true;
}

/* Parse a uuencode "begin <octal mode> <name>" line.  Returns the length of
 * the name, or 0 when the line is not a begin header. */
static size_t xx_shar_uu_begin(const uint8_t *line, size_t size,
                               size_t *name_offset) {
    size_t position = 0U;
    size_t digits = 0U;

    if (size != 0U && line[size - 1U] == '\r') --size;
    while (position < size && xx_shar_blank(line[position])) ++position;
    if (size - position < 6U ||
        xx_rt_memcmp(line + position, "begin ", 6U) != 0) {
        return 0U;
    }
    position += 6U;
    while (position < size && xx_shar_blank(line[position])) ++position;
    while (position < size && line[position] >= '0' && line[position] <= '7') {
        ++position;
        ++digits;
    }
    /* A mode is three or four octal digits and must be followed by the name. */
    if (digits < 3U || digits > 4U || position >= size ||
        !xx_shar_blank(line[position])) {
        return 0U;
    }
    while (position < size && xx_shar_blank(line[position])) ++position;
    if (position >= size) return 0U;
    *name_offset = position;
    return size - position;
}

/* One uuencoded line yields three plaintext bytes per four characters, but
 * the leading length byte is authoritative; this only measures. */
static bool xx_shar_uu_measure(const uint8_t *body, size_t size,
                               int64_t *plain_size) {
    size_t position = 0U;
    int64_t total = 0;

    while (position < size) {
        size_t end = position;
        size_t length;
        while (end < size && body[end] != '\n') ++end;
        length = end - position;
        if (length != 0U && body[position + length - 1U] == '\r') --length;
        if (length != 0U) {
            uint8_t count = (uint8_t)((body[position] - ' ') & 0x3FU);
            if (length >= 3U && xx_rt_memcmp(body + position, "end", 3U) == 0) {
                *plain_size = total;
                return true;
            }
            if (count != 0U) {
                /* Each group of four characters carries three bytes. */
                size_t need = 1U + (((size_t)count + 2U) / 3U) * 4U;
                if (length + 1U < need) return false;
                total += count;
            }
        }
        position = end + 1U;
    }
    /* A truncated member never reached its "end" line; what was measured is
     * still what can be produced. */
    *plain_size = total;
    return true;
}

/* Decode a uuencoded body into @p output, which the caller sized from
 * xx_shar_uu_measure.  Stops at the "end" line or at the end of the body. */
static bool xx_shar_uu_decode(const uint8_t *body, size_t size,
                              uint8_t *output, size_t capacity,
                              size_t *written) {
    size_t position = 0U;
    size_t produced = 0U;

    while (position < size) {
        size_t end = position;
        size_t length;
        size_t cursor;
        uint8_t count;
        while (end < size && body[end] != '\n') ++end;
        length = end - position;
        if (length != 0U && body[position + length - 1U] == '\r') --length;
        if (length == 0U) {
            position = end + 1U;
            continue;
        }
        if (length >= 3U && xx_rt_memcmp(body + position, "end", 3U) == 0) {
            break;
        }
        count = (uint8_t)((body[position] - ' ') & 0x3FU);
        cursor = position + 1U;
        while (count != 0U) {
            uint8_t group[4];
            size_t index;
            uint8_t take = count > 3U ? 3U : count;
            if (cursor + 4U > position + length) return false;
            for (index = 0U; index < 4U; ++index)
                group[index] = (uint8_t)((body[cursor + index] - ' ') & 0x3FU);
            cursor += 4U;
            if (produced + take > capacity) return false;
            if (take > 0U)
                output[produced++] =
                    (uint8_t)((group[0] << 2) | (group[1] >> 4));
            if (take > 1U)
                output[produced++] =
                    (uint8_t)((group[1] << 4) | (group[2] >> 2));
            if (take > 2U)
                output[produced++] =
                    (uint8_t)((group[2] << 6) | group[3]);
            count = (uint8_t)(count - take);
        }
        position = end + 1U;
    }
    *written = produced;
    return true;
}

/* ---------------------------------------------------------------- parse -- */

static bool xx_shar_line_is_delim(const uint8_t *line, size_t size,
                                  const uint8_t *delim, size_t delim_size,
                                  bool strip_tabs) {
    size_t start = 0U;

    if (size != 0U && line[size - 1U] == '\r') --size;
    if (strip_tabs) {
        while (start < size && line[start] == '\t') ++start;
    }
    return size - start == delim_size &&
           xx_rt_memcmp(line + start, delim, delim_size) == 0;
}

/* Size of the body once the quoting prefix is removed: one byte per line
 * that actually carries it. */
static int64_t xx_shar_plain_size(const uint8_t *body, size_t size,
                                  uint8_t prefix) {
    size_t position = 0U;
    int64_t total = 0;

    if (prefix == 0U) return (int64_t)size;
    while (position < size) {
        size_t end = position;
        while (end < size && body[end] != '\n') ++end;
        if (body[position] == prefix) {
            total += (int64_t)(end - position) - 1;
        } else {
            total += (int64_t)(end - position);
        }
        if (end < size) ++total; /* the newline itself */
        position = end + 1U;
    }
    return total;
}

static xx_shar_stream *xx_shar_scan(Abstractformat *self, const uint8_t *data,
                                    size_t size, xx_pd_struct *pd) {
    xx_shar_stream *stream;
    xx_shar_member member;
    size_t position = 0U;

    (void)self;
    stream = (xx_shar_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    while (position < size) {
        size_t line_end = position;
        xx_shar_head head;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        while (line_end < size && data[line_end] != '\n') ++line_end;
        if (!xx_shar_match_head(data + position, line_end - position, &head) &&
            !xx_shar_match_here(data + position, line_end - position, &head)) {
            position = line_end + 1U;
            continue;
        }
        if (line_end >= size) break; /* header line with no body behind it */

        {
            size_t body = line_end + 1U;
            size_t cursor = body;
            bool terminated = false;
            char name_buffer[XX_SHAR_MAX_NAME + 1];
            size_t index;

            while (cursor < size) {
                size_t end = cursor;
                while (end < size && data[end] != '\n') ++end;
                if (xx_shar_line_is_delim(data + cursor, end - cursor,
                                          data + position + head.delim_offset,
                                          head.delim_size, head.strip_tabs)) {
                    terminated = true;
                    break;
                }
                cursor = end + 1U;
            }
            if (!terminated) cursor = size;

            xx_mem_zero(&member, sizeof(member));
            if (head.uu) {
                /* A here-document with no redirection only becomes a member
                 * when its first line really is a uuencode header: that line
                 * is both the proof and the source of the name. */
                size_t first_end = body;
                size_t name_offset = 0U;
                size_t name_size;
                while (first_end < cursor && data[first_end] != '\n')
                    ++first_end;
                name_size = xx_shar_uu_begin(data + body, first_end - body,
                                             &name_offset);
                if (name_size == 0U ||
                    name_size > (size_t)XX_SHAR_MAX_NAME ||
                    !xx_shar_name_ok(data + body + name_offset, name_size)) {
                    position = terminated ? cursor : size;
                    while (position < size && data[position] != '\n')
                        ++position;
                    ++position;
                    continue;
                }
                head.name_offset = body - position + name_offset;
                head.name_size = name_size;
                /* The body proper starts after the begin line. */
                body = first_end < cursor ? first_end + 1U : cursor;
            }
            for (index = 0U; index < head.name_size; ++index) {
                name_buffer[index] = (char)data[position + head.name_offset + index];
            }
            name_buffer[head.name_size] = '\0';
            if (!xx_shar_path_safe(name_buffer)) {
                /* A name this reader would refuse to write is a parse that
                 * went wrong; skip the block rather than publish it. */
                position = terminated ? cursor : size;
                continue;
            }
            member.name = xx_str_dup(name_buffer);
            if (!member.name) goto fail;
            member.header_offset = self->base_address + (int64_t)position;
            /* For a uuencoded body the "begin" line is part of the framing,
             * not of the payload, so the header runs up to where body now
             * points rather than just to the end of the command line. */
            member.header_size = (int64_t)(body - position);
            member.data_offset = self->base_address + (int64_t)body;
            member.compressed_size = (int64_t)(cursor - body);
            member.prefix = head.prefix;
            if (head.uu) {
                member.method = XX_SHAR_METHOD_UU;
                if (!xx_shar_uu_measure(data + body, cursor - body,
                                        &member.uncompressed_size)) {
                    xx_str_free(member.name);
                    position = terminated ? cursor : size;
                    while (position < size && data[position] != '\n')
                        ++position;
                    ++position;
                    continue;
                }
            } else {
                member.method = head.prefix != 0U ? 1U : 0U;
                member.uncompressed_size = xx_shar_plain_size(
                    data + body, cursor - body, head.prefix);
            }
            member.truncated = !terminated;
            member.is_folder = false;
            if (stream->count >= (size_t)XX_SHAR_MAX_MEMBERS) {
                xx_str_free(member.name);
                goto fail;
            }
            if (!xx_shar_add(stream, &member)) {
                xx_str_free(member.name);
                goto fail;
            }
            if (!terminated) break; /* truncated archive: nothing follows */
            /* Resume after the terminator line. */
            while (cursor < size && data[cursor] != '\n') ++cursor;
            position = cursor + 1U;
        }
    }
    if (stream->count == 0U) goto fail;
    return stream;

fail:
    xx_shar_stream_free(stream);
    return NULL;
}

static xx_shar_stream *xx_shar_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_shar_stream *stream;
    uint8_t *data;
    int64_t total;
    int64_t span;
    size_t window;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_SHAR_MIN_INPUT || span > XX_SHAR_MAX_INPUT) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;

    /* The whole script is scanned, so it is read once; the cap above is what
     * keeps that allocation bounded by the real file size. */
    data = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!data) return NULL;
    if (!xx_shar_read_at(self, self->base_address, data, (size_t)span)) {
        xx_mem_free(data);
        return NULL;
    }
    if (data[0] != '#') {
        xx_mem_free(data);
        return NULL;
    }
    window = span < XX_SHAR_BANNER_WINDOW ? (size_t)span
                                          : (size_t)XX_SHAR_BANNER_WINDOW;
    /* A here-document on its own is ordinary shell.  The banner is what says
     * the script was produced as an archive. */
    if (!xx_shar_contains_ci(data, window, "shell archive", 13U) &&
        !xx_shar_contains_ci(data, window, "shar archive", 12U) &&
        !xx_shar_contains_ci(data, window, "SHAR_EOF", 8U)) {
        xx_mem_free(data);
        return NULL;
    }
    stream = xx_shar_scan(self, data, (size_t)span, pd);
    xx_mem_free(data);
    if (!stream) return NULL;
    stream->archive_size = span;
    return stream;
}

/* --------------------------------------------------------------- unpack -- */

/* Copies the body out, removing one leading @p prefix byte per line.  The
 * member is stored, so this is the whole "decoder". */
/* A write-only device that appends into a heap buffer.  The Unix compress
 * decoder writes through an xx_io_device and the plaintext length is not
 * known in advance, so the sink grows instead of being pre-sized. */
typedef struct xx_shar_sink_s {
    uint8_t *data;
    size_t size;
    size_t capacity;
    bool failed;
} xx_shar_sink;

static ssize_t xx_shar_sink_write(xx_io_device *device, const void *buffer,
                                  size_t size) {
    xx_shar_sink *sink = device ? (xx_shar_sink *)device->priv : NULL;
    if (!sink || (!buffer && size != 0U)) return -1;
    if (size > (size_t)XX_SHAR_MAX_PLAIN - sink->size) {
        sink->failed = true;
        return -1;
    }
    if (sink->size + size > sink->capacity) {
        size_t wanted = sink->capacity != 0U ? sink->capacity : 65536U;
        uint8_t *grown;
        while (wanted < sink->size + size) {
            if (wanted > (size_t)XX_SHAR_MAX_PLAIN / 2U) {
                wanted = sink->size + size;
                break;
            }
            wanted *= 2U;
        }
        grown = (uint8_t *)xx_mem_realloc(sink->data, wanted);
        if (!grown) {
            sink->failed = true;
            return -1;
        }
        sink->data = grown;
        sink->capacity = wanted;
    }
    if (size != 0U) xx_rt_memcpy(sink->data + sink->size, buffer, size);
    sink->size += size;
    return (ssize_t)size;
}

/* Decode a uuencoded member body, then - because these packers always pipe
 * the file through compress(1) before uuencoding it - run the Unix compress
 * decoder when the decoded bytes carry its header.  Anything else is handed
 * back as it decoded. */
static bool xx_shar_expand_uu(const uint8_t *raw, size_t size,
                              const xx_shar_member *member, uint8_t **out,
                              size_t *out_size, xx_pd_struct *pd) {
    uint8_t *decoded;
    size_t capacity;
    size_t written = 0U;

    if (member->uncompressed_size < 0 ||
        member->uncompressed_size > XX_SHAR_MAX_PLAIN) {
        return false;
    }
    capacity = (size_t)member->uncompressed_size;
    decoded = (uint8_t *)xx_mem_alloc(capacity != 0U ? capacity : 1U);
    if (!decoded) return false;
    if (!xx_shar_uu_decode(raw, size, decoded, capacity, &written)) {
        xx_mem_free(decoded);
        return false;
    }
    if (!xx_compress_has_header(decoded, written)) {
        *out = decoded;
        *out_size = written;
        return true;
    }
    {
        xx_io_device *source = xx_io_mem_open_ro(decoded, written);
        xx_io_device sink_device;
        xx_shar_sink sink;
        bool ok;
        xx_mem_zero(&sink, sizeof(sink));
        xx_mem_zero(&sink_device, sizeof(sink_device));
        sink_device.write = xx_shar_sink_write;
        sink_device.priv = &sink;
        ok = source != NULL &&
             xx_compress_decode_device(source, 0, (int64_t)written,
                                       &sink_device, NULL, pd) &&
             !sink.failed;
        if (source) xx_io_close(source);
        xx_mem_free(decoded);
        if (!ok) {
            xx_mem_free(sink.data);
            return false;
        }
        if (!sink.data) {
            sink.data = (uint8_t *)xx_mem_alloc(1U);
            if (!sink.data) return false;
        }
        *out = sink.data;
        *out_size = sink.size;
        return true;
    }
}

static bool xx_shar_extract(Abstractformat *self, const xx_shar_member *member,
                            uint8_t **out, size_t *out_size,
                            xx_pd_struct *pd) {
    uint8_t *raw;
    uint8_t *plain;
    size_t size;
    size_t position = 0U;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 ||
        member->compressed_size > XX_SHAR_MAX_INPUT) {
        return false;
    }
    size = (size_t)member->compressed_size;
    if (size == 0U) {
        plain = (uint8_t *)xx_mem_alloc(1U);
        if (!plain) return false;
        *out = plain;
        *out_size = 0U;
        return true;
    }
    raw = (uint8_t *)xx_mem_alloc(size);
    if (!raw) return false;
    if (!xx_shar_read_at(self, member->data_offset, raw, size)) {
        xx_mem_free(raw);
        return false;
    }
    if (member->method == XX_SHAR_METHOD_UU) {
        bool ok = xx_shar_expand_uu(raw, size, member, out, out_size, pd);
        xx_mem_free(raw);
        return ok;
    }
    if (member->prefix == 0U) {
        *out = raw;
        *out_size = size;
        return true;
    }
    plain = (uint8_t *)xx_mem_alloc(size);
    if (!plain) {
        xx_mem_free(raw);
        return false;
    }
    while (position < size) {
        size_t end = position;
        size_t start = position;
        while (end < size && raw[end] != '\n') ++end;
        if (raw[start] == member->prefix) ++start;
        while (start < end) plain[written++] = raw[start++];
        if (end < size) plain[written++] = (uint8_t)'\n';
        position = end + 1U;
    }
    xx_mem_free(raw);
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_shar_init(xx_shar *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SHAR_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-shar");
    xx_format_set_extension(&archive->format, "shar");
    archive->format.check_is_valid = xx_shar_check_is_valid;
    archive->format.handle_base_info = xx_shar_handle_base_info;
    archive->format.get_format_size = xx_shar_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_shar_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_shar_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_shar_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_shar_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_shar_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_shar_free_archive_records_reading;
    archive->format.destroy = xx_shar_vtable_destroy;
}

xx_shar *xx_shar_create(xx_io_device *device, int64_t base_address) {
    xx_shar *archive = (xx_shar *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_shar_init(archive, device, base_address);
    return archive;
}

void xx_shar_destroy(xx_shar *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_shar_free(xx_shar *archive) {
    if (!archive) return;
    xx_shar_destroy(archive);
    xx_mem_free(archive);
}

static void xx_shar_vtable_destroy(Abstractformat *self) {
    xx_shar_destroy((xx_shar *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_shar_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_shar_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_shar_parse(self, pd);
    if (!stream) return false;
    xx_shar_stream_free(stream);
    return true;
}

bool xx_shar_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_shar *archive = (xx_shar *)self;
    xx_shar_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_shar_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_shar_stream_free(stream);
    return true;
}

int64_t xx_shar_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_shar_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_shar *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_shar_set_record(xx_archive_record *record,
                               const xx_shar_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_shar_copy_options(xx_list_s *target, const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_shar_get_option(const xx_list_s *options,
                                        uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_shar_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_shar_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_shar_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_shar_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_shar_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_shar_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_shar_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_shar_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_shar_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_shar_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_shar_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_shar_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_shar_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_shar_stream *stream;
    const xx_shar_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_shar_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_shar_path_safe(member->name)) return false;

    path_option = xx_shar_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: read the member back and discard it, which
         * verifies it without writing anything. */
        result = xx_shar_extract(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    {
        char *relative = xx_shar_make_extract_name(member->name);
        if (!relative) {
            xx_str_free(converted_path);
            return false;
        }
        if (base_path[0] != '\0' &&
            base_path[xx_str_len(base_path) - 1U] != '/' &&
            base_path[xx_str_len(base_path) - 1U] != '\\') {
            target_path = xx_str_concat3(base_path, "/", relative);
        } else {
            target_path = xx_str_concat(base_path, relative);
        }
        xx_str_free(relative);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_shar_extract(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_shar_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
