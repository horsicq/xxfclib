/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* YARA-X-exact MSI helpers.
 *
 * The MSI Authenticode hash input (MsiHasher, YARA-X
 * lib/src/modules/msi/parser.rs): the header sector with the CLSID and state
 * bits (bytes 8..28) zeroed, then the contents of every directory entry in
 * IndexMap order except DigitalSignature and MsiDigitalSignatureEx. Unreadable
 * entries are skipped. A file shorter than one sector feeds nothing; the
 * reference ignores that failure and finalizes the digest anyway.
 *
 * The PKCS#7 signature parsing itself lives with the PE signature machinery
 * and is not part of this helper.
 */

#ifndef XXFCLIB_MSI_YX_H
#define XXFCLIB_MSI_YX_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/document/cfbf/xx_olecf_yx.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hash-update callback, matching YARA-X's AuthenticodeHasher::update. */
typedef void (*xx_msi_yx_update_fn)(void *state, const uint8_t *data,
                                    size_t size);

/* Feeds the MSI Authenticode hash input of an OLECF file to `update`.
 * False for non-OLECF input or invalid arguments. */
XXFC_API bool xx_msi_yx_hash_input(const uint8_t *data, size_t size,
                                   xx_msi_yx_update_fn update, void *state);

/* The OLECF reader removed one leading control character from each stream
 * name; the msi module trims every remaining leading character below U+0020
 * (trim_start_matches) and compares with eq_ignore_ascii_case. Such
 * characters are single bytes in UTF-8, so trimming bytes is equivalent. */
XXFC_API bool xx_msi_yx_name_is(const xx_olecf_yx_stream *entry,
                                const char *expected);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_MSI_YX_H */
