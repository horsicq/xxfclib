/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_qcow1.h @brief QEMU QCOW version 1 disk image reader. */

/* QCOW version 1 - the original QEMU copy-on-write disk image. It shares the
 * "QFI\xfb" magic with QCOW2/QCOW3 (src/formats/qcow) but not the header,
 * which is why it is a reader of its own. Every field is BIG endian.
 *
 *   header, 48 bytes
 *     +0    u32  magic, "QFI\xfb" (0x514649fb)
 *     +4    u32  version, 1
 *     +8    u64  backing file name offset, 0 when there is none
 *     +16   u32  backing file name length
 *     +20   u32  mtime of the backing file
 *     +24   u64  size, the VIRTUAL disk size in bytes
 *     +32   u8   cluster_bits; the cluster size is 1 << cluster_bits (9..16)
 *     +33   u8   l2_bits; an L2 table holds 1 << l2_bits entries (6..13)
 *     +34   u16  padding
 *     +36   u32  crypt_method, 0 none, 1 AES-128-CBC
 *     +40   u64  l1_table_offset
 *
 * The L1 table has ceil(size / (1 << (cluster_bits + l2_bits))) entries,
 * each the host byte offset of an L2 table (0 = not allocated). Guest
 * cluster i uses L1 entry i >> l2_bits and L2 entry i & ((1 << l2_bits) - 1):
 *
 *   entry == 0         unallocated: zeros, or the backing file's data
 *   bit 63 set         COMPRESSED: bits 0 .. 62-cluster_bits are the host
 *                      byte offset, the next cluster_bits bits the exact
 *                      compressed byte count; the payload is RAW deflate
 *   otherwise          host byte offset of the cluster
 *
 * The reader publishes ONE member, "disk.img", the reconstructed guest disk.
 * Legacy AES-128-CBC encrypted clusters accept an explicitly supplied
 * OPT_PASSWORD (the first 16 bytes, zero padded). This legacy encryption has
 * no password verifier or integrity tag. NULL-destination decoding validates
 * all stored clusters. A backing file is reported but never opened.
 */

#ifndef XXFCLIB_FORMAT_QCOW1_H
#define XXFCLIB_FORMAT_QCOW1_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_qcow1 xx_qcow1;
typedef struct xx_qcow1 xx_qcow1_t;
typedef struct xx_qcow1 XQcow1;

struct xx_qcow1 {
    Abstractformat format;
    uint64_t number_of_records; /**< Always 1: the guest disk image. */
    uint64_t virtual_size;      /**< Guest-visible disk size in bytes. */
    uint64_t l1_table_offset;
    uint32_t cluster_bits;
    uint32_t cluster_size;
    uint32_t l2_bits;
    uint32_t l1_size;      /**< Entries in the L1 table. */
    uint32_t crypt_method; /**< 0 none, 1 AES. */
    uint32_t mtime;
    bool has_backing_file;
    bool is_encrypted;
    void *internal;
};

XXFC_API void xx_qcow1_init(xx_qcow1 *qcow1, xx_io_device *dev, int64_t base_address);
XXFC_API xx_qcow1 *xx_qcow1_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_qcow1_destroy(xx_qcow1 *qcow1);
XXFC_API void xx_qcow1_free(xx_qcow1 *qcow1);

XXFC_API bool xx_qcow1_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_qcow1_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_qcow1_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_qcow1_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_qcow1_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_qcow1_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_qcow1_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_qcow1_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_qcow1_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_qcow1_get_virtual_size(const xx_qcow1 *qcow1);
XXFC_API uint32_t xx_qcow1_get_cluster_size(const xx_qcow1 *qcow1);
XXFC_API uint32_t xx_qcow1_get_crypt_method(const xx_qcow1 *qcow1);
/** The backing file name, or NULL. Owned by the reader. */
XXFC_API const char *xx_qcow1_get_backing_file(const xx_qcow1 *qcow1);

static inline Abstractformat *xx_qcow1_to_format(xx_qcow1 *qcow1)
{
    return qcow1 ? &qcow1->format : NULL;
}
static inline void XQcow1_init(xx_qcow1 *qcow1, xx_io_device *dev, int64_t base_address)
{
    xx_qcow1_init(qcow1, dev, base_address);
}
static inline xx_qcow1 *XQcow1_create(xx_io_device *dev, int64_t base_address)
{
    return xx_qcow1_create(dev, base_address);
}
static inline void XQcow1_free(xx_qcow1 *qcow1)
{
    xx_qcow1_free(qcow1);
}
static inline bool XQcow1_is_valid(xx_qcow1 *qcow1, xx_pd_struct *pd)
{
    return qcow1 ? xx_format_is_valid(&qcow1->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_qcow1_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_qcow1_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_qcow1_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_qcow1_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_qcow1_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_QCOW1_H */
