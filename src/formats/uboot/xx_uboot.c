/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/uboot/xx_uboot.h"

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
} xx_uboot_private;

static void xx_uboot_vtable_destroy(Abstractformat *self);

/*
 * The block sizes real boards use for CONFIG_ENV_SIZE.  The value is not
 * stored anywhere in the block, so recognising one means trying sizes until
 * the CRC comes out.  Largest first: a short candidate can never validate
 * against a longer real block (the CRC would be over the wrong range), while
 * trying small sizes first wastes passes on the common large cases.
 */
static const uint32_t xx_uboot_candidate_sizes[] = {
    0x40000U, 0x20000U, 0x10000U, 0x8000U, 0x4000U,
    0x2000U,  0x1000U,  0x800U,   0x400U};
#define XX_UBOOT_CANDIDATE_COUNT \
    (sizeof(xx_uboot_candidate_sizes) / sizeof(xx_uboot_candidate_sizes[0]))

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

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_uboot_range_within(int64_t total_size, int64_t offset,
                                  int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
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

static bool xx_uboot_push(xx_uboot_private *parsed, const uint8_t *entry,
                          size_t entry_length, size_t separator) {
    xx_uboot_variable *slot;
    size_t index;
    if (!parsed || parsed->count >= XX_UBOOT_MAX_VARIABLES) return false;
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
 * first byte is examined; by XX_UBOOT_MAX_ENTRY_LENGTH per entry; and by
 * XX_UBOOT_MAX_VARIABLES overall.  A block with no terminating empty entry
 * therefore stops at the end of the data area instead of scanning on, and a
 * block that is all one-byte entries cannot allocate without limit.
 */
static bool xx_uboot_parse_entries(xx_uboot_private *parsed,
                                   const uint8_t *data, size_t data_size) {
    size_t position = 0U;
    while (position < data_size) {
        size_t cursor;
        size_t separator = 0U;
        bool have_separator = false;
        size_t entry_length;
        /* An empty entry is the terminator; 0xFF is the NOR erase value and
         * means the padding started without one. */
        if (data[position] == 0x00U || data[position] == 0xFFU) break;
        for (cursor = position; cursor < data_size; ++cursor) {
            if (data[cursor] == 0x00U) break;
            if (!have_separator && data[cursor] == (uint8_t)'=') {
                separator = cursor - position;
                have_separator = true;
            }
            if (cursor - position >= XX_UBOOT_MAX_ENTRY_LENGTH) return false;
        }
        if (cursor >= data_size) {
            /* The last entry runs off the end of the block: truncated, so it
             * is dropped rather than half-reported.  Everything before it is
             * still good, and the CRC has already vouched for the bytes. */
            break;
        }
        entry_length = cursor - position;
        /* Every U-Boot setting is "key=value"; an entry without a separator,
         * or with an empty key, means this is not an environment block. */
        if (!have_separator || separator == 0U) return false;
        if (!xx_uboot_push(parsed, data + position, entry_length, separator)) {
            return false;
        }
        position = cursor + 1U;
    }
    return parsed->count != 0U;
}

/*
 * Try one (size, layout) combination.  Returns true only when the stored CRC
 * matches a fresh CRC32 over the whole data area AND the data area parses.
 *
 * U-Boot's crc32() is zlib's, i.e. ordinary ISO-HDLC CRC32 with the final
 * complement - so xx_crc32_calc(0, ...) computes it directly.  No JAMCRC
 * complement of the kind the TRX reader needs applies here.
 */
static bool xx_uboot_try(Abstractformat *self, xx_uboot_private *parsed,
                         uint32_t env_size, xx_uboot_layout_t layout,
                         xx_pd_struct *pd) {
    uint8_t header[XX_UBOOT_CRC_SIZE + 1U];
    uint8_t *data = NULL;
    uint32_t prefix =
        (layout == XX_UBOOT_LAYOUT_REDUNDANT) ? (XX_UBOOT_CRC_SIZE + 1U)
                                              : XX_UBOOT_CRC_SIZE;
    uint32_t data_size;
    uint32_t stored;
    if (env_size <= prefix || env_size > XX_UBOOT_MAX_ENV_SIZE) return false;
    data_size = env_size - prefix;
    if (!xx_uboot_range_within(parsed->input_size, self->base_address,
                               (int64_t)env_size) ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    if (!xx_uboot_read_at(self->device, self->base_address, header, prefix)) {
        return false;
    }
    stored = xx_data_get_u32(header, sizeof(header), 0U, false);
    /* data_size is capped by XX_UBOOT_MAX_ENV_SIZE above, so this allocation
     * is bounded no matter what the device holds. */
    data = (uint8_t *)xx_mem_alloc(data_size);
    if (!data) return false;
    if (!xx_uboot_read_at(self->device, self->base_address + (int64_t)prefix,
                          data, data_size) ||
        xx_crc32_calc(0U, data, data_size) != stored) {
        xx_mem_free(data);
        return false;
    }
    parsed->layout = layout;
    parsed->env_size = env_size;
    parsed->data_size = data_size;
    parsed->crc32 = stored;
    parsed->flags = (layout == XX_UBOOT_LAYOUT_REDUNDANT) ? header[4] : 0U;
    parsed->archive_end = self->base_address + (int64_t)env_size;
    if (!xx_uboot_parse_entries(parsed, data, data_size)) {
        xx_mem_free(data);
        /* Undo the partial fill so the next candidate starts clean. */
        xx_uboot_private_cleanup(parsed);
        parsed->input_size = xx_io_total_size(self->device);
        return false;
    }
    xx_mem_free(data);
    return true;
}

static bool xx_uboot_parse(Abstractformat *self, xx_uboot_private *parsed,
                           xx_pd_struct *pd) {
    static const xx_uboot_layout_t layouts[2] = {XX_UBOOT_LAYOUT_PLAIN,
                                                 XX_UBOOT_LAYOUT_REDUNDANT};
    const xx_uboot *uboot = (const xx_uboot *)self;
    uint32_t forced;
    int64_t remaining;
    size_t layout_index;
    size_t size_index;
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
    remaining = parsed->input_size - self->base_address;
    if (remaining < (int64_t)(XX_UBOOT_CRC_SIZE + 2U)) goto fail;

    /* A caller that knows CONFIG_ENV_SIZE pins it and the search is skipped. */
    forced = uboot->env_size;
    if (forced != 0U) {
        for (layout_index = 0U; layout_index < 2U; ++layout_index) {
            if (xx_uboot_try(self, parsed, forced, layouts[layout_index], pd)) {
                return true;
            }
        }
        goto fail;
    }

    /* The whole remainder of the device first: a dumped environment file is
     * exactly one block and nothing else, which is the common standalone
     * case and the only one where the size is known for certain. */
    if (remaining <= (int64_t)XX_UBOOT_MAX_ENV_SIZE) {
        for (layout_index = 0U; layout_index < 2U; ++layout_index) {
            if (xx_uboot_try(self, parsed, (uint32_t)remaining,
                             layouts[layout_index], pd)) {
                return true;
            }
        }
    }
    for (size_index = 0U; size_index < XX_UBOOT_CANDIDATE_COUNT; ++size_index) {
        uint32_t candidate = xx_uboot_candidate_sizes[size_index];
        if (candidate < XX_UBOOT_MIN_ENV_SIZE) continue;
        if ((int64_t)candidate > remaining) continue;
        for (layout_index = 0U; layout_index < 2U; ++layout_index) {
            if (xx_uboot_try(self, parsed, candidate, layouts[layout_index],
                             pd)) {
                return true;
            }
        }
    }
fail:
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
    /* The CRC field is little endian regardless of the target's byte order:
     * U-Boot stores it through its own env_crc helpers, not as a raw word. */
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
