/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/uboot/xx_uboot.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_UBOOT_ENV exists in the enum. */
#ifdef UBOOT_ENV
#define XX_UBOOT_FILE_TYPE XX_FILE_TYPE_UBOOT_ENV
#else
#define XX_UBOOT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** Variable array growth step. */
#define XX_UBOOT_GROW_STEP 64U

/** Bytes read up front for the cheap name check: the redundant header, the
 * longest accepted name, and its '='. */
#define XX_UBOOT_GATE_SIZE (XX_UBOOT_CRC_SIZE + 1U + XX_UBOOT_MAX_KEY_LENGTH + 1U)

typedef struct xx_uboot_variable_s {
    char *key;
    char *value;
} xx_uboot_variable;

typedef struct xx_uboot_private_s {
    xx_uboot_variable *variables;
    size_t count;
    size_t capacity;
    int64_t input_size;
    int64_t archive_end;
    xx_uboot_layout_t layout;
    uint32_t env_size;
    uint32_t data_size;
    uint32_t crc32;
    uint8_t flags;
    bool big_endian;
} xx_uboot_private;

static void xx_uboot_vtable_destroy(Abstractformat *self);

/*
 * The block sizes real boards use for CONFIG_ENV_SIZE.  The value is not
 * stored anywhere in the block, so recognising one means trying sizes until
 * the CRC comes out.  Largest first is the order of PREFERENCE: a short
 * candidate can never validate against a longer real block (the CRC would be
 * over the wrong range).  The CRCs themselves are computed in one ascending
 * incremental pass, so the order costs nothing.
 */
static const uint32_t xx_uboot_candidate_sizes[] = {
    0x40000U, 0x20000U, 0x10000U, 0x8000U, 0x4000U,
    0x2000U,  0x1000U,  0x800U,   0x400U};
#define XX_UBOOT_CANDIDATE_COUNT \
    (sizeof(xx_uboot_candidate_sizes) / sizeof(xx_uboot_candidate_sizes[0]))
/** Candidates plus the whole-remainder / pinned size. */
#define XX_UBOOT_MAX_TRIES (XX_UBOOT_CANDIDATE_COUNT + 1U)

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

/* All positioning goes through seek64: an environment block is small but its
 * base address inside a flash dump is not, and long is 32-bit on Win64. */
static bool xx_uboot_read_at(xx_io_device *device, int64_t offset, void *data,
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

/* A variable-name byte: printable ASCII other than space and '='.  U-Boot
 * only forbids '=' in a name, but a name with blanks or control bytes has
 * never been seen in a real block, and refusing them is what lets garbage be
 * turned away before any CRC is computed. */
static bool xx_uboot_is_key_char(uint8_t value) {
    return value > 0x20U && value < 0x7FU && value != (uint8_t)'=';
}

/* True when @p data starts with 1..XX_UBOOT_MAX_KEY_LENGTH name bytes and
 * then '='.  This is exactly what xx_uboot_parse_entries() demands of the
 * first entry, so it rejects nothing the full parse would accept. */
static bool xx_uboot_key_prefix_ok(const uint8_t *data, size_t size) {
    size_t index;
    for (index = 0U; index < size && index <= XX_UBOOT_MAX_KEY_LENGTH;
         ++index) {
        if (data[index] == (uint8_t)'=') return index != 0U;
        if (!xx_uboot_is_key_char(data[index])) return false;
    }
    return false;
}

static void xx_uboot_private_cleanup(xx_uboot_private *parsed) {
    size_t index;
    if (!parsed) return;
    if (parsed->variables) {
        for (index = 0U; index < parsed->count; ++index) {
            if (parsed->variables[index].key) {
                xx_str_free(parsed->variables[index].key);
            }
            if (parsed->variables[index].value) {
                xx_str_free(parsed->variables[index].value);
            }
        }
        xx_mem_free(parsed->variables);
    }
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

/* Drop the variables collected by a failed attempt, keep the rest. */
static void xx_uboot_private_reset_variables(xx_uboot_private *parsed) {
    int64_t input_size = parsed->input_size;
    xx_uboot_private_cleanup(parsed);
    parsed->input_size = input_size;
}

static bool xx_uboot_push(xx_uboot_private *parsed, const uint8_t *entry,
                          size_t entry_length, size_t separator) {
    xx_uboot_variable *slot;
    size_t index;
    if (!parsed || parsed->count >= XX_UBOOT_MAX_VARIABLES ||
        separator >= entry_length) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        size_t capacity = parsed->capacity + XX_UBOOT_GROW_STEP;
        xx_uboot_variable *grown = (xx_uboot_variable *)xx_mem_realloc(
            parsed->variables, capacity * sizeof(*grown));
        if (!grown) return false;
        parsed->variables = grown;
        parsed->capacity = capacity;
    }
    slot = &parsed->variables[parsed->count];
    slot->key = xx_str_create_len(separator);
    slot->value = xx_str_create_len(entry_length - separator - 1U);
    if (!slot->key || !slot->value) {
        if (slot->key) xx_str_free(slot->key);
        if (slot->value) xx_str_free(slot->value);
        slot->key = NULL;
        slot->value = NULL;
        return false;
    }
    for (index = 0U; index < separator; ++index) {
        slot->key[index] = (char)entry[index];
    }
    slot->key[separator] = '\0';
    for (index = separator + 1U; index < entry_length; ++index) {
        slot->value[index - separator - 1U] = (char)entry[index];
    }
    slot->value[entry_length - separator - 1U] = '\0';
    ++parsed->count;
    return true;
}

/*
 * Walk the NUL-separated entries.
 *
 * The walk is bounded three ways: by data_size, which is fixed before the
 * first byte is examined; by XX_UBOOT_MAX_ENTRY_LENGTH per entry (and
 * XX_UBOOT_MAX_KEY_LENGTH per name); and by XX_UBOOT_MAX_VARIABLES overall.
 * A block with no terminating empty entry therefore stops at the end of the
 * data area instead of scanning on, and a block that is all tiny entries
 * cannot allocate without limit.
 */
static bool xx_uboot_parse_entries(xx_uboot_private *parsed,
                                   const uint8_t *data, size_t data_size) {
    size_t position = 0U;
    while (position < data_size) {
        size_t cursor = position;
        size_t separator;
        size_t entry_length;
        /* An empty entry is the terminator; 0xFF is the NOR erase value and
         * means the padding started without one. */
        if (data[position] == 0x00U || data[position] == 0xFFU) break;
        /* The name: printable, non-empty, bounded, ended by '='. */
        while (cursor < data_size && data[cursor] != (uint8_t)'=') {
            if (!xx_uboot_is_key_char(data[cursor]) ||
                cursor - position >= XX_UBOOT_MAX_KEY_LENGTH) {
                return false;
            }
            ++cursor;
        }
        if (cursor >= data_size) break; /* truncated last entry, see below */
        separator = cursor - position;
        if (separator == 0U) return false;
        /* The value: anything but NUL. */
        for (++cursor; cursor < data_size; ++cursor) {
            if (data[cursor] == 0x00U) break;
            if (cursor - position >= XX_UBOOT_MAX_ENTRY_LENGTH) return false;
        }
        if (cursor >= data_size) {
            /* The last entry runs off the end of the block: truncated, so it
             * is dropped rather than half-reported.  Everything before it is
             * still good, and the CRC has already vouched for the bytes. */
            break;
        }
        entry_length = cursor - position;
        if (!xx_uboot_push(parsed, data + position, entry_length, separator)) {
            return false;
        }
        position = cursor + 1U;
    }
    return parsed->count != 0U;
}

/*
 * Recognise the block at base_address.
 *
 * 1. Gate: read at most XX_UBOOT_GATE_SIZE bytes and keep only the layouts
 *    whose first entry starts like "name=".  Most non-environment data is
 *    turned away here, before anything else is read.
 * 2. Pick the sizes to try: the pinned size alone, or else the whole
 *    remainder (when it is at most XX_UBOOT_MAX_ENV_SIZE) and every table
 *    size that fits.  No size below XX_UBOOT_MIN_ENV_SIZE is ever tried: a
 *    shorter remainder is refused up front, every table size is at least
 *    0x400, and a pinned size below it fails the range check.
 * 3. Read the largest of them ONCE (bounded by XX_UBOOT_MAX_ENV_SIZE) and,
 *    per surviving layout, CRC the data area in a single ascending pass,
 *    recording the running CRC at every candidate end.
 * 4. In order of preference - whole remainder, then largest table size
 *    first; plain before redundant; little- before big-endian - accept the
 *    first size whose stored CRC matches and whose data parses.
 */
static bool xx_uboot_parse(Abstractformat *self, xx_uboot_private *parsed,
                           xx_pd_struct *pd) {
    static const uint32_t prefixes[2] = {XX_UBOOT_CRC_SIZE,
                                         XX_UBOOT_CRC_SIZE + 1U};
    static const xx_uboot_layout_t layouts[2] = {XX_UBOOT_LAYOUT_PLAIN,
                                                 XX_UBOOT_LAYOUT_REDUNDANT};
    const xx_uboot *uboot = (const xx_uboot *)self;
    uint8_t gate[XX_UBOOT_GATE_SIZE];
    uint32_t sizes[XX_UBOOT_MAX_TRIES];       /* preference order */
    uint32_t ascending[XX_UBOOT_MAX_TRIES];   /* CRC pass order */
    uint32_t crcs[2][XX_UBOOT_MAX_TRIES];     /* per layout, per ascending */
    bool layout_ok[2];
    size_t size_count = 0U;
    size_t gate_size;
    size_t index;
    size_t layout_index;
    uint32_t span = 0U;
    uint32_t stored_le;
    uint32_t stored_be;
    uint8_t *block = NULL;
    int64_t remaining;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (parsed->input_size < 0 || self->base_address > parsed->input_size) {
        goto fail;
    }
    remaining = parsed->input_size - self->base_address;
    /* Nothing shorter than the smallest accepted block can be one, so a
     * tiny file is turned away without a single read. */
    if (remaining < (int64_t)XX_UBOOT_MIN_ENV_SIZE) goto fail;

    /* 1. The name gate. */
    gate_size = remaining < (int64_t)sizeof(gate) ? (size_t)remaining
                                                  : sizeof(gate);
    if (!xx_uboot_read_at(self->device, self->base_address, gate, gate_size)) {
        goto fail;
    }
    layout_ok[0] = xx_uboot_key_prefix_ok(gate + prefixes[0],
                                          gate_size - prefixes[0]);
    layout_ok[1] = gate_size > prefixes[1] &&
                   xx_uboot_key_prefix_ok(gate + prefixes[1],
                                          gate_size - prefixes[1]);
    if (!layout_ok[0] && !layout_ok[1]) goto fail;
    stored_le = xx_data_get_u32(gate, gate_size, 0U, false);
    stored_be = xx_data_get_u32(gate, gate_size, 0U, true);

    /* 2. The sizes, in order of preference. */
    if (uboot->env_size != 0U) {
        /* A caller that knows CONFIG_ENV_SIZE pins it; no search. */
        if ((int64_t)uboot->env_size > remaining) goto fail;
        sizes[size_count++] = uboot->env_size;
    } else {
        /* The whole remainder first: a dumped environment file is exactly
         * one block and nothing else, the common standalone case and the
         * only one where the size is known for certain.  It is at least
         * XX_UBOOT_MIN_ENV_SIZE, checked above. */
        if (remaining <= (int64_t)XX_UBOOT_MAX_ENV_SIZE) {
            sizes[size_count++] = (uint32_t)remaining;
        }
        /* Every table size is between XX_UBOOT_MIN_ENV_SIZE and
         * XX_UBOOT_MAX_ENV_SIZE, so only fit needs checking. */
        for (index = 0U; index < XX_UBOOT_CANDIDATE_COUNT; ++index) {
            uint32_t candidate = xx_uboot_candidate_sizes[index];
            if ((int64_t)candidate >= remaining) {
                continue; /* too big, or equal to the remainder: tried */
            }
            sizes[size_count++] = candidate;
        }
    }
    for (index = 0U; index < size_count; ++index) {
        /* XX_UBOOT_MIN_ENV_SIZE is far above both prefixes, so every data
         * area below is well over two bytes long. */
        if (sizes[index] < XX_UBOOT_MIN_ENV_SIZE ||
            sizes[index] > XX_UBOOT_MAX_ENV_SIZE) {
            goto fail; /* only a pinned size can be out of range here */
        }
        if (sizes[index] > span) span = sizes[index];
    }
    if (size_count == 0U) goto fail;
    /* Ascending order for the incremental pass (at most 10 entries). */
    for (index = 0U; index < size_count; ++index) {
        size_t scan = index;
        ascending[index] = sizes[index];
        while (scan > 0U && ascending[scan - 1U] > ascending[scan]) {
            uint32_t swap = ascending[scan - 1U];
            ascending[scan - 1U] = ascending[scan];
            ascending[scan] = swap;
            --scan;
        }
    }

    /* 3. One bounded read, one CRC pass per surviving layout.  span is at
     * most XX_UBOOT_MAX_ENV_SIZE and at most the remainder. */
    block = (uint8_t *)xx_mem_alloc(span);
    if (!block) goto fail;
    if (!xx_uboot_read_at(self->device, self->base_address, block, span)) {
        goto fail;
    }
    for (layout_index = 0U; layout_index < 2U; ++layout_index) {
        uint32_t crc = 0U;
        uint32_t done = prefixes[layout_index];
        if (!layout_ok[layout_index]) continue;
        for (index = 0U; index < size_count; ++index) {
            if (pd && xx_pd_is_stopped(pd)) goto fail;
            crc = xx_crc32_calc(crc, block + done, ascending[index] - done);
            done = ascending[index];
            crcs[layout_index][index] = crc;
        }
    }

    /* 4. Accept in order of preference. */
    for (index = 0U; index < size_count; ++index) {
        size_t rank;
        for (rank = 0U; rank < size_count; ++rank) {
            if (ascending[rank] == sizes[index]) break;
        }
        for (layout_index = 0U; layout_index < 2U; ++layout_index) {
            uint32_t prefix = prefixes[layout_index];
            uint32_t crc;
            if (!layout_ok[layout_index] || rank >= size_count) continue;
            crc = crcs[layout_index][rank];
            if (crc != stored_le && crc != stored_be) continue;
            if (!xx_uboot_parse_entries(parsed, block + prefix,
                                        sizes[index] - prefix)) {
                xx_uboot_private_reset_variables(parsed);
                continue;
            }
            parsed->layout = layouts[layout_index];
            parsed->env_size = sizes[index];
            parsed->data_size = sizes[index] - prefix;
            parsed->big_endian = (crc != stored_le);
            parsed->crc32 = crc;
            parsed->flags = (layout_index == 1U) ? block[4] : 0U;
            parsed->archive_end = self->base_address + (int64_t)sizes[index];
            xx_mem_free(block);
            return true;
        }
    }
fail:
    if (block) xx_mem_free(block);
    xx_uboot_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_uboot_init(xx_uboot *uboot, xx_io_device *dev, int64_t base_address) {
    if (!uboot) return;
    xx_mem_zero(uboot, sizeof(*uboot));
    xx_format_init(&uboot->format, dev, base_address);
    /* The CRC is stored in the target's byte order; this is replaced by the
     * order actually found once the block has been recognised. */
    uboot->format.endian = XX_ENDIAN_LITTLE;
    uboot->format.file_type = XX_UBOOT_FILE_TYPE;
    /* Not an archive: there is no payload inside to hand to another reader. */
    uboot->format.format_type = XX_TYPE_RAW;
    uboot->format.is_archive = false;
    xx_format_set_mime_type(&uboot->format, "application/x-uboot-environment");
    xx_format_set_extension(&uboot->format, "env");
    uboot->format.check_is_valid = xx_uboot_check_is_valid;
    uboot->format.handle_base_info = xx_uboot_handle_base_info;
    uboot->format.get_format_size = xx_uboot_get_format_size;
    uboot->format.destroy = xx_uboot_vtable_destroy;
    uboot->layout = XX_UBOOT_LAYOUT_NONE;
    uboot->archive_end = -1;
}

xx_uboot *xx_uboot_create(xx_io_device *dev, int64_t base_address) {
    xx_uboot *uboot = (xx_uboot *)xx_mem_alloc(sizeof(*uboot));
    if (uboot) xx_uboot_init(uboot, dev, base_address);
    return uboot;
}

void xx_uboot_destroy(xx_uboot *uboot) {
    if (!uboot) return;
    if (uboot->internal) {
        xx_uboot_private_cleanup((xx_uboot_private *)uboot->internal);
        xx_mem_free(uboot->internal);
        uboot->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&uboot->format);
}

static void xx_uboot_vtable_destroy(Abstractformat *self) {
    xx_uboot_destroy((xx_uboot *)self);
}

void xx_uboot_free(xx_uboot *uboot) {
    if (!uboot) return;
    xx_uboot_destroy(uboot);
    xx_mem_free(uboot);
}

void xx_uboot_set_env_size(xx_uboot *uboot, uint32_t env_size) {
    if (!uboot) return;
    if (env_size > XX_UBOOT_MAX_ENV_SIZE) return;
    uboot->env_size = env_size;
}

bool xx_uboot_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_uboot_private parsed;
    bool result = xx_uboot_parse(self, &parsed, pd);
    xx_uboot_private_cleanup(&parsed);
    return result;
}

bool xx_uboot_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_uboot_private *parsed;
    xx_uboot *uboot = (xx_uboot *)self;
    int64_t total_size;
    if (!self || !uboot) return false;
    parsed = (xx_uboot_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_uboot_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (uboot->internal) {
        xx_uboot_private_cleanup((xx_uboot_private *)uboot->internal);
        xx_mem_free(uboot->internal);
    }
    uboot->internal = parsed;
    uboot->layout = parsed->layout;
    uboot->number_of_variables = parsed->count;
    uboot->env_size = parsed->env_size;
    uboot->data_size = parsed->data_size;
    uboot->crc32 = parsed->crc32;
    uboot->flags = parsed->flags;
    uboot->archive_end = parsed->archive_end;
    self->endian = parsed->big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    /* The variables are metadata, not members: the count is published through
     * the metadata counter rather than the archive record counter. */
    self->number_of_archive_records = 0U;
    self->number_of_metadata = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_uboot_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_uboot_get_number_of_variables(const xx_uboot *uboot) {
    return uboot ? uboot->number_of_variables : 0U;
}
uint32_t xx_uboot_get_env_size(const xx_uboot *uboot) {
    return uboot ? uboot->env_size : 0U;
}
uint32_t xx_uboot_get_crc32(const xx_uboot *uboot) {
    return uboot ? uboot->crc32 : 0U;
}
xx_uboot_layout_t xx_uboot_get_layout(const xx_uboot *uboot) {
    return uboot ? uboot->layout : XX_UBOOT_LAYOUT_NONE;
}

const char *xx_uboot_get_variable_key(const xx_uboot *uboot, uint64_t index) {
    const xx_uboot_private *parsed =
        uboot ? (const xx_uboot_private *)uboot->internal : NULL;
    if (!parsed || index >= (uint64_t)parsed->count) return NULL;
    return parsed->variables[(size_t)index].key;
}

const char *xx_uboot_get_variable_value(const xx_uboot *uboot,
                                        uint64_t index) {
    const xx_uboot_private *parsed =
        uboot ? (const xx_uboot_private *)uboot->internal : NULL;
    if (!parsed || index >= (uint64_t)parsed->count) return NULL;
    return parsed->variables[(size_t)index].value;
}

const char *xx_uboot_find_variable(const xx_uboot *uboot, const char *key) {
    const xx_uboot_private *parsed =
        uboot ? (const xx_uboot_private *)uboot->internal : NULL;
    size_t index;
    if (!parsed || !key) return NULL;
    for (index = 0U; index < parsed->count; ++index) {
        if (xx_str_equals(parsed->variables[index].key, key)) {
            return parsed->variables[index].value;
        }
    }
    return NULL;
}
