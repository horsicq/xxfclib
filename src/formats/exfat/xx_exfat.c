/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native reader written from Microsoft's exFAT specification;
 * no code from another filesystem implementation is incorporated.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/exfat/xx_exfat.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>

#ifdef EXFAT
#define EXFAT_TYPE XX_FILE_TYPE_EXFAT
#else
#define EXFAT_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define EXFAT_MAX_CLUSTERS UINT32_C(0x08000000)
#define EXFAT_MAX_STEPS UINT32_C(0x01000000)
#define EXFAT_MAX_SLOTS UINT32_C(0x00400000)
#define EXFAT_MAX_RUNS UINT32_C(0x00100000)
#define EXFAT_MAX_MEMBERS 100000U
#define EXFAT_MAX_DEPTH 64U
#define EXFAT_MAX_PATH 4096U
#define EXFAT_MAX_DIRECTORY UINT64_C(0x10000000)
#define EXFAT_COPY_CHUNK 65536U

typedef struct exfat_geo_s {
    int64_t base;
    uint64_t size;
    uint64_t fat;
    uint64_t heap;
    uint32_t sector;
    uint32_t cluster;
    uint32_t clusters;
    uint32_t root;
    uint32_t serial;
    uint8_t fats;
    uint8_t active;
    bool backup;
} exfat_geo;
typedef struct exfat_run_s {
    uint64_t logical; /* Cluster index within the stream. */
    uint32_t first;
    uint32_t count;
} exfat_run;
typedef struct exfat_data_s {
    exfat_run *runs;
    size_t count;
    size_t capacity;
    uint64_t size;
    uint64_t valid;
} exfat_data;
typedef struct exfat_member_s {
    char *name;
    char *key;
    exfat_data data;
    int64_t header;
    uint32_t header_size;
    uint16_t attributes;
    bool folder;
} exfat_member;
typedef struct exfat_parsed_s {
    exfat_geo geo;
    exfat_member *members;
    size_t count;
    size_t capacity;
    uint32_t *names;
    size_t name_capacity;
    uint8_t *allocated;
    uint8_t *claimed;
    size_t bitmap_size;
    uint16_t *upcase;
    uint32_t steps;
    uint32_t slots;
    uint32_t runs;
    size_t refs;
    uint64_t retained_memory;
} exfat_parsed;
typedef struct exfat_cursor_s {
    exfat_parsed *parsed;
    size_t index;
} exfat_cursor;

static uint16_t exfat_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t exfat_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t exfat_u64(const uint8_t *p) {
    return exfat_u32(p) | ((uint64_t)exfat_u32(p + 4) << 32);
}
static uint32_t exfat_sum32(uint32_t sum, uint8_t value) {
    return (sum >> 1) + (sum << 31) + value;
}
static uint16_t exfat_sum16(uint16_t sum, uint8_t value) {
    return (uint16_t)((sum >> 1) + ((uint32_t)sum << 15) + value);
}

/* Seek/read/restore also works for devices returning short successful reads. */
static bool exfat_read(xx_io_device *device, int64_t offset, void *buffer,
                       size_t size, xx_pd_struct *pd) {
    int64_t saved;
    size_t done = 0U;
    bool ok = false;
    if (!device || (!buffer && size) || offset < 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    saved = xx_io_tell(device);
    if (saved < 0) return false;
    if (xx_io_seek64(device, offset, SEEK_SET) == 0) {
        while (done < size && !(pd && xx_pd_is_stopped(pd))) {
            ssize_t got = xx_io_read(device, (uint8_t *)buffer + done,
                                      size - done);
            if (got <= 0 || (size_t)got > size - done) break;
            done += (size_t)got;
        }
        ok = done == size;
    }
    if (xx_io_seek64(device, saved, SEEK_SET) != 0) ok = false;
    return ok && !(pd && xx_pd_is_stopped(pd));
}
static bool exfat_rel_read(xx_io_device *device, const exfat_geo *geo,
                           uint64_t offset, void *buffer, size_t size,
                           xx_pd_struct *pd) {
    if (offset > geo->size || (uint64_t)size > geo->size - offset)
        return false;
    return exfat_read(device, geo->base + (int64_t)offset, buffer, size, pd);
}

static bool exfat_boot(Abstractformat *self, uint64_t location,
                       uint32_t expected_sector, exfat_geo *geo,
                       xx_pd_struct *pd) {
    uint8_t first[512];
    uint8_t *region = NULL;
    uint64_t available, sectors, fat_offset, fat_length, heap, heap_end;
    uint64_t size;
    uint32_t sector, cluster, clusters, root, sum = 0U;
    uint16_t flags;
    size_t i;
    int64_t total;
    bool ok = false;
    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    available = (uint64_t)(total - self->base_address);
    if (location > available || available - location < sizeof(first) ||
        !exfat_read(self->device, self->base_address + (int64_t)location,
                     first, sizeof(first), pd)) return false;
    if (first[0] != 0xEBU || first[1] != 0x76U || first[2] != 0x90U ||
        xx_rt_memcmp(first + 3, "EXFAT   ", 8U) != 0 ||
        first[510] != 0x55U || first[511] != 0xAAU ||
        first[108] < 9U || first[108] > 12U ||
        first[109] > 25U - first[108] ||
        exfat_u16(first + 104) != 0x0100U) return false;
    for (i = 11U; i < 64U; ++i) if (first[i]) return false;
    sector = UINT32_C(1) << first[108];
    if (expected_sector && sector != expected_sector) return false;
    cluster = sector << first[109];
    sectors = exfat_u64(first + 72);
    fat_offset = exfat_u32(first + 80);
    fat_length = exfat_u32(first + 84);
    heap = exfat_u32(first + 88);
    clusters = exfat_u32(first + 92);
    root = exfat_u32(first + 96);
    flags = exfat_u16(first + 106);
    if ((first[110] != 1U && first[110] != 2U) ||
        ((flags & 1U) && first[110] == 1U) ||
        (first[112] > 100U && first[112] != 0xFFU) ||
        !clusters || clusters > EXFAT_MAX_CLUSTERS ||
        root < 2U || root - 2U >= clusters || fat_offset < 24U ||
        fat_length < (((uint64_t)clusters + 2U) * 4U + sector - 1U) / sector ||
        heap < fat_offset + fat_length * first[110] ||
        sectors < UINT64_C(1048576) / sector ||
        sectors > available / sector) return false;
    heap_end = heap + ((uint64_t)clusters << first[109]);
    if (heap_end > sectors ||
        ((sectors - heap) >> first[109]) != clusters) return false;
    /* The backup flags are stale. There is no safe active-pair guess. */
    if (location && first[110] != 1U) return false;
    size = sectors * sector;
    if (location > size || (uint64_t)sector * 12U > size - location)
        return false;
    region = (uint8_t *)xx_mem_alloc((size_t)sector * 12U);
    if (!region || !exfat_read(self->device,
            self->base_address + (int64_t)location, region,
            (size_t)sector * 12U, pd)) goto done;
    for (i = 0U; i < (size_t)sector * 11U; ++i) {
        if (i != 106U && i != 107U && i != 112U)
            sum = exfat_sum32(sum, region[i]);
    }
    for (i = (size_t)sector * 11U; i < (size_t)sector * 12U; i += 4U)
        if (exfat_u32(region + i) != sum) goto done;
    for (i = 1U; i <= 8U; ++i)
        if (exfat_u32(region + (i + 1U) * sector - 4U) !=
            UINT32_C(0xAA550000)) goto done;
    xx_mem_zero(geo, sizeof(*geo));
    geo->base = self->base_address;
    geo->size = size;
    geo->sector = sector;
    geo->cluster = cluster;
    geo->clusters = clusters;
    geo->root = root;
    geo->serial = exfat_u32(first + 100);
    geo->fats = first[110];
    geo->active = (uint8_t)(flags & 1U);
    geo->backup = location != 0U;
    geo->fat = (fat_offset + fat_length * geo->active) * sector;
    geo->heap = heap * sector;
    ok = !(pd && xx_pd_is_stopped(pd));
done:
    xx_mem_free(region);
    return ok;
}
static bool exfat_geometry(Abstractformat *self, exfat_geo *geo,
                           xx_pd_struct *pd) {
    uint32_t shift;
    if (exfat_boot(self, 0U, 0U, geo, pd)) return true;
    for (shift = 9U; shift <= 12U; ++shift) {
        uint32_t sector = UINT32_C(1) << shift;
        if (exfat_boot(self, (uint64_t)sector * 12U, sector, geo, pd))
            return true;
    }
    return false;
}
static void exfat_data_free(exfat_data *data) {
    xx_mem_free(data->runs);
    xx_mem_zero(data, sizeof(*data));
}
static void exfat_release(exfat_parsed *parsed) {
    size_t i;
    if (!parsed || --parsed->refs) return;
    for (i = 0U; i < parsed->count; ++i) {
        xx_mem_free(parsed->members[i].name);
        xx_mem_free(parsed->members[i].key);
        exfat_data_free(&parsed->members[i].data);
    }
    xx_mem_free(parsed->members);
    xx_mem_free(parsed->names);
    xx_mem_free(parsed->allocated);
    xx_mem_free(parsed->claimed);
    xx_mem_free(parsed->upcase);
    xx_mem_free(parsed);
}
static bool exfat_fat_next(xx_io_device *device, const exfat_geo *geo,
                           uint32_t cluster, uint32_t *next,
                           xx_pd_struct *pd) {
    uint8_t raw[4];
    if (cluster < 2U || cluster - 2U >= geo->clusters ||
        !exfat_rel_read(device, geo, geo->fat + (uint64_t)cluster * 4U,
                         raw, sizeof(raw), pd)) return false;
    *next = exfat_u32(raw);
    return true;
}
static bool exfat_claim(exfat_parsed *parsed, uint32_t cluster,
                        xx_pd_struct *pd) {
    uint32_t bit;
    uint8_t mask;
    if (cluster < 2U || cluster - 2U >= parsed->geo.clusters ||
        !parsed->steps || (pd && xx_pd_is_stopped(pd))) return false;
    --parsed->steps;
    bit = cluster - 2U;
    mask = (uint8_t)(1U << (bit & 7U));
    if ((parsed->claimed[bit >> 3] & mask) ||
        (parsed->allocated && !(parsed->allocated[bit >> 3] & mask)))
        return false;
    parsed->claimed[bit >> 3] |= mask;
    return true;
}
static bool exfat_add_run(exfat_parsed *parsed, exfat_data *data,
                          uint32_t cluster, uint64_t logical) {
    exfat_run *last;
    if (data->count) {
        last = &data->runs[data->count - 1U];
        if ((uint64_t)last->first + last->count == cluster) {
            ++last->count;
            return true;
        }
    }
    if (!parsed->runs) return false;
    if (data->count == data->capacity) {
        size_t capacity = data->capacity ? data->capacity * 2U : 8U;
        exfat_run *grown;
        if (capacity > EXFAT_MAX_RUNS) capacity = EXFAT_MAX_RUNS;
        if (capacity <= data->capacity || capacity > SIZE_MAX / sizeof(*grown))
            return false;
        grown = (exfat_run *)xx_mem_realloc(data->runs,
                                             capacity * sizeof(*grown));
        if (!grown) return false;
        data->runs = grown;
        data->capacity = capacity;
    }
    --parsed->runs;
    last = &data->runs[data->count++];
    last->logical = logical;
    last->first = cluster;
    last->count = 1U;
    return true;
}
static bool exfat_make_data(xx_io_device *device, exfat_parsed *parsed,
                            uint32_t first, uint64_t size, uint64_t valid,
                            bool contiguous, bool root, exfat_data *data,
                            xx_pd_struct *pd) {
    uint64_t needed, logical = 0U;
    uint32_t cluster = first, next = 0U;
    xx_mem_zero(data, sizeof(*data));
    if (valid > size || size > (uint64_t)parsed->geo.clusters *
                                    parsed->geo.cluster) return false;
    if (!root && size == 0U) return first == 0U && !contiguous;
    if (first < 2U || first - 2U >= parsed->geo.clusters) return false;
    needed = root ? EXFAT_MAX_DIRECTORY / parsed->geo.cluster
                  : (size + parsed->geo.cluster - 1U) / parsed->geo.cluster;
    if (contiguous && needed > (uint64_t)parsed->geo.clusters + 2U - first)
        return false;
    while (logical < needed) {
        if (!exfat_claim(parsed, cluster, pd) ||
            !exfat_add_run(parsed, data, cluster, logical)) goto bad;
        ++logical;
        if (contiguous) {
            ++cluster;
        } else {
            if (!exfat_fat_next(device, &parsed->geo, cluster, &next, pd)) goto bad;
            if (next == UINT32_MAX) {
                if (!root && logical != needed) goto bad;
                if (root) size = valid = logical * parsed->geo.cluster;
                break;
            }
            if (!root && logical == needed) goto bad;
            cluster = next;
        }
    }
    if (root && (!logical || (logical == needed && next != UINT32_MAX)))
        goto bad;
    data->size = size;
    data->valid = valid;
    return true;
bad:
    exfat_data_free(data);
    return false;
}
static bool exfat_data_location(const exfat_geo *geo, const exfat_data *data,
                                uint64_t offset, uint64_t *physical,
                                uint64_t *available) {
    uint64_t logical = offset / geo->cluster;
    size_t low = 0U, high = data->count;
    const exfat_run *run;
    while (low < high) {
        size_t mid = low + (high - low) / 2U;
        if (data->runs[mid].logical <= logical) low = mid + 1U;
        else high = mid;
    }
    if (!low) return false;
    run = &data->runs[low - 1U];
    if (logical - run->logical >= run->count) return false;
    *physical = geo->heap + ((uint64_t)run->first - 2U) * geo->cluster +
                offset - run->logical * geo->cluster;
    *available = (run->logical + run->count) * geo->cluster - offset;
    return true;
}
static bool exfat_data_read(xx_io_device *device, const exfat_geo *geo,
                            const exfat_data *data, uint64_t offset,
                            void *buffer, size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (offset > data->size || (uint64_t)size > data->size - offset)
        return false;
    while (done < size) {
        uint64_t physical, available;
        size_t part = size - done;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !exfat_data_location(geo, data, offset + done,
                                  &physical, &available)) return false;
        if ((uint64_t)part > available) part = (size_t)available;
        if (!exfat_rel_read(device, geo, physical,
                             (uint8_t *)buffer + done, part, pd)) return false;
        done += part;
    }
    return !(pd && xx_pd_is_stopped(pd));
}
static bool exfat_slot(xx_io_device *device, exfat_parsed *parsed,
                       const exfat_data *data, uint64_t slot, uint8_t *raw,
                       xx_pd_struct *pd) {
    if (!parsed->slots) return false;
    --parsed->slots;
    return exfat_data_read(device, &parsed->geo, data, slot * 32U,
                            raw, 32U, pd);
}

/* Identify root-only system files before directory names can be trusted. */
static bool exfat_system(xx_io_device *device, exfat_parsed *parsed,
                         const exfat_data *root, xx_pd_struct *pd) {
    uint32_t bitmap_first[2] = {0U, 0U}, upcase_first = 0U, upcase_sum = 0U;
    uint64_t bitmap_length[2] = {0U, 0U}, upcase_length = 0U;
    uint64_t slot, slots = root->size / 32U;
    uint8_t raw[32], *table = NULL;
    exfat_data bitmap, upcase;
    bool ok = false;
    size_t i, position;
    uint32_t character, sum;
    xx_mem_zero(&bitmap, sizeof(bitmap));
    xx_mem_zero(&upcase, sizeof(upcase));
    for (slot = 0U; slot < slots; ++slot) {
        if (!exfat_slot(device, parsed, root, slot, raw, pd)) goto done;
        if (!raw[0]) break;
        if (raw[0] == 0x80U) goto done;
        if (!(raw[0] & 0x80U)) continue;
        if (raw[0] == 0x81U) {
            unsigned id = raw[1] & 1U;
            if (id >= parsed->geo.fats || bitmap_first[id]) goto done;
            bitmap_first[id] = exfat_u32(raw + 20);
            bitmap_length[id] = exfat_u64(raw + 24);
            if (!bitmap_first[id]) goto done;
        } else if (raw[0] == 0x82U) {
            if (upcase_first) goto done;
            upcase_sum = exfat_u32(raw + 4);
            upcase_first = exfat_u32(raw + 20);
            upcase_length = exfat_u64(raw + 24);
            if (!upcase_first) goto done;
        } else if (raw[0] == 0x83U) {
            if (raw[1] > 11U) goto done;
        } else if (!(raw[0] & 0x40U)) {
            if (raw[0] != 0x85U && !(raw[0] & 0x20U)) goto done;
            if ((uint64_t)raw[1] >= slots - slot) goto done;
            slot += raw[1];
        } else goto done; /* Secondary outside its entry set. */
    }
    for (i = 0U; i < parsed->geo.fats; ++i)
        if (!bitmap_first[i] || bitmap_length[i] < parsed->bitmap_size)
            goto done;
    if (!upcase_first || !upcase_length || (upcase_length & 1U) ||
        upcase_length > UINT64_C(131072)) goto done;
    if (!exfat_make_data(device, parsed, bitmap_first[parsed->geo.active],
          bitmap_length[parsed->geo.active], bitmap_length[parsed->geo.active],
          false, false, &bitmap, pd)) goto done;
    parsed->allocated = (uint8_t *)xx_mem_alloc(parsed->bitmap_size);
    if (!parsed->allocated ||
        !exfat_data_read(device, &parsed->geo, &bitmap, 0U,
                           parsed->allocated, parsed->bitmap_size, pd)) goto done;
    /* Root and bitmap were claimed before the bitmap could be loaded. */
    for (i = 0U; i < parsed->bitmap_size; ++i) {
        if (parsed->claimed[i] & (uint8_t)~parsed->allocated[i]) goto done;
        if ((i & 4095U) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
    }
    if (!exfat_make_data(device, parsed, upcase_first, upcase_length,
                          upcase_length, false, false, &upcase, pd)) goto done;
    table = (uint8_t *)xx_mem_alloc((size_t)upcase_length);
    parsed->upcase = (uint16_t *)xx_mem_alloc(65536U * sizeof(uint16_t));
    if (!table || !parsed->upcase || !exfat_data_read(device, &parsed->geo,
          &upcase, 0U, table, (size_t)upcase_length, pd)) goto done;
    sum = 0U;
    for (i = 0U; i < (size_t)upcase_length; ++i)
        sum = exfat_sum32(sum, table[i]);
    if (sum != upcase_sum) goto done;
    position = 0U;
    character = 0U;
    while (character < 65536U) {
        uint32_t value;
        if (position >= (size_t)upcase_length ||
            (pd && xx_pd_is_stopped(pd))) goto done;
        value = exfat_u16(table + position);
        position += 2U;
        if (value == 0xFFFFU && character < 65535U) {
            uint32_t count;
            if (position >= (size_t)upcase_length) goto done;
            count = exfat_u16(table + position);
            position += 2U;
            if (!count || count > 65536U - character) goto done;
            while (count--) {
                parsed->upcase[character] = (uint16_t)character;
                ++character;
            }
        } else parsed->upcase[character++] = (uint16_t)value;
    }
    if (position != (size_t)upcase_length) goto done;
    for (i = 0U; i < 128U; ++i) {
        uint16_t mandatory = (uint16_t)(i >= 'a' && i <= 'z' ? i - 32U : i);
        if (parsed->upcase[i] != mandatory) goto done;
    }
    ok = true;
done:
    xx_mem_free(table);
    exfat_data_free(&bitmap);
    exfat_data_free(&upcase);
    return ok;
}

static bool exfat_device_name(const char *name) {
    static const char *const names[] = {
        "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"
    };
    size_t stem = 0U, i, j;
    while (name[stem] && name[stem] != '.') ++stem;
    while (stem && name[stem - 1U] == ' ') --stem;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        for (j = 0U; j < stem; ++j) {
            unsigned char c = (unsigned char)name[j];
            if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32U);
            if (!names[i][j] || c != (unsigned char)names[i][j]) break;
        }
        if (j == stem && !names[i][j]) return true;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9') {
        char a = name[0], b = name[1], c = name[2];
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if ((a == 'C' && b == 'O' && c == 'M') ||
            (a == 'L' && b == 'P' && c == 'T')) return true;
    }
    return false;
}
static bool exfat_name(const uint16_t *name, size_t length,
                       const uint16_t *upcase, bool key, char *out) {
    size_t i, used = 0U;
    if (!length || (length == 1U && name[0] == '.') ||
        (length == 2U && name[0] == '.' && name[1] == '.')) return false;
    for (i = 0U; i < length; ++i) {
        uint32_t c = key ? upcase[name[i]] : name[i];
        if (c < 32U || c == '"' || c == '*' || c == '/' || c == ':' ||
            c == '<' || c == '>' || c == '?' || c == '\\' || c == '|')
            return false;
        if (c >= 0xD800U && c <= 0xDBFFU) {
            uint32_t low;
            if (++i >= length) return false;
            low = key ? upcase[name[i]] : name[i];
            if (low < 0xDC00U || low > 0xDFFFU) return false;
            c = 0x10000U + ((c - 0xD800U) << 10) + low - 0xDC00U;
        } else if (c >= 0xDC00U && c <= 0xDFFFU) return false;
        if (c < 0x80U) out[used++] = (char)(c == 0x7FU ? '_' : c);
        else if (c < 0x800U) {
            out[used++] = (char)(0xC0U | (c >> 6));
            out[used++] = (char)(0x80U | (c & 63U));
        } else if (c < 0x10000U) {
            out[used++] = (char)(0xE0U | (c >> 12));
            out[used++] = (char)(0x80U | ((c >> 6) & 63U));
            out[used++] = (char)(0x80U | (c & 63U));
        } else {
            out[used++] = (char)(0xF0U | (c >> 18));
            out[used++] = (char)(0x80U | ((c >> 12) & 63U));
            out[used++] = (char)(0x80U | ((c >> 6) & 63U));
            out[used++] = (char)(0x80U | (c & 63U));
        }
    }
    for (i = used; i && (out[i - 1U] == '.' || out[i - 1U] == ' '); --i)
        out[i - 1U] = '_';
    if (!used) return false;
    out[used] = 0;
    if (exfat_device_name(out)) {
        for (i = used + 1U; i > 0U; --i) out[i] = out[i - 1U];
        out[0] = '_';
    }
    return true;
}
static uint32_t exfat_hash(const char *key) {
    uint32_t hash = UINT32_C(2166136261);
    while (*key) hash = (hash ^ (uint8_t)*key++) * UINT32_C(16777619);
    return hash;
}
static bool exfat_names_grow(exfat_parsed *parsed) {
    uint32_t *slots;
    size_t capacity = parsed->name_capacity ? parsed->name_capacity * 2U : 64U;
    size_t i;
    if (capacity > SIZE_MAX / sizeof(*slots)) return false;
    slots = (uint32_t *)xx_mem_calloc(capacity, sizeof(*slots));
    if (!slots) return false;
    for (i = 0U; i < parsed->count; ++i) {
        size_t slot = exfat_hash(parsed->members[i].key) & (capacity - 1U);
        while (slots[slot]) slot = (slot + 1U) & (capacity - 1U);
        slots[slot] = (uint32_t)i + 1U;
    }
    xx_mem_free(parsed->names);
    parsed->names = slots;
    parsed->name_capacity = capacity;
    return true;
}
static bool exfat_name_slot(exfat_parsed *parsed, const char *key,
                            size_t *result) {
    size_t slot = exfat_hash(key) & (parsed->name_capacity - 1U);
    while (parsed->names[slot]) {
        if (xx_str_cmp(parsed->members[parsed->names[slot] - 1U].key, key) == 0)
            return false;
        slot = (slot + 1U) & (parsed->name_capacity - 1U);
    }
    *result = slot;
    return true;
}
static char *exfat_join(const char *prefix, const char *name,
                        const char *suffix) {
    size_t a = xx_str_len(prefix), b = xx_str_len(name), c = xx_str_len(suffix);
    size_t size = a + (a ? 1U : 0U) + b + c + 1U;
    char *path;
    if (size > EXFAT_MAX_PATH) return NULL;
    path = (char *)xx_mem_alloc(size);
    if (!path) return NULL;
    xx_mem_copy(path, prefix, a);
    if (a) path[a++] = '/';
    xx_mem_copy(path + a, name, b);
    xx_mem_copy(path + a + b, suffix, c);
    path[a + b + c] = 0;
    return path;
}
static bool exfat_add_member(exfat_parsed *parsed, exfat_member *member,
                             const char *prefix, const char *prefix_key,
                             const char *name, const char *key) {
    size_t slot;
    unsigned attempt = 0U;
    char suffix[32] = {0};
    if (parsed->count >= EXFAT_MAX_MEMBERS) return false;
    if ((parsed->count + 1U) * 2U >= parsed->name_capacity &&
        !exfat_names_grow(parsed)) return false;
    do {
        xx_mem_free(member->name);
        xx_mem_free(member->key);
        member->name = exfat_join(prefix, name, suffix);
        member->key = exfat_join(prefix_key, key, suffix);
        if (!member->name || !member->key) return false;
        if (exfat_name_slot(parsed, member->key, &slot)) break;
        if (++attempt > 10000U) return false;
        xx_rt_snprintf(suffix, sizeof(suffix), "~%u",
                       (unsigned)(parsed->count + attempt));
    } while (true);
    if (parsed->count == parsed->capacity) {
        size_t capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        exfat_member *grown;
        if (capacity > EXFAT_MAX_MEMBERS) capacity = EXFAT_MAX_MEMBERS;
        if (capacity > SIZE_MAX / sizeof(*grown)) return false;
        grown = (exfat_member *)xx_mem_realloc(parsed->members,
                                                capacity * sizeof(*grown));
        if (!grown) return false;
        parsed->members = grown;
        parsed->capacity = capacity;
    }
    parsed->members[parsed->count] = *member;
    parsed->names[slot] = (uint32_t)++parsed->count;
    return true;
}
static bool exfat_walk(xx_io_device *device, exfat_parsed *parsed,
                       const exfat_data *directory, const char *prefix,
                       const char *prefix_key, unsigned depth, bool root,
                       xx_pd_struct *pd) {
    uint64_t slot, slots = directory->size / 32U;
    uint8_t *set = NULL;
    bool ok = false;
    if (depth > EXFAT_MAX_DEPTH || directory->size > EXFAT_MAX_DIRECTORY ||
        (directory->size % parsed->geo.cluster)) return false;
    set = (uint8_t *)xx_mem_alloc(256U * 32U);
    if (!set) return false;
    for (slot = 0U; slot < slots; ++slot) {
        unsigned secondaries, names, j, k, length;
        uint16_t checksum = 0U, name_hash = 0U, wide[255];
        char name[1024], key[1024];
        uint32_t first;
        uint64_t size, valid, physical, available;
        exfat_member member;
        size_t member_index;
        if (!exfat_slot(device, parsed, directory, slot, set, pd)) goto done;
        if (!set[0]) { ok = true; goto done; }
        if (set[0] == 0x80U) goto done;
        if (!(set[0] & 0x80U)) continue;
        if (set[0] == 0x81U || set[0] == 0x82U || set[0] == 0x83U) {
            if (!root) goto done;
            continue;
        }
        if (set[0] & 0x40U) goto done;
        if (set[0] != 0x85U && !(set[0] & 0x20U)) goto done;
        secondaries = set[1];
        if ((uint64_t)secondaries >= slots - slot) goto done;
        for (j = 1U; j <= secondaries; ++j) {
            if (!exfat_slot(device, parsed, directory, slot + j,
                             set + j * 32U, pd) ||
                (set[j * 32U] & 0xC0U) != 0xC0U) goto done;
        }
        for (j = 0U; j < (secondaries + 1U) * 32U; ++j)
            if (j != 2U && j != 3U) checksum = exfat_sum16(checksum, set[j]);
        if (checksum != exfat_u16(set + 2)) goto done;
        if (set[0] != 0x85U) { slot += secondaries; continue; }
        if (secondaries < 2U || set[32] != 0xC0U ||
            !(set[33] & 1U) || !set[35]) goto done;
        length = set[35];
        names = (length + 14U) / 15U;
        /* Unknown file extensions are not interpreted as ordinary bytes. */
        if (secondaries != names + 1U) goto done;
        for (j = 0U, k = 0U; j < names; ++j) {
            unsigned n;
            const uint8_t *entry = set + (j + 2U) * 32U;
            if (entry[0] != 0xC1U || (entry[1] & 3U)) goto done;
            for (n = 0U; n < 15U && k < length; ++n, ++k)
                wide[k] = exfat_u16(entry + 2U + n * 2U);
        }
        for (j = 0U; j < length; ++j) {
            uint16_t up = parsed->upcase[wide[j]];
            name_hash = exfat_sum16(name_hash, (uint8_t)up);
            name_hash = exfat_sum16(name_hash, (uint8_t)(up >> 8));
        }
        if (name_hash != exfat_u16(set + 36) ||
            !exfat_name(wide, length, parsed->upcase, false, name) ||
            !exfat_name(wide, length, parsed->upcase, true, key)) goto done;
        xx_mem_zero(&member, sizeof(member));
        member.attributes = exfat_u16(set + 4);
        member.folder = (member.attributes & 0x10U) != 0U;
        first = exfat_u32(set + 52);
        valid = exfat_u64(set + 40);
        size = exfat_u64(set + 56);
        if (member.folder && (valid != size || size > EXFAT_MAX_DIRECTORY ||
                              size % parsed->geo.cluster)) goto done;
        if (!exfat_make_data(device, parsed, first, size, valid,
                (set[33] & 2U) != 0U, false, &member.data, pd)) goto done;
        if (!exfat_data_location(&parsed->geo, directory, slot * 32U,
                                  &physical, &available)) {
            exfat_data_free(&member.data);
            goto done;
        }
        member.header = parsed->geo.base + (int64_t)physical;
        member.header_size = (secondaries + 1U) * 32U;
        member_index = parsed->count;
        if (!exfat_add_member(parsed, &member, prefix, prefix_key, name, key)) {
            xx_mem_free(member.name);
            xx_mem_free(member.key);
            exfat_data_free(&member.data);
            goto done;
        }
        if (member.folder && size) {
            /* A recursive push may relocate members; data/path allocations
             * remain stable, and a local data descriptor avoids that array. */
            const char *child_name = parsed->members[member_index].name;
            const char *child_key = parsed->members[member_index].key;
            exfat_data child_data = parsed->members[member_index].data;
            if (!exfat_walk(device, parsed, &child_data, child_name, child_key,
                              depth + 1U, false, pd)) goto done;
        }
        slot += secondaries;
    }
    ok = true; /* A completely full directory need not have an end marker. */
done:
    xx_mem_free(set);
    return ok;
}
static bool exfat_memory_add(uint64_t *total, uint64_t bytes) {
    if (bytes > UINT64_MAX - *total) return false;
    *total += bytes;
    return true;
}
/* Retained metadata is shared by the volume and its iterators. Count its
 * allocation capacities once, including unused run/member/hash slots. */
static bool exfat_measure_memory(exfat_parsed *parsed, xx_pd_struct *pd) {
    uint64_t total = sizeof(*parsed);
    size_t i;
    if (!exfat_memory_add(&total, (uint64_t)parsed->capacity * sizeof(exfat_member)) ||
        !exfat_memory_add(&total, (uint64_t)parsed->name_capacity * sizeof(uint32_t)) ||
        !exfat_memory_add(&total, (uint64_t)parsed->bitmap_size * 2U) ||
        !exfat_memory_add(&total, UINT64_C(65536) * sizeof(uint16_t))) return false;
    for (i = 0U; i < parsed->count; ++i) {
        const exfat_member *member = &parsed->members[i];
        if ((pd && xx_pd_is_stopped(pd)) ||
            !exfat_memory_add(&total, (uint64_t)xx_str_len(member->name) + 1U) ||
            !exfat_memory_add(&total, (uint64_t)xx_str_len(member->key) + 1U) ||
            !exfat_memory_add(&total, (uint64_t)member->data.capacity * sizeof(exfat_run)))
            return false;
    }
    parsed->retained_memory = total;
    return !(pd && xx_pd_is_stopped(pd));
}
static exfat_parsed *exfat_parse(Abstractformat *self, xx_pd_struct *pd) {
    exfat_parsed *parsed;
    exfat_data root;
    uint8_t reserved[8];
    bool ok;
    parsed = (exfat_parsed *)xx_mem_calloc(1U, sizeof(*parsed));
    if (!parsed) return NULL;
    parsed->refs = 1U;
    xx_mem_zero(&root, sizeof(root));
    parsed->steps = EXFAT_MAX_STEPS;
    parsed->slots = EXFAT_MAX_SLOTS;
    parsed->runs = EXFAT_MAX_RUNS;
    if (!exfat_geometry(self, &parsed->geo, pd)) goto bad;
    parsed->bitmap_size = ((size_t)parsed->geo.clusters + 7U) / 8U;
    parsed->claimed = (uint8_t *)xx_mem_calloc(parsed->bitmap_size, 1U);
    if (!parsed->claimed || !exfat_rel_read(self->device, &parsed->geo,
          parsed->geo.fat, reserved, sizeof(reserved), pd) ||
        (exfat_u32(reserved) & UINT32_C(0xFFFFFF00)) !=
                                 UINT32_C(0xFFFFFF00)) goto bad;
    if (!exfat_make_data(self->device, parsed, parsed->geo.root, 0U, 0U,
                          false, true, &root, pd)) goto bad;
    ok = exfat_system(self->device, parsed, &root, pd) &&
         exfat_walk(self->device, parsed, &root, "", "", 0U, true, pd) &&
         !(pd && xx_pd_is_stopped(pd));
    exfat_data_free(&root);
    if (!ok || !exfat_measure_memory(parsed, pd)) goto bad;
    return parsed;
bad:
    exfat_data_free(&root);
    exfat_release(parsed);
    return NULL;
}

static void exfat_vtable_destroy(Abstractformat *self) {
    xx_exfat_destroy((xx_exfat *)self);
}
void xx_exfat_init(xx_exfat *volume, xx_io_device *device, int64_t base_address) {
    if (!volume) return;
    xx_mem_zero(volume, sizeof(*volume));
    xx_format_init(&volume->format, device, base_address);
    volume->format.endian = XX_ENDIAN_LITTLE;
    volume->format.file_type = EXFAT_TYPE;
    volume->format.format_type = XX_TYPE_ARCHIVE;
    volume->format.is_archive = true;
    xx_format_set_mime_type(&volume->format, "application/x-exfat-fs");
    xx_format_set_extension(&volume->format, "img");
    volume->format.check_is_valid = xx_exfat_check_is_valid;
    volume->format.handle_base_info = xx_exfat_handle_base_info;
    volume->format.get_format_size = xx_exfat_get_format_size;
    volume->format.get_number_of_archive_records =
        xx_exfat_get_number_of_archive_records;
    volume->format.create_archive_records_reading =
        xx_exfat_create_archive_records_reading;
    volume->format.get_current_archive_record =
        xx_exfat_get_current_archive_record;
    volume->format.archive_record_move_to_next =
        xx_exfat_archive_record_move_to_next;
    volume->format.unpack_current_archive_record =
        xx_exfat_unpack_current_archive_record;
    volume->format.free_archive_records_reading =
        xx_exfat_free_archive_records_reading;
    volume->format.destroy = exfat_vtable_destroy;
}
xx_exfat *xx_exfat_create(xx_io_device *device, int64_t base_address) {
    xx_exfat *volume = (xx_exfat *)xx_mem_alloc(sizeof(*volume));
    if (volume) xx_exfat_init(volume, device, base_address);
    return volume;
}
void xx_exfat_destroy(xx_exfat *volume) {
    if (!volume) return;
    exfat_release((exfat_parsed *)volume->internal);
    volume->internal = NULL;
    xx_format_cleanup_extra_parameters(&volume->format);
}
void xx_exfat_free(xx_exfat *volume) {
    if (!volume) return;
    xx_exfat_destroy(volume);
    xx_mem_free(volume);
}
bool xx_exfat_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    exfat_parsed *parsed = exfat_parse(self, pd);
    if (!parsed) return false;
    exfat_release(parsed);
    return true;
}
bool xx_exfat_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_exfat *volume = (xx_exfat *)self;
    exfat_parsed *parsed;
    int64_t total, end;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    if (self->base_info_handled && volume->internal) return self->is_valid;
    parsed = exfat_parse(self, pd);
    if (!parsed) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    exfat_release((exfat_parsed *)volume->internal);
    volume->internal = parsed;
    volume->number_of_records = parsed->count;
    volume->volume_size = parsed->geo.size;
    volume->bytes_per_sector = parsed->geo.sector;
    volume->bytes_per_cluster = parsed->geo.cluster;
    volume->cluster_count = parsed->geo.clusters;
    volume->root_cluster = parsed->geo.root;
    volume->volume_serial = parsed->geo.serial;
    volume->active_fat = parsed->geo.active;
    volume->used_backup_boot = parsed->geo.backup;
    self->number_of_archive_records = parsed->count;
    self->format_size = (int64_t)parsed->geo.size;
    total = xx_io_total_size(self->device);
    end = self->base_address + self->format_size;
    self->overlay_offset = total > end ? end : -1;
    self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}
int64_t xx_exfat_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    return self && xx_exfat_handle_base_info(self, pd) ? self->format_size : -1;
}
uint64_t xx_exfat_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    return self && xx_exfat_handle_base_info(self, pd)
               ? ((xx_exfat *)self)->number_of_records : 0U;
}
static bool exfat_set_record(xx_archive_record *record,
                             const exfat_geo *geo, const exfat_member *member) {
    uint64_t physical, available;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header;
    record->header_size = member->header_size;
    record->data_offset = -1;
    if (!member->folder && member->data.size &&
        exfat_data_location(geo, &member->data, 0U, &physical, &available))
        record->data_offset = geo->base + (int64_t)physical;
    record->compressed_size = member->folder ? 0 : (int64_t)member->data.size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                           member->folder ? 0U : member->data.size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                           member->folder ? 0U : member->data.size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                           member->attributes) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, member->folder);
}
static bool exfat_options(xx_list_s *destination, const xx_list_s *source) {
    size_t i;
    if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(source, i);
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
static void exfat_cursor_free(void *pointer) {
    exfat_cursor *cursor = (exfat_cursor *)pointer;
    if (!cursor) return;
    exfat_release(cursor->parsed);
    xx_mem_free(cursor);
}
xx_archive_record_state *xx_exfat_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    exfat_cursor *cursor;
    exfat_parsed *parsed;
    if (!self || !xx_exfat_handle_base_info(self, pd)) return NULL;
    parsed = (exfat_parsed *)((xx_exfat *)self)->internal;
    cursor = (exfat_cursor *)xx_mem_calloc(1U, sizeof(*cursor));
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!cursor || !state) {
        xx_mem_free(cursor);
        xx_mem_free(state);
        return NULL;
    }
    ++parsed->refs;
    cursor->parsed = parsed;
    xx_archive_record_state_init(state, self);
    state->internal_state = cursor;
    state->free_internal = exfat_cursor_free;
    state->total_records = (int64_t)parsed->count;
    if (!exfat_options(&state->options, options) ||
        (parsed->count && !exfat_set_record(&state->current_record,
                            &parsed->geo, &parsed->members[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = parsed->count != 0U;
    state->current_index = state->has_record ? 0 : -1;
    return state;
}
const xx_archive_record *xx_exfat_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}
bool xx_exfat_archive_record_move_to_next(Abstractformat *self,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    exfat_cursor *cursor;
    if (!self || !state || state->format != self || !state->has_record ||
        !(cursor = (exfat_cursor *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd))) return false;
    if (cursor->index + 1U >= cursor->parsed->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!exfat_set_record(&state->current_record, &cursor->parsed->geo,
                          &cursor->parsed->members[cursor->index + 1U])) {
        state->has_record = false;
        return false;
    }
    ++cursor->index;
    ++state->current_index;
    return true;
}
static bool exfat_extraction_limits(Abstractformat *self,
    const xx_archive_record_state *state, const exfat_cursor *cursor,
    size_t *chunk) {
    const exfat_member *member = &cursor->parsed->members[cursor->index];
    const xx_var *size_limit = xx_format_resolve_extra_parameter(self,
        &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    const xx_var *memory_limit = xx_format_resolve_extra_parameter(self,
        &state->options, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t size = member->folder ? 0U : member->data.size;
    uint64_t memory = cursor->parsed->retained_memory;
    *chunk = (size_t)(size < EXFAT_COPY_CHUNK ? size : EXFAT_COPY_CHUNK);
    return (!size_limit || size <= xx_var_get_u64(size_limit)) &&
        exfat_memory_add(&memory, sizeof(*cursor)) &&
        exfat_memory_add(&memory, *chunk) &&
        (!memory_limit || memory <= xx_var_get_u64(memory_limit));
}
bool xx_exfat_extract_record_to_device(Abstractformat *self,
    xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    exfat_cursor *cursor;
    const exfat_member *member;
    uint8_t *buffer;
    uint64_t offset = 0U;
    size_t chunk;
    bool ok = true;
    if (!self || !self->device || destination == self->device || !state ||
        state->format != self || !state->has_record ||
        !(cursor = (exfat_cursor *)state->internal_state) ||
        cursor->index >= cursor->parsed->count ||
        (pd && xx_pd_is_stopped(pd))) return false;
    member = &cursor->parsed->members[cursor->index];
    if (!exfat_extraction_limits(self, state, cursor, &chunk)) return false;
    if (member->folder || !member->data.size) return true;
    buffer = (uint8_t *)xx_mem_alloc(chunk);
    if (!buffer) return false;
    while (offset < member->data.size) {
        size_t part = (size_t)(member->data.size - offset < chunk
                                  ? member->data.size - offset : chunk);
        size_t initialized = offset < member->data.valid
             ? (size_t)(member->data.valid - offset < part
                            ? member->data.valid - offset : part) : 0U;
        size_t written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) ||
            (initialized && !exfat_data_read(self->device, &cursor->parsed->geo,
               &member->data, offset, buffer, initialized, pd))) { ok = false; break; }
        if (initialized < part) xx_mem_zero(buffer + initialized, part - initialized);
        while (destination && written < part && !(pd && xx_pd_is_stopped(pd))) {
            ssize_t got = xx_io_write(destination, buffer + written, part - written);
            if (got <= 0 || (size_t)got > part - written ||
                (pd && xx_pd_is_stopped(pd))) { ok = false; break; }
            written += (size_t)got;
        }
        if (!ok || (pd && xx_pd_is_stopped(pd))) { ok = false; break; }
        offset += part;
    }
    xx_mem_free(buffer);
    return ok && !(pd && xx_pd_is_stopped(pd));
}
static bool exfat_safe_path(const char *path) {
    const char *start = path, *p;
    if (!path || !*path || *path == '/') return false;
    for (p = path;; ++p) {
        unsigned char c = (unsigned char)*p;
        if (!c || c == '/') {
            size_t n = (size_t)(p - start);
            if (!n || (n == 1U && start[0] == '.') ||
                (n == 2U && start[0] == '.' && start[1] == '.') ||
                start[n - 1U] == '.' || start[n - 1U] == ' ') return false;
            if (!c) return true;
            start = p + 1;
        } else if (c < 32U || c == 127U || c == '\\' || c == ':' || c == '"' ||
                   c == '<' || c == '>' || c == '|' || c == '?' || c == '*') return false;
    }
}
static xx_io_device *exfat_stage(const char *destination, char **stage_path) {
    unsigned attempt;
    size_t i, parent = 0U;
    char *directory = xx_str_dup(destination);
    *stage_path = NULL;
    if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i)
        if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48];
        char *candidate;
        xx_io_device *output;
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_exfat.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix);
        if (!candidate) break;
        {
            const char *a = candidate, *b = destination;
            while (*a && *b) {
                unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
                if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32U);
                if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32U);
                if (ca != cb) break;
                ++a; ++b;
            }
            if (!*a && !*b) { xx_str_free(candidate); continue; }
        }
        output = xx_io_file_open(candidate, "wbx");
        if (output) { *stage_path = candidate; xx_str_free(directory); return output; }
        xx_str_free(candidate);
    }
    xx_str_free(directory);
    return NULL;
}
bool xx_exfat_unpack_current_archive_record(Abstractformat *self,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    exfat_cursor *cursor;
    const exfat_member *member;
    const xx_var *option, *overwrite_option;
    const char *base;
    char *owned = NULL, *path = NULL, *stage_path = NULL;
    size_t chunk;
    bool ok = false, overwrite;
    if (!self || !state || state->format != self || !state->has_record ||
        !(cursor = (exfat_cursor *)state->internal_state) ||
        cursor->index >= cursor->parsed->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &cursor->parsed->members[cursor->index];
    if (!exfat_extraction_limits(self, state, cursor, &chunk)) return false;
    option = xx_format_resolve_extra_parameter(self, &state->options,
                                                 XX_META_ID_OPT_UNPACK_PATH);
    overwrite_option = xx_format_resolve_extra_parameter(self, &state->options,
                                                           XX_META_ID_OPT_OVERWRITE);
    overwrite = overwrite_option && xx_var_get_bool(overwrite_option);
    if (!option) return xx_exfat_extract_record_to_device(self, state, NULL, pd);
    if (!exfat_safe_path(member->name)) return false;
    base = NULL;
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned;
    }
    if (!base) goto done;
    path = (*base && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
         ? xx_str_concat3(base, "/", member->name) : xx_str_concat(base, member->name);
    if (!path) goto done;
    if (member->folder) { ok = xx_store_create_dirs_a(path, true); goto done; }
    if (!overwrite && xx_io_file_exists_a(path)) goto done;
    if (!xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = exfat_stage(path, &stage_path);
        if (!output) goto done;
        ok = xx_exfat_extract_record_to_device(self, state, output, pd);
        if (xx_io_close(output) != 0) ok = false;
    }
    if (ok && !(pd && xx_pd_is_stopped(pd)))
        ok = xx_io_file_replace_a(stage_path, path, overwrite);
    else ok = false;
done:
    if (stage_path) {
        if (!ok) (void)xx_io_file_remove_a(stage_path);
        xx_str_free(stage_path);
    }
    xx_str_free(path);
    xx_str_free(owned);
    return ok;
}
void xx_exfat_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
