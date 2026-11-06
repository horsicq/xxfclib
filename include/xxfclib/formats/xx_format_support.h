/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_FORMAT_SUPPORT_H
#define XX_FORMAT_SUPPORT_H

/* Presets expand to individual format families. Individual flags may also
 * be supplied directly. Define flags consistently for the library and its
 * unpacker; setting a flag on the final executable cannot add linked readers.
 * No selection preserves the historical complete-library configuration. */
#if (defined(XFU_SUPPORT_FORMAT_ALL) + defined(XFU_SUPPORT_FORMAT_DEFAULT) + \
     defined(XFU_SUPPORT_FORMAT_SCANENGINEMIN)) > 1
#error "Select only one XFU format preset"
#endif

/* The generated list of every family flag: no preset and no flag selects
 * the complete library. */
#include "xxfclib/formats/xx_format_support_flags.inc"
#if !defined(XFU_SUPPORT_FORMAT_ALL) && !defined(XFU_SUPPORT_FORMAT_DEFAULT) && \
    !defined(XFU_SUPPORT_FORMAT_SCANENGINEMIN) && !XFU_FORMAT_SUPPORT_ANY_FLAG
#define XFU_SUPPORT_FORMAT_ALL 1
#endif

#if defined(XFU_SUPPORT_FORMAT_ALL) || defined(XFU_SUPPORT_FORMAT_DEFAULT) || \
    defined(XFU_SUPPORT_FORMAT_SCANENGINEMIN)
#ifndef XFU_SUPPORT_FORMAT_ZIP
#define XFU_SUPPORT_FORMAT_ZIP 1
#endif
#ifndef XFU_SUPPORT_FORMAT_7ZIP
#define XFU_SUPPORT_FORMAT_7ZIP 1
#endif
#ifndef XFU_SUPPORT_FORMAT_RAR
#define XFU_SUPPORT_FORMAT_RAR 1
#endif
#ifndef XFU_SUPPORT_FORMAT_CAB
#define XFU_SUPPORT_FORMAT_CAB 1
#endif
#ifndef XFU_SUPPORT_FORMAT_ISO9660
#define XFU_SUPPORT_FORMAT_ISO9660 1
#endif
#endif

#if defined(XFU_SUPPORT_FORMAT_ALL) || defined(XFU_SUPPORT_FORMAT_DEFAULT)
#ifndef XFU_SUPPORT_FORMAT_TAR
#define XFU_SUPPORT_FORMAT_TAR 1
#endif
#ifndef XFU_SUPPORT_FORMAT_GZ
#define XFU_SUPPORT_FORMAT_GZ 1
#endif
#ifndef XFU_SUPPORT_FORMAT_BZ2
#define XFU_SUPPORT_FORMAT_BZ2 1
#endif
#ifndef XFU_SUPPORT_FORMAT_XZ
#define XFU_SUPPORT_FORMAT_XZ 1
#endif
#ifndef XFU_SUPPORT_FORMAT_ZSTD
#define XFU_SUPPORT_FORMAT_ZSTD 1
#endif
#ifndef XFU_SUPPORT_FORMAT_LZ4
#define XFU_SUPPORT_FORMAT_LZ4 1
#endif
#ifndef XFU_SUPPORT_FORMAT_CPIO
#define XFU_SUPPORT_FORMAT_CPIO 1
#endif
#ifndef XFU_SUPPORT_FORMAT_AR
#define XFU_SUPPORT_FORMAT_AR 1
#endif
#endif

/* ALL includes ZIP, so testing ZIP's presence cannot select a restricted
 * code path. Use this normalized predicate for the complete engine/registry. */
#ifdef XFU_SUPPORT_FORMAT_ALL
#define XFU_FORMAT_SUPPORT_FULL 1
#else
#define XFU_FORMAT_SUPPORT_FULL 0
#endif

#if defined(XFU_SUPPORT_FORMAT_AR) && !XFU_FORMAT_SUPPORT_FULL && \
    !defined(XFU_SUPPORT_FORMAT_ZIP) && !defined(XFU_SUPPORT_FORMAT_TAR) && \
    !defined(XFU_SUPPORT_FORMAT_7ZIP) && !defined(XFU_SUPPORT_FORMAT_RAR) && \
    !defined(XFU_SUPPORT_FORMAT_CAB) && !defined(XFU_SUPPORT_FORMAT_ISO9660) && \
    !defined(XFU_SUPPORT_FORMAT_CPIO)
#define XFU_FORMAT_SUPPORT_AR_ONLY 1
#else
#define XFU_FORMAT_SUPPORT_AR_ONLY 0
#endif

/* The dedicated TAR component retains the complete historical filter set.
 * A custom plain-TAR selection does not acquire these codec dependencies. */
#if defined(XFU_SUPPORT_FORMAT_TAR) && !XFU_FORMAT_SUPPORT_FULL && \
    defined(XFU_SUPPORT_FORMAT_GZ) && defined(XFU_SUPPORT_FORMAT_BZ2) && \
    defined(XFU_SUPPORT_FORMAT_XZ) && defined(XFU_SUPPORT_FORMAT_ZSTD) && \
    defined(XFU_SUPPORT_FORMAT_LZ4) && defined(XFU_SUPPORT_FORMAT_LZMA) && \
    defined(XFU_SUPPORT_FORMAT_LZIP) && defined(XFU_SUPPORT_FORMAT_LZOP) && \
    defined(XFU_SUPPORT_FORMAT_UNIX_COMPRESS) && defined(XFU_SUPPORT_FORMAT_UUE) && \
    defined(XFU_SUPPORT_FORMAT_BASE64) && \
    !defined(XFU_SUPPORT_FORMAT_ZIP) && !defined(XFU_SUPPORT_FORMAT_7ZIP) && \
    !defined(XFU_SUPPORT_FORMAT_RAR) && !defined(XFU_SUPPORT_FORMAT_CAB) && \
    !defined(XFU_SUPPORT_FORMAT_ISO9660) && !defined(XFU_SUPPORT_FORMAT_AR) && \
    !defined(XFU_SUPPORT_FORMAT_CPIO)
#define XFU_FORMAT_SUPPORT_TAR_FILTERS 1
#else
#define XFU_FORMAT_SUPPORT_TAR_FILTERS 0
#endif

/* One-file stream families: `a` writes --format=raw behind their filter. */
#if defined(XFU_SUPPORT_FORMAT_GZ) || defined(XFU_SUPPORT_FORMAT_BZ2) || \
    defined(XFU_SUPPORT_FORMAT_XZ) || defined(XFU_SUPPORT_FORMAT_ZSTD) || \
    defined(XFU_SUPPORT_FORMAT_LZMA) || defined(XFU_SUPPORT_FORMAT_LZ4) || \
    defined(XFU_SUPPORT_FORMAT_LZIP) || defined(XFU_SUPPORT_FORMAT_LZOP) || \
    defined(XFU_SUPPORT_FORMAT_UNIX_COMPRESS) || defined(XFU_SUPPORT_FORMAT_UUE) || \
    defined(XFU_SUPPORT_FORMAT_BASE64)
#define XFU_FORMAT_SUPPORT_RAW_STREAMS 1
#else
#define XFU_FORMAT_SUPPORT_RAW_STREAMS 0
#endif

/* The one-file encoders of native_filter_formats.c (LZ4, lzip, lzop,
 * compress, uuencode, Base64): the dedicated TAR component has them all; a
 * single-family build links that file and sets XFU_NATIVE_FILTER_WRITERS,
 * and gets the encoders of its own flags. */
#if XFU_FORMAT_SUPPORT_TAR_FILTERS || defined(XFU_NATIVE_FILTER_WRITERS)
#define XFU_FORMAT_SUPPORT_EXTRA_FILTERS 1
#else
#define XFU_FORMAT_SUPPORT_EXTRA_FILTERS 0
#endif

#if defined(XFU_SUPPORT_FORMAT_TAR) || defined(XFU_SUPPORT_FORMAT_7ZIP) || \
    defined(XFU_SUPPORT_FORMAT_CPIO) || XFU_FORMAT_SUPPORT_RAW_STREAMS
#define XFU_FORMAT_SUPPORT_COMMON_WRITERS 1
#else
#define XFU_FORMAT_SUPPORT_COMMON_WRITERS 0
#endif

#endif
