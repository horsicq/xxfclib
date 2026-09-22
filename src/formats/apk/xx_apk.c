/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/apk/xx_apk.h"
#include "xxfclib/memory/xx_memory.h"

#define XX_APK_MANIFEST_NAME "AndroidManifest.xml"
#define XX_APK_MANIFEST_LIMIT (UINT64_C(16) * UINT64_C(1024) * UINT64_C(1024))

static void xx_apk_apply_identity(Abstractformat *format) {
    if (!format) {
        return;
    }
    format->file_type = XX_FILE_TYPE_APK;
    format->format_type = XX_TYPE_PACKAGE;
    format->os = XX_OS_ANDROID;
    format->is_archive = true;
    format->is_executable = false;
    xx_format_set_mime_type(format, "application/vnd.android.package-archive");
    xx_format_set_extension(format, "apk");
}

static void xx_apk_vtable_destroy(Abstractformat *self) {
    if (self) {
        xx_apk_destroy((xx_apk *)self);
    }
}

void xx_apk_init(xx_apk *apk, xx_io_device *dev, int64_t base_address) {
    Abstractformat *format;

    if (!apk) {
        return;
    }
    xx_mem_zero(apk, sizeof(*apk));
    xx_zip_init(&apk->zip, dev, base_address);
    format = &apk->zip.format;
    xx_apk_apply_identity(format);

    /* All remaining callbacks stay wired directly to xx_zip.c. */
    format->check_is_valid = xx_apk_check_is_valid;
    format->handle_base_info = xx_apk_handle_base_info;
    format->destroy = xx_apk_vtable_destroy;
}

xx_apk *xx_apk_create(xx_io_device *dev, int64_t base_address) {
    xx_apk *apk = (xx_apk *)xx_mem_alloc(sizeof(xx_apk));
    if (!apk) {
        return NULL;
    }
    xx_apk_init(apk, dev, base_address);
    return apk;
}

void xx_apk_destroy(xx_apk *apk) {
    if (apk) {
        xx_zip_destroy(&apk->zip);
    }
}

void xx_apk_free(xx_apk *apk) {
    if (!apk) {
        return;
    }
    xx_apk_destroy(apk);
    xx_mem_free(apk);
}

bool xx_apk_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    return xx_zip_has_valid_file(self, XX_APK_MANIFEST_NAME,
                                 XX_APK_MANIFEST_LIMIT, pd);
}

bool xx_apk_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    bool result;

    if (!self) {
        return false;
    }
    result = xx_zip_handle_base_info(self, pd);
    if (result) {
        result = xx_zip_has_valid_file(self, XX_APK_MANIFEST_NAME,
                                       XX_APK_MANIFEST_LIMIT, pd);
    }
    xx_apk_apply_identity(self);
    self->is_valid = result;
    return result;
}
