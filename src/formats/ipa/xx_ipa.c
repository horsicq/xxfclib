/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/ipa/xx_ipa.h"
#include "xxfclib/memory/xx_memory.h"

#define XX_IPA_INFO_PREFIX "Payload/"
#define XX_IPA_INFO_SUFFIX ".app/Info.plist"
#define XX_IPA_INFO_LIMIT (UINT64_C(16) * UINT64_C(1024) * UINT64_C(1024))

static void xx_ipa_apply_identity(Abstractformat *format) {
    if (!format) {
        return;
    }
    format->file_type = XX_FILE_TYPE_IPA;
    format->format_type = XX_TYPE_PACKAGE;
    format->os = XX_OS_IOS;
    format->is_archive = true;
    format->is_executable = false;
    xx_format_set_mime_type(format, "application/x-itunes-ipa");
    xx_format_set_extension(format, "ipa");
}

static void xx_ipa_vtable_destroy(Abstractformat *self) {
    if (self) {
        xx_ipa_destroy((xx_ipa *)self);
    }
}

void xx_ipa_init(xx_ipa *ipa, xx_io_device *dev, int64_t base_address) {
    Abstractformat *format;

    if (!ipa) {
        return;
    }
    xx_mem_zero(ipa, sizeof(*ipa));
    xx_zip_init(&ipa->zip, dev, base_address);
    format = &ipa->zip.format;
    xx_ipa_apply_identity(format);

    /* All remaining callbacks stay wired directly to xx_zip.c. */
    format->check_is_valid = xx_ipa_check_is_valid;
    format->handle_base_info = xx_ipa_handle_base_info;
    format->destroy = xx_ipa_vtable_destroy;
}

xx_ipa *xx_ipa_create(xx_io_device *dev, int64_t base_address) {
    xx_ipa *ipa = (xx_ipa *)xx_mem_alloc(sizeof(*ipa));
    if (!ipa) {
        return NULL;
    }
    xx_ipa_init(ipa, dev, base_address);
    return ipa;
}

void xx_ipa_destroy(xx_ipa *ipa) {
    if (ipa) {
        xx_zip_destroy(&ipa->zip);
    }
}

void xx_ipa_free(xx_ipa *ipa) {
    if (!ipa) {
        return;
    }
    xx_ipa_destroy(ipa);
    xx_mem_free(ipa);
}

bool xx_ipa_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    return xx_zip_has_valid_file_pattern(
        self, XX_IPA_INFO_PREFIX, XX_IPA_INFO_SUFFIX, true,
        XX_IPA_INFO_LIMIT, pd);
}

bool xx_ipa_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    bool result;

    if (!self) {
        return false;
    }
    result = xx_zip_handle_base_info(self, pd);
    if (result) {
        result = xx_ipa_check_is_valid(self, pd);
    }
    xx_ipa_apply_identity(self);
    self->is_valid = result;
    return result;
}
