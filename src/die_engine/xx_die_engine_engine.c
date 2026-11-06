/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_die_engine_engine.h"
#include "xxfclib/rt/xx_rt.h"

static bool die_engine_create(xx_scan_engine *engine, const void *config, xx_pd_struct *pd)
{
    (void)config;
    (void)pd;
    if (!engine) return false;
    engine->context = NULL;
    return true;
}

static void die_engine_destroy(xx_scan_engine *engine)
{
    if (engine) engine->context = NULL;
}

static bool die_engine_load_database(xx_scan_engine *engine, const void *database, xx_pd_struct *pd)
{
    if (!engine || !database) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Missing DIE engine or database");
        return false;
    }
    engine->context = (void *)database;
    return true;
}

static bool die_engine_supports_type(const xx_scan_engine *engine, xx_file_type_t type)
{
    (void)engine;
    return type != XX_FILE_TYPE_UNKNOWN && die_engine_xft_from_native(type) != XFT_UNKNOWN;
}

static xx_scan_result *die_engine_scan_region(xx_scan_engine *engine, const xx_scan_region *region,
                                              const xx_scan_options *options, xx_pd_struct *pd)
{
    xx_scan_options local;
    xx_scan_result *result;
    XFileType type;
    bool success;
    if (!engine || !engine->context || !region || !region->device) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Missing DIE engine, database or input");
        return NULL;
    }
    if (xx_pd_is_stopped(pd)) return NULL;
    if (options) local = *options;
    else xx_scan_options_init(&local);
    if (local.recursive_scan || local.first_wrapper_only || local.all_types_scan ||
        local.overlay_scan || local.resources_scan || local.archives_scan) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "DIE region received traversal options");
        return NULL;
    }
    type = die_engine_xft_from_native(region->file_type);
    if (type == XFT_UNKNOWN) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Unsupported DIE file type");
        return NULL;
    }
    local.offset = 0;
    local.size = region->size;
    local.file_type = region->file_type;
    local.scan_id = region->scan_id;
    local.parent_id = region->parent_id;
    local.scan_part = region->part;
    result = (xx_scan_result *)xx_rt_calloc(1, sizeof(*result));
    if (!result) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate DIE scan result");
        return NULL;
    }
    success = die_engine_scan_pass_device(region->device, (DBase *)engine->context, &local, type, result, pd) != 0;
    if (!success || xx_pd_is_stopped(pd) || (pd && pd->last_error)) {
        if (!xx_pd_is_stopped(pd) && (!pd || !pd->last_error)) xx_pd_set_error(pd, XXFC_ERR_IO, "DIE signature scan failed");
        xx_scan_free_result(engine, result);
        return NULL;
    }
    result->engine_id = XX_SCAN_ENGINE_DIE;
    for (int i = 0; i < result->nCount; ++i) result->pRecords[i].engine_id = XX_SCAN_ENGINE_DIE;
    return result;
}

static bool die_engine_catalog_add(xx_scan_catalog *catalog, const char *text, xx_pd_struct *pd)
{
    char **items;
    char *copy;
    size_t length;
    if (!catalog || !text || catalog->count == SIZE_MAX / sizeof(*items)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid DIE catalog entry");
        return false;
    }
    length = xx_rt_strlen(text);
    copy = (char *)xx_rt_malloc(length + 1);
    if (!copy) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate DIE catalog entry");
        return false;
    }
    xx_rt_memcpy(copy, text, length + 1);
    items = (char **)xx_rt_realloc(catalog->items, (catalog->count + 1) * sizeof(*items));
    if (!items) {
        xx_rt_free(copy);
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate DIE catalog");
        return false;
    }
    catalog->items = items;
    catalog->items[catalog->count++] = copy;
    return true;
}

static bool die_engine_list_signatures(const xx_scan_engine *engine, xx_scan_catalog *catalog, xx_pd_struct *pd)
{
    const DBase *database;
    int i;
    if (!engine || !engine->context || !catalog) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Missing DIE signature catalog argument");
        return false;
    }
    xx_scan_catalog_free(catalog);
    database = (const DBase *)engine->context;
    for (i = 0; i < database->nCount; ++i) {
        if (!die_engine_catalog_add(catalog, database->pRecords[i].pName ? database->pRecords[i].pName : "", pd)) {
            xx_scan_catalog_free(catalog);
            return false;
        }
    }
    return true;
}

static bool die_engine_list_tags(const xx_scan_engine *engine, xx_scan_catalog *catalog, xx_pd_struct *pd)
{
    char *tags;
    char *current;
    if (!engine || !engine->context || !catalog) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Missing DIE tag catalog argument");
        return false;
    }
    xx_scan_catalog_free(catalog);
    tags = db_get_used_tags((const DBase *)engine->context);
    if (!tags) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate DIE tag catalog");
        return false;
    }
    current = tags;
    while (*current) {
        char *end = current;
        char saved;
        while (*end && *end != ';') ++end;
        saved = *end;
        *end = 0;
        if (!die_engine_catalog_add(catalog, current, pd)) {
            xx_rt_free(tags);
            xx_scan_catalog_free(catalog);
            return false;
        }
        if (!saved) break;
        current = end + 1;
    }
    xx_rt_free(tags);
    return true;
}

static const xx_scan_engine_ops die_ops = {
    XX_SCAN_ENGINE_ABI_VERSION,
    sizeof(xx_scan_engine_ops),
    XX_SCAN_ENGINE_DIE,
    "die",
    "Detect It Easy (DiE)",
    XX_SCAN_CAP_EXTERNAL_DATABASE_REQUIRED | XX_SCAN_CAP_TAGS | XX_SCAN_CAP_DEEP |
        XX_SCAN_CAP_AGGRESSIVE | XX_SCAN_CAP_VERBOSE | XX_SCAN_CAP_TYPE_QUERY |
        XX_SCAN_CAP_THREAD_SAFE | XX_SCAN_CAP_REENTRANT,
    die_engine_create,
    die_engine_destroy,
    die_engine_load_database,
    die_engine_supports_type,
    die_engine_scan_region,
    die_engine_list_signatures,
    die_engine_list_tags
};

const xx_scan_engine_ops *die_engine_get_ops(void)
{
    return &die_ops;
}

bool die_engine_init(xx_scan_engine *engine, DBase *database)
{
    return xx_scan_engine_init(engine, &die_ops, NULL, database, NULL);
}

xx_file_type_t die_engine_native_from_xft(XFileType type)
{
    switch (type) {
        case XFT_BINARY: return XX_FILE_TYPE_BINARY;
        case XFT_COM: return XX_FILE_TYPE_COM;
        case XFT_MSDOS: return XX_FILE_TYPE_MSDOS;
        case XFT_NE: return XX_FILE_TYPE_NE;
        case XFT_LE: return XX_FILE_TYPE_LE;
        case XFT_LX: return XX_FILE_TYPE_LX;
        case XFT_PE: return XX_FILE_TYPE_PE;
        case XFT_PE32: return XX_FILE_TYPE_PE32;
        case XFT_PE64: return XX_FILE_TYPE_PE64;
        case XFT_ELF: return XX_FILE_TYPE_ELF;
        case XFT_ELF32: return XX_FILE_TYPE_ELF32;
        case XFT_ELF64: return XX_FILE_TYPE_ELF64;
        case XFT_MACHO: return XX_FILE_TYPE_MACHO;
        case XFT_MACHO32: return XX_FILE_TYPE_MACHO32;
        case XFT_MACHO64: return XX_FILE_TYPE_MACHO64;
        case XFT_ZIP: return XX_FILE_TYPE_ZIP;
        case XFT_JAR: return XX_FILE_TYPE_JAR;
        case XFT_APK: return XX_FILE_TYPE_APK;
        case XFT_IPA: return XX_FILE_TYPE_IPA;
        case XFT_DEX: return XX_FILE_TYPE_DEX;
        case XFT_NPM: return XX_FILE_TYPE_NPM;
        case XFT_MACHOFAT: return XX_FILE_TYPE_MACHOFAT;
        case XFT_ARCHIVE: return XX_FILE_TYPE_ARCHIVE;
        case XFT_PDF: return XX_FILE_TYPE_PDF;
        case XFT_CFBF: return XX_FILE_TYPE_CFBF;
        case XFT_IMAGE: return XX_FILE_TYPE_IMAGE;
        case XFT_JPEG: return XX_FILE_TYPE_JPEG;
        case XFT_PNG: return XX_FILE_TYPE_PNG;
        case XFT_RAR: return XX_FILE_TYPE_RAR;
        case XFT_ISO9660: return XX_FILE_TYPE_ISO9660;
        case XFT_AMIGAHUNK: return XX_FILE_TYPE_AMIGAHUNK;
        case XFT_ATARIST: return XX_FILE_TYPE_ATARIST;
        case XFT_JAVACLASS: return XX_FILE_TYPE_JAVA_CLASS;
        case XFT_PYC: return XX_FILE_TYPE_PYC;
        case XFT_DOS16M: return XX_FILE_TYPE_DOS16M;
        case XFT_DOS4G: return XX_FILE_TYPE_DOS4G;
        case XFT_CLI_ASSEMBLY: return XX_FILE_TYPE_DOTNET;
        default: return XX_FILE_TYPE_UNKNOWN;
    }
}

XFileType die_engine_xft_from_native(xx_file_type_t type)
{
    switch (type) {
        case XX_FILE_TYPE_BINARY:
        case XX_FILE_TYPE_LHA:
        case XX_FILE_TYPE_GZ:
        case XX_FILE_TYPE_BZ2: return XFT_BINARY;
        case XX_FILE_TYPE_COM: return XFT_COM;
        case XX_FILE_TYPE_MSDOS: return XFT_MSDOS;
        case XX_FILE_TYPE_NE: return XFT_NE;
        case XX_FILE_TYPE_LE: return XFT_LE;
        case XX_FILE_TYPE_LX: return XFT_LX;
        case XX_FILE_TYPE_PE: return XFT_PE;
        case XX_FILE_TYPE_PE32: return XFT_PE32;
        case XX_FILE_TYPE_PE64: return XFT_PE64;
        case XX_FILE_TYPE_ELF: return XFT_ELF;
        case XX_FILE_TYPE_ELF32: return XFT_ELF32;
        case XX_FILE_TYPE_ELF64: return XFT_ELF64;
        case XX_FILE_TYPE_MACHO: return XFT_MACHO;
        case XX_FILE_TYPE_MACHO32: return XFT_MACHO32;
        case XX_FILE_TYPE_MACHO64: return XFT_MACHO64;
        case XX_FILE_TYPE_ZIP:
        case XX_FILE_TYPE_ZIP64: return XFT_ZIP;
        case XX_FILE_TYPE_JAR: return XFT_JAR;
        case XX_FILE_TYPE_APK: return XFT_APK;
        case XX_FILE_TYPE_IPA: return XFT_IPA;
        case XX_FILE_TYPE_DEX: return XFT_DEX;
        case XX_FILE_TYPE_NPM: return XFT_NPM;
        case XX_FILE_TYPE_MACHOFAT: return XFT_MACHOFAT;
        case XX_FILE_TYPE_ARCHIVE: return XFT_ARCHIVE;
        case XX_FILE_TYPE_PDF: return XFT_PDF;
        case XX_FILE_TYPE_CFBF: return XFT_CFBF;
        case XX_FILE_TYPE_IMAGE: return XFT_IMAGE;
        case XX_FILE_TYPE_JPEG: return XFT_JPEG;
        case XX_FILE_TYPE_PNG: return XFT_PNG;
        case XX_FILE_TYPE_RAR: return XFT_RAR;
        case XX_FILE_TYPE_ISO9660: return XFT_ISO9660;
        case XX_FILE_TYPE_AMIGAHUNK: return XFT_AMIGAHUNK;
        case XX_FILE_TYPE_ATARIST: return XFT_ATARIST;
        case XX_FILE_TYPE_JAVA_CLASS: return XFT_JAVACLASS;
        case XX_FILE_TYPE_PYC: return XFT_PYC;
        case XX_FILE_TYPE_DOS16M: return XFT_DOS16M;
        case XX_FILE_TYPE_DOS4G: return XFT_DOS4G;
        case XX_FILE_TYPE_DOTNET: return XFT_CLI_ASSEMBLY;
        default: return XFT_UNKNOWN;
    }
}
