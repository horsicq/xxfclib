/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/gbdev/pandocs/master/src/The_Cartridge_Header.md
 * GB/CGB cartridges with full48-byte boot logo, declared ROM-size/bank table, header checksum and global16-bit checksum. Supports normal size codes0-8
 * and52-54,32KiB-8MiB. Exports original16KiB banks; mapper emulation, copier wrappers, malformed/incomplete dumps and execution unsupported.
 */
#include "xxfclib/formats/nintendo_gb_rom/xx_nintendo_gb_rom.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

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
    static const uint8_t logo[] = {0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0c, 0x00, 0x0d,
                                   0x00, 0x08, 0x11, 0x1f, 0x88, 0x89, 0x00, 0x0e, 0xdc, 0xcc, 0x6e, 0xe6, 0xdd, 0xdd, 0xd9, 0x99,
                                   0xbb, 0xbb, 0x67, 0x63, 0x6e, 0x0e, 0xec, 0xcc, 0xdd, 0xdc, 0x99, 0x9f, 0xbb, 0xb9, 0x33, 0x3e};
    uint32_t banks, i;
    uint64_t end, at;
    uint8_t check = 0;
    uint32_t sum = 0;
    char label[32];
    if (n < 336 || xx_rt_memcmp(b + 0x104, logo, sizeof(logo)) || (b[0x148] > 8 && (b[0x148] < 0x52 || b[0x148] > 0x54)) || b[0x149] > 5 || b[0x14a] > 1) return false;
    switch (b[0x147]) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 5:
        case 6:
        case 8:
        case 9:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0f:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x19:
        case 0x1a:
        case 0x1b:
        case 0x1c:
        case 0x1d:
        case 0x1e:
        case 0x20:
        case 0x22:
        case 0xfc:
        case 0xfd:
        case 0xfe:
        case 0xff: break;
        default: return false;
    }
    banks = b[0x148] <= 8 ? 2U << b[0x148] : b[0x148] == 0x52 ? 72 : b[0x148] == 0x53 ? 80 : 96;
    end = (uint64_t)banks * 16384;
    if (end > n) return false;
    for (i = 0x134; i <= 0x14c; ++i) check = (uint8_t)(check - b[i] - 1);
    if (check != b[0x14d]) return false;
    for (at = 0; at < end; ++at) {
        if ((at & 65535) == 0 && stop(pd)) return false;
        if (at != 0x14e && at != 0x14f) sum += b[at];
    }
    if ((sum & 65535) != xx_data_get_u16(b + 0x14e, 2, 0, true)) return false;
    for (i = 0; i < banks; ++i) {
        xx_rt_snprintf(label, sizeof(label), "bank-%u.bin", i);
        if (!emit(f, s, label, (uint64_t)i * 16384, 16384, end)) return false;
    }
    s->size = (int64_t)end;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t available = pm_available(f);
    uint8_t *b, probe[32];
    bool result;
    if (available < 1 || available > 67108864 || stop(pd)) return false;
    if (available < 268 || !pm_read(f, 260, probe, 8) || xx_rt_memcmp(probe, "\xce\xed\x66\x66\xcc\x0d\x00\x0b", 8)) return false;
    b = (uint8_t *)xx_mem_alloc((size_t)available);
    if (!b) return false;
    result = pm_read(f, 0, b, (size_t)available) && parse_data(f, s, b, (uint64_t)available, pd);
    xx_mem_free(b);
    return result;
}

void xx_nintendo_gb_rom_init(xx_nintendo_gb_rom *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NINTENDO_GB_ROM, "gb");
    }
}
xx_nintendo_gb_rom *xx_nintendo_gb_rom_create(xx_io_device *d, int64_t b)
{
    xx_nintendo_gb_rom *r = (xx_nintendo_gb_rom *)xx_mem_alloc(sizeof(*r));
    if (r) xx_nintendo_gb_rom_init(r, d, b);
    return r;
}
void xx_nintendo_gb_rom_destroy(xx_nintendo_gb_rom *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_nintendo_gb_rom_free(xx_nintendo_gb_rom *r)
{
    if (r) {
        xx_nintendo_gb_rom_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_nintendo_gb_rom_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_nintendo_gb_rom_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
