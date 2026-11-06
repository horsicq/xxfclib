/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_types_internal.h"
#include "xxfclib/memory/xx_memory.h"


typedef struct xx_scan_type_window {
    xx_io_device view;
    xx_io_device *source;
    int64_t base;
    int64_t length; /* -1 when the source size is unknown. */
    int64_t position;
} xx_scan_type_window;

static ssize_t xx_scan_window_read(xx_io_device *view, void *buffer, size_t size)
{
    xx_scan_type_window *window = (xx_scan_type_window *)view->priv;
    ssize_t count;
    if (!window || (!buffer && size)) return -1;
    if (!size || (window->length >= 0 && window->position >= window->length)) return 0;
    if (size > (size_t)PTRDIFF_MAX) size = (size_t)PTRDIFF_MAX;
    if (window->length < 0) {
        int64_t remaining = INT64_MAX - window->base - window->position;
        if (remaining <= 0) return 0;
        if ((uint64_t)size > (uint64_t)remaining) size = (size_t)remaining;
    }
    if (window->length >= 0 && (uint64_t)size > (uint64_t)(window->length - window->position)) size = (size_t)(window->length - window->position);
    if (xx_io_seek64(window->source, window->base + window->position, SEEK_SET) != 0) return -1;
    count = xx_io_read(window->source, buffer, size);
    if (count > 0 && (uint64_t)count > (uint64_t)size) return -1;
    if (count > 0) window->position += count;
    return count;
}

static int xx_scan_window_seek64(xx_io_device *view, int64_t offset, int whence)
{
    xx_scan_type_window *window = (xx_scan_type_window *)view->priv;
    int64_t origin;
    int64_t maximum;
    if (!window) return -1;
    maximum = window->length >= 0 ? window->length : INT64_MAX - window->base;
    if (whence == SEEK_SET) origin = 0;
    else if (whence == SEEK_CUR) origin = window->position;
    else if (whence == SEEK_END && window->length >= 0) origin = window->length;
    else return -1;
    if (offset < -origin || offset > maximum - origin) return -1;
    window->position = origin + offset;
    return 0;
}

static int xx_scan_window_seek(xx_io_device *view, long offset, int whence)
{
    return xx_scan_window_seek64(view, (int64_t)offset, whence);
}

static int64_t xx_scan_window_size(xx_io_device *view)
{
    xx_scan_type_window *window = (xx_scan_type_window *)view->priv;
    return window ? window->length : -1;
}

static int64_t xx_scan_window_tell(xx_io_device *view)
{
    xx_scan_type_window *window = (xx_scan_type_window *)view->priv;
    return window ? window->position : -1;
}



static size_t xx_scan_read_head(xx_io_device *device, uint8_t head[64])
{
    size_t done = 0;
    if (xx_io_seek64(device, 0, SEEK_SET) != 0) return 0;
    while (done < 64U) {
        ssize_t count = xx_io_read(device, head + done, 64U - done);
        if (count <= 0) break;
        done += (size_t)count;
    }
    return done;
}



/* Keep this focused detector independent of the full format dispatcher. */
static xx_file_type_t xx_scan_parent_type(xx_file_type_t type)
{
    switch (type) {
        case XX_FILE_TYPE_PE32:
        case XX_FILE_TYPE_PE64:
        case XX_FILE_TYPE_DOTNET: return XX_FILE_TYPE_PE;
        case XX_FILE_TYPE_PE:
        case XX_FILE_TYPE_NE:
        case XX_FILE_TYPE_LE:
        case XX_FILE_TYPE_LX:
        case XX_FILE_TYPE_DOS16M:
        case XX_FILE_TYPE_DOS4G: return XX_FILE_TYPE_MSDOS;
        case XX_FILE_TYPE_ELF32:
        case XX_FILE_TYPE_ELF64: return XX_FILE_TYPE_ELF;
        case XX_FILE_TYPE_MACHO32:
        case XX_FILE_TYPE_MACHO64:
        case XX_FILE_TYPE_MACHOFAT: return XX_FILE_TYPE_MACHO;
        case XX_FILE_TYPE_APK: return XX_FILE_TYPE_JAR;
        case XX_FILE_TYPE_IPA:
        case XX_FILE_TYPE_JAR: return XX_FILE_TYPE_ZIP;
        case XX_FILE_TYPE_NPM: return XX_FILE_TYPE_TAR_GZ;
        case XX_FILE_TYPE_TAR_GZ: return XX_FILE_TYPE_GZ;
        case XX_FILE_TYPE_GZ:
        case XX_FILE_TYPE_LHA:
        case XX_FILE_TYPE_BZ2:
        case XX_FILE_TYPE_RAR: return XX_FILE_TYPE_ARCHIVE;
        case XX_FILE_TYPE_JPEG:
        case XX_FILE_TYPE_PNG: return XX_FILE_TYPE_IMAGE;
        case XX_FILE_TYPE_BINARY: return XX_FILE_TYPE_UNKNOWN;
        default: return XX_FILE_TYPE_BINARY;
    }
}

static xx_list_t *xx_scan_type_list(xx_file_type_t type, xx_pd_struct *pd)
{
    xx_file_type_t chain[8];
    size_t count = 0;
    xx_list_t *types = xx_list_create(sizeof(xx_file_type_t), NULL);
    if (!types) goto oom;
    while (type != XX_FILE_TYPE_UNKNOWN && count < sizeof(chain) / sizeof(chain[0])) {
        chain[count++] = type;
        type = xx_scan_parent_type(type);
    }
    while (count) {
        if (!xx_list_append(types, &chain[--count])) {
            xx_list_destroy(types);
            goto oom;
        }
    }
    return types;
oom:
    xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create scan file-type list");
    return NULL;
}


static xx_list_t *xx_scan_detect_type_window(xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd)
{
    uint8_t head[64] = {0};
    size_t head_size = xx_scan_read_head(device, head);
    int64_t size = xx_io_total_size(device);
    xx_file_type_t type;

    if (head_size >= 2U && (!memcmp(head, "MZ", 2) || !memcmp(head, "ZM", 2))) {
        bool managed = false;
        if (head_size < 64U) return xx_scan_type_list(XX_FILE_TYPE_MSDOS, pd);
        type = xx_scan_type_detect_pe(device, head, head_size, size, &managed, pd);
        if (type != XX_FILE_TYPE_UNKNOWN) {
            xx_list_t *types = xx_scan_type_list(type, pd);
            if (types && managed) {
                xx_file_type_t dotnet = XX_FILE_TYPE_DOTNET;
                if (!xx_list_append(types, &dotnet)) {
                    xx_list_destroy(types);
                    xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot append managed scan file type");
                    return NULL;
                }
            }
            return types;
        }
        type = xx_scan_type_detect_msdos(device);
        if (type != XX_FILE_TYPE_MSDOS) return xx_scan_type_list(type, pd);
        type = xx_scan_type_detect_ne_le_lx(device);
        return xx_scan_type_list(type != XX_FILE_TYPE_UNKNOWN ? type : XX_FILE_TYPE_MSDOS, pd);
    }

    type = xx_scan_type_detect_elf(head, head_size);
    if (type != XX_FILE_TYPE_UNKNOWN) return xx_scan_type_list(type, pd);
    if (xx_scan_type_is_macho_fat(device, head, head_size, size, pd)) return xx_scan_type_list(XX_FILE_TYPE_MACHOFAT, pd);
    if (xx_scan_type_is_java_class(head, head_size)) return xx_scan_type_list(XX_FILE_TYPE_JAVA_CLASS, pd);
    type = xx_scan_type_detect_macho(head, head_size);
    if (type != XX_FILE_TYPE_UNKNOWN) return xx_scan_type_list(type, pd);
    type = xx_scan_type_detect_gzip(device, head, head_size, pd);
    if (type != XX_FILE_TYPE_UNKNOWN) return xx_scan_type_list(type, pd);
    type = xx_scan_type_detect_dex(head, head_size);
    if (type != XX_FILE_TYPE_UNKNOWN) return xx_scan_type_list(type, pd);
    if (xx_scan_type_is_pdf(device, size, pd)) return xx_scan_type_list(XX_FILE_TYPE_PDF, pd);
    type = xx_scan_type_detect_zip(device, size, head, head_size, pd);
    if (type != XX_FILE_TYPE_UNKNOWN) return xx_scan_type_list(type, pd);
    type = xx_scan_type_detect_image(head, head_size);
    if (type != XX_FILE_TYPE_UNKNOWN) return xx_scan_type_list(type, pd);
    type = xx_scan_type_detect_amiga_atarist(device, head, head_size);
    if (type != XX_FILE_TYPE_UNKNOWN) return xx_scan_type_list(type, pd);
    if (xx_scan_type_is_iso9660(device, size)) return xx_scan_type_list(XX_FILE_TYPE_ISO9660, pd);
    type = xx_scan_type_detect_pyc(head, head_size);
    if (type != XX_FILE_TYPE_UNKNOWN) return xx_scan_type_list(type, pd);
    if (size > 0 && size <= XX_SCAN_COM_MAX_SIZE && options && xx_scan_has_com_suffix(options->file_name, true))
        return xx_scan_type_list(XX_FILE_TYPE_COM, pd);
    return xx_scan_type_list(XX_FILE_TYPE_BINARY, pd);
}


xx_list_t *xx_scan_detect_type(xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd)
{
    xx_scan_type_window window = {0};
    xx_list_t *types;
    int64_t original_position;
    int64_t total_size;
    int64_t offset = options ? options->offset : 0;
    int64_t requested_size = options ? options->size : -1;
    if (!device) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Scan device is NULL");
        return NULL;
    }
    if (pd && pd->is_stop) return NULL;
    if (offset < 0 || requested_size < -1 || (requested_size >= 0 && requested_size > INT64_MAX - offset)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid scan type range");
        return NULL;
    }
    total_size = xx_io_total_size(device);
    if (total_size >= 0 && (offset > total_size || (requested_size >= 0 && requested_size > total_size - offset))) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Scan type range exceeds input");
        return NULL;
    }
    window.source = device;
    window.base = offset;
    window.length = requested_size >= 0 ? requested_size : total_size >= 0 ? total_size - offset : -1;
    window.view.priv = &window;
    window.view.read = xx_scan_window_read;
    window.view.seek = xx_scan_window_seek;
    window.view.seek64 = xx_scan_window_seek64;
    window.view.tell = xx_scan_window_tell;
    window.view.total_size = xx_scan_window_size;
    original_position = xx_io_tell(device);
    types = xx_scan_detect_type_window(&window.view, options, pd);
    if (original_position >= 0) (void)xx_io_seek64(device, original_position, SEEK_SET);
    if (pd && pd->is_stop) {
        xx_list_destroy(types);
        return NULL;
    }
    return types;
}

xx_list_t *xx_scan_get_file_types(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd)
{
    (void)engine;
    return xx_scan_detect_type(device, options, pd);
}

