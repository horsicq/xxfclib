/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef XXFCLIB_TOML_XX_TOML_H
#define XXFCLIB_TOML_XX_TOML_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TOML 1.0 document parsing.
 *
 * A document parser independent of any configuration schema: it reproduces,
 * byte for byte, the syntax diagnostics of the Rust `toml` 0.8 crate as used
 * by the YARA-X 1.23.0 CLI for its configuration files (error texts such as
 * "invalid table header", "duplicate key", "recursion limit exceeded", the
 * 80-key / 80-deep limits, and the toml_edit-style span reporting), so syntax
 * errors take precedence over schema type errors exactly like the reference.
 */

typedef enum xx_toml_kind {
    XX_TOML_TABLE, XX_TOML_ARRAY, XX_TOML_STRING, XX_TOML_INTEGER,
    XX_TOML_FLOAT, XX_TOML_BOOLEAN, XX_TOML_DATETIME
} xx_toml_kind;
typedef struct xx_toml_value xx_toml_value;
struct xx_toml_value {
    xx_toml_kind kind;
    char *key;
    size_t key_length;
    char *string;
    size_t string_length;
    int64_t integer;
    double floating;
    bool boolean;
    xx_toml_value **children;
    size_t count;
    size_t offset, length;
    bool implicit, dotted, sealed, table_array;
};
typedef struct xx_toml_error {
    size_t offset;
    size_t length;
    char *message;
} xx_toml_error;
/* Input remains owned by the caller. The returned tree owns all its strings.
 * Offsets refer to the original input, including a leading UTF-8 BOM. */
XXFC_API xx_toml_value *xx_toml_parse(const char *source, size_t length,
                                      xx_toml_error *error);
/* Release the diagnostic message after an unsuccessful parse. */
XXFC_API void xx_toml_error_clear(xx_toml_error *error);
XXFC_API void xx_toml_free(xx_toml_value *value);
XXFC_API const xx_toml_value *xx_toml_get(const xx_toml_value *table,
                                          const char *key);

#ifdef __cplusplus
}
#endif

#endif
