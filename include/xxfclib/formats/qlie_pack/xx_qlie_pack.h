/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_QLIE_PACK_H
#define XXFCLIB_FORMAT_QLIE_PACK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Native read-only FilePackVer1.0/2.0/3.0/3.1 archives. The28-byte footer
 * belongs at the declared archive end. A zero archive_size uses device EOF;
 * set_archive_size permits a bounded embedded archive and trailing overlay.
 * FilePackVer1.0 names/data have three indistinguishable legacy profiles:
 * listing/extraction requires an explicit legacy profile; header validity
 * alone is recognition. Other versions select their documented profiles.
 * Stored bytes and1PC-FF byte-pair compression (16/32-bit symbol counts) are
 * returned unchanged as resource files; there is no image conversion.
 * Version3.1 encryption methods0/1/2 and version1..3.0's boolean encryption
 * are supported. Unknown versions/methods are refused. Version3 payload hashes
 * check complete ciphertext qwords; short tails and encryption keys have no
 * authentication. A wrong supplied key can still produce plausible stored
 * output. HashVer lookup data is bounded opaque metadata, not enumerated.
 *
 * Version3.0 without a key file uses its simple feedback cipher. A supplied
 * key file selects Qlie's modified64-state generator, optionally mixed with
 * supplied256-byte game key data. No key files, executable scans or game key
 * database are implicit. Version3.1 method2 uses the documented default table
 * when no key file is available. The first member whose original name contains
 * the case-sensitive text pack_keyfile replaces the key for subsequent members
 * (3.1 always;3.0 when an initial key was supplied). Key members remain listed.
 * Callers must supply initial keys for members preceding an embedded key.
 * Keys and raw names are snapshotted by every iterator. Operation OPT_PASSWORD
 * precedes format-wide OPT_PASSWORD and then the setter; it accepts raw bytes
 * or an even-length narrow hexadecimal string, up to1MiB decoded bytes.
 *
 * CP932 bytes use reversible %XX escapes; UTF16 non-ASCII/control code units
 * use reversible %uXXXX escapes. Literal '%' is escaped, real backslashes
 * become '/', and CP932 trail backslashes stay escaped. Original raw names
 * drive encryption before safe aliases. Duplicate and implicit-directory leaf
 * collisions use %_<index>; unsafe output paths remain listable only.
 * Short I/O, cancellation, known input cursors, operation/format member and
 * memory limits, exclusive short sibling stages and explicit overwrite work
 * through both the record callback and direct device API. NULL output verifies
 * decoding. Bounds:0xFFFFF members,256 raw name units,64MiB index/name storage,
 * 256MiB packed/plain member,1MiB key file. MEMORY_LIMIT includes retained
 * iterator capacities and packed/plain decoding workspace, excluding generic
 * record metadata and parser transients. Source/crypto provenance and complete
 * upstream notices are retained in the implementation.
 */
typedef enum xx_qlie_pack_legacy_profile {
    XX_QLIE_PACK_LEGACY_UNSELECTED = 0,
    XX_QLIE_PACK_LEGACY_V1 = 1,
    XX_QLIE_PACK_LEGACY_V2_NO_HASH = 2,
    XX_QLIE_PACK_LEGACY_V2_WITH_HASH = 3
} xx_qlie_pack_legacy_profile;
/* Reader-local operation/format selector, values above, for1.0 archives. */
#define XX_QLIE_PACK_OPT_LEGACY_PROFILE XX_META_ID_ENCRYPTION_METHOD

typedef struct xx_qlie_pack {
    Abstractformat format;
    uint64_t number_of_records, archive_size;
    uint32_t archive_key;
    uint8_t major_version, minor_version;
    xx_qlie_pack_legacy_profile legacy_profile;
    uint8_t *key_file;
    size_t key_file_size;
    bool has_key_file, has_game_key;
    uint8_t game_key[256];
} xx_qlie_pack;
typedef xx_qlie_pack xx_qlie_pack_t;
typedef xx_qlie_pack XQliePack;
XXFC_API void xx_qlie_pack_init(xx_qlie_pack *, xx_io_device *, int64_t);
XXFC_API xx_qlie_pack *xx_qlie_pack_create(xx_io_device *, int64_t);
XXFC_API void xx_qlie_pack_destroy(xx_qlie_pack *);
XXFC_API void xx_qlie_pack_free(xx_qlie_pack *);
XXFC_API bool xx_qlie_pack_set_legacy_profile(xx_qlie_pack *, xx_qlie_pack_legacy_profile);
XXFC_API bool xx_qlie_pack_set_archive_size(xx_qlie_pack *, uint64_t);
/* Setters copy bytes; invalid requests preserve the previous key. */
XXFC_API bool xx_qlie_pack_set_key_file(xx_qlie_pack *, const uint8_t *, size_t);
XXFC_API void xx_qlie_pack_clear_key_file(xx_qlie_pack *);
XXFC_API bool xx_qlie_pack_set_game_key(xx_qlie_pack *, const uint8_t[256]);
XXFC_API void xx_qlie_pack_clear_game_key(xx_qlie_pack *);
XXFC_API bool xx_qlie_pack_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_qlie_pack_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_qlie_pack_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_qlie_pack_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_qlie_pack_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_qlie_pack_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_qlie_pack_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_qlie_pack_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_qlie_pack_unpack_current_archive_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_qlie_pack_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
#ifdef __cplusplus
}
#endif
#endif
