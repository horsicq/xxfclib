/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZPAQ block prefix, seven bytes:
 *
 *   0..2  "zPQ"
 *   3     level, 1 or 2
 *   4     block type, 1
 *   5..6  uint16 LE size of the bytecode header that follows
 *
 * The block may be preceded by a 13-byte locator tag, which exists so a ZPAQ
 * stream can be found embedded in another file. Both placements are accepted;
 * xx_zpaq_get_block_offset reports which one was found.
 *
 * The last byte of the bytecode header must be zero -- that is the terminator
 * of the block's decompression program. Checking it is what makes the three
 * ASCII bytes "zPQ" a safe signature rather than a guess, because it forces a
 * consistent size field as well as a plausible one.
 *
 * ZPAQ blocks carry their own decompression program, so there is no fixed
 * codec to implement; XArchive's XZPAQ delegates to an external backend for
 * the same reason, and this reader advertises no archive records.
 *
 * Not handled: encrypted ZPAQ, which has no plaintext signature at all -- a
 * 32-byte salt followed by AES-CTR ciphertext. XArchive will accept such a
 * file only behind an explicit opt-in that its SFX reader sets after it has
 * recognised zpaqfranz's executable-overlay framing, and only then
 * authenticates the password before exposing anything. Guessing at it from a
 * salt alone would classify any 33-byte file as ZPAQ.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/zpaq/xx_zpaq.h"

#include "xxfclib/memory/xx_memory.h"

#include <stdio.h>

#define XX_ZPAQ_TAG_SIZE 13
#define XX_ZPAQ_BLOCK_PREFIX_SIZE 7
#define XX_ZPAQ_MIN_HEADER_SIZE 7U
#define XX_ZPAQ_BLOCK_TYPE 1U

static const uint8_t XX_ZPAQ_TAG[XX_ZPAQ_TAG_SIZE] = {
    0x37U, 0x6BU, 0x53U, 0x74U, 0xA0U, 0x31U, 0x83U,
    0xD3U, 0x8CU, 0xB2U, 0x28U, 0xB0U, 0xD3U};

static void xx_zpaq_vtable_destroy(Abstractformat *self);

static bool xx_zpaq_read_at(Abstractformat *self, int64_t offset,
                            uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

/*
 * Returns the offset of the first block relative to the base address, or -1.
 * The tagged form is tested first: a tagged stream also contains "zPQ", just
 * not at offset 0, so testing the bare form first would mislocate the block.
 */
static int64_t xx_zpaq_find_block(Abstractformat *self, int64_t span,
                                  bool *has_tag) {
    uint8_t prefix[XX_ZPAQ_TAG_SIZE + 3];

    if (span >= XX_ZPAQ_TAG_SIZE + 3 &&
        xx_zpaq_read_at(self, self->base_address, prefix, sizeof(prefix)) &&
        xx_rt_memcmp(prefix, XX_ZPAQ_TAG, XX_ZPAQ_TAG_SIZE) == 0 &&
        prefix[XX_ZPAQ_TAG_SIZE] == 'z' &&
        prefix[XX_ZPAQ_TAG_SIZE + 1] == 'P' &&
        prefix[XX_ZPAQ_TAG_SIZE + 2] == 'Q') {
        if (has_tag) *has_tag = true;
        return XX_ZPAQ_TAG_SIZE;
    }
    if (span >= 3 &&
        xx_zpaq_read_at(self, self->base_address, prefix, 3U) &&
        prefix[0] == 'z' && prefix[1] == 'P' && prefix[2] == 'Q') {
        if (has_tag) *has_tag = false;
        return 0;
    }
    return -1;
}

static bool xx_zpaq_probe(Abstractformat *self, xx_zpaq *out) {
    uint8_t block[XX_ZPAQ_BLOCK_PREFIX_SIZE];
    uint8_t terminator;
    bool has_tag = false;
    int64_t block_offset;
    int64_t total;
    int64_t span;
    int64_t remaining;
    uint16_t header_size;

    if (!self || !self->device || self->base_address < 0) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;

    block_offset = xx_zpaq_find_block(self, span, &has_tag);
    if (block_offset < 0 || block_offset > span - XX_ZPAQ_BLOCK_PREFIX_SIZE) {
        return false;
    }
    if (!xx_zpaq_read_at(self, self->base_address + block_offset, block,
                         sizeof(block))) {
        return false;
    }
    if ((block[3] != 1U && block[3] != 2U) ||
        block[4] != XX_ZPAQ_BLOCK_TYPE) {
        return false;
    }
    header_size = (uint16_t)((uint16_t)block[5] | ((uint16_t)block[6] << 8));
    remaining = span - block_offset - XX_ZPAQ_BLOCK_PREFIX_SIZE;
    if (header_size < XX_ZPAQ_MIN_HEADER_SIZE ||
        (int64_t)header_size > remaining) {
        return false;
    }
    /* The bytecode header ends with a zero terminator. This is the check that
     * ties the size field to the content, so "zPQ" alone cannot pass. */
    if (!xx_zpaq_read_at(self,
                         self->base_address + block_offset +
                             XX_ZPAQ_BLOCK_PREFIX_SIZE + header_size - 1,
                         &terminator, 1U) ||
        terminator != 0U) {
        return false;
    }

    if (out) {
        out->block_offset = block_offset;
        out->has_tag = has_tag;
        out->level = block[3];
        out->header_size = header_size;
    }
    return true;
}

void xx_zpaq_init(xx_zpaq *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ZPAQ;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    archive->block_offset = -1;
    xx_format_set_mime_type(&archive->format, "application/x-zpaq");
    xx_format_set_extension(&archive->format, "zpaq");
    archive->format.check_is_valid = xx_zpaq_check_is_valid;
    archive->format.handle_base_info = xx_zpaq_handle_base_info;
    archive->format.get_format_size = xx_zpaq_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_zpaq_get_number_of_archive_records;
    archive->format.destroy = xx_zpaq_vtable_destroy;
}

xx_zpaq *xx_zpaq_create(xx_io_device *device, int64_t base_address) {
    xx_zpaq *archive = (xx_zpaq *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_zpaq_init(archive, device, base_address);
    return archive;
}

void xx_zpaq_destroy(xx_zpaq *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches back through format.destroy. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->block_offset = -1;
}

void xx_zpaq_free(xx_zpaq *archive) {
    if (!archive) return;
    xx_zpaq_destroy(archive);
    xx_mem_free(archive);
}

static void xx_zpaq_vtable_destroy(Abstractformat *self) {
    xx_zpaq_destroy((xx_zpaq *)self);
}

bool xx_zpaq_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    return xx_zpaq_probe(self, NULL);
}

bool xx_zpaq_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_zpaq *archive = (xx_zpaq *)self;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    self->is_valid = xx_zpaq_probe(self, archive);
    if (!self->is_valid) {
        self->format_size = 0;
        return false;
    }
    /* Only the first block is parsed, so the archive is reported as running to
     * the end of the device rather than claiming a length not walked. */
    self->format_size = xx_io_total_size(self->device) - self->base_address;
    return true;
}

int64_t xx_zpaq_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_zpaq_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    /* Each block carries its own decompression bytecode; none is run here. */
    (void)self;
    (void)pd;
    return 0U;
}

int64_t xx_zpaq_get_block_offset(const xx_zpaq *archive) {
    return archive ? archive->block_offset : -1;
}

bool xx_zpaq_has_tag(const xx_zpaq *archive) {
    return archive ? archive->has_tag : false;
}

uint8_t xx_zpaq_get_level(const xx_zpaq *archive) {
    return archive ? archive->level : 0U;
}
