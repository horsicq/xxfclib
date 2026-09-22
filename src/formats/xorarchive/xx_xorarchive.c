/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Reader for XOR-masked ZIP and ARJ containers.
 *
 * The archive itself is an ordinary ZIP or ARJ file; every byte has merely
 * been pushed through a fixed, position-independent byte mask so that a
 * scanner looking for "PK\3\4" or "60 EA" walks past it.  The mask observed
 * across the sample corpora is
 *
 *     stored = rotate_left(original, rotation) ^ key
 *
 * where rotation is 0 for the ZIP family (a plain single-byte XOR, key 0x63
 * / 0xF0 / 0x50 depending on the sample) and 5 for the ARJ family (key 0x74).
 * Both parameters are recovered from the known plaintext of the container
 * magic, so nothing is hard coded.
 *
 * Because the mask does not depend on the byte position, undoing it is a
 * pure 1:1 byte map.  This reader therefore installs a private xx_io_device
 * that unmasks on read and hands that device to the existing xx_zip / xx_arj
 * reader, which is embedded in a union and does all the real work.  The same
 * delegation idea as the apk/jar/ipa readers, with an unmasking device in
 * between: no ZIP or ARJ parsing is duplicated, and listing, extraction and
 * data-struct walking come along for free.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/xorarchive/xx_xorarchive.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_XORARCHIVE exists in the enum. */
#ifdef XORARCHIVE
#define XX_XORARCHIVE_FILE_TYPE XX_FILE_TYPE_XORARCHIVE
#else
#define XX_XORARCHIVE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Bytes of the magic window the prefilter inspects.  Never more than the
 * dispatcher's 64-byte window. */
#define XX_XORARCHIVE_WINDOW 16U

/* Upper bound on mask candidates: one per rotation for ARJ plus one for ZIP. */
#define XX_XORARCHIVE_MAX_CANDIDATES 9U

/* ARJ basic header bounds, from the ARJ technical note. */
#define XX_XORARCHIVE_ARJ_HEADER_MIN 30U
#define XX_XORARCHIVE_ARJ_HEADER_MAX 2600U
#define XX_XORARCHIVE_ARJ_FIRST_MIN 30U
#define XX_XORARCHIVE_ARJ_FIRST_MAX 62U
#define XX_XORARCHIVE_ARJ_HOST_MAX 11U
#define XX_XORARCHIVE_ARJ_TYPE_MAX 4U

typedef struct xx_xorarchive_candidate_s {
    uint8_t rotation;
    uint8_t key;
    uint8_t family;
} xx_xorarchive_candidate;

/* ------------------------------------------------------------------ */
/* Mask primitives                                                     */
/* ------------------------------------------------------------------ */

static uint8_t xx_xorarchive_rol(uint8_t value, unsigned rotation) {
    rotation &= 7U;
    if (rotation == 0U) return value;
    return (uint8_t)(((unsigned)value << rotation) |
                     ((unsigned)value >> (8U - rotation)));
}

/** Undo the mask for one byte: original = rotate_right(stored ^ key). */
static uint8_t xx_xorarchive_unmask(uint8_t stored, uint8_t key,
                                    unsigned rotation) {
    return xx_xorarchive_rol((uint8_t)(stored ^ key), (8U - (rotation & 7U)) & 7U);
}

static void xx_xorarchive_build_table(uint8_t *table, uint8_t key,
                                      unsigned rotation) {
    unsigned index;
    for (index = 0U; index < 256U; ++index)
        table[index] = xx_xorarchive_unmask((uint8_t)index, key, rotation);
}

/* ------------------------------------------------------------------ */
/* Unmasking I/O device                                                */
/* ------------------------------------------------------------------ */

static ssize_t xx_xorarchive_view_read(xx_io_device *device, void *data,
                                       size_t size) {
    xx_xorarchive *archive = device ? (xx_xorarchive *)device->priv : NULL;
    uint8_t *bytes = (uint8_t *)data;
    ssize_t amount;
    size_t index;

    if (!archive || !archive->source || (!data && size != 0U)) return -1;
    if (size == 0U) return 0;
    amount = xx_io_read(archive->source, data, size);
    if (amount <= 0) return amount;
    if ((size_t)amount > size) return -1;
    for (index = 0U; index < (size_t)amount; ++index)
        bytes[index] = archive->table[bytes[index]];
    return amount;
}

static int xx_xorarchive_view_seek(xx_io_device *device, long offset,
                                   int origin) {
    xx_xorarchive *archive = device ? (xx_xorarchive *)device->priv : NULL;
    if (!archive || !archive->source) return -1;
    return xx_io_seek(archive->source, offset, origin);
}

static int xx_xorarchive_view_seek64(xx_io_device *device, int64_t offset,
                                     int origin) {
    xx_xorarchive *archive = device ? (xx_xorarchive *)device->priv : NULL;
    if (!archive || !archive->source) return -1;
    return xx_io_seek64(archive->source, offset, origin);
}

static int64_t xx_xorarchive_view_tell(xx_io_device *device) {
    xx_xorarchive *archive = device ? (xx_xorarchive *)device->priv : NULL;
    if (!archive || !archive->source) return -1;
    return xx_io_tell(archive->source);
}

static int64_t xx_xorarchive_view_size(xx_io_device *device) {
    xx_xorarchive *archive = device ? (xx_xorarchive *)device->priv : NULL;
    if (!archive || !archive->source) return -1;
    return xx_io_total_size(archive->source);
}

/** The masked device is borrowed from the caller and is never closed here. */
static int xx_xorarchive_view_close(xx_io_device *device) {
    (void)device;
    return 0;
}

static void xx_xorarchive_view_bind(xx_xorarchive *archive) {
    xx_mem_zero(&archive->view, sizeof(archive->view));
    archive->view.priv = archive;
    archive->view.read = xx_xorarchive_view_read;
    archive->view.write = NULL;
    archive->view.seek = xx_xorarchive_view_seek;
    archive->view.seek64 = xx_xorarchive_view_seek64;
    archive->view.tell = xx_xorarchive_view_tell;
    archive->view.close = xx_xorarchive_view_close;
    archive->view.total_size = xx_xorarchive_view_size;
    archive->view.get_total_size = xx_xorarchive_view_size;
    archive->view.size = xx_xorarchive_view_size;
}

/* ------------------------------------------------------------------ */
/* Mask recovery                                                       */
/* ------------------------------------------------------------------ */

/** Does the unmasked window still look like a ZIP local file header? */
static bool xx_xorarchive_window_is_zip(const uint8_t *window, size_t size,
                                        uint8_t key, unsigned rotation) {
    uint8_t decoded[10];
    unsigned index;
    if (size < sizeof(decoded)) return false;
    for (index = 0U; index < sizeof(decoded); ++index)
        decoded[index] = xx_xorarchive_unmask(window[index], key, rotation);
    if (decoded[0] != 0x50U || decoded[1] != 0x4BU || decoded[2] != 0x03U ||
        decoded[3] != 0x04U)
        return false;
    /* "version needed" and "method" are small little-endian words. */
    if (decoded[5] != 0U || decoded[9] != 0U) return false;
    if (decoded[4] == 0U || decoded[4] > 0x3FU) return false;
    return true;
}

/** Does the unmasked window still look like an ARJ main header? */
static bool xx_xorarchive_window_is_arj(const uint8_t *window, size_t size,
                                        uint8_t key, unsigned rotation) {
    uint8_t decoded[12];
    unsigned index;
    unsigned header_size;
    if (size < sizeof(decoded)) return false;
    for (index = 0U; index < sizeof(decoded); ++index)
        decoded[index] = xx_xorarchive_unmask(window[index], key, rotation);
    if (decoded[0] != 0x60U || decoded[1] != 0xEAU) return false;
    header_size = (unsigned)decoded[2] | ((unsigned)decoded[3] << 8U);
    if (header_size < XX_XORARCHIVE_ARJ_HEADER_MIN ||
        header_size > XX_XORARCHIVE_ARJ_HEADER_MAX)
        return false;
    if (decoded[4] < XX_XORARCHIVE_ARJ_FIRST_MIN ||
        decoded[4] > XX_XORARCHIVE_ARJ_FIRST_MAX)
        return false;
    if (decoded[4] > header_size) return false;
    if (decoded[7] > XX_XORARCHIVE_ARJ_HOST_MAX) return false;
    if (decoded[10] > XX_XORARCHIVE_ARJ_TYPE_MAX) return false;
    return true;
}

/**
 * Recover every mask under which the window uncovers a plausible container
 * header.  The identity mask (rotation 0, key 0) is rejected: an unmasked
 * ZIP or ARJ belongs to the plain readers, not here.
 */
static unsigned xx_xorarchive_collect(const uint8_t *window, size_t size,
                                      xx_xorarchive_candidate *candidates,
                                      unsigned capacity) {
    static const uint8_t zip_magic[4] = {0x50U, 0x4BU, 0x03U, 0x04U};
    static const uint8_t arj_magic[2] = {0x60U, 0xEAU};
    unsigned count = 0U;
    unsigned rotation;

    if (!window || !candidates || capacity == 0U) return 0U;

    for (rotation = 0U; rotation < 8U; ++rotation) {
        unsigned index;
        uint8_t key;
        bool match;

        if (size >= sizeof(zip_magic) && count < capacity) {
            key = (uint8_t)(xx_xorarchive_rol(zip_magic[0], rotation) ^ window[0]);
            match = !(rotation == 0U && key == 0U);
            for (index = 0U; match && index < sizeof(zip_magic); ++index) {
                if ((uint8_t)(xx_xorarchive_rol(zip_magic[index], rotation) ^ key) !=
                    window[index])
                    match = false;
            }
            if (match && xx_xorarchive_window_is_zip(window, size, key, rotation)) {
                candidates[count].rotation = (uint8_t)rotation;
                candidates[count].key = key;
                candidates[count].family = (uint8_t)XX_XORARCHIVE_FAMILY_ZIP;
                ++count;
            }
        }

        if (size >= sizeof(arj_magic) && count < capacity) {
            key = (uint8_t)(xx_xorarchive_rol(arj_magic[0], rotation) ^ window[0]);
            match = !(rotation == 0U && key == 0U);
            for (index = 0U; match && index < sizeof(arj_magic); ++index) {
                if ((uint8_t)(xx_xorarchive_rol(arj_magic[index], rotation) ^ key) !=
                    window[index])
                    match = false;
            }
            if (match && xx_xorarchive_window_is_arj(window, size, key, rotation)) {
                candidates[count].rotation = (uint8_t)rotation;
                candidates[count].key = key;
                candidates[count].family = (uint8_t)XX_XORARCHIVE_FAMILY_ARJ;
                ++count;
            }
        }
    }
    return count;
}

bool xx_xorarchive_test_magic(const uint8_t *magic, size_t magic_size) {
    xx_xorarchive_candidate candidates[XX_XORARCHIVE_MAX_CANDIDATES];
    if (!magic) return false;
    if (magic_size > XX_XORARCHIVE_WINDOW) magic_size = XX_XORARCHIVE_WINDOW;
    return xx_xorarchive_collect(magic, magic_size, candidates,
                                 XX_XORARCHIVE_MAX_CANDIDATES) != 0U;
}

/* ------------------------------------------------------------------ */
/* Identity                                                            */
/* ------------------------------------------------------------------ */

static void xx_xorarchive_describe(xx_xorarchive *archive) {
    static const char digits[] = "0123456789ABCDEF";
    char text[32];
    size_t at = 0U;
    const char *name = (archive->family == (uint8_t)XX_XORARCHIVE_FAMILY_ARJ)
                           ? "ARJ"
                           : "ZIP";
    if (archive->family == (uint8_t)XX_XORARCHIVE_FAMILY_NONE) {
        xx_format_set_version(&archive->container.format, "");
        return;
    }
    text[at++] = name[0];
    text[at++] = name[1];
    text[at++] = name[2];
    text[at++] = ' ';
    text[at++] = 'r';
    text[at++] = 'o';
    text[at++] = 'l';
    text[at++] = (char)('0' + (archive->rotation & 7U));
    text[at++] = ' ';
    text[at++] = 'x';
    text[at++] = 'o';
    text[at++] = 'r';
    text[at++] = ' ';
    text[at++] = digits[(archive->key >> 4U) & 0x0FU];
    text[at++] = digits[archive->key & 0x0FU];
    text[at] = 0;
    xx_format_set_version(&archive->container.format, text);
}

static void xx_xorarchive_apply_identity(xx_xorarchive *archive) {
    Abstractformat *format = &archive->container.format;
    format->file_type = XX_XORARCHIVE_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->endian = XX_ENDIAN_LITTLE;
    format->is_archive = true;
    format->is_executable = false;
    xx_format_set_mime_type(format, "application/octet-stream");
    xx_format_set_extension(format,
                            archive->family == (uint8_t)XX_XORARCHIVE_FAMILY_ARJ
                                ? "arj"
                                : "zip");
    xx_xorarchive_describe(archive);
}

static void xx_xorarchive_vtable_destroy(Abstractformat *self) {
    if (self) xx_xorarchive_destroy((xx_xorarchive *)self);
}

/** Tear the embedded container down without touching our own state. */
static void xx_xorarchive_release_container(xx_xorarchive *archive) {
    if (archive->family == (uint8_t)XX_XORARCHIVE_FAMILY_ZIP)
        xx_zip_destroy(&archive->container.zip);
    else if (archive->family == (uint8_t)XX_XORARCHIVE_FAMILY_ARJ)
        xx_arj_destroy(&archive->container.arj);
    else
        xx_format_cleanup_extra_parameters(&archive->container.format);
}

/**
 * Bind the embedded container for one mask candidate and ask the real
 * ZIP/ARJ reader whether the unmasked stream parses end to end.  Four bytes
 * of recovered magic are not evidence; a complete parse is.
 */
static bool xx_xorarchive_try_candidate(xx_xorarchive *archive,
                                        const xx_xorarchive_candidate *candidate,
                                        int64_t base_address) {
    Abstractformat *format;
    bool valid;

    archive->key = candidate->key;
    archive->rotation = candidate->rotation;
    archive->family = candidate->family;
    xx_xorarchive_build_table(archive->table, archive->key, archive->rotation);
    xx_xorarchive_view_bind(archive);

    if (candidate->family == (uint8_t)XX_XORARCHIVE_FAMILY_ZIP) {
        xx_zip_init(&archive->container.zip, &archive->view, base_address);
        format = &archive->container.format;
        valid = xx_zip_check_is_valid(format, NULL);
    } else {
        xx_arj_init(&archive->container.arj, &archive->view, base_address);
        format = &archive->container.format;
        valid = xx_arj_check_is_valid(format, NULL);
    }
    if (!valid) {
        xx_xorarchive_release_container(archive);
        archive->family = (uint8_t)XX_XORARCHIVE_FAMILY_NONE;
        archive->key = 0U;
        archive->rotation = 0U;
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void xx_xorarchive_init(xx_xorarchive *archive, xx_io_device *device,
                        int64_t base_address) {
    uint8_t window[XX_XORARCHIVE_WINDOW];
    xx_xorarchive_candidate candidates[XX_XORARCHIVE_MAX_CANDIDATES];
    unsigned count = 0U;
    unsigned index;
    size_t filled = 0U;

    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    archive->source = device;
    archive->family = (uint8_t)XX_XORARCHIVE_FAMILY_NONE;

    /* Identity map, so the view is safe to read even before a mask is found. */
    xx_xorarchive_build_table(archive->table, 0U, 0U);
    xx_xorarchive_view_bind(archive);

    if (device && base_address >= 0 &&
        xx_io_seek64(device, base_address, SEEK_SET) == 0) {
        while (filled < sizeof(window)) {
            ssize_t amount = xx_io_read(device, window + filled,
                                        sizeof(window) - filled);
            if (amount <= 0 || (size_t)amount > sizeof(window) - filled) break;
            filled += (size_t)amount;
        }
        count = xx_xorarchive_collect(window, filled, candidates,
                                      XX_XORARCHIVE_MAX_CANDIDATES);
    }

    for (index = 0U; index < count; ++index) {
        if (xx_xorarchive_try_candidate(archive, &candidates[index],
                                        base_address))
            break;
    }

    if (archive->family == (uint8_t)XX_XORARCHIVE_FAMILY_NONE) {
        /* Nothing recognizable: keep an inert but well-formed object. */
        xx_format_init(&archive->container.format, &archive->view, base_address);
    }

    xx_xorarchive_apply_identity(archive);
    archive->container.format.check_is_valid = xx_xorarchive_check_is_valid;
    archive->container.format.handle_base_info = xx_xorarchive_handle_base_info;
    archive->container.format.destroy = xx_xorarchive_vtable_destroy;
    archive->container.format.is_valid =
        archive->family != (uint8_t)XX_XORARCHIVE_FAMILY_NONE;
    archive->container.format.base_info_handled = false;
}

xx_xorarchive *xx_xorarchive_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_xorarchive *archive = (xx_xorarchive *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_xorarchive_init(archive, device, base_address);
    return archive;
}

void xx_xorarchive_destroy(xx_xorarchive *archive) {
    if (!archive) return;
    xx_xorarchive_release_container(archive);
    archive->source = NULL;
}

void xx_xorarchive_free(xx_xorarchive *archive) {
    if (!archive) return;
    xx_xorarchive_destroy(archive);
    xx_mem_free(archive);
}

/* ------------------------------------------------------------------ */
/* Format callbacks                                                    */
/* ------------------------------------------------------------------ */

bool xx_xorarchive_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_xorarchive *archive = (xx_xorarchive *)self;
    if (!archive) return false;
    if (archive->family == (uint8_t)XX_XORARCHIVE_FAMILY_ZIP)
        return xx_zip_check_is_valid(self, pd);
    if (archive->family == (uint8_t)XX_XORARCHIVE_FAMILY_ARJ)
        return xx_arj_check_is_valid(self, pd);
    return false;
}

bool xx_xorarchive_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_xorarchive *archive = (xx_xorarchive *)self;
    bool result;

    if (!archive) return false;
    if (archive->family == (uint8_t)XX_XORARCHIVE_FAMILY_ZIP)
        result = xx_zip_handle_base_info(self, pd);
    else if (archive->family == (uint8_t)XX_XORARCHIVE_FAMILY_ARJ)
        result = xx_arj_handle_base_info(self, pd);
    else
        result = false;

    xx_xorarchive_apply_identity(archive);
    self->is_valid = result;
    self->base_info_handled = result;
    return result;
}

/* ------------------------------------------------------------------ */
/* Getters                                                             */
/* ------------------------------------------------------------------ */

uint8_t xx_xorarchive_get_key(const xx_xorarchive *archive) {
    return archive ? archive->key : 0U;
}

uint8_t xx_xorarchive_get_rotation(const xx_xorarchive *archive) {
    return archive ? archive->rotation : 0U;
}

xx_xorarchive_family_t xx_xorarchive_get_family(const xx_xorarchive *archive) {
    return archive ? (xx_xorarchive_family_t)archive->family
                   : XX_XORARCHIVE_FAMILY_NONE;
}
