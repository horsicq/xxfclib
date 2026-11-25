/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/devkitPro/gba-tools/master/src/gbafix.c
 * Standard GBA ROM headers with complete boot-logo CRC32, fixed byte 0x96,
 * reserved fields, header complement and bounded ARM branch entry. Image sizes
 * are bounded to 256 bytes-32 MiB; trimmed/non-power-of-two dumps are valid.
 * Exports header and original body; multiboot/debug headers, copier wrappers
 * and emulation are unsupported.
 */
#include "xxfclib/formats/game/nintendo_gba_rom/xx_nintendo_gba_rom.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#define XX_GBA_ROM_ADDRESS UINT64_C(0x08000000)
#define XX_GBA_ROM_LIMIT UINT64_C(0x0a000000)
#define XX_GBA_ROM_CAPACITY (XX_GBA_ROM_LIMIT - XX_GBA_ROM_ADDRESS)

static __inline bool span(uint64_t a, uint64_t n, uint64_t e)
{
    return a <= e && n <= e - a;
}
static __inline bool stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static __inline bool zero(const uint8_t *b, uint64_t n)
{
    uint64_t i;
    for (i = 0; i < n; ++i)
        if (b[i]) return false;
    return true;
}
static __inline bool finite32(const uint8_t *p, bool be)
{
    return (xx_data_get_u32(p, 4, 0, be) & 0x7f800000U) != 0x7f800000U;
}
static __inline bool finite64(const uint8_t *p, bool be)
{
    return (xx_data_get_u64(p, 8, 0, be) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}
static __inline bool floats(const uint8_t *b, uint64_t at, uint64_t count, bool be, uint64_t n)
{
    uint64_t i;
    if (!span(at, count * 4, n)) return false;
    for (i = 0; i < count; ++i)
        if (!finite32(b + at + i * 4, be)) return false;
    return true;
}
static __inline bool emit(Abstractformat *f, pm_stream *s, const char *label, uint64_t a, uint64_t n, uint64_t e)
{
    return span(a, n, e) && s->count < 4096 && pm_add(f, s, label, (int64_t)a, (int64_t)n);
}
static __inline bool cstr(const uint8_t *b, uint64_t *at, uint64_t end, uint64_t maximum, bool empty)
{
    uint64_t start = *at;
    while (*at < end && *at - start <= maximum) {
        uint8_t c = b[(*at)++];
        if (!c) return empty || *at > start + 1;
        if (c < 32 || c == 127) return false;
    }
    return false;
}
typedef struct range {
    uint64_t at, n;
} range;
static __inline bool reserve(range *r, unsigned *nr, unsigned max, uint64_t at, uint64_t n, uint64_t lo, uint64_t end)
{
    unsigned i;
    if (*nr >= max || at < lo || !span(at, n, end)) return false;
    for (i = 0; i < *nr; ++i)
        if (n && r[i].n && at < r[i].at + r[i].n && r[i].at < at + n) return false;
    r[*nr].at = at;
    r[*nr].n = n;
    ++*nr;
    return true;
}
static bool parse_data(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint32_t branch, i;
    int64_t target, displacement;
    uint8_t check = 0;
    if (n < 256 || n > 33554432 || b[0xb2] != 0x96 || b[0xb3] || (b[0xb4] & 0x7fU) || !zero(b + 0xb5, 7) || !zero(b + 0xbe, 2) ||
        xx_crc32_calc(0U, b + 4, 156) != 0xd0beb55eU)
        return false;
    branch = xx_data_get_u32(b, 4, 0, false);
    if ((branch >> 24) != 0xea) return false;
    displacement = branch & 0xffffffU;
    if (displacement & 0x800000) displacement -= 0x1000000;
    target = 8 + displacement * 4;
    if (target < 192 || (uint64_t)target >= n) return false;
    for (i = 0xa0; i < 0xbd; ++i) {
        check = (uint8_t)(check + b[i]);
    }
    check = (uint8_t)(0U - check - 0x19U);
    if (check != b[0xbd] || stop(pd)) return false;
    if (!emit(f, s, "header.bin", 0, 192, n) || !emit(f, s, "rom-body.bin", 192, n - 192, n)) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t available = pm_available(f);
    uint8_t *b, probe[32];
    bool result;
    if (available < 1 || available > 67108864 || stop(pd)) return false;
    if (available < 12 || !pm_read(f, 4, probe, 8) || xx_rt_memcmp(probe, "\x24\xff\xae\x51\x69\x9a\xa2\x21", 8)) return false;
    b = (uint8_t *)xx_mem_alloc((size_t)available);
    if (!b) return false;
    result = pm_read(f, 0, b, (size_t)available) && parse_data(f, s, b, (uint64_t)available, pd);
    xx_mem_free(b);
    return result;
}

void xx_nintendo_gba_rom_init(xx_nintendo_gba_rom *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NINTENDO_GBA_ROM, "gba");
        r->format.format_type = XX_TYPE_ROM;
        r->format.endian = XX_ENDIAN_LITTLE;
        r->format.arch = XX_ARCH_ARM;
        r->format.is_archive = false;
        r->format.is_executable = true;
        r->format.get_memory_map = xx_nintendo_gba_rom_get_memory_map;
    }
}
xx_nintendo_gba_rom *xx_nintendo_gba_rom_create(xx_io_device *d, int64_t b)
{
    xx_nintendo_gba_rom *r = (xx_nintendo_gba_rom *)xx_mem_alloc(sizeof(*r));
    if (r) xx_nintendo_gba_rom_init(r, d, b);
    return r;
}
void xx_nintendo_gba_rom_destroy(xx_nintendo_gba_rom *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_nintendo_gba_rom_free(xx_nintendo_gba_rom *r)
{
    if (r) {
        xx_nintendo_gba_rom_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_nintendo_gba_rom_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_nintendo_gba_rom_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

bool xx_nintendo_gba_rom_get_memory_map(Abstractformat *f, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    int64_t total;
    int64_t size;

    if (!f || !output || !f->device || !f->base_info_handled || f->base_address < 0 || stop(pd)) return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) mode = XX_MEMORY_MAP_MODE_MAPS;
    if (mode != XX_MEMORY_MAP_MODE_MAPS) return false;

    total = xx_io_total_size(f->device);
    size = f->format_size;
    if (total < f->base_address || size <= 0 || (uint64_t)size > XX_GBA_ROM_CAPACITY || size > total - f->base_address) return false;

    output->binary_offset = f->base_address;
    output->module_address = XX_GBA_ROM_ADDRESS;
    output->is_image = f->is_mapped;
    output->binary_size = size;
    output->entry_point_address = XX_GBA_ROM_ADDRESS;
    output->code_base = (int64_t)XX_GBA_ROM_ADDRESS;
    output->start_load_offset = f->base_address;
    output->file_type = f->file_type;
    output->format_type = f->format_type;
    output->endian = f->endian;
    output->arch = f->arch;
    output->mode = mode;

    /* The cartridge window is a 32 MiB executable/read-only ROM bank.  The
     * shared map API represents its permissions at format level; the second
     * record carries the explicit virtual-only marker. */
    return xx_memory_map_add_part(output, f->base_address, size, XX_GBA_ROM_ADDRESS, (int64_t)XX_GBA_ROM_CAPACITY, XX_FILE_PART_REGION, 0, "ROM", false) && !stop(pd) &&
           xx_memory_map_finalize(output);
}
