/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_luks.h @brief LUKS1 / LUKS2 encrypted container identification. */

/* LUKS - the Linux Unified Key Setup container, versions 1 and 2. Both put
 * the same magic at offset 0 and both are BIG endian in their binary parts.
 *
 *   LUKS1 phdr, 592 bytes at offset 0
 *     +0    char[6]  "LUKS\xba\xbe"
 *     +6    u16      version, 1
 *     +8    char[32] cipher-name, NUL padded ("aes")
 *     +40   char[32] cipher-mode ("xts-plain64")
 *     +72   char[32] hash-spec ("sha256")
 *     +104  u32      payload-offset, in 512-byte SECTORS
 *     +108  u32      key-bytes, the master key length
 *     +112  u8[20]   mk-digest
 *     +132  u8[32]   mk-digest-salt
 *     +164  u32      mk-digest-iter
 *     +168  char[40] uuid
 *     +208  8 key slots of 48 bytes each
 *              +0   u32  state, 0x0000dead disabled / 0x00ac71f3 enabled
 *              +4   u32  iterations
 *              +8   u8[32] salt
 *              +40  u32  key-material-offset, in sectors
 *              +44  u32  stripes
 *
 *   LUKS2 binary header, 4096 bytes, followed by a JSON metadata area
 *     +0    char[6]  "LUKS\xba\xbe" (the SECONDARY copy reads "SKUL\xba\xbe")
 *     +6    u16      version, 2
 *     +8    u64      hdr_size, the binary header plus the JSON area
 *     +16   u64      seqid
 *     +24   char[48] label
 *     +72   char[32] checksum algorithm
 *     +104  u8[64]   salt
 *     +168  char[40] uuid
 *     +208  char[48] subsystem
 *     +256  u64      hdr_offset
 *     +448  u8[64]   checksum
 *   The data segment's offset lives in the JSON area, not in the binary part.
 *
 * A LUKS container is ENCRYPTED BY DEFINITION: without the passphrase there
 * is no plaintext to hand out, and this reader attempts NO key derivation,
 * NO passphrase search and NO decryption of any kind. It identifies the
 * container, publishes the header metadata, publishes the payload region as
 * a single record flagged XX_META_ID_IS_ENCRYPTED, and refuses extraction.
 */

#ifndef XXFCLIB_FORMAT_LUKS_H
#define XXFCLIB_FORMAT_LUKS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_LUKS_KEY_SLOTS 8

typedef struct xx_luks xx_luks;
typedef struct xx_luks xx_luks_t;
typedef struct xx_luks XLuks;

struct xx_luks {
    Abstractformat format;
    uint64_t number_of_records;  /**< Always 1: the encrypted payload. */
    uint64_t payload_offset;     /**< Byte offset of the data segment. */
    uint64_t payload_size;       /**< Bytes from payload_offset to the end. */
    uint64_t header_size;        /**< LUKS2 hdr_size; 592 for LUKS1. */
    uint64_t seqid;              /**< LUKS2 only. */
    uint32_t version;            /**< 1 or 2. */
    uint32_t key_bytes;          /**< LUKS1 master key length. */
    uint32_t mk_digest_iter;     /**< LUKS1 only. */
    uint32_t active_slots;       /**< LUKS1 bitmask of enabled key slots. */
    /* NUL-terminated copies of the header's fixed-width name fields. */
    char cipher_name[33];
    char cipher_mode[33];
    char hash_spec[33];
    char uuid[41];
    char label[49];              /**< LUKS2 only. */
    char subsystem[49];          /**< LUKS2 only. */
    bool payload_offset_exact;   /**< False when a LUKS2 offset was inferred. */
    void *internal;
};

XXFC_API void xx_luks_init(xx_luks *luks, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_luks *xx_luks_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_luks_destroy(xx_luks *luks);
XXFC_API void xx_luks_free(xx_luks *luks);

XXFC_API bool xx_luks_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_luks_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_luks_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_luks_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_luks_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_luks_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
/** Always returns false: a LUKS payload is ciphertext and stays that way. */
XXFC_API bool xx_luks_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_luks_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_luks_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint32_t xx_luks_get_version(const xx_luks *luks);
XXFC_API uint64_t xx_luks_get_payload_offset(const xx_luks *luks);
XXFC_API const char *xx_luks_get_cipher_name(const xx_luks *luks);
XXFC_API const char *xx_luks_get_cipher_mode(const xx_luks *luks);
XXFC_API const char *xx_luks_get_uuid(const xx_luks *luks);
/** True when key slot index is enabled. LUKS1 only; LUKS2 slots are JSON. */
XXFC_API bool xx_luks_is_key_slot_active(const xx_luks *luks, unsigned index);

static inline Abstractformat *xx_luks_to_format(xx_luks *luks) {
    return luks ? &luks->format : NULL;
}
static inline void XLuks_init(xx_luks *luks, xx_io_device *dev,
                              int64_t base_address) {
    xx_luks_init(luks, dev, base_address);
}
static inline xx_luks *XLuks_create(xx_io_device *dev, int64_t base_address) {
    return xx_luks_create(dev, base_address);
}
static inline void XLuks_free(xx_luks *luks) { xx_luks_free(luks); }
static inline bool XLuks_is_valid(xx_luks *luks, xx_pd_struct *pd) {
    return luks ? xx_format_is_valid(&luks->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LUKS_H */
