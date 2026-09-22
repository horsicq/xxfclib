/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_qcow.h @brief QEMU QCOW2 / QCOW3 disk image reader. */

/* QCOW2 - the QEMU copy-on-write disk image, version 2 and version 3.
 * Every field in the container is BIG endian.
 *
 *   header
 *     +0    u32  magic, "QFI\xfb" (0x514649fb)
 *     +4    u32  version, 2 or 3
 *     +8    u64  backing file name offset, 0 when there is none
 *     +16   u32  backing file name length
 *     +20   u32  cluster_bits; the cluster size is 1 << cluster_bits
 *     +24   u64  size, the VIRTUAL disk size in bytes
 *     +32   u32  crypt_method, 0 none, 1 AES, 2 LUKS
 *     +36   u32  l1_size, the number of entries in the L1 table
 *     +40   u64  l1_table_offset
 *     +48   u64  refcount_table_offset
 *     +56   u32  refcount_table_clusters
 *     +60   u32  nb_snapshots
 *     +64   u64  snapshots_offset
 *   version 3 continues
 *     +72   u64  incompatible_features
 *     +80   u64  compatible_features
 *     +88   u64  autoclear_features
 *     +96   u32  refcount_order
 *     +100  u32  header_length
 *     +104  u8   compression_type, 0 deflate, 1 zstd (only when
 *                header_length > 104)
 *
 * The guest's linear address space is mapped through two levels of tables.
 * A guest cluster index i selects L1 entry i >> (cluster_bits - 3), whose
 * non-zero payload (bits 9..55) is the host offset of an L2 table one cluster
 * long; L2 entry i & ((1 << (cluster_bits - 3)) - 1) then describes the
 * cluster itself:
 *
 *   entry == 0                 unallocated; the cluster reads as zeros, or
 *                              comes from the backing file when one is named
 *   bit 0 set (v3)             the cluster reads as zeros
 *   bit 62 set                 the cluster is COMPRESSED. Bits 0..x-1 are a
 *                              BYTE offset, bits x..61 the number of extra
 *                              512-byte sectors minus one, where
 *                              x = 62 - (cluster_bits - 8). The payload is a
 *                              RAW DEFLATE stream (no zlib wrapper), or a
 *                              zstd frame when the v3 compression_type says so
 *   otherwise                  bits 9..55 are the host offset of the cluster
 *
 * cluster_bits and l1_size come straight off the disk and are therefore
 * attacker controlled: cluster_bits is bounded to the spec's 9..21, the L1
 * table is capped, and every host offset is re-validated against the device
 * before it is read. L2 entries are fetched eight bytes at a time as the
 * output is produced, so nothing an L2 entry can say - including pointing
 * back into the L1 table - turns into recursion or into a large allocation.
 *
 * QCOW version 1 has an unrelated header and is rejected rather than guessed
 * at; see xx_qcow.c. Encrypted images (crypt_method != 0) are listed but
 * never decrypted.
 */

#ifndef XXFCLIB_FORMAT_QCOW_H
#define XXFCLIB_FORMAT_QCOW_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_qcow xx_qcow;
typedef struct xx_qcow xx_qcow_t;
typedef struct xx_qcow XQcow;

struct xx_qcow {
    Abstractformat format;
    uint64_t number_of_records;   /**< Always 1: the guest disk image. */
    uint64_t virtual_size;        /**< Guest-visible disk size in bytes. */
    uint64_t l1_table_offset;
    uint64_t refcount_table_offset;
    uint64_t snapshots_offset;
    uint64_t incompatible_features; /**< Zero for version 2. */
    uint32_t version;             /**< 2 or 3. */
    uint32_t cluster_bits;
    uint32_t cluster_size;
    uint32_t crypt_method;        /**< 0 none, 1 AES, 2 LUKS. */
    uint32_t l1_size;             /**< Entries in the L1 table. */
    uint32_t nb_snapshots;
    uint32_t refcount_order;      /**< 4 for version 2. */
    uint8_t compression_type;     /**< 0 deflate, 1 zstd. */
    bool has_backing_file;
    bool is_encrypted;
    void *internal;
};

XXFC_API void xx_qcow_init(xx_qcow *qcow, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_qcow *xx_qcow_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_qcow_destroy(xx_qcow *qcow);
XXFC_API void xx_qcow_free(xx_qcow *qcow);

XXFC_API bool xx_qcow_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_qcow_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_qcow_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_qcow_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_qcow_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_qcow_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_qcow_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_qcow_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_qcow_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_qcow_get_virtual_size(const xx_qcow *qcow);
XXFC_API uint32_t xx_qcow_get_version(const xx_qcow *qcow);
XXFC_API uint32_t xx_qcow_get_cluster_size(const xx_qcow *qcow);
XXFC_API uint32_t xx_qcow_get_crypt_method(const xx_qcow *qcow);
/** The backing file name, or NULL. Owned by the reader. */
XXFC_API const char *xx_qcow_get_backing_file(const xx_qcow *qcow);

static inline Abstractformat *xx_qcow_to_format(xx_qcow *qcow) {
    return qcow ? &qcow->format : NULL;
}
static inline void XQcow_init(xx_qcow *qcow, xx_io_device *dev,
                              int64_t base_address) {
    xx_qcow_init(qcow, dev, base_address);
}
static inline xx_qcow *XQcow_create(xx_io_device *dev, int64_t base_address) {
    return xx_qcow_create(dev, base_address);
}
static inline void XQcow_free(xx_qcow *qcow) { xx_qcow_free(qcow); }
static inline bool XQcow_is_valid(xx_qcow *qcow, xx_pd_struct *pd) {
    return qcow ? xx_format_is_valid(&qcow->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_QCOW_H */
