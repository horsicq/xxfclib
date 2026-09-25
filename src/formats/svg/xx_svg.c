/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SVG images.  Validation and size follow binwalk's src/signatures/svg.rs,
 * src/structures/svg.rs and src/extractors/svg.rs: from the root "<svg ",
 * every "<svg " / "</svg>" occurrence is a tag, one of them must carry the
 * SVG namespace, and the image ends at the "</svg>" that closes the root.
 * Before the root only an XML prolog is allowed.  The rules, and the two
 * places where this reader is stricter than binwalk, are in xx_svg.h.
 *
 * NOT an archive.  binwalk's extractor carves the image itself and declines
 * even that at offset 0, so there is nothing inside to publish as a record.
 *
 * Every read goes through a cursor that refuses bytes at or past a limit
 * (the device end, or base + XX_SVG_MAX_SIZE), and every loop advances by
 * at least one byte towards that limit, so a scan is linear in the image
 * size.  The tag walk needs two independent monotonic cursors: one looking
 * for the next "<" and one running ahead to the ">" that ends the current
 * tag, validating UTF-8 and looking for the head magic on the way.  Tags
 * nested inside one another's span reuse that one pass instead of
 * rescanning it, so "<svg <svg <svg ... >" costs one pass, not a square.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/svg/xx_svg.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_SVG exists in the enum. */
#ifdef SVG
#define XX_SVG_FILE_TYPE XX_FILE_TYPE_SVG
#else
#define XX_SVG_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Read granularity of each cursor.  Every peek is at most
 * XX_SVG_MAX_PEEK bytes, far below this. */
#define XX_SVG_WINDOW_SIZE 65536U
#define XX_SVG_MAX_PEEK 64U
/* How often (in scanned bytes) the stop flag is polled. */
#define XX_SVG_STOP_POLL_MASK 0xFFFFU

typedef struct xx_svg_cursor_s {
    xx_io_device *device;
    int64_t limit;        /* nothing at or past this is ever read */
    int64_t window_start; /* absolute offset of window[0] */
    size_t window_size;   /* valid bytes in window, 0 = empty */
    uint8_t *window;      /* XX_SVG_WINDOW_SIZE bytes */
} xx_svg_cursor;

typedef struct xx_svg_parsed_s {
    int64_t input_size;
    int64_t root;
    int64_t head;
    int64_t end;
    uint64_t svg_tags;
    uint32_t max_depth;
    uint32_t comments;
    uint32_t pis;
    bool bom;
    bool xml_declaration;
    bool doctype;
} xx_svg_parsed;

/* The look-ahead pass of the tag walk: covers [start, gt] where gt is the
 * first '>' at or after start. */
typedef struct xx_svg_span_s {
    int64_t start;    /* first byte of the pass, -1 before the first */
    int64_t gt;       /* the '>' that ended the pass, -1 none yet */
    int64_t last_err; /* start of the last invalid UTF-8 sequence, or -1 */
    int64_t last_head;/* start of the last head magic, or -1 */
} xx_svg_span;

static void xx_svg_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: long is 32-bit on Win64. */
static bool xx_svg_read_at(xx_io_device *device, int64_t offset, void *data,
                           size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;

    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_svg_cursor_open(xx_svg_cursor *cursor, xx_io_device *device,
                               int64_t limit) {
    xx_mem_zero(cursor, sizeof(*cursor));
    cursor->device = device;
    cursor->limit = limit;
    cursor->window_start = -1;
    cursor->window = (uint8_t *)xx_mem_alloc(XX_SVG_WINDOW_SIZE);
    return cursor->window != NULL;
}

static void xx_svg_cursor_close(xx_svg_cursor *cursor) {
    if (cursor->window) xx_mem_free(cursor->window);
    cursor->window = NULL;
    cursor->window_size = 0U;
}

/* Pointer to `size` (<= XX_SVG_MAX_PEEK) bytes at `offset`, or NULL when
 * any of them lies at or past the limit or cannot be read.  With `run`, the
 * number of window bytes available from `offset` is stored there too. */
static const uint8_t *xx_svg_peek(xx_svg_cursor *cursor, int64_t offset,
                                  size_t size, size_t *run) {
    if (!cursor->window || offset < 0 || size == 0U ||
        size > XX_SVG_MAX_PEEK || offset >= cursor->limit ||
        (int64_t)size > cursor->limit - offset) {
        return NULL;
    }
    if (cursor->window_size == 0U || offset < cursor->window_start ||
        offset - cursor->window_start >
            (int64_t)cursor->window_size - (int64_t)size) {
        int64_t remaining = cursor->limit - offset;
        size_t want = remaining < (int64_t)XX_SVG_WINDOW_SIZE
                          ? (size_t)remaining
                          : (size_t)XX_SVG_WINDOW_SIZE;
        cursor->window_size = 0U;
        if (!xx_svg_read_at(cursor->device, offset, cursor->window, want)) {
            return NULL;
        }
        cursor->window_start = offset;
        cursor->window_size = want;
    }
    if (run) {
        *run = cursor->window_size - (size_t)(offset - cursor->window_start);
    }
    return cursor->window + (size_t)(offset - cursor->window_start);
}

static bool xx_svg_match(xx_svg_cursor *cursor, int64_t offset,
                         const char *text, size_t size) {
    const uint8_t *p = xx_svg_peek(cursor, offset, size, NULL);
    return p != NULL && xx_rt_memcmp(p, text, size) == 0;
}

static bool xx_svg_is_space(uint8_t c) {
    return c == 0x20U || c == 0x09U || c == 0x0DU || c == 0x0AU;
}

static bool xx_svg_poll(uint64_t *counter, xx_pd_struct *pd) {
    if ((++*counter & XX_SVG_STOP_POLL_MASK) == 0U && pd &&
        xx_pd_is_stopped(pd)) {
        return false;
    }
    return true;
}

/* First occurrence of `text` (no repeated first byte needed) starting in
 * [from, stop); on success *found is its offset.  Linear: each position is
 * compared once, with at most `size` bytes. */
static bool xx_svg_find(xx_svg_cursor *cursor, int64_t from, int64_t stop,
                        const char *text, size_t size, int64_t *found,
                        uint64_t *counter, xx_pd_struct *pd) {
    int64_t pos = from;

    while (pos < stop) {
        size_t run = 0U;
        size_t i;
        const uint8_t *p = xx_svg_peek(cursor, pos, 1U, &run);
        if (!p) return false;
        if ((int64_t)run > stop - pos) run = (size_t)(stop - pos);
        for (i = 0U; i < run; ++i) {
            if (p[i] == (uint8_t)text[0]) break;
        }
        if (!xx_svg_poll(counter, pd)) return false;
        if (i == run) {
            pos += (int64_t)run;
            continue;
        }
        pos += (int64_t)i;
        if ((int64_t)size > stop - pos) return false;
        if (xx_svg_match(cursor, pos, text, size)) {
            *found = pos;
            return true;
        }
        ++pos;
    }
    return false;
}

/* ------------------------------------------------------------- prolog -- */

/* <!DOCTYPE at `pos`.  The name must be "svg"; quoted literals and an
 * internal subset (with its own comments and PIs) are skipped so a '>' or
 * ']' inside them does not end the declaration.  *next is one past '>'. */
static bool xx_svg_skip_doctype(xx_svg_cursor *cursor, int64_t pos,
                                int64_t stop, int64_t *next,
                                uint64_t *counter, xx_pd_struct *pd) {
    const uint8_t *p;
    uint8_t quote = 0U;
    bool subset = false;

    pos += 9; /* "<!DOCTYPE" */
    p = xx_svg_peek(cursor, pos, 1U, NULL);
    if (pos >= stop || !p || !xx_svg_is_space(*p)) return false;
    while (pos < stop) {
        p = xx_svg_peek(cursor, pos, 1U, NULL);
        if (!p) return false;
        if (!xx_svg_is_space(*p)) break;
        ++pos;
    }
    if (stop - pos < 4 || !xx_svg_match(cursor, pos, "svg", 3U)) return false;
    pos += 3;
    p = xx_svg_peek(cursor, pos, 1U, NULL);
    if (!p || !(xx_svg_is_space(*p) || *p == '[' || *p == '>')) return false;

    while (pos < stop) {
        uint8_t c;
        p = xx_svg_peek(cursor, pos, 1U, NULL);
        if (!p || !xx_svg_poll(counter, pd)) return false;
        c = *p;
        if (quote != 0U) {
            if (c == quote) quote = 0U;
            ++pos;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            ++pos;
            continue;
        }
        if (subset) {
            int64_t end;
            if (c == '<' && xx_svg_match(cursor, pos, "<!--", 4U)) {
                if (!xx_svg_find(cursor, pos + 4, stop, "-->", 3U, &end,
                                 counter, pd)) {
                    return false;
                }
                pos = end + 3;
                continue;
            }
            if (c == '<' && xx_svg_match(cursor, pos, "<?", 2U)) {
                if (!xx_svg_find(cursor, pos + 2, stop, "?>", 2U, &end,
                                 counter, pd)) {
                    return false;
                }
                pos = end + 2;
                continue;
            }
            if (c == ']') subset = false;
            ++pos;
            continue;
        }
        if (c == '[') {
            subset = true;
        } else if (c == '>') {
            *next = pos + 1;
            return true;
        }
        ++pos;
    }
    return false;
}

/* Walks the prolog from the base address; on success parsed->root is the
 * offset of the root's "<svg ". */
static bool xx_svg_parse_prolog(xx_svg_cursor *cursor, int64_t base,
                                xx_svg_parsed *parsed, uint64_t *counter,
                                xx_pd_struct *pd) {
    int64_t stop = cursor->limit;
    int64_t pos = base;

    if (stop - base > (int64_t)XX_SVG_MAX_PROLOG_SIZE) {
        stop = base + (int64_t)XX_SVG_MAX_PROLOG_SIZE;
    }
    if (xx_svg_match(cursor, pos, "\xEF\xBB\xBF", 3U)) {
        parsed->bom = true;
        pos += 3;
    }
    while (pos < stop) {
        const uint8_t *p = xx_svg_peek(cursor, pos, 1U, NULL);
        int64_t end;
        if (!p || !xx_svg_poll(counter, pd)) return false;
        if (xx_svg_is_space(*p)) {
            ++pos;
            continue;
        }
        if (*p != '<') return false;
        if (xx_svg_match(cursor, pos, XX_SVG_OPEN_TAG,
                         XX_SVG_OPEN_TAG_SIZE)) {
            parsed->root = pos;
            return true;
        }
        if (xx_svg_match(cursor, pos, "<?", 2U)) {
            if (xx_svg_match(cursor, pos, "<?xml", 5U)) {
                const uint8_t *q = xx_svg_peek(cursor, pos + 5, 1U, NULL);
                if (q && xx_svg_is_space(*q)) parsed->xml_declaration = true;
            }
            if (!xx_svg_find(cursor, pos + 2, stop, "?>", 2U, &end, counter,
                             pd)) {
                return false;
            }
            ++parsed->pis;
            pos = end + 2;
            continue;
        }
        if (xx_svg_match(cursor, pos, "<!--", 4U)) {
            if (!xx_svg_find(cursor, pos + 4, stop, "-->", 3U, &end, counter,
                             pd)) {
                return false;
            }
            ++parsed->comments;
            pos = end + 3;
            continue;
        }
        if (xx_svg_match(cursor, pos, "<!DOCTYPE", 9U)) {
            if (parsed->doctype ||
                !xx_svg_skip_doctype(cursor, pos, stop, &end, counter, pd)) {
                return false;
            }
            parsed->doctype = true;
            pos = end;
            continue;
        }
        return false;
    }
    return false;
}

/* ------------------------------------------------------------ tag walk -- */

/* Length of the UTF-8 sequence led by `c` and the allowed range of its
 * second byte (RFC 3629, as Rust's str::from_utf8 applies it); 0 = `c`
 * cannot lead a sequence. */
static uint32_t xx_svg_utf8_lead(uint8_t c, uint8_t *lo, uint8_t *hi) {
    *lo = 0x80U;
    *hi = 0xBFU;
    if (c >= 0xC2U && c <= 0xDFU) return 2U;
    if (c == 0xE0U) { *lo = 0xA0U; return 3U; }
    if ((c >= 0xE1U && c <= 0xECU) || c == 0xEEU || c == 0xEFU) return 3U;
    if (c == 0xEDU) { *hi = 0x9FU; return 3U; }
    if (c == 0xF0U) { *lo = 0x90U; return 4U; }
    if (c >= 0xF1U && c <= 0xF3U) return 4U;
    if (c == 0xF4U) { *hi = 0x8FU; return 4U; }
    return 0U;
}

/* Runs the look-ahead pass from `start` to the first '>' at or after it,
 * recording the last UTF-8 error and the last head magic on the way.  The
 * pass restarts at `start`, which is always a '<', i.e. an ASCII byte and
 * so a character boundary.  False when no '>' lies before the limit. */
static bool xx_svg_span_run(xx_svg_cursor *cursor, xx_svg_span *span,
                            int64_t start, uint64_t *counter,
                            xx_pd_struct *pd) {
    int64_t pos = start;

    span->start = start;
    span->gt = -1;
    span->last_err = -1;
    span->last_head = -1;
    while (pos < cursor->limit) {
        size_t run = 0U;
        size_t i = 0U;
        const uint8_t *p = xx_svg_peek(cursor, pos, 1U, &run);
        if (!p || !xx_svg_poll(counter, pd)) return false;
        /* Plain ASCII other than '>' and 'x' needs no attention. */
        while (i < run && p[i] < 0x80U && p[i] != '>' && p[i] != 'x') ++i;
        pos += (int64_t)i;
        if (i == run) continue;
        p += i;
        if (*p == '>') {
            span->gt = pos;
            return true;
        }
        if (*p == 'x') {
            /* Cheap reject on the next byte when it is already at hand. */
            if ((run - i < 2U || p[1] == 'm') &&
                xx_svg_match(cursor, pos, XX_SVG_HEAD_MAGIC,
                             XX_SVG_HEAD_MAGIC_SIZE)) {
                span->last_head = pos;
            }
            ++pos;
            continue;
        }
        {
            uint8_t lo;
            uint8_t hi;
            uint32_t length = xx_svg_utf8_lead(*p, &lo, &hi);
            const uint8_t *q = length != 0U
                                   ? xx_svg_peek(cursor, pos, length, NULL)
                                   : NULL;
            bool ok = q != NULL && q[1] >= lo && q[1] <= hi;
            uint32_t k;
            for (k = 2U; ok && k < length; ++k) {
                ok = q[k] >= 0x80U && q[k] <= 0xBFU;
            }
            if (ok) {
                pos += (int64_t)length;
            } else {
                /* Resume at the next byte: an ASCII byte is never a
                 * continuation, so a later '<' or '>' is still seen. */
                span->last_err = pos;
                ++pos;
            }
        }
    }
    return false;
}

/* binwalk parse_svg_image from the root. */
static bool xx_svg_walk(xx_svg_cursor *scan, xx_svg_cursor *ahead,
                        xx_svg_parsed *parsed, uint64_t *counter,
                        xx_pd_struct *pd) {
    xx_svg_span span;
    int64_t pos = parsed->root;
    uint64_t heads = 0U;
    uint32_t depth = 0U;

    span.start = -1;
    span.gt = -1;
    span.last_err = -1;
    span.last_head = -1;

    while (pos < scan->limit) {
        size_t run = 0U;
        size_t i = 0U;
        bool is_open;
        bool is_close;
        bool is_head;
        const uint8_t *p = xx_svg_peek(scan, pos, 1U, &run);
        if (!p || !xx_svg_poll(counter, pd)) return false;
        while (i < run && p[i] != '<') ++i;
        pos += (int64_t)i;
        if (i == run) continue;
        /* Cheap reject on the next byte when it is already at hand. */
        if (run - i >= 2U && p[i + 1] != 's' && p[i + 1] != '/') {
            ++pos;
            continue;
        }

        is_open = xx_svg_match(scan, pos, XX_SVG_OPEN_TAG,
                               XX_SVG_OPEN_TAG_SIZE);
        is_close = !is_open && xx_svg_match(scan, pos, XX_SVG_CLOSE_TAG,
                                            XX_SVG_CLOSE_TAG_SIZE);
        if (!is_open && !is_close) {
            ++pos;
            continue;
        }

        /* The tag is [pos, gt].  A '>' found for an earlier tag at or after
         * pos is also the first one after pos: there is none in between. */
        if (span.gt < pos &&
            !xx_svg_span_run(ahead, &span, pos, counter, pd)) {
            return false; /* no '>' at all: binwalk's walk fails */
        }
        if (span.last_err >= pos) return false; /* not valid UTF-8 */
        is_head = span.last_head >= pos;

        if (is_head) {
            if (++heads == 1U) parsed->head = pos;
        }
        if (is_open) {
            ++parsed->svg_tags;
            if (depth == UINT32_MAX) return false;
            ++depth;
            if (depth > parsed->max_depth) parsed->max_depth = depth;
        }
        if (is_close) {
            /* depth is never 0 here: the walk stops when it gets there. */
            --depth;
        }
        if (heads > 1U) return false;
        if (depth == 0U) {
            /* The root element is closed.  binwalk ends here only with a
             * head tag; without one it would scan on (see xx_svg.h). */
            if (heads != 1U) return false;
            parsed->end = pos + (int64_t)XX_SVG_CLOSE_TAG_SIZE;
            return true;
        }
        ++pos;
    }
    return false;
}

/* --------------------------------------------------------------- parse -- */

static bool xx_svg_parse(Abstractformat *self, xx_svg_parsed *parsed,
                         xx_pd_struct *pd) {
    xx_svg_cursor scan;
    xx_svg_cursor ahead;
    int64_t limit;
    uint64_t counter = 0U;
    bool ok = false;

    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->root = -1;
    parsed->head = -1;
    parsed->end = -1;
    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (parsed->input_size < self->base_address ||
        parsed->input_size - self->base_address < (int64_t)XX_SVG_MIN_SIZE) {
        return false;
    }
    limit = parsed->input_size;
    if (limit - self->base_address > (int64_t)XX_SVG_MAX_SIZE) {
        limit = self->base_address + (int64_t)XX_SVG_MAX_SIZE;
    }
    if (!xx_svg_cursor_open(&scan, self->device, limit)) return false;
    if (!xx_svg_cursor_open(&ahead, self->device, limit)) {
        xx_svg_cursor_close(&scan);
        return false;
    }
    ok = xx_svg_parse_prolog(&scan, self->base_address, parsed, &counter,
                             pd) &&
         xx_svg_walk(&scan, &ahead, parsed, &counter, pd);
    xx_svg_cursor_close(&ahead);
    xx_svg_cursor_close(&scan);
    return ok && parsed->end > self->base_address && parsed->end <= limit;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_svg_init(xx_svg *svg, xx_io_device *dev, int64_t base_address) {
    if (!svg) return;
    xx_mem_zero(svg, sizeof(*svg));
    xx_format_init(&svg->format, dev, base_address);
    svg->format.file_type = XX_SVG_FILE_TYPE;
    /* The format-type enum has no image kind; an SVG is not an archive,
     * executable, firmware or package, so it stays UNKNOWN. */
    svg->format.format_type = XX_TYPE_UNKNOWN;
    svg->format.is_archive = false;
    xx_format_set_mime_type(&svg->format, "image/svg+xml");
    xx_format_set_extension(&svg->format, "svg");
    svg->format.check_is_valid = xx_svg_check_is_valid;
    svg->format.handle_base_info = xx_svg_handle_base_info;
    svg->format.get_format_size = xx_svg_get_format_size;
    svg->format.destroy = xx_svg_vtable_destroy;
    svg->root_offset = -1;
    svg->head_offset = -1;
    svg->end_offset = -1;
}

xx_svg *xx_svg_create(xx_io_device *dev, int64_t base_address) {
    xx_svg *svg = (xx_svg *)xx_mem_alloc(sizeof(*svg));

    if (svg) xx_svg_init(svg, dev, base_address);
    return svg;
}

void xx_svg_destroy(xx_svg *svg) {
    if (!svg) return;
    xx_format_cleanup_extra_parameters(&svg->format);
}

static void xx_svg_vtable_destroy(Abstractformat *self) {
    xx_svg_destroy((xx_svg *)self);
}

void xx_svg_free(xx_svg *svg) {
    if (!svg) return;
    xx_svg_destroy(svg);
    xx_mem_free(svg);
}

/* -------------------------------------------------------------- format -- */

/* The prefilter is exactly the first step of xx_svg_parse_prolog, so it is
 * a necessary condition of the probe: whatever it refuses, the probe would
 * refuse too.  When the detector's window is full and ends before the
 * first token is complete (a long run of white space), it cannot decide
 * and lets the probe do so. */
bool xx_svg_check_magic(const uint8_t *magic, size_t magic_size) {
    static const char *const tokens[] = {
        XX_SVG_OPEN_TAG, "<?", "<!--", "<!DOCTYPE"
    };
    static const size_t lengths[] = { XX_SVG_OPEN_TAG_SIZE, 2U, 4U, 9U };
    bool window_full = magic_size >= XX_SVG_MAGIC_WINDOW;
    size_t i = 0U;
    size_t rest;
    size_t k;

    if (!magic || magic_size == 0U) return false;
    if (magic_size >= 3U && magic[0] == 0xEFU && magic[1] == 0xBBU &&
        magic[2] == 0xBFU) {
        i = 3U;
    }
    while (i < magic_size && xx_svg_is_space(magic[i])) ++i;
    rest = magic_size - i;
    if (rest == 0U) return window_full;
    for (k = 0U; k < sizeof(tokens) / sizeof(tokens[0]); ++k) {
        size_t length = lengths[k];
        size_t n = rest < length ? rest : length;
        if (xx_rt_memcmp(magic + i, tokens[k], n) == 0 &&
            (n == length || window_full)) {
            return true;
        }
    }
    return false;
}

bool xx_svg_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_svg_parsed parsed;

    return xx_svg_parse(self, &parsed, pd);
}

bool xx_svg_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_svg *svg = (xx_svg *)self;
    xx_svg_parsed parsed;

    if (!self) return false;
    if (!xx_svg_parse(self, &parsed, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    svg->root_offset = parsed.root;
    svg->head_offset = parsed.head;
    svg->end_offset = parsed.end;
    svg->number_of_svg_tags = parsed.svg_tags;
    svg->max_depth = parsed.max_depth;
    svg->number_of_prolog_comments = parsed.comments;
    svg->number_of_prolog_pis = parsed.pis;
    svg->has_bom = parsed.bom;
    svg->has_xml_declaration = parsed.xml_declaration;
    svg->has_doctype = parsed.doctype;
    self->format_size = parsed.end - self->base_address;
    if (parsed.input_size > parsed.end) {
        self->overlay_offset = parsed.end;
        self->overlay_size = parsed.input_size - parsed.end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 0U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_svg_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

/* ------------------------------------------------------------ accessors -- */

int64_t xx_svg_get_root_offset(const xx_svg *svg) {
    return svg ? svg->root_offset : -1;
}

int64_t xx_svg_get_head_offset(const xx_svg *svg) {
    return svg ? svg->head_offset : -1;
}

int64_t xx_svg_get_end_offset(const xx_svg *svg) {
    return svg ? svg->end_offset : -1;
}

uint64_t xx_svg_get_number_of_svg_tags(const xx_svg *svg) {
    return svg ? svg->number_of_svg_tags : 0U;
}

uint32_t xx_svg_get_max_depth(const xx_svg *svg) {
    return svg ? svg->max_depth : 0U;
}

bool xx_svg_has_xml_declaration(const xx_svg *svg) {
    return svg ? svg->has_xml_declaration : false;
}

bool xx_svg_has_doctype(const xx_svg *svg) {
    return svg ? svg->has_doctype : false;
}
