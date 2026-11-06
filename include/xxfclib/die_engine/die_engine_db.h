/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file die_engine_db.h
 * @brief Detect It Easy database models and compatibility file types.
 *
 * This header is the compatibility home of the database API that used to be
 * declared by scan/xx_scan.h. New engine-neutral callers do not need it.
 */

#ifndef XX_DIE_ENGINE_DB_H
#define XX_DIE_ENGINE_DB_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    XFT_UNKNOWN = 0,
    XFT_BINARY,
    XFT_COM,
    XFT_MSDOS,
    XFT_NE,
    XFT_LE,
    XFT_LX,
    XFT_PE,
    XFT_PE32,
    XFT_PE64,
    XFT_ELF,
    XFT_ELF32,
    XFT_ELF64,
    XFT_MACHO,
    XFT_MACHO32,
    XFT_MACHO64,
    XFT_ZIP,
    XFT_JAR,
    XFT_APK,
    XFT_IPA,
    XFT_DEX,
    XFT_NPM,
    XFT_MACHOFAT,
    XFT_ARCHIVE,
    XFT_PDF,
    XFT_CFBF,
    XFT_IMAGE,
    XFT_JPEG,
    XFT_PNG,
    XFT_RAR,
    XFT_ISO9660,
    XFT_AMIGAHUNK,
    XFT_ATARIST,
    XFT_JAVACLASS,
    XFT_PYC,
    XFT_DOS16M,
    XFT_DOS4G,
    XFT_CLI_ASSEMBLY,
    XFT_COUNT
} XFileType;

XXFC_API const char *xft_to_string(XFileType type);
XXFC_API int xft_check(XFileType databaseType, XFileType fileType);

typedef enum {
    DB_MAIN = 0,
    DB_EXTRA,
    DB_CUSTOM
} DBKind;

typedef struct {
    char *pName;
    char *pFilePath;
    char *pText;
    size_t nSize;
    XFileType fileType;
    DBKind databaseType;
    int64_t nElapsedTime;
    char *pTags;
} DBSignature;

typedef struct {
    char *pName;
    char *pTags;
    XFileType fileType;
    unsigned int databaseKinds;
} DBSignatureMetadata;

typedef struct {
    DBSignature *pRecords;
    int nCount;
    int nCapacity;
    DBSignatureMetadata *pMetadata;
    int nMetadataCount;
    int nMetadataCapacity;
    void *pJsProgramCache;
} DBase;

XXFC_API int db_load(DBase *database, const char *path, DBKind kind);
XXFC_API int db_load_tar(DBase *database, const char *path, DBKind kind);
XXFC_API int db_load_builtin_audio(DBase *database, DBKind kind);
XXFC_API int db_load_builtin_music(DBase *database, DBKind kind);
XXFC_API int db_create_tar(const char *database_path, const char *tar_path);
XXFC_API int db_create_tar_precompiled(const char *database_path, const char *tar_path);
XXFC_API void db_sort(DBase *database);
XXFC_API void db_free(DBase *database);
XXFC_API int db_count_for_type(DBase *database, XFileType file_type);
XXFC_API int db_signature_has_tag(const DBSignature *signature, const char *tag);
XXFC_API int db_signature_matches_tag_filters(const DBSignature *signature,
                                               const char *include_tags,
                                               const char *exclude_tags);
XXFC_API char *db_get_used_tags(const DBase *database);

/** Legacy detected-type set retained for existing DiE API callers. */
typedef struct xx_scan_file_type_set {
    bool types[XFT_COUNT];
} xx_scan_file_type_set;

XXFC_API bool xx_scan_file_type_set_detect(xx_io_device *device, const char *file_name, xx_scan_file_type_set *set);
XXFC_API bool xx_scan_file_type_set_contains(const xx_scan_file_type_set *set, XFileType type);
XXFC_API XFileType xx_scan_pick_file_type(xx_io_device *device, const char *file_name, const xx_scan_file_type_set *set);

#ifdef __cplusplus
}
#endif

#endif /* XX_DIE_ENGINE_DB_H */
