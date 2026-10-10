/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Universal Mach-O FAT/FAT64. Table grammar and range checks follow
 * Formats/exec/xmachofat.cpp and Apple's mach-o/fat.h. Slice bytes may be
 * Mach-O objects, archives or other toolchain payloads; they are preserved.
 */
#include "xxfclib/formats/machofat/xx_machofat.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#define XX_MACHOFAT_MAX_ARCHITECTURES 4096U

static bool machofat_magic(const uint8_t *h, bool *wide, bool *big)
{
    uint32_t magic = xx_data_get_u32(h, 4, 0, true);
    if (magic == UINT32_C(0xcafebabe) || magic == UINT32_C(0xcafebabf)) {
        *big = true;
        *wide = magic == UINT32_C(0xcafebabf);
        return true;
    }
    if (magic == UINT32_C(0xbebafeca) || magic == UINT32_C(0xbfbafeca)) {
        *big = false;
        *wide = magic == UINT32_C(0xbfbafeca);
        return true;
    }
    return false;
}

static const char *machofat_arch(uint32_t cpu)
{
    switch (cpu) {
        case 7U: return "i386";
        case UINT32_C(0x01000007): return "x86_64";
        case 12U: return "arm";
        case UINT32_C(0x0100000c): return "arm64";
        case UINT32_C(0x0200000c): return "arm64_32";
        case 18U: return "ppc";
        case UINT32_C(0x01000012): return "ppc64";
        case 6U: return "m68k";
        case 14U: return "sparc";
        default: return "cpu";
    }
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t header[8], entry[32];
    bool wide, big;
    uint32_t count, i, stride;
    uint64_t table_end, end;
    int64_t available = pm_available(f);
    if (available < 8 || !pm_read(f, 0, header, sizeof(header)) || !machofat_magic(header, &wide, &big)) return false;
    count = xx_data_get_u32(header + 4, 4, 0, big);
    stride = wide ? 32U : 20U;
    table_end = 8U + (uint64_t)count * stride;
    if (!count || count > XX_MACHOFAT_MAX_ARCHITECTURES || table_end > (uint64_t)available) return false;
    end = table_end;
    for (i = 0; i < count; ++i) {
        uint32_t cpu, subtype, align;
        uint64_t offset, size, mask;
        char name[80];
        if (xx_pd_is_stopped(pd) || !pm_read(f, 8 + (int64_t)i * stride, entry, stride)) return false;
        cpu = xx_data_get_u32(entry, 4, 0, big);
        subtype = xx_data_get_u32(entry + 4, 4, 0, big);
        offset = wide ? xx_data_get_u64(entry + 8, 8, 0, big) : xx_data_get_u32(entry + 8, 4, 0, big);
        size = wide ? xx_data_get_u64(entry + 16, 8, 0, big) : xx_data_get_u32(entry + 12, 4, 0, big);
        align = xx_data_get_u32(entry + (wide ? 24 : 16), 4, 0, big);
        if (!cpu || !size || align > 63U || (wide && xx_data_get_u32(entry + 28, 4, 0, big))) return false;
        mask = align ? (UINT64_C(1) << align) - 1U : 0U;
        if (offset < table_end || (offset & mask) || offset > (uint64_t)available || size > (uint64_t)available - offset) {
            return false;
        }
        xx_rt_snprintf(name, sizeof(name), "%s-%08x-%08x.bin", machofat_arch(cpu), (unsigned)cpu, (unsigned)subtype);
        if (!pm_add(f, s, name, (int64_t)offset, (int64_t)size)) return false;
        if (offset + size > end) end = offset + size;
    }
    f->endian = big ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    s->size = (int64_t)end;
    return !xx_pd_is_stopped(pd);
}

void xx_machofat_init(xx_machofat *reader, xx_io_device *device, int64_t base)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base, XX_FILE_TYPE_MACHOFAT, "fat");
    reader->format.check_is_valid = xx_machofat_check_is_valid;
    reader->format.handle_base_info = xx_machofat_handle_base_info;
    reader->format.os = XX_OS_MACOS;
    xx_format_set_mime_type(&reader->format, "application/x-mach-binary");
}
xx_machofat *xx_machofat_create(xx_io_device *device, int64_t base)
{
    xx_machofat *reader = (xx_machofat *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_machofat_init(reader, device, base);
    return reader;
}
void xx_machofat_destroy(xx_machofat *reader)
{
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}
void xx_machofat_free(xx_machofat *reader)
{
    if (reader) {
        xx_machofat_destroy(reader);
        xx_mem_free(reader);
    }
}
bool xx_machofat_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    int64_t saved;
    bool valid;
    if (!f || !f->device || (saved = xx_io_tell(f->device)) < 0) return false;
    valid = pm_valid(f, pd);
    return xx_io_seek64(f->device, saved, SEEK_SET) == 0 && valid;
}
bool xx_machofat_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    int64_t saved;
    bool valid;
    if (!f || !f->device || (saved = xx_io_tell(f->device)) < 0) return false;
    valid = pm_handle(f, pd);
    if (xx_io_seek64(f->device, saved, SEEK_SET) != 0) {
        f->is_valid = false;
        return false;
    }
    return valid;
}
xx_file_type_t xx_machofat_detect(xx_io_device *device, int64_t base)
{
    uint8_t magic[4];
    bool wide, big;
    xx_machofat reader;
    bool valid;
    if (!xx_io_read_at(device, base, magic, sizeof(magic)) || !machofat_magic(magic, &wide, &big)) return XX_FILE_TYPE_UNKNOWN;
    xx_machofat_init(&reader, device, base);
    valid = xx_machofat_check_is_valid(&reader.format, NULL);
    xx_machofat_destroy(&reader);
    return valid ? XX_FILE_TYPE_MACHOFAT : XX_FILE_TYPE_UNKNOWN;
}
