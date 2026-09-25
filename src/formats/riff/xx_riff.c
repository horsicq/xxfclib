/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * RIFF containers.  Detection and the carve size follow binwalk's "RIFF
 * image" signature: src/signatures/riff.rs calls parse_riff_header() from
 * src/structures/riff.rs, which reads "RIFF", the u32 LE riff_size and the
 * form type, and reports riff_size + 8; src/binwalk.rs then drops the result
 * when that size runs past the end of the data.  binwalk's result.size is
 * riff_size + 8, and so is ours.
 *
 * Because the magic is four bytes, the reader is deliberately STRICTER than
 * binwalk: every binwalk check is applied first, then the form type and the
 * whole top-level chunk list have to be what the RIFF specification says
 * (see xx_riff.h for the list and the one tolerated writer deviation).
 *
 * Not an archive: binwalk's extractor carves the RIFF itself.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/riff/xx_riff.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as RIFF is registered there. */
#ifdef RIFF
#define XX_RIFF_FILE_TYPE XX_FILE_TYPE_RIFF
#else
#define XX_RIFF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

typedef struct xx_riff_parsed_s {
    int64_t input_size;
    int64_t format_size;      /* riff_size + 8 */
    uint32_t riff_size;
    uint8_t form_type[4];
    uint8_t first_chunk_id[4];
    uint32_t number_of_chunks;
    bool pad_outside;
} xx_riff_parsed;

static void xx_riff_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: a RIFF carved out of a disk image can
 * sit past 2 GiB, and long is 32-bit on Win64. */
static bool xx_riff_read_at(xx_io_device *device, int64_t offset, void *data,
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

static uint32_t xx_riff_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

/* A FOURCC as the RIFF specification defines it: four ASCII characters,
 * space padded on the right.  Printable ASCII is accepted anywhere (real
 * form types such as the "-=SO" of an obfuscated WAVE use punctuation);
 * control bytes and bytes >= 0x80 are not.  binwalk only requires valid
 * UTF-8, which every accepted value is. */
static bool xx_riff_fourcc_is_valid(const uint8_t *id) {
    size_t index;

    for (index = 0U; index < 4U; ++index) {
        if (id[index] < 0x20U || id[index] > 0x7EU) return false;
    }
    return true;
}

static void xx_riff_copy_fourcc(char *destination, const uint8_t *id) {
    size_t index;

    for (index = 0U; index < 4U; ++index) destination[index] = (char)id[index];
    destination[4] = '\0';
}

/* --------------------------------------------------------------- parse -- */

static bool xx_riff_parse(Abstractformat *self, xx_riff_parsed *parsed,
                          xx_pd_struct *pd) {
    uint8_t header[XX_RIFF_HEADER_SIZE];
    uint8_t chunk[XX_RIFF_CHUNK_HEADER_SIZE];
    int64_t available;
    int64_t start;
    int64_t position;
    int64_t end;
    uint32_t count;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->format_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (parsed->input_size < self->base_address) return false;
    available = parsed->input_size - self->base_address;
    start = self->base_address;

    /* --- binwalk: parse_riff_header ------------------------------------ */
    if (available < (int64_t)XX_RIFF_HEADER_SIZE) return false;
    if (!xx_riff_read_at(self->device, start, header, sizeof(header))) {
        return false;
    }
    if (header[0] != 0x52U || header[1] != 0x49U || header[2] != 0x46U ||
        header[3] != 0x46U) { /* "RIFF" */
        return false;
    }
    parsed->riff_size = xx_riff_u32(header + 4);
    /* riff_size is a u32, so the sum cannot overflow an int64. */
    parsed->format_size = (int64_t)parsed->riff_size +
                          (int64_t)XX_RIFF_CHUNK_HEADER_SIZE;
    /* binwalk.rs: a size past the end of the data voids the signature. */
    if (parsed->format_size > available) return false;
    xx_rt_memcpy(parsed->form_type, header + 8, 4U);

    /* --- beyond binwalk ------------------------------------------------- */
    /* The body is the form type plus at least one chunk header. */
    if (parsed->riff_size < XX_RIFF_MIN_RIFF_SIZE) return false;
    /* binwalk: String::from_utf8(form type).  Narrowed to a FOURCC. */
    if (!xx_riff_fourcc_is_valid(parsed->form_type) ||
        parsed->form_type[0] == 0x20U) {
        return false;
    }

    /* Walk the top-level chunk list.  Every step reads one 8-byte header and
     * advances by at least 8 bytes, and the count is capped, so the loop is
     * bounded by both XX_RIFF_MAX_CHUNKS and riff_size / 8. */
    position = (int64_t)XX_RIFF_HEADER_SIZE;
    end = parsed->format_size;
    count = 0U;
    while (end - position >= (int64_t)XX_RIFF_CHUNK_HEADER_SIZE) {
        uint32_t size;
        int64_t room;

        if (count >= XX_RIFF_MAX_CHUNKS) return false;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_riff_read_at(self->device, start + position, chunk,
                             sizeof(chunk))) {
            return false;
        }
        if (!xx_riff_fourcc_is_valid(chunk)) return false;
        size = xx_riff_u32(chunk + 4);
        room = end - position - (int64_t)XX_RIFF_CHUNK_HEADER_SIZE;
        if ((int64_t)size > room) return false;
        if (count == 0U) xx_rt_memcpy(parsed->first_chunk_id, chunk, 4U);
        position += (int64_t)XX_RIFF_CHUNK_HEADER_SIZE + (int64_t)size;
        ++count;
        if ((size & 1U) != 0U) {
            if (position == end) {
                /* The last chunk is odd and its pad byte was left out of
                 * riff_size.  It is not part of the carve. */
                parsed->pad_outside = true;
                break;
            }
            ++position; /* position < end here, so still <= end */
        }
    }
    /* The chunks must tile the body: no stray tail shorter than a header. */
    if (position != end || count == 0U) return false;
    parsed->number_of_chunks = count;
    return !(pd && xx_pd_is_stopped(pd));
}

/* The extension and MIME type binwalk's extractor and the common tools use
 * for the frequent form types; everything else stays a generic RIFF. */
static void xx_riff_set_names(Abstractformat *self, const uint8_t *form_type) {
    static const struct {
        char form[5];
        const char *extension;
        const char *mime;
    } k_names[] = {
        {"WAVE", "wav", "audio/wav"},
        {"AVI ", "avi", "video/x-msvideo"},
        {"WEBP", "webp", "image/webp"},
        {"ACON", "ani", "application/x-navi-animation"},
        {"RMID", "rmi", "audio/mid"},
        {"sfbk", "sf2", "audio/x-soundfont"},
        {"DLS ", "dls", "audio/dls"},
        {"PAL ", "pal", "application/x-riff"},
    };
    size_t index;

    for (index = 0U; index < sizeof(k_names) / sizeof(k_names[0]); ++index) {
        if (xx_rt_memcmp(form_type, k_names[index].form, 4U) == 0) {
            xx_format_set_extension(self, k_names[index].extension);
            xx_format_set_mime_type(self, k_names[index].mime);
            return;
        }
    }
    xx_format_set_extension(self, "riff");
    xx_format_set_mime_type(self, "application/x-riff");
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_riff_init(xx_riff *riff, xx_io_device *dev, int64_t base_address) {
    if (!riff) return;
    xx_mem_zero(riff, sizeof(*riff));
    xx_format_init(&riff->format, dev, base_address);
    riff->format.endian = XX_ENDIAN_LITTLE;
    riff->format.file_type = XX_RIFF_FILE_TYPE;
    /* The format-type enum has no media kind; a RIFF is not an archive,
     * executable, firmware or package, so it stays UNKNOWN. */
    riff->format.format_type = XX_TYPE_UNKNOWN;
    riff->format.is_archive = false;
    xx_format_set_mime_type(&riff->format, "application/x-riff");
    xx_format_set_extension(&riff->format, "riff");
    riff->format.check_is_valid = xx_riff_check_is_valid;
    riff->format.handle_base_info = xx_riff_handle_base_info;
    riff->format.get_format_size = xx_riff_get_format_size;
    riff->format.destroy = xx_riff_vtable_destroy;
}

xx_riff *xx_riff_create(xx_io_device *dev, int64_t base_address) {
    xx_riff *riff = (xx_riff *)xx_mem_alloc(sizeof(*riff));

    if (riff) xx_riff_init(riff, dev, base_address);
    return riff;
}

void xx_riff_destroy(xx_riff *riff) {
    if (!riff) return;
    xx_format_cleanup_extra_parameters(&riff->format);
}

static void xx_riff_vtable_destroy(Abstractformat *self) {
    xx_riff_destroy((xx_riff *)self);
}

void xx_riff_free(xx_riff *riff) {
    if (!riff) return;
    xx_riff_destroy(riff);
    xx_mem_free(riff);
}

/* -------------------------------------------------------------- format -- */

bool xx_riff_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_riff_parsed parsed;

    return xx_riff_parse(self, &parsed, pd);
}

bool xx_riff_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_riff *riff = (xx_riff *)self;
    xx_riff_parsed parsed;
    int64_t end;

    if (!self || !riff) return false;
    if (!xx_riff_parse(self, &parsed, pd)) {
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    riff->riff_size = parsed.riff_size;
    xx_riff_copy_fourcc(riff->form_type, parsed.form_type);
    xx_riff_copy_fourcc(riff->first_chunk_id, parsed.first_chunk_id);
    riff->number_of_chunks = parsed.number_of_chunks;
    riff->pad_outside = parsed.pad_outside;
    xx_riff_set_names(self, parsed.form_type);

    /* binwalk's carve length.  parse() guaranteed base + size <= total. */
    end = self->base_address + parsed.format_size;
    self->format_size = parsed.format_size;
    if (end < parsed.input_size) {
        self->overlay_offset = end;
        self->overlay_size = parsed.input_size - end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->file_type = XX_RIFF_FILE_TYPE;
    self->number_of_archive_records = 0U;
    self->is_archive = false;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_riff_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

/* ------------------------------------------------------------ accessors -- */

uint32_t xx_riff_get_riff_size(const xx_riff *riff) {
    return riff ? riff->riff_size : 0U;
}

const char *xx_riff_get_form_type(const xx_riff *riff) {
    return riff ? riff->form_type : "";
}

const char *xx_riff_get_first_chunk_id(const xx_riff *riff) {
    return riff ? riff->first_chunk_id : "";
}

uint32_t xx_riff_get_number_of_chunks(const xx_riff *riff) {
    return riff ? riff->number_of_chunks : 0U;
}
