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
    void *document; /* private parsed cross-reference index */
    void *scan;     /* private structure scan with the rules of XPDF */
    bool scan_only; /* analyzed without an object index: report strings only */
    uint32_t object_count;
    uint32_t stream_count;
    bool encrypted;
    char version[8]; /* "1.7"; UTF-8 of three Latin-1 characters at most */
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
/** Decode the current stream to a caller-owned device without opening an
 * archive-derived output path. Honors cursor size/memory limits and validates
 * the decoded extent. The destination may be partially written on failure. */
XXFC_API bool xx_pdf_unpack_current_archive_record_to_device(Abstractformat *format, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd);

/* xx_pdf_analyze uses the bounded object parser of extraction. If the xref
 * is damaged or absent it recovers directly readable objects, stopping at
 * streams whose boundaries cannot be proved; a successful analysis does not
 * imply that the container is valid for extraction. It supplies the version
 * and the security details (xx_pdf_is_encrypted and its getters). A file with
 * a "%PDF" header in which no object can be parsed is still analyzed, as the
 * reference does: it has a version and report strings, and is not encrypted.
 *
 * The report strings follow _mylibs/XPDF instead: XPDF::scanStructure finds
 * the objects (through the accepted "startxref" footers, else by reading the
 * objects from the start of the file), each object is a list of tokens cut
 * at part_limit, and names are found by comparing whole tokens. Indirect
 * values are not followed. xx_pdf_get_filters lists the sorted unique names
 * of the first /Filter of every object (100 tokens per object);
 * xx_pdf_get_info joins, with "; ", the filters, the metadata values
 * (omitted when encrypted), "Linearized" and the trigger names, from 256
 * tokens per object and from the decoded object streams. The value queries
 * return the distinct values that follow `key`, which is compared with its
 * slash ("/Creator"); a literal string is read as the reference reads it
 * (the backslash is dropped and no escape is decoded; Latin-1, or 16-bit
 * characters after a FE FF mark), strings_only keeps the literals that are
 * not dates, and a hex string is returned without its brackets.
 * Returned char* strings are UTF-8 and belong to the caller (xx_str_free);
 * version is borrowed. Query output must be an initialized list of char*;
 * each appended string belongs to the caller.
 */
XXFC_API bool xx_pdf_analyze(xx_pdf *pdf, xx_pd_struct *pd);
XXFC_API const char *xx_pdf_get_version(const xx_pdf *pdf);
XXFC_API bool xx_pdf_is_encrypted(const xx_pdf *pdf);
XXFC_API char *xx_pdf_get_filters(xx_pdf *pdf, xx_pd_struct *pd);
/** Overlay as XPDF::getFileParts(FILEPART_OVERLAY) reports it: the bytes
 * after the last accepted "startxref ... %%EOF" footer (after the header line
 * when there is none) and after the last object. The offset is relative to
 * base_address and is -1 with size 0 when there is no overlay. Works on
 * documents that xx_pdf_handle_base_info rejects; false means no "%PDF"
 * header, an I/O error or a stopped operation. */
XXFC_API bool xx_pdf_get_overlay(xx_pdf *pdf, int64_t *offset, int64_t *size, xx_pd_struct *pd);
XXFC_API char *xx_pdf_get_info(xx_pdf *pdf, xx_pd_struct *pd);
XXFC_API char *xx_pdf_get_header_comment_hex(xx_pdf *pdf, xx_pd_struct *pd);
XXFC_API char *xx_pdf_get_encryption(xx_pdf *pdf, xx_pd_struct *pd);
XXFC_API char *xx_pdf_get_permissions(xx_pdf *pdf, xx_pd_struct *pd);
XXFC_API bool xx_pdf_get_values_by_key(xx_pdf *pdf, const char *key, bool strings_only, size_t part_limit, xx_list_s *out, xx_pd_struct *pd);
XXFC_API bool xx_pdf_is_values_hex_by_key(xx_pdf *pdf, const char *key, size_t part_limit, xx_pd_struct *pd);

static inline Abstractformat *xx_pdf_to_format(xx_pdf *pdf)
{
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
XXFC_API xx_file_type_t xx_pdf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pdf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pdf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_pdf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
