/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ar/xx_ar.h"
#include "xx_ar_defs.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef enum xx_ar_kind_e {
    XX_AR_KIND_PLAIN = 0,
    XX_AR_KIND_DEB,     /* first member "debian-binary" */
    XX_AR_KIND_MSLIB    /* two leading "/" linker members (Microsoft .lib) */
} xx_ar_kind;

typedef struct xx_ar_member_s {
    int64_t header_offset;
    int64_t stored_data_offset;
    int64_t data_offset;
    int64_t stored_size;
    int64_t data_size;
    uint64_t name_prefix_size;
    uint64_t timestamp;
    uint64_t owner_id;
    uint64_t group_id;
    uint64_t mode;
    char *name;          /* resolved member name (renamed when duplicated) */
    char *extract_name;  /* relative output path, NULL when refused */
    bool special;
} xx_ar_member;

typedef struct xx_ar_members_s {
    xx_ar_member *items;
    size_t count;
    uint64_t visible_count;
    int64_t archive_end;
    xx_ar_kind kind;
} xx_ar_members;

/* What xx_ar_parse_archive builds beyond the member walk. */
#define XX_AR_PARSE_MEMBERS 1U       /* member table with resolved names */
#define XX_AR_PARSE_EXTRACT_NAMES 2U /* plus unique extraction paths */

typedef struct xx_ar_archive_stream_s {
    xx_ar_members members;
    size_t member_index;
} xx_ar_archive_stream;

typedef struct xx_ar_ds_stream_s {
    xx_data_struct *items;
    size_t count;
} xx_ar_ds_stream;

typedef struct xx_ar_record_stream_s {
    const xx_data_struct_field_desc *fields;
    size_t count;
} xx_ar_record_stream;

/* How the 16-byte name field of one member header is to be read. */
typedef enum xx_ar_name_kind_e {
    XX_AR_NAME_SHORT = 0, /* the name itself (trailing '/' removed) */
    XX_AR_NAME_BSD,       /* "#1/<n>": n name bytes open the member data */
    XX_AR_NAME_GNU        /* "/<n>": offset n in the "//" name table */
} xx_ar_name_kind;

typedef struct xx_ar_raw_name_s {
    xx_ar_name_kind kind;
    char text[17];       /* SHORT: the name; BSD/GNU: the raw field */
    uint64_t value;      /* BSD: name length; GNU: table offset */
    bool is_name_table;  /* raw field "//" */
} xx_ar_raw_name;

static void xx_ar_vtable_destroy(Abstractformat *self);

static bool xx_ar_read_exact_at(xx_io_device *device, int64_t offset,
                                void *buffer, size_t size) {
    size_t done = 0;
    uint8_t *bytes = (uint8_t *)buffer;
    if (!device || (!buffer && size != 0U) || offset < 0) {
        return false;
    }
    if (xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, bytes + done, size - done);
        if (got <= 0) {
            return false;
        }
        done += (size_t)got;
    }
    return true;
}

static bool xx_ar_parse_uint(const char *field, size_t size, unsigned base,
                             bool allow_blank, uint64_t *value) {
    size_t begin = 0;
    size_t end = size;
    uint64_t result = 0;
    bool have_digit = false;

    if (!field || !value || (base != 8U && base != 10U)) {
        return false;
    }
    while (begin < end && field[begin] == ' ') {
        ++begin;
    }
    while (end > begin && (field[end - 1U] == ' ' || field[end - 1U] == '\0')) {
        --end;
    }
    if (begin == end) {
        *value = 0;
        return allow_blank;
    }
    for (size_t i = begin; i < end; ++i) {
        unsigned digit;
        if (field[i] < '0' || field[i] > '9') {
            return false;
        }
        digit = (unsigned)(field[i] - '0');
        if (digit >= base || result > (UINT64_MAX - digit) / base) {
            return false;
        }
        result = result * base + digit;
        have_digit = true;
    }
    if (!have_digit) {
        return false;
    }
    *value = result;
    return true;
}

static void xx_ar_copy_raw_name(const xx_ar_member_header *header,
                                char output[17]) {
    size_t length = 16U;
    xx_mem_copy(output, header->name, 16U);
    while (length > 0U && (output[length - 1U] == ' ' ||
                           output[length - 1U] == '\0')) {
        --length;
    }
    output[length] = '\0';
}

static bool xx_ar_read_header(xx_io_device *device, int64_t total_size,
                              int64_t offset, xx_ar_member_header *header,
                              uint64_t *member_size, int64_t *next_offset) {
    uint64_t size_value;
    int64_t data_offset;
    int64_t data_end;

    if (!device || !header || !member_size || !next_offset || offset < 0 ||
        total_size < offset || total_size - offset < XX_AR_MEMBER_HEADER_SIZE ||
        !xx_ar_read_exact_at(device, offset, header, sizeof(*header)) ||
        header->trailer[0] != '`' || header->trailer[1] != '\n' ||
        !xx_ar_parse_uint(header->size, sizeof(header->size), 10U, false,
                          &size_value) || size_value > (uint64_t)INT64_MAX) {
        return false;
    }
    data_offset = offset + XX_AR_MEMBER_HEADER_SIZE;
    if (size_value > (uint64_t)(INT64_MAX - data_offset)) {
        return false;
    }
    data_end = data_offset + (int64_t)size_value;
    if (data_end > total_size) {
        return false;
    }
    if ((size_value & 1U) != 0U) {
        /* An odd-sized member is followed by one filler byte so that the next
         * header starts on an even offset. GNU ar writes '\n', but OS/2 emx,
         * Apple and several DOS librarians write '\0' or leave the byte
         * uninitialised, so the value must not be validated. A writer that
         * omits the filler entirely for the LAST member is also accepted:
         * there is no following header that could be misaligned by it. */
        if (data_end < total_size) {
            ++data_end;
        }
    }
    *member_size = size_value;
    *next_offset = data_end;
    return true;
}

/* Classify the name field of a header that xx_ar_read_header accepted.
 * Fails only for a "#1/" field whose length is not a usable number: the
 * member data offset then cannot be known. */
static bool xx_ar_classify_name(const xx_ar_member_header *header,
                                uint64_t member_size, xx_ar_raw_name *out) {
    char raw[17];
    size_t length;

    xx_ar_copy_raw_name(header, raw);
    length = xx_str_len(raw);
    xx_mem_zero(out, sizeof(*out));
    out->kind = XX_AR_NAME_SHORT;
    if (length >= 3U && raw[0] == '#' && raw[1] == '1' && raw[2] == '/') {
        uint64_t name_size;
        if (length == 3U ||
            !xx_ar_parse_uint(raw + 3U, length - 3U, 10U, false, &name_size) ||
            name_size > member_size || name_size > XX_AR_MAX_NAME_SIZE) {
            return false;
        }
        out->kind = XX_AR_NAME_BSD;
        out->value = name_size;
        xx_mem_copy(out->text, raw, length + 1U);
        return true;
    }
    out->is_name_table = xx_str_cmp(raw, "//") == 0;
    if (raw[0] == '/' && raw[1] >= '0' && raw[1] <= '9') {
        uint64_t name_offset;
        if (xx_ar_parse_uint(raw + 1U, length - 1U, 10U, false,
                             &name_offset)) {
            out->kind = XX_AR_NAME_GNU;
            out->value = name_offset;
            xx_mem_copy(out->text, raw, length + 1U);
            return true;
        }
    }
    /* SysV/GNU/MS end short names with '/', so that trailing spaces are not
     * lost; "/SYM64/" is the one special name that keeps it. */
    if (length > 1U && raw[length - 1U] == '/' &&
        xx_str_cmp(raw, "/SYM64/") != 0) {
        raw[--length] = '\0';
    }
    xx_mem_copy(out->text, raw, length + 1U);
    return true;
}

/* Index and name-table members, which are not files: the GNU/SysV and
 * Microsoft symbol tables ("/", "/SYM64/", Microsoft writes two "/"), the
 * long-name table ("//"), BSD symbol tables, and the Microsoft linker's
 * "/<NAME>/" members ("/<XFGHASHMAP>/", "/<ECSYMBOLS>/"). */
static bool xx_ar_name_is_special(const char *name) {
    size_t length;
    if (!name) {
        return false;
    }
    length = xx_str_len(name);
    if (length >= 3U && name[0] == '/' && name[1] == '<' &&
        name[length - 1U] == '>') {
        return true;
    }
    return xx_str_cmp(name, "/") == 0 || xx_str_cmp(name, "//") == 0 ||
           xx_str_cmp(name, "/SYM64/") == 0 ||
           xx_str_cmp(name, "__.SYMDEF") == 0 ||
           xx_str_cmp(name, "__.SYMDEF SORTED") == 0 ||
           xx_str_cmp(name, "__.SYMDEF_64") == 0 ||
           xx_str_cmp(name, "__.SYMDEF_64 SORTED") == 0 ||
           xx_str_cmp(name, "__.GOSYMDEF") == 0;
}

/* A BSD name is special only when it is short enough to be one of the
 * index names; the member walk and the table build apply the same rule. */
static bool xx_ar_bsd_name_is_special(const char *name, uint64_t name_size) {
    return name_size <= XX_AR_BSD_SPECIAL_PEEK && xx_ar_name_is_special(name);
}

static bool xx_ar_peek_bsd_special(xx_io_device *device, int64_t offset,
                                   uint64_t name_size) {
    char buffer[XX_AR_BSD_SPECIAL_PEEK + 1U];
    size_t length;
    if (name_size == 0U || name_size > XX_AR_BSD_SPECIAL_PEEK) {
        return false;
    }
    length = (size_t)name_size;
    if (!xx_ar_read_exact_at(device, offset, buffer, length)) {
        return false;
    }
    while (length > 0U && buffer[length - 1U] == '\0') {
        --length;
    }
    buffer[length] = '\0';
    return xx_ar_bsd_name_is_special(buffer, name_size);
}

static char *xx_ar_read_name_bytes(xx_io_device *device, int64_t offset,
                                   uint64_t size) {
    char *name;
    size_t length;
    if (size > XX_AR_MAX_NAME_SIZE || size > (uint64_t)(SIZE_MAX - 1U)) {
        return NULL;
    }
    length = (size_t)size;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) {
        return NULL;
    }
    if (length != 0U && !xx_ar_read_exact_at(device, offset, name, length)) {
        xx_mem_free(name);
        return NULL;
    }
    while (length > 0U && name[length - 1U] == '\0') {
        --length;
    }
    name[length] = '\0';
    return name;
}

static char *xx_ar_dup_bytes(const char *text, size_t length) {
    char *copy = xx_str_create_len(length);
    if (copy && length != 0U) {
        xx_mem_copy(copy, text, length);
    }
    return copy;
}

/* Look a GNU "/<n>" reference up in the loaded "//" table. GNU ends each
 * name with "/\n", SysV with "\n" and Microsoft with a NUL. A name longer
 * than XX_AR_MAX_NAME_SIZE, or one that runs past the loaded part of an
 * oversized table, is left unresolved. *scanned receives the number of table
 * bytes examined, which the caller charges to its name budget: many members
 * pointing at one unterminated run must not cost a full scan each for free.
 * At most max_scan bytes are examined; a name not found within them is left
 * unresolved as well. */
static char *xx_ar_table_name(const char *table, size_t loaded,
                              uint64_t table_size, uint64_t name_offset,
                              uint64_t max_scan, uint64_t *scanned) {
    size_t start;
    size_t end;
    size_t limit;
    bool terminated = false;

    *scanned = 0U;
    if (!table || name_offset >= (uint64_t)loaded || max_scan == 0U) {
        return NULL;
    }
    start = (size_t)name_offset;
    limit = loaded - start > (size_t)XX_AR_MAX_NAME_SIZE
                ? start + (size_t)XX_AR_MAX_NAME_SIZE + 1U
                : loaded;
    if ((uint64_t)(limit - start) > max_scan) {
        limit = start + (size_t)max_scan;
    }
    for (end = start; end < limit; ++end) {
        if (table[end] == '\0' || table[end] == '\n') {
            terminated = true;
            break;
        }
    }
    *scanned = (uint64_t)(end - start) + 1U;
    if (!terminated && (limit < loaded || table_size > (uint64_t)loaded)) {
        return NULL;
    }
    if (end > start && table[end - 1U] == '/') {
        --end;
    }
    if (end - start > XX_AR_MAX_NAME_SIZE) {
        return NULL;
    }
    return xx_ar_dup_bytes(table + start, end - start);
}

static void xx_ar_members_cleanup(xx_ar_members *members) {
    if (!members) {
        return;
    }
    if (members->items) {
        for (size_t i = 0; i < members->count; ++i) {
            if (members->items[i].name) {
                xx_str_free(members->items[i].name);
            }
            if (members->items[i].extract_name) {
                xx_str_free(members->items[i].extract_name);
            }
        }
        xx_mem_free(members->items);
    }
    xx_mem_zero(members, sizeof(*members));
}

/* ------------------------------------------------------ extraction names -- */

static char xx_ar_upper(char ch) {
    return (ch >= 'a' && ch <= 'z') ? (char)(ch - 'a' + 'A') : ch;
}

static bool xx_ar_equal_ci(const char *text, const char *word, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (xx_ar_upper(text[i]) != word[i]) {
            return false;
        }
    }
    return true;
}

/* Windows opens a device instead of a file for these names, with or without
 * an extension: CON, PRN, AUX, NUL, COM0-9, LPT0-9 (also with the
 * superscript digits 1-3), CONIN$, CONOUT$ and CLOCK$. */
static bool xx_ar_is_device_name(const char *component, size_t length) {
    static const char *const k_devices[] = {
        "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"
    };
    size_t base = 0U;
    while (base < length && component[base] != '.') {
        ++base;
    }
    while (base > 0U && component[base - 1U] == ' ') {
        --base;
    }
    for (size_t i = 0; i < sizeof(k_devices) / sizeof(k_devices[0]); ++i) {
        if (xx_str_len(k_devices[i]) == base &&
            xx_ar_equal_ci(component, k_devices[i], base)) {
            return true;
        }
    }
    if (base >= 4U && (xx_ar_equal_ci(component, "COM", 3U) ||
                       xx_ar_equal_ci(component, "LPT", 3U))) {
        const unsigned char *tail = (const unsigned char *)component + 3;
        if (base == 4U && tail[0] >= '0' && tail[0] <= '9') {
            return true;
        }
        if (base == 5U && tail[0] == 0xC2U &&
            (tail[1] == 0xB9U || tail[1] == 0xB2U || tail[1] == 0xB3U)) {
            return true;
        }
    }
    return false;
}

/* Length of the well-formed UTF-8 sequence at text (1-4), or 0 when the bytes
 * there are not one (stray continuation, overlong form, surrogate, a code
 * point above U+10FFFF, or a truncated sequence). The byte after a
 * NUL-terminated string's last byte is never read: NUL is no continuation. */
static size_t xx_ar_utf8_length(const unsigned char *text) {
    unsigned char lead = text[0];
    unsigned char low = 0x80U;
    unsigned char high = 0xBFU;
    size_t length;
    if (lead < 0x80U) {
        return 1U;
    }
    if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2U;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        length = 3U;
        if (lead == 0xE0U) {
            low = 0xA0U;
        } else if (lead == 0xEDU) {
            high = 0x9FU;
        }
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4U;
        if (lead == 0xF0U) {
            low = 0x90U;
        } else if (lead == 0xF4U) {
            high = 0x8FU;
        }
    } else {
        return 0U;
    }
    if (text[1] < low || text[1] > high) {
        return 0U;
    }
    for (size_t i = 2U; i < length; ++i) {
        if (text[i] < 0x80U || text[i] > 0xBFU) {
            return 0U;
        }
    }
    return length;
}

/* Build the relative path a member is extracted to. Microsoft librarians
 * store the path the object was built from: ".\\Release\\foo.obj", or an
 * absolute "d:\\os\\obj\\...\\dll\\mt\\..\\..\\convert\\...\\atof.obj"
 * (libucrt.lib: 775 of its 784 members). A drive prefix and leading
 * separators are dropped (as tar drops a leading '/'), "." components and
 * empty components are dropped, separators become '/', trailing dots and
 * spaces, which Windows ignores, are trimmed, and a ".." component removes
 * the component before it - on the text, before anything touches the disk.
 * A ".." with nothing left to remove (it would leave the destination), a
 * control character, one of :<>"|?*, a device name, or nothing left at all
 * refuses the member (NULL).
 * Member names are raw bytes (Latin-1, CP437, ...), but the path is opened
 * as UTF-8, and Windows turns every byte that is not well-formed UTF-8 into
 * U+FFFD: "\xe4.o" and "\xf6.o" would open the same file. Such bytes are
 * written as "%XX" instead, so that different names stay different and the
 * duplicate check below sees exactly the path the file system gets. */
static char *xx_ar_make_extract_name(const char *name) {
    const char *cursor = name;
    char *result;
    size_t out = 0U;
    size_t length_in;

    if (!name) {
        return NULL;
    }
    if (((cursor[0] >= 'A' && cursor[0] <= 'Z') ||
         (cursor[0] >= 'a' && cursor[0] <= 'z')) && cursor[1] == ':') {
        cursor += 2;
    }
    while (*cursor == '/' || *cursor == '\\') {
        ++cursor;
    }
    /* Room for the name plus two more bytes per byte to be escaped. */
    length_in = 0U;
    for (const unsigned char *at = (const unsigned char *)cursor; *at != 0U;) {
        size_t sequence = xx_ar_utf8_length(at);
        if (length_in > SIZE_MAX - 4U) {
            return NULL;
        }
        length_in += sequence == 0U ? 3U : sequence;
        at += sequence == 0U ? 1U : sequence;
    }
    result = xx_str_create_len(length_in);
    if (!result) {
        return NULL;
    }
    while (*cursor != '\0') {
        const char *component = cursor;
        size_t length;
        size_t kept;
        while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') {
            unsigned char ch = (unsigned char)*cursor;
            if (ch < 32U || ch == ':' || ch == '<' || ch == '>' ||
                ch == '"' || ch == '|' || ch == '?' || ch == '*') {
                xx_str_free(result);
                return NULL;
            }
            ++cursor;
        }
        length = (size_t)(cursor - component);
        if (*cursor != '\0') {
            ++cursor;
        }
        if (length == 0U || (length == 1U && component[0] == '.')) {
            continue;
        }
        if (length == 2U && component[0] == '.' && component[1] == '.') {
            if (out == 0U) {
                xx_str_free(result);
                return NULL;
            }
            while (out > 0U && result[out - 1U] != '/') {
                --out;
            }
            if (out > 0U) {
                --out;  /* the separator before the removed component */
            }
            continue;
        }
        kept = length;
        while (kept > 0U && (component[kept - 1U] == '.' ||
                             component[kept - 1U] == ' ')) {
            --kept;
        }
        if (kept == 0U || xx_ar_is_device_name(component, kept)) {
            xx_str_free(result);
            return NULL;
        }
        if (out != 0U) {
            result[out++] = '/';
        }
        for (size_t i = 0U; i < kept;) {
            const unsigned char *at = (const unsigned char *)component + i;
            size_t sequence = xx_ar_utf8_length(at);
            if (sequence == 0U) {
                static const char k_hex[] = "0123456789ABCDEF";
                result[out++] = '%';
                result[out++] = k_hex[at[0] >> 4];
                result[out++] = k_hex[at[0] & 15U];
                ++i;
                continue;
            }
            /* A well-formed sequence never contains '.', ' ' or a
             * separator, so it cannot run past kept. */
            xx_mem_copy(result + out, at, sequence);
            out += sequence;
            i += sequence;
        }
    }
    if (out == 0U) {
        xx_str_free(result);
        return NULL;
    }
    result[out] = '\0';
    return result;
}

typedef struct xx_ar_name_key_s {
    const char *key;
    size_t item;
} xx_ar_name_key;

/* Fold value of a code point that has, or may have, a case partner outside
 * the ranges folded exactly below. */
#define XX_AR_FOLD_CASED 0x110000U
/* Fold value base of a byte that is not well-formed UTF-8 (extraction names
 * never hold one; kept distinct per byte for completeness). */
#define XX_AR_FOLD_BAD_BYTE 0x110100U

/* Map one code point to the value that the duplicate check compares.
 * Windows (NTFS $UpCase) and macOS compare names ignoring case, and not
 * only ASCII case: "\xc3\x84.o" and "\xc3\xa4.o" (A/a umlaut) are one file.
 * ASCII, Latin-1 and Latin Extended-A are folded exactly (U+0131 dotless i
 * and U+017F long s upper-case to ASCII I and S). Every other code point in
 * a script or block that has letter case - Latin Extended-B/IPA, Greek,
 * Cyrillic, Armenian, Georgian, Cherokee, Latin Extended Additional, Greek
 * Extended, letter-like symbols and number forms, circled letters,
 * Glagolitic, Coptic, the extended Latin/Cyrillic blocks, fullwidth Latin
 * and the case-bearing supplementary scripts - folds to one shared value.
 * That is conservative: two such names of the same shape are treated as
 * equal, and the later one is renamed, even when a file system would keep
 * them apart. A missed collision would overwrite a member; an extra rename
 * only changes its name. Everything else (CJK, symbols, ...) has no case
 * and is compared as it is. */
static uint32_t xx_ar_fold_code_point(uint32_t cp) {
    static const uint32_t k_cased[][2] = {
        {0x0180U, 0x02AFU}, {0x0345U, 0x0345U}, {0x0370U, 0x052FU},
        {0x0531U, 0x058FU}, {0x10A0U, 0x10FFU}, {0x13A0U, 0x13FFU},
        {0x1C80U, 0x1CBFU}, {0x1D00U, 0x1DBFU}, {0x1E00U, 0x1FFFU},
        {0x2100U, 0x218FU}, {0x24B6U, 0x24E9U}, {0x2C00U, 0x2D2FU},
        {0xA640U, 0xA69FU}, {0xA720U, 0xA7FFU}, {0xAB30U, 0xABBFU},
        {0xFF21U, 0xFF5AU}, {0x10400U, 0x104FFU}, {0x10570U, 0x105BFU},
        {0x10C80U, 0x10CFFU}, {0x118A0U, 0x118FFU}, {0x16E40U, 0x16E9FU},
        {0x1E900U, 0x1E95FU}
    };
    if (cp < 0x80U) {
        return (cp >= 'a' && cp <= 'z') ? cp - 0x20U : cp;
    }
    if (cp < 0x100U) {
        if (cp == 0xB5U || cp == 0xDFU) {
            return XX_AR_FOLD_CASED; /* micro sign -> Greek Mu; sharp s */
        }
        if (cp >= 0xE0U && cp <= 0xFEU && cp != 0xF7U) {
            return cp - 0x20U;
        }
        return cp == 0xFFU ? 0x178U : cp;
    }
    if (cp < 0x180U) {
        if (cp == 0x130U || cp == 0x131U) {
            return 'I';
        }
        if (cp == 0x17FU) {
            return 'S';
        }
        if (cp == 0x138U || cp == 0x149U || cp == 0x178U) {
            return cp;
        }
        if ((cp >= 0x139U && cp <= 0x148U) || cp >= 0x179U) {
            return (cp & 1U) != 0U ? cp : cp - 1U;
        }
        return cp & ~1U;
    }
    for (size_t i = 0; i < sizeof(k_cased) / sizeof(k_cased[0]); ++i) {
        if (cp >= k_cased[i][0] && cp <= k_cased[i][1]) {
            return XX_AR_FOLD_CASED;
        }
    }
    return cp;
}

/* The next compared unit of a NUL-terminated name (0 at its end). */
static uint32_t xx_ar_fold_next(const unsigned char **cursor) {
    const unsigned char *at = *cursor;
    size_t length;
    uint32_t cp;
    if (at[0] == 0U) {
        return 0U;
    }
    length = xx_ar_utf8_length(at);
    if (length == 0U) {
        *cursor = at + 1;
        return XX_AR_FOLD_BAD_BYTE + at[0];
    }
    if (length == 1U) {
        cp = at[0];
    } else if (length == 2U) {
        cp = ((uint32_t)(at[0] & 0x1FU) << 6) | (uint32_t)(at[1] & 0x3FU);
    } else if (length == 3U) {
        cp = ((uint32_t)(at[0] & 0x0FU) << 12) |
             ((uint32_t)(at[1] & 0x3FU) << 6) | (uint32_t)(at[2] & 0x3FU);
    } else {
        cp = ((uint32_t)(at[0] & 0x07U) << 18) |
             ((uint32_t)(at[1] & 0x3FU) << 12) |
             ((uint32_t)(at[2] & 0x3FU) << 6) | (uint32_t)(at[3] & 0x3FU);
    }
    *cursor = at + length;
    return xx_ar_fold_code_point(cp);
}

/* Order of two extraction paths under the fold above. With prefix_only,
 * returns 0 as soon as y is used up (x starts with y). */
static int xx_ar_compare_fold(const char *x, const char *y, bool prefix_only) {
    const unsigned char *a = (const unsigned char *)x;
    const unsigned char *b = (const unsigned char *)y;
    for (;;) {
        uint32_t ua = xx_ar_fold_next(&a);
        uint32_t ub = xx_ar_fold_next(&b);
        if (prefix_only && ub == 0U) {
            return 0;
        }
        if (ua != ub) {
            return ua < ub ? -1 : 1;
        }
        if (ua == 0U) {
            return 0;
        }
    }
}

static int xx_ar_compare_keys(const void *left, const void *right) {
    const xx_ar_name_key *a = (const xx_ar_name_key *)left;
    const xx_ar_name_key *b = (const xx_ar_name_key *)right;
    int order = xx_ar_compare_fold(a->key, b->key, false);
    if (order != 0) {
        return order;
    }
    /* Equal names keep archive order, so the first member keeps its name. */
    return a->item < b->item ? -1 : (a->item > b->item ? 1 : 0);
}

/* "dir\\NAME.EXT" -> "dir\\NAME_<n>.EXT": the suffix goes before the last dot
 * of the last component (after it when the component has no dot or starts
 * with its only dot). */
static char *xx_ar_suffixed_name(const char *name, uint32_t number) {
    char digits[12];
    size_t digit_count = 0U;
    size_t length = xx_str_len(name);
    size_t start = 0U;
    size_t dot;
    size_t at = 0U;
    char *result;

    for (size_t i = 0; i < length; ++i) {
        if (name[i] == '/' || name[i] == '\\') {
            start = i + 1U;
        }
    }
    dot = length;
    for (size_t i = start; i < length; ++i) {
        if (name[i] == '.') {
            dot = i;
        }
    }
    if (dot == start) {
        dot = length;
    }
    do {
        digits[digit_count++] = (char)('0' + (number % 10U));
        number /= 10U;
    } while (number != 0U && digit_count < sizeof(digits));
    result = xx_str_create_len(length + digit_count + 1U);
    if (!result) {
        return NULL;
    }
    for (size_t i = 0; i < dot; ++i) {
        result[at++] = name[i];
    }
    result[at++] = '_';
    while (digit_count != 0U) {
        result[at++] = digits[--digit_count];
    }
    for (size_t i = dot; i < length; ++i) {
        result[at++] = name[i];
    }
    result[at] = '\0';
    return result;
}

/* First index whose key is not below text (prefix_only: whose first units
 * are not below text). keys is sorted, and truncating every key to the
 * length of text keeps that order, so both searches are valid. */
static size_t xx_ar_lower_bound(const xx_ar_name_key *keys, size_t used,
                                const char *text, bool prefix_only) {
    size_t low = 0U;
    size_t high = used;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (xx_ar_compare_fold(keys[middle].key, text, prefix_only) < 0) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low;
}

/* Is there a key equal to text under the fold? */
static bool xx_ar_key_exists(const xx_ar_name_key *keys, size_t used,
                             const char *text) {
    size_t at = xx_ar_lower_bound(keys, used, text, false);
    return at < used && xx_ar_compare_fold(keys[at].key, text, false) == 0;
}

/* Is text a directory of some key, that is, does a key start with
 * text + "/"? 1 yes, 0 no, -1 out of memory. */
static int xx_ar_key_is_directory(const xx_ar_name_key *keys, size_t used,
                                  const char *text) {
    char *probe = xx_str_concat(text, "/");
    size_t at;
    int found;
    if (!probe) {
        return -1;
    }
    at = xx_ar_lower_bound(keys, used, probe, true);
    found = at < used && xx_ar_compare_fold(keys[at].key, probe, true) == 0;
    xx_str_free(probe);
    return found;
}

/* Two members must never be written to the same file, and no member may be
 * written where another one needs a directory. Microsoft import libraries
 * hold one member per imported function, all named after the DLL, static
 * libraries can hold one object twice, and a file "a" next to a member
 * "a/b" would make one of the two fail.
 * In every group of equal extraction paths (compared under the fold above)
 * the first member keeps its path and each later one gets PATH_<n>.EXT, with
 * n counting up past any path that another member has, or that is a
 * directory of another member. When the group's path is itself such a
 * directory, its first member is renamed too, so that the directory wins
 * whatever the member order. Renaming touches only the last component, so
 * the set of directories never changes.
 * A candidate is "PATH" + "_" + digits inserted before the extension, so two
 * different groups can never produce the same candidate, and every number a
 * group skips is a path, or a directory, that some member already has: the
 * work stays O(total name length * log(members)).
 * The record name of a renamed member gets the same "_<n>" suffix, applied to
 * its stored name, so the listing keeps the stored style of every member
 * ("d:\\lib\\thing.obj", "d:\\lib\\thing_1.obj") while matching the file
 * that extraction writes ("lib/thing.obj", "lib/thing_1.obj"). */
static bool xx_ar_make_names_unique(xx_ar_members *members) {
    xx_ar_name_key *keys;
    char **fresh;
    uint32_t *numbers;
    size_t used = 0U;
    bool ok = true;

    if (members->count == 0U) {
        return true;
    }
    if (members->count > SIZE_MAX / sizeof(*keys) ||
        members->count > SIZE_MAX / sizeof(*fresh) ||
        members->count > SIZE_MAX / sizeof(*numbers)) {
        return false;
    }
    keys = (xx_ar_name_key *)xx_mem_alloc(members->count * sizeof(*keys));
    if (!keys) {
        return false;
    }
    for (size_t i = 0; i < members->count; ++i) {
        const xx_ar_member *member = &members->items[i];
        if (!member->special && member->extract_name) {
            keys[used].key = member->extract_name;
            keys[used].item = i;
            ++used;
        }
    }
    if (used < 2U) {
        xx_mem_free(keys);
        return true;
    }
    xx_rt_qsort(keys, used, sizeof(*keys), xx_ar_compare_keys);
    fresh = (char **)xx_mem_alloc(used * sizeof(*fresh));
    numbers = (uint32_t *)xx_mem_alloc(used * sizeof(*numbers));
    if (!fresh || !numbers) {
        if (fresh) xx_mem_free(fresh);
        if (numbers) xx_mem_free(numbers);
        xx_mem_free(keys);
        return false;
    }
    xx_mem_zero(fresh, used * sizeof(*fresh));
    xx_mem_zero(numbers, used * sizeof(*numbers));

    for (size_t group = 0U; ok && group < used;) {
        size_t end = group + 1U;
        uint32_t number = 0U;
        int is_directory;
        while (end < used &&
               xx_ar_compare_fold(keys[end].key, keys[group].key, false) == 0) {
            ++end;
        }
        is_directory = xx_ar_key_is_directory(keys, used, keys[group].key);
        if (is_directory < 0) {
            ok = false;
            break;
        }
        for (size_t index = is_directory ? group : group + 1U;
             ok && index < end; ++index) {
            char *candidate = NULL;
            while (ok) {
                int taken;
                if (number == UINT32_MAX) {
                    ok = false;
                    break;
                }
                ++number;
                candidate = xx_ar_suffixed_name(keys[index].key, number);
                if (!candidate) {
                    ok = false;
                    break;
                }
                taken = xx_ar_key_exists(keys, used, candidate)
                            ? 1
                            : xx_ar_key_is_directory(keys, used, candidate);
                if (taken == 0) {
                    break;
                }
                xx_str_free(candidate);
                candidate = NULL;
                if (taken < 0) {
                    ok = false;
                }
            }
            fresh[index] = candidate;
            numbers[index] = number;
        }
        group = end;
    }

    /* keys[] points at the old paths, so they are replaced only now. */
    for (size_t index = 0U; index < used; ++index) {
        xx_ar_member *member = &members->items[keys[index].item];
        char *record_name;
        if (!fresh[index]) {
            continue;
        }
        record_name = ok && member->name
                          ? xx_ar_suffixed_name(member->name, numbers[index])
                          : NULL;
        if (!record_name) {
            ok = false;
            xx_str_free(fresh[index]);
            continue;
        }
        xx_str_free(member->name);
        member->name = record_name;
        xx_str_free(member->extract_name);
        member->extract_name = fresh[index];
    }
    xx_mem_free(numbers);
    xx_mem_free(fresh);
    xx_mem_free(keys);
    return ok;
}

static bool xx_ar_prepare_extract_names(xx_ar_members *members) {
    for (size_t i = 0; i < members->count; ++i) {
        xx_ar_member *member = &members->items[i];
        if (!member->special && member->name) {
            member->extract_name = xx_ar_make_extract_name(member->name);
        }
    }
    return xx_ar_make_names_unique(members);
}

/* ----------------------------------------------------------------- parse -- */

/* Walk the member chain without allocating: member count, visible count,
 * archive end, flavour, and where the "//" name table is. */
static bool xx_ar_walk(Abstractformat *self, xx_ar_members *members,
                       int64_t *table_offset, uint64_t *table_size,
                       xx_pd_struct *pd) {
    uint8_t magic[XX_AR_MAGIC_SIZE];
    int64_t total_size;
    int64_t offset;
    size_t member_count = 0U;
    uint64_t visible_count = 0U;
    bool first_is_symbol_table = false;

    total_size = xx_io_total_size(self->device);
    if (total_size < 0 || self->base_address > total_size ||
        total_size - self->base_address < XX_AR_MAGIC_SIZE ||
        !xx_ar_read_exact_at(self->device, self->base_address, magic,
                             sizeof(magic))) {
        ((xx_ar *)self)->is_thin = false;
        return false;
    }
    if (xx_rt_memcmp(magic, XX_AR_THIN_MAGIC, XX_AR_MAGIC_SIZE) == 0) {
        ((xx_ar *)self)->is_thin = true;
        return false;
    }
    ((xx_ar *)self)->is_thin = false;
    if (xx_rt_memcmp(magic, XX_AR_MAGIC, XX_AR_MAGIC_SIZE) != 0) {
        return false;
    }

    offset = self->base_address + XX_AR_MAGIC_SIZE;
    while (offset < total_size) {
        xx_ar_member_header header;
        xx_ar_raw_name raw;
        uint64_t member_size;
        int64_t next_offset;
        bool special;
        if (pd && xx_pd_is_stopped(pd)) {
            return false;
        }
        if (member_count >= (size_t)XX_AR_MAX_MEMBERS) {
            break;
        }
        /* The chain ends where it stops making sense.  Archives that were
         * moved through a shell archive or a text-mode transfer routinely
         * lose their last few bytes, or grow a block of unrelated text after
         * the members; the members that DID survive are still readable, and
         * refusing the whole file for the sake of its tail loses them.  The
         * archive is still gated on "!<arch>\n" plus a first member header
         * that parses, so a break here can never turn unrelated data into an
         * archive - it only stops a good chain early. */
        if (!xx_ar_read_header(self->device, total_size, offset, &header,
                               &member_size, &next_offset) ||
            !xx_ar_classify_name(&header, member_size, &raw)) {
            if (member_count == 0U) {
                return false;
            }
            break;
        }
        if (raw.is_name_table) {
            *table_offset = offset + XX_AR_MEMBER_HEADER_SIZE;
            *table_size = member_size;
        }
        if (raw.kind == XX_AR_NAME_BSD) {
            special = xx_ar_peek_bsd_special(
                self->device, offset + XX_AR_MEMBER_HEADER_SIZE, raw.value);
        } else {
            special = raw.kind == XX_AR_NAME_SHORT &&
                      xx_ar_name_is_special(raw.text);
        }
        if (raw.kind == XX_AR_NAME_SHORT) {
            if (member_count == 0U) {
                first_is_symbol_table = xx_str_cmp(raw.text, "/") == 0 &&
                                        !raw.is_name_table;
                if (xx_str_cmp(raw.text, "debian-binary") == 0) {
                    members->kind = XX_AR_KIND_DEB;
                }
            } else if (member_count == 1U && first_is_symbol_table &&
                       !raw.is_name_table &&
                       xx_str_cmp(raw.text, "/") == 0) {
                members->kind = XX_AR_KIND_MSLIB;
            }
        }
        if (!special) {
            ++visible_count;
        }
        ++member_count;
        offset = next_offset;
    }
    /* No member count check here. A first header that fails to parse is
     * already refused inside the loop, so zero members can only mean the
     * file ends right after "!<arch>\n" -- an empty archive, which is
     * valid and simply has nothing to list. */
    members->count = member_count;
    members->visible_count = visible_count;
    /* The archive ends at the last boundary the chain reached, which is EOF
     * for an intact file and the start of the damaged tail otherwise. */
    members->archive_end = offset;
    return true;
}

/* Second pass over the chain the walk measured: offsets, metadata and
 * resolved names of every member. */
static bool xx_ar_build_members(Abstractformat *self, xx_ar_members *members,
                                int64_t table_offset, uint64_t table_size,
                                xx_pd_struct *pd) {
    int64_t total_size = xx_io_total_size(self->device);
    int64_t offset = self->base_address + XX_AR_MAGIC_SIZE;
    char *table = NULL;
    size_t table_loaded = 0U;
    bool table_tried = false;
    uint64_t names_total = 0U;
    uint64_t visible_count = 0U;
    bool ok = true;

    if (members->count == 0U) {
        return true;
    }
    if (members->count > SIZE_MAX / sizeof(*members->items)) {
        return false;
    }
    members->items = (xx_ar_member *)xx_mem_alloc(members->count *
                                                  sizeof(xx_ar_member));
    if (!members->items) {
        return false;
    }
    xx_mem_zero(members->items, members->count * sizeof(xx_ar_member));

    for (size_t i = 0; ok && i < members->count; ++i) {
        xx_ar_member_header header;
        xx_ar_raw_name raw;
        xx_ar_member *member = &members->items[i];
        uint64_t member_size;
        int64_t next_offset;
        uint64_t cost = 0U;
        char *name = NULL;

        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_ar_read_header(self->device, total_size, offset, &header,
                               &member_size, &next_offset) ||
            !xx_ar_classify_name(&header, member_size, &raw)) {
            ok = false;
            break;
        }
        /* The timestamp/uid/gid/mode fields are informational only: the member
         * chain is defined by name/size/trailer alone. Real archives abuse
         * them freely - MS import libraries pack several values into the
         * timestamp field ("787117117 0 "), Atari/DRI .A libraries store a raw
         * binary 4-byte time inside the mode field, and Microsoft writes "-1".
         * Treat an unparsable field as "unknown" (0) instead of discarding
         * the whole archive. */
        if (!xx_ar_parse_uint(header.timestamp, sizeof(header.timestamp), 10U,
                              true, &member->timestamp)) {
            member->timestamp = 0U;
        }
        if (!xx_ar_parse_uint(header.owner_id, sizeof(header.owner_id), 10U,
                              true, &member->owner_id)) {
            member->owner_id = 0U;
        }
        if (!xx_ar_parse_uint(header.group_id, sizeof(header.group_id), 10U,
                              true, &member->group_id)) {
            member->group_id = 0U;
        }
        if (!xx_ar_parse_uint(header.mode, sizeof(header.mode), 8U, true,
                              &member->mode)) {
            member->mode = 0U;
        }

        member->header_offset = offset;
        member->stored_data_offset = offset + XX_AR_MEMBER_HEADER_SIZE;
        member->data_offset = member->stored_data_offset;
        member->stored_size = (int64_t)member_size;
        member->data_size = (int64_t)member_size;

        /* Every long name costs the bytes read or scanned for it, charged to
         * one budget: a hostile archive cannot make the member table, or the
         * time spent building it, grow past XX_AR_MAX_NAMES_TOTAL. Once the
         * budget is spent, a long name is not read: the member keeps its raw
         * 16-byte name field ("#1/<n>", "/<n>"), so it is still listed and
         * extracted, and the table always has every member the walk counted
         * (raw fields cost at most 17 bytes times XX_AR_MAX_MEMBERS). */
        if (raw.kind == XX_AR_NAME_BSD) {
            cost = raw.value + 1U;
            if (cost <= XX_AR_MAX_NAMES_TOTAL - names_total) {
                name = xx_ar_read_name_bytes(self->device,
                                             member->stored_data_offset,
                                             raw.value);
            } else {
                cost = 0U;
            }
            if (name) {
                member->special = xx_ar_bsd_name_is_special(name, raw.value);
            } else {
                /* Same rule as the walk, so the visible count agrees. */
                member->special = xx_ar_peek_bsd_special(
                    self->device, member->stored_data_offset, raw.value);
                name = xx_str_dup(raw.text);
            }
            member->name_prefix_size = raw.value;
            member->data_offset += (int64_t)raw.value;
            member->data_size -= (int64_t)raw.value;
        } else if (raw.kind == XX_AR_NAME_GNU) {
            if (!table_tried) {
                table_tried = true;
                if (table_offset >= 0) {
                    size_t want = table_size > XX_AR_MAX_NAME_TABLE
                                      ? (size_t)XX_AR_MAX_NAME_TABLE
                                      : (size_t)table_size;
                    table = (char *)xx_mem_alloc(want + 1U);
                    if (table && want != 0U &&
                        !xx_ar_read_exact_at(self->device, table_offset, table,
                                             want)) {
                        xx_mem_free(table);
                        table = NULL;
                    }
                    table_loaded = table ? want : 0U;
                }
            }
            name = xx_ar_table_name(table, table_loaded, table_size,
                                    raw.value,
                                    XX_AR_MAX_NAMES_TOTAL - names_total,
                                    &cost);
            if (!name) {
                /* An unresolvable reference keeps its raw "/<n>" field,
                 * as 7-Zip does; the rest of the archive stays readable. */
                name = xx_str_dup(raw.text);
            }
            member->special = false;
        } else {
            name = xx_str_dup(raw.text);
            member->special = name && xx_ar_name_is_special(name);
        }
        if (!name) {
            ok = false;  /* out of memory */
            break;
        }
        member->name = name;
        names_total = cost > XX_AR_MAX_NAMES_TOTAL - names_total
                          ? XX_AR_MAX_NAMES_TOTAL
                          : names_total + cost;
        if (!member->special) {
            ++visible_count;
        }
        offset = next_offset;
    }
    if (table) {
        xx_mem_free(table);
    }
    if (ok) {
        members->visible_count = visible_count;
    }
    return ok;
}

static bool xx_ar_parse_archive(Abstractformat *self, xx_ar_members *members,
                                unsigned flags, xx_pd_struct *pd) {
    int64_t table_offset = -1;
    uint64_t table_size = 0U;

    if (!members) {
        return false;
    }
    xx_mem_zero(members, sizeof(*members));
    if (!self || !self->device || self->base_address < 0 ||
        !xx_ar_walk(self, members, &table_offset, &table_size, pd)) {
        xx_mem_zero(members, sizeof(*members));
        return false;
    }
    if ((flags & XX_AR_PARSE_MEMBERS) == 0U) {
        return true;
    }
    if (!xx_ar_build_members(self, members, table_offset, table_size, pd) ||
        ((flags & XX_AR_PARSE_EXTRACT_NAMES) != 0U &&
         !xx_ar_prepare_extract_names(members))) {
        xx_ar_members_cleanup(members);
        return false;
    }
    return true;
}

static bool xx_ar_copy_options(xx_list_s *destination,
                               const xx_list_s *source) {
    if (!destination || !source) {
        return source == NULL;
    }
    for (size_t i = 0; i < source->count; ++i) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, i);
        xx_meta copy;
        if (!item) {
            continue;
        }
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_ar_find_option(const xx_list_s *options,
                                       uint32_t meta_id) {
    if (!options) {
        return NULL;
    }
    for (size_t i = 0; i < options->count; ++i) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (item && item->meta_id == meta_id) {
            return &item->var;
        }
    }
    return NULL;
}

static bool xx_ar_populate_archive_record(xx_archive_record *record,
                                          const xx_ar_member *member) {
    if (!record || !member || member->special) {
        return false;
    }
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = XX_AR_MEMBER_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          member->name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->mode) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static void xx_ar_archive_stream_free(void *pointer) {
    xx_ar_archive_stream *stream = (xx_ar_archive_stream *)pointer;
    if (stream) {
        xx_ar_members_cleanup(&stream->members);
        xx_mem_free(stream);
    }
}

void xx_ar_init(xx_ar *ar, xx_io_device *dev, int64_t base_address) {
    if (!ar) {
        return;
    }
    xx_mem_zero(ar, sizeof(*ar));
    xx_format_init(&ar->format, dev, base_address);
    ar->format.endian = XX_ENDIAN_UNKNOWN;
    ar->format.file_type = XX_FILE_TYPE_AR;
    ar->format.format_type = XX_TYPE_ARCHIVE;
    ar->format.is_archive = true;
    xx_format_set_mime_type(&ar->format, "application/x-archive");
    xx_format_set_extension(&ar->format, "a");

    ar->format.check_is_valid = xx_ar_check_is_valid;
    ar->format.handle_base_info = xx_ar_handle_base_info;
    ar->format.get_format_size = xx_ar_get_format_size;
    ar->format.get_number_of_archive_records =
        xx_ar_get_number_of_archive_records;
    ar->format.create_archive_records_reading =
        xx_ar_create_archive_records_reading;
    ar->format.get_current_archive_record = xx_ar_get_current_archive_record;
    ar->format.unpack_current_archive_record =
        xx_ar_unpack_current_archive_record;
    ar->format.archive_record_move_to_next =
        xx_ar_archive_record_move_to_next;
    ar->format.free_archive_records_reading =
        xx_ar_free_archive_records_reading;
    ar->format.data_struct_id_to_string = xx_ar_data_struct_id_to_string;
    ar->format.data_struct_string_to_id = xx_ar_data_struct_string_to_id;
    ar->format.create_data_structs_reading = xx_ar_create_data_structs_reading;
    ar->format.get_current_data_struct = xx_ar_get_current_data_struct;
    ar->format.data_struct_move_to_next = xx_ar_data_struct_move_to_next;
    ar->format.free_data_structs_reading = xx_ar_free_data_structs_reading;
    ar->format.create_data_struct_records_reading =
        xx_ar_create_data_struct_records_reading;
    ar->format.get_current_data_struct_record =
        xx_ar_get_current_data_struct_record;
    ar->format.data_struct_record_move_to_next =
        xx_ar_data_struct_record_move_to_next;
    ar->format.free_data_struct_records_reading =
        xx_ar_free_data_struct_records_reading;
    ar->format.destroy = xx_ar_vtable_destroy;
    ar->archive_end = -1;
}

xx_ar *xx_ar_create(xx_io_device *dev, int64_t base_address) {
    xx_ar *ar = (xx_ar *)xx_mem_alloc(sizeof(*ar));
    if (ar) {
        xx_ar_init(ar, dev, base_address);
    }
    return ar;
}

void xx_ar_destroy(xx_ar *ar) {
    if (ar && ar->format.close) {
        ar->format.close(&ar->format);
    }
    if (ar) xx_format_cleanup_extra_parameters(&ar->format);
}

static void xx_ar_vtable_destroy(Abstractformat *self) {
    xx_ar_destroy((xx_ar *)self);
}

void xx_ar_free(xx_ar *ar) {
    if (ar) {
        xx_ar_destroy(ar);
        xx_mem_free(ar);
    }
}

bool xx_ar_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    uint8_t magic[XX_AR_MAGIC_SIZE];
    int64_t total_size;
    (void)pd;
    if (!self || !self->device || self->base_address < 0) {
        return false;
    }
    ((xx_ar *)self)->is_thin = false;
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        total_size - self->base_address < XX_AR_MAGIC_SIZE ||
        !xx_ar_read_exact_at(self->device, self->base_address, magic,
                             sizeof(magic))) {
        return false;
    }
    if (xx_rt_memcmp(magic, XX_AR_THIN_MAGIC, XX_AR_MAGIC_SIZE) == 0) {
        ((xx_ar *)self)->is_thin = true;
        return false;
    }
    return xx_rt_memcmp(magic, XX_AR_MAGIC, XX_AR_MAGIC_SIZE) == 0;
}

bool xx_ar_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ar_members members;
    xx_ar *ar;
    /* Counting needs no member table: the walk alone gives every number
     * reported here, so opening an archive allocates nothing. */
    if (!self || !self->device || !xx_ar_parse_archive(self, &members, 0U, pd)) {
        if (self) {
            self->is_valid = false;
        }
        return false;
    }
    ar = (xx_ar *)self;
    if (members.kind == XX_AR_KIND_DEB) {
        xx_format_set_extension(self, "deb");
        xx_format_set_mime_type(self, "application/vnd.debian.binary-package");
    } else if (members.kind == XX_AR_KIND_MSLIB) {
        xx_format_set_extension(self, "lib");
        xx_format_set_mime_type(self, "application/x-archive");
    } else {
        xx_format_set_extension(self, "a");
        xx_format_set_mime_type(self, "application/x-archive");
    }
    ar->number_of_members = (uint64_t)members.count;
    ar->number_of_records = members.visible_count;
    ar->archive_end = members.archive_end;
    ar->is_thin = false;
    self->format_size = members.archive_end - self->base_address;
    {
        /* Whatever follows the last member the chain reached is not part of
         * the archive: a damaged tail or an appended block, reported as
         * overlay rather than silently folded into the format size. */
        int64_t total = xx_io_total_size(self->device);
        bool has_tail = total > members.archive_end;
        self->overlay_offset = has_tail ? members.archive_end : -1;
        self->overlay_size = has_tail ? total - members.archive_end : 0;
    }
    self->number_of_archive_records = members.visible_count;
    self->file_type = XX_FILE_TYPE_AR;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_valid = true;
    self->base_info_handled = true;
    xx_ar_members_cleanup(&members);
    return true;
}

int64_t xx_ar_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_ar_get_number_of_archive_records(Abstractformat *self,
                                             xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return ((xx_ar *)self)->number_of_records;
}

xx_archive_record_state *xx_ar_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_ar_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_ar_archive_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    xx_mem_zero(stream, sizeof(*stream));
    if (!xx_ar_copy_options(&state->options, options) ||
        !xx_ar_parse_archive(self, &stream->members,
                             XX_AR_PARSE_MEMBERS | XX_AR_PARSE_EXTRACT_NAMES,
                             pd)) {
        xx_ar_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->member_index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_ar_archive_stream_free;
    state->total_records = (int64_t)stream->members.visible_count;
    while (stream->member_index < stream->members.count &&
           stream->members.items[stream->member_index].special) {
        ++stream->member_index;
    }
    if (stream->member_index < stream->members.count &&
        xx_ar_populate_archive_record(
            &state->current_record,
            &stream->members.items[stream->member_index])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_ar_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ar_archive_record_move_to_next(Abstractformat *self,
                                       xx_archive_record_state *state,
                                       xx_pd_struct *pd) {
    xx_ar_archive_stream *stream;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ar_archive_stream *)state->internal_state;
    ++stream->member_index;
    while (stream->member_index < stream->members.count &&
           stream->members.items[stream->member_index].special) {
        ++stream->member_index;
    }
    if (stream->member_index >= stream->members.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_ar_populate_archive_record(
            &state->current_record,
            &stream->members.items[stream->member_index])) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    ++state->current_index;
    return true;
}

bool xx_ar_unpack_current_archive_record(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_ar_archive_stream *stream;
    const xx_var *path_value;
    const char *name;
    const char *base_utf8 = NULL;
    char *owned_base = NULL;
    char *destination;
    bool result;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    record = &state->current_record;
    stream = (const xx_ar_archive_stream *)state->internal_state;
    if (stream->member_index >= stream->members.count) {
        return false;
    }
    /* The extraction path was made safe and unique when the member table
     * was built (see xx_ar_make_extract_name / xx_ar_make_names_unique);
     * NULL means the member name was refused. */
    name = stream->members.items[stream->member_index].extract_name;
    if (!name) {
        return false;
    }
    path_value = xx_ar_find_option(&state->options,
                                   XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        int64_t total = xx_io_total_size(self->device);
        return record->data_offset >= 0 && record->compressed_size >= 0 &&
               record->data_offset <= total &&
               record->compressed_size <= total - record->data_offset;
    }
    if (path_value->type == XX_VAR_TYPE_STRING ||
        path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base_utf8 = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING ||
               path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_utf8 = owned_base;
    }
    if (!base_utf8) {
        if (owned_base) xx_str_free(owned_base);
        return false;
    }
    if (base_utf8[0] != '\0' &&
        base_utf8[xx_str_len(base_utf8) - 1U] != '/' &&
        base_utf8[xx_str_len(base_utf8) - 1U] != '\\') {
        destination = xx_str_concat3(base_utf8, "/", name);
    } else {
        destination = xx_str_concat(base_utf8, name);
    }
    if (owned_base) {
        xx_str_free(owned_base);
    }
    if (!destination) {
        return false;
    }
    if (!xx_store_create_dirs_a(destination, false)) {
        xx_str_free(destination);
        return false;
    }
    /* The helper deletes its own output on failure, and only output it
     * created; removing destination here could delete a user's file. */
    result = xx_store_unpack_device_to_file(
        self->device, record->data_offset, record->compressed_size,
        destination, pd);
    xx_str_free(destination);
    return result;
}

void xx_ar_free_archive_records_reading(Abstractformat *self,
                                        xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

typedef struct xx_ar_ds_name_s {
    xx_ar_data_struct_id_t id;
    const char *name;
} xx_ar_ds_name;

static const xx_ar_ds_name xx_ar_ds_names[] = {
    {XX_AR_DS_UNKNOWN, "UNKNOWN"},
    {XX_AR_DS_GLOBAL_HEADER, "GLOBAL_HEADER"},
    {XX_AR_DS_MEMBER_HEADER, "MEMBER_HEADER"},
    {XX_AR_DS_BSD_EXTENDED_NAME, "BSD_EXTENDED_NAME"},
    {XX_AR_DS_DATA, "DATA"},
    {XX_AR_DS_PADDING, "PADDING"}
};

const char *xx_ar_data_struct_id_to_string(Abstractformat *self, uint32_t id) {
    (void)self;
    for (size_t i = 0; i < sizeof(xx_ar_ds_names) / sizeof(xx_ar_ds_names[0]);
         ++i) {
        if ((uint32_t)xx_ar_ds_names[i].id == id) {
            return xx_ar_ds_names[i].name;
        }
    }
    return "UNKNOWN";
}

uint32_t xx_ar_data_struct_string_to_id(Abstractformat *self,
                                        const char *name) {
    (void)self;
    if (name) {
        for (size_t i = 0;
             i < sizeof(xx_ar_ds_names) / sizeof(xx_ar_ds_names[0]); ++i) {
            if (xx_str_cmp(name, xx_ar_ds_names[i].name) == 0) {
                return (uint32_t)xx_ar_ds_names[i].id;
            }
        }
    }
    return (uint32_t)XX_AR_DS_UNKNOWN;
}

static void xx_ar_ds_stream_free(void *pointer) {
    xx_ar_ds_stream *stream = (xx_ar_ds_stream *)pointer;
    if (stream) {
        if (stream->items) xx_mem_free(stream->items);
        xx_mem_free(stream);
    }
}

static void xx_ar_set_ds(xx_data_struct *item, uint32_t id, int64_t offset,
                         int64_t size, xx_data_struct_type_t type,
                         bool is_mapped) {
    item->id = id;
    item->offset = offset;
    item->address = is_mapped ? offset : -1;
    item->entry_size = size;
    item->total_size = size;
    item->count = 1U;
    item->type = type;
}

xx_data_struct_state *xx_ar_create_data_structs_reading(Abstractformat *self,
                                                        xx_pd_struct *pd) {
    xx_data_struct_state *state;
    xx_ar_ds_stream *stream;
    xx_ar_members members;
    size_t capacity;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !xx_ar_parse_archive(self, &members, XX_AR_PARSE_MEMBERS, pd)) {
        return NULL;
    }
    if (members.count > (SIZE_MAX - 1U) / 4U) {
        xx_ar_members_cleanup(&members);
        return NULL;
    }
    capacity = 1U + members.count * 4U;
    if (capacity > SIZE_MAX / sizeof(*stream->items)) {
        xx_ar_members_cleanup(&members);
        return NULL;
    }
    state = (xx_data_struct_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_ar_ds_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        xx_ar_members_cleanup(&members);
        return NULL;
    }
    xx_data_struct_state_init(state, self);
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = (xx_data_struct *)xx_mem_alloc(
        capacity * sizeof(xx_data_struct));
    if (!stream->items) {
        xx_mem_free(stream);
        xx_data_struct_state_free(state);
        xx_ar_members_cleanup(&members);
        return NULL;
    }
    xx_ar_set_ds(&stream->items[stream->count++], XX_AR_DS_GLOBAL_HEADER,
                 self->base_address, XX_AR_MAGIC_SIZE,
                 XX_DATA_STRUCT_TYPE_STRUCT, self->is_mapped);
    for (size_t i = 0; i < members.count; ++i) {
        const xx_ar_member *member = &members.items[i];
        xx_ar_set_ds(&stream->items[stream->count++], XX_AR_DS_MEMBER_HEADER,
                     member->header_offset, XX_AR_MEMBER_HEADER_SIZE,
                     XX_DATA_STRUCT_TYPE_STRUCT, self->is_mapped);
        if (member->name_prefix_size != 0U) {
            xx_ar_set_ds(&stream->items[stream->count++],
                         XX_AR_DS_BSD_EXTENDED_NAME,
                         member->stored_data_offset,
                         (int64_t)member->name_prefix_size,
                         XX_DATA_STRUCT_TYPE_RAW_DATA, self->is_mapped);
        }
        if (member->data_size != 0) {
            xx_ar_set_ds(&stream->items[stream->count++], XX_AR_DS_DATA,
                         member->data_offset, member->data_size,
                         XX_DATA_STRUCT_TYPE_RAW_DATA, self->is_mapped);
        }
        if ((member->stored_size & 1) != 0) {
            xx_ar_set_ds(&stream->items[stream->count++], XX_AR_DS_PADDING,
                         member->stored_data_offset + member->stored_size, 1,
                         XX_DATA_STRUCT_TYPE_RAW_DATA, self->is_mapped);
        }
    }
    xx_ar_members_cleanup(&members);
    state->internal_state = stream;
    state->free_internal = xx_ar_ds_stream_free;
    state->total_structs = (int64_t)stream->count;
    state->current_index = 0;
    state->has_struct = stream->count != 0U;
    if (state->has_struct) {
        state->current_struct = stream->items[0];
    }
    return state;
}

const xx_data_struct *xx_ar_get_current_data_struct(Abstractformat *self,
                                                    xx_data_struct_state *state) {
    return self && state && state->format == self && state->has_struct
               ? &state->current_struct
               : NULL;
}

bool xx_ar_data_struct_move_to_next(Abstractformat *self,
                                    xx_data_struct_state *state,
                                    xx_pd_struct *pd) {
    xx_ar_ds_stream *stream;
    int64_t next;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_struct || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ar_ds_stream *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || (size_t)next >= stream->count) {
        state->has_struct = false;
        return false;
    }
    state->current_index = next;
    state->current_struct = stream->items[next];
    state->has_struct = true;
    return true;
}

void xx_ar_free_data_structs_reading(Abstractformat *self,
                                     xx_data_struct_state *state) {
    (void)self;
    xx_data_struct_state_free(state);
}

static const xx_data_struct_field_desc xx_ar_global_fields[] = {
    {L"magic", L"char[8]", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID}
};

static const xx_data_struct_field_desc xx_ar_member_fields[] = {
    {L"name", L"char[16]", 0, 16, XX_DATA_STRUCT_RECORD_PROPERTY_STRING},
    {L"timestamp", L"char[12]", 16, 12,
     XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP},
    {L"owner_id", L"char[6]", 28, 6, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"group_id", L"char[6]", 34, 6, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"mode", L"char[8]", 40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"size", L"char[10]", 48, 10, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"trailer", L"char[2]", 58, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID}
};

static void xx_ar_record_stream_free(void *pointer) {
    if (pointer) xx_mem_free(pointer);
}

static bool xx_ar_populate_field(xx_io_device *device,
                                 const xx_data_struct *parent,
                                 const xx_data_struct_field_desc *field,
                                 xx_data_struct_record *record) {
    char *text;
    wchar_t *display;
    size_t length;
    int64_t total_size;
    if (!device || !parent || !field || !record || field->size < 0 ||
        (uint64_t)field->size > (uint64_t)(SIZE_MAX - 1U)) {
        return false;
    }
    total_size = xx_io_total_size(device);
    if (parent->offset < 0 || field->rel_offset < 0 ||
        parent->offset > INT64_MAX - field->rel_offset ||
        parent->offset + field->rel_offset > total_size ||
        field->size > total_size - (parent->offset + field->rel_offset)) {
        return false;
    }
    length = (size_t)field->size;
    text = (char *)xx_mem_alloc(length + 1U);
    if (!text || !xx_ar_read_exact_at(device,
                                      parent->offset + field->rel_offset,
                                      text, length)) {
        if (text) xx_mem_free(text);
        return false;
    }
    while (length > 0U && (text[length - 1U] == ' ' ||
                           text[length - 1U] == '\0')) {
        --length;
    }
    text[length] = '\0';
    display = xx_str_utf8_to_unicode(text);
    if (!display) {
        xx_mem_free(text);
        return false;
    }
    xx_data_struct_record_init(record);
    record->offset = field->rel_offset;
    record->size = field->size;
    record->property = field->property;
    if (!xx_var_set_str(&record->value, text) ||
        !xx_data_struct_record_set_name(record, field->name) ||
        !xx_data_struct_record_set_type(record, field->type) ||
        !xx_data_struct_record_set_display_value(record, display)) {
        xx_str_wfree(display);
        xx_mem_free(text);
        xx_data_struct_record_cleanup(record);
        return false;
    }
    xx_str_wfree(display);
    xx_mem_free(text);
    return true;
}

xx_data_struct_record_state *xx_ar_create_data_struct_records_reading(
    Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd) {
    xx_data_struct_record_state *state;
    xx_ar_record_stream *stream;
    const xx_data_struct_field_desc *fields;
    size_t count;
    (void)pd;
    if (!self || !self->device || !ds) {
        return NULL;
    }
    if (ds->id == (uint32_t)XX_AR_DS_GLOBAL_HEADER) {
        fields = xx_ar_global_fields;
        count = sizeof(xx_ar_global_fields) / sizeof(xx_ar_global_fields[0]);
    } else if (ds->id == (uint32_t)XX_AR_DS_MEMBER_HEADER) {
        fields = xx_ar_member_fields;
        count = sizeof(xx_ar_member_fields) / sizeof(xx_ar_member_fields[0]);
    } else {
        return NULL;
    }
    state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_ar_record_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_data_struct_record_state_init(state, self, ds);
    stream->fields = fields;
    stream->count = count;
    state->internal_state = stream;
    state->free_internal = xx_ar_record_stream_free;
    state->total_records = (int64_t)count;
    if (count != 0U && xx_ar_populate_field(self->device, ds, &fields[0],
                                            &state->current_record)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_data_struct_record *xx_ar_get_current_data_struct_record(
    Abstractformat *self, xx_data_struct_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ar_data_struct_record_move_to_next(
    Abstractformat *self, xx_data_struct_record_state *state,
    xx_pd_struct *pd) {
    xx_ar_record_stream *stream;
    int64_t next;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ar_record_stream *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || (size_t)next >= stream->count) {
        state->has_record = false;
        return false;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    if (!xx_ar_populate_field(self->device, &state->parent_struct,
                              &stream->fields[next],
                              &state->current_record)) {
        state->has_record = false;
        return false;
    }
    state->current_index = next;
    state->has_record = true;
    return true;
}

void xx_ar_free_data_struct_records_reading(
    Abstractformat *self, xx_data_struct_record_state *state) {
    (void)self;
    xx_data_struct_record_state_free(state);
}

uint64_t xx_ar_get_number_of_records(const xx_ar *ar) {
    return ar ? ar->number_of_records : 0U;
}

uint64_t xx_ar_get_number_of_members(const xx_ar *ar) {
    return ar ? ar->number_of_members : 0U;
}

int64_t xx_ar_get_archive_end(const xx_ar *ar) {
    return ar ? ar->archive_end : -1;
}

bool xx_ar_is_thin(const xx_ar *ar) {
    return ar ? ar->is_thin : false;
}
