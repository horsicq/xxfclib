/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/jar/xx_jar.h"
#include "xxfclib/memory/xx_memory.h"

#define XX_JAR_MANIFEST_NAME "META-INF/MANIFEST.MF"
#define XX_JAR_MANIFEST_LIMIT (UINT64_C(16) * UINT64_C(1024) * UINT64_C(1024))

static void xx_jar_apply_identity(Abstractformat *format) {
    if (!format) {
        return;
    }
    format->file_type = XX_FILE_TYPE_JAR;
    format->format_type = XX_TYPE_PACKAGE;
    format->os = XX_OS_GENERIC;
    format->is_archive = true;
    format->is_executable = false;
    xx_format_set_mime_type(format, "application/java-archive");
    xx_format_set_extension(format, "jar");
}

static void xx_jar_vtable_destroy(Abstractformat *self) {
    if (self) {
        xx_jar_destroy((xx_jar *)self);
    }
}

void xx_jar_init(xx_jar *jar, xx_io_device *dev, int64_t base_address) {
    Abstractformat *format;

    if (!jar) {
        return;
    }
    xx_mem_zero(jar, sizeof(*jar));
    xx_zip_init(&jar->zip, dev, base_address);
    format = &jar->zip.format;
    xx_jar_apply_identity(format);

    /* All remaining callbacks stay wired directly to xx_zip.c. */
    format->check_is_valid = xx_jar_check_is_valid;
    format->handle_base_info = xx_jar_handle_base_info;
    format->destroy = xx_jar_vtable_destroy;
}

xx_jar *xx_jar_create(xx_io_device *dev, int64_t base_address) {
    xx_jar *jar = (xx_jar *)xx_mem_alloc(sizeof(xx_jar));
    if (!jar) {
        return NULL;
    }
    xx_jar_init(jar, dev, base_address);
    return jar;
}

void xx_jar_destroy(xx_jar *jar) {
    if (jar) {
        xx_zip_destroy(&jar->zip);
    }
}

void xx_jar_free(xx_jar *jar) {
    if (!jar) {
        return;
    }
    xx_jar_destroy(jar);
    xx_mem_free(jar);
}

bool xx_jar_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    return xx_zip_has_valid_file(self, XX_JAR_MANIFEST_NAME,
                                 XX_JAR_MANIFEST_LIMIT, pd);
}

bool xx_jar_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    bool result;

    if (!self) {
        return false;
    }
    result = xx_zip_handle_base_info(self, pd);
    if (result) {
        result = xx_zip_has_valid_file(self, XX_JAR_MANIFEST_NAME,
                                       XX_JAR_MANIFEST_LIMIT, pd);
    }
    xx_jar_apply_identity(self);
    self->is_valid = result;
    return result;
}
