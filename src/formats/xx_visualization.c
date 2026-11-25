/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_visualization.c
 * @brief Binary visualization core: the C counterpart of XVisualization.
 */

/* xx_format.h first: it brings xx_pd_struct (xxfclib/data/xx_pd.h), which
 * xx_visualization.h uses without including. */
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/xx_visualization.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#include <math.h>

#define XX_VIS_CHUNK_MAX ((size_t)1 << 20)
#define XX_VIS_GRID_MAX 4096U
/* Upper bound for block positions derived from doubles (keeps int64 sums safe). */
#define XX_VIS_BLOCK_LIMIT ((double)((int64_t)1 << 62))

static const uint32_t g_region_colors[11] = {0x808000U, 0x008B8BU, 0xCD853FU, 0x9ACD32U, 0x8FBC8FU, 0xB03060U,
                                             0xF08080U, 0xDA70D6U, 0xF0E68CU, 0x87CEEBU, 0x7B68EEU};

static const uint32_t g_highlight_colors[7] = {0xDC143CU, 0x0000FFU, 0xA020F0U, 0x00FF00U, 0x7F007FU, 0xFF4500U, 0x006400U};

#define XX_VIS_DATA_PARTS                                                                                                      \
    (XX_FILE_PART_TABLE | XX_FILE_PART_RESOURCE | XX_FILE_PART_DEBUG | XX_FILE_PART_SIGNATURE | XX_FILE_PART_STREAM |            \
     XX_FILE_PART_OBJECT | XX_FILE_PART_VALUE | XX_FILE_PART_DATA)

/* ------------------------------------------------------------------ */
/* Statistics                                                          */
/* ------------------------------------------------------------------ */

typedef struct xx_vis_acc {
    uint64_t counts[256];
    uint64_t sum;
    uint64_t zeros;
    uint64_t text;
    uint64_t total;
} xx_vis_acc;

static void xx_vis_acc_reset(xx_vis_acc *acc)
{
    xx_mem_zero(acc, sizeof(*acc));
}

static void xx_vis_acc_add(xx_vis_acc *acc, const uint8_t *data, size_t size, bool want_hist, bool want_sum, bool want_zeros, bool want_text)
{
    size_t i;
    if (want_hist) {
        for (i = 0; i < size; i++) acc->counts[data[i]]++;
    }
    if (want_sum) {
        uint64_t s = 0;
        for (i = 0; i < size; i++) s += data[i];
        acc->sum += s;
    }
    if (want_zeros) {
        uint64_t z = 0;
        for (i = 0; i < size; i++) z += (data[i] == 0);
        acc->zeros += z;
    }
    if (want_text) {
        uint64_t t = 0;
        for (i = 0; i < size; i++) {
            uint8_t c = data[i];
            if ((c >= 32 && c <= 126) || c == 8 || c == 10 || c == 13) t++;
        }
        acc->text += t;
    }
    acc->total += size;
}

/* XBinary::getBinaryStatus: H = -sum(p * log2 p), in its operation order
 * (log(p) times 1 / ln 2) so results match the Qt original bit for bit. */
static double xx_vis_entropy(const uint64_t counts[256], uint64_t total)
{
    const double inv_log2 = 1.4426950408889634073599246810023;
    double result = 0.0;
    double n;
    int j;
    if (total == 0) return 0.0;
    n = (double)total;
    for (j = 0; j < 256; j++) {
        if (counts[j]) {
            double p = (double)counts[j] / n;
            result += -p * (log(p) * inv_log2);
        }
    }
    return result;
}

static double xx_vis_gradient(uint64_t sum, uint64_t total)
{
    return total ? (double)sum / ((double)total * 255.0) : 0.0;
}

static double xx_vis_share(uint64_t count, uint64_t total)
{
    return total ? (double)count / (double)total : 0.0;
}

void xx_visualization_stats(const uint8_t *data, size_t size, double *entropy, double *gradient, double *zeros, double *text)
{
    xx_vis_acc acc;
    xx_vis_acc_reset(&acc);
    if (data && size) xx_vis_acc_add(&acc, data, size, true, true, true, true);
    if (entropy) *entropy = xx_vis_entropy(acc.counts, acc.total);
    if (gradient) *gradient = xx_vis_gradient(acc.sum, acc.total);
    if (zeros) *zeros = xx_vis_share(acc.zeros, acc.total);
    if (text) *text = xx_vis_share(acc.text, acc.total);
}

static int16_t xx_vis_value(double share)
{
    return (int16_t)(100.0 + 200.0 * share);
}

/* ------------------------------------------------------------------ */
/* Simple helpers                                                      */
/* ------------------------------------------------------------------ */

void xx_visualization_init(xx_visualization *vis)
{
    if (!vis) return;
    xx_mem_zero(vis, sizeof(*vis));
    vis->width = 1;
    vis->height = 1;
    vis->methods = XX_VISUALIZATION_DEFAULT_METHODS;
    vis->file_type = XX_FILE_TYPE_UNKNOWN;
    vis->map_mode = XX_MEMORY_MAP_MODE_UNKNOWN;
}

static void xx_vis_clear_output(xx_visualization *vis)
{
    xx_mem_free(vis->blocks);
    xx_mem_free(vis->regions);
    xx_mem_free(vis->highlights);
    vis->blocks = NULL;
    vis->block_count = 0;
    vis->regions = NULL;
    vis->region_count = 0;
    vis->highlights = NULL;
    vis->highlight_count = 0;
    vis->detected_type = XX_FILE_TYPE_UNKNOWN;
    vis->file_size = 0;
    vis->block_size = 0.0;
}

void xx_visualization_free(xx_visualization *vis)
{
    if (!vis) return;
    xx_vis_clear_output(vis);
}

void xx_visualization_default_size(int64_t file_size, uint32_t *width, uint32_t *height)
{
    uint32_t w = 1;
    uint32_t h = 1;
    int64_t pieces = file_size > 0 ? file_size / 0x300 : 0;
    if (pieces > 3) {
        w = pieces < 100 ? (uint32_t)pieces : 100U;
        h = pieces < 100 ? (uint32_t)(2 * pieces) : 200U;
    }
    if (width) *width = w;
    if (height) *height = h;
}

const char *xx_visualization_map_mode_name(xx_memory_map_mode_t mode)
{
    switch (mode) {
        case XX_MEMORY_MAP_MODE_UNKNOWN: return "Default";
        case XX_MEMORY_MAP_MODE_REGIONS: return "Regions";
        case XX_MEMORY_MAP_MODE_SEGMENTS: return "Segments";
        case XX_MEMORY_MAP_MODE_SECTIONS: return "Sections";
        case XX_MEMORY_MAP_MODE_OBJECTS: return "Objects";
        case XX_MEMORY_MAP_MODE_STREAMS: return "Streams";
        case XX_MEMORY_MAP_MODE_MAPS: return "Maps";
        case XX_MEMORY_MAP_MODE_DATA: return "Data";
        default: return "Unknown";
    }
}

const char *xx_visualization_method_name(xx_visualization_method method)
{
    switch (method) {
        case XX_VISUALIZATION_NONE: return "None";
        case XX_VISUALIZATION_ENTROPY: return "Entropy";
        case XX_VISUALIZATION_GRADIENT: return "Gradient";
        case XX_VISUALIZATION_ZEROS: return "Zeros";
        case XX_VISUALIZATION_ZEROS_GRADIENT: return "Zeros(Gradient)";
        case XX_VISUALIZATION_TEXT: return "Text";
        case XX_VISUALIZATION_TEXT_GRADIENT: return "Text(Gradient)";
        default: return "Unknown";
    }
}

uint32_t xx_visualization_region_color(size_t index)
{
    return g_region_colors[index % 11U];
}

uint32_t xx_visualization_highlight_color(size_t index)
{
    return g_highlight_colors[index % 7U];
}

uint32_t xx_visualization_darker(uint32_t rgb, int factor)
{
    uint32_t r, g, b, f;
    if (factor <= 100) return rgb & 0xFFFFFFU;
    f = (uint32_t)factor;
    r = ((rgb >> 16) & 0xFFU) * 100U / f;
    g = ((rgb >> 8) & 0xFFU) * 100U / f;
    b = (rgb & 0xFFU) * 100U / f;
    return (r << 16) | (g << 8) | b;
}

/* ------------------------------------------------------------------ */
/* Regions / highlights                                                */
/* ------------------------------------------------------------------ */

typedef struct xx_vis_area_list {
    xx_visualization_area *items;
    size_t count;
    size_t capacity;
} xx_vis_area_list;

static bool xx_vis_is_data_part(xx_file_part_t part)
{
    return (part & XX_VIS_DATA_PARTS) != 0;
}

static int64_t xx_vis_blocks_of(double value)
{
    if (!(value > 0.0)) return 0; /* also NaN */
    if (value >= XX_VIS_BLOCK_LIMIT) return (int64_t)1 << 62;
    return (int64_t)value;
}

static bool xx_vis_area_push(xx_vis_area_list *list, const xx_memory_record *record, double block_size, uint32_t color)
{
    xx_visualization_area *area;
    double d_offset;
    double d_size;
    size_t i;
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2U : 16U;
        xx_visualization_area *items;
        if (new_capacity < list->capacity || new_capacity > SIZE_MAX / sizeof(*items)) return false;
        items = (xx_visualization_area *)xx_mem_alloc(new_capacity * sizeof(*items));
        if (!items) return false;
        if (list->count) xx_mem_copy(items, list->items, list->count * sizeof(*items));
        xx_mem_free(list->items);
        list->items = items;
        list->capacity = new_capacity;
    }
    area = &list->items[list->count];
    xx_mem_zero(area, sizeof(*area));
    area->enabled = true;
    area->offset = record->offset;
    area->size = record->size;
    area->color = color;
    area->file_part = record->file_part;
    for (i = 0; i + 1 < sizeof(area->name) && i < sizeof(record->name) && record->name[i]; i++) area->name[i] = record->name[i];
    area->name[i] = '\0';
    d_offset = (double)record->offset / block_size;
    d_size = (double)record->size / block_size;
    area->block_offset = xx_vis_blocks_of(d_offset);
    area->block_count = xx_vis_blocks_of(d_size);
    if (d_size > (double)area->block_count) area->block_count++;
    list->count++;
    return true;
}

typedef enum xx_vis_pick {
    XX_VIS_PICK_ALL,
    XX_VIS_PICK_NON_DATA,
    XX_VIS_PICK_DATA
} xx_vis_pick;

static bool xx_vis_collect(xx_vis_area_list *list, const xx_memory_map *map, xx_vis_pick pick, double block_size, bool highlight)
{
    size_t i;
    for (i = 0; i < map->record_count; i++) {
        const xx_memory_record *record = &map->records[i];
        bool is_data;
        if (record->offset < 0 || record->size <= 0) continue;
        is_data = xx_vis_is_data_part(record->file_part);
        if (pick == XX_VIS_PICK_DATA && !is_data) continue;
        if (pick == XX_VIS_PICK_NON_DATA && is_data) continue;
        if (!xx_vis_area_push(list, record, block_size, highlight ? xx_visualization_highlight_color(list->count) : xx_visualization_region_color(list->count))) return false;
    }
    return true;
}

/* A format "has a memory map" when its reader builds one itself. */
static Abstractformat *xx_vis_open_reader(xx_io_device *device, xx_file_type_t type)
{
    Abstractformat *reader;
    if (type == XX_FILE_TYPE_UNKNOWN) return NULL;
    reader = xx_format_create_reader(type, device, 0);
    if (!reader) return NULL;
    if (!reader->get_memory_map) {
        xx_format_free_reader(reader);
        return NULL;
    }
    return reader;
}

/* The map for @p mode, accepted only when the format really produced that mode. */
static const xx_memory_map *xx_vis_get_map(Abstractformat *reader, xx_memory_map_mode_t mode, xx_pd_struct *pd)
{
    const xx_memory_map *map = xx_format_get_memory_map(reader, mode, pd);
    if (!map) return NULL;
    if (mode != XX_MEMORY_MAP_MODE_UNKNOWN && map->mode != mode) return NULL;
    return map;
}

static bool xx_vis_build_areas(xx_visualization *vis, xx_io_device *device, xx_pd_struct *pd)
{
    xx_vis_area_list regions = {NULL, 0, 0};
    xx_vis_area_list highlights = {NULL, 0, 0};
    xx_file_type_t type = vis->file_type;
    Abstractformat *reader;
    const xx_memory_map *map;
    bool data_map = false;

    if (type == XX_FILE_TYPE_UNKNOWN) type = xx_format_get_file_type_device(device);
    vis->detected_type = type;
    if (xx_pd_is_stopped(pd)) return false;

    /* Regions: the chosen map mode, without the data-like records. */
    reader = xx_vis_open_reader(device, type);
    if (reader) {
        map = xx_vis_get_map(reader, vis->map_mode, pd);
        if (map && !xx_vis_collect(&regions, map, XX_VIS_PICK_NON_DATA, vis->block_size, false)) goto fail_reader;
        xx_format_free_reader(reader);
        reader = NULL;
    }
    if (xx_pd_is_stopped(pd)) goto fail;

    /* Highlights: the DATA map, else the data-like records of the default map. */
    reader = xx_vis_open_reader(device, type);
    if (reader) {
        map = xx_vis_get_map(reader, XX_MEMORY_MAP_MODE_DATA, pd);
        if (map) {
            data_map = true;
            if (!xx_vis_collect(&highlights, map, XX_VIS_PICK_ALL, vis->block_size, true)) goto fail_reader;
        }
        xx_format_free_reader(reader);
        reader = NULL;
        if (!data_map && !xx_pd_is_stopped(pd)) {
            reader = xx_vis_open_reader(device, type);
            if (reader) {
                map = xx_vis_get_map(reader, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
                if (map && !xx_vis_collect(&highlights, map, XX_VIS_PICK_DATA, vis->block_size, true)) goto fail_reader;
                xx_format_free_reader(reader);
                reader = NULL;
            }
        }
    }
    if (xx_pd_is_stopped(pd)) goto fail;

    vis->regions = regions.items;
    vis->region_count = regions.count;
    vis->highlights = highlights.items;
    vis->highlight_count = highlights.count;
    return true;

fail_reader:
    xx_format_free_reader(reader);
fail:
    xx_mem_free(regions.items);
    xx_mem_free(highlights.items);
    return false;
}

size_t xx_visualization_map_modes(xx_io_device *device, xx_file_type_t file_type, xx_memory_map_mode_t *modes, size_t capacity)
{
    size_t count = 0;
    int m;
    if (!device) return 0;
    if (modes && capacity > 0) modes[0] = XX_MEMORY_MAP_MODE_UNKNOWN;
    count = 1;
    if (file_type == XX_FILE_TYPE_UNKNOWN) file_type = xx_format_get_file_type_device(device);
    for (m = (int)XX_MEMORY_MAP_MODE_REGIONS; m <= (int)XX_MEMORY_MAP_MODE_DATA; m++) {
        Abstractformat *reader = xx_vis_open_reader(device, file_type);
        const xx_memory_map *map;
        bool ok = false;
        if (!reader) break;
        map = xx_vis_get_map(reader, (xx_memory_map_mode_t)m, NULL);
        if (map) {
            size_t i;
            for (i = 0; i < map->record_count; i++) {
                if (map->records[i].offset >= 0 && map->records[i].size > 0) {
                    ok = true;
                    break;
                }
            }
        }
        xx_format_free_reader(reader);
        if (ok) {
            if (modes && count < capacity) modes[count] = (xx_memory_map_mode_t)m;
            count++;
        }
    }
    return count < capacity || !modes ? count : capacity;
}

/* ------------------------------------------------------------------ */
/* Processing                                                          */
/* ------------------------------------------------------------------ */

/* floor(i * n / blocks) without overflowing int64. */
static int64_t xx_vis_block_start(uint64_t i, uint64_t n, uint64_t blocks)
{
    uint64_t q = n / blocks;
    uint64_t r = n % blocks;
    return (int64_t)(i * q + (i * r) / blocks);
}

bool xx_visualization_process(xx_visualization *vis, xx_io_device *device, xx_pd_struct *pd)
{
    uint64_t blocks;
    uint64_t file_size;
    uint64_t max_block;
    uint64_t i;
    size_t buffer_size;
    uint8_t *buffer = NULL;
    xx_vis_acc *acc = NULL;
    bool want_entropy, want_gradient, want_zeros, want_text;
    int level;
    int64_t total;

    if (!vis) return false;
    xx_vis_clear_output(vis);
    if (!device) return false;
    if (vis->width < 1 || vis->width > XX_VIS_GRID_MAX || vis->height < 1 || vis->height > XX_VIS_GRID_MAX) return false;
    blocks = (uint64_t)vis->width * (uint64_t)vis->height;
    if (blocks > XX_VISUALIZATION_MAX_BLOCKS) return false;
    total = xx_io_total_size(device);
    if (total <= 0) return false;
    if (xx_pd_is_stopped(pd)) return false;
    file_size = (uint64_t)total;

    want_entropy = (vis->methods & XX_VISUALIZATION_FLAG(XX_VISUALIZATION_ENTROPY)) != 0;
    want_gradient = (vis->methods & XX_VISUALIZATION_FLAG(XX_VISUALIZATION_GRADIENT)) != 0;
    want_zeros = (vis->methods & (XX_VISUALIZATION_FLAG(XX_VISUALIZATION_ZEROS) | XX_VISUALIZATION_FLAG(XX_VISUALIZATION_ZEROS_GRADIENT))) != 0;
    want_text = (vis->methods & (XX_VISUALIZATION_FLAG(XX_VISUALIZATION_TEXT) | XX_VISUALIZATION_FLAG(XX_VISUALIZATION_TEXT_GRADIENT))) != 0;

    vis->blocks = (xx_visualization_block *)xx_mem_calloc((size_t)blocks, sizeof(xx_visualization_block));
    acc = (xx_vis_acc *)xx_mem_alloc(sizeof(*acc));
    max_block = file_size / blocks + ((file_size % blocks) ? 1U : 0U);
    buffer_size = max_block < (uint64_t)XX_VIS_CHUNK_MAX ? (size_t)max_block : XX_VIS_CHUNK_MAX;
    if (buffer_size == 0) buffer_size = 1;
    buffer = (uint8_t *)xx_mem_alloc(buffer_size);
    if (!vis->blocks || !acc || !buffer) goto fail;
    vis->block_count = (size_t)blocks;
    vis->file_size = total;
    vis->block_size = (double)total / (double)blocks;

    level = xx_pd_enter_level(pd, blocks, "Visualization");

    for (i = 0; i < blocks; i++) {
        xx_visualization_block *block = &vis->blocks[i];
        int64_t start = xx_vis_block_start(i, file_size, blocks);
        int64_t end = xx_vis_block_start(i + 1, file_size, blocks);
        int64_t pos = start;
        int m;

        if (xx_pd_is_stopped(pd)) goto fail_level;
        block->offset = start;
        block->size = end - start;
        for (m = 0; m < XX_VISUALIZATION_METHOD_COUNT; m++) block->value[m] = 100;

        if (block->size == 0 || !(want_entropy || want_gradient || want_zeros || want_text)) {
            if (level >= 0) xx_pd_set_current(pd, level, i + 1);
            continue;
        }

        xx_vis_acc_reset(acc);
        while (pos < end) {
            uint64_t left = (uint64_t)(end - pos);
            size_t chunk = left < (uint64_t)buffer_size ? (size_t)left : buffer_size;
            if (!xx_io_read_at(device, pos, buffer, chunk)) goto fail_level;
            xx_vis_acc_add(acc, buffer, chunk, want_entropy, want_gradient, want_zeros, want_text);
            pos += (int64_t)chunk;
            if (xx_pd_is_stopped(pd)) goto fail_level;
        }

        if (want_entropy) {
            block->entropy = xx_vis_entropy(acc->counts, acc->total);
            block->value[XX_VISUALIZATION_ENTROPY] = (int16_t)(100.0 + (200.0 * block->entropy) / 8.0);
        }
        if (want_gradient) {
            block->gradient = xx_vis_gradient(acc->sum, acc->total);
            block->value[XX_VISUALIZATION_GRADIENT] = xx_vis_value(block->gradient);
        }
        if (want_zeros) {
            block->zeros = xx_vis_share(acc->zeros, acc->total);
            block->value[XX_VISUALIZATION_ZEROS_GRADIENT] = xx_vis_value(block->zeros);
            block->value[XX_VISUALIZATION_ZEROS] = (block->zeros == 1.0) ? 300 : 100;
        }
        if (want_text) {
            block->text = xx_vis_share(acc->text, acc->total);
            block->value[XX_VISUALIZATION_TEXT_GRADIENT] = xx_vis_value(block->text);
            block->value[XX_VISUALIZATION_TEXT] = (block->text == 1.0) ? 300 : 100;
        }
        if (level >= 0) xx_pd_set_current(pd, level, i + 1);
    }

    if (level >= 0) xx_pd_leave_level(pd, level);
    xx_mem_free(buffer);
    xx_mem_free(acc);
    buffer = NULL;
    acc = NULL;

    if (!xx_vis_build_areas(vis, device, pd)) goto fail;
    return true;

fail_level:
    if (level >= 0) xx_pd_leave_level(pd, level);
fail:
    xx_mem_free(buffer);
    xx_mem_free(acc);
    xx_vis_clear_output(vis);
    return false;
}

/* ------------------------------------------------------------------ */
/* Colours and rendering                                               */
/* ------------------------------------------------------------------ */

static const xx_visualization_area *xx_vis_find_area(const xx_visualization_area *areas, size_t count, int64_t index)
{
    size_t i;
    if (!areas) return NULL;
    for (i = 0; i < count; i++) {
        const xx_visualization_area *area = &areas[i];
        if (area->enabled && index >= area->block_offset && index - area->block_offset < area->block_count) return area;
    }
    return NULL;
}

uint32_t xx_visualization_block_color(const xx_visualization *vis, size_t index, xx_visualization_method method, uint32_t base_rgb)
{
    const xx_visualization_area *area;
    int value = 100;
    int64_t idx;
    if (!vis) return base_rgb & 0xFFFFFFU;
    if (vis->blocks && index < vis->block_count && (unsigned)method < (unsigned)XX_VISUALIZATION_METHOD_COUNT) {
        value = vis->blocks[index].value[method];
    }
    idx = index > (size_t)INT64_MAX ? INT64_MAX : (int64_t)index;
    area = xx_vis_find_area(vis->highlights, vis->highlight_count, idx);
    if (area) {
        if (area->block_count == 1) return area->color & 0xFFFFFFU;
        return xx_visualization_darker(area->color, value);
    }
    area = xx_vis_find_area(vis->regions, vis->region_count, idx);
    if (area) return xx_visualization_darker(area->color, value);
    return xx_visualization_darker(base_rgb, value);
}

size_t xx_visualization_block_at(const xx_visualization *vis, uint32_t x, uint32_t y)
{
    size_t index;
    if (!vis || x >= vis->width || y >= vis->height) return SIZE_MAX;
    index = (size_t)y * (size_t)vis->width + (size_t)x;
    if (index >= vis->block_count) return SIZE_MAX;
    return index;
}

bool xx_visualization_render(const xx_visualization *vis, xx_visualization_method method, uint32_t base_rgb, uint32_t block_px, size_t marked, uint8_t *rgba)
{
    size_t row_pixels;
    uint32_t x, y;
    if (!vis || !rgba || block_px == 0 || !vis->blocks) return false;
    if (vis->width < 1 || vis->width > XX_VIS_GRID_MAX || vis->height < 1 || vis->height > XX_VIS_GRID_MAX) return false;
    if ((uint64_t)vis->width * vis->height != (uint64_t)vis->block_count) return false;
    if ((uint64_t)vis->width * block_px > (uint64_t)(SIZE_MAX / 4U)) return false;
    row_pixels = (size_t)vis->width * block_px;
    if ((uint64_t)vis->height * block_px > (uint64_t)(SIZE_MAX / 4U / row_pixels)) return false;

    for (y = 0; y < vis->height; y++) {
        for (x = 0; x < vis->width; x++) {
            size_t index = (size_t)y * vis->width + x;
            uint32_t color = (index == marked) ? 0xFF0000U : xx_visualization_block_color(vis, index, method, base_rgb);
            uint8_t r = (uint8_t)((color >> 16) & 0xFFU);
            uint8_t g = (uint8_t)((color >> 8) & 0xFFU);
            uint8_t b = (uint8_t)(color & 0xFFU);
            uint32_t py;
            for (py = 0; py < block_px; py++) {
                uint8_t *p = rgba + (((size_t)y * block_px + py) * row_pixels + (size_t)x * block_px) * 4U;
                uint32_t px;
                for (px = 0; px < block_px; px++) {
                    p[0] = r;
                    p[1] = g;
                    p[2] = b;
                    p[3] = 255;
                    p += 4;
                }
            }
        }
    }
    return true;
}
