/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_xva.h @brief Original native Xen XVA disk reconstruction.
 * Modern single, uncompressed V7/USTAR containers with an XML-RPC ova.xml
 * manifest and one-megabyte VDI chunks are supported. Stored chunks require
 * SHA1 .checksum or seed-zero XXH64 .xxhash companions. Missing numbered
 * chunks are zero holes; zero-length keepalives consume archive numbering
 * without disk bytes. Final chunks may be short or padded to one megabyte.
 * Each included VDI is exposed as Ref_N.img; metadata-only VDI references
 * are omitted. The last logical chunk must be stored. Listing validates the
 * manifest/index and checksum syntax; extraction verifies stored payloads,
 * including padding and keepalives, before publishing a staged file.
 * Old appliance XML/gzipped one-gigabyte chunks, checksum-table-only exports,
 * outer compression, PAX/GNU extensions, TAR links and multiple containers
 * are unsupported. This reader reconstructs disks, not a runnable VM.
 * Primary layouts: Xen xapi importexport.ml/stream_vdi.ml and DiscUtils.Xva.
 * No upstream implementation is incorporated. Native XXH64 follows its
 * published algorithm; SHA1 uses the library's shared native implementation.
 * Bounds: 16MiB XML, 262144 XML nodes, 64 XML levels, 16 million XML work
 * steps, 100000 TAR entries, 1024 VDIs, 128MiB parser allocations, 1PiB per
 * disk, 255-byte archive paths. Initial parsing uses these hard ceilings.
 * Extraction MAX_MEMBER_SIZE covers logical bytes including zero holes;
 * MEMORY_LIMIT covers the retained view, iterator and bounded 64KiB transfer
 * and checksum workspace. Generic metadata/options and caller device storage
 * are excluded. Iterator options override format options; input positions
 * are preserved. Output uses exclusive sibling staging, cancellation before
 * publication, and overwrite=false by default. Borrowed input devices must
 * remain open and unchanged until the volume and iterators are released.
 */
#ifndef XXFCLIB_FORMAT_XVA_H
#define XXFCLIB_FORMAT_XVA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_xva {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t total_disk_size;
    uint32_t chunk_size;
    void *internal;
} xx_xva;
typedef xx_xva xx_xva_t;
typedef xx_xva XXva;
XXFC_API void xx_xva_init(xx_xva *, xx_io_device *, int64_t);
XXFC_API xx_xva *xx_xva_create(xx_io_device *, int64_t);
XXFC_API void xx_xva_destroy(xx_xva *);
XXFC_API void xx_xva_free(xx_xva *);
XXFC_API bool xx_xva_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_xva_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_xva_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_xva_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_xva_create_archive_records_reading(
    Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_xva_get_current_archive_record(
    Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_xva_archive_record_move_to_next(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_xva_unpack_current_archive_record(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
/** NULL destination verifies every stored chunk, including keepalive/padding.
 * Destination advances and must differ from the borrowed input device. */
XXFC_API bool xx_xva_extract_record_to_device(
    Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_xva_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_xva_to_format(xx_xva *v) { return v ? &v->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif
