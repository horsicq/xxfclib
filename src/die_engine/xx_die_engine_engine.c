/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_die_engine_engine.h"
#include "xxfclib/rt/xx_rt.h"
#include <limits.h>

static xx_scan_result *die_scan_device(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd)
{
    xx_scan_options local;
    xx_io_device *view;
    xx_scan_result *result;
    XFileType type;
    int64_t total;
    int64_t size;
    int i;
    bool success;
    if (!engine || !engine->priv || !device) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Missing DIE engine, database or input");
        return NULL;
    }
    if (xx_pd_is_stopped(pd)) return NULL;
    if (options) local = *options;
    else xx_scan_options_init(&local);
    if (local.recursive_scan || local.first_wrapper_only) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Unsupported DIE scan mode");
        return NULL;
    }
    if (local.all_types_scan || local.overlay_scan || local.resources_scan || local.archives_scan) return xx_scan_device(engine, device, &local, pd);
    type = die_engine_xft_from_native(local.file_type);
    if (local.file_type != XX_FILE_TYPE_UNKNOWN && type == XFT_UNKNOWN) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Unsupported DIE file type");
        return NULL;
    }
    total = xx_io_total_size(device);
    if (local.offset < 0 || local.size < -1 || (local.size >= 0 && local.offset > INT64_MAX - local.size)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid DIE scan range");
        return NULL;
    }
    if (total < 0) {
        xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot determine DIE input size");
        return NULL;
    }
    size = local.size < 0 ? total - local.offset : local.size;
    if (local.offset > total || size > total - local.offset) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "DIE scan range exceeds input");
        return NULL;
    }
    view = xx_io_sub_open_ro(device, local.offset, size);
    if (!view) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create DIE scan input view");
        return NULL;
    }
    result = (xx_scan_result *)xx_rt_calloc(1, sizeof(*result));
    if (!result) {
        xx_io_close(view);
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate DIE scan result");
        return NULL;
    }
    success = die_engine_scan_pass_device(view, (DBase *)engine->priv, &local, type, result, pd) != 0;
    xx_io_close(view);
    if (!success || xx_pd_is_stopped(pd) || (pd && pd->last_error)) {
        if (!xx_pd_is_stopped(pd) && (!pd || !pd->last_error)) xx_pd_set_error(pd, XXFC_ERR_IO, "DIE signature scan failed");
        xx_scan_free_result(engine, result);
        return NULL;
    }
    for (i = 0; i < result->nCount; ++i) {
        xx_scan_record *record = &result->pRecords[i];
        if (record->offset < 0 || record->offset > INT64_MAX - local.offset) {
            xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "DIE record range overflows");
            xx_scan_free_result(engine, result);
            return NULL;
        }
        record->offset += local.offset;
    }
    return result;
}

#define DIE_SCAN_CALLBACK(name, native_type) \
    static xx_scan_result *die_scan_##name(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd) \
    { \
        xx_scan_options local; \
        if (options) local = *options; else xx_scan_options_init(&local); \
        local.file_type = native_type; \
        return die_scan_device(engine, device, &local, pd); \
    }

DIE_SCAN_CALLBACK(binary, XX_FILE_TYPE_BINARY)
DIE_SCAN_CALLBACK(msdos, XX_FILE_TYPE_MSDOS)
DIE_SCAN_CALLBACK(machofat, XX_FILE_TYPE_MACHOFAT)
DIE_SCAN_CALLBACK(java_class, XX_FILE_TYPE_JAVA_CLASS)
DIE_SCAN_CALLBACK(zip, XX_FILE_TYPE_ZIP)
DIE_SCAN_CALLBACK(jar, XX_FILE_TYPE_JAR)
DIE_SCAN_CALLBACK(npm, XX_FILE_TYPE_NPM)
DIE_SCAN_CALLBACK(ipa, XX_FILE_TYPE_IPA)
DIE_SCAN_CALLBACK(iso9660, XX_FILE_TYPE_ISO9660)
DIE_SCAN_CALLBACK(apk, XX_FILE_TYPE_APK)
DIE_SCAN_CALLBACK(amigahunk, XX_FILE_TYPE_AMIGAHUNK)
DIE_SCAN_CALLBACK(atarist, XX_FILE_TYPE_ATARIST)
DIE_SCAN_CALLBACK(cfbf, XX_FILE_TYPE_CFBF)
DIE_SCAN_CALLBACK(com, XX_FILE_TYPE_COM)
DIE_SCAN_CALLBACK(dex, XX_FILE_TYPE_DEX)
DIE_SCAN_CALLBACK(dos16m, XX_FILE_TYPE_DOS16M)
DIE_SCAN_CALLBACK(dos4g, XX_FILE_TYPE_DOS4G)
DIE_SCAN_CALLBACK(jpeg, XX_FILE_TYPE_JPEG)
DIE_SCAN_CALLBACK(png, XX_FILE_TYPE_PNG)
DIE_SCAN_CALLBACK(pdf, XX_FILE_TYPE_PDF)
DIE_SCAN_CALLBACK(ne, XX_FILE_TYPE_NE)
DIE_SCAN_CALLBACK(le, XX_FILE_TYPE_LE)
DIE_SCAN_CALLBACK(lx, XX_FILE_TYPE_LX)
DIE_SCAN_CALLBACK(rar, XX_FILE_TYPE_RAR)

#undef DIE_SCAN_CALLBACK

#define DIE_SCAN_WIDTH_CALLBACK(name, broad, narrow, wide, extra) \
    static xx_scan_result *die_scan_##name(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd) \
    { \
        xx_scan_options local; \
        if (options) local = *options; else xx_scan_options_init(&local); \
        if (local.file_type != narrow && local.file_type != wide && local.file_type != extra) local.file_type = broad; \
        return die_scan_device(engine, device, &local, pd); \
    }

DIE_SCAN_WIDTH_CALLBACK(pe, XX_FILE_TYPE_PE, XX_FILE_TYPE_PE32, XX_FILE_TYPE_PE64, XX_FILE_TYPE_DOTNET)
DIE_SCAN_WIDTH_CALLBACK(elf, XX_FILE_TYPE_ELF, XX_FILE_TYPE_ELF32, XX_FILE_TYPE_ELF64, XX_FILE_TYPE_ELF)
DIE_SCAN_WIDTH_CALLBACK(macho, XX_FILE_TYPE_MACHO, XX_FILE_TYPE_MACHO32, XX_FILE_TYPE_MACHO64, XX_FILE_TYPE_MACHO)

#undef DIE_SCAN_WIDTH_CALLBACK

bool die_engine_init(xx_scan_engine *engine, DBase *database)
{
    if (!engine) return false;
    xx_rt_memset(engine, 0, sizeof(*engine));
    if (!database) return false;
    engine->priv = database;
    engine->name = "Detect It Easy";
    engine->scan_device = die_scan_device;
    engine->get_record_count = xx_scan_get_record_count;
    engine->get_record = xx_scan_get_record;
    engine->free_result = xx_scan_free_result;
    engine->cleanup = xx_scan_engine_cleanup;
    engine->get_file_types = xx_scan_get_file_types;
    engine->get_overlay = xx_scan_get_overlay;
    engine->append_result = xx_scan_append_result;
    engine->scan_binary = die_scan_binary;
    engine->scan_msdos = die_scan_msdos;
    engine->scan_pe = die_scan_pe;
    engine->scan_elf = die_scan_elf;
    engine->scan_macho = die_scan_macho;
    engine->scan_machofat = die_scan_machofat;
    engine->scan_java_class = die_scan_java_class;
    engine->scan_zip = die_scan_zip;
    engine->scan_jar = die_scan_jar;
    engine->scan_npm = die_scan_npm;
    engine->scan_ipa = die_scan_ipa;
    engine->scan_iso9660 = die_scan_iso9660;
    engine->scan_apk = die_scan_apk;
    engine->scan_amigahunk = die_scan_amigahunk;
    engine->scan_atarist = die_scan_atarist;
    engine->scan_cfbf = die_scan_cfbf;
    engine->scan_com = die_scan_com;
    engine->scan_dex = die_scan_dex;
    engine->scan_dos16m = die_scan_dos16m;
    engine->scan_dos4g = die_scan_dos4g;
    engine->scan_jpeg = die_scan_jpeg;
    engine->scan_png = die_scan_png;
    engine->scan_pdf = die_scan_pdf;
    engine->scan_ne = die_scan_ne;
    engine->scan_le = die_scan_le;
    engine->scan_lx = die_scan_lx;
    engine->scan_rar = die_scan_rar;
    return true;
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
        /* DIE has no dedicated signature directories for these containers. */
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
