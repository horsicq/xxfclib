/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "supported_types.h"
#include "native_writer_backend.h"
#include <xxfclib/formats/xx_format.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef XFU_XXFCLIB_ONLY
#define XFU_CODEC_READ "no"
#define XFU_ENGINE_READ "no"
#elif defined(XFU_NATIVE_CODEC_LIBRARIES)
#define XFU_CODEC_READ "partial"
#define XFU_ENGINE_READ "helper"
#else
#define XFU_CODEC_READ "helper"
#define XFU_ENGINE_READ "helper"
#endif

char *xfu_capabilities_text(void)
{
    static const char native[] =
        "\nPeaZip comparison: native and in-process library operations\n"
        "read=partial means a documented subset; read=no is unavailable.\n"
        "External process engines are disabled. See docs/PEAZIP_FORMATS.md for limits.\n"
        "native 7zip read=partial create=yes\n"
        "native zip read=partial create=yes\n"
        "native tar read=yes create=yes\n"
        "native wim read=partial create=yes\n"
        "native gzip read=yes create=yes\n"
        "native bzip2 read=yes create=yes\n"
        "native xz read=yes create=yes\n"
        "native zstd read=yes create=yes\n"
        "native brotli read=yes create=no\n"
        "native zpaq read=partial create=no\n"
        "native pea read=partial create=yes\n"
        "native freearc read=partial create=yes\n"
        "native paq8 read=" XFU_CODEC_READ
        " create=no\n"
        "native paq8f read=" XFU_CODEC_READ
        " create=no\n"
        "native paq8jd read=" XFU_CODEC_READ
        " create=no\n"
        "native paq8l read=" XFU_CODEC_READ
        " create=no\n"
        "native paq8o read=" XFU_CODEC_READ
        " create=no\n"
        "native lpaq1 read=" XFU_CODEC_READ
        " create=no\n"
        "native lpaq5 read=" XFU_CODEC_READ
        " create=no\n"
        "native lpaq8 read=" XFU_CODEC_READ
        " create=no\n"
        "native quad read=" XFU_CODEC_READ
        " create=no\n"
        "native balz read=partial create=no\n"
        "native bcm read=partial create=no\n"
        "native split read=partial create=yes\n"
        "native sevenzip_engine read=" XFU_ENGINE_READ
        " create=no\n"
        "native upx read=" XFU_CODEC_READ
        " create=no\n"
        "native zim read=partial create=no\n"
        "native rar read=partial create=no\n"
        "native ace read=partial create=no\n"
#ifdef XFU_HAVE_7ZIP_SFX
        "native 7zip_sfx read=partial create=yes\n"
#else
        "native 7zip_sfx read=partial create=no\n"
#endif
#ifdef XFU_HAVE_FREEARC_SFX
        "native freearc_sfx read=partial create=yes\n";
#else
        "native freearc_sfx read=partial create=no\n";
#endif
    char *compatibility = xfu_native_capabilities_text();
    char *result;
    size_t size;
    if (!compatibility) return NULL;
    size = strlen(compatibility);
    if (size > SIZE_MAX - sizeof(native)) {
        free(compatibility);
        return NULL;
    }
    result = (char *)malloc(size + sizeof(native));
    if (result) {
        memcpy(result, compatibility, size);
        memcpy(result + size, native, sizeof(native));
    }
    free(compatibility);
    return result;
}

char *xfu_supported_types_text(size_t *count)
{
    xx_list_t *types = xx_format_get_supported_file_types();
    size_t i, length = 4096, used, total;
    char *text = NULL;
    char *capabilities = NULL;
    if (count) *count = 0;
    if (!types) return NULL;
    capabilities = xfu_capabilities_text();
    if (!capabilities) goto finish;
    if (strlen(capabilities) > SIZE_MAX - length) goto finish;
    length += strlen(capabilities);
    total = xx_list_count(types);
    for (i = 0; i < total; ++i) {
        xx_file_type_t type = *(const xx_file_type_t *)xx_list_at(types, i);
        size_t add = strlen(xx_format_file_type_to_string(type)) + 32;
        if (length > SIZE_MAX - add) goto finish;
        length += add;
    }
    text = (char *)malloc(length);
    if (!text) goto finish;
    used = (size_t)snprintf(text, length,
                            "Supported file types (xxfclib): %zu\n"
                            "Detection/inspection formats.\n"
                            "Extraction and writing depend on the format.\n"
                            "\nArchive creation: 7z, ZIP, TAR, GZIP, BZIP2, XZ, stored WIM;\n"
                            "CPIO and compressed TAR (native).\n"
                            "In-process creation also includes PEA, stored/LZMA FreeArc\n"
                            "and numbered split volumes. Formats without writers report create=no;\n"
                            "helper-only readers report read=no.\n"
                            "Raw compressed or encoded streams hold one input file.\n"
                            "External 7-Zip, media, GARbro, UPX, UHARC, filesystem and codec\n"
                            "helper processes are disabled in this build.\n"
                            "Documents: native PDF streams and attachments;\n"
                            "MIME bodies and attachments;\n"
                            "gettext MO to PO; Qt QM to TS;\n"
                            "SQLite SQL export (sqlite3 keeps raw pages).\n"
                            "Native Python bytecode reader and PYC carving (--reader pyc).\n"
                            "\n"
                            "%s\n"
                            "   ID  File type\n",
                            total, capabilities);
    for (i = 0; i < total; ++i) {
        xx_file_type_t type = *(const xx_file_type_t *)xx_list_at(types, i);
        used += (size_t)snprintf(text + used, length - used, "%5u  %s\n", (unsigned int)type, xx_format_file_type_to_string(type));
    }
    if (count) *count = total;
finish:
    free(capabilities);
    xx_list_destroy(types);
    return text;
}
