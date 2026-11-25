/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_pef.h @brief Classic Mac OS Preferred Executable Format reader. */

#ifndef XXFCLIB_FORMAT_PEF_H
#define XXFCLIB_FORMAT_PEF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_pef {
    Abstractformat format;
    uint32_t architecture; /**< Big-endian four-character code (`pwpc` or `m68k`). */
    uint16_t section_count;
    uint16_t instantiated_section_count;
    uint32_t loader_offset;
    uint32_t loader_size;
    uint32_t loader_strings_offset;
    uint32_t imported_library_count;
    uint32_t imported_symbol_count;
    int32_t main_section;
    uint32_t main_offset;
    int32_t init_section;
    uint32_t init_offset;
    int32_t term_section;
    uint32_t term_offset;
} xx_pef;

/** Optional stream providers, available when their build module is enabled. */
XXFC_API const StreamImport *xx_pef_get_stream_import(void);

XXFC_API void xx_pef_init(xx_pef *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_pef *xx_pef_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pef_destroy(xx_pef *reader);
XXFC_API void xx_pef_free(xx_pef *reader);
XXFC_API bool xx_pef_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_pef_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_pef_get_number_of_imports(Abstractformat *self, xx_pd_struct *pd);

/** Number of loader import libraries after base information is handled. */
XXFC_API uint32_t xx_pef_get_number_of_imported_libraries(xx_pef *reader, xx_pd_struct *pd);
/** Library name in loader order, newly allocated; free with xx_str_free(). */
XXFC_API char *xx_pef_get_imported_library_name(xx_pef *reader, uint32_t index, xx_pd_struct *pd);

/** Loader entry tuples, available after base information is handled. False
 * means the corresponding section is -1/absent. The returned section index is
 * zero based and the offset is section relative. */
XXFC_API bool xx_pef_get_entry_point(const xx_pef *reader, uint16_t *section_index, uint32_t *section_offset);
XXFC_API bool xx_pef_get_init_point(const xx_pef *reader, uint16_t *section_index, uint32_t *section_offset);
XXFC_API bool xx_pef_get_term_point(const xx_pef *reader, uint16_t *section_index, uint32_t *section_offset);

XXFC_API xx_import_state *xx_pef_create_imports_reading(Abstractformat *self, xx_pd_struct *pd);
XXFC_API const xx_import_record *xx_pef_get_current_import(Abstractformat *self, xx_import_state *state);
XXFC_API bool xx_pef_import_move_to_next(Abstractformat *self, xx_import_state *state, xx_pd_struct *pd);
XXFC_API void xx_pef_free_imports_reading(Abstractformat *self, xx_import_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_pef_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_pef_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pef_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pef_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_pef_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_PEF_H */
