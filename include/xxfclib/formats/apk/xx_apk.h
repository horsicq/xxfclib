/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_apk.h
 * @brief Android package format implemented as a ZIP-derived format.
 */

#ifndef XXFCLIB_FORMAT_APK_H
#define XXFCLIB_FORMAT_APK_H

#include "xxfclib/formats/zip/xx_zip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_apk xx_apk;
typedef struct xx_apk xx_apk_t;

typedef struct xx_apk_native_library_info {
    const char *entry_name; /* lib/<abi>/<name> */
    const char *abi;
    const char *name; /* Exact filename, e.g. libzjni.so. */
} xx_apk_native_library_info;

/**
 * @brief APK format object.
 *
 * The embedded xx_zip object must remain first: all inherited archive,
 * packing, extraction, and data-structure callbacks operate on it directly.
 */
struct xx_apk {
    xx_zip zip;
    char *manifest_text;
    char *package_name;
    char *launcher_activity;
    void *dex_analysis;
    void *native_analysis;
};

XXFC_API void xx_apk_init(xx_apk *apk, xx_io_device *dev, int64_t base_address);
XXFC_API xx_apk *xx_apk_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_apk_free(xx_apk *apk);
XXFC_API void xx_apk_destroy(xx_apk *apk);

XXFC_API bool xx_apk_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_apk_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_apk_analyze(xx_apk *apk, xx_pd_struct *pd);
XXFC_API const char *xx_apk_get_manifest(const xx_apk *apk);
XXFC_API char *xx_apk_manifest_record(const xx_apk *apk, const char *key);
/* Manifest metadata is prepared by xx_apk_analyze. Returned UTF-8 strings
 * are borrowed until destruction; empty strings mean unavailable metadata.
 * Incomplete legacy XML trees can have decoded text but no metadata.
 * Android attributes are identified by namespace URI. Launcher matching
 * requires MAIN and LAUNCHER in the same closed intent-filter; aliases use
 * their targetActivity. Relative names are expanded against the package. */
XXFC_API const char *xx_apk_get_package_name(const xx_apk *apk);
XXFC_API const char *xx_apk_get_launcher_activity(const xx_apk *apk);
/* Cache up to 128 root classes.dex/classesN.dex names in numeric order.
 * N is >=2 without leading zeroes. Duplicate numbers fail atomically.
 * Resource-only APKs and archives missing classes.dex may have an inventory;
 * requiring the primary DEX is the consumer's policy. Inventory names are
 * owned independently of the inherited ZIP inspection cache, until APK
 * destruction. Before analysis count is zero; an invalid index returns NULL. */
XXFC_API bool xx_apk_analyze_dex(xx_apk *apk, xx_pd_struct *pd);
XXFC_API size_t xx_apk_get_dex_count(const xx_apk *apk);
XXFC_API const char *xx_apk_get_dex_name(const xx_apk *apk, size_t index);
/* Automatically prepares inventory, then verifies decoded size and ZIP CRC.
 * Output data is owned: release with xx_mem_free. Both packed and unpacked
 * sizes must fit limit. Outputs are NULL/0 on failure; no archive path opens. */
XXFC_API bool xx_apk_read_dex(xx_apk *apk, size_t index, size_t limit, uint8_t **data, size_t *size, xx_pd_struct *pd);

/* Inventory portable lib/<abi>/lib<name>.so members in ABI/name order.
 * ABI components contain ASCII letters/digits/_/- (up to 63 bytes); library
 * filenames additionally permit dots (up to 255 bytes). Other paths are
 * ignored. Up to 1024 libraries and 64 ABIs are cached; duplicate member
 * paths fail atomically. Empty inventories are valid. Strings and records
 * are borrowed until APK destruction, independently of ZIP cache cleanup. */
XXFC_API bool xx_apk_analyze_native_libraries(xx_apk *apk, xx_pd_struct *pd);
XXFC_API size_t xx_apk_get_native_library_count(const xx_apk *apk);
XXFC_API const xx_apk_native_library_info *xx_apk_get_native_library(const xx_apk *apk, size_t index);
XXFC_API size_t xx_apk_get_native_abi_count(const xx_apk *apk);
XXFC_API const char *xx_apk_get_native_abi(const xx_apk *apk, size_t index);
/* Exact ABI and filename lookup; never opens an archive-supplied path.
 * Auto-analyzes inventory and verifies ZIP decoded size/CRC. Both packed and
 * unpacked sizes must fit limit. Outputs are NULL/0 on failure; owned data
 * is released with xx_mem_free. This extracts bytes, without loading ELF. */
XXFC_API bool xx_apk_read_native_library(xx_apk *apk, const char *abi, const char *name, size_t limit, uint8_t **data, size_t *size, xx_pd_struct *pd);

static inline Abstractformat *xx_apk_to_format(xx_apk *apk)
{
    return apk ? &apk->zip.format : NULL;
}

static inline const Abstractformat *xx_apk_to_format_const(const xx_apk *apk)
{
    return apk ? &apk->zip.format : NULL;
}

static inline xx_zip *xx_apk_to_zip(xx_apk *apk)
{
    return apk ? &apk->zip : NULL;
}

static inline const xx_zip *xx_apk_to_zip_const(const xx_apk *apk)
{
    return apk ? &apk->zip : NULL;
}

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_apk_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_apk_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_apk_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_apk_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_apk_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_APK_H */
