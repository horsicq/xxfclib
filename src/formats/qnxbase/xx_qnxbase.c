/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * QNX Neutrino boot images (.ifs / .boot / .altboot).
 *
 *   0x00  16 bytes  IPL boot record prefix, constant:
 *                   eb 4c 44 44 44 44 00 00 00 00 00 01 00 00 00 00
 *
 *   struct startup_header, 0x34 bytes, NOT at a fixed offset - QNX 6.1
 *   builds put it at 0x3d0, others at 0x3f8 or 0x400, so it is located the
 *   way the IPL itself does, by scanning 4-byte aligned for the signature
 *   word whose preboot_size points back at its own offset:
 *
 *     0x00  u32 LE   signature, 0x00ff7eeb
 *     0x04  u16 LE   version
 *     0x06  u8       flags1
 *     0x20  u32 LE   startup_size; the compressed chain begins at
 *                    header_offset + startup_size
 *     0x24  u32 LE   stored_size
 *     0x2c  u32 LE   imagefs_size - the plaintext length, and the only
 *                    figure the decoder can be held to
 *     0x30  u32 LE   preboot_size, which equals the header's own offset
 *
 *   the compressed payload is a chain of independent UCL NRV2B streams,
 *   each introduced by a BIG-endian u16 byte length, the chain closed by a
 *   length word of 0. All blocks share one output history.
 *
 *   the decompressed image filesystem:
 *     0x00   7 bytes  "imagefs"
 *     0x07   u8       flags, 0 or 4 in every known build
 *     0x08   u32 LE   image_size, which must equal imagefs_size
 *     0x10   u32 LE   dir_offset, the first directory record
 *
 *   directory record, variable length, chain closed by a record whose size
 *   word is 0:
 *     0x00   u16 LE   record size
 *     0x08   u16 LE   mode; 0x4000 directory and 0xa000 symlink are skipped
 *     0x14   u32 LE   mtime
 *     0x18   u32 LE   file offset within the image
 *     0x1c   u32 LE   file size
 *     0x20   ...      NUL-terminated path, to the end of the record
 *
 * The container is SOLID: a member has no extent of its own in the file, so
 * every member's data_offset and header_offset are offsets into the
 * DECOMPRESSED image filesystem, and compressed_size is the whole block
 * chain that every member shares. Extraction therefore decompresses the
 * whole image and slices it, which is what the reference implementation
 * does too.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/qnxbase/xx_qnxbase.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/qnxbase/xx_qnxbase.h"

#include <stdio.h>

#define XX_QNXBASE_COPY_CHUNK (64 * 1024)

typedef struct xx_qnxbase_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_qnxbase_member;

typedef struct xx_qnxbase_stream_s {
    xx_qnxbase_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_qnxbase_stream;

static void xx_qnxbase_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_qnxbase_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_qnxbase_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_qnxbase_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_qnxbase_stream_free(void *pointer) {
    xx_qnxbase_stream *stream = (xx_qnxbase_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_qnxbase_add(xx_qnxbase_stream *stream,
                          const xx_qnxbase_member *member) {
    xx_qnxbase_member *grown = (xx_qnxbase_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_QNXBASE_BOOT_PREFIX_SIZE 16
#define XX_QNXBASE_STARTUP_HEADER_SIZE ((int64_t)0x34)
#define XX_QNXBASE_STARTUP_SIGNATURE 0x00ff7eebU
#define XX_QNXBASE_HEADER_SCAN_LIMIT ((int64_t)0x10000)
#define XX_QNXBASE_HEADER_SCAN_STEP 4
#define XX_QNXBASE_MAX_IMAGEFS_SIZE ((int64_t)0x8000000)
#define XX_QNXBASE_MAX_COMPRESSED_SIZE ((int64_t)0x8000000)
#define XX_QNXBASE_MAX_BLOCKS 0x100000
#define XX_QNXBASE_MAX_MEMBERS 100000
#define XX_QNXBASE_MIN_IMAGE_SIZE ((int64_t)0x5c)
#define XX_QNXBASE_DIRENT_MIN_SIZE ((int64_t)0x1c)
#define XX_QNXBASE_DIRENT_FILE_SIZE ((int64_t)0x20)
#define XX_QNXBASE_MODE_TYPE_MASK 0xf000U
#define XX_QNXBASE_MODE_DIR 0x4000U
#define XX_QNXBASE_MODE_LNK 0xa000U
#define XX_QNXBASE_MODE_REG 0x8000U
#define XX_QNXBASE_METHOD_NRV2B 0U

typedef struct xx_qnxbase_header_s {
    int64_t header_offset;   /* relative to base_address */
    int64_t startup_size;
    int64_t imagefs_size;
    int64_t chain_offset;    /* relative to base_address */
    uint16_t version;
} xx_qnxbase_header;

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_qnxbase_le16(const uint8_t *data);
static uint32_t xx_qnxbase_le32(const uint8_t *data);
static bool xx_qnxbase_find_header(Abstractformat *self, int64_t span, xx_pd_struct *pd, xx_qnxbase_header *header);
static bool xx_qnxbase_measure_chain(Abstractformat *self, int64_t span, xx_pd_struct *pd, int64_t chain_offset, int64_t *chain_size);
static bool xx_qnxbase_load_image(Abstractformat *self, xx_pd_struct *pd, uint8_t **image, int64_t *image_size, int64_t *chain_offset, int64_t *chain_size);
static xx_qnxbase_stream *xx_qnxbase_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_qnxbase_decode(Abstractformat *self, const xx_qnxbase_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);



/* The startup header sits inside the first sectors in every known build, so
 * the scan is bounded rather than open. */

/* The reference's own ceilings: 128 MiB each. imagefs_size is range checked
 * because it drives an allocation; startup_size only has to be positive. */

/* 0x5c is the length of the FIXED part of struct image_header. dir_offset
 * legitimately points exactly AT 0x5c - mkifs puts the first record
 * immediately after the fixed header - so this is a bound the directory may
 * sit on, not one it has to clear. */


/* One codec, no stored method number. */



static uint16_t xx_qnxbase_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_qnxbase_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Locate struct startup_header.
 *
 * Both conditions are required, and that is what makes an open scan safe: the
 * odds of a random dword pair carrying the signature AND a preboot_size equal
 * to its own offset are 2^-64. Dropping either one turns this into a
 * match-anything scan of the first 64 KiB of every file on disk. */
static bool xx_qnxbase_find_header(Abstractformat *self, int64_t span,
                                   xx_pd_struct *pd,
                                   xx_qnxbase_header *header) {
    static const uint8_t prefix[XX_QNXBASE_BOOT_PREFIX_SIZE] = {
        0xebU, 0x4cU, 0x44U, 0x44U, 0x44U, 0x44U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U};
    uint8_t *scan;
    int64_t scan_size;
    int64_t last_candidate;
    int64_t offset;
    bool found = false;

    if (span < (int64_t)XX_QNXBASE_BOOT_PREFIX_SIZE +
                   XX_QNXBASE_STARTUP_HEADER_SIZE) {
        return false;
    }

    scan_size = XX_QNXBASE_HEADER_SCAN_LIMIT + XX_QNXBASE_STARTUP_HEADER_SIZE;
    if (scan_size > span) scan_size = span;

    scan = (uint8_t *)xx_mem_alloc((size_t)scan_size);
    if (!scan) return false;
    if (!xx_qnxbase_read_at(self, self->base_address, scan,
                            (size_t)scan_size)) {
        xx_mem_free(scan);
        return false;
    }

    /* The 16 constant bytes the QNX IPL writes at the very start. Cheap, and
     * the reason the scan below never runs on an unrelated file. */
    if (xx_rt_memcmp(scan, prefix, sizeof(prefix)) != 0) {
        xx_mem_free(scan);
        return false;
    }

    last_candidate = scan_size - XX_QNXBASE_STARTUP_HEADER_SIZE;
    for (offset = 0; offset <= last_candidate;
         offset += XX_QNXBASE_HEADER_SCAN_STEP) {
        const uint8_t *data = scan + offset;
        int64_t startup_size;
        int64_t imagefs_size;
        int64_t chain_offset;

        if (pd && xx_pd_is_stopped(pd)) break;
        if (xx_qnxbase_le32(data) != XX_QNXBASE_STARTUP_SIGNATURE) continue;
        if ((int64_t)xx_qnxbase_le32(data + 0x30) != offset) continue;

        /* Both fields are written as u32 but read as signed by the reference;
         * a negative one is a rejection, not a four-gigabyte value. */
        startup_size = (int64_t)(int32_t)xx_qnxbase_le32(data + 0x20);
        imagefs_size = (int64_t)(int32_t)xx_qnxbase_le32(data + 0x2c);
        if (startup_size <= 0) continue;
        if ((imagefs_size <= 0) || (imagefs_size > XX_QNXBASE_MAX_IMAGEFS_SIZE)) {
            continue;
        }

        chain_offset = offset + startup_size;
        /* The chain must have room for at least its first length word. */
        if (!xx_qnxbase_range_within(span, chain_offset, 2)) continue;

        header->header_offset = offset;
        header->startup_size = startup_size;
        header->imagefs_size = imagefs_size;
        header->chain_offset = chain_offset;
        header->version = xx_qnxbase_le16(data + 0x04);
        found = true;
        break;
    }

    xx_mem_free(scan);
    return found;
}

/* Walk the block chain without decoding it, to learn how many bytes to read.
 * Every block is a BIG-endian u16 length followed by that many bytes; a zero
 * length closes the chain. */
static bool xx_qnxbase_measure_chain(Abstractformat *self, int64_t span,
                                     xx_pd_struct *pd, int64_t chain_offset,
                                     int64_t *chain_size) {
    int64_t offset = chain_offset;
    int32_t blocks = 0;

    for (;;) {
        uint8_t length[2];
        int64_t block_size;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_qnxbase_range_within(span, offset, 2)) return false;
        if (!xx_qnxbase_read_at(self, self->base_address + offset, length,
                                2U)) {
            return false;
        }
        block_size = ((int64_t)length[0] << 8) | (int64_t)length[1];
        offset += 2;
        if (block_size == 0) break;
        if (!xx_qnxbase_range_within(span, offset, block_size)) return false;
        offset += block_size;
        if (++blocks > XX_QNXBASE_MAX_BLOCKS) return false;
    }

    /* A chain with no data block at all is not an image, however well the
     * startup header checked out. */
    if (blocks == 0) return false;

    *chain_size = offset - chain_offset;
    if ((*chain_size <= 0) || (*chain_size > XX_QNXBASE_MAX_COMPRESSED_SIZE)) {
        return false;
    }
    return true;
}

/* Locate, read and decompress the whole image filesystem. Shared by parse and
 * decode; neither caches it, so the two always agree. */
static bool xx_qnxbase_load_image(Abstractformat *self, xx_pd_struct *pd,
                                  uint8_t **image, int64_t *image_size,
                                  int64_t *chain_offset, int64_t *chain_size) {
    xx_qnxbase_header header;
    uint8_t *packed;
    uint8_t *plain;
    int64_t total;
    int64_t span;
    int64_t size = 0;
    size_t written = 0U;

    *image = NULL;
    *image_size = 0;
    *chain_offset = 0;
    *chain_size = 0;

    if (!self || !self->device || self->base_address < 0) return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;

    xx_mem_zero(&header, sizeof(header));
    if (!xx_qnxbase_find_header(self, span, pd, &header)) return false;
    if (!xx_qnxbase_measure_chain(self, span, pd, header.chain_offset, &size)) {
        return false;
    }

    packed = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!packed) return false;
    if (!xx_qnxbase_read_at(self, self->base_address + header.chain_offset,
                            packed, (size_t)size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc((size_t)header.imagefs_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* imagefs_size is both the allocation and the requirement: the reference
     * insists the decompressed length is exactly imagefs_size, so a chain that
     * stops short is a failed image and not a short one. */
    if (!xx_qnxbase_decode_memory(packed, (size_t)size, plain,
                                  (size_t)header.imagefs_size, &written) ||
        written != (size_t)header.imagefs_size) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);

    *image = plain;
    *image_size = header.imagefs_size;
    *chain_offset = header.chain_offset;
    *chain_size = size;
    return true;
}

static xx_qnxbase_stream *xx_qnxbase_parse(Abstractformat *self,
                                           xx_pd_struct *pd) {
    xx_qnxbase_stream *stream = NULL;
    xx_qnxbase_member member;
    uint8_t *image = NULL;
    int64_t image_size = 0;
    int64_t chain_offset = 0;
    int64_t chain_size = 0;
    int64_t dir_offset;
    int64_t offset;
    int64_t remaining;
    bool terminated = false;
    char *name = NULL;

    if (!self) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_qnxbase_load_image(self, pd, &image, &image_size, &chain_offset,
                               &chain_size)) {
        return NULL;
    }
    if (image_size <= XX_QNXBASE_MIN_IMAGE_SIZE) goto fail;

    /* The image header is the second magic, and the one that proves the
     * decompression produced an image filesystem rather than plausible noise.
     * All three parts matter: without the image_size agreement a chain that
     * happens to decode to imagefs_size bytes of anything would be accepted. */
    if (xx_rt_memcmp(image, "imagefs", 7U) != 0) goto fail;
    if ((image[7] != 0U) && (image[7] != 4U)) goto fail;
    if ((int64_t)xx_qnxbase_le32(image + 8) != image_size) goto fail;

    dir_offset = (int64_t)(int32_t)xx_qnxbase_le32(image + 0x10);
    /* ">=" on the lower bound rejected every real image: mkifs puts the first
     * record exactly at 0x5c. */
    if ((dir_offset < XX_QNXBASE_MIN_IMAGE_SIZE) ||
        (dir_offset >= image_size)) {
        goto fail;
    }

    stream = (xx_qnxbase_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    offset = dir_offset;
    remaining = image_size - dir_offset;

    while (remaining > (XX_QNXBASE_DIRENT_MIN_SIZE - 1)) {
        int64_t record_size;
        int64_t file_offset;
        int64_t file_size;
        int64_t name_offset;
        int64_t name_limit;
        int64_t name_end;
        int64_t index;
        uint32_t mode;
        uint32_t type;
        uint32_t mtime;

        if (pd && xx_pd_is_stopped(pd)) goto fail;

        record_size = (int64_t)xx_qnxbase_le16(image + offset);
        if (record_size == 0) {
            terminated = true;
            break;
        }
        if ((record_size > remaining) ||
            (record_size < XX_QNXBASE_DIRENT_MIN_SIZE)) {
            goto fail;
        }

        mode = (uint32_t)xx_qnxbase_le16(image + offset + 8);
        type = mode & XX_QNXBASE_MODE_TYPE_MASK;
        if ((type == XX_QNXBASE_MODE_DIR) || (type == XX_QNXBASE_MODE_LNK)) {
            /* Directories and symlinks carry no offset/size pair; the
             * reference skips them entirely rather than publishing them. */
            offset += record_size;
            remaining -= record_size;
            continue;
        }
        /* Anything that is neither a directory, a symlink nor a plain file
         * still has to be long enough to carry the offset/size pair. */
        if ((type != XX_QNXBASE_MODE_REG) &&
            ((record_size - XX_QNXBASE_DIRENT_MIN_SIZE) < 4)) {
            goto fail;
        }
        if (record_size < XX_QNXBASE_DIRENT_FILE_SIZE) goto fail;

        mtime = xx_qnxbase_le32(image + offset + 0x14);
        file_offset = (int64_t)xx_qnxbase_le32(image + offset + 0x18);
        file_size = (int64_t)xx_qnxbase_le32(image + offset + 0x1c);
        /* Bounded by the IMAGE: a member has no extent in the file at all. */
        if (!xx_qnxbase_range_within(image_size, file_offset, file_size)) {
            goto fail;
        }

        name_offset = offset + XX_QNXBASE_DIRENT_FILE_SIZE;
        name_limit = record_size - XX_QNXBASE_DIRENT_FILE_SIZE;
        name_end = name_offset;
        while ((name_limit > 0) && (image[name_end] != 0U)) {
            ++name_end;
            --name_limit;
        }
        if (name_end == name_offset) goto fail;
        for (index = name_offset; index < name_end; ++index) {
            /* Image filesystem paths are plain POSIX names written by mkifs;
             * nothing outside printable ASCII has ever appeared in one, and a
             * control byte here means the directory walk has desynchronised. */
            if ((image[index] < 0x20U) || (image[index] > 0x7eU)) goto fail;
        }

        if (stream->count >= (size_t)XX_QNXBASE_MAX_MEMBERS) goto fail;

        name = (char *)xx_mem_alloc((size_t)(name_end - name_offset) + 1U);
        if (!name) goto fail;
        xx_rt_memcpy(name, image + name_offset,
                     (size_t)(name_end - name_offset));
        name[name_end - name_offset] = '\0';

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        /* header_offset and data_offset are IMAGE offsets, not file offsets:
         * the archive is solid and the member simply does not exist in the
         * file as a contiguous run of bytes. */
        member.header_offset = offset;
        member.header_size = record_size;
        member.data_offset = file_offset;
        /* Every member reads the same block chain, exactly as the reference
         * reports it. */
        member.compressed_size = chain_size;
        member.uncompressed_size = file_size;
        member.method = XX_QNXBASE_METHOD_NRV2B;
        member.timestamp = (uint64_t)mtime;
        member.is_folder = false;

        if (!xx_qnxbase_add(stream, &member)) goto fail;
        name = NULL;

        /* The reference decrements its counter in three pieces that add up to
         * exactly record_size; written as one step it is the same walk. */
        offset += record_size;
        remaining -= record_size;
    }

    /* Success only on the zero-length record that closes the directory:
     * running off the end of the image is a failure, not a truncated listing.
     * Dropping this turns a corrupt image into a short but plausible one. */
    if (!terminated) goto fail;
    if (stream->count == 0U) goto fail;

    stream->archive_size = chain_offset + chain_size;
    xx_mem_free(image);
    return stream;

fail:
    if (name) xx_str_free(name);
    if (image) xx_mem_free(image);
    xx_qnxbase_stream_free(stream);
    return NULL;
}


/* Slice one member out of the decompressed image filesystem.
 *
 * The image has to be rebuilt from scratch for every member: the archive is
 * solid, the member has no compressed extent of its own, and nothing may be
 * cached on the format object because decode must stay free of side effects.
 * That is O(image) per member, and deliberately so - the alternative is a
 * mutable cache whose staleness the caller cannot see. */
static bool xx_qnxbase_decode(Abstractformat *self,
                              const xx_qnxbase_member *member, uint8_t **out,
                              size_t *out_size, xx_pd_struct *pd) {
    uint8_t *image = NULL;
    uint8_t *output;
    int64_t image_size = 0;
    int64_t chain_offset = 0;
    int64_t chain_size = 0;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* The container carries exactly one codec and no method number; refusing
     * anything else keeps an invented method from being served as stored. */
    if (member->method != XX_QNXBASE_METHOD_NRV2B) return false;
    if (member->uncompressed_size < 0) return false;
    if (member->uncompressed_size > XX_QNXBASE_MAX_IMAGEFS_SIZE) return false;

    if (!xx_qnxbase_load_image(self, pd, &image, &image_size, &chain_offset,
                               &chain_size)) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(image);
        return false;
    }

    /* data_offset is an offset into the IMAGE, not into the file, so it is
     * bounded by the image and never by the device span. */
    if (!xx_qnxbase_range_within(image_size, member->data_offset,
                                 member->uncompressed_size)) {
        xx_mem_free(image);
        return false;
    }

    /* A zero-length member is legal in an image filesystem, but the decode
     * contract has no way to return a zero-byte success that a caller can
     * tell from a failure, so allocate one byte and report zero length. */
    output = (uint8_t *)xx_mem_alloc(
        (size_t)(member->uncompressed_size > 0 ? member->uncompressed_size
                                               : 1));
    if (!output) {
        xx_mem_free(image);
        return false;
    }
    if (member->uncompressed_size > 0) {
        xx_rt_memcpy(output, image + member->data_offset,
                     (size_t)member->uncompressed_size);
    }
    xx_mem_free(image);

    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_qnxbase_init(xx_qnxbase *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_QNXBASE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-qnx-ifs");
    xx_format_set_extension(&archive->format, "ifs");
    archive->format.check_is_valid = xx_qnxbase_check_is_valid;
    archive->format.handle_base_info = xx_qnxbase_handle_base_info;
    archive->format.get_format_size = xx_qnxbase_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_qnxbase_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_qnxbase_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_qnxbase_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_qnxbase_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_qnxbase_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_qnxbase_free_archive_records_reading;
    archive->format.destroy = xx_qnxbase_vtable_destroy;
}

xx_qnxbase *xx_qnxbase_create(xx_io_device *device, int64_t base_address) {
    xx_qnxbase *archive = (xx_qnxbase *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_qnxbase_init(archive, device, base_address);
    return archive;
}

void xx_qnxbase_destroy(xx_qnxbase *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_qnxbase_free(xx_qnxbase *archive) {
    if (!archive) return;
    xx_qnxbase_destroy(archive);
    xx_mem_free(archive);
}

static void xx_qnxbase_vtable_destroy(Abstractformat *self) {
    xx_qnxbase_destroy((xx_qnxbase *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_qnxbase_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_qnxbase_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_qnxbase_parse(self, pd);
    if (!stream) return false;
    xx_qnxbase_stream_free(stream);
    return true;
}

bool xx_qnxbase_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_qnxbase *archive = (xx_qnxbase *)self;
    xx_qnxbase_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_qnxbase_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_qnxbase_stream_free(stream);
    return true;
}

int64_t xx_qnxbase_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_qnxbase_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_qnxbase *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_qnxbase_set_record(xx_archive_record *record,
                                 const xx_qnxbase_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_qnxbase_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_qnxbase_get_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_qnxbase_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_qnxbase_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_qnxbase_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_qnxbase_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_qnxbase_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_qnxbase_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_qnxbase_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_qnxbase_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_qnxbase_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_qnxbase_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_qnxbase_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_qnxbase_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_qnxbase_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_qnxbase_stream *stream;
    const xx_qnxbase_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_qnxbase_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_qnxbase_path_safe(member->name)) return false;

    path_option = xx_qnxbase_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_qnxbase_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_qnxbase_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_qnxbase_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
