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

#ifndef XXFCLIB_FORMATS_OTHER_CUCKOO_XX_CUCKOO_YX_H
#define XXFCLIB_FORMATS_OTHER_CUCKOO_XX_CUCKOO_YX_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* YARA-X-exact Cuckoo report reader.
 *
 * Cuckoo sandbox reports are scanner module data, not the bytes of the
 * scanned file. This reader reproduces, case for case, the serde_json-based
 * decoding of YARA-X 1.23.0's cuckoo module (lib/src/modules/cuckoo/):
 * the network.domains / network.dns (hostname) / network.hosts /
 * behavior.summary.{mutexes,files,keys} string lists, the http requests
 * (method, uri, user-agent), the tcp/udp connections (dst, dst_domain,
 * dport), the duplicate-key "last value wins" pass through
 * serde_json::Value, the sequence-in-declaration-order representation of the
 * derived structs, and an absent or empty report is valid and yields zero
 * for every function. Invalid JSON or wrong field types fail; the
 * serde-compatible diagnostic texts are produced by the parse_ex error
 * buffer (see xx_cuckoo_yx_errors.inc).
 *
 * The result borrows nothing from the input; release it with
 * xx_cuckoo_yx_report_free. The regex callback of xx_cuckoo_yx_query
 * receives decoded UTF-8 bytes.
 */

typedef struct xx_cuckoo_yx_report xx_cuckoo_yx_report;

typedef bool (*xx_cuckoo_yx_match_fn)(const uint8_t *bytes, size_t length,
                                      void *opaque);

XXFC_API bool xx_cuckoo_yx_report_parse(const uint8_t *json, size_t size,
                                        xx_cuckoo_yx_report **out);
XXFC_API bool xx_cuckoo_yx_report_parse_ex(const uint8_t *json, size_t size,
                                           xx_cuckoo_yx_report **out,
                                           char *error,
                                           size_t error_capacity);
XXFC_API void xx_cuckoo_yx_report_free(xx_cuckoo_yx_report *report);

/* `function` is one of the exported names after `cuckoo.`: network.dns_lookup,
 * network.host, sync.mutex, filesystem.file_access, registry.key_access,
 * network.http_request, network.http_get, network.http_post,
 * network.http_user_agent, network.tcp, network.udp. TCP and UDP require a
 * port argument; other functions require has_port=false. Counts follow
 * YARA-X's integer-valued exports, including zero when a field is absent. */
XXFC_API bool xx_cuckoo_yx_query(const xx_cuckoo_yx_report *report,
                                 const char *function,
                                 xx_cuckoo_yx_match_fn matches, void *opaque,
                                 bool has_port, int64_t port,
                                 uint64_t *count);

#ifdef __cplusplus
}
#endif

#endif
