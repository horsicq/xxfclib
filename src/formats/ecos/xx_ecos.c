/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * eCos MIPS kernel images, recognised by their exception-handler stub.  The
 * four opcode patterns and the endianness rule follow binwalk's
 * src/signatures/ecos.rs, which is the only module that format has - there is
 * no structures/ecos.rs and no extractors/ecos.rs, because an eCos image has
 * no header and no members.  The reasoning and the limitations are spelled
 * out in xx_ecos.h.
 *
 * This reader is DETECTION ONLY on purpose.  It publishes no archive records:
 * an eCos image is one flat blob and nothing in it says where the kernel ends
 * and a concatenated rootfs begins.  Publishing a record with a guessed
 * boundary would be worse than publishing none.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ecos/xx_ecos.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as ECOS is registered there. */
#ifdef ECOS
#define XX_ECOS_FILE_TYPE XX_FILE_TYPE_ECOS
#else
#define XX_ECOS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

typedef struct xx_ecos_pattern_s {
    uint8_t bytes[XX_ECOS_MAX_PATTERN_SIZE];
    uint8_t size;
    bool big_endian;
    bool has_nop;
} xx_ecos_pattern;

/* Longest first: a big-endian image with the nop also matches the shorter
 * no-nop big-endian pattern's first four bytes, so the longer one has to be
 * tried first for has_nop to be reported correctly. */
static const xx_ecos_pattern xx_ecos_patterns[] = {
    {{0x40U, 0x1AU, 0x68U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x33U, 0x5AU,
      0x00U, 0x7FU},
     12U,
     true,
     true},
    {{0x00U, 0x68U, 0x1AU, 0x40U, 0x00U, 0x00U, 0x00U, 0x00U, 0x7FU, 0x00U,
      0x5AU, 0x33U},
     12U,
     false,
     true},
    {{0x40U, 0x1AU, 0x68U, 0x00U, 0x33U, 0x5AU, 0x00U, 0x7FU, 0x00U, 0x00U,
      0x00U, 0x00U},
     8U,
     true,
     false},
    {{0x00U, 0x68U, 0x1AU, 0x40U, 0x7FU, 0x00U, 0x5AU, 0x33U, 0x00U, 0x00U,
      0x00U, 0x00U},
     8U,
     false,
     false}};

#define XX_ECOS_PATTERN_COUNT \
    (sizeof(xx_ecos_patterns) / sizeof(xx_ecos_patterns[0]))

typedef struct xx_ecos_parsed_s {
    int64_t input_size;
    uint32_t pattern_size;
    bool big_endian;
    bool has_nop;
} xx_ecos_parsed;

static void xx_ecos_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: the base address inside a larger flash
 * dump is not bounded by any 32-bit field, and long is 32-bit on Win64. */
static bool xx_ecos_read_at(xx_io_device *device, int64_t offset, void *data,
                            size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;

    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* --------------------------------------------------------------- parse -- */

static bool xx_ecos_parse(Abstractformat *self, xx_ecos_parsed *parsed,
                          xx_pd_struct *pd) {
    uint8_t window[XX_ECOS_MAX_PATTERN_SIZE];
    size_t available;
    size_t index;
    int64_t span;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (parsed->input_size < self->base_address) return false;
    span = parsed->input_size - self->base_address;
    /* The shortest pattern is eight bytes; anything smaller cannot match. */
    if (span < 8) return false;

    available = (span < (int64_t)XX_ECOS_MAX_PATTERN_SIZE)
                    ? (size_t)span
                    : XX_ECOS_MAX_PATTERN_SIZE;
    if (!xx_ecos_read_at(self->device, self->base_address, window, available)) {
        return false;
    }

    for (index = 0U; index < XX_ECOS_PATTERN_COUNT; ++index) {
        const xx_ecos_pattern *pattern = &xx_ecos_patterns[index];
        if ((size_t)pattern->size > available) continue;
        if (xx_rt_memcmp(window, pattern->bytes, pattern->size) != 0) continue;
        parsed->pattern_size = pattern->size;
        parsed->big_endian = pattern->big_endian;
        parsed->has_nop = pattern->has_nop;
        return true;
    }
    return false;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_ecos_init(xx_ecos *ecos, xx_io_device *dev, int64_t base_address) {
    if (!ecos) return;
    xx_mem_zero(ecos, sizeof(*ecos));
    xx_format_init(&ecos->format, dev, base_address);
    /* Overwritten by handle_base_info once the pattern says which it is. */
    ecos->format.endian = XX_ENDIAN_BIG;
    ecos->format.file_type = XX_ECOS_FILE_TYPE;
    /* Not an archive: there is nothing inside to enumerate. */
    ecos->format.format_type = XX_TYPE_FIRMWARE;
    ecos->format.is_archive = false;
    xx_format_set_mime_type(&ecos->format, "application/x-ecos-kernel");
    xx_format_set_extension(&ecos->format, "bin");
    ecos->format.check_is_valid = xx_ecos_check_is_valid;
    ecos->format.handle_base_info = xx_ecos_handle_base_info;
    ecos->format.get_format_size = xx_ecos_get_format_size;
    ecos->format.destroy = xx_ecos_vtable_destroy;
}

xx_ecos *xx_ecos_create(xx_io_device *dev, int64_t base_address) {
    xx_ecos *ecos = (xx_ecos *)xx_mem_alloc(sizeof(*ecos));

    if (ecos) xx_ecos_init(ecos, dev, base_address);
    return ecos;
}

void xx_ecos_destroy(xx_ecos *ecos) {
    if (!ecos) return;
    xx_format_cleanup_extra_parameters(&ecos->format);
}

static void xx_ecos_vtable_destroy(Abstractformat *self) {
    xx_ecos_destroy((xx_ecos *)self);
}

void xx_ecos_free(xx_ecos *ecos) {
    if (!ecos) return;
    xx_ecos_destroy(ecos);
    xx_mem_free(ecos);
}

/* -------------------------------------------------------------- format -- */

bool xx_ecos_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ecos_parsed parsed;

    return xx_ecos_parse(self, &parsed, pd);
}

bool xx_ecos_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ecos *ecos = (xx_ecos *)self;
    xx_ecos_parsed parsed;

    if (!self || !ecos) return false;
    if (!xx_ecos_parse(self, &parsed, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    ecos->pattern_size = parsed.pattern_size;
    ecos->big_endian = parsed.big_endian;
    ecos->has_nop = parsed.has_nop;
    self->endian = parsed.big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    /* The whole remaining region is the kernel image.  There is no length
     * field anywhere in it, so this is a statement about what was handed to
     * the reader, not a claim recovered from the file - which is also why
     * there is no overlay. */
    self->format_size = parsed.input_size - self->base_address;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 0U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_ecos_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

/* ------------------------------------------------------------ accessors -- */

bool xx_ecos_is_big_endian(const xx_ecos *ecos) {
    return ecos ? ecos->big_endian : false;
}

bool xx_ecos_has_nop(const xx_ecos *ecos) {
    return ecos ? ecos->has_nop : false;
}

uint32_t xx_ecos_get_pattern_size(const xx_ecos *ecos) {
    return ecos ? ecos->pattern_size : 0U;
}
