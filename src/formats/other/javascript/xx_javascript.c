/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native implementation of DIE's JavaScript minified/compiled heuristic.
 * It intentionally mirrors __MiniJavaScriptHeuristic_By_DosX.7.sg rather
 * than attempting to be a complete JavaScript lexer. In particular, comments
 * and regular-expression literals are treated as ordinary code, top-level
 * quotes split retained-code segments, and nested template expressions retain
 * the original rule's first-closing-brace behavior.
 */
#include "xxfclib/formats/other/javascript/xx_javascript.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#define XX_JAVASCRIPT_FALLBACK_CHUNK 4096U
#define XX_JAVASCRIPT_HISTORY_MASK UINT64_C(0x00FFFFFFFFFFFFFF)

typedef enum xx_javascript_lex_state {
    XX_JS_CODE = 0,
    XX_JS_TOP_SINGLE,
    XX_JS_TOP_DOUBLE,
    XX_JS_TOP_TEMPLATE,
    XX_JS_TOP_EXPRESSION,
    XX_JS_TOP_EXPRESSION_SINGLE,
    XX_JS_TOP_EXPRESSION_DOUBLE,
    XX_JS_NESTED_TEMPLATE,
    XX_JS_NESTED_EXPRESSION,
    XX_JS_NESTED_EXPRESSION_SINGLE,
    XX_JS_NESTED_EXPRESSION_DOUBLE
} xx_javascript_lex_state;

typedef struct xx_javascript_segment {
    uint64_t history;
    unsigned history_length;
    unsigned pattern_a_state;
    unsigned pattern_a_pairs;
    bool has_bytes;
    bool invalid_spacing;
    bool pattern_a_found;
    bool pattern_b_found;
} xx_javascript_segment;

typedef struct xx_javascript_scanner {
    xx_javascript_lex_state state;
    xx_javascript_segment segment;
    size_t top_brace_depth;
    size_t nested_template_depth;
    bool skip_next;
    bool template_dollar;
} xx_javascript_scanner;

static bool xx_javascript_ascii_alpha(uint8_t value)
{
    return (value >= (uint8_t)'A' && value <= (uint8_t)'Z') || (value >= (uint8_t)'a' && value <= (uint8_t)'z');
}

static bool xx_javascript_ascii_word(uint8_t value)
{
    return xx_javascript_ascii_alpha(value) || (value >= (uint8_t)'0' && value <= (uint8_t)'9') || value == (uint8_t)'_';
}

static uint8_t xx_javascript_history_byte(const xx_javascript_segment *segment, unsigned back)
{
    return (uint8_t)(segment->history >> (back * 8U));
}

static bool xx_javascript_history_is_keyword(const xx_javascript_segment *segment)
{
    if (segment->history_length >= 3U) {
        uint8_t a = xx_javascript_history_byte(segment, 2U);
        uint8_t b = xx_javascript_history_byte(segment, 1U);
        uint8_t c = xx_javascript_history_byte(segment, 0U);
        if ((a == (uint8_t)'v' && b == (uint8_t)'a' && c == (uint8_t)'r') || (a == (uint8_t)'l' && b == (uint8_t)'e' && c == (uint8_t)'t')) return true;
    }
    return segment->history_length >= 5U && xx_javascript_history_byte(segment, 4U) == (uint8_t)'c' && xx_javascript_history_byte(segment, 3U) == (uint8_t)'o' &&
           xx_javascript_history_byte(segment, 2U) == (uint8_t)'n' && xx_javascript_history_byte(segment, 1U) == (uint8_t)'s' &&
           xx_javascript_history_byte(segment, 0U) == (uint8_t)'t';
}

static void xx_javascript_history_push(xx_javascript_segment *segment, uint8_t value)
{
    segment->history = ((segment->history << 8U) | value) & XX_JAVASCRIPT_HISTORY_MASK;
    if (segment->history_length < 7U) ++segment->history_length;
}

static bool xx_javascript_pattern_b_ends_here(const xx_javascript_segment *segment)
{
    uint8_t end;
    uint8_t bit;
    if (segment->history_length < 5U) return false;
    end = xx_javascript_history_byte(segment, 0U);
    bit = xx_javascript_history_byte(segment, 1U);
    if ((end != (uint8_t)';' && end != (uint8_t)',' && end != (uint8_t)'}' && end != (uint8_t)')') || (bit != (uint8_t)'0' && bit != (uint8_t)'1') ||
        xx_javascript_history_byte(segment, 2U) != (uint8_t)'!')
        return false;

    if (xx_javascript_history_byte(segment, 3U) == (uint8_t)'=' && xx_javascript_ascii_alpha(xx_javascript_history_byte(segment, 4U))) return true;
    if (segment->history_length >= 6U && xx_javascript_history_byte(segment, 3U) == (uint8_t)'=' &&
        (xx_javascript_history_byte(segment, 4U) == (uint8_t)'!' || xx_javascript_history_byte(segment, 4U) == (uint8_t)'=') &&
        xx_javascript_ascii_alpha(xx_javascript_history_byte(segment, 5U)))
        return true;
    return segment->history_length >= 7U && xx_javascript_history_byte(segment, 3U) == (uint8_t)'=' && xx_javascript_history_byte(segment, 4U) == (uint8_t)'=' &&
           (xx_javascript_history_byte(segment, 5U) == (uint8_t)'!' || xx_javascript_history_byte(segment, 5U) == (uint8_t)'=') &&
           xx_javascript_ascii_alpha(xx_javascript_history_byte(segment, 6U));
}

static void xx_javascript_segment_feed(xx_javascript_segment *segment, uint8_t value)
{
    bool starts_pattern_a;
    segment->has_bytes = true;

    if (value == (uint8_t)'\t' || (value == (uint8_t)' ' && segment->history_length && xx_javascript_history_byte(segment, 0U) == (uint8_t)' ')) {
        segment->invalid_spacing = true;
        return;
    }
    if (segment->invalid_spacing) return;

    switch (segment->pattern_a_state) {
        case 1U: /* First one-letter identifier after the prefix. */
            if (xx_javascript_ascii_alpha(value)) {
                segment->pattern_a_state = 2U;
                segment->pattern_a_pairs = 0U;
            } else {
                segment->pattern_a_state = 0U;
            }
            break;
        case 2U: /* A letter was retained; a comma may continue the list. */
            if (value == (uint8_t)',') {
                if (segment->pattern_a_pairs >= 3U) segment->pattern_a_found = true;
                segment->pattern_a_state = 3U;
            } else {
                if (segment->pattern_a_pairs >= 3U && !xx_javascript_ascii_word(value)) segment->pattern_a_found = true;
                segment->pattern_a_state = 0U;
            }
            break;
        case 3U: /* Letter following a comma. */
            if (xx_javascript_ascii_alpha(value)) {
                ++segment->pattern_a_pairs;
                segment->pattern_a_state = 2U;
            } else {
                segment->pattern_a_state = 0U;
            }
            break;
        default: break;
    }

    starts_pattern_a = value == (uint8_t)'(' || ((value == (uint8_t)' ' || value == (uint8_t)'\t') && xx_javascript_history_is_keyword(segment));
    if (starts_pattern_a) {
        segment->pattern_a_state = 1U;
        segment->pattern_a_pairs = 0U;
    }

    xx_javascript_history_push(segment, value);
    if (xx_javascript_pattern_b_ends_here(segment)) segment->pattern_b_found = true;
}

static bool xx_javascript_segment_finish(xx_javascript_segment *segment)
{
    bool result;
    if (!segment->invalid_spacing && segment->pattern_a_state == 2U && segment->pattern_a_pairs >= 3U) segment->pattern_a_found = true;
    result = segment->has_bytes && !segment->invalid_spacing && (segment->pattern_a_found || segment->pattern_b_found);
    *segment = (xx_javascript_segment){0};
    return result;
}

/* Returns 1 for a completed matching segment, 0 to continue, and -1 for a
 * depth overflow. A pending '$' is kept explicitly so '${' may cross an I/O
 * window without peeking outside the borrowed view. */
static int xx_javascript_scan_byte(xx_javascript_scanner *scanner, uint8_t value)
{
    if (scanner->skip_next) {
        scanner->skip_next = false;
        return 0;
    }

    if (scanner->template_dollar) {
        scanner->template_dollar = false;
        if (value == (uint8_t)'{') {
            if (scanner->state == XX_JS_TOP_TEMPLATE) {
                scanner->top_brace_depth = 1U;
                scanner->state = XX_JS_TOP_EXPRESSION;
            } else {
                scanner->state = XX_JS_NESTED_EXPRESSION;
            }
            return 0;
        }
    }

    switch (scanner->state) {
        case XX_JS_CODE:
            if (value == (uint8_t)'\'' || value == (uint8_t)'"' || value == (uint8_t)'`') {
                bool result = xx_javascript_segment_finish(&scanner->segment);
                scanner->state = value == (uint8_t)'\'' ? XX_JS_TOP_SINGLE : (value == (uint8_t)'"' ? XX_JS_TOP_DOUBLE : XX_JS_TOP_TEMPLATE);
                return result ? 1 : 0;
            }
            xx_javascript_segment_feed(&scanner->segment, value);
            return 0;

        case XX_JS_TOP_SINGLE:
        case XX_JS_TOP_DOUBLE:
            if (value == (uint8_t)'\\') scanner->skip_next = true;
            else if ((scanner->state == XX_JS_TOP_SINGLE && value == (uint8_t)'\'') || (scanner->state == XX_JS_TOP_DOUBLE && value == (uint8_t)'"'))
                scanner->state = XX_JS_CODE;
            return 0;

        case XX_JS_TOP_TEMPLATE:
            if (value == (uint8_t)'\\') scanner->skip_next = true;
            else if (value == (uint8_t)'`') scanner->state = XX_JS_CODE;
            else if (value == (uint8_t)'$') scanner->template_dollar = true;
            return 0;

        case XX_JS_TOP_EXPRESSION:
            if (value == (uint8_t)'\\') {
                scanner->skip_next = true;
            } else if (value == (uint8_t)'\'') {
                scanner->state = XX_JS_TOP_EXPRESSION_SINGLE;
            } else if (value == (uint8_t)'"') {
                scanner->state = XX_JS_TOP_EXPRESSION_DOUBLE;
            } else if (value == (uint8_t)'`') {
                scanner->nested_template_depth = 1U;
                scanner->state = XX_JS_NESTED_TEMPLATE;
            } else if (value == (uint8_t)'{') {
                if (scanner->top_brace_depth == SIZE_MAX) return -1;
                ++scanner->top_brace_depth;
                xx_javascript_segment_feed(&scanner->segment, value);
            } else if (value == (uint8_t)'}') {
                if (scanner->top_brace_depth > 1U) {
                    --scanner->top_brace_depth;
                    xx_javascript_segment_feed(&scanner->segment, value);
                } else {
                    scanner->top_brace_depth = 0U;
                    scanner->state = XX_JS_TOP_TEMPLATE;
                }
            } else {
                xx_javascript_segment_feed(&scanner->segment, value);
            }
            return 0;

        case XX_JS_TOP_EXPRESSION_SINGLE:
        case XX_JS_TOP_EXPRESSION_DOUBLE:
            if (value == (uint8_t)'\\') scanner->skip_next = true;
            else if ((scanner->state == XX_JS_TOP_EXPRESSION_SINGLE && value == (uint8_t)'\'') ||
                     (scanner->state == XX_JS_TOP_EXPRESSION_DOUBLE && value == (uint8_t)'"'))
                scanner->state = XX_JS_TOP_EXPRESSION;
            return 0;

        case XX_JS_NESTED_TEMPLATE:
            if (value == (uint8_t)'\\') {
                scanner->skip_next = true;
            } else if (value == (uint8_t)'`') {
                if (!scanner->nested_template_depth) return -1;
                --scanner->nested_template_depth;
                scanner->state = scanner->nested_template_depth ? XX_JS_NESTED_EXPRESSION : XX_JS_TOP_EXPRESSION;
            } else if (value == (uint8_t)'$') {
                scanner->template_dollar = true;
            }
            return 0;

        case XX_JS_NESTED_EXPRESSION:
            if (value == (uint8_t)'\\') {
                scanner->skip_next = true;
            } else if (value == (uint8_t)'\'') {
                scanner->state = XX_JS_NESTED_EXPRESSION_SINGLE;
            } else if (value == (uint8_t)'"') {
                scanner->state = XX_JS_NESTED_EXPRESSION_DOUBLE;
            } else if (value == (uint8_t)'`') {
                if (scanner->nested_template_depth == SIZE_MAX) return -1;
                ++scanner->nested_template_depth;
                scanner->state = XX_JS_NESTED_TEMPLATE;
            } else if (value == (uint8_t)'}') {
                scanner->state = XX_JS_NESTED_TEMPLATE;
            }
            return 0;

        case XX_JS_NESTED_EXPRESSION_SINGLE:
        case XX_JS_NESTED_EXPRESSION_DOUBLE:
            if (value == (uint8_t)'\\') scanner->skip_next = true;
            else if ((scanner->state == XX_JS_NESTED_EXPRESSION_SINGLE && value == (uint8_t)'\'') ||
                     (scanner->state == XX_JS_NESTED_EXPRESSION_DOUBLE && value == (uint8_t)'"'))
                scanner->state = XX_JS_NESTED_EXPRESSION;
            return 0;
    }

    return -1;
}

bool xx_javascript_is_minified(xx_io_device *device, int64_t offset, int64_t size, xx_pd_struct *pd)
{
    xx_javascript_scanner scanner = {0};
    unsigned char fallback[XX_JAVASCRIPT_FALLBACK_CHUNK];
    int64_t total;
    int64_t done = 0;
    size_t capacity;
    bool buffered;

    if (!device || offset < 0 || (total = xx_io_total_size(device)) < 0 || offset >= total) return false;
    if (size < 0 || size > total - offset) size = total - offset;
    if (size <= 0 || (pd && xx_pd_is_stopped(pd))) return false;

    capacity = xx_io_buffer_capacity(device);
    buffered = capacity != 0U;
    if (!capacity) capacity = sizeof(fallback);

    while (done < size) {
        uint64_t remaining = (uint64_t)(size - done);
        size_t count = remaining < (uint64_t)capacity ? (size_t)remaining : capacity;
        const unsigned char *bytes;
        size_t i;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (buffered) {
            bytes = xx_io_buffer_view(device, offset + done, &count);
            if (!bytes || !count) return false;
        } else {
            if (!xx_io_read_at(device, offset + done, fallback, count)) return false;
            bytes = fallback;
        }

        for (i = 0; i < count; ++i) {
            int status;
            if (!bytes[i]) return xx_javascript_segment_finish(&scanner.segment);
            status = xx_javascript_scan_byte(&scanner, bytes[i]);
            if (status > 0) return true;
            if (status < 0) return false;
        }
        done += (int64_t)count;
    }

    return xx_javascript_segment_finish(&scanner.segment);
}
