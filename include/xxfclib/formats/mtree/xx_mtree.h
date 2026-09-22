/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_mtree.h @brief Native BSD mtree manifest reader. */

#ifndef XXFCLIB_FORMAT_MTREE_H
#define XXFCLIB_FORMAT_MTREE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_mtree {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_mtree;

typedef xx_mtree xx_mtree_t;
typedef xx_mtree XMTree;

XXFC_API void xx_mtree_init(xx_mtree *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_mtree *xx_mtree_create(xx_io_device *device,
                                   int64_t base_address);
XXFC_API void xx_mtree_destroy(xx_mtree *archive);
XXFC_API void xx_mtree_free(xx_mtree *archive);

XXFC_API bool xx_mtree_check_is_valid(Abstractformat *self,
                                      xx_pd_struct *pd);
XXFC_API bool xx_mtree_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_mtree_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_mtree_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_mtree_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mtree_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mtree_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mtree_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mtree_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_mtree_to_format(xx_mtree *archive) {
    return archive ? &archive->format : NULL;
}

static inline void XMTree_init(xx_mtree *archive, xx_io_device *device,
                               int64_t base_address) {
    xx_mtree_init(archive, device, base_address);
}
static inline xx_mtree *XMTree_create(xx_io_device *device,
                                      int64_t base_address) {
    return xx_mtree_create(device, base_address);
}
static inline void XMTree_free(xx_mtree *archive) { xx_mtree_free(archive); }
static inline bool XMTree_is_valid(xx_mtree *archive, xx_pd_struct *pd) {
    return archive ? xx_format_is_valid(&archive->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MTREE_H */
