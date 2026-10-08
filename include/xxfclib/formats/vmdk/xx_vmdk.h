/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_vmdk.h @brief VMware sparse extent (VMDK) disk image. */

#ifndef XXFCLIB_FORMAT_VMDK_H
#define XXFCLIB_FORMAT_VMDK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* VMware hosted sparse (versions 1-3), compressed grain markers and
 * streamOptimized GD_AT_END footer layouts, presented as one disk.img.
 * Deflate/zlib is decoded natively. Parent chains are unsupported. External
 * QEMU flat and split sparse descriptors may attach safe local sidecars.
 * An embedded descriptor
 * must prove parentCID=ffffffff before unallocated grains become zeroes.
 * Descriptorless extents remain listable, but extraction refuses unknown
 * inherited grains; allocated and explicitly zeroed grains remain readable.
 * Limits: grain <=32 MiB, descriptor <=1 MiB, directory <=4M entries,
 * table <=1M entries, <=16M grains; compressed payload <=2*grain+64 bytes.
 * Input cursor is preserved. File extraction stages output exclusively and
 * defaults to no overwrite; OPT_MEMORY_LIMIT/MAX_MEMBER_SIZE are honored. */
typedef struct xx_vmdk {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    struct xx_vmdk_external_state *external;
    bool parentless_extent;
} xx_vmdk;

typedef struct xx_vmdk xx_vmdk_t;

XXFC_API void xx_vmdk_init(xx_vmdk *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_vmdk *xx_vmdk_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_vmdk_destroy(xx_vmdk *archive);
XXFC_API void xx_vmdk_free(xx_vmdk *archive);
XXFC_API bool xx_vmdk_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_vmdk_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_vmdk_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_vmdk_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_vmdk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_vmdk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_vmdk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
/** Reconstruct record 0 to a borrowed output device; never closes it. */
XXFC_API bool xx_vmdk_unpack_to_device(xx_vmdk *archive, uint64_t index,
                                       xx_io_device *destination, xx_pd_struct *pd);
/** Open the logical disk as a bounded, read-only, seekable device. The caller
 * owns the returned device and closes it with xx_io_close(); the input device
 * and archive must outlive it. Sparse holes read as zero only when the VMDK
 * descriptor proves that no parent image is needed. */
XXFC_API xx_io_device *xx_vmdk_open_disk_device(xx_vmdk *archive,
                                                xx_pd_struct *pd);
/** Attach each sidecar named by a parentless QEMU descriptor, resolving only
 * safe basenames inside the descriptor's directory. The archive owns them. */
XXFC_API bool xx_vmdk_open_data_files(xx_vmdk *archive,
                                     const char *descriptor_path);
XXFC_API bool xx_vmdk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_vmdk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_vmdk_to_format(xx_vmdk *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_vmdk_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_vmdk_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_vmdk_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_VMDK_H */
