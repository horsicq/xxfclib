/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT */
#include "xxfclib/formats/executable/upx_engine/xx_upx_engine.h"
#include "xxfclib/formats/archive/sevenzip_engine/xx_sevenzip_engine.h"
#include "xxfclib/formats/executable/pe/xx_pe.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
Abstractformat *xx_upx_create(xx_io_device *d, int64_t base)
{
#ifdef XFU_UPX_LIBRARY
    Abstractformat *format = xx_sevenzip_engine_create(d, base, "UPX");
    if (format) {
        format->file_type = XX_FILE_TYPE_UPX;
        xx_format_set_extension(format, "exe");
    }
    return format;
#else
    return xx_sevenzip_engine_create_helper(d, base, "UPX", "xfu_upx_helper.exe", XX_FILE_TYPE_UPX, "exe");
#endif
}
void xx_upx_free(Abstractformat *f)
{
    xx_sevenzip_engine_free(f);
}
static bool upx_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool upx_read_exact(xx_io_device *d, void *p, size_t n, xx_pd_struct *pd)
{
    size_t at = 0;
    while (at < n && !upx_stopped(pd)) {
        ssize_t got = xx_io_read(d, (unsigned char *)p + at, n - at);
        if (got <= 0 || (size_t)got > n - at) return false;
        at += (size_t)got;
    }
    return at == n;
}
static bool upx_scan(xx_io_device *d, int64_t start, int64_t count, unsigned required_format, xx_pd_struct *pd)
{
    unsigned char buf[4103];
    size_t carried = 0;
    if (xx_io_seek64(d, start, SEEK_SET) != 0) return false;
    while (count > 0 && !upx_stopped(pd)) {
        size_t n = count > 4096 ? 4096 : (size_t)count, i;
        if (!upx_read_exact(d, buf + carried, n, pd)) return false;
        for (i = 0; i + 8 <= carried + n; ++i)
            if (!xx_rt_memcmp(buf + i, "UPX!", 4) && buf[i + 4] > 0 && buf[i + 4] < 255 && buf[i + 5] > 0 && (!required_format || buf[i + 5] == required_format) &&
                ((buf[i + 6] >= 2 && buf[i + 6] <= 10) || buf[i + 6] == 14) && (buf[i + 7] & 15) >= 1 && (buf[i + 7] & 15) <= 10)
                return true;
        {
            size_t total = carried + n;
            carried = total < 7 ? total : 7;
            xx_rt_memmove(buf, buf + total - carried, carried);
        }
        count -= n;
    }
    return false;
}
/* A PE can contain another packed program in its resources or overlay. Its
 * pack header must belong to the root headers or entry-point section, not
 * an unrelated embedded image. Keep the existing bounded first/last windows
 * within each of those ranges; packed section names need not be UPX0/UPX1. */
static bool upx_scan_range(xx_io_device *d, int64_t start, int64_t count, xx_pd_struct *pd)
{
    int64_t window = count < 1024 * 1024 ? count : 1024 * 1024;
    return upx_scan(d, start, window, 0, pd) ||
           (count > window && upx_scan(d, start + count - window, window, 0, pd));
}
static bool upx_pe_marker(xx_io_device *d, xx_pd_struct *pd)
{
    xx_pe *pe = xx_pe_create(d, 0);
    bool found = false;
    uint16_t i;
    if (!pe || !xx_pe_handle_base_info(&pe->format, pd)) goto done;
    found = upx_scan_range(d, 0, pe->size_of_headers, pd);
    for (i = 0; !found && i < pe->number_of_sections && !upx_stopped(pd); ++i) {
        const xx_pe_section *section = &pe->sections[i];
        uint32_t ep = pe->entry_point_rva;
        if (ep >= section->virtual_address && ep - section->virtual_address < section->raw_size &&
            (section->characteristics & UINT32_C(0x20000000)))
            found = upx_scan_range(d, section->raw_offset, section->raw_size, pd);
    }
done:
    xx_pe_free(pe);
    return found;
}
bool xx_upx_has_marker_device(xx_io_device *d, xx_pd_struct *pd)
{
    int64_t saved = xx_io_tell(d), size = xx_io_total_size(d), window;
    unsigned char h[4];
    unsigned legacy = 0;
    bool found = false;
    if (saved < 0 || size < 4 || upx_stopped(pd) || xx_io_seek64(d, 0, SEEK_SET) != 0 || !upx_read_exact(d, h, 4, pd)) goto done;
    /* COMMAIN1 starts CMP SP,imm16; SYSMAIN1 starts the DOS device sentinel.
     * PackCom::canUnpack reads its type1/type2 header within the first128 bytes.
     * Requiring those loader prefixes prevents archives/text containing a
     * child's pack header from being classified as a legacy executable. */
    if (size <= 0xff00 && h[0] == 0x81 && h[1] == 0xfc) legacy = 1;
    else if (size <= 0x10000 && h[0] == 0xff && h[1] == 0xff && h[2] == 0xff && h[3] == 0xff) legacy = 2;
    if (legacy) {
        found = upx_scan(d, 0, size < 128 ? size : 128, legacy, pd);
        goto done;
    }
    if (h[0] == 'M' && h[1] == 'Z' && size >= 64) {
        uint8_t pointer[4], signature[4];
        if (xx_io_read_at(d, 0x3c, pointer, sizeof(pointer))) {
            uint32_t nt = (uint32_t)pointer[0] | ((uint32_t)pointer[1] << 8) |
                          ((uint32_t)pointer[2] << 16) | ((uint32_t)pointer[3] << 24);
            if (nt >= 64U && xx_io_read_at(d, nt, signature, sizeof(signature)) && !xx_rt_memcmp(signature, "PE\0\0", 4U)) {
                found = upx_pe_marker(d, pd);
                goto done;
            }
        }
    }
    /* Official PE/DOS, ELF, Mach-O and Atari carriers. */
    if (!(h[0] == 'M' && h[1] == 'Z') && xx_rt_memcmp(h, "\177ELF", 4) && xx_rt_memcmp(h, "\xcf\xfa\xed\xfe", 4) && xx_rt_memcmp(h, "\xce\xfa\xed\xfe", 4) &&
        xx_rt_memcmp(h, "\xfe\xed\xfa\xcf", 4) && xx_rt_memcmp(h, "\xfe\xed\xfa\xce", 4) && xx_rt_memcmp(h, "\xca\xfe\xba\xbe", 4) && !(h[0] == 0x60 && h[1] == 0x1a))
        goto done;
    window = size < 1024 * 1024 ? size : 1024 * 1024;
    found = upx_scan(d, 0, window, 0, pd);
    if (!found && size > window) found = upx_scan(d, size - window, window, 0, pd);
done:
    if (saved >= 0) (void)xx_io_seek64(d, saved, SEEK_SET);
    return found && !upx_stopped(pd);
}
xx_file_type_t xx_upx_detect_device(xx_io_device *d, xx_pd_struct *pd)
{
    int64_t saved = xx_io_tell(d);
    xx_file_type_t result = XX_FILE_TYPE_UNKNOWN;
    Abstractformat *f = NULL;
    if (!xx_upx_has_marker_device(d, pd)) goto done;
    f = xx_upx_create(d, 0);
    if (f && xx_format_is_valid(f, pd) && xx_format_handle_base_info(f, pd)) result = XX_FILE_TYPE_UPX;
done:
    xx_upx_free(f);
    if (saved >= 0) (void)xx_io_seek64(d, saved, SEEK_SET);
    return result;
}
