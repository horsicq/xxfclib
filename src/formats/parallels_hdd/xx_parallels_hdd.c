/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Parallels expanding disk image (.hds), header version 2. The field table
 * and the mapping rules are in xx_parallels_hdd.h; the layout comes from
 * QEMU's docs/interop/parallels.rst.
 *
 * One member is published: the guest disk. Producing it walks the guest
 * clusters in order, fetching the matching BAT entry from a 4 KB window of
 * the table, and either copies the host cluster or writes zeros. Which
 * entries count as mapped follows QEMU's read-only open so that the output
 * is byte-identical to `qemu-img convert -O raw`; the rules are spelled out
 * at phdd_map_cluster().
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/parallels_hdd/xx_parallels_hdd.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as PARALLELS_HDD is registered
 * there. */
#ifdef PARALLELS_HDD
#define XX_PARALLELS_HDD_FILE_TYPE XX_FILE_TYPE_PARALLELS_HDD
#else
#define XX_PARALLELS_HDD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PHDD_HEADER XX_PARALLELS_HDD_HEADER_SIZE
#define PHDD_SECTOR 512U
#define PHDD_VERSION 2U

/* QEMU refuses these, and so does this reader: a zero cluster size, a
 * cluster so large that tracks * 513 overflows an int, and a catalog whose
 * byte size overflows an int. */
#define PHDD_MAX_TRACKS (UINT32_C(0x7FFFFFFF) / 513U)
#define PHDD_MAX_BAT_ENTRIES (UINT32_C(0x7FFFFFFF) / 4U)
#define PHDD_MAX_EXT_OFF (UINT64_C(0x7FFFFFFFFFFFFFFF) >> 9)

/* Guest disks larger than 16 TB are refused rather than listed, as the QCOW
 * reader does: the member size is a header field and extraction writes
 * every byte of it. The old magic can only describe 2 TB anyway. */
#define PHDD_MAX_VIRTUAL_SIZE ((uint64_t)1 << 44)

/* BAT entries fetched per read, and the copy / zero-fill granule. */
#define PHDD_BAT_WINDOW 1024U
#define PHDD_IO_CHUNK 65536U

/* The member name. The data file carries no name of its own. */
#define PHDD_MEMBER_NAME "disk.img"

typedef struct phdd_context_s {
    int64_t base;            /**< Device offset of the header. */
    int64_t input_size;      /**< Bytes present from base to the end. */
    int64_t format_size;     /**< Measured extent, <= input_size. */
    uint64_t nb_sectors;
    uint64_t virtual_size;
    uint64_t ext_off;
    uint64_t file_sectors;   /**< input_size in sectors, rounded up. */
    uint64_t data_start;     /**< Sectors. */
    uint64_t data_end;       /**< Sectors; mapped clusters end at or before. */
    uint64_t multiplier;     /**< BAT unit in sectors: 1 or tracks. */
    uint64_t allocated;      /**< Mapped BAT entries. */
    uint32_t heads;
    uint32_t cylinders;
    uint32_t tracks;
    uint32_t cluster_size;
    uint32_t bat_entries;
    uint32_t in_use;
    uint32_t data_off;
    uint32_t flags;
    bool is_extended;
    bool truncated;
    bool measured;           /**< format_size / allocated are filled in. */
} phdd_context;

typedef struct phdd_bat_s {
    uint8_t raw[PHDD_BAT_WINDOW * 4U];
    uint64_t first;          /**< Index of raw[0]. */
    uint32_t count;          /**< Entries held; 0 = empty. */
} phdd_bat;

typedef struct phdd_stream_s {
    phdd_context context;
    bool consumed;
} phdd_stream;

/* ------------------------------------------------------------- helpers -- */

/* Every read goes through xx_io_seek64: a disk image routinely exceeds 2 GB
 * and long is 32 bits on Win64. */
static bool phdd_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool phdd_write_all(xx_io_device *output, const uint8_t *data,
                           size_t size) {
    size_t done = 0U;

    while (done < size) {
        ssize_t sent = xx_io_write(output, data + done, size - done);
        if (sent <= 0 || (size_t)sent > size - done) return false;
        done += (size_t)sent;
    }
    return true;
}

static uint32_t phdd_le32(const uint8_t *p) {
    return xx_data_get_u32(p, 4U, 0U, false);
}

/* ---------------------------------------------------------------- parse -- */

/* Header, catalog and data area bounds; no BAT entry is read here, so this
 * is all check_is_valid() costs: one 64-byte read. */
static bool phdd_parse_header(Abstractformat *self, phdd_context *ctx) {
    uint8_t header[PHDD_HEADER];
    int64_t total;
    uint64_t min_off;
    uint64_t payload;
    uint64_t used;
    uint32_t version;

    if (!ctx) return false;
    xx_mem_zero(ctx, sizeof(*ctx));
    if (!self || !self->device || self->base_address < 0) return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address ||
        total - self->base_address < (int64_t)PHDD_HEADER ||
        !phdd_read_at(self->device, self->base_address, header,
                      sizeof(header))) {
        return false;
    }
    if (xx_rt_memcmp(header, "WithouFreSpacExt", 16U) == 0) {
        ctx->is_extended = true;
    } else if (xx_rt_memcmp(header, "WithoutFreeSpace", 16U) != 0) {
        return false;
    }
    version = phdd_le32(header + 16);
    if (version != PHDD_VERSION) return false;

    ctx->base = self->base_address;
    ctx->input_size = total - self->base_address;
    ctx->heads = phdd_le32(header + 20);
    ctx->cylinders = phdd_le32(header + 24);
    ctx->tracks = phdd_le32(header + 28);
    ctx->bat_entries = phdd_le32(header + 32);
    ctx->nb_sectors = xx_data_get_u64(header, sizeof(header), 36U, false);
    ctx->in_use = phdd_le32(header + 44);
    ctx->data_off = phdd_le32(header + 48);
    ctx->flags = phdd_le32(header + 52);
    ctx->ext_off = xx_data_get_u64(header, sizeof(header), 56U, false);

    /* The old header only defines the low half of the sector count. */
    if (!ctx->is_extended) ctx->nb_sectors &= UINT64_C(0xFFFFFFFF);

    if (ctx->tracks == 0U || ctx->tracks > PHDD_MAX_TRACKS) return false;
    if (ctx->bat_entries > PHDD_MAX_BAT_ENTRIES) return false;
    if (ctx->ext_off >= PHDD_MAX_EXT_OFF) return false;
    ctx->cluster_size = ctx->tracks * PHDD_SECTOR;
    ctx->multiplier = ctx->is_extended ? (uint64_t)ctx->tracks : 1U;

    /* The catalog has to cover the whole disk (QEMU: "Catalog size too
     * small"). Both factors are bounded above, so the product fits. */
    if ((uint64_t)ctx->bat_entries * ctx->tracks < ctx->nb_sectors) {
        return false;
    }
    if (ctx->nb_sectors > PHDD_MAX_VIRTUAL_SIZE / PHDD_SECTOR) return false;
    ctx->virtual_size = ctx->nb_sectors * PHDD_SECTOR;

    /* The BAT must be present in full. QEMU would read a missing tail as
     * zero entries, but a table that runs off the end of the file is not an
     * image any writer produces, and requiring it ties the largest disk a
     * file can describe to the file's own size. */
    if ((uint64_t)ctx->input_size <
        (uint64_t)PHDD_HEADER + (uint64_t)ctx->bat_entries * 4U) {
        return false;
    }

    /* Data area start, as QEMU's parallels_test_data_off() settles it: the
     * header value when it lies between the end of the BAT (rounded up to a
     * cluster for the new magic) and the end of the file, that minimum
     * otherwise. */
    ctx->file_sectors =
        ((uint64_t)ctx->input_size + PHDD_SECTOR - 1U) / PHDD_SECTOR;
    min_off = ((uint64_t)PHDD_HEADER + (uint64_t)ctx->bat_entries * 4U +
               PHDD_SECTOR - 1U) / PHDD_SECTOR;
    if (ctx->is_extended) {
        min_off = (min_off + ctx->tracks - 1U) / ctx->tracks * ctx->tracks;
    }
    ctx->data_start = min_off;
    if (ctx->data_off != 0U && (uint64_t)ctx->data_off >= min_off &&
        (uint64_t)ctx->data_off <= ctx->file_sectors) {
        ctx->data_start = ctx->data_off;
    }

    /* The data area ends at the file's last cluster, counted in whole
     * clusters from the data start (QEMU's used bitmap). A cluster that
     * reaches past it reads as zeros. */
    used = 0U;
    if ((uint64_t)ctx->input_size >= ctx->data_start * PHDD_SECTOR) {
        payload = (uint64_t)ctx->input_size - ctx->data_start * PHDD_SECTOR;
        used = (payload + ctx->cluster_size - 1U) / ctx->cluster_size;
    }
    ctx->data_end = ctx->data_start + used * ctx->tracks;
    return true;
}

/* Fetch BAT entry index through the window. */
static bool phdd_bat_get(xx_io_device *device, const phdd_context *ctx,
                         phdd_bat *bat, uint64_t index, uint32_t *entry) {
    if (index >= (uint64_t)ctx->bat_entries) {
        *entry = 0U;
        return true;
    }
    if (bat->count == 0U || index < bat->first ||
        index - bat->first >= (uint64_t)bat->count) {
        uint64_t left = (uint64_t)ctx->bat_entries - index;
        uint32_t count = left < PHDD_BAT_WINDOW ? (uint32_t)left
                                                 : PHDD_BAT_WINDOW;
        bat->count = 0U;
        if (!phdd_read_at(device,
                          ctx->base + (int64_t)PHDD_HEADER +
                              (int64_t)(index * 4U),
                          bat->raw, (size_t)count * 4U)) {
            return false;
        }
        bat->first = index;
        bat->count = count;
    }
    *entry = phdd_le32(bat->raw + (size_t)(index - bat->first) * 4U);
    return true;
}

/* Host sector of a BAT entry, or false when the cluster reads as zeros:
 * the entry is 0, the cluster starts before the data area, or it ends past
 * the data area's last cluster. These are the three cases QEMU's
 * seek_to_sector() turns into "not allocated". */
static bool phdd_map_cluster(const phdd_context *ctx, uint32_t entry,
                             uint64_t *sector) {
    uint64_t host;

    if (entry == 0U) return false;
    host = (uint64_t)entry * ctx->multiplier;
    if (host < ctx->data_start || host + ctx->tracks > ctx->data_end) {
        return false;
    }
    *sector = host;
    return true;
}

/* Walk the whole BAT once: count mapped clusters and find the furthest byte
 * the image owns. The walk is bounded by the table, which parse already
 * required to be inside the file. */
static bool phdd_measure(xx_io_device *device, phdd_context *ctx,
                         xx_pd_struct *pd) {
    phdd_bat *bat;
    uint64_t end;
    uint64_t index;
    uint64_t sector;
    uint32_t entry;

    end = (uint64_t)PHDD_HEADER + (uint64_t)ctx->bat_entries * 4U;
    if (ctx->data_start * PHDD_SECTOR > end) {
        end = ctx->data_start * PHDD_SECTOR;
    }
    if (ctx->ext_off != 0U &&
        ctx->ext_off + ctx->tracks <= ctx->file_sectors &&
        (ctx->ext_off + ctx->tracks) * PHDD_SECTOR > end) {
        end = (ctx->ext_off + ctx->tracks) * PHDD_SECTOR;
    }
    bat = (phdd_bat *)xx_mem_alloc(sizeof(*bat));
    if (!bat) return false;
    bat->count = 0U;
    bat->first = 0U;
    ctx->allocated = 0U;
    ctx->truncated = false;
    for (index = 0U; index < (uint64_t)ctx->bat_entries; ++index) {
        if ((index & 0xFFFFU) == 0U && pd && xx_pd_is_stopped(pd)) {
            xx_mem_free(bat);
            return false;
        }
        if (!phdd_bat_get(device, ctx, bat, index, &entry)) {
            xx_mem_free(bat);
            return false;
        }
        if (!phdd_map_cluster(ctx, entry, &sector)) continue;
        ++ctx->allocated;
        if ((sector + ctx->tracks) * PHDD_SECTOR > end) {
            end = (sector + ctx->tracks) * PHDD_SECTOR;
        }
    }
    xx_mem_free(bat);
    if (end > (uint64_t)ctx->input_size) {
        ctx->truncated = ctx->allocated != 0U;
        end = (uint64_t)ctx->input_size;
    }
    ctx->format_size = (int64_t)end;
    ctx->measured = true;
    return true;
}

/* ------------------------------------------------------------- produce -- */

/* Produce the guest disk. With output NULL nothing is written and only the
 * BAT entries that cover the disk are fetched. */
static bool phdd_write_image(xx_io_device *device, const phdd_context *ctx,
                             xx_io_device *output, xx_pd_struct *pd) {
    phdd_bat *bat;
    uint8_t *buffer = NULL;
    uint64_t remaining = ctx->virtual_size;
    uint64_t cluster = 0U;
    bool result = true;

    bat = (phdd_bat *)xx_mem_alloc(sizeof(*bat));
    if (output) buffer = (uint8_t *)xx_mem_alloc(PHDD_IO_CHUNK);
    if (!bat || (output && !buffer)) {
        if (bat) xx_mem_free(bat);
        if (buffer) xx_mem_free(buffer);
        return false;
    }
    bat->count = 0U;
    bat->first = 0U;

    while (remaining != 0U && result) {
        uint64_t length = remaining < (uint64_t)ctx->cluster_size
                              ? remaining : (uint64_t)ctx->cluster_size;
        uint64_t sector = 0U;
        uint64_t done = 0U;
        uint32_t entry = 0U;
        bool mapped;

        if (pd && xx_pd_is_stopped(pd)) {
            result = false;
            break;
        }
        if (!phdd_bat_get(device, ctx, bat, cluster, &entry)) {
            result = false;
            break;
        }
        mapped = phdd_map_cluster(ctx, entry, &sector);
        while (output && done < length) {
            size_t piece = (length - done) < (uint64_t)PHDD_IO_CHUNK
                               ? (size_t)(length - done) : PHDD_IO_CHUNK;
            size_t have = 0U;

            if (mapped) {
                /* The mapped cluster ends inside data_end, which is at most
                 * one cluster past the end of the file, so this offset is
                 * small and positive; only the tail may be missing. */
                uint64_t at = sector * PHDD_SECTOR + done;
                if (at < (uint64_t)ctx->input_size) {
                    uint64_t left = (uint64_t)ctx->input_size - at;
                    have = left < (uint64_t)piece ? (size_t)left : piece;
                    if (!phdd_read_at(device, ctx->base + (int64_t)at, buffer,
                                      have)) {
                        result = false;
                        break;
                    }
                }
            }
            if (have < piece) xx_mem_zero(buffer + have, piece - have);
            if (!phdd_write_all(output, buffer, piece)) {
                result = false;
                break;
            }
            done += (uint64_t)piece;
        }
        remaining -= length;
        ++cluster;
    }
    xx_mem_free(bat);
    if (buffer) xx_mem_free(buffer);
    return result;
}

/* ------------------------------------------------------------ lifecycle -- */

void xx_parallels_hdd_init(xx_parallels_hdd *image, xx_io_device *dev,
                           int64_t base_address) {
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    xx_format_init(&image->format, dev, base_address);
    image->format.endian = XX_ENDIAN_LITTLE;
    image->format.file_type = XX_PARALLELS_HDD_FILE_TYPE;
    image->format.format_type = XX_TYPE_ARCHIVE;
    image->format.is_archive = true;
    xx_format_set_mime_type(&image->format, "application/x-parallels-disk");
    xx_format_set_extension(&image->format, "hds");
    image->format.check_is_valid = xx_parallels_hdd_check_is_valid;
    image->format.handle_base_info = xx_parallels_hdd_handle_base_info;
    image->format.get_format_size = xx_parallels_hdd_get_format_size;
    image->format.get_number_of_archive_records =
        xx_parallels_hdd_get_number_of_archive_records;
    image->format.create_archive_records_reading =
        xx_parallels_hdd_create_archive_records_reading;
    image->format.get_current_archive_record =
        xx_parallels_hdd_get_current_archive_record;
    image->format.unpack_current_archive_record =
        xx_parallels_hdd_unpack_current_archive_record;
    image->format.archive_record_move_to_next =
        xx_parallels_hdd_archive_record_move_to_next;
    image->format.free_archive_records_reading =
        xx_parallels_hdd_free_archive_records_reading;
}

xx_parallels_hdd *xx_parallels_hdd_create(xx_io_device *dev,
                                          int64_t base_address) {
    xx_parallels_hdd *image =
        (xx_parallels_hdd *)xx_mem_alloc(sizeof(*image));

    if (image) xx_parallels_hdd_init(image, dev, base_address);
    return image;
}

void xx_parallels_hdd_destroy(xx_parallels_hdd *image) {
    if (image) xx_format_cleanup_extra_parameters(&image->format);
}

void xx_parallels_hdd_free(xx_parallels_hdd *image) {
    if (!image) return;
    xx_parallels_hdd_destroy(image);
    xx_mem_free(image);
}

/* --------------------------------------------------------------- format -- */

bool xx_parallels_hdd_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    phdd_context ctx;

    (void)pd;
    return phdd_parse_header(self, &ctx);
}

bool xx_parallels_hdd_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd) {
    xx_parallels_hdd *image = (xx_parallels_hdd *)self;
    phdd_context ctx;

    if (!self || !phdd_parse_header(self, &ctx) ||
        !phdd_measure(self->device, &ctx, pd)) {
        if (self) {
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    image->number_of_records = 1U;
    image->virtual_size = ctx.virtual_size;
    image->nb_sectors = ctx.nb_sectors;
    image->ext_off = ctx.ext_off;
    image->data_start = ctx.data_start;
    image->allocated_clusters = ctx.allocated;
    image->version = PHDD_VERSION;
    image->heads = ctx.heads;
    image->cylinders = ctx.cylinders;
    image->tracks = ctx.tracks;
    image->cluster_size = ctx.cluster_size;
    image->bat_entries = ctx.bat_entries;
    image->in_use = ctx.in_use;
    image->data_off = ctx.data_off;
    image->flags = ctx.flags;
    image->is_extended = ctx.is_extended;
    image->truncated = ctx.truncated;
    self->format_size = ctx.format_size;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_parallels_hdd_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_parallels_hdd_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_parallels_hdd_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_parallels_hdd_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_parallels_hdd *)self)->number_of_records;
}

bool xx_parallels_hdd_unpack_to_device(xx_parallels_hdd *image,
                                       xx_io_device *destination,
                                       xx_pd_struct *pd) {
    phdd_context ctx;

    if (!image || !phdd_parse_header(&image->format, &ctx)) return false;
    return phdd_write_image(image->format.device, &ctx, destination, pd);
}

/* -------------------------------------------------------------- records -- */

static bool phdd_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
    size_t index;

    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *phdd_find_option(const xx_list_s *options,
                                      uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool phdd_populate_record(xx_archive_record *record,
                                 const phdd_context *ctx) {
    uint64_t data = ctx->data_start * PHDD_SECTOR;

    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = ctx->base;
    record->header_size = (int64_t)PHDD_HEADER + (int64_t)ctx->bat_entries * 4;
    /* The guest clusters are scattered through the data area; the record
     * points at its start. */
    if (data > (uint64_t)ctx->format_size) data = (uint64_t)ctx->format_size;
    record->data_offset = ctx->base + (int64_t)data;
    record->compressed_size = ctx->format_size;
    return xx_archive_record_set_original_name(record, PHDD_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          ctx->virtual_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)ctx->format_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static void phdd_stream_free(void *pointer) {
    if (pointer) xx_mem_free(pointer);
}

xx_archive_record_state *xx_parallels_hdd_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    phdd_stream *stream;

    if (!self || !self->device) return NULL;
    stream = (phdd_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!phdd_parse_header(self, &stream->context) ||
        !phdd_measure(self->device, &stream->context, pd)) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = phdd_stream_free;
    state->total_records = 1U;
    if (!phdd_copy_options(&state->options, options) ||
        !phdd_populate_record(&state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_parallels_hdd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_parallels_hdd_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    phdd_stream *stream;

    (void)pd;
    if (!self || !state || state->format != self || !state->has_record) {
        return false;
    }
    /* One member, so the first step is always the last. */
    stream = (phdd_stream *)state->internal_state;
    if (stream) stream->consumed = true;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_parallels_hdd_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    phdd_stream *stream;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    xx_io_device *output = NULL;
    bool result;
    bool created = false;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (phdd_stream *)state->internal_state;
    if (!stream || stream->consumed) return false;

    option = phdd_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        return phdd_write_image(self->device, &stream->context, NULL, pd);
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) {
        if (owned_base) xx_str_free(owned_base);
        return false;
    }
    /* The member name is a constant, so there is nothing in the file that
     * could steer the output path. */
    if (base[0] != '\0' && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", PHDD_MEMBER_NAME);
    } else {
        destination = xx_str_concat(base, PHDD_MEMBER_NAME);
    }
    if (owned_base) xx_str_free(owned_base);
    if (!destination) return false;
    if (!xx_store_create_dirs_a(destination, false)) {
        xx_str_free(destination);
        return false;
    }
    output = xx_io_file_open(destination, "wb");
    created = output != NULL;
    result = output != NULL &&
             phdd_write_image(self->device, &stream->context, output, pd);
    if (output && xx_io_close(output) != 0) result = false;
    if (!result && created) xx_rt_remove(destination);
    xx_str_free(destination);
    return result;
}

void xx_parallels_hdd_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
