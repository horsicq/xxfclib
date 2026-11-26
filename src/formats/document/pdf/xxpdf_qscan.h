/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_PDF_QSCAN_H
#define XXFCLIB_PDF_QSCAN_H

#include "xxfclib/data/xx_pd.h"
#include "xxfclib/io/xx_io.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Structure scan with the rules of _mylibs/XPDF (XPDF::scanStructure and the
 * token lists of XPDF::getParts). DIE reports are produced from these rules,
 * not from the cross-reference index of the archive reader: the scan accepts
 * damaged files, lists every physical object and keeps the tokenizer's
 * behaviour on malformed input. It never changes the device. */
typedef struct xx_pdf_qscan xx_pdf_qscan;

/* "/Title", "/Author", "/Subject", "/Keywords", "/Creator", "/Producer",
 * "/CreationDate", "/ModDate": the keys of XPDF::getMetaInfoString. */
#define XX_PDF_QSCAN_META_COUNT 8U
extern const char *const xx_pdf_qscan_meta_keys[XX_PDF_QSCAN_META_COUNT];

/* Result of one XPDF::getInfo pass over the object token lists. Strings are
 * UTF-8 and owned by the caller (xx_str_free); a missing line is an empty
 * string. */
typedef struct xx_pdf_qscan_info {
    char *filters;    /* "/A, /B": sorted unique names, without a label */
    char *meta[XX_PDF_QSCAN_META_COUNT]; /* first non-empty value per key */
    char *linearized; /* "Linearized" or "Linearized (declared L=.., actual=..)" */
    char *suspicious; /* "/OpenAction, /JS": trigger order, without a label */
} xx_pdf_qscan_info;

/* NULL when the range has no "%PDF" header, on I/O failure, when stopped or
 * out of memory. */
xx_pdf_qscan *xx_pdf_qscan_create(xx_io_device *device, int64_t base_address, xx_pd_struct *pd);
void xx_pdf_qscan_free(xx_pdf_qscan *scan);

/* XPDF::getVersion: at most three Latin-1 characters after "%PDF-", as UTF-8. */
void xx_pdf_qscan_version(const xx_pdf_qscan *scan, char version[8]);
/* XPDF::getHeaderCommentAsHex; the caller frees the string. */
char *xx_pdf_qscan_header_comment_hex(xx_pdf_qscan *scan);

/* XPDF::getFilePartsWithDecodeLimit(FILEPART_OVERLAY): offset is relative to
 * base_address and is -1 (size 0) when the file has no overlay. */
void xx_pdf_qscan_overlay(const xx_pdf_qscan *scan, int64_t *offset, int64_t *size);

/* part_limit is the per-object token limit of XPDF::getParts (256 for
 * getInfo, 100 for getFilters). locked is XPDF::isEncryptedButLocked(): the
 * object streams of such a file are not searched for trigger names. An
 * object stream is decoded with its last filter only; a stream whose decoder
 * reports an error is not searched. */
bool xx_pdf_qscan_get_info(xx_pdf_qscan *scan, int part_limit, bool locked, xx_pdf_qscan_info *info, xx_pd_struct *pd);
void xx_pdf_qscan_info_free(xx_pdf_qscan_info *info);

/* XBinary::VT_* of XPDF::_parseValue, as far as callers tell them apart. */
typedef enum xx_pdf_qscan_value_type {
    XX_PDF_QSCAN_VALUE_OTHER, /* number, name, reference, delimiter, ... */
    XX_PDF_QSCAN_VALUE_STRING,
    XX_PDF_QSCAN_VALUE_HEX,
    XX_PDF_QSCAN_VALUE_DATE
} xx_pdf_qscan_value_type;
typedef struct xx_pdf_qscan_value {
    char *text; /* QVariant::toString, UTF-8 */
    xx_pdf_qscan_value_type type;
} xx_pdf_qscan_value;

/* XPDF::getValuesByKey over XPDF::getParts(part_limit): the distinct values
 * of the tokens that follow `key` (compared as a whole token, with its
 * slash), in object order. Literal strings keep the reference's reading: no
 * escape is decoded, the backslash is dropped; bytes are Latin-1, or 16-bit
 * characters after a FE FF mark. A "(D:...)" string of 18 characters or more
 * is a date in ISO form. Free with xx_pdf_qscan_values_free. */
bool xx_pdf_qscan_get_values(xx_pdf_qscan *scan, const char *key, int part_limit, xx_pdf_qscan_value **values, size_t *count, xx_pd_struct *pd);
void xx_pdf_qscan_values_free(xx_pdf_qscan_value *values, size_t count);

#ifdef __cplusplus
}
#endif

#endif
