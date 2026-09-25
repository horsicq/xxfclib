/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * HP Printer Job Language data.  Validation and size follow binwalk's
 * src/signatures/pjl.rs: the thirteen-byte magic ESC "%-12345X@PJL" at the
 * base, then a C string from base + 9 that must be valid UTF-8, whose
 * length is binwalk's result.size.  The rules, and what that size does and
 * does not cover, are in xx_pjl.h.
 *
 * NOT an archive.  binwalk registers no extractor for PJL, so there is
 * nothing inside to publish as a record.
 *
 * The scan is one forward pass from base + 9 through a fixed buffer, in
 * XX_PJL_CHUNK_SIZE reads, each of which is clamped to the device end.  It
 * stops at the first NUL, at the end of the input, or at the first byte
 * that makes the UTF-8 invalid.  Every chunk read either advances the
 * position by at least one byte or fails the parse, so the loop is bounded
 * by the input size, and the stop flag is polled once per chunk.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pjl/xx_pjl.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_PJL exists in the enum. */
#ifdef PJL
#define XX_PJL_FILE_TYPE XX_FILE_TYPE_PJL
#else
#define XX_PJL_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Read granularity of the scan. */
#define XX_PJL_CHUNK_SIZE 65536U

typedef struct xx_pjl_parsed_s {
    int64_t input_size;
    int64_t text_offset;
    int64_t text_size;
    int64_t text_end;
    uint64_t lines;
    uint64_t commands;
    uint64_t uels;
    bool terminator;
    bool ascii;
} xx_pjl_parsed;

/* Streaming UTF-8 validator, exactly Rust's core::str::from_utf8. */
typedef struct xx_pjl_utf8_s {
    uint8_t pending; /* continuation bytes still expected */
    uint8_t lo;      /* allowed range of the NEXT continuation byte */
    uint8_t hi;
} xx_pjl_utf8;

/* The per-byte text statistics, carried across chunks. */
typedef struct xx_pjl_scan_s {
    xx_pjl_utf8 utf8;
    int cmd_state;  /* matched bytes of "@PJL" at a command start, -1 off */
    uint32_t uel_state; /* matched bytes of the UEL */
    uint64_t lines;     /* LF bytes seen */
    uint64_t commands;
    uint64_t uels;
    uint8_t last;       /* the previous byte */
    bool ascii;
} xx_pjl_scan;

static void xx_pjl_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: long is 32-bit on Win64. */
static bool xx_pjl_read_at(xx_io_device *device, int64_t offset, void *data,
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

/* Feeds one non-NUL byte; false when the byte makes the text invalid. */
static bool xx_pjl_utf8_feed(xx_pjl_utf8 *state, uint8_t c) {
    if (state->pending != 0U) {
        if (c < state->lo || c > state->hi) return false;
        --state->pending;
        state->lo = 0x80U;
        state->hi = 0xBFU;
        return true;
    }
    if (c < 0x80U) return true;
    state->lo = 0x80U;
    state->hi = 0xBFU;
    if (c >= 0xC2U && c <= 0xDFU) {
        state->pending = 1U;
    } else if (c == 0xE0U) {
        state->pending = 2U;
        state->lo = 0xA0U; /* no overlong 3-byte forms */
    } else if ((c >= 0xE1U && c <= 0xECU) || c == 0xEEU || c == 0xEFU) {
        state->pending = 2U;
    } else if (c == 0xEDU) {
        state->pending = 2U;
        state->hi = 0x9FU; /* no surrogates */
    } else if (c == 0xF0U) {
        state->pending = 3U;
        state->lo = 0x90U; /* no overlong 4-byte forms */
    } else if (c >= 0xF1U && c <= 0xF3U) {
        state->pending = 3U;
    } else if (c == 0xF4U) {
        state->pending = 3U;
        state->hi = 0x8FU; /* nothing above U+10FFFF */
    } else {
        return false; /* 80-BF stray continuation, C0/C1, F5-FF */
    }
    return true;
}

static void xx_pjl_scan_byte(xx_pjl_scan *scan, uint8_t c) {
    static const uint8_t prefix[] = XX_PJL_COMMAND_PREFIX;
    static const uint8_t uel[] = XX_PJL_UEL;

    if (c >= 0x80U) scan->ascii = false;
    if (scan->cmd_state >= 0) {
        if (c == prefix[scan->cmd_state]) {
            if (++scan->cmd_state == (int)XX_PJL_COMMAND_PREFIX_SIZE) {
                ++scan->commands;
                scan->cmd_state = -1;
            }
        } else {
            scan->cmd_state = -1;
        }
    }
    if (c == '\n') {
        ++scan->lines;
        scan->cmd_state = 0;
    }
    /* ESC occurs once in the UEL, at its start, so a mismatch can only
     * restart the match on an ESC. */
    if (c == uel[scan->uel_state]) {
        if (++scan->uel_state == XX_PJL_UEL_SIZE) {
            ++scan->uels;
            scan->uel_state = 0U;
            scan->cmd_state = 0;
        }
    } else {
        scan->uel_state = (c == uel[0]) ? 1U : 0U;
    }
    scan->last = c;
}

/* --------------------------------------------------------------- parse -- */

static bool xx_pjl_parse(Abstractformat *self, xx_pjl_parsed *parsed,
                         xx_pd_struct *pd) {
    uint8_t magic[XX_PJL_MAGIC_SIZE];
    xx_pjl_scan scan;
    uint8_t *buffer = NULL;
    int64_t position;
    bool ok = false;
    bool done = false;

    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->text_offset = -1;
    parsed->text_size = -1;
    parsed->text_end = -1;
    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (parsed->input_size < self->base_address ||
        parsed->input_size - self->base_address <
            (int64_t)XX_PJL_MAGIC_SIZE ||
        !xx_pjl_read_at(self->device, self->base_address, magic,
                        sizeof(magic)) ||
        xx_rt_memcmp(magic, XX_PJL_MAGIC, XX_PJL_MAGIC_SIZE) != 0) {
        return false;
    }

    buffer = (uint8_t *)xx_mem_alloc(XX_PJL_CHUNK_SIZE);
    if (!buffer) return false;
    xx_mem_zero(&scan, sizeof(scan));
    scan.cmd_state = 0; /* the text starts right after the base UEL */
    scan.ascii = true;

    parsed->text_offset = self->base_address + (int64_t)XX_PJL_COMMANDS_OFFSET;
    position = parsed->text_offset;
    while (!done) {
        int64_t remaining = parsed->input_size - position;
        size_t want;
        size_t i;

        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        if (remaining <= 0) break; /* end of input: no terminator */
        want = remaining < (int64_t)XX_PJL_CHUNK_SIZE
                   ? (size_t)remaining
                   : (size_t)XX_PJL_CHUNK_SIZE;
        if (!xx_pjl_read_at(self->device, position, buffer, want)) {
            goto cleanup;
        }
        for (i = 0U; i < want; ++i) {
            uint8_t c = buffer[i];
            if (c == 0U) {
                parsed->terminator = true;
                done = true;
                break;
            }
            if (!xx_pjl_utf8_feed(&scan.utf8, c)) goto cleanup;
            xx_pjl_scan_byte(&scan, c);
        }
        position += (int64_t)i;
    }
    /* A multi-byte sequence cut short by the NUL or the end of input. */
    if (scan.utf8.pending != 0U) goto cleanup;

    parsed->text_end = position;
    parsed->text_size = position - parsed->text_offset;
    /* binwalk: `if result.size > 0`.  The magic puts "@PJL" there, so this
     * only guards the arithmetic. */
    if (parsed->text_size < (int64_t)XX_PJL_COMMAND_PREFIX_SIZE) goto cleanup;
    parsed->lines = scan.lines + (scan.last != '\n' ? 1U : 0U);
    parsed->commands = scan.commands;
    parsed->uels = scan.uels;
    parsed->ascii = scan.ascii;
    ok = true;

cleanup:
    xx_mem_free(buffer);
    return ok;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_pjl_init(xx_pjl *pjl, xx_io_device *dev, int64_t base_address) {
    if (!pjl) return;
    xx_mem_zero(pjl, sizeof(*pjl));
    xx_format_init(&pjl->format, dev, base_address);
    pjl->format.file_type = XX_PJL_FILE_TYPE;
    /* The format-type enum has no document kind; a print job is not an
     * archive, executable, firmware or package, so it stays UNKNOWN. */
    pjl->format.format_type = XX_TYPE_UNKNOWN;
    pjl->format.is_archive = false;
    xx_format_set_mime_type(&pjl->format, "application/vnd.hp-pjl");
    xx_format_set_extension(&pjl->format, "pjl");
    pjl->format.check_is_valid = xx_pjl_check_is_valid;
    pjl->format.handle_base_info = xx_pjl_handle_base_info;
    pjl->format.get_format_size = xx_pjl_get_format_size;
    pjl->format.destroy = xx_pjl_vtable_destroy;
    pjl->text_offset = -1;
    pjl->text_size = -1;
    pjl->text_end = -1;
}

xx_pjl *xx_pjl_create(xx_io_device *dev, int64_t base_address) {
    xx_pjl *pjl = (xx_pjl *)xx_mem_alloc(sizeof(*pjl));

    if (pjl) xx_pjl_init(pjl, dev, base_address);
    return pjl;
}

void xx_pjl_destroy(xx_pjl *pjl) {
    if (!pjl) return;
    xx_format_cleanup_extra_parameters(&pjl->format);
}

static void xx_pjl_vtable_destroy(Abstractformat *self) {
    xx_pjl_destroy((xx_pjl *)self);
}

void xx_pjl_free(xx_pjl *pjl) {
    if (!pjl) return;
    xx_pjl_destroy(pjl);
    xx_mem_free(pjl);
}

/* -------------------------------------------------------------- format -- */

bool xx_pjl_check_magic(const uint8_t *magic, size_t magic_size) {
    return magic && magic_size >= XX_PJL_MAGIC_SIZE &&
           xx_rt_memcmp(magic, XX_PJL_MAGIC, XX_PJL_MAGIC_SIZE) == 0;
}

bool xx_pjl_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_pjl_parsed parsed;

    return xx_pjl_parse(self, &parsed, pd);
}

bool xx_pjl_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_pjl *pjl = (xx_pjl *)self;
    xx_pjl_parsed parsed;
    int64_t format_end;

    if (!self) return false;
    if (!xx_pjl_parse(self, &parsed, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    pjl->text_offset = parsed.text_offset;
    pjl->text_size = parsed.text_size;
    pjl->text_end = parsed.text_end;
    pjl->number_of_lines = parsed.lines;
    pjl->number_of_commands = parsed.commands;
    pjl->number_of_uels = parsed.uels;
    pjl->has_terminator = parsed.terminator;
    pjl->is_ascii = parsed.ascii;
    /* binwalk result.size is the string length, counted from the signature
     * offset; see xx_pjl.h.  text_size <= input - base - 9, so format_end
     * is always inside the input. */
    self->format_size = parsed.text_size;
    format_end = self->base_address + parsed.text_size;
    if (parsed.input_size > format_end) {
        self->overlay_offset = format_end;
        self->overlay_size = parsed.input_size - format_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 0U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_pjl_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

/* ------------------------------------------------------------ accessors -- */

int64_t xx_pjl_get_text_offset(const xx_pjl *pjl) {
    return pjl ? pjl->text_offset : -1;
}

int64_t xx_pjl_get_text_size(const xx_pjl *pjl) {
    return pjl ? pjl->text_size : -1;
}

int64_t xx_pjl_get_text_end(const xx_pjl *pjl) {
    return pjl ? pjl->text_end : -1;
}

uint64_t xx_pjl_get_number_of_lines(const xx_pjl *pjl) {
    return pjl ? pjl->number_of_lines : 0U;
}

uint64_t xx_pjl_get_number_of_commands(const xx_pjl *pjl) {
    return pjl ? pjl->number_of_commands : 0U;
}

uint64_t xx_pjl_get_number_of_uels(const xx_pjl *pjl) {
    return pjl ? pjl->number_of_uels : 0U;
}

bool xx_pjl_has_terminator(const xx_pjl *pjl) {
    return pjl ? pjl->has_terminator : false;
}

bool xx_pjl_is_ascii(const xx_pjl *pjl) {
    return pjl ? pjl->is_ascii : false;
}
