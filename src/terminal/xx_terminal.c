/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/terminal/xx_terminal.h"
#include "platforms/xx_terminal_platform.h"
#include "../global/platforms/xx_global_platform.h"

static size_t xx_terminal_text_size(const char *text)
{
    size_t size = 0U;
    if (!text) return 0U;
    while (text[size]) ++size;
    return size;
}

void xx_terminal_writer_init(xx_terminal_writer *writer,
                             xx_terminal_write_fn write,
                             void *context)
{
    if (!writer) return;
    writer->write = write;
    writer->context = context;
    writer->status = write ? XXFC_OK : XXFC_ERR_NULL_PARAM;
}

xxfc_status_t xx_terminal_writer_status(const xx_terminal_writer *writer)
{
    return writer ? writer->status : XXFC_ERR_NULL_PARAM;
}

xxfc_status_t xx_terminal_writer_write(xx_terminal_writer *writer,
                                       const char *data,
                                       size_t size)
{
    xxfc_status_t status;
    if (!writer) return XXFC_ERR_NULL_PARAM;
    if (writer->status != XXFC_OK) return writer->status;
    if (!writer->write) {
        writer->status = XXFC_ERR_NULL_PARAM;
        return writer->status;
    }
    if (!data && size) {
        writer->status = XXFC_ERR_NULL_PARAM;
        return writer->status;
    }
    if (!size) return XXFC_OK;
    status = writer->write(writer->context, data, size);
    if (status != XXFC_OK) writer->status = status;
    return writer->status;
}

/* Decode one shortest-form UTF-8 sequence.  Zero means invalid UTF-8. */
static size_t xx_terminal_utf8_decode(const unsigned char *data,
                                      size_t available,
                                      uint32_t *codepoint)
{
    unsigned char first;
    uint32_t value;
    size_t length;
    size_t index;

    if (!data || !available || !codepoint) return 0U;
    first = data[0];
    if (first < 0x80U) {
        *codepoint = first;
        return 1U;
    }
    if (first >= 0xc2U && first <= 0xdfU) {
        value = (uint32_t)(first & 0x1fU);
        length = 2U;
    } else if (first >= 0xe0U && first <= 0xefU) {
        value = (uint32_t)(first & 0x0fU);
        length = 3U;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        value = (uint32_t)(first & 0x07U);
        length = 4U;
    } else {
        return 0U;
    }
    if (length > available) return 0U;
    for (index = 1U; index < length; ++index) {
        if ((data[index] & 0xc0U) != 0x80U) return 0U;
        value = (value << 6U) | (uint32_t)(data[index] & 0x3fU);
    }
    if ((length == 2U && value < 0x80U) ||
        (length == 3U && value < 0x800U) ||
        (length == 4U && value < 0x10000U) ||
        (value >= 0xd800U && value <= 0xdfffU) || value > 0x10ffffU) {
        return 0U;
    }
    *codepoint = value;
    return length;
}

static bool xx_terminal_xml_codepoint_allowed(uint32_t codepoint)
{
    bool xml10 = codepoint == 0x09U || codepoint == 0x0aU ||
                 codepoint == 0x0dU ||
                 (codepoint >= 0x20U && codepoint <= 0xd7ffU) ||
                 (codepoint >= 0xe000U && codepoint <= 0xfffdU) ||
                 (codepoint >= 0x10000U && codepoint <= 0x10ffffU);
    return xml10 && !(codepoint >= 0xfdd0U && codepoint <= 0xfdefU) &&
           (codepoint & 0xffffU) != 0xfffeU &&
           (codepoint & 0xffffU) != 0xffffU;
}

xxfc_status_t xx_terminal_writer_json_string_n(xx_terminal_writer *writer,
                                                const char *text,
                                                size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *data = (const unsigned char *)text;
    size_t position = 0U;

    if (!writer || (!text && size)) return XXFC_ERR_NULL_PARAM;
    if (xx_terminal_writer_write(writer, "\"", 1U) != XXFC_OK)
        return xx_terminal_writer_status(writer);
    while (position < size && xx_terminal_writer_status(writer) == XXFC_OK) {
        uint32_t codepoint = 0U;
        size_t sequence = xx_terminal_utf8_decode(data + position,
                                                  size - position,
                                                  &codepoint);
        const char *escape = NULL;
        size_t escape_size = 0U;

        if (!sequence) {
            escape = "\\uFFFD";
            escape_size = 6U;
            sequence = 1U;
        } else if (sequence == 1U) {
            switch (data[position]) {
                case '"': escape = "\\\""; escape_size = 2U; break;
                case '\\': escape = "\\\\"; escape_size = 2U; break;
                case '\b': escape = "\\b"; escape_size = 2U; break;
                case '\f': escape = "\\f"; escape_size = 2U; break;
                case '\n': escape = "\\n"; escape_size = 2U; break;
                case '\r': escape = "\\r"; escape_size = 2U; break;
                case '\t': escape = "\\t"; escape_size = 2U; break;
                default:
                    if (data[position] < 0x20U) {
                        char control[6] = {'\\', 'u', '0', '0',
                                           hex[data[position] >> 4U],
                                           hex[data[position] & 0x0fU]};
                        (void)xx_terminal_writer_write(writer, control,
                                                       sizeof(control));
                        position += sequence;
                        continue;
                    }
                    break;
            }
        }
        if (escape) {
            (void)xx_terminal_writer_write(writer, escape, escape_size);
        } else {
            (void)xx_terminal_writer_write(writer,
                                           (const char *)(data + position),
                                           sequence);
        }
        position += sequence;
    }
    if (xx_terminal_writer_status(writer) == XXFC_OK)
        (void)xx_terminal_writer_write(writer, "\"", 1U);
    return xx_terminal_writer_status(writer);
}

xxfc_status_t xx_terminal_writer_json_string(xx_terminal_writer *writer,
                                              const char *text)
{
    return xx_terminal_writer_json_string_n(writer, text ? text : "",
                                             xx_terminal_text_size(text));
}

static xxfc_status_t xx_terminal_writer_xml_n(xx_terminal_writer *writer,
                                               const char *text,
                                               size_t size,
                                               bool attribute)
{
    const unsigned char *data = (const unsigned char *)text;
    size_t position = 0U;

    if (!writer || (!text && size)) return XXFC_ERR_NULL_PARAM;
    while (position < size && xx_terminal_writer_status(writer) == XXFC_OK) {
        uint32_t codepoint = 0U;
        size_t sequence = xx_terminal_utf8_decode(data + position,
                                                  size - position,
                                                  &codepoint);
        const char *escape = NULL;
        size_t escape_size = 0U;

        if (!sequence || !xx_terminal_xml_codepoint_allowed(codepoint)) {
            escape = "&#xFFFD;";
            escape_size = 8U;
            sequence = sequence ? sequence : 1U;
        } else if (sequence == 1U) {
            switch (data[position]) {
                case '&': escape = "&amp;"; escape_size = 5U; break;
                case '<': escape = "&lt;"; escape_size = 4U; break;
                case '>': escape = "&gt;"; escape_size = 4U; break;
                case '"':
                    if (attribute) {
                        escape = "&quot;";
                        escape_size = 6U;
                    }
                    break;
                case '\'':
                    if (attribute) {
                        escape = "&apos;";
                        escape_size = 6U;
                    }
                    break;
                case '\t':
                    if (attribute) {
                        escape = "&#x9;";
                        escape_size = 5U;
                    }
                    break;
                case '\n':
                    if (attribute) {
                        escape = "&#xA;";
                        escape_size = 5U;
                    }
                    break;
                case '\r':
                    if (attribute) {
                        escape = "&#xD;";
                        escape_size = 5U;
                    }
                    break;
                default: break;
            }
        }
        if (escape) {
            (void)xx_terminal_writer_write(writer, escape, escape_size);
        } else {
            (void)xx_terminal_writer_write(writer,
                                           (const char *)(data + position),
                                           sequence);
        }
        position += sequence;
    }
    return xx_terminal_writer_status(writer);
}

xxfc_status_t xx_terminal_writer_xml_text_n(xx_terminal_writer *writer,
                                             const char *text,
                                             size_t size)
{
    return xx_terminal_writer_xml_n(writer, text, size, false);
}

xxfc_status_t xx_terminal_writer_xml_text(xx_terminal_writer *writer,
                                           const char *text)
{
    return xx_terminal_writer_xml_text_n(writer, text ? text : "",
                                          xx_terminal_text_size(text));
}

xxfc_status_t xx_terminal_writer_xml_attribute_n(xx_terminal_writer *writer,
                                                  const char *text,
                                                  size_t size)
{
    return xx_terminal_writer_xml_n(writer, text, size, true);
}

xxfc_status_t xx_terminal_writer_xml_attribute(xx_terminal_writer *writer,
                                                const char *text)
{
    return xx_terminal_writer_xml_attribute_n(writer, text ? text : "",
                                               xx_terminal_text_size(text));
}

static bool xx_terminal_option_equals(const char *option, const char *expected)
{
    while (*option && *option == *expected) {
        ++option;
        ++expected;
    }
    return *option == *expected;
}

bool xx_terminal_handle_option(const char *option)
{
    if (!option) {
        return false;
    }
#if defined(_M_IX86) || (defined(_M_X64) && !defined(_M_ARM64EC)) || defined(__i386__) || \
    (defined(__x86_64__) && !defined(_M_ARM64EC))
    bool enable;
    if (xx_terminal_option_equals(option, "--sse2") || xx_terminal_option_equals(option, "--nosse2") || xx_terminal_option_equals(option, "--no-sse2")) {
        enable = xx_terminal_option_equals(option, "--sse2");
        if (xx_has_sse2()) {
            xx_set_sse2_enabled(enable);
        }
        return true;
    }
    if (xx_terminal_option_equals(option, "--sse42") || xx_terminal_option_equals(option, "--nosse42") || xx_terminal_option_equals(option, "--no-sse42") ||
        xx_terminal_option_equals(option, "--sse4.2") || xx_terminal_option_equals(option, "--nosse4.2") || xx_terminal_option_equals(option, "--no-sse4.2")) {
        enable = xx_terminal_option_equals(option, "--sse42") || xx_terminal_option_equals(option, "--sse4.2");
        if (xx_has_sse42()) {
            xx_set_sse42_enabled(enable);
        }
        return true;
    }
    if (xx_terminal_option_equals(option, "--avx2") || xx_terminal_option_equals(option, "--noavx2") || xx_terminal_option_equals(option, "--no-avx2")) {
        enable = xx_terminal_option_equals(option, "--avx2");
        if (xx_has_avx2()) {
            xx_set_avx2_enabled(enable);
        }
        return true;
    }
    if (xx_terminal_option_equals(option, "--aes") || xx_terminal_option_equals(option, "--noaes") || xx_terminal_option_equals(option, "--no-aes")) {
        enable = xx_terminal_option_equals(option, "--aes");
        if (xx_has_aes()) {
            xx_set_aes_enabled(enable);
        }
        return true;
    }
    if (xx_terminal_option_equals(option, "--sha") || xx_terminal_option_equals(option, "--nosha") || xx_terminal_option_equals(option, "--no-sha")) {
        enable = xx_terminal_option_equals(option, "--sha");
        if (xx_has_sha()) {
            xx_set_sha_enabled(enable);
        }
        return true;
    }
    if (xx_terminal_option_equals(option, "--vpclmulqdq") || xx_terminal_option_equals(option, "--novpclmulqdq") ||
        xx_terminal_option_equals(option, "--no-vpclmulqdq")) {
        enable = xx_terminal_option_equals(option, "--vpclmulqdq");
        if (xx_has_vpclmulqdq()) {
            xx_set_vpclmulqdq_enabled(enable);
        }
        return true;
    }
#elif defined(_M_ARM) || defined(_M_ARM64) || defined(_M_ARM64EC) || defined(__arm__) || defined(__aarch64__)
    bool enable;
    if (xx_terminal_option_equals(option, "--neon") || xx_terminal_option_equals(option, "--noneon") ||
        xx_terminal_option_equals(option, "--no-neon")) {
        enable = xx_terminal_option_equals(option, "--neon");
        if (xx_has_neon()) {
            xx_set_neon_enabled(enable);
        }
        return true;
    }
    if (xx_terminal_option_equals(option, "--armcrc32") || xx_terminal_option_equals(option, "--arm-crc32") ||
        xx_terminal_option_equals(option, "--noarmcrc32") || xx_terminal_option_equals(option, "--no-arm-crc32")) {
        enable = xx_terminal_option_equals(option, "--armcrc32") || xx_terminal_option_equals(option, "--arm-crc32");
        if (xx_has_arm_crc32()) {
            xx_set_arm_crc32_enabled(enable);
        }
        return true;
    }
    if (xx_terminal_option_equals(option, "--armcrypto") || xx_terminal_option_equals(option, "--arm-crypto") ||
        xx_terminal_option_equals(option, "--noarmcrypto") || xx_terminal_option_equals(option, "--no-arm-crypto")) {
        enable = xx_terminal_option_equals(option, "--armcrypto") || xx_terminal_option_equals(option, "--arm-crypto");
        if (xx_has_arm_crypto()) {
            xx_set_arm_crypto_enabled(enable);
        }
        return true;
    }
    if (xx_terminal_option_equals(option, "--armdotprod") || xx_terminal_option_equals(option, "--arm-dotprod") ||
        xx_terminal_option_equals(option, "--noarmdotprod") || xx_terminal_option_equals(option, "--no-arm-dotprod")) {
        enable = xx_terminal_option_equals(option, "--armdotprod") || xx_terminal_option_equals(option, "--arm-dotprod");
        if (xx_has_arm_dotprod()) {
            xx_set_arm_dotprod_enabled(enable);
        }
        return true;
    }
    if (xx_terminal_option_equals(option, "--armpmull") || xx_terminal_option_equals(option, "--arm-pmull") ||
        xx_terminal_option_equals(option, "--noarmpmull") || xx_terminal_option_equals(option, "--no-arm-pmull")) {
        enable = xx_terminal_option_equals(option, "--armpmull") || xx_terminal_option_equals(option, "--arm-pmull");
        if (xx_has_arm_pmull()) {
            xx_set_arm_pmull_enabled(enable);
        }
        return true;
    }
#endif
    if (xx_terminal_option_equals(option, "--color") || xx_terminal_option_equals(option, "--nocolor") || xx_terminal_option_equals(option, "--no-color")) {
        xx_set_color_output_enabled(xx_terminal_option_equals(option, "--color"));
        return true;
    }
    return false;
}

static bool xx_terminal_valid_stream(xx_terminal_stream_t stream)
{
    return stream == XX_TERMINAL_STDOUT || stream == XX_TERMINAL_STDERR;
}

xx_terminal_state xx_terminal_init(xx_terminal_stream_t stream)
{
    xx_terminal_state state = {0};
    xx_terminal_type_t type;

    state.stream = stream;
    if (!xx_terminal_valid_stream(stream)) {
        return state;
    }
    type = xx_get_terminal_type();
    if (stream == XX_TERMINAL_STDERR) {
        type = xx_global_platform_detect_terminal_type(true);
    }
    xx_terminal_platform_init(&state, type);
    return state;
}

void xx_terminal_finish(const xx_terminal_state *state)
{
    if (state && xx_terminal_valid_stream(state->stream)) {
        xx_terminal_platform_finish(state);
    }
}

xxfc_status_t xx_terminal_write(const xx_terminal_state *state, const char *text, size_t size)
{
    if (!state || (!text && size)) {
        return XXFC_ERR_NULL_PARAM;
    }
    if (!xx_terminal_valid_stream(state->stream)) {
        return XXFC_ERR_INVALID_ARG;
    }
    if (!size) {
        return XXFC_OK;
    }
    return xx_terminal_platform_write(state, text, size);
}

xxfc_status_t xx_terminal_print(const xx_terminal_state *state, const char *text)
{
    size_t size = 0;
    if (!text) {
        return XXFC_ERR_NULL_PARAM;
    }
    while (text[size]) {
        ++size;
    }
    return xx_terminal_write(state, text, size);
}

xxfc_status_t xx_terminal_flush(const xx_terminal_state *state)
{
    if (!state) {
        return XXFC_ERR_NULL_PARAM;
    }
    if (!xx_terminal_valid_stream(state->stream)) {
        return XXFC_ERR_INVALID_ARG;
    }
    return xx_terminal_platform_flush(state);
}

bool xx_terminal_get_attributes(const xx_terminal_state *state, uint16_t *attributes)
{
    return state && attributes && xx_terminal_valid_stream(state->stream) && xx_terminal_platform_get_attributes(state, attributes);
}

bool xx_terminal_set_attributes(const xx_terminal_state *state, uint16_t attributes)
{
    return state && xx_terminal_valid_stream(state->stream) && xx_terminal_platform_set_attributes(state, attributes);
}

/* ANSI escape sequences through a writer: the caller decides when to style. */
static bool xx_terminal_ansi_color_valid(xx_terminal_ansi_color_t color)
{
    unsigned int code = (unsigned int)color;
    return code == 0U || (code >= 30U && code <= 37U) || (code >= 90U && code <= 97U);
}

xxfc_status_t xx_terminal_writer_ansi_style(xx_terminal_writer *writer,
    xx_terminal_ansi_color_t foreground, uint32_t flags)
{
    char sequence[9];
    size_t length = 0U;
    unsigned int code = (unsigned int)foreground;
    if (!writer) return XXFC_ERR_NULL_PARAM;
    if (!xx_terminal_ansi_color_valid(foreground) || (flags & ~XX_TERMINAL_ANSI_BOLD))
        return XXFC_ERR_INVALID_ARG;
    if (code) {
        sequence[length++] = '\033';
        sequence[length++] = '[';
        sequence[length++] = (char)('0' + code / 10U);
        sequence[length++] = (char)('0' + code % 10U);
        sequence[length++] = 'm';
    }
    if (flags & XX_TERMINAL_ANSI_BOLD) {
        sequence[length++] = '\033';
        sequence[length++] = '[';
        sequence[length++] = '1';
        sequence[length++] = 'm';
    }
    return xx_terminal_writer_write(writer, sequence, length);
}

xxfc_status_t xx_terminal_writer_ansi_reset(xx_terminal_writer *writer)
{
    return xx_terminal_writer_write(writer, "\033[0m", 4U);
}

xxfc_status_t xx_terminal_writer_ansi_span(xx_terminal_writer *writer,
    const char *text, size_t size, xx_terminal_ansi_color_t foreground, uint32_t flags)
{
    xxfc_status_t status;
    if (!writer || (!text && size)) return XXFC_ERR_NULL_PARAM;
    status = xx_terminal_writer_ansi_style(writer, foreground, flags);
    if (status != XXFC_OK) return status;
    (void)xx_terminal_writer_write(writer, text, size);
    return xx_terminal_writer_ansi_reset(writer);
}

xxfc_status_t xx_terminal_writer_ansi_link_begin(xx_terminal_writer *writer,
    const char *url, size_t size)
{
    if (!writer) return XXFC_ERR_NULL_PARAM;
    (void)xx_terminal_writer_write(writer, "\033]8;;", 5U);
    if (url && size) (void)xx_terminal_writer_write(writer, url, size);
    return xx_terminal_writer_write(writer, "\033\\", 2U);
}

xxfc_status_t xx_terminal_writer_ansi_link_end(xx_terminal_writer *writer)
{
    return xx_terminal_writer_ansi_link_begin(writer, NULL, 0U);
}

xxfc_status_t xx_terminal_writer_ansi_clear_line(xx_terminal_writer *writer)
{
    return xx_terminal_writer_write(writer, "\r\033[2K", 5U);
}
