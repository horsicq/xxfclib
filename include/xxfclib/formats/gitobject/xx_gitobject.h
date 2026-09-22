/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_gitobject.h @brief Native Git loose-object reader. */

#ifndef XXFCLIB_FORMAT_GITOBJECT_H
#define XXFCLIB_FORMAT_GITOBJECT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_gitobject {
    Abstractformat format;
    uint64_t object_size;
    int64_t stream_end;
    char object_type[7];
} xx_gitobject;

typedef xx_gitobject xx_gitobject_t;
typedef xx_gitobject XGitObject;

XXFC_API void xx_gitobject_init(xx_gitobject *archive, xx_io_device *device,
                                int64_t base_address);
XXFC_API xx_gitobject *xx_gitobject_create(xx_io_device *device,
                                            int64_t base_address);
XXFC_API void xx_gitobject_destroy(xx_gitobject *archive);
XXFC_API void xx_gitobject_free(xx_gitobject *archive);

XXFC_API bool xx_gitobject_check_is_valid(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API bool xx_gitobject_handle_base_info(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API int64_t xx_gitobject_get_format_size(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API uint64_t xx_gitobject_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Validate the loose object and write its body, without its Git header. */
XXFC_API bool xx_gitobject_unpack_to_device(xx_gitobject *archive,
                                             xx_io_device *destination,
                                             xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_gitobject_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_gitobject_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_gitobject_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_gitobject_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_gitobject_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_gitobject_get_object_size(const xx_gitobject *archive);
XXFC_API int64_t xx_gitobject_get_stream_end(const xx_gitobject *archive);
XXFC_API const char *xx_gitobject_get_object_type(const xx_gitobject *archive);

static inline Abstractformat *xx_gitobject_to_format(xx_gitobject *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GITOBJECT_H */
