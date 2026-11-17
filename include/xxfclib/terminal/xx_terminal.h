/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_TERMINAL_H
#define XX_TERMINAL_H

#include "xxfclib/global/xx_global.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_terminal_stream_e {
    XX_TERMINAL_STDOUT = 0,
    XX_TERMINAL_STDERR
} xx_terminal_stream_t;

typedef struct xx_terminal_state_s {
    xx_terminal_stream_t stream;
    xx_terminal_type_t type;
    uint32_t original_mode;
    uint32_t current_mode;
    uint16_t original_attributes;
    bool valid;
    bool attributes_valid;
} xx_terminal_state;

/**
 * @brief Destination callback used by the structured-output helpers.
 *
 * The callback must consume all @p size bytes or return an error status.  It
 * may target a standard stream, a staging file, an in-memory buffer, or any
 * other byte sink.  Keeping the sink abstract lets the terminal module remain
 * usable by CRT-free builds.
 */
typedef xxfc_status_t (*xx_terminal_write_fn)(void *context,
                                               const char *data,
                                               size_t size);

/** @brief Sticky writer state shared by JSON and XML string emitters. */
typedef struct xx_terminal_writer_s {
    xx_terminal_write_fn write;
    void *context;
    xxfc_status_t status;
} xx_terminal_writer;

/** @brief Initialize a structured-output writer. */
XXFC_API void xx_terminal_writer_init(xx_terminal_writer *writer,
                                      xx_terminal_write_fn write,
                                      void *context);

/** @brief Return the first error reported by the writer or its callback. */
XXFC_API xxfc_status_t xx_terminal_writer_status(
    const xx_terminal_writer *writer);

/**
 * @brief Write bytes through the callback and preserve the first error.
 *
 * Once a write fails, later calls return the same error without invoking the
 * callback again.
 */
XXFC_API xxfc_status_t xx_terminal_writer_write(xx_terminal_writer *writer,
                                                const char *data,
                                                size_t size);

/**
 * @brief Write a NUL-terminated UTF-8 value as one complete JSON string.
 *
 * Quotes are included.  Valid shortest-form UTF-8 is preserved.  Invalid
 * input is replaced with \uFFFD and JSON control characters are escaped.
 * NULL is treated as an empty string.
 */
XXFC_API xxfc_status_t xx_terminal_writer_json_string(
    xx_terminal_writer *writer, const char *text);

/** @brief Length-delimited form of xx_terminal_writer_json_string(). */
XXFC_API xxfc_status_t xx_terminal_writer_json_string_n(
    xx_terminal_writer *writer, const char *text, size_t size);

/**
 * @brief Write NUL-terminated UTF-8 as XML 1.0 character data.
 *
 * Markup characters are escaped.  Invalid UTF-8, XML-forbidden characters,
 * and Unicode noncharacters are replaced with &#xFFFD;.  NULL is treated as an
 * empty string.
 */
XXFC_API xxfc_status_t xx_terminal_writer_xml_text(
    xx_terminal_writer *writer, const char *text);

/** @brief Length-delimited form of xx_terminal_writer_xml_text(). */
XXFC_API xxfc_status_t xx_terminal_writer_xml_text_n(
    xx_terminal_writer *writer, const char *text, size_t size);

/**
 * @brief Write NUL-terminated UTF-8 as an XML 1.0 attribute value.
 *
 * The surrounding quotes are not included.  In addition to XML markup,
 * quotation marks and attribute whitespace are escaped so parsing preserves
 * the original value.
 */
XXFC_API xxfc_status_t xx_terminal_writer_xml_attribute(
    xx_terminal_writer *writer, const char *text);

/** @brief Length-delimited form of xx_terminal_writer_xml_attribute(). */
XXFC_API xxfc_status_t xx_terminal_writer_xml_attribute_n(
    xx_terminal_writer *writer, const char *text, size_t size);

/* ANSI escape sequences written through a structured-output writer.
 *
 * These helpers emit raw control sequences into any byte sink (a standard
 * stream, a staging file, a buffer).  They neither detect a terminal nor
 * consult the global color setting: the caller decides when styling is
 * wanted, so it can reproduce another program's color decisions and bytes
 * exactly.  Each attribute is its own sequence ("\033[91m\033[1m", not
 * "\033[91;1m"), the way termcolor-style writers emit them. */

/** @brief The 16 classic ANSI foreground colors (their SGR parameters). */
typedef enum xx_terminal_ansi_color_e {
    XX_TERMINAL_ANSI_DEFAULT = 0, /**< no color sequence */
    XX_TERMINAL_ANSI_BLACK = 30,
    XX_TERMINAL_ANSI_RED = 31,
    XX_TERMINAL_ANSI_GREEN = 32,
    XX_TERMINAL_ANSI_YELLOW = 33,
    XX_TERMINAL_ANSI_BLUE = 34,
    XX_TERMINAL_ANSI_MAGENTA = 35,
    XX_TERMINAL_ANSI_CYAN = 36,
    XX_TERMINAL_ANSI_WHITE = 37,
    XX_TERMINAL_ANSI_BRIGHT_BLACK = 90,
    XX_TERMINAL_ANSI_BRIGHT_RED = 91,
    XX_TERMINAL_ANSI_BRIGHT_GREEN = 92,
    XX_TERMINAL_ANSI_BRIGHT_YELLOW = 93,
    XX_TERMINAL_ANSI_BRIGHT_BLUE = 94,
    XX_TERMINAL_ANSI_BRIGHT_MAGENTA = 95,
    XX_TERMINAL_ANSI_BRIGHT_CYAN = 96,
    XX_TERMINAL_ANSI_BRIGHT_WHITE = 97
} xx_terminal_ansi_color_t;

/** @brief xx_terminal_writer_ansi_style() flag: bold (increased intensity). */
#define XX_TERMINAL_ANSI_BOLD 0x1u

/**
 * @brief Write the sequences that select a foreground color and attributes.
 *
 * The color comes first ("\033[<color>m"), then one sequence per attribute
 * flag ("\033[1m" for XX_TERMINAL_ANSI_BOLD).  XX_TERMINAL_ANSI_DEFAULT
 * writes no color sequence.  An unknown color or flag returns
 * XXFC_ERR_INVALID_ARG and writes nothing.
 */
XXFC_API xxfc_status_t xx_terminal_writer_ansi_style(xx_terminal_writer *writer,
    xx_terminal_ansi_color_t foreground, uint32_t flags);

/** @brief Write the SGR reset sequence "\033[0m". */
XXFC_API xxfc_status_t xx_terminal_writer_ansi_reset(xx_terminal_writer *writer);

/**
 * @brief Write one styled span: the style, the text, then "\033[0m".
 *
 * The same bytes as xx_terminal_writer_ansi_style(), xx_terminal_writer_write()
 * and xx_terminal_writer_ansi_reset() in turn; the reset is written even
 * for an empty span.
 */
XXFC_API xxfc_status_t xx_terminal_writer_ansi_span(xx_terminal_writer *writer,
    const char *text, size_t size, xx_terminal_ansi_color_t foreground, uint32_t flags);

/**
 * @brief Open an OSC 8 hyperlink: "\033]8;;" URL "\033\\".
 *
 * The URL bytes are written as given.  Text written afterwards is the link
 * text; xx_terminal_writer_ansi_link_end() closes the link.  A NULL or empty
 * URL writes the closing sequence.
 */
XXFC_API xxfc_status_t xx_terminal_writer_ansi_link_begin(xx_terminal_writer *writer,
    const char *url, size_t size);

/** @brief Close an OSC 8 hyperlink: "\033]8;;\033\\". */
XXFC_API xxfc_status_t xx_terminal_writer_ansi_link_end(xx_terminal_writer *writer);

/** @brief Return to column 0 and erase the whole line: "\r\033[2K". */
XXFC_API xxfc_status_t xx_terminal_writer_ansi_clear_line(xx_terminal_writer *writer);

/**
 * @brief Apply one common command-line option to the process-global settings.
 * X86 builds recognize --sse2/--nosse2, --sse42/--nosse42,
 * --avx2/--noavx2, --aes/--noaes, --sha/--nosha and
 * --vpclmulqdq/--novpclmulqdq. Arm builds recognize --neon/--noneon,
 * --armcrc32/--noarmcrc32, --armcrypto/--noarmcrypto and
 * --armdotprod/--noarmdotprod, --armpmull/--noarmpmull. Every build recognizes --color/--nocolor.
 * Options for the other CPU architecture are unknown. Hyphenated --no-*
 * spellings are also accepted; the Arm options may be enabled as
 * --arm-crc32, --arm-crypto --arm-dotprod and --arm-pmull.
 * SSE4.2 may also be spelled
 * --sse4.2/--nosse4.2/--no-sse4.2.
 * CPU options are recognized but ignored if the CPU/OS lacks the feature.
 * Supported CPU features and color output are enabled by default; options do
 * not reset existing settings, so the last option for each setting wins.
 * Apply options before starting work. The caller handles --, application
 * options and positional arguments. This function performs no allocation.
 * @return true for a recognized option, false for NULL or an unknown option.
 */
XXFC_API bool xx_terminal_handle_option(const char *option);

/**
 * @brief Prepare a standard stream for terminal output.
 * Stdout uses the global terminal type; stderr is detected independently.
 * Redirected streams remain plain. Pair each init with finish, in reverse order.
 */
XXFC_API xx_terminal_state xx_terminal_init(xx_terminal_stream_t stream);
/** @brief Restore the console mode and attributes saved by init. */
XXFC_API void xx_terminal_finish(const xx_terminal_state *state);

/** @brief Write exactly size bytes, including embedded NULs. No formatting. */
XXFC_API xxfc_status_t xx_terminal_write(const xx_terminal_state *state, const char *text, size_t size);
/** @brief Write a NUL-terminated string without interpreting format specifiers. */
XXFC_API xxfc_status_t xx_terminal_print(const xx_terminal_state *state, const char *text);
/** @brief Write one colored span, then restore the terminal's colors.
 * Colors are 0xRRGGBB or UINT32_MAX for the terminal default. The 16-color
 * approximation and ANSI/native mappings follow XOptions::printConsole.
 * Redirected/unsupported streams and disabled global colors stay plain.
 * The caller must flush any separately buffered output before calling this.
 * Color restoration is attempted even if writing the span fails. */
XXFC_API xxfc_status_t xx_terminal_write_color(const xx_terminal_state *state,
    const char *text, size_t size, uint32_t foreground, uint32_t background);

/** @brief xx_terminal_write_color() flag: on ANSI terminals, write the colors
 * exactly instead of their 16-color approximation. A color equal to one of
 * the 16 palette colors uses its classic code (0xffff00 is "33", 0x00ff00 is
 * "32"), any other one a 24-bit "38;2;r;g;b" / "48;2;r;g;b" sequence, and a
 * default color adds no code. Native Windows consoles keep the nearest
 * console color. */
#define XX_TERMINAL_COLOR_EXACT 0x1u

/** @brief xx_terminal_write_color() with flags (XX_TERMINAL_COLOR_*). The
 * escape sequence, the text and the reset are composed into one write when
 * they fit in a small stack buffer, so a terminal never renders a span in
 * pieces. */
XXFC_API xxfc_status_t xx_terminal_write_color_ex(const xx_terminal_state *state,
    const char *text, size_t size, uint32_t foreground, uint32_t background, uint32_t flags);
XXFC_API xxfc_status_t xx_terminal_flush(const xx_terminal_state *state);

/** @brief Read/write raw native Windows attributes. Color mapping is the caller's responsibility. */
XXFC_API bool xx_terminal_get_attributes(const xx_terminal_state *state, uint16_t *attributes);
XXFC_API bool xx_terminal_set_attributes(const xx_terminal_state *state, uint16_t attributes);

#ifdef __cplusplus
}
#endif

#endif /* XX_TERMINAL_H */
