/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_freearc.h @brief FreeArc archive reader. */

#ifndef XXFCLIB_FORMAT_FREEARC_H
#define XXFCLIB_FORMAT_FREEARC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A FreeArc archive.
 *
 * Identification needs only the eight-byte header and the block descriptor
 * signature that follows it. Listing walks the footer block found near the
 * end of the archive and every directory block it names. Members are
 * extracted when their solid block's method chain consists only of the
 * methods this reader decodes: storing, lzma, rep, exe, delta and bounded
 * FreeArc lzp. The lzp stage accepts original b/l/h/d/s and compression-
 * threshold options with a 256 MiB block cap, at most 20 hash bits and a
 * 64-million-entry total hash-table initialization work budget per chain.
 * Blocks using any other FreeArc method (tor, ppmd, grzip, dict,
 * encryption, ...) are listed but their members cannot be unpacked.
 */
typedef struct xx_freearc {
    Abstractformat format;
    uint16_t flags;   /**< Header flags, bytes 4..5. */
    uint16_t version; /**< Header version, bytes 6..7. */
    int64_t archive_size; /**< End of the footer descriptor, or -1. */
    void *index;          /**< Parsed directory (private), NULL until read. */
    bool index_tried;     /**< The directory has been parsed (or failed). */
} xx_freearc;

typedef xx_freearc xx_freearc_t;
typedef xx_freearc XFreearc;

XXFC_API void xx_freearc_init(xx_freearc *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_freearc *xx_freearc_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_freearc_destroy(xx_freearc *archive);
XXFC_API void xx_freearc_free(xx_freearc *archive);

XXFC_API bool xx_freearc_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_freearc_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_freearc_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_freearc_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_freearc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_freearc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_freearc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_freearc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_freearc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode a FreeArc method chain held in memory.
 *
 * @p method is the chain text ("rep:96mb+exe+lzma:1mb", ...). The stages are
 * undone from last to first. @p expected is the final size, or -1 when it is
 * unknown. On success *out receives a buffer from xx_mem_alloc (possibly
 * @p in itself) that the caller frees; @p in is always consumed.
 */
XXFC_API bool xx_freearc_decode_chain(const char *method, uint8_t *in,
                                      size_t in_size, int64_t expected,
                                      uint8_t **out, size_t *out_size,
                                      xx_pd_struct *pd);
/** @brief True when every stage of @p method is one this reader decodes. */
XXFC_API bool xx_freearc_method_supported(const char *method);

XXFC_API uint16_t xx_freearc_get_flags(const xx_freearc *archive);
XXFC_API uint16_t xx_freearc_get_version(const xx_freearc *archive);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_freearc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_freearc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_freearc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_freearc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_freearc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_FREEARC_H */
