/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_PDF_H
#define XXFCLIB_FORMAT_PDF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Native PDF container reader. The input device is borrowed. Stream records
 * expose decoded bytes; DCT and JPX image streams retain their encoded bytes.
 * Encrypted documents can be identified but are not decrypted. PDF rendering
 * and page-text interpretation are outside this interface.
 *
 * Parsing is bounded to 262144 cross-reference entries, 64 revisions and
 * 64 levels of object nesting. The default format-owned memory budget and
 * decoded-member limit are each 256 MiB; the standard archive options can
 * lower these limits. Unknown filters fail extraction rather than returning
 * encoded bytes as decoded data.
 */
typedef struct xx_pdf {
    Abstractformat format;
    void *document;               /* private parsed cross-reference index */
    uint32_t object_count;
    uint32_t stream_count;
    bool encrypted;
    char version[4];
} xx_pdf;
typedef xx_pdf xx_pdf_t;

XXFC_API void xx_pdf_init(xx_pdf *pdf, xx_io_device *device, int64_t base_address);
XXFC_API xx_pdf *xx_pdf_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pdf_destroy(xx_pdf *pdf);
XXFC_API void xx_pdf_free(xx_pdf *pdf);
XXFC_API bool xx_pdf_check_magic(const uint8_t *data, size_t size);
XXFC_API bool xx_pdf_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_pdf_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
XXFC_API int64_t xx_pdf_get_format_size(Abstractformat *format, xx_pd_struct *pd);

/* Inspection uses the same bounded object parser as extraction. If the xref
 * is damaged or absent it recovers directly readable objects, stopping at
 * streams whose boundaries cannot be proved. A successful analysis does not
 * imply that the container is valid for extraction. Active xref revisions
 * take precedence, and compressed objects are inspected after decoding.
 * Returned char* strings belong to the caller (xx_str_free); version is
 * borrowed. Query output must be an initialized list of char*; distinct
 * values are appended and each appended string belongs to the caller.
 * part_limit counts dictionary/array delimiters and values per object.
 * Literal strings use PDF escapes/PDFDocEncoding/Unicode; generic hex
 * queries preserve their source brackets, and strings_only keeps literals.
 * Filters are sorted unique names (including arrays); filter inspection
 * visits 100 tokens per object, and general info visits 256. Security details
 * are provided by the dedicated getters; encrypted metadata is omitted.
 */
XXFC_API bool xx_pdf_analyze(xx_pdf *pdf, xx_pd_struct *pd);
XXFC_API const char *xx_pdf_get_version(const xx_pdf *pdf);
XXFC_API bool xx_pdf_is_encrypted(const xx_pdf *pdf);
XXFC_API char *xx_pdf_get_filters(xx_pdf *pdf, xx_pd_struct *pd);
XXFC_API char *xx_pdf_get_info(xx_pdf *pdf, xx_pd_struct *pd);
XXFC_API char *xx_pdf_get_header_comment_hex(xx_pdf *pdf, xx_pd_struct *pd);
XXFC_API char *xx_pdf_get_encryption(xx_pdf *pdf, xx_pd_struct *pd);
XXFC_API char *xx_pdf_get_permissions(xx_pdf *pdf, xx_pd_struct *pd);
XXFC_API bool xx_pdf_get_values_by_key(xx_pdf *pdf, const char *key,
                                      bool strings_only, size_t part_limit,
                                      xx_list_s *out, xx_pd_struct *pd);
XXFC_API bool xx_pdf_is_values_hex_by_key(xx_pdf *pdf, const char *key,
                                         size_t part_limit, xx_pd_struct *pd);

static inline Abstractformat *xx_pdf_to_format(xx_pdf *pdf) {
    return pdf ? &pdf->format : NULL;
}

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_pdf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pdf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pdf_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
