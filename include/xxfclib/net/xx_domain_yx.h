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

#ifndef XXFCLIB_NET_XX_DOMAIN_YX_H
#define XXFCLIB_NET_XX_DOMAIN_YX_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* YARA-X-exact network helpers from the vt module.
 *
 * These reproduce the semantics of YARA-X 1.23.0's vt module
 * (lib/src/modules/vt/mod.rs and its typos/bitsquatting/homoglyphs/
 * interleaved engines):
 *
 * - xx_domain_yx_ip_in_range() parses the address like Rust std's
 *   IpAddr::from_str (core::net::parser; no leading zeros in IPv4 octets,
 *   `::` must stand for at least one group) and the network like ipnet
 *   2.12.2's IpNet::from_str (its parser is a copy of the older std parser,
 *   so IPv4 octets may have leading zeros there and `::` may stand for no
 *   group). Unparsable input yields false.
 * - xx_domain_yx_public_suffix_length() is the psl crate 2.1.243 lookup
 *   (snapshot documented in src/net/xx_psl_yx_data.h) over the name's
 *   labels, 0 when there is no suffix.
 * - xx_domain_yx_parse_domain() splits a name into prefix/subdomain/domain/
 *   tld at its public suffix, moving common host names ("www", "ftp", "m",
 *   "mail", "webmail", "ns1", "ns2") to the subdomain, as the reference
 *   parse_domain does.
 * - xx_domain_yx_permutation_of() is vt.net.EnrichedDomain.permutation_of:
 *   the typos (insertion/omission/replacement/doubling/swap/vowel-swap on a
 *   QWERTY neighbour table), homoglyph, bitsquatting, interleaved-subdomain
 *   and hyphenation checks over the domain labels; `kinds` is the
 *   vt.Domain.Permutation bit set (TYPO 1, HOMOGLYPH 2, HYPHENATION 4,
 *   SUBDOMAIN 8, BITSQUATTING 16; ALL is 0x1F).
 *
 * All pointers in the results refer to the caller's input text, which must
 * outlive the result. No normalization (lowercasing, punycode) is applied;
 * comparison is over Unicode scalar values exactly like the Rust iterators.
 */

/* The parts of a domain name split at its public suffix (parse_domain in
 * vt/mod.rs); absent parts have NULL pointers. */
typedef struct {
    const char *prefix;
    size_t prefix_length;
    const char *subdomain;
    size_t subdomain_length;
    const char *domain;
    size_t domain_length;
    const char *tld;
    size_t tld_length;
} xx_domain_yx_parts;

/* vt.net.EnrichedIP.in_range(cidr): `ip` is the struct's `raw` field,
 * parsed like Rust's IpAddr, and `cidr` is parsed like ipnet's IpNet.
 * Unparsable input yields false. */
XXFC_API bool xx_domain_yx_ip_in_range(const char *ip, size_t ip_length,
                                       const char *cidr, size_t cidr_length);

/* Length of psl::suffix(name), 0 for None. */
XXFC_API size_t xx_domain_yx_public_suffix_length(const char *name,
                                                  size_t length);

XXFC_API bool xx_domain_yx_parse_domain(const char *name, size_t length,
                                        xx_domain_yx_parts *parts);

/* vt.net.EnrichedDomain.permutation_of(target[, kinds]): `domain` is the
 * struct's `raw` field; `kinds` is a vt.Domain.Permutation bit set
 * (permutation_of(target) uses ALL = 0x1F). */
XXFC_API bool xx_domain_yx_permutation_of(const char *domain,
                                          size_t domain_length,
                                          const char *target,
                                          size_t target_length,
                                          int64_t kinds);

#ifdef __cplusplus
}
#endif

#endif
